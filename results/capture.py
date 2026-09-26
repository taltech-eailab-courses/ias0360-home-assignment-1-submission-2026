"""Capture complete Pico CSV windows using only Python standard libraries (macOS)."""
import csv, os, select, sys, termios, time
from pathlib import Path
case = sys.argv[1]
assert case in ('stationary', 'slow_movement', 'fast_movement')
folder = Path(__file__).resolve().parent
stamp = time.strftime('%Y%m%d_%H%M%S')
output = folder / (case + '_500hz_' + stamp + '.csv')
raw = folder / (case + '_500hz_' + stamp + '.log')
header = 'window,axis,fs_hz,min_dt_us,max_dt_us,mean_g,variance_g2,std_g,min_g,max_g,peak_hz,peak_g'.split(',')
fd = os.open('/dev/cu.usbmodem1401', os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
old = termios.tcgetattr(fd)
try:
    attrs = termios.tcgetattr(fd)
    attrs[0] = 0
    attrs[1] = 0
    attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    attrs[3] = 0
    attrs[4] = attrs[5] = termios.B115200
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIFLUSH)
    buffer = b''
    windows = {}
    saved = 0
    first_window = None
    deadline = time.monotonic() + 45
    with raw.open('wb') as log, output.open('w', newline='') as out:
        writer = csv.writer(out)
        writer.writerow(header)
        print('Recording ' + case + ': collecting 8 complete windows (~19 seconds at 500 Hz).', flush=True)
        while saved < 8 and time.monotonic() < deadline:
            ready, _, _ = select.select([fd], [], [], 1)
            if not ready: continue
            data = os.read(fd, 8192)
            if not data: continue
            log.write(data)
            buffer += data
            while b'\n' in buffer:
                line, buffer = buffer.split(b'\n', 1)
                row = line.decode('ascii', errors='replace').strip().split(',')
                if len(row) != len(header) or row[1] not in ('x','y','z'): continue
                try:
                    window = int(row[0])
                    values = [float(v) for v in row[2:]]
                except ValueError: continue
                if abs(values[0] - 500.0) > 1.0:
                    raise SystemExit('Expected 500 Hz firmware; received %.3f Hz. Flash the new UF2 first.' % values[0])
                if first_window is None: first_window = window
                if window == first_window: continue  # omit pre-recording/transitional window
                windows.setdefault(window, {})[row[1]] = row
                if len(windows[window]) == 3:
                    for axis in 'xyz': writer.writerow(windows[window][axis])
                    out.flush()
                    saved += 1
                    del windows[window]
                    print('Saved window %d (%d/8)' % (window, saved), flush=True)
                    if saved == 8: break
    print('CSV: ' + str(output), flush=True)
    print('Raw log: ' + str(raw), flush=True)
    if saved < 8: raise SystemExit('Incomplete recording: %d/8 windows' % saved)
finally:
    termios.tcsetattr(fd, termios.TCSANOW, old)
    os.close(fd)
