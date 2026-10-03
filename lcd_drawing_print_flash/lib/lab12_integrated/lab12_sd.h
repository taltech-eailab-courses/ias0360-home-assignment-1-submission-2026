#ifndef LAB12_SD_H
#define LAB12_SD_H

#include <stddef.h>
#include <stdint.h>
#include "ff.h"

#ifdef __cplusplus
extern "C" {
#endif

/* base_path example: "0:/20260927_142300" (without suffix/extension). */
FRESULT lab12_save_statistics(const char *base_path, const uint8_t *shadow, size_t n);
FRESULT lab12_save_quantization(const char *base_path, const uint8_t *shadow, size_t n);
FRESULT lab12_save_fft(const char *base_path, const uint8_t *shadow, int w, int h);
FRESULT lab12_save_sobel_bmp(const char *base_path, const uint8_t *shadow, int w, int h);

#ifdef __cplusplus
}
#endif

#endif
