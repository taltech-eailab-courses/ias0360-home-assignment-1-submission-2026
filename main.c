#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/util/queue.h"
#include "pico/time.h"

#include "icm20948.h"

#include "ff.h"
#include "sd_card.h"
#include "f_util.h"
#include "hw_config.h"
#include "statistic.h"

#define PATH_MAX_LEN 256
#define INT_STATUS_1 0x1A   //IMU register, bit 0 flags data ready
#define QUEUE_SIZE 256
#define LOG_BUF_SIZE 2048
#define N 64                //Number of samples for mean calculation

typedef struct {
    int16_t ax, ay, az;
    int16_t gx, gy, gz;
    uint32_t t_us;   // timestamp (lower 32 bits is fine for short runs)
} Sample;

static queue_t sample_q;
static volatile uint32_t dropped_samples = 0;
static char log_buf[LOG_BUF_SIZE];
static size_t log_len = 0;

// --------- Globals (FatFs requires the FS to outlive the mount) ----------
static FATFS fs;                 // must be static/global (lives as long as the mount)
static sd_card_t *g_sd = NULL;   // active SD card
static const char *g_drive = NULL; // typically "0:"

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
        printf("No files found (but %lu director%s present).\n", stats.dirs, (stats.dirs == 1 ? "y" : "ies"));
    } else {
        printf("\nSummary: %lu file%s in %lu director%s, total %llu bytes.\n",
               stats.files, (stats.files == 1 ? "" : "s"),
               stats.dirs, (stats.dirs == 1 ? "y" : "ies"),
               (unsigned long long)stats.total_bytes);
    }
    return FR_OK;
}

// ------------------------------ Main -------------------------------------
static void core1_reader(void)
{
    while (1) {
        IMU_ST_SENSOR_DATA stGyroRawData, stAccelRawData;
        // int16_t gx, gy, gz, ax, ay, az;
        // icm20948AccelFastRead(&ax, &ay, &az);
        // icm20948GyroFastRead (&gx, &gy, &gz);
        
        if(I2C_ReadOneByte(INT_STATUS_1) & 0x01){   // read sensor only if new data is ready, bit 0
            imuDataAccGyrGet(&stGyroRawData, &stAccelRawData);

            uint32_t t_now = (uint32_t)time_us_64();
            Sample s = { stAccelRawData.s16X, stAccelRawData.s16Y, stAccelRawData.s16Z,
                        stGyroRawData.s16X, stGyroRawData.s16Y, stGyroRawData.s16Z,
                        t_now };
            queue_add_blocking(&sample_q, &s);
            // (Optional) pace the producer slightly if needed
            // sleep_us(500); // ~2 kHz -> uncomment to throttle
        }
    }
}

void sd_card_init(FIL *pFile, char *pPath, uint32_t path_size) {
        // 1) Init + mount
    if (!sd_init_and_mount()) {
        loop_forever_msg("SD init/mount failed.");
    }
    
    // Build absolute file path: <drive>/test.txt
    join_path(pPath, path_size, g_drive, "IMU_test.txt");

    // 2) Create the file
    FRESULT fr = create_file(pPath, pFile);
    if (fr != FR_OK) die(fr, "f_open(create)");
}

void sd_card_write(FIL *pFile, char* pPath, const char *buf, size_t len) {
    // 3) Write data
    UINT bw = 0;
    FRESULT fr = write_to_file(pFile, buf, (UINT)len, &bw);
    if (fr != FR_OK || bw != len) die(fr, "f_write/f_sync");
    printf("Wrote %u bytes to %s\n", bw, pPath);
}

void sd_card_end(FIL *pFile) {
    // Close the file
    f_close(pFile);

    // 4) Check and list files (recursively) on the card
    FRESULT fr = check_and_list_files(g_drive);
    if (fr != FR_OK) die(fr, "check_and_list_files");

    // Optional: unmount
    fr = f_unmount(g_drive);
    printf("f_unmount -> %s (%d)\n", FRESULT_str(fr), fr);
}

void place_into_arrays(float* ax, float* ay, float* az,
                float* gx, float* gy, float* gz, int i, Sample* s){
    ax[i] = s->ax;
    ay[i] = s->ay;
    az[i] = s->az;

    gx[i] = s->gx;
    gy[i] = s->gy;
    gz[i] = s->gz;
}

int main(void) {

    stdio_init_all();
    sleep_ms(3000);

    IMU_EN_SENSOR_TYPE type;
    imuInit(&type);

    // init card and open file
    FIL f;
    char path[PATH_MAX_LEN];
    sd_card_init(&f, path, sizeof(path));

    if (IMU_EN_SENSOR_TYPE_ICM20948 == type) {
        printf("Motion sensor is ICM-20948 (multicore)\n");
    } else {
        printf("Motion sensor NULL\n");
    }

    // Queue can hold up to N samples; adjust for your bandwidth
    queue_init(&sample_q, sizeof(Sample), QUEUE_SIZE);

    // Launch core1 reader
    multicore_launch_core1(core1_reader);

    uint32_t t_prev = (uint32_t)time_us_64();
    //uint16_t sample_i = 0;  // index for sample_q

    static float ax[N], ay[N], az[N], gx[N], gy[N], gz[N];  //Arrays for Accel data for statistics
    int i = 0;                                              // Array counter

    float maxX, maxY, maxZ;         //Max values of a set on samples
    float minX, minY, minZ;         //Min values of a set of samples
    maxX = maxY = maxZ = minX = minY = minZ = 0;    // initializing

    while (1) {
        Sample s;
        queue_remove_blocking(&sample_q, &s);
        
        // calculate freq of readings
        uint32_t t_now = (uint32_t)time_us_64();
        uint32_t dt_us = (t_now - t_prev);
        t_prev = t_now;
        float hz = (dt_us > 0) ? (1000000.0f / (float)dt_us) : 0.0f;

        // Place samples into arrays
        place_into_arrays(ax, ay, az, gx, gy, gz, i, &s);

        // Statistics of N samples
        if(i == N - 1){
            float meanX = mean_f32(ax, N);
            float meanY = mean_f32(ay, N);
            float meanZ = mean_f32(az, N);

            float varX = variance_f32(ax, N, meanX);
            float varY = variance_f32(ay, N, meanY);
            float varZ = variance_f32(az, N, meanZ);

            float stdevX = stddev_f32(varX);
            float stdevY = stddev_f32(varY);
            float stdevZ = stddev_f32(varZ);

            min_max_f32(ax, N, &minX, &maxX);
            // Printout of statistics
            printf("Max accel: X= %6.2f | Variance X: %6.2f | ST DEV: X= %6.2f\n",
                                                maxX, varX, stdevX);
            printf("Max accel: Y= %6.2f | Variance Y: %6.2f | ST DEV: Y= %6.2f\n",
                                                maxY, varY, stdevY);
            printf("Max accel: Z= %6.2f | Variance Z: %6.2f | ST DEV: Z= %6.2f\n",
                                                maxZ, varZ, stdevZ);                                    
            i = 0;
        }else{
            i ++;
        }

        // writes current sample to buffer and calculates the length of it
        log_len += (size_t)snprintf(log_buf + log_len,
                                    sizeof(log_buf) - log_len, 
                                    "%lu,%d,%d,%d,%d,%d,%d\n", 
                                    (unsigned long)s.t_us, s.ax, s.ay, s.az,
                                    s.gx, s.gy, s.gz);

        // Write data to SD card when buffer starts to get full
        if (log_len > sizeof log_buf - 64) {   //a line of log should be 53 byte.   
            sd_card_write(&f, path, log_buf, log_len);
            log_len = 0;
            // raw IMU printout to terminal
            printf("ACC: X=%d Y=%d Z=%d | GYRO: X=%d Y=%d Z=%d | RX Rate: %.1f Hz\r\n",
                s.ax, s.ay, s.az, s.gx, s.gy, s.gz, hz);
        }
    }
    sd_card_end(&f);
}
