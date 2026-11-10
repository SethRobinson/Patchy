"""Testy's reader for the paragraph settings a type layer leaves to its sheets
(testy/text_sheets.py), and the rule that the Photoshop probe never sets text settings
to read or refresh them. No Photoshop is launched: the PSDs are built here, and the
Photoshop-written fixtures are read only when this machine has them."""
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'testy'))
import text_sheets
from drivers import photoshop



def fixtures_dir():
    """local-test-fixtures/psd of this checkout, or of the main checkout when this is a
    worktree (untracked fixtures live only there)."""
    own = ROOT / 'local-test-fixtures' / 'psd'
    git = ROOT / '.git'
    if own.is_dir() or not git.is_file():
        return own
    pointer = git.read_text(encoding='utf-8').strip()
    if pointer.startswith('gitdir:'):
        main = Path(pointer[len('gitdir:'):].strip()).parents[2] / 'local-test-fixtures' / 'psd'
        if main.is_dir():
            return main
    return own


FIXTURES = fixtures_dir()


def block(key, payload, pad=4):
    return b'8BIM' + key + struct.pack('>I', len(payload)) + payload + b'\x00' * (-len(payload) % pad)


def tysh(text_index=None, engine=b''):
    payload = b'\x00\x01' + b'\x00' * 48
    if text_index is not None:
        payload += b'\x00\x00\x00\x09TextIndexlong' + struct.pack('>i', text_index)
    if engine:
        payload += b'\x00\x00\x00\x0aEngineDatatdta' + struct.pack('>I', len(engine)) + engine
    return payload


def record(name, divider=0, text=None):
    """One layer record without channels: (name, section divider type, TySh payload)."""
    blocks = block(b'luni', struct.pack('>I', len(name)) + name.encode('utf-16-be'))
    if divider:
        blocks += block(b'lsct', struct.pack('>I', divider))
    if text is not None:
        blocks += block(b'TySh', text)
    pascal = bytes([len(name[:3])]) + name[:3].encode('latin-1', 'replace')
    pascal += b'\x00' * (-len(pascal) % 4)
    extra = struct.pack('>I', 0) + struct.pack('>I', 0) + pascal + blocks
    return (struct.pack('>iiii', 0, 0, 0, 0) + struct.pack('>H', 0) + b'8BIM' + b'norm' + b'\xff\x00\x00\x00'
            + struct.pack('>I', len(extra)) + extra)


def psd(records, txt2=None):
    """A PSD with these layer records (file order: bottom to top) and Txt2 body."""
    info = struct.pack('>h', len(records)) + b''.join(records)
    info += b'\x00' * (-len(info) % 2)
    section = struct.pack('>I', len(info)) + info + struct.pack('>I', 0)
    if txt2 is not None:
        section += block(b'Txt2', txt2)
    header = b'8BPS' + struct.pack('>H', 1) + b'\x00' * 6 + struct.pack('>HIIHH', 3, 8, 8, 8, 3)
    return header + struct.pack('>I', 0) + struct.pack('>I', 0) + struct.pack('>I', len(section)) + section


def paragraph_run(length, sheet, parent=1):
    return '<< /0 << /0 << /0 (\xfe\xff) /5 << %s >> /6 %d >> >> /1 %d >>' % (sheet, parent, length)


def txt2(*objects):
    """A Txt2 body: "Normal RGB" (Hyphenate on) and "Basic Paragraph" (off, Single-line)
    parent sheets, a default sheet, and one text object per list of paragraph runs."""
    body = (' /98 << /0 14 >> /0 << /6 << /0 ['
            ' << /0 << /0 (\xfe\xffN) /5 << /0 0 /9 true /29 false >> >> >>'
            ' << /0 << /0 (\xfe\xffB) /5 << /0 0 /9 false /29 false >> /6 0 >> >> ] >> >>'
            ' /1 << /1 [ %s ] /3 << /0 0 /9 true /29 false >> >>')
    return (body % ' '.join('<< /0 << /0 (\xfe\xffx) /5 << /0 [ %s ] >> >> >>' % ' '.join(runs)
                            for runs in objects)).encode('latin-1')


class EngineDataSyntax(unittest.TestCase):
    def test_values_nesting_and_strings_with_parentheses(self):
        parsed = text_sheets.parse_engine_data(
            b'\n\n<< /A 1 /B -.5 /C true /D /name /E [ 1 2 << /F false >> ] /G (\xfe\xff\x00a\\)\x00b) /H << >> >>')
        self.assertEqual(parsed['A'], 1)
        self.assertEqual(parsed['B'], -0.5)
        self.assertIs(parsed['C'], True)
        self.assertEqual(parsed['D'], '/name')
        self.assertEqual(parsed['E'], [1, 2, {'F': False}])
        self.assertEqual(parsed['G'], b'\xfe\xff\x00a\\)\x00b')
        self.assertEqual(parsed['H'], {})

    def test_a_bare_body_with_nul_padding(self):
        parsed = text_sheets.parse_engine_data(b' /98 << /0 14 >> /1 [ ]\x00\x00\x00')
        self.assertEqual(parsed, {'98': {'0': 14}, '1': []})


class ParagraphSheets(unittest.TestCase):
    def test_a_run_takes_what_it_leaves_out_from_its_parent_sheet(self):
        data = psd([record('Background'), record('Unstated', text=tysh(0)), record('Stated', text=tysh(1))],
                   txt2([paragraph_run(40, '/0 3')],
                        [paragraph_run(10, '/0 6 /9 true /29 true'), paragraph_run(5, '/0 6 /29 false', parent=0)]))
        sheets = text_sheets.paragraph_sheets(data)
        self.assertEqual(sheets['1'], {'name': 'Unstated', 'source': 'Txt2', 'paragraphs': [
            {'length': 40, 'hyphenate': False, 'everyLineComposer': False}]})
        self.assertEqual(sheets['0']['paragraphs'], [
            {'length': 10, 'hyphenate': True, 'everyLineComposer': True},
            {'length': 5, 'hyphenate': True, 'everyLineComposer': False}])  # Hyphenate from "Normal RGB"

    def test_groups_give_the_paths_photoshop_uses(self):
        # File order is bottom to top; a group is its closing record, its layers, its header.
        data = psd([record('Bottom', text=tysh(0)), record('</Layer group>', divider=3),
                    record('Inner', text=tysh(1)), record('Pixels'), record('Group', divider=1),
                    record('Top', text=tysh(2))],
                   txt2([paragraph_run(1, '')], [paragraph_run(2, '')], [paragraph_run(3, '')]))
        sheets = text_sheets.paragraph_sheets(data)
        self.assertEqual({path: info['name'] for path, info in sheets.items()},
                         {'0': 'Top', '1/1': 'Inner', '2': 'Bottom'})

    def test_a_layer_without_a_txt2_object_reads_its_engine_data(self):
        engine = (b'\n\n<< /EngineDict << /ParagraphRun << /RunArray [ << /ParagraphSheet << /Properties '
                  b'<< /AutoHyphenate false /EveryLineComposer true >> >> >> << /ParagraphSheet << /Properties '
                  b'<< /Justification 0 >> >> >> ] /RunLengthArray [ 7 9 ] >> >> /ResourceDict << '
                  b'/TheNormalParagraphSheet 0 /ParagraphSheetSet [ << /Properties << /AutoHyphenate true '
                  b'/EveryLineComposer false >> >> ] >> >>')
        sheets = text_sheets.paragraph_sheets(psd([record('Old', text=tysh(None, engine))]))
        self.assertEqual(sheets['0']['source'], 'TySh')
        self.assertEqual(sheets['0']['paragraphs'], [
            {'length': 7, 'hyphenate': False, 'everyLineComposer': True},
            {'length': 9, 'hyphenate': True, 'everyLineComposer': False}])

    def test_filling_a_manifest_keeps_what_photoshop_states(self):
        scratch = ROOT / 'build' / 'test-output'
        scratch.mkdir(parents=True, exist_ok=True)
        path = scratch / 'testy-text-sheets-fill.psd'
        path.write_bytes(psd([record('Body', text=tysh(0)), record('Title', text=tysh(1))],
                             txt2([paragraph_run(20, '/0 3')], [paragraph_run(8, '/0 3')])))
        self.addCleanup(path.unlink)

        def paragraph(**values):
            return dict({'from': 0, 'to': 8, 'align': 'left', 'hyphenate': None, 'direction': None,
                         'everyLineComposer': None}, **values)

        layers = [dict(path='0', name='Title', kind='TEXT', paragraphs=[paragraph(hyphenate=True)]),
                  dict(path='1', name='Renamed', kind='TEXT', paragraphs=[paragraph()]),
                  dict(path='2', name='Photo', kind='NORMAL')]
        self.assertEqual(text_sheets.fill_paragraph_settings(path, layers), 1)
        self.assertEqual((layers[0]['paragraphs'][0]['hyphenate'], layers[0]['paragraphs'][0]['everyLineComposer']),
                         (True, False))            # stated value kept, composer from the sheet
        self.assertIsNone(layers[1]['paragraphs'][0]['everyLineComposer'])  # another name: left alone
        two = [dict(path='1', name='Body', kind='TEXT', paragraphs=[paragraph(), paragraph()])]
        self.assertEqual(text_sheets.fill_paragraph_settings(path, two), 0)  # another paragraph count
        self.assertEqual(text_sheets.fill_paragraph_settings(scratch / 'no-such-file.psd', two), 0)
        path.write_bytes(b'not a psd')
        self.assertEqual(text_sheets.fill_paragraph_settings(path, two), 0)

    def test_photoshop_fixtures_if_available(self):
        cases = {
            'issue-672-justify-modes.psd': [(False, False)],                               # names only its justification
            'every-line-composer/justify-modes-parent-sheet-every-line.psd': [(False, True)],  # parent sheet says Every-line
            'every-line-composer/justify-modes-every-line-photoshop.psd': [(False, True)],
            'every-line-composer/two-paragraphs-every-then-single-photoshop.psd': [(False, True), (False, False)],
        }
        read = 0
        for name, expected in cases.items():
            path = FIXTURES / name
            if not path.exists():
                continue
            read += 1
            sheets = text_sheets.paragraph_sheets(path.read_bytes())
            self.assertTrue(sheets, name)
            for info in sheets.values():
                self.assertEqual([(p['hyphenate'], p['everyLineComposer']) for p in info['paragraphs']],
                                 expected, name)
        if not read:
            self.skipTest('optional fixture missing: local-test-fixtures/psd line composer files')


class ProbeNeverSetsTextSettings(unittest.TestCase):
    """docs/txt2-kept-settings.md, "Line composer": a set that carries a paragraph style
    without its line composer turns Adobe Every-line Composer on."""

    def test_the_refresh_and_the_settings_read_write_no_styles(self):
        jsx = photoshop._PROBE_JSX
        refresh = jsx.split('  function relayText', 1)[1].split('  function refreshSmartObjects', 1)[0]
        self.assertIn("textClickPoint", refresh)
        for forbidden in ('paragraphStyle', 'textStyle', 'textStyleRange', "stringIDToTypeID('textLayer'), textKey"):
            self.assertNotIn(forbidden, refresh)
        self.assertNotIn('textItem.justification =', jsx)
        self.assertNotIn('resolveTextSettings', jsx)
        settings = jsx.split('  function textSettings', 1)[1].split('  function walk', 1)[0]
        self.assertNotIn('executeAction(', settings)


if __name__ == '__main__':
    unittest.main(verbosity=2)
