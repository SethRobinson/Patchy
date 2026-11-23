"""Other editors as native import columns: verdicts, the breaker, the cell cache and references."""

import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "testy"))
import native_cross
import native_editors as editors
import native_import as native
import native_reference


def png(path: Path, color="red", size=(2, 2)) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.new("RGBA", size, color).save(path)
    return path


class Scratch(unittest.TestCase):
    def setUp(self):
        scratch = ROOT / "build" / "test-output"
        scratch.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="patchy-native-editors-", dir=scratch)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def adapter(self, answers, key="gimp", color="red"):
        """An editor whose successive exports give `answers`; "ok" writes the file."""
        calls = []

        def export(source, output):
            answer = answers[min(len(calls), len(answers) - 1)]
            calls.append(output.name)
            if answer == "ok":
                if output.suffix == ".png":
                    png(output, color)
                else:
                    output.write_bytes(b"8BPS converted")
                return {"ok": True, "exitCode": 0, "stderr": ""}
            if isinstance(answer, Exception):
                raise answer
            return {"ok": False, **answer}

        made = editors.Adapter(key, editors.NAMES[key], "1.0", export)
        made.calls = calls
        return made


REFUSED = {"exitCode": 1, "stderr": "unknown file type", "fileRejected": True}
HUNG = {"exitCode": -1, "stderr": "timeout after 180s", "fileRejected": False}
CRASHED = {"exitCode": 3221225477, "stderr": "", "fileRejected": False}
NOT_STARTED = {"exitCode": -1, "stderr": "[WinError 2] missing", "fileRejected": False}


class Verdicts(Scratch):
    def test_refusal_crash_and_harness_failure_are_three_different_results(self):
        source = self.root / "a.xcf"
        refused = editors.convert(self.adapter([REFUSED]), source, self.root / "r", True)
        self.assertIs(refused["opened"], False)
        self.assertNotIn("appFailure", refused)
        self.assertNotIn("savedPsd", refused)
        for answer in (HUNG, CRASHED):
            failed = editors.convert(self.adapter([answer]), source, self.root / f"f{answer['exitCode']}", True)
            self.assertIs(failed["opened"], False)
            self.assertTrue(failed["appFailure"])
        for answer in (NOT_STARTED, RuntimeError("driver bug"), {"harness": True, "stderr": "page never loaded"}):
            unknown = editors.convert(self.adapter([answer]), source, self.root / f"h{id(answer)}", True)
            self.assertIsNone(unknown["opened"])
            self.assertIn("error", unknown)

    def test_psd_step_runs_only_after_a_render_and_keeps_its_own_verdict(self):
        opened = self.adapter(["ok", REFUSED])
        cell = editors.convert(opened, self.root / "a.kra", self.root / "cell", True)
        self.assertEqual(opened.calls, ["render.png", "converted.psd"])
        self.assertTrue(cell["opened"] and cell["rendered"])
        self.assertIs(cell["savedPsd"], False)
        self.assertEqual(cell["psdError"], "unknown file type")
        plain = self.adapter(["ok"])
        self.assertNotIn("savedPsd", editors.convert(plain, self.root / "a.kra", self.root / "plain", False))
        self.assertEqual(plain.calls, ["render.png"])

    def test_breaker_skips_a_broken_column_but_refusals_never_trip_it(self):
        broken = self.adapter([HUNG])
        cells = [editors.convert(broken, self.root / "a.xcf", self.root / f"b{n}", False) for n in range(5)]
        self.assertEqual(len(broken.calls), editors.BREAKER_LIMIT)
        self.assertIn("skipped", cells[-1])
        self.assertIsNone(cells[-1]["opened"])
        picky = self.adapter([REFUSED])
        for n in range(6):
            self.assertIs(editors.convert(picky, self.root / "a.xcf", self.root / f"p{n}", False)["opened"], False)
        self.assertEqual(len(picky.calls), 6)
        mixed = self.adapter([HUNG, HUNG, "ok", HUNG, HUNG, "ok"])
        for n in range(6):
            editors.convert(mixed, self.root / "a.xcf", self.root / f"m{n}", False)
        self.assertEqual(len(mixed.calls), 6)

    def test_editor_list_is_validated(self):
        self.assertEqual(editors.parse_editors(None), ["patchy"])
        self.assertEqual(editors.parse_editors("GIMP, krita"), ["gimp", "krita"])
        for bad in ("photoshop", "gimp,gimp", ","):
            with self.assertRaises(ValueError):
                editors.parse_editors(bad)


class CrossRun(Scratch):
    def manifest(self, cases):
        path = self.root / "manifest.json"
        path.write_text(json.dumps({"name": "Test", "cases": cases}), encoding="utf-8")
        return path

    def run_cross(self, cases, cross, name="out"):
        def probe(case, copy, folder, exe, settings, timeout, extra=None):
            png(folder / "render.png")
            for key, target in (extra or {}).items():
                Path(target).write_bytes(b"8BPS patchy")
            return {"opened": True, "exported": True, "version": "9.9", "psdSaved": "psdOutput" in (extra or {}),
                    "aseSaved": "aseOutput" in (extra or {})}

        with patch.object(native, "_probe", side_effect=probe), patch.dict(os.environ, {"TESTY_SHUTDOWN_FILE": ""}):
            return native.run(self.manifest(cases), Path(sys.executable), self.root / name, cross=cross)

    def test_columns_cache_and_the_report(self):
        png(self.root / "ref.png")
        (self.root / "a.xcf").write_bytes(b"xcf one")
        (self.root / "b.ase").write_bytes(b"ase one")
        cases = [{"id": "a", "source": "a.xcf", "reference": "ref.png"},
                 {"id": "b", "source": "b.ase", "frames": ["ref.png", None], "durationsMs": [10, 20]}]
        cache = self.root / "cache"

        def cross():
            gimp, krita = self.adapter(["ok"]), self.adapter(["ok"], "krita", "blue")
            original = gimp.export
            gimp.refusals = []
            gimp.export = lambda source, output: (gimp.refusals.append(source.name) or REFUSED
                                                  if source.suffix == ".ase" else original(source, output))
            made = native_cross.Cross({"gimp": gimp, "krita": krita}, {"photopea": "needs the dashboard"}, cache=cache)
            return made, gimp, krita

        first, gimp, krita = cross()
        result = self.run_cross(cases, first)
        still, sprite = result["cases"]
        self.assertEqual(still["cells"]["patchy"]["appearance"]["status"], "pass")
        self.assertEqual(still["cells"]["gimp"]["appearance"]["status"], "pass")
        self.assertEqual(still["cells"]["krita"]["appearance"]["status"], "fail")
        # A format an editor cannot read is attempted like any other: the result is what was measured.
        self.assertIs(sprite["cells"]["gimp"]["opened"], False)
        self.assertEqual(sprite["cells"]["gimp"]["error"], "unknown file type")
        self.assertNotIn("appearance", sprite["cells"]["gimp"])
        self.assertEqual(gimp.refusals, ["b.ase"])
        self.assertIn("first frame", sprite["cells"]["krita"]["notes"][0])
        self.assertEqual(result["editors"]["gimp"], {"name": "GIMP", "version": "1.0"})
        self.assertEqual(result["editors"]["patchy"]["version"], "9.9")
        self.assertNotIn("psdJudge", result)
        html = (self.root / "out" / "report.html").read_text(encoding="utf-8")
        for text in ("Editors by format", "Opened none of this format", "the format's own app",
                     "photopea was not measured", "Did not open",
                     'href="cells/0001/gimp/render.png"'):
            self.assertIn(text, html)

        # A second run restores what each editor wrote, and its clean refusals, and scores again.
        again, gimp, krita = cross()
        rerun = self.run_cross(cases, again, "again")["cases"]
        cached = rerun[0]["cells"]
        self.assertTrue(cached["gimp"]["cached"])
        self.assertEqual(cached["krita"]["appearance"]["status"], "fail")
        self.assertTrue(rerun[1]["cells"]["gimp"]["cached"])
        self.assertIs(rerun[1]["cells"]["gimp"]["opened"], False)
        self.assertEqual((gimp.calls, gimp.refusals, krita.calls), ([], [], []))
        self.assertTrue((self.root / "again" / "cells" / "0001" / "gimp" / "render.png").is_file())
        fresh, gimp, krita = cross()
        fresh.fresh = True
        self.assertNotIn("cached", self.run_cross(cases, fresh, "fresh")["cases"][0]["cells"]["gimp"])

    def test_a_stored_composite_no_layer_reader_matches_is_left_out_of_the_scores(self):
        import zipfile

        def ora(name, color):
            with zipfile.ZipFile(self.root / name, "w") as archive:
                archive.write(png(self.root / "composite.png", color), arcname="mergedimage.png")
                archive.writestr("stack.xml", '<image w="2" h="2"><stack/></image>')
            return {"id": name, "source": name, "reference": "@mergedimage"}

        cases = [ora("stale.ora", "green"), ora("true.ora", "blue")]
        cross = native_cross.Cross({"gimp": self.adapter(["ok"]), "krita": self.adapter(["ok"], "krita", "blue"),
                                    "photopea": self.adapter(["ok"], "photopea", "green")}, cache=self.root / "cache")
        result = self.run_cross(cases, cross)
        stale, true = result["cases"]
        self.assertTrue(stale["storedComposite"] and true["storedComposite"])
        # Patchy, GIMP and Krita all compose something else; only the editor that shows the stored picture matches.
        self.assertIn("none of the 3 editors", stale["referenceNotUsed"])
        self.assertEqual(stale["scores"], dict.fromkeys(("patchy", "gimp", "krita", "photopea"), {"outcome": "clean"}))
        self.assertNotIn("referenceNotUsed", true)  # Krita reproduces this one, so the others are held to it
        self.assertEqual((true["scores"]["patchy"]["import"], true["scores"]["krita"]["import"]), (0.0, 1.0))
        self.assertEqual(result["scores"]["referencesNotUsed"], {"ora": 1})
        self.assertEqual(result["scores"]["formats"]["ora"]["patchy"]["importFiles"], 1)
        html = (self.root / "out" / "report.html").read_text(encoding="utf-8")
        self.assertIn("1 file left out of the picture scores", html)
        self.assertIn("Reference not used", html)

    def test_aseprite_reopens_the_still_sprite_patchy_saved(self):
        (self.root / "still.ase").write_bytes(b"still")
        (self.root / "moving.ase").write_bytes(b"moving")
        cases = [{"id": "still", "source": "still.ase"},
                 {"id": "moving", "source": "moving.ase", "durationsMs": [10, 20]}]

        def frames(count):
            return lambda exe, source, folder: ([png(folder / f"frame-{n}.png") for n in range(count)], "")

        def run(name, reopened):
            refs = native_reference.References("pinned", cache=self.root / "refcache")
            refs.mode = "installed"
            refs.apps["aseprite"] = {"name": "Aseprite", "exe": Path("aseprite"), "version": "1.3",
                                     "render": lambda exe, source, folder: frames(
                                         2 if source.name.startswith("moving") else 1)(exe, source, folder)}
            cross = native_cross.Cross(references=refs, cache=self.root / "cache")
            with patch.object(native_cross.aseprite, "render_frames", side_effect=reopened):
                return self.run_cross(cases, cross, name)

        result = run("out", frames(1))
        still, moving = result["cases"]
        self.assertEqual(still["saveReopen"]["status"], "pass")
        self.assertEqual(still["saveReopen"]["credit"], "Aseprite 1.3")
        self.assertEqual(still["reference"], {"source": "Aseprite 1.3 render", "crossCheck": "none"})
        self.assertNotIn("saveReopen", moving)  # Patchy's .aseprite holds one frame
        self.assertEqual(result["references"]["aseprite"], "Aseprite 1.3 render")
        self.assertIn("Patchy save reopened in Aseprite", (self.root / "out/report.html").read_text(encoding="utf-8"))
        split = run("split", frames(2))["cases"][0]["saveReopen"]
        self.assertEqual(split["status"], "fail")
        self.assertIn("2 frames", split["reason"])

    def test_plain_run_has_no_editor_data(self):
        (self.root / "a.xcf").write_bytes(b"xcf")
        result = self.run_cross([{"source": "a.xcf"}], None)
        self.assertNotIn("editors", result)
        self.assertNotIn("cells", result["cases"][0])
        self.assertNotIn("Editors by format", (self.root / "out" / "report.html").read_text(encoding="utf-8"))

    def test_psd_judge_scores_each_saved_psd_and_reuses_a_cached_probe(self):
        png(self.root / "ref.png")
        (self.root / "a.xcf").write_bytes(b"xcf one")
        cases = [{"id": "a", "source": "a.xcf", "reference": "ref.png", "expect": {"layers": [{"name": "Ink"}]}}]

        class Photoshop:
            unavailable = False
            unavailable_reason = None
            probes = 0

            def version(self):
                return "28.0"

            def probe(self, psd, render):
                Photoshop.probes += 1
                if psd.read_bytes().endswith(b"patchy"):
                    return {"ok": False, "error": "not a Photoshop document", "fileRejected": True}
                png(render)
                return {"ok": True, "render": "ok", "layers": [{"name": "Ink", "kind": "NORMAL", "group": False,
                                                                 "path": "0"}]}

        def run(name):
            cross = native_cross.Cross({"gimp": self.adapter(["ok"])}, {}, photoshop=Photoshop(), psd_judge=True,
                                       cache=self.root / "cache")
            return self.run_cross(cases, cross, name)

        result = run("out")
        cells = result["cases"][0]["cells"]
        self.assertEqual(result["psdJudge"], {"photoshop": "28.0"})
        self.assertEqual(cells["gimp"]["psd"]["accepted"]["status"], "pass")
        self.assertEqual(cells["gimp"]["psd"]["appearance"]["status"], "pass")
        self.assertEqual(cells["gimp"]["psd"]["layers"]["layersKept"], 1)
        self.assertEqual(cells["patchy"]["psd"]["accepted"]["status"], "fail")
        self.assertEqual(cells["patchy"]["psd"]["layers"]["status"], "unscored")
        html = (self.root / "out" / "report.html").read_text(encoding="utf-8")
        self.assertIn("All layers kept in the PSD", html)
        self.assertIn("Photoshop 28.0", html)
        self.assertEqual(Photoshop.probes, 2)
        again = run("again")["cases"][0]["cells"]
        self.assertEqual(Photoshop.probes, 3)  # Patchy's again; GIMP's probe came from the cache
        self.assertEqual(again["gimp"]["psd"]["appearance"]["status"], "pass")


class Reference(Scratch):
    def references(self, frames):
        refs = native_reference.References("pinned", cache=self.root / "refcache")
        self.renders = 0

        def render(exe, source, folder):
            self.renders += 1
            return ([png(folder / f"f{n}.png", color) for n, color in enumerate(frames)], "" if frames else "it refused")

        refs.mode = "installed"
        refs.apps["aseprite"] = {"name": "Aseprite", "exe": Path("aseprite"), "version": "1.3", "render": render}
        return refs

    def resolve(self, refs, case):
        staged = self.root / "s.ase"
        staged.write_bytes(b"sprite")
        return refs.resolve(case, staged, self.root, "aseprite", "ab" * 32)

    def test_installed_app_supplies_frames_and_the_supplied_png_is_a_cross_check(self):
        png(self.root / "red.png")
        refs = self.references(["red", "blue"])
        case, info = self.resolve(refs, {"source": "s.ase", "frames": ["red.png", None], "durationsMs": [1, 2]})
        self.assertEqual(info, {"source": "Aseprite 1.3 render", "crossCheck": "agrees"})
        self.assertEqual(len(case["frames"]), 2)
        self.assertTrue(all(Path(frame).is_file() for frame in case["frames"]))
        self.resolve(refs, {"source": "s.ase", "frames": ["red.png", None], "durationsMs": [1, 2]})
        self.assertEqual(self.renders, 1)  # cached by source hash and app version
        self.assertEqual(refs.sources()["aseprite"], "Aseprite 1.3 render")
        self.assertEqual(refs.sources()["kra"], "Composite saved in the file")

    def test_disagreement_makes_appearance_ineligible_and_keeps_timing(self):
        png(self.root / "red.png")
        case, info = self.resolve(self.references(["blue", "blue"]),
                                  {"source": "s.ase", "frames": ["red.png", None], "durationsMs": [1, 2]})
        self.assertEqual(info["crossCheck"], "differs")
        self.assertIn("disagree", info["ineligible"])
        self.assertNotIn("frames", case)
        self.assertEqual(case["durationsMs"], [1, 2])

    def test_unusable_render_falls_back_to_the_supplied_reference(self):
        png(self.root / "red.png")
        for frames in ([], ["red"]):
            original = {"source": "s.ase", "frames": ["red.png", None], "durationsMs": [1, 2]}
            case, info = self.resolve(self.references(frames), original)
            self.assertIs(case, original)
            self.assertEqual(info["source"], "PNG supplied with the corpus")
        still, info = self.resolve(self.references(["red"]), {"source": "s.ase"})
        self.assertTrue(Path(still["reference"]).is_file())
        self.assertEqual(info["crossCheck"], "none")
        self.assertEqual(native_reference.References("pinned").resolve({"source": "s.ase"}, self.root, self.root,
                                                                      "aseprite", "00"), ({"source": "s.ase"}, None))


if __name__ == "__main__":
    unittest.main()
