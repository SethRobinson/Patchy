"""Native import reporting on disk, without a running editor or dashboard."""

from html.parser import HTMLParser
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "testy"))
from native_import_report import write_report


class ReportMarkup(HTMLParser):
    def __init__(self, page):
        super().__init__()
        self.links = []
        self.tags = []
        self.content = []
        self.feed(page)

    def handle_starttag(self, tag, attrs):
        self.tags.append(tag)
        if tag == "a":
            self.links.append(dict(attrs)["href"])

    def handle_data(self, data):
        self.content.append(data)


def case(name, fmt, **updates):
    result = {"id": name, "source": name, "format": fmt, "folder": "cells/0001",
              "opened": True, "exported": True, "notes": [], "render": None,
              "appearance": {"status": "pass"}, "structure": {"status": "pass"},
              "timing": {"status": "unscored", "reason": "No frame timings supplied"}}
    result.update(updates)
    return result


class TestNativeReport(unittest.TestCase):
    def setUp(self):
        base = ROOT / "build/test-output"
        base.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="testy-native-report-", dir=base)
        self.addCleanup(self.temp.cleanup)
        self.out = Path(self.temp.name)
        (self.out / "cells/0001").mkdir(parents=True)

    def generate(self, cases):
        write_report(self.out, {"name": "Pilot", "cases": cases, "patchyVersion": "1.39"})
        return (self.out / "report.html").read_text(encoding="utf-8")

    def test_denominators_failures_and_unavailable_reference(self):
        cases = [
            case("mask.xcf", "xcf", structure={"status": "fail", "mismatches": ["Missing layer mask"]}),
            case("missing.xcf", "xcf", appearance={"status": "unscored", "reason": "No GIMP reference"}),
            case("bad.kra", "kra", opened=False, exported=False, error="Open failed at layer 2",
                 appearance={"status": "unscored"}, structure={"status": "unscored"}),
        ]
        page = self.generate(cases)
        self.assertIn("<td>2 / 2</td><td>2 / 2</td>", page)
        self.assertIn("<td>0 / 1</td><td>0 / 0</td>", page)
        self.assertIn("<td>1 / 1<small>0 failed &middot; 1 unscored", page)
        self.assertIn("<td>1 / 2<small>1 failed &middot; 0 unscored", page)
        self.assertIn("Missing layer mask", page)
        self.assertIn("No GIMP reference", page)
        self.assertIn("Open failed at layer 2", page)

    def test_evidence_is_text_and_render_links_stay_inside_their_cell(self):
        injection = '<script>alert("wrong")</script>'
        good = self.out / "cells/0001/résult #1.png"
        good.write_bytes(b"")
        (self.out / "outside.png").write_bytes(b"")
        cases = [case(injection, "kra", notes=[injection], render="cells/0001/résult #1.png",
                      observed={"layers": [{"name": injection}]})]
        for path in ("cells/0001/../../outside.png", "cells/0001/absent.png", "outside.png",
                     "https://example.org/file.png", r"\\server\share\remote.png", "C:/remote.png"):
            cases.append(case("other", "kra", render=path))
        parsed = ReportMarkup(self.generate(cases))
        self.assertNotIn("script", parsed.tags)
        self.assertIn(injection, parsed.content)
        self.assertEqual(parsed.links, ["#format-0", "cells/0001/r%C3%A9sult%20%231.png"])

    def test_interrupted_run_and_missing_source_are_identified(self):
        item = case("missing", "ora", opened=None, exported=False,
                    appearance={"status": "unscored"}, structure={"status": "unscored"},
                    referenceNote="Reference comes from the source editor")
        write_report(self.out, {"name": "Partial run", "cases": [item], "total": 3,
                                "state": "interrupted"})
        page = (self.out / "report.html").read_text(encoding="utf-8")
        self.assertIn("Run interrupted", page)
        self.assertIn("1 / 3 files processed", page)
        self.assertIn("0 / 0<small>1 not measured", page)
        self.assertIn("Open not measured", page)
        self.assertIn("Reference comes from the source editor", page)

    def test_optional_reopen_check_has_its_own_denominator_and_credit(self):
        item = case("modes", "ora", appearance={"status": "fail"},
                    reopenAppearance={"status": "pass", "credit": "Krita <editor>"})
        page = self.generate([item, case("other", "ora")])
        self.assertIn("Appearance after reopen · Pass", page)
        self.assertIn("Krita &lt;editor&gt;", page)
        self.assertIn("1 / 1<small>0 failed &middot; 0 unscored</small>", page)
        self.assertIn("1 / 2<small>1 failed &middot; 0 unscored</small>", page)

    def test_comparison_frames_notes_and_source_credits(self):
        for name in ("reference.png", "actual.png"):
            (self.out / "cells/0001" / name).write_bytes(b"")
        item = case("flower", "kra", notes=["Animation shown at frame 1"],
                    document={"status": "pass"}, appearance={"status": "fail", "comparisonImages": True})
        write_report(self.out, {"name": "Snapshots", "cases": [item], "sources": [
            {"credit": "Artists <group>", "license": "CC BY", "url": "https://example.org/data"},
            {"credit": "Contributor", "url": "javascript:alert(0)"},
        ]})
        markup = ReportMarkup((self.out / "report.html").read_text(encoding="utf-8"))
        self.assertIn("Files with import notes", markup.content)
        self.assertIn("Document properties · Pass", markup.content)
        self.assertTrue(any("Artists <group>" in text for text in markup.content))
        self.assertEqual(markup.links, ["#format-0", "cells/0001/reference.png",
                                        "cells/0001/actual.png", "https://example.org/data"])


if __name__ == "__main__":
    unittest.main()
