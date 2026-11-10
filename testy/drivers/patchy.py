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


def render_text_afresh(exe: Path, input_path: Path, output_path: Path, rerender_text: bool = True,
                       rerender_smart: bool = True) -> dict:
    """Export `input_path` after Patchy has laid out every type layer itself.

    Patchy shows the pixels saved in the file for a type layer or a smart object until
    the layer is edited; drivers/patchy_text_afresh.js calls layer.rerenderText() and
    layer.rerenderSmartObject() on each one, which change nothing else. Returns {"ok",
    "done"/"failed": [type layer names], "smartDone"/"smartFailed": [embedded smart
    object names], "fontFallback": [type layer names Patchy lacked a font for and drew
    in another], "error"}. `rerender_text=False` leaves type layers alone (their
    font is missing here, so the baked pixels stay); `rerender_smart=False` leaves smart
    objects alone. An `output_path` ending in .psd is saved as a document instead."""
    script = Path(__file__).with_name("patchy_text_afresh.js")
    report = output_path.with_name(output_path.stem + ".script.txt")
    report.unlink(missing_ok=True)
    result = _run(exe, ["--headless", "--run-script", str(script), "--script-output", str(report),
                        "--script-arg", f"out={output_path}",
                        "--script-arg", f"text={1 if rerender_text else 0}",
                        "--script-arg", f"smart={1 if rerender_smart else 0}", str(input_path)])
    lines = report.read_text(encoding="utf-8", errors="replace").splitlines() if report.exists() else []
    report.unlink(missing_ok=True)
    answer: dict = {}
    current_layer: str | None = None
    font_fallback: list[str] = []
    for line in lines:
        if line.startswith("{") and "testyTextAfresh" in line:
            try:
                answer = json.loads(line)
            except ValueError:
                pass
        elif line.startswith("{") and "testyLayer" in line:
            try:
                current_layer = str(json.loads(line).get("testyLayer"))
            except ValueError:
                current_layer = None
        elif (line.startswith("[warn] layer.rerenderText") and current_layer is not None
              and current_layer not in font_fallback):
            # rerenderText's only warnings are font ones: a face that is not installed
            # for Patchy, or has no glyph for the text, drawn in a fallback.
            font_fallback.append(current_layer)
    ok = (result["exitCode"] == 0 and bool(answer.get("exported"))
          and output_path.exists() and output_path.stat().st_size > 0)
    if ok and output_path.suffix.lower() == ".png":
        import analyze

        analyze.split_deep_png(output_path)
    error = "" if ok else (result.get("stderr") or " | ".join(lines[-3:]) or f"exit {result['exitCode']}")
    return {"ok": ok, "done": list(answer.get("done") or []), "failed": list(answer.get("failed") or []),
            "smartDone": list(answer.get("smartDone") or []),
            "smartFailed": list(answer.get("smartFailed") or []),
            "fontFallback": font_fallback, "error": error}


def save_text_afresh(exe: Path, input_path: Path, output_psd: Path) -> dict:
    """Save `input_path` as a .psd after Patchy has laid out every type layer itself,
    so the file carries the text data Patchy writes (an untouched type layer's data is
    passed through on a plain save). The same script as render_text_afresh, with smart
    objects left alone. layer.rerenderText() is the per-layer form of Type > Update All
    Text Layers; the menu command skips a layer whose font Patchy lacks, so such a
    layer ("fontFallback") is one the text-save leg does not measure."""
    return render_text_afresh(exe, input_path, output_psd, rerender_text=True, rerender_smart=False)


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
