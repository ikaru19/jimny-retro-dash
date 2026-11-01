#!/usr/bin/env python3
"""Control the dash on the real ESP32 from the Mac keyboard, over the USB cable.

Handy while the knob is not wired yet or its switch does not work.
  Left / Right arrow (or A / D)   turn the knob
  Space / Enter                   click
  H                               hold
  Q / Ctrl+C                      quit

Run:  ./remote.py      (only standard Python, no extra packages)
"""
import glob
import os
import select
import sys
import termios
import tty

KEYS = {
    b"\x1b[D": b"L", b"a": b"L", b"A": b"L",
    b"\x1b[C": b"R", b"d": b"R", b"D": b"R",
    b" ": b"C", b"\r": b"C", b"\n": b"C",
    b"h": b"H", b"H": b"H",
}
NAMES = {b"L": "turn left", b"R": "turn right", b"C": "click", b"H": "hold"}


def open_port():
    ports = sorted(glob.glob("/dev/cu.usbserial*") + glob.glob("/dev/cu.SLAB*") + glob.glob("/dev/cu.wchusbserial*"))
    if not ports:
        sys.exit("No ESP32 found on USB. Check the cable (it must be a data cable).")
    try:
        fd = os.open(ports[0], os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    except OSError as e:
        sys.exit(f"Cannot open {ports[0]}: {e}. Close the Serial Monitor first (Ctrl+C in that window).")
    tty.setraw(fd)
    attrs = termios.tcgetattr(fd)
    attrs[4] = attrs[5] = termios.B115200  # input and output speed
    attrs[2] &= ~termios.HUPCL              # do not reset the ESP32 when we close
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd, ports[0]


def main():
    fd, name = open_port()
    print(f"Connected to {name}.")
    print("Left/Right = turn, Space/Enter = click, H = hold, Q = quit\n")
    old = termios.tcgetattr(sys.stdin)
    tty.setcbreak(sys.stdin.fileno())
    try:
        while True:
            ready, _, _ = select.select([sys.stdin, fd], [], [])
            if fd in ready:  # show whatever the ESP32 prints
                try:
                    data = os.read(fd, 256)
                except BlockingIOError:
                    data = b""
                if data:
                    sys.stdout.write(data.decode("utf-8", "replace"))
                    sys.stdout.flush()
            if sys.stdin in ready:
                key = os.read(sys.stdin.fileno(), 3)
                if key in (b"q", b"Q", b"\x03"):
                    break
                cmd = KEYS.get(key)
                if cmd:
                    os.write(fd, cmd)
                    print(f"> {NAMES[cmd]}")
    finally:
        termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old)
        os.close(fd)


if __name__ == "__main__":
    main()
