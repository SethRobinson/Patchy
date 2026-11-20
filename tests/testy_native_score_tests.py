"""Native-format scores exercise independent structure, frame timing and appearance oracles."""
import sys
import tempfile
import unittest
from pathlib import Path
import struct
from unittest.mock import patch
from zipfile import ZipFile
import zlib

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "testy"))
import native_import_score as scorer


class NativeScores(unittest.TestCase):
    def setUp(self):
        scratch = ROOT / "build/test-output"
        scratch.mkdir(parents=True, exist_ok=True)
        self.scratch = tempfile.TemporaryDirectory(prefix="testy-native-score-", dir=scratch)
        self.addCleanup(self.scratch.cleanup)
        self.folder = Path(self.scratch.name)
        self.observed = {"opened": True, "exported": True, "width": 4, "height": 3, "depth": 8,
                         "layers": [{"name": "paint", "visible": True}, {"name": "sky", "visible": False}]}
        self.render = self.folder / "render.png"

    def png(self, name, color, size=(4, 3)):
        image = Image.new("RGBA", size, color)
        image.save(self.folder / name)
        return image

    def score(self, **case):
        return scorer.score_case({"source": "sample.kra", **case}, self.observed, self.folder, self.folder)

    def test_linear_samples_check_hdr_and_alpha_independently_of_png(self):
        import tifffile
        rgba = np.zeros((3, 4, 4), dtype=np.float32)
        rgba[0, 0] = (-.125, .5, 3.75, .25)
        tifffile.imwrite(self.folder / "render32.tif", rgba, photometric="rgb", extrasamples=["unassalpha"])
        self.observed.update(inspected=True, depth=32, floatExported=True)
        wanted = [{"x": 0, "y": 0, "rgba": [-.125, .5, 3.75, .25]},
                  {"x": 3, "y": 2, "rgba": [9, 9, 9, 0]}]
        self.assertEqual(self.score(linearSamples=wanted)["linearSamples"]["status"], "pass")
        wanted[0]["rgba"][2] = 1
        result = self.score(linearSamples=wanted)
        self.assertEqual(result["linearSamples"]["status"], "fail")
        self.assertEqual(result["appearance"]["status"], "unscored")
        self.observed["depth"] = 16
        self.assertEqual(self.score(linearSamples=wanted)["linearSamples"]["status"], "fail")
        self.observed.update(depth=32, floatExported=False)
        self.assertEqual(self.score(linearSamples=wanted)["linearSamples"]["status"], "unscored")

    def test_structure_is_partial_ordered_and_independent_of_export(self):
        self.observed["exported"] = False
        expectation = {"width": 4, "layers": [{"name": "paint"}, {"visible": False}]}
        result = self.score(expect=expectation, reference="image.png")
        self.assertEqual(result["document"]["status"], "pass")
        self.assertEqual(result["structure"]["status"], "pass")
        dimensions = self.score(expect={"width": 4, "height": 3, "depth": 8})
        self.assertEqual(dimensions["document"]["status"], "pass")
        self.assertEqual(dimensions["structure"]["status"], "unscored")
        self.assertEqual(result["appearance"]["status"], "unscored")
        expectation["layers"].reverse()
        self.assertEqual(self.score(expect=expectation)["structure"]["status"], "fail")
        expectation["layers"].pop()
        self.assertIn("layers.length", self.score(expect=expectation)["structure"]["reason"])

    def test_missing_references_are_unscored_and_cannot_hide_failure(self):
        self.png("render.png", "green")
        self.assertEqual(self.score()["appearance"]["status"], "unscored")
        self.assertEqual(self.score(reference="missing.png")["appearance"]["status"], "unscored")
        self.png("frame.png", "orange")
        result = self.score(frames=[None, "frame.png"])["appearance"]
        self.assertEqual(result["status"], "fail")
        self.assertEqual(result["counts"], {"pass": 0, "fail": 1, "unscored": 1})
        self.assertEqual(result["actualFrames"], 1)

    def test_authoring_and_reopen_references_score_independently(self):
        self.png("render.png", "white")
        self.png("authored.png", "black")
        self.png("reopened.png", "white")
        spec = {"png": "reopened.png", "credit": "Independent editor"}
        result = self.score(reference="authored.png", reopenReference=spec)
        self.assertEqual(result["appearance"]["status"], "fail")
        self.assertEqual((result["reopenAppearance"]["status"], result["reopenAppearance"]["credit"]),
                         ("pass", "Independent editor"))
        with Image.open(self.folder / "reference.png") as image:
            self.assertEqual(image.convert("RGB").getpixel((0, 0)), (0, 0, 0))
        result = self.score(reference="authored.png", reopenReference={**spec, "png": "missing.png"})
        self.assertEqual(result["appearance"]["status"], "fail")
        self.assertEqual(result["reopenAppearance"]["status"], "unscored")
        self.assertEqual(self.score(reopenReference=spec)["reopenAppearance"]["status"], "pass")

    def test_unreadable_png_and_missing_mergedimage_are_unscored(self):
        self.png("render.png", "green")
        (self.folder / "bad.png").write_bytes(b"bad")
        self.assertEqual(self.score(reference="bad.png")["appearance"]["status"], "unscored")
        with ZipFile(self.folder / "sample.kra", "w") as archive:
            archive.write(self.render, arcname="preview.png")
        self.assertIn("mergedimage.png", self.score(reference="@mergedimage")["appearance"]["reason"])

    def test_mergedimage_oracle_checks_size_and_bounds_its_input(self):
        self.png("render.png", "orange")
        with ZipFile(self.folder / "sample.kra", "w") as archive:
            archive.write(self.render, arcname="mergedimage.png")
            archive.writestr("maindoc.xml", '<DOC xmlns="urn:example"><IMAGE width="4" height="3"/></DOC>')
        self.assertEqual(self.score(reference="@mergedimage")["appearance"]["status"], "pass")
        with patch.object(scorer, "MAX_MERGED_BYTES", 1):
            self.assertEqual(self.score(reference="@mergedimage")["appearance"]["status"], "unscored")
        self.png("render.png", "orange", (2, 3))
        self.assertIn("dimensions", self.score(reference="@mergedimage")["appearance"]["reason"])

    def test_padded_embedded_render_is_unscored(self):
        self.png("render.png", "orange")
        self.png("padded.png", "orange", (8, 8))
        with ZipFile(self.folder / "sample.ora", "w") as archive:
            archive.writestr("stack.xml", '<image w="4" h="3"><stack/></image>')
            archive.write(self.folder / "padded.png", arcname="mergedimage.png")
        result = self.score(source="sample.ora", reference="@mergedimage")["appearance"]
        self.assertEqual(result["status"], "unscored")
        self.assertIn("source canvas", result["reason"])

    def test_still_checks_canvas_perceptual_color_and_alpha(self):
        self.png("render.png", "white")
        self.png("light.png", (252, 252, 252, 255))
        self.assertEqual(self.score(reference="light.png")["appearance"]["status"], "pass")
        self.png("black.png", "black")
        self.assertEqual(self.score(reference="black.png")["appearance"]["status"], "fail")
        self.png("clear.png", (255, 255, 255, 0))
        row = self.score(reference="clear.png")["appearance"]["frames"][0]
        self.assertEqual(row["perceptual"]["badFraction"], 0)
        self.assertEqual(row["differentAlphaPixels"], 12)
        self.assertEqual(row["status"], "fail")
        self.png("slight.png", (255, 255, 255, 250))
        row = self.score(reference="slight.png")["appearance"]["frames"][0]
        self.assertEqual((row["status"], row["differentAlphaPixels"], row["alphaBadPixels"]), ("pass", 12, 0))
        self.observed["width"] = 3
        self.assertIn("canvas", self.score(reference="light.png")["appearance"]["reason"])

    def test_16_bit_grayscale_uses_display_values(self):
        # 32768 narrows to middle gray. Pillow's direct convert('RGBA') clips it to white.
        reference = Image.frombytes("I;16", (4, 3), b"\x00\x80" * 12)
        reference.save(self.folder / "deep.png")
        self.png("render.png", (128, 128, 128, 255))
        self.assertEqual(self.score(reference="deep.png")["appearance"]["status"], "pass")

    def test_deep_rgba_and_embedded_reference_round_to_8_bits(self):
        def chunk(kind, data):
            return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

        pixels = struct.pack(">4H", 65535, 255, 0, 511) * 4
        png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 4, 3, 16, 6, 0, 0, 0))
               + chunk(b"IDAT", zlib.compress((b"\0" + pixels) * 3)) + chunk(b"IEND", b""))
        self.png("render.png", (255, 1, 0, 2))
        with ZipFile(self.folder / "sample.kra", "w") as archive:
            archive.writestr("maindoc.xml", '<DOC><IMAGE width="4" height="3"/></DOC>')
            archive.writestr("mergedimage.png", png)
        row = self.score(reference="@mergedimage")["appearance"]["frames"][0]
        self.assertEqual((row["status"], row["differentAlphaPixels"]), ("pass", 0))
        self.render.write_bytes(png)
        self.png("ref.png", (255, 1, 0, 2))
        row = self.score(reference="ref.png")["appearance"]["frames"][0]
        self.assertEqual((row["status"], row["differentAlphaPixels"]), ("pass", 0))

    def test_sprite_compares_exact_visible_channels(self):
        ref = self.png("ref.png", (255, 0, 9, 0))
        self.png("render.png", (0, 0, 0, 0))
        result = self.score(source="sprite.ase", reference="ref.png")["appearance"]
        self.assertEqual(result["status"], "pass")
        ref.putpixel((1, 1), (0, 0, 0, 1))
        ref.save(self.folder / "ref.png")
        result = self.score(source="sprite.ase", reference="ref.png")["appearance"]
        self.assertEqual(result["frames"][0]["differentPixels"], 1)
        self.assertEqual(result["status"], "fail")

    def test_sprite_severity_weights_color_by_alpha_and_keeps_exact_status(self):
        ref = self.png("ref.png", (255, 0, 0, 0))
        actual = self.png("render.png", (0, 0, 0, 0))
        # Raw 255 with alpha 1 has a visible impact of one byte level.
        ref.putpixel((0, 0), (255, 0, 0, 1))
        actual.putpixel((0, 0), (0, 0, 0, 1))
        # A difference of exactly six passes the diagnostic tolerance, but still fails exact comparison.
        ref.putpixel((1, 0), (6, 0, 0, 255))
        actual.putpixel((1, 0), (0, 0, 0, 255))
        ref.save(self.folder / "ref.png")
        actual.save(self.render)
        row = self.score(source="sprite.ase", reference="ref.png")["appearance"]["frames"][0]
        self.assertEqual((row["status"], row["differentPixels"], row["maxVisibleDelta"],
                          row["visiblePixelsOverTolerance"]), ("fail", 2, 255, 1))
        self.assertEqual((row["maxPremultipliedDelta"], row["premultipliedPixelsOverTolerance"]), (6, 0))
        # The third pixel differs by alpha alone, the fourth by 1540/255 in premultiplied red.
        ref.putpixel((2, 0), (0, 0, 0, 7))
        ref.putpixel((3, 0), (140, 0, 0, 11))
        actual.putpixel((3, 0), (0, 0, 0, 11))
        ref.save(self.folder / "ref.png")
        actual.save(self.render)
        row = self.score(source="sprite.ase", reference="ref.png")["appearance"]["frames"][0]
        self.assertEqual((row["differentPixels"], row["maxPremultipliedDelta"],
                          row["premultipliedPixelsOverTolerance"], row["visiblePixelsOverTolerance"]), (4, 7, 2, 3))

    def test_animated_frames_and_timing_include_order_and_partial_coverage(self):
        first = self.png("one.png", "red")
        second = self.png("two.png", "blue")
        first.save(self.render, save_all=True, append_images=[second], duration=[60, 130], loop=0)
        result = self.score(frames=["one.png", "two.png"], durationsMs=[60, 130])
        self.assertEqual((result["appearance"]["status"], result["timing"]["status"]), ("pass", "pass"))
        result = self.score(frames=["one.png", None], durationsMs=[130, 60])
        self.assertEqual(result["appearance"]["status"], "unscored")
        self.assertEqual(result["appearance"]["counts"]["pass"], 1)
        self.assertEqual(result["timing"]["status"], "fail")
        self.assertEqual(result["timing"]["frames"][1]["actualMs"], 130)
        result = self.score(frames=["one.png", "one.png"], durationsMs=[60])
        self.assertEqual(result["appearance"]["status"], "fail")
        self.assertTrue(result["appearance"]["comparisonImages"])
        with Image.open(self.folder / "reference.png") as expected, Image.open(self.folder / "actual.png") as actual:
            np.testing.assert_array_equal(np.asarray(expected), np.asarray(first))
            np.testing.assert_array_equal(np.asarray(actual), np.asarray(second))
        self.assertIn("rendered 2", result["timing"]["reason"])

    def test_apng_poster_does_not_count_as_animation(self):
        poster = Image.new("RGBA", (4, 3), "yellow")
        first = self.png("one.png", "red")
        second = self.png("two.png", "blue")
        poster.save(self.render, save_all=True, append_images=[first, second], default_image=True,
                    duration=[1001, 130], loop=0)
        result = self.score(frames=["one.png", "two.png"], durationsMs=[1001, 130])
        self.assertEqual((result["appearance"]["status"], result["timing"]["status"]), ("pass", "pass"))


if __name__ == "__main__":
    unittest.main()
