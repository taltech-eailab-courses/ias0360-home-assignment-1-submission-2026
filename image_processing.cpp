#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/sync.h"

#include "LCD_Driver.h"
#include "LCD_Touch.h"
#include "LCD_GUI.h"
#include "DEV_Config.h"

#include "ff.h"
#include "sd_card.h"
#include "f_util.h"
#include "hw_config.h"

#define PATH_MAX_LEN 256
#define IMAGE_WIDTH BOX_W
#define IMAGE_HEIGHT BOX_H
#define DOWNSAMPLED_WIDTH (IMAGE_WIDTH / 2)
#define DOWNSAMPLED_HEIGHT (IMAGE_HEIGHT / 2)

static mutex_t touch_mutex;
static TP_DATA tp_data = {0, NULL};

static FATFS fs;
static sd_card_t *g_sd = NULL;
static const char *g_drive = NULL;

// Replace these three bodies with the preprocessing algorithms from the assignment.
static void low_pass_filter(const uint8_t *input, uint8_t *output, size_t width, size_t height) {
    // Smooth the binary image with a 3x3 mean filter.
    for (size_t i = 0; i < height; ++i) {
        for (size_t j = 0; j < width; ++j) {
            if (i == 0 || i == height - 1 || j == 0 || j == width - 1) {
                output[i * width + j] = input[i * width + j];
            } else {
                int sum = 0;
                for (int row_offset = -1; row_offset <= 1; ++row_offset) {
                    for (int column_offset = -1; column_offset <= 1; ++column_offset) {
                        sum += input[(i + row_offset) * width + (j + column_offset)];
                    }
                }

                // Threshold the mean: a pixel is kept when most neighbours are set.
                if (sum >= 8) {
                    output[i * width + j] = 1;
                } else {
                    output[i * width + j] = 0;
                }
            }
        }
    }
}

static void high_pass_filter(const uint8_t *input, uint8_t *output, size_t width, size_t height) {
    // Detect edges with a high-pass filter.
    for (size_t i = 0; i < height; ++i) {
        for (size_t j = 0; j < width; ++j) {
            if (i == 0 || i == height - 1 || j == 0 || j == width - 1) {
                output[i * width + j] = input[i * width + j];
            } else {
                int sum = 0;
                for (int row_offset = -1; row_offset <= 1; ++row_offset) {
                    for (int column_offset = -1; column_offset <= 1; ++column_offset) {
                        int multiplier;
                        if (column_offset == 0 && row_offset == 0){
                            multiplier = 8;
                        }else{
                            multiplier = -1;
                        }
                        sum += input[(i + row_offset) * width + (j + column_offset)]*multiplier;
                    }
                }

                // Threshold the mean: a pixel is kept when most neighbours are set.
                if (sum >= 3) {
                    output[i * width + j] = 1;
                } else {
                    output[i * width + j] = 0;
                }
            }
        }
    }
}

static bool high_pass_filter_in_place(uint8_t *image, size_t width, size_t height) {
    if (width < 3 || height < 3) {
        return true;
    }

    uint8_t *rows = (uint8_t *)malloc(width * 3);
    if (!rows) {
        return false;
    }

    uint8_t *previous = rows;
    uint8_t *current = rows + width;
    uint8_t *next = rows + width * 2;

    memcpy(previous, image, width);
    memcpy(current, image + width, width);
    memcpy(next, image + width * 2, width);

    for (size_t y = 1; y + 1 < height; ++y) {
        for (size_t x = 0; x < width; ++x) {
            if (x == 0 || x + 1 == width) {
                image[y * width + x] = current[x];
                continue;
            }

            int sum = 8 * current[x]
                - previous[x - 1] - previous[x] - previous[x + 1]
                - current[x - 1] - current[x + 1]
                - next[x - 1] - next[x] - next[x + 1];
            image[y * width + x] = sum >= 3 ? 1 : 0;
        }

        uint8_t *old_previous = previous;
        previous = current;
        current = next;
        next = old_previous;

        if (y + 2 < height) {
            memcpy(next, image + (y + 2) * width, width);
        }
    }

    free(rows);
    return true;
}

static void downsample(const uint8_t *input, uint8_t *output, size_t width, size_t height, int downsample_factor) {
    for (size_t i = 0; i < height; i=i+downsample_factor) {
        for (size_t j = 0; j < width; j=j+downsample_factor) {
            int sum = 0;
            for (int row_offset = 0; row_offset < downsample_factor; ++row_offset) {
                for (int column_offset = 0; column_offset < downsample_factor; ++column_offset) {                    
                    sum += input[(i + row_offset) * width + (j + column_offset)];
                }
            }

            // Threshold the mean: a pixel is kept when most neighbours are set.
            if (sum > (downsample_factor * downsample_factor) / 2) {
                for (int row_offset = 0; row_offset < downsample_factor; ++row_offset) {
                    for (int column_offset = 0; column_offset < downsample_factor; ++column_offset) {
                        output[(i + row_offset) * width + (j + column_offset)] = 1;
                    }
                }
            }else{
                for (int row_offset = 0; row_offset < downsample_factor; ++row_offset) {
                    for (int column_offset = 0; column_offset < downsample_factor; ++column_offset) {
                        output[(i + row_offset) * width + (j + column_offset)] = 0;
                    }
                }
            }
        }
    }
}

static void join_path(char *out, size_t out_size, const char *drive, const char *relative_path) {
    if (relative_path && relative_path[0] == '/') {
        ++relative_path;
    }

    if (drive && drive[strlen(drive) - 1] == '/') {
        snprintf(out, out_size, "%s%s", drive,
                 relative_path ? relative_path : "");
    } else {
        snprintf(out, out_size, "%s/%s", drive,
                 relative_path ? relative_path : "");
    }
}

static bool sd_init_and_mount(void) {
    if (!sd_init_driver()) {
        printf("sd_init_driver() failed\n");
        return false;
    }

    g_sd = sd_get_by_num(0);
    if (!g_sd) {
        return false;
    }

    g_drive = sd_get_drive_prefix(g_sd);
    if (!g_drive) {
        return false;
    }

    FRESULT result = f_mount(&fs, g_drive, 1);
    if (result == FR_NO_FILESYSTEM) {
        BYTE work[4096];
        MKFS_PARM options = {FM_FAT | FM_SFD, 0, 0, 0, 0};
        result = f_mkfs(g_drive, &options, work, sizeof(work));
        if (result == FR_OK) {
            result = f_mount(&fs, g_drive, 1);
        }
    }

    return result == FR_OK;
}

static bool write_bitmap_file(const char *name, const uint8_t *image, size_t width, size_t height) {
    char path[PATH_MAX_LEN];
    join_path(path, sizeof(path), g_drive, name);

    FIL file;
    FRESULT result = f_open(&file, path, FA_WRITE | FA_CREATE_ALWAYS);
    if (result != FR_OK) {
        printf("Could not open %s: %s (%d)\n", path, FRESULT_str(result), result);
        return false;
    }

    char row[IMAGE_WIDTH + 1];
    bool success = true;
    for (size_t y = 0; y < height && success; ++y) {
        for (size_t x = 0; x < width; ++x) {
            row[x] = image[y * width + x] ? '1' : '0';
        }
        row[width] = '\n';

        UINT written = 0;
        result = f_write(&file, row, (UINT)(width + 1), &written);
        success = result == FR_OK && written == width + 1;
    }

    if (success) {
        success = f_sync(&file) == FR_OK;
    }
    f_close(&file);

    if (success) {
        printf("Saved %s\n", path);
    } else {
        printf("Could not write %s\n", path);
    }
    return success;
}

static bool save_capture(uint32_t capture_number) {
    if (!tp_data.data || tp_data.data_len != IMAGE_WIDTH * IMAGE_HEIGHT) {
        printf("No complete drawing is available\n");
        return false;
    }

    char name[64];
    uint8_t *processed = (uint8_t *)malloc(IMAGE_WIDTH * IMAGE_HEIGHT);

    if (!processed) {
        printf("Could not allocate preprocessing buffer\n");
        return false;
    }

    /* ORIGINAL */
    snprintf(name, sizeof(name), "%03lu_original.txt", (unsigned long)capture_number);
    bool success = write_bitmap_file(name, tp_data.data, IMAGE_WIDTH, IMAGE_HEIGHT);
    uint32_t start_us;
    uint32_t low_pass_us;
    uint32_t combined_high_pass_us;
    uint32_t high_pass_us;
    uint32_t downsample_2_us;
    uint32_t downsample_4_us;
    uint32_t downsample_8_us;

    /* LOW PASS FILTER */
    start_us = time_us_32();
    low_pass_filter(tp_data.data, processed, IMAGE_WIDTH, IMAGE_HEIGHT);
    low_pass_us = time_us_32() - start_us;
    snprintf(name, sizeof(name), "%03lu_low_pass.txt", (unsigned long)capture_number);
    success = write_bitmap_file(name, processed, IMAGE_WIDTH, IMAGE_HEIGHT) && success;

    /* LOW PASS + HIGH PASS */
    start_us = time_us_32();
    success = high_pass_filter_in_place(processed, IMAGE_WIDTH, IMAGE_HEIGHT) && success;
    combined_high_pass_us = time_us_32() - start_us;
    snprintf(name, sizeof(name), "%03lu_low_pass_high_pass.txt", (unsigned long)capture_number);
    success = write_bitmap_file(name, processed, IMAGE_WIDTH, IMAGE_HEIGHT) && success;

    /* HIGH PASS FILTER */
    start_us = time_us_32();
    high_pass_filter(tp_data.data, processed, IMAGE_WIDTH, IMAGE_HEIGHT);
    high_pass_us = time_us_32() - start_us;
    snprintf(name, sizeof(name), "%03lu_high_pass.txt", (unsigned long)capture_number);
    success = write_bitmap_file(name, processed, IMAGE_WIDTH, IMAGE_HEIGHT) && success;

    /* DOWNSAMPLE BY TWO */
    start_us = time_us_32();
    downsample(tp_data.data, processed, IMAGE_WIDTH, IMAGE_HEIGHT, 2);
    downsample_2_us = time_us_32() - start_us;
    snprintf(name, sizeof(name), "%03lu_downsampled_2.txt", (unsigned long)capture_number);
    success = write_bitmap_file(name, processed, IMAGE_WIDTH, IMAGE_HEIGHT) && success;

    /* DOWNSAMPLE BY FOUR */
    start_us = time_us_32();
    downsample(tp_data.data, processed, IMAGE_WIDTH, IMAGE_HEIGHT, 4);
    downsample_4_us = time_us_32() - start_us;
    snprintf(name, sizeof(name), "%03lu_downsampled_4.txt", (unsigned long)capture_number);
    success = write_bitmap_file(name, processed, IMAGE_WIDTH, IMAGE_HEIGHT) && success;

    /* DOWNSAMPLE BY EIGHT */
    start_us = time_us_32();
    downsample(tp_data.data, processed, IMAGE_WIDTH, IMAGE_HEIGHT, 8);
    downsample_8_us = time_us_32() - start_us;
    snprintf(name, sizeof(name), "%03lu_downsampled_8.txt", (unsigned long)capture_number);
    success = write_bitmap_file(name, processed, IMAGE_WIDTH, IMAGE_HEIGHT) && success;
    
    free(processed);

    printf("Timings us: low=%lu combined-high=%lu high=%lu down2=%lu down4=%lu down8=%lu\n",
           (unsigned long)low_pass_us,
           (unsigned long)combined_high_pass_us,
           (unsigned long)high_pass_us,
           (unsigned long)downsample_2_us,
           (unsigned long)downsample_4_us,
           (unsigned long)downsample_8_us);
    return success;
}

static void core1_entry(void) {
    printf("Core 1 initialized: waiting for drawing captures\n");

    if (!sd_init_and_mount()) {
        printf("Core 1: SD mount failed\n");
        multicore_fifo_push_blocking(WRITE_FAILED_FLAG);
        while (true) {
            tight_loop_contents();
        }
    }

    uint32_t capture_number = 0;
    while (true) {
        uint32_t message = multicore_fifo_pop_blocking();
        if (message != DATA_READY_FLAG) {
            continue;
        }

        mutex_enter_blocking(&touch_mutex);
        bool success = save_capture(capture_number);
        mutex_exit(&touch_mutex);

        if (success) {
            ++capture_number;
            multicore_fifo_push_blocking(TASK_COMPLETE_FLAG);
        } else {
            multicore_fifo_push_blocking(WRITE_FAILED_FLAG);
        }
    }
}

int main(void) {
    stdio_init_all();
    System_Init();
    mutex_init(&touch_mutex);

    LCD_SCAN_DIR lcd_scan_dir = SCAN_DIR_DFT;
    LCD_Init(lcd_scan_dir, 1000);
    TP_Init(lcd_scan_dir, &tp_data, &touch_mutex);
    TP_GetAdFac();
    TP_Dialog();

    multicore_launch_core1(core1_entry);

    while (true) {
        LCD_SetBackLight(1000);
        TP_DrawBoard();

        if (multicore_fifo_rvalid()) {
            uint32_t message = multicore_fifo_pop_blocking();
            if (message == TASK_COMPLETE_FLAG) {
                printf("Capture saved: original + 5 preprocessed images\n");
            } else if (message == WRITE_FAILED_FLAG) {
                printf("Capture failed: check the SD card and serial output\n");
            }
        }
    }
}
