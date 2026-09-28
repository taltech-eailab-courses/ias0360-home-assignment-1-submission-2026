#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "pico/util/queue.h"
#include "hardware/i2c.h"
#include "hardware/pio.h"
#include "ff.h"
#include "sd_card.h"
#include "hw_config.h"
#include "f_util.h"

// raw values use +/-2 g for acceleration and +/-1000 degrees/s for rotation
// core 1 reads i2c while core 0 writes queued samples to the card
//первое ядро читает датчик, а нулевое записывает очередь на карту
constexpr uint32_t PERIOD_US = 1600; // 625 Hz, margin above 500 Hz
constexpr uint32_t DURATION_US = 30000000;
struct Sample { uint32_t seq, us; int16_t v[6]; };
static queue_t samples;
static FATFS fs;
static struct { uint32_t missed, errors, dropped; } stats;
alignas(4) static char block[8192];
static char result[256];

// these helpers read and write the selected register in bank 0
//эти функции читают и записывают отдельные регистры датчика
static bool wr(uint8_t reg, uint8_t value) {
    uint8_t b[] = {reg, value};
    return i2c_write_timeout_us(i2c1, 0x68, b, 2, false, 1000) == 2;
}
static bool rd(uint8_t reg, uint8_t *b, size_t n) {
    return i2c_write_timeout_us(i2c1, 0x68, &reg, 1, true, 1000) == 1 &&
           i2c_read_timeout_us(i2c1, 0x68, b, n, false, 1000) == (int)n;
}
static bool init_imu() {
    i2c_init(i2c1, 400000);
    gpio_set_function(6, GPIO_FUNC_I2C); gpio_set_function(7, GPIO_FUNC_I2C);
    gpio_pull_up(6); gpio_pull_up(7);
    uint8_t id = 0;
    if (!wr(0x7f, 0) || !rd(0, &id, 1) || id != 0xea) return false;
    if (!wr(6, 0x80)) return false;
    sleep_ms(100);
    if (!wr(6, 1) || !wr(7, 0) || !wr(0x7f, 0x20)) return false;
    // bank 2 holds the sample divider, filter and measurement range settings
    //во втором банке находятся частота, фильтр и диапазоны измерений
    if (!wr(0, 0) || !wr(1, 0x15) || !wr(0x10, 0) ||
        !wr(0x11, 0) || !wr(0x14, 0x11) || !wr(0x7f, 0)) return false;
    sleep_ms(100);
    return true;
}
static void reader() {
    if (!init_imu()) {
        stats.errors++;
    } else {
        const uint64_t start = time_us_64();
        for (uint32_t i = 0; i < DURATION_US / PERIOD_US; ++i) {
            uint64_t due = start + (uint64_t)i * PERIOD_US;
            sleep_until(from_us_since_boot(due));
            uint64_t now = time_us_64();
            if (now >= due + PERIOD_US) { stats.missed++; continue; }
            Sample s = {}; s.seq = i; s.us = (uint32_t)(now - start);
            uint8_t b[12];
            if (!rd(0x2d, b, sizeof b)) { stats.errors++; continue; }
            for (int j=0; j<6; ++j) s.v[j] = (int16_t)((uint16_t)b[j*2]<<8 | b[j*2+1]);
            //не останавливаем измерения, даже если запись временно медленнее
            if (!queue_try_add(&samples, &s)) stats.dropped++;
        }
        sleep_until(from_us_since_boot(start + DURATION_US));
    }
    Sample end = {}; end.seq = UINT32_MAX;
    queue_add_blocking(&samples, &end);
    while (true) tight_loop_contents();
}
static bool write_block(FIL *file, size_t n) {
    UINT written = 0;
    FRESULT status = f_write(file, block, n, &written);
    return status == FR_OK && written == n;
}
static void run() {
    puts("Initializing SD card...");
    if (!sd_init_driver()) { snprintf(result,sizeof result,"FAIL SD initialization"); return; }
    sd_card_t *card = sd_get_by_num(0);
    if (!card) { snprintf(result,sizeof result,"FAIL SD configuration"); return; }
    const char *drive = sd_get_drive_prefix(card);
    if (!drive) { snprintf(result,sizeof result,"FAIL SD drive"); return; }
    FRESULT fr = f_mount(&fs, drive, 1);
    if (fr != FR_OK) { snprintf(result,sizeof result,"FAIL mount: %s",FRESULT_str(fr)); return; }
    FIL file; char path[64];
    // create a new file without overwriting an earlier experiment
    //выбираем новое имя, чтобы не удалить прошлый эксперимент
    fr = FR_EXIST;
    for (unsigned i=0; i<10000 && fr==FR_EXIST; ++i) {
        snprintf(path,sizeof path,"%s/imu%04u.csv",drive,i);
        fr=f_open(&file,path,FA_WRITE|FA_CREATE_NEW);
    }
    if (fr!=FR_OK) { snprintf(result,sizeof result,"FAIL create: %s",FRESULT_str(fr)); f_unmount(drive); return; }
    strcpy(block,"seq,t_us,ax,ay,az,gx,gy,gz\n");
    size_t used = strlen(block);
    bool ok = true;
    uint32_t saved = 0;
    queue_init(&samples,sizeof(Sample),2048);
    puts("Recording for 30 seconds. Move the board gently. Do not disconnect.");
    uint64_t start = time_us_64(), report = start;
    multicore_launch_core1(reader);
    while (true) {
        Sample s;
        uint64_t wait_start = time_us_64();
        while (!queue_try_remove(&samples, &s)) {
            //do not service CYW43 while the acquisition is active.
            if (time_us_64() - wait_start > 5000000) {
                puts("FAIL: no samples from core 1 for 5 seconds; stopping test" );
                multicore_reset_core1();
                f_close(&file); f_unmount(drive);
                snprintf(result, sizeof result, "FAIL core 1 timeout; partial CSV closed" );
                return;
            }
            sleep_us(100);
        }
        if (s.seq==UINT32_MAX) break;
        if (ok) {
            char line[128];
            int n=snprintf(line,sizeof line,"%lu,%lu,%d,%d,%d,%d,%d,%d\n",
                (unsigned long)s.seq,(unsigned long)s.us,s.v[0],s.v[1],s.v[2],s.v[3],s.v[4],s.v[5]);
        //larger writes reduce the filesystem overhead for every sample
            if (used+(size_t)n>sizeof block) { ok=write_block(&file,used); used=0; }
                      //записываем блоками, потому что это быстрее отдельных строк
            if (ok) { memcpy(block+used,line,n); used+=n; saved++; }
        }
        if (time_us_64()-report>=1000000) {
            printf("Recording: %lu samples processed\n",(unsigned long)saved);
            report=time_us_64();
        }
    }
    if (ok && used) ok=write_block(&file,used);
    FRESULT closed=f_close(&file);
    ok=ok && closed==FR_OK;
    double seconds=(time_us_64()-start)/1000000.0;
    puts("Verifying saved CSV...");

    //читаем файл повторно и проверяем порядок сохранённых измерений
    uint32_t verified=0, prev=0, prev_us=0, gaps=0;
    bool valid=ok;
    if (ok) {
        fr=f_open(&file,path,FA_READ);
        valid=fr==FR_OK;
        if (valid) {
            char line[128];
            valid=f_gets(line,sizeof line,&file)!=nullptr && strcmp(line,"seq,t_us,ax,ay,az,gx,gy,gz\n")==0;
            while (valid && f_gets(line,sizeof line,&file)) {
                unsigned long seq,us; int a,b,c,d,e,f;
                if (sscanf(line,"%lu,%lu,%d,%d,%d,%d,%d,%d",&seq,&us,&a,&b,&c,&d,&e,&f)!=8) { valid=false; break; }
                if ((verified && (seq!=prev+1 || us<=prev_us)) || (!verified && seq!=0)) gaps++;
                prev=seq; prev_us=us; verified++;
            }
            if (f_error(&file)) valid=false;
             if (f_close(&file)!=FR_OK) valid=false;
        }
    }
    if (f_unmount(drive)!=FR_OK) valid=false;
    double hz=verified/seconds;
    //a pass means that acquisition, storage and readback all succeeded
    //pass выдаётся только если сбор, запись и чтение прошли без ошибок
    bool pass=valid && verified==saved && verified==DURATION_US/PERIOD_US &&
        !gaps && !stats.missed && !stats.errors && !stats.dropped && hz>=500;
    snprintf(result,sizeof result,
        "%s %s: verified=%lu rate=%.1f Hz; missed=%lu i2c_errors=%lu dropped=%lu gaps=%lu. File closed.",
        pass?"PASS":"FAIL",path,(unsigned long)verified,hz,(unsigned long)stats.missed,
        (unsigned long)stats.errors,(unsigned long)stats.dropped,(unsigned long)gaps);
}
// all led access stays on core 0 so it cannot delay i2c reads
static void led(bool on) {
    // this gpio write does not start any background wireless work
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
}
int main() {
    stdio_init_all();
    // reserve pio1 while cyw43 starts so the sd driver can use it later
    pio_claim_sm_mask(pio1, 0x0f);
    int led_init_result = cyw43_arch_init();
    for (uint sm = 0; sm < 4; ++sm) pio_sm_unclaim(pio1, sm);
    if (led_init_result != 0) {
        while (true) { puts("FAIL LED controller initialization"); sleep_ms(2000); }
    }
    // blinkslowly until the serial monitor connects
    while (!stdio_usb_connected()) {
        led(true); sleep_ms(500);
        led(false); sleep_ms(500);
    }
    sleep_ms(300);
    led(true); //solid light means recording or verification is active
    puts("IAS0360 IMU logger with SD storage and LED status");
    run();
    bool passed = strncmp(result, "PASS ", 5) == 0;
    while (true) {
        puts(result);
        if (passed) {
            //two short flashes show that the saved file passed  verification
            led(true); sleep_ms(150); led(false); sleep_ms(150);
            led(true); sleep_ms(150); led(false); sleep_ms(1550);
        } else {
            for (int i=0; i<10; ++i) {
                led(true); sleep_ms(100); led(false); sleep_ms(100);
            }
        }
    }
}
