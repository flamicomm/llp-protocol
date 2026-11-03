#!/usr/bin/env python3
"""Measure the LLP v4 core footprint (RAM and flash) per target and profile.

For every target that has a toolchain available, this script compiles a minimal
translation unit that uses the whole v4 API and reports:

  - RAM: `sizeof(llp4_parser_t)`, `sizeof(llp4_tx_t)` and `sizeof(llp4_sink_t)`,
    read from the object symbols (works for cross targets, no execution);
  - flash: the size delta between that unit and an identical one with empty
    function bodies, i.e. the code the core actually adds.

Profiles:
  micro      - stats off, 16-nibble CRC (kept in flash on AVR)
  micro-bits - stats off, bitwise CRC (smallest flash)
  stats      - statistics counters on

Usage:
    python3 scripts/measure_v4.py                 # print the table
    python3 scripts/measure_v4.py --update-readme # rewrite the README section
    python3 scripts/measure_v4.py --check-readme  # fail if the README is stale
    python3 scripts/measure_v4.py --budget        # fail if a budget is exceeded

The budget gate is what CI runs: it fails if RAM or flash grows past
`BUDGET_*` below, so the numbers in the README cannot rot.
"""

from __future__ import annotations

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INCLUDE = ROOT / "include"

README = ROOT / "README.md"
BEGIN_MARK = "<!-- LLP4-SIZE-TABLE:BEGIN -->"
END_MARK = "<!-- LLP4-SIZE-TABLE:END -->"

# Hard budgets for the smallest target (ATmega328P, micro profile, stats off).
# CI fails if any of these are exceeded. Current numbers: 17 B / 6 B / 622 B.
BUDGET = {
    "avr_parser_ram": 20,   # sizeof(llp4_parser_t)
    "avr_tx_ram": 8,        # sizeof(llp4_tx_t)
    "avr_flash": 800,       # core code (encode + parse + nibble CRC table)
}

# Tolerance when comparing the committed README table against a fresh
# measurement: different binutils versions can move a few bytes around.
TABLE_FLASH_TOLERANCE = 32

PROFILES = {
    "micro": ["-DLLP4_ENABLE_STATS=0", "-DLLP4_CRC_TABLE=1"],
    "micro-bits": ["-DLLP4_ENABLE_STATS=0", "-DLLP4_CRC_TABLE=0"],
    "stats": ["-DLLP4_ENABLE_STATS=1", "-DLLP4_CRC_TABLE=1"],
}

SOURCE = """\
#include "llp_v4.h"

llp4_parser_t g_parser;
llp4_tx_t g_tx;
llp4_sink_t g_sink;

static void sink(void *ctx, uint8_t byte)
{
    (void)ctx;
    (void)byte;
}

size_t use_all(const uint8_t *in, uint8_t len, uint8_t *out, size_t cap,
               uint16_t now)
{
    llp4_parser_t p;
    int rc;
    uint8_t got;
    llp4_tx_t tx;
    uint8_t i;

    llp4_parser_init(&p, out, len, LLP4_DEFAULT_TIMEOUT_MS);
    rc = llp4_parser_feed(&p, in[0], now);
    if (rc == LLP4_EV_FRAME) {
        (void)llp4_parser_payload(&p, &got);
        (void)llp4_parser_channel(&p);
    }

    llp4_tx_begin(&tx, 1, len, sink, 0);
    for (i = 0; i < len; i++) {
        llp4_tx_byte(&tx, in[i]);
    }
    llp4_tx_end(&tx);

    return llp4_encode(1, in, len, out, cap) + (size_t)rc;
}
"""

BASELINE = """\
#include "llp_v4.h"

llp4_parser_t g_parser;
llp4_tx_t g_tx;
llp4_sink_t g_sink;

size_t use_all(const uint8_t *in, uint8_t len, uint8_t *out, size_t cap,
               uint16_t now)
{
    (void)in;
    (void)len;
    (void)out;
    (void)cap;
    (void)now;
    return 0;
}
"""


@dataclass
class Toolchain:
    name: str
    cc: str
    nm: str
    size: str
    flags: list[str]
    ram_note: str


def _find_platformio(tool: str, package: str) -> str | None:
    pattern = os.path.expanduser(f"~/.platformio/packages/{package}/bin/{tool}")
    matches = sorted(glob.glob(pattern))
    return matches[-1] if matches else None


def discover_toolchains(only: set[str] | None = None) -> list[Toolchain]:
    found: list[Toolchain] = []

    if only is None or "avr" in only:
        cc = _find_platformio("avr-gcc", "toolchain-atmelavr") or shutil.which("avr-gcc")
        if cc:
            base = str(Path(cc).parent)
            found.append(
                Toolchain(
                    name="ATmega328P",
                    cc=cc,
                    nm=f"{base}/avr-nm",
                    size=f"{base}/avr-size",
                    flags=["-mmcu=atmega328p"],
                    ram_note="AVR",
                )
            )

    if only is None or "arm" in only:
        cc = _find_platformio("arm-none-eabi-gcc", "toolchain-gccarmnoneeabi") or shutil.which(
            "arm-none-eabi-gcc"
        )
        if cc:
            base = str(Path(cc).parent)
            found.append(
                Toolchain(
                    name="ARM Cortex-M4",
                    cc=cc,
                    nm=f"{base}/arm-none-eabi-nm",
                    size=f"{base}/arm-none-eabi-size",
                    flags=["-mcpu=cortex-m4", "-mthumb"],
                    ram_note="ARM",
                )
            )

    if only is None or "host" in only:
        cc = shutil.which("cc") or shutil.which("gcc")
        if cc:
            found.append(
                Toolchain(
                    name="x86-64 (host)",
                    cc=cc,
                    nm="nm",
                    size="size",
                    flags=[],
                    ram_note="host",
                )
            )

    return found


def compile_object(toolchain: Toolchain, source: str, defines: list[str],
                   out_dir: Path, stem: str) -> Path:
    src = out_dir / f"{stem}.c"
    obj = out_dir / f"{stem}.o"
    src.write_text(source, encoding="utf-8")
    cmd = [
        toolchain.cc,
        "-Os",
        "-std=c99",
        "-Wall",
        "-Wextra",
        "-I",
        str(INCLUDE),
        *toolchain.flags,
        *defines,
        "-c",
        str(src),
        "-o",
        str(obj),
    ]
    subprocess.run(cmd, check=True, capture_output=True)
    return obj


def text_size(toolchain: Toolchain, obj: Path) -> int:
    out = subprocess.run([toolchain.size, "-A", str(obj)], check=True,
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[0] == ".text":
            return int(parts[1])
    return 0


def symbol_sizes(toolchain: Toolchain, obj: Path) -> dict[str, int]:
    out = subprocess.run([toolchain.nm, "-S", str(obj)], check=True,
                         capture_output=True, text=True).stdout
    sizes: dict[str, int] = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 4 and parts[3] in ("g_parser", "g_tx", "g_sink"):
            sizes[parts[3]] = int(parts[1], 16)
    return sizes


def measure(toolchains: list[Toolchain]) -> list[dict]:
    rows: list[dict] = []
    with tempfile.TemporaryDirectory() as tmp:
        out_dir = Path(tmp)
        for toolchain in toolchains:
            for profile, defines in PROFILES.items():
                full = compile_object(toolchain, SOURCE, defines, out_dir,
                                      f"full_{profile}_{toolchain.ram_note}")
                base = compile_object(toolchain, BASELINE, defines, out_dir,
                                      f"base_{profile}_{toolchain.ram_note}")
                sizes = symbol_sizes(toolchain, full)
                rows.append(
                    {
                        "target": toolchain.name,
                        "profile": profile,
                        "parser": sizes.get("g_parser", 0),
                        "tx": sizes.get("g_tx", 0),
                        "sink": sizes.get("g_sink", 0),
                        "flash": text_size(toolchain, full) - text_size(toolchain, base),
                    }
                )
    return rows


def markdown_table(rows: list[dict]) -> str:
    lines = [
        "| Target | Profile | `sizeof(parser)` | `sizeof(tx)` | Core flash |",
        "|---|---|---:|---:|---:|",
    ]
    for row in rows:
        lines.append(
            f"| {row['target']} | `{row['profile']}` | {row['parser']} B | "
            f"{row['tx']} B | {row['flash']} B |"
        )
    lines.append("")
    lines.append(
        "_Generated by `scripts/measure_v4.py`. \"Core flash\" is the code the "
        "v4 core adds over an empty build (encode + parse + CRC), not a full "
        "application._"
    )
    return "\n".join(lines)


def update_readme(table: str) -> None:
    text = README.read_text(encoding="utf-8")
    if BEGIN_MARK not in text or END_MARK not in text:
        raise SystemExit(
            f"README.md is missing {BEGIN_MARK} / {END_MARK} markers"
        )
    pattern = re.compile(
        re.escape(BEGIN_MARK) + r".*?" + re.escape(END_MARK), re.DOTALL
    )
    text = pattern.sub(f"{BEGIN_MARK}\n{table}\n{END_MARK}", text)
    README.write_text(text, encoding="utf-8")
    print("README.md updated")


def readme_table() -> str | None:
    text = README.read_text(encoding="utf-8")
    pattern = re.compile(
        re.escape(BEGIN_MARK) + r"\n(.*?)\n" + re.escape(END_MARK), re.DOTALL
    )
    match = pattern.search(text)
    return match.group(1) if match else None


def _parse_table(text: str) -> dict[tuple[str, str], tuple[int, int, int]]:
    rows: dict[tuple[str, str], tuple[int, int, int]] = {}
    for line in text.splitlines():
        if not line.startswith("|") or line.startswith("|---"):
            continue
        cells = [cell.strip() for cell in line.strip("|").split("|")]
        if len(cells) != 5 or cells[0] == "Target":
            continue
        target, profile = cells[0], cells[1].strip("`")
        rows[(target, profile)] = (
            int(cells[2].split()[0]),
            int(cells[3].split()[0]),
            int(cells[4].split()[0]),
        )
    return rows


def compare_tables(committed: str, generated: str) -> list[str]:
    old = _parse_table(committed)
    new = _parse_table(generated)
    problems: list[str] = []

    if set(old) != set(new):
        missing = set(old) - set(new)
        extra = set(new) - set(old)
        if missing:
            problems.append(f"rows missing from the new measurement: {sorted(missing)}")
        if extra:
            problems.append(f"rows not present in the README: {sorted(extra)}")

    for key in sorted(set(old) & set(new)):
        old_parser, old_tx, old_flash = old[key]
        new_parser, new_tx, new_flash = new[key]
        label = f"{key[0]} / {key[1]}"
        if old_parser != new_parser:
            problems.append(f"{label}: parser {old_parser} -> {new_parser} B")
        if old_tx != new_tx:
            problems.append(f"{label}: tx {old_tx} -> {new_tx} B")
        if abs(old_flash - new_flash) > TABLE_FLASH_TOLERANCE:
            problems.append(
                f"{label}: flash {old_flash} -> {new_flash} B "
                f"(tolerance {TABLE_FLASH_TOLERANCE})"
            )
    return problems


def check_budget(rows: list[dict], toolchains: list[Toolchain]) -> int:
    if not any(t.ram_note == "AVR" for t in toolchains):
        print("budget: AVR toolchain not found, skipping gate")
        return 0

    failures: list[str] = []
    for row in rows:
        if row["target"] != "ATmega328P":
            continue
        if row["profile"] == "micro":
            if row["parser"] > BUDGET["avr_parser_ram"]:
                failures.append(
                    f"parser RAM {row['parser']} B > {BUDGET['avr_parser_ram']} B"
                )
            if row["tx"] > BUDGET["avr_tx_ram"]:
                failures.append(f"tx RAM {row['tx']} B > {BUDGET['avr_tx_ram']} B")
            if row["flash"] > BUDGET["avr_flash"]:
                failures.append(f"flash {row['flash']} B > {BUDGET['avr_flash']} B")

    if failures:
        print("BUDGET EXCEEDED:")
        for failure in failures:
            print("  -", failure)
        return 1

    print("budget OK (ATmega328P, micro profile)")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--update-readme", action="store_true")
    parser.add_argument("--check-readme", action="store_true")
    parser.add_argument("--budget", action="store_true")
    parser.add_argument(
        "--toolchains",
        default="avr,host",
        help="comma separated: avr,arm,host (default: avr,host, for CI "
        "determinism; add 'arm' locally for the Cortex-M row)",
    )
    args = parser.parse_args()

    only = {name.strip() for name in args.toolchains.split(",") if name.strip()}
    toolchains = discover_toolchains(only)
    if not toolchains:
        print("no toolchains found")
        return 2

    rows = measure(toolchains)
    table = markdown_table(rows)

    if args.update_readme:
        update_readme(table)
        print(table)
        return 0

    if args.check_readme:
        current = readme_table()
        if current is None:
            print("README.md: size table markers not found")
            return 1
        problems = compare_tables(current, table)
        if problems:
            print("README.md size table is stale; run --update-readme")
            for problem in problems:
                print("  -", problem)
            return 1
        print("README.md size table matches the measurement")
        return 0

    print(table)

    if args.budget:
        return check_budget(rows, toolchains)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
