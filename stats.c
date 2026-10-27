#include "math.h"
#include "stats.h"


float mean_f32(const float* x, int n) { // Mean.
    double acc = 0.0;
    for (int i = 0; i < n; i++) acc += x[i];
    return (float)(acc / (double)n);
}

float variance_f32(const float* x, int n, float mean) { // Variance.
    if (n <= 1) return 0.0f;
    double acc = 0.0;
    for (int i = 0; i < n; i++) {
        double d = (double)x[i] - (double)mean;
        acc += d * d;
    }
    return (float)(acc / (double)(n - 1));
}

float stddev_f32(float variance) { // Standard deviance.
    return sqrtf(variance);
}