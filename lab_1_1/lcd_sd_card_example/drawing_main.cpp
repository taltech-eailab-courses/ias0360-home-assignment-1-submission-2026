#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "ff.h"
#include "sd_card.h"
#include "hw_config.h"
#include "LCD_Driver.h"
#include "LCD_Touch.h"
#include "LCD_GUI.h"
#include "DEV_Config.h"

static mutex_t drawing_mutex;
static TP_DATA drawing = {};
static FATFS fs;

// save the current drawing as a binary pbm image and read it back for verification
// сохраняем рисунок в pbm и затем проверяем файл повторным чтением
static bool save_image() {
    sd_card_t *card = sd_get_by_num(0);
    if (!card) return false;
    const char *drive = sd_get_drive_prefix(card);
    if (!drive || f_mount(&fs, drive, 1) != FR_OK) return false;
    FIL file;
    // use the first free number so an older drawing is never overwritten
    // ищем первое свободное имя, чтобы не перезаписать старый рисунок
    char path[64]; FRESULT fr = FR_EXIST;
    for (unsigned i=0; i<10000 && fr==FR_EXIST; ++i) {
        snprintf(path, sizeof path, "%s/draw%04u.pbm",drive,i);
        fr=f_open(&file,path,FA_WRITE|FA_CREATE_NEW);
    }
    if (fr!=FR_OK) { f_unmount(drive); return false; }
    char header[40]; int hn=snprintf(header,sizeof header,"P4\n%d %d\n",BOX_W,BOX_H);
    UINT count=0; bool ok=f_write(&file,header,hn,&count)==FR_OK && count==(UINT)hn;
    // pbm stores eight black or white pixels in each byte
    // один байт pbm хранит восемь чёрно-белых пикселей
    uint8_t row[(BOX_W+7)/8];
    for (int y=0; ok && y<BOX_H; ++y) {
        memset(row,0,sizeof row);
        for (int x=0;x<BOX_W;++x)
            if (drawing.data[y*BOX_W+x]) row[x/8] |= 0x80 >> (x%8);
        ok=f_write(&file,row,sizeof row,&count)==FR_OK && count==sizeof row;
    }
    if(f_close(&file)!=FR_OK) ok=false;
    // compare the file with the source pixels instead of trusting f_write alone
    // сравниваем файл с исходным буфером, а не только результат f_write
    if(ok) {
        ok=f_open(&file,path,FA_READ)==FR_OK;
        if(ok) {
            char actual[40]={};
            ok=f_read(&file,actual,hn,&count)==FR_OK && count==(UINT)hn && !memcmp(header,actual,hn);
            for(int y=0;ok && y<BOX_H;++y) {
                ok=f_read(&file,row,sizeof row,&count)==FR_OK && count==sizeof row;
                for(int x=0;ok && x<BOX_W;++x)
                    if (!!(row[x/8] & (0x80>>(x%8))) != !!drawing.data[y*BOX_W+x]) ok=false;
            }
            if(f_close(&file)!=FR_OK) ok=false;
        }
    }
    if(f_unmount(drive)!=FR_OK) ok=false;
    printf("%s drawing %s (%dx%d), readback %s\n",ok?"PASS":"FAIL",path,BOX_W,BOX_H,ok?"verified":"failed");
    return ok;
}
static void sd_worker() {
    bool initialized=sd_init_driver();
    while(true) {
        uint32_t request=multicore_fifo_pop_blocking();
        if(request!=DATA_READY_FLAG) continue;
        // lock the buffer so drawing cannot change it during a save
        // блокируем буфер, чтобы рисунок не менялся во время записи
        mutex_enter_blocking(&drawing_mutex);
        bool ok=initialized && drawing.data && drawing.data_len==BOX_W*BOX_H && save_image();
        mutex_exit(&drawing_mutex);
        multicore_fifo_push_blocking(ok?TASK_COMPLETE_FLAG:WRITE_FAILED_FLAG);
    }
}
int main() {
    System_Init();
    mutex_init(&drawing_mutex);
    LCD_Init(SCAN_DIR_DFT,1000);
    TP_Init(SCAN_DIR_DFT,&drawing,&drawing_mutex);
    TP_GetAdFac();
    TP_Dialog();
    // the second core handles slow sd operations while core 0 reads touch input
    // второе ядро пишет sd, пока нулевое продолжает читать touch
    multicore_launch_core1(sd_worker);
    puts("DRAWING: draw inside the box; SAVE writes and verifies a new PBM file.");
    while(true) { TP_DrawBoard(); sleep_ms(5); }
}
