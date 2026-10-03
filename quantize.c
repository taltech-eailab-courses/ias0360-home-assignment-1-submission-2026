#include "hw1.h"
#include <math.h>

static inline float clampf(float v, float lo, float hi) {
  return (v < lo) ? lo : (v > hi) ? hi : v;
}

// levels = 2^(bits-1), step = 1/levels. Top of the range is (levels-1)/levels,
// not 1.0, so at 4 bits anything above 0.875 clips. Clips are counted.
void quantize(const float *x, int n, int bits, int16_t *y, int *clip_count) {
  const int levels = 1 << (bits - 1);
  const int max = levels - 1;
  const int min = -levels;
  const float limit = (float)max / (float)levels;

  int clips = 0;
  for (int i = 0; i < n; i++) {
    float s = clampf(x[i], -1.0f, limit);
    if (s != x[i])
      clips++;
    int32_t q = (int32_t)lrintf(s * (float)levels);
    if (q > max)
      q = max;
    if (q < min)
      q = min;
    y[i] = (int16_t)q;
  }
  if (clip_count)
    *clip_count = clips;
}

void dequantize(const int16_t *x, int n, int bits, float *y) {
  const int levels = 1 << (bits - 1);
  for (int i = 0; i < n; i++)
    y[i] = (float)x[i] / (float)levels;
}

// test vs ref: SNR, RMS error, worst single error
void error_metrics(const float *ref, const float *test, int n, float *snr_db,
                   float *max_abs_err, float *rms_err) {
  double sig = 0.0, err = 0.0;
  float maxe = 0.0f;
  for (int i = 0; i < n; i++) {
    float e = ref[i] - test[i];
    sig += (double)ref[i] * (double)ref[i];
    err += (double)e * (double)e;
    if (fabsf(e) > maxe)
      maxe = fabsf(e);
  }
  *rms_err = (float)sqrt(err / (double)n);
  *max_abs_err = maxe;
  *snr_db = (err == 0.0) ? INFINITY : 10.0f * log10f((float)(sig / err));
}
