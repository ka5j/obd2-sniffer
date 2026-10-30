#!/usr/bin/env python3
"""Check that CI's release files match the builds tested on the board.

Pushing a tag vX.Y.Z makes the Release workflow build both firmware builds
from the tag and attach the files to a draft GitHub Release. This downloads
them and compares each one with the local build of the same tag, the one that
was flashed for the board checks:

- .bin and .hex must be byte-identical: they are what goes onto the board.
- .elf must be identical once debug info is stripped. The debug info records
  the folder each build ran in, so it always differs between machines.
- SHA256SUMS.txt must match the downloaded files.

Needs the GitHub CLI (gh, logged in) and the Arm toolchain's objcopy. Exits
with status 1 on any difference.

Usage:
    python3 scripts/release_check.py vX.Y.Z
"""

from __future__ import annotations

import argparse
import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILDS = {"debug": "firmware", "release": "firmware-release"}  # file name -> preset
OBJCOPY = "arm-none-eabi-objcopy"


def local_version(preset: str) -> str:
    header = ROOT / "build" / preset / "firmware" / "generated" / "version.h"
    for line in header.read_text().splitlines():
        if line.startswith("#define FW_VERSION "):
            return line.split('"')[1]
    raise ValueError(f"no FW_VERSION in {header}")


def without_debug_info(elf: Path, out: Path) -> bytes:
    subprocess.run([OBJCOPY, "--strip-debug", str(elf), str(out)], check=True)
    return out.read_bytes()


def check_sums(folder: Path) -> list[str]:
    sums = folder / "SHA256SUMS.txt"
    if not sums.exists():
        return ["SHA256SUMS.txt: missing from the draft release"]
    problems = []
    for line in sums.read_text().splitlines():
        digest, name = line.split(maxsplit=1)
        actual = hashlib.sha256((folder / name).read_bytes()).hexdigest()
        if actual != digest:
            problems.append(f"{name}: SHA256SUMS.txt does not match the file")
    return problems


def compare(tag: str, ci: Path, scratch: Path) -> list[str]:
    problems = []
    for name, preset in BUILDS.items():
        built = ROOT / "build" / preset / "firmware"
        version = local_version(preset)
        if version != tag:
            problems.append(f"local {preset} build is {version}, not {tag}: build it from the tag")
            continue
        for ext in ("bin", "hex", "elf"):
            remote = ci / f"obd2_sniffer-{tag}-{name}.{ext}"
            local = built / f"obd2_sniffer.{ext}"
            if not remote.exists():
                problems.append(f"{remote.name}: missing from the draft release")
                continue
            if ext == "elf":
                same = without_debug_info(local, scratch / "local.elf") == without_debug_info(
                    remote, scratch / "ci.elf"
                )
                how = "identical without debug info"
            else:
                same = local.read_bytes() == remote.read_bytes()
                how = "byte-identical"
            print(f"{'ok  ' if same else 'DIFF'}  {remote.name}{f': {how}' if same else ''}")
            if not same:
                problems.append(f"{remote.name}: differs from the local {preset} build")
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("tag", help="release tag, e.g. v0.4.2")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        ci = Path(tmp) / "ci"
        subprocess.run(
            ["gh", "release", "download", args.tag, "--dir", str(ci)], check=True, cwd=ROOT
        )
        problems = compare(args.tag, ci, Path(tmp)) + check_sums(ci)

    for problem in problems:
        print(f"FAIL  {problem}")
    print("All release files match the builds tested on the board." if not problems else "")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
