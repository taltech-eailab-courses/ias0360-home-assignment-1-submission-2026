/*****************************************************************************
* | File        : sobel.h
* | Author      : TalTech IAS0360 / Martin Lukacka
* | Function    : Sobel 3x3 Edge Detection & Norm Benchmark (L1 vs L2)
* | Info        : Adapted from lab_1_2/sobel.c for HA1
*----------------
* | This version: V1.0
* | Date        : 2026-09-25
* | Info        : Basic version
******************************************************************************/

#ifndef SOBEL_H_
#define SOBEL_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Sobel 3x3 edge detection using Euclidean L2 norm (floating-point)
 *        mag = clamp(sqrt(gx^2 + gy^2), 0..255)
 * @param img Input grayscale image (w * h)
 * @param w   Image width
 * @param h   Image height
 * @param out Output edge magnitude image (w * h)
 */
void sobel3x3_l2(const uint8_t* img, int w, int h, uint8_t* out);

/**
 * @brief Sobel 3x3 edge detection using Manhattan L1 norm (fast integer math)
 *        mag = clamp(|gx| + |gy|, 0..255)
 * @param img Input grayscale image (w * h)
 * @param w   Image width
 * @param h   Image height
 * @param out Output edge magnitude image (w * h)
 */
void sobel3x3_l1(const uint8_t* img, int w, int h, uint8_t* out);

/**
 * @brief Benchmark execution latency of L1 vs L2 Sobel filters on RP2040
 * @param img         Input grayscale image
 * @param w           Image width
 * @param h           Image height
 * @param time_l1_us  Output latency for L1 filter (microseconds)
 * @param time_l2_us  Output latency for L2 filter (microseconds)
 */
void sobel_benchmark(const uint8_t* img, int w, int h,
                     uint32_t* time_l1_us, uint32_t* time_l2_us);

/**
 * @brief Print side-by-side ASCII maps of two 8-bit images to serial console
 * @param img1 Left image buffer (e.g. grayscale input)
 * @param img2 Right image buffer (e.g. Sobel edge map)
 * @param w    Width
 * @param h    Height
 */
void sobel_print_ascii_dual(const uint8_t* img1, const uint8_t* img2, int w, int h);

#ifdef __cplusplus
}
#endif

#endif  // SOBEL_H_
