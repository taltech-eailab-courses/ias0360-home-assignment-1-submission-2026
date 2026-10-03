#ifndef HW1_H
#define HW1_H

#include <stdint.h>

// must match the logger's struct exactly - 16 bytes, packed
typedef struct {
  int16_t ax, ay, az;
  int16_t gx, gy, gz;
  uint32_t t_us;
} __attribute__((packed)) imu_sample_t;

// from the ranges icm20948.c sets: +/-2 g and +/-1000 dps
#define ACCEL_LSB_PER_G 16384.0f
#define GYRO_LSB_PER_DPS 32.8f

#define SAMPLE_RATE_HZ 500.0f

// preprocess.c
float remove_dc(float *x, int n);
void normalise_peak(float *x, int n, float peak);
float peak_abs(const float *x, int n);

// filter.c
typedef struct {
  float b0, b1, b2, a1, a2;
  float z1, z2; // state
} biquad_t;

void lowpass_design(float fc_hz, float fs_hz, biquad_t *f);
void lowpass_block(biquad_t *f, const float *x, int n, float *y);

// quantize.c
void quantize(const float *x, int n, int bits, int16_t *y, int *clip_count);
void dequantize(const int16_t *x, int n, int bits, float *y);
void error_metrics(const float *ref, const float *test, int n, float *snr_db,
                   float *max_abs_err, float *rms_err);

// stats.c
typedef struct {
  float mean, variance, stddev, min, max, median, mode;
  int mode_count;
} stats_t;

void compute_stats(const float *x, int n, stats_t *s, float *scratch);
void print_stats(const char *label, const stats_t *s);

// fft.c
#define FFT_PEAKS 5

typedef struct {
  int idx[FFT_PEAKS];
  float freq_hz[FFT_PEAKS];
  float amplitude[FFT_PEAKS];
  int count;
} peaks_t;

int spectrum(const float *x, int n, float fs, float *mag_out, peaks_t *peaks);

#endif
