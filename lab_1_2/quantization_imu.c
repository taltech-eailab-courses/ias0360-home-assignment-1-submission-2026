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

static const char *axis_name[AXES]={"X","Y","Z"};
static int16_t raw[AXES][N];
static float data[AXES][N];
static int16_t q16[AXES][N];
static int8_t q8[AXES][N];
static uint8_t q4[AXES][(N+1)/2];
static float dq16[AXES][N];
static float dq8[AXES][N];
static float dq4[AXES][N];

typedef struct {
    float snr;
    float rmse;
    float max_error;
    int clips;
    int bytes;
} Result;

//all formats use the same normalized input range of about -1 to +1
//все форматы используют общий нормализованный диапазон примерно от -1 до  +1
static int32_t quantize_value(float value,int bits,int *clipped) {
    int32_t scale=1L<<(bits-1);
    int32_t min=-scale;
    int32_t max=scale-1;
    float max_input=(float)max/(float)scale;
    *clipped=0;
    if(value<-1.0f) {
        value=-1.0f;
        *clipped=1;
    } else if(value>max_input) {
        value=max_input;
        *clipped=1;
    }
    int32_t q=(int32_t)lrintf(value*(float)scale);
    if(q<min) q=min;
    if(q>max) q=max;
    return q;
}

static float dequantize_value(int32_t value,int bits) {
    return (float)value/(float)(1L<<(bits-1));
}

static int8_t unpack4(uint8_t value) {
    value&=0x0f;
    if(value&0x08) value|=0xf0;
    return (int8_t)value;
}

static int8_t get4(const uint8_t *input,int index) {
    uint8_t packed=input[index/2];
    return unpack4((index%2==0)?packed:(packed>>4));
}

static int quantize16(const float *input,int16_t *output) {
    int clips=0;
    for(int i=0;i<N;i++) {
        int clipped;
        output[i]=(int16_t)quantize_value(input[i],16,&clipped);
        clips+=clipped;
    }
    return clips;
}

static int quantize8(const float *input,int8_t *output) {
    int clips=0;
    for(int i=0;i<N;i++) {
        int clipped;
        output[i]=(int8_t)quantize_value(input[i],8,&clipped);
        clips+=clipped;
    }
    return clips;
}

static int quantize4(const float *input,uint8_t *output) {
    int clips=0;
    for(int i=0;i<N;i+=2) {
        int clipped1,clipped2=0;
        int8_t first=(int8_t)quantize_value(input[i],4,&clipped1);
        int8_t second=0;
        if(i+1<N) second=(int8_t)quantize_value(input[i+1],4,&clipped2);
        //store two 4-bit samples in one byte to obtain the real size reduction
        //упаковываем два отсчёта в байт, чтобы действительно уменьшить размер
        output[i/2]=((uint8_t)first&0x0f)|(((uint8_t)second&0x0f)<<4);
        clips+=clipped1+clipped2;
    }
    return clips;
}

static void dequantize16(const int16_t *input,float *output) {
    for(int i=0;i<N;i++) output[i]=dequantize_value(input[i],16);
}

static void dequantize8(const int8_t *input,float *output) {
    for(int i=0;i<N;i++) output[i]=dequantize_value(input[i],8);
}

static void dequantize4(const uint8_t *input,float *output) {
    for(int i=0;i<N;i++) output[i]=dequantize_value(get4(input,i),4);
}

static void calculate_error(const float *original,const float *restored,Result *result) {
    double signal=0.0;
    double error=0.0;
    float max_error=0.0f;
    for(int i=0;i<N;i++) {
        float e=original[i]-restored[i];
        signal+=(double)original[i]*original[i];
        error+=(double)e*e;
        if(fabsf(e)>max_error) max_error=fabsf(e);
    }
    result->rmse=sqrtf((float)(error/N));
    result->max_error=max_error;
    result->snr=error==0.0?INFINITY:10.0f*log10f((float)(signal/error));
}

static void min_max(const float *values,float *min,float *max) {
    *min=values[0];
    *max=values[0];
    for(int i=1;i<N;i++) {
        if(values[i]<*min) *min=values[i];
        if(values[i]>*max) *max=values[i];
    }
}

static void process_axis(int axis,Result *r16,Result *r8,Result *r4) {
    //quantize and reconstruct one axis before calculating its errors
    //сначала сжимаем и восстанавливаем одну ось потом считаем ошибку
    r16->clips=quantize16(data[axis],q16[axis]);
    r16->bytes=sizeof(q16[axis]);
    dequantize16(q16[axis],dq16[axis]);
    calculate_error(data[axis],dq16[axis],r16);

    r8->clips=quantize8(data[axis],q8[axis]);
    r8->bytes=sizeof(q8[axis]);
    dequantize8(q8[axis],dq8[axis]);
    calculate_error(data[axis],dq8[axis],r8);

    r4->clips=quantize4(data[axis],q4[axis]);
    r4->bytes=sizeof(q4[axis]);
    dequantize4(q4[axis],dq4[axis]);
    calculate_error(data[axis],dq4[axis],r4);
}

static void print_result(const char *axis,const char *format,const Result *result) {
    printf("%s | %-6s | %5d | %8.2f | %.8f | %.8f | %d\n",
           axis,format,result->bytes,result->snr,result->rmse,result->max_error,result->clips);
}

int main(void) {
    stdio_init_all();
    while(!stdio_usb_connected()) sleep_ms(100);
    sleep_ms(300);

    printf("\n=== ICM-20948 quantization ===\n");
    printf("Keep the board still during sensor initialization.\n");

    IMU_EN_SENSOR_TYPE sensor;
    imuInit(&sensor);
    if(sensor!=IMU_EN_SENSOR_TYPE_ICM20948) {
        printf("ERROR: ICM-20948 was not detected on I2C1 GP6/GP7.\n");
        while(true) tight_loop_contents();
    }

    printf("Sensor detected. Capture starts in 3 seconds.\n");
    sleep_ms(3000);

    //absolute deadlines prevent a small loop delay from accumulating every sample
    //абсолютное время не даёт маленькой задержке накапливаться в каждом цикле
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

    //raw counts are divided by the sensor sensitivity for the +/-1 range
    //делим сырые значения на чувствительность датчика для нормализации
    for(int axis=0;axis<AXES;axis++) {
        for(int i=0;i<N;i++) data[axis][i]=(float)raw[axis][i]/32768.0f;
    }

    Result results[AXES][3];
    for(int axis=0;axis<AXES;axis++) {
        process_axis(axis,&results[axis][0],&results[axis][1],&results[axis][2]);
    }

    printf("Captured %d samples per axis in %llu us, target=%.1f Hz, actual=%.2f Hz\n",
           N,elapsed,TARGET_FS,fs);
    for(int axis=0;axis<AXES;axis++) {
        float min,max;
        min_max(data[axis],&min,&max);
        printf("Axis %s: raw first=%d, normalized range=[%.6f, %.6f]\n",
               axis_name[axis],raw[axis][0],min,max);
    }

    printf("\nAxis | Format | bytes |  SNR(dB) |       RMSE |  max error | clips\n");
    printf("-----+--------+-------+----------+------------+------------+------\n");
    for(int axis=0;axis<AXES;axis++) {
        print_result(axis_name[axis],"16-bit",&results[axis][0]);
        print_result(axis_name[axis],"8-bit",&results[axis][1]);
        print_result(axis_name[axis],"4-bit",&results[axis][2]);
    }

    printf("\nFirst 5 samples:\n");
    printf("i | axis | raw | normalized | q16 | deq16 | q8 | deq8 | q4 | deq4\n");
    for(int i=0;i<5;i++) {
        for(int axis=0;axis<AXES;axis++) {
            printf("%d | %s | %d | %.6f | %d | %.6f | %d | %.6f | %d | %.6f\n",
                   i,axis_name[axis],raw[axis][i],data[axis][i],q16[axis][i],dq16[axis][i],
                   q8[axis][i],dq8[axis][i],get4(q4[axis],i),dq4[axis][i]);
        }
    }

    printf("\nStorage for all 3 axes, %d samples each:\n",N);
    printf("float32: %d bytes\n",AXES*N*(int)sizeof(float));
    printf("16-bit: %d bytes\n",AXES*N*(int)sizeof(int16_t));
    printf("8-bit: %d bytes\n",AXES*N*(int)sizeof(int8_t));
    printf("4-bit: %d bytes\n",AXES*((N+1)/2));

    printf("\nMeasured stream size:\n");
    printf("float32: %.2f kB/s\n",AXES*4.0f*fs/1000.0f);
    printf("16-bit: %.2f kB/s\n",AXES*2.0f*fs/1000.0f);
    printf("8-bit: %.2f kB/s\n",AXES*fs/1000.0f);
    printf("4-bit: %.2f kB/s\n",AXES*0.5f*fs/1000.0f);

    while(true) tight_loop_contents();
    return 0;
}
