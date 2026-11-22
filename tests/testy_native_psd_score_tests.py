"""Photoshop's verdict on a PSD converted from a native file: acceptance, render and layers kept."""

from pathlib import Path
import sys
import tempfile
import unittest

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "testy"))
import native_psd_score as psd


def layer(name, path, kind="NORMAL", **more):
    return {"name": name, "path": path, "kind": kind, "group": kind == "GROUP", **more}


# Bottom-to-top, as manifests list layers.
TREE = [{"name": "Backdrop", "visible": True, "opacity": 100},
        {"name": "Set", "kind": "group", "children": [
            {"name": "Ink", "hasMask": True, "blendMode": "multiply", "opacity": 50},
            {"name": "Title", "kind": "text", "visible": False, "blendMode": "linear-multiply"}]}]
# Top first, as Photoshop lists them.
KEPT = [layer("Set", "0", "GROUP"),
        layer("Title", "0/0", "TEXT", layerVisible=False, visible=False, blend="NORMAL"),
        layer("Ink", "0/1", userMask=True, blend="MULTIPLY", opacity=50.2),
        layer("Backdrop", "1", layerVisible=True, opacity=100)]


class LayersKept(unittest.TestCase):
    def test_tree_is_flattened_in_photoshop_order(self):
        rows = psd.expected_layers(TREE)
        self.assertEqual([row["name"] for row in rows], ["Set", "Title", "Ink", "Backdrop"])
        self.assertEqual([row["group"] for row in rows], [True, False, False, False])
        self.assertIs(rows[1]["parent"], rows[0])
        self.assertIsNone(rows[3]["parent"])

    def test_every_layer_and_attribute_kept(self):
        result = psd.layers_kept(TREE, KEPT)
        self.assertEqual((result["status"], result["layersKept"], result["layersTotal"]), ("pass", 4, 4))
        self.assertEqual(result["attributes"], {"nesting": {"kept": 4, "total": 4}, "visible": {"kept": 2, "total": 2},
                                                "mask": {"kept": 1, "total": 1}, "opacity": {"kept": 2, "total": 2},
                                                "blend": {"kept": 1, "total": 1}})
        self.assertNotIn("lost", result)

    def test_a_mode_a_psd_cannot_hold_is_never_expected(self):
        self.assertEqual(psd.photoshop_blend("linear-dodge"), "LINEARDODGE")
        self.assertEqual(psd.photoshop_blend("passThrough"), "PASSTHROUGH")
        self.assertIsNone(psd.photoshop_blend("linear-multiply"))
        self.assertIsNone(psd.photoshop_blend("grain-merge"))

    def test_rasterized_text_moved_and_changed_layers_are_reported(self):
        actual = [layer("Set", "0", "GROUP"), layer("Ink", "0/0", blend="NORMAL", opacity=100),
                  layer("Title", "1", layerVisible=True), layer("Backdrop", "2", layerVisible=True, opacity=100)]
        result = psd.layers_kept(TREE, actual)
        self.assertEqual((result["status"], result["layersKept"]), ("fail", 3))
        self.assertEqual(result["lost"], [{"name": "Title", "kind": "text", "became": "NORMAL"}])
        self.assertIn("'Title' became NORMAL", result["reason"])
        attributes = result["attributes"]
        self.assertEqual(attributes["nesting"], {"kept": 3, "total": 4})
        self.assertEqual(attributes["mask"]["kept"], 0)
        self.assertEqual(attributes["opacity"], {"kept": 1, "total": 2})
        self.assertEqual(attributes["blend"]["kept"], 0)
        self.assertEqual(attributes["visible"], {"kept": 1, "total": 2})

    def test_a_flattened_psd_keeps_one_layer_at_most(self):
        result = psd.layers_kept(TREE, [layer("Background", "0")])
        self.assertEqual((result["status"], result["layersKept"], result["layersTotal"]), ("fail", 0, 4))
        self.assertEqual(result["lost"][0], {"name": "Set", "kind": "group", "became": "NORMAL"})
        plain = psd.layers_kept([{"name": "Only"}, {"name": "Other"}], [layer("Background", "0")])
        self.assertEqual((plain["layersKept"], plain["renamed"]), (1, 1))

    def test_missing_tree_is_unscored(self):
        self.assertEqual(psd.layers_kept(None, KEPT)["status"], "unscored")
        self.assertEqual(psd.layers_kept([], KEPT)["status"], "unscored")


class Score(unittest.TestCase):
    def setUp(self):
        scratch = ROOT / "build" / "test-output"
        scratch.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="patchy-native-psd-", dir=scratch)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        Image.new("RGBA", (4, 4), "red").save(self.root / "ref.png")

    def render(self, color):
        Image.new("RGBA", (4, 4), color).save(self.root / "photoshop.png")
        return {"ok": True, "render": "ok", "layers": [layer("Only", "0")], "dialogs": ["Some text may look wrong"]}

    def test_accepted_psd_gets_render_and_layers(self):
        case = {"source": "a.xcf", "reference": "ref.png", "expect": {"layers": [{"name": "Only"}]}}
        result = psd.score(self.render("red"), "28.0", case, self.root, self.root)
        self.assertEqual([result[key]["status"] for key in ("accepted", "appearance", "layers")], ["pass"] * 3)
        self.assertEqual(result["warnings"], ["Some text may look wrong"])
        self.assertEqual(result["photoshop"], "28.0")
        self.assertEqual(psd.score(self.render("blue"), "28.0", case, self.root, self.root)["appearance"]["status"],
                         "fail")
        self.assertEqual(psd.score(self.render("red"), "28.0", {"source": "a.xcf", "reference": "ref.png"},
                                   self.root, self.root)["layers"]["status"], "unscored")

    def test_sprite_psd_uses_the_still_rule_and_an_animation_has_no_picture(self):
        still = {"source": "a.aseprite", "frames": ["ref.png"]}
        self.assertEqual(psd.score(self.render("red"), "28.0", still, self.root, self.root)["appearance"]["metric"],
                         "8-bit sRGB appearance over white")
        moving = {"source": "a.aseprite", "frames": ["ref.png", "ref.png"], "durationsMs": [10, 10]}
        result = psd.score(self.render("red"), "28.0", moving, self.root, self.root)
        self.assertEqual(result["appearance"]["status"], "unscored")
        self.assertIn("animation", result["appearance"]["reason"])
        self.assertEqual(result["accepted"]["status"], "pass")

    def test_photoshop_render_has_no_transparency_to_compare(self):
        Image.new("RGBA", (4, 4), (255, 255, 255, 0)).save(self.root / "ref.png")
        result = psd.score(self.render("white"), "28.0", {"source": "a.xcf", "reference": "ref.png"},
                           self.root, self.root)
        self.assertEqual(result["appearance"]["status"], "pass")
        self.assertNotIn("alphaBadFraction", result["appearance"])

    def test_rejection_counts_and_a_photoshop_failure_does_not(self):
        case = {"source": "a.xcf", "reference": "ref.png"}
        rejected = psd.score({"ok": False, "error": "not a Photoshop document", "fileRejected": True}, "28.0", case,
                             self.root, self.root)
        self.assertEqual(rejected["accepted"], {"status": "fail", "reason": "not a Photoshop document"})
        self.assertEqual(rejected["layers"]["status"], "unscored")
        for failure in ({"hung": True}, {"launchFailure": True}, {"crashed": True}):
            result = psd.score({"ok": False, "error": "photoshop hung", **failure}, "28.0", case, self.root, self.root)
            self.assertEqual(result["accepted"]["status"], "unscored")
            self.assertIn("could not be asked", result["accepted"]["reason"])


if __name__ == "__main__":
    unittest.main()
