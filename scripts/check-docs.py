#!/usr/bin/env python3
"""Checks the agent-facing documentation (AGENTS.md and docs/) and the root markdown files.

Errors (exit 1):
  - AGENTS.md or a docs/*.md file over the 30,000-byte limit (AGENTS.md "Repository-wide policy").
  - A relative Markdown link to a file that does not exist, or to a #heading that file lacks.
  - An image reference whose file does not exist.
  - A code or doc citation of the form `docs/<name>.md "Label"` (or `("Label")`) whose label is
    no longer in that document, as a heading, a bold label or verbatim text. Code comments cite
    docs this way, so a split or rename must keep the label or update the citation.
Warnings (exit 0 unless --strict):
  - A docs/*.md file no other checked document links to (unreachable from the index).
  - AGENTS.md or a docs/*.md file within 1,000 bytes of the limit (the next edit may break it).
  - A file under docs/images/ that no checked document references.

Usage: python scripts/check-docs.py [--strict]
Stdlib only; run from anywhere inside the repository.
"""
import os
import re
import sys
from pathlib import Path

LIMIT = 30000
NEAR_LIMIT = LIMIT - 1000
ROOT = Path(__file__).resolve().parent.parent
LINK = re.compile(r"(?<!!)\[[^\]]*\]\(([^)\s]+)\)")
IMAGE = re.compile(r"!\[[^\]]*\]\(([^)\s]+)\)")
FENCE = re.compile(r"^\s*(```|~~~)")
HEADING = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")
# docs/name.md "Label", docs/name.md ("Label"), name.md ("Label"), name.md, "Label"
CITATION = re.compile(r"(?:docs/)?([a-z0-9][a-z0-9-]*\.md)[,:]?\s{0,2}\(?\s?[\"“]([^\"”\n]{2,120})[\"”]")
CODE_DIRS = ("src", "tests", "scripts", "cmake", "packaging")
CODE_SUFFIXES = (".cpp", ".hpp", ".h", ".mm", ".c", ".py", ".ps1", ".sh", ".bat", ".js", ".mjs", ".cmake", ".txt", ".md", ".in")
VENDORED = {"bc7enc", "bcdec", "dav1d", "libaom", "libavif", "libheif", "libjxl", "libraw", "libwebp",
            "miniz", "openjpeg", "stb", "tinyexr", "zstd", "gmic", "lcms2", "bundled"}
IMAGE_SUFFIXES = (".png", ".jpg", ".jpeg", ".gif", ".svg", ".webp")


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
_labels = {}


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


def has_label(path: Path, label: str) -> bool:
    """A cited label is a heading, a bold label, or verbatim text of the document."""
    if path not in _labels:
        text = path.read_text(encoding="utf-8")
        headings = set()
        for line in text.splitlines():
            m = HEADING.match(line)
            if m:
                headings.add(re.sub(r"`|\*\*", "", m.group(2)).strip())
        _labels[path] = (text, headings)
    text, headings = _labels[path]
    plain = label.strip()
    return any(h == plain or h.startswith(plain) for h in headings) or plain in text


def iter_code_files():
    for base in CODE_DIRS:
        for dirpath, dirnames, filenames in os.walk(ROOT / base):
            dirnames[:] = [d for d in dirnames if d not in VENDORED and not d.startswith(".")]
            for name in filenames:
                if name.endswith(CODE_SUFFIXES):
                    yield Path(dirpath) / name
    yield ROOT / "CMakeLists.txt"


def main() -> int:
    strict = "--strict" in sys.argv[1:]
    docs = sorted((ROOT / "docs").glob("*.md"))
    root_docs = [ROOT / "AGENTS.md", ROOT / "README.md", ROOT / "RELEASE-HISTORY.md", ROOT / "NOTICE-THIRD-PARTY.md"]
    checked = [p for p in root_docs if p.exists()] + docs
    errors, warnings, linked, images_used = [], [], set(), set()

    for path in [ROOT / "AGENTS.md"] + docs:
        size = path.stat().st_size
        if size > LIMIT:
            errors.append(f"{path.relative_to(ROOT)}: {size} bytes, over the {LIMIT}-byte limit")
        elif size > NEAR_LIMIT:
            warnings.append(f"{path.relative_to(ROOT)}: {size} bytes, within {LIMIT - size} of the {LIMIT}-byte limit")

    for path in checked:
        text = path.read_text(encoding="utf-8")
        for line in strip_code(text.splitlines()):
            for target in IMAGE.findall(line):
                if re.match(r"^[a-z][a-z0-9+.-]*:", target, re.I):
                    continue
                dest = (path.parent / target).resolve()
                if not dest.exists():
                    errors.append(f"{path.relative_to(ROOT)}: image '{target}': file not found")
                images_used.add(dest)
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
                if dest.suffix.lower() in IMAGE_SUFFIXES:
                    images_used.add(dest)
                if anchor and dest.suffix == ".md" and anchor.lower() not in anchors(dest):
                    errors.append(f"{where}: no heading '#{anchor}' in {dest.relative_to(ROOT)}")
        # HTML image tags in README-style pages
        for target in re.findall(r"<img[^>]+src=\"([^\"]+)\"", text):
            if not re.match(r"^[a-z][a-z0-9+.-]*:", target, re.I):
                images_used.add((path.parent / target).resolve())

    for doc in docs:
        if doc.resolve() not in linked:
            warnings.append(f"{doc.relative_to(ROOT)}: no checked document links to it")

    # Citations of a document label from code, scripts, CMake and other docs.
    doc_names = {d.name: d for d in docs}
    for path in list(iter_code_files()) + checked:
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for m in CITATION.finditer(text):
            name, label = m.group(1), m.group(2)
            dest = doc_names.get(name)
            if dest is None or dest == path:
                continue
            if not has_label(dest, label):
                line_no = text.count("\n", 0, m.start()) + 1
                errors.append(f"{path.relative_to(ROOT)}:{line_no}: cites docs/{name} \"{label}\", which has no such heading, label or text")

    images_dir = ROOT / "docs" / "images"
    if images_dir.exists():
        for image in sorted(images_dir.rglob("*")):
            if image.is_file() and image.resolve() not in images_used:
                warnings.append(f"{image.relative_to(ROOT)}: no checked document references it")

    for w in warnings:
        print(f"warning: {w}")
    for e in errors:
        print(f"error: {e}")
    print(f"[SUMMARY] {len(checked)} files checked, {len(errors)} errors, {len(warnings)} warnings")
    return 1 if errors or (strict and warnings) else 0


if __name__ == "__main__":
    sys.exit(main())
