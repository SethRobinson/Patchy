"""Testy lifecycle regressions using isolated servers and harmless Python children."""
import http.client
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import tempfile
import time
import types
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "testy"))
import serve
import testy


class ShutdownTests(unittest.TestCase):
    def setUp(self):
        scratch = ROOT / "build/test-output"
        scratch.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="testy-shutdown-", dir=scratch)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.runs = self.root / "runs"
        self.runs.mkdir()
        for name, value in (("TESTY_ROOT", self.root), ("RUNS_DIR", self.runs)):
            patch = mock.patch.object(testy.config, name, value)
            patch.start()
            self.addCleanup(patch.stop)
        for name, value in (("_child_run", None), ("_child_run_dir", None),
                            ("_in_process_run_active", False), ("_in_process_run_dir", None)):
            patch = mock.patch.object(testy, name, value)
            patch.start()
            self.addCleanup(patch.stop)
        self.server, self.port = testy.start_server(0)
        self.addCleanup(self.stop_server)
        self.children = []
        self.addCleanup(self.stop_children)
        testy._interrupt_requested.clear()
        self.addCleanup(testy._interrupt_requested.clear)

    def stop_children(self):
        for child in self.children:
            if child.poll() is None:
                child.terminate()
            child.wait(timeout=10)

    def stop_server(self):
        self.server.request_stop()
        self.assertTrue(self.server.stopped.wait(10), "server failed to shut down")

    def request(self, path, method="POST", body=b""):
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=5)
        try:
            conn.request(method, path, body=body)
            response = conn.getresponse()
            return response.status, response.read()
        finally:
            conn.close()

    def wait_for(self, predicate):
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if predicate():
                return
            time.sleep(.02)
        self.fail("timed out waiting for test checkpoint")

    def assert_port_released(self):
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", self.port))
            listener.listen()

    def spawn(self, code):
        handler = types.SimpleNamespace(server=self.server)
        testy.TestyRequestHandler._spawn_child(handler, [sys.executable, "-c", code])
        self.children.append(testy._child_run)
        return testy._child_run

    def test_idle_button_stops_server_and_releases_port(self):
        code, body = self.request("/testy-shutdown")
        self.assertEqual(code, 202)
        self.assertTrue(json.loads(body)["shuttingDown"])
        self.stop_server()
        self.assert_port_released()
        self.assertFalse(self.server.stop_file.exists())

    def test_active_child_finishes_step_checkpoints_and_exits_before_server(self):
        # No status.json exists at shutdown time, as with a just-spawned run.
        run_dir = self.runs / "20261002-000001 日本"
        run_dir.mkdir()
        child = self.spawn(f"""
import json, sys, time
from pathlib import Path
sys.path.insert(0, {str(ROOT / 'testy')!r})
import testy
runner = object.__new__(testy.Runner)
runner.server = None
runner.run_dir = Path({str(run_dir)!r})
runner.status = {{'state': 'running', 'run': {{}}, 'files': [{{'state': 'done'}}, {{'state': 'pending'}}]}}
runner._cleanup_drivers = lambda: (runner.run_dir / 'cleaned').touch()
runner.push = lambda: (runner.run_dir / 'status.json').write_text(json.dumps(runner.status))
while not runner._pause_requested(): time.sleep(.02)
(runner.run_dir / 'signal-seen').touch()
# Emulate a step in progress: a shutdown signal must not interrupt it.
while not (runner.run_dir / 'finish-step').exists(): time.sleep(.02)
raise SystemExit(runner._graceful_pause_exit())
""")
        # Another Python process is unrelated and must survive Testy's shutdown.
        unrelated = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
        self.children.append(unrelated)
        self.assertEqual(self.request("/testy-shutdown")[0], 202)
        self.wait_for(lambda: (run_dir / "signal-seen").exists())
        self.assertIsNone(child.poll())
        self.assertFalse(self.server.stopped.is_set())
        state = json.loads(self.request("/testy-run-state", "GET")[1])
        self.assertTrue(state["running"])
        self.assertTrue(state["shuttingDown"])
        self.assertEqual(self.request("/testy-shutdown")[0], 202)
        for endpoint in ("start-run", "resume-run", "rerun-file", "retest-file", "delete-runs", "cancel-run"):
            self.assertEqual(self.request("/testy-" + endpoint)[0], 409, endpoint)
        # The finishing Photopea cell still needs to upload its result.
        self.assertEqual(self.request("/testy-upload?name=runs/result.png", body=b"result")[0], 200)
        self.assertEqual((self.runs / "result.png").read_bytes(), b"result")
        (run_dir / "finish-step").touch()
        self.assertEqual(child.wait(timeout=10), 3, (self.runs / "last-child-run.log").read_text(errors="replace"))
        self.stop_server()
        saved = json.loads((run_dir / "status.json").read_text())
        self.assertEqual(saved["state"], "paused")
        self.assertEqual(saved["files"], [{"state": "done"}, {"state": "pending"}])
        self.assertTrue((run_dir / "cleaned").exists())
        self.assertFalse(self.server.stop_file.exists())
        self.assertIsNone(unrelated.poll())
        self.assert_port_released()

    def test_child_failure_does_not_prevent_shutdown(self):
        child = self.spawn("raise SystemExit(7)")
        self.assertEqual(child.wait(timeout=10), 7)
        self.server.request_stop()
        self.stop_server()
        self.assert_port_released()

    def test_signal_write_failure_stays_visible_and_retries(self):
        with mock.patch.object(testy, "_in_process_run_active", True):
            with mock.patch.object(Path, "touch", side_effect=OSError("test write failure")):
                self.server.request_stop()
                self.wait_for(lambda: bool(self.server.stop_error))
                state = json.loads(self.request("/testy-run-state", "GET")[1])
                self.assertIn("test write failure", state["shutdownError"])
                self.assertFalse(self.server.stopped.is_set())
            self.wait_for(lambda: self.server.stop_file.exists())
            self.assertEqual(self.server.stop_error, "")
        self.stop_server()

    def test_serve_ctrl_c_uses_same_shutdown_and_restores_signal_handler(self):
        original = signal.getsignal(signal.SIGINT)
        wait = self.server.wait_until_stopped
        calls = []

        def interrupt_and_wait():
            if not calls:
                calls.append(True)
                signal.raise_signal(signal.SIGINT)
            wait()

        with mock.patch.object(serve, "start_server", return_value=(self.server, self.port)), \
                mock.patch.object(self.server, "wait_until_stopped", side_effect=interrupt_and_wait), \
                mock.patch.object(sys, "argv", ["serve.py", str(self.port)]):
            self.assertEqual(serve.main(), 0)
        self.assertIs(signal.getsignal(signal.SIGINT), original)
        self.assert_port_released()

    def test_cli_ctrl_c_checkpoints_instead_of_interrupting_editor_code(self):
        runner = object.__new__(testy.Runner)
        runner.server = self.server
        runner.run_dir = self.runs
        runner.status = {"run": {}}

        def run():
            signal.raise_signal(signal.SIGINT)
            self.assertTrue(runner._pause_requested())
            self.assertFalse(self.server.stopped.is_set())
            return 3

        runner.run = run
        original = signal.getsignal(signal.SIGINT)
        with mock.patch.object(testy, "Runner", return_value=runner), \
                mock.patch.object(sys, "argv", ["testy.py", "--no-browser"]):
            self.assertEqual(testy.main(), 3)
        self.assertIs(signal.getsignal(signal.SIGINT), original)
        self.assert_port_released()

    def test_completed_cli_dashboard_button_can_exit_main(self):
        runner = types.SimpleNamespace(server=self.server, status={"run": {}},
                                       close_check_drivers=lambda: None)

        def run():
            self.server.request_stop()
            return 0

        runner.run = run
        with mock.patch.object(testy, "Runner", return_value=runner), \
                mock.patch.object(sys, "argv", ["testy.py", "--no-browser"]):
            self.assertEqual(testy.main(), 0)
        self.assert_port_released()


if __name__ == "__main__":
    unittest.main()
