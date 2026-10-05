/*****************************************************************************
* | File        : sobel.c
* | Author      : TalTech IAS0360 / Martin Lukacka
* | Function    : Sobel 3x3 Edge Detection & Norm Benchmark (L1 vs L2)
* | Info        : Adapted from lab_1_2/sobel.c for HA1
*----------------
* | This version: V1.0
* | Date        : 2026-09-25
* | Info        : Basic version
******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "pico/stdlib.h"
#include "sobel.h"

static inline char to_ascii(uint8_t v) {
    if (v > 200) return '#';
    if (v > 120) return '+';
    if (v >  40) return ':';
    if (v >  10) return '.';
    return ' ';
}

void sobel3x3_l2(const uint8_t* img, int w, int h, uint8_t* out) {
    // Clear top and bottom borders
    for (int x = 0; x < w; x++) {
        out[x] = 0;
        out[(h - 1) * w + x] = 0;
    }
    // Clear left and right borders
    for (int y = 0; y < h; y++) {
        out[y * w] = 0;
        out[y * w + (w - 1)] = 0;
    }

    for (int y = 1; y < h - 1; y++) {
        for (int x = 1; x < w - 1; x++) {
            int i = y * w + x;
            int gx = - img[i - w - 1] - 2 * img[i - 1] - img[i + w - 1]
                     + img[i - w + 1] + 2 * img[i + 1] + img[i + w + 1];
            int gy = - img[i - w - 1] - 2 * img[i - w] - img[i - w + 1]
                     + img[i + w - 1] + 2 * img[i + w] + img[i + w + 1];

            int mag = (int)sqrtf((float)(gx * gx + gy * gy));
            if (mag < 0)   mag = 0;
            if (mag > 255) mag = 255;
            out[i] = (uint8_t)mag;
        }
    }
}

void sobel3x3_l1(const uint8_t* img, int w, int h, uint8_t* out) {
    // Clear top and bottom borders
    for (int x = 0; x < w; x++) {
        out[x] = 0;
        out[(h - 1) * w + x] = 0;
    }
    // Clear left and right borders
    for (int y = 0; y < h; y++) {
        out[y * w] = 0;
        out[y * w + (w - 1)] = 0;
    }

    for (int y = 1; y < h - 1; y++) {
        for (int x = 1; x < w - 1; x++) {
            int i = y * w + x;
            int gx = - img[i - w - 1] - 2 * img[i - 1] - img[i + w - 1]
                     + img[i - w + 1] + 2 * img[i + 1] + img[i + w + 1];
            int gy = - img[i - w - 1] - 2 * img[i - w] - img[i - w + 1]
                     + img[i + w - 1] + 2 * img[i + w] + img[i + w + 1];

            int mag = abs(gx) + abs(gy);
            if (mag > 255) mag = 255;
            out[i] = (uint8_t)mag;
        }
    }
}

void sobel_benchmark(const uint8_t* img, int w, int h,
                     uint32_t* time_l1_us, uint32_t* time_l2_us) {
    if (!time_l1_us || !time_l2_us) return;

    static uint8_t temp_out[28 * 28];
    if (!img || w * h <= 0 || w * h > (int)sizeof(temp_out)) {
        *time_l1_us = 0;
        *time_l2_us = 0;
        return;
    }

    // Warm-up run
    sobel3x3_l1(img, w, h, temp_out);

    // Measure L1 norm
    uint32_t start_l1 = time_us_32();
    for (int iter = 0; iter < 10; iter++) {
        sobel3x3_l1(img, w, h, temp_out);
    }
    uint32_t end_l1 = time_us_32();
    *time_l1_us = (end_l1 - start_l1) / 10;

    // Measure L2 norm
    uint32_t start_l2 = time_us_32();
    for (int iter = 0; iter < 10; iter++) {
        sobel3x3_l2(img, w, h, temp_out);
    }
    uint32_t end_l2 = time_us_32();
    *time_l2_us = (end_l2 - start_l2) / 10;
}

void sobel_print_ascii_dual(const uint8_t* img1, const uint8_t* img2, int w, int h) {
    if (!img1 || !img2 || w <= 0 || h <= 0) return;

    printf("+----------------------------+  +----------------------------+\n");
    for (int y = 0; y < h; y++) {
        putchar('|');
        for (int x = 0; x < w; x++) {
            putchar(to_ascii(img1[y * w + x]));
        }
        printf("|  |");
        for (int x = 0; x < w; x++) {
            putchar(to_ascii(img2[y * w + x]));
        }
        printf("|\n");
    }
    printf("+----------------------------+  +----------------------------+\n");
}
