#!/usr/bin/env python3
"""
IAS0360 Homework 1 - Real IMU Signal Processing & Feature Extraction Verification
Author: Miguel Robledo (miguro)

Validates MCU execution against PC re-computations using physical sensor data recorded on Raspberry Pi Pico 2:
1. 'rotation_imu_data.bin' & 'rotation_imu_features.csv' (Rotations experiment)
2. 'tapping_imu_features.csv' (Tapping experiment)
3. 'shaking_imu_features.csv' (Shaking experiment)
"""

import os
import sys
import struct
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

plt.style.use('seaborn-v0_8-paper' if 'seaborn-v0_8-paper' in plt.style.available else 'default')
plt.rcParams.update({
    'font.size': 8.5,
    'axes.labelsize': 8.5,
    'axes.titlesize': 9.0,
    'xtick.labelsize': 7.5,
    'ytick.labelsize': 7.5,
    'legend.fontsize': 7.5,
    'figure.titlesize': 10,
    'figure.dpi': 230,
    'lines.linewidth': 1.2,
    'grid.alpha': 0.35,
    'grid.linestyle': '--'
})

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
DATA_DIR = os.path.join(BASE_DIR, "data")
FIG_DIR = os.path.join(BASE_DIR, "figures")
os.makedirs(FIG_DIR, exist_ok=True)

RAW_BIN = os.path.join(DATA_DIR, "rotation_imu_data.bin")
ROT_CSV = os.path.join(DATA_DIR, "rotation_imu_features.csv")
TAP_CSV = os.path.join(DATA_DIR, "tapping_imu_features.csv")
SHK_CSV = os.path.join(DATA_DIR, "shaking_imu_features.csv")

FS = 500.0  # Sampling rate in Hz
N = 256     # Window size (512 ms)
ACCEL_SCALE = 16384.0

# -----------------------------------------------------------------------------
# 1. Unpack Real Raw Sensor Samples (Rotation Run)
# -----------------------------------------------------------------------------
sample_fmt = "<hhhhhhI"
sample_size = struct.calcsize(sample_fmt)

with open(RAW_BIN, "rb") as f:
    raw_bytes = f.read(N * sample_size * 5)  # Read 5 windows (1280 samples ~ 2.5s)

n_read = len(raw_bytes) // sample_size
samples = [struct.unpack(sample_fmt, raw_bytes[i*sample_size:(i+1)*sample_size]) for i in range(n_read)]

ax_all = np.array([s[0] for s in samples], dtype=np.float32) / ACCEL_SCALE
ay_all = np.array([s[1] for s in samples], dtype=np.float32) / ACCEL_SCALE
az_all = np.array([s[2] for s in samples], dtype=np.float32) / ACCEL_SCALE
t_all = np.arange(len(ax_all)) / FS

# First window for strict single-window algorithmic parity
ax_w0 = ax_all[:N]
ay_w0 = ay_all[:N]
az_w0 = az_all[:N]
amag_w0 = np.sqrt(ax_w0**2 + ay_w0**2 + az_w0**2)
t_w0 = t_all[:N]

# -----------------------------------------------------------------------------
# 2. Independent PC Feature Computation on Real Window 0
# -----------------------------------------------------------------------------
mean_val = float(np.mean(amag_w0))
std_val = float(np.std(amag_w0, ddof=1))
min_val = float(np.min(amag_w0))
max_val = float(np.max(amag_w0))
range_val = max_val - min_val
med_val = float(np.median(amag_w0))
rms_val = float(np.sqrt(np.mean(amag_w0**2)))

# FFT with Hamming window
w = np.hamming(N)
X = np.fft.rfft(amag_w0 * w, n=N)
amp = np.abs(X) * ((2.0 / N) / 0.54)
freqs = np.fft.rfftfreq(N, 1.0 / FS)
top_idx = np.argsort(amp[1:])[::-1][:5] + 1
top_freqs = freqs[top_idx]
top_amps = amp[top_idx]
spectral_energy = float(np.sum(amp[1:]**2))

# Q15 Quantization
norm_x = np.clip((amag_w0 / 2.0) - 0.5, -0.999969, 0.999969)
q15 = np.round(norm_x * 32768.0).astype(np.int16)
dequant = q15.astype(np.float32) / 32768.0
err = norm_x - dequant
sig_pwr = np.mean(norm_x**2)
err_pwr = np.mean(err**2)
snr_db = 10.0 * np.log10(sig_pwr / err_pwr) if err_pwr > 0 else 999.0

# -----------------------------------------------------------------------------
# 3. Load Embedded Features Recorded by Pico across the 3 Experiments
# -----------------------------------------------------------------------------
df_rot = pd.read_csv(ROT_CSV)
df_tap = pd.read_csv(TAP_CSV)
df_shk = pd.read_csv(SHK_CSV)

# -----------------------------------------------------------------------------
# 4. Generate Composite Publication Figures for IEEE Report
# -----------------------------------------------------------------------------

# Figure 1: Real Sensor Waveforms & Spectrum from Physical Pico 2
fig, (ax1, ax2, ax3) = plt.subplots(1, 3, figsize=(7.1, 2.15))

# (a) Real tri-axial acceleration transitioning during physical rotation
ax1.plot(t_all, ax_all, label=r"$a_x$", color="#1f77b4")
ax1.plot(t_all, ay_all, label=r"$a_y$", color="#ff7f0e")
ax1.plot(t_all, az_all, label=r"$a_z$", color="#2ca02c")
ax1.set_xlabel("Time (s)")
ax1.set_ylabel("Accel (g)")
ax1.set_title(r"(a) Measured $a_x, a_y, a_z$ (Rotation)")
ax1.grid(True)
ax1.legend(loc="upper right", ncol=3, framealpha=0.85, fontsize=7)

# (b) Orientation-invariant magnitude norm
amag_all = np.sqrt(ax_all**2 + ay_all**2 + az_all**2)
ax2.plot(t_all, amag_all, label=r"$\|a\|$", color="#d62728", lw=1.2)
ax2.axhline(1.0, color="black", linestyle="--", label="1.0g Reference")
ax2.set_xlabel("Time (s)")
ax2.set_ylabel("Norm |a| (g)")
ax2.set_title(r"(b) Vector Norm Invariance $\|a\| \approx 1g$")
ax2.set_ylim(0.7, 1.3)
ax2.grid(True)
ax2.legend(loc="upper right", framealpha=0.85, fontsize=7)

# (c) Real FFT Spectrum from physical sensor window
ax3.plot(freqs, amp, color="#1f77b4", label="FFT (Hamming)")
ax3.scatter(top_freqs[:3], top_amps[:3], color="#d62728", s=25, zorder=5, label="Top Peaks")
for f_val, a_val in zip(top_freqs[:2], top_amps[:2]):
    ax3.annotate(f"{f_val:.1f}Hz",
                 xy=(f_val, a_val), xytext=(4, 4),
                 textcoords="offset points", fontsize=7.5,
                 bbox=dict(boxstyle="round,pad=0.15", fc="yellow", alpha=0.75, ec="orange"),
                 arrowprops=dict(arrowstyle="->", color="black", lw=0.7))
ax3.set_xlabel("Frequency (Hz)")
ax3.set_ylabel("Amplitude (g)")
ax3.set_title(r"(c) Measured Spectrum ($\Delta f = 1.95$ Hz)")
ax3.set_xlim(0, 50)
ax3.grid(True)
ax3.legend(loc="upper right", framealpha=0.85, fontsize=7)

plt.tight_layout()
fig.savefig(os.path.join(FIG_DIR, "fig_time_freq_overview.png"), bbox_inches="tight")
plt.close(fig)

# Figure 2: Comparative Experimental Evaluation (Rotations vs Tapping vs Shaking)
fig, (ax_v1, ax_v2) = plt.subplots(1, 2, figsize=(7.1, 2.15))

# (a) Motion intensity (std dev) over window sequence across the 3 physical activities
w_len = min(len(df_rot), len(df_tap), len(df_shk), 45)
w_idx = np.arange(w_len)

ax_v1.plot(w_idx, df_rot['amag_std'].iloc[:w_len], label="Rotations (Quasi-Static)", color="#2ca02c", lw=1.3)
ax_v1.plot(w_idx, df_tap['amag_std'].iloc[:w_len], label="Tapping (Periodic Impacts)", color="#1f77b4", lw=1.3)
ax_v1.plot(w_idx, df_shk['amag_std'].iloc[:w_len], label="Shaking (High Dynamic)", color="#d62728", lw=1.3)
ax_v1.set_title("(a) Motion Intensity: Std Dev ($\sigma$)")
ax_v1.set_xlabel("Window Index (512 ms / step)")
ax_v1.set_ylabel(r"$\sigma_{\text{norm}}$ (g)")
ax_v1.grid(True)
ax_v1.legend(loc="upper left", framealpha=0.85, fontsize=7)

# (b) Spectral Energy vs Quantization SNR across the 3 activities
categories = ['Rotations', 'Tapping', 'Shaking']
avg_energy = [df_rot['spectral_energy'].mean(), df_tap['spectral_energy'].mean(), df_shk['spectral_energy'].mean()]
avg_snr = [df_rot['q15_snr_db'].mean(), df_tap['q15_snr_db'].mean(), df_shk['q15_snr_db'].mean()]

ax_b1 = ax_v2
ax_b2 = ax_b1.twinx()

bars = ax_b1.bar(np.arange(3) - 0.18, avg_energy, width=0.35, color="#1f77b4", label="Spectral Energy ($g^2$)")
lines = ax_b2.plot(np.arange(3) + 0.18, avg_snr, color="#d62728", marker='o', markersize=4, label="Q15 SNR (dB)")

ax_b1.set_xticks(np.arange(3))
ax_b1.set_xticklabels(categories)
ax_b1.set_ylabel("Spectral Energy ($g^2$)", color="#1f77b4")
ax_b2.set_ylabel("Q15 SNR (dB)", color="#d62728")
ax_b1.set_title("(b) Energy & Quantization SNR Comparison")
ax_b1.grid(True)

# Combined legend
h1, l1 = ax_b1.get_legend_handles_labels()
h2, l2 = ax_b2.get_legend_handles_labels()
ax_b1.legend(h1 + h2, l1 + l2, loc="upper right", framealpha=0.85, fontsize=7)

fig.tight_layout()
fig.savefig(os.path.join(FIG_DIR, "fig_variations_overview.png"), bbox_inches="tight")
plt.close(fig)

# -----------------------------------------------------------------------------
# 5. Print Detailed Physical Validation Summary
# -----------------------------------------------------------------------------
row0_mcu = df_rot.iloc[0]

print("=====================================================================")
print("  IAS0360 HW1 PHYSICAL EMBEDDED VALIDATION (Pico 2 + ICM-20948)     ")
print("=====================================================================")
print(f"Sampling Rate: {FS:.1f} Hz | Window: {N} samples ({N/FS*1000:.1f} ms)")
print("---------------------------------------------------------------------")
print("1. MCU vs. PC RE-COMPUTATION PARITY (Physical Sensor Window 0):")
print(f"  - Mean (|a|)      : MCU = {row0_mcu['amag_mean']:.5f} g | PC = {mean_val:.5f} g | Diff = {abs(row0_mcu['amag_mean'] - mean_val):.6f}")
print(f"  - StdDev (|a|)    : MCU = {row0_mcu['amag_std']:.5f} g | PC = {std_val:.5f} g | Diff = {abs(row0_mcu['amag_std'] - std_val):.6f}")
print(f"  - Min (|a|)       : MCU = {row0_mcu['amag_min']:.5f} g | PC = {min_val:.5f} g | Diff = {abs(row0_mcu['amag_min'] - min_val):.6f}")
print(f"  - Max (|a|)       : MCU = {row0_mcu['amag_max']:.5f} g | PC = {max_val:.5f} g | Diff = {abs(row0_mcu['amag_max'] - max_val):.6f}")
print(f"  - Range (|a|)     : MCU = {row0_mcu['amag_range']:.5f} g | PC = {range_val:.5f} g | Diff = {abs(row0_mcu['amag_range'] - range_val):.6f}")
print(f"  - RMS Power (|a|) : MCU = {row0_mcu['amag_rms']:.5f} g | PC = {rms_val:.5f} g | Diff = {abs(row0_mcu['amag_rms'] - rms_val):.6f}")
print(f"  - Dominant Freq   : MCU = {row0_mcu['f_dom_hz']:.2f} Hz | PC = {top_freqs[0]:.2f} Hz | Diff = {abs(row0_mcu['f_dom_hz'] - top_freqs[0]):.2f} Hz")
print(f"  - Spectral Energy : MCU = {row0_mcu['spectral_energy']:.4e} | PC = {spectral_energy:.4e} | Diff = {abs(row0_mcu['spectral_energy'] - spectral_energy):.4e}")
print(f"  - Q15 SNR         : MCU = {row0_mcu['q15_snr_db']:.2f} dB | PC = {snr_db:.2f} dB | Diff = {abs(row0_mcu['q15_snr_db'] - snr_db):.2f} dB")
print("---------------------------------------------------------------------")
print("2. EXPERIMENTAL COMPARISON ACROSS THE THREE PHYSICAL RUNS:")
print(f"  - Rotations : Mean |a|={df_rot['amag_mean'].mean():.3f}g, StdDev={df_rot['amag_std'].mean():.4f}g, Energy={df_rot['spectral_energy'].mean():.2f}, SNR={df_rot['q15_snr_db'].mean():.1f} dB")
print(f"  - Tapping   : Mean |a|={df_tap['amag_mean'].mean():.3f}g, StdDev={df_tap['amag_std'].mean():.4f}g, Energy={df_tap['spectral_energy'].mean():.2f}, SNR={df_tap['q15_snr_db'].mean():.1f} dB")
print(f"  - Shaking   : Mean |a|={df_shk['amag_mean'].mean():.3f}g, StdDev={df_shk['amag_std'].mean():.4f}g, Energy={df_shk['spectral_energy'].mean():.2f}, SNR={df_shk['q15_snr_db'].mean():.1f} dB")
print("=====================================================================")
print(f"Updated publication plots in: {FIG_DIR}")
