#!/usr/bin/env python3
"""MISRA C:2012 check: run cppcheck's MISRA addon and enforce the project policy.

Every finding in project code must be either fixed, covered by an advisory
rule the project has disapplied, or covered by a documented deviation tagged
at the exact place in the source (see misra/config.toml). Anything else is an
open finding and fails the check. General cppcheck warnings (likely bugs) are
treated the same way.

Usage:
    python3 scripts/misra.py [--out build/misra] [--summary-md FILE]
"""

from __future__ import annotations

import argparse
import fnmatch
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from collections import Counter
from dataclasses import asdict, dataclass
from pathlib import Path

import tomllib

ROOT = Path(__file__).resolve().parent.parent
CONFIG = ROOT / "misra" / "config.toml"

# Mandatory MISRA C:2012 rules (including Amendments 1-2): never deviated.
MANDATORY = {
    "9.1",
    "12.5",
    "13.6",
    "17.3",
    "17.4",
    "17.6",
    "19.1",
    "21.13",
    "21.17",
    "21.18",
    "21.19",
    "21.20",
    "22.2",
    "22.4",
    "22.5",
    "22.6",
}

TAG_RE = re.compile(r"MISRA\s+(DEV-\d+)")


@dataclass
class Finding:
    file: str
    line: int
    rule: str  # "15.5" for MISRA, cppcheck id otherwise
    checker: str  # "misra" or "cppcheck"
    message: str
    status: str = "open"  # open | disapplied | false_positive | deviated
    resolution: str = ""  # disapplied rule or deviation id


def base_command(cfg: dict, cppcheck: str) -> list[str]:
    # The MISRA addon reports at "style" severity: without --enable=style its
    # findings are silently dropped.
    return [
        cppcheck,
        "--addon=misra",
        f"--std={cfg['tool']['std']}",
        f"--platform={cfg['tool']['platform']}",
        "--enable=style,warning,performance,portability",
        "--inline-suppr",
        "--xml",
        "--xml-version=2",
        "--quiet",
    ]


def self_test(cfg: dict, cppcheck: str, workdir: Path) -> bool:
    """Check that the analysis actually reports MISRA findings.

    A misconfigured run (wrong severity filter, broken addon, missing Python)
    produces no output at all, which would look like a clean result. Analyse a
    snippet with known violations first and require that they are found."""
    canary = workdir / "canary.c"
    canary.write_text("int canary(int a) { if (a) { return 1; } return 0; }\n")
    out = workdir / "canary.xml"
    subprocess.run(
        [*base_command(cfg, cppcheck), f"--output-file={out}", str(canary)], check=True, cwd=workdir
    )
    ids = {e.get("id") for e in ET.parse(out).getroot().iter("error")}
    return {"misra-c2012-14.4", "misra-c2012-15.5"} <= ids


def run_cppcheck(cfg: dict, cppcheck: str, workdir: Path) -> Path:
    scope = cfg["scope"]
    stub = workdir / "stub"
    stub.mkdir(parents=True, exist_ok=True)
    # version.h is generated at build time; the analysis only needs the macro.
    (stub / "version.h").write_text('#define FW_VERSION "analysis"\n')

    xml_out = workdir / "cppcheck.xml"
    cmd = [
        *base_command(cfg, cppcheck),
        f"--output-file={xml_out}",
        f"--cppcheck-build-dir={workdir / 'cache'}",
    ]
    (workdir / "cache").mkdir(exist_ok=True)
    cmd += [f"-I{ROOT / p}" for p in scope["include"]] + [f"-I{stub}"]
    cmd += [f"-D{d}" for d in scope["defines"]]
    cmd += [f"-i{ROOT / p}" for p in scope["exclude"]]
    cmd += [str(ROOT / s) for s in scope["sources"]]
    subprocess.run(cmd, check=True, cwd=ROOT)
    return xml_out


def parse_findings(xml_path: Path, cfg: dict) -> list[Finding]:
    findings = []
    excluded = [ROOT / p for p in cfg["scope"]["exclude"]]
    for err in ET.parse(xml_path).getroot().iter("error"):
        loc = err.find("location")
        if loc is None:
            continue  # e.g. "checkersReport": no source location
        path = Path(loc.get("file"))
        path = path if path.is_absolute() else ROOT / path
        if any(str(path).startswith(str(e)) for e in excluded):
            continue
        if not str(path).startswith(str(ROOT)):
            continue  # system headers
        err_id = err.get("id", "")
        if err.get("severity") == "information":
            continue  # tool notices (missing system includes etc.)
        match = re.fullmatch(r"misra-c2012-(\d+\.\d+)", err_id)
        rule, checker = (match.group(1), "misra") if match else (err_id, "cppcheck")
        findings.append(
            Finding(
                str(path.relative_to(ROOT)),
                int(loc.get("line", 0)),
                rule,
                checker,
                err.get("msg", ""),
            )
        )
    # The addon reports some findings more than once (e.g. via macro expansion).
    unique = {(f.file, f.line, f.rule): f for f in findings}
    return sorted(unique.values(), key=lambda f: (f.file, f.line, f.rule))


def validate_config(cfg: dict) -> list[str]:
    problems = []
    ids = [d["id"] for d in cfg.get("deviation", [])]
    if len(ids) != len(set(ids)):
        problems.append("duplicate deviation ids in misra/config.toml")
    for d in cfg.get("deviation", []):
        for rule in d["rules"]:
            if rule in MANDATORY:
                problems.append(f"{d['id']} covers mandatory rule {rule}: not allowed")
    for d in cfg.get("disapplied", []):
        if d["category"] != "advisory":
            problems.append(
                f"rule {d['rule']} is {d['category']}: only advisory rules can be disapplied"
            )
        if d["rule"] in MANDATORY:
            problems.append(f"rule {d['rule']} is mandatory and cannot be disapplied")
    return problems


def source_lines(file: str, cache: dict) -> list[str]:
    if file not in cache:
        cache[file] = (ROOT / file).read_text().splitlines()
    return cache[file]


def source_line(file: str, line: int, cache: dict) -> str:
    lines = source_lines(file, cache)
    return lines[line - 1] if 0 < line <= len(lines) else ""


def is_comment_only(text: str) -> bool:
    stripped = text.strip()
    return stripped.startswith("/*") or stripped.startswith("//")


def source_tags(file: str, line: int, cache: dict) -> set[str]:
    """Deviation tags that apply to `line`: a tag on the same line, or a tag
    on a comment-only line directly above (never a trailing comment of the
    previous statement)."""
    lines = source_lines(file, cache)
    tags = set()
    if 0 < line <= len(lines):
        tags.update(TAG_RE.findall(lines[line - 1]))
    if 1 < line <= len(lines) + 1 and is_comment_only(lines[line - 2]):
        tags.update(TAG_RE.findall(lines[line - 2]))
    return tags


def classify(findings: list[Finding], cfg: dict) -> list[str]:
    deviations = {d["id"]: d for d in cfg.get("deviation", [])}
    problems = []
    cache: dict = {}
    for f in findings:
        if f.checker == "misra":
            for d in cfg.get("disapplied", []):
                if d["rule"] == f.rule and any(fnmatch.fnmatch(f.file, p) for p in d["paths"]):
                    f.status, f.resolution = "disapplied", f"Rule {f.rule}"
                    break
        if f.status == "open" and f.checker == "misra":
            text = source_line(f.file, f.line, cache)
            for fp in cfg.get("false_positive", []):
                if (
                    fp["rule"] == f.rule
                    and any(fnmatch.fnmatch(f.file, p) for p in fp["paths"])
                    and re.search(fp["match"], text)
                ):
                    f.status, f.resolution = "false_positive", fp["id"]
                    break
        if f.status != "open":
            continue
        for tag in sorted(source_tags(f.file, f.line, cache)):
            dev = deviations.get(tag)
            if dev is None:
                problems.append(f"{f.file}:{f.line}: unknown deviation {tag}")
            elif f.checker == "misra" and f.rule in dev["rules"]:
                f.status, f.resolution = "deviated", tag
                break
    return problems


def unused_tags(findings: list[Finding], cfg: dict) -> list[str]:
    """Deviation tags in the sources that no longer cover any finding."""
    deviated = {(f.file, f.line) for f in findings if f.status == "deviated"}
    stale = []
    for src in cfg["scope"]["sources"] + ["lib/include", "firmware/include"]:
        for path in sorted((ROOT / src).rglob("*.[ch]")):
            rel = str(path.relative_to(ROOT))
            for n, text in enumerate(path.read_text().splitlines(), start=1):
                if not TAG_RE.search(text):
                    continue
                covers_next = is_comment_only(text) and (rel, n + 1) in deviated
                if (rel, n) not in deviated and not covers_next:
                    stale.append(f"{rel}:{n}")
    return stale


def addon_rule_count() -> int:
    """Number of MISRA rules/directives the installed addon has a check for."""
    exe = shutil.which("cppcheck")
    if exe is None:
        return 0
    for candidate in Path(exe).resolve().parent.parent.rglob("addons/misra.py"):
        names = re.findall(r"def misra_((?:dir_)?\d+_\d+)\(", candidate.read_text())
        return len(set(names))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", type=Path, default=ROOT / "build" / "misra")
    parser.add_argument("--cppcheck", default="cppcheck")
    parser.add_argument("--summary-md", type=Path)
    args = parser.parse_args()

    cfg = tomllib.loads(CONFIG.read_text())
    version = (
        subprocess.run([args.cppcheck, "--version"], capture_output=True, text=True, check=True)
        .stdout.strip()
        .split()[-1]
    )
    if version != cfg["tool"]["cppcheck_version"]:
        print(
            f"warning: cppcheck {version}, config expects {cfg['tool']['cppcheck_version']}; "
            "results may differ from CI",
            file=sys.stderr,
        )

    args.out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        if not self_test(cfg, args.cppcheck, Path(tmp)):
            print(
                "error: self-test failed: the MISRA addon reported nothing for a file with "
                "known violations, so the analysis cannot be trusted",
                file=sys.stderr,
            )
            return 2
        xml_path = run_cppcheck(cfg, args.cppcheck, Path(tmp))
        shutil.copy(xml_path, args.out / "cppcheck.xml")

    findings = parse_findings(args.out / "cppcheck.xml", cfg)
    problems = validate_config(cfg)
    problems += classify(findings, cfg)
    stale = unused_tags(findings, cfg)

    open_items = [f for f in findings if f.status == "open"]
    counts = Counter(f.status for f in findings)
    files = sorted(
        {str(p.relative_to(ROOT)) for s in cfg["scope"]["sources"] for p in (ROOT / s).rglob("*.c")}
    )

    def rule_rows(kind: str, key: str) -> list[dict]:
        rows = []
        for item in cfg.get(kind, []):
            rules = item["rules"] if "rules" in item else [item["rule"]]
            label = item.get("id", f"Rule {item.get('rule')}")
            n = sum(1 for f in findings if f.status != "open" and f.resolution == label)
            rows.append(
                {
                    key: label,
                    "rules": rules,
                    "category": item.get("category", ""),
                    "title": item["title"],
                    "reason": item["reason"],
                    "mitigation": item.get("mitigation", ""),
                    "where": item.get("where", ""),
                    "paths": item.get("paths", []),
                    "instances": n,
                }
            )
        return rows

    summary = {
        "standard": "MISRA C:2012",
        "tool": f"cppcheck {version} (MISRA addon)",
        "platform": cfg["tool"]["platform"],
        "rules_checked": addon_rule_count(),
        "files": files,
        "passed": not open_items and not problems,
        "counts": {
            "open": counts["open"],
            "deviated": counts["deviated"],
            "disapplied": counts["disapplied"],
            "false_positive": counts["false_positive"],
            "total": len(findings),
        },
        "by_rule": dict(sorted(Counter(f.rule for f in findings).items())),
        "disapplied": rule_rows("disapplied", "rule"),
        "deviations": rule_rows("deviation", "id"),
        "false_positives": rule_rows("false_positive", "id"),
        "problems": problems,
        "stale_tags": stale,
        "findings": [asdict(f) for f in findings],
    }
    (args.out / "misra-summary.json").write_text(json.dumps(summary, indent=2))

    verdict = "PASSED" if summary["passed"] else "FAILED"
    md = [
        f"### MISRA C:2012 ({summary['tool']}): {verdict}",
        "",
        f"{len(files)} source files, {summary['rules_checked']} rules with automated checks.",
        "",
        "| Result | Findings |",
        "|---|---|",
        f"| Open (must be fixed) | {counts['open']} |",
        f"| Covered by a documented deviation | {counts['deviated']} |",
        f"| Advisory rule not applied | {counts['disapplied']} |",
        f"| Documented tool false positive | {counts['false_positive']} |",
        "",
    ]
    if open_items:
        md += (
            ["Open findings:", ""]
            + [f"- `{f.file}:{f.line}` {f.checker} {f.rule}: {f.message}" for f in open_items]
            + [""]
        )
    if problems:
        md += ["Configuration problems:", ""] + [f"- {p}" for p in problems] + [""]
    if stale:
        md += (
            ["Deviation tags that no longer cover a finding (remove them):", ""]
            + [f"- `{s}`" for s in stale]
            + [""]
        )
    text = "\n".join(md)
    print(text)
    if os.environ.get("GITHUB_ACTIONS") == "true":
        # Workflow commands: show open findings as annotations on the PR diff.
        for f in open_items:
            print(
                f"::error file={f.file},line={f.line},title=MISRA {f.rule}::{f.checker} {f.rule}: "
                f"{f.message}"
            )
    if args.summary_md:
        with args.summary_md.open("a") as fh:
            fh.write(text + "\n")
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
