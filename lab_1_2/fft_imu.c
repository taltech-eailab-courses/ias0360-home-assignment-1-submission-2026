#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "pico/time.h"
#include "icm20948.h"
#include "ff.h"
#include "sd_card.h"
#include "hw_config.h"

#define N 256
#define TARGET_FS 140.0f
#define ACCEL_LSB_PER_G 16384.0f
#define PEAKS 5

typedef struct {
    float re;
    float im;
} Complex;

static float raw_x[N];
static float windowed[N];
static Complex spectrum[N];
static float magnitude[N/2+1];
static FATFS fs_state;

//проверяем число байтов, потому что запись может оказаться неполной
static bool write_text(FIL *file,const char *text,UINT *total) {
    UINT written=0;
    UINT length=(UINT)strlen(text);
    FRESULT result=f_write(file,text,length,&written);
    *total+=written;
    return result==FR_OK&&written==length;
}

static bool save_files(float fs,uint64_t fft_us) {
    printf("\nInitializing SD card...\n");
    if(!sd_init_driver()) {
        printf("SD ERROR: initialization failed.\n");
        return false;
    }
    sd_card_t *card=sd_get_by_num(0);
    if(card==NULL) {
        printf("SD ERROR: card configuration was not found.\n");
        return false;
    }
    const char *drive=sd_get_drive_prefix(card);
    FRESULT result=f_mount(&fs_state,drive,1);
    if(result!=FR_OK) {
        printf("SD ERROR: mount failed, code=%d. Format the card as FAT32.\n",result);
        return false;
    }

    char path[32];
    char line[128];
    FIL file;
    UINT total=0;
    snprintf(path,sizeof(path),"%s/raw_data.csv",drive);
    result=f_open(&file,path,FA_WRITE|FA_CREATE_ALWAYS);
    if(result!=FR_OK) {
        printf("SD ERROR: cannot create %s, code=%d.\n",path,result);
        f_unmount(drive);
        return false;
    }
    snprintf(line,sizeof(line),"META,axis,X\r\nMETA,n,%d\r\nMETA,fs,%.9f\r\nindex,time_s,value_g\r\n",N,fs);
    bool ok=write_text(&file,line,&total);
    for(int i=0;i<N&&ok;i++) {
        snprintf(line,sizeof(line),"%d,%.9f,%.9f\r\n",i,i/fs,raw_x[i]);
        ok=write_text(&file,line,&total);
    }
    if(f_close(&file)!=FR_OK) ok=false;
    if(!ok) {
        printf("SD ERROR: writing %s failed.\n",path);
        f_unmount(drive);
        return false;
    }
    printf("Saved %s: %u bytes.\n",path,total);

    total=0;
    //2 file contains every single-sided fft bin, not only the peaks
    //второй файл хранит весь односторонний спектр, а не только пики
    snprintf(path,sizeof(path),"%s/fft_data.csv",drive);
    result=f_open(&file,path,FA_WRITE|FA_CREATE_ALWAYS);
    if(result!=FR_OK) {
        printf("SD ERROR: cannot create %s, code=%d.\n",path,result);
        f_unmount(drive);
        return false;
    }
    snprintf(line,sizeof(line),"META,axis,X\r\nMETA,n,%d\r\nMETA,fs,%.9f\r\nMETA,fft_us,%llu\r\nbin,frequency_hz,amplitude_g\r\n",N,fs,fft_us);
    ok=write_text(&file,line,&total);
    for(int i=0;i<=N/2&&ok;i++) {
        snprintf(line,sizeof(line),"%d,%.9f,%.9f\r\n",i,i*fs/N,magnitude[i]);
        ok=write_text(&file,line,&total);
    }
    if(f_close(&file)!=FR_OK) ok=false;
    f_unmount(drive);
    if(!ok) {
        printf("SD ERROR: writing %s failed.\n",path);
        return false;
    }
    printf("Saved %s: %u bytes.\n",path,total);
    printf("SD card unmounted. It is safe to remove after power is disconnected.\n");
    return true;
}

static unsigned reverse_bits(unsigned value,int bits) {
    unsigned result=0;
    for(int i=0;i<bits;i++) {
        result=(result<<1)|(value&1u);
        value>>=1u;
    }
    return result;
}

static void fft(Complex *values) {
    //radix-2 fft starts by placing samples in bit-reversed order
    //начинается с перестановки индексов в обратном порядке битов
    int bits=0;
    while((1<<bits)<N) bits++;
    for(unsigned i=0;i<N;i++) {
        unsigned j=reverse_bits(i,bits);
        if(j>i) {
            Complex temp=values[i];
            values[i]=values[j];
            values[j]=temp;
        }
    }

    //каждый этап объединяет пары всё более крупных частей преобразования
    for(int size=2;size<=N;size<<=1) {
        int half=size/2;
        float angle=-2.0f*(float)M_PI/(float)size;
        for(int start=0;start<N;start+=size) {
            for(int j=0;j<half;j++) {
                float a=angle*j;
                float wr=cosf(a);
                float wi=sinf(a);
                int even=start+j;
                int odd=even+half;
                float tr=wr*values[odd].re-wi*values[odd].im;
                float ti=wr*values[odd].im+wi*values[odd].re;
                float er=values[even].re;
                float ei=values[even].im;
                values[even].re=er+tr;
                values[even].im=ei+ti;
                values[odd].re=er-tr;
                values[odd].im=ei-ti;
            }
        }
    }
}

static void find_peaks(const float *values,int *indices) {
    //bin 0 is skipped because it represents the dc component
    //нулевой bin пропускаем, потому что он хранит постоянную составляющую
    for(int p=0;p<PEAKS;p++) {
        int best=1;
        float best_value=-1.0f;
        for(int i=1;i<=N/2;i++) {
            bool used=false;
            for(int j=0;j<p;j++) {
                if(indices[j]==i) used=true;
            }
            if(!used&&values[i]>best_value) {
                best=i;
                best_value=values[i];
            }
        }
        indices[p]=best;
    }
}

int main(void) {
    stdio_init_all();
    while(!stdio_usb_connected()) sleep_ms(100);
    sleep_ms(300);

    printf("\n=== ICM-20948 FFT ===\n");
    printf("Keep the board still during sensor initialization.\n");

    IMU_EN_SENSOR_TYPE sensor;
    imuInit(&sensor);
    if(sensor!=IMU_EN_SENSOR_TYPE_ICM20948) {
        printf("ERROR: ICM-20948 was not detected on I2C1 GP6/GP7.\n");
        while(true) tight_loop_contents();
    }

    printf("Move the board rhythmically along X during capture.\n");
    printf("Capture starts in 3 seconds.\n");
    sleep_ms(3000);

    //fixed sampling deadlines give a more accurate frequency axis
    //фиксированные сроки измерений дают более точную шкалу частот
    uint32_t interval_us=(uint32_t)(1000000.0f/TARGET_FS);
    absolute_time_t next=get_absolute_time();
    uint64_t first_time=0;
    uint64_t last_time=0;
    for(int i=0;i<N;i++) {
        int16_t ax,ay,az;
        icm20948AccelFastRead(&ax,&ay,&az);
        raw_x[i]=(float)ax/ACCEL_LSB_PER_G;
        uint64_t now=time_us_64();
        if(i==0) first_time=now;
        last_time=now;
        if(i+1<N) {
            next=delayed_by_us(next,interval_us);
            sleep_until(next);
        }
    }

    uint64_t elapsed=last_time-first_time;
    float fs=(float)(N-1)*1000000.0f/(float)elapsed;
    uint64_t fft_start=time_us_64();

    // remove the dc offset and apply a hamming window before the fft
    //убираем среднее и применяем окно Хэмминга перед fft
    double sum=0.0;
    for(int i=0;i<N;i++) sum+=raw_x[i];
    float mean=(float)(sum/N);
    for(int i=0;i<N;i++) {
        float window=0.54f-0.46f*cosf(2.0f*(float)M_PI*i/(N-1));
        windowed[i]=(raw_x[i]-mean)*window;
        spectrum[i].re=windowed[i];
        spectrum[i].im=0.0f;
    }

    fft(spectrum);
    // convert the complex result to a corrected single-sided amplitude spectrum
    //преобразуем комплексный результат в односторонний спектр амплитуд
    float scale=(2.0f/N)/0.54f;
    for(int i=0;i<=N/2;i++) {
        magnitude[i]=sqrtf(spectrum[i].re*spectrum[i].re+spectrum[i].im*spectrum[i].im)*scale;
    }
    magnitude[0]*=0.5f;
    uint64_t fft_us=time_us_64()-fft_start;
    float max_realtime_fs=(float)N*1000000.0f/(float)fft_us;

    int peaks[PEAKS];
    find_peaks(magnitude,peaks);
    printf("Captured %d X-axis samples in %llu us, target=%.1f Hz, actual=%.4f Hz\n",
           N,elapsed,TARGET_FS,fs);
    printf("FFT processing time: %llu us, theoretical real-time limit: %.1f samples/s\n",
           fft_us,max_realtime_fs);
    printf("Frequency resolution: %.6f Hz\n",fs/N);

    printf("\nTop %d bins:\n",PEAKS);
    for(int i=0;i<PEAKS;i++) {
        int bin=peaks[i];
        printf("%d: bin=%d, frequency=%.6f Hz, amplitude=%.6f g\n",
               i+1,bin,bin*fs/N,magnitude[bin]);
    }

    printf("\nMETA,axis,X\n");
    printf("META,n,%d\n",N);
    printf("META,fs,%.9f\n",fs);
    printf("META,fft_us,%llu\n",fft_us);
    printf("RAW_BEGIN\n");
    printf("index,time_s,value_g\n");
    for(int i=0;i<N;i++) printf("%d,%.9f,%.9f\n",i,i/fs,raw_x[i]);
    printf("RAW_END\n");

    printf("FFT_BEGIN\n");
    printf("bin,frequency_hz,amplitude_g\n");
    for(int i=0;i<=N/2;i++) printf("%d,%.9f,%.9f\n",i,i*fs/N,magnitude[i]);
    printf("FFT_END\n");

    if(save_files(fs,fft_us)) printf("SD save complete.\n");
    else printf("Serial output is still available, but SD files were not saved.\n");

    while(true) tight_loop_contents();
    return 0;
}
