#!/usr/bin/env python3
"""Coverage report and gate for the C unit tests.

Run after `ctest --preset host-coverage`. Merges the raw LLVM profiles, writes
an HTML report (with MC/DC detail per decision), a text summary and a JSON
summary, and exits non-zero if any metric is below the threshold.

Metrics checked: lines, functions, regions, branches and MC/DC.
Only the portable core (lib/) is measured; tests and third-party code are
excluded.

Usage:
    python3 scripts/coverage.py [--build-dir build/host-coverage] [--fail-under 100]
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

METRICS = [
    ("lines", "Lines"),
    ("functions", "Functions"),
    ("regions", "Regions"),
    ("branches", "Branches"),
    ("mcdc", "MC/DC"),
]
IGNORE_REGEX = r"(/tests/|/_deps/)"


def find_tool(name: str) -> str:
    """Locate an LLVM tool that matches the compiler used for the build."""
    env = os.environ.get(name.upper().replace("-", "_"))
    if env:
        return env
    if platform.system() == "Darwin":
        out = subprocess.run(["xcrun", "-f", name], capture_output=True, text=True)
        if out.returncode == 0:
            return out.stdout.strip()
    for version in range(30, 17, -1):
        path = shutil.which(f"{name}-{version}")
        if path:
            return path
    path = shutil.which(name)
    if path:
        return path
    sys.exit(f"error: {name} not found (set {name.upper().replace('-', '_')})")


def run(cmd: list[str], **kwargs) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, check=True, text=True, **kwargs)


def percent(entry: dict) -> float:
    # A metric with nothing to measure (e.g. no multi-condition decisions)
    # counts as fully covered.
    return 100.0 if entry.get("count", 0) == 0 else float(entry["percent"])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build-dir", type=Path, default=Path("build/host-coverage"))
    parser.add_argument("--output", type=Path, help="report directory (default: <build-dir>/report)")
    parser.add_argument("--fail-under", type=float, default=100.0)
    parser.add_argument("--summary-md", type=Path, help="append a Markdown summary (e.g. $GITHUB_STEP_SUMMARY)")
    args = parser.parse_args()

    build = args.build_dir
    out = args.output or build / "report"
    binaries = sorted(p for p in (build / "tests").glob("test_*") if p.is_file() and os.access(p, os.X_OK))
    if not binaries:
        sys.exit(f"error: no test executables in {build / 'tests'}")

    # The profiles folder is never emptied, so it also holds profiles from
    # runs of older test builds. Merging those would mix stale counts into the
    # result (coverage from a deleted test could still pass the gate) or fail
    # outright after a compiler change. Only profiles written after the newest
    # test build are used; at worst that asks for the tests to be run again.
    newest_build = max(p.stat().st_mtime for p in binaries)
    all_profiles = sorted((build / "profiles").glob("*.profraw"))
    profiles = [p for p in all_profiles if p.stat().st_mtime >= newest_build]
    if not profiles:
        sys.exit(f"error: no coverage profiles from the current build in {build / 'profiles'}; "
                 "run `ctest --preset host-coverage` first")
    if len(profiles) < len(all_profiles):
        print(f"note: ignored {len(all_profiles) - len(profiles)} profile(s) from older test builds")

    llvm_profdata = find_tool("llvm-profdata")
    llvm_cov = find_tool("llvm-cov")

    profdata = build / "coverage.profdata"
    run([llvm_profdata, "merge", "-sparse", "-o", str(profdata), *map(str, profiles)])

    objects = [str(binaries[0])]
    for extra in binaries[1:]:
        objects += ["-object", str(extra)]
    common = [f"-instr-profile={profdata}", f"-ignore-filename-regex={IGNORE_REGEX}"]

    html_dir = out / "coverage"
    if html_dir.exists():
        shutil.rmtree(html_dir)
    run([llvm_cov, "show", *objects, *common, "-format=html", f"-output-dir={html_dir}",
         "-show-mcdc", "-show-branches=count", "-show-line-counts-or-regions",
         "-project-title=OBD2 CAN Sniffer: portable core library"])

    text = run([llvm_cov, "report", *objects, *common, "-show-mcdc-summary", "-show-branch-summary"],
               capture_output=True).stdout
    (out / "coverage.txt").write_text(text)

    exported = json.loads(run([llvm_cov, "export", *objects, *common, "-summary-only"],
                              capture_output=True).stdout)
    totals = exported["data"][0]["totals"]

    summary = {}
    failed = []
    for key, label in METRICS:
        entry = totals.get(key, {"count": 0, "covered": 0})
        pct = percent(entry)
        summary[key] = {"label": label, "covered": entry.get("covered", 0),
                        "count": entry.get("count", 0), "percent": pct}
        if pct < args.fail_under:
            failed.append(label)
    summary["threshold"] = args.fail_under
    summary["passed"] = not failed
    (out / "coverage-summary.json").write_text(json.dumps(summary, indent=2))

    rows = [f"| {s['label']} | {s['covered']} / {s['count']} | {s['percent']:.2f}% |"
            for s in (summary[k] for k, _ in METRICS)]
    table = "\n".join(["| Metric | Covered | Percent |", "|---|---|---|", *rows])

    print(text)
    print(table)
    print(f"\nHTML report: {html_dir / 'index.html'}")

    if args.summary_md:
        verdict = "passed" if not failed else "FAILED: " + ", ".join(failed)
        with args.summary_md.open("a") as f:
            f.write(f"### C coverage (lib/), threshold {args.fail_under:g}%: {verdict}\n\n{table}\n\n")

    if failed:
        print(f"\nCoverage below {args.fail_under:g}% for: {', '.join(failed)}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
