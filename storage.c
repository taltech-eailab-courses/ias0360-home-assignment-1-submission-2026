#include "storage.h"
#include "storage.h"
#include "ff.h"
#include "sd_card.h"
#include "f_util.h"
#include "hw_config.h"
#include "pico/stdlib.h"
#include "stdio.h"
#include "string.h"

static FATFS fs;
static FIL log_file;
static motion_state_t last_logged_state = MOTION_STILL;
MKFS_PARM opt = { FM_FAT | FM_SFD, 0, 0, 0, 0 };
BYTE work[4096];

bool storage_init(void) {
    // TODO: sd_init_driver(), sd_get_by_num(0), sd_get_drive_prefix() —
    //       same three calls as sd_card_example's sd_init_and_mount()
    sd_init_driver(); // Initalize SD card driver.
    sd_card_t *sd = sd_get_by_num(0);
    const char *drive = sd_get_drive_prefix(sd);

    // TODO: f_mount(&fs, drive, 1); if it returns FR_NO_FILESYSTEM, fall
    //       back to f_mkfs then f_mount again — same pattern as the example
    FRESULT fr = f_mount(&fs, drive, 1);
    if(fr == FR_NO_FILESYSTEM){
        fr = f_mkfs(drive, &opt, work, sizeof work);
        if(fr == FR_OK){
            fr = f_mount(&fs, drive, 1);
        }
        
    }else{
       
    }

    // TODO: build a path like "<drive>/motion_log.txt" and f_open it with
    //       FA_WRITE | FA_CREATE_ALWAYS into the static log_file — this
    //       starts a fresh file each boot (one "session"), not an
    //       accumulating history across reboots
    char *path = "0:/motion_log.txt";
    FRESULT fr2 = f_open(&log_file, path, FA_WRITE | FA_CREATE_ALWAYS);

    // TODO: return true if mount+open both succeeded, false otherwise
    if((fr == FR_OK) && (fr2 == FR_OK)){
        return true;
    } else {
        return false;
    }
}
void storage_log_transition(motion_state_t state, float stddev) {
    // TODO: if state == last_logged_state, this isn't a new transition
    //       return immediately, don't touch the file
    if(state == last_logged_state){
        return;
    }

    // TODO: update last_logged_state = state
    last_logged_state = state;

    // TODO: build a text line into a small char buffer with snprintf,
    //       e.g. "%lu,%s,%f\n" using to_ms_since_boot(get_absolute_time
    //       for the timestamp and a short STILL/MOVING/WALKING/RUNNING
    //       string for the state (same if/else-if style you already use
    //       in main.c to pick which printf to run)
            char line[64];
            
            uint32_t timestamp_ms = to_ms_since_boot(get_absolute_time()); // Estalbish timestamp for the log.
            const char *state_name; // Declare a char that could hold the state value.
            if(state == MOTION_STILL){ // Condition for state classifying.
                state_name = "STILL";
            }else if(state == MOTION_MOVING){
                state_name = "MOTION_MOVING";
            }else if(state == MOTION_WALKING){
                state_name = "MOTION_WALKING";
            }else if(state == MOTION_RUNNING){
                state_name = "MOTION_RUNNING";
            }else{
                state_name = "UNKNOWN";
            }
            int len = snprintf(line, sizeof(line), "%lu,%s,%f\n", timestamp_ms, state_name, stddev);
                

    // TODO: f_write the buffer to log_file, then f_sync(&log_file)
    UINT bytes_written;
    FRESULT fr2 = f_write(&log_file, line, len, &bytes_written);
    if((fr2 == FR_OK) && (bytes_written == (UINT)len)){
        f_sync(&log_file);
    }else{
        printf("Err: Write error or full card\n");
    }
    }
    