"""A Photoshop on another machine, for the reopen checks (photoshop_agent.py there).

Speaks the agent's folder protocol over a file share: each probe copies the PSD into
the run's inbox on the host, drops a job file and waits for the result. The agent is
started over ssh through testy/remote/start-agent.ps1 (a temporary interactive
scheduled task, because COM only works from the logged-on console session) and
stopped by quit(). Host settings (ssh target, share, the matching local folder on the
host, user, python) come from config.local.json's photoshop.hosts; nothing about a
machine is stored in this repository. With an empty "ssh" the agent runs on this PC
as a plain child process, which is how the protocol is tested without a second box.

Only open checks are supported (render_png must be None): nothing comes back but the
driver's verdict and layer manifest.
"""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path, PureWindowsPath
from typing import Callable

TESTY_ROOT = Path(__file__).resolve().parent.parent

# The agent's code, as published to the host: (source, path under <run>/agent).
AGENT_FILES = (
    (TESTY_ROOT / "photoshop_agent.py", "photoshop_agent.py"),
    (TESTY_ROOT / "win_dialogs.py", "win_dialogs.py"),
    (TESTY_ROOT / "drivers" / "__init__.py", "drivers/__init__.py"),
    (TESTY_ROOT / "drivers" / "photoshop.py", "drivers/photoshop.py"),
    (TESTY_ROOT / "remote" / "start-agent.ps1", "start-agent.ps1"),
)

START_SECONDS = 120      # task registration plus a cold Python on the host
STALE_SECONDS = 60       # no heartbeat this long (it beats every 5s): the agent is gone
RESULT_SECONDS = 900     # a probe that hangs, restarts and retries stays well inside
QUIT_SECONDS = 90        # the agent quits its Photoshops with a 30s grace each
TASK_NAME = "testy-photoshop-agent"


def _write_json(path: Path, value: dict) -> None:
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(value), encoding="utf-8")
    os.replace(temporary, path)


def _read_json(path: Path) -> dict | None:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None


class RemotePhotoshop:
    """The PhotoshopDriver surface the reopen checks use: probe(path, None),
    version(), unavailable / unavailable_reason, quit()."""

    def __init__(self, install, host: dict, run_name: str,
                 log: Callable[[str], None] | None = None) -> None:
        self.install = install
        self.ssh = str(host.get("ssh") or "")
        self.user = str(host.get("user") or "")
        self.python = str(host.get("python") or (sys.executable if not self.ssh else "python"))
        folder = f"testy-{run_name}"
        self.share = Path(host["share"]) / folder
        self.remote = PureWindowsPath(host.get("dir") or host["share"]) / folder
        self._log = log
        self._started = False
        self._process: subprocess.Popen | None = None
        self._last_beat: tuple[int, float] = (-1, 0.0)
        self._version: str | None = None
        self._unavailable_reason: str | None = None
        self._jobs = 0

    # ---------- the PhotoshopDriver surface ----------

    @property
    def unavailable(self) -> bool:
        return self._unavailable_reason is not None

    @property
    def unavailable_reason(self) -> str | None:
        return self._unavailable_reason

    def version(self) -> str:
        """The remote exe's file version, from the agent's heartbeat (no Photoshop launch)."""
        if self._version is None and not self.unavailable:
            self._ensure_agent()
        return self._version or "unknown"

    def probe(self, psd_path: Path, render_png: Path | None = None,
              resave_psd: Path | None = None) -> dict:
        if render_png is not None or resave_psd is not None:
            raise ValueError("a remote Photoshop only runs open checks")
        if not self._ensure_agent():
            return {"ok": False, "agentFailure": True, "error": self._unavailable_reason}
        self._jobs += 1
        digest = hashlib.sha1(str(psd_path).encode("utf-8")).hexdigest()[:8]
        job_id = f"{self._jobs:05d}-{digest}"
        inbox = self.share / "inbox" / f"{job_id}.psd"
        remote_input = str(self.remote / "inbox" / f"{job_id}.psd")
        shutil.copyfile(psd_path, inbox)
        job = {"id": job_id, "install": self.install.progid, "input": remote_input}
        try:
            for attempt in (1, 2):
                _write_json(self.share / "jobs" / f"{job_id}.json", job)
                result = self._wait_for(job_id)
                if result is not None:
                    return self._scrubbed(result, remote_input, psd_path.name)
                if attempt == 1:
                    self._note("the Photoshop agent stopped answering; starting it again")
                    self._started = False
                    if not self._ensure_agent():
                        break
            self._unavailable_reason = self._unavailable_reason or (
                f"the Photoshop agent on the remote host stopped answering twice ({self.install.display_name})")
            return {"ok": False, "agentFailure": True, "error": self._unavailable_reason}
        finally:
            (self.share / "jobs" / f"{job_id}.json").unlink(missing_ok=True)
            (self.share / "results" / f"{job_id}.json").unlink(missing_ok=True)
            inbox.unlink(missing_ok=True)

    def quit(self) -> None:
        """Ask the agent to quit the Photoshops it started, then remove its task and
        the files this run put on the host (agent.log and any crash dumps stay)."""
        if not self._started:
            return
        (self.share / "stop.flag").write_text("stop\n", encoding="utf-8")
        deadline = time.monotonic() + QUIT_SECONDS
        while time.monotonic() < deadline:
            beat = _read_json(self.share / "heartbeat.json") or {}
            if beat.get("state") == "exited":
                break
            time.sleep(1.0)
        else:
            self._note("the Photoshop agent did not confirm it exited")
        if self.ssh:
            self._run_start_script("-Stop")
        elif self._process is not None:
            try:
                self._process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                pass
        self._started = False
        self._clean_share()

    # ---------- the agent ----------

    def _note(self, message: str) -> None:
        if self._log is not None:
            self._log(f"    [{self.install.display_name}] {message}")

    def _ensure_agent(self) -> bool:
        if self.unavailable:
            return False
        if self._started:
            return True
        try:
            for name in ("agent/drivers", "jobs", "results", "inbox"):
                (self.share / name).mkdir(parents=True, exist_ok=True)
            for source, name in AGENT_FILES:
                shutil.copyfile(source, self.share / "agent" / name)
            for name in ("stop.flag", "heartbeat.json"):
                (self.share / name).unlink(missing_ok=True)
        except OSError as error:
            self._unavailable_reason = f"cannot reach the Photoshop host's share: {error}"
            return False
        self._note("starting the Photoshop agent")
        if self.ssh:
            output = self._run_start_script()
            if "started" not in output:
                self._unavailable_reason = f"could not start the Photoshop agent: {output.strip()[-400:]}"
                return False
        else:
            self._launch_local_agent()
        deadline = time.monotonic() + START_SECONDS
        while time.monotonic() < deadline:
            beat = _read_json(self.share / "heartbeat.json")
            if beat is not None:
                if beat.get("error"):
                    self._unavailable_reason = beat["error"]
                    return False
                if beat.get("state") == "running":
                    return self._agent_up(beat)
            time.sleep(1.0)
        self._unavailable_reason = (f"the Photoshop agent did not start within {START_SECONDS}s "
                                    "(is the host's user logged on at the console?)")
        return False

    def _launch_local_agent(self) -> None:
        """No ssh: the agent runs here as a child process (protocol checks on one PC)."""
        self._process = subprocess.Popen(
            [self.python, str(self.share / "agent" / "photoshop_agent.py"), "--dir", str(self.share)],
            cwd=str(self.share / "agent"), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def _agent_up(self, beat: dict) -> bool:
        wanted = self.install.progid
        match = next((i for i in beat.get("installs") or [] if wanted in (i.get("progid"), i.get("label"))), None)
        if match is None:
            labels = ", ".join(i.get("label", "?") for i in beat.get("installs") or []) or "none"
            self._unavailable_reason = f"the remote host has no Photoshop {wanted} (it has: {labels})"
            return False
        self._version = match.get("version") or "unknown"
        self._started = True
        self._last_beat = (int(beat.get("beat", 0)), time.monotonic())
        return True

    def _alive(self) -> bool:
        """The agent beats between jobs; a beat that stopped changing means it died."""
        beat = _read_json(self.share / "heartbeat.json") or {}
        if beat.get("state") == "exited":
            return False
        number = int(beat.get("beat", -1))
        if number != self._last_beat[0]:
            self._last_beat = (number, time.monotonic())
        return time.monotonic() - self._last_beat[1] < STALE_SECONDS

    def _wait_for(self, job_id: str) -> dict | None:
        result_path = self.share / "results" / f"{job_id}.json"
        deadline = time.monotonic() + RESULT_SECONDS
        next_check = time.monotonic() + 10
        while time.monotonic() < deadline:
            result = _read_json(result_path) if result_path.exists() else None
            if result is not None:
                return result
            if time.monotonic() >= next_check:
                next_check = time.monotonic() + 10
                if not self._alive():
                    return None
            time.sleep(0.5)
        return None

    def _run_start_script(self, *extra: str) -> str:
        command = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=15", self.ssh,
                   "powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                   f'"{self.remote / "agent" / "start-agent.ps1"}"',
                   "-Python", f'"{self.python}"', "-RunDir", f'"{self.remote}"',
                   "-User", f'"{self.user}"', "-TaskName", TASK_NAME, *extra]
        try:
            completed = subprocess.run(command, capture_output=True, text=True, timeout=120)
            return (completed.stdout or "") + (completed.stderr or "")
        except Exception as error:
            return f"ssh failed: {error}"

    def _scrubbed(self, result: dict, remote_input: str, name: str) -> dict:
        """Errors name the host's copy; report the file's own name and no host paths."""
        for key in ("error",):
            text = result.get(key)
            if isinstance(text, str):
                for form in (remote_input, remote_input.replace("\\", "/")):
                    text = text.replace(form, name)
                for form in (str(self.remote), str(self.remote).replace("\\", "/")):
                    text = text.replace(form, "<remote>")
                result[key] = text
        return result

    def _clean_share(self) -> None:
        """Remove exactly what this client put there; directories only when empty."""
        for _source, name in AGENT_FILES:
            (self.share / "agent" / name).unlink(missing_ok=True)
        for name in ("stop.flag", "heartbeat.json", "heartbeat.json.tmp"):
            (self.share / name).unlink(missing_ok=True)
        for name in ("agent/drivers/__pycache__", "agent/__pycache__"):
            directory = self.share / name
            if directory.is_dir():
                for item in directory.glob("*.pyc"):
                    item.unlink(missing_ok=True)
        for name in ("agent/drivers/__pycache__", "agent/__pycache__", "agent/drivers", "agent",
                     "jobs", "results", "inbox"):
            try:
                (self.share / name).rmdir()
            except OSError:
                pass
