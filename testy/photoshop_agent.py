"""Photoshop reopen agent: opens PSDs for a Testy run on another machine.

Runs on the Photoshop host, in the logged-on console session (COM cannot start
Photoshop from an ssh session; drivers/photoshop_remote.py starts this through a
temporary interactive scheduled task). It needs Python 3.11+ with pywin32 there
and nothing else: drivers/photoshop_remote.py copies this file, win_dialogs.py and
drivers/photoshop.py into the run's folder on the host before starting it, so the
host always runs the same driver, dialog guard and restart rules as this PC.

The run folder is the whole protocol (every write is a temporary file renamed into
place, so a reader never sees half a file):

  jobs/<id>.json      {"id", "install": ProgID or install label, "input": path}
  results/<id>.json   the driver's probe() result for that job, plus psVersion
  heartbeat.json      {"beat": n, "state": "running"|"exited", "installs": [...],
                       "error": why the agent could not start, if it could not}
  stop.flag           asks the agent to quit the Photoshops it started and exit
  agent.log           what it did

The agent exits on stop.flag or after IDLE_SECONDS without a job.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import threading
import time
import traceback
from pathlib import Path

IDLE_SECONDS = 30 * 60
POLL_SECONDS = 0.3
BEAT_SECONDS = 5.0

sys.path.insert(0, str(Path(__file__).resolve().parent))


def _write_json(path: Path, value: dict) -> None:
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(value), encoding="utf-8")
    os.replace(temporary, path)


class Agent:
    def __init__(self, run_dir: Path) -> None:
        self.run_dir = run_dir
        self.jobs = run_dir / "jobs"
        self.results = run_dir / "results"
        self.dumps = run_dir / "dumps"
        for directory in (self.jobs, self.results):
            directory.mkdir(parents=True, exist_ok=True)
        self.beat = 0
        self.beat_lock = threading.Lock()
        self.beat_stop = threading.Event()
        self.installs: list = []
        self.install_list: list[dict] = []
        self.drivers: dict = {}
        self.error: str | None = None

    def log(self, message: str) -> None:
        line = f"{time.strftime('%Y-%m-%d %H:%M:%S')} {message}\n"
        with open(self.run_dir / "agent.log", "a", encoding="utf-8") as handle:
            handle.write(line)

    def heartbeat(self, state: str = "running") -> None:
        with self.beat_lock:
            self.beat += 1
            _write_json(self.run_dir / "heartbeat.json", {
                "beat": self.beat, "state": state, "pid": os.getpid(), "error": self.error,
                "installs": self.install_list,
            })

    def _beat_forever(self) -> None:
        """Beats from a side thread, so a probe that takes minutes (a hang, then a
        restart and a retry) still shows a live agent; the driver's own watchdog
        bounds the probe."""
        while not self.beat_stop.wait(BEAT_SECONDS):
            try:
                self.heartbeat()
            except OSError:
                pass

    def driver_for(self, spec: str):
        from drivers import photoshop

        if spec not in self.drivers:
            install = next((i for i in self.installs if spec in (i.progid, i.label)), None)
            if install is None:
                return None
            self.drivers[spec] = photoshop.PhotoshopDriver(log=self.log, install=install)
        return self.drivers[spec]

    def move_new_dumps(self, since: float) -> list[str]:
        """Crash dumps Photoshop left during a job, moved beside the log so the host's
        CrashDumps folder does not fill up and the run can name them."""
        source = Path(os.path.expandvars(r"%LOCALAPPDATA%\CrashDumps"))
        moved: list[str] = []
        try:
            candidates = list(source.glob("Photoshop*.dmp"))
        except OSError:
            return moved
        for dump in candidates:
            try:
                if dump.stat().st_mtime < since:
                    continue
                self.dumps.mkdir(exist_ok=True)
                os.replace(dump, self.dumps / dump.name)
                moved.append(dump.name)
            except OSError:
                pass
        return moved

    def run_job(self, job_path: Path) -> None:
        job = json.loads(job_path.read_text(encoding="utf-8"))
        job_id = str(job["id"])
        started = time.time()
        driver = self.driver_for(str(job["install"]))
        if driver is None:
            result = {"ok": False, "agentFailure": True,
                      "error": f"no Photoshop {job['install']} on this host"}
        else:
            self.log(f"job {job_id}: {job['input']} in {driver.install.label}")
            try:
                result = driver.probe(Path(job["input"]), None)
                result["psVersion"] = driver.version()
            except Exception as error:  # never let one file take the agent down
                result = {"ok": False, "agentFailure": True, "error": f"agent error: {error}"}
                self.log(traceback.format_exc())
            if not result.get("ok") and (result.get("crashed") or result.get("hung")):
                dumps = self.move_new_dumps(started)
                if dumps:
                    result["crashDumps"] = dumps
        result["id"] = job_id
        _write_json(self.results / f"{job_id}.json", result)
        job_path.unlink(missing_ok=True)
        self.log(f"job {job_id}: {'ok' if result.get('ok') else result.get('error')}")

    def find_installs(self) -> None:
        from drivers import photoshop

        import win32com.client  # noqa: F401  (pywin32 is the one requirement)

        self.installs = photoshop.discover_installs()
        self.install_list = [{"label": i.label, "progid": i.progid, "exe": str(i.exe),
                              "version": photoshop.exe_version(i.exe)} for i in self.installs]

    def serve(self) -> int:
        try:
            self.find_installs()
        except Exception as error:
            self.error = f"the agent could not start: {error} (needs Python 3.11+ with pywin32)"
            self.log(self.error)
            self.heartbeat("exited")
            return 1
        self.log("agent up; installs: " + ", ".join(f"{i.label} ({i.progid})" for i in self.installs))
        idle_since = time.monotonic()
        self.heartbeat()
        beater = threading.Thread(target=self._beat_forever, name="heartbeat", daemon=True)
        beater.start()
        try:
            while True:
                if (self.run_dir / "stop.flag").exists():
                    self.log("stop requested")
                    break
                pending = sorted(self.jobs.glob("*.json"))
                if not pending:
                    if time.monotonic() - idle_since > IDLE_SECONDS:
                        self.log("idle too long; exiting")
                        break
                    time.sleep(POLL_SECONDS)
                    continue
                try:
                    self.run_job(pending[0])
                except Exception as error:  # a broken job must not take the agent down
                    self.log(traceback.format_exc())
                    job_id = pending[0].stem
                    _write_json(self.results / f"{job_id}.json",
                                {"id": job_id, "ok": False, "agentFailure": True,
                                 "error": f"agent error: {error}"})
                    pending[0].unlink(missing_ok=True)
                idle_since = time.monotonic()
        finally:
            for driver in self.drivers.values():
                driver.quit()
            self.beat_stop.set()
            beater.join(timeout=10)
            self.heartbeat("exited")
            self.log("agent exited")
        return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--dir", required=True, help="the run's folder on this host")
    args = parser.parse_args()
    return Agent(Path(args.dir)).serve()


if __name__ == "__main__":
    sys.exit(main())
