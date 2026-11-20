"""Local corpus conversion preserves native file and tree references."""

from pathlib import Path
import sys
import tempfile
import unittest
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "testy"))
from local_import_corpus import build_manifest


class TestLocalCorpus(unittest.TestCase):
    def test_archive_canvas_tree_and_linked_resources(self):
        scratch = ROOT / "build/test-output"
        scratch.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="testy-local-archive-", dir=scratch) as tmp:
            base = Path(tmp)
            kra, ora = base / "kra-corpus", base / "ora"
            kra.mkdir()
            ora.mkdir()
            (kra / "images").mkdir()
            (kra / "images/sky.png").write_bytes(b"\x89PNG\r\n\x1a\n")
            (base / "outside.png").write_bytes(b"\x89PNG\r\n\x1a\n")

            def write_kra(name, layers):
                with ZipFile(kra / name, "w") as archive:
                    archive.writestr("maindoc.xml", '<DOC xmlns="urn:krita"><IMAGE width="3" height="4">'
                                     f'<layers>{layers}</layers></IMAGE></DOC>')

            write_kra("plain.kra", '<layer nodetype="grouplayer" name="Set"><layers>'
                      '<layer nodetype="paintlayer" name="Top" visible="0"/>'
                      '<layer nodetype="paintlayer" name="Bottom"/></layers></layer>'
                      '<layer nodetype="paintlayer" name="Ground"/>')
            write_kra("linked.kra", '<layer nodetype="filelayer" name="Sky" source="images/sky.png"/>'
                      '<layer nodetype="filelayer" name="Lost" source="../outside.png"/>')
            write_kra("hidden.kra", '<layer nodetype="grouplayer" visible="0"><layers>'
                      '<layer nodetype="filelayer" name="Caption" source="//host/remote.png"/></layers></layer>')
            write_kra("alpha.kra", '<layer nodetype="paintlayer" channelflags="1110"/>')
            with ZipFile(ora / "stack.ora", "w") as archive:
                archive.writestr("stack.xml", '<image w="3" h="5"><stack><stack name="Panel"><layer '
                                 'name="Ink" visibility="hidden"/></stack><layer name="Paper"/></stack></image>')
            with ZipFile(ora / "other.ora", "w") as archive:
                archive.writestr("stack.xml", '<image w="3" h="5"><stack><text/></stack></image>')
            with ZipFile(ora / "large.ora", "w") as archive:
                archive.writestr("stack.xml", '<image w="3" h="5"><stack>' + ' ' * (1 << 20) + '</stack></image>')

            manifest, _ = build_manifest(base, base / "corpus/local.json")
            expect = {c["id"]: c for c in manifest["cases"]}
            group = expect["kra-corpus/plain.kra"]["expect"]["layers"][1]
            self.assertEqual((group["kind"], group["name"]), ("group", "Set"))
            self.assertEqual([(n["name"], n["visible"]) for n in group["children"]],
                             [("Bottom", True), ("Top", False)])
            linked = expect["kra-corpus/linked.kra"]
            self.assertEqual(linked["assets"], ["../kra-corpus/images/sky.png"])
            self.assertNotIn("reference", linked)
            self.assertEqual(linked["expect"], {"width": 3, "height": 4})
            self.assertIn("Lost", linked["referenceNote"])
            hidden = expect["kra-corpus/hidden.kra"]
            self.assertEqual(hidden["reference"], "@mergedimage")
            self.assertNotIn("assets", hidden)
            self.assertIn("hidden", hidden["referenceNote"])
            self.assertIn("Inherit Alpha", expect["kra-corpus/alpha.kra"]["referenceNote"])
            self.assertEqual(expect["ora/stack.ora"]["expect"], {
                "width": 3, "height": 5, "layers": [
                    {"name": "Paper", "kind": "layer", "visible": True},
                    {"name": "Panel", "kind": "group", "visible": True, "children": [
                        {"name": "Ink", "kind": "layer", "visible": False}]}]})
            for name in ("other", "large"):
                self.assertEqual(expect[f"ora/{name}.ora"]["expect"], {"width": 3, "height": 5})
                self.assertIn("Canvas dimensions only", expect[f"ora/{name}.ora"]["referenceNote"])

    def test_native_trees_masks_refusals_and_absent_collections(self):
        scratch = ROOT / "build/test-output"
        scratch.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="testy-local-corpus-", dir=scratch) as tmp:
            base = Path(tmp)
            xcf = base / "xcf-corpus"
            renders = xcf / "gimp-renders"
            trees = xcf / "gimp-trees"
            renders.mkdir(parents=True)
            trees.mkdir()
            for name in ("拒否.xcf", "layers.xcf.gz", "unknown.xcf", "erase.xcf"):
                (xcf / name).touch()
            (renders / "failures.txt").write_text(
                "/previous/checkout/xcf-corpus/拒否.xcf: load returned nothing\n"
                "/previous/checkout/xcf-corpus/unknown.xcf: export failed\n", encoding="utf-8")
            (renders / "layers.xcf.gz.png").touch()
            header = "image 40 30 0 150\n"
            (trees / "layers.xcf.gz.tree.txt").write_text(
                header + "0\ttext\t0\t1\t100\t0\t0\t40\t30\t0\tTitle\n"
                "0\tgroup\t61\t1\t100\t0\t0\t40\t30\t0\tGroup\n"
                "1\tlayer\t0\t0\t100\t0\t0\t40\t30\t1\tUpper\n"
                "1\tlayer\t0\t1\t100\t0\t0\t40\t30\t0\tLower\n",
                encoding="utf-8")
            (trees / "erase.xcf.tree.txt").write_text(
                header + "0\tlayer\t58\t1\t100\t0\t0\t40\t30\t0\tErase\n", encoding="utf-8")
            kra = base / "kra-corpus"
            kra.mkdir()
            (kra / "paint.krz").touch()
            out = base / "corpus/local.json"
            manifest, messages = build_manifest(base, out)
            items = {case["id"]: case for case in manifest["cases"]}
            self.assertEqual(len(items), 4)
            self.assertTrue(any("GIMP refused" in line for line in messages))
            self.assertTrue(any("ora-corpus is not installed" in line for line in messages))
            self.assertNotIn("reference", items["xcf-corpus/unknown.xcf"])
            self.assertEqual(items["kra-corpus/paint.krz"]["reference"], "@mergedimage")
            self.assertEqual(items["xcf-corpus/erase.xcf"]["expect"], {"width": 40, "height": 30})
            self.assertIn("Erase", items["xcf-corpus/erase.xcf"]["referenceNote"])
            layers = items["xcf-corpus/layers.xcf.gz"]["expect"]["layers"]
            self.assertEqual([node["name"] for node in layers], ["Group", "Title"])
            self.assertEqual(layers[1]["kind"], "text")
            self.assertEqual(layers[0]["children"], [
                {"name": "Lower", "visible": True},
                {"name": "Upper", "visible": False, "hasMask": True}])
            self.assertEqual(items["xcf-corpus/layers.xcf.gz"]["reference"],
                             "../xcf-corpus/gimp-renders/layers.xcf.gz.png")


if __name__ == "__main__":
    unittest.main()
