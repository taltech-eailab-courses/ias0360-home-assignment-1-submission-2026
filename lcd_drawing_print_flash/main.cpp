#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "hardware/watchdog.h"

#include "ff.h"
#include "sd_card.h"
#include "f_util.h"
#include "hw_config.h"
#include "LCD_Driver.h"
#include "LCD_Touch.h"
#include "LCD_GUI.h"
#include "DEV_Config.h"

#include "hardware/rtc.h"

// Integration 
#include "lib/lab12_integrated/lab12_filters.h"
#include "lib/lab12_integrated/lab12_sd.h"

#define PATH_MAX_LEN 256
#define MAX_FILE_WRITE 200


mutex_t mutex;
TP_DATA tp_data;

// --------- Globals (FatFs requires the FS to outlive the mount) ----------
static FATFS fs;                 // must be static/global (lives as long as the mount)
static sd_card_t *g_sd = NULL;   // active SD card
static const char *g_drive = NULL; // typically "0:"

// ------------------------- Utility / Error -------------------------------
static void die(FRESULT fr, const char *op) {
    printf("%s failed: %s (%d)\n", op, FRESULT_str(fr), fr);
    multicore_fifo_push_blocking(WRITE_FAILED_FLAG);
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
        BYTE work[4096]; // >= FF_MAX_SS
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
    
    printf("write_to_file: About to write %u bytes from pointer %p\n", len, data);
    printf("write_to_file: First few bytes: %02X %02X %02X %02X\n",
           ((uint8_t*)data)[0], ((uint8_t*)data)[1], 
           ((uint8_t*)data)[2], ((uint8_t*)data)[3]);
    
    FRESULT fr = f_write(file, data, len, bytes_written);
    printf("write_to_file: f_write returned FR=%d, wrote %u bytes\n", fr, *bytes_written);
    
    if (fr == FR_OK) {
        fr = f_sync(file); // ensure data hits the card
        printf("write_to_file: f_sync returned FR=%d\n", fr);
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

// image creation / saving function

static FRESULT write_all(FIL *file, const void *data, UINT len)
{
    UINT bw = 0;
    FRESULT fr = f_write(file, data, len, &bw);

    if (fr != FR_OK) {
        return fr;
    }

    if (bw != len) {
        return FR_DISK_ERR;
    }

    return FR_OK;
}

static void le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static FRESULT write_bitmap_as_bmp(
    FIL *file,
    const uint8_t *bitmap,
    uint32_t width,
    uint32_t height
)
{
    if (file == NULL || bitmap == NULL || width == 0 || height == 0) {
        return FR_INVALID_OBJECT;
    }

    // BMP 24 bits : 3 octets par pixel (B, G, R)
    uint32_t row_bytes_no_pad = width * 3;
    uint32_t row_padding = (4 - (row_bytes_no_pad % 4)) % 4;
    uint32_t row_stride = row_bytes_no_pad + row_padding;
    uint32_t pixel_data_size = row_stride * height;
    uint32_t file_size = 54 + pixel_data_size;

    uint8_t header[54];
    memset(header, 0, sizeof(header));

    // Signature BMP
    header[0] = 'B';
    header[1] = 'M';

    // Taille fichier
    le32(&header[2], file_size);

    // Offset vers pixels
    le32(&header[10], 54);

    // Taille DIB header
    le32(&header[14], 40);

    // Largeur / hauteur
    le32(&header[18], width);
    le32(&header[22], height);

    // Plans
    le16(&header[26], 1);

    // Bits par pixel
    le16(&header[28], 24);

    // Taille des données pixels
    le32(&header[34], pixel_data_size);

    FRESULT fr = write_all(file, header, sizeof(header));
    if (fr != FR_OK) {
        return fr;
    }

    uint8_t *row = (uint8_t *)malloc(row_stride);
    if (row == NULL) {
        return FR_NOT_ENOUGH_CORE;
    }

    // BMP stocke les lignes de bas en haut
    for (int32_t y = (int32_t)height - 1; y >= 0; y--) {

        for (uint32_t x = 0; x < width; x++) {
            uint8_t pixel = bitmap[(uint32_t)y * width + x];

            // 1 = noir dessiné, 0 = blanc fond
            uint8_t c = pixel ? 0x00 : 0xFF;

            row[x * 3 + 0] = c; // B
            row[x * 3 + 1] = c; // G
            row[x * 3 + 2] = c; // R
        }

        for (uint32_t p = 0; p < row_padding; p++) {
            row[row_bytes_no_pad + p] = 0x00;
        }

        fr = write_all(file, row, row_stride);
        if (fr != FR_OK) {
            free(row);
            return fr;
        }
    }

    free(row);
    return FR_OK;
}


// ------------------------------ Main -------------------------------------

// void core1_entry() {

//     printf("Core 1 entry: write to SD card\n");
//     sleep_ms(2000);

//     // 1) Init + mount
//     if (!sd_init_and_mount()) {
//         loop_forever_msg("SD init/mount failed.");
//     }

//     int count = 0;
//     FRESULT fr;

//     while (count < MAX_FILE_WRITE) {

//         uint32_t msg = multicore_fifo_pop_blocking();
//         if (msg != DATA_READY_FLAG) {
//             printf("Core 1 received unexpected message: 0x%08lX\n", msg);
//             continue;
//         }

//         printf("Writing data to a file\n");

//         // Build absolute file path: <drive>/lcd_sd_card_example_<iteration>.txt
//         char path[PATH_MAX_LEN];
//         char name[64];
//         snprintf(name, sizeof(name), "lcd_sd_card_example_%d.txt", count);
//         join_path(path, sizeof path, g_drive, name);

//         printf("Core 1: Creating and writing to file: %s\n", path);
//         // 2) Create the file
//         FIL f;
//         fr = create_file(path, &f);
//         if (fr != FR_OK) die(fr, "f_open(create)");

//         mutex_enter_blocking(&mutex);

//         // 3) Write data
//         UINT bw = 0;

//         // Check if data is valid
//         printf("Core 1: tp_data.data_len = %zu\n", tp_data.data_len);
//         printf("Core 1: tp_data.data pointer = %p\n", (void*)tp_data.data);
        
//         if (tp_data.data == NULL || tp_data.data_len == 0) {
//             printf("ERROR: tp_data.data is NULL or data_len is 0!\n");
//             mutex_exit(&mutex);
//             f_close(&f);
//             continue;
//         }

//         // Print all the data stored in tp_data.data
//         printf("Data contents (%zu bytes): ", tp_data.data_len);
//         for (size_t i = 0; i < tp_data.data_len; i++) {
//             printf("%u ", tp_data.data[i]);
//             if ((i + 1) % BOX_W == 0) printf("\n");
//         }
//         if (tp_data.data_len % 16 != 0) printf("\n");

//         // Convert binary 0/1 to ASCII '0'/'1' for human-readable text file
//         char *ascii_buffer = (char *)malloc(tp_data.data_len);
//         if (ascii_buffer == NULL) {
//             printf("ERROR: Failed to allocate ASCII buffer\n");
//             mutex_exit(&mutex);
//             f_close(&f);
//             die(FR_NOT_ENOUGH_CORE, "malloc");
//         }
        
//         for (size_t i = 0; i < tp_data.data_len; i++) {
//             ascii_buffer[i] = tp_data.data[i] ? '1' : '0';  // Convert to ASCII '0' or '1'
//         }
        
//         fr = write_to_file(&f, ascii_buffer, (UINT)tp_data.data_len, &bw);
//         free(ascii_buffer);
        
//         printf("Core 1: write_to_file returned FR=%d, bytes_written=%u (expected %zu)\n", 
//                fr, bw, tp_data.data_len);
//         if (fr != FR_OK || bw != tp_data.data_len) {
//             printf("ERROR: Write failed or incomplete! FR=%d, wrote %u/%zu bytes\n", 
//                    fr, bw, tp_data.data_len);
//             die(fr, "f_write/f_sync");
//         }
        
//         // // Close the file
//         // f_close(&f);


//         // mutex_exit(&mutex);
        
//         // count++;
//         // printf("----- File write iteration %d -----\n", count);

//         // Close the file

//         // New way to close the file and handle errors (possibility to save another file )
//         fr = f_close(&f);

//         if (fr != FR_OK) {
//             die(fr, "f_close");
//         }

//         mutex_exit(&mutex);

//         // La sauvegarde est terminée.
//         // Un nouvel appui sur SAVE est maintenant autorisé.
//         TP_SetSavePending(false);

//         count++;

//         printf(
//             "----- File write iteration %d -----\n",
//             count
//         );
//     }

//     // Optional: unmount
//     fr = f_unmount(g_drive);
//     printf("f_unmount -> %s (%d)\n", FRESULT_str(fr), fr);

//     sleep_ms(1000);  // optional flush delay
//     multicore_fifo_push_blocking(TASK_COMPLETE_FLAG); // acknowledge successful send

//     printf("Core 1 task complete.\n");

//     while (1) { tight_loop_contents(); }
// }

void core1_entry() {

    sleep_ms(2000);

    if (!sd_init_and_mount()) {
        loop_forever_msg("SD init/mount failed.");
    }

    int count = 0;
    FRESULT fr;

    while (count < MAX_FILE_WRITE) {

        uint32_t msg = multicore_fifo_pop_blocking();
        if (msg != DATA_READY_FLAG) {
            continue;
        }

        char path[PATH_MAX_LEN];
        char name[64];
        char timestamp[32];

        datetime_t now;

        /*
         * Create one base timestamp for every output generated from
         * the same drawing. The raw BMP and all Lab 1.2 files will
         * therefore share the same prefix.
         */
        if (rtc_get_datetime(&now))
        {
            snprintf(
                timestamp,
                sizeof(timestamp),
                "%04d%02d%02d_%02d%02d%02d",
                now.year,
                now.month,
                now.day,
                now.hour,
                now.min,
                now.sec
            );
        }
        else
        {
            /*
             * Fallback if the RTC has not been synchronized yet.
             */
            snprintf(
                timestamp,
                sizeof(timestamp),
                "image_%d",
                count
            );
        }

        /*
         * The original captured image is explicitly marked as "raw".
         * Example: 20260928_115500_raw.bmp
         */
        snprintf(
            name,
            sizeof(name),
            "%s_raw.bmp",
            timestamp
        );

        join_path(path, sizeof(path), g_drive, name);

        FIL f;
        fr = create_file(path, &f);
        if (fr != FR_OK) {
            die(fr, "f_open(create bmp)");
        }

        mutex_enter_blocking(&mutex);

        if (tp_data.data == NULL || tp_data.data_len == 0) {
            mutex_exit(&mutex);
            f_close(&f);
            TP_SetSavePending(false);
            continue;
        }

        if (tp_data.data_len != (size_t)(BOX_W * BOX_H)) {
            mutex_exit(&mutex);
            f_close(&f);
            TP_SetSavePending(false);
            continue;
        }

        fr = write_bitmap_as_bmp(
            &f,
            tp_data.data,
            BOX_W,
            BOX_H
        );

        if (fr != FR_OK) {
            mutex_exit(&mutex);
            f_close(&f);
            die(fr, "write_bitmap_as_bmp");
        }

        fr = f_sync(&f);
        if (fr != FR_OK) {
            mutex_exit(&mutex);
            f_close(&f);
            die(fr, "f_sync");
        }

        fr = f_close(&f);
        if (fr != FR_OK) {
            mutex_exit(&mutex);
            die(fr, "f_close");
        }

        mutex_exit(&mutex);

        char base_path[PATH_MAX_LEN];

        snprintf(
            base_path,
            sizeof(base_path),
            "%s/%s",
            g_drive,
            timestamp
        );


        // ============================================================
        // Quantization
        // ============================================================

        fr = lab12_save_quantization(
            base_path,
            tp_data.data,
            tp_data.data_len
        );

        if (fr != FR_OK) {
            // gestion erreur
        }


        // ============================================================
        // Statistics
        // ============================================================

        fr = lab12_save_statistics(
            base_path,
            tp_data.data,
            tp_data.data_len
        );

        if (fr != FR_OK) {
            // gestion erreur
        }


        // ============================================================
        // FFT
        // ============================================================

        fr = lab12_save_fft(
            base_path,
            tp_data.data,
            BOX_W,
            BOX_H
        );

        if (fr != FR_OK) {
            // gestion erreur
        }


        // ============================================================
        // Sobel
        // ============================================================

        fr = lab12_save_sobel_bmp(
            base_path,
            tp_data.data,
            BOX_W,
            BOX_H
        );

        if (fr != FR_OK) {
            // gestion erreur
        }


        // Tout est terminé
        TP_SetSavePending(false);

        count++;
    }

    while (1) {
        tight_loop_contents();
    }
}


int main(void) {

    System_Init();
    rtc_init();
    sleep_ms(1000);

    mutex_init(&mutex);  // Initialize the mutex

	LCD_SCAN_DIR  lcd_scan_dir = SCAN_DIR_DFT;
	LCD_Init(lcd_scan_dir,1000);
	TP_Init(lcd_scan_dir, &tp_data, &mutex);
	TP_GetAdFac();
	TP_Dialog();

    multicore_launch_core1(core1_entry);

    bool save_was_pending = false; // test

	while(1){
        if (multicore_fifo_rvalid()) {
            uint32_t msg = multicore_fifo_pop_blocking();
            if (msg == TASK_COMPLETE_FLAG) {
                printf("Core 0: Core 1 task complete.\n");
                break;
            } else if (msg == WRITE_FAILED_FLAG) {
                printf("Core 0: Core 1 reported write failure.\n");
                loop_forever_msg("Write failed on Core 1.");
            }
        } 
        // else {
        //     // Pendant une écriture SD, Core 0 ne touche pas au SPI.
        //     if (!TP_IsSavePending()) {
        //         LCD_SetBackLight(1000);
        //         TP_DrawBoard();
        //     }

        //     tight_loop_contents();
        // }
        else {

            bool save_pending =
                TP_IsSavePending();


            // --------------------------------------------------------
            // Sauvegarde en cours
            // --------------------------------------------------------

            if (save_pending)
            {
                /*
                * Ne surtout pas appeler TP_DrawBoard().
                *
                * Core 1 utilise le SPI pour écrire sur la SD.
                */
                save_was_pending = true;

                tight_loop_contents();

                continue;
            }


            // --------------------------------------------------------
            // La sauvegarde vient de finir
            // --------------------------------------------------------

            if (save_was_pending)
            {
                TP_ShowSaveButton();

                save_was_pending = false;
            }


            // --------------------------------------------------------
            // Fonctionnement normal
            // --------------------------------------------------------

            LCD_SetBackLight(1000);

            TP_DrawBoard();
        }
	}

    printf("All tasks complete.\n");
    multicore_reset_core1();

    printf("Exiting main().\n");
    
    return 0;
}
