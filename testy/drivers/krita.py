"""Krita driver: the documented headless CLI export path.

`krita.com <in> --export --export-filename <out>` renders through Krita's own
compositor; the output format follows the extension (PNG render, PSD resave).
The console shim prints nothing through pipes on this machine, so success is
judged by exit code plus output existence.
"""

from __future__ import annotations

import subprocess
from pathlib import Path

TIMEOUT_SECONDS = 300


def export(exe: Path, input_path: Path, output_path: Path) -> dict:
    try:
        completed = subprocess.run(
            [str(exe), str(input_path), "--export", "--export-filename", str(output_path)],
            capture_output=True,
            text=True,
            timeout=TIMEOUT_SECONDS,
        )
        exit_code = completed.returncode
        # Krita always spews harmless Fontconfig warnings; without filtering they bury
        # the real failure cause in the report.
        lines = [line for line in (completed.stderr or "").splitlines()
                 if line.strip() and "Fontconfig error" not in line]
        stderr = "\n".join(lines).strip()[-2000:]
    except subprocess.TimeoutExpired:
        exit_code, stderr = -1, f"timeout after {TIMEOUT_SECONDS}s"
    except OSError as error:
        exit_code, stderr = -1, str(error)
    ok = exit_code == 0 and output_path.exists() and output_path.stat().st_size > 0
    if not ok and not stderr:
        stderr = f"export failed (exit {exit_code}, no output file)"
    return {
        "exitCode": exit_code,
        "stderr": stderr,
        "ok": ok,
    }
