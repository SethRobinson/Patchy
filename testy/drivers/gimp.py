"""GIMP driver: headless Script-Fu batch through gimp-console-3.exe.

GIMP 3 refuses batch work without an explicit --batch-interpreter, and a batch
command that errors makes the console STOP at the failing command without ever
reaching the trailing (gimp-quit 0) batch, leaving the process alive forever.
The script therefore wraps its whole body in Script-Fu's catch, whose handler
only runs on error, so the quit batch always executes and the process exits 0
either way; success is judged by output existence, and the GIMP-Error lines
naming the real cause reach stderr whether or not the error was caught.
gimp-file-save picks the format from the extension (PNG render, PSD resave)
and merges layers itself for flat formats, keeping alpha, so the render leg
needs no explicit flatten (Testy composites over white during analysis).
"""

from __future__ import annotations

import subprocess
from pathlib import Path

TIMEOUT_SECONDS = 180

# Startup/noise lines that would bury the real failure cause in the report.
_NOISE_MARKERS = ("Welcome to GIMP", "batch command executed successfully",
                  "TESTY-BATCH-ERROR", "Fontconfig")


def _scheme_string(path: Path) -> str:
    """A Scheme string literal for a Windows path: forward slashes (backslash is
    the Scheme escape character), quotes escaped."""
    text = str(path).replace("\\", "/").replace('"', '\\"')
    return f'"{text}"'


def export(exe: Path, input_path: Path, output_path: Path) -> dict:
    script = (
        '(catch (gimp-message "TESTY-BATCH-ERROR") '
        f"(let* ((image (car (gimp-file-load RUN-NONINTERACTIVE {_scheme_string(input_path)})))) "
        f"(gimp-file-save RUN-NONINTERACTIVE image {_scheme_string(output_path)}) "
        "(gimp-image-delete image)))"
    )
    command = [
        str(exe), "-i", "-d", "-f",
        "--batch-interpreter", "plug-in-script-fu-eval",
        "-b", script, "-b", "(gimp-quit 0)",
    ]
    try:
        process = subprocess.Popen(
            command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )
    except OSError as error:
        return {"exitCode": -1, "stderr": str(error), "ok": False, "fileRejected": False}
    try:
        _, raw_stderr = process.communicate(timeout=TIMEOUT_SECONDS)
        exit_code = process.returncode
    except subprocess.TimeoutExpired:
        # gimp-console-3.exe runs batches in a separate script-fu plug-in
        # process; killing only the direct child leaks the rest of the tree.
        subprocess.run(["taskkill", "/f", "/t", "/pid", str(process.pid)],
                       capture_output=True)
        process.communicate()
        exit_code, raw_stderr = -1, f"timeout after {TIMEOUT_SECONDS}s"
    lines = [line for line in (raw_stderr or "").splitlines()
             if line.strip() and not any(marker in line for marker in _NOISE_MARKERS)]
    stderr = "\n".join(lines).strip()[-2000:]
    # No fabricated text here: the orchestrator interprets which PHASE failed
    # (import vs export) and words the cell message accordingly.
    ok = exit_code == 0 and output_path.exists() and output_path.stat().st_size > 0
    return {
        "exitCode": exit_code,
        "stderr": stderr,
        "ok": ok,
        # GIMP ran to completion and gave a verdict (the -1 above is this driver's
        # own sentinel for a process that never got that far). That verdict is news
        # about the file, not evidence that GIMP is broken, so the orchestrator's
        # circuit breaker skips it.
        "fileRejected": not ok and exit_code >= 0,
    }
