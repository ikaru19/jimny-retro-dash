#!/bin/bash
# Compile sketch.ino untuk ESP32 di Mac (tanpa antre Wokwi online).
# Hasilnya dipakai simulator Wokwi di VS Code (lihat wokwi.toml) dan bisa di-upload ke ESP32 asli.
#   ./build-esp32.sh            compile saja
#   ./build-esp32.sh upload     compile lalu upload ke ESP32 yang tersambung USB
set -eu
ROOT="$(cd "$(dirname "$0")" && pwd)"
SRC="$ROOT/build/esp32-src/sketch"  # arduino-cli mau nama folder = nama file .ino
OUT="$ROOT/build/esp32"
FQBN="esp32:esp32:esp32"

mkdir -p "$SRC" "$OUT"
ln -sf "$ROOT/sketch.ino" "$SRC/sketch.ino"  # link, jadi error menunjuk ke file asli
arduino-cli compile --fqbn "$FQBN" --build-path "$ROOT/build/esp32-cache" --output-dir "$OUT" "$SRC"

if [ "${1:-}" = "upload" ]; then
  PORT="$(arduino-cli board list | awk '/usbserial|SLAB|wchusbserial|usbmodem/ {print $1; exit}')"
  [ -z "$PORT" ] && { echo "ESP32 tidak ketemu di USB."; exit 1; }
  arduino-cli upload --fqbn "$FQBN" --port "$PORT" --input-dir "$OUT"
fi
