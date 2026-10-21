#!/usr/bin/env python3
"""Lists every access to cmd_frame in the companion command handlers.

For each row of MyMesh::CMD_TABLE (examples/companion_radio/MyMesh.cpp) it prints
the row's minimum, then every line of the handler that touches cmd_frame, sorted
into four kinds:

  fixed   cmd_frame[N] or &cmd_frame[N] with a constant N (the hand counted offsets)
  cursor  cmd_frame[i] / &cmd_frame[i] with a running index
  write   an assignment into cmd_frame (the in place string terminators)
  len     a length derived from len that is handed on with a pointer into the frame

and, for the fixed offsets, the highest byte the handler reads without a further
length test, against the row's minimum. A constant offset at or above the minimum
is only safe behind a len test, which the script marks as "guarded" when the line
or an enclosing if names len.

Usage: tools/refactoring/cmd_frame_offsets.py [path/to/MyMesh.cpp]
"""

import re
import sys

PUB_KEY_SIZE = 32
MAX_PATH_SIZE = 64
CONSTS = {"PUB_KEY_SIZE": PUB_KEY_SIZE, "MAX_PATH_SIZE": MAX_PATH_SIZE}

# widths of the reads a line does at a fixed offset, by the call around it
WIDTH_HINTS = [
    (re.compile(r"memcpy\(\s*&\w+\s*,\s*&cmd_frame\[([^\]]+)\]\s*,\s*(\d+)\s*\)"), None),
    (re.compile(r"memcmp\(\s*&cmd_frame\[([^\]]+)\]\s*,\s*\"[^\"]*\"\s*,\s*(\d+)\s*\)"), None),
]


def ev(expr):
    e = expr
    for k, v in CONSTS.items():
        e = e.replace(k, str(v))
    if not re.fullmatch(r"[\d\s+\-*()]+", e):
        return None
    return eval(e)  # digits and + - * only


def handlers(src):
    table = re.search(r"const MyMesh::CmdHandler MyMesh::CMD_TABLE\[\] = \{(.*?)\n\};", src, re.S).group(1)
    rows = re.findall(r"\{\s*(CMD_\w+),\s*([^,]+?),\s*&MyMesh::(\w+)\s*\}", table)
    for code, minimum, fn in rows:
        m = re.search(r"\nbool MyMesh::" + fn + r"\(size_t[^)]*\) \{\n(.*?)\n\}\n", src, re.S)
        yield code, ev(minimum.strip()), fn, m.group(1).split("\n")


def classify(line):
    kinds = []
    if re.search(r"cmd_frame\[[^\]]+\]\s*=[^=]", line) or re.search(r"\*np\+\+\s*=", line):
        kinds.append("write")
    for m in re.finditer(r"(&?)cmd_frame\[([^\]]+)\]", line):
        off = ev(m.group(2))
        kinds.append(("fixed", off, m.group(1) == "&") if off is not None else ("cursor", m.group(2)))
    if re.search(r"len\s*-\s*[\w(]", line) and "cmd_frame" in line or re.search(r"\blen - \(", line):
        kinds.append("len")
    return kinds


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "examples/companion_radio/MyMesh.cpp"
    src = open(path).read()
    total = {"fixed": 0, "cursor": 0, "write": 0, "len": 0}
    nhandlers = 0
    for code, minimum, fn, body in handlers(src):
        nhandlers += 1
        print(f"{code} (min {minimum}) {fn}")
        guards = []
        top = 0
        for line in body:
            s = line.strip()
            if re.match(r"if \(.*\blen\b", s) or re.match(r"\} else if \(.*\blen\b", s):
                guards.append(line)
            kinds = classify(line)
            if not kinds and "cmd_frame" not in line and not re.search(r"\blen\b", line):
                continue
            guarded = bool(re.search(r"\blen\b", line)) or any(len(g) - len(g.lstrip()) < len(line) - len(line.lstrip()) for g in guards)
            tags = []
            for k in kinds:
                if isinstance(k, tuple) and k[0] == "fixed":
                    total["fixed"] += 1
                    width = 1
                    mm = re.search(r"&cmd_frame\[[^\]]+\][^,]*,\s*(\d+)\)", line)
                    if k[2] and mm:
                        width = int(mm.group(1))
                    elif k[2]:
                        width = 0  # a pointer handed on; its extent is the callee's
                    hi = k[1] + width
                    if not guarded and width:
                        top = max(top, hi)
                    tags.append(f"fixed[{k[1]}..{hi})" + (" guarded" if guarded else ""))
                elif isinstance(k, tuple):
                    total["cursor"] += 1
                    tags.append(f"cursor[{k[1]}]")
                else:
                    total[k] += 1
                    tags.append(k)
            if tags or re.search(r"\blen\b", line):
                print(f"    {', '.join(tags) or 'len test':40s} {s}")
        if top:
            mark = "ok" if minimum is not None and top <= minimum else "ABOVE MIN"
            print(f"    -> unguarded fixed reads end at {top}, minimum {minimum}: {mark}")
    print()
    print(f"{nhandlers} handlers; fixed offsets {total['fixed']}, cursor {total['cursor']}, "
          f"writes {total['write']}, len-derived lengths {total['len']}")


if __name__ == "__main__":
    main()
