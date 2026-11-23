"""Scores of the cross-editor native import benchmark: where each kind of failure lands."""

import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "testy"))
import native_import as native
import native_scoring as scoring


def still(bad=0.0, alpha=0.0):
    status = "pass" if max(bad, alpha) <= 0.10 else "fail"
    return {"status": status, "frames": [{"frame": 1, "status": status, "perceptual": {"badFraction": bad},
                                           "alphaBadFraction": alpha, "pixels": 100}]}


def psd(accepted="pass", bad=0.0, kept=2, total=2, **more):
    if accepted != "pass":
        blank = {"status": "unscored"}
        return {"accepted": {"status": accepted}, "appearance": blank, "layers": blank, **more}
    layers = {"status": "pass" if kept == total else "fail", "layersKept": kept, "layersTotal": total}
    return {"accepted": {"status": "pass"}, "layers": layers,
            "appearance": {"status": "pass" if bad <= 0.1 else "fail", "perceptual": {"badFraction": bad}}, **more}


def opened(appearance=None, **more):
    return {"opened": True, "rendered": True, "appearance": appearance or still(), **more}


REFUSED = {"opened": False, "rendered": False, "error": "unknown file type"}
CRASHED = {"opened": False, "rendered": False, "appFailure": True}
UNMEASURED = {"opened": None, "rendered": False}


class Accuracy(unittest.TestCase):
    def test_pictures_become_the_share_of_pixels_that_match(self):
        self.assertEqual(scoring.accuracy(still()), 1.0)
        self.assertAlmostEqual(scoring.accuracy(still(0.16, 0.02)), 0.84)
        self.assertAlmostEqual(scoring.accuracy(still(0.0, 0.97)), 0.03)  # the worse of the two
        sprite = {"status": "fail", "frames": [
            {"status": "pass", "differentPixels": 0, "pixels": 64},
            {"status": "fail", "differentPixels": 16, "pixels": 64},
            {"status": "unscored", "reason": "No reference PNG for this frame"},
            {"status": "fail", "reason": "Missing rendered frame"}]}
        self.assertAlmostEqual(scoring.accuracy(sprite), (1 + 0.75 + 0) / 3)
        self.assertIsNone(scoring.accuracy({"status": "unscored", "reason": "No reference PNG"}))
        self.assertIsNone(scoring.accuracy(None))
        self.assertEqual(scoring.accuracy({"status": "fail", "frames": [], "reason": "Expected 2 frames"}), 0.0)
        self.assertAlmostEqual(scoring.accuracy({"status": "fail", "perceptual": {"badFraction": 0.4}}), 0.6)


class Cases(unittest.TestCase):
    def score(self, cells, judged=True):
        return scoring.score_case({"id": "a", "cells": cells}, judged)

    def test_a_file_an_editor_cannot_open_scores_zero_on_everything_eligible(self):
        scores = self.score({"patchy": opened(savedPsd=True, psd=psd()), "gimp": REFUSED, "krita": CRASHED,
                             "photopea": UNMEASURED})
        self.assertEqual(scores["patchy"], {"outcome": "clean", "render": 1.0, "psdValid": 1.0, "psdRender": 1.0,
                                            "layers": 1.0, "import": 1.0, "conversion": 1.0})
        zero = {"render": 0.0, "psdValid": 0.0, "psdRender": 0.0, "layers": 0.0, "import": 0.0, "conversion": 0.0}
        self.assertEqual(scores["gimp"], {"outcome": "refused", **zero})
        self.assertEqual(scores["krita"], {"outcome": "crashed", **zero})
        self.assertEqual(scores["photopea"], {"outcome": "notMeasured"})  # the harness has no verdict

    def test_a_check_nobody_could_be_scored_on_marks_nobody_down(self):
        blank = {"status": "unscored", "reason": "Embedded reference dimensions differ"}
        scores = self.score({"patchy": opened(blank, savedPsd=True, psd={**psd(), "appearance": blank,
                                                                         "layers": {"status": "unscored"}}),
                             "gimp": REFUSED, "krita": opened(blank, savedPsd=False)})
        self.assertEqual(scores["patchy"], {"outcome": "clean", "psdValid": 1.0, "conversion": 1.0})
        self.assertEqual(scores["gimp"], {"outcome": "refused", "psdValid": 0.0, "conversion": 0.0})
        self.assertEqual(scores["krita"], {"outcome": "noPsd", "psdValid": 0.0, "conversion": 0.0})

    def test_each_psd_failure_has_its_own_cost(self):
        scores = self.score({
            "patchy": opened(savedPsd=True, psd=psd()),
            "gimp": opened(savedPsd=True, psd=psd(kept=1)),                         # a layer rasterized
            "krita": opened(savedPsd=True, psd=psd(bad=0.4)),                       # the look changed
            "photocraft": opened(savedPsd=True, psd=psd(warnings=["Some text may look wrong"])),
            "photodemon": opened(savedPsd=True, psd=psd("fail")),                   # Photoshop rejects it
            "photopea": opened(savedPsd=False, psdError="export failed"),
            "other": opened(savedPsd=True, psd=psd("unscored"))})                   # Photoshop could not be asked
        self.assertEqual((scores["gimp"]["conversion"], scores["gimp"]["outcome"]), (round((1 + 1 + 0.5) / 3, 4),
                                                                                    "layersLost"))
        self.assertEqual((scores["krita"]["conversion"], scores["krita"]["outcome"]), (round((1 + 0.6 + 1) / 3, 4),
                                                                                      "psdRenderMismatch"))
        self.assertEqual((scores["photocraft"]["conversion"], scores["photocraft"]["outcome"]),
                         (round(2 / 3, 4), "psdWarning"))
        for key, outcome in (("photodemon", "psdRejected"), ("photopea", "noPsd")):
            self.assertEqual((scores[key]["conversion"], scores[key]["outcome"]), (0.0, outcome))
            self.assertEqual(scores[key]["import"], 1.0)  # it still showed the file correctly
        self.assertEqual(scores["other"], {"outcome": "clean", "render": 1.0, "import": 1.0})

    def test_render_failures_and_the_first_problem_wins(self):
        scores = self.score({"patchy": opened(), "gimp": opened(still(0.3), savedPsd=True, psd=psd(kept=0)),
                             "krita": {"opened": True, "rendered": False}}, judged=True)
        self.assertEqual((scores["gimp"]["import"], scores["gimp"]["outcome"]), (0.7, "renderMismatch"))
        self.assertEqual(scores["gimp"]["conversion"], round((1 + 1 + 0) / 3, 4))
        self.assertEqual((scores["krita"]["import"], scores["krita"]["outcome"]), (0.0, "noRender"))
        plain = self.score({"patchy": opened(savedPsd=True, psd=psd(kept=0)), "gimp": REFUSED}, judged=False)
        self.assertEqual(plain["patchy"], {"outcome": "clean", "render": 1.0, "import": 1.0})
        self.assertEqual(plain["gimp"], {"outcome": "refused", "render": 0.0, "import": 0.0})


class StoredComposites(unittest.TestCase):
    """A composite stored in an OpenRaster file is a reference only while an editor
    that composes the layers reproduces it."""

    def case(self, cells, fmt="ora", stored=True):
        case = {"id": "a", "format": fmt, "cells": cells}
        return {**case, "storedComposite": True} if stored else case

    def wrong(self, **more):
        return opened(still(1.0), savedPsd=True, psd=psd(bad=1.0), **more)

    def test_a_composite_no_layer_reader_matches_leaves_the_pictures_out(self):
        case = self.case({"patchy": self.wrong(), "gimp": self.wrong(), "krita": opened(still(0.4), savedPsd=False),
                          "photodemon": REFUSED, "photopea": opened(savedPsd=True, psd=psd(kept=1))})
        self.assertIn("none of the 3 editors", scoring.unreliable_reference(case))
        scores = scoring.score_case(case, True)
        # Nobody is held to the picture, the editor that shows the stored one included.
        self.assertEqual(scores["patchy"], {"outcome": "clean", "psdValid": 1.0, "layers": 1.0, "conversion": 1.0})
        self.assertEqual(scores["photopea"], {"outcome": "layersLost", "psdValid": 1.0, "layers": 0.5,
                                              "conversion": 0.75})
        self.assertEqual(scores["krita"], {"outcome": "noPsd", "psdValid": 0.0, "layers": 0.0, "conversion": 0.0})
        self.assertEqual(scores["photodemon"], {"outcome": "refused", "psdValid": 0.0, "layers": 0.0,
                                                "conversion": 0.0})
        self.assertNotIn("import", scoring.score_case(case, False)["gimp"])

    def test_one_layer_reader_that_matches_keeps_the_composite(self):
        case = self.case({"patchy": self.wrong(), "gimp": opened(), "krita": self.wrong(),
                          "photocraft": self.wrong(), "photopea": opened()})
        self.assertIsNone(scoring.unreliable_reference(case))
        scores = scoring.score_case(case, True)
        self.assertEqual((scores["patchy"]["import"], scores["patchy"]["outcome"]), (0.0, "renderMismatch"))
        self.assertEqual(scores["gimp"]["import"], 1.0)

    def test_the_rule_needs_three_layer_readers_and_an_untrusted_stored_composite(self):
        three = {"patchy": self.wrong(), "gimp": self.wrong(), "krita": self.wrong()}
        self.assertIsNotNone(scoring.unreliable_reference(self.case(three)))
        # Two readers, or a third that did not open the file, are not enough to overrule it.
        self.assertIsNone(scoring.unreliable_reference(self.case({"patchy": self.wrong(), "gimp": self.wrong(),
                                                                  "krita": REFUSED, "photopea": opened()})))
        # A reference supplied beside the file, and Krita's own composite in a KRA, are trusted.
        self.assertIsNone(scoring.unreliable_reference(self.case(three, stored=False)))
        self.assertIsNone(scoring.unreliable_reference(self.case(three, fmt="kra")))
        # A render nobody scored is no vote.
        blank = opened({"status": "unscored", "reason": "Embedded reference dimensions differ"})
        self.assertIsNone(scoring.unreliable_reference(self.case({**three, "krita": blank})))


class Runs(unittest.TestCase):
    def run_data(self):
        def case(name, fmt, cells):
            return {"id": name, "format": fmt, "source": name, "opened": True, "exported": True, "notes": [],
                    "cells": cells, **{key: {"status": "unscored"} for key in ("appearance", "document", "structure",
                                                                                "timing")}}
        return {"name": "Scored", "cases": [
            case("a.xcf", "xcf", {"patchy": opened(savedPsd=True, psd=psd()), "gimp": opened(still(0.2), savedPsd=True,
                                                                                             psd=psd(kept=1))}),
            case("b.xcf", "xcf", {"patchy": opened(savedPsd=True, psd=psd()), "gimp": UNMEASURED}),
            case("c.ase", "aseprite", {"patchy": opened(savedPsd=True, psd=psd()), "gimp": REFUSED})],
            "editors": {"patchy": {"name": "Patchy", "version": "1"}, "gimp": {"name": "GIMP", "version": "3"}},
            "psdJudge": {"photoshop": "28.0"}}

    def test_averages_leave_out_what_the_harness_could_not_measure(self):
        run = self.run_data()
        scores = scoring.apply(run)
        self.assertEqual(scores["version"], scoring.SCORING_VERSION)
        gimp = scores["formats"]["xcf"]["gimp"]
        self.assertEqual((gimp["files"], gimp["measured"], gimp["opened"]), (2, 1, 1))
        self.assertEqual((gimp["import"], gimp["importFiles"]), (0.8, 1))
        self.assertEqual(gimp["conversion"], round((1 + 1 + 0.5) / 3, 4))
        self.assertEqual(gimp["outcomes"], {"notMeasured": 1, "renderMismatch": 1})
        everything = scores["all"]["gimp"]
        self.assertEqual((everything["measured"], everything["opened"], everything["import"]), (2, 1, 0.4))
        self.assertEqual(scores["all"]["patchy"]["import"], 1.0)
        self.assertEqual(scores["formats"]["aseprite"]["gimp"]["outcomes"], {"refused": 1})
        self.assertEqual(run["cases"][2]["scores"]["gimp"]["conversion"], 0.0)
        self.assertIsNone(scoring.apply({"cases": []}))

    def ora_run(self):
        run = self.run_data()
        wrong = opened(still(1.0), savedPsd=True, psd=psd(bad=1.0))
        run["editors"]["krita"] = {"name": "Krita", "version": "5"}
        run["cases"].append({**run["cases"][0], "id": "d.ora", "source": "d.ora", "format": "ora",
                             "cells": {"patchy": wrong, "gimp": wrong, "krita": wrong}})
        return run

    def test_a_dropped_composite_is_counted_and_explained(self):
        run = self.ora_run()
        self.assertEqual(scoring.apply(run)["referencesNotUsed"], {})  # nothing says what it was scored against
        run["cases"][3]["storedComposite"] = True
        scores = scoring.apply(run)
        self.assertEqual(scores["referencesNotUsed"], {"ora": 1})
        self.assertIn("none of the 3 editors", run["cases"][3]["referenceNotUsed"])
        patchy = scores["formats"]["ora"]["patchy"]
        self.assertEqual((patchy["import"], patchy["importFiles"], patchy["conversion"]), (None, 0, 1.0))
        self.assertEqual(patchy["outcomes"], {"clean": 1})
        run["cases"][3]["cells"]["gimp"] = opened()  # scored again after a reader matches: the mark goes
        self.assertEqual(scoring.apply(run)["referencesNotUsed"], {})
        self.assertNotIn("referenceNotUsed", run["cases"][3])

    def test_rescore_rewrites_a_finished_run_from_its_measurements(self):
        scratch = ROOT / "build" / "test-output"
        scratch.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="patchy-native-scoring-", dir=scratch) as temp:
            folder = Path(temp)
            (folder / "results.json").write_text(json.dumps(self.run_data()), encoding="utf-8")
            result = native.rescore(folder)
            self.assertEqual(json.loads((folder / "results.json").read_text(encoding="utf-8")), result)
            html = (folder / "report.html").read_text(encoding="utf-8")
            for text in ("<h2>Scores</h2>", "To PSD", "80.0%", "Picture differs", "Did not open", "Not measured",
                         "All files"):
                self.assertIn(text, html)
            (folder / "results.json").write_text(json.dumps({"name": "Plain", "cases": []}), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Patchy alone"):
                native.rescore(folder)

    def test_rescore_learns_from_the_manifest_which_files_used_their_stored_composite(self):
        scratch = ROOT / "build" / "test-output"
        scratch.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="patchy-native-scoring-", dir=scratch) as temp:
            folder = Path(temp)
            manifest = folder / "manifest.json"
            manifest.write_text(json.dumps({"cases": [{"source": "a.xcf", "reference": "a.png"},
                                                      {"source": "d.ora", "reference": "@mergedimage"}]}),
                                encoding="utf-8")
            run = {**self.ora_run(), "manifestSHA256": native.file_digest(manifest)}
            (folder / "results.json").write_text(json.dumps(run), encoding="utf-8")
            self.assertEqual(native.rescore(folder)["scores"]["referencesNotUsed"], {})
            result = native.rescore(folder, manifest)
            self.assertEqual(result["scores"]["referencesNotUsed"], {"ora": 1})
            self.assertEqual([bool(case.get("storedComposite")) for case in result["cases"]],
                             [False, False, False, True])
            html = (folder / "report.html").read_text(encoding="utf-8")
            for text in ("1 file left out of the picture scores", "Reference not used", "none of the 3 editors"):
                self.assertIn(text, html)
            manifest.write_text(json.dumps({"cases": [{"source": "d.ora"}]}), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "not the one this run measured"):
                native.rescore(folder, manifest)


if __name__ == "__main__":
    unittest.main()
