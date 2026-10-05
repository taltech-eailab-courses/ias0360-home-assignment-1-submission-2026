/*****************************************************************************
* | File        : statistic.c
* | Author      : TalTech IAS0360 / Martin Lukacka
* | Function    : Statistical Feature Descriptors for Gesture and
*                 Digit Characterization
* | Info        : Adapted from lab_1_2/statistic.c for HA1
*----------------
* | This version: V1.0
* | Date        : 2026-09-25
* | Info        : Basic version
******************************************************************************/

#include <stdio.h>
#include <stdbool.h>
#include <math.h>
#include "statistic.h"

float mean_u8(const uint8_t* x, int n) {
    if (n <= 0) return 0.0f;
    double acc = 0.0;
    for (int i = 0; i < n; i++) {
        acc += x[i];
    }
    return (float)(acc / (double)n);
}

float variance_u8(const uint8_t* x, int n, float mean) {
    if (n <= 1) return 0.0f;
    double acc = 0.0;
    for (int i = 0; i < n; i++) {
        double d = (double)x[i] - (double)mean;
        acc += d * d;
    }
    return (float)(acc / (double)(n - 1));
}

float stddev_f32(float variance) {
    return sqrtf(variance);
}

void min_max_u8(const uint8_t* x, int n, uint8_t* mn, uint8_t* mx) {
    if (n <= 0) {
        *mn = 0;
        *mx = 0;
        return;
    }
    uint8_t a = x[0], b = x[0];
    for (int i = 1; i < n; i++) {
        if (x[i] < a) a = x[i];
        if (x[i] > b) b = x[i];
    }
    *mn = a;
    *mx = b;
}

float active_edge_density(const uint8_t* edge_map, int n, uint8_t threshold) {
    if (n <= 0) return 0.0f;
    int edge_count = 0;
    for (int i = 0; i < n; i++) {
        if (edge_map[i] > threshold) {
            edge_count++;
        }
    }
    return (float)edge_count / (float)n;
}

float bounding_box_aspect_ratio(int min_x, int max_x, int min_y, int max_y) {
    float width = (float)(max_x - min_x + 1);
    float height = (float)(max_y - min_y + 1);
    if (width <= 0.0f) return 1.0f;
    return height / width;
}

void compute_bounding_box(const uint8_t* img, int w, int h,
                          int* min_x, int* max_x, int* min_y, int* max_y,
                          uint8_t threshold) {
    int x0 = w - 1, x1 = 0, y0 = h - 1, y1 = 0;
    bool found = false;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (img[y * w + x] > threshold) {
                found = true;
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
        }
    }
    if (!found) {
        x0 = 0; x1 = w - 1; y0 = 0; y1 = h - 1;
    }
    *min_x = x0;
    *max_x = x1;
    *min_y = y0;
    *max_y = y1;
}

void extract_features(const uint8_t* raw_img, const uint8_t* edge_map,
                      int w, int h, int min_x, int max_x, int min_y, int max_y,
                      feature_vector_t* feats) {
    int n = w * h;
    feats->mean = mean_u8(raw_img, n);
    feats->variance = variance_u8(raw_img, n, feats->mean);
    feats->stddev = stddev_f32(feats->variance);
    min_max_u8(raw_img, n, &feats->min_val, &feats->max_val);

    // Active edge density: pixels with gradient magnitude > 40
    feats->active_edge_density = active_edge_density(edge_map, n, 40);

    // Aspect ratio: height / width
    feats->aspect_ratio = bounding_box_aspect_ratio(min_x, max_x, min_y, max_y);
}
