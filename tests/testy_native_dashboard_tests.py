"""Exercise the dashboard's native import endpoints on an isolated loopback server."""
import http.client
import json
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "testy"))
import testy


CHILD = """\
import json, os, sys, time
from pathlib import Path
command, manifest, flag, folder, *options = sys.argv[1:]
assert command == 'native-import' and Path(manifest).name == 'sprites.json' and flag == '--output'
out = Path(folder)
out.mkdir()
(out/'options.json').write_text(json.dumps(options))
def progress(state):
    (out/'progress.json').write_text(json.dumps(dict(state=state,name='Sprites',completed=1,total=2)))
progress('running')
while not (out/'stop.flag').exists() and not Path(os.environ['TESTY_SHUTDOWN_FILE']).exists():
    time.sleep(.02)
(out/'report.html').write_text('Native report')
(out/'results.json').write_text('{}')
progress('stopped')
"""


class NativeDashboard(unittest.TestCase):
    def setUp(self):
        scratch = ROOT / "build/test-output"
        scratch.mkdir(parents=True, exist_ok=True)
        tmp = tempfile.TemporaryDirectory(prefix="testy-native-dashboard-", dir=scratch)
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        self.runs = self.root / "runs"
        self.runs.mkdir()
        self.corpus = self.root / "corpus"
        self.corpus.mkdir()
        (self.corpus / "sprites.json").write_text('{"cases":[{"source":"sprite.ase"}]}')
        (self.root / "testy.py").write_text(CHILD)
        for key, val in (("TESTY_ROOT", self.root), ("RUNS_DIR", self.runs)):
            self.enterContext(patch.object(testy.config, key, val))
        for key in ("_child_run", "_child_run_dir", "_child_run_started", "_in_process_run_dir"):
            self.enterContext(patch.object(testy, key, None))
        self.enterContext(patch.object(testy, "_in_process_run_active", False))
        self.server, self.port = testy.start_server(0)
        self.addCleanup(self.stop_server)

    def stop_server(self):
        self.server.request_stop()
        self.assertTrue(self.server.stopped.wait(10))

    def request(self, url, body=None, headers=None):
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=5)
        try:
            conn.request("GET" if body is None else "POST", url, body=json.dumps(body) if body is not None else None,
                         headers=headers or {})
            response = conn.getresponse()
            content = response.read().decode()
            return response.status, json.loads(content) if "application/json" in response.getheader("Content-Type") else content
        finally:
            conn.close()

    def run_native(self, **options):
        status, res = self.request("/testy-start-native", {"manifest": "sprites.json", **options})
        self.assertEqual(status, 202, res)
        folder = self.runs / res["run"]
        end = time.monotonic() + 6
        while time.monotonic() < end:
            _, native = self.request("/testy-native-state")
            matches = [r for r in native["runs"] if r["run"] == folder.name and r["state"] == "running"]
            if matches:
                self.assertTrue(matches[0]["canStop"])
                self.assertEqual((matches[0]["completed"], matches[0]["total"]), (1, 2))
                return folder
            time.sleep(.02)
        self.fail((self.runs / "last-child-run.log").read_text())

    def test_only_listed_manifests_can_start_and_server_selects_command(self):
        self.assertEqual(self.request("/testy-native-state")[1]["manifests"], ["sprites.json"])
        for body in ({"manifest": "../sprites.json"}, {"manifest": str(self.corpus / "sprites.json")},
                     {"manifest": "sprites.json", "command": "extra"}, {"manifest": "sprites.json", "output": "elsewhere"}):
            self.assertEqual(self.request("/testy-start-native", body)[0], 400)
        folder = self.run_native()
        self.assertRegex(folder.name, r"^native-\d{8}-\d{6}-[0-9a-f]{8}$")
        self.assertEqual(self.request("/testy-start-native", {"manifest": "sprites.json"})[0], 409)
        self.assertEqual(self.request("/testy-start-run", {})[0], 409)
        state = self.request("/testy-run-state")[1]
        self.assertTrue(state["busy"] and state["nativeRunning"])
        self.assertFalse(state["running"])
        old_psd = self.runs / "20261119-120000"
        old_psd.mkdir()
        (old_psd / "status.json").write_text('{"state":"done"}')
        (old_psd / "rerun-state.json").write_text('{"state":"running"}')
        self.assertEqual(self.request("/testy-rerun-state?run=" + old_psd.name)[1]["state"], "failed")
        for route in ("pause-run", "resume-run", "cancel-run"):
            self.assertEqual(self.request("/testy-" + route, {})[0], 409)
        self.assertIsNone(testy._child_run.poll())
        self.assertEqual(self.request("/testy-stop-native", {"run": "another"})[0], 409)
        for headers in ({"Origin": "https://external.example"}, {"Host": "external.example"},
                        {"Sec-Fetch-Site": "cross-site"}, {"Origin": "null"}):
            self.assertEqual(self.request("/testy-stop-native", {"run": folder.name}, headers)[0], 403)
            self.assertEqual(self.request("/testy-start-native", {"manifest": "sprites.json"}, headers)[0], 403)
        self.assertFalse((folder / "stop.flag").exists())
        self.assertEqual(self.request("/testy-stop-native", {"run": folder.name})[0], 202)
        self.assertEqual(testy._child_run.wait(timeout=6), 0)
        state = self.request("/testy-native-state")[1]
        self.assertFalse(state["busy"])
        self.assertEqual(state["runs"][0]["state"], "stopped")
        self.assertTrue(state["runs"][0]["report"])
        self.assertFalse(state["runs"][0]["canStop"])
        self.assertEqual(self.request(f"/runs/{folder.name}/report.html"), (200, "Native report"))
        self.assertEqual(self.request("/testy-stop-native", {"run": folder.name})[0], 409)

    def upload(self, name):
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=5)
        try:
            conn.request("POST", "/testy-upload?name=" + name, body=b"bytes")
            response = conn.getresponse()
            response.read()
            return response.status
        finally:
            conn.close()

    def test_cross_editor_options_reach_the_child_and_only_its_photopea_cells_take_uploads(self):
        editors = self.request("/testy-native-state")[1]["editors"]
        self.assertEqual([editor["key"] for editor in editors],
                         ["gimp", "krita", "photodemon", "photocraft", "photopea"])
        for bad in ({"editors": ["photoshop"]}, {"editors": "gimp"}, {"editors": ["gimp", "gimp"]},
                    {"psdJudge": "yes"}, {"reference": "fresh"}):
            self.assertEqual(self.request("/testy-start-native", {"manifest": "sprites.json", **bad})[0], 400, bad)
        stale = self.runs / "native-20261120-142759-deadbeef"
        (stale / "cells" / "0001" / "photopea").mkdir(parents=True)
        folder = self.run_native(editors=["gimp", "photopea"], psdJudge=True, reference="installed")
        self.assertEqual(json.loads((folder / "options.json").read_text()),
                         ["--editors", "gimp,photopea", "--server-url", f"http://127.0.0.1:{self.port}",
                          "--psd-judge", "--reference", "installed"])
        cell = f"runs/{folder.name}/cells/0001/"
        self.assertEqual(self.upload(cell + "photopea/render.png"), 200)
        self.assertEqual(self.upload(cell + "photopea/converted.psd"), 200)
        self.assertEqual((folder / "cells/0001/photopea/render.png").read_bytes(), b"bytes")
        for refused in (cell + "gimp/render.png", cell + "photopea/other.png", cell + "render.png",
                        f"runs/{folder.name}/report.html", f"runs/{folder.name}/cells/x/photopea/render.png",
                        f"runs/{stale.name}/cells/0001/photopea/render.png"):
            self.assertEqual(self.upload(refused), 403, refused)
        self.assertEqual(self.request("/testy-stop-native", {"run": folder.name})[0], 202)
        self.assertEqual(testy._child_run.wait(timeout=6), 0)
        self.assertEqual(self.upload(cell + "photopea/render.png"), 403)  # no live child owns it now

    def test_lists_cli_reports_and_keeps_psd_report_controls(self):
        folder = self.runs / "native-20261120-142759-deadbeef"
        folder.mkdir()
        (folder / "progress.json").write_text('{"name":"An offline run","state":"complete","completed":7,"total":7}')
        (folder / "report.html").write_text("Native report")
        old = self.runs / "20261120-142758"
        old.mkdir()
        (old / "report.html").write_text("Old PSD report")
        _, state = self.request("/testy-native-state")
        self.assertEqual(len(state["runs"]), 1)
        self.assertFalse(state["runs"][0]["canStop"])
        self.assertTrue(state["runs"][0]["report"])
        self.assertEqual(self.request(f"/runs/{old.name}/report.html")[1], testy.report._PAGE)
        self.assertEqual(self.request("/testy-delete-runs", {"runs": [folder.name]})[0], 400)
        self.assertTrue((folder / "report.html").exists())

    def test_unowned_running_progress_remains_unconfirmed(self):
        folder = self.runs / "native-20261120-144959-baadf00d"
        folder.mkdir()
        (folder / "progress.json").write_text('{"state":"running","completed":3,"total":8}')
        row = self.request("/testy-native-state")[1]["runs"][0]
        self.assertEqual((row["state"], row["completed"], row["total"]), ("unconfirmed", 3, 8))
        self.assertFalse(row["canStop"])
        self.assertEqual(self.request("/testy-stop-native", {"run": folder.name})[0], 409)

    def test_shutdown_finishes_a_native_child_before_releasing_port(self):
        folder = self.run_native()
        self.assertEqual(self.request("/testy-shutdown", {})[0], 202)
        self.stop_server()
        self.assertEqual(json.loads((folder / "progress.json").read_text())["state"], "stopped")
        self.assertTrue((folder / "report.html").exists())


if __name__ == "__main__":
    unittest.main()
