/*****************************************************************************
* | File        : statistic.h
* | Author      : TalTech IAS0360 / Martin Lukacka
* | Function    : Statistical Feature Descriptors for Gesture and
*                 Digit Characterization
* | Info        : Adapted from lab_1_2/statistic.c for HA1
*----------------
* | This version: V1.0
* | Date        : 2026-09-25
* | Info        : Basic version
******************************************************************************/

#ifndef STATISTIC_H_
#define STATISTIC_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float mean;                // Mean pixel intensity (overall stroke weight)
    float variance;            // Spatial intensity dispersion
    float stddev;              // Standard deviation
    uint8_t min_val;           // Minimum intensity
    uint8_t max_val;           // Maximum intensity
    float active_edge_density; // Ratio of edge pixels (> threshold)
    float aspect_ratio;        // Bounding box height / width ratio (H/W)
} feature_vector_t;

/**
 * @brief Arithmetic mean of an 8-bit array
 */
float mean_u8(const uint8_t* x, int n);

/**
 * @brief Sample variance with Bessel's correction (N - 1)
 */
float variance_u8(const uint8_t* x, int n, float mean);

/**
 * @brief Standard deviation: sqrt(variance)
 */
float stddev_f32(float variance);

/**
 * @brief Dynamic range: minimum and maximum pixel values
 */
void min_max_u8(const uint8_t* x, int n, uint8_t* mn, uint8_t* mx);

/**
 * @brief Active edge density: proportion of pixels exceeding edge threshold
 */
float active_edge_density(const uint8_t* edge_map, int n, uint8_t threshold);

/**
 * @brief Aspect ratio: (height) / (width) of the bounding box
 */
float bounding_box_aspect_ratio(int min_x, int max_x, int min_y, int max_y);

/**
 * @brief Compute active bounding box around digit strokes
 */
void compute_bounding_box(const uint8_t* img, int w, int h,
                          int* min_x, int* max_x, int* min_y, int* max_y,
                          uint8_t threshold);

/**
 * @brief Extract the feature vector from downsampled image & edge map
 */
void extract_features(const uint8_t* raw_img, const uint8_t* edge_map,
                      int w, int h, int min_x, int max_x, int min_y, int max_y,
                      feature_vector_t* feats);

#ifdef __cplusplus
}
#endif

#endif  // STATISTIC_H_
