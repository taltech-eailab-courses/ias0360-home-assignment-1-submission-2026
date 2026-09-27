# IAS0360 Homework 1: Real-Time IMU Feature Extraction & Signal Processing

**Author:** Miguel Robledo  
**UNI-ID:** miguro  
**Platform:** Raspberry Pi Pico 2 W (RP2350 Dual-Core Arm Cortex-M33)  
**Sensors & Peripherals:** ICM-20948 9-DOF IMU (I2C fast mode @ 400 kHz), MicroSD Card (SPI/SDIO via FATFS)

---

## 1. Project Overview

This project implements an embedded, real-time feature extraction and digital signal processing (DSP) pipeline for the **Inertial Measurement Unit (IMU)** domain. 

### Key Capabilities:
- **Dual-Core Architecture:**
  - **Core 1 (Real-Time Producer):** Deterministic 500 Hz hardware-timed sampling loop ($T_{\text{sample}} = 2000\,\mu\text{s}$). Reads raw 3-axis accelerometer and 3-axis gyroscope data and enqueues into a lock-free inter-core queue (`sample_q`).
  - **Core 0 (DSP & Telemetry Consumer):** Collects samples into an $N = 256$ window buffer ($512\text{ ms}$). Executes unit conversion, statistical analysis, spectral decomposition, quantization, SD card logging, and serial streaming.
- **Time-Domain Statistical Extraction:**
  - Tri-axial acceleration ($a_x, a_y, a_z$) in $g$ units ($\pm 2g$ scale).
  - Euclidean norm vector magnitude: $\|a\| = \sqrt{a_x^2 + a_y^2 + a_z^2}$ (orientation invariant).
  - Metrics computed: Mean ($\mu$), Sample Variance ($\sigma^2$), Standard Deviation ($\sigma$), Min, Max, Peak-to-Peak Range ($R$), Insertion-Sorted Median, and Root Mean Square (RMS).
- **Frequency-Domain Spectral Analysis:**
  - In-place Hamming windowing to suppress spectral leakage side-lobes by $>30\text{ dB}$.
  - In-place 256-point Radix-2 Cooley--Tukey FFT ($\Delta f = 1.953\text{ Hz}$).
  - Coherent-gain scaled single-sided magnitude spectrum.
  - Automated Top-$K$ ($K=5$) peak frequency and amplitude picker (excluding DC bin 0).
  - Total spectral energy computation.
- **Fixed-Point Quantization & Error Analysis:**
  - Normalized floating-point mapping to signed 16-bit fixed-point (Q15).
  - In-line calculation of Signal-to-Noise Ratio (SNR), RMS error, and clipping counts.
  - Demonstrates a 2.0$\times$ storage reduction (from 4 bytes `float32` to 2 bytes `int16_t` per sample) with $>85.6\text{ dB}$ SNR.
- **Data Logging & Visual Indication:**
  - Writes raw binary records to `imu_data.bin` on SD card.
  - Writes structured telemetry rows to `imu_features.csv` on SD card.
  - Onboard CYW43 LED heartbeat toggle per processed window.
  - Graceful fallback: functions in continuous USB streaming mode even if no SD card is present.

---

## 2. Directory Structure

```
homework1/
├── CMakeLists.txt              # Build configuration with Pico SDK & math library
├── pico_sdk_import.cmake       # Pico SDK CMake bootstrap
├── main.c                      # Multicore scheduler and telemetry loop
├── dsp_features.h              # Header defining DSP structures and functions
├── dsp_features.c              # Statistical moments, FFT, and Q15 quantization
├── verify_homework1.py         # PC Python verification script & plot generator
├── report.tex                  # IEEE conference paper source (strictly 3 pages)
├── report.pdf                  # Compiled IEEE conference report
├── figures/                    # Generated publication plots
│   ├── fig_time_freq_overview.png
│   └── fig_variations_overview.png
├── config/                     # SD card SPI pinout configuration
├── icm20948/                   # ICM-20948 hardware driver
└── sd_card_driver/             # FATFS SD card driver
```

---

## 3. How to Compile the Code

The build environment is pre-configured in the Docker container `ias0360-2026`.

### Option A: From inside the Docker container
```bash
cd /root/homework1
mkdir -p build && cd build
cmake -DPICO_BOARD=pico2_w ..
make -j$(nproc)
```

### Option B: From the host machine via Docker exec
```bash
docker exec ias0360-2026 bash -c "cd /root/homework1 && mkdir -p build && cd build && cmake .. && make -j\$(nproc)"
```

The resulting executable binary will be generated at:
```
homework1/build/imu.uf2
```

---

## 4. How to Flash and Run on the Embedded System

1. Put the Raspberry Pi Pico 2 W into BOOTSEL mode:
   - Hold the white **BOOTSEL** button on the board.
   - Plug the micro-USB cable into your PC.
   - Release the button after 2 seconds.
   - Verify with `lsusb`: device appears as **"Raspberry Pi RP2 Boot"**.
2. Flash the compiled firmware using the workspace flashing script:
   ```bash
   ./flash.sh homework1/build/imu.uf2
   ```
3. Once flashed, the Pico reboots automatically and opens a virtual USB serial CDC COM port (`/dev/ttyACM0`).

---

## 5. Expected Output Structure

### A. USB CDC Serial Console Output
Connect using your preferred serial terminal (e.g. `picocom`, `minicom`, or `screen`):
```bash
picocom -b 115200 /dev/ttyACM0
```

Every $N = 256$ samples ($\approx 512\text{ ms}$), the MCU outputs formatted telemetry:
```text
========================================================
  IAS0360 Homework 1 - IMU DSP & Feature Extraction     
  Author: Miguel Robledo | Sampling: 500 Hz | N = 256   
========================================================

[SD] Mounted successfully on 0:
[IMU] Motion sensor ICM-20948 initialized successfully!
[CORE1] Launched IMU sampling task @ 500 Hz
[DSP] Starting real-time feature extraction pipeline...

----------------------------------------------------------------------------------
[WINDOW #0 | t = 0.51 s | Rate = 500.0 Hz]
  Orientation / Mean (g) : ax=+0.350, ay=-0.150, az=+0.900
  Magnitude Stats (g)    : mean=1.064, std=0.3431, min=0.298, max=1.731, range=1.433, rms=1.118
  Spectral Features (FFT): Dom=1.95 Hz | Energy=1.142e+00 | Top Peaks:
    Peak 1:    1.95 Hz (amp = 0.9831 g)
    Peak 2:    3.91 Hz (amp = 0.3279 g)
    Peak 3:    5.86 Hz (amp = 0.1876 g)
  Q15 Quantization       : SNR=85.69 dB | RMS_err=0.000009 | Compression=2.0x (1024 -> 512 B)
----------------------------------------------------------------------------------
```

### B. SD Card Files
If a FAT32 MicroSD card is inserted:
1. `imu_data.bin`: Raw packed 16-bit binary accelerometer and gyroscope readings.
2. `imu_features.csv`: Comma-separated feature summary containing:
   - `window_id`: Window sequence number
   - `t_sec`: Elapsed timestamp in seconds
   - `ax_mean,ay_mean,az_mean`: Static gravity/orientation components
   - `amag_mean,amag_std,amag_min,amag_max,amag_range,amag_median,amag_rms`: Time-domain moments
   - `f_dom_hz,spectral_energy`: Dominant frequency and total spectral power
   - `p1_hz,p1_amp,p2_hz,p2_amp,p3_hz,p3_amp`: Top 3 spectral peak frequencies and amplitudes
   - `q15_snr_db,q15_rms_err,q15_clips`: Quantization distortion metrics

---

## 6. PC Algorithm Verification & Report Reproduction

To run the PC Python verification and re-generate the publication plots:
```bash
docker exec ias0360-2026 python3 /root/homework1/verify_homework1.py
```
This runs the NumPy/SciPy reference algorithms and compares against the MCU output, confirming exact numerical parity:
- **Mean relative error:** $0.0000\%$
- **Peak frequency identification error:** $0.0000\%$
- **Quantization SNR match:** $85.69\text{ dB}$ (within $10^{-7}$ RMS error)

---

## 7. Recompiling the LaTeX Report

The 3-page IEEE conference paper report is generated using:
```bash
cd homework1
pdflatex -interaction=nonstopmode report.tex
pdflatex -interaction=nonstopmode report.tex
```
The output PDF is saved at `homework1/report.pdf` (verified: exactly 3 pages).
