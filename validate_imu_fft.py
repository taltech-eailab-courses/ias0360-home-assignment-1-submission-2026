import argparse
import numpy as np
import pandas as pd


def main():
    parser = argparse.ArgumentParser(description="Validate exported 64-point MCU FFT against NumPy")
    parser.add_argument("--raw", default="road_raw_imu.csv")
    parser.add_argument("--fft", default="road_fft_mcu.csv")
    args = parser.parse_args()
    raw = pd.read_csv(args.raw)
    fft = pd.read_csv(args.fft)
    rows = []
    for block_no, block in fft.groupby("block", sort=True):
        block = block.sort_values("bin")
        if len(block) != 33 or not np.array_equal(block.bin.to_numpy(), np.arange(33)):
            raise ValueError(f"Block {block_no}: expected bins 0..32")
        first = int(block.start_index.iloc[0]); last = int(block.end_index.iloc[0])
        if last - first + 1 != 64:
            raise ValueError(f"Block {block_no}: expected exactly 64 raw samples")
        values = raw.iloc[first:last + 1]
        ts = values.sensor_timestamp_us.to_numpy(dtype=np.int64)
        fs = 1e6 * 63 / (ts[-1] - ts[0])
        x = values.az.to_numpy(dtype=np.float64) / 32768.0
        w = np.hamming(64)
        y = np.fft.rfft((x - x.mean()) * w)
        mag = np.abs(y) / w.sum()
        mag[1:-1] *= 2.0
        freq = np.fft.rfftfreq(64, d=1 / fs)
        mcu_mag = block.magnitude.to_numpy(dtype=np.float64)
        mcu_freq = block.freq_hz.to_numpy(dtype=np.float64)
        rows.append({
            "block": int(block_no),
            "first_index": first,
            "last_index": last,
            "fs_mcu": float(block.fs_hz.iloc[0]),
            "fs_pc": fs,
            "max_frequency_error_hz": float(np.max(np.abs(mcu_freq - freq))),
            "max_magnitude_error": float(np.max(np.abs(mcu_mag - mag))),
            "rms_magnitude_error": float(np.sqrt(np.mean((mcu_mag - mag) ** 2))),
            "peak_hz_mcu": float(block.peak_hz.iloc[0]),
            "peak_hz_pc": float(freq[np.argmax(mag[1:]) + 1]),
            "fft_us": int(block.fft_us.iloc[0]),
        })
    out = pd.DataFrame(rows)
    out.to_csv("fft_mcu_pc_validation.csv", index=False)
    print("blocks", len(out), "first indices", (out.first_index.iloc[0], out.last_index.iloc[0]), "last indices", (out.first_index.iloc[-1], out.last_index.iloc[-1]))
    print("max frequency error Hz", out.max_frequency_error_hz.max())
    print("max magnitude error", out.max_magnitude_error.max())
    print("rms magnitude error max block", out.rms_magnitude_error.max())
    print("median FFT us", out.fft_us.median())
    print("wrote fft_mcu_pc_validation.csv")


if __name__ == "__main__":
    main()
