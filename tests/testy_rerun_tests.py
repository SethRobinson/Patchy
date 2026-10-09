"""Local Testy regression tests. No editors or desktop automation are launched."""
import copy
import hashlib
import http.client
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'testy'))
import rerun
import report
import testy
from drivers import photoshop


def cell(score):
    return dict(state='done', opens='ok', renderMetrics=dict(accuracy=score, badFraction=1-score,
        perceptual=dict(accuracy=score, badFraction=1-score)),
        native=dict(nativeScore=score, nativeKept=int(score*10), nativeTotal=10),
        artifacts=dict(render='files/image/patchy/render.png'))


class RerunTests(unittest.TestCase):
    def setUp(self):
        scratch = ROOT / 'build/test-output'
        scratch.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix='testy-rerun-', dir=scratch)
        self.addCleanup(self.temp.cleanup)
        self.runs = Path(self.temp.name)
        self.parent = self.runs / '20261002-000001'
        self.child = self.runs / '20261002-000002'
        self.parent.mkdir()
        self.child.mkdir()
        source = self.runs / 'image 日本%20.psd'
        source.write_bytes(b'task-owned synthetic source')
        self.row = dict(source=str(source), name=source.name, sha1=hashlib.sha1(source.read_bytes()).hexdigest(),
            groundTruth=dict(state='done'), cells=dict(patchy=cell(.5), photoshop=cell(1)),
            scan=dict(flagged=True, reasons=['old reason']))
        other = copy.deepcopy(self.row)
        other['source'] += '.other.psd'
        other['name'] = 'other.psd'
        self.base = dict(state='done', run=dict(name=self.parent.name, startedAt='2026-10-02T00:00:01', patchyGit='old', patchyVersion='old',
            editorOrder=['patchy', 'photoshop'], compare='perceptual', scan=dict(thresholdPct=10)),
            editors=dict(patchy=dict(displayName='Patchy', version='old'),
                         photoshop=dict(displayName='Photoshop', version='27')),
            files=[self.row, other])
        rerun.write_json(self.parent / 'status.json', self.base)
        rerun.write_json(self.parent / 'results.json', self.base)
        rerun.write_text(self.runs / 'history.jsonl', json.dumps(testy.Runner.summarize_status(self.base)) + '\n')
        self.job = rerun.prepare(self.runs, self.parent.name, self.row['source'], ['patchy'])
        fresh_row = copy.deepcopy(self.row)
        fresh_row.pop('scan')
        fresh_row['cells'] = dict(patchy=cell(1))
        self.fresh = dict(state='done', run=dict(name=self.child.name, sourcesUntouched=True,
            patchyGit='new', finishedAt='2026-10-02T12:00:00', editorOrder=['patchy']),
            editors=dict(patchy=dict(displayName='Patchy', version='new')), files=[fresh_row])
        artifact = self.child / 'files/image/patchy/render.png'
        artifact.parent.mkdir(parents=True)
        artifact.write_bytes(b'new render')
        rerun.write_json(self.child / 'results.json', self.fresh)

    def apply(self):
        return rerun.apply(self.runs, self.child, self.job, summarize=testy.Runner.summarize_status,
                           scan_reasons=testy.Runner.scan_reasons_for_status)

    def test_updates_selected_cells_totals_flags_and_retains_previous_artifacts(self):
        updated = self.apply()
        self.assertEqual(updated['files'][1], self.base['files'][1])
        self.assertEqual(updated['files'][0]['cells']['photoshop'], self.row['cells']['photoshop'])
        self.assertEqual(updated['files'][0]['cells']['patchy']['renderMetrics']['accuracy'], 1)
        self.assertFalse(updated['files'][0]['scan']['flagged'])
        self.assertTrue(updated['files'][0]['scan']['artifactsKept'])
        snapshot = json.loads((self.parent / updated['files'][0]['reruns'][0]['previous']).read_text())
        self.assertEqual(snapshot, self.base)
        image = self.parent / updated['files'][0]['cells']['patchy']['artifacts']['render']
        self.assertEqual(image.read_bytes(), b'new render')
        self.assertEqual(json.loads((self.parent/'results.json').read_text()), updated)
        history = [json.loads(line) for line in (self.runs/'history.jsonl').read_text().splitlines()]
        self.assertEqual(len(history), 1)
        self.assertEqual(history[0]['editors']['patchy']['render'], .75)
        self.assertNotIn(self.row['source']+'\n', (self.parent/'flagged.txt').read_text())
        self.assertEqual(updated['run']['patchyGit'], 'old')
        self.assertEqual(updated['run']['reruns'][0]['patchyGit'], 'new')

    def test_all_editors_and_repeated_reruns(self):
        self.job['editors'] = ['patchy', 'photoshop']
        self.fresh['files'][0]['cells']['photoshop'] = cell(.8)
        self.fresh['editors']['photoshop'] = dict(displayName='Photoshop', version='28')
        rerun.write_json(self.child/'results.json', self.fresh)
        self.apply()
        self.child = self.runs / '20261002-000003'
        self.child.mkdir()
        self.fresh['run']['name'] = self.child.name
        # No images needed to validate the second revision's result update.
        self.fresh['files'][0]['cells']['patchy']['artifacts'] = {}
        self.fresh['files'][0]['cells']['photoshop']['artifacts'] = {}
        rerun.write_json(self.child/'results.json', self.fresh)
        self.job = rerun.prepare(self.runs, self.parent.name, self.row['source'], ['patchy', 'photoshop'])
        updated = self.apply()
        self.assertEqual(len(updated['files'][0]['reruns']), 2)
        self.assertEqual(updated['files'][0]['cells']['photoshop']['renderMetrics']['accuracy'], .8)

    def test_failed_or_incomplete_child_does_not_change_batch(self):
        original = (self.parent/'status.json').read_bytes()
        cases = [('state', 'running'), ('source-change', None), ('truth-failed', None), ('editor-failed', None)]
        for case, value in cases:
            bad = copy.deepcopy(self.fresh)
            if case == 'state': bad['state'] = value
            elif case == 'source-change': bad['files'][0]['sha1'] = 'different'
            elif case == 'truth-failed': bad['files'][0]['groundTruth']['state'] = 'failed'
            else: bad['files'][0]['cells']['patchy']['state'] = 'failed'
            rerun.write_json(self.child/'results.json', bad)
            with self.subTest(case=case), self.assertRaises(ValueError): self.apply()
            self.assertEqual((self.parent/'status.json').read_bytes(), original)
            self.assertEqual((self.parent/'results.json').read_bytes(), original)

    def test_rejects_changed_source_stale_row_and_traversal(self):
        with self.assertRaises(ValueError):
            rerun.prepare(self.runs, '../outside', self.row['source'], ['patchy'])
        with self.assertRaises(ValueError):
            rerun.prepare(self.runs, self.parent.name, self.row['source'], ['unknown'])
        Path(self.row['source']).write_bytes(b'changed')
        with self.assertRaises(ValueError):
            rerun.prepare(self.runs, self.parent.name, self.row['source'], ['patchy'])
        changed = copy.deepcopy(self.base)
        changed['files'][0]['cells']['patchy']['state'] = 'changed'
        rerun.write_json(self.parent/'status.json', changed)
        with self.assertRaises(ValueError): self.apply()

    def test_failed_atomic_replace_keeps_old_file(self):
        path = self.parent/'status.json'
        original = path.read_bytes()
        with mock.patch.object(rerun.os, 'replace', side_effect=PermissionError('busy')):
            with self.assertRaises(PermissionError): rerun.write_json(path, {'oops': True})
        self.assertEqual(path.read_bytes(), original)
        self.assertFalse(list(self.parent.glob('*.tmp')))

    def test_publish_failure_restores_results_and_history(self):
        paths = [self.parent/'status.json', self.parent/'results.json', self.runs/'history.jsonl']
        originals = {p: p.read_bytes() for p in paths}
        write = rerun.write_text
        def fail_status(path, value):
            if path == self.parent/'status.json':
                raise PermissionError('status busy')
            write(path, value)
        with mock.patch.object(rerun, 'write_text', side_effect=fail_status):
            with self.assertRaises(PermissionError): self.apply()
        self.assertEqual({p:p.read_bytes() for p in paths}, originals)

    def test_font_inventory_changes_cache_key_between_runs(self):
        first = photoshop.PhotoshopDriver()
        first._app = mock.Mock()
        first._app.DoJavaScript.return_value = 'Installed'
        before = first.font_cache_key()
        self.assertEqual(first.font_cache_key(), before)
        first._app.DoJavaScript.assert_called_once()
        second = photoshop.PhotoshopDriver()
        second._app = mock.Mock()
        second._app.DoJavaScript.return_value = 'Installed\nNewFace'
        self.assertNotEqual(second.font_cache_key(), before)

    def test_refused_files_score_zero_and_harness_failures_stay_out(self):
        truth = dict(state='done', artifacts=dict(render='files/a/_truth/render.png'))
        opened = dict(state='done', opens='ok', renderMetrics=dict(
            accuracy=0.8, badFraction=0.2, perceptual=dict(accuracy=0.9, badFraction=0.1)))
        refused = dict(state='failed', opens='fail', fileRejected=True, error='unsupported mode')
        timed_out = dict(state='failed', error='timeout after 180s')
        self.assertTrue(testy.refused_with_reference(dict(groundTruth=truth), refused))
        self.assertFalse(testy.refused_with_reference(dict(groundTruth=truth), timed_out))
        self.assertFalse(testy.refused_with_reference(dict(groundTruth=dict(state='failed')), refused))
        self.assertFalse(testy.refused_with_reference(dict(groundTruth=dict(state='done', artifacts={})), refused))
        status = dict(run=dict(editorOrder=['krita'], name='r', patchyGit='x'), files=[
            dict(groundTruth=truth, cells=dict(krita=opened)),
            dict(groundTruth=truth, cells=dict(krita=refused)),
            dict(groundTruth=truth, cells=dict(krita=timed_out)),
        ])
        summary = testy.Runner.summarize_status(status)['editors']['krita']
        self.assertAlmostEqual(summary['render'], 0.4)
        self.assertAlmostEqual(summary['visual'], 0.45)
        self.assertEqual((summary['opened'], summary['total']), (1, 3))

    def test_long_cache_keys_keep_their_trailing_qualifiers(self):
        short = "5.3.2-settled1-freshtext2-nocache11"
        self.assertEqual(testy._version_slug(short), short)
        base = "exe-0123456789abcdef0123-fontcheck1-0123456789abcdef-srgb1-freshtext2-freshsmart1"
        first, second = testy._version_slug(base + "-nocache10"), testy._version_slug(base + "-nocache11")
        self.assertNotEqual(first, second)
        self.assertLessEqual(len(first), 60)
        self.assertTrue(first.startswith("exe-0123456789abcdef0123-fontcheck1"))

    def test_patchy_cache_key_follows_the_exe_not_the_commit(self):
        exe = self.runs / 'patchy.exe'
        exe.write_bytes(b'build one')
        first = testy.patchy_build_key(exe, 'abc123')
        self.assertTrue(first.startswith('exe-'))
        self.assertEqual(testy.patchy_build_key(exe, 'a-different-commit'), first)
        exe.write_bytes(b'build two!')
        self.assertNotEqual(testy.patchy_build_key(exe, 'abc123'), first)
        self.assertEqual(testy.patchy_build_key(None, 'abc123'), 'abc123')
        self.assertEqual(testy.patchy_build_key(self.runs / 'missing.exe', 'abc123'), 'abc123')

    def test_endpoint_spawns_fresh_row_rerun_and_serves_new_controls_for_old_report(self):
        (self.parent/'report.html').write_text('old report')
        handler = lambda *args, **kwargs: testy.TestyRequestHandler(*args, directory=str(self.runs.parent), **kwargs)
        server = testy._ExclusiveHTTPServer(('127.0.0.1', 0), handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        self.addCleanup(server.server_close)
        self.addCleanup(server.shutdown)
        with mock.patch.object(testy.config, 'RUNS_DIR', self.runs), \
             mock.patch.object(testy, '_run_in_progress', return_value=False), \
             mock.patch.object(testy.TestyRequestHandler, '_spawn_child') as spawn:
            conn = http.client.HTTPConnection('127.0.0.1', server.server_port)
            self.addCleanup(conn.close)
            conn.request('POST', '/testy-rerun-file', json.dumps(dict(run=self.parent.name, source=self.row['source'], scope='patchy')),
                         {'Content-Type': 'application/json'})
            response = conn.getresponse()
            self.assertEqual(response.status, 200, response.read())
            command = spawn.call_args.args[0]
            self.assertIn('--fresh', command)
            self.assertIn('--apply-to-run', command)
            self.assertEqual(command[command.index('--editors')+1], 'patchy')
            self.assertNotIn('--scan', command)
            conn.request('GET', '/' + self.runs.name + '/' + self.parent.name + '/report.html')
            response = conn.getresponse()
            self.assertEqual(response.status, 200)
            self.assertIn(b'rerunRowControls(f, fi)', response.read())
            self.assertEqual((self.parent/'report.html').read_text(), 'old report')
        with mock.patch.object(testy, '_run_in_progress', return_value=True), \
             mock.patch.object(testy.TestyRequestHandler, '_spawn_child') as spawn:
            conn.request('POST', '/testy-rerun-file', '{}', {'Content-Type': 'application/json'})
            response = conn.getresponse()
            self.assertEqual(response.status, 409)
            response.read()
            spawn.assert_not_called()

    def test_colliding_stems_get_distinct_artifact_directories(self):
        corpus = [Path('a/x.psd'), Path('a/x.psb'), Path('b/X.psd'), Path('b/x.psb'), Path('y.psd')]
        names = testy.unique_artifact_dirs(corpus)
        self.assertEqual(names, [None, 'x~psb', 'X~psd', 'x~2', None])
        entries = [dict(name=p.name, **({'dir': n} if n else {})) for p, n in zip(corpus, names)]
        resolved = [testy.artifact_dir_name(e) for e in entries]
        self.assertEqual(resolved, ['x', 'x~psb', 'X~psd', 'x~2', 'y'])
        self.assertEqual(len({r.lower() for r in resolved}), len(resolved))

    def test_file_traits_read_depth_mode_and_artboards(self):
        header = b'8BPS' + (1).to_bytes(2, 'big') + bytes(6) + (3).to_bytes(2, 'big') + bytes(8)
        plain = self.runs / 'plain.psd'
        plain.write_bytes(header + (8).to_bytes(2, 'big') + (3).to_bytes(2, 'big') + bytes(32))
        deep = self.runs / 'deep.psb'
        deep.write_bytes(header + (32).to_bytes(2, 'big') + (1).to_bytes(2, 'big') + b'....8B64artb....')
        self.assertEqual(testy.file_traits(plain), {'depth': 8, 'mode': 3})
        self.assertEqual(testy.file_traits(deep), {'depth': 32, 'mode': 1, 'artboards': True})
        self.assertIsNone(testy.file_traits(self.runs / 'missing.psd'))
        (self.runs / 'not.psd').write_bytes(b'not a psd at all, but long enough')
        self.assertIsNone(testy.file_traits(self.runs / 'not.psd'))

    def test_saved_bit_depth_is_read_from_both_headers(self):
        def psd(name, depth):
            path = self.runs / name
            path.write_bytes(b'8BPS' + (1).to_bytes(2, 'big') + bytes(6) + (3).to_bytes(2, 'big') + bytes(8)
                             + depth.to_bytes(2, 'big') + (3).to_bytes(2, 'big') + bytes(32))
            return path

        self.assertEqual(testy.psd_header_depth(psd('d16.psd', 16)), 16)
        self.assertIsNone(testy.psd_header_depth(self.runs / 'missing.psd'))
        self.assertEqual(testy.save_depth_record(16, psd('r8.psd', 8)),
                         {'original': 16, 'resaved': 8, 'kept': False})
        self.assertEqual(testy.save_depth_record(16, psd('r16.psd', 16))['kept'], True)
        # Saving deeper than the original loses nothing.
        self.assertEqual(testy.save_depth_record(8, psd('r16b.psd', 16))['kept'], True)
        self.assertIsNone(testy.save_depth_record(None, psd('r16c.psd', 16)))
        (self.runs / 'junk.psd').write_bytes(b'x' * 40)
        self.assertIsNone(testy.save_depth_record(16, self.runs / 'junk.psd'))
        # The summary counts deep files only, and averages the 16-bit precision legs.
        deep_cell = lambda kept, accuracy: dict(state='done', opens='ok',
            saveDepth=dict(original=16, resaved=16 if kept else 8, kept=kept),
            deepRoundtrip=dict(state='done', accuracy=accuracy))
        status = dict(run=dict(editorOrder=['patchy'], name='r', patchyGit='x'), files=[
            dict(cells=dict(patchy=deep_cell(True, 1.0))),
            dict(cells=dict(patchy=deep_cell(False, 0.2))),
            dict(cells=dict(patchy=dict(state='done', opens='ok',
                                        saveDepth=dict(original=8, resaved=8, kept=True)))),
        ])
        summary = testy.Runner.summarize_status(status)['editors']['patchy']
        self.assertEqual((summary['depthKept'], summary['depthTotal']), (1, 2))
        self.assertAlmostEqual(summary['deepRoundtrip'], 0.6)
        self.assertIsNone(summary['deepRender'])

    def test_psdtools_column_is_opt_in_and_names_missing_packages(self):
        from drivers import psdtools
        self.assertNotIn('psdtools', testy.DEFAULT_EDITORS)
        self.assertIn('psdtools', testy.OPT_IN_EDITORS)
        self.assertIn('psdtools', testy.KNOWN_CELL_DIRS)
        with mock.patch.object(psdtools, 'version', return_value='1.17.0'), \
                mock.patch.object(psdtools, 'missing_composite_modules', return_value=['scipy']):
            info = testy.config.discover_editors('hash')['psdtools']
        self.assertFalse(info.available)
        self.assertIn('psd-tools[composite]', info.notes[0])
        self.assertIn('scipy', info.notes[0])
        with mock.patch.object(psdtools, 'version', return_value='1.17.0'), \
                mock.patch.object(psdtools, 'missing_composite_modules', return_value=[]):
            info = testy.config.discover_editors('hash')['psdtools']
        self.assertTrue(info.available)
        self.assertEqual(info.version, '1.17.0')
        failed = mock.Mock(returncode=1, stdout='', stderr='Traceback\nValueError: bad file\n')
        with mock.patch.object(psdtools.subprocess, 'run', return_value=failed):
            result = psdtools.export(self.runs / 'in.psd', self.runs / 'missing.png')
        self.assertEqual((result['ok'], result['fileRejected'], result['stderr'], result['note']),
                         (False, True, 'ValueError: bad file', ''))
        rendered = self.runs / 'out.png'
        rendered.write_bytes(b'png')
        noted = mock.Mock(returncode=0, stderr='', stdout='x\n' + psdtools.NOTE_MARKER + 'fell back\n')
        with mock.patch.object(psdtools.subprocess, 'run', return_value=noted):
            result = psdtools.export(self.runs / 'in.psd', rendered)
        self.assertEqual((result['ok'], result['note']), (True, 'fell back'))

    def test_photocraft_column_is_default_and_maps_cli_exit_codes(self):
        from drivers import photocraft
        self.assertIn('photocraft', testy.DEFAULT_EDITORS)
        self.assertIn('photocraft', testy.KNOWN_CELL_DIRS)
        self.assertEqual(testy.TEXT_RENDER_BASIS['photocraft'][0], 'open')
        exe = self.runs / 'photocraft-cli.exe'
        exe.write_bytes(b'')
        with mock.patch.dict(testy.config.LOCAL, {'editors': {'photocraft': str(exe)}}), \
                mock.patch.object(photocraft, 'version', return_value='0.5.0 (e5e3e3975, 2026-10-08)'):
            info = testy.config.discover_editors('hash')['photocraft']
        self.assertTrue(info.available)
        self.assertEqual((info.exe, info.display_name, info.version),
                         (exe, 'PhotoCraft', '0.5.0 (e5e3e3975, 2026-10-08)'))
        version_out = mock.Mock(returncode=0, stdout='photocraft-cli 0.5.0 (e5e3e3975, 2026-10-08)\n')
        with mock.patch.object(photocraft.subprocess, 'run', return_value=version_out):
            self.assertEqual(photocraft.version(exe), '0.5.0 (e5e3e3975, 2026-10-08)')

        def process(returncode, stderr='', output=None, hang=False):
            proc = mock.Mock(returncode=returncode, pid=1)
            def communicate(timeout=None):
                if hang and timeout is not None:
                    raise photocraft.subprocess.TimeoutExpired('photocraft-cli', timeout)
                if output is not None:
                    output.write_bytes(b'psd')
                return '', stderr
            proc.communicate.side_effect = communicate
            return proc

        rendered = self.runs / 'out.psd'
        ok = process(0, 'warning: 2 layer(s) flattened; layers, masks and blend modes are not kept\n'
                        'warning: font "Foo" is not installed\n', output=rendered)
        with mock.patch.object(photocraft.subprocess, 'Popen', return_value=ok):
            result = photocraft.export(exe, self.runs / 'in.psd', rendered)
        self.assertEqual((result['ok'], result['fileRejected'], result['note']),
                         (True, False, 'font "Foo" is not installed'))
        refused = process(1, 'error: codec: unrecognized image format\n')
        with mock.patch.object(photocraft.subprocess, 'Popen', return_value=refused):
            result = photocraft.export(exe, self.runs / 'in.psd', self.runs / 'missing.png')
        self.assertEqual((result['ok'], result['fileRejected'], result['stderr']),
                         (False, True, 'codec: unrecognized image format'))
        # A hang or crash is news about PhotoCraft, not the file: the breaker counts it.
        hung = process(None, hang=True)
        with mock.patch.object(photocraft.subprocess, 'Popen', return_value=hung), \
                mock.patch.object(photocraft.subprocess, 'run') as taskkill:
            result = photocraft.export(exe, self.runs / 'in.psd', self.runs / 'missing.png')
        self.assertEqual((result['ok'], result['fileRejected'], result['exitCode']), (False, False, -1))
        self.assertIn('/t', taskkill.call_args.args[0])
        crashed = process(-1073741819)
        with mock.patch.object(photocraft.subprocess, 'Popen', return_value=crashed):
            result = photocraft.export(exe, self.runs / 'in.psd', self.runs / 'missing.png')
        self.assertEqual((result['fileRejected'], result['stderr']),
                         (False, 'PhotoCraft crashed (exit 0xC0000005)'))
        # The cache-free leg's text re-layout: a document without type layers refuses the
        # command, and gets a plain render instead.
        afresh = self.runs / 'afresh.png'
        no_type = process(1, 'error: `type.updateAllTextLayers`: command `type.updateAllTextLayers` is not '
                             'available right now: the document has no type layers\n')
        with mock.patch.object(photocraft.subprocess, 'Popen',
                               side_effect=[no_type, process(0, output=afresh)]) as popen:
            result = photocraft.render_text_afresh(exe, self.runs / 'in.psd', afresh)
        self.assertTrue(result['ok'])
        self.assertEqual([call.args[0][1] for call in popen.call_args_list], ['run', 'convert'])
        self.assertIn('type.updateAllTextLayers', popen.call_args_list[0].args[0])

    def test_add_editors_measures_only_the_new_column_against_recorded_ground_truth(self):
        import argparse
        import types
        root = self.runs / 'testy-root'
        run_dir = root / 'runs' / '20261009-000000'
        cache = root / 'cache'
        run_dir.mkdir(parents=True)
        sources = [self.runs / f'{name}.psd' for name in ('cached', 'uncached', 'changed')]
        sha = {str(path): f'sha-{path.stem}' for path in sources}
        for path in sources:
            path.write_bytes(b'psd')
        truth = dict(state='done', artifacts={})
        patchy_cell = cell(.7)
        status = dict(state='done', run=dict(name=run_dir.name, suffix='~TESTY~', compare='perceptual',
            editorOrder=['photoshop', 'patchy'], startedAt='2026-10-09T00:00:00', finishedAt='2026-10-09T01:00:00',
            patchyGit='old', sourcesUntouched=True),
            editors=dict(photoshop=dict(displayName='Photoshop'), patchy=dict(displayName='Patchy', version='old')),
            files=[dict(name=p.name, source=str(p), sha1=sha[str(p)], traits={}, groundTruth=dict(truth),
                        cells=dict(photoshop=cell(1), patchy=copy.deepcopy(patchy_cell))) for p in sources])
        (run_dir / 'status.json').write_text(json.dumps(status), encoding='utf-8')
        history = root / 'runs' / 'history.jsonl'
        history.write_text(json.dumps({'run': 'older'}) + '\n' + json.dumps({'run': run_dir.name}) + '\n',
                           encoding='utf-8')
        ps = mock.Mock(unavailable=False, unavailable_reason=None)
        ps.version.return_value = '27.0'
        ps.font_cache_key.return_value = 'fonts'
        gt = cache / f'gt-sha-cached-27.0-~TESTY~-fontcheck1-fonts'
        gt.mkdir(parents=True)
        (gt / 'result.json').write_text(json.dumps(dict(ok=True, layers=[{'name': 'L'}])), encoding='utf-8')
        (gt / 'render.png').write_bytes(b'png')
        editors = {key: testy.config.EditorInfo(key, name, Path('x.exe'), version='v1', available=True)
                   for key, name in (('photoshop', 'Photoshop'), ('patchy', 'Patchy'), ('photocraft', 'PhotoCraft'))}
        args = argparse.Namespace(resume=str(run_dir), add_editors='photocraft', suffix='~TESTY~', scan=None,
                                  compare='perceptual', fresh=False, no_build=False, server_url=None,
                                  no_serve=True, no_browser=True, port=0, apply_to_run=None)
        measured = []

        def run_cell(runner, index, editor_key, staged, truth_result):
            measured.append((runner.status['files'][index]['name'], editor_key, truth_result))
            runner.status['files'][index]['cells'][editor_key].update(cell(.9))

        def stage(source, _dir):
            digest = 'sha-moved' if Path(source).stem == 'changed' else sha[str(source)]
            return types.SimpleNamespace(sha1=digest, trap_error=None, trap_skipped=None, original=source)

        with mock.patch.object(testy.config, 'discover_editors', return_value=editors), \
                mock.patch.object(testy.config, 'CACHE_DIR', cache), \
                mock.patch.object(testy.config, 'TESTY_ROOT', root), \
                mock.patch.object(testy.config, 'RUNS_DIR', root / 'runs'), \
                mock.patch.object(testy, 'PhotoshopDriver', return_value=ps), \
                mock.patch.object(testy, 'git_hash', return_value='new'), \
                mock.patch.object(testy.staging, 'stage_psd', side_effect=stage), \
                mock.patch.object(testy.staging, 'sha1_of_file', side_effect=lambda p: sha[str(p)]), \
                mock.patch.object(testy.Runner, 'run_cell', run_cell):
            self.assertEqual(testy.Runner(args).run(), 0)
            # Adding the same column again, or to an unfinished run, is refused.
            with self.assertRaises(SystemExit):
                testy.Runner(args)
            args.add_editors = 'photocraft,krita'
            done = json.loads((run_dir / 'status.json').read_text(encoding='utf-8'))
            (run_dir / 'status.json').write_text(json.dumps({**done, 'state': 'paused'}), encoding='utf-8')
            with self.assertRaises(SystemExit):
                testy.Runner(args)
        ps.probe.assert_not_called()  # Photoshop never renders a reference again
        self.assertEqual(measured, [('cached.psd', 'photocraft', dict(ok=True, layers=[{'name': 'L'}],
                                                                      textFontsMissing=None))])
        self.assertTrue((run_dir / 'files' / 'cached' / '_truth' / 'render.png').exists())
        files = done['files']
        self.assertEqual(done['run']['editorOrder'], ['photoshop', 'patchy', 'photocraft'])
        self.assertEqual([f['cells']['patchy'] for f in files], [patchy_cell] * 3)
        self.assertEqual([f['groundTruth'] for f in files], [truth] * 3)
        self.assertEqual(files[0]['cells']['photocraft']['state'], 'done')
        self.assertEqual(files[1]['cells']['photocraft']['state'], 'skipped')
        self.assertIn('no longer in testy/cache', files[1]['cells']['photocraft']['error'])
        self.assertIn('source changed', files[2]['cells']['photocraft']['error'])
        self.assertEqual((done['run']['finishedAt'], done['run']['patchyGit']), ('2026-10-09T01:00:00', 'old'))
        self.assertNotIn('addingEditors', done['run'])
        self.assertEqual(done['run']['addedEditors'][0]['editors'], ['photocraft'])
        self.assertIn('addedAt', done['editors']['photocraft'])
        lines = [json.loads(line) for line in history.read_text(encoding='utf-8').splitlines()]
        self.assertEqual([line['run'] for line in lines], ['older', run_dir.name])
        self.assertIn('photocraft', lines[1]['editors'])
        self.assertEqual(lines[1]['editors']['photocraft']['opened'], 1)

    def test_cache_stripper_empties_cached_layers_and_only_those(self):
        import psd_sections

        source = Path(__file__).resolve().parents[1] / "test-fixtures" / "psd" / "photoshop-text-tracking.psd"
        data = source.read_bytes()
        stripped, changed = psd_sections.strip_cached_pixels(data)
        plain, changed_plain = psd_sections.strip_cached_pixels(data, plain=True)
        self.assertTrue(changed)
        self.assertEqual(changed, changed_plain)
        self.assertTrue(any(layer["kind"] == "text" for layer in changed))
        for layer in changed:
            left, top, right, bottom = layer["bounds"]
            self.assertTrue(right > left and bottom > top)
        # The stripped copy keeps what defines the layers; the plain copy does not,
        # and differs from the stripped one in nothing but those four-byte keys.
        self.assertIn(b"8BIMTySh", stripped)
        self.assertNotIn(b"8BIMTySh", plain)
        self.assertEqual(len(stripped), len(plain))
        self.assertLess(len(stripped), len(data))
        differing = sum(1 for a, b in zip(stripped, plain) if a != b)
        self.assertLessEqual(differing, 4 * len(changed))
        # Both still parse, with the same layer count as the original.
        self.assertEqual(psd_sections.read_layout(stripped).layer_count,
                         psd_sections.read_layout(data).layer_count)
        # Kinds can be left alone: with text kept, this text-only file has nothing to strip.
        with self.assertRaises(psd_sections.PsdParseError):
            psd_sections.strip_cached_pixels(data, keep_kinds=("text",))
        # Nothing left to strip the second time round.
        with self.assertRaises(psd_sections.PsdParseError):
            psd_sections.strip_cached_pixels(stripped)

    def test_cache_stripper_handles_layer_records_in_an_lr16_block(self):
        # 16/32-bit files keep their layer records in an Lr16/Lr32 block behind an empty
        # standard layer info. Rebuild a tracked 8-bit fixture that way and strip it.
        import struct

        import psd_sections

        source = Path(__file__).resolve().parents[1] / "test-fixtures" / "psd" / "photoshop-text-tracking.psd"
        data = source.read_bytes()
        offset = 26
        offset += 4 + struct.unpack_from(">I", data, offset)[0]
        offset += 4 + struct.unpack_from(">I", data, offset)[0]
        section_length = struct.unpack_from(">I", data, offset)[0]
        info_length = struct.unpack_from(">I", data, offset + 4)[0]
        records = data[offset + 8:offset + 8 + info_length]
        records += b"\0" * (-len(records) % 4)
        trailer = b"8BIMLMsk" + struct.pack(">I", 2) + b"\0\0" + b"\0\0"  # a block after it, padded
        section = (struct.pack(">I", 0) + struct.pack(">I", 0)
                   + b"8BIMLr16" + struct.pack(">I", len(records)) + records + trailer)
        deep = data[:offset] + struct.pack(">I", len(section)) + section + data[offset + 4 + section_length:]

        self.assertEqual(psd_sections.read_layout(deep).layer_count, psd_sections.read_layout(data).layer_count)
        stripped, changed = psd_sections.strip_cached_pixels(deep)
        self.assertEqual([layer["kind"] for layer in changed],
                         [layer["kind"] for layer in psd_sections.strip_cached_pixels(data)[1]])
        self.assertLess(len(stripped), len(deep))
        # The rewritten file still walks: same layer count, the block after Lr16 intact,
        # the image data where the section length says it is.
        layout = psd_sections.read_layout(stripped)
        self.assertEqual(layout.layer_count, psd_sections.read_layout(deep).layer_count)
        self.assertEqual(stripped[layout.image_data_offset:], deep[psd_sections.read_layout(deep).image_data_offset:])
        self.assertEqual(stripped[layout.image_data_offset - len(trailer):layout.image_data_offset], trailer)
        self.assertIn(b"8BIMTySh", stripped)
        plain, _ = psd_sections.strip_cached_pixels(deep, plain=True)
        self.assertNotIn(b"8BIMTySh", plain)
        with self.assertRaises(psd_sections.PsdParseError):
            psd_sections.strip_cached_pixels(stripped)

    def test_cache_stripper_undoes_the_combined_pixel_and_vector_mask_if_available(self):
        # A layer with both masks stores their combination (channel -2) beside the pixel
        # mask (channel -3). Needs the psd-tools collection (testy/fetch_psd_tools_corpus.py).
        import struct

        import psd_sections

        source = ROOT / "local-test-fixtures" / "psd-tools" / "tests" / "psd_files" / "mask_parameters.psd"
        if not source.exists():
            self.skipTest("psd-tools collection not fetched")
        data = source.read_bytes()
        keep = ("text", "smart", "vector")
        stripped, changed = psd_sections.strip_cached_pixels(data, keep_kinds=keep)
        self.assertEqual([layer["kind"] for layer in changed], ["mask"])
        # Left alone on request, and nothing more to undo the second time round.
        with self.assertRaises(psd_sections.PsdParseError):
            psd_sections.strip_cached_pixels(data, keep_kinds=keep + ("mask",))
        with self.assertRaises(psd_sections.PsdParseError):
            psd_sections.strip_cached_pixels(stripped, keep_kinds=keep)

        def masks(blob):
            """(mask rectangle, real rectangle, combined bytes, pixel-mask bytes) of the masked layer."""
            offset = 26
            offset += 4 + struct.unpack_from(">I", blob, offset)[0]
            offset += 4 + struct.unpack_from(">I", blob, offset)[0]
            cursor = offset + 8
            count = abs(struct.unpack_from(">h", blob, cursor)[0])
            cursor += 2
            layers = []
            for _ in range(count):
                channel_count = struct.unpack_from(">H", blob, cursor + 16)[0]
                channels = [struct.unpack_from(">hI", blob, cursor + 18 + 6 * i) for i in range(channel_count)]
                cursor += 18 + 6 * channel_count + 12
                extra = struct.unpack_from(">I", blob, cursor)[0]
                mask_length = struct.unpack_from(">I", blob, cursor + 4)[0]
                mask = blob[cursor + 8:cursor + 8 + mask_length]
                cursor += 4 + extra
                layers.append((channels, mask))
            found = None
            for channels, mask in layers:
                chunks = {}
                for channel_id, length in channels:
                    chunks[channel_id] = blob[cursor:cursor + length]
                    cursor += length
                if -3 in chunks:
                    found = (mask[0:16], mask[20:36], chunks[-2], chunks[-3])
            return found

        before, after = masks(data), masks(stripped)
        self.assertNotEqual(before[2], before[3])   # the file really stored a combination
        self.assertEqual(after[3], before[3])       # the pixel mask is untouched
        self.assertEqual(after[2], before[3])       # and now stands where the combination was
        self.assertEqual(after[0], before[1])       # with the pixel mask's own rectangle
        self.assertEqual(psd_sections.read_layout(stripped).layer_count, psd_sections.read_layout(data).layer_count)

    def test_no_cache_render_checks_tell_a_blank_layer_from_another_document(self):
        import analyze
        from PIL import Image

        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)

            def picture(name, size=(40, 40), fills=()):
                image = Image.new("RGBA", size, (255, 255, 255, 255))
                for box, color in fills:
                    image.paste(color, box)
                image.save(root / name)
                return root / name

            box = [10, 10, 20, 20]
            opened = picture("opened.png", fills=[((10, 10, 20, 20), (0, 0, 200, 255))])
            blank = picture("blank.png")
            other_size = picture("small.png", size=(20, 20))
            elsewhere = picture("elsewhere.png", fills=[((0, 30, 40, 40), (0, 0, 0, 255))])

            self.assertEqual(analyze.boxes_changed(opened, blank, [box, [100, 100, 120, 120]]), [1.0, -1.0])
            self.assertEqual(analyze.boxes_changed(blank, blank, [box]), [0.0])
            # Two sizes are not two renders of one document: no verdict at all.
            self.assertIsNone(analyze.boxes_changed(opened, other_size, [box]))
            self.assertIsNone(analyze.changed_outside_boxes(opened, other_size, [box]))
            # A change inside the layer's box is what the leg measures...
            self.assertEqual(analyze.changed_outside_boxes(opened, blank, [box]), 0.0)
            # ...and one far outside it means the render is of something else.
            self.assertGreater(analyze.changed_outside_boxes(opened, elsewhere, [box]), 0.05)
            self.assertEqual(analyze.changed_outside_boxes(opened, elsewhere, [[0, 0, 40, 40]]), 0.0)

            scored = root / "scored.png"
            analyze.compose_scored_render(scored, opened, [(box, "x")], clear_placeholders=True)
            pixels = Image.open(scored).convert("RGBA")
            self.assertEqual(pixels.getpixel((15, 15))[3], 0)        # cached pixels pruned
            self.assertEqual(pixels.getpixel((10, 10)), (200, 0, 0, 255))  # outlined
            analyze.compose_scored_render(scored, blank, [], opened, [box])
            self.assertEqual(Image.open(scored).convert("RGBA").getpixel((15, 15)), (0, 0, 200, 255))

    def test_a_blank_text_layer_only_counts_against_editors_known_to_draw_text(self):
        # Photoshop itself shows nothing for a type layer whose cache is gone, so an
        # editor is marked down for one only when its text engine is known to run.
        for editor in ("patchy", "krita", "affinity", "gimp", "psdtools", "photopea", "photodemon",
                       "photocraft"):
            self.assertIn("TEXT", testy.BLANK_IS_FAILURE[editor])
        # PhotoCraft draws nothing from an embedded document's data on open, like Photoshop.
        self.assertNotIn("SMARTOBJECT", testy.BLANK_IS_FAILURE["photocraft"])
        # An editor Testy knows nothing about is never marked down for a blank text layer.
        self.assertEqual(testy.BLANK_IS_FAILURE.get("some-new-editor", ()), ())
        # Patchy's text keeps its cache and is re-rendered by script (layer.rerenderText).
        self.assertIn("patchy", testy.TEXT_CACHE_KEPT)
        script = (ROOT / "testy" / "drivers" / "patchy_text_afresh.js").read_text(encoding="utf-8")
        self.assertIn("layer.rerenderText()", script)
        self.assertEqual(set(testy.NOT_MEASURED_REASON), {"TEXT", "SMARTOBJECT"})
        self.assertNotIn("photoshop", testy.BLANK_IS_FAILURE)

    def test_text_that_is_not_rendered_or_not_saved_scores_zero_with_the_reason(self):
        import manifest

        text = dict(name="Title", kind="TEXT")
        pixels = dict(name="Photo", kind="NORMAL")
        kept = manifest.compare_manifests([text, pixels], [dict(text), dict(pixels)])
        self.assertEqual(kept["nativeScore"], 1.0)
        self.assertNotIn("textNotSaved", kept)
        # The text came back as pixels: half the objects survived, the score is 0.
        lost = manifest.compare_manifests([text, pixels], [dict(name="Title", kind="NORMAL"), dict(pixels)])
        self.assertEqual(lost["nativeScore"], 0.0)
        self.assertEqual(lost["nativeScoreMeasured"], 0.5)
        self.assertEqual(lost["textNotSaved"], {"lost": 1, "total": 1})
        self.assertEqual((lost["nativeKept"], lost["nativeTotal"]), (1, 2))
        self.assertFalse(manifest.apply_text_save_rule(lost))  # applied once
        # A renamed layer is not a lost layer: what the name pass leaves over is paired
        # in stack order, and the kind still has to hold.
        renamed = manifest.compare_manifests(
            [dict(name="Background", kind="NORMAL"), dict(name="Title", kind="TEXT")],
            [dict(name="Фон", kind="NORMAL"), dict(name="Title", kind="TEXT")])
        self.assertEqual((renamed["nativeKept"], renamed["nativeTotal"], renamed["renamedLayers"]), (2, 2, 1))
        self.assertEqual(renamed["lostLayers"], [])
        self.assertEqual(renamed["matching"], manifest.MATCHING_VERSION)
        flattened_text = manifest.compare_manifests([dict(name="Title", kind="TEXT")],
                                                    [dict(name="Layer 0", kind="NORMAL")])
        self.assertEqual(flattened_text["nativeKept"], 0)       # renamed AND rasterized: lost
        dropped = manifest.compare_manifests([dict(name="A", kind="NORMAL"), dict(name="B", kind="NORMAL")],
                                             [dict(name="A", kind="NORMAL")])
        self.assertEqual((dropped["nativeKept"], len(dropped["lostLayers"])), (1, 1))
        # A file with no text is untouched by the rule.
        no_text = manifest.compare_manifests([pixels], [dict(name="Photo", kind="SMARTOBJECT")])
        self.assertNotIn("textNotSaved", no_text)

        rendered = dict(renderMetrics=dict(accuracy=0.9, badFraction=0.1,
                                           perceptual=dict(accuracy=0.95, badFraction=0.05)),
                        noCache=dict(state="done", notRendered=["Logo"], notMeasured=[]))
        self.assertFalse(testy.Runner._apply_text_render_rule(rendered))  # a shape, not text
        blank = copy.deepcopy(rendered)
        blank["noCache"]["textNotRendered"] = ["Title"]
        self.assertTrue(testy.Runner._apply_text_render_rule(blank))
        metrics = blank["renderMetrics"]
        self.assertEqual((metrics["accuracy"], metrics["badFraction"]), (0.0, 1.0))
        self.assertEqual((metrics["perceptual"]["accuracy"], metrics["perceptual"]["badFraction"]), (0.0, 1.0))
        self.assertEqual(metrics["measured"]["perceptualAccuracy"], 0.95)
        self.assertEqual(metrics["textNotRendered"], ["Title"])
        self.assertFalse(testy.Runner._apply_text_render_rule(blank))
        # Not measured is not "cannot render": no zero.
        unmeasured = copy.deepcopy(rendered)
        unmeasured["noCache"]["notMeasured"] = ["Title"]
        self.assertFalse(testy.Runner._apply_text_render_rule(unmeasured))

    def test_photopea_is_handed_the_font_files_the_text_uses(self):
        import config
        import fonts
        from drivers import photopea

        carlito = ROOT / "third_party" / "fonts-web" / "carlito" / "Carlito-Bold.ttf"
        with tempfile.TemporaryDirectory(dir=ROOT / "testy") as temp, \
                mock.patch.object(config, "CACHE_DIR", Path(temp)), \
                mock.patch.object(fonts, "_index", {"Carlito-Bold": (carlito, 0)}):
            served = fonts.font_file("Carlito-Bold")
            self.assertEqual(served.read_bytes(), carlito.read_bytes())
            self.assertIsNone(fonts.font_file("NoSuchFont-Regular"))
            self.assertIsNone(fonts.font_file(""))
            # Names are deduplicated and unknown ones skipped.
            self.assertEqual(fonts.font_files(["Carlito-Bold", "NoSuchFont", "Carlito-Bold"]), [served])
            query = photopea._fonts_query("http://127.0.0.1:1", ROOT / "testy", ["Carlito-Bold", "NoSuchFont"])
            self.assertTrue(query["fonts"].startswith("http://127.0.0.1:1/"))
            self.assertTrue(query["fonts"].endswith("/photopea-fonts/Carlito-Bold.ttf"))
            self.assertEqual(photopea._fonts_query("http://127.0.0.1:1", ROOT / "testy", ["NoSuchFont"]), {})
            self.assertEqual(photopea._fonts_query("http://127.0.0.1:1", ROOT / "testy", None), {})
        host = (ROOT / "testy" / "photopea_host.html").read_text(encoding="utf-8")
        self.assertIn('params.get("fonts")', host)

    def test_krita_render_is_the_most_common_of_its_takes(self):
        from PIL import Image
        from drivers import krita

        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            output = root / "render.png"

            def run(colors):
                """Each scripted export writes the next color; returns (result, launches)."""
                queue = list(colors)
                launches = []

                def fake_script(_runner, _source, take):
                    launches.append(take.name)
                    if not queue:
                        return ""
                    color = queue.pop(0)
                    if color is None:
                        return "failed: export"
                    Image.new("RGBA", (4, 4), color).save(take)
                    return "ok"

                with mock.patch.object(krita, "_export_script", side_effect=fake_script):
                    result = krita._export_png_by_majority(root / "kritarunner.com", root / "in.psd", output)
                return result, launches

            red, blue = (255, 0, 0, 255), (0, 0, 255, 255)
            # Two matching takes settle it: no third launch, nothing varied.
            result, launches = run([red, red, blue])
            self.assertEqual(result, (True, False))
            self.assertEqual(len(launches), 2)
            self.assertEqual(Image.open(output).getpixel((0, 0)), red)
            # Two different takes need a third, and the majority wins.
            result, launches = run([red, blue, blue])
            self.assertEqual(result, (True, True))
            self.assertEqual(len(launches), 3)
            self.assertEqual(Image.open(output).getpixel((0, 0)), blue)
            result, _ = run([blue, red, blue])
            self.assertEqual(Image.open(output).getpixel((0, 0)), blue)
            # The script route failing outright reports failure (the caller falls back).
            output.unlink()
            self.assertEqual(run([None])[0], (False, False))
            self.assertFalse(output.exists())
            self.assertEqual([p.name for p in root.iterdir()], [])

    def test_static_export_is_self_contained_and_names_nothing_local(self):
        import export_static
        from PIL import Image

        with tempfile.TemporaryDirectory() as temp:
            run_dir = Path(temp) / "runs" / "20260101-000000"
            cell_dir = run_dir / "files" / "art" / "gimp"
            cell_dir.mkdir(parents=True)
            Image.new("RGB", (2, 2)).save(cell_dir / "render.png")
            (cell_dir / "resave.psd").write_bytes(b"8BPS")
            (cell_dir / "nocache_plain.png").write_bytes(b"not linked")
            corpus = str(ROOT / "local-test-fixtures" / "set")
            status = {
                "state": "done",
                "run": {"name": "20260101-000000", "editorOrder": ["gimp"], "startedAt": "a", "finishedAt": "b"},
                "editors": {"gimp": {"displayName": "GIMP", "version": "3.2"}},
                "files": [
                    {"name": "art.psd", "source": corpus + "\\layers\\art.psd",
                     "groundTruth": {"state": "done", "artifacts": {}},
                     "cells": {"gimp": {"state": "failed",
                                        "error": "could not open (" + str(run_dir / "files" / "art") + "/x.psd)",
                                        "artifacts": {"render": "files/art/gimp/render.png",
                                                      "resavePsd": "files/art/gimp/resave.psd",
                                                      "gone": "files/art/gimp/missing.png"}}}},
                    {"name": "b.psd", "source": corpus + "\\masks\\b.psd", "cells": {}},
                ],
            }
            (run_dir / "status.json").write_text(json.dumps(status), encoding="utf-8")
            (run_dir / "report.html").write_text("old page", encoding="utf-8")
            out = Path(temp) / "public"
            summary = export_static.export_run(run_dir, out, "A title <here>")
            self.assertEqual((summary["images"], summary["missing"], summary["files"]), (1, 1, 2))
            exported = json.loads((out / "status.json").read_text(encoding="utf-8"))
            # Sources keep the folder below the common one, so "By folder" still groups.
            self.assertEqual([f["source"] for f in exported["files"]], ["set/layers/art.psd", "set/masks/b.psd"])
            cell = exported["files"][0]["cells"]["gimp"]
            self.assertNotIn("resavePsd", cell["artifacts"])
            self.assertIn("<run>", cell["error"])
            self.assertEqual(export_static.private_strings(exported), [])
            names = sorted(p.relative_to(out).as_posix() for p in out.rglob("*") if p.is_file())
            self.assertEqual(names, ["files/art/gimp/render.png", "index.html", "report.html", "status.json",
                                     "testy-export.txt"])
            self.assertIn("A title &lt;here&gt;", (out / "index.html").read_text(encoding="utf-8"))
            self.assertIn('href="report.html"', (out / "index.html").read_text(encoding="utf-8"))
            # The page is the current one, which stops polling and links back to the index.
            page = (out / "report.html").read_text(encoding="utf-8")
            self.assertIn('back.href = "index.html"', page)
            self.assertIn("if (!RUN_ID && S && S.state !== \"running\") return;", page)
            # It replaces its own earlier output, and nothing else.
            export_static.export_run(run_dir, out, "Again")
            other = Path(temp) / "mine"
            other.mkdir()
            (other / "keep.txt").write_text("x", encoding="utf-8")
            with self.assertRaises(SystemExit):
                export_static.export_run(run_dir, other, "No")
            self.assertTrue((other / "keep.txt").exists())
            # A leftover local path anywhere stops the export.
            status["editors"]["gimp"]["notes"] = [str(Path.home() / "Desktop" / "x")]
            self.assertTrue(export_static.private_strings(status))

    def test_reference_cache_key_follows_what_the_reference_re_renders(self):
        import psd_sections

        self.assertEqual(testy.reference_space_key({"depth": 8, "mode": 3}), "")
        self.assertEqual(testy.reference_space_key({"depth": 8, "mode": 3, "text": True}), "-freshtext2")
        self.assertEqual(testy.reference_space_key({"depth": 8, "mode": 3, "smart": True}), "-freshsmart1")
        self.assertEqual(testy.reference_space_key({"depth": 16, "mode": 3, "text": True, "smart": True}),
                         "-srgb1-freshtext2-freshsmart1-deep1")
        # Only 16-bit files get the 16-bit renders, so only they are measured afresh.
        self.assertEqual(testy.reference_space_key({"depth": 32, "mode": 3}), "-srgb1")
        # A linked smart object has nothing in the file to render from: never stripped.
        self.assertNotIn(b"SoLE", psd_sections.CACHED_LAYER_KEYS["smart"])
        self.assertIn(b"SoLd", psd_sections.CACHED_LAYER_KEYS["smart"])

    def test_failed_build_is_not_success_even_with_compile_output(self):
        with mock.patch.object(testy.config, 'REPO_ROOT', self.runs), \
             mock.patch.object(testy.config, 'BUILD_COMMAND', 'cmake --build --preset release'), \
             mock.patch.object(testy.subprocess, 'run', return_value=mock.Mock(returncode=-1,
                 stdout='Building CXX object foo', stderr='link failed')) as process:
            self.assertFalse(testy.refresh_patchy_build())
            self.assertIn('run-throttled.bat', process.call_args.args[0][2])
            self.assertEqual(process.call_args.kwargs['env']['CMAKE_BUILD_PARALLEL_LEVEL'], '20')

    def test_missing_fonts_remove_cached_text_score_without_altering_other_metrics(self):
        cached = cell(.8)
        cached.update(textRender={'accuracy': .1}, mutateError='old failure')
        cached['artifacts'].update(mutated='old.png', mutatedThumb='thumb.png')
        testy.Runner._skip_unavailable_text_comparison(cached, {'mutateSkipped': 'Required fonts unavailable: Example'})
        self.assertNotIn('textRender', cached)
        self.assertNotIn('mutateError', cached)
        self.assertNotIn('mutated', cached['artifacts'])
        self.assertEqual(cached['renderMetrics']['accuracy'], .8)
        self.assertEqual(cached['textRenderSkipped'], 'Required fonts unavailable: Example')
        # The retired appended-text leg's leftovers go even when no font is missing,
        # and its other skip reason ("could not edit every layer") no longer means anything.
        stale = cell(.8)
        stale.update(textRender={'accuracy': .1})
        testy.Runner._skip_unavailable_text_comparison(
            stale, {'mutateSkipped': 'Photoshop could not edit every eligible text layer'})
        self.assertNotIn('textRender', stale)
        self.assertNotIn('textRenderSkipped', stale)
        self.assertEqual(testy.text_fonts_missing({'textFontsMissing': 'Required fonts unavailable: A'}),
                         'Required fonts unavailable: A')
        self.assertIsNone(testy.text_fonts_missing({'textFontsMissing': None}))
        self.assertEqual(testy.TEXT_RENDER_BASIS['patchy'][0], 'open')
        self.assertNotIn('mutate', photoshop._PROBE_JSX)

    def test_report_javascript_and_photoshop_font_preflight(self):
        script = report._PAGE.split('<script>', 1)[1].split('</script>', 1)[0]
        js_file = self.runs/'report.js'
        js_file.write_text(script, encoding='utf-8')
        subprocess.run(['node', '--check', str(js_file)], check=True, capture_output=True)
        preflight = photoshop._PROBE_JSX.split('  function textFontProblems', 1)[1].split('  var opened = null;', 1)[0]
        preflight = 'function textFontProblems' + preflight
        controls = script.split('function rerunRowControls', 1)[1].split('function render()', 1)[0]
        controls = 'function rerunRowControls' + controls
        fonts = photoshop._PROBE_JSX.split('    var missingFonts = freshFonts;', 1)[1].split('    var missingJson', 1)[0]
        fonts = 'var missingFonts = freshFonts;' + fonts
        test_js = """
const assert = require('node:assert/strict');
const stringIDToTypeID = x => x;
const app = { fonts: {getByName(face) { if (face !== 'Installed') throw Error('missing'); }} };
function desc(value) {return {hasKey:k=>k in value, getString:k=>value[k], getBoolean:k=>value[k], getObjectValue:k=>desc(value[k]), getList:k=>({count:value[k].length,getObjectValue:i=>desc(value[k][i])})};}
const styles = [{fontPostScriptName:'Installed'}, {fontPostScriptName:'MissingLaterRange'}];
const layerDescriptor = () => desc({textKey:{textStyleRange:styles.map(textStyle=>({textStyle}))}});
const layer = {id:1,name:'Mixed styles',kind:'LayerKind.TEXT',textItem:{font:'Installed'}};
""" + preflight + """
let missing=[]; textFontProblems([layer],missing); assert.deepEqual(missing,['MissingLaterRange']);
styles.pop(); missing=[]; textFontProblems([layer],missing); assert.deepEqual(missing,[]);
styles[0].fontAvailable=false; missing=[]; textFontProblems([layer],missing); assert.deepEqual(missing,['Installed']);
missing=[]; textFontProblems([{typename:'LayerSet',layers:[{...layer,allLocked:true}]}],missing); assert.deepEqual(missing,[]);
let RUN_ID='batch', runState={running:false}, S={state:'done',run:{editorOrder:['patchy','photoshop']}}, rowRerunState=null,rowRerunPending=null,rowRerunError='';
const rowRerunScopes={}; const pct=(x,d)=>(100*x).toFixed(d===undefined?1:d)+'%'; const esc=s=>String(s).replaceAll('<','&lt;').replaceAll('"','&quot;');
""" + controls + """
let markup=rerunRowControls({source:'one',name:'Image <one>'},0); assert(markup.includes('Rerun</button>')); assert(markup.includes('value="patchy" selected')); assert(!markup.includes(' disabled'));
runState.running=true; assert(rerunRowControls({source:'one',name:'Image'},0).includes(' disabled'));
rowRerunState={state:'running',source:'one'}; assert(rerunRowControls({source:'one',name:'Image'},0).includes('Rerunning...'));
runState=null; assert.equal(rerunRowControls({source:'one',name:'Image'},0),'');
"""
        test_js += """
function fontVerdict(freshFonts) {
""" + fonts + """
return textFontsMissing;
}
assert.equal(fontVerdict([]), null);
assert.equal(fontVerdict(['Installed','Other']), 'Required fonts unavailable: Installed, Other');
"""
        rollup = script.split('const TOP_GROUP', 1)[1].split('let groupFilter', 1)[0]
        test_js += 'const TOP_GROUP' + rollup + r"""
const groupFiles = [
  {source:'D:\\c\\psd_files\\a.psd', cells:{patchy:{state:'done',opens:'ok',bad:0.02,native:{nativeScore:1}}}},
  {source:'D:\\c\\psd_files\\fx\\b.psd', cells:{patchy:{state:'done',opens:'ok',bad:0.5,resaveRejected:true,native:{nativeScore:0.5}}}},
  {source:'D:/c/psd_files/fx/c.psb', cells:{patchy:{state:'failed',opens:'fail'}}},
  {source:'D:\\c\\psd_files\\fx\\deep\\d.psd', cells:{patchy:{state:'pending'}}},
];
const groupNames = fileGroups(groupFiles);
assert.deepEqual(groupNames, [TOP_GROUP,'fx','fx','fx']);
assert.deepEqual(fileGroups([{source:'D:\\c\\one.psd'},{source:'D:\\c\\two.psd'}]), [TOP_GROUP,TOP_GROUP]);
const rolled = groupRollup(groupFiles, groupNames, ['patchy'], c => c.bad == null ? null : c.bad, 0.10);
assert.deepEqual(rolled[TOP_GROUP].editors.patchy, {total:1,opened:1,matched:1,compared:1,badSaves:0,native:[1]});
assert.equal(rolled.fx.files, 3);
assert.deepEqual(rolled.fx.editors.patchy, {total:2,opened:1,matched:0,compared:1,badSaves:1,native:[0.5]});
"""
        known = script.split('function knownLimit', 1)[1].split('let skipKnown', 1)[0]
        test_js += 'function knownLimit' + known + """
assert.equal(knownLimit({}), '');
assert.equal(knownLimit({traits:{depth:8,mode:3}}), '');
assert.equal(knownLimit({traits:{depth:1,mode:0}}), '');
assert.equal(knownLimit({traits:{depth:16,mode:3}}), '16-bit');
assert.equal(knownLimit({traits:{depth:32,mode:3,artboards:true}}), '32-bit, artboards');
assert.equal(knownLimit({traits:{depth:8,mode:3,artboards:true}}), 'artboards');
"""
        scoring = script.split('function refusedWithReference', 1)[1].split('let groupFilter', 1)[0]
        test_js += 'function refusedWithReference' + scoring + """
const gtOk = {state:'done', artifacts:{render:'r.png'}};
assert.equal(refusedWithReference({groundTruth:gtOk}, {state:'failed', opens:'fail'}), true);
assert.equal(refusedWithReference({groundTruth:gtOk}, {state:'failed', error:'timeout'}), false);
assert.equal(refusedWithReference({groundTruth:{state:'done', artifacts:{}}}, {state:'failed', opens:'fail'}), false);
const legCell = state => ({cells:{gimp:{noCache:state}}});
assert.equal(replayNote({cells:{gimp:{}}}, 'gimp'), '');
assert.equal(replayNote(legCell({state:'done', cachedLayers:3, notRendered:[], notMeasured:[]}), 'gimp'), '');
assert(replayNote(legCell({state:'done', cachedLayers:3, notRendered:['Title','Logo'], notMeasured:[]}), 'gimp').includes('cannot render 2 of 3'));
assert(replayNote(legCell({state:'done', cachedLayers:3, notRendered:[], notMeasured:['Title']}), 'gimp').includes('1 layer(s) not measured'));
assert.equal(textZeroReasons({renderMetrics:{accuracy:0.9}, native:{nativeScore:1}}).length, 0);
const textStanding = psdTextStanding({
  patchy:{scores:[0.9,0.7], files:2, noRender:0, noSave:0},
  affinity:{scores:[0,0], files:2, noRender:0, noSave:2},
  gimp:{scores:[0,0], files:2, noRender:2, noSave:2},
  krita:{scores:[0.6,0], files:2, noRender:0, noSave:1},
  photoshop:{scores:[1], files:1, noRender:0, noSave:0}, photopea:{scores:[], files:0, noRender:0, noSave:0}},
  ['photoshop','patchy','krita','gimp','affinity','photopea'], {affinity:'Affinity', gimp:'GIMP', krita:'Krita', patchy:'Patchy'});
assert.deepEqual(textStanding.rows.map(r => r.key), ['patchy','krita','affinity','gimp']);
assert.equal(textStanding.rows[0].label, '80%');
assert.equal(textStanding.rows[1].label, '30% *');
assert.equal(textStanding.rows[2].label, '0% (FAIL ***)');
assert.equal(textStanding.rows[3].label, '0% (FAIL ** ***)');
assert(textStanding.rows[2].failed && !textStanding.rows[1].failed);
assert.deepEqual(textStanding.rows[2].reasons, ['Cannot save text objects back out into the .psd as text']);
assert.equal(textStanding.rows[3].reasons.length, 2); assert.equal(textStanding.rows[0].reasons.length, 0);
const helped = psdTextStanding({photopea:{scores:[0.96], files:1, noRender:0, noSave:0}, patchy:{scores:[0.9], files:1, noRender:0, noSave:0}},
  ['patchy','photopea'], {}, {photopea:'Photopea is fed the correct fonts'});
assert.equal(helped.rows[0].key, 'photopea'); assert.equal(helped.rows[0].label, '96% †');
assert.deepEqual(helped.rows[0].reasons, ['Photopea is fed the correct fonts']);
assert.deepEqual(helped.notes, [{mark:'†', text:'Photopea is fed the correct fonts'}]);
assert.equal(helped.rows[1].label, '90%');
const noteFor = mark => textStanding.notes.find(n => n.mark === mark).text;
assert.equal(noteFor('***'), 'Cannot save text objects back out into the .psd as text');
assert.equal(noteFor('**'), 'Cannot render psd text objects, only uses the baked pixels saved in the file');
assert(noteFor('*').includes('Krita did not save the text objects back into the .psd as text in 1 of 2 files'));
const zeroed = textZeroReasons({renderMetrics:{accuracy:0, textNotRendered:['Title'], measured:{accuracy:0.9, perceptualAccuracy:0.95}},
  native:{nativeScore:0, nativeScoreMeasured:0.5, nativeKept:1, nativeTotal:2, textNotSaved:{lost:1, total:1}}});
assert.equal(zeroed.length, 2);
assert(zeroed[0].long.includes('cannot render 1 Photoshop text object(s)') && zeroed[0].long.includes('Title'));
assert(zeroed[0].long.includes('can only show the pixels Photoshop cached'));
assert(zeroed[1].long.includes('1 of 1 Photoshop text object(s) did not come back as text'));
assert(zeroed[1].long.includes('1/2'));
assert(replayNote(legCell({state:'not measured', reason:'could not open'}), 'gimp').includes('not measured'));
"""
        losses = script.split('const LOSS_LABELS', 1)[1].split('function cellSummary', 1)[0]
        test_js += 'const LOSS_LABELS' + losses + """
const fullKeep = {perCategory:{raster:{kept:2,total:2}}, attributes:{blend:{kept:2,total:2}}};
assert.deepEqual(cellLosses({native:fullKeep}), []);
assert.deepEqual(cellLosses({native:fullKeep, saveDepth:{original:16,resaved:16,kept:true}}), []);
assert.equal(lossText(cellLosses({native:fullKeep, saveDepth:{original:16,resaved:8,kept:false}})),
  '1/1 bit depth (16 to 8-bit)');
assert.equal(cellLosses({saveDepth:{original:32,resaved:8,kept:false}}).length, 1);
"""
        standing = script.split('function standingRows', 1)[1].split('let groupFilter', 1)[0]
        test_js += 'function standingRows' + standing + """
assert.deepEqual(standingRows({photoshop:1, patchy:0.79, krita:0.37, photopea:0.88, gimp:null},
  ['photoshop','patchy','krita','gimp','photopea']).map(r => r.key), ['photopea','patchy','krita']);
"""
        js_file.write_text(test_js, encoding='utf-8')
        result = subprocess.run(['node', str(js_file)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main(verbosity=2)
