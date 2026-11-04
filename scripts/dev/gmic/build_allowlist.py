#!/usr/bin/env python3
"""First-pass review of the vendored G'MIC standard library (docs/gmic.md).

Reads src/filters/gmic/gmic_stdlib.gmic and writes, beside it:

  patchy_filters_allowlist.txt   filter ids Patchy may offer
  patchy_filters_excluded.txt    every other filter with the reason it was left out

A filter is left out when:
  - it is named below (the legal boundary in docs/legal-constraints-filters.md: inpainting,
    patch-based synthesis and smoothing, content-aware resizing, interactive
    segmentation and colorization, neural networks, gradient-domain compositing);
  - its command, or a command it can reach, runs an external program or a neural
    network;
  - it does not run. The engine Patchy builds has no display, no network, no
    external programs and no patch-based algorithms, so a filter that needs one of
    them fails; the gmic_allowlisted_filters_run_with_defaults core test finds
    those and they are listed by hand in patchy_filters_run_failures.txt, which
    this script reads.
A file or folder parameter is not a reason by itself (Ascii Art only offers to
save a text file). The allowlist is a reviewed, committed file: rerun this script
only when the library or the rules change, and compare the result with the
committed files instead of overwriting them blindly.

    python scripts/dev/gmic/build_allowlist.py [--check]
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
GMIC_DIR = ROOT / "src" / "filters" / "gmic"

# Commands that mark a filter, and everything that can reach them, as excluded.
# Deliberately short: reach is read from whole words, which mistakes variables
# for commands (a variable named h is also the help command), so only names that
# cannot be anything else are listed. Everything else is decided by running the
# filter (patchy_filters_run_failures.txt).
SEEDS = {
    "external program": {"exec", "exec_out"},
}
PREFIX_SEEDS = {
    "neural network": ("nn_",),
}
# Left out by name whatever the analysis says (docs/legal-constraints-filters.md).
NAMED = [
    (r"\bInpaint\b", "inpainting"),
    (r"Patch-Based|Patch-PCA|Shuffle Patches", "patch-based algorithm"),
    (r"Seamcarve", "content-aware resizing"),
    (r"Extract Foreground|Colorize \[Interactive\]|Colorize Lineart", "interactive segmentation or colorization"),
    (r"\[CNN|CNN\]|Neural", "neural network"),
    (r"Resynthesize Texture", "texture synthesis"),
    (r"Blend \[Seamless\]", "gradient-domain compositing"),
    (r"Custom Code", "runs G'MIC code typed by the user"),
    (r"^Stylize$", "patch-based style transfer"),
    (r"^Blur \[Motion\]$", "blur kernel drawn as a user path (not offered, docs/patent-research-gmic.md)"),
    (r"^Blur \[Depth-of-Field\]$", "per-pixel blur level around a focus point (not offered, docs/patent-research-gmic.md)"),
]

DEFINITION = re.compile(r"^([A-Za-z_+][A-Za-z0-9_]*)\s*:(.*)$")
CALL = re.compile(r"^[+-]{0,2}([A-Za-z_][A-Za-z0-9_]*)(?:\[[^\]]*\]|\.+)?$")
QUOTED = re.compile(r'"(?:[^"\\]|\\.)*"')


def calls(body):
    """Names used as whole words (optionally with a selection), outside quoted text."""
    found = set()
    for word in QUOTED.sub(" ", body).split():
        match = CALL.match(word)
        if match:
            found.add(match.group(1))
    return found


def plain(name):
    name = re.sub(r"<[^>]*>", "", name)
    for entity, text in (("&amp;", "&"), ("&lt;", "<"), ("&gt;", ">"), ("&quot;", '"')):
        name = name.replace(entity, text)
    return name.strip()


def read_commands(lines):
    """Command name -> body text (several definitions of one name are joined)."""
    bodies = {}
    current = None
    for line in lines:
        if line.startswith("#"):
            continue
        match = DEFINITION.match(line)
        if match:
            current = match.group(1).lstrip("+")
            bodies[current] = bodies.get(current, "") + "\n" + match.group(2)
        elif current is not None and (line.startswith((" ", "\t")) or not line.strip()):
            bodies[current] += "\n" + line
        else:
            current = None
    return bodies


def read_filters(lines):
    """The same reading order and ids as parse_catalog (gmic_catalog.cpp)."""
    filters, folders, uses = [], [], {}
    logical, continuing = [], False
    for line in lines:
        if continuing:
            body = line[1:] if line.startswith("#") else line
            continuing = body.endswith("\\")
            logical[-1] += body.rstrip("\\")
        elif line.startswith("#@gui "):
            body = line[6:]
            continuing = body.endswith("\\")
            logical.append(body.rstrip("\\"))
    current = None
    for body in logical:
        body = body.strip()
        if not body:
            continue
        if body.startswith(":"):
            if current is not None:
                current["parameters"] += " " + body[1:]
            continue
        if ":" not in body:
            leave = len(body) - len(body.lstrip("_"))
            del folders[max(0, len(folders) - leave):]
            name = plain(body[leave:])
            if name:
                folders.append(name)
            current = None
            continue
        name, rest = body.split(":", 1)
        parts = rest.rsplit(":", 1)
        if len(parts) == 2 and parts[1].strip() in list("x.*+-vi"):
            rest = parts[0]
        pieces = rest.split(",", 1)
        command = pieces[0].strip()
        if not command or command == "_none_":
            current = None
            continue
        preview = re.sub(r"\(.*$", "", pieces[1]).strip() if len(pieces) > 1 else ""
        uses[command] = uses.get(command, 0) + 1
        current = {
            "id": command if uses[command] == 1 else f"{command}#{uses[command]}",
            "name": plain(name),
            "folder": " / ".join(folders),
            "commands": [c for c in (command, preview) if c and c != "_none_"],
            "parameters": "",
        }
        filters.append(current)
    return filters


def reasons_for(command, bodies, memo, stack):
    if command in memo:
        return memo[command]
    if command in stack:
        return set()
    found = set()
    for reason, names in SEEDS.items():
        if command in names:
            found.add(reason)
    for reason, prefixes in PREFIX_SEEDS.items():
        if command.startswith(prefixes):
            found.add(reason)
    body = bodies.get(command)
    if body is not None:
        stack.add(command)
        for token in calls(body):
            if token != command and (token in bodies or any(token in names for names in SEEDS.values())
                                     or any(token.startswith(p) for ps in PREFIX_SEEDS.values() for p in ps)):
                found |= reasons_for(token, bodies, memo, stack)
        stack.discard(command)
    memo[command] = found
    return found


def main():
    lines = (GMIC_DIR / "gmic_stdlib.gmic").read_text(encoding="latin-1").split("\n")
    bodies = read_commands(lines)
    filters = read_filters(lines)
    memo = {}
    run_failures = {}
    failures_file = GMIC_DIR / "patchy_filters_run_failures.txt"
    if failures_file.exists():
        for line in failures_file.read_text(encoding="utf-8").split("\n"):
            if line.strip() and not line.startswith("#"):
                ident, _, why = line.partition("#")
                run_failures[ident.strip()] = why.strip()
    allowed, excluded = [], []
    for item in filters:
        reasons = set()
        for command in item["commands"]:
            if command not in bodies:
                reasons.add("command not defined in the library")
            reasons |= reasons_for(command, bodies, memo, set())
        for pattern, reason in NAMED:
            if re.search(pattern, item["name"]):
                reasons.add(reason)
        if item["folder"].split(" / ")[0] in ("About", "Testing"):
            reasons.add("information or test page")
        if not reasons and item["id"] in run_failures:
            reasons.add(run_failures[item["id"]])
        (excluded if reasons else allowed).append((item, sorted(reasons)))

    header = ("# G'MIC {} standard library: {}.\n"
              "# Generated by scripts/dev/gmic/build_allowlist.py, then maintained by hand.\n"
              "# One filter id per line; text after white space is a comment. See docs/gmic.md.\n")
    allow_text = header.format("4.0.5", "filters Patchy may offer") + "".join(
        f"{item['id']}  # {item['folder']} / {item['name']}\n" for item, _ in allowed)
    exclude_text = header.format("4.0.5", "filters Patchy leaves out, with the reason") + "".join(
        f"{item['id']}  # {item['folder']} / {item['name']}: {'; '.join(reasons)}\n" for item, reasons in excluded)

    print(f"{len(filters)} filters: {len(allowed)} allowed, {len(excluded)} excluded")
    counts = {}
    for _, reasons in excluded:
        for reason in reasons:
            counts[reason] = counts.get(reason, 0) + 1
    for reason, count in sorted(counts.items(), key=lambda pair: -pair[1]):
        print(f"  {count:4d}  {reason}")

    targets = {"patchy_filters_allowlist.txt": allow_text, "patchy_filters_excluded.txt": exclude_text}
    if "--check" in sys.argv:
        stale = [name for name, text in targets.items()
                 if not (GMIC_DIR / name).exists() or (GMIC_DIR / name).read_text(encoding="utf-8") != text]
        print("differs from the committed files: " + (", ".join(stale) if stale else "nothing"))
        return 1 if stale else 0
    for name, text in targets.items():
        (GMIC_DIR / name).write_text(text, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
