#include "hw1.h"
#include <math.h>

// Returns the mean it removed - for the gyro that's the bias, worth printing.
float remove_dc(float *x, int n) {
  double acc = 0.0;
  for (int i = 0; i < n; i++)
    acc += x[i];
  float mean = (float)(acc / (double)n);
  for (int i = 0; i < n; i++)
    x[i] -= mean;
  return mean;
}

float peak_abs(const float *x, int n) {
  float m = 0.0f;
  for (int i = 0; i < n; i++) {
    float a = fabsf(x[i]);
    if (a > m)
      m = a;
  }
  return m;
}

// 0.999 and not 1.0 so the top sample doesn't land on the clip limit.
void normalise_peak(float *x, int n, float peak) {
  if (peak <= 0.0f)
    return;
  float g = 0.999f / peak;
  for (int i = 0; i < n; i++)
    x[i] *= g;
}
