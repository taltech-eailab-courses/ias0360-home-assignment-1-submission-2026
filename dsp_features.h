#ifndef DSP_FEATURES_H
#define DSP_FEATURES_H

#include <stdint.h>
#include <stdbool.h>

#define IMU_FS_HZ           500.0f
#define IMU_WINDOW_SIZE     256
#define IMU_TOP_K_PEAKS     5

// Sensitivity scaling factors for ICM-20948
// Accelerometer: +/-2g range -> 16384 LSB/g
#define ACCEL_SENSITIVITY_2G     16384.0f
// Gyroscope: +/-1000 dps range -> 32.8 LSB/dps
#define GYRO_SENSITIVITY_1000DPS 32.8f

typedef struct {
    float re;
    float im;
} c32;

typedef struct {
    float mean;
    float variance;
    float stddev;
    float min_val;
    float max_val;
    float range;
    float median;
    float rms;
} imu_stats_t;

typedef struct {
    int bin;
    float freq_hz;
    float amplitude;
} imu_peak_t;

typedef struct {
    imu_peak_t peaks[IMU_TOP_K_PEAKS];
    int peak_count;
    float spectral_energy;
    float dominant_freq_hz;
} imu_fft_result_t;

typedef struct {
    float snr_db;
    float max_abs_err;
    float rms_err;
    int clip_count;
    int original_bytes;
    int quantized_bytes;
} quant_eval_t;

#ifdef __cplusplus
extern "C" {
#endif

// Unit conversion & Vector magnitude
void imu_convert_units(int16_t raw_ax, int16_t raw_ay, int16_t raw_az,
                       int16_t raw_gx, int16_t raw_gy, int16_t raw_gz,
                       float* ax_g, float* ay_g, float* az_g, float* amag_g,
                       float* gx_dps, float* gy_dps, float* gz_dps);

// Time-domain statistical features
float dsp_mean_f32(const float* x, int n);
float dsp_variance_f32(const float* x, int n, float mean);
float dsp_stddev_f32(float variance);
void  dsp_min_max_f32(const float* x, int n, float* mn, float* mx);
float dsp_median_f32(const float* x, int n);
float dsp_rms_f32(const float* x, int n);
void  dsp_compute_stats(const float* x, int n, imu_stats_t* stats);

// Frequency-domain spectral features
void dsp_hamming_window(float* x, int n);
int  dsp_fft_radix2(c32* x, int n, int dir);
void dsp_fft_mag(const c32* X, int n, float* mag);
void dsp_extract_fft_features(const float* signal, int n, float fs,
                              imu_fft_result_t* result, float* out_mag_scaled);

// Fixed-point quantization & error analysis
void dsp_quantize_q15(const float* x, int n, int16_t* y, int* clip_count);
void dsp_dequantize_q15(const int16_t* x, int n, float* y);
void dsp_evaluate_quantization(const float* ref, const int16_t* q15, int n, quant_eval_t* eval);

#ifdef __cplusplus
}
#endif

#endif // DSP_FEATURES_H
