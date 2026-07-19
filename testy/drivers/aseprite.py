"""Aseprite driver.

As of 1.3.x Aseprite has no PSD import or export; the probe detects that honestly
each run (its error text names the format), so a future Aseprite that gains PSD
support starts being measured without harness changes.
"""

from __future__ import annotations

import subprocess
from pathlib import Path

TIMEOUT_SECONDS = 180


def export(exe: Path, input_path: Path, output_path: Path) -> dict:
    try:
        completed = subprocess.run(
            [str(exe), "-b", str(input_path), "--save-as", str(output_path)],
            capture_output=True,
            text=True,
            timeout=TIMEOUT_SECONDS,
        )
        exit_code = completed.returncode
        output = ((completed.stdout or "") + (completed.stderr or "")).strip()
    except subprocess.TimeoutExpired:
        exit_code, output = -1, f"timeout after {TIMEOUT_SECONDS}s"
    except OSError as error:
        exit_code, output = -1, str(error)

    produced = output_path.exists() and output_path.stat().st_size > 0
    unsupported = "can't load" in output.lower() or "cannot load" in output.lower()
    return {
        "exitCode": exit_code,
        "stderr": output[-2000:],
        "ok": exit_code == 0 and produced and not unsupported,
        "unsupported": unsupported or (exit_code == 0 and not produced),
    }
