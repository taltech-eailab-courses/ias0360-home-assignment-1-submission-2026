#include "dsp_features.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void imu_convert_units(int16_t raw_ax, int16_t raw_ay, int16_t raw_az,
                       int16_t raw_gx, int16_t raw_gy, int16_t raw_gz,
                       float* ax_g, float* ay_g, float* az_g, float* amag_g,
                       float* gx_dps, float* gy_dps, float* gz_dps)
{
    float ax = (float)raw_ax / ACCEL_SENSITIVITY_2G;
    float ay = (float)raw_ay / ACCEL_SENSITIVITY_2G;
    float az = (float)raw_az / ACCEL_SENSITIVITY_2G;

    if (ax_g) *ax_g = ax;
    if (ay_g) *ay_g = ay;
    if (az_g) *az_g = az;
    if (amag_g) *amag_g = sqrtf(ax * ax + ay * ay + az * az);

    if (gx_dps) *gx_dps = (float)raw_gx / GYRO_SENSITIVITY_1000DPS;
    if (gy_dps) *gy_dps = (float)raw_gy / GYRO_SENSITIVITY_1000DPS;
    if (gz_dps) *gz_dps = (float)raw_gz / GYRO_SENSITIVITY_1000DPS;
}

// -------------------------------------------------------------
// Time-Domain Statistics
// -------------------------------------------------------------

float dsp_mean_f32(const float* x, int n)
{
    if (n <= 0) return 0.0f;
    double acc = 0.0;
    for (int i = 0; i < n; i++) acc += x[i];
    return (float)(acc / (double)n);
}

float dsp_variance_f32(const float* x, int n, float mean)
{
    if (n <= 1) return 0.0f;
    double acc = 0.0;
    for (int i = 0; i < n; i++) {
        double d = (double)x[i] - (double)mean;
        acc += d * d;
    }
    return (float)(acc / (double)(n - 1));
}

float dsp_stddev_f32(float variance)
{
    return (variance > 0.0f) ? sqrtf(variance) : 0.0f;
}

void dsp_min_max_f32(const float* x, int n, float* mn, float* mx)
{
    if (n <= 0) {
        if (mn) *mn = 0.0f;
        if (mx) *mx = 0.0f;
        return;
    }
    float a = x[0], b = x[0];
    for (int i = 1; i < n; i++) {
        if (x[i] < a) a = x[i];
        if (x[i] > b) b = x[i];
    }
    if (mn) *mn = a;
    if (mx) *mx = b;
}

float dsp_median_f32(const float* x, int n)
{
    if (n <= 0) return 0.0f;
    if (n == 1) return x[0];

    // Static buffer to avoid dynamic allocation on embedded stack
    static float tmp[IMU_WINDOW_SIZE];
    int count = (n > IMU_WINDOW_SIZE) ? IMU_WINDOW_SIZE : n;
    for (int i = 0; i < count; i++) tmp[i] = x[i];

    // Insertion sort
    for (int i = 1; i < count; i++) {
        float key = tmp[i];
        int j = i - 1;
        while (j >= 0 && tmp[j] > key) {
            tmp[j + 1] = tmp[j];
            j--;
        }
        tmp[j + 1] = key;
    }

    if (count & 1) return tmp[count / 2];
    return 0.5f * (tmp[count / 2 - 1] + tmp[count / 2]);
}

float dsp_rms_f32(const float* x, int n)
{
    if (n <= 0) return 0.0f;
    double acc = 0.0;
    for (int i = 0; i < n; i++) {
        acc += (double)x[i] * (double)x[i];
    }
    return (float)sqrt(acc / (double)n);
}

void dsp_compute_stats(const float* x, int n, imu_stats_t* stats)
{
    if (!stats || n <= 0) return;
    stats->mean = dsp_mean_f32(x, n);
    stats->variance = dsp_variance_f32(x, n, stats->mean);
    stats->stddev = dsp_stddev_f32(stats->variance);
    dsp_min_max_f32(x, n, &stats->min_val, &stats->max_val);
    stats->range = stats->max_val - stats->min_val;
    stats->median = dsp_median_f32(x, n);
    stats->rms = dsp_rms_f32(x, n);
}

// -------------------------------------------------------------
// Frequency-Domain Spectral Features
// -------------------------------------------------------------

void dsp_hamming_window(float* x, int n)
{
    for (int i = 0; i < n; i++) {
        x[i] *= 0.54f - 0.46f * cosf(2.0f * (float)M_PI * (float)i / (float)(n - 1));
    }
}

static inline int is_power_of_two(int n)
{
    return (n > 0) && ((n & (n - 1)) == 0);
}

static inline unsigned reverse_bits(unsigned v, int nbits)
{
    unsigned r = 0u;
    for (int i = 0; i < nbits; i++) {
        r = (r << 1) | (v & 1u);
        v >>= 1u;
    }
    return r;
}

int dsp_fft_radix2(c32* x, int n, int dir)
{
    if (!is_power_of_two(n)) return -1;
    int logn = 0;
    while ((1 << logn) < n) logn++;

    // Bit-reversal permutation
    for (unsigned i = 0; i < (unsigned)n; i++) {
        unsigned j = reverse_bits(i, logn);
        if (j > i) {
            c32 t = x[i];
            x[i] = x[j];
            x[j] = t;
        }
    }

    const float sgn = (dir >= 0) ? -1.0f : 1.0f;
    for (int s = 1; s <= logn; s++) {
        int m = 1 << s;
        int m2 = m >> 1;
        float theta = sgn * (float)M_PI / (float)m2;
        float wpr = -2.0f * sinf(0.5f * theta) * sinf(0.5f * theta);
        float wpi = sinf(theta);
        for (int k = 0; k < n; k += m) {
            float wr = 1.0f, wi = 0.0f;
            for (int j = 0; j < m2; j++) {
                int t = k + j + m2;
                int u = k + j;
                float tr = wr * x[t].re - wi * x[t].im;
                float ti = wr * x[t].im + wi * x[t].re;
                float ur = x[u].re, ui = x[u].im;
                x[t].re = ur - tr;
                x[t].im = ui - ti;
                x[u].re = ur + tr;
                x[u].im = ui + ti;
                float tmp = wr;
                wr = wr + (wr * wpr - wi * wpi);
                wi = wi + (wi * wpr + tmp * wpi);
            }
        }
    }

    if (dir < 0) {
        float inv = 1.0f / (float)n;
        for (int i = 0; i < n; i++) {
            x[i].re *= inv;
            x[i].im *= inv;
        }
    }
    return 0;
}

void dsp_fft_mag(const c32* X, int n, float* mag)
{
    for (int i = 0; i < n; i++) {
        mag[i] = sqrtf(X[i].re * X[i].re + X[i].im * X[i].im);
    }
}

void dsp_extract_fft_features(const float* signal, int n, float fs,
                              imu_fft_result_t* result, float* out_mag_scaled)
{
    if (!result || n <= 0 || !is_power_of_two(n)) return;

    static float win_buf[IMU_WINDOW_SIZE];
    static c32 c_buf[IMU_WINDOW_SIZE];
    static float mag_raw[IMU_WINDOW_SIZE];

    int count = (n > IMU_WINDOW_SIZE) ? IMU_WINDOW_SIZE : n;
    for (int i = 0; i < count; i++) win_buf[i] = signal[i];

    // Apply Hamming window
    dsp_hamming_window(win_buf, count);

    // Pack into complex array
    for (int i = 0; i < count; i++) {
        c_buf[i].re = win_buf[i];
        c_buf[i].im = 0.0f;
    }

    // Run forward FFT
    dsp_fft_radix2(c_buf, count, +1);

    // Compute raw magnitude
    dsp_fft_mag(c_buf, count, mag_raw);

    // Single-sided spectrum scaling: (2 / N) / 0.54 (Hamming coherent gain)
    const float scale = (2.0f / (float)count) / 0.54f;
    int n_half = count / 2;

    float energy = 0.0f;
    for (int k = 0; k <= n_half; k++) {
        mag_raw[k] *= scale;
        if (out_mag_scaled) out_mag_scaled[k] = mag_raw[k];
        if (k > 0) energy += mag_raw[k] * mag_raw[k];
    }
    result->spectral_energy = energy;

    // Pick top K peaks (excluding DC bin 0)
    int found = 0;
    int taken_bins[IMU_TOP_K_PEAKS];

    for (int k = 0; k < IMU_TOP_K_PEAKS; k++) {
        int best_i = -1;
        float best_v = -1.0f;
        for (int i = 1; i <= n_half; i++) {
            bool already_taken = false;
            for (int j = 0; j < found; j++) {
                if (taken_bins[j] == i) {
                    already_taken = true;
                    break;
                }
            }
            if (already_taken) continue;
            if (mag_raw[i] > best_v) {
                best_v = mag_raw[i];
                best_i = i;
            }
        }
        if (best_i >= 0) {
            taken_bins[found] = best_i;
            result->peaks[found].bin = best_i;
            result->peaks[found].freq_hz = (float)best_i * (fs / (float)count);
            result->peaks[found].amplitude = best_v;
            found++;
        }
    }
    result->peak_count = found;
    result->dominant_freq_hz = (found > 0) ? result->peaks[0].freq_hz : 0.0f;
}

// -------------------------------------------------------------
// Fixed-Point Quantization & Error Analysis
// -------------------------------------------------------------

static inline float fclamp(float v, float lo, float hi)
{
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

void dsp_quantize_q15(const float* x, int n, int16_t* y, int* clip_count)
{
    int clips = 0;
    for (int i = 0; i < n; i++) {
        float s = fclamp(x[i], -0.999969f, 0.999969f);
        if (s != x[i]) clips++;
        int32_t q = (int32_t)lrintf(s * 32768.0f);
        if (q >  32767) q =  32767;
        if (q < -32768) q = -32768;
        y[i] = (int16_t)q;
    }
    if (clip_count) *clip_count = clips;
}

void dsp_dequantize_q15(const int16_t* x, int n, float* y)
{
    for (int i = 0; i < n; i++) {
        y[i] = (float)x[i] / 32768.0f;
    }
}

void dsp_evaluate_quantization(const float* ref, const int16_t* q15, int n, quant_eval_t* eval)
{
    if (!eval || n <= 0) return;

    double sig_pwr = 0.0;
    double err_pwr = 0.0;
    float max_err = 0.0f;

    for (int i = 0; i < n; i++) {
        float dequant = (float)q15[i] / 32768.0f;
        float err = ref[i] - dequant;
        sig_pwr += (double)ref[i] * (double)ref[i];
        err_pwr += (double)err * (double)err;
        if (fabsf(err) > max_err) max_err = fabsf(err);
    }

    eval->max_abs_err = max_err;
    eval->rms_err = (float)sqrt(err_pwr / (double)n);
    eval->original_bytes = n * (int)sizeof(float);
    eval->quantized_bytes = n * (int)sizeof(int16_t);

    if (err_pwr == 0.0) {
        eval->snr_db = 999.0f;
    } else if (sig_pwr == 0.0) {
        eval->snr_db = 0.0f;
    } else {
        eval->snr_db = (float)(10.0 * log10(sig_pwr / err_pwr));
    }
}
