#ifndef SENSOR_H
#define SENSOR_H

#include "stdio.h"
#include "stdint.h"
void sensor_init(void);
void sensor_read_accel(int16_t* ax, int16_t* ay, int16_t* az);

#endif