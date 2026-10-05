/*****************************************************************************
* | File        : main.c
* | Author      : TalTech IAS0360 / Martin Lukacka
* | Function    : Image Processing & Feature Extraction
* | Info        : Core 0: Interactive 280x240 touch canvas & GUI display
*                 Core 1: Pipelined signal processing (Sobel, quantization,
*                         feature extraction) & SD card dataset logging
*----------------
* | This version: V1.0
* | Date        : 2026-09-25
* | Info        : Multicore double-buffered pipeline with SD card integration
******************************************************************************/

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/sync.h"

#include "ff.h"
#include "sd_card.h"
#include "f_util.h"
#include "hw_config.h"

#include "DEV_Config.h"
#include "LCD_Driver.h"
#include "LCD_GUI.h"
#include "LCD_Touch.h"

#include "sobel.h"
#include "quantization.h"
#include "statistic.h"

#define PATH_MAX_LEN 256

// Double buffer snapshot for Core 1 processing
static uint8_t s_tp_save_buf[BOX_W * BOX_H];

// Sector-aligned chunk buffer for FatFs writes
static char s_chunk_buf[4096];

// Static FatFs structures
static FATFS fs;
static FIL s_fil;
static char s_path[PATH_MAX_LEN];
static char s_name[64];
static sd_card_t *g_sd = NULL;
static const char *g_drive = NULL;

static mutex_t mutex;
static TP_DATA tp_data;

// Shared pipeline results passed from Core 1 to Core 0 for LCD display
typedef struct {
    feature_vector_t feats;
    quant_metric_t quant[4];
    uint32_t sobel_l1_us;
    uint32_t sobel_l2_us;
    uint8_t downsampled[DOWNSAMPLED_SIZE * DOWNSAMPLED_SIZE];
    uint8_t edge_map[DOWNSAMPLED_SIZE * DOWNSAMPLED_SIZE];
} proc_result_t;

static proc_result_t s_proc_result;

static void join_path(char *out, size_t out_sz,
                      const char *drive, const char *rel) {
    if (rel && rel[0] == '/') rel++;
    if (drive && drive[strlen(drive) - 1] == '/')
        snprintf(out, out_sz, "%s%s", drive, rel ? rel : "");
    else
        snprintf(out, out_sz, "%s/%s", drive, rel ? rel : "");
}

static void sd_unmount_and_deinit(void) {
    if (g_drive) {
        f_unmount(g_drive);
    }
    if (g_sd && g_sd->deinit) {
        g_sd->deinit(g_sd);
    }
    gpio_pull_up(SD_CS_PIN);
}

static bool sd_init_and_mount(void) {
    if (!sd_init_driver()) {
        printf("[CORE1] [ERR] [SD] sd_init_driver() failed\n");
        return false;
    }

    g_sd = sd_get_by_num(0);
    if (!g_sd) {
        printf("[CORE1] [ERR] [SD] No SD config found (sd_get_by_num(0) == NULL)\n");
        return false;
    }

    g_drive = sd_get_drive_prefix(g_sd);
    if (!g_drive) {
        printf("[CORE1] [ERR] [SD] sd_get_drive_prefix() returned NULL\n");
        return false;
    }

    // Force driver state to uninitialized so f_mount performs a fresh probe
    sd_unmount_and_deinit();

    FRESULT fr = f_mount(&fs, g_drive, 1);
    printf("[CORE1] [SD] f_mount -> %s (%d)\n", FRESULT_str(fr), fr);

    if (fr == FR_NO_FILESYSTEM) {
        static BYTE work[4096];
        MKFS_PARM opt = { FM_FAT | FM_SFD, 0, 0, 0, 0 };
        fr = f_mkfs(g_drive, &opt, work, sizeof(work));
        printf("[CORE1] [SD] f_mkfs -> %s (%d)\n", FRESULT_str(fr), fr);
        if (fr == FR_OK) {
            fr = f_mount(&fs, g_drive, 1);
            printf("[CORE1] [SD] f_mount (post mkfs) -> %s (%d)\n",
                   FRESULT_str(fr), fr);
        }
    }

    if (fr != FR_OK) {
        printf("[CORE1] [ERR] [SD] Mount failed: %s (%d)\n", FRESULT_str(fr), fr);
        sd_unmount_and_deinit();
        return false;
    }

    return true;
}

static bool is_dot_or_dotdot(const char *name) {
    return (name[0] == '.' &&
            (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')));
}

static uint32_t s_max_sample_id = 0;

static FRESULT check_and_list_files(const char *root_drive) {
    static char root[PATH_MAX_LEN];
    join_path(root, sizeof(root), root_drive, "");

    static DIR dir;
    static FILINFO fno;
    FRESULT fr = f_opendir(&dir, root);
    if (fr != FR_OK) {
        printf("[CORE1] [ERR] [SD] f_opendir('%s') -> %s (%d)\n",
               root, FRESULT_str(fr), fr);
        return fr;
    }

    uint32_t files = 0, dirs = 0;
    uint64_t total_bytes = 0;

    for (;;) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == '\0') break;
        if (is_dot_or_dotdot(fno.fname)) continue;

        if (fno.fattrib & AM_DIR) {
            dirs++;
        } else {
            files++;
            total_bytes += (uint64_t)fno.fsize;
            unsigned long sid = 0;
            if (sscanf(fno.fname, "sample_%lu.txt", &sid) == 1) {
                if ((uint32_t)sid > s_max_sample_id) {
                    s_max_sample_id = (uint32_t)sid;
                }
            }
        }
    }
    f_closedir(&dir);

    printf("[CORE1] [SD] Mounted '%s' (%lu files, %llu KB). Existing dataset up to sample %lu\n\n",
           root_drive, (unsigned long)files,
           (unsigned long long)(total_bytes / 1024),
           (unsigned long)s_max_sample_id);
    return FR_OK;
}

// Downsample 280x240 buffer into 28x28 using integer block averaging
static void downsample_canvas(const uint8_t *src, uint8_t *dst) {
    for (int r = 0; r < DOWNSAMPLED_SIZE; r++) {
        int y0 = (r * BOX_H) / DOWNSAMPLED_SIZE;
        int y1 = ((r + 1) * BOX_H) / DOWNSAMPLED_SIZE;
        for (int c = 0; c < DOWNSAMPLED_SIZE; c++) {
            int x0 = (c * BOX_W) / DOWNSAMPLED_SIZE;
            int x1 = ((c + 1) * BOX_W) / DOWNSAMPLED_SIZE;
            uint32_t count = 0;
            uint32_t area = (uint32_t)((y1 - y0) * (x1 - x0));
            for (int y = y0; y < y1; y++) {
                const uint8_t *row = &src[y * BOX_W];
                for (int x = x0; x < x1; x++) {
                    count += row[x];
                }
            }
            dst[r * DOWNSAMPLED_SIZE + c] =
                (area > 0) ? (uint8_t)((count * 255) / area) : 0;
        }
    }
}

// Core 1 Entry: Pipelined Signal Processing, Feature Extraction, & SD logging
void core1_entry(void) {
    printf("[CORE1] Worker started (DSP & SD logging)\n");

    bool mounted = sd_init_and_mount();
    if (mounted) {
        check_and_list_files(g_drive);
    } else {
        printf("[CORE1] [WARN] [SD] Card not detected at boot (standby)\n");
    }

    uint32_t gesture_counter = 0;

    while (true) {
        uint32_t msg = multicore_fifo_pop_blocking();
        if (msg != DATA_READY_FLAG) {
            continue;
        }

        // Snapshot downsampling from double buffer under mutex
        mutex_enter_blocking(&mutex);
        downsample_canvas(s_tp_save_buf, s_proc_result.downsampled);
        mutex_exit(&mutex);

        gesture_counter++;

        // SD Card Dataset Logging (probe on demand if not currently mounted)
        if (!mounted) {
            printf("[CORE1] [SD] Mounting on demand...\n");
            mounted = sd_init_and_mount();
            if (mounted) {
                check_and_list_files(g_drive);
            }
        }

        uint32_t target_sd_id = 0;
        if (mounted) {
            target_sd_id = s_max_sample_id + 1;
        }

        // Bounding box estimation for active gesture aspect ratio
        int min_x, max_x, min_y, max_y;
        compute_bounding_box(s_proc_result.downsampled,
                             DOWNSAMPLED_SIZE, DOWNSAMPLED_SIZE,
                             &min_x, &max_x, &min_y, &max_y, 10);

        // Sobel Operator Benchmark (L1 vs. L2 Norm)
        sobel_benchmark(s_proc_result.downsampled,
                        DOWNSAMPLED_SIZE, DOWNSAMPLED_SIZE,
                        &s_proc_result.sobel_l1_us, &s_proc_result.sobel_l2_us);

        // Sobel Edge Extraction Map (Integer L1 Norm)
        sobel3x3_l1(s_proc_result.downsampled, DOWNSAMPLED_SIZE, DOWNSAMPLED_SIZE,
                    s_proc_result.edge_map);

        // Multi-Bit Quantization Sweep (16-bit Q15, 8-bit, 4-bit, 1-bit)
        quantization_sweep(s_proc_result.downsampled,
                           DOWNSAMPLED_SIZE * DOWNSAMPLED_SIZE, s_proc_result.quant);

        // Statistical Feature Extraction Vector
        extract_features(s_proc_result.downsampled, s_proc_result.edge_map,
                         DOWNSAMPLED_SIZE, DOWNSAMPLED_SIZE,
                         min_x, max_x, min_y, max_y, &s_proc_result.feats);

        // Print interactive sample telemetry to USB stdio
        float speedup = (s_proc_result.sobel_l1_us > 0) ?
            ((float)s_proc_result.sobel_l2_us / (float)s_proc_result.sobel_l1_us) : 0.0f;

        if (mounted) {
            printf("\n[CORE1] [SAMPLE] %lu\n", (unsigned long)target_sd_id);
        } else {
            printf("\n[CORE1] [GESTURE] %lu (RAM-only)\n", (unsigned long)gesture_counter);
        }

        printf("[CORE1] [BENCH] Sobel 3x3: L1=%luus L2=%luus (%.2fx)\n",
               (unsigned long)s_proc_result.sobel_l1_us,
               (unsigned long)s_proc_result.sobel_l2_us,
               speedup);

        printf("[CORE1] [STATS] Mean=%.1f Var=%.1f Std=%.1f Min=%u Max=%u\n",
               s_proc_result.feats.mean,
               s_proc_result.feats.variance,
               s_proc_result.feats.stddev,
               s_proc_result.feats.min_val,
               s_proc_result.feats.max_val);

        if (min_x >= 0) {
            printf("[CORE1] [MORPH] Edge=%.3f Asp=%.2f BBox=%dx%d\n",
                   s_proc_result.feats.active_edge_density,
                   s_proc_result.feats.aspect_ratio,
                   (max_x - min_x + 1), (max_y - min_y + 1));
        } else {
            printf("[CORE1] [MORPH] Edge=%.3f Asp=%.2f BBox=empty\n",
                   s_proc_result.feats.active_edge_density,
                   s_proc_result.feats.aspect_ratio);
        }

        printf("[CORE1] [QUANT] SNR: 16b=%.1fdB 8b=%.1fdB 4b=%.1fdB 1b=%.1fdB\n",
               s_proc_result.quant[0].snr_db, s_proc_result.quant[1].snr_db,
               s_proc_result.quant[2].snr_db, s_proc_result.quant[3].snr_db);

        printf("[CORE1] [MAP] Grayscale (Left)  |  Sobel 3x3 Edge Map (Right):\n");
        sobel_print_ascii_dual(s_proc_result.downsampled, s_proc_result.edge_map,
                               DOWNSAMPLED_SIZE, DOWNSAMPLED_SIZE);

        bool write_ok = false;
        if (mounted) {
            // Write detailed report file: 0:/sample_<id>.txt
            snprintf(s_name, sizeof(s_name), "sample_%lu.txt",
                     (unsigned long)target_sd_id);
            join_path(s_path, sizeof(s_path), g_drive, s_name);

            FRESULT fr = f_open(&s_fil, s_path, FA_WRITE | FA_CREATE_ALWAYS);
            if (fr == FR_OK) {
                int hlen = snprintf(s_chunk_buf, sizeof(s_chunk_buf),
                    "# Sample: %lu\n"
                    "# Sobel 3x3: L1=%luus L2=%luus (%.2fx)\n"
                    "# Mean=%.1f Var=%.1f Std=%.1f Min=%d Max=%d\n"
                    "# Edge=%.3f Asp=%.2f BBox=%dx%d\n"
                    "# Quant SNR: 16b=%.1fdB 8b=%.1fdB 4b=%.1fdB 1b=%.1fdB\n",
                    (unsigned long)target_sd_id,
                    (unsigned long)s_proc_result.sobel_l1_us,
                    (unsigned long)s_proc_result.sobel_l2_us,
                    speedup,
                    s_proc_result.feats.mean,
                    s_proc_result.feats.variance,
                    s_proc_result.feats.stddev,
                    s_proc_result.feats.min_val, s_proc_result.feats.max_val,
                    s_proc_result.feats.active_edge_density,
                    s_proc_result.feats.aspect_ratio,
                    (max_x - min_x + 1), (max_y - min_y + 1),
                    s_proc_result.quant[0].snr_db, s_proc_result.quant[1].snr_db,
                    s_proc_result.quant[2].snr_db, s_proc_result.quant[3].snr_db);

                UINT bw = 0;
                fr = f_write(&s_fil, s_chunk_buf, (UINT)hlen, &bw);

                // Write 28x28 CSV pixel matrix
                if (fr == FR_OK) {
                    for (int r = 0; r < DOWNSAMPLED_SIZE; r++) {
                        int pos = 0;
                        for (int c = 0; c < DOWNSAMPLED_SIZE; c++) {
                            uint8_t px = s_proc_result.downsampled[
                                r * DOWNSAMPLED_SIZE + c];
                            pos += snprintf(&s_chunk_buf[pos],
                                            sizeof(s_chunk_buf) - pos,
                                            (c == DOWNSAMPLED_SIZE - 1) ? "%u\n" : "%u,",
                                            px);
                        }
                        fr = f_write(&s_fil, s_chunk_buf, (UINT)pos, &bw);
                        if (fr != FR_OK) break;
                    }
                }
                if (fr == FR_OK) {
                    fr = f_sync(&s_fil);
                }
                f_close(&s_fil);
            }

            if (fr != FR_OK) {
                printf("[CORE1] [ERR] [SD] Write %s: %s (%d)\n",
                       s_name, FRESULT_str(fr), fr);
                sd_unmount_and_deinit();
                mounted = false;
            } else {
                // Append to dataset.csv: <sample_id>,<p0>,<p1>,...,<p783>
                join_path(s_path, sizeof(s_path), g_drive, "dataset.csv");
                fr = f_open(&s_fil, s_path, FA_WRITE | FA_OPEN_APPEND);
                if (fr == FR_OK) {
                    int pos = snprintf(s_chunk_buf, sizeof(s_chunk_buf),
                                       "%lu", (unsigned long)target_sd_id);
                    for (int i = 0; i < DOWNSAMPLED_SIZE * DOWNSAMPLED_SIZE; i++) {
                        pos += snprintf(&s_chunk_buf[pos], sizeof(s_chunk_buf) - pos,
                                        ",%u", s_proc_result.downsampled[i]);
                        if (pos >= (int)(sizeof(s_chunk_buf) - 32)) {
                            UINT bw = 0;
                            fr = f_write(&s_fil, s_chunk_buf, (UINT)pos, &bw);
                            pos = 0;
                            if (fr != FR_OK) break;
                        }
                    }
                    if (fr == FR_OK && pos > 0) {
                        if (pos < (int)sizeof(s_chunk_buf)) {
                            s_chunk_buf[pos++] = '\n';
                        }
                        UINT bw = 0;
                        fr = f_write(&s_fil, s_chunk_buf, (UINT)pos, &bw);
                    }
                    if (fr == FR_OK) {
                        fr = f_sync(&s_fil);
                    }
                    f_close(&s_fil);
                }

                if (fr != FR_OK) {
                    printf("[CORE1] [ERR] [SD] Append dataset.csv: %s (%d)\n",
                           FRESULT_str(fr), fr);
                    sd_unmount_and_deinit();
                    mounted = false;
                } else {
                    write_ok = true;
                }
            }

            if (mounted && g_sd && g_sd->sync) {
                g_sd->sync(g_sd);
            }
        }

        if (write_ok) {
            s_max_sample_id = target_sd_id;
            printf("[CORE1] [SD] Saved %s (dataset.csv appended)\n",
                   s_name);
            multicore_fifo_push_blocking(WRITE_SUCCESS_FLAG);
        } else {
            printf("[CORE1] [WARN] [SD] Storage unavailable (RAM-only)\n");
            multicore_fifo_push_blocking(SD_UNAVAILABLE_FLAG);
        }
    }
}

int main(void) {
    stdio_init_all();
    System_Init();

    mutex_init(&mutex);

    // Initialize double buffer for Core 1 dispatch
    tp_data.data = s_tp_save_buf;
    tp_data.data_len = sizeof(s_tp_save_buf);

    LCD_SCAN_DIR lcd_scan_dir = SCAN_DIR_DFT;
    LCD_Init(lcd_scan_dir, 1000);
    TP_Init(lcd_scan_dir, &tp_data, &mutex);
    TP_GetAdFac();
    TP_Dialog();

    // Launch Core 1 for pipelined DSP and SD card logging
    multicore_launch_core1(core1_entry);

    // Allow USB terminal connection
    sleep_ms(2000);

    printf("[SYS] Ready for interactive touchscreen gestures (0-9)...\n\n");

    char disp_buf[80];

    while (true) {
        if (multicore_fifo_rvalid()) {
            uint32_t msg = multicore_fifo_pop_blocking();

            if (msg == WRITE_SUCCESS_FLAG || msg == SD_UNAVAILABLE_FLAG) {
                TP_ShowStatus((msg == WRITE_SUCCESS_FLAG) ?
                              "Status: done (sd)" : "Status: done (no sd)", BLACK);

                char c1[16], c2[16], c3[16], c4[16], c5[16];
                snprintf(c1, sizeof(c1), (s_proc_result.feats.mean >= 100.0f) ?
                         "Mean: %.0f" : "Mean: %.1f", s_proc_result.feats.mean);
                snprintf(c2, sizeof(c2), "Edge: %.3f", s_proc_result.feats.active_edge_density);
                snprintf(c3, sizeof(c3), (s_proc_result.feats.aspect_ratio >= 10.0f) ?
                         "Asp: %.1f" : "Asp: %.2f", s_proc_result.feats.aspect_ratio);
                snprintf(c4, sizeof(c4), "L1: %luus", (unsigned long)s_proc_result.sobel_l1_us);
                snprintf(c5, sizeof(c5), "L2: %luus", (unsigned long)s_proc_result.sobel_l2_us);

                snprintf(disp_buf, sizeof(disp_buf),
                         "%-11.11s | %-11.11s | %-9.9s | %-11.11s | %-11.11s",
                         c1, c2, c3, c4, c5);
                TP_ShowTelemetry(disp_buf);
                TP_SetSaveBusy(false);
            } else if (msg == WRITE_FAILED_FLAG) {
                TP_ShowStatus("Status: write failed", RED);
                TP_SetSaveBusy(false);
            }
        }

        TP_DrawBoard();
    }

    return 0;
}
