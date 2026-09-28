#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "pico/time.h"
#include "icm20948.h"

#define N 512
#define AXES 3
#define TARGET_FS 140.0f
#define ACCEL_LSB_PER_G 16384.0f

static const char *axis_name[AXES]={"X","Y","Z"};
static int16_t raw[AXES][N];
static float accel_g[AXES][N];
static float zscore[AXES][N];
static float float_copy[N];
static int16_t raw_copy[N];

typedef struct {
    float min;
    float max;
    float mean;
    float median;
    float mode;
    float variance;
    float stddev;
    int mode_count;
} Stats;

// insertion sort is sufficient for the fixed block of 512 values
//сортировки вставками достаточно для фиксированного блока из  512 значений
static void sort_float(float *values) {
    for(int i=1;i<N;i++) {
        float key=values[i];
        int j=i-1;
        while(j>=0&&values[j]>key) {
            values[j+1]=values[j];
            j--;
        }
        values[j+1]=key;
    }
}

static void sort_i16(int16_t *values) {
    for(int i=1;i<N;i++) {
        int16_t key=values[i];
        int j=i-1;
        while(j>=0&&values[j]>key) {
            values[j+1]=values[j];
            j--;
        }
        values[j+1]=key;
    }
}

static Stats calculate_stats(const float *values,const int16_t *raw_values) {
    Stats s={0};
    s.min=values[0];
    s.max=values[0];
    double sum=0.0;
    for(int i=0;i<N;i++) {
        if(values[i]<s.min) s.min=values[i];
        if(values[i]>s.max) s.max=values[i];
        sum+=values[i];
        float_copy[i]=values[i];
        raw_copy[i]=raw_values[i];
    }
    s.mean=(float)(sum/N);

    double squared=0.0;
    for(int i=0;i<N;i++) {
        double d=(double)values[i]-s.mean;
        squared+=d*d;
    }
    //N-1 gives the sample variance of the captured measurement block
    //деление на N-1 даёт выборочную дисперсию измеренного блока
    s.variance=(float)(squared/(N-1));
    s.stddev=sqrtf(s.variance);

    sort_float(float_copy);
    s.median=0.5f*(float_copy[N/2-1]+float_copy[N/2]);
    // mode is counted on raw integers because float equality is unreliable
    //моду считаем по сырым целымпотому что float нельзя надёжно сравнивать точно
    sort_i16(raw_copy);
    int best_count=1;
    int current_count=1;
    int16_t best_value=raw_copy[0];
    for(int i=1;i<N;i++) {
        if(raw_copy[i]==raw_copy[i-1]) {
            current_count++;
        } else {
            if(current_count>best_count) {
                best_count=current_count;
                best_value=raw_copy[i-1];
            }
            current_count=1;
        }
    }
    if(current_count>best_count) {
        best_count=current_count;
        best_value=raw_copy[N-1];
    }
    s.mode=(float)best_value/ACCEL_LSB_PER_G;
    s.mode_count=best_count;
    return s;
}

static void normalize_zscore(const float *input,float *output,const Stats *stats) {
    if(stats->stddev==0.0f) {
        for(int i=0;i<N;i++) output[i]=0.0f;
        return;
    }
    for(int i=0;i<N;i++) output[i]=(input[i]-stats->mean)/stats->stddev;
}

static void zscore_check(const float *values,float *mean,float *stddev,float *min,float *max) {
    double sum=0.0;
    *min=values[0];
    *max=values[0];
    for(int i=0;i<N;i++) {
        sum+=values[i];
        if(values[i]<*min) *min=values[i];
        if(values[i]>*max) *max=values[i];
    }
    *mean=(float)(sum/N);
    double squared=0.0;
    for(int i=0;i<N;i++) {
        double d=(double)values[i]-*mean;
        squared+=d*d;
    }
    *stddev=sqrtf((float)(squared/(N-1)));
}

int main(void) {
    stdio_init_all();
    while(!stdio_usb_connected()) sleep_ms(100);
    sleep_ms(300);

    printf("\n=== ICM-20948 statistics ===\n");
    printf("Keep the board still during sensor initialization.\n");

    IMU_EN_SENSOR_TYPE sensor;
    imuInit(&sensor);
    if(sensor!=IMU_EN_SENSOR_TYPE_ICM20948) {
        printf("ERROR: ICM-20948 was not detected on I2C1 GP6/GP7.\n");
        while(true) tight_loop_contents();
    }

    printf("Sensor detected. Capture starts in 3 seconds.\n");
    sleep_ms(3000);

    //use fixed deadlines so the requested sampling rate remains stable
    //фиксированные моменты времени сохраняют стабильную частоту измерений
    uint32_t interval_us=(uint32_t)(1000000.0f/TARGET_FS);
    absolute_time_t next=get_absolute_time();
    uint64_t first_time=0;
    uint64_t last_time=0;
    for(int i=0;i<N;i++) {
        icm20948AccelFastRead(&raw[0][i],&raw[1][i],&raw[2][i]);
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
    for(int axis=0;axis<AXES;axis++) {
        for(int i=0;i<N;i++) accel_g[axis][i]=(float)raw[axis][i]/ACCEL_LSB_PER_G;
    }

    //calculate features first and then verify the normalized data
    //сначала считаем признаки, затем проверяем нормализованные значения
    Stats stats[AXES];
    for(int axis=0;axis<AXES;axis++) {
        stats[axis]=calculate_stats(accel_g[axis],raw[axis]);
        normalize_zscore(accel_g[axis],zscore[axis],&stats[axis]);
    }

    printf("Captured %d samples per axis in %llu us, target=%.1f Hz, actual=%.2f Hz\n",
           N,elapsed,TARGET_FS,fs);
    printf("Values are shown in g.\n\n");
    printf("Axis | min | max | mean | median | mode(count) | variance | stddev\n");
    printf("-----+-----+-----+------+--------+-------------+----------+-------\n");
    for(int axis=0;axis<AXES;axis++) {
        printf("%s | %.6f | %.6f | %.6f | %.6f | %.6f(%d) | %.8f | %.6f\n",
               axis_name[axis],stats[axis].min,stats[axis].max,stats[axis].mean,
               stats[axis].median,stats[axis].mode,stats[axis].mode_count,
               stats[axis].variance,stats[axis].stddev);
    }

    printf("\nZ-score validation:\n");
    printf("Axis | mean | stddev | min | max\n");
    for(int axis=0;axis<AXES;axis++) {
        float mean,stddev,min,max;
        zscore_check(zscore[axis],&mean,&stddev,&min,&max);
        printf("%s | %.6f | %.6f | %.6f | %.6f\n",axis_name[axis],mean,stddev,min,max);
    }

    printf("\nFirst 5 samples:\n");
    printf("i | X(g) | X(z) | Y(g) | Y(z) | Z(g) | Z(z)\n");
    for(int i=0;i<5;i++) {
        printf("%d | %.6f | %.6f | %.6f | %.6f | %.6f | %.6f\n",
               i,accel_g[0][i],zscore[0][i],accel_g[1][i],zscore[1][i],
               accel_g[2][i],zscore[2][i]);
    }

    //this comparison shows the storage benefit of keeping only features
    //сравнение показывает экономию при хранении только статистических признаков
    int raw_bytes=AXES*N*(int)sizeof(float);
    int feature_bytes=AXES*(int)sizeof(Stats);
    printf("\nRepresentation size:\n");
    printf("Raw float block: %d bytes\n",raw_bytes);
    printf("Statistics: %d bytes\n",feature_bytes);
    printf("Reduction: %.1fx\n",(double)raw_bytes/feature_bytes);

    while(true) tight_loop_contents();
    return 0;
}
