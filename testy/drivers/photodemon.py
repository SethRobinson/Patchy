"""PhotoDemon driver: headless export through a locally patched build.

Stock PhotoDemon has no automation surface at all (the command line only loads
files into the GUI), so this driver requires the patched build kept in the
PhotoDemon checkout next to this repository: its /testy-export switch performs
a batch-style load+export with all dialogs suppressed and exits when done
(`PhotoDemon.exe <in> /testy-export <out>`, format by extension).  The patch
writes a one-line phase report to "<out>.testy.txt" ("ok" / "load-failed" /
"save-failed" / "bad-extension"); this driver consumes and deletes that
sidecar so run directories only ever hold the exact artifact names scan-mode
scrubbing knows about.  PhotoDemon is a GUI process with no console output, so
the sidecar is the only failure detail available.

Success is judged by the sidecar plus the artifact, NOT the exit code: the
sidecar is written after the export completes, so a process that then crashes
or hangs on its way out still produced a valid, fully-written file (July 2026:
a PSD export wedged in PhotoDemon's ExifTool metadata wait after the .psd was
already complete on disk, and Photoshop happily reopened it).  Such a leg
scores ok with the crash surfaced as a driver note.  Spawns happen inside
suppressed_error_dialogs() so a real crash exits immediately instead of
holding the cell at a "has stopped working" dialog until the timeout.
"""

from __future__ import annotations

import subprocess
from pathlib import Path

from drivers import winproc

TIMEOUT_SECONDS = 180

_CLEAN_FAILURES = ("load-failed", "save-failed", "bad-extension")


def export(exe: Path, input_path: Path, output_path: Path) -> dict:
    status_file = Path(str(output_path) + ".testy.txt")
    try:
        status_file.unlink()
    except FileNotFoundError:
        pass
    command = [str(exe), str(input_path), "/testy-export", str(output_path)]
    try:
        with winproc.suppressed_error_dialogs():
            process = subprocess.Popen(
                command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            )
    except OSError as error:
        return {"exitCode": -1, "stderr": str(error), "ok": False,
                "fileRejected": False, "note": ""}
    timed_out = False
    try:
        process.communicate(timeout=TIMEOUT_SECONDS)
        exit_code = process.returncode
    except subprocess.TimeoutExpired:
        # PhotoDemon spawns ExifTool as a helper process; kill the whole tree.
        subprocess.run(["taskkill", "/f", "/t", "/pid", str(process.pid)],
                       capture_output=True)
        process.communicate()
        exit_code, timed_out = -1, True
    status = ""
    if status_file.exists():
        try:
            status = status_file.read_text(encoding="utf-8", errors="replace").strip()
        except OSError:
            pass
        try:
            status_file.unlink()
        except OSError:
            pass
    ok = (status == "ok" and output_path.exists()
          and output_path.stat().st_size > 0)
    note = ""
    if ok and timed_out:
        note = f"exported, then hung on exit (killed after {TIMEOUT_SECONDS}s)"
    elif ok and exit_code != 0:
        note = f"exported, then crashed on exit (exit 0x{exit_code & 0xFFFFFFFF:08X})"
    if timed_out and not ok:
        detail = f"timeout after {TIMEOUT_SECONDS}s"
    elif status and status != "ok":
        detail = f"PhotoDemon reported: {status}"
    elif not ok and exit_code != 0:
        detail = f"PhotoDemon crashed (exit 0x{exit_code & 0xFFFFFFFF:08X})"
    else:
        detail = ""
    # No fabricated text here: the orchestrator interprets which PHASE failed
    # (import vs export) and words the cell message accordingly.
    return {
        "exitCode": exit_code,
        "stderr": detail,
        "ok": ok,
        "note": note,
        # Only a clean exit with an explicit sidecar verdict counts as "the app
        # ran and refused this file" (which the circuit breaker must ignore).
        # A crash, hang, or launch failure is evidence about PhotoDemon itself,
        # so it still counts toward the breaker.
        "fileRejected": not ok and exit_code == 0 and status in _CLEAN_FAILURES,
    }
