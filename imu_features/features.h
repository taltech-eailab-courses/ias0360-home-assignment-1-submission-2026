#ifndef FEATURES_H
#define FEATURES_H
#define WINDOW_N 1024
typedef struct {
    float mean, variance, stddev, min, max;
    float peak_hz, peak_amplitude;
} Features;
/* x: acceleration in g; sample_rate: measured Hz; fixed WINDOW_N samples.
 * Uses static scratch buffers: call from one core/task only. */
Features extract_features(const float x[WINDOW_N], float sample_rate);
#endif
