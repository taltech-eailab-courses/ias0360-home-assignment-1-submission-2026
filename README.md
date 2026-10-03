# IAS0360 Home Assignment 1 - IMU feature extraction

Markkus Koddala, student code 253101IAPM

Feature extraction and signal processing on IMU data, running on a Raspberry Pi
Pico with a Waveshare Pico Eval Board.

There are two programs. `logger/` records the ICM-20948 to the SD card.
`hw1` (this directory) reads a recording back and runs the Lab 1.2 algorithms
over it on the Pico. `analyze.py` repeats the FFT on a PC with NumPy so the
on-device result can be checked.

## What is implemented

| Stage | Where | Notes |
|---|---|---|
| DC removal, peak normalisation | `preprocess.c` | |
| Second-order Butterworth low-pass | `filter.c` | cutoff set by `FC_HZ` |
| Quantisation at 16 / 8 / 4 bits, dequantisation, SNR / RMSE | `quantize.c` | |
| Mean, variance, std, min, max, median, mode | `stats.c` | per axis |
| Radix-2 FFT with Hamming window, top-5 peaks | `fft.c` | power-of-two sizes |

Sobel and Canny are not included. Both work on images and have no meaning for
an inertial signal.

## Hardware

- Raspberry Pi Pico W on a Waveshare Pico Eval Board
- ICM-20948 IMU, accelerometer +/-2 g, gyroscope +/-1000 dps
- microSD card, FAT formatted, in the board's slot (wired for SDIO)

## Build

Everything is built inside the course Docker container, which carries the Pico
SDK and `picotool`. The container and the `build_in_docker.sh` / `flash.sh`
scripts come from the course lab repository
(`ias0360-lab-excercises-2026`), so place the contents of this branch in that
repository as `hw1/` and start the container from its root:

```sh
git clone -b submissions/253101IAPM <this repo> hw1   # inside the lab repo
./build_in_docker.sh
```

Then, inside the container:

```sh
cd hw1/build
cmake -DDATASET=still ..
cmake --build . -j4
```

`DATASET` selects which recording the program reads. `N_SAMPLES` sets how many
samples are analysed and must be a power of two; it defaults to 2048. `FC_HZ`
is the low-pass cutoff in Hz, default 5.0 - it has to match the motion being
looked for, since a cutoff below the motion's own frequency removes the signal
rather than the noise.

Flash with:

```sh
./flash.sh hw1/build/hw1.uf2
```

## Recording data

Build and flash the logger once per recording:

```sh
cd hw1/logger/build
cmake -DOUT_NAME=walking -DRUN_SECONDS=15 -DSTART_DELAY_MS=10000 ..
cmake --build . -j4
../../../flash.sh logger.uf2
```

`START_DELAY_MS` is the time between power-on and the start of logging, so
there is a chance to pick the board up before it starts. The program writes
`OUT_NAME.bin` to the card and then idles.

The three recordings used in the report are `still`, `walking` and `shaking`,
15 s each at 500 Hz.

## Running the analysis

Flash `hw1.uf2` and open the serial port:

```sh
cat /dev/ttyACM0
```

The program prints after a 3 s delay. It runs once per reset; to run it again,
reset the board or reflash.

## Expected output

On the serial port:

```
=== IAS0360 HW1 - IMU feature extraction ===
dataset = shaking, requested 2048 samples
loaded 0:/shaking.bin: 2048 samples (32768 bytes)
raw block written to 0:/shaking_raw.csv

=== Low-pass filter (ax, 2nd order Butterworth, fc = 5.0 Hz) ===
  std before = 0.097659, after = 0.056659  (42.0% of the spread removed)
  removed component: RMS 0.110214, largest 0.285979
  [11849 us for 2048 samples, 5.79 us/sample]

=== Statistics (physical units: g, dps) ===
  ax   mean=-0.05625  var= 0.00954  std= 0.09766  min=-0.34467  max= 0.20459  median=-0.06580
       mode=-0.16461 (repeats 4 times)
  ...
  [3078245 us for 6 axes]

=== Quantisation (ax, DC removed, peak normalised) ===
  removed DC = -0.05625, peak before scaling = 0.28842
  bits      SNR dB      RMS err      max err    clips      bytes
  16         91.61    8.880e-06    1.526e-05        0       4096   [11430 us]
  8          43.43    2.280e-03    3.905e-03        0       2048   [12209 us]
  4          19.45    3.602e-02    6.241e-02        6       1024   [12030 us]
  reference float32: 8192 bytes

=== FFT (ax, 2048 points, fs = 500 Hz) ===
  bin width = 0.2441 Hz, 160294 us for the whole transform
  per-sample budget at 500 Hz = 2000.0 us; FFT costs 78.3 us/sample
  peak 1: bin   31     7.568 Hz  amplitude 0.051364
  ...
  spectrum written to 0:/shaking_fft.csv
done.
```

Two files are written to the card per run:

| File | Contents |
|---|---|
| `<dataset>_raw.csv` | header `# fs_hz=... n=...`, then `ax,ay,az,gx,gy,gz,ax_filt` in g and dps; the last column is the low-pass output |
| `<dataset>_fft.csv` | `freq_hz,amplitude`, one row per bin, single sided |

## PC cross-check

Copy the CSV files from the card into `data/`, then:

```sh
python3 analyze.py                    # all three recordings
python3 analyze.py shaking            # or just one
```

It needs `numpy` and `matplotlib`. For each recording it prints the largest
difference between the on-device and NumPy spectra and the five strongest peaks
side by side, and writes plots to `plots/`:

```
=== shaking ===
  fs = 500.0 Hz, n = 2048, bin width = 0.2441 Hz
  max |PC - MCU|      = 3.018e-07
  that is             = 0.0006% of the largest PC bin
  RMS difference      = 1.825e-08
  top peaks (PC vs MCU):
    PC bin   31    7.568 Hz 0.051364   |   MCU bin   31    7.568 Hz 0.051364
  ...
  plot -> plots/shaking.png
```

## Layout

```
hw1/
  main.c            SD mount, load, pipeline, timing, printing
  preprocess.c      DC removal, peak normalisation
  filter.c          second-order Butterworth low-pass (biquad)
  quantize.c        quantise / dequantise / error metrics
  stats.c           statistics
  fft.c             radix-2 FFT, Hamming window, peak picking
  hw1.h             shared types, sensor scale factors
  CMakeLists.txt
  analyze.py        host-side NumPy cross-check and plots
  logger/           IMU recorder (Lab 1.1 with build-time options)
  config/           hw_config.c, SD card pin configuration
  lib/              board libraries, shared with logger/
  data/             recordings and CSV exports
  plots/            figures produced by analyze.py
  report/           IEEE report, LaTeX source and PDF
```

## Notes

- The accelerometer is at +/-2 g. Shaking the board hard saturates it at 32767
  counts, which no amount of processing can undo. If larger motions matter,
  raise the range in `icm20948.c`.
- Statistics are dominated by the median, which uses an insertion sort and is
  `O(n^2)`. At 2048 samples that is roughly 3 s for six axes, about twenty
  times the cost of the FFT.
