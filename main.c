#include "icm20948.h"
#include "dsp_features.h"

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/multicore.h"
#include "pico/util/queue.h"
#include "pico/cyw43_arch.h"

#include "ff.h"
#include "sd_card.h"
#include "f_util.h"
#include "hw_config.h"

typedef struct {
    int16_t ax, ay, az;
    int16_t gx, gy, gz;
    uint32_t t_us;
} Sample;

static queue_t sample_q;
static FATFS fs;
static bool sd_card_mounted = false;
static FIL raw_file;
static FIL feat_file;

// Core 1: Dedicated sensor reader running strictly at 500 Hz (2000 us interval)
static void core1_reader(void)
{
    absolute_time_t target = get_absolute_time();
    while (1) {
        IMU_ST_SENSOR_DATA stGyroRawData, stAccelRawData;

        target = delayed_by_us(target, 2000);
        uint32_t t_now = (uint32_t)time_us_64();

        imuDataAccGyrGet(&stGyroRawData, &stAccelRawData);

        Sample s = {
            .ax = stAccelRawData.s16X,
            .ay = stAccelRawData.s16Y,
            .az = stAccelRawData.s16Z,
            .gx = stGyroRawData.s16X,
            .gy = stGyroRawData.s16Y,
            .gz = stGyroRawData.s16Z,
            .t_us = t_now
        };

        queue_add_blocking(&sample_q, &s);
        sleep_until(target);
    }
}

static void blink_heartbeat(bool* state)
{
    *state = !(*state);
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, *state);
}

int main(void)
{
    stdio_init_all();

    // Small delay to allow USB serial connection when monitoring
    sleep_ms(1500);

    if (cyw43_arch_init()) {
        printf("[WARN] Failed to initialize CYW43 architecture\n");
    }

    printf("\n========================================================\n");
    printf("  IAS0360 Homework 1 - IMU DSP & Feature Extraction     \n");
    printf("  Author: Miguel Robledo | Sampling: 500 Hz | N = 256   \n");
    printf("========================================================\n\n");

    // Initialize SD card driver and filesystem
    const char *drive = "0:";
    if (sd_init_driver()) {
        sd_card_t *pSD = sd_get_by_num(0);
        if (pSD) {
            drive = sd_get_drive_prefix(pSD);
            if (!drive) drive = "0:";
        }
        FRESULT fr = f_mount(&fs, drive, 1);
        if (fr == FR_OK) {
            char raw_path[64], feat_path[64];
            snprintf(raw_path, sizeof(raw_path), "%s/imu_data.bin", drive);
            snprintf(feat_path, sizeof(feat_path), "%s/imu_features.csv", drive);

            FRESULT fr_raw = f_open(&raw_file, raw_path, FA_WRITE | FA_CREATE_ALWAYS);
            FRESULT fr_feat = f_open(&feat_file, feat_path, FA_WRITE | FA_CREATE_ALWAYS);

            if (fr_raw == FR_OK && fr_feat == FR_OK) {
                sd_card_mounted = true;
                printf("[SD] Mounted successfully on %s\n", drive);

                // Write CSV header
                const char* header = "window_id,t_sec,ax_mean,ay_mean,az_mean,"
                                     "amag_mean,amag_std,amag_min,amag_max,amag_range,amag_median,amag_rms,"
                                     "f_dom_hz,spectral_energy,"
                                     "p1_hz,p1_amp,p2_hz,p2_amp,p3_hz,p3_amp,"
                                     "q15_snr_db,q15_rms_err,q15_clips\n";
                UINT bw;
                f_write(&feat_file, header, strlen(header), &bw);
                f_sync(&feat_file);
            } else {
                printf("[SD WARN] Could not open files on SD card: %d / %d\n", fr_raw, fr_feat);
            }
        } else {
            printf("[SD INFO] No SD card mounted (fr=%d). Running in USB streaming mode.\n", fr);
        }
    } else {
        printf("[SD INFO] SD driver not present. Running in USB streaming mode.\n");
    }

    // Initialize ICM-20948 sensor
    IMU_EN_SENSOR_TYPE type;
    imuInit(&type);
    if (IMU_EN_SENSOR_TYPE_ICM20948 == type) {
        printf("[IMU] Motion sensor ICM-20948 initialized successfully!\n");
    } else {
        printf("[IMU WARN] Sensor init returned type %d. Running in continuous mode.\n", type);
    }

    // Initialize inter-core queue
    queue_init(&sample_q, sizeof(Sample), 512);

    // Launch Core 1 sampling thread
    multicore_launch_core1(core1_reader);
    printf("[CORE1] Launched IMU sampling task @ 500 Hz\n");

    // Working buffers for Core 0 processing
    static Sample raw_window[IMU_WINDOW_SIZE];
    static float ax_arr[IMU_WINDOW_SIZE];
    static float ay_arr[IMU_WINDOW_SIZE];
    static float az_arr[IMU_WINDOW_SIZE];
    static float amag_arr[IMU_WINDOW_SIZE];
    static float norm_amag[IMU_WINDOW_SIZE];
    static int16_t q15_arr[IMU_WINDOW_SIZE];
    static float fft_mag_spectrum[IMU_WINDOW_SIZE / 2 + 1];

    uint16_t sample_count = 0;
    uint32_t window_idx = 0;
    bool led_state = false;
    uint32_t t_last_report = (uint32_t)time_us_64();

    printf("\n[DSP] Starting real-time feature extraction pipeline...\n\n");

    while (1) {
        Sample s;
        queue_remove_blocking(&sample_q, &s);

        raw_window[sample_count] = s;
        sample_count++;

        if (sample_count == IMU_WINDOW_SIZE) {
            uint32_t t_now = (uint32_t)time_us_64();
            float dt_sec = (float)(t_now - t_last_report) / 1000000.0f;
            t_last_report = t_now;
            float actual_rate_hz = (dt_sec > 0.0f) ? ((float)IMU_WINDOW_SIZE / dt_sec) : 0.0f;

            // 1. Convert units and compute acceleration magnitude
            for (int i = 0; i < IMU_WINDOW_SIZE; i++) {
                float gx_dps, gy_dps, gz_dps;
                imu_convert_units(raw_window[i].ax, raw_window[i].ay, raw_window[i].az,
                                  raw_window[i].gx, raw_window[i].gy, raw_window[i].gz,
                                  &ax_arr[i], &ay_arr[i], &az_arr[i], &amag_arr[i],
                                  &gx_dps, &gy_dps, &gz_dps);
                norm_amag[i] = (amag_arr[i] / 2.0f) - 0.5f;
            }

            // 2. Statistical Feature Extraction
            imu_stats_t stats_amag, stats_ax, stats_ay, stats_az;
            dsp_compute_stats(amag_arr, IMU_WINDOW_SIZE, &stats_amag);
            dsp_compute_stats(ax_arr, IMU_WINDOW_SIZE, &stats_ax);
            dsp_compute_stats(ay_arr, IMU_WINDOW_SIZE, &stats_ay);
            dsp_compute_stats(az_arr, IMU_WINDOW_SIZE, &stats_az);

            // 3. Frequency-Domain Feature Extraction (Hamming + Radix-2 FFT)
            imu_fft_result_t fft_result;
            dsp_extract_fft_features(amag_arr, IMU_WINDOW_SIZE, IMU_FS_HZ, &fft_result, fft_mag_spectrum);

            // 4. Fixed-Point Q15 Quantization & Distortion Analysis
            quant_eval_t quant_eval;
            int clips = 0;
            dsp_quantize_q15(norm_amag, IMU_WINDOW_SIZE, q15_arr, &clips);
            quant_eval.clip_count = clips;
            dsp_evaluate_quantization(norm_amag, q15_arr, IMU_WINDOW_SIZE, &quant_eval);

            // 5. USB Serial Telemetry Output
            printf("----------------------------------------------------------------------------------\n");
            printf("[WINDOW #%lu | t = %.2f s | Rate = %.1f Hz]\n",
                   (unsigned long)window_idx, (float)s.t_us / 1000000.0f, actual_rate_hz);
            printf("  Orientation / Mean (g) : ax=%+.3f, ay=%+.3f, az=%+.3f\n",
                   stats_ax.mean, stats_ay.mean, stats_az.mean);
            printf("  Magnitude Stats (g)    : mean=%.3f, std=%.4f, min=%.3f, max=%.3f, range=%.3f, rms=%.3f\n",
                   stats_amag.mean, stats_amag.stddev, stats_amag.min_val, stats_amag.max_val, stats_amag.range, stats_amag.rms);
            printf("  Spectral Features (FFT): Dom=%.1f Hz | Energy=%.3e | Top Peaks:\n",
                   fft_result.dominant_freq_hz, fft_result.spectral_energy);
            for (int k = 0; k < 3 && k < fft_result.peak_count; k++) {
                printf("    Peak %d: %6.1f Hz (amp = %.4f g)\n",
                       k + 1, fft_result.peaks[k].freq_hz, fft_result.peaks[k].amplitude);
            }
            printf("  Q15 Quantization       : SNR=%.2f dB | RMS_err=%.6f | Compression=2.0x (%d -> %d B)\n",
                   quant_eval.snr_db, quant_eval.rms_err, quant_eval.original_bytes, quant_eval.quantized_bytes);

            // 6. SD Card Logging (if available)
            if (sd_card_mounted) {
                UINT bw;
                f_write(&raw_file, raw_window, sizeof(raw_window), &bw);
                f_sync(&raw_file);

                char csv_buf[256];
                int len = snprintf(csv_buf, sizeof(csv_buf),
                    "%lu,%.3f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,"
                    "%.2f,%.4e,"
                    "%.2f,%.4f,%.2f,%.4f,%.2f,%.4f,"
                    "%.2f,%.6f,%d\n",
                    (unsigned long)window_idx, (float)s.t_us / 1000000.0f,
                    stats_ax.mean, stats_ay.mean, stats_az.mean,
                    stats_amag.mean, stats_amag.stddev, stats_amag.min_val, stats_amag.max_val,
                    stats_amag.range, stats_amag.median, stats_amag.rms,
                    fft_result.dominant_freq_hz, fft_result.spectral_energy,
                    fft_result.peaks[0].freq_hz, fft_result.peaks[0].amplitude,
                    fft_result.peaks[1].freq_hz, fft_result.peaks[1].amplitude,
                    fft_result.peaks[2].freq_hz, fft_result.peaks[2].amplitude,
                    quant_eval.snr_db, quant_eval.rms_err, quant_eval.clip_count);

                f_write(&feat_file, csv_buf, len, &bw);
                f_sync(&feat_file);
            }

            blink_heartbeat(&led_state);
            sample_count = 0;
            window_idx++;
        }
    }

    return 0;
}
