#include "sensor.h"
#include "icm20948/icm20948.h"
#include "stdio.h"

void sensor_init(void){
    IMU_EN_SENSOR_TYPE sensorType;
    imuInit(&sensorType);
    if (IMU_EN_SENSOR_TYPE_ICM20948 == sensorType){
        printf("Motion sensor is ICM-20948\n");
    } else {
        printf("Motion sensor NULL\n");
    }
}

void sensor_read_accel(int16_t *ax, int16_t *ay, int16_t *az){
    icm20948AccelFastRead(ax, ay, az);
}