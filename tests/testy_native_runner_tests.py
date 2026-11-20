"""Native import staging, independent outcomes, interruption and manifest validation."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "testy"))
import native_import as native


class NativeRun(unittest.TestCase):
    def setUp(self):
        scratch = ROOT / "build" / "test-output"
        scratch.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="patchy-native-runner-", dir=scratch)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.manifest = self.root / "manifest.json"

    def write_manifest(self, cases):
        self.manifest.write_text(json.dumps({"name": "Test", "cases": cases}), encoding="utf-8")

    def test_stages_unicode_and_sidecars_per_case_and_reports_without_changing_inputs(self):
        source = self.root / "\u753b\u50cf.xcf"
        source.write_bytes(b"source bytes")
        asset = self.root / "images" / "linked.png"
        asset.parent.mkdir()
        Image.new("RGBA", (2, 2), "red").save(asset)
        self.write_manifest([{"source": source.name, "assets": ["images/linked.png"],
                              "reference": "images/linked.png", "expect": {"layerCount": 1}},
                             {"source": "absent.xcf"}])
        def probe(case, copy, folder, exe, settings, timeout):
            self.assertNotEqual(copy, source)
            self.assertEqual(source.read_bytes(), copy.read_bytes())
            self.assertEqual((copy.parent / "images/linked.png").read_bytes(), asset.read_bytes())
            (folder / "render.png").write_bytes(asset.read_bytes())
            return {"opened": True, "exported": True, "width": 2, "height": 2, "layerCount": 1,
                    "version": "test", "notes": ["Imported as pixels"]}
        out = self.root / "output"
        with patch.object(native, "_probe", side_effect=probe), patch.dict(os.environ, {"TESTY_SHUTDOWN_FILE": ""}):
            result = native.run(self.manifest, Path(sys.executable), out)
        ok, missing = result["cases"]
        self.assertEqual(result["state"], "complete")
        self.assertEqual(ok["appearance"]["status"], "pass")
        self.assertEqual(ok["structure"]["status"], "pass")
        self.assertEqual(ok["notes"], ["Imported as pixels"])
        self.assertIsNone(missing["opened"])
        self.assertEqual(missing["structure"]["status"], "unscored")
        self.assertEqual(json.loads((out / "results.json").read_text(encoding="utf-8")), result)
        self.assertTrue((out / "report.html").is_file())
        self.assertEqual(list(out.rglob(source.name)), [])
        self.assertEqual(source.read_bytes(), b"source bytes")
        with self.assertRaises(FileExistsError):
            native.run(self.manifest, Path(sys.executable), out)

    def test_stop_at_cell_boundary_checkpoints_finished_checks(self):
        self.write_manifest([{"source": "a.ase"}, {"source": "b.ase"}])
        out = self.root / "stopped"
        with patch.dict(os.environ, {"TESTY_SHUTDOWN_FILE": str(self.manifest)}):
            result = native.run(self.manifest, Path(sys.executable), out)
        self.assertEqual(result["state"], "stopped")
        self.assertEqual(result["cases"], [])
        self.assertEqual(json.loads((out / "progress.json").read_text())["state"], "stopped")
        self.assertTrue((out / "report.html").is_file())

    def test_export_timeout_keeps_the_measured_open_and_inspection(self):
        opened = {"opened": True, "inspected": True, "layers": [], "width": 2, "height": 2}
        def timeout(*args, **kwargs):
            (self.root / "checkpoint.json").write_text(json.dumps(opened), encoding="utf-8")
            raise subprocess.TimeoutExpired(args[0], 1)
        with patch.object(native.subprocess, "run", side_effect=timeout):
            row = native._probe({}, self.root / "image.kra", self.root, Path(sys.executable), self.root, 1)
        self.assertTrue(row["opened"])
        self.assertTrue(row["inspected"])
        self.assertFalse(row["exported"])
        self.assertIn("timed out", row["error"])

    def test_case_filter_and_manifest_attribution_are_validated_before_start(self):
        self.write_manifest([{"id": "a", "source": "a.kra"}, {"id": "b", "source": "b.kra"}])
        out = self.root / "filtered"
        with self.assertRaisesRegex(ValueError, "Unknown case"):
            native.run(self.manifest, Path(sys.executable), out, case_ids=["c"])
        result = native.run(self.manifest, Path(sys.executable), out, case_ids=["b"])
        self.assertEqual([row["id"] for row in result["cases"]], ["b"])
        for key, invalid in (("name", 7), ("sources", [7])):
            self.manifest.write_text(json.dumps({"cases": [{"source": "a.kra"}], key: invalid}))
            with self.assertRaises(ValueError):
                native.load_manifest(self.manifest)

    def test_rejects_ambiguous_references_or_asset_escape_before_running(self):
        invalid = [
            [{"source": "a.xcf"}, {"source": "a.xcf"}],
            [{"source": "//unrequested/share/a.xcf"}],
            [{"source": "f/a.xcf", "assets": ["f/../other.png"]}],
            [{"source": "a.ase", "reference": "a.png", "frames": ["b.png"]}],
            [{"source": "a.ase", "frames": ["a.png"], "durationsMs": [40, 80]}],
            [{"source": "a.xcf", "reference": "@mergedimage"}],
        ]
        for cases in invalid:
            with self.subTest(cases=cases):
                self.write_manifest(cases)
                with self.assertRaises(ValueError):
                    native.load_manifest(self.manifest)


if __name__ == "__main__":
    unittest.main()
