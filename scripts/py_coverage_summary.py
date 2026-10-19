#!/usr/bin/env python3
"""Convert coverage.py results into the report site's coverage format.

Reads the JSON written by `coverage json` and the HTML directory written by
`coverage html`, and produces <out>/coverage-summary.json (same schema as
scripts/coverage.py) plus <out>/coverage/ for scripts/build_report_site.py.

Usage:
    python3 scripts/py_coverage_summary.py --json host/coverage.json \
        --html host/htmlcov --out build/python-coverage
"""

from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--json", type=Path, required=True, help="output of `coverage json`")
    parser.add_argument("--html", type=Path, required=True, help="output of `coverage html`")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--fail-under", type=float, default=100.0)
    args = parser.parse_args()

    totals = json.loads(args.json.read_text())["totals"]

    def metric(label: str, covered: int, count: int) -> dict:
        percent = 100.0 if count == 0 else 100.0 * covered / count
        return {"label": label, "covered": covered, "count": count, "percent": percent}

    summary = {
        "lines": metric("Lines", totals["covered_lines"], totals["num_statements"]),
        "branches": metric("Branches", totals["covered_branches"], totals["num_branches"]),
        "threshold": args.fail_under,
    }
    summary["passed"] = all(
        summary[key]["percent"] >= args.fail_under for key in ("lines", "branches")
    )

    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "coverage-summary.json").write_text(json.dumps(summary, indent=2))
    if (args.out / "coverage").exists():
        shutil.rmtree(args.out / "coverage")
    shutil.copytree(args.html, args.out / "coverage")
    print(f"Python coverage: lines {summary['lines']['percent']:.1f}%, "
          f"branches {summary['branches']['percent']:.1f}%")


if __name__ == "__main__":
    main()
