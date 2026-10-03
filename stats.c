#include "hw1.h"
#include <math.h>
#include <stdio.h>

static float mean_f32(const float *x, int n) {
  double acc = 0.0;
  for (int i = 0; i < n; i++)
    acc += x[i];
  return (float)(acc / (double)n);
}

// n-1, not n
static float variance_f32(const float *x, int n, float mean) {
  if (n <= 1)
    return 0.0f;
  double acc = 0.0;
  for (int i = 0; i < n; i++) {
    double d = (double)x[i] - (double)mean;
    acc += d * d;
  }
  return (float)(acc / (double)(n - 1));
}

static void min_max_f32(const float *x, int n, float *mn, float *mx) {
  float a = x[0], b = x[0];
  for (int i = 1; i < n; i++) {
    if (x[i] < a)
      a = x[i];
    if (x[i] > b)
      b = x[i];
  }
  *mn = a;
  *mx = b;
}

// Scratch comes from the caller. A VLA here would be 8 kB of stack at
// n = 2048 and the RP2040 hasn't got it.
static float median_f32(const float *x, int n, float *tmp) {
  for (int i = 0; i < n; i++)
    tmp[i] = x[i];

  for (int i = 1; i < n; i++) { /* insertion sort */
    float key = tmp[i];
    int j = i - 1;
    while (j >= 0 && tmp[j] > key) {
      tmp[j + 1] = tmp[j];
      j--;
    }
    tmp[j + 1] = key;
  }

  if (n & 1)
    return tmp[n / 2];
  return 0.5f * (tmp[n / 2 - 1] + tmp[n / 2]);
}

// Longest run in the sorted copy. O(n), as opposed to comparing every pair.
static float mode_sorted(const float *sorted, int n, int *count_out) {
  float best_val = sorted[0];
  int best_count = 1, run = 1;
  for (int i = 1; i < n; i++) {
    run = (sorted[i] == sorted[i - 1]) ? run + 1 : 1;
    if (run > best_count) {
      best_count = run;
      best_val = sorted[i];
    }
  }
  if (count_out)
    *count_out = best_count;
  return best_val; /* best_count == 1 means there is no mode */
}

// scratch needs n floats, comes back sorted
void compute_stats(const float *x, int n, stats_t *s, float *scratch) {
  s->mean = mean_f32(x, n);
  s->variance = variance_f32(x, n, s->mean);
  s->stddev = sqrtf(s->variance);
  min_max_f32(x, n, &s->min, &s->max);
  s->median = median_f32(x, n, scratch); /* leaves scratch sorted */
  s->mode = mode_sorted(scratch, n, &s->mode_count);
}

void print_stats(const char *label, const stats_t *s) {
  printf("  %-4s mean=% .5f  var=% .5f  std=% .5f  min=% .5f  max=% .5f  "
         "median=% .5f\n",
         label, s->mean, s->variance, s->stddev, s->min, s->max, s->median);
  if (s->mode_count > 1)
    printf("       mode=% .5f (repeats %d times)\n", s->mode, s->mode_count);
  else
    printf("       mode: none - no value repeats\n");
}
