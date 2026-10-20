#!/usr/bin/env bash
# Usage:
#   ./build.sh            -> compile only
#   ./build.sh upload     -> compile + flash over USB
#   ./build.sh uploadfs   -> upload data/ (SPIFFS) to the board — do this once
#                            so index.html is actually on the ESP32
#   ./build.sh monitor    -> open serial monitor at 115200

set -e

if ! command -v pio &> /dev/null; then
    echo "PlatformIO CLI not found. Install it first:"
    echo "  pip install -U platformio"
    exit 1
fi

case "$1" in
  upload)
    pio run -t upload
    ;;
  uploadfs)
    pio run -t uploadfs
    ;;
  monitor)
    pio device monitor -b 115200
    ;;
  *)
    pio run
    ;;
esac
