#include "icm20948.h"
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/multicore.h"     // only used by main_4
#include "pico/util/queue.h"    // only used by main_4

#include "ff.h"
#include "sd_card.h"
#include "f_util.h"
#include "hw_config.h"

#define SAMPLES (QUEUE_SIZE * 10)
#define QUEUE_SIZE 512

#define FILE_PATH "0:/imu_log.bin"

#define SAMPLES_PER_SECTOR 32 // 32 * 16 bytes = 512 bytes (1 physical SD sector)

#define AXIS_CHANNELS 6
#define TOTAL_SAMPLES (QUEUE_SIZE * AXIS_CHANNELS) // 3072 values

static uint32_t total_logged = 0;
static uint32_t t_start = 0;

// Quantization helpers

static inline float clampf(float v, float lo, float hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

// Quantize float in ~[-1,1) to signed Q15
static void quantize_q15(const float* x, int n, int16_t* y, int* clip_count) {
    int clips = 0;
    const float min_val = -1.0f;
    const float max_val = 32767.0f / 32768.0f; // 0.99996948f
    for (int i = 0; i < n; i++) {
        float s = clampf(x[i], min_val, max_val); // avoid +1.0 overflow
        if (s != x[i]) clips++;
        int32_t q = (int32_t)lrintf(s * 32768.0f);
        if (q >  32767) q =  32767;
        if (q < -32768) q = -32768;
        y[i] = (int16_t)q;
    }
    if (clip_count) *clip_count = clips;
}

static void dequantize_q15(const int16_t* x, int n, float* y) {
    for (int i = 0; i < n; i++) y[i] = (float)x[i] / 32768.0f;
}

static void quantize_q7(const float* x, int n, int8_t* y, int* clip_count) {
    int clips = 0;
    const float min_val = -1.0f;
    const float max_val = 127.0f / 128.0f; // 0.9921875f
    for (int i = 0; i < n; i++) {
        float s = clampf(x[i], min_val, max_val);
        if (s != x[i]) clips++;
        int32_t q = (int32_t)lrintf(s * 128.0f);
        if (q >  127) q =  127;
        if (q < -128) q = -128;
        y[i] = (int8_t)q;
    }
    if (clip_count) *clip_count = clips;
}

static void dequantize_q7(const int8_t* x, int n, float* y) {
    for (int i = 0; i < n; i++) y[i] = (float)x[i] / 128.0f;
}

static void quantize_q3(const float* x, int n, int8_t* y, int* clip_count) {
    int clips = 0;
    const float min_val = -1.0f;
    const float max_val = 7.0f / 8.0f; // 0.875f
    for (int i = 0; i < n; i++) {
        float s = clampf(x[i], min_val, max_val);
        if (s != x[i]) clips++;
        int32_t q = (int32_t)lrintf(s * 8.0f);
        if (q >  7) q =  7;
        if (q < -8) q = -8;
        y[i] = (int8_t)q;
    }
    if (clip_count) *clip_count = clips;
}

static void dequantize_q3(const int8_t* x, int n, float* y) {
    for (int i = 0; i < n; i++) y[i] = (float)x[i] / 8.0f;
}

static void snr_and_error(const float* ref, const float* test, int n,
                          float* snr_db, float* max_abs_err, float* rms_err) {
    double sig = 0.0, err = 0.0;
    float maxe = 0.0f;
    for (int i = 0; i < n; i++) {
        float e = ref[i] - test[i];
        sig += (double)ref[i] * (double)ref[i];
        err += (double)e * (double)e;
        if (fabsf(e) > maxe) maxe = fabsf(e);
    }
    float rms = (float)sqrt(err / (double)n);
    *rms_err = rms;
    *max_abs_err = maxe;
    if (err == 0.0) {
        *snr_db = INFINITY;
    } else {
        *snr_db = 10.0f * log10f((float)(sig / err));
    }
}

// Statistical helpers

static float mean_f32(const float* x, int n) {
    double acc = 0.0;
    for (int i = 0; i < n; i++) acc += x[i];
    return (float)(acc / (double)n);
}

// sample variance (denominator n-1)
static float variance_f32(const float* x, int n, float mean) {
    if (n <= 1) return 0.0f;
    double acc = 0.0;
    for (int i = 0; i < n; i++) {
        double d = (double)x[i] - (double)mean;
        acc += d * d;
    }
    return (float)(acc / (double)(n - 1));
}

// standard deviation from variance
static float stddev_f32(float variance) {
    return sqrtf(variance);
}

// min & max
static void min_max_f32(const float* x, int n, float* mn, float* mx) {
    float a = x[0], b = x[0];
    for (int i = 1; i < n; i++) {
        if (x[i] < a) a = x[i];
        if (x[i] > b) b = x[i];
    }
    *mn = a; *mx = b;
}

// median (sorts a local copy; insertion sort - fine for small N)
static float median_f32(const float* x, int n) {
    static float tmp[QUEUE_SIZE];
    for (int i = 0; i < n; i++) tmp[i] = x[i];

    // insertion sort (ascending)
    for (int i = 1; i < n; i++) {
        float key = tmp[i];
        int j = i - 1;
        while (j >= 0 && tmp[j] > key) {
            tmp[j + 1] = tmp[j];
            j--;
        }
        tmp[j + 1] = key;
    }

    if (n & 1) return tmp[n / 2];
    return 0.5f * (tmp[n / 2 - 1] + tmp[n / 2]);
}

// mode (for discrete/repeated values). If all counts are 1, no mode.
// eps lets you treat near-equal floats as equal (use 0 for exact).
static float mode_f32(const float* x, int n, int* count_out, float eps) {
    int best_count = 0;
    float best_val = NAN;

    for (int i = 0; i < n; i++) {
        int cnt = 1;
        for (int j = i + 1; j < n; j++) {
            if (fabsf(x[j] - x[i]) <= eps) cnt++;
        }
        if (cnt > best_count) {
            best_count = cnt;
            best_val = x[i];
        }
    }

    if (count_out) *count_out = best_count;
    return best_val; // if best_count==1, there is no mode
}

// FFT

#define M_PI_F 3.14159265358979323846f

// Simple complex type
typedef struct {
    float re, im;
} c32;

// Hamming window (in-place)
static void hamming_window(float* x, int n) {
    const float factor = (2.0f * M_PI_F) / (n - 1);
    for (int i = 0; i < n; i++) {
        x[i] *= 0.54f - 0.46f * cosf(i * factor);
    }
}

static unsigned reverse_bits(unsigned v, int nbits) {
    unsigned r = 0;
    for (int i = 0; i < nbits; i++) {
        r = (r << 1) | (v & 1u);
        v >>= 1u;
    }
    return r;
}

// In-place radix-2 Cooley–Tukey FFT
// dir = +1 for FFT, -1 for IFFT
static int fft_radix2(c32* x, int n, int dir) {
    if (!((n > 0) && ((n & (n - 1)) == 0))) return -1;
    int logn = __builtin_ctz((unsigned)n);

    // Bit-reversal permutation
    for (unsigned i = 0; i < (unsigned)n; i++) {
        unsigned j = reverse_bits(i, logn);
        if (j > i) {
            c32 t = x[i]; x[i] = x[j]; x[j] = t;
        }
    }

    const float sgn = (dir >= 0) ? -1.0f : 1.0f;
    for (int s = 1; s <= logn; s++) {
        int m = 1 << s;
        int m2 = m >> 1;
        float theta = (sgn * M_PI_F) / m2;
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
                x[t].re = ur - tr; x[t].im = ui - ti;
                x[u].re = ur + tr; x[u].im = ui + ti;
                // twiddle update (CORDIC-free recurrence)
                float tmp = wr;
                wr += wr * wpr - wi * wpi;
                wi += wi * wpr + tmp * wpi;
            }
        }
    }

    if (dir < 0) {
        float inv = 1.0f / n;
        for (int i = 0; i < n; i++) {
            x[i].re *= inv;
            x[i].im *= inv;
        }
    }
    return 0;
}

// Magnitude spectrum
static void fft_mag(const c32* X, int n, float* mag) {
    for (int i = 0; i < n; i++) {
        mag[i] = sqrtf(X[i].re * X[i].re + X[i].im * X[i].im);
    }
}

// Peak picking (top-K, single-sided)
static void top_k_peaks(const float* mag, int n_half, int k_exclude_dc, int K,
                        int* out_idx, float* out_val, int* out_count) {
    // simple selection without sorting the full array
    int count = 0;
    for (int k = 0; k < K; k++) {
        int best_i = -1;
        float best_v = -1.0f;
        for (int i = k_exclude_dc; i < n_half; i++) {
            // skip already taken
            bool taken = false;
            for (int j = 0; j < count; j++) {
                if (out_idx[j] == i) {
                    taken = true;
                    break;
                }
            }
            if (!taken && (mag[i] > best_v)) {
                best_v = mag[i];
                best_i = i;
            }
        }

        if (best_i < 0) break;
        out_idx[count] = best_i;
        out_val[count] = best_v;
        count++;
    }
    *out_count = count;
}

// IMU hardware

#define REG_ADD_INT_STATUS_1        0x1A
#define REG_VAL_RAW_DATA_0_RDY_INT  0x01

bool icm20948IsDataReady(void) {
    // Ensure we are in User Bank 0 where INT_STATUS_1 lives
    I2C_WriteOneByte(REG_ADD_REG_BANK_SEL, REG_VAL_REG_BANK_0);

    // Read the interrupt status register
    uint8_t int_status = I2C_ReadOneByte(REG_ADD_INT_STATUS_1);

    // Check if bit 0 (RAW_DATA_0_RDY_INT) is set to 1
    return (int_status & REG_VAL_RAW_DATA_0_RDY_INT) != 0;
}

typedef struct {
    int16_t ax, ay, az;
    int16_t gx, gy, gz;
    uint32_t t_us;   // timestamp (lower 32 bits is fine for short runs)
} Sample;

static queue_t sample_q;

// FatFs requires the FS to outlive the mount
static FATFS fs;                 // must be static/global (lives as long as the mount)
static sd_card_t *g_sd = NULL;   // active SD card
static const char *g_drive = NULL; // typically "0:"

static bool sd_init_and_mount(void) {
    printf("Initializing SD Card...\n");

    if (!sd_init_driver()) {
        printf("sd_init_driver() failed\n");
        return false;
    }

    g_sd = sd_get_by_num(0);
    if (!g_sd) {
        printf("No SD config found (sd_get_by_num(0) == NULL)\n");
        return false;
    }

    g_drive = sd_get_drive_prefix(g_sd);  // usually "0:"
    if (!g_drive) {
        printf("sd_get_drive_prefix() returned NULL\n");
        return false;
    }

    FRESULT fr = f_mount(&fs, g_drive, 1);
    printf("f_mount -> %s (%d)\n", FRESULT_str(fr), fr);

    if (fr == FR_NO_FILESYSTEM) {
        static BYTE work[4096]; // >= FF_MAX_SS
        MKFS_PARM opt = { FM_FAT | FM_SFD, 0, 0, 0, 0 };
        fr = f_mkfs(g_drive, &opt, work, sizeof work);
        printf("f_mkfs -> %s (%d)\n", FRESULT_str(fr), fr);
        if (fr == FR_OK) {
            fr = f_mount(&fs, g_drive, 1);
            printf("f_mount(after mkfs) -> %s (%d)\n", FRESULT_str(fr), fr);
        }
    }

    if (fr != FR_OK) {
        printf("Mount failed: %s (%d)\n", FRESULT_str(fr), fr);
        return false;
    }

    return true;
}

static void core1_reader(void) {
    while (true) {
        if (!icm20948IsDataReady()) {
            sleep_us(200); // reduce I2C bus traffic and power consumption
            continue;
        }
        IMU_ST_SENSOR_DATA stGyroRawData, stAccelRawData;
        imuDataAccGyrGet(&stGyroRawData, &stAccelRawData);

        Sample s = { stAccelRawData.s16X, stAccelRawData.s16Y, stAccelRawData.s16Z,
                      stGyroRawData.s16X, stGyroRawData.s16Y, stGyroRawData.s16Z,
                      time_us_32() };
        queue_add_blocking(&sample_q, &s);
    }
}

void sd_save(FIL log_file, Sample *sector_buffer, size_t size, uint8_t *buf_idx) {
    if (*buf_idx >= SAMPLES_PER_SECTOR) {
        UINT bw;
        f_write(&log_file, sector_buffer, size, &bw);
        *buf_idx = 0;
        total_logged += SAMPLES_PER_SECTOR;
        if ((total_logged % SAMPLES) == 0) {
            f_sync(&log_file);
            float hz = 1000000.0f * total_logged / (time_us_32() - t_start);
            printf("Logged %lu samples | Storage Rate: %.1f Hz\n", total_logged, hz);
        }
    }
}

int main(void) {
    stdio_init_all();
    sleep_ms(2000);

    if (!sd_init_and_mount()) {
        printf("SD Mount Failed!\n");
        while (true) tight_loop_contents();
    }

    IMU_EN_SENSOR_TYPE type;
    imuInit(&type);

    queue_init(&sample_q, sizeof(Sample), QUEUE_SIZE);

    FIL log_file;
    FRESULT fr = f_open(&log_file, FILE_PATH, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK) {
        printf("Failed to create log file: %d\n", fr);
        while (true) tight_loop_contents();
    }

    // Launch core1 reader
    multicore_launch_core1(core1_reader);

    Sample sector_buffer[SAMPLES_PER_SECTOR];
    uint8_t buf_idx = 0;
    t_start = time_us_32();

    static int16_t q15_axes[AXIS_CHANNELS][QUEUE_SIZE];
    static int8_t q7_axes[AXIS_CHANNELS][QUEUE_SIZE];
    static int8_t q3_axes[AXIS_CHANNELS][QUEUE_SIZE];

    static float deq_16_axes[AXIS_CHANNELS][QUEUE_SIZE];
    static float deq_8_axes[AXIS_CHANNELS][QUEUE_SIZE];
    static float deq_4_axes[AXIS_CHANNELS][QUEUE_SIZE];

    static float mean[AXIS_CHANNELS];
    static float variance[AXIS_CHANNELS];
    static float standard_dev[AXIS_CHANNELS];
    static float median[AXIS_CHANNELS];
    static int mcount[AXIS_CHANNELS];
    static float mode[AXIS_CHANNELS];
    static float min[AXIS_CHANNELS];
    static float max[AXIS_CHANNELS];

    static c32 complex[QUEUE_SIZE];
    static float mag[QUEUE_SIZE];
    static float windowed_x[QUEUE_SIZE];

    static float raw_axes[AXIS_CHANNELS][QUEUE_SIZE];

    uint16_t sample_cnt = 0;

    while (true) {
        Sample s;
        queue_remove_blocking(&sample_q, &s);

        sector_buffer[buf_idx++] = s;

        // Normalize to float [-1.0, 1.0)
        raw_axes[0][sample_cnt] = (float)s.ax / 32768.0f;
        raw_axes[1][sample_cnt] = (float)s.ay / 32768.0f;
        raw_axes[2][sample_cnt] = (float)s.az / 32768.0f;
        raw_axes[3][sample_cnt] = (float)s.gx / 32768.0f;
        raw_axes[4][sample_cnt] = (float)s.gy / 32768.0f;
        raw_axes[5][sample_cnt] = (float)s.gz / 32768.0f;

        sample_cnt++;

        if (sample_cnt >= QUEUE_SIZE) {
            // Benchmark quantization pre-processing
            uint32_t start_quant = time_us_32();

            int clips16 = 0, clips8 = 0, clips4 = 0;

            quantize_q15(&raw_axes[0][0], TOTAL_SAMPLES, &q15_axes[0][0], &clips16);
            quantize_q7(&raw_axes[0][0], TOTAL_SAMPLES, &q7_axes[0][0], &clips8);
            quantize_q3(&raw_axes[0][0], TOTAL_SAMPLES, &q3_axes[0][0], &clips4);

            dequantize_q15(&q15_axes[0][0], TOTAL_SAMPLES, &deq_16_axes[0][0]);
            dequantize_q7(&q7_axes[0][0], TOTAL_SAMPLES, &deq_8_axes[0][0]);
            dequantize_q3(&q3_axes[0][0], TOTAL_SAMPLES, &deq_4_axes[0][0]);

            float snr16, max_err16, rms16;
            float snr8, max_err8, rms8;
            float snr4, max_err4, rms4;

            snr_and_error(&raw_axes[0][0], &deq_16_axes[0][0], TOTAL_SAMPLES, &snr16, &max_err16, &rms16);
            snr_and_error(&raw_axes[0][0], &deq_8_axes[0][0],  TOTAL_SAMPLES, &snr8,  &max_err8,  &rms8);
            snr_and_error(&raw_axes[0][0], &deq_4_axes[0][0],  TOTAL_SAMPLES, &snr4,  &max_err4,  &rms4);

            uint32_t end_quant = time_us_32() - start_quant;

            // Benchmark statistical extraction
            uint32_t start_stat = time_us_32();

            for (int ch = 0; ch < AXIS_CHANNELS; ch++) {

                mean[ch] = mean_f32(raw_axes[ch], QUEUE_SIZE);
                variance[ch] = variance_f32(raw_axes[ch], QUEUE_SIZE, mean[ch]);
                standard_dev[ch] = stddev_f32(variance[ch]);
                // median[ch] = median_f32(raw_axes[ch], QUEUE_SIZE);
                // mode[ch] = mode_f32(raw_axes[ch], QUEUE_SIZE, &mcount[ch], 1e-4f);
                min_max_f32(raw_axes[ch], QUEUE_SIZE, &min[ch], &max[ch]);
            }

            uint32_t end_stat = time_us_32() - start_stat;

            // Benchmark FFT
            uint32_t start_fft = time_us_32();

            for (int i = 0; i < QUEUE_SIZE; i++) {
                windowed_x[i] = raw_axes[0][i]; // Single axis, Accel X
            }

            // Apply Hamming window and pack into complex format
            hamming_window(windowed_x, QUEUE_SIZE);
            for (int i = 0; i < QUEUE_SIZE; i++) {
                complex[i].re = windowed_x[i];
                complex[i].im = 0.0f;
            }

            // Run MCU Radix-2 FFT
            fft_radix2(complex, QUEUE_SIZE, +1);
            fft_mag(complex, QUEUE_SIZE, mag);

            // Apply coherent gain & single-sided scaling factor
            const float scale = (2.0f / QUEUE_SIZE) / 0.54f;
            for (int k = 0; k <= QUEUE_SIZE / 2; k++) {
                mag[k] *= scale;
            }

            uint32_t end_fft = time_us_32() - start_fft;

            printf("Quantization: %lu us\n", end_quant);
            // printf("16b | RMS Error: %14.7e\n", rms16);
            // printf("16b | Max Error: %14.7e\n", max_err16);
            // printf("16b | SNR (dB): %.2f\n", snr16);
            // printf("16b | Clips: %d\n", clips16);
            
            // printf(" 8b | RMS Error: %14.7e\n", rms8);
            // printf(" 8b | Max Error: %14.7e\n", max_err8);
            // printf(" 8b | SNR (dB): %.2f\n", snr8);
            // printf(" 8b | Clips: %d\n", clips8);
            
            // printf(" 4b | RMS Error: %14.7e\n", rms4);
            // printf(" 4b | Max Error: %14.7e\n", max_err4);
            // printf(" 4b | SNR (dB): %.2f\n", snr4);
            // printf(" 4b | Clips: %d\n", clips4);

            printf("Statistical extraction: %lu us\n", end_stat);

            // for (int ch = 0; ch < AXIS_CHANNELS; ch++) {
            //     printf("%d -> Mean: %.6f | Variance: %.3e | StdDev: %.3e | Median: %.6f | Mode (count=%d): %.6f | Min: %.6f | Max: %.6f\n", 
            //             ch, mean[ch], variance[ch], standard_dev[ch], median[ch], mcount[ch], mode[ch], min[ch], max[ch]);
            // }

            printf("FFT: %lu us\n", end_fft);
            
            sample_cnt = 0; // Reset idx for next block
        }

        sd_save(log_file, sector_buffer, sizeof(sector_buffer), &buf_idx);
    }

    f_close(&log_file);
    return 0;
}
