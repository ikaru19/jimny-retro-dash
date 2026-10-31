#!/bin/bash
# Upload a small test program (tests/ folder) to the ESP32, then open the Serial Monitor.
#   ./test.sh            list the tests
#   ./test.sh 03         upload test 03-display (the number is enough)
#   ./test.sh monitor    open the Serial Monitor only (exit: Ctrl+C)
set -eu
ROOT="$(cd "$(dirname "$0")" && pwd)"
FQBN="esp32:esp32:esp32"

if [ -z "${1:-}" ]; then
  echo "Pick a test:"
  ls "$ROOT/tests"
  echo "Example: ./test.sh 01"
  exit 0
fi

PORT="$(arduino-cli board list | awk '/usbserial|SLAB|wchusbserial|usbmodem/ {print $1; exit}')"
[ -z "$PORT" ] && { echo "No ESP32 found on USB. Check the cable (it must be a data cable, not charge-only)."; exit 1; }

if [ "$1" != "monitor" ]; then
  DIR="$(ls -d "$ROOT"/tests/"$1"* 2>/dev/null | head -1)"
  [ -z "$DIR" ] && { echo "No test '$1'. Run ./test.sh for the list."; exit 1; }
  NAME="$(basename "$DIR")"
  echo "Uploading $NAME to $PORT ..."
  arduino-cli compile --fqbn "$FQBN" --build-path "$ROOT/build/tests/$NAME" --upload --port "$PORT" "$DIR"
fi

echo "Serial Monitor (exit: Ctrl+C)"
arduino-cli monitor --port "$PORT" --config baudrate=115200
