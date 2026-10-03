#!/usr/bin/env python3
# Checks the Pico's FFT against numpy. Reads the CSVs the board wrote to the
# SD card, redoes the transform here with the same window and scaling, and
# diffs them bin by bin. Run from the hw1 directory.

import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

DATA = Path(__file__).parent / "data"
PLOTS = Path(__file__).parent / "plots"
DATASETS = ["still", "walking", "shaking"]
AXIS = "ax"   # the axis the board analysed
PEAKS = 5


def load_raw(name):
    path = DATA / f"{name}_raw.csv"
    header = path.open().readline()
    fs = float(header.split("fs_hz=")[1].split()[0])
    data = np.genfromtxt(path, delimiter=",", names=True, skip_header=1)
    cols = {c: np.asarray(data[c], dtype=float) for c in data.dtype.names}
    return fs, len(cols[AXIS]), cols


def load_mcu_fft(name):
    d = np.genfromtxt(DATA / f"{name}_fft.csv", delimiter=",", names=True)
    return np.asarray(d["freq_hz"], float), np.asarray(d["amplitude"], float)


def pc_spectrum(x, fs):
    # same steps as the board: DC out, Hamming, single sided
    n = len(x)
    x = x - x.mean()
    w = 0.54 - 0.46 * np.cos(2 * np.pi * np.arange(n) / (n - 1))
    X = np.fft.fft(x * w)
    half = n // 2
    mag = np.abs(X[:half]) / n
    mag[1:] *= 2.0
    freq = np.arange(half) * fs / n
    return freq, mag


def top_peaks(freq, mag, k=PEAKS):
    idx = np.argsort(mag[1:])[::-1][:k] + 1   # +1 because DC is skipped
    return [(int(i), float(freq[i]), float(mag[i])) for i in idx]


def report(name):
    fs, n, cols = load_raw(name)
    x = cols[AXIS]

    f_pc, m_pc = pc_spectrum(x, fs)
    f_mcu, m_mcu = load_mcu_fft(name)

    k = min(len(m_pc), len(m_mcu))
    diff = m_pc[:k] - m_mcu[:k]
    denom = np.maximum(m_pc[:k].max(), 1e-12)

    print(f"\n=== {name} ===")
    print(f"  fs = {fs:.1f} Hz, n = {n}, bin width = {fs / n:.4f} Hz")
    print(f"  max |PC - MCU|      = {np.abs(diff).max():.3e}")
    print(f"  that is             = {100 * np.abs(diff).max() / denom:.4f}% of the largest PC bin")
    print(f"  RMS difference      = {np.sqrt((diff ** 2).mean()):.3e}")

    print("  top peaks (PC vs MCU):")
    for (i_pc, hz_pc, a_pc), (i_m, hz_m, a_m) in zip(
        top_peaks(f_pc, m_pc), top_peaks(f_mcu, m_mcu)
    ):
        print(f"    PC bin {i_pc:4d} {hz_pc:8.3f} Hz {a_pc:.6f}   |   "
              f"MCU bin {i_m:4d} {hz_m:8.3f} Hz {a_m:.6f}")

    PLOTS.mkdir(exist_ok=True)
    t = np.arange(n) / fs

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(9, 7))

    ax1.plot(t, x, lw=0.6)
    ax1.set_title(f"{name}: accelerometer X, raw")
    ax1.set_xlabel("time [s]")
    ax1.set_ylabel("acceleration [g]")
    ax1.grid(alpha=0.3)

    ax2.plot(f_pc, m_pc, lw=1.2, label="PC (numpy)")
    ax2.plot(f_mcu, m_mcu, lw=0.9, ls="--", label="MCU (Pico)")
    ax2.set_title(f"{name}: single-sided spectrum, MCU vs PC")
    ax2.set_xlabel("frequency [Hz]")
    ax2.set_ylabel("amplitude [g]")
    ax2.set_xlim(0, 50)
    ax2.legend()
    ax2.grid(alpha=0.3)

    fig.tight_layout()
    out = PLOTS / f"{name}.png"
    fig.savefig(out, dpi=150)
    plt.close(fig)
    print(f"  plot -> {out}")

    # spectrum only, for the report - fig 1 already has the time domain
    fig2, ax = plt.subplots(figsize=(6.5, 2.6))
    ax.plot(f_pc, m_pc, lw=1.3, label="PC (numpy)")
    ax.plot(f_mcu, m_mcu, lw=1.0, ls="--", label="MCU (Pico)")
    ax.set_xlabel("frequency [Hz]")
    ax.set_ylabel("amplitude [g]")
    ax.set_xlim(0, 50)
    ax.legend()
    ax.grid(alpha=0.3)
    fig2.tight_layout()
    fig2.savefig(PLOTS / f"{name}_spec.png", dpi=170)
    plt.close(fig2)

    return dict(name=name, fs=fs, n=n, x=x, cols=cols,
                max_diff=float(np.abs(diff).max()))


def summary(results):
    plt.rcParams.update({"font.size": 11, "axes.titlesize": 12,
                         "axes.labelsize": 11})
    fig, axes = plt.subplots(len(results), 2, figsize=(13, 2.6 * len(results)))
    for row, r in enumerate(results):
        t = np.arange(r["n"]) / r["fs"]
        f, m = pc_spectrum(r["x"], r["fs"])

        axes[row][0].plot(t, r["x"], lw=0.7)
        axes[row][0].set_ylabel(f"{r['name']}\n$a_x$ [g]")
        axes[row][0].grid(alpha=0.3)

        axes[row][1].plot(f, m, lw=1.1)
        axes[row][1].set_xlim(0, 30)
        axes[row][1].set_ylabel("amplitude [g]")
        axes[row][1].grid(alpha=0.3)

        # label the top bin, easier to read than hunting for it
        peak = int(np.argmax(m[1:]) + 1)
        axes[row][1].annotate(f"{f[peak]:.2f} Hz",
                              xy=(f[peak], m[peak]),
                              xytext=(f[peak] + 3, m[peak] * 0.85),
                              arrowprops=dict(arrowstyle="->", lw=0.9))

    axes[0][0].set_title("time domain")
    axes[0][1].set_title("single-sided spectrum")
    axes[-1][0].set_xlabel("time [s]")
    axes[-1][1].set_xlabel("frequency [Hz]")
    fig.tight_layout()
    out = PLOTS / "comparison.png"
    fig.savefig(out, dpi=170)
    plt.close(fig)
    print(f"\ncombined plot -> {out}")


def main():
    names = sys.argv[1:] or DATASETS
    results = []
    for name in names:
        if not (DATA / f"{name}_raw.csv").exists():
            print(f"skipping {name}: no {name}_raw.csv")
            continue
        results.append(report(name))
    if len(results) > 1:
        summary(results)


if __name__ == "__main__":
    main()
