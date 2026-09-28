#!/usr/bin/env bash
# ha1 in one go: build the firmware, flash it, record and analyse.
#   ./run.sh            all steps
#   ./run.sh build      only build build/ha1_imu.uf2 (in the course docker image ias0360-2026)
#   ./run.sh flash      only flash it
#   ./run.sh capture    only record and analyse, the board already runs this firmware
set -eu
cd "$(dirname "$0")"

build() {
    docker run --rm --user "$(id -u):$(id -g)" --entrypoint bash -v "$PWD:$PWD" -w "$PWD" ias0360-2026 \
        -c "rm -rf build && mkdir build && cd build && cmake .. > /dev/null && make -j4 > /dev/null"
    echo "built build/ha1_imu.uf2"
}

flash() {
    if command -v picotool > /dev/null; then
        picotool load build/ha1_imu.uf2 -f -x       # -f reboots the board into bootsel, -x starts it
    else
        # hold BOOTSEL while plugging the board in, it then shows up as a drive
        if [ "$(uname -s)" = Darwin ]; then DRIVE=/Volumes/RPI-RP2; else DRIVE="/media/$USER/RPI-RP2"; fi
        cp build/ha1_imu.uf2 "$DRIVE/"
    fi
    sleep 3    # the pico restarts
}

capture() {
    python3 host.py
}

case "${1:-all}" in
    all)     build; flash; capture ;;
    build)   build ;;
    flash)   flash ;;
    capture) capture ;;
    *)       echo "usage: ./run.sh [all|build|flash|capture]" >&2; exit 1 ;;
esac
