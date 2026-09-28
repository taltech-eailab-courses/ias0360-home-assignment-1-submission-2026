#!/usr/bin/env python3
# ha1, the pc side.
#   python3 host.py             record three runs from the pico (still, shake, rock), then analyse
#   python3 host.py --analyse   only analyse the logs already saved in output/
# recording needs pyserial, the analysis needs numpy.
#
# the analysis redoes the pico's fft with numpy as a check, writes the two files the course fft.py
# reads, and writes output/overall_data.txt with every number the report uses.

import csv
import glob
import os
import sys
import time

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "output")
RUNS = {"still": "leave the board STILL on the table",
        "shake": "pick the board up and SHAKE it hard until STOP",
        "rock": "ROCK the board gently side to side until STOP"}
AXES = ["ax", "ay", "az", "gx", "gy", "gz"]


def record():
    import serial
    ports = glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*") + glob.glob("/dev/pico")
    if not ports:
        sys.exit("no pico serial port found")
    os.makedirs(OUT, exist_ok=True)
    print("put the board flat on the table and do not touch it")
    port = serial.Serial(ports[0], 115200, timeout=1)
    time.sleep(1.5)                 # after a restart the pico now measures the gyro bias
    port.reset_input_buffer()
    for run, what in RUNS.items():
        print(f"\n{run}: {what}")
        for k in [5, 4, 3, 2, 1]:
            print(f"  {k}", end="", flush=True)
            time.sleep(1)
        print("  GO", flush=True)
        port.write(b"s")
        time.sleep(1.2)             # the recording takes 1.024 s
        print("  STOP, the pico is processing, about 5 s", flush=True)
        lines, deadline = [], time.time() + 90
        while not lines or lines[-1] != "DONE":
            if time.time() > deadline:
                sys.exit("no DONE from the pico within 90 s, record again")
            line = port.readline().decode("ascii", errors="replace").strip()
            if line:
                lines.append(line)
        with open(os.path.join(OUT, f"{run}_run_log.txt"), "w") as f:
            f.write("\n".join(lines) + "\n")
    port.close()


def read_log(run):
    # the log is "#begin name", a csv header and rows (info: key=value lines), "#end name"
    sections, name = {}, None
    for line in open(os.path.join(OUT, f"{run}_run_log.txt")):
        line = line.strip()
        if line.startswith("#begin "):
            name = line[7:]
            sections[name] = []
        elif line.startswith("#end "):
            name = None
        elif name:
            sections[name].append(line)
    info = dict(line.split("=") for line in sections.pop("info"))
    tables = {name: list(csv.DictReader(lines)) for name, lines in sections.items()}
    if len(tables["raw"]) != int(info["n"]):
        sys.exit(f"{run}: lines got lost over usb, record again")
    return info, tables


def col(table, name):
    return np.array([float(row[name]) for row in table])


def top5(amp):
    # the 5 biggest bins, bin 0 (the average) skipped, like the course fft.py
    return sorted(int(b) for b in np.argsort(amp[1:])[::-1][:5] + 1)


def fft_check(t):
    # the pico's fft input through numpy, same hamming window and scaling as the pico
    n = len(t["fft_input"])
    result = {}
    for axis in ["ax", "ay", "az"]:
        pico = col(t["fft"], axis)
        pc = np.abs(np.fft.rfft(col(t["fft_input"], axis) * np.hamming(n))) * (2 / n) / 0.54
        pico_top = [int(row["bin"]) for row in t["fft_peaks"] if row["axis"] == axis]
        slow = col(t["downsampled"], axis + "_g")      # the 250 per second copy
        slow_pc = np.abs(np.fft.rfft((slow - slow.mean()) * np.hamming(len(slow))))
        result[axis] = {"diff": np.max(np.abs(pico - pc)),
                        "same": sorted(pico_top) == top5(pc),
                        "slow_same": sorted(pico_top) == top5(slow_pc),
                        "peak_hz": float(t["fft"][pico_top[0]]["freq_hz"])}
    return result


def write_course_files(run, info, t):
    # the two files the course fft.py reads, for ax
    with open(os.path.join(OUT, f"{run}_raw_imu_mcu.txt"), "w") as f:
        f.write(",".join(row["ax"] for row in t["fft_input"]) + "\n")
        f.write(f"fs_hz={info['fs_hz']},n={info['n']},axis=ax\n")
    with open(os.path.join(OUT, f"{run}_out_fft_mcu.txt"), "w") as f:
        f.write("bin,freq,amp\n")
        for row in t["fft_peaks"]:
            if row["axis"] == "ax":
                f.write(f"{row['bin']},{row['freq_hz']},{row['amp']}\n")


def row(label, values, fmt="{}"):
    return f"  {label:<42}" + "".join(f"{fmt.format(v):>10}" for v in values)


def title(text, columns=list(RUNS)):
    return f"\n{text:<44}" + "".join(f"{c:>10}" for c in columns)


def analyse():
    logs = {run: read_log(run) for run in RUNS}
    info = {run: logs[run][0] for run in RUNS}
    t = {run: logs[run][1] for run in RUNS}
    check = {run: fft_check(t[run]) for run in RUNS}
    for run in RUNS:
        write_course_files(run, info[run], t[run])

    out = ["HA1 OVERALL NUMBERS, every number in report/ha1_report.tex comes from here"]

    out.append(title("SAMPLING"))
    out.append(row("samples per second", [info[r]["achieved_hz"] for r in RUNS]))
    out.append(row("late samples", [info[r]["late_samples"] for r in RUNS]))
    out.append(row("sensor read, average (microseconds)", [info[r]["read_us_avg"] for r in RUNS]))

    out.append(title("PICO FFT AGAINST NUMPY (ax, ay, az)"))
    out.append(row("largest difference (millionths of a g)",
                   [max(c["diff"] for c in check[r].values()) * 1e6 for r in RUNS], "{:.2f}"))
    out.append(row("same five strongest frequencies",
                   ["yes" if all(c["same"] for c in check[r].values()) else "no" for r in RUNS]))
    out.append(row("same five from the 250 per second data",
                   ["yes" if all(c["slow_same"] for c in check[r].values()) else "no" for r in RUNS]))
    for axis in ["ax", "ay", "az"]:
        out.append(row(f"strongest frequency {axis} (Hz)", [check[r][axis]["peak_hz"] for r in RUNS], "{:.2f}"))

    out.append(title("ZERO CROSSINGS, ROUGH FREQUENCY (Hz)"))
    for axis in AXES:
        out.append(row(axis, [next(z["rough_hz"] for z in t[r]["zcr"] if z["axis"] == axis) for r in RUNS]))

    out.append(title("TIME PER STEP (ms)", list(RUNS) + ["average"]))
    steps = {row_["step"]: [int(s["us"]) / 1000 for r in RUNS for s in t[r]["timing"] if s["step"] == row_["step"]]
             for row_ in t["still"]["timing"]}
    for step, ms in steps.items():
        out.append(row(step, ms + [sum(ms) / 3], "{:.1f}"))
    total = [sum(steps[s][k] for s in steps if s != "record") for k in range(3)]
    out.append(row("processing, without recording (s)", [x / 1000 for x in total], "{:.2f}"))

    out.append(title("FFT TIMING, AVERAGE OF THE RECORDINGS", ["time ms", "between", "buffer"]))
    out.append("  between: highest samples per second if the FFT must fit between two samples")
    out.append("  buffer: highest samples per second with a second buffer")
    for k, size in enumerate(row_["n"] for row_ in t["still"]["fft_timing"]):
        mean = {c: np.mean([float(t[r]["fft_timing"][k][c]) for r in RUNS])
                for c in ["fft_us", "fs_max_blocking_hz", "fs_max_buffered_hz"]}
        out.append(row(f"FFT size {size}", [mean["fft_us"] / 1000, mean["fs_max_blocking_hz"],
                                            mean["fs_max_buffered_hz"]], "{:.1f}"))

    out.append(title("QUANTIZATION (signal-to-noise ratio, dB)"))
    # above 120 db the values sat exactly on the stored levels, there is no rounding to compare
    out.append(row("largest measured - expected difference",
                   [max(abs(float(q["snr_db"]) - float(q["theory_snr_db"])) for q in t[r]["quantization"]
                        if float(q["snr_db"]) < 120) for r in RUNS], "{:.2f}"))
    for r in RUNS:
        for q in t[r]["quantization"]:
            if float(q["snr_db"]) >= 120:
                out.append(f"  almost no error: {r} {q['axis']} {q['bits']} bit {q['snr_db']} dB "
                           f"(expected {q['theory_snr_db']})")
    rock_ay = [q for q in t["rock"]["quantization"] if q["axis"] == "ay"]
    out.append(title("  rock ay", ["16 bit", "8 bit", "4 bit"]))
    out.append(row("measured", [q["snr_db"] for q in rock_ay]))
    out.append(row("expected for this signal", [q["theory_snr_db"] for q in rock_ay]))
    out.append("  memory per axis: 32-bit 4096 B, 16-bit 2048 B, 8-bit 1024 B, 4-bit 1024 B (512 B two per byte)")

    out.append(title("SENSOR STEPS EACH AXIS SPANS (raw counts)"))
    for axis, unit, scale in [(a, "_g", 16384) for a in AXES[:3]] + [(a, "_dps", 32.8) for a in AXES[3:]]:
        out.append(row(axis, [int(np.ptp(np.round(col(t[r]["raw"], axis + unit) * scale))) for r in RUNS]))

    out.append(title("STATISTICS"))
    out.append("  size: 7 numbers x 6 axes x 4 bytes = 168 B, against 1024 x 6 x 2 = 12288 B of raw values (73 times smaller)")
    stats = {r: {s["axis"]: s for s in t[r]["stats"] if s["data"] == "raw"} for r in RUNS}
    out.append(row("az average (g)", [float(stats[r]["az"]["mean"]) for r in RUNS], "{:.3f}"))
    out.append(row("az spread (thousandths of a g)", [float(stats[r]["az"]["std"]) * 1000 for r in RUNS], "{:.1f}"))
    out.append(row("az smallest (g)", [float(stats[r]["az"]["min"]) for r in RUNS], "{:.3f}"))
    out.append(row("az largest (g)", [float(stats[r]["az"]["max"]) for r in RUNS], "{:.3f}"))
    for axis in AXES:
        out.append(row(f"most common value of {axis}, times", [stats[r][axis]["mode_count"] for r in RUNS]))

    out.append(title("TILT (degrees, largest minus smallest)"))
    for name, label in [("roll_acc", "roll from gravity"), ("roll_cf", "roll, complementary filter"),
                        ("pitch_acc", "pitch from gravity"), ("pitch_cf", "pitch, complementary filter")]:
        out.append(row(label, [np.ptp(col(t[r]["orientation"], name)) for r in RUNS], "{:.1f}"))
    for axis in "xyz":
        # how far the gyroscope says the board turned: rate minus the start-up offset, added up
        turned = [np.ptp(np.cumsum(np.r_[0, col(t[r]["raw"], f"g{axis}_dps") - float(info[r][f"gyro_bias_{axis}_dps"])])
                         / float(info[r]["fs_hz"])) for r in RUNS]
        out.append(row(f"gyroscope turning g{axis}, offset removed", turned, "{:.2f}"))

    with open(os.path.join(OUT, "overall_data.txt"), "w") as f:
        f.write("\n".join(out) + "\n")
    print(f"wrote {OUT}/overall_data.txt and the fft.py files")


if __name__ == "__main__":
    if "--analyse" not in sys.argv:
        record()
    try:
        import numpy as np
    except ImportError:
        sys.exit("no numpy here: run python3 host.py --analyse in a python that has it")
    analyse()
