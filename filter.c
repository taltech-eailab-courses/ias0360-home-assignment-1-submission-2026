#include "hw1.h"
#include <math.h>

// 2nd order Butterworth low-pass, bilinear transform, run as a biquad.
// Forward pass only - same as you'd do on a live stream, costs some phase lag.

void lowpass_design(float fc_hz, float fs_hz, biquad_t *f) {
  const float q = 0.70710678f; // Butterworth
  const float k = tanf((float)M_PI * fc_hz / fs_hz);
  const float k2 = k * k;
  const float norm = 1.0f / (1.0f + k / q + k2);

  f->b0 = k2 * norm;
  f->b1 = 2.0f * f->b0;
  f->b2 = f->b0;
  f->a1 = 2.0f * (k2 - 1.0f) * norm;
  f->a2 = (1.0f - k / q + k2) * norm;
  f->z1 = 0.0f;
  f->z2 = 0.0f;
}

void lowpass_block(biquad_t *f, const float *x, int n, float *y) {
  // prime the state from x[0], otherwise the output ramps up from zero and
  // the first ~0.5 s is useless
  f->z1 = x[0] * (1.0f - f->b0);
  f->z2 = x[0] * (1.0f - f->b0 - f->b1 + f->a1);

  // direct form II transposed
  for (int i = 0; i < n; i++) {
    float out = f->b0 * x[i] + f->z1;
    f->z1 = f->b1 * x[i] - f->a1 * out + f->z2;
    f->z2 = f->b2 * x[i] - f->a2 * out;
    y[i] = out;
  }
}
