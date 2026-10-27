#ifndef SAMPLING_H
#define SAMPLING_H

#include "stdint.h"

void sampling_timing_test(void);
void sampling_core1_entry(void);
uint64_t sampling_get_core1_count(void);
uint32_t sampling_get_bad_sample_count(void);

#endif