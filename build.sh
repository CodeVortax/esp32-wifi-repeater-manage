#!/usr/bin/env bash
# ESP32 WiFi Repeater: build helper.
#
#   ./build.sh              compile only
#   ./build.sh merge        build firmware + web UI -> dist/esp32dev-merged.bin  (flash at 0x0)
#   ./build.sh bin          same as merge, then copy the .bin to your phone's Downloads (Termux)
#   ./build.sh upload       compile + flash firmware over USB
#   ./build.sh uploadfs     upload the web UI (data/) to the board's SPIFFS
#   ./build.sh monitor      serial monitor at 115200
#   ./build.sh clean        remove build output

set -euo pipefail
cd "$(dirname "$0")"

if command -v pio >/dev/null 2>&1; then
    PIO=(pio)
elif python3 -m platformio --version >/dev/null 2>&1; then
    PIO=(python3 -m platformio)
else
    echo "PlatformIO is not installed. Install it first:" >&2
    echo "  pip install -U platformio" >&2
    exit 1
fi

OUT="dist/esp32dev-merged.bin"

case "${1:-build}" in
  build)    "${PIO[@]}" run ;;
  merge)    "${PIO[@]}" run -t webflash ;;
  bin)
    "${PIO[@]}" run -t webflash
    # Termux: put the file where the phone's file manager / flasher app can see it.
    for dir in "$HOME/storage/downloads" "/sdcard/Download" "/storage/emulated/0/Download"; do
        if [ -d "$dir" ]; then
            cp "$OUT" "$dir/esp32-wifi-repeater.bin"
            echo "Copied to $dir/esp32-wifi-repeater.bin"
            exit 0
        fi
    done
    echo "No Downloads folder found. In Termux run 'termux-setup-storage' once, then retry." >&2
    echo "The file is still at: $OUT" >&2
    ;;
  upload)   "${PIO[@]}" run -t upload ;;
  uploadfs) "${PIO[@]}" run -t uploadfs ;;
  monitor)  "${PIO[@]}" device monitor -b 115200 ;;
  clean)    "${PIO[@]}" run -t clean; rm -rf dist ;;
  *)
    sed -n '2,11p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
    ;;
esac
