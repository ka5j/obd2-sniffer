#!/usr/bin/env python3
"""Check that every relative link and section link in the documents resolves.

Links to websites are not fetched: this only catches what the repo itself can
break, such as a moved file or a renamed heading. Exits with status 1 if any
link is broken.

Usage:
    python3 scripts/check_links.py
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DOCUMENTS = [
    "README.md",
    "host/README.md",
    "docs/*.md",
    "docs/design/*.md",
    "hardware/*.md",
    "hardware/*/*.md",
]
CODE_BLOCK = re.compile(r"^```.*?^```", re.DOTALL | re.MULTILINE)
INLINE_CODE = re.compile(r"`[^`\n]*`")
LINK = re.compile(r"\]\(([^)\s]+)\)")
HEADING = re.compile(r"^#{1,6}\s+(.*)$", re.MULTILINE)


def anchors(path: Path) -> set[str]:
    """The section links GitHub makes from a document's headings."""
    text = CODE_BLOCK.sub("", path.read_text())
    found = set()
    for heading in HEADING.findall(text):
        cleaned = re.sub(r"[^\w\- ]", "", heading.strip().lower())
        found.add(cleaned.replace(" ", "-"))
    return found


def broken_links(path: Path) -> list[str]:
    text = INLINE_CODE.sub("", CODE_BLOCK.sub("", path.read_text()))
    problems = []
    for target in LINK.findall(text):
        if target.startswith(("http://", "https://", "mailto:")):
            continue
        file_part, _, anchor = target.partition("#")
        linked = (path.parent / file_part).resolve() if file_part else path
        if not linked.exists():
            problems.append(f"{target}: no such file")
        elif anchor and linked.suffix == ".md" and anchor not in anchors(linked):
            problems.append(f"{target}: no such section")
    return problems


def main() -> int:
    paths = [path for pattern in DOCUMENTS for path in sorted(ROOT.glob(pattern))]
    failed = 0
    for path in paths:
        for problem in broken_links(path):
            print(f"{path.relative_to(ROOT)}: {problem}")
            failed += 1
    print(f"{len(paths)} documents: {failed} broken links.")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
