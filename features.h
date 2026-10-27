#ifndef FEATURES_H
#define FEATURES_H

#include "stdbool.h"
#include "status.h"

#define WINDOW_SIZE 64
bool features_classify_window(motion_state_t*,float*);
motion_state_t features_debounce(motion_state_t raw_state);

#endif