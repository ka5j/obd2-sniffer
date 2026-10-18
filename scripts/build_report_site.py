#!/usr/bin/env python3
"""Build the static test and coverage report site (published to GitHub Pages).

Inputs are JUnit XML test results and coverage report directories; the output
is a self-contained folder with an index page, a test results page and the
full coverage reports.

Usage:
    python3 scripts/build_report_site.py --out site \
        --junit "C unit tests=build/host-coverage/junit-c.xml" \
        --coverage "C core library=build/host-coverage/report"
"""

from __future__ import annotations

import argparse
import datetime as dt
import html
import json
import os
import re
import shutil
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path

STYLE = """
:root { --bg:#ffffff; --fg:#1f2328; --muted:#59636e; --card:#f6f8fa; --border:#d1d9e0;
        --pass:#1a7f37; --fail:#cf222e; --accent:#0969da; }
@media (prefers-color-scheme: dark) {
  :root { --bg:#0d1117; --fg:#e6edf3; --muted:#9198a1; --card:#151b23; --border:#3d444d;
          --pass:#3fb950; --fail:#f85149; --accent:#4493f8; } }
* { box-sizing: border-box; }
body { margin:0; background:var(--bg); color:var(--fg);
       font:16px/1.55 -apple-system,BlinkMacSystemFont,"Segoe UI",Helvetica,Arial,sans-serif; }
main { max-width: 960px; margin: 0 auto; padding: 32px 16px 64px; }
h1 { font-size: 1.8rem; margin: 0 0 4px; }
h2 { font-size: 1.25rem; margin: 36px 0 12px; }
p.meta, .muted { color: var(--muted); font-size: .9rem; }
a { color: var(--accent); }
.cards { display:grid; grid-template-columns:repeat(auto-fit,minmax(200px,1fr)); gap:12px; }
.card { background:var(--card); border:1px solid var(--border); border-radius:8px; padding:16px; }
.card .value { font-size:1.6rem; font-weight:600; }
.pass { color: var(--pass); } .fail { color: var(--fail); }
.table-wrap { overflow-x:auto; }
table { width:100%; border-collapse:collapse; font-size:.95rem; }
th, td { text-align:left; padding:8px 10px; border-bottom:1px solid var(--border); vertical-align:top; }
th { background:var(--card); }
td.num { font-variant-numeric: tabular-nums; white-space: nowrap; }
code { font-family: ui-monospace,SFMono-Regular,Menlo,monospace; font-size:.88em; }
dl dt { font-weight:600; margin-top:10px; } dl dd { margin:2px 0 0 0; color:var(--muted); }
"""

COVERAGE_HELP = [
    ("Lines", "Every line of code was run by at least one test."),
    ("Functions", "Every function was called by at least one test."),
    ("Regions", "Every block of code, including both sides of short expressions, was run."),
    ("Branches", "Every decision (if/else, loop condition) went both ways in some test."),
    ("MC/DC", "Modified Condition/Decision Coverage, the standard used for safety-critical "
              "aviation and automotive software: for a decision like `if (a || b)`, each "
              "condition was shown, on its own, to change the outcome."),
]


@dataclass
class Case:
    suite: str
    name: str
    seconds: float
    status: str  # passed | failed | skipped
    message: str = ""


@dataclass
class Suite:
    label: str
    cases: list[Case] = field(default_factory=list)

    def count(self, status: str) -> int:
        return sum(1 for c in self.cases if c.status == status)


def parse_junit(label: str, path: Path) -> Suite:
    suite = Suite(label)
    root = ET.parse(path).getroot()
    for tc in root.iter("testcase"):
        status = "passed"
        message = ""
        for tag in ("failure", "error"):
            node = tc.find(tag)
            if node is not None:
                status = "failed"
                message = node.get("message") or (node.text or "")
        if tc.find("skipped") is not None and status == "passed":
            status = "skipped"
            message = tc.find("skipped").get("message") or ""
        name = tc.get("name", "")
        classname = tc.get("classname", "")
        # CTest writes "<suite>.<case>" into both name and classname; pytest
        # puts the module path in classname and the test function in name.
        if classname == name and "." in name:
            group, _, case = name.partition(".")
        else:
            group, case = classname or label, name
        suite.cases.append(Case(group, case, float(tc.get("time", 0) or 0), status, message.strip()))
    return suite


def page(title: str, body: str) -> str:
    return (f"<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
            f"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
            f"<title>{html.escape(title)}</title><style>{STYLE}</style></head>"
            f"<body><main>{body}</main></body></html>")


def readable(case_name: str) -> str:
    return re.sub(r"^test_", "", case_name).replace("_", " ")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", type=Path, default=Path("site"))
    parser.add_argument("--junit", action="append", default=[], metavar="LABEL=FILE")
    parser.add_argument("--coverage", action="append", default=[], metavar="LABEL=DIR",
                        help="directory with coverage-summary.json and a coverage/ HTML report")
    args = parser.parse_args()

    if args.out.exists():
        shutil.rmtree(args.out)
    args.out.mkdir(parents=True)

    suites = []
    for item in args.junit:
        label, _, path = item.partition("=")
        suites.append(parse_junit(label, Path(path)))

    coverages = []
    for i, item in enumerate(args.coverage):
        label, _, path = item.partition("=")
        src = Path(path)
        summary = json.loads((src / "coverage-summary.json").read_text())
        dest = f"coverage-{i}"
        shutil.copytree(src / "coverage", args.out / dest)
        coverages.append((label, summary, dest))

    sha = os.environ.get("GITHUB_SHA", "")[:7] or "local"
    ref = os.environ.get("GITHUB_REF_NAME", "")
    repo = os.environ.get("GITHUB_REPOSITORY", "ka5j/obd2-sniffer")
    when = dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    commit_link = (f'<a href="https://github.com/{repo}/commit/{os.environ["GITHUB_SHA"]}">{sha}</a>'
                   if os.environ.get("GITHUB_SHA") else sha)
    meta = f"Generated {when} from commit {commit_link}" + (f" on <code>{html.escape(ref)}</code>" if ref else "")

    total = sum(len(s.cases) for s in suites)
    passed = sum(s.count("passed") for s in suites)
    failed = sum(s.count("failed") for s in suites)

    # ---- tests page ----
    sections = []
    for s in suites:
        rows = []
        for c in s.cases:
            cls = "pass" if c.status == "passed" else ("fail" if c.status == "failed" else "muted")
            detail = f"<br><span class=\"muted\">{html.escape(c.message[:300])}</span>" if c.message else ""
            rows.append(f"<tr><td>{html.escape(c.suite)}</td><td>{html.escape(readable(c.name))}"
                        f"<br><code class=\"muted\">{html.escape(c.name)}</code>{detail}</td>"
                        f"<td class=\"{cls}\">{c.status}</td><td class=\"num\">{c.seconds:.3f} s</td></tr>")
        sections.append(
            f"<h2>{html.escape(s.label)}: {s.count('passed')} of {len(s.cases)} passed</h2>"
            f"<div class=\"table-wrap\"><table><tr><th>Module</th><th>Test</th><th>Result</th><th>Time</th></tr>"
            f"{''.join(rows)}</table></div>")
    (args.out / "tests.html").write_text(page("Test results", (
        f"<p><a href=\"index.html\">Back to report</a></p><h1>Test results</h1><p class=\"meta\">{meta}</p>"
        + "".join(sections))))

    # ---- index page ----
    cards = [f"<div class=\"card\"><div class=\"muted\">Tests passed</div>"
             f"<div class=\"value {'pass' if failed == 0 else 'fail'}\">{passed} / {total}</div></div>"]
    cov_sections = []
    for label, summary, dest in coverages:
        mcdc = summary.get("mcdc", {}).get("percent", 0.0)
        cards.append(f"<div class=\"card\"><div class=\"muted\">{html.escape(label)}: MC/DC coverage</div>"
                     f"<div class=\"value {'pass' if summary.get('passed') else 'fail'}\">{mcdc:.0f}%</div></div>")
        rows = "".join(
            f"<tr><td>{html.escape(v['label'])}</td><td class=\"num\">{v['covered']} / {v['count']}</td>"
            f"<td class=\"num {'pass' if v['percent'] >= summary['threshold'] else 'fail'}\">{v['percent']:.1f}%</td></tr>"
            for k, v in summary.items() if isinstance(v, dict))
        cov_sections.append(
            f"<h2>Code coverage: {html.escape(label)}</h2>"
            f"<p>Required: {summary['threshold']:g}% on every measure. "
            f"<a href=\"{dest}/index.html\">Open the line-by-line coverage report</a>.</p>"
            f"<div class=\"table-wrap\"><table><tr><th>Measure</th><th>Covered</th><th>Percent</th></tr>{rows}</table></div>")

    help_items = "".join(f"<dt>{html.escape(k)}</dt><dd>{html.escape(v)}</dd>" for k, v in COVERAGE_HELP)
    body = (
        "<h1>OBD2 CAN Sniffer: quality report</h1>"
        f"<p class=\"meta\">{meta}. <a href=\"https://github.com/{repo}\">Source on GitHub</a></p>"
        "<p>This page is rebuilt automatically every time code is merged. It shows the results of the "
        "automated tests and how much of the code those tests exercise.</p>"
        f"<div class=\"cards\">{''.join(cards)}</div>"
        f"<h2>Tests</h2><p>{passed} of {total} automated tests passed. "
        "<a href=\"tests.html\">See every test and its result</a>.</p>"
        + "".join(cov_sections)
        + f"<h2>What the coverage measures mean</h2><dl>{help_items}</dl>"
        "<p class=\"muted\">Coverage is measured on the hardware-independent code, which runs on a "
        "normal computer. Code that drives the microcontroller's hardware directly is tested on "
        "the board itself.</p>")
    (args.out / "index.html").write_text(page("OBD2 CAN Sniffer: quality report", body))
    (args.out / ".nojekyll").write_text("")
    print(f"Report site written to {args.out / 'index.html'}")


if __name__ == "__main__":
    main()
