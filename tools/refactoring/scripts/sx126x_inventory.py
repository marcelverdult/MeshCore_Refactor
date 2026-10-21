#!/usr/bin/env python3
"""Measure how far the SX126x chip classes in src/helpers/radiolib differ.

Usage: python3 tools/refactoring/scripts/sx126x_inventory.py [repo root]

Cuts every member out of CustomSX1262.h, CustomSX1268.h and CustomLLCC68.h,
removes comments and white space and replaces the chip name with a placeholder,
then prints per member which chips carry an identical copy. For members that
differ, it prints a unified diff of the normalized lines against SX1262.
Lines outside the class body (includes) are compared the same way.
"""
import difflib
import hashlib
import re
import sys
from pathlib import Path

CHIPS = ["SX1262", "SX1268", "LLCC68"]


def normalize(text, chip):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    text = text.replace("Custom" + chip, "Custom@CHIP@").replace(chip, "@CHIP@")
    lines = [re.sub(r"\s+", " ", l).strip() for l in text.splitlines()]
    return [l for l in lines if l]


def members(lines):
    """Split the normalized class body into members by brace depth."""
    out, head, cur, depth, in_class = {}, [], [], 0, False
    for l in lines:
        if not in_class:
            if l.startswith("class "):
                in_class = True
                depth = l.count("{") - l.count("}")
            else:
                head.append(l)
            continue
        if depth == 1 and not cur and l in ("public:", "};"):
            continue
        cur.append(l)
        depth += l.count("{") - l.count("}")
        if depth == 1 and (l.endswith("}") or l.endswith(";")):
            sig = next((c for c in cur if not c.startswith("#")), cur[0])
            name = re.search(r"([~\w]+)\s*\(", sig)
            key = name.group(1) if name else sig
            while key in out:
                key += "'"
            out[key] = cur
            cur = []
    return head, out


def main():
    root = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    parsed = {}
    for chip in CHIPS:
        src = (root / "src/helpers/radiolib" / f"Custom{chip}.h").read_text()
        parsed[chip] = members(normalize(src, chip))

    names = []
    for chip in CHIPS:
        for n in ["(includes)"] + list(parsed[chip][1]):
            if n not in names:
                names.append(n)

    def body(chip, n):
        head, ms = parsed[chip]
        return head if n == "(includes)" else ms.get(n)

    print(f"{'member':<22} {'lines':>5}  groups of identical copies")
    diffs = []
    for n in names:
        groups = {}
        for chip in CHIPS:
            b = body(chip, n)
            h = hashlib.sha1("\n".join(b).encode()).hexdigest()[:8] if b is not None else "absent"
            groups.setdefault(h, []).append(chip)
        ln = len(body("SX1262", n) or [])
        desc = " / ".join(", ".join(v) + ("" if k != "absent" else " (absent)") for k, v in groups.items())
        print(f"{n:<22} {ln:>5}  {desc}")
        if len(groups) > 1:
            diffs.append(n)

    for n in diffs:
        for chip in CHIPS[1:]:
            a, b = body("SX1262", n) or [], body(chip, n) or []
            if a == b:
                continue
            print(f"\n--- {n}: SX1262 vs {chip}")
            for l in difflib.unified_diff(a, b, "SX1262", chip, n=0, lineterm=""):
                if not l.startswith(("---", "+++")):
                    print(l)


if __name__ == "__main__":
    main()
