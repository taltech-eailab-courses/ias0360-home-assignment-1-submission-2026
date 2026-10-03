#include "lab12_filters.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline float lab12_clampf(float v, float lo, float hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

uint8_t lab12_shadow_to_gray(uint8_t shadow_value) {
    return shadow_value ? 0u : 255u;
}

float lab12_gray_to_normalized(uint8_t gray) {
    /* Keeps the domain strictly below +1, as required by the supplied Q15 code. */
    return ((float)gray / 128.0f) - 1.0f;
}

int32_t lab12_quantize_signed(float x, int bits, int *clipped) {
    if (bits < 2 || bits > 16) {
        if (clipped) *clipped = 0;
        return 0;
    }

    const int32_t scale = (int32_t)1 << (bits - 1);
    const int32_t qmin = -scale;
    const int32_t qmax = scale - 1;
    const float max_value = (float)qmax / (float)scale;

    const float s = lab12_clampf(x, -1.0f, max_value);
    if (clipped) *clipped = (s != x) ? 1 : 0;

    int32_t q = (int32_t)lrintf(s * (float)scale);
    if (q > qmax) q = qmax;
    if (q < qmin) q = qmin;
    return q;
}

float lab12_dequantize_signed(int32_t q, int bits) {
    if (bits < 2 || bits > 16) return 0.0f;
    const int32_t scale = (int32_t)1 << (bits - 1);
    return (float)q / (float)scale;
}

void lab12_quant_metrics_shadow(const uint8_t *shadow, size_t n, int bits,
                                lab12_quant_metrics_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->bits = bits;

    if (!shadow || n == 0 || bits < 2 || bits > 16) {
        out->snr_db = NAN;
        out->max_abs_error = NAN;
        out->rmse = NAN;
        return;
    }

    double sig = 0.0;
    double err = 0.0;
    float maxe = 0.0f;
    uint32_t clips = 0;

    for (size_t i = 0; i < n; ++i) {
        const float ref = lab12_gray_to_normalized(lab12_shadow_to_gray(shadow[i]));
        int clipped = 0;
        const int32_t q = lab12_quantize_signed(ref, bits, &clipped);
        const float dq = lab12_dequantize_signed(q, bits);
        const float e = ref - dq;

        clips += (uint32_t)clipped;
        sig += (double)ref * (double)ref;
        err += (double)e * (double)e;
        if (fabsf(e) > maxe) maxe = fabsf(e);
    }

    out->clip_count = clips;
    out->rmse = (float)sqrt(err / (double)n);
    out->max_abs_error = maxe;
    out->snr_db = (err == 0.0) ? INFINITY : 10.0f * log10f((float)(sig / err));
}

uint8_t lab12_pack_q4(int8_t first, int8_t second) {
    return (uint8_t)(((uint8_t)first & 0x0Fu) | (((uint8_t)second & 0x0Fu) << 4));
}

void lab12_statistics_shadow(const uint8_t *shadow, size_t n, lab12_stats_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!shadow || n == 0) return;

    /* The current LCD capture is binary, so a 2-bin histogram is exact. */
    size_t drawn = 0;
    for (size_t i = 0; i < n; ++i) {
        if (shadow[i]) ++drawn;
    }
    const size_t white = n - drawn;

    const float black_v = lab12_gray_to_normalized(0);   /* -1.0 */
    const float white_v = lab12_gray_to_normalized(255); /* 0.9921875 */

    const double sum = (double)drawn * black_v + (double)white * white_v;
    const float mean = (float)(sum / (double)n);

    double ss = 0.0;
    if (drawn) {
        const double d = (double)black_v - mean;
        ss += (double)drawn * d * d;
    }
    if (white) {
        const double d = (double)white_v - mean;
        ss += (double)white * d * d;
    }

    out->min = drawn ? black_v : white_v;
    out->max = white ? white_v : black_v;
    out->mean = mean;
    out->variance = (n > 1) ? (float)(ss / (double)(n - 1)) : 0.0f;
    out->stddev = sqrtf(out->variance);

    if (drawn > n / 2) out->median = black_v;
    else if (white > n / 2) out->median = white_v;
    else out->median = 0.5f * (black_v + white_v);

    if (drawn >= white) {
        out->mode = black_v;
        out->mode_count = (uint32_t)drawn;
    } else {
        out->mode = white_v;
        out->mode_count = (uint32_t)white;
    }
}

uint8_t lab12_sobel_shadow_pixel(const uint8_t *shadow, int w, int h, int x, int y) {
    if (!shadow || w <= 0 || h <= 0 || x <= 0 || y <= 0 || x >= w - 1 || y >= h - 1)
        return 0;

#define G(xx, yy) ((int)lab12_shadow_to_gray(shadow[(yy) * w + (xx)]))
    const int gx =
        -G(x-1,y-1) - 2*G(x-1,y) - G(x-1,y+1)
        +G(x+1,y-1) + 2*G(x+1,y) + G(x+1,y+1);
    const int gy =
        -G(x-1,y-1) - 2*G(x,y-1) - G(x+1,y-1)
        +G(x-1,y+1) + 2*G(x,y+1) + G(x+1,y+1);
#undef G

    int mag = (int)sqrtf((float)(gx * gx + gy * gy));
    if (mag < 0) mag = 0;
    if (mag > 255) mag = 255;
    return (uint8_t)mag;
}

static int lab12_is_power_of_two(int n) {
    return (n > 0) && ((n & (n - 1)) == 0);
}

static unsigned lab12_reverse_bits(unsigned v, int nbits) {
    unsigned r = 0u;
    for (int i = 0; i < nbits; ++i) {
        r = (r << 1) | (v & 1u);
        v >>= 1u;
    }
    return r;
}

static void lab12_hamming_window(float *x, int n) {
    for (int i = 0; i < n; ++i) {
        x[i] *= 0.54f - 0.46f * cosf(2.0f * (float)M_PI * (float)i / (float)(n - 1));
    }
}

static int lab12_fft_radix2(lab12_c32_t *x, int n, int dir) {
    if (!lab12_is_power_of_two(n)) return -1;

    int logn = 0;
    while ((1 << logn) < n) ++logn;

    for (unsigned i = 0; i < (unsigned)n; ++i) {
        const unsigned j = lab12_reverse_bits(i, logn);
        if (j > i) {
            const lab12_c32_t t = x[i];
            x[i] = x[j];
            x[j] = t;
        }
    }

    const float sgn = (dir >= 0) ? -1.0f : 1.0f;

    for (int s = 1; s <= logn; ++s) {
        const int m = 1 << s;
        const int m2 = m >> 1;
        const float theta = sgn * (float)M_PI / (float)m2;
        const float wpr = -2.0f * sinf(0.5f * theta) * sinf(0.5f * theta);
        const float wpi = sinf(theta);

        for (int k = 0; k < n; k += m) {
            float wr = 1.0f, wi = 0.0f;
            for (int j = 0; j < m2; ++j) {
                const int t = k + j + m2;
                const int u = k + j;
                const float tr = wr * x[t].re - wi * x[t].im;
                const float ti = wr * x[t].im + wi * x[t].re;
                const float ur = x[u].re, ui = x[u].im;
                x[t].re = ur - tr;
                x[t].im = ui - ti;
                x[u].re = ur + tr;
                x[u].im = ui + ti;

                const float tmp = wr;
                wr = wr + (wr * wpr - wi * wpi);
                wi = wi + (wi * wpr + tmp * wpi);
            }
        }
    }

    if (dir < 0) {
        const float inv = 1.0f / (float)n;
        for (int i = 0; i < n; ++i) {
            x[i].re *= inv;
            x[i].im *= inv;
        }
    }
    return 0;
}

int lab12_fft_shadow_row(const uint8_t *shadow, int w, int h, int row,
                         float raw_out[LAB12_FFT_N],
                         float mag_out[LAB12_FFT_BINS]) {
    if (!shadow || !raw_out || !mag_out || w < LAB12_FFT_N || h <= 0)
        return -1;

    if (row < 0) row = h / 2;
    if (row >= h) return -2;

    float windowed[LAB12_FFT_N];
    lab12_c32_t X[LAB12_FFT_N];

    for (int i = 0; i < LAB12_FFT_N; ++i) {
        const uint8_t gray = lab12_shadow_to_gray(shadow[row * w + i]);
        raw_out[i] = lab12_gray_to_normalized(gray);
        windowed[i] = raw_out[i];
    }

    lab12_hamming_window(windowed, LAB12_FFT_N);
    for (int i = 0; i < LAB12_FFT_N; ++i) {
        X[i].re = windowed[i];
        X[i].im = 0.0f;
    }

    if (lab12_fft_radix2(X, LAB12_FFT_N, +1) != 0)
        return -3;

    const float scale = (2.0f / (float)LAB12_FFT_N) / 0.54f;
    for (int k = 0; k < LAB12_FFT_BINS; ++k) {
        const float mag = sqrtf(X[k].re * X[k].re + X[k].im * X[k].im);
        mag_out[k] = mag * scale;
    }

    return 0;
}
