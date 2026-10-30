#!/usr/bin/env python3
"""Check CI's release files against the local build of the same tag, and flash them.

Pushing a tag vX.Y.Z makes the Release workflow build both firmware builds
from the tag and attach the files to a draft GitHub Release. This downloads
them into build/release/vX.Y.Z/ and checks:

- SHA256SUMS.txt matches the files;
- each .bin is exactly its .elf's program image, so drag-and-drop and OpenOCD
  load the same thing;
- each .elf has the same functions and variables, each of the same size, and
  the same section sizes as the local build of the tag (debug info aside).

The files can't be byte-identical to a laptop build: the macOS and Linux
toolchains ship separately built C libraries, and the linker places newlib's
functions in a different order. So the files to publish are flashed and
checked on the board themselves: --flash loads one onto the board after the
checks pass (then run scripts/board_check.py).

Needs the GitHub CLI (gh, logged in), the Arm toolchain and, for --flash,
OpenOCD. Exits with status 1 on any difference.

Usage:
    python3 scripts/release_check.py vX.Y.Z [--flash debug|release]
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILDS = {"debug": "firmware", "release": "firmware-release"}  # file name -> preset


def run(*args: str) -> str:
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


def local_version(preset: str) -> str:
    header = ROOT / "build" / preset / "firmware" / "generated" / "version.h"
    for line in header.read_text().splitlines():
        if line.startswith("#define FW_VERSION "):
            return line.split('"')[1]
    raise ValueError(f"no FW_VERSION in {header}")


def symbols(elf: Path) -> tuple[dict[str, tuple[str, int]], dict[str, int]]:
    """Sized symbols (name -> (type, size)) and their addresses."""
    sized, addresses = {}, {}
    for line in run("arm-none-eabi-nm", "-S", str(elf)).splitlines():
        parts = line.split()
        if len(parts) == 4:
            address, size, kind, name = parts
            sized[name] = (kind, int(size, 16))
            addresses[name] = int(address, 16)
    return sized, addresses


def sections(elf: Path) -> dict[str, int]:
    """Section sizes, debug info aside: it records the folder each build ran in."""
    sizes = {}
    for line in run("arm-none-eabi-size", "-A", str(elf)).splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[0].startswith(".") and not parts[0].startswith(".debug"):
            sizes[parts[0]] = int(parts[1])
    return sizes


def check_sums(folder: Path) -> list[str]:
    sums = folder / "SHA256SUMS.txt"
    if not sums.exists():
        return ["SHA256SUMS.txt: missing from the draft release"]
    problems = []
    for line in sums.read_text().splitlines():
        digest, name = line.split(maxsplit=1)
        if hashlib.sha256((folder / name).read_bytes()).hexdigest() != digest:
            problems.append(f"{name}: SHA256SUMS.txt does not match the file")
    return problems


def compare(tag: str, ci: Path) -> list[str]:
    problems = []
    for name, preset in BUILDS.items():
        files = {ext: ci / f"obd2_sniffer-{tag}-{name}.{ext}" for ext in ("bin", "hex", "elf")}
        missing = [f.name for f in files.values() if not f.exists()]
        if missing:
            problems += [f"{m}: missing from the draft release" for m in missing]
            continue

        with tempfile.TemporaryDirectory() as tmp:
            image = Path(tmp) / "image.bin"
            run("arm-none-eabi-objcopy", "-O", "binary", str(files["elf"]), str(image))
            if image.read_bytes() != files["bin"].read_bytes():
                problems.append(f"{files['bin'].name}: not the program image of the .elf")

        version = local_version(preset)
        if version != tag:
            problems.append(f"local {preset} build is {version}, not {tag}: build it from the tag")
            continue
        local_elf = ROOT / "build" / preset / "firmware" / "obd2_sniffer.elf"
        ci_sized, ci_at = symbols(files["elf"])
        local_sized, local_at = symbols(local_elf)
        if ci_sized != local_sized:
            changed = sorted(set(ci_sized.items()) ^ set(local_sized.items()))
            names = ", ".join(sorted({n for n, _ in changed})[:5])
            problems.append(f"{files['elf'].name}: symbols differ from the local build ({names})")
            continue
        if sections(files["elf"]) != sections(local_elf):
            problems.append(f"{files['elf'].name}: section sizes differ from the local build")
            continue
        moved = sum(1 for n in ci_sized if ci_at[n] != local_at[n])
        print(
            f"ok    {name}: {len(ci_sized)} symbols and all section sizes match the local build"
            f"{f'; {moved} placed at other addresses' if moved else ''}"
        )
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("tag", help="release tag, e.g. v0.4.2")
    parser.add_argument("--flash", choices=sorted(BUILDS), help="then load this build's .elf")
    args = parser.parse_args()

    ci = ROOT / "build" / "release" / args.tag
    shutil.rmtree(ci, ignore_errors=True)
    subprocess.run(["gh", "release", "download", args.tag, "--dir", str(ci)], check=True, cwd=ROOT)
    problems = check_sums(ci) + compare(args.tag, ci)
    for problem in problems:
        print(f"FAIL  {problem}")
    if problems:
        return 1
    print(f"CI's files for {args.tag} are in {ci.relative_to(ROOT)}.")

    if args.flash:
        elf = ci / f"obd2_sniffer-{args.tag}-{args.flash}.elf"
        subprocess.run(
            ["openocd", "-f", "board/st_nucleo_f4.cfg", "-c", f"program {elf} verify reset exit"],
            check=True,
        )
        print(f"Flashed {elf.name}: now run scripts/board_check.py --crash.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
