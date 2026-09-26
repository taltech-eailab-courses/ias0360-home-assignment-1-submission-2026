#include <stdio.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "icm20948.h"
#include "features.h"

#define SAMPLE_PERIOD_US 10000u /* target 100 Hz */
#define COUNTS_PER_G 16384.0f   /* lab driver configures +/-2 g */
static float samples[3][WINDOW_N];

/* Lab register layout, with checked transfers and no moving average. */
static bool read_accel(float *x, float *y, float *z) {
    uint8_t reg = REG_ADD_ACCEL_XOUT_H, b[6];
    if (i2c_write_timeout_us(i2c1, I2C_ADD_ICM20948, &reg, 1, true, 2000) != 1)
        return false;
    if (i2c_read_timeout_us(i2c1, I2C_ADD_ICM20948, b, 6, false, 2000) != 6)
        return false;
    *x = (int16_t)((b[0]<<8)|b[1]) / COUNTS_PER_G;
    *y = (int16_t)((b[2]<<8)|b[3]) / COUNTS_PER_G;
    *z = (int16_t)((b[4]<<8)|b[5]) / COUNTS_PER_G;
    return true;
}

int main(void) {
    stdio_init_all();
    sleep_ms(2000);
    IMU_EN_SENSOR_TYPE type;
    imuInit(&type);
    if (type != IMU_EN_SENSOR_TYPE_ICM20948) {
        while (true) {
            printf("# Sensor not detected: check GP6 SDA, GP7 SCL, power and address 0x68.\n");
            sleep_ms(2000);
        }
    }
    printf("# ICM-20948: 256 samples/window, target 100 Hz, acceleration in g\n");
    printf("window,axis,fs_hz,min_dt_us,max_dt_us,mean_g,variance_g2,std_g,min_g,max_g,peak_hz,peak_g\n");
    unsigned window = 0;
    while (true) {
        uint64_t next = time_us_64() + SAMPLE_PERIOD_US;
        uint64_t first = 0, last = 0;
        uint32_t min_dt = UINT32_MAX, max_dt = 0;
        bool valid = true;
        for (int i = 0; i < WINDOW_N; ++i) {
            sleep_until(from_us_since_boot(next));
            uint64_t stamp = time_us_64();
            if (stamp > next + SAMPLE_PERIOD_US/10) { valid = false; break; }
            if (!read_accel(&samples[0][i], &samples[1][i], &samples[2][i])) {
                valid = false; break;
            }
            if (i == 0) first = stamp;
            else {
                uint32_t dt = (uint32_t)(stamp-last);
                if (dt < min_dt) min_dt = dt;
                if (dt > max_dt) max_dt = dt;
            }
            last = stamp;
            next += SAMPLE_PERIOD_US;
        }
        if (!valid) {
            printf("# Discarded window: I2C failure or sampling deadline missed.\n");
            sleep_ms(100);
            continue;
        }
        float fs = (WINDOW_N-1)*1000000.0f/(float)(last-first);
        for (int axis = 0; axis < 3; ++axis) {
            Features f = extract_features(samples[axis], fs);
            printf("%u,%c,%.3f,%lu,%lu,%.6f,%.6f,%.6f,%.6f,%.6f,%.3f,%.6f\n",
                   window, "xyz"[axis], (double)fs,
                   (unsigned long)min_dt, (unsigned long)max_dt,
                   (double)f.mean, (double)f.variance, (double)f.stddev,
                   (double)f.min, (double)f.max, (double)f.peak_hz,
                   (double)f.peak_amplitude);
        }
        ++window;
    }
}
