/*****************************************************************************
* | File        : quantization.h
* | Author      : TalTech IAS0360 / Martin Lukacka
* | Function    : Multi-Bit Quantization, Dequantization, SNR and
*                 RMS Error Analysis
* | Info        : Adapted from lab_1_2/quantization.c for HA1
*----------------
* | This version: V1.0
* | Date        : 2026-09-25
* | Info        : Basic version
******************************************************************************/

#ifndef QUANTIZATION_H_
#define QUANTIZATION_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int bits;
    int levels;
    float snr_db;
    float rms_error;
    float max_error;
    int memory_bytes;
    float compression_ratio;
} quant_metric_t;

/**
 * @brief Quantize float in [-1, 1) to signed Q15 int16_t
 */
void quantize_q15(const float* x, int n, int16_t* y, int* clip_count);

/**
 * @brief Dequantize signed Q15 int16_t back to float in [-1, 1)
 */
void dequantize_q15(const int16_t* x, int n, float* y);

/**
 * @brief Uniformly quantize normalized image [0.0, 1.0] to a given bit-depth
 * @param in              Input float array [0.0, 1.0]
 * @param n               Array length
 * @param bits            Bit depth (e.g. 8, 4, 1)
 * @param out_dequantized Reconstructed float array after quantization
 */
void quantize_uniform(const float* in, int n, int bits, float* out_dequantized);

/**
 * @brief Compute Signal-to-Noise Ratio (SNR in dB) and RMS / Max error
 */
void snr_and_error(const float* ref, const float* test, int n,
                   float* snr_db, float* max_abs_err, float* rms_err);

/**
 * @brief Run a sweep of 16-bit (Q15), 8-bit, 4-bit, and 1-bit quantization
 * @param img_u8  Input uint8 image [0..255]
 * @param n       Total pixels
 * @param results Output metrics array (length 4)
 */
void quantization_sweep(const uint8_t* img_u8, int n,
                        quant_metric_t results[4]);

#ifdef __cplusplus
}
#endif

#endif  // QUANTIZATION_H_
