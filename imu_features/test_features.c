#include "features.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#define PI 3.14159265358979323846
int main(void) {
    float x[WINDOW_N];
    for (int i=0; i<WINDOW_N; ++i) x[i]=1;
    Features f=extract_features(x,100);
    assert(f.mean==1 && f.variance==0 && f.peak_amplitude==0);
    for (int i=0; i<WINDOW_N; ++i) x[i]=1+0.4f*sinf(2*PI*8*i/WINDOW_N);
    f=extract_features(x,100);
    assert(fabsf(f.mean-1)<1e-6f);
    assert(fabsf(f.variance-0.08f*WINDOW_N/(WINDOW_N-1))<1e-5f);
    assert(fabsf(f.stddev-sqrtf(f.variance))<1e-6f);
    assert(fabsf(f.min-0.6f)<1e-6f && fabsf(f.max-1.4f)<1e-6f);
    assert(fabsf(f.peak_hz-3.125f)<1e-6f);
    assert(fabsf(f.peak_amplitude-0.4f)<0.001f);
    for (int i=0; i<WINDOW_N; ++i) x[i]=(i%2 ? -0.25f : 0.25f);
    f=extract_features(x,100);
    assert(fabsf(f.peak_hz-50)<1e-6f);
    assert(fabsf(f.peak_amplitude-0.25f)<0.001f);
    puts("PASS: constant signal, biased sine statistics/FFT, Nyquist scaling");
}
