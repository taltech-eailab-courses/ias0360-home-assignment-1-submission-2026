# Lab 1_2 integration into the LCD/SD project

## 1. Why the original lab files cannot simply be added to CMake

`statistic.c`, `quantization.c`, `fft.c`, and `sobel.c` each contain a standalone
`main()`. Linking them with the LCD application's `main.cpp` would create multiple
`main` definitions. `lab12_filters.c/.h` extracts/adapts the algorithms as reusable
functions.

## 2. Input representation

The current LCD capture stores one byte per pixel:

- shadow `1` = drawn pixel = black = grayscale `0`
- shadow `0` = empty pixel = white = grayscale `255`

For quantization/statistics it is normalized to the lab's signed domain with:

    x = gray / 128.0 - 1.0

which maps grayscale 0..255 to -1.0 .. 0.9921875 and therefore stays in `[-1,1)`.

## 3. What is preserved from the supplied lab

- Q15: round-to-nearest signed fixed point + dequantization + SNR/RMSE/max error.
- Statistics: arithmetic mean, sample variance (`n-1`), standard deviation,
  min/max, median and mode definitions.
- FFT: Hamming window, radix-2 Cooley-Tukey implementation, magnitude and
  `(2/N)/0.54` amplitude scaling.
- Sobel: exactly `sqrt(Gx^2 + Gy^2)` and clamp to 0..255.
- Canny host defaults: threshold1=50, threshold2=150, `L2gradient=False`.

## 4. Necessary adaptations

### Quantization 8-bit and 4-bit

The supplied `quantization.c` only implements Q15/16-bit. The module derives Q7
(8-bit) and Q3 (4-bit) by using the same signed fixed-point rule with a scale of
`2^(bits-1)`.

### Statistics memory

The lab median makes a local `float tmp[n]`. For a 280x240 image this would be
268800 bytes just for that array. Since our image is binary, the module computes
an exact 2-bin histogram instead. The mathematical definitions are unchanged.

### FFT input

The supplied `fft.c` applies the Hamming window and then prints `x`; the supplied
`fft.py` reads that output and applies Hamming again. That double-windows the
signal. The integrated version stores `*_fft_raw.txt` BEFORE Hamming, then applies
Hamming only inside the MCU FFT. The PC must do exactly one Hamming window too.

Because this project uses an image sensor, the FFT is a 1-D spatial FFT of the
first 256 pixels of the center row. Frequencies are `cycles/pixel`, not Hz.

### Sobel memory

No second 67200-byte edge framebuffer is allocated. Each Sobel output row is
computed from the original shadow buffer and written directly into the BMP.

## 5. Call sequence after an LCD SAVE

Keep your existing raw BMP save, but name it:

    <base>_raw.bmp

where `<base>` is your timestamp, e.g. `20260927_142300`.

Then, while the SD/LCD lock is still owned by the SD-writing core:

```cpp
#include "lab12_filters.h"
#include "lab12_sd.h"

char base_path[256];
snprintf(base_path, sizeof(base_path), "%s/%s", g_drive, timestamp);

FRESULT fr;

fr = lab12_save_quantization(base_path, tp_data.data, tp_data.data_len);
if (fr != FR_OK) { /* your existing failure handling */ }

fr = lab12_save_statistics(base_path, tp_data.data, tp_data.data_len);
if (fr != FR_OK) { /* failure handling */ }

fr = lab12_save_fft(base_path, tp_data.data, BOX_W, BOX_H);
if (fr != FR_OK) { /* failure handling */ }

fr = lab12_save_sobel_bmp(base_path, tp_data.data, BOX_W, BOX_H);
if (fr != FR_OK) { /* failure handling */ }

TP_SetSavePending(false);
```

## 6. Files produced per SAVE

- `<base>_raw.bmp` — your existing image saver
- `<base>_sobel.bmp`
- `<base>_stats.csv`
- `<base>_norm_f32.bin`
- `<base>_q16.bin`
- `<base>_dq16_f32.bin`
- `<base>_q8.bin`
- `<base>_dq8_f32.bin`
- `<base>_q4.bin` (two signed 4-bit values per byte)
- `<base>_dq4_f32.bin`
- `<base>_quant_metrics.csv`
- `<base>_fft_raw.txt`
- `<base>_fft_mcu.txt`

## 7. Important interpretation for the report

The captured LCD data is binary, not a natural 8-bit grayscale photograph. As a
result, 16-bit and 8-bit quantization can reproduce its two normalized levels
exactly with this normalization, so their RMSE can legitimately be 0 and SNR can
be infinite. Q4 loses precision for the white level. This is a characteristic of
the selected data, not a failed quantizer.
