#!/usr/bin/env python3
"""Checks the agent-facing documentation (AGENTS.md and docs/) and the root markdown files.

Errors (exit 1):
  - AGENTS.md or a docs/*.md file over the 30,000-byte limit (AGENTS.md "Documentation").
  - A relative Markdown link to a file that does not exist, or to a #heading that file lacks.
  - An image reference whose file does not exist.
  - A code or doc citation of the form `docs/<name>.md "Label"` (or `("Label")`) whose label is
    no longer in that document, as a heading, a bold label or verbatim text. Code comments cite
    docs this way, so a split or rename must keep the label or update the citation.
  - A source, script or CMake file that names `docs/<name>.md` when no such doc exists (a doc
    was renamed, split away or deleted without updating the comment). URLs are ignored.
Warnings (exit 0 unless --strict):
  - A docs/*.md file no other checked document links to (unreachable from the index).
  - AGENTS.md or a docs/*.md file over 27,000 bytes: split it by subtopic now, while there is
    room, instead of squeezing prose to fit under the cap later.
  - A doc that opens with "Split from [parent.md](parent.md)" when the parent does not link it
    in its first 3,000 bytes (its "Related documents" list), so readers of the parent find it.
  - A file under docs/images/ that no checked document references.

Usage: python scripts/check-docs.py [--strict]
Stdlib only; run from anywhere inside the repository.
"""
import os
import re
import sys
from pathlib import Path

LIMIT = 30000
# Warn well before the cap: a doc split at 27 KB keeps its prose; one squeezed under 30 KB loses it.
NEAR_LIMIT = 27000
ROOT = Path(__file__).resolve().parent.parent
LINK = re.compile(r"(?<!!)\[[^\]]*\]\(([^)\s]+)\)")
IMAGE = re.compile(r"!\[[^\]]*\]\(([^)\s]+)\)")
FENCE = re.compile(r"^\s*(```|~~~)")
HEADING = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")
# docs/<name>.md "Label", docs/<name>.md ("Label"), <name>.md ("Label"), <name>.md, "Label"
CITATION = re.compile(r"(?:docs/)?([a-z0-9][a-z0-9-]*\.md)[,:]?\s{0,2}\(?\s?[\"“]([^\"”\n]{2,120})[\"”]")
DOC_MENTION = re.compile(r"(?<![\w-])docs/([a-z0-9][a-z0-9-]*\.md)\b")
SPLIT_FROM = re.compile(r"Split from \[[^\]]*\]\(([a-z0-9][a-z0-9-]*\.md)\)")
# A split child is found from its parent's top: the parent links it in this many leading bytes.
PARENT_HEAD = 3000
CODE_DIRS = ("src", "tests", "scripts", "cmake", "packaging", "themes", "testy", "agent-kit")
CODE_SUFFIXES = (".cpp", ".hpp", ".h", ".mm", ".c", ".py", ".ps1", ".sh", ".bat", ".js", ".mjs", ".cmake", ".txt", ".md",
                 ".in", ".ts", ".json")
# Third-party source trees: skipped, except the PATCHY.md note Patchy keeps at the top of each.
VENDORED = {"bc7enc", "bcdec", "dav1d", "libaom", "libavif", "libheif", "libjpeg-turbo", "libjxl", "libraw", "libwebp",
            "miniz", "openjpeg", "stb", "tinyexr", "zstd", "lcms2", "pocketfft"}
VENDORED_FILES = {"gmic.cpp", "gmic.h", "CImg.h"}
IMAGE_SUFFIXES = (".png", ".jpg", ".jpeg", ".gif", ".svg", ".webp")
# One comment line (C/C++, scripts, batch files); its text is group 1.
COMMENT_LINE = re.compile(r"^\s*(?://+!?|#+|\*+(?!/)|/\*+!?|;+|[Rr][Ee][Mm]\b|::)\s?(.*)$")
PROSE_SUFFIXES = (".md", ".txt", ".in")


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
        _labels[path] = (text, headings, re.sub(r"\s+", " ", text))
    text, headings, flat = _labels[path]
    plain = label.strip()
    return (any(h == plain or h.startswith(plain) for h in headings) or plain in text
            or re.sub(r"\s+", " ", plain) in flat)


def wrapped_blocks(path: Path, text: str):
    """Runs of two or more comment lines (paragraphs in prose files), as [(line number, text)].
    A citation whose label wraps onto the next comment line is only visible once they are joined."""
    prose = path.suffix in PROSE_SUFFIXES
    blocks, run = [], []
    for no, line in enumerate(text.splitlines(), 1):
        if prose:
            content = line.strip()
        else:
            m = COMMENT_LINE.match(line)
            content = m.group(1).strip() if m else ""
        if content:
            run.append((no, content))
            continue
        if len(run) > 1:
            blocks.append(run)
        run = []
    if len(run) > 1:
        blocks.append(run)
    return blocks


def in_url(text: str, pos: int) -> bool:
    """True when pos sits inside a URL token (another project's docs/ folder, for example)."""
    start = max(text.rfind(c, 0, pos) for c in " \t\n(<\"'`")
    return "://" in text[start + 1:pos]


def iter_code_files():
    for base in CODE_DIRS:
        for dirpath, dirnames, filenames in os.walk(ROOT / base):
            for d in dirnames:
                note = Path(dirpath) / d / "PATCHY.md"
                if d in VENDORED and note.exists():
                    yield note
            dirnames[:] = [d for d in dirnames if d not in VENDORED and not d.startswith(".")]
            for name in filenames:
                if name.endswith(CODE_SUFFIXES) and name not in VENDORED_FILES:
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
            warnings.append(f"{path.relative_to(ROOT)}: {size} bytes, past the {NEAR_LIMIT}-byte split point "
                            f"({LIMIT - size} left under the {LIMIT}-byte cap): split it by subtopic")

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

    for doc in docs:
        m = SPLIT_FROM.search(doc.read_bytes()[:900].decode("utf-8", errors="ignore"))
        parent = doc_names.get(m.group(1)) if m else None
        if parent is None or parent == doc:
            continue
        head = parent.read_bytes()[:PARENT_HEAD].decode("utf-8", errors="ignore")
        if f"]({doc.name})" not in head and f"]({doc.name}#" not in head:
            warnings.append(f"{doc.relative_to(ROOT)}: split from {parent.name}, which does not link it in its first "
                            f"{PARENT_HEAD} bytes: list it under the parent's \"Related documents\"")
    code_files = list(iter_code_files())
    code_file_set = set(code_files)
    for path in code_files + checked:
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        if path in code_file_set:
            for m in DOC_MENTION.finditer(text):
                if m.group(1) not in doc_names and not in_url(text, m.start()):
                    line_no = text.count("\n", 0, m.start()) + 1
                    errors.append(f"{path.relative_to(ROOT)}:{line_no}: names docs/{m.group(1)}, which does not exist")
        for m in CITATION.finditer(text):
            name, label = m.group(1), m.group(2)
            dest = doc_names.get(name)
            if dest is None or dest == path:
                continue
            if not has_label(dest, label):
                line_no = text.count("\n", 0, m.start()) + 1
                errors.append(f"{path.relative_to(ROOT)}:{line_no}: cites docs/{name} \"{label}\", which has no such heading, label or text")
        # The same citations when the label wraps onto the next comment line (or prose line).
        for block in wrapped_blocks(path, text):
            joined, starts = "", []
            for line_no, content in block:
                starts.append((len(joined), line_no))
                joined += content + " "
            for m in CITATION.finditer(joined):
                if not any(m.start() < offset < m.end() for offset, _ in starts[1:]):
                    continue  # on one line: checked above
                name, label = m.group(1), m.group(2)
                dest = doc_names.get(name)
                if dest is None or dest == path or has_label(dest, label):
                    continue
                line_no = max(no for offset, no in starts if offset <= m.start())
                errors.append(f"{path.relative_to(ROOT)}:{line_no}: cites docs/{name} \"{label}\" (wrapped over lines), "
                              "which has no such heading, label or text")

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
