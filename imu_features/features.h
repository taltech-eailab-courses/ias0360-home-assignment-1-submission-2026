#ifndef FEATURES_H
#define FEATURES_H
#define WINDOW_N 256
typedef struct {
    float mean, variance, stddev, min, max;
    float peak_hz, peak_amplitude;
} Features;
/* x: acceleration in g; sample_rate: measured Hz; fixed 256-sample window. */
Features extract_features(const float x[WINDOW_N], float sample_rate);
#endif
