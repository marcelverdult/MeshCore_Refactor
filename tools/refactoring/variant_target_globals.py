#!/usr/bin/env python3
"""Inventory of the radio, wrapper and RTC globals in the variants' target.cpp.

Used for cut D of step 11 of the refactoring series
(docs/refactoring/0011-variant-target.md). Python 3 standard library plus the
`pio` command; run from the repository root:

    python3 tools/refactoring/variant_target_globals.py [--detail]

For every variants/*/target.cpp it extracts the "radio block": the declarations
of the SPI object, `radio`, `radio_driver`, `fallback_clock` and `rtc_clock`,
together with the preprocessor lines around them, from the first of those lines
to the last. Blocks are compared normalized (comments and white space removed)
and grouped. For each group it reports the variants, their firmware environments
(an environment belongs to a variant when its build_src_filter names
variants/<dir>), the platforms and whether the block is contiguous (nothing but
those declarations and preprocessor lines between the first and the last).
It then does the same for the bodies of radio_init(), and lists for every
variant which of `radio`, `spi` and `fallback_clock` its target.cpp still names
once the radio block and a radio_init() body that a shared default can take over
are gone (the variants that name none can take their globals from shared code).
"""

import collections
import glob
import json
import re
import subprocess
import sys

DECL = re.compile(
    r"^\s*(?:static\s+)?(?:"
    r"SPIClass\s+\w+|"
    r"RADIO_CLASS\s+radio\b|"
    r"WRAPPER_CLASS\s+radio_driver\b|"
    r"\w*RTCClock\s+fallback_clock\b|"
    r"\w*RTCClock\s+rtc_clock\b)"
)
PREPROC = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b")


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def norm(line):
    return re.sub(r"\s+", " ", line).strip()


def radio_block(src):
    """(block lines, contiguous, depth problem) of the radio declarations."""
    lines = [norm(l) for l in strip_comments(src).splitlines()]
    lines = [l for l in lines if l]
    idx = [i for i, l in enumerate(lines) if DECL.match(l)]
    if not idx:
        return [], True, []
    first, last = idx[0], idx[-1]
    # widen to the enclosing preprocessor lines that open/close inside the block
    depth, lo = 0, first
    for i in range(first, last + 1):
        m = PREPROC.match(lines[i])
        if m and m.group(1) in ("if", "ifdef", "ifndef"):
            depth += 1
        elif m and m.group(1) == "endif":
            depth -= 1
    hi = last
    while depth > 0 and hi + 1 < len(lines):
        hi += 1
        m = PREPROC.match(lines[hi])
        if m and m.group(1) in ("if", "ifdef", "ifndef"):
            depth += 1
        elif m and m.group(1) == "endif":
            depth -= 1
    while depth < 0 and lo > 0:
        lo -= 1
        m = PREPROC.match(lines[lo])
        if m and m.group(1) in ("if", "ifdef", "ifndef"):
            depth += 1
        elif m and m.group(1) == "endif":
            depth -= 1
    block = lines[lo:hi + 1]
    foreign = [l for l in block if not DECL.match(l) and not PREPROC.match(l)]
    # preprocessor context the first declaration sits in (unclosed #if before it)
    ctx = []
    for l in lines[:lo]:
        m = PREPROC.match(l)
        if m and m.group(1) in ("if", "ifdef", "ifndef"):
            ctx.append(l)
        elif m and m.group(1) == "endif" and ctx:
            ctx.pop()
    return block, not foreign, ctx


def function_body(src, name):
    src = strip_comments(src)
    m = re.search(r"^[^\n;]*\b" + re.escape(name) + r"\s*\(\s*\)\s*\{", src, re.M)
    if m is None:
        return None
    depth, i = 1, m.end()
    while depth:
        depth += (src[i] == "{") - (src[i] == "}")
        i += 1
    return norm(src[m.end():i - 1])


# radio_init() bodies a shared default can take over: none (cut C's nRF52
# default), or the two ESP32 bodies that differ only where P_LORA_SCLK is
# undefined, which their variants' blocks never are.
SHAREABLE_INIT = {
    None,
    "fallback_clock.begin(); rtc_clock.begin(Wire); #if defined(P_LORA_SCLK) "
    "return radio.std_init(&spi); #else return radio.std_init(); #endif",
    "fallback_clock.begin(); rtc_clock.begin(Wire); return radio.std_init(&spi);",
}


def remaining_refs(src, init_body):
    """Names of the radio block target.cpp still uses outside the block."""
    src = strip_comments(src)
    rest = "\n".join(l for l in src.splitlines() if not DECL.match(norm(l)))
    if init_body in SHAREABLE_INIT:
        m = re.search(r"bool\s+radio_init\s*\(\s*\)\s*\{", rest)
        if m:
            depth, i = 1, m.end()
            while depth:
                depth += (rest[i] == "{") - (rest[i] == "}")
                i += 1
            rest = rest[:m.start()] + rest[i:]
    return sorted(set(re.findall(r"\b(radio|spi|fallback_clock)\b", rest)))


def environments():
    out = subprocess.run(["pio", "project", "config", "--json-output"],
                         capture_output=True, text=True, check=True).stdout
    envs = {}
    for section, items in json.loads(out):
        if not section.startswith("env:"):
            continue
        kv = dict(items)
        flags = " ".join(kv.get("build_flags", []))
        envs[section[4:]] = {
            "src": " ".join(kv.get("build_src_filter", [])),
            "platform": kv.get("platform", ""),
            "flags": flags,
        }
    return envs


def platform_name(p):
    if "espressif32" in p:
        return "ESP32"
    if "nordicnrf52" in p:
        return "nRF52"
    if "raspberrypi" in p:
        return "RP2040"
    if "ststm32" in p:
        return "STM32"
    return p or "native"


def flag(flags, name):
    m = re.search(r"-D\s*" + name + r"(?:=(\S+))?", flags)
    return None if m is None else (m.group(1) or "1")


def main():
    detail = "--detail" in sys.argv
    envs = environments()
    variants = sorted(p.split("/")[1] for p in glob.glob("variants/*/target.cpp"))
    info = {}
    for v in variants:
        src = open(f"variants/{v}/target.cpp", encoding="utf-8", errors="replace").read()
        block, contiguous, ctx = radio_block(src)
        mine = {e: d for e, d in envs.items()
                if re.search(r"variants/" + re.escape(v) + r"(?:>|/)", d["src"])}
        info[v] = {
            "block": block, "contiguous": contiguous, "ctx": ctx,
            "envs": mine,
            "platforms": sorted({platform_name(d["platform"]) for d in mine.values()}),
            "sclk": sorted({flag(d["flags"], "P_LORA_SCLK") is not None for d in mine.values()}),
            "wrapper": sorted({flag(d["flags"], "WRAPPER_CLASS") or "-" for d in mine.values()}),
            "radio_init": function_body(src, "radio_init"),
        }
        info[v]["refs"] = remaining_refs(src, info[v]["radio_init"])

    print(f"target.cpp: {len(variants)}, firmware environments: "
          f"{sum(len(i['envs']) for i in info.values())} of {len(envs)}")
    print()
    print("== radio blocks (normalized) ==")
    groups = collections.defaultdict(list)
    for v, i in info.items():
        groups[tuple(i["block"])].append(v)
    for block, vs in sorted(groups.items(), key=lambda kv: -len(kv[1])):
        nenv = sum(len(info[v]["envs"]) for v in vs)
        plats = sorted({p for v in vs for p in info[v]["platforms"]})
        cont = sum(info[v]["contiguous"] for v in vs)
        ctx = sorted({" / ".join(info[v]["ctx"]) for v in vs if info[v]["ctx"]})
        sclk = sorted({str(s) for v in vs for s in info[v]["sclk"]})
        print(f"-- {len(vs)} variants, {nenv} envs, {','.join(plats)}, contiguous {cont}/{len(vs)},"
              f" P_LORA_SCLK in envs: {'/'.join(sclk)}" + (f", inside: {ctx}" if ctx else ""))
        for l in block:
            print(f"   {l}")
        print(f"   variants: {', '.join(vs)}")
    print()
    print("== radio_init() bodies (normalized), by platform ==")
    rg = collections.defaultdict(list)
    for v, i in info.items():
        if i["radio_init"] is not None:
            rg[(",".join(i["platforms"]), i["radio_init"])].append(v)
    for (plat, body), vs in sorted(rg.items(), key=lambda kv: (kv[0][0], -len(kv[1]))):
        nenv = sum(len(info[v]["envs"]) for v in vs)
        print(f"-- {plat}: {len(vs)} variants, {nenv} envs")
        print(f"   {body}")
        print(f"   variants: {', '.join(vs)}")
    without = [v for v, i in info.items() if i["radio_init"] is None]
    print(f"-- without own radio_init() (cut C default or none): {len(without)}")
    print()
    print("== variants whose target.cpp names no radio global once block and shareable radio_init() are gone ==")
    cand = collections.defaultdict(list)
    for v, i in info.items():
        if not i["refs"] and i["block"] and "radio_driver" in " ".join(i["block"]):
            cand[(",".join(i["platforms"]), i["radio_init"] is None)].append(v)
    for (plat, default), vs in sorted(cand.items()):
        nenv = sum(len(info[v]["envs"]) for v in vs)
        how = "radio_init() already shared" if default else "own radio_init() shareable"
        print(f"-- {plat}, {how}: {len(vs)} variants, {nenv} envs")
        print(f"   {', '.join(vs)}")
    rest = [f"{v} ({'/'.join(i['refs'])})" for v, i in info.items() if i["refs"]]
    print(f"-- still naming them: {len(rest)}: {', '.join(rest)}")
    if detail:
        print()
        for v, i in info.items():
            print(v, i["platforms"], len(i["envs"]), "sclk", i["sclk"], i["wrapper"])


if __name__ == "__main__":
    main()
