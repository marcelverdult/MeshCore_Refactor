#!/usr/bin/env python3
"""Measure how much of the board variants' code is copied.

Used for step 5 of the refactoring series (docs/refactoring/0008-variant-target.md)
to show the duplication before each cut and what is left after it. Python 3
standard library only; run from the repository root:

    python3 tools/refactoring/variant_duplication.py

For every function that a variant's target.cpp defines, it reports in how many
files the function exists and how many different bodies there are, once with
only runs of spaces collapsed (strict) and once with comments and all white space
removed (normalized). The FEM gain handlers of the board classes are compared
with the board class name replaced, since that name is the only thing they are
expected to differ in.
"""

import collections
import glob
import re

FUNC_HEAD = re.compile(r"^(?:[\w:<>*&]+\s+)+([\w:]+)\s*\([^;{)]*\)\s*(?:const\s*)?\{", re.M)

# Board classes whose FEM gain handlers are expected to be copies of each other,
# plus the other boards that have handlers of the same name, for comparison.
FEM_BOARDS = [
    ("variants/heltec_v4/HeltecV4Board.cpp", "HeltecV4Board"),
    ("variants/heltec_v4_r8/HeltecV4R8Board.cpp", "HeltecV4R8Board"),
    ("variants/heltec_t096/T096Board.cpp", "T096Board"),
    ("variants/heltec_tracker_v2/HeltecTrackerV2Board.cpp", "HeltecTrackerV2Board"),
    ("variants/lilygo_tbeam_1w/TBeam1WBoard.cpp", "TBeam1WBoard"),
    ("variants/station_g3_esp32/StationG3Board.cpp", "StationG3Board"),
    ("variants/wio-tracker-l1-1w/WioTrackerL1Board.cpp", "WioTrackerL1Board"),
]


def body(src, start):
    """The text of the function whose head ends at `start` (just after its `{`)."""
    depth, i = 1, start
    while depth:
        depth += (src[i] == "{") - (src[i] == "}")
        i += 1
    return src[:i]


def function(src, name):
    m = re.search(r"^[^\n]*\b" + re.escape(name) + r"\s*\([^)]*\)\s*\{", src, re.M)
    return None if m is None else body(src[m.start():], m.end() - m.start())


def strict(text):
    return re.sub(r" +", " ", text)


def normalized(text):
    text = re.sub(r"//[^\n]*", "", text)
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"\s+", " ", text).strip()


def variant_functions():
    files = sorted(glob.glob("variants/*/target.cpp"))
    seen = collections.defaultdict(list)  # name -> [(variant, body)]
    for path in files:
        src = open(path).read()
        for m in FUNC_HEAD.finditer(src):
            seen[m.group(1)].append((path.split("/")[1], body(src[m.start():], m.end() - m.start())))
    return files, seen


def main():
    variant_dirs = sorted(glob.glob("variants/*/"))
    files, seen = variant_functions()
    print(f"variant directories: {len(variant_dirs)}, with target.cpp: {len(files)}")
    lines = sum(len(open(f).read().splitlines()) for f in files)
    print(f"lines in all target.cpp: {lines}")
    print()
    print(f"{'function':32s} {'files':>5s} {'strict':>6s} {'normal':>6s}  largest normalized group")
    for name, defs in sorted(seen.items(), key=lambda kv: -len(kv[1])):
        if len(defs) < 2:
            continue
        strict_groups = collections.Counter(strict(b) for _, b in defs)
        norm_groups = collections.Counter(normalized(b) for _, b in defs)
        print(f"{name:32s} {len(defs):5d} {len(strict_groups):6d} {len(norm_groups):6d}  "
              f"{norm_groups.most_common(1)[0][1]}")

    print()
    print("radio_new_identity() bodies:")
    groups = collections.defaultdict(list)
    for variant, b in seen.get("radio_new_identity", []):
        groups[normalized(b)].append(variant)
    for text, variants in sorted(groups.items(), key=lambda kv: -len(kv[1])):
        listed = "" if len(variants) > 5 else " (" + ", ".join(variants) + ")"
        print(f"  {len(variants):3d} x {text}{listed}")

    print()
    print("FEM gain handlers, board class name replaced by BOARD:")
    for fn in ("attachDynamicPrefs", "handleCommand"):
        groups = collections.defaultdict(list)
        for path, cls in FEM_BOARDS:
            b = function(open(path).read(), fn)
            groups["<none>" if b is None else normalized(b.replace(cls, "BOARD"))].append(cls)
        print(f"  {fn}: {len(groups)} distinct bodies")
        for text, classes in groups.items():
            print(f"    {len(text):5d} chars: {', '.join(classes)}")


if __name__ == "__main__":
    main()
