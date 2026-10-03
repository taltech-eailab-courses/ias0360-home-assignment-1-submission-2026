// IAS0360 HW1 - IMU feature extraction.
// Reads a recording off the SD card and runs the lab 1.2 algorithms on it,
// on the Pico. Stages are timed so the cost can be put against the 2 ms
// sample period at 500 Hz.

#include "hw1.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

#include "f_util.h"
#include "ff.h"
#include "hw_config.h"
#include "sd_card.h"

#define AXES 6

static FATFS fs_sd;
static imu_sample_t records[N_SAMPLES];

// one buffer per axis, in g and dps
static float axis[AXES][N_SAMPLES];
static const char *axis_name[AXES] = {"ax", "ay", "az", "gx", "gy", "gz"};

static float scratch[N_SAMPLES];
static float work_f[N_SAMPLES];
static int16_t q[N_SAMPLES];
static float deq[N_SAMPLES];
static float mag[N_SAMPLES / 2];
static float filt[N_SAMPLES];

static const int bit_depths[] = {16, 8, 4};
#define N_DEPTHS (int)(sizeof(bit_depths) / sizeof(bit_depths[0]))

static bool sd_begin(const char **drive_out) {
  if (!sd_init_driver()) {
    printf("sd_init_driver() FAILED\n");
    return false;
  }
  sd_card_t *sd = sd_get_by_num(0);
  if (!sd) {
    printf("sd_get_by_num(0) returned NULL\n");
    return false;
  }
  const char *drive = sd_get_drive_prefix(sd);
  FRESULT fr = f_mount(&fs_sd, drive, 1);
  if (fr != FR_OK) {
    printf("f_mount -> %s (%d)\n", FRESULT_str(fr), fr);
    return false;
  }
  *drive_out = drive;
  return true;
}

static int load_recording(const char *drive, const char *name) {
  char path[64];
  snprintf(path, sizeof path, "%s/%s.bin", drive, name);

  FIL f;
  FRESULT fr = f_open(&f, path, FA_READ);
  if (fr != FR_OK) {
    printf("f_open(%s) -> %s (%d)\n", path, FRESULT_str(fr), fr);
    return -1;
  }

  UINT br = 0;
  fr = f_read(&f, records, sizeof records, &br);
  f_close(&f);
  if (fr != FR_OK) {
    printf("f_read -> %s (%d)\n", FRESULT_str(fr), fr);
    return -1;
  }

  int n = (int)(br / sizeof(imu_sample_t));
  printf("loaded %s: %d samples (%u bytes)\n", path, n, (unsigned)br);
  return n;
}

// counts -> g and dps
static void to_physical(int n) {
  for (int i = 0; i < n; i++) {
    axis[0][i] = (float)records[i].ax / ACCEL_LSB_PER_G;
    axis[1][i] = (float)records[i].ay / ACCEL_LSB_PER_G;
    axis[2][i] = (float)records[i].az / ACCEL_LSB_PER_G;
    axis[3][i] = (float)records[i].gx / GYRO_LSB_PER_DPS;
    axis[4][i] = (float)records[i].gy / GYRO_LSB_PER_DPS;
    axis[5][i] = (float)records[i].gz / GYRO_LSB_PER_DPS;
  }
}

// Cutoff is a build option on purpose: walking is under 2 Hz, a hand shake is
// nearer 8 Hz, so one fixed value can't suit both.
static void stage_filter(int n, int a) {
  biquad_t f;
  lowpass_design(FC_HZ, SAMPLE_RATE_HZ, &f);

  uint64_t t0 = time_us_64();
  lowpass_block(&f, axis[a], n, filt);
  uint64_t dt = time_us_64() - t0;

  stats_t before, after;
  compute_stats(axis[a], n, &before, scratch);
  compute_stats(filt, n, &after, scratch);

  printf("\n=== Low-pass filter (%s, 2nd order Butterworth, fc = %.1f Hz) ===\n",
         axis_name[a], (float)FC_HZ);
  printf("  std before = %.6f, after = %.6f  (%.1f%% of the spread removed)\n",
         before.stddev, after.stddev,
         100.0f * (1.0f - after.stddev / before.stddev));
  printf("  [%llu us for %d samples, %.2f us/sample]\n", dt, n,
         (float)dt / (float)n);
}

static void stage_statistics(int n) {
  printf("\n=== Statistics (physical units: g, dps) ===\n");
  uint64_t t0 = time_us_64();
  for (int a = 0; a < AXES; a++) {
    stats_t s;
    compute_stats(axis[a], n, &s, scratch);
    print_stats(axis_name[a], &s);
  }
  printf("  [%llu us for %d axes]\n", time_us_64() - t0, AXES);
}

// One axis only. DC out and normalised first, or everything clips.
static void stage_quantisation(int n, int a) {
  printf("\n=== Quantisation (%s, DC removed, peak normalised) ===\n",
         axis_name[a]);

  memcpy(work_f, axis[a], (size_t)n * sizeof(float));
  remove_dc(work_f, n);
  float pk = peak_abs(work_f, n);
  normalise_peak(work_f, n, pk);
  printf("  %-5s %10s %12s %12s %8s\n", "bits", "SNR dB", "RMS err", "max err",
         "clips");

  for (int d = 0; d < N_DEPTHS; d++) {
    const int bits = bit_depths[d];
    int clips = 0;

    uint64_t t0 = time_us_64();
    quantize(work_f, n, bits, q, &clips);
    dequantize(q, n, bits, deq);
    uint64_t dt = time_us_64() - t0;

    float snr, maxe, rmse;
    error_metrics(work_f, deq, n, &snr, &maxe, &rmse);

    printf("  %-5d %10.2f %12.3e %12.3e %8d   [%llu us]\n", bits, snr, rmse,
           maxe, clips, dt);
  }
}

static void stage_spectrum(const char *drive, int n, int a) {
  printf("\n=== FFT (%s, %d points, fs = %.0f Hz) ===\n", axis_name[a], n,
         SAMPLE_RATE_HZ);

  memcpy(work_f, axis[a], (size_t)n * sizeof(float));
  remove_dc(work_f, n);

  peaks_t pk;
  uint64_t t0 = time_us_64();
  int rc = spectrum(work_f, n, SAMPLE_RATE_HZ, mag, &pk);
  uint64_t dt = time_us_64() - t0;
  if (rc != 0) {
    printf("  spectrum() failed - n must be a power of two\n");
    return;
  }

  printf("  [%llu us, %.1f us/sample]\n", dt, (float)dt / (float)n);
  for (int i = 0; i < pk.count; i++)
    printf("  peak %d: bin %4d  %8.3f Hz  amplitude %.6f\n", i + 1, pk.idx[i],
           pk.freq_hz[i], pk.amplitude[i]);

  // dump the spectrum so numpy can be run over the same block
  char path[64];
  snprintf(path, sizeof path, "%s/%s_fft.csv", drive, DATASET);
  FIL f;
  if (f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
    char line[64];
    int len = snprintf(line, sizeof line, "freq_hz,amplitude\n");
    UINT bw;
    f_write(&f, line, len, &bw);
    for (int i = 0; i < n / 2; i++) {
      len = snprintf(line, sizeof line, "%.4f,%.8f\n",
                     (float)i * SAMPLE_RATE_HZ / (float)n, mag[i]);
      f_write(&f, line, len, &bw);
    }
    f_close(&f);
    printf("  spectrum written to %s\n", path);
  }
}

// export the exact block that was analysed, not the whole recording
static void export_raw(const char *drive, int n) {
  char path[64];
  snprintf(path, sizeof path, "%s/%s_raw.csv", drive, DATASET);
  FIL f;
  if (f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
    return;

  char line[128];
  UINT bw;
  int len = snprintf(line, sizeof line, "# fs_hz=%.1f n=%d\n", SAMPLE_RATE_HZ, n);
  f_write(&f, line, len, &bw);
  len = snprintf(line, sizeof line, "ax,ay,az,gx,gy,gz\n");
  f_write(&f, line, len, &bw);
  for (int i = 0; i < n; i++) {
    len = snprintf(line, sizeof line, "%.6f,%.6f,%.6f,%.4f,%.4f,%.4f\n",
                   axis[0][i], axis[1][i], axis[2][i], axis[3][i], axis[4][i],
                   axis[5][i]);
    f_write(&f, line, len, &bw);
  }
  f_close(&f);
  printf("raw block written to %s\n", path);
}

int main(void) {
  stdio_init_all();
  sleep_ms(3000);

  printf("\n=== IAS0360 HW1 - IMU feature extraction ===\n");
  printf("dataset = %s, requested %d samples\n", DATASET, N_SAMPLES);

  const char *drive;
  if (!sd_begin(&drive))
    goto done;

  int n = load_recording(drive, DATASET);
  if (n <= 0)
    goto done;

  to_physical(n);

  stage_filter(n, 0); // ax
  export_raw(drive, n);

  stage_statistics(n);
  stage_quantisation(n, 0); // ax
  stage_spectrum(drive, n, 0);

  printf("\ndone.\n");

done:
  while (true)
    tight_loop_contents();
  return 0;
}
