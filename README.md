# IAS0360 Home Assignment 1: IMU signal analysis

For this assignment, I combined the IMU driver from Lab 1.1 with the signal
processing functions from Lab 1.2. The aim was to see how acceleration changes
when the board is resting or moving, and how much detail is lost when the
measurements are represented with fewer bits.

The program runs on a Raspberry Pi Pico W and reads the X-axis acceleration
from an ICM-20948. It collects 256 samples at about 100 Hz, subtracts their mean,
and calculates minimum, maximum, variance and standard deviation. It then
compares 16-, 8- and 4-bit quantization and uses a Hamming window and FFT to
find the strongest frequencies. All results are printed over USB.

## Hardware setup

I used the course Pico W board with an ICM-20948 and a USB connection to the
computer. The sensor uses I2C1, with SDA on GP6 and SCL on GP7. Its address is
`0x68`, and the acceleration range is set to ±2 g. An external sensor board
needs a compatible supply, 3.3 V I2C logic and a common ground with the Pico.

## Build and run

In the course Docker environment, run:

```bash
cd ~/submission/ias0360-home-assignment-1-submission-2026
cmake -S . -B ../../.cache/h1-course-build -DCMAKE_BUILD_TYPE=Release
cmake --build ../../.cache/h1-course-build -j4
```

The course image provides the Pico SDK. If building in another environment,
set `PICO_SDK_PATH` to the SDK folder first. The commands above keep the build
files outside the submission folder.

To load the program, connect the Pico while holding BOOTSEL. Then copy
`imu_signal_analysis.uf2` to the `RPI-RP2` drive. On macOS, run this from the
submission folder **on the host computer, after leaving Docker**:

```bash
cp ../../.cache/h1-course-build/imu_signal_analysis.uf2 /Volumes/RPI-RP2/
```

Open the Pico's USB serial port at 115200 baud, for example with
`screen /dev/cu.usbmodemXXXX 115200`, replacing the port name. The program waits
for the terminal connection before starting. Keep the board still during
initialization, then leave it resting or move it to compare the results.

## Reading the output

Each block shows the statistics before and after mean removal, a comparison
of the three bit depths, and the five strongest FFT bins. RMSE and maximum
error describe the difference after quantization; SNR compares the signal
with that error. The clipping count shows how many values exceeded the
chosen quantization range.

This is a shortened excerpt from the real measurement on 28 September 2026:

```text
=== Block 17: N=256, measured polling rate=100.000 Hz ===
...
bits | RMSE [g]  | max error [g] | SNR [dB] | clipped
  16 | 0.00000787 | 0.00000787 |   102.66 | 0
   8 | 0.00460488 | 0.00780463 |    47.31 | 0
   4 | 0.06978264 | 0.12425971 |    23.70 | 5
Dominant frequencies: strongest bins excluding DC, df=0.390625 Hz
  1: bin=5 frequency=1.9531 Hz amplitude~=1.375237 g
...
```

## Testing and observations

The project was built in the course Docker environment and tested on the
Pico W with the real sensor. For the measurement, I left the board resting,
moved it slowly, moved it faster, and put it down again. The saved USB log
(`messung_20260928_181150.txt`) contains 15 complete blocks, all reporting
100 Hz. The results are discussed in the accompanying report.

The measurements showed a clear difference between rest and movement. The
strongest frequencies were around 1 Hz during slow movement and around 2 Hz
during faster movement. Eight-bit quantization gave much smaller errors than
four-bit quantization during movement. Four bits also caused some clipping,
while sixteen bits preserved the small variations at rest much better.
The processing functions were also checked separately with a synthetic test
signal on the computer, including an FFT comparison against a direct DFT.

The program only processes one axis. The course driver smooths the readings
over eight samples, and processing and printing leave short gaps between
blocks. Neighbouring FFT bins can belong to the same movement, and small
peaks are still printed when the board is resting. The log contains the
calculated results rather than individual samples or the full spectrum.

## Code used

| Part | Source and changes |
|---|---|
| `icm20948/` | Lab 1.1 driver. The header and CMake file are unchanged; the C file has small fixes for byte types, an unused variable and the bit conversion in `invSqrt`. |
| `signal_processing.c` | Functions from Lab 1.2 `statistic.c`, `quantization.c` and `fft.c`, without their example programs. Statistics and FFT function bodies are unchanged. Q15 limits were corrected, and the same quantization approach was extended to 8 and 4 bits. |
| `main.c` | New code connecting sensor readings, processing and USB output. It follows the course FFT example, with corrected scaling for the DC and Nyquist bins. |
| `signal_processing.h` and root `CMakeLists.txt` | Shared declarations and build setup for the combined program. |

AI assistance was used for integration, adaptations, tests and documentation.
