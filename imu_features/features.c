/* Adapted from lab_1_2/statistic.c and lab_1_2/fft.c. */
#include "features.h"
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
// ---------- Simple complex type ----------
typedef struct { float re, im; } c32;

// ---------- Small utilities ----------



// ---------- Hamming window (in-place) ----------
static void hamming_window(float* x, int n){
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
static int fft_radix2(c32* x, int n, int dir){
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


Features extract_features(const float x[WINDOW_N], float sample_rate) {
    Features f = {0};
    double sum = 0, squared = 0;
    f.min = f.max = x[0];
    for (int i = 0; i < WINDOW_N; ++i) {
        sum += x[i];
        if (x[i] < f.min) f.min = x[i];
        if (x[i] > f.max) f.max = x[i];
    }
    f.mean = (float)(sum / WINDOW_N);
    static float centered[WINDOW_N]; /* Keep large buffers off the Pico stack. */
    for (int i = 0; i < WINDOW_N; ++i) {
        centered[i] = x[i] - f.mean;
        squared += (double)centered[i] * centered[i];
    }
    f.variance = (float)(squared / (WINDOW_N - 1));
    f.stddev = sqrtf(f.variance);
    /* Remove the mean before windowing so gravity/DC does not dominate. */
    hamming_window(centered, WINDOW_N);
    static c32 spectrum[WINDOW_N];
    float window_sum = 0;
    for (int i = 0; i < WINDOW_N; ++i) {
        spectrum[i] = (c32){centered[i], 0};
        window_sum += 0.54f - 0.46f*cosf(2.0f*(float)M_PI*i/(WINDOW_N-1));
    }
    fft_radix2(spectrum, WINDOW_N, 1);
    /* DC excluded; Nyquist is not doubled. Exact Hamming gain correction. */
    for (int k = 1; k <= WINDOW_N/2; ++k) {
        float scale = (k == WINDOW_N/2 ? 1.0f : 2.0f) / window_sum;
        float amplitude = hypotf(spectrum[k].re, spectrum[k].im) * scale;
        if (amplitude > f.peak_amplitude) {
            f.peak_amplitude = amplitude;
            f.peak_hz = k * sample_rate / WINDOW_N;
        }
    }
    return f;
}
