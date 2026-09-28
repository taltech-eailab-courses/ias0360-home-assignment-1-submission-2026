# Home Assignment 1: Data Acquisition and Pre-processing
This submission contains the completed tasks from Lab 1.1 and Lab 1.2. The software was developed for Raspberry Pi Pico W and tested with the ICM-20948 IMU, an LCD touch board and its microSD card slot.

## 1.Hardware and connections
- Raspberry Pi Pico W
- ICM-20948 accelerometer and gyroscope
- 3.5 inch LCD with resistive  touch and microSD slot
- FAT32 microSD card
-  USB data cable

The ICM-20948 uses I2C1:
| Signal | Pico pin 
| SDA | GP6 
| SCL | GP7 
  
The SD card uses the four-bit SDIO interface configured in `hw_config.c`.Its main pins are CMD on GP18 and D0 on GP19. The SD driver derives the other SDIO pins from this configuration.

## 2.Development tools
- Visual Studio Code
- Raspberry Pi Pico extension 
- Pico SDK 2.2.0 for Lab 1.1
- Pico SDK 2.3.1 for Lab 1.2
-  CMake 4.3.4 and Ninja 1.13.2 installed by the Pico extension
- Python 3, NumPy and Matplotlib for the FFT comparison

## 3.Building and  flashing on Windows
### 3.1Recommended VS Code method
1. Open the folder that contains the required `CMakeLists.txt`
2. the Raspberry Pi Pico extension
3. Import the project if it has not been imported before
4. Select `pico_w` as the board
5. Select the requird build target
6. Run  `Compile Project`
7. Connect the Pico while holding BOOTSEL for the first flash
8. Run `Run Project (USB)`  ffor copy the generated UF2 to the `RPI-RP2` drive
9. Open Serial Monitor at 115200 baud

### 3.2 PowerShell method with Pico extension tools
Start PowerShell in the repository  root and define the tool paths:
```powershell
$cmake="$env:USERPROFILE\.pico-sdk\cmake\v4.3.4\bin\cmake.exe"
$ninja="$env:USERPROFILE\.pico-sdk\ninja\v1.13.2\ninja.exe"
```
For Lab 1.1 use Pico SDK and Picotool 2.2.0:
```powershell
$env:PICO_SDK_PATH="$env:USERPROFILE\.pico-sdk\sdk\2.2.0"
$picotool="$env:USERPROFILE\.pico-sdk\picotool\2.2.0\picotool\picotool.exe"
```
For Lab 1.2 use Pico SDK and Picotool 2.3.1:

```powershell
$env:PICO_SDK_PATH="$env:USERPROFILE\.pico-sdk\sdk\2.3.1"
$picotool="$env:USERPROFILE\.pico-sdk\picotool\2.3.1\picotool\picotool.exe"
```
The first flash may require BOOTSEL
The PowerShell commands  below use `build_windows`.  This keeps the Windows Ninja cache separate from a `build` directory previously created by Docker with Unix Makefiles.

### 3.3 Course Docker method
The repository a lso provides `build_in_docker.sh` and `flash.sh`. These commands must be entered inside the Linux/Docker shell, not ordinary PowerShell:

```bash
./build_in_docker.sh
cd <application-folder>
mkdir -p build
cd build
cmake ..
make -j$(nproc)
cd ../../..
./flash.sh <application-folder>/build/<program>.uf2
```
## 4. Lab 1.1: IMU data logger
### 4.1Purpose
The program records acceleration and angular velocity for 30 seconds. Core 1 reads the ICM-20948 at a requested period of 1600 microseconds. Core 0 receives samples through a queue and writes buffered CSV data to the SD card

### 4.2Source files
- `lab_1_1/sd_card_example/imu_logger.cpp`: sensor acquisition, queue, buffering, file writing and verification
- `lab_1_1/sd_card_example/config/hw_config.c`: SD card hardware configuration supplied with the  example
- `lab_1_1/sd_card_example/CMakeLists.txt`: adds the `imu_logger` target and required libraries
- `lab_1_1/imu_example/icm20948` the supplied ICM-20948 driver
- `lab_1_1/sd_card_example/sd_card_driver` the supplied FatFs and SD driver
The original `sd_card_example` target  remains available. `imu_logger` is an additional target for the assignment.

### 4.3Data flow
```text
ICM-20948 -> core 1 -> sample queue ->core 0 -> 8192-byte buffer -> CSV on SD
```
Each `Sample` contains a sequence number  relative timestamp, `ax`, `ay`, `az`, `gx`, `gy` and `gz`. The queue separates time-sensitive sensor reads from slower filesystem operations. Block writes reduce the overhead of calling the filesystem for every sample.

### 4.4Build in PowerShell
```powershell
cd C:\Users\milan\IAS0360\ias0360-lab-excercises-2026\lab_1_1\sd_card_example
$env:PICO_SDK_PATH="$env:USERPROFILE\.pico-sdk\sdk\2.2.0"
$cmake="$env:USERPROFILE\.pico-sdk\cmake\v4.3.4\bin\cmake.exe"
$ninja="$env:USERPROFILE\.pico-sdk\ninja\v1.13.2\ninja.exe"
$picotool="$env:USERPROFILE\.pico-sdk\picotool\2.2.0\picotool\picotool.exe"
& $cmake -S . -B build_windows -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" -DPICO_BOARD=pico_w
& $ninja -C build_windows imu_logger
```
The output is `lab_1_1/sd_card_example/build_windows/imu_logger.uf2`. Flash it with:
```powershell
& $picotool load build_windows\imu_logger.uf2 -fx
```

### 4.5Build and flash in Docker
```bash
cd lab_1_1/sd_card_example
mkdir -p build
cd build
cmake ..
make -j$(nproc) imu_logger
cd ../../..
./flash.sh lab_1_1/sd_card_example/build/imu_logger.uf2
```
### 4.6  Test procedure
1. Insert a FAT32 microSD card.
2. Flash `imu_logger.uf2`.
3. Open Serial Monitor on the Pico COM port at 115200 baud.
4. Wait for the slow LED  blinking to stop.
5. Move the board gently during the  30-second recording.
7. Wait  for the verification result.
Expected final line:
```text
PASS 0:/imu0000.csv: verified=18750 rate=620.8 Hz; missed=0 i2c_errors=0 dropped=0 gaps=0. File closed.
```
The number in the file name and measured rate may differ. A correct result has `PASS`, at least 500 Hz, zero I2C errors, zero dropped samples and zero sequence gaps. The LED gives two short flashes after a pass. A fast repeated blink indicates failure.

The generated CSV header is:
```text
seq,t_us,ax,ay,az,gx,gy,gz
```

## 5.Lab 1.1: touch drawing stored on SD
### 5.1Purpose
The user draws inside the box onthe LCD andd `CLEAR` resets the visible area and its memory buffer. `SAVE` asks core 1 to create a new binary PBM image. The application reads the file back and compares every stored pixel with the original drawing.

### 5.2Source files
- `lab_1_1/lcd_sd_card_example/drawing_main.cpp`: application start, SD worker, PBM writing and readback
- `lab_1_1/lcd_sd_card_example/drawing_touch.c`: touch input,raing buffer, CLEAR and SAVE buttons
- `lab_1_1/lcd_sd_card_example/config/hw_config.c`: corrected SDIO interface pointer for the current driver
- `lab_1_1/lcd_sd_card_example/lib/lcd`:supplied LCD and touch support library
- `lab_1_1/lcd_sd_card_example/CMakeLists.txt`: adds the `drawing_app` target

The original `main` application remains available. `drawing_app` is the assignment target.
### 5.3Data flow
```text
touch input -> visible pixel +hadow buffer -> SAVE request -> core 1
core 1 -> pack 8 pixels per byte -> PBM file -> readback -> pixel comparison
```
A mutex prevents the drawing buffer from changing while it is being saved. Multicore FIFO carries the save request and its result between the two cores.

### 5.4Build in PowerShell
```powershell
cd C:\Users\milan\IAS0360\ias0360-lab-excercises-2026\lab_1_1\lcd_sd_card_example
$env:PICO_SDK_PATH="$env:USERPROFILE\.pico-sdk\sdk\2.2.0"
$cmake="$env:USERPROFILE\.pico-sdk\cmake\v4.3.4\bin\cmake.exe"
$ninja="$env:USERPROFILE\.pico-sdk\ninja\v1.13.2\ninja.exe"
$picotool="$env:USERPROFILE\.pico-sdk\picotool\2.2.0\picotool\picotool.exe"
& $cmake -S . -B build_windows -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" -DPICO_BOARD=pico_w
& $ninja -C build_windows drawing_app
& $picotool load build_windows\drawing_app.uf2 -fx
```

### 5.5  Build and flash in Docker
```bash
cd lab_1_1/lcd_sd_card_example
mkdir -p build
cd build
cmake ..
make -j$(nproc) drawing_app
cd ../../..
./flash.sh lab_1_1/lcd_sd_card_example/build/drawing_app.uf2
```

### 5.6 Test procedure
1. Insert the SD card and flash `drawing_app.uf2`.
2. Open Serial Monitor at 115200 baud.
3. Draw several lines inside the drawing box.
4.  `SAVE` once and wait for completion.
5. Draw something different and press `SAVE` again.
6. `CLEAR` and confirm that the drawing area is empty.

Expected messages:
```text
DRAWING: draw inside the box; SAVE writes and verifies a new PBM file.
PASS drawing 0:/draw0000.pbm (...), readback verified
PASS drawing 0:/draw0001.pbm (...), readback verified
```
Every save uses a new number and does not overwrite the previous image.

## 6.Lab 1.2 build selection
The Lab 1.2 programs each contain their own `main()` function. Therefore, only one processing source is enabled in `lab_1_2/CMakeLists.txt` at a time. For example, the FFT version uses:
```cmake
add_executable(main
fft_imu.c
hw_config.c
#statistics_imu.c
#quantization_imu.c
#quantization.c
)
``` 
After changing the selected source, configure and build again:
```powershell
cd C:\Users\milan\IAS0360\ias0360-lab-excercises-2026\lab_1_2
$env:PICO_SDK_PATH="$env:USERPROFILE\.pico-sdk\sdk\2.3.1"
$cmake="$env:USERPROFILE\.pico-sdk\cmake\v4.3.4\bin\cmake.exe"
$ninja="$env:USERPROFILE\.pico-sdk\ninja\v1.13.2\ninja.exe"
$picotool="$env:USERPROFILE\.pico-sdk\picotool\2.3.1\picotool\picotool.exe"
& $cmake -S . -B build_windows -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" -DPICO_BOARD=pico_w
& $ninja -C build_windows main
& $picotool load build_windows\main.uf2 -fx
```
The PowerShell output is `lab_1_2/build_windows/main.uf2`. The Pico extension normally uses `lab_1_2/build/main.uf2`.

## 7. Lab 1.2: synthetic quantization
Enable `quantization.c` and disable `fft_imu.c`, `hw_config.c`, `quantization_imu.c` and `statistics_imu.c`.
The program generates a known signal containing 7 Hz and 23 Hz components. It converts the same 512 float samples to 16-bit, 8-bit and packed 4-bit formats and reconstructs them.
It reports stored bytes, SNR, RMSE, maximum absolute error, clipping count, theoretical RMSE and stream size for three axes.

Expected behavior
- 16-bit has the smallest error and uses half the float32 memory
- 8-bit uses  one quarter of the float32 memory with a visible but small error
- packed 4-bit uses one eighth of the float32 memory and has the largest error

This synthetic test confirms that the quantization implementation works before connecting it to real sensor data.

## 8. Lab 1.2:IMU quantization
Enable `quantization_imu.c` and disable the other files containing `main()`.
The program captures 512 measurements from X, Y and Z at approximately 140 Hz. Raw signed accelerometer counts are divided by 32768 to produce the normalized input used by the quantizers. Each axis is converted to 16-bit, 8-bit and packed 4-bit form.

Test twice:
1. Keep the board still for the complete capture.
2.  Reset the program and move the board during capture.

The still test shows quantization of a nearly constant signal. The movement test gives a more meaningful SNR because the useful signal energy is larger.
Expected result during movement:
- 16-bit reproduces the source values exactly in this representation
- 8-bit normally gives about  40 dB SNR and an RMSE near 0.0023
- 4- bit normally gives about 16-20 dB SNR and an RMSE near 0.037

## 9.Lab 1.2: statistics and z-score
Enable `statistics_imu.c` and disable the other files containing `main()`.
The program captures 512 acceleration samples per axis in g and calculates minimum, maximum, arithmetic mean, median, mode, sample variance and sample standard deviation.
It then performs z-score normalization:
```text
z = (x - mean) / standard_deviation
```
Test once with the board still and once while moving it. For correctly normalized non-constant data, the reported z-score mean must be close to 0 and the standard deviation close to 1.
The final size comparison treats the eight stored statistics for each axis as a compact feature representation:
```text
raw float block: 6144 bytes
statistics: 96 bytes
reduction: 64 times
```
## 10.Lab 1.2: FFT with IMU and SD output
Enable `fft_imu.c` and `hw_config.c`.
The program captures 256 X-axis acceleration values at approximately 140 Hz. During capture, move the board rhythmically along the X axis.
Processing steps:
1. Calculate the measured sampling rate from timestamps
2. Subtract the mean to remove the DC offset
3. Apply a Hamming window to reduce spectral leakage
4. Run an in-place radix-2  FFT
5. Convert the complex outut to a single-sided amplitude spectrum
6. Report the five strongest non-DC bins
7. Print the complete raw data and spectrum through USB Serial
8. Save the same data on SD card

The frequency resolution is `df = sampling_rate / 256`. At 140 Hz, it is approximately 0.547 Hz.
The program writes:
```text
0:/raw_data.csv
0:/fft_data.csv
```
Successful SD output ends with:
```text
Saved 0:/raw_data.csv: ... bytes.
Saved 0:/fft_data.csv: ... bytes.
SD card unmounted. It is safe to remove after power is disconnected.
SD save comlete.
```
The same information is printed between `RAW_BEGIN`, `RAW_END`, `FFT_BEGIN` and `FFT_END` markers.
## 11.Comparing Pico FFT with NumPy
Copy the complete serial output to `lab_1_2/python_examples/fft_capture.txt` and run:

```powershell
cd C:\Users\milan\IAS0360\ias0360-lab-excercises-2026\lab_1_2\python_examples
python fft_compare_imu.py fft_capture.txt
```
If `python` is not recognized, try:

```powershell
py fft_compare_imu.py fft_capture.txt
```
If the files were copied dire ctly from the SD card, run:
```powershell
python fft_compare_imu.py raw_data.csv fft_data.csv
```
The script repeats the centering, Hamming window, FFT and amplitude scaling with NumPy. It compares every frequency bin and prints RMSE and maximum error. It creates `imu_time.png` and `imu_fft_comparison.png`.
A very small difference is normal because the Pico code uses 32-bit floating-point calculations while NumPy normally uses higher precision.

## 12.Supplied examples and assignment versions
| Supplied file | Assignment file| Reason for the assignment version|
|---|---|---|
| `imu_example/main.c` | `sd_card_example/imu_logger.cpp` | adds timed acquisition, SD buffering and readback verification |
| `sd_card_example/main.cpp` | `sd_card_example/imu_logger.cpp` | changes a generic file example into a sensor logger |
| `lcd_sd_card_example/main.cpp` | `drawing_main.cpp` | provides a smaller drawing-specific application with PBM verification |
| `lib/lcd/LCD_Touch.c` | `drawing_touch.c` | keeps the library example intact and uses a stable drawing buffer |
| `lcd_sd_card_example/config/hw_config.c` | corrected in place | uses `sdio_if_p` for an SDIO interface instead of the incompatible SPI pointer |
| original `quantization.c` | extended `quantization.c` | adds 8-bit and packed 4-bit comparison to the original 16-bit test |
| `statistic.c` | `statistics_imu.c` | applies statistics and z-score to eal ICM-20948 data |
| `fft.c` | `fft_imu.c` | replaces synthetic frequencies with measured IMU data and SD output |
| `python_examples/fft.py` | `fft_compare_imu.py` | reads the produced format and compares every FFT bin |
The supplied files are not deleted. The separate assignment versions make it possible to compare the starting examples with the completed real-sensor programs.
