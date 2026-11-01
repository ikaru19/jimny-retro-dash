#!/bin/bash
# Compile sketch.ino for the ESP32 on the Mac (no Wokwi online queue).
# The output is used by the Wokwi simulator in VS Code (see wokwi.toml) and can be uploaded to a real ESP32.
#   ./build-esp32.sh            compile only
#   ./build-esp32.sh upload     compile, then upload to the ESP32 connected over USB
set -eu
ROOT="$(cd "$(dirname "$0")" && pwd)"
SRC="$ROOT/build/esp32-src/sketch"  # arduino-cli wants folder name = .ino file name
OUT="$ROOT/build/esp32"
FQBN="esp32:esp32:esp32:PartitionScheme=huge_app"  # 3 MB for the program (Bluetooth is big), no OTA

mkdir -p "$SRC" "$OUT"
ln -sf "$ROOT/sketch.ino" "$SRC/sketch.ino"  # symlink, so errors point at the real file
arduino-cli compile --fqbn "$FQBN" --build-path "$ROOT/build/esp32-cache" --output-dir "$OUT" "$SRC"

if [ "${1:-}" = "upload" ]; then
  PORT="$(arduino-cli board list | awk '/usbserial|SLAB|wchusbserial|usbmodem/ {print $1; exit}')"
  [ -z "$PORT" ] && { echo "No ESP32 found on USB."; exit 1; }
  arduino-cli upload --fqbn "$FQBN" --port "$PORT" --input-dir "$OUT"
fi
