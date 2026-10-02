import argparse
from pathlib import Path

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

SCALE = 32768.0
STARTUP = 20
CAL_N = 40
WINDOW = 20
HOP = 10
ALPHAS = (0.35, 0.15)
FIELDS = ("raw_peak", "raw_rms", "raw_variance", "lp035_peak", "lp035_rms", "lp035_variance", "lp015_peak", "lp015_rms", "lp015_variance", "gyro_peak_raw")


def expected_features(raw):
    az = raw.az.to_numpy(dtype=np.float64) / SCALE
    baseline = az[STARTUP:STARTUP + CAL_N].mean()
    x = az[STARTUP + CAL_N:] - baseline
    filtered = []
    for alpha in ALPHAS:
        state = np.float32(0.0)
        y = []
        for value in x:
            state = np.float32(state + np.float32(alpha) * (np.float32(value) - state))
            y.append(float(state))
        filtered.append(np.asarray(y))
    gx, gy, gz = (raw[c].to_numpy(dtype=np.float64)[STARTUP + CAL_N:] for c in ("gx", "gy", "gz"))
    gyro = np.sqrt(gx * gx + gy * gy + gz * gz)

    def metrics(v):
        return float(np.max(np.abs(v))), float(np.sqrt(np.mean(v * v))), float(np.var(v, ddof=1))

    records = []
    for end in range(WINDOW, len(x) + 1, HOP):
        start = end - WINDOW
        a = metrics(x[start:end])
        b = metrics(filtered[0][start:end])
        c = metrics(filtered[1][start:end])
        records.append({
            "start_index": STARTUP + CAL_N + start,
            "end_index": STARTUP + CAL_N + end - 1,
            "start_sensor_timestamp_us": int(raw.sensor_timestamp_us.iloc[STARTUP + CAL_N + start]),
            "end_sensor_timestamp_us": int(raw.sensor_timestamp_us.iloc[STARTUP + CAL_N + end - 1]),
            "baseline_normalized": baseline,
            **dict(zip(FIELDS, (*a, *b, *c, float(np.max(gyro[start:end]))))),
        })
    return pd.DataFrame.from_records(records)


def inspect_trial(label, raw_path, feature_path):
    raw = pd.read_csv(raw_path)
    mcu = pd.read_csv(feature_path)
    needed = set(("sensor_timestamp_us", "az", "gx", "gy", "gz"))
    missing = needed.difference(raw.columns)
    if missing:
        raise ValueError(f"{raw_path}: missing {sorted(missing)}")
    expected = expected_features(raw)
    if len(mcu) != len(expected):
        raise ValueError(f"{label}: MCU feature count {len(mcu)} != expected {len(expected)}")
    checks = {}
    for field in ("start_index", "end_index", "start_sensor_timestamp_us", "end_sensor_timestamp_us"):
        if not np.array_equal(mcu[field].to_numpy(), expected[field].to_numpy()):
            raise ValueError(f"{label}: {field} does not match raw CSV")
    for field in ("baseline_normalized", *FIELDS):
        checks[field] = float(np.max(np.abs(mcu[field].to_numpy(dtype=float) - expected[field].to_numpy(dtype=float))))

    t = (raw.sensor_timestamp_us.to_numpy(dtype=np.float64) - raw.sensor_timestamp_us.iloc[0]) / 1e6
    dt = np.diff(t)
    features = mcu.copy()
    features.insert(0, "trial", label)
    features["time_s"] = (features.end_sensor_timestamp_us - raw.sensor_timestamp_us.iloc[0]) / 1e6
    peak_index = features.raw_peak.idxmax()
    peak = features.loc[peak_index]
    summary = {
        "trial": label,
        "raw_rows": len(raw),
        "feature_rows": len(features),
        "duration_s": float(t[-1]),
        "mean_read_rate_hz": float((len(raw) - 1) / (t[-1] - t[0])),
        "median_interval_ms": float(np.median(dt) * 1000),
        "baseline_normalized": float(features.baseline_normalized.iloc[0]),
        "raw_peak_max": float(features.raw_peak.max()),
        "lp035_peak_max": float(features.lp035_peak.max()),
        "lp015_peak_max": float(features.lp015_peak.max()),
        "raw_rms_max": float(features.raw_rms.max()),
        "lp035_rms_max": float(features.lp035_rms.max()),
        "lp015_rms_max": float(features.lp015_rms.max()),
        "raw_variance_max": float(features.raw_variance.max()),
        "lp035_variance_max": float(features.lp035_variance.max()),
        "lp015_variance_max": float(features.lp015_variance.max()),
        "gyro_peak_raw_max": float(features.gyro_peak_raw.max()),
        "peak_window_time_s": float(peak.time_s),
        "median_compute_us": float(features.compute_us.median()),
        "max_mcu_vs_pc_feature_error": max(checks.values()),
        "max_abs_az_change_counts": float(np.max(np.abs(np.diff(raw.az.to_numpy(dtype=float))))),
    }
    return summary, features, raw, checks


def main():
    parser = argparse.ArgumentParser(description="Verify MCU feature windows and compare four IMU trials")
    for trial in ("still", "tap", "tilt", "lift"):
        parser.add_argument(f"--{trial}-raw", required=True)
        parser.add_argument(f"--{trial}-features", required=True)
    parser.add_argument("--out", default="imu_comparison")
    args = parser.parse_args()
    output = Path(args.out)
    output.mkdir(parents=True, exist_ok=True)
    summary_rows, all_features, all_checks = [], [], []
    for label in ("still", "tap", "tilt", "lift"):
        summary, features, raw, checks = inspect_trial(
            label, getattr(args, f"{label}_raw"), getattr(args, f"{label}_features"))
        summary_rows.append(summary)
        all_features.append(features)
        all_checks.extend({"trial": label, "feature": field, "max_abs_error": error} for field, error in checks.items())

        t0 = raw.sensor_timestamp_us.iloc[0]
        t = (raw.sensor_timestamp_us.to_numpy(dtype=np.float64) - t0) / 1e6
        baseline = features.baseline_normalized.iloc[0]
        fig, ax = plt.subplots(figsize=(10, 4))
        ax.plot(t, raw.az.to_numpy(dtype=float) / SCALE - baseline, lw=1, label="Baseline-corrected raw az")
        ax.axvline((raw.sensor_timestamp_us.iloc[STARTUP + CAL_N] - t0) / 1e6, ls="--", color="grey", label="Feature analysis begins")
        ax.set(title=f"{label}: normalized vertical acceleration", xlabel="Seconds since first read", ylabel="Normalized counts")
        ax.legend()
        fig.tight_layout()
        fig.savefig(output / f"{label}_signal.png", dpi=170)
        plt.close(fig)

        fig, ax = plt.subplots(figsize=(10, 4))
        for field, name in (("raw_peak", "Raw peak"), ("lp035_peak", "LP 0.35 peak"), ("lp015_peak", "LP 0.15 peak")):
            ax.plot(features.time_s, features[field], label=name)
        ax.set(title=f"{label}: MCU peak feature comparison", xlabel="Seconds since first read", ylabel="Peak normalized counts")
        ax.legend()
        fig.tight_layout()
        fig.savefig(output / f"{label}_peaks.png", dpi=170)
        plt.close(fig)

    pd.DataFrame(summary_rows).to_csv(output / "trial_summary.csv", index=False)
    pd.concat(all_features, ignore_index=True).to_csv(output / "all_feature_windows.csv", index=False)
    pd.DataFrame(all_checks).to_csv(output / "mcu_pc_validation.csv", index=False)
    print(pd.DataFrame(summary_rows).to_string(index=False))
    print(f"Results written to {output}")


if __name__ == "__main__":
    main()
