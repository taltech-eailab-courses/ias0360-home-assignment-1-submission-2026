// New declarations for the functions adapted from IAS0360 Lab 1.2.
#ifndef SIGNAL_PROCESSING_H
#define SIGNAL_PROCESSING_H

#include <stdint.h>

enum { N = 256, TOP_K = 5 };

// Complex type copied from Lab 1.2 fft.c.
typedef struct { float re, im; } c32;

// All sample arrays contain n values; statistics/quantization require n > 0.
float mean_f32(const float* x, int n);
float variance_f32(const float* x, int n, float mean);
float stddev_f32(float variance);
void min_max_f32(const float* x, int n, float* mn, float* mx);

void quantize_q15(const float* x, int n, int16_t* y, int* clip_count);
void dequantize_q15(const int16_t* x, int n, float* y);
void quantize_q7(const float* x, int n, int8_t* y, int* clip_count);
void dequantize_q7(const int8_t* x, int n, float* y);
void quantize_q3(const float* x, int n, int8_t* y, int* clip_count);
void dequantize_q3(const int8_t* x, int n, float* y);
void snr_and_error(const float* ref, const float* test, int n,
                   float* snr_db, float* max_abs_err, float* rms_err);

void hamming_window(float* x, int n); // n > 1; modifies x
int fft_radix2(c32* x, int n, int dir); // power of two; +1 FFT, -1 IFFT
void fft_mag(const c32* X, int n, float* mag);
void top_k_peaks(const float* mag, int n_half, int k_exclude_dc, int K,
                 int* out_idx, float* out_val, int* out_count);

#endif
