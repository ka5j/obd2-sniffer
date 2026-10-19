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
import sys
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
th, td { text-align:left; padding:8px 10px; border-bottom:1px solid var(--border);
         vertical-align:top; }
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
    (
        "MC/DC",
        "Modified Condition/Decision Coverage, the standard used for safety-critical "
        "aviation and automotive software: for a decision like `if (a || b)`, each "
        "condition was shown, on its own, to change the outcome. Measured for the C code; "
        "Python has no MC/DC tooling, so the laptop tool is held to 100% line and branch "
        "coverage instead.",
    ),
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
        suite.cases.append(
            Case(group, case, float(tc.get("time", 0) or 0), status, message.strip())
        )
    return suite


def page(title: str, body: str) -> str:
    return (
        f'<!doctype html><html lang="en"><head><meta charset="utf-8">'
        f'<meta name="viewport" content="width=device-width,initial-scale=1">'
        f"<title>{html.escape(title)}</title><style>{STYLE}</style></head>"
        f"<body><main>{body}</main></body></html>"
    )


def misra_page(summary: dict, meta: str, repo: str) -> str:
    """Compliance page for the MISRA C:2012 check (scripts/misra.py output)."""
    counts = summary["counts"]
    blob = f"https://github.com/{repo}/blob/{os.environ.get('GITHUB_SHA', 'develop')}"

    def locations(resolution: str) -> str:
        refs = [f for f in summary["findings"] if f["resolution"] == resolution]
        if not refs:
            return '<span class="muted">none</span>'
        return ", ".join(
            f'<a href="{blob}/{html.escape(f["file"])}#L{f["line"]}"><code>'
            f"{html.escape(f['file'])}:{f['line']}</code></a> (Rule {html.escape(f['rule'])})"
            for f in refs
        )

    verdict = (
        '<span class="pass">Compliant: no open findings</span>'
        if summary["passed"]
        else f'<span class="fail">{counts["open"]} open '
        f"finding{'' if counts['open'] == 1 else 's'}</span>"
    )
    result_rows = "".join(
        f'<tr><td>{label}</td><td class="num">{counts[key]}</td></tr>'
        for key, label in [
            ("open", "Open: must be fixed before merging"),
            ("deviated", "Covered by a documented deviation"),
            ("disapplied", "Advisory rule not applied, with a recorded reason"),
            ("false_positive", "Tool false positive, with evidence"),
        ]
    )

    def describe(resolution: str) -> str:
        if not resolution:
            return "open"
        if resolution.startswith("Rule "):
            return "advisory rule not applied"
        return resolution

    resolution_of = {}
    for f in summary["findings"]:
        resolution_of.setdefault(f["rule"], set()).add(describe(f["resolution"]))
    rule_rows = "".join(
        f'<tr><td>{html.escape(rule)}</td><td class="num">{n}</td>'
        f"<td>{html.escape(', '.join(sorted(resolution_of.get(rule, []))))}</td></tr>"
        for rule, n in sorted(
            summary["by_rule"].items(),
            key=lambda kv: [int(x) if x.isdigit() else x for x in kv[0].split(".")],
        )
    )

    def items(rows: list[dict], key: str, with_mitigation: bool) -> str:
        out = []
        for r in rows:
            rules = ", ".join(r["rules"])
            category = f" ({html.escape(r['category'])})" if r["category"] else ""
            scope = (
                f"<dd><b>Applies to:</b> <code>{html.escape(', '.join(r['paths']))}</code></dd>"
                if r.get("paths")
                else ""
            )
            mitigation = (
                f"<dd><b>Risk control:</b> {html.escape(r['mitigation'])}</dd>"
                if with_mitigation and r.get("mitigation")
                else ""
            )
            where = (
                f"<dd><b>Locations ({r['instances']}):</b> {locations(r[key])}</dd>"
                if key == "id"
                else f"<dd><b>Findings covered:</b> {r['instances']}</dd>"
            )
            if r.get("where"):
                where += f"<dd><b>Applies to:</b> <code>{html.escape(r['where'])}</code></dd>"
            head = (
                f"{html.escape(r[key])}: Rule {html.escape(rules)}"
                if key == "id"
                else html.escape(r[key])
            )
            out.append(
                f"<dt>{head}{category}: "
                f"{html.escape(r['title'])}</dt><dd>{html.escape(r['reason'])}</dd>"
                f"{mitigation}{scope}{where}"
            )
        return "<dl>" + "".join(out) + "</dl>" if out else '<p class="muted">None.</p>'

    files = "".join(f"<li><code>{html.escape(f)}</code></li>" for f in summary["files"])
    return page(
        "MISRA C:2012 compliance",
        (
            '<p><a href="index.html">Back to report</a></p><h1>MISRA C:2012 compliance</h1>'
            f'<p class="meta">{meta}</p>'
            "<p><b>MISRA C</b> is a set of coding rules for C, written for the automotive industry "
            "and now used across safety-critical software. The rules ban constructs that are easy "
            "to get wrong or behave differently between compilers, such as unchecked type "
            "conversions, hidden side effects and unclear operator precedence. Each rule is "
            "<i>mandatory</i> "
            "(never broken), <i>required</i> (broken only with a documented, reviewed deviation) "
            "or <i>advisory</i> (a project may choose not to apply it, with a recorded reason).</p>"
            f"<h2>Result: {verdict}</h2>"
            f"<p>Checked with {html.escape(summary['tool'])}, which has automated checks for "
            f"{summary['rules_checked']} rules, analysing for the target's 32-bit Arm type sizes. "
            "Every change is checked in CI; any finding that is not fixed or covered below blocks "
            "the merge. Rules that no tool can decide on its own are covered by code review.</p>"
            '<div class="table-wrap"><table><tr><th>Findings</th><th>Count</th></tr>'
            f"{result_rows}</table></div>"
            "<h2>Deviations from required rules</h2>"
            "<p>Places where a required rule is broken on purpose. Each one is tagged in the "
            "source (<code>/* MISRA DEV-xx */</code>) and only covers the rules listed for it.</p>"
            + items(summary["deviations"], "id", True)
            + "<h2>Advisory rules not applied</h2>"
            + items(summary["disapplied"], "rule", False)
            + "<h2>Tool false positives</h2>"
            + items(summary.get("false_positives", []), "id", False)
            + '<h2>Findings by rule</h2><div class="table-wrap"><table>'
            "<tr><th>Rule</th><th>Findings</th>"
            f"<th>Resolved by</th></tr>{rule_rows}</table></div>"
            "<h2>Files analysed</h2>"
            "<p>Project code only; ST's HAL and CMSIS (third-party) are excluded.</p>"
            f"<ul>{files}</ul>"
        ),
    )


def readable(case_name: str) -> str:
    return re.sub(r"^test_", "", case_name).replace("_", " ")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", type=Path, default=Path("site"))
    parser.add_argument("--junit", action="append", default=[], metavar="LABEL=FILE")
    parser.add_argument(
        "--coverage",
        action="append",
        default=[],
        metavar="LABEL=DIR",
        help="directory with coverage-summary.json and a coverage/ HTML report",
    )
    parser.add_argument(
        "--misra",
        type=Path,
        metavar="DIR",
        help="directory with misra-summary.json from scripts/misra.py",
    )
    args = parser.parse_args()

    if args.out.exists():
        shutil.rmtree(args.out)
    args.out.mkdir(parents=True)

    # Inputs from a failed job may be missing; build what is available.
    suites = []
    for item in args.junit:
        label, _, path = item.partition("=")
        if not Path(path).exists():
            print(f"warning: {path} not found, skipping", file=sys.stderr)
            continue
        suites.append(parse_junit(label, Path(path)))

    coverages = []
    for i, item in enumerate(args.coverage):
        label, _, path = item.partition("=")
        src = Path(path)
        if not (src / "coverage-summary.json").exists():
            print(f"warning: {src} has no coverage summary, skipping", file=sys.stderr)
            continue
        summary = json.loads((src / "coverage-summary.json").read_text())
        dest = f"coverage-{i}"
        shutil.copytree(src / "coverage", args.out / dest)
        coverages.append((label, summary, dest))

    sha = os.environ.get("GITHUB_SHA", "")[:7] or "local"
    ref = os.environ.get("GITHUB_REF_NAME", "")
    repo = os.environ.get("GITHUB_REPOSITORY", "ka5j/obd2-sniffer")
    when = dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    commit_link = (
        f'<a href="https://github.com/{repo}/commit/{os.environ["GITHUB_SHA"]}">{sha}</a>'
        if os.environ.get("GITHUB_SHA")
        else sha
    )
    meta = f"Generated {when} from commit {commit_link}" + (
        f" on <code>{html.escape(ref)}</code>" if ref else ""
    )

    total = sum(len(s.cases) for s in suites)
    passed = sum(s.count("passed") for s in suites)
    failed = sum(s.count("failed") for s in suites)

    # ---- tests page ----
    sections = []
    for s in suites:
        rows = []
        for c in s.cases:
            cls = "pass" if c.status == "passed" else ("fail" if c.status == "failed" else "muted")
            detail = (
                f'<br><span class="muted">{html.escape(c.message[:300])}</span>'
                if c.message
                else ""
            )
            rows.append(
                f"<tr><td>{html.escape(c.suite)}</td><td>{html.escape(readable(c.name))}"
                f'<br><code class="muted">{html.escape(c.name)}</code>{detail}</td>'
                f'<td class="{cls}">{c.status}</td><td class="num">{c.seconds:.3f} s</td></tr>'
            )
        sections.append(
            f"<h2>{html.escape(s.label)}: {s.count('passed')} of {len(s.cases)} passed</h2>"
            '<div class="table-wrap"><table>'
            "<tr><th>Module</th><th>Test</th><th>Result</th><th>Time</th></tr>"
            f"{''.join(rows)}</table></div>"
        )
    (args.out / "tests.html").write_text(
        page(
            "Test results",
            (
                '<p><a href="index.html">Back to report</a></p><h1>Test results</h1>'
                f'<p class="meta">{meta}</p>' + "".join(sections)
            ),
        )
    )

    # ---- index page ----
    cards = [
        f'<div class="card"><div class="muted">Tests passed</div>'
        f'<div class="value {"pass" if failed == 0 else "fail"}">{passed} / {total}</div></div>'
    ]
    cov_sections = []
    for label, summary, dest in coverages:
        # Headline the strongest measure available: MC/DC for C, branch
        # coverage for Python (which has no MC/DC tooling).
        headline = "mcdc" if "mcdc" in summary else "branches"
        name = "MC/DC" if headline == "mcdc" else "branch"
        pct = summary[headline]["percent"]
        cards.append(
            f'<div class="card"><div class="muted">{html.escape(label)}: {name} coverage</div>'
            f'<div class="value {"pass" if summary.get("passed") else "fail"}">'
            f"{pct:.0f}%</div></div>"
        )
        rows = "".join(
            f"<tr><td>{html.escape(v['label'])}</td>"
            f'<td class="num">{v["covered"]} / {v["count"]}</td>'
            f'<td class="num {"pass" if v["percent"] >= summary["threshold"] else "fail"}">'
            f"{v['percent']:.1f}%</td></tr>"
            for k, v in summary.items()
            if isinstance(v, dict)
        )
        cov_sections.append(
            f"<h2>Code coverage: {html.escape(label)}</h2>"
            f"<p>Required: {summary['threshold']:g}% on every measure. "
            f'<a href="{dest}/index.html">Open the line-by-line coverage report</a>.</p>'
            '<div class="table-wrap"><table>'
            f"<tr><th>Measure</th><th>Covered</th><th>Percent</th></tr>{rows}</table></div>"
        )

    misra_section = ""
    misra_file = args.misra / "misra-summary.json" if args.misra else None
    if misra_file is not None and misra_file.exists():
        misra = json.loads(misra_file.read_text())
        (args.out / "misra.html").write_text(misra_page(misra, meta, repo))
        open_n = misra["counts"]["open"]
        cards.append(
            f'<div class="card"><div class="muted">MISRA C:2012 open findings</div>'
            f'<div class="value {"pass" if misra["passed"] else "fail"}">{open_n}</div></div>'
        )
        misra_section = (
            "<h2>Coding standard: MISRA C:2012</h2>"
            "<p>All project C code is checked against MISRA C, the coding rules used for "
            f"automotive and other safety-critical software. Open findings: {open_n}. "
            f"{misra['counts']['deviated']} findings are covered by "
            f"{len(misra['deviations'])} documented deviations. "
            '<a href="misra.html">See the full compliance report</a>.</p>'
        )
    elif args.misra:
        print(f"warning: {misra_file} not found, skipping MISRA section", file=sys.stderr)

    help_items = "".join(
        f"<dt>{html.escape(k)}</dt><dd>{html.escape(v)}</dd>" for k, v in COVERAGE_HELP
    )
    body = (
        "<h1>OBD2 CAN Sniffer: quality report</h1>"
        f'<p class="meta">{meta}. <a href="https://github.com/{repo}">Source on GitHub</a></p>'
        "<p>This page is rebuilt automatically every time code is merged. It shows the results of "
        "the automated tests, how much of the code those tests exercise, and compliance with the "
        "MISRA C coding standard.</p>"
        f'<div class="cards">{"".join(cards)}</div>'
        f"<h2>Tests</h2><p>{passed} of {total} automated tests passed. "
        '<a href="tests.html">See every test and its result</a>.</p>'
        + "".join(cov_sections)
        + misra_section
        + f"<h2>What the coverage measures mean</h2><dl>{help_items}</dl>"
        '<p class="muted">C coverage is measured on the hardware-independent firmware code, which '
        "runs on a normal computer. Code that drives the microcontroller's hardware directly is "
        "tested on the board itself.</p>"
    )
    (args.out / "index.html").write_text(page("OBD2 CAN Sniffer: quality report", body))
    (args.out / ".nojekyll").write_text("")
    print(f"Report site written to {args.out / 'index.html'}")


if __name__ == "__main__":
    main()
