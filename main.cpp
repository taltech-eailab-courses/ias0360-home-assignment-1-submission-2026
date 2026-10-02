#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/sync.h"

#include "ff.h"
#include "sd_card.h"
#include "f_util.h"
#include "hw_config.h"
#include "icm20948/icm20948.h"

#define PATH_MAX_LEN 256
#define SAMPLE_PERIOD_US 2000
#define WRITE_BATCH_SIZE 32
#define RAW_RECORD_LIMIT 3000

#define APP_LED_INIT 6
#define APP_LED_LOGGING 7
#define APP_LED_ERROR 8

#define STARTUP_SKIP 20u
#define CALIBRATION_SAMPLES 40u
#define FEATURE_N 20u
#define FEATURE_HOP 10u
#define ALPHA_035 0.35f
#define ALPHA_015 0.15f
#define RAW_SCALE 32768.0f

#define FFT_N 64u
#define FFT_BLOCKS_MAX ((RAW_RECORD_LIMIT - STARTUP_SKIP - CALIBRATION_SAMPLES) / FFT_N)
#define FFT_PI 3.14159265358979323846f

typedef struct {
    uint64_t sensor_timestamp_us;
    int16_t ax, ay, az;
    int16_t gx, gy, gz;
    int16_t mx, my, mz;
} latest_imu_t;

typedef struct {
    uint64_t storage_timestamp_us;
    uint64_t sensor_timestamp_us;
    int16_t ax, ay, az;
    int16_t gx, gy, gz;
    int16_t mx, my, mz;
} imu_sample_t;

typedef struct {
    uint64_t first_us, last_us;
    unsigned first_index, last_index;
    float baseline, raw_peak, raw_rms, raw_variance;
    float lp035_peak, lp035_rms, lp035_variance;
    float lp015_peak, lp015_rms, lp015_variance;
    float gyro_peak_raw;
    uint64_t compute_us;
} feature_row_t;

typedef struct { float re, im; } fft_complex_t;
typedef struct {
    unsigned block, start_index, end_index;
    uint64_t first_us, last_us, fft_us;
    float fs_hz, peak_hz, peak_magnitude;
    float magnitude[FFT_N / 2u + 1u];
} fft_row_t;

static feature_row_t feature_rows[(RAW_RECORD_LIMIT - STARTUP_SKIP - CALIBRATION_SAMPLES) / FEATURE_HOP];
static float raw_ring[FEATURE_N], a_ring[FEATURE_N], b_ring[FEATURE_N], gyro_ring[FEATURE_N];
static uint64_t t_ring[FEATURE_N];
static unsigned feature_count = 0, ring_written = 0, since_feature = 0;
static double baseline_sum = 0.0;
static float baseline = 0.0f, state_a = 0.0f, state_b = 0.0f;

static latest_imu_t latest_imu = {};
static mutex_t latest_imu_mutex;
static FATFS fs;
static FIL raw_file;
static FIL feature_file;

static FIL fft_file;
static float fft_input[FFT_N];
static fft_complex_t fft_work[FFT_N];
static fft_row_t fft_rows[FFT_BLOCKS_MAX];
static unsigned fft_filled = 0, fft_count = 0;
static uint64_t fft_first_us = 0;
static unsigned fft_first_index = 0;

static sd_card_t *g_sd = NULL;
static const char *g_drive = NULL;
static volatile bool sd_failed = false;

static void fatal_message(const char *message) {
    gpio_put(APP_LED_ERROR, 1);
    sd_failed = true;
    printf("FATAL: %s\n", message);
    while (true) sleep_ms(100);
}

static void fatal(const char *context, FRESULT fr) {
    gpio_put(APP_LED_ERROR, 1);
    sd_failed = true;
    printf("FATAL: %s: %s (%d)\n", context, FRESULT_str(fr), (int)fr);
    while (true) sleep_ms(100);
}

static void join_path(char *out, size_t size, const char *drive, const char *name) {
    if (name[0] == '/') ++name;
    size_t dlen = strlen(drive);
    snprintf(out, size, "%s%s%s", drive,
             dlen && drive[dlen - 1] == '/' ? "" : "/", name);
}

static bool sd_init_and_mount(void) {
    if (!sd_init_driver()) return false;
    g_sd = sd_get_by_num(0);
    if (!g_sd) return false;
    g_drive = sd_get_drive_prefix(g_sd);
    if (!g_drive) return false;

    FRESULT fr = f_mount(&fs, g_drive, 1);
    printf("f_mount -> %s (%d)\n", FRESULT_str(fr), (int)fr);
    return fr == FR_OK;
}

static void checked_write(FIL *file, const void *data, UINT length, const char *what) {
    UINT written = 0;
    FRESULT fr = f_write(file, data, length, &written);
    if (fr != FR_OK || written != length) fatal(what, fr);
}

static void checked_close(FIL *file, const char *what) {
    FRESULT fr = f_close(file);
    if (fr != FR_OK) fatal(what, fr);
}

static void copy_latest_sample(imu_sample_t *sample) {
    mutex_enter_blocking(&latest_imu_mutex);
    sample->storage_timestamp_us = time_us_64();
    sample->sensor_timestamp_us = latest_imu.sensor_timestamp_us;
    sample->ax = latest_imu.ax;
    sample->ay = latest_imu.ay;
    sample->az = latest_imu.az;
    sample->gx = latest_imu.gx;
    sample->gy = latest_imu.gy;
    sample->gz = latest_imu.gz;
    sample->mx = latest_imu.mx;
    sample->my = latest_imu.my;
    sample->mz = latest_imu.mz;
    mutex_exit(&latest_imu_mutex);
}

static void ring_stats(const float *x, float *peak, float *rms, float *variance) {
    double sum = 0.0, sum_sq = 0.0;
    *peak = 0.0f;
    for (unsigned i = 0; i < FEATURE_N; ++i) {
        float v = x[i];
        sum += v;
        sum_sq += (double)v * v;
        if (fabsf(v) > *peak) *peak = fabsf(v);
    }
    double centered = sum_sq - sum * sum / FEATURE_N;
    if (centered < 0.0) centered = 0.0;
    *rms = (float)sqrt(sum_sq / FEATURE_N);
    *variance = (float)(centered / (FEATURE_N - 1u));
}

static void finish_fft_block(unsigned last_index, uint64_t last_us);

static void export_fft(void);

static void process_accepted_sample(const imu_sample_t *s, unsigned index) {
    unsigned accepted = index + 1u;
    if (accepted <= STARTUP_SKIP) return;
    if (accepted <= STARTUP_SKIP + CALIBRATION_SAMPLES) {
        baseline_sum += (float)s->az / RAW_SCALE;
        if (accepted == STARTUP_SKIP + CALIBRATION_SAMPLES) {
            baseline = (float)(baseline_sum / CALIBRATION_SAMPLES);
            printf("Calibration complete: baseline=%.9g\n", (double)baseline);
        }
        return;
    }

    float x = (float)s->az / RAW_SCALE - baseline;

    if (fft_filled == 0u) {
        fft_first_index = index;
        fft_first_us = s->sensor_timestamp_us;
    }
    fft_input[fft_filled++] = x;
    if (fft_filled == FFT_N) {
        finish_fft_block(index, s->sensor_timestamp_us);
        fft_filled = 0u;
    }

    state_a += ALPHA_035 * (x - state_a);
    state_b += ALPHA_015 * (x - state_b);
    unsigned pos = ring_written % FEATURE_N;
    raw_ring[pos] = x;
    a_ring[pos] = state_a;
    b_ring[pos] = state_b;
    float gx = (float)s->gx, gy = (float)s->gy, gz = (float)s->gz;
    gyro_ring[pos] = sqrtf(gx * gx + gy * gy + gz * gz);
    t_ring[pos] = s->sensor_timestamp_us;
    ++ring_written;
    ++since_feature;

    if (ring_written < FEATURE_N || since_feature < FEATURE_HOP) return;
    if (feature_count >= sizeof(feature_rows) / sizeof(feature_rows[0]))
        fatal_message("Feature buffer full");
    uint64_t start = time_us_64();
    feature_row_t *r = &feature_rows[feature_count++];
    r->first_index = index - FEATURE_N + 1u;
    r->last_index = index;
    r->first_us = t_ring[ring_written % FEATURE_N];
    r->last_us = s->sensor_timestamp_us;
    r->baseline = baseline;
    ring_stats(raw_ring, &r->raw_peak, &r->raw_rms, &r->raw_variance);
    ring_stats(a_ring, &r->lp035_peak, &r->lp035_rms, &r->lp035_variance);
    ring_stats(b_ring, &r->lp015_peak, &r->lp015_rms, &r->lp015_variance);
    r->gyro_peak_raw = 0.0f;
    for (unsigned i = 0; i < FEATURE_N; ++i)
        if (gyro_ring[i] > r->gyro_peak_raw) r->gyro_peak_raw = gyro_ring[i];
    r->compute_us = time_us_64() - start;
    since_feature = 0;
}

static void export_features(void) {
    char path[PATH_MAX_LEN], line[384];
    join_path(path, sizeof(path), g_drive, "road_features_mcu.csv");
    printf("Opening feature CSV after closing raw CSV\n");
    FIL *features = &feature_file;
    printf("LOG: feature f_open begin\n");
    FRESULT fr = f_open(features, path, FA_WRITE | FA_CREATE_ALWAYS);
    printf("LOG: feature f_open returned %d\n", (int)fr);
    if (fr != FR_OK) fatal("open road_features_mcu.csv", fr);
    const char *header =
        "feature_index,start_index,end_index,start_sensor_timestamp_us,end_sensor_timestamp_us,"
        "baseline_normalized,raw_peak,raw_rms,raw_variance,"
        "lp035_peak,lp035_rms,lp035_variance,lp015_peak,lp015_rms,lp015_variance,"
        "gyro_peak_raw,compute_us\n";
    checked_write(features, header, (UINT)strlen(header), "feature header");
    for (unsigned i = 0; i < feature_count; ++i) {
        const feature_row_t *r = &feature_rows[i];
        int n = snprintf(line, sizeof(line),
            "%u,%u,%u,%llu,%llu,%.9g,%.9g,%.9g,%.9g,"
            "%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%llu\n",
            i, r->first_index, r->last_index,
            (unsigned long long)r->first_us,
            (unsigned long long)r->last_us,
            (double)r->baseline,
            (double)r->raw_peak, (double)r->raw_rms, (double)r->raw_variance,
            (double)r->lp035_peak, (double)r->lp035_rms, (double)r->lp035_variance,
            (double)r->lp015_peak, (double)r->lp015_rms, (double)r->lp015_variance,
            (double)r->gyro_peak_raw, (unsigned long long)r->compute_us);
        if (n < 0 || (size_t)n >= sizeof(line)) fatal_message("Feature CSV line too long");
        checked_write(features, line, (UINT)n, "feature row");
    }
    checked_close(features, "close feature CSV");
    printf("Exported %u feature rows\n", feature_count);
}

static unsigned fft_reverse_bits(unsigned value, unsigned bits) {
    unsigned result = 0;
    for (unsigned i = 0; i < bits; ++i) {
        result = (result << 1u) | (value & 1u);
        value >>= 1u;
    }
    return result;
}

static void fft_radix2_64(fft_complex_t *a) {
    for (unsigned i = 0; i < FFT_N; ++i) {
        unsigned j = fft_reverse_bits(i, 6u);
        if (j > i) {
            fft_complex_t tmp = a[i];
            a[i] = a[j];
            a[j] = tmp;
        }
    }
    for (unsigned size = 2u; size <= FFT_N; size <<= 1u) {
        float theta = -2.0f * FFT_PI / (float)size;
        float step_re = cosf(theta), step_im = sinf(theta);
        for (unsigned base = 0; base < FFT_N; base += size) {
            float wr = 1.0f, wi = 0.0f;
            for (unsigned j = 0; j < size / 2u; ++j) {
                unsigned left = base + j, right = left + size / 2u;
                float tr = wr * a[right].re - wi * a[right].im;
                float ti = wr * a[right].im + wi * a[right].re;
                float ur = a[left].re, ui = a[left].im;
                a[left].re = ur + tr;
                a[left].im = ui + ti;
                a[right].re = ur - tr;
                a[right].im = ui - ti;
                float next_wr = wr * step_re - wi * step_im;
                wi = wr * step_im + wi * step_re;
                wr = next_wr;
            }
        }
    }
}

static void finish_fft_block(unsigned last_index, uint64_t last_us) {
    if (fft_count >= FFT_BLOCKS_MAX) fatal_message("FFT results buffer full");
    fft_row_t *row = &fft_rows[fft_count];
    row->block = fft_count;
    row->start_index = fft_first_index;
    row->end_index = last_index;
    row->first_us = fft_first_us;
    row->last_us = last_us;
    if (last_us <= fft_first_us) fatal_message("FFT timestamps not increasing");
    row->fs_hz = (float)(1000000.0 * (FFT_N - 1u) / (double)(last_us - fft_first_us));

    uint64_t start = time_us_64();
    float mean = 0.0f;
    for (unsigned i = 0; i < FFT_N; ++i) mean += fft_input[i];
    mean /= (float)FFT_N;
    for (unsigned i = 0; i < FFT_N; ++i) {
        float hamming = 0.54f - 0.46f *
            cosf(2.0f * FFT_PI * (float)i / (float)(FFT_N - 1u));
        fft_work[i].re = (fft_input[i] - mean) * hamming;
        fft_work[i].im = 0.0f;
    }
    fft_radix2_64(fft_work);
    const float gain = 0.54f - 0.46f / (float)FFT_N;
    for (unsigned k = 0; k <= FFT_N / 2u; ++k) {
        float factor = (k == 0u || k == FFT_N / 2u) ? 1.0f : 2.0f;
        row->magnitude[k] = hypotf(fft_work[k].re, fft_work[k].im) *
                            factor / ((float)FFT_N * gain);
    }
    row->peak_magnitude = row->magnitude[1];
    unsigned best_bin = 1u;
    for (unsigned k = 2u; k <= FFT_N / 2u; ++k) {
        if (row->magnitude[k] > row->peak_magnitude) {
            row->peak_magnitude = row->magnitude[k];
            best_bin = k;
        }
    }
    row->peak_hz = (float)best_bin * row->fs_hz / (float)FFT_N;
    row->fft_us = time_us_64() - start;
    ++fft_count;
}

static void export_fft(void) {
    char path[PATH_MAX_LEN], line[224];
    join_path(path, sizeof(path), g_drive, "road_fft_mcu.csv");
    printf("LOG: FFT f_open begin\n");
    FIL *f = &fft_file;
    FRESULT fr = f_open(f, path, FA_WRITE | FA_CREATE_ALWAYS);
    printf("LOG: FFT f_open returned %d\n", (int)fr);
    if (fr != FR_OK) fatal("open road_fft_mcu.csv", fr);
    const char *header =
        "block,start_index,end_index,start_sensor_timestamp_us,end_sensor_timestamp_us,"
        "fs_hz,fft_us,peak_hz,peak_magnitude,bin,freq_hz,magnitude\n";
    checked_write(f, header, (UINT)strlen(header), "FFT header");
    for (unsigned i = 0; i < fft_count; ++i) {
        const fft_row_t *r = &fft_rows[i];
        for (unsigned k = 0; k <= FFT_N / 2u; ++k) {
            int n = snprintf(line, sizeof(line),
                "%u,%u,%u,%llu,%llu,%.9g,%llu,%.9g,%.9g,%u,%.9g,%.9g\n",
                r->block, r->start_index, r->end_index,
                (unsigned long long)r->first_us,
                (unsigned long long)r->last_us,
                (double)r->fs_hz, (unsigned long long)r->fft_us,
                (double)r->peak_hz, (double)r->peak_magnitude,
                k, (double)((float)k * r->fs_hz / (float)FFT_N),
                (double)r->magnitude[k]);
            if (n < 0 || (size_t)n >= sizeof(line))
                fatal_message("FFT CSV line too long");
            checked_write(f, line, (UINT)n, "FFT row");
        }
    }
    checked_close(f, "close FFT CSV");
    printf("Exported %u FFT blocks\n", fft_count);
}

void core1_entry(void) {
    if (!sd_init_and_mount()) fatal_message("SD initialization/mount failed");

    char path[PATH_MAX_LEN];
    join_path(path, sizeof(path), g_drive, "road_raw_imu.csv");

    FIL *file = &raw_file;
    FRESULT fr = f_open(file, path, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK) fatal("open road_raw_imu.csv", fr);

    const char *header =
        "index,storage_timestamp_us,sensor_timestamp_us,"
        "ax,ay,az,gx,gy,gz,mx,my,mz\n";
    checked_write(file, header, (UINT)strlen(header), "CSV header");

    gpio_put(APP_LED_LOGGING, 1);
    uint32_t count = 0;
    uint32_t buffered = 0;
    imu_sample_t buffer[WRITE_BATCH_SIZE];
    uint64_t next_storage_time_us = time_us_64();
    uint64_t previous_sensor_timestamp_us = 0;
    uint32_t unique_sensor_updates = 0;

    while (count < RAW_RECORD_LIMIT) {
        next_storage_time_us += SAMPLE_PERIOD_US;

        imu_sample_t sample = {};
        copy_latest_sample(&sample);
        if (sample.sensor_timestamp_us == 0) {
            uint64_t now = time_us_64();
            if (now < next_storage_time_us)
                sleep_us((uint32_t)(next_storage_time_us - now));
            continue;
        }
        
        if (sample.sensor_timestamp_us == previous_sensor_timestamp_us) {
            uint64_t now = time_us_64();
            if (now < next_storage_time_us)
                sleep_us((uint32_t)(next_storage_time_us - now));
            else
                next_storage_time_us = now;
            continue;
        }
        ++unique_sensor_updates;
        previous_sensor_timestamp_us = sample.sensor_timestamp_us;
        process_accepted_sample(&sample, count);
        buffer[buffered++] = sample;
        ++count;
        if ((count % 100u) == 0u)
            printf("Logged reads=%lu features=%u\n", (unsigned long)count, feature_count);

        if (buffered == WRITE_BATCH_SIZE || count == RAW_RECORD_LIMIT) {
            for (uint32_t i = 0; i < buffered; ++i) {
                char line[256];
                int n = snprintf(
                    line, sizeof(line),
                    "%lu,%llu,%llu,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
                    (unsigned long)(count - buffered + i),
                    (unsigned long long)buffer[i].storage_timestamp_us,
                    (unsigned long long)buffer[i].sensor_timestamp_us,
                    (int)buffer[i].ax, (int)buffer[i].ay, (int)buffer[i].az,
                    (int)buffer[i].gx, (int)buffer[i].gy, (int)buffer[i].gz,
                    (int)buffer[i].mx, (int)buffer[i].my, (int)buffer[i].mz);
                if (n < 0 || n >= (int)sizeof(line)) fatal_message("CSV line too long");
                checked_write(file, line, (UINT)n, "CSV row");
            }
            buffered = 0;
            fr = f_sync(file);
            if (fr != FR_OK) fatal("sync CSV", fr);
        }

        uint64_t now = time_us_64();
        if (now < next_storage_time_us)
            sleep_us((uint32_t)(next_storage_time_us - now));
        else
            next_storage_time_us = now + SAMPLE_PERIOD_US;
    }

    printf("LOG: closing raw file\n");
    checked_close(file, "close road_raw_imu.csv");
    printf("LOG: raw closed; exporting %u features and %u FFT blocks\n", feature_count, fft_count);
    export_features();
    printf("LOG: feature export completed; exporting FFT\n");
    export_fft();
    printf("LOG: FFT export completed\n");
    gpio_put(APP_LED_LOGGING, 0);
    printf("recording done, records=%lu unique_sensor_updates=%lu\n",
        (unsigned long)count, (unsigned long)unique_sensor_updates);
    while (true) sleep_ms(1000);
}

int main(void) {
    IMU_EN_SENSOR_TYPE motion_sensor_type;
    IMU_ST_ANGLES_DATA angles;
    IMU_ST_SENSOR_DATA gyro_raw, accel_raw, mag_raw;

    stdio_init_all();

    gpio_init(APP_LED_INIT);
    gpio_set_dir(APP_LED_INIT, GPIO_OUT);
    gpio_put(APP_LED_INIT, 1);
    gpio_init(APP_LED_LOGGING);
    gpio_set_dir(APP_LED_LOGGING, GPIO_OUT);
    gpio_put(APP_LED_LOGGING, 0);
    gpio_init(APP_LED_ERROR);
    gpio_set_dir(APP_LED_ERROR, GPIO_OUT);
    gpio_put(APP_LED_ERROR, 0);

    sleep_ms(5000);

    imuInit(&motion_sensor_type);
    if (motion_sensor_type != IMU_EN_SENSOR_TYPE_ICM20948)
        fatal_message("Motion sensor was not detected");

    mutex_init(&latest_imu_mutex);
    gpio_put(APP_LED_INIT, 0);
    multicore_launch_core1(core1_entry);

    uint32_t sensor_updates = 0;
    while (true) {
        imuDataGet(&angles, &gyro_raw, &accel_raw, &mag_raw);
        uint64_t sensor_timestamp_us = time_us_64();

        mutex_enter_blocking(&latest_imu_mutex);
        latest_imu.sensor_timestamp_us = sensor_timestamp_us;
        latest_imu.ax = accel_raw.s16X;
        latest_imu.ay = accel_raw.s16Y;
        latest_imu.az = accel_raw.s16Z;
        latest_imu.gx = gyro_raw.s16X;
        latest_imu.gy = gyro_raw.s16Y;
        latest_imu.gz = gyro_raw.s16Z;
        latest_imu.mx = mag_raw.s16X;
        latest_imu.my = mag_raw.s16Y;
        latest_imu.mz = mag_raw.s16Z;
        mutex_exit(&latest_imu_mutex);

        ++sensor_updates;
        if ((sensor_updates % 100) == 0)
            printf("sensor_updates=%lu\n", (unsigned long)sensor_updates);

        tight_loop_contents();
    }
}
