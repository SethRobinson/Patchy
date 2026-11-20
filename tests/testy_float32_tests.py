"""Opt-in Testy comparisons of unclipped, as-opened float samples."""
from pathlib import Path
import struct
import sys
import tempfile
import types
import unittest
from unittest import mock

import numpy as np
import tifffile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "testy"))
import float32_check as raw
import export_static
import render_check
import testy


def linear_profile():
    colorants = ((.43607, .22250, .01393), (.38506, .71688, .09710), (.14308, .06062, .71417))
    tags = {}
    for channel, xyz in zip((b"r", b"g", b"b"), colorants):
        tags[channel + b"XYZ"] = b"XYZ " + bytes(4) + struct.pack(">iii", *(round(c * 65536) for c in xyz))
        tags[channel + b"TRC"] = b"curv" + bytes(8)
    table = bytearray(132 + len(tags) * 12)
    table[16:24], table[36:40] = b"RGB XYZ ", b"acsp"
    struct.pack_into(">I", table, 128, len(tags))
    for i, (key, value) in enumerate(tags.items()):
        struct.pack_into(">4sII", table, 132 + i * 12, key, len(table), len(value))
        table.extend(value)
    struct.pack_into(">I", table, 0, len(table))
    return bytes(table)


class FloatSamples(unittest.TestCase):
    def setUp(self):
        parent = ROOT / "build/test-output"
        parent.mkdir(parents=True, exist_ok=True)
        tmp = tempfile.TemporaryDirectory(prefix="testy-float-", dir=parent)
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        self.source = self.root / "源-original.tif"
        self.actual = self.root / "actual.tif"
        self.rgb = np.array([[[-.25, .5, 2.0], [1.0, 1.5, 4.0]], [[.25, .25, .25], [1., 1., 1.]]], np.float32)

    def write(self, path, data, profile=linear_profile(), **options):
        tags = [(34675, 7, len(profile), profile, False)] if profile else []
        tifffile.imwrite(path, data, photometric="rgb", extratags=tags, metadata=None, **options)

    def test_hdr_and_signed_samples_over_white_in_both_alpha_conventions(self):
        alpha = np.array([[[1.], [.5]], [[.75], [0.]]], np.float32)
        expected = self.rgb * alpha + 1. - alpha
        self.write(self.source, expected, rowsperstrip=1)
        for extra, foreground in (("UNASSALPHA", self.rgb), ("ASSOCALPHA", self.rgb * alpha)):
            self.write(self.actual, np.concatenate((foreground, alpha), axis=2), profile=None, extrasamples=extra)
            with mock.patch.object(raw, "BLOCK_PIXELS", 1):
                score = raw.compare(self.source, self.actual, (2, 2))
            self.assertEqual(score["pixelsOverTolerance"], 0, score)
            self.assertEqual(score["maxAbsoluteError"], 0)
            self.assertEqual(score["referenceRange"], [-.25, 2.5])

    def test_full_canvas_counts_outliers_that_clip_to_the_same_display(self):
        self.write(self.source, self.rgb, byteorder=">")
        modified = self.rgb.copy()
        modified[0, 0, 0] -= 4
        modified[0, 1, 2] += 2
        self.write(self.actual, modified)
        score = raw.compare(self.source, self.actual, (2, 2), repeat=True)
        self.assertFalse(score["identical"])
        self.assertEqual((score["fractionOverTolerance"], score["maxAbsoluteError"], score["meanAbsoluteError"]),
                         (.5, 4, .5))
        self.write(self.actual, self.rgb)
        repeat = raw.compare(self.source, self.actual, (2, 2), repeat=True)
        self.assertTrue(repeat["identical"])
        self.assertEqual(score["sampleSHA256"], repeat["sampleSHA256"])
        self.assertEqual(raw.compare(self.source, self.actual, (2, 2), expected_digest="wrong")["state"], "unscored")

    def test_unqualified_reference_is_unscored_and_nonfinite_patchy_counts_as_a_mismatch(self):
        self.write(self.source, self.rgb)
        self.write(self.actual, self.rgb.astype(np.uint16))
        self.assertEqual(raw.compare(self.source, self.actual, (2, 2))["state"], "unscored")
        self.write(self.actual, self.rgb)
        self.assertEqual(raw.compare(self.source, self.actual, (3, 2))["state"], "unscored")
        for profile in (None, b"test", linear_profile().replace(b"curv", b"mft2")):
            self.write(self.source, self.rgb, profile=profile)
            self.assertIn("profile", raw.compare(self.source, self.actual, (2, 2))["reason"])
        marked = self.rgb.copy()
        marked[0, 1] = np.nan
        self.write(self.source, marked)
        self.assertEqual(raw.compare(self.source, self.actual, (2, 2))["state"], "unscored")
        self.write(self.source, self.rgb)
        self.write(self.actual, marked)
        score = raw.compare(self.source, self.actual, (2, 2))
        self.assertEqual((score["nonfinitePixels"], score["pixelsOverTolerance"]), (1, 1))

    def test_visually_consistent_reference_can_have_different_float_samples(self):
        first = self.root / "render.png"
        first.write_bytes(b"display")
        self.write(raw.companion(first), self.rgb)
        def probe(source, second):
            second.write_bytes(b"display")
            self.write(raw.companion(second), self.rgb + 2)
            return {"ok": True}
        record = render_check.settle_render(probe, self.source, first, None, lambda a, b: True,
                                            "file", float_size=(2, 2))
        self.assertEqual((record["outcome"], record["float32"]["state"]), ("repeatable", "unscored"))
        self.assertFalse(raw.companion(render_check.again_path(first, 2)).exists())
        self.assertIn("again2-render32.tif", testy.Runner.SCRUB_TRUTH)
        oversized = raw.confirm_reference(first, [first], (raw.MAX_PIXELS + 1, 1))
        self.assertIn("size limit", oversized["reason"])
        self.assertFalse(oversized.get("retry"))

    def test_warning_remote_and_unconfirmed_sources_never_export_a_patchy_tiff(self):
        runner = testy.Runner.__new__(testy.Runner)
        runner.status = {"run": {"float32": True}}
        runner.ps = types.SimpleNamespace(supports_float32=True)
        entry = {"traits": {"depth": 32, "mode": 3}, "docSize": [2, 2], "groundTruth": {"state": "done"}}
        stage = types.SimpleNamespace(sha1="abc")
        runner.ps.font_cache_key = lambda: "fonts"
        runner.ps.version = lambda: "1"
        runner.suffix = "test"
        enabled = runner._ground_truth_cache_dir(entry, stage)
        self.assertTrue(runner._float32_requested(entry))
        runner.status["run"].clear()
        normal = runner._ground_truth_cache_dir(entry, stage)
        self.assertNotEqual(normal, enabled)
        runner.status["run"]["float32"] = True
        entry["traits"]["depth"] = 16
        opted_sixteen = runner._ground_truth_cache_dir(entry, stage)
        runner.status["run"].clear()
        self.assertEqual(runner._ground_truth_cache_dir(entry, stage), opted_sixteen)
        self.assertFalse(runner._float32_probe_options(entry))
        entry["traits"]["depth"] = 32
        runner.status["run"]["float32"] = True
        for truth, supports, reason in (({"dialogs":["notice"]}, True, "warning"),
                                        ({"state":"failed","fileRejected":True}, True, "refused"),
                                        ({"renderCheck":{"outcome":"unstable"}}, True, "unstable"),
                                        ({}, False, "local float TIFFs"),
                                        ({}, True, "confirmed")):
            runner.ps.supports_float32 = supports
            entry["groundTruth"] = truth
            cell = {"artifacts": {}}
            with mock.patch.object(testy.patchy_driver, "export", side_effect=AssertionError("unexpected export")):
                runner._measure_float32(entry, None, stage, cell, self.root, self.source)
            self.assertEqual(cell["float32"]["state"], "unscored")
            self.assertIn(reason, cell["float32"]["reason"])

    def test_scan_counts_a_float_error_and_static_export_keeps_only_its_metrics(self):
        runner = testy.Runner.__new__(testy.Runner)
        runner.editors = {"patchy": types.SimpleNamespace(display_name="Patchy")}
        runner.editor_order, runner.scan_threshold, runner.compare_mode = ["patchy"], .1, "strict"
        runner.status = {"run": {"float32": True}}
        metrics = {"state": "done", "fractionOverTolerance": .2, "threshold": .001}
        entry = {"groundTruth":{"state":"done", "artifacts":{"render32":"files/truth/render32.tif"}},
                 "cells":{"patchy":{"state":"done", "float32":metrics, "opens":"ok",
                                    "artifacts":{"render32":"files/patchy/render32.tif"},
                                    "renderMetrics":{"badFraction":0.0}}}}
        self.assertEqual(runner._scan_flag_reasons(entry), ["Patchy: float RGB differs by more than 0.001 on 20.0% of pixels"])
        public, files = export_static.public_status({"files":[entry]})
        self.assertEqual(files, [])
        self.assertEqual(public["files"][0]["cells"]["patchy"]["float32"], metrics)


if __name__ == "__main__":
    unittest.main()
