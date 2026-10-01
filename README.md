Home Assignment 1 — On-Device IMU Feature Extraction

IAS0360 — Machine Learning for Embedded Systems

Implements quantization, statistical feature extraction, and FFT-based spectral analysis on real accelerometer data, running live on an RP2040 (Raspberry Pi Pico W) alongside a 500 Hz IMU data-acquisition and SD-card logging pipeline built in an earlier lab.
Hardware

    Waveshare Pico-Eval-Board (RP2040, Pico W variant)
    Onboard ICM-20948 IMU (I2C)
    microSD card (FAT32-formatted), inserted in the board's SD slot

Project structure

homework_1/
├── CMakeLists.txt
├── main.c                  # sampler (Core 0) + SD logging & analysis (Core 1)
├── pico_sdk_import.cmake
├── icm20948/                # IMU driver
├── sd_card_driver/           # SD card + FatFs driver
├── include/                  # (if present) additional driver headers
└── config/
    └── hw_config.c            # SD card hardware configuration

Prerequisites

    Raspberry Pi Pico SDK (tested with SDK 2.2.0) and the arm-none-eabi GCC toolchain, either installed natively or available through the course's Docker build environment.
    cmake (>= 3.13) and make.
    A serial terminal program (e.g. minicom, screen, or VS Code's built-in serial monitor).

Build
bash

cd homework_1
mkdir -p build
cd build
cmake ..
make -j$(nproc)

A successful build produces build/imu_sd_logger.uf2.

    Known build-time fix required: sd_card_driver/include/my_debug.h relies solely on #pragma once, which does not reliably prevent double-inclusion on some filesystems/toolchains (observed inside a Docker bind mount), causing a redefinition of 'dump_bytes' compile error. If you hit this, add a traditional include guard to that file:
    c

    #pragma once
    #ifndef MY_DEBUG_H_INCLUDED
    #define MY_DEBUG_H_INCLUDED
    /* ... existing file content ... */
    #endif // MY_DEBUG_H_INCLUDED

Flashing

Option A — picotool (if installed with USB support):
bash

./flash.sh build/imu_sd_logger.uf2

Option B — BOOTSEL drag-and-drop (always works, no extra tools needed):

    Unplug the Pico.
    Hold the BOOTSEL button, plug the USB cable back in, release BOOTSEL after ~2 seconds.
    The Pico appears as a removable drive named RPI-RP2.
    Copy/drag build/imu_sd_logger.uf2 onto that drive.
    The board flashes itself and reboots automatically.

Running / viewing output
bash

ls /dev/ttyACM*            # find the serial device, e.g. /dev/ttyACM0
minicom -D /dev/ttyACM0 -b 115200

(Or use VS Code's Serial Monitor pointed at the same port and baud rate.)
Expected output

On boot, the console prints IMU initialization status and then begins sampling:

IMU OK. Starting 500 Hz sampling...
f_mount -> Succeeded (0)
Logging to 0:/imu_log.bin
Sampled 500 total, 0 dropped

A Sampled X total, N dropped line is printed once per second; X should climb by ~500 each second and N (dropped samples due to queue overrun) should remain 0 under normal operation.

Approximately every 512 ms (every 256 samples), a feature-extraction report is printed for each accelerometer axis:

=== [accel_x] window analysis (n=256, fs=500.0 Hz) ===
Quant  16-bit: SNR=inf dB  RMSE=0.0000000  max_err=0.0000000  clips=0
Quant   8-bit: SNR=23.64 dB  RMSE=0.0033596  max_err=0.0039062  clips=0
Quant   4-bit: SNR=0.00 dB  RMSE=0.0511034  max_err=0.0534058  clips=0
Stats: mean=-0.05110 median=-0.05089 var=0.00000 std=0.00079 min=-0.05341 max=-0.04999 mode=-0.05084(x11)
FFT top-5 peaks (df=1.953 Hz):
  bin=1  freq=1.95 Hz  amp=0.0434
  bin=3  freq=5.86 Hz  amp=0.0007
  ...

This repeats for accel_x, accel_y, and accel_z each window.

On the SD card: a binary log file imu_log.bin is created, containing raw accelerometer/gyroscope samples with timestamps (16 bytes/sample: uint32_t t_us; int16_t ax, ay, az, gx, gy, gz;), written in 2048-byte (128-sample) batches.
Known limitations

    Raw sensor codes are normalized to [-1, 1) by dividing by 32768, rather than converting through the IMU's configured full-scale range into physical units (g / dps). Quantization and FFT results are therefore in raw-code terms, not physical units.
    The FFT shows a dominant low-frequency bin on every axis due to DC bias (gravity/sensor offset) leaking into the spectrum; the Hamming window does not remove a constant offset. A DC-removal step (subtracting the window mean) before windowing is a planned improvement, not yet implemented.
    PC-side FFT comparison (saving raw/FFT data to SD as text and cross-checking against a NumPy FFT, as described in Lab 1_2) is out of scope for this submission.

