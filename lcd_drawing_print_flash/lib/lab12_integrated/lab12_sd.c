#include "lab12_sd.h"
#include "lab12_filters.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static FRESULT write_all(FIL *f, const void *data, UINT len) {
    UINT bw = 0;
    FRESULT fr = f_write(f, data, len, &bw);
    if (fr != FR_OK) return fr;
    return (bw == len) ? FR_OK : FR_DISK_ERR;
}

static void make_path(char *out, size_t out_sz, const char *base, const char *suffix) {
    snprintf(out, out_sz, "%s%s", base, suffix);
}

static FRESULT write_text_line(FIL *f, const char *line) {
    return write_all(f, line, (UINT)strlen(line));
}

FRESULT lab12_save_statistics(const char *base_path, const uint8_t *shadow, size_t n) {
    lab12_stats_t s;
    lab12_statistics_shadow(shadow, n, &s);

    char path[256];
    make_path(path, sizeof(path), base_path, "_stats.csv");

    FIL f;
    FRESULT fr = f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) return fr;

    char line[160];
    fr = write_text_line(&f, "metric,value\n");
    if (fr == FR_OK) { snprintf(line, sizeof(line), "min,%.9g\n", s.min); fr = write_text_line(&f, line); }
    if (fr == FR_OK) { snprintf(line, sizeof(line), "max,%.9g\n", s.max); fr = write_text_line(&f, line); }
    if (fr == FR_OK) { snprintf(line, sizeof(line), "mean,%.9g\n", s.mean); fr = write_text_line(&f, line); }
    if (fr == FR_OK) { snprintf(line, sizeof(line), "median,%.9g\n", s.median); fr = write_text_line(&f, line); }
    if (fr == FR_OK) { snprintf(line, sizeof(line), "mode,%.9g\n", s.mode); fr = write_text_line(&f, line); }
    if (fr == FR_OK) { snprintf(line, sizeof(line), "mode_count,%lu\n", (unsigned long)s.mode_count); fr = write_text_line(&f, line); }
    if (fr == FR_OK) { snprintf(line, sizeof(line), "variance,%.9g\n", s.variance); fr = write_text_line(&f, line); }
    if (fr == FR_OK) { snprintf(line, sizeof(line), "stddev,%.9g\n", s.stddev); fr = write_text_line(&f, line); }

    if (fr == FR_OK) fr = f_sync(&f);
    FRESULT fc = f_close(&f);
    return (fr != FR_OK) ? fr : fc;
}

static FRESULT write_float_file(const char *path, const uint8_t *shadow, size_t n,
                                int bits, int dequantized) {
    FIL f;
    FRESULT fr = f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) return fr;

    float buf[64];
    size_t pos = 0;
    while (pos < n && fr == FR_OK) {
        size_t count = n - pos;
        if (count > 64) count = 64;
        for (size_t i = 0; i < count; ++i) {
            float x = lab12_gray_to_normalized(lab12_shadow_to_gray(shadow[pos + i]));
            if (dequantized) {
                int32_t q = lab12_quantize_signed(x, bits, NULL);
                x = lab12_dequantize_signed(q, bits);
            }
            buf[i] = x;
        }
        fr = write_all(&f, buf, (UINT)(count * sizeof(float)));
        pos += count;
    }

    if (fr == FR_OK) fr = f_sync(&f);
    FRESULT fc = f_close(&f);
    return (fr != FR_OK) ? fr : fc;
}

static FRESULT write_q16_file(const char *path, const uint8_t *shadow, size_t n) {
    FIL f;
    FRESULT fr = f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) return fr;

    int16_t buf[64];
    size_t pos = 0;
    while (pos < n && fr == FR_OK) {
        size_t count = n - pos;
        if (count > 64) count = 64;
        for (size_t i = 0; i < count; ++i) {
            float x = lab12_gray_to_normalized(lab12_shadow_to_gray(shadow[pos + i]));
            buf[i] = (int16_t)lab12_quantize_signed(x, 16, NULL);
        }
        fr = write_all(&f, buf, (UINT)(count * sizeof(int16_t)));
        pos += count;
    }
    if (fr == FR_OK) fr = f_sync(&f);
    FRESULT fc = f_close(&f);
    return (fr != FR_OK) ? fr : fc;
}

static FRESULT write_q8_file(const char *path, const uint8_t *shadow, size_t n) {
    FIL f;
    FRESULT fr = f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) return fr;

    int8_t buf[128];
    size_t pos = 0;
    while (pos < n && fr == FR_OK) {
        size_t count = n - pos;
        if (count > 128) count = 128;
        for (size_t i = 0; i < count; ++i) {
            float x = lab12_gray_to_normalized(lab12_shadow_to_gray(shadow[pos + i]));
            buf[i] = (int8_t)lab12_quantize_signed(x, 8, NULL);
        }
        fr = write_all(&f, buf, (UINT)count);
        pos += count;
    }
    if (fr == FR_OK) fr = f_sync(&f);
    FRESULT fc = f_close(&f);
    return (fr != FR_OK) ? fr : fc;
}

static FRESULT write_q4_file(const char *path, const uint8_t *shadow, size_t n) {
    FIL f;
    FRESULT fr = f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) return fr;

    uint8_t buf[128];
    size_t pos = 0;
    while (pos < n && fr == FR_OK) {
        size_t out_count = 0;
        while (pos < n && out_count < sizeof(buf)) {
            float x0 = lab12_gray_to_normalized(lab12_shadow_to_gray(shadow[pos++]));
            int8_t q0 = (int8_t)lab12_quantize_signed(x0, 4, NULL);
            int8_t q1 = 0;
            if (pos < n) {
                float x1 = lab12_gray_to_normalized(lab12_shadow_to_gray(shadow[pos++]));
                q1 = (int8_t)lab12_quantize_signed(x1, 4, NULL);
            }
            buf[out_count++] = lab12_pack_q4(q0, q1);
        }
        fr = write_all(&f, buf, (UINT)out_count);
    }
    if (fr == FR_OK) fr = f_sync(&f);
    FRESULT fc = f_close(&f);
    return (fr != FR_OK) ? fr : fc;
}

FRESULT lab12_save_quantization(const char *base_path, const uint8_t *shadow, size_t n) {
    if (!base_path || !shadow || n == 0) return FR_INVALID_PARAMETER;

    char path[256];
    FRESULT fr;

    make_path(path, sizeof(path), base_path, "_norm_f32.bin");
    fr = write_float_file(path, shadow, n, 16, 0);
    if (fr != FR_OK) return fr;

    make_path(path, sizeof(path), base_path, "_q16.bin");
    fr = write_q16_file(path, shadow, n);
    if (fr != FR_OK) return fr;
    make_path(path, sizeof(path), base_path, "_dq16_f32.bin");
    fr = write_float_file(path, shadow, n, 16, 1);
    if (fr != FR_OK) return fr;

    make_path(path, sizeof(path), base_path, "_q8.bin");
    fr = write_q8_file(path, shadow, n);
    if (fr != FR_OK) return fr;
    make_path(path, sizeof(path), base_path, "_dq8_f32.bin");
    fr = write_float_file(path, shadow, n, 8, 1);
    if (fr != FR_OK) return fr;

    make_path(path, sizeof(path), base_path, "_q4.bin");
    fr = write_q4_file(path, shadow, n);
    if (fr != FR_OK) return fr;
    make_path(path, sizeof(path), base_path, "_dq4_f32.bin");
    fr = write_float_file(path, shadow, n, 4, 1);
    if (fr != FR_OK) return fr;

    lab12_quant_metrics_t q16, q8, q4;
    lab12_quant_metrics_shadow(shadow, n, 16, &q16);
    lab12_quant_metrics_shadow(shadow, n, 8, &q8);
    lab12_quant_metrics_shadow(shadow, n, 4, &q4);

    make_path(path, sizeof(path), base_path, "_quant_metrics.csv");
    FIL f;
    fr = f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) return fr;

    fr = write_text_line(&f, "bits,rmse,max_abs_error,snr_db,clip_count,stored_bytes\n");
    char line[192];
    const lab12_quant_metrics_t *arr[3] = {&q16, &q8, &q4};
    const unsigned long sizes[3] = {(unsigned long)(n * 2u), (unsigned long)n, (unsigned long)((n + 1u) / 2u)};
    for (int i = 0; i < 3 && fr == FR_OK; ++i) {
        const lab12_quant_metrics_t *q = arr[i];
        if (isinf(q->snr_db)) {
            snprintf(line, sizeof(line), "%d,%.9g,%.9g,inf,%lu,%lu\n",
                     q->bits, q->rmse, q->max_abs_error,
                     (unsigned long)q->clip_count, sizes[i]);
        } else {
            snprintf(line, sizeof(line), "%d,%.9g,%.9g,%.9g,%lu,%lu\n",
                     q->bits, q->rmse, q->max_abs_error, q->snr_db,
                     (unsigned long)q->clip_count, sizes[i]);
        }
        fr = write_text_line(&f, line);
    }

    if (fr == FR_OK) fr = f_sync(&f);
    FRESULT fc = f_close(&f);
    return (fr != FR_OK) ? fr : fc;
}

FRESULT lab12_save_fft(const char *base_path, const uint8_t *shadow, int w, int h) {
    float raw[LAB12_FFT_N];
    float mag[LAB12_FFT_BINS];
    int rc = lab12_fft_shadow_row(shadow, w, h, h / 2, raw, mag);
    if (rc != 0) return FR_INVALID_PARAMETER;

    char path[256];
    FIL f;
    FRESULT fr;
    char line[160];

    make_path(path, sizeof(path), base_path, "_fft_raw.txt");
    fr = f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) return fr;
    fr = write_text_line(&f, "sample_count=256,sample_spacing_px=1\n");
    if (fr == FR_OK) {
        for (int i = 0; i < LAB12_FFT_N; ++i) {
            snprintf(line, sizeof(line), "%s%.9g", (i ? "," : ""), raw[i]);
            fr = write_text_line(&f, line);
            if (fr != FR_OK) break;
        }
    }
    if (fr == FR_OK) fr = write_text_line(&f, "\n");
    if (fr == FR_OK) fr = f_sync(&f);
    FRESULT fc = f_close(&f);
    if (fr != FR_OK) return fr;
    if (fc != FR_OK) return fc;

    make_path(path, sizeof(path), base_path, "_fft_mcu.txt");
    fr = f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) return fr;
    fr = write_text_line(&f, "bin,freq_cycles_per_pixel,amp\n");
    for (int k = 0; k < LAB12_FFT_BINS && fr == FR_OK; ++k) {
        const float freq = (float)k / (float)LAB12_FFT_N;
        snprintf(line, sizeof(line), "%d,%.9g,%.9g\n", k, freq, mag[k]);
        fr = write_text_line(&f, line);
    }
    if (fr == FR_OK) fr = f_sync(&f);
    fc = f_close(&f);
    return (fr != FR_OK) ? fr : fc;
}

static void le16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
}

static void le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
    p[2] = (uint8_t)((v >> 16) & 0xffu);
    p[3] = (uint8_t)((v >> 24) & 0xffu);
}

FRESULT lab12_save_sobel_bmp(const char *base_path, const uint8_t *shadow, int w, int h) {
    if (!base_path || !shadow || w <= 0 || h <= 0 || w > 480) return FR_INVALID_PARAMETER;

    char path[256];
    make_path(path, sizeof(path), base_path, "_sobel.bmp");

    const uint32_t row_raw = (uint32_t)w * 3u;
    const uint32_t padding = (4u - (row_raw % 4u)) % 4u;
    const uint32_t stride = row_raw + padding;
    const uint32_t pixel_bytes = stride * (uint32_t)h;
    const uint32_t file_size = 54u + pixel_bytes;

    uint8_t header[54] = {0};
    header[0] = 'B'; header[1] = 'M';
    le32(&header[2], file_size);
    le32(&header[10], 54u);
    le32(&header[14], 40u);
    le32(&header[18], (uint32_t)w);
    le32(&header[22], (uint32_t)h);
    le16(&header[26], 1u);
    le16(&header[28], 24u);
    le32(&header[34], pixel_bytes);

    FIL f;
    FRESULT fr = f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) return fr;
    fr = write_all(&f, header, sizeof(header));

    uint8_t row[480 * 3 + 3];
    for (int y = h - 1; y >= 0 && fr == FR_OK; --y) {
        for (int x = 0; x < w; ++x) {
            uint8_t v = lab12_sobel_shadow_pixel(shadow, w, h, x, y);
            row[x * 3 + 0] = v;
            row[x * 3 + 1] = v;
            row[x * 3 + 2] = v;
        }
        for (uint32_t p = 0; p < padding; ++p) row[row_raw + p] = 0;
        fr = write_all(&f, row, (UINT)stride);
    }

    if (fr == FR_OK) fr = f_sync(&f);
    FRESULT fc = f_close(&f);
    return (fr != FR_OK) ? fr : fc;
}
