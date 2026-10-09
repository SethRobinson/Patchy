"""PhotoCraft driver: headless export through the stock photocraft-cli.

PhotoCraft (github.com/storytold/photocraft) ships a console CLI beside the app:
`photocraft-cli convert <in> <out>` opens a document without a window and writes
it out by extension (.png render, .psd layered resave). Exit codes: 0 ok, 1 the
file or the write failed (PhotoCraft ran and said why on stderr), 2 usage. So a
clean exit 1 is "the app refused this file", which the circuit breaker ignores;
a crash, hang, or launch failure counts against PhotoCraft itself.

stderr carries "warning: ..." lines. The flattening notice every PNG export
prints is dropped; the rest (missing fonts, unreadable settings) becomes the
cell's driver note. `run` prints each command's JSON result on stdout, which is
not needed here. A 16-bit document exports a 16-bit PNG, which is split into
`<stem>16.png` and an 8-bit render the way the Patchy driver does it.
"""

from __future__ import annotations

import re
import subprocess
from pathlib import Path

from drivers import winproc

TIMEOUT_SECONDS = 180

_FLATTENED = re.compile(r"^warning: \d+ layer\(s\) flattened")
_NO_TYPE_LAYERS = "the document has no type layers"


def version(exe: Path) -> str:
    """"0.5.0 (e5e3e3975, 2026-10-08)" from `photocraft-cli --version`, or ""."""
    try:
        out = subprocess.run([str(exe), "--version"], capture_output=True, text=True,
                             encoding="utf-8", errors="replace", timeout=30)
    except (OSError, subprocess.TimeoutExpired):
        return ""
    text = (out.stdout or "").strip()
    prefix = "photocraft-cli "
    return text[len(prefix):] if out.returncode == 0 and text.startswith(prefix) else ""


def export(exe: Path, input_path: Path, output_path: Path) -> dict:
    return _run_cli([str(exe), "convert", str(input_path), str(output_path)], output_path)


def render_text_afresh(exe: Path, input_path: Path, output_path: Path) -> dict:
    """A render with every type layer laid out again from its text data (Type > Update
    All Text Layers), for the cache-free leg. Like Photoshop, PhotoCraft 0.5.0 shows a
    type layer's stored pixels and draws nothing for one without them until it is
    edited; this command is that edit, and changes nothing else. A document with no
    type layers refuses the command, so it gets a plain render."""
    result = _run_cli([str(exe), "run", str(input_path), "--cmd", "type.updateAllTextLayers",
                       "--out", str(output_path)], output_path)
    if not result["ok"] and _NO_TYPE_LAYERS in result["stderr"]:
        return export(exe, input_path, output_path)
    return result


def _run_cli(command: list[str], output_path: Path) -> dict:
    try:
        with winproc.suppressed_error_dialogs():
            process = subprocess.Popen(
                command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                text=True, encoding="utf-8", errors="replace",
            )
    except OSError as error:
        return {"exitCode": -1, "stderr": str(error), "ok": False,
                "fileRejected": False, "note": ""}
    timed_out = False
    try:
        _, stderr = process.communicate(timeout=TIMEOUT_SECONDS)
        exit_code = process.returncode
    except subprocess.TimeoutExpired:
        subprocess.run(["taskkill", "/f", "/t", "/pid", str(process.pid)],
                       capture_output=True)
        _, stderr = process.communicate()
        exit_code, timed_out = -1, True
    lines = [line.strip() for line in (stderr or "").splitlines() if line.strip()]
    warnings = [line[len("warning: "):] for line in lines
                if line.startswith("warning: ") and not _FLATTENED.match(line)]
    errors = [line[len("error: "):] if line.startswith("error: ") else line
              for line in lines if not line.startswith("warning: ")]
    ok = exit_code == 0 and output_path.exists() and output_path.stat().st_size > 0
    if ok and output_path.suffix.lower() == ".png":
        import analyze

        analyze.split_deep_png(output_path)
    if ok:
        detail = ""
    elif timed_out:
        detail = f"timeout after {TIMEOUT_SECONDS}s"
    elif exit_code not in (0, 1, 2):
        detail = f"PhotoCraft crashed (exit 0x{exit_code & 0xFFFFFFFF:08X})"
    else:
        detail = " | ".join(errors)[:2000]
    return {
        "exitCode": exit_code,
        "stderr": detail,
        "ok": ok,
        "note": "; ".join(warnings)[:2000],
        "fileRejected": not ok and exit_code == 1,
    }
