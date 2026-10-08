"""Patchy driver: runs the release patchy.exe through its CLI automation flags."""

from __future__ import annotations

import json
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
    if result["ok"] and output_path.suffix.lower() == ".png":
        # A 16/32-bit document exports a 16-bit PNG (deep editing on): keep it as
        # <stem>16.png for the precision metric, and an 8-bit render.png as before.
        import analyze

        analyze.split_deep_png(output_path)
    # patchy.exe ran and reported a verdict (the -1 above is this driver's own sentinel
    # for a process that never got that far). That is news about the file, not evidence
    # that Patchy is broken, so the orchestrator's circuit breaker skips it.
    result["fileRejected"] = not result["ok"] and result["exitCode"] >= 0
    return result


def render_text_afresh(exe: Path, input_path: Path, output_path: Path, rerender_text: bool = True) -> dict:
    """Export `input_path` after Patchy has laid out every type layer itself.

    Patchy shows the pixels saved in the file for a type layer or a smart object until
    the layer is edited; drivers/patchy_text_afresh.js calls layer.rerenderText() and
    layer.rerenderSmartObject() on each one, which change nothing else. Returns {"ok",
    "done"/"failed": [type layer names], "smartDone"/"smartFailed": [embedded smart
    object names], "error"}. `rerender_text=False` leaves type layers alone (their
    font is missing here, so the baked pixels stay)."""
    script = Path(__file__).with_name("patchy_text_afresh.js")
    report = output_path.with_name(output_path.stem + ".script.txt")
    report.unlink(missing_ok=True)
    result = _run(exe, ["--headless", "--run-script", str(script), "--script-output", str(report),
                        "--script-arg", f"out={output_path}",
                        "--script-arg", f"text={1 if rerender_text else 0}", str(input_path)])
    lines = report.read_text(encoding="utf-8", errors="replace").splitlines() if report.exists() else []
    report.unlink(missing_ok=True)
    answer: dict = {}
    for line in lines:
        if line.startswith("{") and "testyTextAfresh" in line:
            try:
                answer = json.loads(line)
            except ValueError:
                pass
    ok = (result["exitCode"] == 0 and bool(answer.get("exported"))
          and output_path.exists() and output_path.stat().st_size > 0)
    if ok and output_path.suffix.lower() == ".png":
        import analyze

        analyze.split_deep_png(output_path)
    error = "" if ok else (result.get("stderr") or " | ".join(lines[-3:]) or f"exit {result['exitCode']}")
    return {"ok": ok, "done": list(answer.get("done") or []), "failed": list(answer.get("failed") or []),
            "smartDone": list(answer.get("smartDone") or []),
            "smartFailed": list(answer.get("smartFailed") or []), "error": error}


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
