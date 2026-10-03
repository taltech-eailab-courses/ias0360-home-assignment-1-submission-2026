#ifndef LAB12_FILTERS_H
#define LAB12_FILTERS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float min;
    float max;
    float mean;
    float median;
    float mode;
    uint32_t mode_count;
    float variance;
    float stddev;
} lab12_stats_t;

typedef struct {
    int bits;
    uint32_t clip_count;
    float snr_db;
    float max_abs_error;
    float rmse;
} lab12_quant_metrics_t;

/* LCD shadow convention: 1 = drawn black, 0 = empty white. */
uint8_t lab12_shadow_to_gray(uint8_t shadow_value);

/* Normalize uint8 grayscale to the lab's signed ~[-1,1) domain. */
float lab12_gray_to_normalized(uint8_t gray);

/* Generic signed fixed-point quantizer derived from the supplied Q15 code. */
int32_t lab12_quantize_signed(float x, int bits, int *clipped);
float lab12_dequantize_signed(int32_t q, int bits);

/* Compute quantization/dequantization errors over the complete shadow image. */
void lab12_quant_metrics_shadow(const uint8_t *shadow, size_t n, int bits,
                                lab12_quant_metrics_t *out);

/* Pack two signed Q4 samples (-8..7) into one byte, two's-complement nibbles. */
uint8_t lab12_pack_q4(int8_t first, int8_t second);

/* Statistics over the normalized image, equivalent definitions to statistic.c. */
void lab12_statistics_shadow(const uint8_t *shadow, size_t n, lab12_stats_t *out);

/* Exact Sobel magnitude used by supplied sobel.c, without a second framebuffer. */
uint8_t lab12_sobel_shadow_pixel(const uint8_t *shadow, int w, int h, int x, int y);

/* FFT definitions adapted from supplied fft.c. */
#define LAB12_FFT_N 256
#define LAB12_FFT_BINS (LAB12_FFT_N / 2 + 1)

typedef struct {
    float re;
    float im;
} lab12_c32_t;

/*
 * Spatial FFT for an image row.
 * raw_out receives UNWINDOWED normalized samples.
 * mag_out receives single-sided magnitudes using the lab scale (2/N)/0.54.
 * Frequency of bin k is k/N cycles/pixel.
 */
int lab12_fft_shadow_row(const uint8_t *shadow, int w, int h, int row,
                         float raw_out[LAB12_FFT_N],
                         float mag_out[LAB12_FFT_BINS]);

#ifdef __cplusplus
}
#endif

#endif
