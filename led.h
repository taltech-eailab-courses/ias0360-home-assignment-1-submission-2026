#ifndef LED_H
#define LED_H

#include "status.h"

void led_init(void);
void led_update(motion_state_t state);

#endif