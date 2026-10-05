// New application glue using IAS0360 Lab 1.1 and Lab 1.2 course functions.
// Integration, adaptations and documentation developed with AI assistance.
#include <stdio.h>
#include "icm20948.h"
#include "signal_processing.h"

#define SAMPLE_PERIOD_US 10000
#define FULL_SCALE_G 2.0f
// At the ±2 g setting, 16384 sensor counts correspond to 1 g.
#define COUNTS_PER_G 16384.0f

// Reuse the same buffers for each block to keep memory use predictable.
static float samples[N], centered[N], normalized[N], reconstructed[N];
static int16_t q15[N];
static int8_t q7[N], q3[N];
static c32 X[N];
static float mag[N / 2 + 1];

static float collect_samples(void) {
    absolute_time_t next = get_absolute_time();
    uint64_t first = 0, last = 0;
    // The first eight readings replace old values in the driver's moving average.
    for (int i = -8; i < N; i++) {
        sleep_until(next);
        int16_t ax, ay, az;
        uint64_t now = time_us_64();
        icm20948AccelFastRead(&ax, &ay, &az);
        if (absolute_time_diff_us(next, get_absolute_time()) >= SAMPLE_PERIOD_US)
            return 0.0f; // Discard a block with a missed sampling interval.
        if (i >= 0) {
            samples[i] = (float)ax / COUNTS_PER_G;
            if (i == 0) first = now;
            last = now;
        }
        // Keep the 10 ms schedule instead of adding a delay after each read.
        next = delayed_by_us(next, SAMPLE_PERIOD_US);
    }
    return (float)(N - 1) * 1000000.0f / (float)(last - first);
}

static void print_statistics(const char* label, const float* x) {
    float mean = mean_f32(x, N);
    float variance = variance_f32(x, N, mean);
    float mn, mx;
    min_max_f32(x, N, &mn, &mx);
    printf("%s: min=%.6f g max=%.6f g mean=%.6f g "
           "variance=%.8f g^2 stddev=%.6f g\n",
           label, mn, mx, mean, variance, stddev_f32(variance));
}

static void print_quantization(int bits, int clips) {
    float snr_db, max_abs_err, rms_err;
    snr_and_error(normalized, reconstructed, N, &snr_db, &max_abs_err, &rms_err);
    printf("  %2d | %.8f | %.8f | %8.2f | %d\n",
           bits, rms_err * FULL_SCALE_G, max_abs_err * FULL_SCALE_G, snr_db, clips);
}

static void process_block(float fs) {
    // Remove the constant offset, then scale to the range used by the quantizers.
    float mean = mean_f32(samples, N);
    for (int i = 0; i < N; i++) {
        centered[i] = samples[i] - mean;
        normalized[i] = centered[i] / FULL_SCALE_G;
    }
    print_statistics("Sensor output (8-sample average)", samples);
    print_statistics("After DC removal", centered);
    printf("Removed DC: %.6f g\n", mean);

    // As in Lab 1.2, convert each version back to float to compare what was lost.
    int clips;
    printf("Quantization: centered signal / 2 g; Q3 stored in int8_t\n");
    printf("bits | RMSE [g]  | max error [g] | SNR [dB] | clipped\n");
    quantize_q15(normalized, N, q15, &clips);
    dequantize_q15(q15, N, reconstructed);
    print_quantization(16, clips);
    quantize_q7(normalized, N, q7, &clips);
    dequantize_q7(q7, N, reconstructed);
    print_quantization(8, clips);
    quantize_q3(normalized, N, q3, &clips);
    dequantize_q3(q3, N, reconstructed);
    print_quantization(4, clips);

    // Follow the course FFT example using the centered float samples.
    // We can apply the window here because the earlier comparisons are finished.
    hamming_window(centered, N);
    for(int i=0;i<N;i++){ X[i].re = centered[i]; X[i].im = 0.0f; }
    if (fft_radix2(X, N, +1) != 0) {
        printf("FFT error: sample count must be a power of two\n");
        return;
    }
    fft_mag(X, N / 2 + 1, mag);
    const float scale = (2.0f / (float)N) / 0.54f;
    for(int k=0;k<=N/2;k++) mag[k] *= scale;
    // DC and Nyquist have no separate negative-frequency partner.
    mag[0] *= 0.5f;
    mag[N / 2] *= 0.5f;

    int idx[TOP_K], found;
    float val[TOP_K];
    // Several neighbouring bins may belong to the same movement.
    top_k_peaks(mag, N/2+1, 1, TOP_K, idx, val, &found);
    printf("Dominant frequencies: strongest bins excluding DC, df=%.6f Hz\n",
           fs / (float)N);
    for (int i = 0; i < found; i++)
        printf("  %d: bin=%d frequency=%.4f Hz amplitude~=%.6f g\n",
               i + 1, idx[i], (float)idx[i] * fs / (float)N, val[i]);
}

int main(void) {
    stdio_init_all();
    // Wait for the terminal so the sensor startup message is not missed.
    while (!stdio_usb_connected()) sleep_ms(100);
    printf("IAS0360 Home Assignment 1: IMU signal analysis\n");
    printf("Input: ICM-20948 X, I2C1 GP6/GP7, 0x68, +/-2 g\n");
    printf("Target: 100 Hz, %d samples; course driver uses 8-sample averaging\n", N);

    IMU_EN_SENSOR_TYPE type;
    imuInit(&type);
    if (type != IMU_EN_SENSOR_TYPE_ICM20948) {
        while (true) {
            printf("Error: ICM-20948 not detected; check wiring and restart\n");
            sleep_ms(2000);
        }
    }
    printf("ICM-20948 detected (WHO-AM-I); initialization writes are unchecked\n");

    unsigned long block = 0;
    while (true) {
        float fs = collect_samples();
        if (fs == 0.0f) {
            printf("Sampling deadline missed; block discarded\n");
            continue;
        }
        printf("\n=== Block %lu: N=%d, measured polling rate=%.3f Hz ===\n",
               ++block, N, fs);
        process_block(fs);
    }
}
