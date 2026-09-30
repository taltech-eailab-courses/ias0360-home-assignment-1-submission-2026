#ifndef HOME_ASSIGNMENT_1_MODEL_SETTINGS_H_
#define HOME_ASSIGNMENT_1_MODEL_SETTINGS_H_

constexpr int kImageWidth = 28;
constexpr int kImageHeight = 28;
constexpr int kImagePixels = kImageWidth * kImageHeight;
constexpr int kDigitCount = 10;

// Replace this after measuring interpreter->arena_used_bytes() for your model.
constexpr int kTensorArenaSize = 60 * 1024;

#endif
