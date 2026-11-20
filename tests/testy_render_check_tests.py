"""Testy's check against Photoshop drawing a file wrong (testy/render_check.py).

Photoshop 28.0 sometimes renders psd-tools' layer_mask_data.psd with part of a layer
mask ignored, on the original and on reopened resaves alike. A render something is
scored against is drawn again when it disagrees, and the picture two opens agree on is
kept. No Photoshop or editor is launched: the probes are fakes that write pictures."""
from pathlib import Path
import json
import shutil
import subprocess
import sys
import tempfile
import types
import unittest
from unittest import mock

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'testy'))
import analyze
import config
import render_check
import report
import testy
from drivers.photoshop import PhotoshopDriver

SIZE = (64, 48)


def picture(path, kind='good', size=SIZE):
    """'good': a blue band over grey; 'glitch': the same with the lower half green, the
    way Photoshop 28.0 drops part of a mask; 'other': the band moved (a real change)."""
    rgb = np.full((size[1], size[0], 3), 200, dtype=np.uint8)
    if kind == 'other':
        rgb[20:30, :, :] = (40, 40, 220)
    else:
        rgb[10:20, :, :] = (40, 40, 220)
    if kind == 'glitch':
        rgb[size[1] // 2:, :, :] = (0, 255, 64)
    Image.fromarray(rgb).save(path)
    return path


def same_picture(first, second):
    return np.array_equal(np.asarray(Image.open(first).convert('RGB')),
                          np.asarray(Image.open(second).convert('RGB')))


class Scratch(unittest.TestCase):
    def setUp(self):
        scratch = ROOT / 'build/test-output'
        scratch.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix='testy-rendercheck-', dir=scratch)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)


class RendersAgree(Scratch):
    def test_photoshop_renders_of_one_file_agree_within_the_byte_tolerance(self):
        a = picture(self.root / 'a.png')
        self.assertTrue(analyze.renders_agree(a, picture(self.root / 'b.png')))
        rgb = np.asarray(Image.open(a).convert('RGB')).copy()
        rgb[5, 5, 1] += analyze.PIXEL_TOLERANCE
        Image.fromarray(rgb).save(self.root / 'near.png')
        self.assertTrue(analyze.renders_agree(a, self.root / 'near.png'))
        rgb[5, 5, 1] += 1
        Image.fromarray(rgb).save(self.root / 'one_off.png')
        self.assertFalse(analyze.renders_agree(a, self.root / 'one_off.png'))

    def test_a_glitch_another_size_or_a_real_change_disagrees(self):
        a = picture(self.root / 'a.png')
        self.assertFalse(analyze.renders_agree(a, picture(self.root / 'glitch.png', 'glitch')))
        self.assertFalse(analyze.renders_agree(a, picture(self.root / 'other.png', 'other')))
        self.assertFalse(analyze.renders_agree(a, picture(self.root / 'big.png', size=(65, 48))))

    def test_transparency_is_compared_over_white(self):
        Image.new('RGBA', SIZE, (0, 0, 0, 0)).save(self.root / 'clear.png')
        Image.new('RGB', SIZE, 'white').save(self.root / 'white.png')
        self.assertTrue(analyze.renders_agree(self.root / 'clear.png', self.root / 'white.png'))


class Settle(unittest.TestCase):
    def test_only_alerts_seen_during_open_affect_source_eligibility(self):
        guard = types.SimpleNamespace(dismissed=["File info ignored", "PNG export notice"],
                                      first_seen=[("File info ignored", 120), ("PNG export notice", 240)])
        result = PhotoshopDriver._with_dialogs(guard, {"ok": True, "openStarted": 100, "openFinished": 200})
        self.assertEqual(result["dialogs"], ["File info ignored"])
        self.assertEqual(result["operationDialogs"], ["PNG export notice"])
        self.assertTrue(render_check.source_exclusion(result))
        guard.dismissed = ["PNG export notice"]
        clean = PhotoshopDriver._with_dialogs(guard, {"ok": True, "openStarted": 100, "openFinished": 200})
        self.assertIsNone(render_check.source_exclusion(clean))

    """The decision alone: renders are names, and two agree when they name one picture."""

    def run_settle(self, first, later, reference=None):
        pictures = {'first': first, 'reference': reference}
        asked = []

        def render_again(number):
            asked.append(number)
            if not later:
                return None
            pictures[f'again{number}'] = later.pop(0)
            return f'again{number}'

        verdict = render_check.settle('first', render_again,
                                      lambda a, b: pictures[a] == pictures[b],
                                      None if reference is None else 'reference')
        return verdict, asked

    def test_a_render_that_matches_the_reference_costs_nothing_more(self):
        verdict, asked = self.run_settle('good', ['good'], reference='good')
        self.assertEqual((verdict['outcome'], verdict['renders'], verdict['kept'], asked),
                         ('agrees', 1, 'first', []))
        self.assertIsNone(render_check.note(verdict, 'this save'))

    def test_the_reference_is_confirmed_by_a_second_open(self):
        verdict, asked = self.run_settle('good', ['good'])
        self.assertEqual((verdict['outcome'], verdict['renders'], verdict['kept'], asked),
                         ('repeatable', 2, 'first', [2]))
        self.assertIsNone(render_check.note(verdict, 'this file'))

    def test_a_glitched_reference_is_replaced_by_the_picture_two_opens_agree_on(self):
        verdict, asked = self.run_settle('glitch', ['good', 'good'])
        self.assertEqual((verdict['outcome'], verdict['basis'], verdict['renders'], verdict['kept'], asked),
                         ('replaced', 'majority', 3, 'again3', [2, 3]))
        self.assertIn('one of 3 opens', render_check.note(verdict, 'this file'))

    def test_a_glitched_confirmation_leaves_the_first_reference(self):
        verdict, _ = self.run_settle('good', ['glitch', 'good'])
        self.assertEqual((verdict['outcome'], verdict['renders'], verdict['kept']), ('repeatable', 3, 'first'))
        self.assertIn('one of 3 opens', render_check.note(verdict, 'this file'))

    def test_a_save_that_really_differs_is_drawn_the_same_again_and_kept(self):
        verdict, asked = self.run_settle('other', ['other'], reference='good')
        self.assertEqual((verdict['outcome'], verdict['renders'], verdict['kept'], asked),
                         ('repeatable', 2, 'first', [2]))
        self.assertIsNone(render_check.note(verdict, 'this save'))

    def test_a_glitched_save_render_gives_way_to_an_open_that_matches_the_reference(self):
        verdict, asked = self.run_settle('glitch', ['good', 'good'], reference='good')
        self.assertEqual((verdict['outcome'], verdict['basis'], verdict['renders'], verdict['kept'], asked),
                         ('replaced', 'reference', 2, 'again2', [2]))
        self.assertIn('1 of 2 opens', render_check.note(verdict, 'this save'))

    def test_three_different_pictures_keep_the_first_and_say_so(self):
        verdict, _ = self.run_settle('glitch', ['other', 'good'])
        self.assertEqual((verdict['outcome'], verdict['renders'], verdict['kept']), ('unstable', 3, 'first'))
        self.assertIn('each of 3 opens', render_check.note(verdict, 'this file'))

    def test_a_render_photoshop_cannot_make_again_keeps_the_first(self):
        verdict, asked = self.run_settle('glitch', [], reference='good')
        self.assertEqual((verdict['outcome'], verdict['renders'], verdict['kept'], asked),
                         ('unconfirmed', 1, 'first', [2]))
        self.assertIn('could not draw', render_check.note(verdict, 'this save'))


class FakePhotoshop:
    """Each probe of a file writes the next picture queued for it (the last one repeats);
    a probe that asks for a resave gets one, and a 16-bit companion is written when
    asked to."""

    def __init__(self, pictures, deep=False):
        self.pictures = {name: list(kinds) for name, kinds in pictures.items()}
        self.deep = deep
        self.probes = []

    def probe(self, path, render_png=None, resave_psd=None):
        name = Path(path).name
        self.probes.append((name, Path(render_png).name if render_png else None))
        queue = self.pictures[name]
        kind = queue.pop(0) if len(queue) > 1 else queue[0]
        if kind is None:
            return {'ok': False, 'error': 'com-error'}
        picture(render_png, kind)
        if self.deep:
            picture(render_check.deep_companion(Path(render_png)), kind)
        if resave_psd is not None:
            Path(resave_psd).write_bytes(b'8BPS resave')
        return {'ok': True, 'layers': [], 'width': SIZE[0], 'height': SIZE[1], 'render': 'ok'}

    def version(self):
        return '28.0.0'

    def font_cache_key(self):
        return 'fonts'


class SettleRender(Scratch):
    def test_a_warning_only_on_the_confirming_open_is_recorded(self):
        def probe(source, output):
            picture(output)
            return {"ok": True, "dialogs": ["File info ignored"]}
        first = picture(self.root / "render.png")
        result = render_check.settle_render(probe, self.root / "file.psd", first, None,
                                            analyze.renders_agree, "this file")
        self.assertEqual(result["dialogs"], ["File info ignored"])
        self.assertEqual(result["outcome"], "repeatable")

    def test_the_kept_render_and_its_16_bit_companion_end_up_in_place(self):
        source = self.root / 'resave.psd'
        source.write_bytes(b'8BPS')
        reference = picture(self.root / 'reference.png')
        render = picture(self.root / 'roundtrip.png', 'glitch')
        picture(render_check.deep_companion(render), 'glitch')
        photoshop = FakePhotoshop({'resave.psd': ['good']}, deep=True)
        check = render_check.settle_render(photoshop.probe, source, render, reference,
                                           analyze.renders_agree, 'this save')
        self.assertEqual(check['outcome'], 'replaced')
        self.assertIn('note', check)
        self.assertTrue(same_picture(render, reference))
        self.assertTrue(same_picture(render_check.deep_companion(render), reference))
        self.assertEqual(sorted(p.name for p in self.root.iterdir()),
                         ['reference.png', 'resave.psd', 'roundtrip.png', 'roundtrip16.png'])

    def test_a_matching_render_is_left_alone(self):
        source = self.root / 'resave.psd'
        source.write_bytes(b'8BPS')
        reference = picture(self.root / 'reference.png')
        render = picture(self.root / 'roundtrip.png')
        photoshop = FakePhotoshop({'resave.psd': ['glitch']})
        self.assertIsNone(render_check.settle_render(photoshop.probe, source, render, reference,
                                                     analyze.renders_agree, 'this save'))
        self.assertEqual(photoshop.probes, [])

    def test_scratch_names_cover_every_open(self):
        names = render_check.scratch_names('render')
        self.assertEqual(names, ('again2-render.png', 'again2-render16.png',
                                 'again3-render.png', 'again3-render16.png'))
        for name in names:
            self.assertIn(name, testy.Runner.SCRUB_TRUTH)
            self.assertIn(name, testy.Runner.SCRUB_CELL)
        self.assertIn('again3-roundtrip16.png', testy.Runner.SCRUB_CELL)
        self.assertIn('again2-textsave.png', testy.Runner.SCRUB_CELL)


class RunnerChecks(Scratch):
    def setUp(self):
        super().setUp()
        cache = self.root / 'cache'
        cache.mkdir()
        patcher = mock.patch.object(config, 'CACHE_DIR', cache)
        patcher.start()
        self.addCleanup(patcher.stop)
        for name in ('log',):
            patcher = mock.patch.object(testy, name)
            patcher.start()
            self.addCleanup(patcher.stop)
        self.original = self.root / 'original.psd'
        self.original.write_bytes(b'8BPS original')
        self.staged = types.SimpleNamespace(original=self.original, sha1='a' * 40, trap=None)

    def runner(self, photoshop):
        runner = object.__new__(testy.Runner)
        runner.args = types.SimpleNamespace(fresh=False)
        runner.suffix = 'test'
        runner.run_dir = self.root / 'run'
        runner.files_dir = runner.run_dir / 'files'
        runner.ps = photoshop
        runner.push = lambda: None
        runner.patchy_hash = 'test'
        runner.scan_threshold = None
        runner._deferred_cell_caches = {}
        runner._text_fonts = []
        runner.editors = {'photoshop': config.EditorInfo('photoshop', 'Photoshop', None, '28.0.0', True)}
        runner.status = {'editors': {'photoshop': {'version': '28.0.0'}},
                         'files': [{'name': 'original.psd', 'traits': {}, 'cells': {'photoshop': {}}}]}
        return runner

    def truth_dir(self, runner):
        return runner.files_dir / 'original' / '_truth'

    def cache_dir(self, runner):
        return runner._ground_truth_cache_dir(runner.file_entry(0), self.staged)

    def test_a_glitched_reference_is_drawn_again_before_anything_is_scored(self):
        photoshop = FakePhotoshop({'original.psd': ['glitch', 'good', 'good']})
        runner = self.runner(photoshop)
        truth = runner.ground_truth(0, self.staged)
        self.assertEqual(len(photoshop.probes), 3)
        self.assertTrue(same_picture(self.truth_dir(runner) / 'render.png', picture(self.root / 'good.png')))
        entry = runner.file_entry(0)
        self.assertEqual(entry['groundTruth']['state'], 'done')
        self.assertIn('one of 3 opens', entry['groundTruth']['renderCheck']['note'])
        self.assertEqual(truth['renderCheck']['outcome'], 'replaced')
        cached = json.loads((self.cache_dir(runner) / 'result.json').read_text(encoding='utf-8'))
        self.assertEqual(cached['renderCheck']['outcome'], 'replaced')
        self.assertTrue(same_picture(self.cache_dir(runner) / 'render.png', self.root / 'good.png'))
        self.assertEqual(sorted(p.name for p in self.truth_dir(runner).iterdir()),
                         ['manifest.json', 'render.png', 'render_thumb.png'])
        self.assertEqual(runner._reference_replaced, frozenset())  # nothing was scored against the glitch

    def test_a_steady_reference_costs_one_more_open_and_says_nothing(self):
        photoshop = FakePhotoshop({'original.psd': ['good']})
        runner = self.runner(photoshop)
        runner.ground_truth(0, self.staged)
        self.assertEqual(len(photoshop.probes), 2)
        self.assertNotIn('renderCheck', runner.file_entry(0)['groundTruth'])
        cached = json.loads((self.cache_dir(runner) / 'result.json').read_text(encoding='utf-8'))
        self.assertEqual(cached['renderCheck'], {'renders': 2, 'outcome': 'repeatable'})
        # The next run reuses the confirmed reference without opening the file.
        photoshop.probes.clear()
        runner = self.runner(photoshop)
        runner.ground_truth(0, self.staged)
        self.assertEqual(photoshop.probes, [])

    def test_a_reference_cached_before_the_check_is_confirmed_once(self):
        photoshop = FakePhotoshop({'original.psd': ['good', 'good']})
        runner = self.runner(photoshop)
        cache_dir = self.cache_dir(runner)
        cache_dir.mkdir(parents=True)
        (cache_dir / 'result.json').write_text(json.dumps(
            {'ok': True, 'layers': [], 'width': SIZE[0], 'height': SIZE[1], 'render': 'ok'}), encoding='utf-8')
        picture(cache_dir / 'render.png', 'glitch')
        runner.ground_truth(0, self.staged)
        self.assertEqual(len(photoshop.probes), 2)
        self.assertTrue(same_picture(self.truth_dir(runner) / 'render.png', picture(self.root / 'good.png')))
        self.assertTrue(same_picture(cache_dir / 'render.png', self.root / 'good.png'))
        self.assertEqual(json.loads((cache_dir / 'result.json').read_text(encoding='utf-8'))['renderCheck']['outcome'],
                         'replaced')
        self.assertEqual(runner._reference_replaced, frozenset({0}))
        self.assertEqual(testy.Runner._reference_replaced, frozenset())

    def test_a_reference_photoshop_cannot_draw_again_is_checked_next_time(self):
        photoshop = FakePhotoshop({'original.psd': ['good', None]})
        runner = self.runner(photoshop)
        runner.ground_truth(0, self.staged)
        self.assertIn('could not draw', runner.file_entry(0)['groundTruth']['renderCheck']['note'])
        cached = json.loads((self.cache_dir(runner) / 'result.json').read_text(encoding='utf-8'))
        self.assertNotIn('renderCheck', cached)

    def photoshop_cell(self, photoshop, reference_replaced=frozenset()):
        runner = self.runner(photoshop)
        runner._reference_replaced = reference_replaced
        truth = runner.ground_truth(0, self.staged)
        photoshop.probes.clear()
        runner.run_cell(0, 'photoshop', self.staged, truth)
        return runner, runner.file_entry(0)['cells']['photoshop']

    def test_the_photoshop_column_and_round_trip_renders_are_checked_against_the_reference(self):
        photoshop = FakePhotoshop({'original.psd': ['good', 'good', 'glitch', 'good'],
                                   'resave.psd': ['glitch', 'good']})
        runner, cell = self.photoshop_cell(photoshop)
        self.assertEqual(photoshop.probes, [('original.psd', 'render.png'), ('original.psd', 'again2-render.png'),
                                            ('resave.psd', 'roundtrip.png'), ('resave.psd', 'again2-roundtrip.png')])
        self.assertEqual(cell['state'], 'done')
        self.assertEqual(cell['renderMetrics']['badFraction'], 0.0)
        self.assertEqual(cell['roundtripRender']['badFraction'], 0.0)
        self.assertIn('1 of 2 opens', cell['renderCheck']['note'])
        self.assertIn('1 of 2 opens', cell['roundtripCheck']['note'])
        cell_dir = runner.files_dir / 'original' / 'photoshop'
        self.assertFalse([p.name for p in cell_dir.iterdir() if p.name.startswith('again')])

    def test_cells_cached_against_a_replaced_reference_are_measured_again(self):
        photoshop = FakePhotoshop({'original.psd': ['good'], 'resave.psd': ['good']})
        runner, cell = self.photoshop_cell(photoshop)
        self.assertNotIn('cached', cell)
        runner, cell = self.photoshop_cell(photoshop)
        self.assertTrue(cell['cached'])
        self.assertEqual(photoshop.probes, [])
        runner, cell = self.photoshop_cell(photoshop, reference_replaced=frozenset({0}))
        self.assertNotIn('cached', cell)
        self.assertEqual(photoshop.probes, [('original.psd', 'render.png'), ('resave.psd', 'roundtrip.png')])


class ReportNotes(unittest.TestCase):
    @unittest.skipUnless(shutil.which('node'), 'node is not installed')
    def test_the_detail_panel_shows_each_render_that_took_more_than_one_open(self):
        script = report._PAGE.split('<script>', 1)[1].split('</script>', 1)[0]
        helper = 'function renderCheckNotes' + script.split('function renderCheckNotes', 1)[1].split(
            'function openDetail', 1)[0]
        test_js = """
const assert = require('assert');
function esc(s) { return String(s == null ? '' : s).replace(/</g, '&lt;'); }
""" + helper + """
assert.equal(renderCheckNotes({}, {}), '');
assert.equal(renderCheckNotes({renderCheck: {renders: 2, outcome: 'repeatable'}}, {}), '');
const html = renderCheckNotes({renderCheck: {note: 'Photoshop drew this file differently on one of 3 opens'}},
  {renderCheck: {note: 'column <note>'}, roundtripCheck: {note: 'resave note'},
   textSave: {renderCheck: {note: 'text save note'}}});
assert(html.includes('Reference: Photoshop drew this file differently on one of 3 opens'));
assert(html.includes('column &lt;note>'));
assert(html.includes('resave note') && html.includes('text save note'));
assert.equal((html.match(/<div class="nums">/g) || []).length, 4);
"""
        result = subprocess.run(['node', '-e', test_js], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
