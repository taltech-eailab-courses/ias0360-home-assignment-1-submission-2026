// Adapted from IAS0360 Lab 1.2 statistic.c, quantization.c and fft.c.
// Example mains/unused functions omitted; public functions lose static.
// Q15 limits corrected; Q7/Q3 added using the same Q15 implementation.
#include "signal_processing.h"
#include <math.h>
#include <stdbool.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------- small helpers (statistics) ----------

// arithmetic mean
float mean_f32(const float* x, int n) {
    double acc = 0.0;
    for (int i = 0; i < n; i++) acc += x[i];
    return (float)(acc / (double)n);
}

// sample variance (denominator n-1)
float variance_f32(const float* x, int n, float mean) {
    if (n <= 1) return 0.0f;
    double acc = 0.0;
    for (int i = 0; i < n; i++) {
        double d = (double)x[i] - (double)mean;
        acc += d * d;
    }
    return (float)(acc / (double)(n - 1));
}

// standard deviation from variance
float stddev_f32(float variance) {
    return sqrtf(variance);
}

// min & max
void min_max_f32(const float* x, int n, float* mn, float* mx) {
    float a = x[0], b = x[0];
    for (int i = 1; i < n; i++) {
        if (x[i] < a) a = x[i];
        if (x[i] > b) b = x[i];
    }
    *mn = a; *mx = b;
}

// ---- From quantization.c ----
static inline float clampf(float v, float lo, float hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

// Quantize float in ~[-1,1) to signed Q15
void quantize_q15(const float* x, int n, int16_t* y, int* clip_count) {
    int clips = 0;
    for (int i = 0; i < n; i++) {
        float s = clampf(x[i], -1.0f, 32767.0f / 32768.0f); // exact Q15 limits
        if (s != x[i]) clips++;
        int32_t q = (int32_t)lrintf(s * 32768.0f);     // round to nearest
        if (q >  32767) q =  32767;
        if (q < -32768) q = -32768;
        y[i] = (int16_t)q;
    }
    if (clip_count) *clip_count = clips;
}

void dequantize_q15(const int16_t* x, int n, float* y) {
    for (int i = 0; i < n; i++) y[i] = (float)x[i] / 32768.0f;
}

// Q7/Q3 use the same course Q15 loop with smaller integer ranges.
// Quantize float in ~[-1,1) to signed Q7
void quantize_q7(const float* x, int n, int8_t* y, int* clip_count) {
    int clips = 0;
    for (int i = 0; i < n; i++) {
        float s = clampf(x[i], -1.0f, 127.0f / 128.0f); // exact Q7 limits
        if (s != x[i]) clips++;
        int32_t q = (int32_t)lrintf(s * 128.0f);     // round to nearest
        if (q >  127) q =  127;
        if (q < -128) q = -128;
        y[i] = (int8_t)q;
    }
    if (clip_count) *clip_count = clips;
}

void dequantize_q7(const int8_t* x, int n, float* y) {
    for (int i = 0; i < n; i++) y[i] = (float)x[i] / 128.0f;
}

// Quantize float in ~[-1,1) to signed Q3
void quantize_q3(const float* x, int n, int8_t* y, int* clip_count) {
    int clips = 0;
    for (int i = 0; i < n; i++) {
        float s = clampf(x[i], -1.0f, 7.0f / 8.0f); // exact Q3 limits
        if (s != x[i]) clips++;
        int32_t q = (int32_t)lrintf(s * 8.0f);     // round to nearest
        if (q >  7) q =  7;
        if (q < -8) q = -8;
        y[i] = (int8_t)q;
    }
    if (clip_count) *clip_count = clips;
}

void dequantize_q3(const int8_t* x, int n, float* y) {
    for (int i = 0; i < n; i++) y[i] = (float)x[i] / 8.0f;
}

void snr_and_error(const float* ref, const float* test, int n,
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
    if (err == 0.0) {
        *snr_db = INFINITY;
    } else {
        *snr_db = 10.0f * log10f((float)(sig / err));
    }
}

// ---- From fft.c (c32 is declared in the header) ----
// ---------- Small utilities ----------
static inline float fsqrtf(float x){ return sqrtf(x); }

// ---------- Hamming window (in-place) ----------
void hamming_window(float* x, int n){
    for(int i=0;i<n;i++){
        x[i] *= 0.54f - 0.46f * cosf(2.0f*(float)M_PI*(float)i/(float)(n-1));
    }
}

// ---------- Bit helpers ----------
static int is_power_of_two(int n){ return (n>0) && ((n & (n-1))==0); }
static unsigned reverse_bits(unsigned v, int nbits){
    unsigned r = 0u;
    for(int i=0;i<nbits;i++){ r = (r<<1) | (v & 1u); v >>= 1u; }
    return r;
}

// ---------- In-place radix-2 Cooley–Tukey FFT ----------
// dir = +1 for FFT, -1 for IFFT
int fft_radix2(c32* x, int n, int dir){
    if(!is_power_of_two(n)) return -1;
    int logn = 0; while((1<<logn) < n) logn++;

    // Bit-reversal permutation
    for(unsigned i=0;i<(unsigned)n;i++){
        unsigned j = reverse_bits(i, logn);
        if(j>i){ c32 t = x[i]; x[i]=x[j]; x[j]=t; }
    }

    const float sgn = (dir >= 0) ? -1.0f : 1.0f;
    for(int s=1; s<=logn; s++){
        int m = 1<<s;
        int m2 = m>>1;
        float theta = sgn * (float)M_PI / (float)m2;
        float wpr = -2.0f * sinf(0.5f*theta) * sinf(0.5f*theta);
        float wpi = sinf(theta);
        for(int k=0; k<n; k+=m){
            float wr = 1.0f, wi = 0.0f;
            for(int j=0; j<m2; j++){
                int t = k + j + m2;
                int u = k + j;
                float tr = wr*x[t].re - wi*x[t].im;
                float ti = wr*x[t].im + wi*x[t].re;
                float ur = x[u].re, ui = x[u].im;
                x[t].re = ur - tr; x[t].im = ui - ti;
                x[u].re = ur + tr; x[u].im = ui + ti;
                // twiddle update (CORDIC-free recurrence)
                float tmp = wr;
                wr = wr + (wr*wpr - wi*wpi);
                wi = wi + (wi*wpr + tmp*wpi);
            }
        }
    }

    if(dir < 0){
        float inv = 1.0f/(float)n;
        for(int i=0;i<n;i++){ x[i].re *= inv; x[i].im *= inv; }
    }
    return 0;
}

// ---------- Magnitude spectrum ----------
void fft_mag(const c32* X, int n, float* mag){
    for(int i=0;i<n;i++){
        mag[i] = fsqrtf(X[i].re*X[i].re + X[i].im*X[i].im);
    }
}

// ---------- Peak picking (top-K, single-sided) ----------
void top_k_peaks(const float* mag, int n_half, int k_exclude_dc, int K,
                        int* out_idx, float* out_val, int* out_count){
    // simple selection without sorting the full array
    int count = 0;
    for(int k=0; k<K; k++){
        int best_i = -1; float best_v = -1.0f;
        for(int i=k_exclude_dc; i<n_half; i++){
            // skip already taken
            bool taken = false;
            for(int j=0;j<count;j++) if(out_idx[j]==i){ taken = true; break; }
            if(taken) continue;
            if(mag[i] > best_v){ best_v = mag[i]; best_i = i; }
        }
        if(best_i >= 0){
            out_idx[count] = best_i;
            out_val[count] = best_v;
            count++;
        }
    }
    *out_count = count;
}
