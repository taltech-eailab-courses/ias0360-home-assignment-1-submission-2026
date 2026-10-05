#ifndef __STATISTIC__H
#define __STATISTIC__H

float mean_f32(const float* x, int n);
float variance_f32(const float* x, int n, float mean);
float stddev_f32(float variance);
void  min_max_f32(const float* x, int n, float* mn, float* mx);
float median_f32(const float* x, int n);
float mode_f32(const float* x, int n, int* count_out, float eps);

#endif 