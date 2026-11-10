"""Testy's text-save leg: the .psd an editor saves after its own text update, reopened
in Photoshop. No editor and no Photoshop is launched: the driver output, Photoshop's
manifests and its renders are fakes."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import types
import unittest
from unittest import mock

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'testy'))
import manifest
import report
import testy
from drivers import patchy as patchy_driver

SIZE = (200, 120)
TITLE_BOX = [20, 20, 120, 50]
BODY_BOX = [20, 70, 180, 100]


def text_layer(name, box, **settings):
    layer = dict(path=name, name=name, kind='TEXT', group=False, visible=True, bounds=list(box),
                 text='Hello ' + name, fonts=['ArialMT'], orientation='horizontal',
                 antiAlias='antiAliasSharp', textKind='box',
                 paragraphs=[dict({'from': 0, 'to': 12, 'align': 'justifyAll', 'hyphenate': None,
                                   'direction': None, 'everyLineComposer': True})])
    layer.update(settings)
    return layer


def original_layers():
    return [text_layer('Title', TITLE_BOX), text_layer('Body', BODY_BOX),
            dict(path='2', name='Photo', kind='NORMAL', group=False, visible=True, bounds=[0, 0, 200, 120])]


def picture(path, marks=(), shift=0):
    """A white canvas with a black bar in each text box, `shift` pixels to the right and
    down; `marks` are extra black boxes."""
    image = Image.new('RGB', SIZE, 'white')
    draw = ImageDraw.Draw(image)
    for box in (TITLE_BOX, BODY_BOX):
        draw.rectangle([box[0] + 4 + shift, box[1] + 10 + shift, box[2] - 4 + shift, box[1] + 14 + shift],
                       fill='black')
    for box in marks:
        draw.rectangle(box, fill='black')
    image.save(path)
    return path


class ScoreTextSave(unittest.TestCase):
    def setUp(self):
        scratch = ROOT / 'build/test-output'
        scratch.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix='testy-textsave-', dir=scratch)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.reference = picture(self.root / 'reference.png')

    def score(self, saved_layers, save_marks=(), opened_marks=None, unreached=None, shift=0, **probe):
        save = picture(self.root / 'save.png', save_marks, shift)
        opened = None if opened_marks is None else picture(self.root / 'opened.png', opened_marks)
        return testy.score_text_save(original_layers(), dict(layers=saved_layers, **probe), SIZE,
                                     self.reference, save, opened, unreached or {})

    def test_text_that_comes_back_the_same_is_kept(self):
        leg = self.score(original_layers())
        self.assertEqual(leg['state'], 'done')
        self.assertEqual((leg['measured'], leg['kept'], leg['same']), (2, 2, 2))
        self.assertEqual(leg['score'], 1.0)
        self.assertEqual((leg['lost'], leg['changed'], leg['notMeasured']), ([], [], []))
        self.assertEqual(leg['render']['unmoved'], 1.0)

    def test_layout_a_pixel_off_is_still_the_same(self):
        leg = self.score(original_layers(), shift=1)
        self.assertEqual((leg['same'], leg['changed']), (2, []))
        self.assertGreater(leg['layers'][0]['badFraction'], 0.0)  # pixels did change
        self.assertEqual(leg['layers'][0]['movedFraction'], 0.0)   # nothing moved out of reach
        self.assertEqual(leg['layers'][0]['render'], 'same')

    def test_layout_that_moved_is_changed(self):
        leg = self.score(original_layers(), shift=6)
        self.assertEqual(leg['changed'], ['Title', 'Body'])

    def test_a_layer_that_is_no_longer_text_is_lost(self):
        saved = original_layers()
        saved[0] = dict(saved[0], kind='NORMAL')
        leg = self.score(saved)
        self.assertEqual(leg['lost'], ['Title'])
        self.assertEqual((leg['measured'], leg['kept'], leg['same']), (2, 1, 1))
        self.assertEqual(leg['score'], 0.5)
        self.assertEqual(leg['layers'][0]['verdict'], 'lost')
        self.assertEqual(leg['layers'][0]['became'], 'NORMAL')

    def test_a_layer_photoshop_draws_differently_is_changed(self):
        leg = self.score(original_layers(), save_marks=[[30, 76, 170, 96]])
        self.assertEqual(leg['changed'], ['Body'])
        self.assertEqual(leg['same'], 1)
        body = leg['layers'][1]
        self.assertEqual((body['verdict'], body['render']), ('changed', 'changed'))
        self.assertGreater(body['movedFraction'], testy.TEXT_SAVE_BOX_LIMIT)
        self.assertEqual(leg['layers'][0]['render'], 'same')
        self.assertLess(leg['render']['unmoved'], 1.0)

    def test_a_changed_setting_counts_even_when_the_render_does_not_move(self):
        saved = original_layers()
        saved[1] = dict(saved[1], paragraphs=[{'from': 0, 'to': 12, 'align': 'justifyLeft',
                                               'hyphenate': True, 'direction': 'dirLeftToRight',
                                               'everyLineComposer': False}])
        leg = self.score(saved)
        self.assertEqual(leg['changed'], ['Body'])
        body = leg['layers'][1]
        self.assertEqual(body['render'], 'same')
        self.assertEqual(body['settingsChanged'],
                         [{'setting': 'justification', 'was': 'justifyAll', 'now': 'justifyLeft'},
                          {'setting': 'line composer', 'was': True, 'now': False}])
        # Photoshop could not give the original's Hyphenate and direction (None): they
        # are listed and not counted.
        self.assertEqual([row['setting'] for row in body['settingsUnstated']],
                         ['Hyphenate', 'paragraph direction'])

    def test_a_resolved_hyphenate_that_turned_on_is_a_change(self):
        # The #683 class: the original's paragraph says Hyphenate off, the save says on.
        before = original_layers()
        before[1]['paragraphs'][0]['hyphenate'] = False
        saved = original_layers()
        saved[1]['paragraphs'][0]['hyphenate'] = True
        save = picture(self.root / 'save.png')
        leg = testy.score_text_save(before, dict(layers=saved), SIZE, self.reference, save, None, {})
        self.assertEqual(leg['changed'], ['Body'])
        self.assertEqual(leg['layers'][1]['settingsChanged'],
                         [{'setting': 'Hyphenate', 'was': False, 'now': True}])

    def test_a_setting_only_one_file_states_is_never_a_change(self):
        saved = original_layers()
        saved[0] = dict(saved[0], paragraphs=[{'from': 0, 'to': 12, 'align': 'justifyAll',
                                               'hyphenate': False, 'direction': None,
                                               'everyLineComposer': None}])
        leg = self.score(saved)
        self.assertEqual(leg['changed'], [])
        self.assertEqual(leg['same'], 2)

    def test_split_paragraph_ranges_compare_by_overlap(self):
        saved = original_layers()
        saved[0] = dict(saved[0], paragraphs=[
            {'from': 0, 'to': 6, 'align': 'justifyAll', 'hyphenate': None, 'direction': None,
             'everyLineComposer': True},
            {'from': 6, 'to': 12, 'align': 'justifyAll', 'hyphenate': None, 'direction': None,
             'everyLineComposer': True}])
        self.assertEqual(self.score(saved)['changed'], [])
        saved[0]['paragraphs'][1]['align'] = 'left'
        self.assertEqual(self.score(saved)['changed'], ['Title'])

    def test_an_unreached_layer_is_not_measured(self):
        saved = original_layers()
        saved[1] = dict(saved[1], kind='NORMAL')  # whatever became of it is not held against the editor
        leg = self.score(saved, save_marks=[[30, 76, 170, 96]],
                         unreached={'Body': "Patchy's script could not re-render this layer"})
        self.assertEqual(leg['notMeasured'], ['Body'])
        self.assertEqual((leg['measured'], leg['kept'], leg['same']), (1, 1, 1))
        self.assertEqual((leg['lost'], leg['changed']), ([], []))
        self.assertEqual(leg['score'], 1.0)
        self.assertEqual(leg['layers'][1]['verdict'], 'not measured')
        self.assertIn('could not re-render', leg['notMeasuredReason'])

    def test_nothing_reached_is_not_measured(self):
        leg = self.score(original_layers(), unreached={'Title': 'x', 'Body': 'x'})
        self.assertEqual(leg['state'], 'not measured')
        self.assertNotIn('score', leg)

    def test_a_box_the_as_opened_save_differs_in_too_is_not_counted(self):
        # Something under the text was lost by the save itself: both saves show it.
        marks = [[30, 76, 170, 96]]
        leg = self.score(original_layers(), save_marks=marks, opened_marks=marks)
        self.assertEqual(leg['changed'], [])
        self.assertEqual(leg['layers'][1]['render'], 'differs as opened too')
        # The same difference without it in the as-opened save is the text update's.
        leg = self.score(original_layers(), save_marks=marks, opened_marks=[])
        self.assertEqual(leg['changed'], ['Body'])

    def test_a_save_photoshop_lacks_a_font_for_is_not_compared_by_render(self):
        saved = original_layers()
        saved[1] = dict(saved[1], fonts=['NoSuchFace-Regular'])
        leg = self.score(saved, save_marks=[[30, 76, 170, 96]],
                         textFontsMissing='Required fonts unavailable: NoSuchFace-Regular')
        self.assertIn('NoSuchFace', leg['saveFontsMissing'])
        self.assertEqual(leg['layers'][0]['render'], 'not compared')
        self.assertEqual(leg['changed'], ['Body'])  # the font it names changed
        self.assertEqual(leg['layers'][1]['settingsChanged'][0]['setting'], 'fonts')

    def test_text_contents_ignore_line_end_style(self):
        before = dict(text='One\rTwo\r')
        self.assertEqual(manifest.text_setting_changes(before, dict(text='One\r\nTwo')), ([], []))
        changed, _unstated = manifest.text_setting_changes(before, dict(text='One\rThree'))
        self.assertEqual(changed[0]['setting'], 'text')


class FakePhotoshop:
    def __init__(self, answer, marks=()):
        self.answer, self.marks, self.probes = answer, marks, []

    def probe(self, path, render_png=None, resave_psd=None):
        self.probes.append(Path(path).name)
        if self.answer.get('ok') and render_png is not None:
            picture(render_png, self.marks)
        return dict(self.answer)


class TextSaveLeg(unittest.TestCase):
    """Runner._text_save_leg with a fake editor save and a fake Photoshop."""

    def setUp(self):
        scratch = ROOT / 'build/test-output'
        scratch.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix='testy-textsave-leg-', dir=scratch)
        self.addCleanup(self.temp.cleanup)
        self.run_dir = Path(self.temp.name)
        self.cell_dir = self.run_dir / 'files' / 'a' / 'patchy'
        self.cell_dir.mkdir(parents=True)
        truth_dir = self.run_dir / 'files' / 'a' / '_truth'
        truth_dir.mkdir()
        picture(truth_dir / 'render.png')
        self.entry = {'name': 'a.psd', 'docSize': list(SIZE), 'traits': {'text': True}}
        self.truth = {'layers': original_layers(), 'textFontsMissing': None}
        self.saves = []

    def runner(self, photoshop, saved=None):
        runner = object.__new__(testy.Runner)
        runner.run_dir = self.run_dir
        runner.files_dir = self.run_dir / 'files'
        runner.ps = photoshop
        runner.server_base = None
        runner._text_fonts = []
        runner.push = lambda: None
        answer = {'ok': True, 'error': '', 'failed': [], 'fontFallback': [], 'updated': 2,
                  'noTypeLayers': False}
        answer.update(saved or {})

        def save(editor_key, info, source, save_psd):
            self.saves.append(editor_key)
            if answer['ok']:
                save_psd.write_bytes(b'8BPS')
            return dict(answer)

        runner._save_text_afresh = save
        return runner

    def leg(self, photoshop, editor='patchy', saved=None, truth=None):
        cell = {'opens': 'ok', 'artifacts': {}}
        staged = types.SimpleNamespace(original=self.run_dir / 'original.psd')
        with mock.patch.object(testy, 'log'):
            self.runner(photoshop, saved)._text_save_leg(
                self.entry, editor, types.SimpleNamespace(exe=None), staged, cell, self.cell_dir,
                self.truth if truth is None else truth)
        return cell

    def test_a_clean_save_scores_every_layer(self):
        photoshop = FakePhotoshop({'ok': True, 'layers': original_layers()})
        cell = self.leg(photoshop)
        self.assertEqual(photoshop.probes, ['textsave.psd'])
        self.assertEqual(cell['textSave']['state'], 'done')
        self.assertEqual((cell['textSave']['same'], cell['textSave']['measured']), (2, 2))
        self.assertEqual(cell['artifacts']['textSaveRender'], 'files/a/patchy/textsave.png')
        self.assertTrue((self.cell_dir / 'textsave_manifest.json').exists())
        self.assertNotIn('native', cell)  # nothing is folded into the as-opened save's score

    def test_a_missing_font_means_not_measured_and_no_editor_run(self):
        photoshop = FakePhotoshop({'ok': True, 'layers': original_layers()})
        truth = dict(self.truth, textFontsMissing='Required fonts unavailable: Futura-Bold')
        cell = self.leg(photoshop, truth=truth)
        self.assertEqual(cell['textSave']['state'], 'not measured')
        self.assertIn('Futura-Bold', cell['textSave']['reason'])
        self.assertEqual((self.saves, photoshop.probes), ([], []))

    def test_editors_without_the_leg_are_left_alone(self):
        photoshop = FakePhotoshop({'ok': True, 'layers': original_layers()})
        for editor in ('krita', 'gimp', 'photodemon', 'affinity', 'psdtools', 'photoshop'):
            self.assertNotIn('textSave', self.leg(photoshop, editor=editor))
        self.assertEqual((self.saves, photoshop.probes), ([], []))
        self.assertEqual(sorted(testy.TEXT_SAVE_EDITORS), ['patchy', 'photocraft', 'photopea'])

    def test_a_file_without_text_layers_has_no_leg(self):
        photoshop = FakePhotoshop({'ok': True, 'layers': []})
        self.assertNotIn('textSave', self.leg(photoshop, truth={'layers': original_layers()[2:]}))

    def test_no_save_written_is_not_measured(self):
        photoshop = FakePhotoshop({'ok': True, 'layers': original_layers()})
        cell = self.leg(photoshop, saved={'ok': False, 'error': 'timeout after 180s'})
        self.assertEqual(cell['textSave']['state'], 'not measured')
        self.assertIn('timeout', cell['textSave']['reason'])
        self.assertEqual(photoshop.probes, [])

    def test_a_save_photoshop_refuses_is_rejected_with_its_dialogs(self):
        photoshop = FakePhotoshop({'ok': False, 'error': 'not a valid Photoshop document',
                                   'dialogs': ['Some text layers contain fonts that are missing.']})
        cell = self.leg(photoshop)
        self.assertEqual(cell['textSave']['state'], 'rejected')
        self.assertIn('not a valid', cell['textSave']['error'])
        self.assertEqual(cell['textSave']['dialogs'], ['Some text layers contain fonts that are missing.'])
        self.assertNotIn('resaveRejected', cell)  # the as-opened save's verdict is its own
        self.assertNotIn('uncacheable', cell)

    def test_a_dialog_the_original_raises_too_is_not_the_saves(self):
        old_text = 'Adobe Photoshop: "Some text layers might need to be updated"'
        photoshop = FakePhotoshop({'ok': True, 'layers': original_layers(),
                                   'dialogs': [old_text, 'Adobe Photoshop: "This document is damaged"']})
        cell = self.leg(photoshop, truth=dict(self.truth, dialogs=[old_text]))
        self.assertEqual(cell['textSave']['dialogs'], ['Adobe Photoshop: "This document is damaged"'])
        cell = self.leg(FakePhotoshop({'ok': True, 'layers': original_layers(), 'dialogs': [old_text]}),
                        truth=dict(self.truth, dialogs=[old_text]))
        self.assertNotIn('dialogs', cell['textSave'])
        cached = {'dialogs': [old_text], 'state': 'done'}
        self.assertTrue(testy.Runner._drop_original_dialogs(cached, {'dialogs': [old_text]}))
        self.assertNotIn('dialogs', cached)
        self.assertFalse(testy.Runner._drop_original_dialogs(cached, {'dialogs': [old_text]}))

    def test_a_photoshop_that_never_started_is_no_verdict(self):
        photoshop = FakePhotoshop({'ok': False, 'launchFailure': True, 'error': 'Server execution failed'})
        cell = self.leg(photoshop)
        self.assertEqual(cell['textSave']['state'], 'not measured')
        self.assertTrue(cell['uncacheable'])

    def test_unreached_and_font_fallback_layers_are_not_measured(self):
        photoshop = FakePhotoshop({'ok': True, 'layers': original_layers()}, marks=[[30, 76, 170, 96]])
        cell = self.leg(photoshop, saved={'fontFallback': ['Body']})
        self.assertEqual(cell['textSave']['notMeasured'], ['Body'])
        self.assertEqual(cell['textSave']['changed'], [])
        self.assertIn('Update All Text Layers', cell['textSave']['notMeasuredReason'])

    def test_the_summary_counts_layers_and_keeps_the_older_keys(self):
        done = {'state': 'done', 'opens': 'ok',
                'native': {'nativeScore': 1.0},
                'textSave': {'state': 'done', 'same': 3, 'measured': 4}}
        rejected = {'state': 'done', 'opens': 'ok', 'textSave': {'state': 'rejected', 'textLayers': 2}}
        unmeasured = {'state': 'done', 'opens': 'ok', 'textSave': {'state': 'not measured'}}
        status = {'run': {'name': 'r', 'editorOrder': ['patchy', 'krita']},
                  'files': [{'cells': {'patchy': done, 'krita': {'state': 'done', 'opens': 'ok'}}},
                            {'cells': {'patchy': rejected}}, {'cells': {'patchy': unmeasured}}]}
        editors = testy.Runner.summarize_status(status)['editors']
        self.assertEqual(editors['patchy']['textSave'], {'same': 3, 'measured': 6, 'files': 2, 'rejected': 1})
        self.assertNotIn('textSave', editors['krita'])
        self.assertEqual(editors['patchy']['native'], 1.0)
        self.assertEqual(editors['patchy']['badSaves'], 0)


class PatchyScriptOutput(unittest.TestCase):
    def test_font_warnings_are_tied_to_their_layer(self):
        scratch = ROOT / 'build/test-output'
        scratch.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='testy-textsave-patchy-', dir=scratch) as temp:
            out = Path(temp) / 'textsave.psd'
            report = out.with_name('textsave.script.txt')
            lines = ['{"testyLayer":"Title"}',
                     '{"testyLayer":"Body"}',
                     '[warn] layer.rerenderText: font not available, rendered with a fallback: Adobe Hebrew',
                     '{"testyTextAfresh":true,"exported":true,"done":["Title","Body"],"failed":[],'
                     '"smartDone":[],"smartFailed":[]}', '[done]']

            def fake_run(exe, arguments):
                self.assertIn('smart=0', arguments)
                self.assertIn(f'out={out}', arguments)
                report.write_text('\n'.join(lines), encoding='utf-8')
                out.write_bytes(b'8BPS')
                return {'exitCode': 0, 'stderr': ''}

            with mock.patch.object(patchy_driver, '_run', side_effect=fake_run):
                result = patchy_driver.save_text_afresh(Path('patchy.exe'), Path('in.psd'), out)
        self.assertTrue(result['ok'])
        self.assertEqual(result['done'], ['Title', 'Body'])
        self.assertEqual(result['fontFallback'], ['Body'])


class ReportWording(unittest.TestCase):
    @unittest.skipUnless(shutil.which('node'), 'node is not installed')
    def test_the_cell_and_the_detail_panel_name_what_changed(self):
        script = report._PAGE.split('<script>', 1)[1].split('</script>', 1)[0]
        helpers = script.split('const TEXT_SAVE_LABEL', 1)[1].split('function renderControls', 1)[0]
        test_js = """
const assert = require('assert');
function pct(x) { return (100 * x).toFixed(1) + '%'; }
function esc(s) { return String(s == null ? '' : s); }
const TEXT_SAVE_LABEL""" + helpers + """
const cell = leg => ({cells: {patchy: {textSave: leg}}});
assert.equal(textSaveNote({cells: {patchy: {}}}, 'patchy'), '');
assert(textSaveNote(cell({state: 'not measured', reason: 'fonts'}), 'patchy').includes('not measured'));
assert(textSaveNote(cell({state: 'rejected', error: 'x'}), 'patchy').includes('Photoshop cannot open it'));
const leg = {state: 'done', update: 'layer.rerenderText()', measured: 4, same: 1, kept: 3,
  lost: ['Logo'], changed: ['Body', 'Quote'], notMeasured: ['Locked'], notMeasuredReason: 'script could not',
  render: {accuracy: 0.72, unmoved: 0.96},
  layers: [
    {name: 'Title', verdict: 'same', render: 'same'},
    {name: 'Logo', verdict: 'lost', became: 'NORMAL'},
    {name: 'Body', verdict: 'changed', render: 'changed', movedFraction: 0.16,
     settingsChanged: [{setting: 'justification', was: 'justifyAll', now: 'justifyLeft'},
                       {setting: 'Hyphenate', was: false, now: true},
                       {setting: 'line composer', was: true, now: false}]},
    {name: 'Quote', verdict: 'changed', render: 'same',
     settingsChanged: [{setting: 'paragraph direction', was: 'dirRightToLeft', now: 'dirLeftToRight'}],
     settingsUnstated: [{setting: 'Hyphenate', was: null, now: true}]},
    {name: 'Locked', verdict: 'not measured', reason: 'script could not'}]};
const note = textSaveNote(cell(leg), 'patchy');
assert(note.includes('1 of 4 text layer(s) no longer text'));
assert(note.includes('2 of 4 text layer(s) changed in Photoshop'));
assert(note.includes('1 text layer(s) not measured'));
const detail = textSaveDetail({textSave: leg}, 'Patchy');
assert(detail.includes('1/4 text layers the same'));
assert(detail.includes('now NORMAL (rasterized)'));
assert(detail.includes('justification: justify all became justify, last line left'));
assert(detail.includes('Hyphenate: off became on'));
assert(detail.includes('line composer: every-line became single-line'));
assert(detail.includes('paragraph direction: right to left became left to right'));
assert(detail.includes('Photoshop draws it differently (16.0% of its box moved)'));
assert(detail.includes('Hyphenate: not known in the original, on in the save'));
assert(!detail.includes('<td>Title</td>'));  // unchanged layers are not listed
const clean = textSaveDetail({textSave: {state: 'done', measured: 2, same: 2, layers: []}}, 'Patchy');
assert(clean.includes('All 2 measured text layer(s) are still text'));
assert(textSaveDetail({textSave: {state: 'rejected', error: 'bad file'}}, 'Patchy').includes('Save rejected'));
assert.equal(textSaveDetail({}, 'Patchy'), '');
"""
        scratch = ROOT / 'build/test-output'
        scratch.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='testy-textsave-js-', dir=scratch) as temp:
            js_file = Path(temp) / 'report.js'
            js_file.write_text(test_js, encoding='utf-8')
            result = subprocess.run(['node', str(js_file)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main(verbosity=2)
