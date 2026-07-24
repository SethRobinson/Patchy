"""Testy: PSD compatibility benchmark across the editors installed on this machine.

Photoshop is ground truth. For every corpus PSD and every editor, Testy measures:
opening, honest rendering accuracy (with a sentinel-composite trap against baked
flat previews), native object preservation after a resave (checked by reopening
the editor's PSD in Photoshop), the round-trip render, and a forced text
re-render where the editor is scriptable. A live browser dashboard shows the
matrix filling in; results persist per run for over-time comparison.

Run with any Python 3.11+ that has the packages from testy/requirements.txt
(machine settings live in testy/config.local.json; see config.example.json):
  python testy\\testy.py [options]
See docs/testy.md.
"""

from __future__ import annotations

import argparse
import datetime as _dt
import functools
import http.server
import json
import re
import shutil
import socket
import subprocess
import sys
import threading
import time
import traceback
import webbrowser
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import analyze
import config
import manifest as manifest_mod
import report
import staging
from drivers import krita as krita_driver
from drivers import patchy as patchy_driver
from drivers.photoshop import PhotoshopDriver

DEFAULT_SUFFIX = "~TESTY~"
# Affinity is deliberately opt-in (--editors photoshop,patchy,krita,photopea,affinity):
# its driver is background-UIA best-effort and the app's cold-start timing is flaky,
# so default runs stay fast and reliable without it. (Aseprite was verified to have no
# PSD I/O at all and removed from the roster entirely.)
DEFAULT_EDITORS = ["photoshop", "patchy", "krita", "photopea"]

# The Patchy release-build refresh command comes from config.local.json
# ("build_command"); without one, runs measure the existing patchy.exe as-is.


def log(message: str) -> None:
    print(f"[testy] {message}", flush=True)


def git_hash() -> str:
    try:
        head = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"], cwd=config.REPO_ROOT,
            capture_output=True, text=True, timeout=30,
        ).stdout.strip()
        dirty = subprocess.run(
            ["git", "status", "--porcelain"], cwd=config.REPO_ROOT,
            capture_output=True, text=True, timeout=30,
        ).stdout.strip()
        return head + ("+dirty" if dirty else "")
    except Exception:
        return "unknown"


def refresh_patchy_build() -> bool:
    """Run the configured release build; trust compile/link evidence, not exit codes."""
    if not config.BUILD_COMMAND:
        log("no build_command configured (config.local.json); measuring patchy.exe as-is")
        return True
    log("refreshing Patchy release build (use --no-build to skip)...")
    # One pre-quoted string, not an argv list: list2cmdline would escape the
    # command's inner quotes as \" which cmd.exe does not understand. With /s,
    # cmd strips exactly the outer quote pair added here.
    completed = subprocess.run(
        'cmd /s /c "' + config.BUILD_COMMAND + '"', cwd=config.REPO_ROOT,
        capture_output=True, text=True, timeout=1200,
    )
    output = (completed.stdout or "") + (completed.stderr or "")
    built = ("ninja: no work to do" in output) or ("Linking CXX executable" in output) or (
        "Building CXX object" in output
    )
    if not built:
        log("WARNING: build produced no compile/link evidence; patchy.exe may be stale")
        log(output[-1500:])
    return built


def read_corpus_file(corpus_path: Path) -> list[Path]:
    files: list[Path] = []
    for line in corpus_path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        path = Path(line)
        if not path.is_absolute():
            path = config.REPO_ROOT / path
        files.append(path.resolve())
    return files


def resolve_corpus(args: argparse.Namespace) -> list[Path]:
    if args.files:
        files = [Path(entry).resolve() for entry in args.files]
    elif args.corpus:
        corpus_path = Path(args.corpus)
        if not corpus_path.is_absolute():
            corpus_path = config.TESTY_ROOT / corpus_path
        files = read_corpus_file(corpus_path)
    else:
        files = config.default_corpus()
        if not files:
            log("no corpus configured: set corpus_file or corpus_dir in "
                "testy/config.local.json (see config.example.json) or pass --files")
    missing = [f for f in files if not f.exists()]
    for f in missing:
        log(f"WARNING: corpus file missing, skipped: {f}")
    return [f for f in files if f.exists()]


# The pause request channel: the server (or anything else) drops this file into a
# live run's directory and the orchestrator checkpoints and exits at its next
# file/cell boundary - a cell mid-flight always finishes first, never interrupted.
PAUSE_FLAG = "pause.flag"

# Browser-initiated runs: the serving process tracks at most one child run (spawned
# via POST /testy-start-run or /testy-resume-run) and refuses overlaps; a process that
# is itself running a benchmark (testy.py CLI) refuses too via the in-process flag.
_child_run: subprocess.Popen | None = None
_child_run_started: _dt.datetime | None = None
_child_run_dir: Path | None = None  # known for resumes, discovered for fresh starts
_in_process_run_active = False
_in_process_run_dir: Path | None = None
_spawn_lock = threading.Lock()
_status_cache: tuple[Path, float, dict] | None = None


def _run_in_progress() -> bool:
    return _in_process_run_active or (_child_run is not None and _child_run.poll() is None)


def _child_run_command(
    base_url: str,
    files: list[str],
    editors: list[str],
    *,
    skip_build: bool = False,
    fresh: bool = False,
    scan_threshold: float | None = None,
    compare: str | None = None,
    suffix: str | None = None,
) -> list[str]:
    """The argv for a browser-spawned child run; shared by start-run and retest.

    The file list travels through a corpus file, never argv: a pasted corpus of a
    few hundred paths overflows the Windows 32K command-line limit and CreateProcess
    fails with WinError 206 before the child ever starts. One run at a time means
    the single well-known name cannot be clobbered mid-run, and resume reads the
    run's own status.json, so overwrites by later runs are harmless."""
    corpus_path = config.RUNS_DIR / "last-child-corpus.txt"
    corpus_path.parent.mkdir(parents=True, exist_ok=True)
    corpus_path.write_text("".join(f"{f}\n" for f in files), encoding="utf-8")
    command = [
        sys.executable, str(config.TESTY_ROOT / "testy.py"),
        "--corpus", str(corpus_path),
        "--editors", ",".join(editors),
        "--no-browser", "--exit-when-done",
        "--server-url", base_url,
    ]
    if compare is not None:
        command.extend(["--compare", compare])
    if suffix is not None:
        command.extend(["--suffix", suffix])
    if skip_build:
        command.append("--no-build")
    if fresh:
        command.append("--fresh")
    if scan_threshold is not None:
        command.extend(["--scan", str(scan_threshold)])
    return command


def _read_status(status_path: Path) -> dict | None:
    """Parse a run's status.json, reusing the last parse while the file is unchanged
    (the run-state endpoint re-reads the same newest file every few seconds)."""
    global _status_cache
    try:
        mtime = status_path.stat().st_mtime
        if _status_cache and _status_cache[0] == status_path and _status_cache[1] == mtime:
            return _status_cache[2]
        status = json.loads(status_path.read_text(encoding="utf-8"))
        _status_cache = (status_path, mtime, status)
        return status
    except Exception:
        return None


def _iter_statuses():
    """(path, parsed status) for every run, newest first, skipping unreadable ones."""
    for status_path in sorted(config.RUNS_DIR.glob("2*/status.json"), reverse=True):
        status = _read_status(status_path)
        if status is not None:
            yield status_path, status


def _live_run_dir() -> Path | None:
    """The live run's directory, or None while it has not written status.json yet."""
    global _child_run_dir
    if _in_process_run_active and _in_process_run_dir is not None:
        return _in_process_run_dir
    if _child_run is None or _child_run.poll() is not None:
        return None
    if _child_run_dir is not None and (_child_run_dir / "status.json").exists():
        return _child_run_dir
    for status_path, status in _iter_statuses():
        if status.get("state") != "running":
            continue
        # A crashed run can leave a stale "running" status behind; the live child's
        # directory is never older than the moment it was spawned.
        try:
            stamp = _dt.datetime.strptime(status_path.parent.name, "%Y%m%d-%H%M%S")
        except ValueError:
            continue
        if _child_run_started is not None and stamp < _child_run_started - _dt.timedelta(seconds=5):
            continue
        _child_run_dir = status_path.parent
        return _child_run_dir
    return None


def _resumable() -> dict | None:
    """The newest run, if it is waiting to be continued: paused, or left "running" by
    a process that died (crash, reboot, taskkill). Canceled runs are deliberate stops
    and are only resumed explicitly from their own report page."""
    if _run_in_progress():
        return None
    for status_path, status in _iter_statuses():
        state = status.get("state")
        if state == "paused":
            return {"run": status_path.parent.name, "state": "paused"}
        if state == "running":
            return {"run": status_path.parent.name, "state": "interrupted"}
        return None
    return None


# Everything a run directory can contain at its top level; run deletion removes
# exactly these names (plus the per-file artifact names in Runner.SCRUB_*) and
# nothing else.
RUN_ROOT_FILES = ("report.html", "status.json", "status.json.tmp", "results.json",
                  "flagged.txt", PAUSE_FLAG)
# Every editor subdirectory a run can create under files/<stem>/.
KNOWN_CELL_DIRS = (*DEFAULT_EDITORS, "affinity")


def _delete_run_dir(run_dir: Path) -> list[str]:
    """Delete one run directory the way scan-mode scrubbing does: only the exact file
    names Testy itself writes, one by one, and plain rmdir (never recursive, never a
    wildcard) for directories - anything unexpected inside survives and is reported
    back instead of deleted."""
    warnings: list[str] = []

    def unlink_known(directory: Path, names: tuple[str, ...]) -> None:
        if not directory.is_dir():
            return
        for name in names:
            try:
                (directory / name).unlink(missing_ok=True)
            except OSError as error:
                warnings.append(f"could not delete {directory / name}: {error}")

    def rmdir_or_report(directory: Path) -> None:
        if not directory.is_dir():
            return
        try:
            directory.rmdir()
        except OSError:
            leftovers = sorted(p.name for p in directory.iterdir())
            shown = ", ".join(leftovers[:8]) + (f" +{len(leftovers) - 8} more" if len(leftovers) > 8 else "")
            warnings.append(f"left {directory} in place (unexpected contents: {shown})")

    files_root = run_dir / "files"
    if files_root.is_dir():
        for stem_dir in sorted(files_root.iterdir()):
            if not stem_dir.is_dir():
                warnings.append(f"left unexpected file in place: {stem_dir}")
                continue
            unlink_known(stem_dir / "_staged", Runner.SCRUB_STAGED)
            rmdir_or_report(stem_dir / "_staged")
            unlink_known(stem_dir / "_truth", Runner.SCRUB_TRUTH)
            rmdir_or_report(stem_dir / "_truth")
            for editor_key in KNOWN_CELL_DIRS:
                unlink_known(stem_dir / editor_key, Runner.SCRUB_CELL)
                rmdir_or_report(stem_dir / editor_key)
            rmdir_or_report(stem_dir)
    rmdir_or_report(files_root)
    unlink_known(run_dir, RUN_ROOT_FILES)
    rmdir_or_report(run_dir)
    return warnings


def _purge_run_listings(names: set[str]) -> None:
    """Drop deleted runs from index.jsonl/history.jsonl so they stop being listed."""
    for filename in ("index.jsonl", "history.jsonl"):
        path = config.RUNS_DIR / filename
        if not path.exists():
            continue
        kept: list[str] = []
        for line in path.read_text(encoding="utf-8").splitlines():
            if not line.strip():
                continue
            try:
                drop = json.loads(line).get("run") in names
            except Exception:
                drop = False
            if not drop:
                kept.append(line)
        temp_path = path.parent / (path.name + ".tmp")
        temp_path.write_text("".join(line + "\n" for line in kept), encoding="utf-8")
        temp_path.replace(path)


class TestyRequestHandler(http.server.SimpleHTTPRequestHandler):
    """Static serving of testy/ plus the control-plane endpoints: the Photopea upload
    sink (confined to runs/), corpus defaults, run state, and start-run."""

    def log_message(self, *args, **kwargs):  # noqa: N802 - stdlib signature
        pass

    def end_headers(self):  # noqa: N802 - stdlib signature
        # Photopea (an https origin) fetches staged PSDs from this server; without
        # CORS the embedded editor silently never loads the document.
        self.send_header("Access-Control-Allow-Origin", "*")
        super().end_headers()

    def _send_json(self, payload: dict, status: int = 200) -> None:
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _guarded(self, handler) -> None:
        """Run one endpoint handler; an unhandled exception becomes a JSON 500 the
        panel can display instead of a dropped connection (which the browser reports
        only as an unhelpful "TypeError: Failed to fetch")."""
        try:
            handler()
        except Exception as error:
            traceback.print_exc()
            try:
                self._send_json({"errors": [f"server error: {error}"]}, status=500)
            except Exception:
                pass  # response already partly sent; the traceback above is the record

    def do_GET(self):  # noqa: N802 - stdlib signature
        from urllib.parse import urlparse

        path = urlparse(self.path).path
        if path == "/testy-defaults":
            self._guarded(self._send_defaults)
            return
        if path == "/testy-run-state":
            self._guarded(self._send_run_state)
            return
        if path.endswith("/status.json"):
            self._guarded(self._send_status_json)
            return
        super().do_GET()

    def _send_status_json(self) -> None:
        """Serve status.json wholly from memory. The stdlib handler streams with the
        file handle open for the entire send, and the live run os.replace()ing the
        same file during that window dies with a Windows sharing error; a big scan's
        status.json exceeds a megabyte and report pages poll it every 1.2s, so the
        collision is routine. Read-then-close shrinks the window to microseconds."""
        target = Path(self.translate_path(self.path))
        deadline = time.monotonic() + 1.0
        while True:
            try:
                body = target.read_bytes()
                break
            except OSError:
                # Opening mid-swap fails transiently while the run's os.replace
                # lands; the writer holds it for microseconds, so retry briefly.
                if time.monotonic() >= deadline:
                    self.send_error(404)
                    return
                time.sleep(0.02)
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _send_defaults(self) -> None:
        defaults = config.default_corpus()
        self._send_json(
            {
                "files": [str(f) for f in defaults if f.exists()],
                "editors": DEFAULT_EDITORS,
                "allEditors": [*DEFAULT_EDITORS, "affinity"],
            }
        )

    def _send_run_state(self) -> None:
        live = _live_run_dir() if _run_in_progress() else None
        self._send_json({
            "running": _run_in_progress(),
            "run": live.name if live is not None else None,
            "pausePending": bool(live is not None and (live / PAUSE_FLAG).exists()),
            "resumable": _resumable(),
        })

    def do_POST(self):  # noqa: N802 - stdlib signature
        from urllib.parse import urlparse

        handler = {
            "/testy-start-run": self._start_run,
            "/testy-cancel-run": self._cancel_run,
            "/testy-pause-run": self._pause_run,
            "/testy-resume-run": self._resume_run,
            "/testy-delete-runs": self._delete_runs,
            "/testy-retest-file": self._retest_file,
            "/testy-upload": self._upload,
        }.get(urlparse(self.path).path)
        if handler is None:
            self.send_error(404)
            return
        self._guarded(handler)

    def _upload(self) -> None:
        from urllib.parse import parse_qs, urlparse

        parsed = urlparse(self.path)
        name = parse_qs(parsed.query).get("name", [""])[0]
        runs_root = (config.TESTY_ROOT / "runs").resolve()
        target = (config.TESTY_ROOT / name).resolve()
        if not str(target).startswith(str(runs_root)):
            self.send_error(403)
            return
        length = int(self.headers.get("Content-Length", "0"))
        if length <= 0 or length > 1 << 30:
            self.send_error(400)
            return
        body = self.rfile.read(length)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(body)
        self.send_response(200)
        self.send_header("Content-Length", "2")
        self.end_headers()
        self.wfile.write(b"ok")

    def _read_json_body(self) -> dict | None:
        """The request's JSON body ({} when empty); None (after a 400) if unparsable."""
        try:
            length = int(self.headers.get("Content-Length", "0"))
            request = json.loads(self.rfile.read(length)) if length else {}
        except Exception:
            self._send_json({"errors": ["request body is not valid JSON"]}, status=400)
            return None
        return request if isinstance(request, dict) else {}

    def _spawn_child(self, command: list[str], run_dir: Path | None = None) -> None:
        """Launch a run child process, logging to runs/last-child-run.log."""
        global _child_run, _child_run_started, _child_run_dir
        log_path = config.RUNS_DIR / "last-child-run.log"
        log_path.parent.mkdir(parents=True, exist_ok=True)
        log_file = open(log_path, "w", encoding="utf-8")
        _child_run_started = _dt.datetime.now()
        _child_run_dir = run_dir
        _child_run = subprocess.Popen(
            command,
            stdout=log_file,
            stderr=subprocess.STDOUT,
            creationflags=subprocess.CREATE_NO_WINDOW,
        )

    def _start_run(self) -> None:
        if _run_in_progress():
            self._send_json({"errors": ["a run is already in progress"]}, status=409)
            return
        request = self._read_json_body()
        if request is None:
            return

        raw_files = [str(f).strip().strip('"') for f in request.get("files", [])]
        raw_files = [f for f in raw_files if f]
        editors = [e for e in request.get("editors", DEFAULT_EDITORS) if e]
        errors = []
        if not raw_files:
            errors.append("no PSD files given")
        known_editors = {*DEFAULT_EDITORS, "affinity"}
        for editor in editors:
            if editor not in known_editors:
                errors.append(f"unknown editor: {editor}")
        if not editors:
            errors.append("no editors selected")
        files = []
        for entry in raw_files:
            path = Path(entry)
            if path.suffix.lower() not in (".psd", ".psb"):
                errors.append(f"not a .psd/.psb: {entry}")
            elif not path.exists():
                errors.append(f"file not found: {entry}")
            else:
                files.append(str(path.resolve()))
        scan_threshold: float | None = None
        if request.get("scan"):
            try:
                scan_threshold = float(request.get("scanThreshold", 10.0))
            except (TypeError, ValueError):
                scan_threshold = -1.0
            if not 0.0 <= scan_threshold <= 100.0:
                errors.append("scan threshold must be a percentage between 0 and 100")
        compare = str(request.get("compare") or "perceptual")
        if compare not in ("strict", "perceptual"):
            errors.append(f"unknown comparison mode: {compare}")
        if errors:
            self._send_json({"errors": errors}, status=400)
            return

        base_url = f"http://127.0.0.1:{self.server.server_address[1]}"
        with _spawn_lock:
            if _run_in_progress():
                self._send_json({"errors": ["a run is already in progress"]}, status=409)
                return
            # Inside the lock: building the command writes the corpus handoff file,
            # which must not be overwritten between here and the spawn.
            command = _child_run_command(
                base_url, files, editors,
                skip_build=bool(request.get("skipBuild")),
                fresh=bool(request.get("fresh")),
                scan_threshold=scan_threshold,
                compare=compare,
            )
            self._spawn_child(command)
        self._send_json({"started": True})

    def _retest_file(self) -> None:
        """Re-run a single file from an existing run as a fresh run of its own.

        The typical use is checking whether a Patchy fix landed: the child run
        refreshes the Patchy build by default, the Photoshop ground truth and the
        other editors' cells come straight from the caches (fast), and the Patchy
        cell re-measures because its cache key includes the git hash.
        """
        if _run_in_progress():
            self._send_json({"errors": ["a run is already in progress"]}, status=409)
            return
        request = self._read_json_body()
        if request is None:
            return
        name = str(request.get("run") or "")
        source = str(request.get("source") or "")
        target = (config.RUNS_DIR / name).resolve()
        status_path = target / "status.json"
        if not name or target.parent != config.RUNS_DIR.resolve() or not status_path.exists():
            self._send_json({"errors": [f"no run named {name}"]}, status=404)
            return
        status = _read_status(status_path)
        if status is None:
            self._send_json({"errors": [f"could not read {name}/status.json"]}, status=500)
            return
        # Only paths the named run itself lists may be retested; the endpoint must
        # not become a generic launch-anything surface.
        if not any(f.get("source") == source for f in status.get("files", [])):
            self._send_json({"errors": ["that file is not part of this run"]}, status=400)
            return
        if not Path(source).exists():
            self._send_json({"errors": [f"the source file is gone: {source}"]}, status=400)
            return
        run_options = status.get("run", {})
        editors = [e for e in run_options.get("editorOrder", DEFAULT_EDITORS) if e]
        base_url = f"http://127.0.0.1:{self.server.server_address[1]}"
        with _spawn_lock:
            if _run_in_progress():
                self._send_json({"errors": ["a run is already in progress"]}, status=409)
                return
            # Inside the lock: building the command writes the corpus handoff file,
            # which must not be overwritten between here and the spawn.
            # No --scan: a retest is an inspection run, so every artifact is kept.
            command = _child_run_command(
                base_url, [source], editors,
                skip_build=bool(request.get("skipBuild")),
                compare=run_options.get("compare", "strict"),
                suffix=run_options.get("suffix"),
            )
            self._spawn_child(command)
        self._send_json({"started": True})

    def _pause_run(self) -> None:
        request = self._read_json_body()
        if request is None:
            return
        with _spawn_lock:
            if not _run_in_progress():
                self._send_json({"errors": ["no run in progress to pause"]}, status=409)
                return
            run_dir = _live_run_dir()
            if run_dir is None:
                self._send_json(
                    {"errors": ["the run is still starting; try again in a moment"]}, status=409)
                return
            wanted = str(request.get("run") or "")
            if wanted and wanted != run_dir.name:
                self._send_json({"errors": [f"run {wanted} is not the live run"]}, status=409)
                return
            status = _read_status(run_dir / "status.json")
            if status is not None and status.get("state") != "running":
                self._send_json(
                    {"errors": [f"run {run_dir.name} is not running ({status.get('state')})"]},
                    status=409)
                return
            (run_dir / PAUSE_FLAG).write_text(
                _dt.datetime.now().isoformat(timespec="seconds"), encoding="utf-8")
        self._send_json({"pausing": True, "run": run_dir.name})

    def _resume_run(self) -> None:
        request = self._read_json_body()
        if request is None:
            return
        with _spawn_lock:
            if _run_in_progress():
                self._send_json({"errors": ["a run is already in progress"]}, status=409)
                return
            name = str(request.get("run") or (_resumable() or {}).get("run") or "")
            if not name:
                self._send_json({"errors": ["nothing to resume"]}, status=409)
                return
            target = (config.RUNS_DIR / name).resolve()
            status_path = target / "status.json"
            if target.parent != config.RUNS_DIR.resolve() or not status_path.exists():
                self._send_json({"errors": [f"no run named {name}"]}, status=404)
                return
            state = (_read_status(status_path) or {}).get("state")
            if state == "done":
                self._send_json({"errors": [f"run {name} already completed"]}, status=400)
                return
            if state not in ("paused", "canceled", "running"):
                self._send_json({"errors": [f"run {name} is not resumable ({state})"]}, status=400)
                return
            # A pause requested just before the old process died must not instantly
            # re-pause the fresh child.
            (target / PAUSE_FLAG).unlink(missing_ok=True)
            base_url = f"http://127.0.0.1:{self.server.server_address[1]}"
            command = [
                sys.executable, str(config.TESTY_ROOT / "testy.py"),
                "--resume", str(target),
                "--no-browser", "--exit-when-done",
                "--server-url", base_url,
            ]
            self._spawn_child(command, run_dir=target)
        self._send_json({"resumed": True, "run": target.name})

    def _delete_runs(self) -> None:
        request = self._read_json_body()
        if request is None:
            return
        names = request.get("runs")
        if (not isinstance(names, list) or not names
                or not all(isinstance(n, str) and n for n in names)):
            self._send_json({"errors": ["no runs given to delete"]}, status=400)
            return
        runs_root = config.RUNS_DIR.resolve()
        targets: list[tuple[str, Path]] = []
        for name in names:
            target = (config.RUNS_DIR / name).resolve()
            if target.parent != runs_root or target == runs_root:
                self._send_json(
                    {"errors": [f"refusing to delete {name}: not a run directory"]}, status=400)
                return
            targets.append((name, target))
        deleted: list[str] = []
        missing: list[str] = []
        partial: list[str] = []
        warnings: list[str] = []
        errors: list[str] = []
        with _spawn_lock:
            live = _live_run_dir() if _run_in_progress() else None
            for name, target in targets:
                if live is not None and target == live:
                    errors.append(f"{name} is the live run; cancel or pause it first")
                    continue
                if not target.exists():
                    missing.append(name)  # dir removed by hand earlier; just unlist it
                    continue
                warnings.extend(_delete_run_dir(target))
                (partial if target.exists() else deleted).append(name)
            _purge_run_listings({*deleted, *missing, *partial})
        self._send_json({"deleted": deleted, "missing": missing, "partial": partial,
                         "warnings": warnings, "errors": errors})

    def _cancel_run(self) -> None:
        global _child_run
        request = self._read_json_body()
        if request is None:
            return
        wanted = str(request.get("run") or "")
        if _child_run is not None and _child_run.poll() is None:
            live = _live_run_dir()
            if wanted and live is not None and wanted != live.name:
                self._send_json({"errors": [f"run {wanted} is not the live run"]}, status=409)
                return
            # Kill the whole tree: the run spawns patchy.exe, krita, headless Chrome,
            # and possibly an Affinity instance of its own.
            subprocess.run(["taskkill", "/PID", str(_child_run.pid), "/T", "/F"],
                           capture_output=True, timeout=30)
            _child_run = None
            # Mark any still-"running" status.json as canceled so dashboards and the
            # run index stop treating the run as live.
            for status_path, status in _iter_statuses():
                if status.get("state") == "running":
                    status["state"] = "canceled"
                    status_path.write_text(json.dumps(status), encoding="utf-8")
                    (status_path.parent / PAUSE_FLAG).unlink(missing_ok=True)
                    break
            self._send_json({"canceled": True})
            return
        if _run_in_progress():
            # An in-process CLI run cannot tree-kill itself from its own handler.
            self._send_json({"errors": ["this run was started from the command line; "
                                        "stop it with Ctrl+C in its console"]}, status=409)
            return
        # No live process: canceling a paused or crash-interrupted run marks it
        # canceled so the control panel stops offering it for resume (its own report
        # page can still resume it explicitly).
        for status_path, status in _iter_statuses():
            if wanted and status_path.parent.name != wanted:
                continue
            if status.get("state") in ("paused", "running"):
                status["state"] = "canceled"
                status_path.write_text(json.dumps(status), encoding="utf-8")
                (status_path.parent / PAUSE_FLAG).unlink(missing_ok=True)
                self._send_json({"canceled": True})
                return
            break
        self._send_json({"errors": ["no run in progress to cancel"]}, status=409)


class _ExclusiveHTTPServer(http.server.ThreadingHTTPServer):
    # On Windows, SO_REUSEADDR lets a second server "bind" a port another process
    # already owns and the two then split incoming connections (uploads once hit a
    # stale server with no POST handler). Exclusive binding makes the conflict a
    # clean OSError so the port scan moves on.
    allow_reuse_address = False


def start_server(port: int) -> tuple[http.server.ThreadingHTTPServer, int]:
    handler = functools.partial(TestyRequestHandler, directory=str(config.TESTY_ROOT))
    for candidate in range(port, port + 20):
        try:
            server = _ExclusiveHTTPServer(("127.0.0.1", candidate), handler)
        except OSError:
            continue
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        return server, candidate
    raise RuntimeError("no free port found for the dashboard")


def _thumb(source: Path, out: Path) -> str | None:
    try:
        analyze.make_thumbnail(source, out)
        return out.name
    except Exception:
        return None


def _version_slug(text: str) -> str:
    return re.sub(r"[^A-Za-z0-9.@+-]+", "_", text)[:60]


class Runner:
    def __init__(self, args: argparse.Namespace) -> None:
        global _in_process_run_dir
        self.args = args
        self.suffix = args.suffix
        # Scan mode: files whose render stays within this bad-pixel fraction of the
        # Photoshop ground truth (and hit no failure of any kind) are "passed" and
        # their run artifacts are discarded. None = normal run, keep everything.
        self.scan_threshold: float | None = None if args.scan is None else args.scan / 100.0
        # Which metric drives scan flagging (both are always computed and reported):
        # "strict" pixel differences or the "perceptual" visual comparison.
        self.compare_mode: str = args.compare
        # In scan mode cell-cache writes wait for the per-file verdict so passing
        # files do not pile renders/resaves into testy/cache: {index: [(cell_dir,
        # cache_dir, cell), ...]}.
        self._deferred_cell_caches: dict[int, list[tuple[Path, Path, dict]]] = {}
        self.resume = args.resume is not None
        self.status: dict = {}
        if self.resume:
            self.run_dir = self._locate_resume_dir(args.resume)
            self.status = json.loads((self.run_dir / "status.json").read_text(encoding="utf-8"))
            if self.status.get("state") == "done":
                raise SystemExit(f"run {self.run_dir.name} already completed; nothing to resume")
            # Corpus, editors, and options come from the run's own status.json: the
            # paused session's caches must stay valid and a mid-run rebuild would
            # change what the remaining cells measure, so --fresh and the build
            # refresh are forced off.
            log(f"resuming {self.run_dir.name}; corpus/editors/options come from its status.json")
            self.suffix = self.status["run"]["suffix"]
            scan = self.status["run"].get("scan")
            self.scan_threshold = scan["thresholdPct"] / 100.0 if scan else None
            # Runs from before the perceptual metric flagged strictly; keep doing so.
            self.compare_mode = self.status["run"].get("compare", "strict")
            self.args.fresh = False
            self.args.no_build = True
        else:
            self.run_dir = config.RUNS_DIR / _dt.datetime.now().strftime("%Y%m%d-%H%M%S")
        self.patchy_hash = git_hash()
        self.editors = config.discover_editors(self.patchy_hash)
        if self.resume:
            self.editor_order = [e for e in self.status["run"]["editorOrder"] if e in self.editors]
        else:
            self.editor_order = [e for e in args.editors.split(",") if e in self.editors]
        self.ps = PhotoshopDriver()
        self.files_dir = self.run_dir / "files"
        self.files_dir.mkdir(parents=True, exist_ok=True)
        config.CACHE_DIR.mkdir(parents=True, exist_ok=True)
        self.run_name = self.run_dir.name
        self.server_port: int | None = None
        self.server_base: str | None = None  # where dashboards/uploads are served
        _in_process_run_dir = self.run_dir

    @staticmethod
    def _locate_resume_dir(spec: str) -> Path:
        for candidate in (Path(spec), config.RUNS_DIR / spec):
            if (candidate / "status.json").exists():
                return candidate.resolve()
        raise SystemExit(f"--resume: no run (status.json) found at {spec}")

    # ---------- status plumbing ----------

    def init_status(self, corpus: list[Path]) -> None:
        self.status = {
            "state": "running",
            "run": {
                "startedAt": _dt.datetime.now().isoformat(timespec="seconds"),
                "patchyVersion": self.editors["patchy"].version,
                "patchyGit": self.patchy_hash,
                "suffix": self.suffix,
                "editorOrder": self.editor_order,
                "name": self.run_name,
                "compare": self.compare_mode,
            },
            "editors": {
                key: {
                    "displayName": info.display_name,
                    "version": info.version,
                    "available": info.available,
                    "notes": info.notes,
                }
                for key, info in self.editors.items()
            },
            "files": [
                {
                    "name": path.name,
                    "source": str(path),
                    "groundTruth": {"state": "pending"},
                    "cells": {key: {"state": "pending"} for key in self.editor_order},
                }
                for path in corpus
            ],
        }
        if self.scan_threshold is not None:
            self.status["run"]["scan"] = {"thresholdPct": self.scan_threshold * 100.0}
        self.push()

    def push(self) -> None:
        try:
            report.write_status(self.run_dir, self.status)
        except OSError as error:
            # A reader that outlasts write_status's retries must not kill a
            # multi-hour run over a progress refresh; every push writes the full
            # snapshot, so the next one heals the miss. Terminal states have no
            # next push and still raise.
            if self.status.get("state") != "running":
                raise
            log(f"could not update status.json ({error}); continuing")

    def file_entry(self, index: int) -> dict:
        return self.status["files"][index]

    # ---------- ground truth ----------

    def ground_truth(self, index: int, staged: staging.StagedPsd) -> dict | None:
        entry = self.file_entry(index)
        gt_dir = self.files_dir / Path(entry["name"]).stem / "_truth"
        gt_dir.mkdir(parents=True, exist_ok=True)
        cache_dir = config.CACHE_DIR / f"gt-{staged.sha1}-{_version_slug(self.ps.version())}-{self.suffix}"
        result_path = cache_dir / "result.json"

        entry["groundTruth"] = {"state": "running", "stage": "Photoshop ground truth"}
        self.push()

        if result_path.exists() and not self.args.fresh:
            result = json.loads(result_path.read_text(encoding="utf-8"))
            for name in ("render.png", "mutated.png"):
                if (cache_dir / name).exists():
                    shutil.copyfile(cache_dir / name, gt_dir / name)
        else:
            result = self.ps.probe(
                staged.original,
                gt_dir / "render.png",
                mutate_suffix=self.suffix,
                mutated_png=gt_dir / "mutated.png",
            )
            if result.get("ok"):
                cache_dir.mkdir(parents=True, exist_ok=True)
                result_path.write_text(json.dumps(result), encoding="utf-8")
                for name in ("render.png", "mutated.png"):
                    if (gt_dir / name).exists():
                        shutil.copyfile(gt_dir / name, cache_dir / name)

        if not result.get("ok"):
            entry["groundTruth"] = {"state": "failed", "error": result.get("error", "unknown")}
            self.push()
            return None

        if self.status["editors"]["photoshop"].get("version") in (None, "", "unknown"):
            self.status["editors"]["photoshop"]["version"] = self.ps.version()

        entry["docSize"] = [int(result["width"]), int(result["height"])]
        entry["layerCount"] = len(result["layers"])
        artifacts = {}
        if (gt_dir / "render.png").exists():
            artifacts["render"] = self._rel(gt_dir / "render.png")
            thumb = _thumb(gt_dir / "render.png", gt_dir / "render_thumb.png")
            if thumb:
                artifacts["renderThumb"] = self._rel(gt_dir / thumb)
        if (gt_dir / "mutated.png").exists():
            artifacts["mutated"] = self._rel(gt_dir / "mutated.png")
            thumb = _thumb(gt_dir / "mutated.png", gt_dir / "mutated_thumb.png")
            if thumb:
                artifacts["mutatedThumb"] = self._rel(gt_dir / thumb)
        entry["groundTruth"] = {
            "state": "done",
            "render": result.get("render"),
            "mutated": result.get("mutated"),
            "mutateCount": result.get("mutateCount"),
            "artifacts": artifacts,
        }
        (gt_dir / "manifest.json").write_text(json.dumps(result["layers"], indent=1), encoding="utf-8")
        self.push()
        return result

    def _rel(self, path: Path) -> str:
        return str(path.relative_to(self.run_dir)).replace("\\", "/")

    # ---------- editor cells ----------

    def run_cell(self, index: int, editor_key: str, staged: staging.StagedPsd, truth: dict | None) -> None:
        entry = self.file_entry(index)
        cell = entry["cells"][editor_key]
        info = self.editors[editor_key]
        cell_dir = self.files_dir / Path(entry["name"]).stem / editor_key
        cell_dir.mkdir(parents=True, exist_ok=True)

        if not info.available:
            cell.update({"state": "failed", "error": "editor not found on this machine"})
            self.push()
            return

        version_key = self.patchy_hash if editor_key == "patchy" else info.version
        cache_dir = config.CACHE_DIR / (
            f"cell-{staged.sha1}-{editor_key}-{_version_slug(version_key)}-{self.suffix}"
        )
        if cache_dir.exists() and not self.args.fresh:
            cached_cell = json.loads((cache_dir / "cell.json").read_text(encoding="utf-8"))
            for item in cache_dir.iterdir():
                if item.name != "cell.json":
                    shutil.copyfile(item, cell_dir / item.name)
            cell.clear()
            cell.update(cached_cell)
            cell["cached"] = True
            self._upgrade_cached_metrics(entry, cell, cell_dir, cache_dir, staged, truth)
            self.push()
            return

        cell.update({"state": "running", "stage": "opening + exporting"})
        self.push()

        render_png = cell_dir / "render.png"
        resave_psd = cell_dir / "resave.psd"
        trap_png = cell_dir / "trap.png"
        mutated_png = cell_dir / "mutated.png"
        artifacts: dict = {}
        cell["artifacts"] = artifacts

        try:
            self._drive_editor(editor_key, info, staged, cell, render_png, resave_psd, trap_png, mutated_png)
        except Exception as error:
            cell.update({"state": "failed", "error": f"driver error: {error}"})
            self.push()
            return

        if cell.get("state") in ("failed", "unsupported"):
            self.push()
            return

        for path, key, thumb_key in (
            (render_png, "render", "renderThumb"),
            (trap_png, "trap", "trapThumb"),
            (mutated_png, "mutated", "mutatedThumb"),
        ):
            if path.exists():
                artifacts[key] = self._rel(path)
                thumb = _thumb(path, path.with_name(path.stem + "_thumb.png"))
                if thumb:
                    artifacts[thumb_key] = self._rel(path.with_name(thumb))
        if resave_psd.exists():
            artifacts["resavePsd"] = self._rel(resave_psd)

        truth_render = self.files_dir / Path(entry["name"]).stem / "_truth" / "render.png"
        document_size = tuple(entry.get("docSize", (0, 0)))

        if truth is not None and render_png.exists() and truth_render.exists() and document_size[0]:
            cell["stage"] = "comparing renders"
            self.push()
            cell["renderMetrics"] = analyze.compare_renders(
                truth_render, render_png, document_size, truth["layers"], cell_dir / "heatmap.png"
            )
            if (cell_dir / "heatmap.png").exists():
                artifacts["heatmap"] = self._rel(cell_dir / "heatmap.png")

        if trap_png.exists():
            cell["trapSentinelFraction"] = round(analyze.sentinel_fraction(trap_png), 4)

        truth_mutated = self.files_dir / Path(entry["name"]).stem / "_truth" / "mutated.png"
        if mutated_png.exists() and truth_mutated.exists() and truth is not None and document_size[0]:
            text_objects = [l for l in truth["layers"] if l.get("kind") == "TEXT"]
            cell["textRender"] = analyze.compare_renders(
                truth_mutated, mutated_png, document_size, text_objects, None
            )

        if resave_psd.exists() and truth is not None:
            cell["stage"] = "reopening resave in Photoshop"
            self.push()
            roundtrip = self.ps.probe(resave_psd, cell_dir / "roundtrip.png")
            if roundtrip.get("ok"):
                (cell_dir / "roundtrip_manifest.json").write_text(
                    json.dumps(roundtrip["layers"], indent=1), encoding="utf-8"
                )
                cell["native"] = manifest_mod.compare_manifests(truth["layers"], roundtrip["layers"])
                if (cell_dir / "roundtrip.png").exists():
                    artifacts["roundtripRender"] = self._rel(cell_dir / "roundtrip.png")
                    thumb = _thumb(cell_dir / "roundtrip.png", cell_dir / "roundtrip_thumb.png")
                    if thumb:
                        artifacts["roundtripThumb"] = self._rel(cell_dir / "roundtrip_thumb.png")
                    if truth_render.exists() and document_size[0]:
                        cell["roundtripRender"] = analyze.compare_renders(
                            truth_render, cell_dir / "roundtrip.png", document_size, [], None
                        )
            else:
                cell["native"] = {"error": f"Photoshop could not open the resave: {roundtrip.get('error')}"}
                cell["resaveRejected"] = True

        cell["state"] = "done"
        cell.pop("stage", None)
        self.push()

        # Only fully-scored successful cells are cached: a failure may be transient (a
        # wedged Photoshop, a busy editor), and a cell scored without ground truth or
        # with failed automation legs would freeze its missing metrics into later runs.
        # Scan mode defers the write to the per-file verdict (passing files stay out
        # of the cache too, or a big scan doubles its wasted space there).
        if cell.get("opens") != "fail" and truth is not None and not cell.pop("uncacheable", False):
            if self.scan_threshold is None:
                self._write_cell_cache(cell_dir, cache_dir, cell)
            else:
                self._deferred_cell_caches.setdefault(index, []).append((cell_dir, cache_dir, cell))

    def _upgrade_cached_metrics(
        self, entry: dict, cell: dict, cell_dir: Path, cache_dir: Path,
        staged: staging.StagedPsd, truth: dict | None
    ) -> None:
        """Reconcile a cached cell with rules that changed since it was written.

        The cache key deliberately stays unchanged (a bump would invalidate every
        slow Photoshop probe); instead the cell is fixed in place and the cache
        entry rewritten. Two upgrades exist: backfilling the perceptual comparison
        into cells from before that metric, and dropping trap verdicts from files
        that no longer get a trap (e.g. flattened files, where the sentinel hit was
        a false positive). Old textRender/roundtripRender blocks are left as-is -
        the report shows a dash.
        """
        changed = False
        metrics = cell.get("renderMetrics")
        if metrics and "perceptual" not in metrics and truth is not None:
            truth_render = self.files_dir / Path(entry["name"]).stem / "_truth" / "render.png"
            render_png = cell_dir / "render.png"
            document_size = tuple(entry.get("docSize", (0, 0)))
            if truth_render.exists() and render_png.exists() and document_size and document_size[0]:
                log("    upgrading cached metrics with the perceptual comparison")
                cell["renderMetrics"] = analyze.compare_renders(
                    truth_render, render_png, document_size, truth["layers"], None
                )
                changed = True
        if staged.trap is None and (
            "trapSentinelFraction" in cell or "trapError" in cell
        ):
            log("    dropping the cached trap verdict (this file no longer gets a trap)")
            cell.pop("trapSentinelFraction", None)
            cell.pop("trapError", None)
            for key in ("trap", "trapThumb"):
                (cell.get("artifacts") or {}).pop(key, None)
            changed = True
        if not changed:
            return
        try:
            cacheable = {k: v for k, v in cell.items() if k != "cached"}
            (cache_dir / "cell.json").write_text(json.dumps(cacheable), encoding="utf-8")
        except OSError as error:
            log(f"    could not rewrite {cache_dir / 'cell.json'}: {error}")

    @staticmethod
    def _write_cell_cache(cell_dir: Path, cache_dir: Path, cell: dict) -> None:
        cache_dir.mkdir(parents=True, exist_ok=True)
        for item in cell_dir.iterdir():
            if item.is_file():
                shutil.copyfile(item, cache_dir / item.name)
        cacheable = {k: v for k, v in cell.items() if k != "cached"}
        (cache_dir / "cell.json").write_text(json.dumps(cacheable), encoding="utf-8")

    def _drive_editor(
        self,
        editor_key: str,
        info: config.EditorInfo,
        staged: staging.StagedPsd,
        cell: dict,
        render_png: Path,
        resave_psd: Path,
        trap_png: Path,
        mutated_png: Path,
    ) -> None:
        if editor_key == "photoshop":
            result = self.ps.probe(staged.original, render_png, resave_psd=resave_psd)
            if not result.get("ok"):
                cell.update({"state": "failed", "opens": "fail", "error": result.get("error")})
                return
            cell["opens"] = "ok" if result.get("render") == "ok" else "fallback-render"
            if staged.trap is not None:
                trap_result = self.ps.probe(staged.trap, trap_png)
                if not trap_result.get("ok"):
                    cell["trapError"] = trap_result.get("error")
            return

        if editor_key == "patchy":
            exported = patchy_driver.export(info.exe, staged.original, render_png)
            if not exported["ok"]:
                cell.update({"state": "failed", "opens": "fail",
                             "error": patchy_driver.failure_text(exported)})
                return
            cell["opens"] = "ok"
            resaved = patchy_driver.export(info.exe, staged.original, resave_psd)
            if not resaved["ok"]:
                cell["resaveError"] = patchy_driver.failure_text(resaved)
            if staged.trap is not None:
                patchy_driver.export(info.exe, staged.trap, trap_png)
            mutated = patchy_driver.export(info.exe, staged.original, mutated_png, append_text=self.suffix)
            if not mutated["ok"]:
                cell["mutateError"] = patchy_driver.failure_text(mutated)
            return

        if editor_key == "krita":
            # The CLI fuses open+export, but a PNG export of an opened document does
            # not fail in practice - so a failed render leg means the PSD IMPORT
            # failed, and a failed resave after a good render means the PSD EXPORT did.
            exported = krita_driver.export(info.exe, staged.original, render_png)
            if not exported["ok"]:
                detail = exported["stderr"] or f"exit {exported['exitCode']}, no output"
                cell.update({"state": "failed", "opens": "fail",
                             "error": f"failed to open the PSD (Krita import error; {detail})"})
                return
            cell["opens"] = "ok"
            resaved = krita_driver.export(info.exe, staged.original, resave_psd)
            if not resaved["ok"]:
                detail = resaved["stderr"] or f"exit {resaved['exitCode']}, no output"
                cell["resaveError"] = f"opened, but Krita's PSD export failed ({detail})"
            if staged.trap is not None:
                krita_driver.export(info.exe, staged.trap, trap_png)
            return

        if editor_key == "photopea":
            from drivers import photopea as photopea_driver

            if self.server_base is None:
                cell.update({"state": "failed",
                             "error": "Photopea needs a server (drop --no-serve, or pass --server-url)"})
                return
            result = photopea_driver.export_all(
                base_url=self.server_base,
                testy_root=config.TESTY_ROOT,
                original=staged.original,
                trap=staged.trap,
                render_png=render_png,
                resave_psd=resave_psd,
                trap_png=trap_png,
                mutated_png=mutated_png,
                suffix=self.suffix,
                progress=lambda stage: (cell.__setitem__("stage", stage), self.push()),
            )
            if not result["ok"]:
                cell.update({"state": "failed", "opens": result.get("opens", "fail"),
                             "error": result.get("error", "photopea failed")})
                return
            cell["opens"] = result.get("opens", "ok")
            if result.get("notes"):
                cell["driverNotes"] = result["notes"]
            return

        if editor_key == "affinity":
            from drivers import affinity as affinity_driver

            result = affinity_driver.export_all(
                info.exe, staged.original, staged.trap, render_png, resave_psd, trap_png,
                progress=lambda stage: (cell.__setitem__("stage", stage), self.push()),
            )
            if result.get("notes"):
                cell["driverNotes"] = result["notes"]
            if not result["ok"]:
                cell.update({"state": "failed", "opens": result.get("opens", "fail"),
                             "error": result.get("error", "automation failed")})
                return
            cell["opens"] = result.get("opens", "ok")
            if result.get("notes"):
                cell["driverNotes"] = result["notes"]
            if result.get("cacheable") is False:
                cell["uncacheable"] = True
            return

        cell.update({"state": "failed", "error": f"no driver for editor '{editor_key}'"})

    # ---------- scan mode ----------

    # Exactly the artifact names a run writes into each per-file directory. Scan-mode
    # cleanup deletes ONLY these names, one by one, and removes directories with
    # rmdir (which refuses non-empty ones) - never a wildcard, glob, or recursive
    # delete - so anything unexpected inside a run directory survives and is logged.
    SCRUB_STAGED = ("original.psd", "original.psb", "trap.psd", "trap.psb")
    SCRUB_TRUTH = ("render.png", "render_thumb.png", "mutated.png", "mutated_thumb.png",
                   "manifest.json")
    SCRUB_CELL = ("render.png", "render_thumb.png", "resave.psd", "trap.png",
                  "trap_thumb.png", "mutated.png", "mutated_thumb.png", "heatmap.png",
                  "roundtrip.png", "roundtrip_thumb.png", "roundtrip_manifest.json")

    def _apply_scan_policy(self, index: int) -> None:
        """After every cell of a file finished: flag it, or scrub a passing file."""
        entry = self.file_entry(index)
        reasons = self._scan_flag_reasons(entry)
        entry["scan"] = {"flagged": bool(reasons), "reasons": reasons}
        deferred = self._deferred_cell_caches.pop(index, [])
        if reasons:
            more = f" (+{len(reasons) - 1} more)" if len(reasons) > 1 else ""
            log(f"    scan: FLAGGED - {reasons[0]}{more}")
            for cell_dir, cache_dir, cell in deferred:
                self._write_cell_cache(cell_dir, cache_dir, cell)
        else:
            log("    scan: passed - discarding this file's saved images and resaves")
            entry["scan"]["artifactsScrubbed"] = True
            self._scrub_passed_file(entry)
        self.push()

    def _scan_flag_reasons(self, entry: dict) -> list[str]:
        """Why this file needs a look. Empty = passed. Conservative on purpose: any
        state that is not a complete, clean, in-budget measurement flags the file."""
        assert self.scan_threshold is not None
        reasons: list[str] = []
        truth = entry.get("groundTruth", {})
        if truth.get("state") != "done":
            reasons.append(f"Photoshop ground truth failed: {truth.get('error', 'unknown')}")
        for editor_key in self.editor_order:
            cell = entry["cells"].get(editor_key, {})
            name = self.editors[editor_key].display_name
            state = cell.get("state")
            if state != "done":
                detail = cell.get("error", "no detail")
                reasons.append(f"{name}: {state} ({detail})")
                continue
            if cell.get("opens") == "fail":
                reasons.append(f"{name}: failed to open the PSD")
            metrics = cell.get("renderMetrics")
            if metrics is None:
                if truth.get("state") == "done":
                    reasons.append(f"{name}: no render comparison was produced")
            elif self.compare_mode == "perceptual":
                perceptual = metrics.get("perceptual")
                if perceptual is None:
                    reasons.append(f"{name}: no perceptual comparison available")
                elif perceptual.get("badFraction", 1.0) > self.scan_threshold:
                    reasons.append(
                        f"{name}: render looks wrong on "
                        f"{perceptual['badFraction'] * 100:.1f}% of pixels "
                        f"(over {self.scan_threshold * 100:g}%; "
                        f"{metrics['badFraction'] * 100:.1f}% strict)"
                    )
            elif metrics.get("badFraction", 1.0) > self.scan_threshold:
                reasons.append(
                    f"{name}: render differs on {metrics['badFraction'] * 100:.1f}% of pixels "
                    f"(over {self.scan_threshold * 100:g}%)"
                )
            sentinel = cell.get("trapSentinelFraction", 0.0)
            if sentinel > 0.05:
                # Photoshop tripping its own trap means the file has layers even the
                # ground truth cannot re-render (missing fonts etc.), so it fell back
                # to the baked composite. Another editor matching that is not a cheat;
                # only sentinel coverage clearly beyond Photoshop's still flags.
                ps_sentinel = (entry["cells"].get("photoshop") or {}).get(
                    "trapSentinelFraction", 0.0)
                if ps_sentinel <= 0.05:
                    reasons.append(f"{name}: trap render shows the baked-composite sentinel")
                elif sentinel > ps_sentinel + 0.05:
                    reasons.append(
                        f"{name}: trap render shows the baked-composite sentinel on "
                        f"{sentinel * 100:.1f}% of pixels, well beyond Photoshop's own "
                        f"{ps_sentinel * 100:.1f}%")
            for key, label in (
                ("resaveError", "resave failed"),
                ("mutateError", "text mutation failed"),
                ("trapError", "trap render failed"),
            ):
                if cell.get(key):
                    reasons.append(f"{name}: {label} ({cell[key]})")
            if cell.get("resaveRejected"):
                reasons.append(f"{name}: resave rejected by Photoshop")
        return reasons

    def _scrub_passed_file(self, entry: dict) -> None:
        """Delete a passing file's artifacts by exact name; keep its metrics."""
        files_root = self.files_dir.resolve()
        stem = Path(entry["name"]).stem
        file_dir = (self.files_dir / stem).resolve()
        if not stem or file_dir.parent != files_root or file_dir == files_root:
            log(f"scan: refusing to scrub unexpected path: {file_dir}")
            return
        directories = [
            (file_dir / "_staged", self.SCRUB_STAGED),
            (file_dir / "_truth", self.SCRUB_TRUTH),
        ] + [(file_dir / editor_key, self.SCRUB_CELL) for editor_key in self.editor_order]
        for directory, names in directories:
            if not directory.is_dir():
                continue
            for name in names:
                try:
                    (directory / name).unlink(missing_ok=True)
                except OSError as error:
                    log(f"scan: could not delete {directory / name}: {error}")
            try:
                directory.rmdir()
            except OSError:
                leftovers = ", ".join(sorted(p.name for p in directory.iterdir()))
                log(f"scan: left {directory} in place (unexpected contents: {leftovers})")
        try:
            file_dir.rmdir()
        except OSError:
            log(f"scan: left {file_dir} in place (not empty)")
        # Drop artifact references so the report never shows broken images.
        entry.get("groundTruth", {}).pop("artifacts", None)
        for cell in entry["cells"].values():
            cell.pop("artifacts", None)

    def _write_flagged_list(self) -> None:
        threshold_pct = self.status["run"]["scan"]["thresholdPct"]
        basis = ("looks visually different from Photoshop's"
                 if self.compare_mode == "perceptual"
                 else "differs from Photoshop's")
        lines = [
            f"# Testy scan: files flagged for review (render {basis} on more "
            f"than {threshold_pct:g}% of pixels, or something failed).",
            "# Reusable as a corpus: python testy\\testy.py --corpus <this file>",
        ]
        flagged = [e for e in self.status["files"] if e.get("scan", {}).get("flagged")]
        for entry in flagged:
            lines.append("")
            for reason in entry["scan"]["reasons"]:
                lines.append(f"# {reason}")
            lines.append(entry["source"])
        if not flagged:
            lines.append("# (none - every file passed)")
        (self.run_dir / "flagged.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")

    # ---------- pause / resume ----------

    # A cell in one of these states is finished business for this run; resume never
    # retries them (a fresh run, which still hits the caches, re-measures failures).
    TERMINAL_CELL_STATES = ("done", "failed", "skipped", "unsupported")

    def _pause_requested(self) -> bool:
        return (self.run_dir / PAUSE_FLAG).exists()

    def _graceful_pause_exit(self) -> int:
        """Checkpoint between cells: everything finished so far is already flushed to
        status.json, so recording the state and exiting IS the checkpoint."""
        log("pause requested - checkpointing at the cell boundary")
        self._cleanup_drivers()
        self.status["state"] = "paused"
        self.status["run"]["pausedAt"] = _dt.datetime.now().isoformat(timespec="seconds")
        self.push()
        (self.run_dir / PAUSE_FLAG).unlink(missing_ok=True)
        log(f'paused - resume with the dashboard\'s Resume button or: '
            f'python testy\\testy.py --resume "{self.run_dir}"')
        return 3

    def _load_resumed_status(self) -> None:
        """Mark the loaded run live again; anything a dead session left mid-flight
        ("running") becomes pending work, everything terminal is kept as-is."""
        now = _dt.datetime.now().isoformat(timespec="seconds")
        self.status["state"] = "running"
        self.status["run"].pop("pausedAt", None)
        self.status["run"].setdefault("resumedAt", []).append(now)
        old_hash = self.status["run"].get("patchyGit")
        if old_hash != self.patchy_hash:
            note = (f"resumed at {now} with patchy git {self.patchy_hash}; "
                    f"cells finished earlier measured {old_hash}")
            log(f"WARNING: {note}")
            self.status["run"].setdefault("notes", []).append(note)
            self.status["run"]["patchyGit"] = self.patchy_hash
        for entry in self.status["files"]:
            if entry.get("groundTruth", {}).get("state") == "running":
                entry["groundTruth"] = {"state": "pending"}
            for cell in entry.get("cells", {}).values():
                if cell.get("state") == "running":
                    cell.clear()
                    cell["state"] = "pending"
        self.push()

    def _file_complete(self, entry: dict) -> bool:
        if entry.get("groundTruth", {}).get("state") not in ("done", "failed"):
            return False
        for editor_key in self.editor_order:
            if entry["cells"].get(editor_key, {}).get("state") not in self.TERMINAL_CELL_STATES:
                return False
        return self.scan_threshold is None or "scan" in entry

    def _cleanup_drivers(self) -> None:
        if "affinity" in self.editor_order:
            from drivers import affinity as affinity_driver

            affinity_driver.cleanup()
        if "photopea" in self.editor_order:
            from drivers import photopea as photopea_driver

            photopea_driver.cleanup()

    # ---------- top level ----------

    def run(self) -> int:
        if self.resume:
            # files[] order IS the corpus order; resolve_corpus would drop missing
            # files and shift every index out from under the loaded status.
            corpus = [Path(entry["source"]) for entry in self.status["files"]]
        else:
            corpus = resolve_corpus(self.args)
        if not corpus:
            log("no corpus files found; nothing to do")
            return 2
        report.write_report_page(self.run_dir)
        if self.resume:
            self._load_resumed_status()
        else:
            self.init_status(corpus)

        server = None
        if self.args.server_url:
            # A controlling server (start-testy.bat's serve.py) already serves the testy
            # root and the upload endpoint; reuse it instead of binding a second port.
            self.server_base = self.args.server_url.rstrip("/")
            log(f"dashboard: {self.server_base}/runs/{self.run_name}/report.html (parent server)")
        elif not self.args.no_serve:
            server, port = start_server(self.args.port)
            self.server_port = port
            self.server_base = f"http://127.0.0.1:{port}"
            url = f"{self.server_base}/runs/{self.run_name}/report.html"
            log(f"dashboard: {url} (run index at {self.server_base}/)")
            if not self.args.no_browser:
                webbrowser.open(url)
        if not self.resume:  # a resumed run was indexed when it first started
            report.append_run_index(config.TESTY_ROOT, self.run_name)

        if not self.args.no_build and "patchy" in self.editor_order:
            refresh_patchy_build()
            self.patchy_hash = git_hash()
            self.editors = config.discover_editors(self.patchy_hash)
            info = self.editors["patchy"]
            self.status["editors"]["patchy"].update(
                {"version": info.version, "available": info.available, "notes": info.notes}
            )
            self.status["run"]["patchyVersion"] = info.version
            self.status["run"]["patchyGit"] = self.patchy_hash
            self.push()

        # Baselines for the end-of-run corruption check. A resumed run reuses the
        # sha1 recorded when each file was first staged, so an edit made while the
        # run sat paused is caught too; a source already missing at resume time has
        # no baseline (its remaining work is failed inside the loop instead).
        source_hashes: dict[str, str] = {}
        for index, path in enumerate(corpus):
            recorded = self.file_entry(index).get("sha1") if self.resume else None
            if recorded:
                source_hashes[str(path)] = recorded
            elif path.exists():
                source_hashes[str(path)] = staging.sha1_of_file(path)

        # Circuit breaker: an editor that fails several files in a row is broken for
        # this run (a dead app, a dead website); skip its remaining cells fast and
        # honestly instead of grinding through every timeout.
        consecutive_failures = {key: 0 for key in self.editor_order}
        BREAKER_LIMIT = 3
        for index, source in enumerate(corpus):
            entry = self.file_entry(index)
            if self.resume and self._file_complete(entry):
                log(f"[{index + 1}/{len(corpus)}] {source.name} - already complete, skipped")
                continue
            if self._pause_requested():
                return self._graceful_pause_exit()
            log(f"[{index + 1}/{len(corpus)}] {source.name}")
            if self.resume and not source.exists():
                log("    source file is gone; failing its remaining work")
                if entry.get("groundTruth", {}).get("state") != "done":
                    entry["groundTruth"] = {"state": "failed",
                                            "error": "source file missing at resume"}
                for cell in entry["cells"].values():
                    if cell.get("state") not in self.TERMINAL_CELL_STATES:
                        cell.update({"state": "failed",
                                     "error": "source file missing at resume"})
                self.push()
                if self.scan_threshold is not None and "scan" not in entry:
                    self._apply_scan_policy(index)
                continue
            staged = staging.stage_psd(source, self.files_dir / source.stem / "_staged")
            entry["sha1"] = staged.sha1
            if staged.trap_error:
                entry["trapError"] = staged.trap_error
            if staged.trap_skipped:
                entry["trapSkipped"] = staged.trap_skipped
            truth = self.ground_truth(index, staged)
            for editor_key in self.editor_order:
                cell = entry["cells"][editor_key]
                if cell.get("state") in self.TERMINAL_CELL_STATES:
                    continue  # finished before a pause; the resume keeps it as-is
                if self._pause_requested():
                    return self._graceful_pause_exit()
                if consecutive_failures[editor_key] >= BREAKER_LIMIT:
                    cell.update({"state": "skipped",
                                 "error": f"editor skipped after {BREAKER_LIMIT} consecutive failures"})
                    self.push()
                    continue
                log(f"    {editor_key}...")
                self.run_cell(index, editor_key, staged, truth)
                if cell.get("state") == "failed":
                    consecutive_failures[editor_key] += 1
                    if consecutive_failures[editor_key] >= BREAKER_LIMIT:
                        log(f"    {editor_key} hit {BREAKER_LIMIT} consecutive failures - "
                            "skipping it for the rest of the run")
                else:
                    consecutive_failures[editor_key] = 0
            if self.scan_threshold is not None and "scan" not in entry:
                self._apply_scan_policy(index)

        # Prove the corpus originals were never touched (for a resumed run: not since
        # they were first staged, so the paused stretch is covered too).
        corruption = []
        for path in corpus:
            baseline = source_hashes.get(str(path))
            if baseline is None:
                continue
            if not path.exists() or staging.sha1_of_file(path) != baseline:
                corruption.append(path)
        if corruption:
            log(f"ERROR: source files changed during the run: {corruption}")
        self.status["run"]["sourcesUntouched"] = not corruption

        self._cleanup_drivers()

        if self.scan_threshold is not None:
            self._write_flagged_list()

        # A pause that lands during the very last cell loses the race on purpose: the
        # run is finished, so the request is void and must not leak into a later run.
        (self.run_dir / PAUSE_FLAG).unlink(missing_ok=True)
        self.status["state"] = "done"
        self.status["run"]["finishedAt"] = _dt.datetime.now().isoformat(timespec="seconds")
        self.push()
        (self.run_dir / "results.json").write_text(json.dumps(self.status, indent=1), encoding="utf-8")
        report.append_history(config.TESTY_ROOT, self._history_summary())
        self._print_summary()

        if server is not None and not self.args.exit_when_done:
            log("run complete - dashboard stays up; press Enter to quit")
            try:
                input()
            except (EOFError, KeyboardInterrupt):
                pass
        return 0

    def _aggregate(self) -> dict:
        aggregate: dict = {}
        for editor_key in self.editor_order:
            opened = total = 0
            render_scores: list[float] = []
            visual_scores: list[float] = []
            native_scores: list[float] = []
            for entry in self.status["files"]:
                cell = entry["cells"].get(editor_key)
                if not cell or cell.get("state") in ("pending", "running", "skipped"):
                    continue
                total += 1
                if cell.get("state") == "done" and cell.get("opens") != "fail":
                    opened += 1
                metrics = cell.get("renderMetrics")
                if metrics:
                    render_scores.append(metrics["accuracy"])
                    if metrics.get("perceptual"):
                        visual_scores.append(metrics["perceptual"]["accuracy"])
                native = cell.get("native")
                if native and "nativeScore" in native:
                    native_scores.append(native["nativeScore"])
            aggregate[editor_key] = {
                "opened": opened,
                "total": total,
                "render": sum(render_scores) / len(render_scores) if render_scores else 0.0,
                "visual": sum(visual_scores) / len(visual_scores) if visual_scores else 0.0,
                "native": sum(native_scores) / len(native_scores) if native_scores else 0.0,
            }
        return aggregate

    def _history_summary(self) -> dict:
        return {
            "run": self.run_name,
            "files": len(self.status["files"]),
            "patchy": self.patchy_hash,
            "editors": self._aggregate(),
        }

    def _print_summary(self) -> None:
        aggregate = self._aggregate()
        log("summary (mean strict render / visual match / native preservation / opened):")
        for editor_key in self.editor_order:
            a = aggregate[editor_key]
            log(
                f"  {self.editors[editor_key].display_name:<10} "
                f"render {a['render'] * 100:5.1f}%   visual {a['visual'] * 100:5.1f}%   "
                f"native {a['native'] * 100:5.1f}%   "
                f"opened {a['opened']}/{a['total']}"
            )
        if self.scan_threshold is not None:
            flagged = [e for e in self.status["files"] if e.get("scan", {}).get("flagged")]
            basis = "visual" if self.compare_mode == "perceptual" else "strict"
            log(f"scan: {len(flagged)}/{len(self.status['files'])} file(s) flagged "
                f"(threshold {self.scan_threshold * 100:g}% {basis} difference); "
                f"passed files' artifacts discarded")
            for entry in flagged:
                log(f"  FLAGGED {entry['name']}: {entry['scan']['reasons'][0]}")
            log(f"flagged list (reusable as a corpus): {self.run_dir / 'flagged.txt'}")
        log(f"report: {self.run_dir / 'report.html'}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Testy PSD compatibility benchmark")
    parser.add_argument("--files", nargs="*", help="explicit PSD paths (overrides --corpus)")
    parser.add_argument("--corpus", default=None,
                        help="corpus list file (default: config.local.json's corpus_file/corpus_dir)")
    parser.add_argument("--editors", default=",".join(DEFAULT_EDITORS),
                        help="comma-separated editor keys to run")
    parser.add_argument("--suffix", default=DEFAULT_SUFFIX, help="text appended by the forced re-render test")
    parser.add_argument("--scan", nargs="?", const=10.0, type=float, default=None, metavar="PCT",
                        help="scan mode: flag files whose render differs from Photoshop on more "
                             "than PCT%% of pixels (default 10) or that fail anything, and "
                             "discard the saved images/resaves of files that pass so big scans "
                             "stay small (metrics are kept for every file)")
    parser.add_argument("--compare", choices=("strict", "perceptual"), default="perceptual",
                        help="which comparison drives scan flagging: 'strict' counts every pixel "
                             "off by more than 6/255, 'perceptual' (default) counts pixels that "
                             "look wrong (SSIM structure + CIEDE2000 color). Both numbers are "
                             "always computed and shown in the report")
    parser.add_argument("--no-build", action="store_true", help="skip the Patchy release build refresh")
    parser.add_argument("--fresh", action="store_true", help="ignore cached ground truth / cells")
    parser.add_argument("--resume", default=None, metavar="RUN_DIR",
                        help="continue a paused/canceled/interrupted run directory "
                             "(runs\\<timestamp>), skipping completed work; corpus, "
                             "editors, and options come from its status.json")
    parser.add_argument("--port", type=int, default=config.PORT, help="dashboard port")
    parser.add_argument("--no-browser", action="store_true", help="do not auto-open the dashboard")
    parser.add_argument("--no-serve", action="store_true", help="write reports without the local server")
    parser.add_argument("--server-url", default=None,
                        help="reuse an already-running Testy server (browser-spawned runs)")
    parser.add_argument("--exit-when-done", action="store_true", help="do not wait for Enter at the end")
    args = parser.parse_args()
    if args.scan is not None and not 0.0 <= args.scan <= 100.0:
        parser.error("--scan threshold must be a percentage between 0 and 100")
    global _in_process_run_active
    _in_process_run_active = True
    try:
        return Runner(args).run()
    finally:
        _in_process_run_active = False


if __name__ == "__main__":
    sys.exit(main())
