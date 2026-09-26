# Assignment 1: IMU feature extraction on Raspberry Pi Pico

**Student:** Tanishq Vashishth

**UNI-ID / supplied student code:** 266058IV

**Submission branch:** `submissions/266058IV`

Application: describing stillness and repeated hand movement from acceleration.
The Pico collects data and computes all features. A computer builds the firmware
and displays USB serial output. No activity classifier is trained here.

## Flow and lab sources

1. Initialize ICM-20948 using the copied driver from
   `lab_1_1/imu_example/icm20948` (unchanged). Its wiring is I2C1,
   GP6=SDA and GP7=SCL, address 0x68. The tested Waveshare Pico-Eval-Board
   (SKU 20159) already includes this sensor; a separate IMU is not required.
2. Read acceleration registers using checked I2C transfers in `imu_features/main.c`.
   Unlike the lab fast-read wrapper, this read does not add an 8-sample moving average.
   The driver's existing hardware filter/configuration is retained.
3. Convert counts to g using 16384 counts/g for the driver's +/-2 g setting.
4. Collect 256 readings at a target 100 Hz (~2.56 seconds per window).
5. Compute mean, sample variance (N-1 denominator), standard deviation,
   minimum and maximum per axis, adapted from `lab_1_2/statistic.c`.
6. Subtract each axis mean, apply the Hamming window, and compute the radix-2 FFT
   adapted from `lab_1_2/fft.c`. Report the strongest non-DC bin and its amplitude.
   Mean removal reduces gravity/offset contribution but is not full gravity compensation.
7. Print three CSV rows per window, one per axis, over USB.

`features.c` is portable C so its math can be tested without the Pico.
The FFT uses the measured polling rate; bin spacing is about 100/256 = 0.391 Hz.
Amplitude uses the exact Hamming window sum and correct Nyquist scaling.
For non-bin-centered tones the amplitude is approximate. A peak in a nearly still
signal can simply be noise: interpret frequency together with amplitude and stddev.

## Build

The project uses the Pico C/C++ SDK, CMake and ARM compiler provided by the
course Docker image. The firmware CMake board target is `pico_w` (the lab default).
The experiment uses no Wi-Fi features. For a plain Pico target, configure a fresh
build directory with `cmake -S . -B build-pico -DPICO_BOARD=pico`.

From the lab repository root, start the existing Docker environment:

```sh
./build_in_docker.sh
```

Inside its shell:

```sh
cd ~/submission/ias0360-home-assignment-1-submission-2026/imu_features
cmake -S . -B build
cmake --build build -j2
```

The output is `build/imu_features.uf2`. Hold BOOTSEL while connecting the Pico,
then copy that UF2 to its RPI-RP2 drive using the host computer, or use the
repository's flash script in an environment with Pico USB access.
Open the Pico USB serial port with a serial terminal (115200 is a conventional
terminal setting; USB CDC does not use it as a physical UART bit rate).
Use the USB connector on the Pico itself. The evaluation board's other USB
connector is its USB-to-UART interface. This firmware enables USB output only.

## Expected output

After the startup message, the program prints this header and three rows per
window (one each for `x`, `y`, `z`):

```csv
window,axis,fs_hz,min_dt_us,max_dt_us,mean_g,variance_g2,std_g,min_g,max_g,peak_hz,peak_g
212,y,100.000,10000,10000,0.035051,0.000000,0.000587,0.033508,0.036682,9.766,0.000278
```

The example is an actual recorded stationary row, not a required numerical output.

| Fields | Meaning |
|---|---|
| `window`, `axis` | Window counter and acceleration axis |
| `fs_hz` | Measured software polling rate in Hz |
| `min_dt_us`, `max_dt_us` | Minimum/maximum interval between poll timestamps, in microseconds |
| `mean_g`, `std_g`, `min_g`, `max_g` | Acceleration statistics in g |
| `variance_g2` | Sample variance in g squared (N-1 denominator) |
| `peak_hz`, `peak_g` | Strongest non-DC FFT bin frequency and corrected amplitude |

Startup/diagnostic messages begin with `#`. An absent sensor triggers a repeated
error message. Read failures or missed deadlines discard the affected window.
Stationary peaks can represent noise; interpret frequency with its amplitude.
Small variances can round to zero in the six-decimal serial format.

## Validate the calculations on a computer

From `imu_features`, in an environment with a C compiler:

```sh
mkdir -p build-test
cc -std=c11 -Wall -Wextra -Werror features.c test_features.c -lm -o build-test/test_features
./build-test/test_features
```

The tests check a constant signal, known biased sinusoid, sample variance,
minimum/maximum, dominant frequency, amplitude, and Nyquist scaling.

## Hardware experiment and report

1. Keep the sensor stationary; save at least five windows of USB output.
2. Move it gently back and forth at a steady rhythm; save at least five windows.
3. Repeat with faster motion. Avoid impacts that saturate the +/-2 g sensor range.
4. Compare standard deviation, minimum/maximum and spectral peak amplitude/frequency.
   Report measured values and explain how they change. Do not invent results.
5. Include your wiring/photo, sample rate, window length, lab sources, build steps,
   saved outputs, and a short discussion of limitations in your submission.

CSV columns include measured sample rate and minimum/maximum polling interval.
Statistics retain gravity, so a stationary axis may have a nonzero mean.
These are polling timestamps, not sensor data-ready timestamps; sensor updates
are asynchronous. Windows with an I2C failure or >1 ms scheduling lateness are
rejected. Acquisition pauses while processing/printing; windows are independent,
not a continuous gap-free recording. The lab initialization code is unchanged
and retains its original blocking I2C behavior.

## Verification status

- Pico firmware built successfully in the course container; output is
  `imu_features/build/imu_features.uf2` (generated build files are not committed).
- Portable C tests passed: constant input, biased sine statistics/FFT, and Nyquist scaling.
- Firmware was flashed and exercised on the physical Pico/ICM-20948 board.
- Three USB recordings were captured on 2026-09-26, with eight complete windows
  and 24 XYZ rows per condition. They represent 2048 sampled vectors per condition.
- `report/analyze_results.py` verifies completeness, numeric values, timing,
  variance/std consistency within output rounding, and FFT frequency-bin alignment.
- The LCD is not used by this implementation.

## Saved recordings and reproduction

The report, measured datasets, capture script and analysis script are included
in the submission branch. The PDF must also be submitted to Moodle separately.

The authoritative report inputs are:

- `results/stationary_20260926_152045.csv` (windows 212–219)
- `results/slow_movement_20260926_152151.csv` (windows 237–244)
- `results/fast_movement_20260926_152235.csv` (windows 254–261)

Matching `.log` files preserve the incoming serial text, including the first
window omitted from the clean CSV. Earlier values pasted into chat are separate
trials and are not mixed into the report. The report's faster Y frequency is
1.562 Hz, not the 1.953 Hz observed in an earlier trial.

To collect new recordings on macOS, close other serial terminals first, find
the port with `ls /dev/cu.usbmodem*`, and edit the port in `results/capture.py`
if it differs from `/dev/cu.usbmodem1401`. From this repository root, run one
command at a time while performing the corresponding action:

```sh
python3 results/capture.py stationary
python3 results/capture.py slow_movement
python3 results/capture.py fast_movement
```

Each capture takes about 23 seconds and writes a timestamped CSV and log.
The script uses only Python's standard library. It is designed for macOS/POSIX.
Updated datasets must be explicitly selected in `report/analyze_results.py`;
new recordings do not silently replace the report evidence.

## IEEE report

`report/report.pdf` is the compiled IEEE conference-format report.
`report/report.tex` is editable LaTeX source using `IEEEtran` with its default
conference layout. Tables and figures use all eight windows per condition.
Motion rate is the varied experimental input; sampling/FFT parameters are fixed.
The report does not claim an algorithm-parameter sweep or measured classifier accuracy.

Regenerate the analysis inside the course container (Python and matplotlib):

```sh
python report/analyze_results.py
```

It generates `report/summary.csv`, `report/provenance.json`, LaTeX table/numeric
fragments and the PDF/PNG comparison figure. Provenance includes SHA-256 hashes
of the exact source CSV files. Summary values are means of per-window features,
not statistics recomputed from unavailable raw acceleration samples.

With LaTeX installed, compile twice to resolve references:

```sh
cd report
pdflatex -interaction=nonstopmode -halt-on-error report.tex
pdflatex -interaction=nonstopmode -halt-on-error report.tex
```

Required Debian packages are `texlive-latex-base`, `texlive-latex-recommended`,
`texlive-publishers`, and `texlive-fonts-recommended`. Alternatively, upload
`report.tex`, `metrics.tex`, `table_rows.tex`, and `results_comparison.pdf` to the
instructor's IEEE Overleaf template and set `report.tex` as the main document.

Submit the report PDF to Moodle separately before the deadline. Git submission
does not submit the report to Moodle.
