#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/util/queue.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"

#include "icm20948.h"

#include "ff.h"
#include "sd_card.h"
#include "f_util.h"
#include "hw_config.h"

// ============================================================
// Config
// ============================================================
#define SAMPLE_HZ         500
#define SAMPLE_PERIOD_US  (1000000 / SAMPLE_HZ)   // 2000 us for 500 Hz

#define QUEUE_DEPTH       1024   // ~2s of buffering @ 500Hz (16KB of RAM)
#define FLUSH_EVERY       128    // samples per SD write -> 128*16 = 2048 bytes
#define SYNC_EVERY_FLUSH  1

#define PATH_MAX_LEN      256
#define LOG_FILENAME      "imu_log.bin"

#define LED_PIN           15    // ADJUST to your board's actual status LED GPIO

// Analysis window (must be power of two for the FFT)
#define WINDOW_N          256
#define FS_HZ             ((float)SAMPLE_HZ)   // real sample rate, used for FFT freq axis

// ============================================================
// Shared types
// ============================================================
typedef struct {
    uint32_t t_us;
    int16_t  ax, ay, az;
    int16_t  gx, gy, gz;
} sample_t;

typedef enum { LED_IDLE, LED_SAMPLING, LED_ERROR } led_state_t;

static queue_t sample_q;
static volatile led_state_t led_state = LED_IDLE;
static volatile uint32_t drop_count = 0;

// ============================================================
// ---- Quantization (generalized to N bits) ----
// ============================================================
static inline float clampf(float v, float lo, float hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

static void quantize_qn(const float* x, int n, int16_t* y, int bits, int* clip_count) {
    int32_t qmax   = (1 << (bits - 1)) - 1;
    int32_t qmin   = -(1 << (bits - 1));
    float   scale  = (float)(1 << (bits - 1));
    int clips = 0;
    for (int i = 0; i < n; i++) {
        float s = clampf(x[i], -0.999969f, 0.999969f);
        if (s != x[i]) clips++;
        int32_t q = (int32_t)lrintf(s * scale);
        if (q > qmax) q = qmax;
        if (q < qmin) q = qmin;
        y[i] = (int16_t)q;
    }
    if (clip_count) *clip_count = clips;
}

static void dequantize_qn(const int16_t* x, int n, float* y, int bits) {
    float scale = (float)(1 << (bits - 1));
    for (int i = 0; i < n; i++) y[i] = (float)x[i] / scale;
}

static void snr_and_error(const float* ref, const float* test, int n,
                           float* snr_db, float* max_abs_err, float* rms_err) {
    double sig = 0.0, err = 0.0;
    float maxe = 0.0f;
    for (int i = 0; i < n; i++) {
        float e = ref[i] - test[i];
        sig += (double)ref[i] * (double)ref[i];
        err += (double)e * (double)e;
        if (fabsf(e) > maxe) maxe = fabsf(e);
    }
    float rms = (float)sqrt(err / (double)n);
    *rms_err = rms;
    *max_abs_err = maxe;
    *snr_db = (err == 0.0) ? INFINITY : 10.0f * log10f((float)(sig / err));
}

// ============================================================
// ---- Statistics ----
// ============================================================
static float mean_f32(const float* x, int n) {
    double acc = 0.0;
    for (int i = 0; i < n; i++) acc += x[i];
    return (float)(acc / (double)n);
}

static float variance_f32(const float* x, int n, float mean) {
    if (n <= 1) return 0.0f;
    double acc = 0.0;
    for (int i = 0; i < n; i++) {
        double d = (double)x[i] - (double)mean;
        acc += d * d;
    }
    return (float)(acc / (double)(n - 1));
}

static float stddev_f32(float variance) { return sqrtf(variance); }

static void min_max_f32(const float* x, int n, float* mn, float* mx) {
    float a = x[0], b = x[0];
    for (int i = 1; i < n; i++) {
        if (x[i] < a) a = x[i];
        if (x[i] > b) b = x[i];
    }
    *mn = a; *mx = b;
}

static float median_f32(const float* x, int n) {
    static float tmp[WINDOW_N];   // fixed-size, avoids a VLA on embedded
    for (int i = 0; i < n; i++) tmp[i] = x[i];
    for (int i = 1; i < n; i++) {
        float key = tmp[i];
        int j = i - 1;
        while (j >= 0 && tmp[j] > key) { tmp[j + 1] = tmp[j]; j--; }
        tmp[j + 1] = key;
    }
    if (n & 1) return tmp[n / 2];
    return 0.5f * (tmp[n / 2 - 1] + tmp[n / 2]);
}

static float mode_f32(const float* x, int n, int* count_out, float eps) {
    int best_count = 0;
    float best_val = NAN;
    for (int i = 0; i < n; i++) {
        int cnt = 1;
        for (int j = i + 1; j < n; j++) {
            if (fabsf(x[j] - x[i]) <= eps) cnt++;
        }
        if (cnt > best_count) { best_count = cnt; best_val = x[i]; }
    }
    if (count_out) *count_out = best_count;
    return best_val;
}

// ============================================================
// ---- FFT ----
// ============================================================
typedef struct { float re, im; } c32;

static void hamming_window(float* x, int n) {
    for (int i = 0; i < n; i++) {
        x[i] *= 0.54f - 0.46f * cosf(2.0f * (float)M_PI * (float)i / (float)(n - 1));
    }
}

static int is_power_of_two(int n) { return (n > 0) && ((n & (n - 1)) == 0); }

static unsigned reverse_bits(unsigned v, int nbits) {
    unsigned r = 0u;
    for (int i = 0; i < nbits; i++) { r = (r << 1) | (v & 1u); v >>= 1u; }
    return r;
}

static int fft_radix2(c32* x, int n, int dir) {
    if (!is_power_of_two(n)) return -1;
    int logn = 0; while ((1 << logn) < n) logn++;

    for (unsigned i = 0; i < (unsigned)n; i++) {
        unsigned j = reverse_bits(i, logn);
        if (j > i) { c32 t = x[i]; x[i] = x[j]; x[j] = t; }
    }

    const float sgn = (dir >= 0) ? -1.0f : 1.0f;
    for (int s = 1; s <= logn; s++) {
        int m = 1 << s, m2 = m >> 1;
        float theta = sgn * (float)M_PI / (float)m2;
        float wpr = -2.0f * sinf(0.5f * theta) * sinf(0.5f * theta);
        float wpi = sinf(theta);
        for (int k = 0; k < n; k += m) {
            float wr = 1.0f, wi = 0.0f;
            for (int j = 0; j < m2; j++) {
                int t = k + j + m2, u = k + j;
                float tr = wr * x[t].re - wi * x[t].im;
                float ti = wr * x[t].im + wi * x[t].re;
                float ur = x[u].re, ui = x[u].im;
                x[t].re = ur - tr; x[t].im = ui - ti;
                x[u].re = ur + tr; x[u].im = ui + ti;
                float tmp = wr;
                wr = wr + (wr * wpr - wi * wpi);
                wi = wi + (wi * wpr + tmp * wpi);
            }
        }
    }
    if (dir < 0) {
        float inv = 1.0f / (float)n;
        for (int i = 0; i < n; i++) { x[i].re *= inv; x[i].im *= inv; }
    }
    return 0;
}

static void fft_mag(const c32* X, int n, float* mag) {
    for (int i = 0; i < n; i++) mag[i] = sqrtf(X[i].re * X[i].re + X[i].im * X[i].im);
}

static void top_k_peaks(const float* mag, int n_half, int k_exclude_dc, int K,
                         int* out_idx, float* out_val, int* out_count) {
    int count = 0;
    for (int k = 0; k < K; k++) {
        int best_i = -1; float best_v = -1.0f;
        for (int i = k_exclude_dc; i < n_half; i++) {
            bool taken = false;
            for (int j = 0; j < count; j++) if (out_idx[j] == i) { taken = true; break; }
            if (taken) continue;
            if (mag[i] > best_v) { best_v = mag[i]; best_i = i; }
        }
        if (best_i >= 0) { out_idx[count] = best_i; out_val[count] = best_v; count++; }
    }
    *out_count = count;
}

// ============================================================
// ---- Per-window analysis pipeline (runs all 3 algorithms) ----
// ============================================================
static void process_window(const int16_t* raw, int n, const char* axis_name) {
    static float norm[WINDOW_N];       // raw normalized to [-1, 1)
    static float fft_in[WINDOW_N];     // copy for windowing (Hamming modifies in place)
    static c32   X[WINDOW_N];
    static float mag[WINDOW_N];
    static int16_t q16[WINDOW_N], q8[WINDOW_N], q4[WINDOW_N];
    static float dq16[WINDOW_N], dq8[WINDOW_N], dq4[WINDOW_N];

    // Treat raw int16 sensor codes as already-16-bit-quantized data in [-1,1)
    for (int i = 0; i < n; i++) norm[i] = (float)raw[i] / 32768.0f;

    // ---- 1) Quantization at 16/8/4 bits ----
    int clip16, clip8, clip4;
    float snr16, max16, rms16, snr8, max8, rms8, snr4, max4, rms4;

    quantize_qn(norm, n, q16, 16, &clip16); dequantize_qn(q16, n, dq16, 16);
    quantize_qn(norm, n, q8,  8,  &clip8);  dequantize_qn(q8,  n, dq8,  8);
    quantize_qn(norm, n, q4,  4,  &clip4);  dequantize_qn(q4,  n, dq4,  4);

    snr_and_error(norm, dq16, n, &snr16, &max16, &rms16);
    snr_and_error(norm, dq8,  n, &snr8,  &max8,  &rms8);
    snr_and_error(norm, dq4,  n, &snr4,  &max4,  &rms4);

    // ---- 2) Statistics ----
    float mn, mx, mean = mean_f32(norm, n);
    float var = variance_f32(norm, n, mean);
    float std = stddev_f32(var);
    float med = median_f32(norm, n);
    int mcount; float mode = mode_f32(norm, n, &mcount, 0.0f);
    min_max_f32(norm, n, &mn, &mx);

    // ---- 3) FFT ----
    memcpy(fft_in, norm, sizeof(float) * n);
    hamming_window(fft_in, n);
    for (int i = 0; i < n; i++) { X[i].re = fft_in[i]; X[i].im = 0.0f; }
    fft_radix2(X, n, +1);
    fft_mag(X, n, mag);

    const float scale = (2.0f / (float)n) / 0.54f;
    for (int k = 0; k <= n / 2; k++) mag[k] *= scale;

    int idx[5]; float val[5]; int found = 0;
    top_k_peaks(mag, n / 2 + 1, 1, 5, idx, val, &found);
    float df = FS_HZ / (float)n;

    // ---- Report ----
    printf("\n=== [%s] window analysis (n=%d, fs=%.1f Hz) ===\n", axis_name, n, FS_HZ);
    printf("Quant  16-bit: SNR=%.2f dB  RMSE=%.7f  max_err=%.7f  clips=%d\n", snr16, rms16, max16, clip16);
    printf("Quant   8-bit: SNR=%.2f dB  RMSE=%.7f  max_err=%.7f  clips=%d\n", snr8,  rms8,  max8,  clip8);
    printf("Quant   4-bit: SNR=%.2f dB  RMSE=%.7f  max_err=%.7f  clips=%d\n", snr4,  rms4,  max4,  clip4);
    printf("Stats: mean=%.5f median=%.5f var=%.5f std=%.5f min=%.5f max=%.5f",
           mean, med, var, std, mn, mx);
    if (mcount > 1) printf(" mode=%.5f(x%d)\n", mode, mcount); else printf(" mode=none\n");
    printf("FFT top-%d peaks (df=%.3f Hz):\n", found, df);
    for (int i = 0; i < found; i++) {
        printf("  bin=%d  freq=%.2f Hz  amp=%.4f\n", idx[i], df * (float)idx[i], val[i]);
    }
}

// ============================================================
// SD helpers (unchanged from earlier working version)
// ============================================================
static FATFS fs;
static sd_card_t *g_sd = NULL;
static const char *g_drive = NULL;

static void die(FRESULT fr, const char *op) {
    printf("%s failed: %s (%d)\n", op, FRESULT_str(fr), fr);
    led_state = LED_ERROR;
    while (1) tight_loop_contents();
}

static void join_path(char *out, size_t out_sz, const char *drive, const char *rel) {
    if (rel && rel[0] == '/') rel++;
    if (drive && drive[strlen(drive) - 1] == '/')
        snprintf(out, out_sz, "%s%s", drive, rel ? rel : "");
    else
        snprintf(out, out_sz, "%s/%s", drive, rel ? rel : "");
}

static bool sd_init_and_mount(void) {
    if (!sd_init_driver()) { printf("sd_init_driver() failed\n"); return false; }
    g_sd = sd_get_by_num(0);
    if (!g_sd) { printf("No SD config found\n"); return false; }
    g_drive = sd_get_drive_prefix(g_sd);
    if (!g_drive) { printf("sd_get_drive_prefix() returned NULL\n"); return false; }

    FRESULT fr = f_mount(&fs, g_drive, 1);
    printf("f_mount -> %s (%d)\n", FRESULT_str(fr), fr);

    if (fr == FR_NO_FILESYSTEM) {
        BYTE work[4096];
        MKFS_PARM opt = { FM_FAT | FM_SFD, 0, 0, 0, 0 };
        fr = f_mkfs(g_drive, &opt, work, sizeof work);
        printf("f_mkfs -> %s (%d)\n", FRESULT_str(fr), fr);
        if (fr == FR_OK) {
            fr = f_mount(&fs, g_drive, 1);
            printf("f_mount(after mkfs) -> %s (%d)\n", FRESULT_str(fr), fr);
        }
    }
    return (fr == FR_OK);
}

// ============================================================
// LED handling
// ============================================================
static void led_init(void) {
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0);
}

static void led_update(void) {
    static uint32_t last_toggle_us = 0;
    static bool on = false;
    uint32_t now = time_us_32();
    uint32_t period_us;
    switch (led_state) {
        case LED_IDLE:      gpio_put(LED_PIN, 0); return;
        case LED_SAMPLING:  period_us = 500000; break;
        case LED_ERROR:     period_us = 100000; break;
        default:            period_us = 500000; break;
    }
    if (now - last_toggle_us >= period_us) {
        on = !on;
        gpio_put(LED_PIN, on);
        last_toggle_us = now;
    }
}

// ============================================================
// Core 1: SD writer + windowed analysis (consumer)
// ============================================================
static void core1_entry(void) {
    if (!sd_init_and_mount()) {
        printf("SD init/mount failed.\n");
        led_state = LED_ERROR;
        while (1) tight_loop_contents();
    }

    char path[PATH_MAX_LEN];
    join_path(path, sizeof path, g_drive, LOG_FILENAME);

    FIL file;
    FRESULT fr = f_open(&file, path, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK) die(fr, "f_open");

    printf("Logging to %s\n", path);

    static sample_t batch[FLUSH_EVERY];
    uint32_t batch_n = 0;

    // Rolling analysis windows (accel X/Y/Z)
    static int16_t win_ax[WINDOW_N], win_ay[WINDOW_N], win_az[WINDOW_N];
    uint32_t win_n = 0;

    while (1) {
        sample_t s;
        queue_remove_blocking(&sample_q, &s);

        // --- SD batching (unchanged) ---
        batch[batch_n++] = s;
        if (batch_n == FLUSH_EVERY) {
            UINT bw = 0;
            fr = f_write(&file, batch, sizeof(batch), &bw);
            if (fr != FR_OK || bw != sizeof(batch)) {
                printf("f_write error: %s (%d)\n", FRESULT_str(fr), fr);
                led_state = LED_ERROR;
            }
            if (SYNC_EVERY_FLUSH) {
                fr = f_sync(&file);
                if (fr != FR_OK) { printf("f_sync error\n"); led_state = LED_ERROR; }
            }
            batch_n = 0;
            if (drop_count > 0) {
                printf("WARNING: %lu samples dropped so far\n", (unsigned long)drop_count);
            }
        }

        // --- Windowed analysis (new) ---
        win_ax[win_n] = s.ax;
        win_ay[win_n] = s.ay;
        win_az[win_n] = s.az;
        win_n++;

        if (win_n == WINDOW_N) {
            process_window(win_ax, WINDOW_N, "accel_x");
            process_window(win_ay, WINDOW_N, "accel_y");
            process_window(win_az, WINDOW_N, "accel_z");
            win_n = 0;
        }
    }
}

// ============================================================
// Core 0: paced IMU sampler (producer) + LED
// ============================================================
int main(void) {
    stdio_init_all();
    sleep_ms(3000);

    led_init();

    IMU_EN_SENSOR_TYPE type;
    imuInit(&type);
    if (type != IMU_EN_SENSOR_TYPE_ICM20948) {
        printf("IMU init failed -- got sensor type %d\n", type);
        led_state = LED_ERROR;
        while (1) { led_update(); sleep_ms(10); }
    }
    printf("IMU OK. Starting %d Hz sampling...\n", SAMPLE_HZ);

    queue_init(&sample_q, sizeof(sample_t), QUEUE_DEPTH);
    multicore_launch_core1(core1_entry);

    led_state = LED_SAMPLING;

    absolute_time_t next = get_absolute_time();
    uint32_t sample_counter = 0;

    while (1) {
        sleep_until(next);
        next = delayed_by_us(next, SAMPLE_PERIOD_US);

        int16_t ax, ay, az, gx, gy, gz;
        icm20948AccelFastRead(&ax, &ay, &az);
        icm20948GyroFastRead(&gx, &gy, &gz);

        sample_t s = {
            .t_us = time_us_32(),
            .ax = ax, .ay = ay, .az = az,
            .gx = gx, .gy = gy, .gz = gz,
        };

        if (!queue_try_add(&sample_q, &s)) {
            drop_count++;
            led_state = LED_ERROR;
        }

        led_update();

        sample_counter++;
        if (sample_counter % SAMPLE_HZ == 0) {
            printf("Sampled %lu total, %lu dropped\n",
                   (unsigned long)sample_counter, (unsigned long)drop_count);
        }
    }

    return 0;
}