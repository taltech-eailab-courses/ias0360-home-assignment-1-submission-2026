# Assignment 1: IMU feature extraction on Raspberry Pi Pico

**Student:** Tanishq Vashishth

**UNI-ID / supplied student code:** 266058IV

**Submission branch:** `submissions/266058IV`

## Current version: 500 Hz

The Pico polls acceleration at **500 Hz** (every 2 ms) and extracts features
from **1024-sample windows**. The firmware has been built, flashed, and tested
on the physical board. The report now uses the new 500 Hz recordings.

Hardware: Raspberry Pi Pico with Waveshare Pico-Eval-Board, SKU 20159. This
board includes an ICM-20948 IMU and LCD. The accelerometer is read through
I2C1 on GP6 (SDA) and GP7 (SCL), address 0x68. The LCD is not used.

## Processing and lab sources

1. Initialize the sensor using the copied driver from
   `lab_1_1/imu_example/icm20948` (unchanged).
2. Configure and read back the accelerometer registers in `imu_features/main.c`:
   divider 0 gives **1125 Hz internal output**. The Pico polls asynchronously at
   **500 Hz**; the sensor divider cannot produce exactly 500 Hz internally.
3. Retain the lab +/-2 g range and DLPF6 (~5.7 Hz bandwidth), suitable for slow
   hand motion. 500 Hz polling does not imply a 250 Hz measurement bandwidth.
4. Read six acceleration bytes using checked I2C transfers, without the lab
   fast-read wrapper's additional moving average. Convert counts to g using
   16384 counts/g.
5. Collect 1024 samples per axis, representing 2.048 seconds of nominal sampled
   time. Compute mean, sample variance (N-1 denominator), standard deviation,
   minimum and maximum, adapted from `lab_1_2/statistic.c`.
6. Subtract each axis mean, apply the Hamming window and radix-2 FFT adapted
   from `lab_1_2/fft.c`, and report the strongest non-DC bin and amplitude.
   FFT-bin spacing is **500/1024 = 0.48828125 Hz**. Amplitude uses the exact
   Hamming-window sum, with no doubling at Nyquist. Off-bin amplitudes are
   approximate. Mean removal is not full gravity compensation.
7. Send one CSV row per axis through USB. A host script captures the output;
   the feature calculations run on the Pico.

Large FFT scratch buffers are static to avoid stack exhaustion. Feature
extraction must run on a single task/core. Acquisition pauses during processing
and printing, so windows are separate rather than a continuous gap-free stream.

## Build and flash

The course Docker image provides the Pico SDK, CMake and ARM compiler.
Start Docker Desktop, then from the **main lab repository root** on the host:

```sh
./build_in_docker.sh
```

Inside the container:

```sh
cd ~/submission/ias0360-home-assignment-1-submission-2026/imu_features
cmake -S . -B build
cmake --build build -j2
```

The output is `imu_features/build/imu_features.uf2`. Hold BOOTSEL while
connecting the Pico's own USB port, release it, and copy the UF2 onto the
RPI-RP2 drive. The Pico restarts. Use the Pico USB connector for serial output;
the evaluation board's other USB connector is USB-to-UART and is not used here.
Generated firmware/build files are excluded from Git.

The CMake board target is `pico_w`, inherited from the lab. No Wi-Fi functions
are used. For a plain Pico configuration, use a fresh build directory:
`cmake -S . -B build-pico -DPICO_BOARD=pico`, then build that directory.

## Expected output

The program emits a startup message, CSV header and three rows per window.
`#` prefixes diagnostics. CSV columns are:

```text
window,axis,fs_hz,min_dt_us,max_dt_us,mean_g,variance_g2,std_g,min_g,max_g,peak_hz,peak_g
```

| Fields | Meaning |
|---|---|
| `window`, `axis` | Window counter and acceleration axis (`x`, `y`, `z`) |
| `fs_hz` | Measured software polling rate, approximately 500 Hz |
| `min_dt_us`, `max_dt_us` | Minimum/maximum polling interval, approximately 2000 microseconds |
| `mean_g`, `std_g`, `min_g`, `max_g` | Acceleration statistics in g |
| `variance_g2` | Sample variance in g squared |
| `peak_hz`, `peak_g` | Strongest non-DC frequency bin and corrected amplitude |

A missing sensor or failed configuration/readback produces an error message.
A read failure or scheduling lateness above 200 microseconds discards a window.
The original lab initialization still uses blocking I2C calls. Polling timestamps
are not sensor data-ready timestamps. Tiny stationary spectral peaks can be
noise; interpret frequency with amplitude. Six-decimal output can round small
variances to zero even when standard deviation is nonzero.

## Verification

The 500 Hz firmware build and these portable C tests passed:

```sh
cd imu_features
mkdir -p build-test
cc -std=c11 -Wall -Wextra -Werror features.c test_features.c -lm -o build-test/test_features
./build-test/test_features
```

Tests cover constant input, a biased sinusoid, sample variance, standard
deviation, minimum/maximum, FFT frequency/amplitude and Nyquist scaling.

Hardware tests collected eight complete windows for each of three conditions
on 2026-09-26. The current report inputs are:

| Condition | File in `results/` | Windows |
|---|---|---|
| Stationary | `stationary_500hz_20260926_163332.csv` | 34–41 |
| Slow movement | `slow_movement_500hz_20260926_163501.csv` | 72–79 |
| Faster movement | `fast_movement_500hz_20260926_163601.csv` | 98–105 |

Each file has 24 feature rows (XYZ for eight windows), representing 8192
sampled acceleration vectors and 16.384 seconds of nominal sampled time.
All retained rows report 500.000 Hz and 2000-microsecond min/max intervals.
Matching `.log` files preserve incoming serial output, including the omitted
first window. These are feature logs, not raw acceleration waveforms.

The X axis shows the strongest motion variation in both movement recordings.
Its mean window standard deviation is 0.000650 g at rest, 0.154299 g during slow
movement, and 0.612748 g during faster movement. Slow and faster X peak bins
are 0.488 Hz and 1.465 Hz, respectively. These are coarse spectral estimates,
not independently calibrated movement rates or classifier accuracy results.

Earlier files without `_500hz_` are retained as **historical 100 Hz evidence**.
They are not included in the current report; the previous report is available
in Git history. Do not mix the two firmware configurations in one comparison.

## Record new experiments (macOS)

Close other serial terminals. Find the port with `ls /dev/cu.usbmodem*` and
edit `/dev/cu.usbmodem1401` in `results/capture.py` if needed. From this
submission repository root, run one command at a time while performing the
corresponding action for about 19 seconds:

```sh
python3 results/capture.py stationary
python3 results/capture.py slow_movement
python3 results/capture.py fast_movement
```

The script uses only Python's standard library, rejects non-500 Hz output,
discards the first encountered window and saves eight complete XYZ windows.
It uses timestamped filenames, so recordings are not overwritten.

## Reproduce the IEEE report

`report/report.pdf` is the current three-page IEEE conference-format report.
`report/report.tex` uses the standard `IEEEtran` conference layout. It includes
methods, experiment design, a table, a figure, results, limitations and references.
Motion rate is the varied experimental input; sampling and FFT parameters are
fixed within the current experiment. No algorithm-parameter sweep is claimed.

Inside an environment with Python and matplotlib (such as the course image):

```sh
python report/analyze_results.py
```

The script explicitly selects the three 500 Hz datasets. It checks completeness,
finite numeric values, timing, variance/std consistency within rounding, and
FFT-bin alignment. It generates `summary.csv`, `provenance.json`, LaTeX numeric
fragments and PDF/PNG figures. Provenance includes file hashes and acquisition
settings. Summaries are means of per-window features, not recomputed statistics
from raw acceleration. New captures must be explicitly selected in the script.

Compile using LaTeX with the IEEE class:

```sh
cd report
pdflatex -interaction=nonstopmode -halt-on-error report.tex
pdflatex -interaction=nonstopmode -halt-on-error report.tex
```

Debian packages: `texlive-latex-base`, `texlive-latex-recommended`,
`texlive-publishers`, `texlive-fonts-recommended`. Alternatively upload
`report.tex`, `metrics.tex`, `table_rows.tex` and `results_comparison.pdf`
to the supplied IEEE Overleaf template and set `report.tex` as the main file.

Submit the PDF to Moodle before the deadline. Uploading the Git branch does
not submit the report to Moodle. Repository upload requires write access to
the instructor's GitHub repository.

## Sensor reference

TDK ICM-20948 datasheet, DS-000189 rev. 1.5, Table 18 and bank-2 accelerometer registers:
https://product.tdk.com/system/files/dam/doc/product/sensor/mortion-inertial/imu/data_sheet/ds-000189-icm-20948-v1.5.pdf
