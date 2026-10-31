#!/bin/bash
# Upload sketch tes kecil (folder tes/) ke ESP32, lalu buka Serial Monitor.
#   ./tes.sh            daftar tes
#   ./tes.sh 03         upload tes 03-layar (cukup nomornya)
#   ./tes.sh monitor    buka Serial Monitor saja (keluar: Ctrl+C)
set -eu
ROOT="$(cd "$(dirname "$0")" && pwd)"
FQBN="esp32:esp32:esp32"

if [ -z "${1:-}" ]; then
  echo "Pilih tes:"
  ls "$ROOT/tes"
  echo "Contoh: ./tes.sh 01"
  exit 0
fi

PORT="$(arduino-cli board list | awk '/usbserial|SLAB|wchusbserial|usbmodem/ {print $1; exit}')"
[ -z "$PORT" ] && { echo "ESP32 tidak ketemu di USB. Cek kabel (harus kabel data, bukan kabel cas saja)."; exit 1; }

if [ "$1" != "monitor" ]; then
  DIR="$(ls -d "$ROOT"/tes/"$1"* 2>/dev/null | head -1)"
  [ -z "$DIR" ] && { echo "Tes '$1' tidak ada. Jalankan ./tes.sh untuk daftar."; exit 1; }
  NAME="$(basename "$DIR")"
  echo "Upload $NAME ke $PORT ..."
  arduino-cli compile --fqbn "$FQBN" --build-path "$ROOT/build/tes/$NAME" --upload --port "$PORT" "$DIR"
fi

echo "Serial Monitor (keluar: Ctrl+C)"
arduino-cli monitor --port "$PORT" --config baudrate=115200
