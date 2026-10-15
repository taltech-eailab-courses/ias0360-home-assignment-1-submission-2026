#include "icm20948.h"
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/util/queue.h"    // only used by main_4
#include "pico/time.h"
#include <math.h>
#include <stdint.h>
#include <stdbool.h>

#include "ff.h"
#include "sd_card.h"
#include "f_util.h"
#include "hw_config.h"

#define PATH_MAX_LEN 256
#define FILE_PATH "0:/imu_data.bin"
#define QUEUE_SIZE 1024
#define SAMPLES_PER_SECTOR 1024
#define PATH_MAX_LEN 256
#define GYRO_ROTATION_FAST 1968 // 60 deg/s
#define GYRO_ROTATION_MEDIUM 820 // 25 deg/s
#define GYRO_ROTATION_SLOW 164 // 5 deg/s


// --------- Globals (FatFs requires the FS to outlive the mount) ----------
static FATFS fs;                 // must be static/global (lives as long as the mount)
static sd_card_t *g_sd = NULL;   // active SD card
static const char *g_drive = NULL; // typically "0:"

// ---------- Quantization ----------------

static inline float clampf(float v, float lo, float hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

// Quantize float in ~[-1,1) to signed Q15
static void quantize_q15(const float* x, int n, int16_t* y, int* clip_count) {
    int clips = 0;
    for (int i = 0; i < n; i++) {
        float s = clampf(x[i], -0.999969f, 0.999969f); // avoid +1.0 overflow
        if (s != x[i]) clips++;
        int32_t q = (int32_t)lrintf(s * 32768.0f);     // round to nearest
        if (q >  32767) q =  32767;
        if (q < -32768) q = -32768;
        y[i] = (int16_t)q;
    }
    if (clip_count) *clip_count = clips;
}

static void dequantize_q15(const int16_t* x, int n, float* y) {
    for (int i = 0; i < n; i++) y[i] = (float)x[i] / 32768.0f;
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

// 8-bit signed Q7
static void quantize_q7(const float* x, int n,
                       int8_t* y, int* clip_count) {
    int clips = 0;
    for (int i = 0; i < n; i++) {
        float s = clampf(x[i], -1.0f, 127.0f / 128.0f);
        if (s != x[i]) clips++;

        int32_t q = (int32_t)lrintf(s * 128.0f);
        if (q > 127)  q = 127;
        if (q < -128) q = -128;
        y[i] = (int8_t)q;
    }
    if (clip_count) *clip_count = clips;
}

static void dequantize_q7(const int8_t* x, int n, float* y) {
    for (int i = 0; i < n; i++)
        y[i] = (float)x[i] / 128.0f;
}

// 4-bit signed Q3, stored unpacked in int8_t
static void quantize_q3(const float* x, int n,
                       int8_t* y, int* clip_count) {
    int clips = 0;
    for (int i = 0; i < n; i++) {
        float s = clampf(x[i], -1.0f, 7.0f / 8.0f);
        if (s != x[i]) clips++;

        int32_t q = (int32_t)lrintf(s * 8.0f);
        if (q > 7)  q = 7;
        if (q < -8) q = -8;
        y[i] = (int8_t)q;
    }
    if (clip_count) *clip_count = clips;
}

static void dequantize_q3(const int8_t* x, int n, float* y) {
    for (int i = 0; i < n; i++)
        y[i] = (float)x[i] / 8.0f;
}

// ---------- small helpers (statistics) ----------

// arithmetic mean
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

// ------------------------- Utility / Error -------------------------------
static void die(FRESULT fr, const char *op) {
    printf("%s failed: %s (%d)\n", op, FRESULT_str(fr), fr);
    while (1) tight_loop_contents();
}

static void loop_forever_msg(const char *msg) {
    printf("%s\n", msg);
    while (1) tight_loop_contents();
}

static void join_path(char *out, size_t out_sz, const char *drive, const char *rel) {
    // drive = "0:" or "0:/", ensure exactly one slash when joining
    if (rel && rel[0] == '/') rel++; // avoid double slashes
    if (drive && drive[strlen(drive) - 1] == '/')
        snprintf(out, out_sz, "%s%s", drive, rel ? rel : "");
    else
        snprintf(out, out_sz, "%s/%s", drive, rel ? rel : "");
}

// ------------------------- 1) Initialization -----------------------------
static bool sd_init_and_mount(void) {
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

// ------------------------- 2) File creation ------------------------------
static FRESULT create_file(const char *abs_path, FIL *out_file) {
    // Creates/truncates a file and opens it for writing
    return f_open(out_file, abs_path, FA_WRITE | FA_CREATE_ALWAYS);
}

// ------------------------- 3) File writing -------------------------------
static FRESULT write_to_file(FIL *file, const void *data, UINT len, UINT *bytes_written) {
    *bytes_written = 0;
    FRESULT fr = f_write(file, data, len, bytes_written);
    if (fr == FR_OK) {
        fr = f_sync(file); // ensure data hits the card
    }
    return fr;
}

// ------------------------- 4) File checking/listing ----------------------
typedef struct {
    uint32_t files;
    uint32_t dirs;
    uint64_t total_bytes;
} list_stats_t;

static bool is_dot_or_dotdot(const char *name) {
    return (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')));
}

static FRESULT list_dir_recursive(const char *path, list_stats_t *stats) {
    DIR dir;
    FILINFO fno;
    FRESULT fr = f_opendir(&dir, path);
    if (fr != FR_OK) {
        printf("f_opendir('%s') -> %s (%d)\n", path, FRESULT_str(fr), fr);
        return fr;
    }

    for (;;) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK) {
            printf("f_readdir('%s') -> %s (%d)\n", path, FRESULT_str(fr), fr);
            break;
        }
        if (fno.fname[0] == '\0') break; // end of directory

        if (is_dot_or_dotdot(fno.fname)) continue;

        if (fno.fattrib & AM_DIR) {
            stats->dirs++;
            char subpath[PATH_MAX_LEN];
            snprintf(subpath, sizeof subpath, "%s/%s", path, fno.fname);
            printf("[DIR]  %s\n", subpath);
            fr = list_dir_recursive(subpath, stats);
            if (fr != FR_OK) break;
        } else {
            stats->files++;
            stats->total_bytes += (uint64_t)fno.fsize;
            printf("[FILE] %s/%s  (%lu bytes)\n", path, fno.fname, (unsigned long)fno.fsize);
        }
    }

    FRESULT frc = f_closedir(&dir);
    if (fr == FR_OK && frc != FR_OK) fr = frc;
    return fr;
}

// Public checker: lists all files and sizes, and tells if any exist
static FRESULT check_and_list_files(const char *root_drive) {
    // Build root path "0:/"
    char root[PATH_MAX_LEN];
    join_path(root, sizeof root, root_drive, ""); // ensures a trailing slash when we add children

    list_stats_t stats = {0};
    printf("\n--- SD Card File Listing for '%s' ---\n", root_drive);
    FRESULT fr = list_dir_recursive(root_drive, &stats);
    if (fr != FR_OK && fr != FR_NO_PATH) {
        printf("Directory listing aborted due to error.\n");
        return fr;
    }

    if (stats.files == 0 && stats.dirs == 0) {
        printf("No files or directories found on the SD card.\n");
    } else if (stats.files == 0) {
        printf("No files found (but %u director%s present).\n", stats.dirs, (stats.dirs == 1 ? "y" : "ies"));
    } else {
        printf("\nSummary: %u file%s in %u director%s, total %llu bytes.\n",
               stats.files, (stats.files == 1 ? "" : "s"),
               stats.dirs, (stats.dirs == 1 ? "y" : "ies"),
               (unsigned long long)stats.total_bytes);
    }
    return FR_OK;
}

// ---------- Simple complex type ----------
typedef struct { float re, im; } c32;

// ---------- Small utilities ----------
static inline float fsqrtf(float x){ return sqrtf(x); }
static inline float fclampf(float v, float lo, float hi){ return (v<lo)?lo:((v>hi)?hi:v); }

// ---------- Hamming window (in-place) ----------
static void hamming_window(float* x, int n){
    for(int i=0;i<n;i++){
        x[i] *= 0.54f - 0.46f * cosf(2.0f*(float)M_PI*(float)i/(float)(n-1));
    }
}

// ---------- Bit helpers ----------
static int is_power_of_two(int n){ return (n>0) && ((n & (n-1))==0); }
static unsigned reverse_bits(unsigned v, int nbits){
    unsigned r = 0u;
    for(int i=0;i<nbits;i++){ r = (r<<1) | (v & 1u); v >>= 1u; }
    return r;
}

// ---------- In-place radix-2 Cooley–Tukey FFT ----------
// dir = +1 for FFT, -1 for IFFT
static int fft_radix2(c32* x, int n, int dir){
    if(!is_power_of_two(n)) return -1;
    int logn = 0; while((1<<logn) < n) logn++;

    // Bit-reversal permutation
    for(unsigned i=0;i<(unsigned)n;i++){
        unsigned j = reverse_bits(i, logn);
        if(j>i){ c32 t = x[i]; x[i]=x[j]; x[j]=t; }
    }

    const float sgn = (dir >= 0) ? -1.0f : 1.0f;
    for(int s=1; s<=logn; s++){
        int m = 1<<s;
        int m2 = m>>1;
        float theta = sgn * (float)M_PI / (float)m2;
        float wpr = -2.0f * sinf(0.5f*theta) * sinf(0.5f*theta);
        float wpi = sinf(theta);
        for(int k=0; k<n; k+=m){
            float wr = 1.0f, wi = 0.0f;
            for(int j=0; j<m2; j++){
                int t = k + j + m2;
                int u = k + j;
                float tr = wr*x[t].re - wi*x[t].im;
                float ti = wr*x[t].im + wi*x[t].re;
                float ur = x[u].re, ui = x[u].im;
                x[t].re = ur - tr; x[t].im = ui - ti;
                x[u].re = ur + tr; x[u].im = ui + ti;
                // twiddle update (CORDIC-free recurrence)
                float tmp = wr;
                wr = wr + (wr*wpr - wi*wpi);
                wi = wi + (wi*wpr + tmp*wpi);
            }
        }
    }

    if(dir < 0){
        float inv = 1.0f/(float)n;
        for(int i=0;i<n;i++){ x[i].re *= inv; x[i].im *= inv; }
    }
    return 0;
}

// ---------- Magnitude spectrum ----------
static void fft_mag(const c32* X, int n, float* mag){
    for(int i=0;i<n;i++){
        mag[i] = fsqrtf(X[i].re*X[i].re + X[i].im*X[i].im);
    }
}

// ---------- Peak picking (top-K, single-sided) ----------
static void top_k_peaks(const float* mag, int n_half, int k_exclude_dc, int K,
                        int* out_idx, float* out_val, int* out_count){
    // simple selection without sorting the full array
    int count = 0;
    for(int k=0; k<K; k++){
        int best_i = -1; float best_v = -1.0f;
        for(int i=k_exclude_dc; i<n_half; i++){
            // skip already taken
            bool taken = false;
            for(int j=0;j<count;j++) if(out_idx[j]==i){ taken = true; break; }
            if(taken) continue;
            if(mag[i] > best_v){ best_v = mag[i]; best_i = i; }
        }
        if(best_i >= 0){
            out_idx[count] = best_i;
            out_val[count] = best_v;
            count++;
        }
    }
    *out_count = count;
}


// ------------------------------ Main -------------------------------------

static queue_t sample_q;

static void core1_reader(void)
{
    IMU_EN_SENSOR_TYPE type;
    // imuInit(&type);
    
    uint32_t t_prev = (uint32_t)time_us_64();
    while (1) {
	I2C_WriteOneByte(REG_ADD_REG_BANK_SEL, REG_VAL_REG_BANK_0);

        if ((uint8_t)I2C_ReadOneByte(0x1A) & 0x01) {
	        IMU_ST_SENSOR_DATA stGyroRawData, stAccelRawData;

	        imuDataAccGyrGet(&stGyroRawData, &stAccelRawData);

	        uint32_t t_now = (uint32_t)time_us_64();
	        int32_t sample[7] = { stAccelRawData.s16X, stAccelRawData.s16Y, stAccelRawData.s16Z,
	                      stGyroRawData.s16X, stGyroRawData.s16Y, stGyroRawData.s16Z,
	                      t_now };
	        queue_add_blocking(&sample_q, &sample);
	}
    }
}


int main(void) {

	stdio_init_all();
	sleep_ms(3000);

	// ---- Settings ----
	enum { N = SAMPLES_PER_SECTOR };           // power-of-two FFT size

	if(!is_power_of_two(N)){
        	printf("N must be power of two\n");
        	while(true) tight_loop_contents();
	}
	// 1) Init + mount
	if (!sd_init_and_mount()) {
        	loop_forever_msg("SD init/mount failed.");
    	}
    	// Build absolute file path: <drive>/test.txt
    	static char path[PATH_MAX_LEN];
    	join_path(path, sizeof path, g_drive, "imuData2222.bin");
    	// 2) Create the file
    	static FIL f;
    	FRESULT fr = create_file(path, &f);
    	if (fr != FR_OK) die(fr, "f_open(create)");
    	IMU_EN_SENSOR_TYPE type;
    	imuInit(&type);
    	if (IMU_EN_SENSOR_TYPE_ICM20948 == type) {
        	printf("Motion sensor is ICM-20948 (multicore)\n");
    	} else {
        	printf("Motion sensor NULL\n");
    	}
	
    	// Queue can hold up to QUEUE_SIZE samples;
    	queue_init(&sample_q, sizeof(uint32_t[7]), QUEUE_SIZE);
	
    	// Launch core1 reader
    	printf("Starting IMU reader on core1...\n");
    	multicore_launch_core1(core1_reader);
    	static int32_t sector_buffer[SAMPLES_PER_SECTOR][7]; // Buffer to store old IMU measurements
	uint8_t firstCount = 0; // Indicator of first count
    	size_t index = 0; // Counter for measurements stored in sector_buffer
    	uint32_t previousTime = (uint32_t)time_us_64();
	static float meanPrevious[6]; // Previous mean value of measurement block
	float angleChangeTotal[3] = {0, 0, 0}; // Total angle change per gyroscope axis
	char axisList[6] = {'x', 'y', 'z', 'x', 'y', 'z'}; // List of axis names (used in for loop printing)
	uint32_t measureStartTime;
    	while (1) {
		if(index == 0){
			measureStartTime = (uint32_t)time_us_64(); // On first measurement, record start time
		}
        	queue_remove_blocking(&sample_q, &sector_buffer[index++]);
		// If nr of samples reaches end of sector buffer, perform analysis and save to SD card
		if (index >= SAMPLES_PER_SECTOR) {
			uint32_t measureEndTime = (uint32_t)time_us_64();
	    		// Saving to SD card
            		UINT bw;
            		f_write(&f, sector_buffer, sizeof(sector_buffer), &bw);
            		index = 0;
                	f_sync(&f);

			// Calculating frequency of measurement from IMU
			float measureTime = 1000000.0f / (sector_buffer[SAMPLES_PER_SECTOR-1][6] - sector_buffer[0][6]);
                	float hz = (float)SAMPLES_PER_SECTOR / measureTime;

			printf("Sampling rate: %.1f\n", hz);

			// Convert IMU data stored in buffer to format [AXIS][Measurement Number]
			static float axisData[7][SAMPLES_PER_SECTOR];
			for(int sectorCnt = 0; sectorCnt < SAMPLES_PER_SECTOR; sectorCnt++){
				for(int sampleCnt = 0; sampleCnt < 7; sampleCnt++){
					axisData[sampleCnt][sectorCnt] =  sector_buffer[sectorCnt][sampleCnt];
				}
			}
			float df = hz / (float)N;

			// -------------- STATISTICAL PROCESSING OF MEASUREMENTS ----------------
			uint32_t timeStartStatistic = (uint32_t)time_us_64(); // Start time of statistical processing
			float meanCurrent[6];
			float varianceCurrent[6];
			float stddevCurrent[6];
			float minCurrent[6];
			float maxCurrent[6];

			// Calculate mean, variance, stddev, min and max for each axis
			for(int i = 0; i < 6; i++){
				meanCurrent[i] = mean_f32(axisData[i], SAMPLES_PER_SECTOR);
				varianceCurrent[i] = variance_f32(axisData[i], SAMPLES_PER_SECTOR, meanCurrent[i]);
				stddevCurrent[i] = stddev_f32(varianceCurrent[i]);
				min_max_f32(axisData[i], SAMPLES_PER_SECTOR, &minCurrent[i], &maxCurrent[i]);
			}
			uint32_t timeEndStatistic = (uint32_t)time_us_64();

			// Calculate angle change from starting point
			float angleChange[3];
			for(int i = 0; i < 3; i++){
				angleChange[i] = (meanCurrent[(i+3)] / 32.8) * measureTime * 0.8182;  // 32.8 LSB/deg/s, multiplying by measurement time gives angle change
				angleChangeTotal[i] = angleChangeTotal[i] + angleChange[i];
			}
			printf("Statistics results for each axis (accelerometer x-z, gyroscope x-z):\n");
			printf("Mean, Variance, Stddev, Min, Max\n\n");
			for(int i=0;i<6;i++){
				printf("%.1f   %.1f   %.1f   %.1f   %.1f\n",meanCurrent[i], varianceCurrent[i], stddevCurrent[i], minCurrent[i],
					maxCurrent[i]);
			}

			// -------------- FREQUENCY ANALYSIS - HAMMING WINDOW, FFT, TOP K PEAKS --------------------
			uint32_t timeStartFFT = (uint32_t)time_us_64(); // Start time of frequency analysis

			static float win_x[SAMPLES_PER_SECTOR];
			for(int i = 0;i<SAMPLES_PER_SECTOR;i++){
				win_x[i] = axisData[0][i];
			}

			// Hamming window
			hamming_window(win_x, SAMPLES_PER_SECTOR);
			static c32 X[SAMPLES_PER_SECTOR];
			for(int i = 0;i<SAMPLES_PER_SECTOR;i++){
				X[i].re = win_x[i]; X[i].im = 0.0f;
			}

			// ---- FFT
    			if(fft_radix2(X, SAMPLES_PER_SECTOR, +1) != 0){
        			printf("FFT error\n");
        			while(true) tight_loop_contents();
    			}
			// ---- Magnitude spectrum (single-sided bins 0..N/2)
    			static float mag[SAMPLES_PER_SECTOR];
    			fft_mag(X, SAMPLES_PER_SECTOR, mag);

		    	const float scale = (2.0f / (float)N) / 0.54f;
    			for(int k=0;k<=N/2;k++) mag[k] *= scale;


    			// ---- Report
    			printf("\n=== IMU FFT ===\n");
    			printf("fs=%.1f Hz, N=%d, resolution df=%.3f Hz\n\n", hz, N, df);

    			printf("Looking for top 5 peaks (excluding DC):\n\n");

    			int idx[5]; float val[5]; int found=0;
    			top_k_peaks(mag, N/2+1, /*exclude up to index*/1, /*K*/5, idx, val, &found);


    			for(int i=0;i<found;i++){
				if((i == 0) && (val[i] > 3000)){
					printf("Strong vibration detected\n\n");
				}
				else if((i == 0) && (val[i] > 300)){
					printf("Small vibration detected\n\n");
				}
        			float fk = df * (float)idx[i];
        			printf("  Peak %d: bin=%d  freq=%.2f Hz  amplitude≈%.4f\n",
               			i+1, idx[i], fk, val[i]);
    			}
			uint32_t timeEndFFT = (uint32_t)time_us_64();
    			printf("\n");

			// ---------------- QUANTISATION -------------------
			
			uint32_t timeStartQuant = (uint32_t)time_us_64();
			
			static int16_t q15[6][SAMPLES_PER_SECTOR];
			static float xq15[6][SAMPLES_PER_SECTOR];
			static int8_t q7[6][SAMPLES_PER_SECTOR];
			static float xq7[6][SAMPLES_PER_SECTOR];
			static int8_t q3[6][SAMPLES_PER_SECTOR];
			static float xq3[6][SAMPLES_PER_SECTOR];
			static float snr_db15[6], max_abs_err15[6], rms_err15[6];
			static float snr_db7[6], max_abs_err7[6], rms_err7[6];
			static float snr_db3[6], max_abs_err3[6], rms_err3[6];
			int clips15 = 0;
			int clips7 = 0;
			int clips3 = 0;
			
			//printf("Error for (ax, ay, az, gx, gy, gz) : q15, q7 and q3; snr, max_abs, rms\n\n");
			static float normalized[SAMPLES_PER_SECTOR];
			printf("\n---------- QUANTIZATION -----------\n\n");
			for (int i = 0; i < 6; i++) {
    			// Normalize signed 16-bit raw sensor counts.
    				for (int j = 0; j < SAMPLES_PER_SECTOR; j++) {
        				normalized[j] = axisData[i][j] / 32768.0f;
    				}
				quantize_q15(normalized, SAMPLES_PER_SECTOR, q15[i], &clips15);
				dequantize_q15(q15[i], SAMPLES_PER_SECTOR, xq15[i]);
				snr_and_error(normalized, xq15[i], SAMPLES_PER_SECTOR, &snr_db15[i], &max_abs_err15[i], &rms_err15[i]);
				quantize_q7(normalized, SAMPLES_PER_SECTOR, q7[i], &clips7);
				dequantize_q7(q7[i], SAMPLES_PER_SECTOR, xq7[i]);
				snr_and_error(normalized, xq7[i], SAMPLES_PER_SECTOR, &snr_db7[i], &max_abs_err7[i], &rms_err7[i]);
				quantize_q3(normalized, SAMPLES_PER_SECTOR, q3[i], &clips3);
				dequantize_q3(q3[i], SAMPLES_PER_SECTOR, xq3[i]);
				snr_and_error(normalized, xq3[i], SAMPLES_PER_SECTOR, &snr_db3[i], &max_abs_err3[i], &rms_err3[i]);
				printf("%.3f  %.3f  %.3f\n%.3f  %.3f  %.3f\n%.3f  %.3f  %.3f\n\n",
					snr_db15[i], max_abs_err15[i], rms_err15[i],
					snr_db7[i], max_abs_err7[i], rms_err7[i],
					snr_db3[i], max_abs_err3[i], rms_err3[i]);
			}
			uint32_t timeEndQuant = (uint32_t)time_us_64();

			// ----------- ROTATION DETECTION --------------
			uint32_t timeStartMovementDetect = (uint32_t)time_us_64();

			if(firstCount == 0){
				firstCount = 1;
				for(int i = 0; i<6; i++){
					meanPrevious[i] = meanCurrent[i];
				}
			}
			else{
				for(int i = 0;i<6;i++){
					static float meanDiff[6];
					meanDiff[i] = (float)meanCurrent[i];
					if(i < 3){/*
						if(meanDiff[i] > ACCEL_MOVEMENT_FAST){
							printf("Fast acceleration detected on accelerometer %c axis.\n", axisList[i]);
						}
						else if(meanDiff[i] > ACCEL_MOVEMENT_MEDIUM){
							printf("Medium acceleration detected on accelerometer %c axis.\n", axisList[i]);
						}
						else if(meanDiff[i] > ACCEL_MOVEMENT_SLOW){
							printf("Slow acceleration detected on accelerometer %c axis.\n", axisList[i]);
						}
						else{
							printf("No significant (unidirectional) acceleration detected on %c axis.\n", axisList[i]);
						}*/

					}
					else if(i < 6){
						if(meanDiff[i] > GYRO_ROTATION_FAST){
							printf("Fast rotation detected on gyroscope %c axis.\n", axisList[i]);
						}
						else if(meanDiff[i] > GYRO_ROTATION_MEDIUM){
							printf("Medium rate of rotation detected on gyroscope %c axis.\n", axisList[i]);
						}
						else if(meanDiff[i] > GYRO_ROTATION_SLOW){
							printf("Slow rotation detected on gyroscope %c axis.\n", axisList[i]);
						}
						else{
							printf("No significant (unidirectional) movement detected on %c axis.\n", axisList[i]);
						}
					}
					meanPrevious[i] = meanCurrent[i];
				}
			}
			printf("\n");
			// Prints angle change for last measurement block and total angle change per gyroscope axis
			for(int i = 0; i < 3; i++)
			{
				printf("Gyroscope %c axis:	Current angle change: %.3fdeg\nTotal angle change: %.3fdeg\n",
					axisList[i], angleChange[i], angleChangeTotal[i]);
			}

			uint32_t timeEndMovementDetect = (uint32_t)time_us_64();
			// Calculate time duration for each function - quantization, movement detection, fft, and statistics
			float timeQuant = (float)(timeEndQuant - timeStartQuant) / (float)1000;
			float timeMovementDetect = (float)(timeEndMovementDetect - timeStartMovementDetect) / (float)1000;
			float timeFFT = (float)(timeEndFFT - timeStartFFT) / (float)1000;
			float timeStatistic = (float)(timeEndStatistic - timeStartStatistic) / (float)1000;
			// Print time values of each function, in addition to total time of processing and processing + measurement
			printf("\n\n------ TIME OF FUNCTIONS ----------\n\n");
			printf("Statistics: %.1f ms	     FFT: %.1f ms\nQuantization: %.1f ms	     Movement detection: %.1f ms     	unit: ms\n\n",
				timeStatistic, timeFFT, timeQuant, timeMovementDetect);
			uint32_t processEndTime = (uint32_t)time_us_64();
			printf("Total time for processing: %.2f ms \n", (float(processEndTime - measureEndTime) / 1000.0));
			printf("Total time for measurement and processing: %.2f ms \n\n", (float(processEndTime - measureStartTime) / 1000.0));
		}
	}
}
    
