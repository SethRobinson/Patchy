"""Aseprite as the reference renderer for sprites (never a benchmark column: it has
no PSD support).

`Aseprite.exe -b <in> --save-as <folder>/frame-{frame}.png` runs without a window
and writes one PNG per frame, with the layers composited as the file saved them.
The frame number in the name is read back instead of assumed, so the numbering
Aseprite starts from does not matter. Aseprite runs only as a command-line tool;
nothing here reads its source.
"""

from __future__ import annotations

import re
import subprocess
from pathlib import Path

from drivers import winproc

TIMEOUT_SECONDS = 180


def version(exe: Path) -> str:
    """"1.3.17-x64" from `Aseprite.exe --version`, or ""."""
    try:
        out = subprocess.run([str(exe), "--version"], capture_output=True, text=True,
                             encoding="utf-8", errors="replace", timeout=30)
    except (OSError, subprocess.TimeoutExpired):
        return ""
    lines = (out.stdout or "").strip().splitlines()
    return lines[0].removeprefix("Aseprite").strip() if out.returncode == 0 and lines else ""


def render_frames(exe: Path, source: Path, folder: Path) -> tuple[list[Path], str]:
    """(the frames in order, "") or ([], why not). `folder` must be empty."""
    try:
        with winproc.suppressed_error_dialogs():
            done = subprocess.run([str(exe), "-b", str(source), "--save-as", str(folder / "frame-{frame}.png")],
                                  capture_output=True, text=True, encoding="utf-8", errors="replace",
                                  timeout=TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired:
        return [], f"Aseprite timed out after {TIMEOUT_SECONDS} s"
    except OSError as error:
        return [], f"Aseprite did not start: {error}"

    def number(path: Path) -> int:
        digits = re.findall(r"[0-9]+", path.stem)
        return int(digits[-1]) if digits else 0

    frames = sorted((path for path in folder.glob("frame-*.png") if path.stat().st_size), key=number)
    if done.returncode or not frames:
        return [], ((done.stderr or done.stdout or "").strip()[-500:]
                    or f"Aseprite wrote no frames (exit {done.returncode})")
    return frames, ""
