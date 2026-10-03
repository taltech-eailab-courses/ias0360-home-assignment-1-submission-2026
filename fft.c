#include "hw1.h"
#include <math.h>
#include <stdbool.h>

typedef struct {
  float re, im;
} c32;

// sized at build time, keeps it off the stack
static c32 work[N_SAMPLES];

static int is_power_of_two(int n) { return (n > 0) && ((n & (n - 1)) == 0); }

static unsigned reverse_bits(unsigned v, int nbits) {
  unsigned r = 0u;
  for (int i = 0; i < nbits; i++) {
    r = (r << 1) | (v & 1u);
    v >>= 1u;
  }
  return r;
}

// in-place radix-2 Cooley-Tukey
static int fft_radix2(c32 *x, int n) {
  if (!is_power_of_two(n))
    return -1;
  int logn = 0;
  while ((1 << logn) < n)
    logn++;

  for (unsigned i = 0; i < (unsigned)n; i++) {
    unsigned j = reverse_bits(i, logn);
    if (j > i) {
      c32 t = x[i];
      x[i] = x[j];
      x[j] = t;
    }
  }

  for (int s = 1; s <= logn; s++) {
    int m = 1 << s;
    int m2 = m >> 1;
    float theta = -(float)M_PI / (float)m2;
    float wpr = -2.0f * sinf(0.5f * theta) * sinf(0.5f * theta);
    float wpi = sinf(theta);
    for (int k = 0; k < n; k += m) {
      float wr = 1.0f, wi = 0.0f;
      for (int j = 0; j < m2; j++) {
        int t = k + j + m2;
        int u = k + j;
        float tr = wr * x[t].re - wi * x[t].im;
        float ti = wr * x[t].im + wi * x[t].re;
        float ur = x[u].re, ui = x[u].im;
        x[t].re = ur - tr;
        x[t].im = ui - ti;
        x[u].re = ur + tr;
        x[u].im = ui + ti;
        float tmp = wr; /* twiddle recurrence, no trig in the inner loop */
        wr = wr + (wr * wpr - wi * wpi);
        wi = wi + (wi * wpr + tmp * wpi);
      }
    }
  }
  return 0;
}

// top K bins, DC skipped
static void top_k_peaks(const float *mag, int n_half, int K, int *out_idx,
                        float *out_val, int *out_count) {
  int count = 0;
  for (int k = 0; k < K; k++) {
    int best_i = -1;
    float best_v = -1.0f;
    for (int i = 1; i < n_half; i++) {
      bool taken = false;
      for (int j = 0; j < count; j++)
        if (out_idx[j] == i) {
          taken = true;
          break;
        }
      if (taken)
        continue;
      if (mag[i] > best_v) {
        best_v = mag[i];
        best_i = i;
      }
    }
    if (best_i >= 0) {
      out_idx[count] = best_i;
      out_val[count] = best_v;
      count++;
    }
  }
  *out_count = count;
}

// window -> FFT -> single-sided magnitude -> peaks.
// x is not modified, mag_out gets n/2 bins.
int spectrum(const float *x, int n, float fs, float *mag_out, peaks_t *peaks) {
  if (n > N_SAMPLES || !is_power_of_two(n))
    return -1;

  for (int i = 0; i < n; i++) {
    work[i].re = x[i];
    work[i].im = 0.0f;
  }
  /* Hamming window: taper the frame edges so that cutting a finite block out
   * of a longer signal does not smear energy across the spectrum. */
  for (int i = 0; i < n; i++)
    work[i].re *=
        0.54f - 0.46f * cosf(2.0f * (float)M_PI * (float)i / (float)(n - 1));

  if (fft_radix2(work, n) != 0)
    return -1;

  const int half = n / 2;
  for (int i = 0; i < half; i++) {
    float m = sqrtf(work[i].re * work[i].re + work[i].im * work[i].im);
    /* single-sided amplitude: double every bin except DC */
    mag_out[i] = (i == 0) ? m / (float)n : 2.0f * m / (float)n;
  }

  float vals[FFT_PEAKS];
  top_k_peaks(mag_out, half, FFT_PEAKS, peaks->idx, vals, &peaks->count);
  const float df = fs / (float)n;
  for (int i = 0; i < peaks->count; i++) {
    peaks->freq_hz[i] = (float)peaks->idx[i] * df;
    peaks->amplitude[i] = vals[i];
  }
  return 0;
}
