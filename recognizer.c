#include "recognizer.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define NORM_W 28
#define NORM_H 28

static const uint8_t digit_templates[10][35] = {

    /* 0 */
    {
        0,1,1,1,0,
        1,0,0,0,1,
        1,0,0,1,1,
        1,0,1,0,1,
        1,1,0,0,1,
        1,0,0,0,1,
        0,1,1,1,0
    },

    /* 1 */
    {
        0,0,1,0,0,
        0,1,1,0,0,
        1,0,1,0,0,
        0,0,1,0,0,
        0,0,1,0,0,
        0,0,1,0,0,
        1,1,1,1,1
    },

    /* 2 */
    {
        0,1,1,1,0,
        1,0,0,0,1,
        0,0,0,0,1,
        0,0,0,1,0,
        0,0,1,0,0,
        0,1,0,0,0,
        1,1,1,1,1
    },

    /* 3 */
    {
        1,1,1,1,0,
        0,0,0,0,1,
        0,0,0,1,0,
        0,0,1,1,0,
        0,0,0,0,1,
        1,0,0,0,1,
        0,1,1,1,0
    },

    /* 4 */
    {
        0,0,0,1,0,
        0,0,1,1,0,
        0,1,0,1,0,
        1,0,0,1,0,
        1,1,1,1,1,
        0,0,0,1,0,
        0,0,0,1,0
    },

    /* 5 */
    {
        1,1,1,1,1,
        1,0,0,0,0,
        1,1,1,1,0,
        0,0,0,0,1,
        0,0,0,0,1,
        1,0,0,0,1,
        0,1,1,1,0
    },

    /* 6 */
    {
        0,1,1,1,0,
        1,0,0,0,0,
        1,0,0,0,0,
        1,1,1,1,0,
        1,0,0,0,1,
        1,0,0,0,1,
        0,1,1,1,0
    },

    /* 7 */
    {
        1,1,1,1,1,
        0,0,0,0,1,
        0,0,0,1,0,
        0,0,1,0,0,
        0,1,0,0,0,
        0,1,0,0,0,
        0,1,0,0,0
    },

    /* 8 */
    {
        0,1,1,1,0,
        1,0,0,0,1,
        1,0,0,0,1,
        0,1,1,1,0,
        1,0,0,0,1,
        1,0,0,0,1,
        0,1,1,1,0
    },

    /* 9 */
    {
        0,1,1,1,0,
        1,0,0,0,1,
        1,0,0,0,1,
        0,1,1,1,1,
        0,0,0,0,1,
        0,0,0,0,1,
        0,1,1,1,0
    }
};


/*
 * Find bounding box of the drawn character.
 */
static int find_bbox(
    const uint8_t *bitmap,
    int w,
    int h,
    int *x0,
    int *y0,
    int *x1,
    int *y1)
{
    int min_x = w;
    int min_y = h;
    int max_x = -1;
    int max_y = -1;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {

            if (bitmap[y * w + x] == 0)
                continue;

            if (x < min_x) min_x = x;
            if (x > max_x) max_x = x;
            if (y < min_y) min_y = y;
            if (y > max_y) max_y = y;
        }
    }

    if (max_x < 0)
        return 0;

    *x0 = min_x;
    *y0 = min_y;
    *x1 = max_x;
    *y1 = max_y;

    return 1;
}


/*
 * Convert bounding box to a 5x7 occupancy representation.
 *
 * Creating a large 28x28 float array.
 */
static void make_5x7_features(
    const uint8_t *bitmap,
    int w,
    int h,
    int x0,
    int y0,
    int x1,
    int y1,
    float features[35])
{
    int bw = x1 - x0 + 1;
    int bh = y1 - y0 + 1;

    memset(features, 0, sizeof(float) * 35);

    if (bw <= 0 || bh <= 0)
        return;

    for (int gy = 0; gy < 7; gy++) {

        int sy0 = y0 + (gy * bh) / 7;
        int sy1 = y0 + ((gy + 1) * bh) / 7;

        for (int gx = 0; gx < 5; gx++) {

            int sx0 = x0 + (gx * bw) / 5;
            int sx1 = x0 + ((gx + 1) * bw) / 5;

            int count = 0;
            int total = 0;

            for (int y = sy0; y < sy1; y++) {
                for (int x = sx0; x < sx1; x++) {

                    if (bitmap[y * w + x])
                        count++;

                    total++;
                }
            }

            if (total > 0) {
                features[gy * 5 + gx] =
                    (float)count / (float)total;
            }
        }
    }
}


char recognize_character(
    const uint8_t *bitmap,
    size_t bitmap_len,
    int src_w,
    int src_h,
    float *out_score)
{
    if (out_score)
        *out_score = 999.0f;

    if (!bitmap)
        return '?';

    if (src_w <= 0 || src_h <= 0)
        return '?';

    if (bitmap_len < (size_t)src_w * (size_t)src_h)
        return '?';

    int x0, y0, x1, y1;

    if (!find_bbox(
            bitmap,
            src_w,
            src_h,
            &x0,
            &y0,
            &x1,
            &y1)) {

        return '?';
    }

    /*
     * Reject extremely small drawings.
     */
    int width = x1 - x0 + 1;
    int height = y1 - y0 + 1;

    if (width < 5 || height < 10)
        return '?';

    float input[35];

    make_5x7_features(
        bitmap,
        src_w,
        src_h,
        x0,
        y0,
        x1,
        y1,
        input
    );

    float best_score = 999.0f;
    int best_digit = -1;

 
    for (int digit = 0; digit < 10; digit++) {

        float score = 0.0f;

        for (int i = 0; i < 35; i++) {

            float target =
                digit_templates[digit][i]
                ? 0.5f
                : 0.0f;

            float diff = input[i] - target;

            score += diff * diff;
        }

        score /= 35.0f;

        if (score < best_score) {
            best_score = score;
            best_digit = digit;
        }
    }

    if (out_score)
        *out_score = best_score;

    if (best_digit < 0)
        return '?';

    if (best_score > 0.20f)
        return '?';

    return (char)('0' + best_digit);
}



int recognizer_preprocess(
    const uint8_t *bitmap,
    size_t bitmap_len,
    int src_w,
    int src_h,
    uint8_t out28[REC_NORM_H][REC_NORM_W])
{
    (void)bitmap;
    (void)bitmap_len;
    (void)src_w;
    (void)src_h;

    memset(out28, 0,
           sizeof(uint8_t) * REC_NORM_W * REC_NORM_H);

    return 0;
}


void recognizer_extract_features(
    const uint8_t img28[REC_NORM_H][REC_NORM_W],
    float features[REC_FEATURES])
{
    size_t k = 0;

    for (int y = 0; y < REC_NORM_H; y++) {
        for (int x = 0; x < REC_NORM_W; x++) {

            if (k < REC_FEATURES) {
                features[k++] =
                    img28[y][x] ? 1.0f : 0.0f;
            }
        }
    }
}


char recognizer_classify(
    const float *features,
    const float *templates,
    const char *labels,
    size_t num_templates,
    float max_distance,
    float *out_score)
{
    if (!features ||
        !templates ||
        !labels ||
        num_templates == 0) {

        if (out_score)
            *out_score = 999.0f;

        return '?';
    }

    float best = 999.0f;
    size_t best_idx = 0;

    for (size_t n = 0; n < num_templates; n++) {

        float score = 0.0f;

        for (size_t i = 0; i < REC_FEATURES; i++) {

            float d =
                features[i] -
                templates[n * REC_FEATURES + i];

            score += d * d;
        }

        score /= REC_FEATURES;

        if (score < best) {
            best = score;
            best_idx = n;
        }
    }

    if (out_score)
        *out_score = best;

    if (best > max_distance)
        return '?';

    return labels[best_idx];
}