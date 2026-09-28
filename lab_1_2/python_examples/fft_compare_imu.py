import argparse
from pathlib import Path
import matplotlib.pyplot as plt
import numpy as np


def section(lines,start,end):
    # skip the csv header immediately after the start marker
    # пропускаем заголовок csv сразу после начального маркера
    first=lines.index(start)+2
    last=lines.index(end)
    return [line.split(",") for line in lines[first:last] if line]


def csv_rows(lines,header):
    # sd files have metadata before the regular csv table
    first=lines.index(header)+1
    return [line.split(",") for line in lines[first:] if line and not line.startswith("META,")]


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("input",nargs="?",default="fft_capture.txt")
    parser.add_argument("fft_input",nargs="?")
    args=parser.parse_args()

    lines=[line.strip() for line in Path(args.input).read_text().splitlines()]
    meta={}
    for line in lines:
        if line.startswith("META,"):
            _,key,value=line.split(",",2)
            meta[key]=value

    # accept either one serial capture or the two files copied from the sd card
    # принимаем один serial-файл или два файла, скопированных с sd-карты
    if args.fft_input:
        fft_lines=[line.strip() for line in Path(args.fft_input).read_text().splitlines()]
        raw=np.array(csv_rows(lines,"index,time_s,value_g"),dtype=float)
        mcu=np.array(csv_rows(fft_lines,"bin,frequency_hz,amplitude_g"),dtype=float)
        for line in fft_lines:
            if line.startswith("META,"):
                _,key,value=line.split(",",2)
                meta.setdefault(key,value)
    else:
        raw=np.array(section(lines,"RAW_BEGIN","RAW_END"),dtype=float)
        mcu=np.array(section(lines,"FFT_BEGIN","FFT_END"),dtype=float)
    n=int(meta["n"])
    fs=float(meta["fs"])
    values=raw[:,2]

    # repeat the same centering, window and amplitude scaling used on the pico
    # повторяем то же удаление среднего, окно и масштабирование, что и на pico
    centered=values-values.mean()
    windowed=centered*np.hamming(n)
    pc_fft=np.fft.rfft(windowed,n=n)
    pc_amplitude=np.abs(pc_fft)*(2.0/n)/0.54
    pc_amplitude[0]*=0.5
    frequencies=np.fft.rfftfreq(n,1.0/fs)

    # compare every frequency bin instead of checking only the largest peaks
    # сравниваем каждый частотный bin, а не только самые большие пики
    difference=mcu[:,2]-pc_amplitude
    rmse=np.sqrt(np.mean(difference**2))
    max_error=np.max(np.abs(difference))
    print(f"N={n}, fs={fs:.6f} Hz, df={fs/n:.6f} Hz")
    print(f"FFT amplitude RMSE={rmse:.9f} g")
    print(f"FFT amplitude max error={max_error:.9f} g")

    indices=np.argsort(pc_amplitude[1:])[::-1][:5]+1
    print("Top PC peaks:")
    for index in indices:
        print(f"bin={index}, frequency={frequencies[index]:.6f} Hz, amplitude={pc_amplitude[index]:.6f} g")

    plt.figure(figsize=(10,4))
    plt.plot(raw[:,1],values)
    plt.xlabel("Time, s")
    plt.ylabel("Acceleration X, g")
    plt.title("ICM-20948 time-domain signal")
    plt.grid(True)
    plt.tight_layout()
    plt.savefig("imu_time.png",dpi=160)

    plt.figure(figsize=(10,4))
    plt.plot(mcu[:,1],mcu[:,2],label="Pico FFT")
    plt.plot(frequencies,pc_amplitude,"--",label="NumPy FFT")
    plt.xlabel("Frequency, Hz")
    plt.ylabel("Amplitude, g")
    plt.title("Pico and NumPy FFT comparison")
    plt.grid(True)
    plt.legend()
    plt.tight_layout()
    plt.savefig("imu_fft_comparison.png",dpi=160)
    print("Saved imu_time.png and imu_fft_comparison.png")


if __name__=="__main__":
    main()
