#!/usr/bin/env python3
"""Measure how far the chip wrappers in src/helpers/radiolib differ.

Usage: python3 tools/refactoring/scripts/wrapper_inventory.py [repo root]

Cuts every member out of the eight Custom<chip>Wrapper.h files, removes comments
and white space and replaces the chip name with a placeholder, then prints per
member which wrappers carry an identical copy (groups separated by " / ",
"absent" for wrappers without the member). The table at the end lists, per
wrapper, which members it overrides at all.
"""
import hashlib
import re
import sys
from pathlib import Path

CHIPS = ["SX1262", "SX1268", "LLCC68", "STM32WLx", "LR1110", "LR1121", "LR2021", "SX1276"]


def normalize(text, chip):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    text = text.replace("Custom" + chip, "Custom@CHIP@").replace(chip, "@CHIP@")
    lines = [re.sub(r"\s+", " ", l).strip() for l in text.splitlines()]
    return [l for l in lines if l]


def members(lines):
    """Split the normalized class body into members by brace depth."""
    out, cur, depth, in_class = {}, [], 0, False
    for l in lines:
        if not in_class:
            if l.startswith("class "):
                in_class = True
                depth = l.count("{") - l.count("}")
            continue
        if depth == 1 and not cur and l in ("public:", "protected:", "private:", "};"):
            continue
        cur.append(l)
        depth += l.count("{") - l.count("}")
        if depth == 1 and (l.endswith("}") or l.endswith(";")):
            sig = cur[0]
            name = re.search(r"([~\w]+)\s*\(", sig)
            key = name.group(1) if name else re.sub(r"\s*(=.*)?;$", "", sig).split()[-1]
            key = key.replace("@CHIP@", "") or "(constructor)"
            if key == "Wrapper" or (key.startswith("Custom") and key.endswith("Wrapper")):
                key = "(constructor)"
            out[key] = cur
            cur = []
    return out


def main():
    root = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    parsed = {}
    for chip in CHIPS:
        src = (root / "src/helpers/radiolib" / f"Custom{chip}Wrapper.h").read_text()
        parsed[chip] = members(normalize(src, chip))

    names = []
    for chip in CHIPS:
        for n in parsed[chip]:
            if n not in names:
                names.append(n)

    print(f"{'member':<22} groups of identical copies (normalized lines)")
    for n in names:
        groups = {}
        for chip in CHIPS:
            b = parsed[chip].get(n)
            h = hashlib.sha1("\n".join(b).encode()).hexdigest()[:8] if b is not None else "absent"
            groups.setdefault(h, []).append(chip)
        parts = []
        for h, cs in groups.items():
            if h == "absent":
                parts.append(", ".join(cs) + " (absent)")
            else:
                parts.append(", ".join(cs) + f" ({len(parsed[cs[0]][n])})")
        print(f"{n:<22} {' / '.join(parts)}")

    print()
    print("whole wrapper bodies, identical after normalization:")
    whole = {}
    for chip in CHIPS:
        h = hashlib.sha1(repr(sorted(parsed[chip].items())).encode()).hexdigest()[:8]
        whole.setdefault(h, []).append(chip)
    for cs in whole.values():
        print("  " + ", ".join(cs))


if __name__ == "__main__":
    main()
