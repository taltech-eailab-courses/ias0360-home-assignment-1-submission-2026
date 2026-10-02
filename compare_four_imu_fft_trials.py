import argparse
from pathlib import Path
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

TRIALS = ("still", "tap", "tilt", "lift")
RAW_NAMES = {"still": "road_still.csv", "tap": "road_tap.csv", "tilt": "road_tilt.csv", "lift": "road_lift.csv"}
FFT_NAMES = {"still": "road_fft_still_mcu.csv", "tap": "road_fft_tap_mcu.csv", "tilt": "road_fft_tilt_mcu.csv", "lift": "road_fft_lift_mcu.csv"}
FEATURE_NAMES = {"still": "road_features_mcu_still.csv", "tap": "road_tap_features_mcu.csv", "tilt": "road_features_mcu_tilt.csv", "lift": "road_features_mcu_lift.csv"}


def main():
    p = argparse.ArgumentParser(description="Compare MCU FFTs across four IMU trials")
    p.add_argument("--folder", default=".")
    p.add_argument("--out", default="fft_trial_comparison")
    args = p.parse_args()
    folder, out = Path(args.folder), Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    summaries, block_rows, spectra = [], [], []

    for trial in TRIALS:
        raw = pd.read_csv(folder / RAW_NAMES[trial])
        fft = pd.read_csv(folder / FFT_NAMES[trial])
        if len(raw) != 3000:
            raise ValueError(f"{trial}: expected 3000 raw rows, got {len(raw)}")
        if not raw.sensor_timestamp_us.is_monotonic_increasing or raw.sensor_timestamp_us.duplicated().any():
            raise ValueError(f"{trial}: read-completion timestamps are not distinct and increasing")
        if not np.array_equal(raw.index.to_numpy(), raw['index'].to_numpy()):
            raise ValueError(f"{trial}: raw index column is not 0..2999")
        if len(fft) != 45 * 33:
            raise ValueError(f"{trial}: expected 1485 FFT rows, got {len(fft)}")
        feat_path = folder / FEATURE_NAMES.get(trial, "__missing__")
        features = pd.read_csv(feat_path) if feat_path.exists() else None
        if features is not None:
            if len(features) != 293 or not np.array_equal(features.end_sensor_timestamp_us.to_numpy(), raw.sensor_timestamp_us.iloc[features.end_index.to_numpy()].to_numpy()):
                raise ValueError(f"{trial}: feature CSV does not match raw capture")

        first_time = int(raw.sensor_timestamp_us.iloc[0])
        interval = raw.sensor_timestamp_us.diff().dropna().to_numpy() / 1e6
        if np.max(interval) > 1.5 * np.median(interval):
            print(f"WARNING {trial}: long read-completion gap {np.max(interval):.4f} s")
        for block_no, group in fft.groupby("block", sort=True):
            b = group.sort_values("bin")
            start, end = int(b.start_index.iloc[0]), int(b.end_index.iloc[0])
            if len(b) != 33 or not np.array_equal(b.bin.to_numpy(), np.arange(33)) or end - start != 63:
                raise ValueError(f"{trial} block {block_no}: invalid bin or raw-index count")
            if int(b.start_sensor_timestamp_us.iloc[0]) != int(raw.sensor_timestamp_us.iloc[start]) or int(b.end_sensor_timestamp_us.iloc[0]) != int(raw.sensor_timestamp_us.iloc[end]):
                raise ValueError(f"{trial} block {block_no}: FFT timestamps do not match raw capture")
            frequency = b.freq_hz.to_numpy(float)
            amplitude = b.magnitude.to_numpy(float)
            low = float(np.sum(amplitude[(frequency >= 0.5) & (frequency < 3.0)] ** 2))
            mid = float(np.sum(amplitude[(frequency >= 3.0) & (frequency < 10.0)] ** 2))
            high = float(np.sum(amplitude[(frequency >= 10.0) & (frequency <= 21.0)] ** 2))
            block_rows.append({
                "trial": trial, "block": int(block_no), "start_index": start, "end_index": end,
                "center_time_s": ((int(b.start_sensor_timestamp_us.iloc[0]) + int(b.end_sensor_timestamp_us.iloc[0])) / 2 - first_time) / 1e6,
                "fs_hz": float(b.fs_hz.iloc[0]), "fft_us": int(b.fft_us.iloc[0]),
                "peak_hz": float(b.peak_hz.iloc[0]), "peak_magnitude": float(b.peak_magnitude.iloc[0]),
                "band_low_mag2": low, "band_mid_mag2": mid, "band_high_mag2": high,
            })
            spectra.append((trial, int(block_no), frequency, amplitude))
        tr = pd.DataFrame([r for r in block_rows if r["trial"] == trial])
        summaries.append({
            "trial": trial, "raw_rows": len(raw), "fft_blocks": len(tr), "feature_rows": len(features) if features is not None else "missing",
            "median_read_interval_ms": float(np.median(interval) * 1000),
            "median_fft_us": float(tr.fft_us.median()),
            "max_peak_magnitude": float(tr.peak_magnitude.max()),
            "max_low_mag2": float(tr.band_low_mag2.max()),
            "max_mid_mag2": float(tr.band_mid_mag2.max()),
            "max_high_mag2": float(tr.band_high_mag2.max()),
            "max_energy_block": int(tr.loc[tr.band_low_mag2.add(tr.band_mid_mag2).add(tr.band_high_mag2).idxmax(), "block"]),
        })

    blocks = pd.DataFrame(block_rows)
    summary = pd.DataFrame(summaries)
    blocks.to_csv(out / "fft_block_band_metrics.csv", index=False)
    summary.to_csv(out / "fft_trial_summary.csv", index=False)
    fig, axs = plt.subplots(2, 2, figsize=(12, 7), constrained_layout=True)
    for ax, metric, title in zip(axs.flat,
                                 ("peak_magnitude", "band_low_mag2", "band_mid_mag2", "band_high_mag2"),
                                 ("Largest non-DC FFT magnitude", "0.5–3 Hz: sum of magnitude²", "3–10 Hz: sum of magnitude²", "10–21 Hz: sum of magnitude²")):
        for trial in TRIALS:
            data = blocks[blocks.trial == trial]
            ax.plot(data.center_time_s, data[metric], marker=".", label=trial)
        ax.set(title=title, xlabel="Seconds since first read")
        ax.legend()
    fig.savefig(out / "fft_band_metrics_by_time.png", dpi=160)
    plt.close(fig)
    fig, axs = plt.subplots(2, 2, figsize=(12, 7), constrained_layout=True)
    for ax, trial in zip(axs.flat, TRIALS):
        block_no = int(summary.loc[summary.trial == trial, "max_energy_block"].iloc[0])
        _, _, frequency, amplitude = next(x for x in spectra if x[0] == trial and x[1] == block_no)
        ax.stem(frequency, amplitude, basefmt=" ")
        ax.set(title=f"{trial}: max-energy block {block_no}", xlabel="Frequency (Hz)", ylabel="One-sided magnitude")
    fig.savefig(out / "representative_spectra.png", dpi=160)
    plt.close(fig)
    print(summary.to_string(index=False))
    print(f"Saved results to {out}")


if __name__ == "__main__":
    main()
