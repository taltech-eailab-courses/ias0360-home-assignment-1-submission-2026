#ifndef RECOGNIZER_H
#define RECOGNIZER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REC_NORM_W 28
#define REC_NORM_H 28
#define REC_FEATURES (REC_NORM_W * REC_NORM_H)

char recognize_character(const uint8_t *bitmap,
                         size_t bitmap_len,
                         int src_w,
                         int src_h,
                         float *out_score);

int recognizer_preprocess(const uint8_t *bitmap,
                         size_t bitmap_len,
                         int src_w,
                         int src_h,
                         uint8_t out28[REC_NORM_H][REC_NORM_W]);

void recognizer_extract_features(
    const uint8_t img28[REC_NORM_H][REC_NORM_W],
    float features[REC_FEATURES]);

char recognizer_classify(const float *features,
                         const float *templates,
                         const char *labels,
                         size_t num_templates,
                         float max_distance,
                         float *out_score);

#ifdef __cplusplus
}
#endif

#endif