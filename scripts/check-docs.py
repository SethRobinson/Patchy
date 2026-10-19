#!/usr/bin/env python3
"""Checks the agent-facing documentation (AGENTS.md and docs/).

Errors (exit 1):
  - AGENTS.md or a docs/*.md file over the 30,000-byte limit (AGENTS.md "Repository-wide policy").
  - A relative Markdown link to a file that does not exist, or to a #heading that file lacks.
Warnings (exit 0 unless --strict):
  - A docs/*.md file no other checked document links to (unreachable from the index).

Usage: python scripts/check-docs.py [--strict]
Stdlib only; run from anywhere inside the repository.
"""
import re
import sys
from pathlib import Path

LIMIT = 30000
ROOT = Path(__file__).resolve().parent.parent
LINK = re.compile(r"(?<!!)\[[^\]]*\]\(([^)\s]+)\)")
FENCE = re.compile(r"^\s*(```|~~~)")
HEADING = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")


def slug(text: str) -> str:
    """GitHub's heading anchor: lowercase, drop punctuation except '-' and '_', spaces to '-'."""
    text = re.sub(r"`|\*\*|__", "", text).strip().lower()
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", text)
    text = re.sub(r"[^\w\- ]", "", text)
    return text.replace(" ", "-")


def strip_code(lines):
    """Lines outside fenced code blocks, with inline code spans blanked."""
    in_fence = False
    for line in lines:
        if FENCE.match(line):
            in_fence = not in_fence
            continue
        if not in_fence:
            yield re.sub(r"`[^`]*`", "", line)


_anchors = {}


def anchors(path: Path):
    if path not in _anchors:
        found, seen = set(), {}
        for line in strip_code(path.read_text(encoding="utf-8").splitlines()):
            m = HEADING.match(line)
            if m:
                base = slug(m.group(2))
                n = seen.get(base, 0)
                seen[base] = n + 1
                found.add(base if n == 0 else f"{base}-{n}")
        _anchors[path] = found
    return _anchors[path]


def main() -> int:
    strict = "--strict" in sys.argv[1:]
    docs = sorted((ROOT / "docs").glob("*.md"))
    checked = [ROOT / "AGENTS.md", ROOT / "README.md"] + docs
    errors, warnings, linked = [], [], set()

    for path in [ROOT / "AGENTS.md"] + docs:
        size = path.stat().st_size
        if size > LIMIT:
            errors.append(f"{path.relative_to(ROOT)}: {size} bytes, over the {LIMIT}-byte limit")

    for path in checked:
        if not path.exists():
            continue
        text = path.read_text(encoding="utf-8")
        for line in strip_code(text.splitlines()):
            for target in LINK.findall(line):
                if re.match(r"^[a-z][a-z0-9+.-]*:", target, re.I):
                    continue  # http:, https:, mailto: and the like
                file_part, _, anchor = target.partition("#")
                dest = (path.parent / file_part).resolve() if file_part else path
                where = f"{path.relative_to(ROOT)}: link '{target}'"
                if not dest.exists():
                    errors.append(f"{where}: file not found")
                    continue
                if dest != path:
                    linked.add(dest)
                if anchor and dest.suffix == ".md" and anchor.lower() not in anchors(dest):
                    errors.append(f"{where}: no heading '#{anchor}' in {dest.relative_to(ROOT)}")

    for doc in docs:
        if doc.resolve() not in linked:
            warnings.append(f"{doc.relative_to(ROOT)}: no checked document links to it")

    for w in warnings:
        print(f"warning: {w}")
    for e in errors:
        print(f"error: {e}")
    print(f"[SUMMARY] {len(checked)} files checked, {len(errors)} errors, {len(warnings)} warnings")
    return 1 if errors or (strict and warnings) else 0


if __name__ == "__main__":
    sys.exit(main())
