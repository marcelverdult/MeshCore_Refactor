#!/usr/bin/env python3
"""Talks to MeshCore nRF52 boards over USB, addressed by USB serial number (macOS, uses ioreg).

Used for the hardware upgrade/downgrade checks of the refactoring series, see README.md.

  nodetool.py port <usb-serial>                    serial port of that board
  nodetool.py cli <usb-serial> <cmd> [<cmd>...]    text console (repeater, room server, sensor)
  nodetool.py comp <usb-serial> <cmd> [<cmd>...]   companion: CMD_RUN_CLI_COMMAND (66) per command
  nodetool.py compinfo <usb-serial>                companion: APP_START self info + tuning params
  nodetool.py dfu <usb-serial> <firmware.zip>      1200-baud touch, then adafruit-nrfutil serial DFU
"""
import os
import plistlib
import select
import struct
import subprocess
import sys
import termios
import time
import tty

# adafruit-nrfutil as shipped with PlatformIO. NRFUTIL_PYTHON selects the interpreter
# (default: PlatformIO's own virtualenv); the tool's bundled packages are put on PYTHONPATH.
NRFUTIL_DIR = os.path.expanduser("~/.platformio/packages/tool-adafruit-nrfutil")
NRFUTIL = [os.environ.get("NRFUTIL_PYTHON", os.path.expanduser("~/.platformio/penv/bin/python")),
           NRFUTIL_DIR + "/adafruit-nrfutil.py"]
NRFUTIL_ENV = dict(os.environ, PYTHONPATH=NRFUTIL_DIR + "/site-packages")


def find_port(usb_serial, timeout=20.0):
    """Returns the first callout device below the USB device with this serial number."""
    end = time.time() + timeout
    while time.time() < end:
        r = subprocess.run(["ioreg", "-a", "-r", "-c", "IOUSBHostDevice", "-l"], capture_output=True)
        if r.returncode == 0:
            def walk(node, inside=False):
                inside = inside or node.get("USB Serial Number") == usb_serial
                if inside and "IOCalloutDevice" in node:
                    yield node["IOCalloutDevice"]
                for c in node.get("IORegistryEntryChildren", []):
                    yield from walk(c, inside)
            ports = sorted(p for root in plistlib.loads(r.stdout) for p in walk(root))
            if ports:
                return ports[0]
        time.sleep(0.5)
    return None


def open_raw(port, baud=termios.B115200):
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    tty.setraw(fd)
    attrs = termios.tcgetattr(fd)
    attrs[2] |= termios.CLOCAL | termios.CREAD
    attrs[4] = attrs[5] = baud
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd


def read_for(fd, seconds):
    data, end = b"", time.time() + seconds
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            try:
                chunk = os.read(fd, 4096)
            except BlockingIOError:
                continue
            if not chunk:
                break
            data += chunk
    return data


def text_cli(usb_serial, commands, timeout=6.0):
    """Server roles echo the command, then print the reply as a line '  -> ...'.
    Input is drained before each command and only a reply line that follows the
    echo of this command counts, so a late reply can never be taken for the next one.
    A command without reply is sent once more (every get/set used here is idempotent)."""
    fd = open_raw(find_port(usb_serial))
    read_for(fd, 0.5)
    out = []
    for cmd in commands:
        if cmd == "reboot":
            os.write(fd, b"reboot\r")
            out.append((cmd, "(sent)"))
            break
        reply = None
        for attempt in range(2):
            read_for(fd, 0.3)
            os.write(fd, (cmd + "\r").encode())
            buf, end = "", time.time() + timeout
            while time.time() < end and reply is None:
                buf += read_for(fd, 0.1).decode(errors="replace")
                echo = buf.find(cmd)
                if echo < 0:
                    continue
                for line in buf[echo + len(cmd):].splitlines():
                    if line.startswith("  -> "):
                        reply = line[5:]
                        break
            if reply is not None:
                break
        out.append((cmd, reply))
    try:
        os.close(fd)
    except OSError:
        pass
    return out


def comp_frame(fd, payload, want, timeout=4.0):
    """Sends one app->radio frame ('<' len16 payload) and waits for a reply frame of code `want`."""
    os.write(fd, b"<" + struct.pack("<H", len(payload)) + payload)
    buf, end = b"", time.time() + timeout
    while time.time() < end:
        buf += read_for(fd, 0.1)
        while True:
            i = buf.find(b">")
            if i < 0 or len(buf) < i + 3:
                break
            n = struct.unpack("<H", buf[i + 1:i + 3])[0]
            if len(buf) < i + 3 + n:
                break
            frame, buf = buf[i + 3:i + 3 + n], buf[i + 3 + n:]
            if frame and frame[0] == want:
                return frame
    return None


def comp_open(usb_serial):
    fd = open_raw(find_port(usb_serial))
    read_for(fd, 0.3)
    return fd


def comp_cli(usb_serial, commands):
    fd = comp_open(usb_serial)
    out = []
    for cmd in commands:
        if cmd == "reboot":   # CMD_REBOOT, no reply
            os.write(fd, b"<" + struct.pack("<H", 7) + bytes([19]) + b"reboot")
            out.append((cmd, "(sent)"))
            break
        f = comp_frame(fd, bytes([66]) + cmd.encode(), 29)
        out.append((cmd, f[1:].decode(errors="replace") if f else None))
    os.close(fd)
    return out


def comp_info(usb_serial):
    fd = comp_open(usb_serial)
    f = comp_frame(fd, bytes([1, 3]) + b"nodetool", 5)   # CMD_APP_START, app ver 3
    info = {}
    if f:
        # RESP_CODE_SELF_INFO: adv_type, tx_power, max_tx_power, pubkey[32], lat, lon, multi_acks,
        # advert_loc_policy, telemetry modes, manual_add, freq*1000, bw*1000, sf, cr, name
        info["tx_power"] = f[2]
        info["pubkey_prefix"] = f[4:10].hex()
        lat, lon = struct.unpack("<ii", f[36:44])
        info["lat"], info["lon"] = lat / 1e6, lon / 1e6
        info["multi_acks"], info["advert_loc_policy"], info["telemetry"], info["manual_add"] = f[44], f[45], f[46], f[47]
        freq, bw = struct.unpack("<II", f[48:56])
        info["freq"], info["bw"], info["sf"], info["cr"] = freq / 1000, bw / 1000, f[56], f[57]
        info["name"] = f[58:].decode(errors="replace")
    t = comp_frame(fd, bytes([43]), 23)                  # CMD_GET_TUNING_PARAMS
    if t:
        rx, af = struct.unpack("<II", t[1:9])
        info["rx_delay"], info["airtime_factor"] = rx / 1000, af / 1000
    os.close(fd)
    return info


def dfu(usb_serial, zip_path):
    port = find_port(usb_serial)
    try:   # 1200-baud touch: the app resets into the serial DFU bootloader
        fd = open_raw(port, termios.B1200)
        os.close(fd)
    except OSError:
        pass
    time.sleep(2.0)
    dfu_port = find_port(usb_serial, timeout=15)
    print("dfu port:", dfu_port, flush=True)
    r = subprocess.run(NRFUTIL + ["dfu", "serial", "-p", dfu_port, "-b", "115200", "--singlebank", "-pkg", zip_path],
                       capture_output=True, text=True, env=NRFUTIL_ENV)
    print(r.stdout[-600:], r.stderr[-600:])
    return r.returncode


def expand(args):
    """'@file' stands for the non-empty lines of that file."""
    out = []
    for a in args:
        if a.startswith("@"):
            out += [l.rstrip("\n") for l in open(a[1:]) if l.strip()]
        else:
            out.append(a)
    return out


def main():
    cmd, serial = sys.argv[1], sys.argv[2]
    sys.argv[3:] = expand(sys.argv[3:])
    if cmd == "port":
        print(find_port(serial))
    elif cmd == "cli":
        for c, r in text_cli(serial, sys.argv[3:]):
            print(f"{c} => {r}")
    elif cmd == "comp":
        for c, r in comp_cli(serial, sys.argv[3:]):
            print(f"{c} => {r}")
    elif cmd == "compinfo":
        for k, v in comp_info(serial).items():
            print(f"{k} = {v}")
    elif cmd == "dfu":
        sys.exit(dfu(serial, sys.argv[3]))


if __name__ == "__main__":
    main()
