"""Patchy driver: runs the release patchy.exe through its CLI automation flags."""

from __future__ import annotations

import os
import subprocess
from pathlib import Path

TIMEOUT_SECONDS = 180


def _run(exe: Path, arguments: list[str]) -> dict:
    env = dict(os.environ)
    env["PATCHY_NO_SINGLE_INSTANCE"] = "1"
    try:
        completed = subprocess.run(
            [str(exe), *arguments],
            capture_output=True,
            text=True,
            timeout=TIMEOUT_SECONDS,
            env=env,
        )
        return {
            "exitCode": completed.returncode,
            "stderr": (completed.stderr or "").strip()[-2000:],
        }
    except subprocess.TimeoutExpired:
        return {"exitCode": -1, "stderr": f"timeout after {TIMEOUT_SECONDS}s"}
    except OSError as error:
        return {"exitCode": -1, "stderr": str(error)}


def export(exe: Path, input_path: Path, output_path: Path, append_text: str | None = None) -> dict:
    arguments = [str(input_path), "--export", str(output_path)]
    if append_text:
        arguments += ["--append-text", append_text]
    result = _run(exe, arguments)
    result["ok"] = result["exitCode"] == 0 and output_path.exists() and output_path.stat().st_size > 0
    return result


def failure_text(result: dict) -> str:
    """Human-accurate phase description from patchy.exe's documented exit codes
    (2 = no document opened, 3 = save failed; see MainWindow::run_cli_export)."""
    exit_code = result.get("exitCode")
    stderr = (result.get("stderr") or "").strip()
    suffix = f" ({stderr})" if stderr else ""
    if exit_code == 2:
        return f"failed to open the file (Patchy reported no document){suffix}"
    if exit_code == 3:
        return f"opened, but saving failed{suffix}"
    if exit_code == -1:
        return stderr or "timed out"
    return f"failed (exit {exit_code}){suffix}"
