#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"

#define N 512
#define FS 2200.0f
#define AXES 3

static float x[N];
static float dq16[N];
static float dq8[N];
static float dq4[N];
static int16_t q16[N];
static int8_t q8[N];
static uint8_t q4[(N+1)/2];

typedef struct {
    float snr;
    float rmse;
    float max_error;
    int clips;
    int bytes;
} Result;

// map a normalized float to the signed integer range of the selected format
// переводим нормализованное число в диапазон выбранного целого формата
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
        // two signed 4-bit samples share one byte
        // два знаковых 4-битных значения помещаются в один байт
        output[i/2]=((uint8_t)first&0x0f)|(((uint8_t)second&0x0f)<<4);
        clips+=clipped1+clipped2;
    }
    return clips;
}

static int8_t unpack4(uint8_t value) {
    value&=0x0f;
    // extend the sign bit before converting the nibble to int8_t
    // расширяем знак, чтобы правильно восстановить отрицательное число
    if(value&0x08) value|=0xf0;
    return (int8_t)value;
}

static int8_t get4(const uint8_t *input,int index) {
    uint8_t packed=input[index/2];
    uint8_t value=(index%2==0)?packed:(packed>>4);
    return unpack4(value);
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

static void print_result(const char *name,int bits,const Result *result) {
    // uniform quantization noise has the expected rms value step/sqrt(12)
    // теоретическая ошибка равномерного квантования равна step/sqrt(12)
    float step=1.0f/(float)(1L<<(bits-1));
    float expected_rmse=step/sqrtf(12.0f);
    printf("%-6s | %5d | %8.2f | %.8f | %.8f | %d\n",
           name,result->bytes,result->snr,result->rmse,result->max_error,result->clips);
    printf("         step=%.8f, theoretical RMSE=%.8f\n",step,expected_rmse);
}

int main(void) {
    stdio_init_all();
    while(!stdio_usb_connected()) sleep_ms(100);
    sleep_ms(300);

    // this known signal makes the three formats easy to compare
    // известный сигнал позволяет проверить все три формата
    for(int i=0;i<N;i++) {
        float t=(float)i/FS;
        x[i]=0.6f*sinf(2.0f*(float)M_PI*7.0f*t)+0.3f*sinf(2.0f*(float)M_PI*23.0f*t);
    }

    Result r16={.clips=quantize16(x,q16),.bytes=sizeof(q16)};
    dequantize16(q16,dq16);
    calculate_error(x,dq16,&r16);

    Result r8={.clips=quantize8(x,q8),.bytes=sizeof(q8)};
    dequantize8(q8,dq8);
    calculate_error(x,dq8,&r8);

    Result r4={.clips=quantize4(x,q4),.bytes=sizeof(q4)};
    dequantize4(q4,dq4);
    calculate_error(x,dq4,&r4);

    int float_bytes=sizeof(x);
    printf("\n=== Quantization comparison ===\n");
    printf("Samples: %d, fs: %.1f Hz, float32: %d bytes\n\n",N,FS,float_bytes);
    printf("Format | bytes |  SNR(dB) |       RMSE |  max error | clips\n");
    printf("-------+-------+----------+------------+------------+------\n");
    print_result("16-bit",16,&r16);
    print_result("8-bit",8,&r8);
    print_result("4-bit",4,&r4);

    printf("\nFirst 10 samples:\n");
    printf("i | original | q16 | deq16 | q8 | deq8 | q4 | deq4\n");
    for(int i=0;i<10;i++) {
        printf("%d | %.6f | %d | %.6f | %d | %.6f | %d | %.6f\n",
               i,x[i],q16[i],dq16[i],q8[i],dq8[i],get4(q4,i),dq4[i]);
    }

    printf("\nStorage reduction:\n");
    printf("16-bit: %.1fx\n",(double)float_bytes/r16.bytes);
    printf("8-bit: %.1fx\n",(double)float_bytes/r8.bytes);
    printf("4-bit: %.1fx\n",(double)float_bytes/r4.bytes);

    printf("\nStream size for %d axes:\n",AXES);
    printf("float32: %.2f kB/s\n",AXES*4.0f*FS/1000.0f);
    printf("16-bit: %.2f kB/s\n",AXES*2.0f*FS/1000.0f);
    printf("8-bit: %.2f kB/s\n",AXES*FS/1000.0f);
    printf("4-bit: %.2f kB/s\n",AXES*0.5f*FS/1000.0f);

    while(true) tight_loop_contents();
    return 0;
}
