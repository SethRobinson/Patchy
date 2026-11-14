#!/usr/bin/env python3
"""Regenerate the contributor credits from GitHub.

    python scripts/dev/update-contributors.py            # rewrite both files
    python scripts/dev/update-contributors.py --dry-run  # print the table only
    python scripts/dev/update-contributors.py --check    # exit 1 when out of date

Rewrites kContributors in src/ui/app_credits.cpp (the About dialog) and the
"Incredible people" line under Credits in README.md, so both always agree.

Points: one for each merged pull request a person authored, one for each issue
they opened that was accepted: closed as completed, or still open with the
"fixed in main" label. Issues closed as not planned, or labeled wontfix,
duplicate or invalid, do not count. The repository owner and bots are left out.

Order: most points first. People with equal points keep the order they already
have in kContributors; a newcomer goes after them, earliest first contribution
first. The result is therefore the same on every run with the same GitHub data.

Credit is by GitHub handle only, never a real or display name. A person already
credited whose contributions GitHub no longer lists (a deleted account) stays,
with the count the file already has.

Reads GitHub through the gh CLI (read-only); writes nothing there.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
CREDITS_CPP = REPO_ROOT / "src" / "ui" / "app_credits.cpp"
README = REPO_ROOT / "README.md"

README_PREFIX = "Incredible people who donated suggestions, bug reports, and code: "
ACCEPTED_OPEN_LABEL = "fixed in main"
REJECTED_LABELS = {"wontfix", "duplicate", "invalid"}
# High enough to read the whole repository in one call; gh pages for us.
FETCH_LIMIT = "100000"

ARRAY_RE = re.compile(
    r"(static constexpr Contributor kContributors\[\] = \{\r?\n)(.*?)(^  \};)",
    re.DOTALL | re.MULTILINE,
)
ENTRY_RE = re.compile(r'\{"([^"]+)", "[^"]+"(?:, (\d+))?\},')


def gh_json(*args: str):
    try:
        result = subprocess.run(
            ["gh", *args], cwd=REPO_ROOT, capture_output=True, text=True, encoding="utf-8", check=False
        )
    except FileNotFoundError:
        sys.exit("update-contributors: the GitHub CLI (gh) is not on PATH")
    if result.returncode != 0:
        sys.exit(f"update-contributors: gh {' '.join(args)} failed:\n{result.stderr.strip()}")
    return json.loads(result.stdout)


def is_bot(author: dict) -> bool:
    login = author.get("login", "")
    return bool(author.get("is_bot")) or login.endswith("[bot]") or not login


def issue_accepted(issue: dict) -> bool:
    labels = {label["name"].lower() for label in issue.get("labels", [])}
    if labels & REJECTED_LABELS:
        return False
    if issue["state"] == "CLOSED":
        return issue.get("stateReason") == "COMPLETED"
    return ACCEPTED_OPEN_LABEL in labels


class Tally:
    def __init__(self, handle: str):
        self.handle = handle
        self.pull_requests = 0
        self.issues = 0
        self.first = ""  # ISO time of the earliest counted contribution

    def add(self, when: str, *, pull_request: bool) -> None:
        if pull_request:
            self.pull_requests += 1
        else:
            self.issues += 1
        if when and (not self.first or when < self.first):
            self.first = when

    @property
    def points(self) -> int:
        return self.pull_requests + self.issues


def fetch_tallies() -> dict[str, Tally]:
    owner = gh_json("repo", "view", "--json", "owner")["owner"]["login"].lower()
    pull_requests = gh_json(
        "pr", "list", "--state", "merged", "--limit", FETCH_LIMIT, "--json", "number,author,mergedAt"
    )
    issues = gh_json(
        "issue", "list", "--state", "all", "--limit", FETCH_LIMIT,
        "--json", "number,author,state,stateReason,labels,createdAt,closedAt",
    )  # fmt: skip

    tallies: dict[str, Tally] = {}

    def tally_for(author: dict) -> Tally | None:
        if is_bot(author) or author["login"].lower() == owner:
            return None
        return tallies.setdefault(author["login"].lower(), Tally(author["login"]))

    for pull_request in pull_requests:
        tally = tally_for(pull_request.get("author") or {})
        if tally:
            tally.add(pull_request.get("mergedAt") or "", pull_request=True)
    for issue in issues:
        if not issue_accepted(issue):
            continue
        tally = tally_for(issue.get("author") or {})
        if tally:
            tally.add(issue.get("closedAt") or issue.get("createdAt") or "", pull_request=False)
    return tallies


def read_existing(cpp_text: str) -> list[tuple[str, int]]:
    match = ARRAY_RE.search(cpp_text)
    if not match:
        sys.exit(f"update-contributors: kContributors not found in {CREDITS_CPP}")
    return [(name, int(count) if count else 0) for name, count in ENTRY_RE.findall(match.group(2))]


def build_list(existing: list[tuple[str, int]], tallies: dict[str, Tally]) -> tuple[list[tuple[str, int]], list[str]]:
    """Returns the ordered (handle, points) list and notes for the report."""
    notes: list[str] = []
    rows: list[tuple[str, int]] = []  # in tie-break order
    seen: set[str] = set()
    for name, old_count in existing:
        key = name.lower()
        seen.add(key)
        tally = tallies.get(key)
        if tally and tally.points > 0:
            # GitHub's spelling of the handle wins (a changed capital letter).
            rows.append((tally.handle, tally.points))
        else:
            kept = max(old_count, 1)
            rows.append((name, kept))
            notes.append(f"{name}: nothing listed on GitHub now, kept with {kept}")
    newcomers = sorted(
        (tally for key, tally in tallies.items() if key not in seen and tally.points > 0),
        key=lambda tally: (tally.first, tally.handle.lower()),
    )
    for tally in newcomers:
        rows.append((tally.handle, tally.points))
        notes.append(f"{tally.handle}: new, {tally.points}")
    # sorted() is stable, so equal counts keep the tie-break order built above.
    return sorted(rows, key=lambda row: -row[1]), notes


def render_cpp(cpp_text: str, rows: list[tuple[str, int]]) -> str:
    eol = "\r\n" if "\r\n" in cpp_text else "\n"
    body = "".join(f'      {{"{name}", "https://github.com/{name}", {count}}},{eol}' for name, count in rows)
    return ARRAY_RE.sub(lambda match: match.group(1) + body + match.group(3), cpp_text, count=1)


def render_readme(readme_text: str, rows: list[tuple[str, int]]) -> str:
    links = [f"[{name}](https://github.com/{name}) ({count})" for name, count in rows]
    joined = links[0] if len(links) == 1 else ", ".join(links[:-1]) + ", and " + links[-1]
    line_re = re.compile(r"^" + re.escape(README_PREFIX) + r"[^\r\n]*", re.MULTILINE)
    if len(line_re.findall(readme_text)) != 1:
        sys.exit(f'update-contributors: expected exactly one "{README_PREFIX.strip()}" line in {README}')
    return line_re.sub(lambda _match: README_PREFIX + joined, readme_text, count=1)


def read_text(path: Path) -> str:
    with open(path, encoding="utf-8", newline="") as handle:
        return handle.read()


def write_text(path: Path, text: str) -> None:
    with open(path, "w", encoding="utf-8", newline="") as handle:
        handle.write(text)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--dry-run", action="store_true", help="print the table, change no file")
    mode.add_argument("--check", action="store_true", help="change no file; exit 1 when a file is out of date")
    args = parser.parse_args()

    cpp_text = read_text(CREDITS_CPP)
    readme_text = read_text(README)
    tallies = fetch_tallies()
    rows, notes = build_list(read_existing(cpp_text), tallies)
    if not rows:
        sys.exit("update-contributors: no contributors found; refusing to write an empty list")

    width = max(len(name) for name, _ in rows)
    print(f"{'handle'.ljust(width)}  points  pull requests  issues")
    for name, count in rows:
        tally = tallies.get(name.lower())
        print(
            f"{name.ljust(width)}  {count:6d}  {tally.pull_requests if tally else 0:13d}  {tally.issues if tally else 0:6d}"
        )
    for note in notes:
        print(f"note: {note}")

    new_cpp = render_cpp(cpp_text, rows)
    new_readme = render_readme(readme_text, rows)
    changed = [path for path, old, new in ((CREDITS_CPP, cpp_text, new_cpp), (README, readme_text, new_readme)) if old != new]
    if args.dry_run or args.check:
        for path in changed:
            print(f"out of date: {path.relative_to(REPO_ROOT).as_posix()}")
        if not changed:
            print("credits are up to date")
        return 1 if (args.check and changed) else 0
    if new_cpp != cpp_text:
        write_text(CREDITS_CPP, new_cpp)
    if new_readme != readme_text:
        write_text(README, new_readme)
    for path in changed:
        print(f"wrote {path.relative_to(REPO_ROOT).as_posix()}")
    if not changed:
        print("credits are up to date")
    return 0


if __name__ == "__main__":
    sys.exit(main())
