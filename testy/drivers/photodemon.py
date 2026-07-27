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
"""

from __future__ import annotations

import subprocess
from pathlib import Path

TIMEOUT_SECONDS = 180


def export(exe: Path, input_path: Path, output_path: Path) -> dict:
    status_file = Path(str(output_path) + ".testy.txt")
    try:
        status_file.unlink()
    except FileNotFoundError:
        pass
    command = [str(exe), str(input_path), "/testy-export", str(output_path)]
    try:
        process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except OSError as error:
        return {"exitCode": -1, "stderr": str(error), "ok": False, "fileRejected": False}
    try:
        process.communicate(timeout=TIMEOUT_SECONDS)
        exit_code = process.returncode
        detail = ""
    except subprocess.TimeoutExpired:
        # PhotoDemon spawns ExifTool as a helper process; kill the whole tree.
        subprocess.run(["taskkill", "/f", "/t", "/pid", str(process.pid)],
                       capture_output=True)
        process.communicate()
        exit_code, detail = -1, f"timeout after {TIMEOUT_SECONDS}s"
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
    if not detail and status and status != "ok":
        detail = f"PhotoDemon reported: {status}"
    # No fabricated text here: the orchestrator interprets which PHASE failed
    # (import vs export) and words the cell message accordingly.
    ok = (exit_code == 0 and status == "ok"
          and output_path.exists() and output_path.stat().st_size > 0)
    return {
        "exitCode": exit_code,
        "stderr": detail,
        "ok": ok,
        # A process that ran to completion gave a verdict about the file, not
        # evidence that PhotoDemon is broken, so the circuit breaker skips it.
        "fileRejected": not ok and exit_code >= 0,
    }
