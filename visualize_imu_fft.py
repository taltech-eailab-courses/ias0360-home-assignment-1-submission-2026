import argparse
from pathlib import Path
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt


def main():
    p = argparse.ArgumentParser(description="Plot MCU IMU FFT input and output")
    p.add_argument("--raw", default="road_raw_imu.csv")
    p.add_argument("--fft", default="road_fft_mcu.csv")
    p.add_argument("--block", type=int, default=0)
    p.add_argument("--out", default="imu_fft_explained.png")
    a = p.parse_args()
    raw = pd.read_csv(a.raw)
    fft = pd.read_csv(a.fft)
    b = fft[fft.block == a.block].sort_values("bin")
    if len(b) != 33 or not np.array_equal(b.bin.to_numpy(), np.arange(33)):
        raise ValueError("Expected 33 FFT bins numbered 0..32")
    start = int(b.start_index.iloc[0]); end = int(b.end_index.iloc[0])
    section = raw.iloc[start:end + 1]
    if len(section) != 64:
        raise ValueError("FFT block must have 64 samples")
    t = (section.sensor_timestamp_us.to_numpy() - section.sensor_timestamp_us.iloc[0]) / 1e6
    x = section.az.to_numpy(dtype=float) / 32768.0
    centered = x - x.mean()
    tapered = centered * np.hamming(64)
    fs = float(b.fs_hz.iloc[0]); freq = b.freq_hz.to_numpy(dtype=float)
    magnitudes = b.magnitude.to_numpy(dtype=float)
    fig, axes = plt.subplots(2, 2, figsize=(11, 7), constrained_layout=True)
    axes[0, 0].plot(t, x, marker=".")
    axes[0, 0].set(title="1. Measured az (64 accepted reads)", xlabel="Time since block start (s)", ylabel="Normalized counts")
    axes[0, 1].plot(t, centered, label="Mean removed", marker=".")
    axes[0, 1].plot(t, tapered, label="After Hamming window", marker=".", alpha=.8)
    axes[0, 1].set(title="2. Remove mean and taper edges", xlabel="Time since block start (s)", ylabel="Normalized counts")
    axes[0, 1].legend()
    axes[1, 0].stem(freq, magnitudes, basefmt=" ")
    strongest = int(np.argmax(magnitudes[1:]) + 1)
    axes[1, 0].annotate(f"Bin {strongest}: {freq[strongest]:.2f} Hz", (freq[strongest], magnitudes[strongest]), xytext=(.45, .8), textcoords="axes fraction", arrowprops={"arrowstyle": "->"})
    axes[1, 0].set(title="3. MCU FFT: frequency-bin amplitudes", xlabel="Frequency (Hz)", ylabel="One-sided magnitude")
    bins = np.arange(1, 33)
    axes[1, 1].bar(bins, magnitudes[1:], width=.85)
    axes[1, 1].set(title="4. Same spectrum by bin", xlabel=f"Bin number (spacing ~{fs/64:.3f} Hz)", ylabel="One-sided magnitude")
    fig.suptitle(f"IMU FFT block {a.block}: raw rows {start}-{end}, fs={fs:.2f} Hz", fontsize=14)
    fig.savefig(a.out, dpi=180)
    print("wrote", Path(a.out).resolve())


if __name__ == "__main__":
    main()
