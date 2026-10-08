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
import hashlib
import http.server
import json
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import threading
import time
import traceback
import types
import uuid
import webbrowser
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import analyze
import config
import fetch_psd_tools_corpus
import manifest as manifest_mod
import report
import rerun
import staging
from drivers import gimp as gimp_driver
from drivers import krita as krita_driver
from drivers import patchy as patchy_driver
from drivers import photodemon as photodemon_driver
from drivers.photoshop import PhotoshopDriver

DEFAULT_SUFFIX = "~TESTY~"

# Photoshop layer kinds whose pixels in a PSD are only a CACHE of something the layer
# defines another way (text and fonts, a path and a fill, an embedded document), with
# the label one gets in the scored render when the editor draws nothing for it once
# the cache is gone. An editor that shows such a layer from the cache has not rendered
# it, and a reader cannot tell; so these files are scored with the caches removed.
CACHED_LAYER_LABELS = {
    "TEXT": "Cannot render Photoshop text objects",
    "SOLIDFILL": "Cannot render Photoshop shape or fill layers",
    "GRADIENTFILL": "Cannot render Photoshop shape or fill layers",
    "PATTERNFILL": "Cannot render Photoshop shape or fill layers",
    "SMARTOBJECT": "Cannot render Photoshop smart objects",
}

# When a cached layer comes out blank once its cache is gone, is that the editor's
# failure or the test's? A shape or fill layer without cached pixels is a state
# Photoshop writes itself (16-bit files) and draws from the layer's data, so a blank
# one is the editor's failure. A type layer or smart object without them is not:
# Photoshop itself then shows nothing until the layer is edited, and an editor built
# the same way would be misjudged. For those two kinds a blank layer counts against
# the editor only when it is known to draw the kind from the layer's data on open (or
# after Testy's scripted no-op edit), or known to have no engine for it at all.
# Any other blank keeps the as-opened pixels and is reported as not measured.
BLANK_IS_FAILURE = {
    "patchy": ("TEXT", "SMARTOBJECT"),      # text after the scripted re-render (TEXT_CACHE_KEPT)
    "affinity": ("TEXT", "SMARTOBJECT"),    # lays out and renders everything afresh
    "krita": ("TEXT", "SMARTOBJECT"),       # converts text to its own on open; no smart objects
    "gimp": ("TEXT", "SMARTOBJECT"),        # imports both as the pixels in the file
    "psdtools": ("TEXT", "SMARTOBJECT"),    # no text engine, no smart-object renderer
    "photopea": ("TEXT", "SMARTOBJECT"),    # smart objects on open; text after the scripted edit
    # PhotoDemon's PSD importer (pdPSD.cls) creates every layer as PDL_Image and never
    # reads the 'TySh' block, so a PSD type layer is only ever its cached pixels there.
    "photodemon": ("TEXT", "SMARTOBJECT"),
}
NOT_MEASURED_REASON = {
    "TEXT": "this editor may only lay text out after an edit made inside the app, which Testy cannot make",
    "SMARTOBJECT": "this editor may only render a smart object after an edit made inside the app",
}
# Editors whose type layers and smart objects keep their cache in the copies they are
# given, and are made to draw them by a script instead (the way Photoshop's own
# reference is produced). Patchy, like Photoshop, shows the stored pixels until the
# layer is edited and takes its placement from them, so with the cache gone its text
# comes out small and misplaced and a smart object not at all, which says nothing
# about its engines; drivers/patchy_text_afresh.js calls layer.rerenderText() and
# layer.rerenderSmartObject() on each such layer. A layer the script did not reach is
# reported as not measured.
TEXT_CACHE_KEPT = {
    "patchy": "Patchy's script could not re-render this layer",
}
# A stripped-copy render that differs from the as-opened one outside the cached layers
# by more than this is not a render of the same document (a stale tab, a fallback):
# the leg is then void and the cell stays scored as opened.
NO_CACHE_OUTSIDE_LIMIT = 0.05
# A box whose two renders differ in no more than this fraction of its pixels is unchanged.
NO_CACHE_BOX_UNCHANGED = 0.005

# What an editor's text-rendering score can honestly be based on. Merely opening a
# PSD proves little: Photoshop, Patchy and others show the raster cached in the file
# until the text changes. "forced" editors are scored on the forced re-render leg,
# "open" editors lay text out afresh on every open so their plain render counts,
# "replay" editors have no text engine at all, and "unmeasured" ones have one that
# Testy cannot drive. The report ranks only the first two and names the rest.
TEXT_RENDER_BASIS = {
    "patchy": ("open", "Patchy's text is laid out afresh by layer.rerenderText()"),
    "krita": ("open", "Krita lays text out afresh on every open"),
    "affinity": ("open", "Affinity lays text out afresh on every open"),
    "gimp": ("replay", "GIMP imports PSD text layers as the rasters Photoshop cached"),
    "psdtools": ("replay", "psd-tools has no text engine; it composites the rasters Photoshop cached"),
    "photopea": ("open", "Photopea's text is laid out afresh by a scripted edit that changes nothing, "
                         "with the document's fonts handed to it"),
    "photodemon": ("replay", "PhotoDemon imports PSD text layers as the rasters Photoshop cached"),
}
# Help an editor gets from Testy for the text score, stated on the Standing card so the
# number is read for what it is.
TEXT_HELP_NOTES = {
    "photopea": "Photopea is fed the correct fonts: it runs in a browser with only its own web fonts, "
                "so Testy hands it this machine's font files for the fonts each document's text uses",
}
# Affinity is deliberately opt-in (--editors photoshop,patchy,krita,photopea,affinity):
# its driver is background-UIA best-effort and the app's cold-start timing is flaky,
# so default runs stay fast and reliable without it. (Aseprite was verified to have no
# PSD I/O at all and removed from the roster entirely.)
DEFAULT_EDITORS = ["photoshop", "patchy", "krita", "gimp", "photodemon", "photopea"]
# psdtools is opt-in too: a Python PSD library, not an editor, measured for its layer
# compositor and for what a load-then-save keeps (see drivers/psdtools.py).
OPT_IN_EDITORS = ["affinity", "psdtools"]

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
    # Keep configured build commands below normal priority and cap cmake's
    # default parallelism. A batch file avoids nesting another quoted command.
    command_file = config.REPO_ROOT / "build" / "testy-build.cmd"
    command_file.parent.mkdir(parents=True, exist_ok=True)
    command_file.write_text("@echo off\n" + config.BUILD_COMMAND + "\nexit /b %errorlevel%\n", encoding="utf-8")
    completed = subprocess.run(
        ["cmd", "/c", str(config.REPO_ROOT / "scripts/run-throttled.bat"), str(command_file)],
        cwd=config.REPO_ROOT, env=dict(os.environ, CMAKE_BUILD_PARALLEL_LEVEL="20"),
        capture_output=True, text=True, timeout=1200,
    )
    output = (completed.stdout or "") + (completed.stderr or "")
    built = ("ninja: no work to do" in output) or ("Linking CXX executable" in output) or (
        "Building CXX object" in output
    )
    if not built or completed.returncode != 0:
        log(f"WARNING: build failed or produced no compile/link evidence (exit {completed.returncode})")
        log(output[-1500:])
    return built and completed.returncode == 0


_patchy_build_keys: dict[tuple[str, int, int], str] = {}


def patchy_build_key(exe: Path | None, git_fallback: str) -> str:
    """What a cached Patchy cell is keyed on: the contents of patchy.exe itself.

    The git commit used to stand in for it, which threw every Patchy cell away on
    any commit, Testy-only ones included, while the binary had not changed (and
    kept stale cells when the binary was rebuilt from an uncommitted tree). The
    hash is remembered per (path, size, mtime), so a rebuild mid-run is noticed
    without rehashing 40 MB for every cell."""
    if exe is None:
        return git_fallback
    try:
        stat = exe.stat()
        memo = (str(exe), stat.st_size, stat.st_mtime_ns)
        if memo not in _patchy_build_keys:
            _patchy_build_keys[memo] = "exe-" + staging.sha1_of_file(exe)[:20]
        return _patchy_build_keys[memo]
    except OSError:
        return git_fallback


def artifact_dir_name(entry: dict) -> str:
    """The files/<dir> name holding one corpus file's artifacts: its stem, unless
    init_status had to disambiguate it (entry["dir"])."""
    return entry.get("dir") or Path(entry["name"]).stem


def unique_artifact_dirs(corpus: list[Path]) -> list[str | None]:
    """Per corpus file, None when its stem is free, else a distinct directory name.

    Two files with one stem (x.psd beside x.psb, or the same name in two folders)
    would otherwise share files/<stem>/ and overwrite each other's artifacts. The
    first keeps the plain stem, so runs without a clash look as they always did.
    Compared case-insensitively: Windows directories are."""
    taken: set[str] = set()
    names: list[str | None] = []
    for path in corpus:
        candidate = path.stem
        if candidate.lower() not in taken:
            taken.add(candidate.lower())
            names.append(None)
            continue
        candidate = f"{path.stem}~{path.suffix.lstrip('.').lower() or 'file'}"
        counter = 2
        while candidate.lower() in taken:
            candidate = f"{path.stem}~{counter}"
            counter += 1
        taken.add(candidate.lower())
        names.append(candidate)
    return names


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
_spawn_lock = threading.RLock()
_interrupt_requested = threading.Event()
SHUTDOWN_FILE_ENV = "TESTY_SHUTDOWN_FILE"
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
KNOWN_CELL_DIRS = (*DEFAULT_EDITORS, *OPT_IN_EDITORS)


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
        if path == "/testy-rerun-state":
            self._guarded(self._send_rerun_state)
            return
        if path.endswith("/report.html") and Path(self.translate_path(self.path)).is_file():
            # Existing batches get current controls without rewriting their reports.
            body = report._PAGE.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
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
                "allEditors": [*DEFAULT_EDITORS, *OPT_IN_EDITORS],
            }
        )

    def _send_run_state(self) -> None:
        live = _live_run_dir() if _run_in_progress() else None
        self._send_json({
            "running": _run_in_progress(),
            "run": live.name if live is not None else None,
            "pausePending": bool(live is not None and (live / PAUSE_FLAG).exists()),
            "resumable": _resumable(),
            "shuttingDown": self.server.stop_requested.is_set(),
            "shutdownError": self.server.stop_error,
        })

    def _send_rerun_state(self) -> None:
        from urllib.parse import parse_qs, urlparse
        name = parse_qs(urlparse(self.path).query).get("run", [""])[0]
        try:
            target = rerun.run_path(config.RUNS_DIR, name)
        except ValueError as error:
            self._send_json({"errors": [str(error)]}, status=404)
            return
        path = target / rerun.STATE_FILE
        state = json.loads(path.read_text(encoding="utf-8")) if path.exists() else {}
        if state.get("state") == "running" and not _run_in_progress():
            state.update(state="failed", error="Rerun interrupted; previous results kept.")
        self._send_json(state)

    def do_POST(self):  # noqa: N802 - stdlib signature
        from urllib.parse import urlparse

        handler = {
            "/testy-shutdown": self._shutdown,
            "/testy-start-run": self._start_run,
            "/testy-cancel-run": self._cancel_run,
            "/testy-pause-run": self._pause_run,
            "/testy-resume-run": self._resume_run,
            "/testy-delete-runs": self._delete_runs,
            "/testy-retest-file": self._retest_file,
            "/testy-rerun-file": self._rerun_file,
            "/testy-upload": self._upload,
        }.get(urlparse(self.path).path)
        if handler is None:
            self.send_error(404)
            return
        # Serialize shutdown with launches and other mutations. Uploads from the
        # current cell must remain available until the child has finished.
        with _spawn_lock:
            if self.server.stop_requested.is_set() and handler not in (self._shutdown, self._upload):
                self._send_json({"errors": ["Testy is shutting down"]}, status=409)
                return
            self._guarded(handler)

    def _shutdown(self) -> None:
        self.server.request_stop()
        self._send_json({"shuttingDown": True}, status=202)

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
        _child_run_started = _dt.datetime.now()
        _child_run_dir = run_dir
        # A server-owned signal reaches a child even before its status.json exists.
        env = dict(os.environ, **{SHUTDOWN_FILE_ENV: str(self.server.stop_file),
                                 "PYTHONIOENCODING": "utf-8"})
        with open(log_path, "w", encoding="utf-8") as log_file:
            _child_run = subprocess.Popen(
                command, stdout=log_file, stderr=subprocess.STDOUT, env=env,
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
        # Unusable paths are skipped, not fatal: a pasted list of a few hundred files
        # commonly carries a handful of moved or deleted ones, and hand-pruning them
        # to start a run is pure busywork. They are reported back to the panel, and
        # only a list with nothing usable left in it stops the run. The CLI's corpus
        # reader (resolve_corpus) has always behaved this way.
        skipped = []
        if not raw_files:
            errors.append("no PSD files given")
        known_editors = {*DEFAULT_EDITORS, *OPT_IN_EDITORS}
        for editor in editors:
            if editor not in known_editors:
                errors.append(f"unknown editor: {editor}")
        if not editors:
            errors.append("no editors selected")
        files = []
        for entry in raw_files:
            path = Path(entry)
            if path.suffix.lower() not in (".psd", ".psb"):
                skipped.append(f"not a .psd/.psb: {entry}")
            elif not path.exists():
                skipped.append(f"file not found: {entry}")
            else:
                files.append(str(path.resolve()))
        if raw_files and not files:
            errors.append(f"none of the {len(raw_files)} listed paths is a readable .psd/.psb")
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
            self._send_json({"errors": errors, "skipped": skipped}, status=400)
            return
        if skipped:
            log(f"start-run: skipping {len(skipped)} unusable path(s) of {len(raw_files)}")
            for entry in skipped:
                log(f"  {entry}")

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
        self._send_json({"started": True, "skipped": skipped, "files": len(files)})

    def _retest_file(self) -> None:
        """Re-run a single file from an existing run as a fresh run of its own.

        The typical use is checking whether a Patchy fix landed: the child run
        refreshes the Patchy build by default, the Photoshop ground truth and the
        other editors' cells come straight from the caches (fast), and the Patchy
        cell re-measures whenever the rebuilt patchy.exe differs (patchy_build_key).
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

    def _rerun_file(self) -> None:
        request = self._read_json_body()
        if request is None:
            return
        with _spawn_lock:
            if _run_in_progress():
                self._send_json({"errors": ["a run is already in progress"]}, status=409)
                return
            try:
                target = rerun.run_path(config.RUNS_DIR, str(request.get("run") or ""))
                status = json.loads((target / "status.json").read_text(encoding="utf-8"))
                scope = request.get("scope", "patchy")
                if scope not in ("patchy", "all"):
                    raise ValueError("unknown rerun scope")
                editors = status["run"]["editorOrder"] if scope == "all" else ["patchy"]
                job = rerun.prepare(config.RUNS_DIR, target.name, str(request.get("source") or ""), editors)
            except ValueError as error:
                self._send_json({"errors": [str(error)]}, status=400)
                return
            options = job["options"]
            command = _child_run_command(
                f"http://127.0.0.1:{self.server.server_address[1]}", [job["source"]], editors,
                fresh=True, compare=options.get("compare", "strict"), suffix=options.get("suffix"))
            command += ["--apply-to-run", target.name, "--expected-entry", job["expectedEntry"]]
            rerun.note_state(config.RUNS_DIR, job, "running")
            try:
                self._spawn_child(command)
            except Exception as error:
                rerun.note_state(config.RUNS_DIR, job, "failed", error=str(error))
                raise
        self._send_json({"started": True, "run": target.name})

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
            # Kill the whole tree: the run spawns patchy.exe, krita, gimp-console,
            # headless Chrome, and possibly an Affinity instance of its own.
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

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.stop_requested = threading.Event()
        self.stopped = threading.Event()
        self.stop_error = ""
        self.stop_file = config.RUNS_DIR / f".shutdown-{uuid.uuid4().hex}.flag"

    def request_stop(self) -> None:
        # Signal handlers only set an event: never take a lock while interrupting
        # code that may already own it, or raise inside an editor/COM operation.
        self.stop_requested.set()

    def finish_stop(self) -> None:
        self.stop_requested.wait()
        log("shutting down: waiting for the current step to finish and checkpoint")
        while True:
            with _spawn_lock:
                if not _run_in_progress():
                    break
                try:
                    self.stop_file.parent.mkdir(parents=True, exist_ok=True)
                    self.stop_file.touch()
                    self.stop_error = ""
                except OSError as error:
                    self.stop_error = f"Could not request a checkpoint: {error}"
            time.sleep(0.2)
        self.shutdown()
        self.server_close()
        try:
            self.stop_file.unlink(missing_ok=True)
        except OSError as error:
            log(f"could not remove shutdown signal: {error}")
        log("Testy stopped; dashboard port released")
        self.stopped.set()

    def wait_until_stopped(self) -> None:
        # Short waits let Windows deliver console signals to the main thread.
        while not self.stopped.wait(0.2):
            pass


def start_server(port: int) -> tuple[_ExclusiveHTTPServer, int]:
    handler = functools.partial(TestyRequestHandler, directory=str(config.TESTY_ROOT))
    for candidate in range(port, port + 20):
        try:
            server = _ExclusiveHTTPServer(("127.0.0.1", candidate), handler)
        except OSError:
            continue
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        threading.Thread(target=server.finish_stop, daemon=True).start()
        return server, server.server_address[1]
    raise RuntimeError("no free port found for the dashboard")


def _thumb(source: Path, out: Path) -> str | None:
    try:
        analyze.make_thumbnail(source, out)
        return out.name
    except Exception:
        return None


def _version_slug(text: str) -> str:
    """A cache directory name component for a version key. A key too long to keep
    whole ends in a hash of all of it: plain truncation dropped the qualifiers at the
    end of Patchy's key (exe hash, font inventory, then every "-nocacheN" style
    bump), so its cells were reused across changes that should have retired them."""
    slug = re.sub(r"[^A-Za-z0-9.@+-]+", "_", text)
    if len(slug) <= 60:
        return slug
    return slug[:47] + "-" + hashlib.sha1(text.encode("utf-8")).hexdigest()[:12]


def _file_size(path: Path) -> int | None:
    """Bytes on disk, or None for a source that is gone (the report just omits it)."""
    try:
        return path.stat().st_size
    except OSError:
        return None


def file_traits(path: Path) -> dict | None:
    """What the report's "known limitations" switch keys on: the PSD header's bit
    depth and color mode, and whether any layer carries artboard data. None for a
    source that is gone or is not a PSD/PSB."""
    try:
        data = path.read_bytes()
    except OSError:
        return None
    if len(data) < 26 or data[:4] != b"8BPS":
        return None
    traits = {"depth": int.from_bytes(data[22:24], "big"),
              "mode": int.from_bytes(data[24:26], "big")}
    # A type layer ('TySh' block): the Photoshop reference re-renders its text.
    if b"8BIMTySh" in data or b"8B64TySh" in data:
        traits["text"] = True
    # An embedded smart object ('SoLd'): the reference re-renders it from its contents.
    if b"8BIMSoLd" in data or b"8B64SoLd" in data:
        traits["smart"] = True
    # An embedded ICC profile (image resource 1039) that is not plain sRGB.
    if b"8BIM\x04\x0f" in data and b"sRGB IEC61966" not in data:
        traits["profile"] = True
    # Artboard groups carry an 'artb' (or older 'artd') tagged block.
    if any(signature + key in data for signature in (b"8BIM", b"8B64")
           for key in (b"artb", b"artd")):
        traits["artboards"] = True
    return traits


def psd_header_depth(path: Path) -> int | None:
    """Bits per channel from a PSD/PSB header, or None for anything else."""
    try:
        with open(path, "rb") as handle:
            header = handle.read(26)
    except OSError:
        return None
    if len(header) < 26 or header[:4] != b"8BPS":
        return None
    return int.from_bytes(header[22:24], "big")


def save_depth_record(original_depth: int | None, resave_psd: Path) -> dict | None:
    """Did the editor's .psd save keep the document's bit depth? Read from the two
    headers, so it needs no Photoshop and works on cached resaves. A deeper save
    (8-bit written as 16) loses nothing and counts as kept; a 16-bit file saved as
    8-bit has lost its extra precision even when every layer survived."""
    resaved = psd_header_depth(resave_psd)
    if original_depth is None or resaved is None:
        return None
    return {"original": original_depth, "resaved": resaved, "kept": resaved >= original_depth}


def refused_with_reference(entry: dict, cell: dict) -> bool:
    """The editor refused a file Photoshop rendered: a zero for the render averages.
    Harness failures (timeouts, a dead app, breaker skips) are not the editor's
    verdict on the file and stay out, as does any file with no reference render."""
    truth = entry.get("groundTruth") or {}
    return (cell.get("opens") == "fail" and truth.get("state") == "done"
            and bool((truth.get("artifacts") or {}).get("render")))


def text_fonts_missing(truth: dict | None) -> str | None:
    """Why no editor's own text render is scored for this file, or None: Photoshop
    lacks a font the text needs, so it cannot draw the text faithfully either and the
    baked pixels stay the reference. (Results cached before the appended-text leg
    was retired carry the same reason as mutateSkipped.)"""
    if not truth:
        return None
    reason = truth.get("textFontsMissing") or truth.get("mutateSkipped")
    return reason if reason and "fonts unavailable" in reason else None


def reference_space_key(traits: dict | None) -> str:
    """Cache qualifier for files whose Photoshop reference changed: anything not plain
    8-bit RGB or carrying a non-sRGB profile (the probe now saves 8-bit sRGB), and any
    file with type layers (the probe now re-renders their text). Their ground truth
    and every editor's cell are measured afresh; other files keep their caches."""
    if not traits:
        return ""
    key = ""
    if traits.get("depth", 8) != 8 or traits.get("mode", 3) != 3 or traits.get("profile"):
        key += "-srgb1"
    if traits.get("text"):
        key += "-freshtext2"  # the reference re-renders text; its manifest lists every face used
    if traits.get("smart"):
        key += "-freshsmart1"  # and embedded smart objects from their contents
    if traits.get("depth") == 16:
        key += "-deep1"  # the probe also saves 16-bit renders (render16.png, roundtrip16.png)
    return key


class Runner:
    def __init__(self, args: argparse.Namespace) -> None:
        global _in_process_run_dir
        self.args = args
        self.server: _ExclusiveHTTPServer | None = None
        self.suffix = args.suffix
        self._text_fonts: list[str] = []
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
        self.ps = PhotoshopDriver(log=log)
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
                # Who the test files belong to (None for a local corpus): the report
                # and the public export credit them.
                "corpus": fetch_psd_tools_corpus.corpus_credit(corpus),
            },
            "editors": {
                key: {
                    "displayName": info.display_name,
                    "version": info.version,
                    "available": info.available,
                    "notes": info.notes,
                    "textBasis": TEXT_RENDER_BASIS.get(key, (None, None))[0],
                    "textBasisNote": TEXT_RENDER_BASIS.get(key, (None, None))[1],
                    "textHelpNote": TEXT_HELP_NOTES.get(key),
                }
                for key, info in self.editors.items()
            },
            "files": [
                {
                    "name": path.name,
                    "source": str(path),
                    "sizeBytes": _file_size(path),
                    "traits": file_traits(path),
                    "groundTruth": {"state": "pending"},
                    "cells": {key: {"state": "pending"} for key in self.editor_order},
                }
                for path in corpus
            ],
        }
        for entry, name in zip(self.status["files"], unique_artifact_dirs(corpus)):
            if name:
                entry["dir"] = name
        if self.scan_threshold is not None:
            self.status["run"]["scan"] = {"thresholdPct": self.scan_threshold * 100.0}
        if getattr(self.args, "apply_to_run", None):
            if len(corpus) != 1 or not self.args.expected_entry:
                raise ValueError("applying a rerun requires one image and its original result digest")
            self.status["run"]["rerunRequest"] = {
                "target": self.args.apply_to_run, "source": str(corpus[0]),
                "expectedEntry": self.args.expected_entry, "editors": self.editor_order}
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
        gt_dir = self.files_dir / artifact_dir_name(entry) / "_truth"
        gt_dir.mkdir(parents=True, exist_ok=True)
        font_key = self.ps.font_cache_key()
        cache_dir = config.CACHE_DIR / (
            f"gt-{staged.sha1}-{_version_slug(self.ps.version())}-{self.suffix}-fontcheck1-{font_key}"
            f"{reference_space_key(entry.get('traits'))}")
        result_path = cache_dir / "result.json"

        entry["groundTruth"] = {"state": "running", "stage": "Photoshop ground truth"}
        self.push()

        if result_path.exists() and not self.args.fresh:
            result = json.loads(result_path.read_text(encoding="utf-8"))
            for name in ("render.png", "render16.png"):
                if (cache_dir / name).exists():
                    shutil.copyfile(cache_dir / name, gt_dir / name)
        else:
            result = self.ps.probe(staged.original, gt_dir / "render.png")
            if result.get("ok"):
                cache_dir.mkdir(parents=True, exist_ok=True)
                result_path.write_text(json.dumps(result), encoding="utf-8")
                for name in ("render.png", "render16.png"):
                    if (gt_dir / name).exists():
                        shutil.copyfile(gt_dir / name, cache_dir / name)
        if result.get("ok"):
            # (Ground truth cached before the appended-text leg was retired calls it
            # mutateSkipped; only its missing-fonts reason still means anything.)
            result["textFontsMissing"] = text_fonts_missing(result)

        if not result.get("ok"):
            entry["groundTruth"] = {"state": "failed", "error": result.get("error", "unknown")}
            if result.get("launchFailure"):
                # Photoshop never started; this is not a verdict on the file, and the
                # run stops on it rather than repeating it 100 more times.
                entry["groundTruth"]["launchFailure"] = True
            if result.get("fileRejected"):
                # Photoshop ran and refused the file. The Photoshop cell inherits this
                # verdict below, so it carries the classification with it.
                entry["groundTruth"]["fileRejected"] = True
            if result.get("dialogs"):
                entry["groundTruth"]["dialogs"] = result["dialogs"]
            self.push()
            return None

        if self.status["editors"]["photoshop"].get("version") in (None, "", "unknown"):
            self.status["editors"]["photoshop"]["version"] = self.ps.version()

        entry["docSize"] = [int(result["width"]), int(result["height"])]
        entry["layerCount"] = len(result["layers"])
        entry["textLayers"] = sum(1 for layer in result["layers"] if layer.get("kind") == "TEXT")
        # Layers whose stored pixels the cache-free leg removes: text, shapes, fills,
        # smart objects, and the combined raster of a pixel mask plus a vector mask.
        entry["cachedLayers"] = sum(1 for layer in result["layers"]
                                    if layer.get("kind") in CACHED_LAYER_LABELS
                                    or (layer.get("userMask") and layer.get("vectorMask")))
        artifacts = {}
        if (gt_dir / "render.png").exists():
            artifacts["render"] = self._rel(gt_dir / "render.png")
            thumb = _thumb(gt_dir / "render.png", gt_dir / "render_thumb.png")
            if thumb:
                artifacts["renderThumb"] = self._rel(gt_dir / thumb)
        if (gt_dir / "render16.png").exists():
            artifacts["render16"] = self._rel(gt_dir / "render16.png")
        entry["groundTruth"] = {
            "state": "done",
            "render": result.get("render"),
            "textFontsMissing": result.get("textFontsMissing"),
            "missingFonts": result.get("missingFonts", []),
            "artifacts": artifacts,
        }
        # Alerts Photoshop raised while opening the file (the driver acknowledged
        # them so the probe could finish); they say something about the file, so
        # the report shows them rather than swallowing them.
        if result.get("dialogs"):
            entry["groundTruth"]["dialogs"] = result["dialogs"]
            log(f"    Photoshop warned while opening this file: {result['dialogs'][0]}")
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
        cell_dir = self.files_dir / artifact_dir_name(entry) / editor_key
        cell_dir.mkdir(parents=True, exist_ok=True)

        if not info.available:
            error = "editor not found on this machine"
            if editor_key == "psdtools" and info.notes:
                error = info.notes[0]  # names the missing Python packages
            cell.update({"state": "failed", "error": error})
            self.push()
            return

        # The ground truth just opened this exact file with this exact driver. If
        # that failed - after its own restart-and-retry - opening it again fails
        # the same way, and a probe that ends in the hang watchdog costs two
        # minutes plus a Photoshop restart. Take the verdict already in hand.
        if editor_key == "photoshop" and truth is None:
            failed_truth = entry.get("groundTruth", {})
            cell.update({"state": "failed", "opens": "fail",
                         "error": failed_truth.get(
                             "error", "Photoshop could not open this file")})
            self._note_file_rejection(cell, failed_truth)
            self.push()
            return

        version_key = (patchy_build_key(info.exe, self.patchy_hash) if editor_key == "patchy"
                       else info.version)
        if editor_key == "patchy":
            # Its forced-text pixels change when Photoshop's font environment
            # changes too; older cells must not retain an unqualified text score.
            version_key += "-fontcheck1-" + self.ps.font_cache_key()
        if editor_key == "psdtools":
            # Cells cached before the column had its load-and-save leg carry no
            # "data kept" score; the qualifier keeps them from being reused.
            version_key += "-resave1"
        version_key += reference_space_key(entry.get("traits"))
        if editor_key != "photoshop" and entry.get("cachedLayers"):
            # Cells for files with cached layers are scored with those caches removed.
            version_key += "-nocache11"
        if editor_key == "krita":
            version_key += "-settled1"  # exported through the script that waits for rendering
        if editor_key == "photopea" and (entry.get("traits") or {}).get("text"):
            version_key += "-fonts1"  # Photopea is now handed the fonts the text uses
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
            self._skip_unavailable_text_comparison(cell, truth)
            self.push()
            return

        cell.update({"state": "running", "stage": "opening + exporting"})
        self._skip_unavailable_text_comparison(cell, truth)
        self.push()

        render_png = cell_dir / "render.png"
        resave_psd = cell_dir / "resave.psd"
        trap_png = cell_dir / "trap.png"
        artifacts: dict = {}
        cell["artifacts"] = artifacts

        try:
            # The fonts this file's text uses (PostScript names from Photoshop's
            # manifest), for the one editor that has to be handed them: Photopea.
            self._text_fonts = sorted({str(face) for layer in (truth or {}).get("layers", [])
                                       if layer.get("kind") == "TEXT"
                                       for face in (layer.get("fonts") or [layer.get("font")]) if face})
            self._drive_editor(editor_key, info, staged, cell, render_png, resave_psd, trap_png)
        except Exception as error:
            cell.update({"state": "failed", "error": f"driver error: {error}"})
            self.push()
            return

        if cell.get("state") in ("failed", "unsupported"):
            self.push()
            return

        self._no_cache_leg(entry, editor_key, info, staged, cell, cell_dir, render_png, truth)
        if (cell_dir / "render_as_opened.png").exists():
            artifacts["renderAsOpened"] = self._rel(cell_dir / "render_as_opened.png")

        for path, key, thumb_key in (
            (render_png, "render", "renderThumb"),
            (trap_png, "trap", "trapThumb"),
        ):
            if path.exists():
                artifacts[key] = self._rel(path)
                thumb = _thumb(path, path.with_name(path.stem + "_thumb.png"))
                if thumb:
                    artifacts[thumb_key] = self._rel(path.with_name(thumb))
        if resave_psd.exists():
            artifacts["resavePsd"] = self._rel(resave_psd)

        truth_render = self.files_dir / artifact_dir_name(entry) / "_truth" / "render.png"
        document_size = tuple(entry.get("docSize", (0, 0)))

        if truth is not None and render_png.exists() and truth_render.exists() and document_size[0]:
            cell["stage"] = "comparing renders"
            self.push()
            cell["renderMetrics"] = analyze.compare_renders(
                truth_render, render_png, document_size, truth["layers"], cell_dir / "heatmap.png"
            )
            if (cell_dir / "heatmap.png").exists():
                artifacts["heatmap"] = self._rel(cell_dir / "heatmap.png")
            self._apply_text_render_rule(cell)
            truth_render16 = truth_render.with_name("render16.png")
            if truth_render16.exists():
                # An editor that also wrote a 16-bit render (Photoshop's own column) is
                # measured on it; otherwise its render is 8-bit, and its rounding is
                # the precision this measures.
                own16 = cell_dir / "render16.png"
                cell["deepRender"] = analyze.compare_deep_renders(
                    truth_render16, own16 if own16.exists() else render_png, document_size)
            if (cell_dir / "render16.png").exists():
                artifacts["render16"] = self._rel(cell_dir / "render16.png")

        if trap_png.exists():
            cell["trapSentinelFraction"] = round(analyze.sentinel_fraction(trap_png), 4)

        if resave_psd.exists():
            depth = save_depth_record((entry.get("traits") or {}).get("depth"), resave_psd)
            if depth is not None:
                cell["saveDepth"] = depth
        if resave_psd.exists() and truth is not None:
            cell["stage"] = "reopening resave in Photoshop"
            self.push()
            roundtrip = self.ps.probe(resave_psd, cell_dir / "roundtrip.png")
            # A dialog here is Photoshop complaining about what THIS editor wrote.
            if roundtrip.get("dialogs"):
                cell["resaveDialogs"] = roundtrip["dialogs"]
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
                # The precision a save kept: Photoshop's render of the editor's resave
                # against its 16-bit render of the original. A resave still at 16 bits
                # gets a 16-bit render (roundtrip16.png); one saved at 8 bits renders at
                # 8, and that rounding is exactly the loss.
                truth_render16 = truth_render.with_name("render16.png")
                deep_resave = cell_dir / "roundtrip16.png"
                if deep_resave.exists():
                    artifacts["roundtrip16"] = self._rel(deep_resave)
                else:
                    deep_resave = cell_dir / "roundtrip.png"
                if deep_resave.exists() and truth_render16.exists() and document_size[0]:
                    cell["deepRoundtrip"] = analyze.compare_deep_renders(
                        truth_render16, deep_resave, document_size)
            else:
                cell["native"] = {"error": f"Photoshop could not open the resave: {roundtrip.get('error')}"}
                # A Photoshop that never started has not rejected anything: blaming the
                # editor's resave for it would flag the wrong side, so the cell records
                # the error but is neither marked rejected nor cached.
                if roundtrip.get("launchFailure"):
                    cell["uncacheable"] = True
                else:
                    cell["resaveRejected"] = True

        cell["state"] = "done"
        cell["profileAware"] = True
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

    def _render_only(self, editor_key: str, info: config.EditorInfo, source: Path, output: Path,
                     scratch_dir: Path, text_afresh: bool = False,
                     rerender_text: bool = True) -> tuple[bool, dict]:
        """One extra render of `source` by this editor, for the no-cache leg. Returns
        (rendered, details); Photopea's and Patchy's details name the text layers
        their scripted re-render did and did not reach. `text_afresh` asks for that
        re-render where the copy still carries the text's cached pixels (Patchy)."""
        if editor_key == "patchy" and text_afresh:
            result = patchy_driver.render_text_afresh(info.exe, source, output, rerender_text=rerender_text)
            return bool(result["ok"]), result
        if editor_key == "patchy":
            return bool(patchy_driver.export(info.exe, source, output)["ok"]), {}
        if editor_key == "krita":
            return bool(krita_driver.export(info.exe, source, output)["ok"]), {}
        if editor_key == "gimp":
            return bool(gimp_driver.export(info.exe, source, output)["ok"]), {}
        if editor_key == "photodemon":
            return bool(photodemon_driver.export(info.exe, source, output)["ok"]), {}
        if editor_key == "psdtools":
            from drivers import psdtools as psdtools_driver

            return bool(psdtools_driver.export(source, output)["ok"]), {}
        if editor_key == "photopea" and self.server_base is not None:
            from drivers import photopea as photopea_driver

            result = photopea_driver.render_text_afresh(self.server_base, config.TESTY_ROOT, source, output,
                                                        text_fonts=self._text_fonts,
                                                        rerender_text=rerender_text)
            return bool(result.get("ok")) and output.exists(), result
        if editor_key == "affinity":
            from drivers import affinity as affinity_driver

            result = affinity_driver.export_all(
                info.exe, source, None, output, scratch_dir / "nocache_resave.psd",
                scratch_dir / "nocache_unused.png")
            return bool(result.get("ok")) and output.exists(), {}
        return False, {}

    def _no_cache_leg(self, entry: dict, editor_key: str, info: config.EditorInfo,
                      staged: staging.StagedPsd, cell: dict, cell_dir: Path, render_png: Path,
                      truth: dict | None) -> None:
        """Score this editor on what it draws itself, not on Photoshop's cached pixels.

        The editor renders the copy of the file whose cached text, shape, fill and
        smart-object pixels were removed. That render replaces render.png as the one
        that gets scored (the render of the file as opened is kept beside it as
        render_as_opened.png). A cached layer the editor draws nothing for is found by
        comparing with its render of the "plain" copy, where those layers are ordinary
        empty pixel layers: no difference in the layer's box means the layer
        contributed nothing. Such a layer is outlined and labeled in the scored image
        when the blank is the editor's failure (see BLANK_IS_FAILURE); otherwise its
        box keeps the as-opened pixels and the layer is reported as not measured.

        The leg must never mark an editor down for the harness's own mistake, so the
        cell stays scored as opened (noCache.state "not measured", with the reason)
        whenever the extra renders cannot be trusted: the editor cannot open a copy,
        a render comes back at another size, or it differs from the as-opened render
        outside the cached layers, which only a different document would.

        When Photoshop itself lacks a font the text needs, nobody can render that text
        faithfully and its baked pixels stay the reference: the type layers keep their
        cache (the *_fontkept copies), are not re-rendered, and are reported as not
        measured, while shapes, fills, smart objects and masks are still scored."""
        if (editor_key == "photoshop" or truth is None or staged.cache_stripped is None
                or staged.cache_plain is None or not render_png.exists()):
            return
        fonts_missing = text_fonts_missing(truth)
        cached = [layer for layer in truth["layers"]
                  if layer.get("kind") in CACHED_LAYER_LABELS and layer.get("visible", True)
                  and layer.get("bounds")]
        font_kept_text: list[str] = []
        if fonts_missing:
            font_kept_text = [layer.get("name", "") for layer in cached if layer["kind"] == "TEXT"]
            cached = [layer for layer in cached if layer["kind"] != "TEXT"]
        # A layer with a pixel mask and a vector mask lost the stored combination of the
        # two in these copies: the editor has to rasterize the vector mask itself, so
        # its render may differ there too. Nothing is labeled; it is simply scored.
        combined_masks = [layer["bounds"] for layer in truth["layers"]
                          if layer.get("userMask") and layer.get("vectorMask") and layer.get("bounds")]
        if not cached and not combined_masks:
            return
        stripped_copy, plain_copy = staged.cache_stripped, staged.cache_plain
        text_kept = editor_key in TEXT_CACHE_KEPT
        if text_kept:
            # (A file whose only cached layers are text has no such copies: the
            # original already is one.)
            stripped_copy = staged.cache_stripped_text_kept or staged.original
            plain_copy = staged.cache_plain_text_kept or staged.original
        elif fonts_missing:
            stripped_copy, plain_copy = staged.cache_stripped_font_kept, staged.cache_plain_font_kept
            if stripped_copy is None or plain_copy is None:
                return  # nothing but text was cached
        cell["stage"] = "rendering without Photoshop's cached pixels"
        self.push()
        boxes = [layer["bounds"] for layer in cached]

        def void(reason: str) -> None:
            cell["noCache"] = {"state": "not measured", "reason": reason, "cachedLayers": len(cached)}
            log(f"    no-cache leg not measured for {editor_key}: {reason}")

        def render(source: Path, name: str, text_afresh: bool = False) -> tuple[Path | None, dict, str]:
            """(render, driver details, why it cannot be trusted or "")."""
            output = cell_dir / name
            problem = ""
            details: dict = {}
            for _attempt in range(2):
                try:
                    output.unlink(missing_ok=True)
                    ok, details = self._render_only(editor_key, info, source, output, cell_dir, text_afresh,
                                                    rerender_text=not fonts_missing)
                except Exception as error:
                    ok = False
                    details = {"error": str(error)}
                for leftover in ("nocache_resave.psd", "nocache_unused.png"):
                    try:
                        (cell_dir / leftover).unlink()
                    except OSError:
                        pass
                if not ok or not output.exists():
                    problem = "the editor could not open the copy of this file with its caches removed"
                    continue
                outside = analyze.changed_outside_boxes(render_png, output, boxes + combined_masks)
                if outside is None:
                    problem = "its render of the cache-free copy came back at a different size"
                elif outside > NO_CACHE_OUTSIDE_LIMIT and analyze.sentinel_fraction(output) < 0.5:
                    problem = (f"its render of the cache-free copy differs from its normal render "
                               f"outside the cached layers ({outside:.0%} of those pixels)")
                else:
                    return output, details, ""
            return None, details, problem

        try:
            stripped_render, details, problem = render(stripped_copy, "nocache.png", text_afresh=text_kept)
            if stripped_render is None:
                void(problem)
                return
            # With no layer left that has pixels, some editors show the file's
            # flattened image instead, which the copies replace with the sentinel.
            showed_composite = (analyze.sentinel_fraction(stripped_render) >= 0.5
                                and analyze.sentinel_fraction(render_png) < 0.5)
            opened_vs_stripped = analyze.boxes_changed(render_png, stripped_render, boxes) or []
            blank: list[int] = []
            if showed_composite:
                blank = list(range(len(cached)))
            elif any(fraction > NO_CACHE_BOX_UNCHANGED for fraction in opened_vs_stripped):
                # (Identical to the as-opened render in every box means every layer
                # was drawn the same way either way: nothing to find.)
                plain_render, _plain_details, problem = render(plain_copy, "nocache_plain.png")
                if plain_render is None:
                    void(problem)
                    return
                contributed = analyze.boxes_changed(plain_render, stripped_render, boxes) or []
                # Blank: nothing drawn for the layer, and that is a visible loss (a
                # layer nobody can see in the as-opened render is no finding).
                blank = [index for index, fraction in enumerate(contributed)
                         if 0 <= fraction <= NO_CACHE_BOX_UNCHANGED
                         and opened_vs_stripped[index] > NO_CACHE_BOX_UNCHANGED]

            failures = BLANK_IS_FAILURE.get(editor_key, ())
            unreached: set[tuple[str, str]] = set()
            if editor_key == "photopea" or text_kept:
                scripted = ("TEXT", "SMARTOBJECT") if text_kept else ("TEXT",)
                unreached = {("TEXT", name) for name in details.get("failed") or []}
                unreached |= {("SMARTOBJECT", name) for name in details.get("smartFailed") or []}
                if details.get("error"):
                    unreached = {(layer["kind"], layer.get("name", "")) for layer in cached
                                 if layer["kind"] in scripted}
            unreached_reason = TEXT_CACHE_KEPT.get(
                editor_key, "Photopea's scripted edit did not reach this text layer")
            failed: list[int] = []
            unmeasured: list[int] = []
            reasons: list[str] = []
            for index, layer in enumerate(cached):
                kind = layer["kind"]
                if (kind, layer.get("name", "")) in unreached:
                    # Never edited, so the editor never drew it: blank or not, the
                    # box says nothing about its engine.
                    unmeasured.append(index)
                    reasons.append(unreached_reason)
                elif index not in blank:
                    continue
                elif kind in NOT_MEASURED_REASON and kind not in failures:
                    unmeasured.append(index)
                    reasons.append(NOT_MEASURED_REASON[kind])
                else:
                    failed.append(index)
            opened_copy = cell_dir / "render_as_opened.png"
            shutil.copyfile(render_png, opened_copy)
            analyze.compose_scored_render(
                render_png, opened_copy if showed_composite else stripped_render,
                [(cached[index]["bounds"], CACHED_LAYER_LABELS[cached[index]["kind"]]) for index in failed],
                opened_copy, [cached[index]["bounds"] for index in unmeasured],
                clear_placeholders=showed_composite)
            cell["noCache"] = {
                "state": "done", "cachedLayers": len(cached) + len(font_kept_text),
                "notRendered": [cached[index].get("name", "") for index in failed],
                "notMeasured": [cached[index].get("name", "") for index in unmeasured] + font_kept_text,
            }
            if font_kept_text:
                reasons.append(f"Photoshop lacks a font this text needs, so its baked pixels stay ({fonts_missing})")
            text_failed = [cached[index].get("name", "") for index in failed
                           if cached[index]["kind"] == "TEXT"]
            if text_failed:
                cell["noCache"]["textNotRendered"] = text_failed
            if showed_composite:
                cell["noCache"]["showedComposite"] = True
            if unmeasured or font_kept_text:
                cell["noCache"]["notMeasuredReason"] = reasons[0]
            if failed:
                log(f"    no-cache leg: draws nothing for {len(failed)} of {len(cached)} cached layer(s)")
        except Exception as error:
            void(f"harness error: {str(error)[:200]}")

    @staticmethod
    def _apply_text_render_rule(cell: dict) -> bool:
        """An editor that cannot render a Photoshop text object (it can only show the
        pixels Photoshop cached in the file) scores 0% for the file's render, whatever
        the rest of the picture looks like. The measured numbers stay under
        renderMetrics.measured; renderMetrics.textNotRendered names the layers.
        Returns True when it changed the cell."""
        names = (cell.get("noCache") or {}).get("textNotRendered")
        metrics = cell.get("renderMetrics")
        if not names or not metrics or "textNotRendered" in metrics:
            return False
        perceptual = metrics.get("perceptual") or {}
        metrics["measured"] = {
            "accuracy": metrics.get("accuracy"), "badFraction": metrics.get("badFraction"),
            "perceptualAccuracy": perceptual.get("accuracy"),
            "perceptualBadFraction": perceptual.get("badFraction"),
        }
        metrics["accuracy"], metrics["badFraction"] = 0.0, 1.0
        if perceptual:
            perceptual["accuracy"], perceptual["badFraction"] = 0.0, 1.0
        metrics["textNotRendered"] = list(names)
        return True

    @staticmethod
    def _skip_unavailable_text_comparison(cell: dict, truth: dict | None) -> None:
        """Note on the cell when Photoshop lacks a font the file's text needs (no
        editor's own text render is scored then), and drop what a cell cached before
        the appended-text leg was retired still carries from it."""
        missing = text_fonts_missing(truth)
        if missing:
            cell["textRenderSkipped"] = missing
        cell.pop("textRender", None)
        cell.pop("mutateError", None)
        for key in ("mutated", "mutatedThumb"):
            (cell.get("artifacts") or {}).pop(key, None)

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
            truth_render = self.files_dir / artifact_dir_name(entry) / "_truth" / "render.png"
            render_png = cell_dir / "render.png"
            document_size = tuple(entry.get("docSize", (0, 0)))
            if truth_render.exists() and render_png.exists() and document_size and document_size[0]:
                log("    upgrading cached metrics with the perceptual comparison")
                cell["renderMetrics"] = analyze.compare_renders(
                    truth_render, render_png, document_size, truth["layers"], None
                )
                changed = True
        if not cell.get("profileAware"):
            # Cells scored before the comparison honored embedded ICC profiles: only a
            # render that carries one can have changed, so only those are re-scored.
            cell["profileAware"] = True
            changed = True
            truth_render = self.files_dir / artifact_dir_name(entry) / "_truth" / "render.png"
            render_png = cell_dir / "render.png"
            document_size = tuple(entry.get("docSize", (0, 0)))
            if (truth is not None and cell.get("renderMetrics") and truth_render.exists()
                    and render_png.exists() and document_size and document_size[0]
                    and analyze.has_embedded_profile(render_png)):
                log("    re-scoring a cached render that carries an ICC profile")
                cell["renderMetrics"] = analyze.compare_renders(
                    truth_render, render_png, document_size, truth["layers"], None
                )
        # Cells cached before saved bit depth was measured: read it from the cached
        # resave's header.
        if "saveDepth" not in cell and (cell_dir / "resave.psd").exists():
            depth = save_depth_record((entry.get("traits") or {}).get("depth"), cell_dir / "resave.psd")
            if depth is not None:
                cell["saveDepth"] = depth
                changed = True
        # Cells compared under older layer-pairing rules: compare again from the stored
        # manifest of the resave (no editor or Photoshop run needed).
        native = cell.get("native")
        stored = cell_dir / "roundtrip_manifest.json"
        if (truth is not None and isinstance(native, dict) and "nativeScore" in native
                and native.get("matching") != manifest_mod.MATCHING_VERSION and stored.exists()):
            try:
                resaved_layers = json.loads(stored.read_text(encoding="utf-8"))
                cell["native"] = manifest_mod.compare_manifests(truth["layers"], resaved_layers)
                changed = True
            except (OSError, ValueError):
                pass
        # Cells cached before the two text rules (no text saved as text, no text
        # rendered: 0% for that score) are brought up to date in place.
        if manifest_mod.apply_text_save_rule(cell.get("native")):
            changed = True
        if self._apply_text_render_rule(cell):
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

    @staticmethod
    def _note_file_rejection(cell: dict, result: dict) -> None:
        """The editor ran and refused this one file. That is real news about the file,
        but no evidence the editor is broken, so the circuit breaker must not count it:
        three unrelated files an importer dislikes can sit next to each other in a
        corpus and look exactly like a dead app (Krita, July 2026)."""
        if result.get("fileRejected"):
            cell["fileRejected"] = True

    def _drive_editor(
        self,
        editor_key: str,
        info: config.EditorInfo,
        staged: staging.StagedPsd,
        cell: dict,
        render_png: Path,
        resave_psd: Path,
        trap_png: Path,
    ) -> None:
        if editor_key == "photoshop":
            result = self.ps.probe(staged.original, render_png, resave_psd=resave_psd)
            # This probe also SAVES a PSD, and Photoshop's save-time warnings are
            # modal too ("contains nested layer groups that may change in
            # appearance..."). The ground truth never saves, so a dialog seen here
            # is one only this leg raises and would otherwise go unrecorded.
            dialogs = list(result.get("dialogs") or [])
            if not result.get("ok"):
                cell.update({"state": "failed", "opens": "fail", "error": result.get("error")})
                self._note_file_rejection(cell, result)
                return
            cell["opens"] = "ok" if result.get("render") == "ok" else "fallback-render"
            if staged.trap is not None:
                trap_result = self.ps.probe(staged.trap, trap_png)
                dialogs += [d for d in trap_result.get("dialogs") or [] if d not in dialogs]
                if not trap_result.get("ok"):
                    cell["trapError"] = trap_result.get("error")
            if dialogs:
                cell["dialogs"] = dialogs
            return

        if editor_key == "patchy":
            exported = patchy_driver.export(info.exe, staged.original, render_png)
            if not exported["ok"]:
                cell.update({"state": "failed", "opens": "fail",
                             "error": patchy_driver.failure_text(exported)})
                self._note_file_rejection(cell, exported)
                return
            cell["opens"] = "ok"
            resaved = patchy_driver.export(info.exe, staged.original, resave_psd)
            if not resaved["ok"]:
                cell["resaveError"] = patchy_driver.failure_text(resaved)
            if staged.trap is not None:
                patchy_driver.export(info.exe, staged.trap, trap_png)
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
                self._note_file_rejection(cell, exported)
                return
            cell["opens"] = "ok"
            resaved = krita_driver.export(info.exe, staged.original, resave_psd)
            if not resaved["ok"]:
                detail = resaved["stderr"] or f"exit {resaved['exitCode']}, no output"
                cell["resaveError"] = f"opened, but Krita's PSD export failed ({detail})"
            krita_notes = list(exported.get("notes") or []) + [
                f"resave: {note}" for note in resaved.get("notes") or []]
            if krita_notes:
                cell["driverNotes"] = krita_notes
            if staged.trap is not None:
                krita_driver.export(info.exe, staged.trap, trap_png)
            return

        if editor_key == "gimp":
            # Same fused open+export CLI shape as Krita: a failed PNG leg means the
            # PSD IMPORT failed, and a failed resave after a good render means the
            # PSD EXPORT did.
            exported = gimp_driver.export(info.exe, staged.original, render_png)
            if not exported["ok"]:
                detail = exported["stderr"] or f"exit {exported['exitCode']}, no output"
                cell.update({"state": "failed", "opens": "fail",
                             "error": f"failed to open the PSD (GIMP import error; {detail})"})
                self._note_file_rejection(cell, exported)
                return
            cell["opens"] = "ok"
            resaved = gimp_driver.export(info.exe, staged.original, resave_psd)
            if not resaved["ok"]:
                detail = resaved["stderr"] or f"exit {resaved['exitCode']}, no output"
                cell["resaveError"] = f"opened, but GIMP's PSD export failed ({detail})"
            if staged.trap is not None:
                gimp_driver.export(info.exe, staged.trap, trap_png)
            return

        if editor_key == "photodemon":
            # Same fused open+export CLI shape as Krita and GIMP: a failed PNG leg
            # means the PSD IMPORT failed, and a failed resave after a good render
            # means the PSD EXPORT did. A leg whose export completed before the
            # process died scores ok, with the crash kept as a driver note.
            notes = []
            exported = photodemon_driver.export(info.exe, staged.original, render_png)
            if exported["note"]:
                notes.append(f"render: {exported['note']}")
            if not exported["ok"]:
                detail = exported["stderr"] or f"exit {exported['exitCode']}, no output"
                cell.update({"state": "failed", "opens": "fail",
                             "error": f"failed to open the PSD (PhotoDemon import error; {detail})"})
                self._note_file_rejection(cell, exported)
                return
            cell["opens"] = "ok"
            resaved = photodemon_driver.export(info.exe, staged.original, resave_psd)
            if resaved["note"]:
                notes.append(f"resave: {resaved['note']}")
            if not resaved["ok"]:
                detail = resaved["stderr"] or f"exit {resaved['exitCode']}, no output"
                cell["resaveError"] = f"opened, but PhotoDemon's PSD export failed ({detail})"
            if staged.trap is not None:
                trapped = photodemon_driver.export(info.exe, staged.trap, trap_png)
                if trapped["note"]:
                    notes.append(f"trap: {trapped['note']}")
            if notes:
                cell["driverNotes"] = notes
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
                progress=lambda stage: (cell.__setitem__("stage", stage), self.push()),
                text_fonts=self._text_fonts,
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
                self._note_file_rejection(cell, result)
                return
            cell["opens"] = result.get("opens", "ok")
            if result.get("notes"):
                cell["driverNotes"] = result["notes"]
            if result.get("cacheable") is False:
                cell["uncacheable"] = True
            return

        if editor_key == "psdtools":
            from drivers import psdtools as psdtools_driver

            exported = psdtools_driver.export(staged.original, render_png)
            if exported["note"]:
                cell["driverNotes"] = [f"render: {exported['note']}"]
            if not exported["ok"]:
                detail = exported["stderr"] or f"exit {exported['exitCode']}, no output"
                cell.update({"state": "failed", "opens": "fail",
                             "error": f"psd-tools could not render the PSD ({detail})"})
                self._note_file_rejection(cell, exported)
                return
            cell["opens"] = "ok"
            resaved = psdtools_driver.export(staged.original, resave_psd)
            if not resaved["ok"]:
                detail = resaved["stderr"] or f"exit {resaved['exitCode']}, no output"
                cell["resaveError"] = f"opened, but psd-tools could not save the PSD ({detail})"
            # The trap leg checks the driver's one promise: force=True really
            # composites the layers instead of returning the baked preview.
            if staged.trap is not None:
                psdtools_driver.export(staged.trap, trap_png)
            return

        cell.update({"state": "failed", "error": f"no driver for editor '{editor_key}'"})

    # ---------- scan mode ----------

    # Exactly the artifact names a run writes into each per-file directory. Scan-mode
    # cleanup deletes ONLY these names, one by one, and removes directories with
    # rmdir (which refuses non-empty ones) - never a wildcard, glob, or recursive
    # delete - so anything unexpected inside a run directory survives and is logged.
    SCRUB_STAGED = ("original.psd", "original.psb", "trap.psd", "trap.psb",
                    "nocache.psd", "nocache.psb", "nocache_plain.psd", "nocache_plain.psb",
                    "nocache_textkept.psd", "nocache_textkept.psb",
                    "nocache_fontkept.psd", "nocache_fontkept.psb",
                    "nocache_plain_fontkept.psd", "nocache_plain_fontkept.psb",
                    "nocache_plain_textkept.psd", "nocache_plain_textkept.psb")
    SCRUB_TRUTH = ("render.png", "render_thumb.png", "render16.png", "mutated.png",
                   "mutated_thumb.png", "manifest.json")
    SCRUB_CELL = ("render.png", "render_thumb.png", "render_as_opened.png", "nocache.png",
                  "nocache_plain.png", "nocache_resave.psd", "nocache_unused.png", "resave.psd", "trap.png",
                  "trap_thumb.png", "mutated.png", "mutated_thumb.png", "heatmap.png",  # mutated*: older runs
                  "roundtrip.png", "roundtrip_thumb.png", "roundtrip_manifest.json",
                  "render16.png", "roundtrip16.png")

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
                        f"{name}: perceptually wrong on "
                        f"{perceptual['badFraction'] * 100:.1f}% of pixels "
                        f"(over {self.scan_threshold * 100:g}%; "
                        f"byte difference {metrics['badFraction'] * 100:.1f}%)"
                    )
            elif metrics.get("badFraction", 1.0) > self.scan_threshold:
                reasons.append(
                    f"{name}: byte difference on {metrics['badFraction'] * 100:.1f}% of pixels "
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
        stem = artifact_dir_name(entry)
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
        basis = ("differs perceptually from Photoshop's"
                 if self.compare_mode == "perceptual"
                 else "differs byte-wise from Photoshop's")
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
        parent_stop = os.environ.get(SHUTDOWN_FILE_ENV)
        return (_interrupt_requested.is_set()
                or bool(self.server and self.server.stop_requested.is_set())
                or bool(parent_stop and Path(parent_stop).exists())
                or (self.run_dir / PAUSE_FLAG).exists())

    def _photoshop_unavailable_exit(self) -> int:
        """Photoshop stopped launching. Ground truth is the baseline every column is
        scored against, so the rest of the run could only produce empty cells while
        still buying two launch timeouts per file. Checkpoint it like a pause: fix
        Photoshop, resume, and nothing measured so far is lost."""
        reason = self.ps.unavailable_reason or "Photoshop is unavailable"
        log(f"ERROR: {reason}")
        log("    ground truth is the baseline for every editor, so the run stops here "
            "instead of grinding through the rest of the corpus for nothing")
        self.status["run"].setdefault("notes", []).append(reason)
        # A launch failure is a verdict on the machine, not on the file it landed on.
        # Hand those files back as pending so the resume measures them properly - cells
        # included, since a cell scored while ground truth was missing carries no
        # comparison at all and keeping it would freeze half a file into the report.
        for entry in self.status["files"]:
            if entry.get("groundTruth", {}).get("launchFailure"):
                entry["groundTruth"] = {"state": "pending"}
                for cell in entry.get("cells", {}).values():
                    cell.clear()
                    cell["state"] = "pending"
        return self._graceful_pause_exit("Photoshop unavailable - checkpointing the run")

    def _graceful_pause_exit(
        self, why: str = "pause requested - checkpointing at the cell boundary"
    ) -> int:
        """Checkpoint between cells: everything finished so far is already flushed to
        status.json, so recording the state and exiting IS the checkpoint."""
        log(why)
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
            # Runs started before the report showed file sizes carry none; one stat
            # per file fills them in, so a resume upgrades the whole report.
            if entry.get("sizeBytes") is None:
                entry["sizeBytes"] = _file_size(Path(entry["source"]))
            if "traits" not in entry:
                entry["traits"] = file_traits(Path(entry["source"]))
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

        if self.args.server_url:
            # A controlling server (start-testy.bat's serve.py) already serves the testy
            # root and the upload endpoint; reuse it instead of binding a second port.
            self.server_base = self.args.server_url.rstrip("/")
            log(f"dashboard: {self.server_base}/runs/{self.run_name}/report.html (parent server)")
        elif not self.args.no_serve:
            server, port = start_server(self.args.port)
            self.server = server
            self.server_port = port
            self.server_base = f"http://127.0.0.1:{port}"
            url = f"{self.server_base}/runs/{self.run_name}/report.html"
            log(f"dashboard: {url} (run index at {self.server_base}/)")
            if not self.args.no_browser:
                webbrowser.open(url)
        if not self.resume:  # a resumed run was indexed when it first started
            report.append_run_index(config.TESTY_ROOT, self.run_name)

        if self._pause_requested():
            return self._graceful_pause_exit()

        if not self.args.no_build and "patchy" in self.editor_order:
            if not refresh_patchy_build():
                raise RuntimeError("Patchy build failed; previous results kept")
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
            staged = staging.stage_psd(source, self.files_dir / artifact_dir_name(entry) / "_staged")
            entry["sha1"] = staged.sha1
            if staged.trap_error:
                entry["trapError"] = staged.trap_error
            if staged.trap_skipped:
                entry["trapSkipped"] = staged.trap_skipped
            truth = self.ground_truth(index, staged)
            if self.ps.unavailable:
                return self._photoshop_unavailable_exit()
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
                # Only failures OF THE EDITOR count. A cell where the editor ran and
                # refused the file proves the opposite (it is alive and answering), so
                # it clears the count the same way a success does.
                if cell.get("state") == "failed" and not cell.get("fileRejected"):
                    consecutive_failures[editor_key] += 1
                    if consecutive_failures[editor_key] >= BREAKER_LIMIT:
                        log(f"    {editor_key} failed {BREAKER_LIMIT} files in a row without "
                            "running - skipping it for the rest of the run")
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
        if self.status["run"].get("rerunRequest"):
            rerun.apply(config.RUNS_DIR, self.run_dir, self.status["run"]["rerunRequest"],
                        summarize=self.summarize_status, scan_reasons=self.scan_reasons_for_status)
        self._print_summary()

        return 0

    def _aggregate(self) -> dict:
        aggregate: dict = {}
        for editor_key in self.editor_order:
            opened = total = bad_saves = 0
            render_scores: list[float] = []
            visual_scores: list[float] = []
            native_scores: list[float] = []
            # Deep (16/32-bit) files only: saves that kept the bit depth, and the
            # 16-bit precision of the render and of Photoshop's render of the resave.
            depth_kept = depth_total = 0
            deep_render: list[float] = []
            deep_roundtrip: list[float] = []
            for entry in self.status["files"]:
                cell = entry["cells"].get(editor_key)
                if not cell or cell.get("state") in ("pending", "running", "skipped"):
                    continue
                total += 1
                if cell.get("state") == "done" and cell.get("opens") != "fail":
                    opened += 1
                if cell.get("resaveRejected"):
                    bad_saves += 1
                metrics = cell.get("renderMetrics")
                if metrics:
                    render_scores.append(metrics["accuracy"])
                    if metrics.get("perceptual"):
                        visual_scores.append(metrics["perceptual"]["accuracy"])
                elif refused_with_reference(entry, cell):
                    # A file the editor would not open scores zero, not "no data":
                    # leaving it out let an editor raise its average by refusing files.
                    render_scores.append(0.0)
                    visual_scores.append(0.0)
                native = cell.get("native")
                if native and "nativeScore" in native:
                    native_scores.append(native["nativeScore"])
                save_depth = cell.get("saveDepth")
                if save_depth and save_depth.get("original", 8) > 8:
                    depth_total += 1
                    depth_kept += 1 if save_depth.get("kept") else 0
                for key, scores in (("deepRender", deep_render), ("deepRoundtrip", deep_roundtrip)):
                    deep = cell.get(key)
                    if deep and deep.get("state") == "done":
                        scores.append(deep["accuracy"])
            aggregate[editor_key] = {
                "opened": opened,
                "total": total,
                "badSaves": bad_saves,
                "render": sum(render_scores) / len(render_scores) if render_scores else 0.0,
                "visual": sum(visual_scores) / len(visual_scores) if visual_scores else 0.0,
                "native": sum(native_scores) / len(native_scores) if native_scores else 0.0,
                # False when no cell produced a resave score (an editor without that
                # leg, or a run that never got one): "native" is then a placeholder 0.
                "nativeMeasured": bool(native_scores),
                "depthKept": depth_kept,
                "depthTotal": depth_total,
                "deepRender": sum(deep_render) / len(deep_render) if deep_render else None,
                "deepRoundtrip": sum(deep_roundtrip) / len(deep_roundtrip) if deep_roundtrip else None,
            }
        return aggregate

    def _history_summary(self) -> dict:
        return {
            "run": self.run_name,
            "files": len(self.status["files"]),
            "patchy": self.patchy_hash,
            "editors": self._aggregate(),
        }

    @classmethod
    def summarize_status(cls, status: dict) -> dict:
        view = object.__new__(cls)
        view.status = status
        view.editor_order = status["run"]["editorOrder"]
        view.run_name = status["run"]["name"]
        view.patchy_hash = status["run"].get("patchyGit", "unknown")
        summary = view._history_summary()
        summary["reruns"] = status["run"].get("reruns", [])
        return summary

    @classmethod
    def scan_reasons_for_status(cls, status: dict, entry: dict) -> list[str]:
        view = object.__new__(cls)
        view.scan_threshold = status["run"]["scan"]["thresholdPct"] / 100.0
        view.compare_mode = status["run"].get("compare", "strict")
        view.editor_order = status["run"]["editorOrder"]
        view.editors = {k: types.SimpleNamespace(display_name=v.get("displayName", k))
                        for k, v in status["editors"].items()}
        return view._scan_flag_reasons(entry)

    def _print_summary(self) -> None:
        aggregate = self._aggregate()
        log("summary (mean byte match / perceptual match / data kept in .psd save / opened):")
        for editor_key in self.editor_order:
            a = aggregate[editor_key]
            log(
                f"  {self.editors[editor_key].display_name:<10} "
                f"byte {a['render'] * 100:5.1f}%   perceptual {a['visual'] * 100:5.1f}%   "
                + (f"kept {a['native'] * 100:5.1f}%   " if a["nativeMeasured"] else "kept     -    ")
                +
                f"opened {a['opened']}/{a['total']}"
                + (f"   bad .psd saves {a['badSaves']}" if a["badSaves"] else "")
                + (f"   deep saves kept depth {a['depthKept']}/{a['depthTotal']}" if a["depthTotal"] else "")
                + (f"   16-bit precise {a['deepRoundtrip'] * 100:.1f}% (resave)"
                   if a["deepRoundtrip"] is not None else "")
            )
        if self.scan_threshold is not None:
            flagged = [e for e in self.status["files"] if e.get("scan", {}).get("flagged")]
            basis = "perceptual" if self.compare_mode == "perceptual" else "byte"
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
    parser.add_argument("--apply-to-run", default=None, help="apply a completed one-image rerun to this batch")
    parser.add_argument("--expected-entry", default=None, help=argparse.SUPPRESS)
    parser.add_argument("--port", type=int, default=config.PORT, help="dashboard port")
    parser.add_argument("--no-browser", action="store_true", help="do not auto-open the dashboard")
    parser.add_argument("--no-serve", action="store_true", help="write reports without the local server")
    parser.add_argument("--server-url", default=None,
                        help="reuse an already-running Testy server (browser-spawned runs)")
    parser.add_argument("--exit-when-done", action="store_true", help="stop the dashboard when the run ends")
    args = parser.parse_args()
    if args.scan is not None and not 0.0 <= args.scan <= 100.0:
        parser.error("--scan threshold must be a percentage between 0 and 100")
    global _in_process_run_active
    _in_process_run_active = True
    _interrupt_requested.clear()
    runner = None

    def request_stop(_signum, _frame):
        _interrupt_requested.set()
        if runner is not None and runner.server is not None:
            runner.server.request_stop()

    previous_sigint = signal.signal(signal.SIGINT, request_stop)
    try:
        runner = Runner(args)
        result = runner.run()
        if result and runner.status.get("run", {}).get("rerunRequest"):
            rerun.note_state(config.RUNS_DIR, runner.status["run"]["rerunRequest"], "failed",
                             error="Rerun did not complete; previous results kept.")
        _in_process_run_active = False
        if result == 0 and runner.server is not None and not args.exit_when_done:
            log("run complete - dashboard stays up; use Shut down Testy or Ctrl+C to quit")
            runner.server.wait_until_stopped()
        return result
    except Exception as error:
        job = runner.status.get("run", {}).get("rerunRequest") if runner else None
        if job:
            rerun.note_state(config.RUNS_DIR, job, "failed", error=str(error))
        raise
    finally:
        _in_process_run_active = False
        if runner is not None and runner.server is not None:
            runner.server.request_stop()
            runner.server.wait_until_stopped()
        signal.signal(signal.SIGINT, previous_sigint)


if __name__ == "__main__":
    sys.exit(main())
