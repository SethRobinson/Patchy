"""Krita driver: headless export through Krita's own Python scripting.

`kritarunner -s testy_krita_export -f main <in> <out> <report>` runs
drivers/krita_scripts/testy_krita_export.py inside Krita: open, wait until the image
has settled, export (format by extension: PNG render, PSD resave). The plain CLI
(`krita.com <in> --export --export-filename <out>`) writes the file as soon as the
document is loaded, before fill and vector layers are drawn, so the same PSD came out
drawn on one run and blank on the next; it remains only as the fallback for a file
the script route cannot finish.

One kind of run-to-run variation survives the wait: Krita resolves a text layer's
font differently from one launch to the next (the right face on one, a fallback on
another). That is Krita's behavior, not the harness's, so a PNG render is made twice
and, when the two differ, a third time; the picture that came out most often is the
one kept, and the result says the renders varied.

Neither console shim prints anything through pipes on this machine, so success is
judged by the script's report file, or for the CLI by exit code plus output existence.
"""

from __future__ import annotations

import shutil
import subprocess
from pathlib import Path

from drivers import winproc

TIMEOUT_SECONDS = 180
_SCRIPTS_DIR = Path(__file__).with_name("krita_scripts")


def _stderr_tail(text: str | None) -> str:
    # Krita always spews harmless Fontconfig warnings; without filtering they bury
    # the real failure cause in the report.
    lines = [line for line in (text or "").splitlines()
             if line.strip() and "Fontconfig error" not in line]
    return "\n".join(lines).strip()[-2000:]


def _export_cli(exe: Path, input_path: Path, output_path: Path) -> tuple[int, str]:
    try:
        with winproc.suppressed_error_dialogs():
            completed = subprocess.run(
                [str(exe), str(input_path), "--export", "--export-filename", str(output_path)],
                capture_output=True,
                text=True,
                timeout=TIMEOUT_SECONDS,
            )
        return completed.returncode, _stderr_tail(completed.stderr)
    except subprocess.TimeoutExpired:
        return -1, f"timeout after {TIMEOUT_SECONDS}s"
    except OSError as error:
        return -1, str(error)


def _runner(exe: Path) -> Path | None:
    runner = exe.with_name("kritarunner" + exe.suffix)
    return runner if runner.exists() else None


def _export_script(runner: Path, input_path: Path, output_path: Path, asked: list[str] | None = None) -> str:
    """Run the waiting export; returns the script's verdict ("ok", "failed: ...") or
    "" when it left none (a crash, a hang, a Krita without Python). What Krita stopped
    to ask, with the answer the script gave, is added to `asked`."""
    report = output_path.with_name(output_path.name + ".krita.txt")
    questions = report.with_name(report.name + ".asked")
    report.unlink(missing_ok=True)
    questions.unlink(missing_ok=True)
    output_path.unlink(missing_ok=True)
    try:
        with winproc.suppressed_error_dialogs():
            # The module is found through the working directory: kritarunner replaces
            # PYTHONPATH with Krita's own.
            subprocess.run(
                [str(runner), "-s", "testy_krita_export", "-f", "main",
                 str(input_path), str(output_path), str(report)],
                cwd=str(_SCRIPTS_DIR), capture_output=True, text=True, timeout=TIMEOUT_SECONDS,
            )
    except (subprocess.TimeoutExpired, OSError):
        pass
    try:
        verdict = report.read_text(encoding="utf-8").strip() if report.exists() else ""
    except OSError:
        verdict = ""
    try:
        if asked is not None and questions.exists():
            asked.extend(line for line in questions.read_text(encoding="utf-8").splitlines() if line.strip())
    except OSError:
        pass
    report.unlink(missing_ok=True)
    questions.unlink(missing_ok=True)
    return verdict


def _same_picture(first: Path, second: Path) -> bool:
    import numpy as np
    from PIL import Image

    with Image.open(first) as a, Image.open(second) as b:
        if a.size != b.size:
            return False
        return bool(np.array_equal(np.asarray(a.convert("RGBA")), np.asarray(b.convert("RGBA"))))


def _export_png_by_majority(runner: Path, input_path: Path, output_path: Path,
                            asked: list[str] | None = None) -> tuple[bool, bool]:
    """(exported, the renders varied). Leaves the most common picture at output_path."""
    takes: list[Path] = []
    for index in range(3):
        take = output_path.with_name(f"{output_path.stem}.take{index}{output_path.suffix}")
        if (_export_script(runner, input_path, take, asked) != "ok" or not take.exists()
                or take.stat().st_size == 0):
            take.unlink(missing_ok=True)
            break
        takes.append(take)
        if index == 1 and _same_picture(takes[0], takes[1]):
            break
    try:
        if not takes:
            return False, False
        chosen = takes[0]
        varied = len(takes) == 3
        if varied and _same_picture(takes[1], takes[2]):
            chosen = takes[1]
        shutil.copyfile(chosen, output_path)
        return True, varied
    finally:
        for take in takes:
            take.unlink(missing_ok=True)


def export(exe: Path, input_path: Path, output_path: Path) -> dict:
    notes: list[str] = []
    runner = _runner(exe)
    exported = False
    asked: list[str] = []
    if runner is not None:
        if output_path.suffix.lower() == ".png":
            exported, varied = _export_png_by_majority(runner, input_path, output_path, asked)
            if varied:
                notes.append("Krita drew this file differently from one launch to the next "
                             "(its font matching varies); the most common render of three is used")
        else:
            exported = _export_script(runner, input_path, output_path, asked) == "ok"
        exported = exported and output_path.exists() and output_path.stat().st_size > 0
        for question in dict.fromkeys(asked):
            notes.append(f"Krita stopped to ask and was {question}")
    if exported:
        exit_code, stderr = 0, ""
    else:
        # The script route left nothing usable (some PSD exports end the process
        # without a verdict): the CLI is the only other way to get a file out.
        output_path.unlink(missing_ok=True)
        exit_code, stderr = _export_cli(exe, input_path, output_path)
        if runner is not None:
            notes.append("exported through the krita --export CLI, which does not wait for rendering")
    # No fabricated text here: the orchestrator interprets which PHASE failed
    # (import vs export) and words the cell message accordingly.
    ok = exit_code == 0 and output_path.exists() and output_path.stat().st_size > 0
    return {
        "exitCode": exit_code,
        "stderr": stderr,
        "ok": ok,
        "notes": notes,
        # Krita ran and gave a verdict (the -1 above is this driver's own sentinel for
        # a process that never got that far). That verdict is news about the file, not
        # evidence that Krita is broken, so the orchestrator's circuit breaker skips it.
        "fileRejected": not ok and exit_code >= 0,
    }
