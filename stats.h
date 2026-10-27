#ifndef STATS_H
#define STATS_H

float mean_f32(const float* x, int n); // Calculates mean.
float variance_f32(const float* x, int n, float mean); // Calculates mean variance from mean.
float stddev_f32(float variance); // Calculates standard deviation from variance.


#endif