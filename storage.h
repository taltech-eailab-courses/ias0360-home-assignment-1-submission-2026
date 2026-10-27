#ifndef STORAGE_H
#define STORAGE_H

#include "status.h"
#include "stdbool.h"

bool storage_init(void);
void storage_log_transition(motion_state_t state, float stddev);

#endif