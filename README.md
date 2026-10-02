# IMU Logger and Analysis

This workspace contains Raspberry Pi Pico W firmware for recording ICM-20948 IMU data to an SD card, plus Python scripts for checking the exported features and FFTs and plotting trial comparisons. The example trials are `still`, `tap`, `tilt`, and `lift`.

## Python Scripts

The scripts use Python 3, NumPy, pandas, and Matplotlib. Install the Python dependencies with:

```powershell
python -m pip install numpy pandas matplotlib
```

### `compare_imu_mcu_trials.py`

Checks the MCU's rolling time-domain features against values recomputed on the PC from raw IMU CSVs. For each trial it also writes a baseline-corrected vertical-acceleration plot and a plot comparing raw and low-pass-filtered window peaks.

- `expected_features(raw)` computes the expected 20-sample peak, RMS, and sample variance for raw vertical acceleration and two low-pass-filtered signals (alpha 0.35 and 0.15), plus the maximum raw gyro magnitude. It skips 20 startup samples and uses the next 40 to estimate the vertical-acceleration baseline; windows advance by 10 samples.
- `inspect_trial(label, raw_path, feature_path)` validates window indices and timestamps, compares MCU and PC feature values, and returns a trial summary, the feature table with time values, raw samples, and per-feature maximum errors.
- `main()` parses paths for all four trials, writes summary and validation CSVs, and saves two plots per trial. It defaults to an output directory named `imu_comparison`.

Example using the checked-in feature captures:

```powershell
python compare_imu_mcu_trials.py `
  --still-raw data/features/still/road_still_test_preprocessing_one.csv `
  --still-features data/features/still/road_still_test_preprocessing_one_features.csv `
  --tap-raw data/features/tap/road_tap_test_preprocessing_one.csv `
  --tap-features data/features/tap/road_tap_test_preprocessing_one_features.csv `
  --tilt-raw data/features/tilt/road_tilt_test_preprocessing_one.csv `
  --tilt-features data/features/tilt/road_tilt_test_preprocessing_one_features.csv `
  --lift-raw data/features/lift/road_vertical_lift_fast_test_preprocessing_one.csv `
  --lift-features data/features/lift/road_vertical_lift_fast_test_preprocessing_one_features.csv `
  --out imu_comparison
```

### `compare_four_imu_fft_trials.py`

Validates and compares the MCU FFT exports across all four trials. It checks raw capture and FFT block structure, verifies FFT timestamps against raw sample indices, and checks feature timestamps when a feature CSV is present. It then calculates the largest non-DC peak and summed magnitude-squared values in the 0.5–3 Hz, 3–10 Hz, and 10–21 Hz bands for each block.

- `main()` reads the four expected raw and FFT CSV filenames from `--folder` (default `.`), optionally reads the feature CSVs, and writes two summary CSVs and two plots to `--out` (default `fft_trial_comparison`). All four trials' expected filenames must be present in the same input folder.

### `validate_imu_fft.py`

Compares each exported 64-point MCU FFT with a NumPy reference computed from the matching raw samples. The reference removes the mean, applies a Hamming window, calculates the one-sided real FFT magnitude and frequency bins, then reports frequency and magnitude errors and FFT processing time.

- `main()` reads `--raw` and `--fft` (defaults `road_raw_imu.csv` and `road_fft_mcu.csv`) and writes `fft_mcu_pc_validation.csv` in the current directory.

### `visualize_imu_fft.py`

Creates a four-panel explanation of one FFT block: measured vertical acceleration, mean removal and Hamming tapering, one-sided spectrum, and spectrum magnitudes by bin.

- `main()` reads `--raw` and `--fft`, plots the block selected by `--block` (default 0), and saves the figure to `--out` (default `imu_fft_explained.png`).

## Firmware Flow (`main.cpp`)

The program initializes USB serial output, status LEDs, and the ICM-20948. The main loop continuously reads accelerometer, gyroscope, and magnetometer values and publishes the latest sample and sensor timestamp under a mutex. It launches `core1_entry()` on the second Pico core to handle SD-card logging independently.

The logging core mounts the SD card and creates `road_raw_imu.csv`. It polls at a nominal 2 ms period, but only accepts a sample when the sensor timestamp has changed, so duplicate reads are not logged. Each accepted record contains storage and sensor timestamps plus the raw three-axis accelerometer, gyroscope, and magnetometer counts. Data is written in batches of 32 and synchronized to the card during capture. The checked-in captures show a median accepted interval of about 24.3 ms (about 41 Hz); the 2 ms setting is the logger polling period, not the observed sensor update rate.

After 3,000 accepted rows, the firmware closes the raw CSV and exports two more files:

- It ignores 20 startup samples, estimates a vertical-acceleration baseline from the next 40, and subtracts that baseline from subsequent `az` values after scaling by 32768. It calculates 20-sample rolling peak, RMS, and sample variance for the raw signal and two first-order low-pass signals (alpha 0.35 and 0.15). Feature windows advance by 10 samples, producing 293 rows per full capture. The maximum gyro-vector magnitude is also included for each window.
- It groups the post-calibration samples into non-overlapping blocks of 64, removes each block's mean, applies a Hamming window, and calculates a radix-2 FFT. Each block exports 33 bins (0 through 32), sampling frequency, strongest non-DC frequency and magnitude, and compute time. A full 3,000-row capture yields 45 complete FFT blocks; trailing samples that do not fill another block are omitted.

SD-card errors or missing sensor detection turn on the error LED and stop in a fatal loop. The init LED marks startup, and the logging LED remains on while capture is in progress.

## CSV Data and Graphs

The `data/features/<trial>/` folder contains raw captures and the matching MCU time-domain feature exports. Its `summary/` folder contains combined trial metrics, all feature windows, and the PC-versus-MCU feature error table. The `data/fft/<trial>/` folder contains raw captures, MCU FFT exports, and feature exports used with the FFT comparisons. Its `summary/` folder contains FFT band metrics, trial summaries, and MCU-versus-PC FFT validation results. Raw files contain signed sensor counts; feature acceleration values are normalized counts after baseline subtraction, while gyro magnitude remains in raw gyro counts.

Feature CSV columns include sample indices and sensor timestamps for each window, the estimated baseline, raw/filtered peak, RMS and variance, maximum gyro magnitude, and MCU compute time. The feature `trial_summary.csv` condenses each trial to capture duration/rate, feature maxima, peak-window time, median feature compute time, and maximum PC-versus-MCU error. `all_feature_windows.csv` combines the per-window rows with a trial label and elapsed time. `mcu_pc_validation.csv` gives the maximum absolute error for each feature and trial; small nonzero differences are expected because the MCU calculations use floating-point arithmetic.

FFT CSVs contain one row per bin, with the block's sample range and timestamps repeated alongside frequency and magnitude. `fft_block_band_metrics.csv` summarizes each block's peak, FFT runtime, sample rate, and energy-like band values (sum of squared magnitudes). `fft_trial_summary.csv` aggregates maxima and median timing by trial. `fft_mcu_pc_validation.csv` records per-block MCU-versus-NumPy frequency and magnitude errors.

The checked-in plots under `graphs/` show:

- `graphs/features/<trial>_signal.png`: baseline-corrected vertical acceleration over the full capture; the dashed line marks where feature analysis begins.
- `graphs/features/<trial>_peaks.png`: raw and two low-pass-filtered peak values across feature-window end times.
- `graphs/fft/fft_band_metrics_by_time.png`: four time-series panels comparing the strongest non-DC FFT magnitude and low-, mid-, and high-frequency band sums across trials.
- `graphs/fft/representative_spectra.png`: one-sided spectrum for each trial's block with the largest total band sum.

These metrics make the activity trials comparable, but they are descriptive signal features, not by themselves a trained classifier or a claim of statistical significance.