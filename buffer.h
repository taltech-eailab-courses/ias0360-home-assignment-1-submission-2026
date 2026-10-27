#ifndef BUFFER_H
#define BUFFER_H
#define BUFFER_CAPACITY 250

#include <stdbool.h>
#include <stdint.h>

void buffer_init(void); //buffer_init - no parameters, returns nothing
bool buffer_push(float value); //buffer_push - takes one float, returns bool (true = stored)
bool buffer_pop(float *out);  //buffer_pop - takes a POINTER to a float it fills in, returns bool (true = got one)
uint32_t buffer_overflow_count(void); //buffer_overflow_count  - no parameters, returns uint32_t
uint32_t buffer_level(void); //buffer_level - returns uint32_t with the number of entries in a buffer.

#endif