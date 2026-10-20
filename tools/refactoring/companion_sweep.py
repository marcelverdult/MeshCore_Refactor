#!/usr/bin/env python3
"""Replays the host frame sweep against a USB companion and compares the replies.

  companion_sweep.py <usb-serial> [golden]

The host suite test/test_companion_frames records, for every command code and
every frame length, what the companion answers to a frame of zeros
(test/test_companion_frames/frame_sweep.golden, the lines of the "00" pass). This
tool sends the same frames to a real board over the USB companion interface and
checks that the board answers the same way, so the host harness is shown to model
the firmware, and a later restructuring can be checked on the board with the same
reference.

Only frames that cannot change the board are sent:

- every frame the golden file says is refused (an error reply and no side effect),
  which covers every length below every command's minimum;
- every length of the commands in READ_ONLY, which only report;
- lengths 1, 2, 50 and 176 of every code that is not a command.

Commands that store, send on air, restart, format or keep session state are never
sent at a length the host accepts. Replies are compared byte for byte where they
are node independent (errors and OK); for the report commands only the reply code
is compared, since their content is the board's own (its key, its clock, its
contacts). Private key export is left out on purpose.
"""
import os
import re
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nodetool  # noqa: E402

GOLDEN = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                      "../../test/test_companion_frames/frame_sweep.golden")

# Commands that report and change nothing, at any length, with zeros as arguments.
READ_ONLY = {
    1,   # APP_START
    5,   # GET_DEVICE_TIME
    6,   # SET_DEVICE_TIME: zero is in the past, so it is refused
    11,  # SET_RADIO_PARAMS: zeros are out of range, refused
    16,  # SHARE_CONTACT: the zero key finds a reserved slot, which has no advert
    17,  # EXPORT_CONTACT: self advert, returned and not sent
    18,  # IMPORT_CONTACT: zeros are no advert, refused
    20,  # GET_BATT_AND_STORAGE
    22,  # DEVICE_QUERY
    28,  # HAS_CONNECTION
    30,  # GET_CONTACT_BY_KEY
    31,  # GET_CHANNEL
    35,  # SIGN_FINISH: nothing started, refused
    40,  # GET_CUSTOM_VARS
    41,  # SET_CUSTOM_VAR: no separator, refused
    42,  # GET_ADVERT_PATH
    43,  # GET_TUNING_PARAMS
    56,  # GET_STATS
    59,  # GET_AUTOADD_CONFIG
    60,  # GET_ALLOWED_REPEAT_FREQ
    62,  # SEND_CHANNEL_DATA: data type 0 is reserved, refused
    64,  # GET_DEFAULT_FLOOD_SCOPE
    66,  # RUN_CLI_COMMAND: empty command, "Unknown command"
}

# Never sent above their minimum, whatever the golden file says: they store, send,
# restart, format, hold session state, or (23) hand out the private key. 13 and 15
# are here because a key of zeros is not "no contact": the eight reserved slots at
# the start of the contact table are all zeros, lookupContactByPubKey() finds them,
# and REMOVE_CONTACT then removes one (RESET_PATH changes one).
NEVER = {2, 3, 4, 7, 8, 9, 10, 12, 13, 14, 15, 19, 21, 23, 24, 25, 26, 27, 29, 32, 33,
         34, 36, 37, 38, 39, 50, 51, 52, 54, 55, 57, 58, 61, 63, 65}

COMMAND_CODES = set(range(1, 44)) | set(range(50, 53)) | set(range(54, 67))


def load_golden(path):
    """{(code, length): reply hex of the first frame, or None for no reply}, pass "00"."""
    out = {}
    for line in open(path):
        m = re.match(r"([0-9a-f]{2}) 00 (\d+)(?:-(\d+))?: (.*)$", line.strip())
        if not m:
            continue
        code, lo = int(m.group(1), 16), int(m.group(2))
        hi = int(m.group(3) or lo)
        rest = m.group(4)
        flags = re.search(r"\[(\w+)\]$", rest)
        replies = re.sub(r"\s*\[\w+\]$", "", rest).split()
        first = None if replies == ["(none)"] else replies[0]
        for n in range(lo, hi + 1):
            out[(code, n)] = (first, flags.group(1) if flags else "")
    return out


def selected(golden):
    for (code, n), (first, flags) in sorted(golden.items()):
        refused = first is not None and first.startswith("01") and not flags
        if code not in COMMAND_CODES:
            if n in (1, 2, 50, 176):
                yield code, n, first, True
        elif refused:
            yield code, n, first, True
        elif code in READ_ONLY and code not in NEVER and not flags:
            exact = first == "00"
            yield code, n, first, exact


def read_frame(fd, timeout=2.0, want_push=False):
    """The next radio->app frame ('>' len16 payload); pushes (0x80 and up) are skipped
    unless one is expected."""
    buf, end = b"", time.time() + timeout
    while time.time() < end:
        buf += nodetool.read_for(fd, 0.02)
        while True:
            i = buf.find(b">")
            if i < 0 or len(buf) < i + 3:
                break
            n = struct.unpack("<H", buf[i + 1:i + 3])[0]
            if len(buf) < i + 3 + n:
                break
            frame, buf = buf[i + 3:i + 3 + n], buf[i + 3 + n:]
            if frame and (frame[0] < 0x80 or want_push):
                return frame
    return None


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    golden = load_golden(sys.argv[2] if len(sys.argv) > 2 else GOLDEN)
    frames = list(selected(golden))
    fd = nodetool.comp_open(sys.argv[1])
    stats = {"sent": 0, "same": 0, "differ": 0, "no reply": 0}
    per_kind = {"exact": 0, "code": 0}
    t0 = time.time()
    for code, n, first, exact in frames:
        expect = bytes.fromhex(first)
        nodetool.read_for(fd, 0.005)
        payload = bytes([code]) + bytes(n - 1)
        os.write(fd, b"<" + struct.pack("<H", len(payload)) + payload)
        got = read_frame(fd, want_push=expect[0] >= 0x80)
        stats["sent"] += 1
        if got is None:
            stats["no reply"] += 1
            print(f"code {code:3d} len {n:3d}: no reply, expected {first}", flush=True)
            continue
        same = (got == expect) if exact else (got[:1] == expect[:1])
        per_kind["exact" if exact else "code"] += 1
        if same:
            stats["same"] += 1
        else:
            stats["differ"] += 1
            print(f"code {code:3d} len {n:3d}: got {got[:24].hex()} expected {first[:48]}", flush=True)
    os.close(fd)
    print(f"{stats['sent']} frames in {time.time() - t0:.0f} s: {stats['same']} as on the host, "
          f"{stats['differ']} different, {stats['no reply']} without reply "
          f"(compared byte for byte: {per_kind['exact']}, by reply code: {per_kind['code']})")
    codes = sorted({c for c, _, _, _ in frames})
    print(f"codes covered: {len(codes)}")
    return 0 if stats["differ"] == 0 and stats["no reply"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
