#!/usr/bin/env python3
"""Refuses a Windows ssh call in scripts/ that keeps an inherited stdin.

Windows OpenSSH can stay alive after its remote command has finished when its stdin is
an inherited console or pipe (a release worker's console, a PowerShell background job).
The release driver hung on such calls on five release days, each time on a different
call than the one fixed the time before: lock releases, the upload scripts, then
release-linux.ps1. So the rule covers every call, not the one that hung:

  * an ssh call that does not feed the remote command passes -n (stdin from NUL), or
    uses the @PatchySshQuick argument list (remote-lock.ps1), which includes it;
  * a call that does feed stdin (`... | ssh`, `ssh ... < file`) is allowed as is;
  * anything else needs a `ssh-stdin-ok: <reason>` comment on the line.

Scans .ps1, .psm1, .bat and .cmd files under scripts/. Shell scripts run on the mac and
Linux hosts, whose OpenSSH does not have this problem, so they are not scanned.
Exit 0 when every call passes, 1 otherwise. See docs/release-process-scripts.md.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

MARKER = "ssh-stdin-ok"
EXTENSIONS = {".ps1", ".psm1", ".bat", ".cmd"}

# ssh used as a command: at a statement start, after an assignment, a pipe, an opening
# parenthesis or backtick (for /f), the call operator, or at the start of a string that
# is later run as a command line (Invoke-CmdLine 'ssh ...').
CALL = re.compile(r"""(?:^|[|(`'"=&;]|\bdo\s)\s*(?:&\s*)?ssh(?:\.exe)?(?:"|')?\s+(?P<args>.*)$""",
                  re.IGNORECASE)


def strip_comment(line: str, suffix: str) -> str | None:
    stripped = line.strip()
    if suffix in (".bat", ".cmd"):
        if re.match(r"(?i)^(@?rem\b|::)", stripped):
            return None
        if re.match(r"(?i)^@?echo\b", stripped):
            return None
        return line
    if stripped.startswith("#"):
        return None
    if re.match(r"(?i)^(Write-(Host|Output|Warning|Error|Verbose)|throw)\b", stripped):
        return None
    return line


def looks_like_ssh_arguments(args: str) -> bool:
    """The first argument of a real call is an option, a variable or a host name."""
    tokens = args.split()
    if not tokens:
        return False
    first = tokens[0].strip("'\"")
    return bool(re.match(r"[-$%@]", first) or "@" in first or "." in first)


def call_is_safe(args: str, before: str) -> bool:
    tokens = args.split()
    for token in tokens:
        token = token.strip("'\"")
        if token == "-n" or token == "@PatchySshQuick":
            return True
        if re.fullmatch(r"-[A-Za-z]*n[A-Za-z]*", token) and not token.startswith("-o"):
            return True
    if re.search(r"\|\s*(?:&\s*)?$", before):
        return True  # input piped into ssh
    if re.search(r"(?<![0-9>])<(?!\s*nul\b)", args, re.IGNORECASE):
        return True  # input redirected from a file
    return False


def scan(root: Path) -> list[str]:
    problems: list[str] = []
    for path in sorted(root.rglob("*")):
        if path.suffix.lower() not in EXTENSIONS or not path.is_file():
            continue
        in_block_comment = False
        text = path.read_text(encoding="utf-8", errors="replace")
        for number, raw in enumerate(text.splitlines(), start=1):
            if path.suffix.lower() in (".ps1", ".psm1"):
                if in_block_comment:
                    if "#>" in raw:
                        in_block_comment = False
                    continue
                if raw.lstrip().startswith("<#"):
                    in_block_comment = "#>" not in raw
                    continue
            line = strip_comment(raw, path.suffix.lower())
            if line is None or MARKER in raw:
                continue
            for match in CALL.finditer(line):
                if not looks_like_ssh_arguments(match.group("args")):
                    continue  # prose such as 'run elevated (ssh as an administrator)'
                before = line[: match.start("args")]
                before = before[: before.lower().rfind("ssh")]
                if not call_is_safe(match.group("args"), before):
                    rel = path.relative_to(root.parent).as_posix()
                    problems.append(f"{rel}:{number}: {raw.strip()}")
    return problems


def main() -> int:
    scripts = Path(__file__).resolve().parents[1]
    problems = scan(scripts)
    for problem in problems:
        print(f"ERROR: ssh without -n keeps the caller's stdin and can hang after its command ends:\n  {problem}")
    if problems:
        print(f"[SUMMARY] ssh stdin check: {len(problems)} call(s) need -n "
              f"(or a `{MARKER}: <reason>` comment), failed")
        return 1
    print("[SUMMARY] ssh stdin check: passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
