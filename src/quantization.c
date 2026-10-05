/*****************************************************************************
* | File        : quantization.c
* | Author      : TalTech IAS0360 / Martin Lukacka
* | Function    : Multi-Bit Quantization, Dequantization, SNR and
*                 RMS Error Analysis
* | Info        : Adapted from lab_1_2/quantization.c for HA1
*----------------
* | This version: V1.0
* | Date        : 2026-09-25
* | Info        : Basic version
******************************************************************************/

#include <stdio.h>
#include <math.h>
#include "quantization.h"

static inline float clampf(float v, float lo, float hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

void quantize_q15(const float* x, int n, int16_t* y, int* clip_count) {
    int clips = 0;
    for (int i = 0; i < n; i++) {
        float s = clampf(x[i], -0.999969f, 0.999969f);
        if (s != x[i]) clips++;
        int32_t q = (int32_t)lrintf(s * 32768.0f);
        if (q >  32767) q =  32767;
        if (q < -32768) q = -32768;
        y[i] = (int16_t)q;
    }
    if (clip_count) *clip_count = clips;
}

void dequantize_q15(const int16_t* x, int n, float* y) {
    for (int i = 0; i < n; i++) {
        y[i] = (float)x[i] / 32768.0f;
    }
}

void quantize_uniform(const float* in, int n, int bits,
                      float* out_dequantized) {
    if (bits <= 1) {
        for (int i = 0; i < n; i++) {
            out_dequantized[i] = (in[i] >= 0.5f) ? 1.0f : 0.0f;
        }
        return;
    }

    float max_level = (float)((1 << bits) - 1);
    for (int i = 0; i < n; i++) {
        float s = clampf(in[i], 0.0f, 1.0f);
        float q = roundf(s * max_level);
        out_dequantized[i] = q / max_level;
    }
}

void snr_and_error(const float* ref, const float* test, int n,
                   float* snr_db, float* max_abs_err, float* rms_err) {
    if (!ref || !test || n <= 0) return;

    double sig = 0.0, err = 0.0;
    float maxe = 0.0f;
    for (int i = 0; i < n; i++) {
        float e = ref[i] - test[i];
        sig += (double)ref[i] * (double)ref[i];
        err += (double)e * (double)e;
        if (fabsf(e) > maxe) maxe = fabsf(e);
    }
    float rms = (float)sqrt(err / (double)n);
    if (rms_err) *rms_err = rms;
    if (max_abs_err) *max_abs_err = maxe;

    if (snr_db) {
        if (err == 0.0) {
            *snr_db = 999.0f;  // Infinite / perfect reconstruction
        } else if (sig == 0.0) {
            *snr_db = 0.0f;
        } else {
            *snr_db = 10.0f * log10f((float)(sig / err));
        }
    }
}

void quantization_sweep(const uint8_t* img_u8, int n,
                        quant_metric_t results[4]) {
    if (!img_u8 || !results || n <= 0 || n > 28 * 28) return;

    static float ref[28 * 28];
    static float test[28 * 28];
    static int16_t q15_buf[28 * 28];

    // Convert uint8 [0..255] to reference normalized float [0.0..1.0]
    for (int i = 0; i < n; i++) {
        ref[i] = (float)img_u8[i] / 255.0f;
    }

    const int bits_to_test[4] = {16, 8, 4, 1};

    for (int idx = 0; idx < 4; idx++) {
        int b = bits_to_test[idx];
        results[idx].bits = b;
        results[idx].levels = (1 << b);

        if (b == 16) {
            // Q15 fixed-point test
            int clips = 0;
            quantize_q15(ref, n, q15_buf, &clips);
            dequantize_q15(q15_buf, n, test);
            results[idx].memory_bytes = n * 2;
        } else {
            // Uniform b-bit test
            quantize_uniform(ref, n, b, test);
            if (b == 8)
                results[idx].memory_bytes = n;
            else if (b == 4)
                results[idx].memory_bytes = (n + 1) / 2;
            else
                results[idx].memory_bytes = (n + 7) / 8;
        }

        results[idx].compression_ratio =
            (float)(n * sizeof(float)) / (float)results[idx].memory_bytes;
        snr_and_error(ref, test, n, &results[idx].snr_db,
                      &results[idx].max_error, &results[idx].rms_error);
    }
}
