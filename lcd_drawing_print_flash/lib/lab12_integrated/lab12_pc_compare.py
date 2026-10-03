#!/usr/bin/env python3
import argparse
from pathlib import Path

import cv2
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

N = 256


def load_gray(path):
    img = cv2.imread(str(path), cv2.IMREAD_GRAYSCALE)
    if img is None:
        raise SystemExit(f"Failed to read image: {path}")
    return img


def run_canny(raw_path, out_path, t1=50.0, t2=150.0, l2=False):
    img = load_gray(raw_path)
    edges = cv2.Canny(img, threshold1=t1, threshold2=t2, L2gradient=l2)
    cv2.imwrite(str(out_path), edges)


def parse_fft_raw(path):
    lines = Path(path).read_text().strip().splitlines()
    if len(lines) < 2:
        raise ValueError("fft_raw file has no data line")
    values = np.array([float(v) for v in lines[1].split(',') if v], dtype=float)
    if len(values) != N:
        raise ValueError(f"Expected {N} samples, got {len(values)}")
    return values


def pc_fft(raw):
    w = np.hamming(N)
    X = np.fft.rfft(raw * w, n=N)
    amp = np.abs(X) * ((2.0 / N) / 0.54)
    freq = np.fft.rfftfreq(N, d=1.0)  # cycles/pixel
    return freq, amp


def compare_fft(raw_txt, mcu_txt, out_png):
    raw = parse_fft_raw(raw_txt)
    freq_pc, amp_pc = pc_fft(raw)

    mcu = pd.read_csv(mcu_txt)
    freq_mcu = mcu["freq_cycles_per_pixel"].to_numpy(dtype=float)
    amp_mcu = mcu["amp"].to_numpy(dtype=float)

    n = min(len(amp_pc), len(amp_mcu))
    rmse = float(np.sqrt(np.mean((amp_pc[:n] - amp_mcu[:n]) ** 2)))
    max_err = float(np.max(np.abs(amp_pc[:n] - amp_mcu[:n])))

    plt.figure(figsize=(8, 4.5))
    plt.plot(freq_pc, amp_pc, label="PC NumPy")
    plt.plot(freq_mcu, amp_mcu, "--", label="MCU radix-2")
    plt.xlabel("Spatial frequency (cycles/pixel)")
    plt.ylabel("Amplitude")
    plt.title(f"FFT MCU vs PC | RMSE={rmse:.3g}, max={max_err:.3g}")
    plt.legend()
    plt.tight_layout()
    plt.savefig(out_png, dpi=150)
    plt.close()

    print(f"FFT RMSE: {rmse:.9g}")
    print(f"FFT max abs error: {max_err:.9g}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("raw_bmp")
    ap.add_argument("--fft-raw")
    ap.add_argument("--fft-mcu")
    ap.add_argument("--canny-out", default="canny.png")
    ap.add_argument("--fft-out", default="fft_compare.png")
    ap.add_argument("--t1", type=float, default=50.0)
    ap.add_argument("--t2", type=float, default=150.0)
    ap.add_argument("--l2", action="store_true")
    args = ap.parse_args()

    run_canny(args.raw_bmp, args.canny_out, args.t1, args.t2, args.l2)
    print("Saved:", args.canny_out)

    if args.fft_raw and args.fft_mcu:
        compare_fft(args.fft_raw, args.fft_mcu, args.fft_out)
        print("Saved:", args.fft_out)


if __name__ == "__main__":
    main()
