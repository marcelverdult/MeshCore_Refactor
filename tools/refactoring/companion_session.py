#!/usr/bin/env python3
"""Runs a fixed sequence of accepted companion commands and prints every reply.

  companion_session.py <usb-serial>

companion_sweep.py only sends frames that a command refuses or that only report.
This sends frames the commands accept, so that two firmware images can be compared
on the same board: run it once per image and diff the output. Every setter writes
back the value the board already holds (read first through the protocol), so the
board's state is the same afterwards. Nothing is sent on air, nothing restarts.

What a reply carries that legitimately differs between two runs (the clock, the
uptime, the battery voltage) is masked in the output.
"""
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nodetool  # noqa: E402
from companion_sweep import read_frame  # noqa: E402


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    fd = nodetool.comp_open(sys.argv[1])

    def tx(payload, note, mask=None, want_push=False):
        nodetool.read_for(fd, 0.02)
        os.write(fd, b"<" + struct.pack("<H", len(payload)) + payload)
        r = read_frame(fd, want_push=want_push)
        shown = r.hex() if r is not None else "(no reply)"
        if r is not None and mask:
            b = bytearray(r)
            for a, n in mask:
                b[a:a + n] = b"\xee" * n
            shown = b.hex()
        print(f"{note:34s} {payload[:1].hex()} len {len(payload):3d} -> {shown}", flush=True)
        return r

    # read the current values the setters will write back
    info = tx(bytes([1, 3, 0, 0, 0, 0, 0, 0]) + b"session", "APP_START")
    dev = tx(bytes([22, 3]), "DEVICE_QUERY")
    tun = tx(bytes([43]), "GET_TUNING_PARAMS")
    auto = tx(bytes([59]), "GET_AUTOADD_CONFIG")
    scope = tx(bytes([64]), "GET_DEFAULT_FLOOD_SCOPE")
    if not (info and dev and tun and auto and scope):
        print("could not read the current values, stopping")
        return 1

    tx_power, lat, lon = info[2], info[36:40], info[40:44]
    multi_acks, loc_policy, telemetry, manual_add = info[44], info[45], info[46], info[47]
    freq, bw, sf, cr, name = info[48:52], info[52:56], info[56], info[57], info[58:]
    pin, repeat, hash_mode = dev[4:8], dev[80], dev[81]

    tx(bytes([12, tx_power]), "SET_RADIO_TX_POWER (same)")
    tx(bytes([11]) + freq + bw + bytes([sf, cr, repeat]), "SET_RADIO_PARAMS (same)")
    tx(bytes([21]) + tun[1:9], "SET_TUNING_PARAMS (same)")
    tx(bytes([38, manual_add, telemetry, loc_policy, multi_acks]), "SET_OTHER_PARAMS (same)")
    tx(bytes([58]) + auto[1:3], "SET_AUTOADD_CONFIG (same)")
    tx(bytes([61, 0, hash_mode]), "SET_PATH_HASH_MODE (same)")
    tx(bytes([8]) + name, "SET_ADVERT_NAME (same)")
    tx(bytes([14]) + lat + lon, "SET_ADVERT_LATLON (same)")
    tx(bytes([37]) + pin, "SET_DEVICE_PIN (same)")
    if len(scope) == 48:
        tx(bytes([63]) + scope[1:48], "SET_DEFAULT_FLOOD_SCOPE (same)")
    tx(bytes([54, 0]), "SET_FLOOD_SCOPE_KEY clear")
    tx(bytes([54, 2]), "SET_FLOOD_SCOPE_KEY sub 2")
    tx(bytes([61, 1, hash_mode]), "SET_PATH_HASH_MODE byte 1 not 0")
    tx(bytes([61, 0, 3]), "SET_PATH_HASH_MODE 3")
    tx(bytes([12, 0x7F]), "SET_RADIO_TX_POWER 127")
    tx(bytes([6]) + struct.pack("<I", 1), "SET_DEVICE_TIME in the past")
    tx(bytes([37]) + struct.pack("<I", 12345), "SET_DEVICE_PIN 5 digits")
    tx(bytes([56, 0]), "GET_STATS core", mask=[(2, 6)])
    tx(bytes([56, 1]), "GET_STATS radio", mask=[(2, 12)])
    tx(bytes([56, 3]), "GET_STATS 3")
    tx(bytes([39, 0, 0, 0]), "SEND_TELEMETRY_REQ self", mask=[(8, 64)], want_push=True)
    tx(bytes([39, 0, 0, 0, 0]), "SEND_TELEMETRY_REQ len 5")
    tx(bytes([33]), "SIGN_START")
    tx(bytes([34]) + b"companion session", "SIGN_DATA")
    tx(bytes([35]), "SIGN_FINISH")
    tx(bytes([35]), "SIGN_FINISH again")
    tx(bytes([31, 0]), "GET_CHANNEL 0")
    tx(bytes([31, 255]), "GET_CHANNEL 255")
    tx(bytes([66]) + b"ver", "RUN_CLI_COMMAND ver")
    tx(bytes([66]) + b"get name", "RUN_CLI_COMMAND get name")
    tx(bytes([19]) + b"rebooX", "REBOOT wrong word")
    tx(bytes([51]) + b"resex", "FACTORY_RESET wrong word")
    tx(bytes([5]), "GET_DEVICE_TIME", mask=[(1, 4)])
    tx(bytes([20]), "GET_BATT_AND_STORAGE", mask=[(1, 2)])
    os.close(fd)
    return 0


if __name__ == "__main__":
    sys.exit(main())
