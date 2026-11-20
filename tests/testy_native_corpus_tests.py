"""Verify offline fixture integrity and the distinction between frame and atlas PNGs."""
import io
from pathlib import Path
import re
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "testy"))
import fetch_native_corpora as corpus


class NativeCorpusTests(unittest.TestCase):
    def setUp(self):
        output = ROOT / "build/test-output"
        output.mkdir(parents=True, exist_ok=True)
        scratch = tempfile.TemporaryDirectory(dir=output, prefix="testy-native-corpus-")
        self.addCleanup(scratch.cleanup)
        self.root = Path(scratch.name)

    def test_pinned_fetch_rejects_changed_local_data_and_skips_other_archive_members(self):
        pack = io.BytesIO()
        with ZipFile(pack, "w") as archive:
            archive.writestr("pack-pin/data/a.png", b"original")
            archive.writestr("pack-pin/script.py", b"unselected")
        checksum = corpus.inventory_hash({"data/a.png": corpus.digest(b"original")})
        args = ("author/pack", "pin", self.root, re.compile(r"data/a\.png"), checksum)
        with patch.object(corpus, "download", return_value=pack.getvalue()) as fetch:
            corpus.fetch_pack(*args, offline=False)
            corpus.fetch_pack(*args, offline=True)
            fetch.assert_called_once()
            self.assertFalse((self.root / "script.py").exists())
            sample = self.root / "data/a.png"
            sample.write_bytes(b"changed")
            with self.assertRaisesRegex(ValueError, "Missing or changed"):
                corpus.fetch_pack(*args, offline=True)
            with self.assertRaisesRegex(ValueError, "Unexpected or modified"):
                corpus.fetch_pack(*args, offline=False)
            self.assertEqual(sample.read_bytes(), b"changed")

    def test_numbered_frames_and_nonframe_images(self):
        inputs = self.root / "tests/data"
        inputs.mkdir(parents=True)
        def sprite(name, durations):
            blob = bytearray(128)
            struct.pack_into("<3H", blob, 6, len(durations), 16, 16)
            for duration in durations:
                blob.extend(struct.pack("<I3H6x", 16, 0xf1fa, 0, duration))
            (inputs / (name + ".aseprite")).write_bytes(blob)
        sprite("indexed", [90, 130, 100, 80])
        sprite("tilemap", [100])
        sprite("tileset", [100])
        sprite("util_extrude", [100])
        with patch.object(corpus, "ASE_DIR", self.root), patch.object(corpus, "CORPORA", self.root):
            rows = {c["id"]: c for c in corpus.ase_cases()}
        indexed = rows["aseprite/indexed"]
        self.assertEqual(indexed["frames"], ["tests/data/indexed_01.png", None, None, None])
        self.assertEqual(indexed["durationsMs"], [90, 130, 100, 80])
        self.assertNotIn("durationsMs", rows["aseprite/tilemap"])
        self.assertNotIn("aseprite/tileset", rows)
        self.assertEqual(rows["aseprite/util_extrude"]["frames"], [None])

    def test_gimp_modern_multiply_keeps_its_source_blend_space(self):
        for suffix, expected in (("", "linear-multiply"), ("-perceptual", "multiply")):
            with self.subTest(suffix=suffix):
                layers = corpus.xcf_layers("layers-8bit" + suffix)
                self.assertEqual(layers[1]["blendMode"], expected)
                children = corpus.xcf_layers("groups" + suffix)[1]["children"]
                self.assertEqual(children[0]["blendMode"], expected)

    def test_official_sprite_with_timing_has_no_appearance_reference(self):
        sprite = bytearray(128)
        struct.pack_into("<3H", sprite, 6, 2, 4, 4)
        for delay in (80, 150):
            sprite.extend(struct.pack("<I3H6x", 16, 0xf1fa, 0, delay))
        (self.root / "tags3.aseprite").write_bytes(sprite)
        files = {"tests/sprites/tags3.aseprite": corpus.digest(sprite), "tests/LICENSE.txt": ""}
        with patch.object(corpus, "ASEPRITE_DIR", self.root), patch.object(corpus, "ASEPRITE_FILES", files):
            case, = corpus.aseprite_upstream_cases()
        self.assertEqual(case["durationsMs"], [80, 150])
        self.assertNotIn("reference", case)
        self.assertNotIn("frames", case)
        self.assertIn("tag directions", case["referenceNote"])


if __name__ == "__main__":
    unittest.main()
