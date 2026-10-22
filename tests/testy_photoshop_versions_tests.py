"""Testy's reopen checks in more than one Photoshop version. No Photoshop is launched:
the version drivers are fakes, and the remote agent runs in-process over a temporary
folder standing in for the host's share."""
import json
from pathlib import Path
import sys
import tempfile
import threading
import types
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'testy'))
import config
import photoshop_agent
import testy
from drivers import photoshop
from drivers import photoshop_remote

LAYERS = [dict(path='0', name='Title', kind='TEXT', group=False),
          dict(path='1', name='Background', kind='NORMAL', group=False)]
RASTERIZED = [dict(LAYERS[0], kind='NORMAL'), LAYERS[1]]
REFERENCE = photoshop.PhotoshopInstall('2026', 'Photoshop.Application', None)
OLDER = photoshop.PhotoshopInstall('2025', 'Photoshop.Application.190', None)


class FakeDriver:
    """Answers probe() from a table keyed by file name and counts the probes."""

    def __init__(self, answers):
        self.answers = answers
        self.probes = []
        self.unavailable = False
        self.unavailable_reason = None
        self.quit_called = False

    def probe(self, path, render_png=None, resave_psd=None):
        self.probes.append(Path(path).name)
        return dict(self.answers[Path(path).name])

    def version(self):
        return '26.9.0.1'

    def quit(self):
        self.quit_called = True


class VersionChecks(unittest.TestCase):
    def setUp(self):
        scratch = ROOT / 'build/test-output'
        scratch.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix='testy-psversions-', dir=scratch)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        cache = self.root / 'cache'
        cache.mkdir()
        patcher = mock.patch.object(config, 'CACHE_DIR', cache)
        patcher.start()
        self.addCleanup(patcher.stop)
        self.original = self.root / 'original.psd'
        self.original.write_bytes(b'original')
        self.staged = types.SimpleNamespace(original=self.original, sha1='a' * 40)

    def runner(self, answers, checks=(OLDER,)):
        runner = object.__new__(testy.Runner)
        runner.args = types.SimpleNamespace(fresh=False)
        runner.suffix = 'test'
        runner.run_name = 'run'
        runner.patchy_hash = 'test'
        runner.run_dir = self.root
        runner.files_dir = self.root / 'files'
        runner.editor_order = ['patchy']
        runner.ps_reference = REFERENCE
        runner.ps_checks = list(checks)
        runner.ps_reference_chosen = False
        runner._check_drivers = {install.label: FakeDriver(answers) for install in checks}
        runner._check_versions = {install.label: '26.9.0.1' for install in checks}
        runner._reopen_memo = {}
        runner.push = lambda: None
        cell_dir = runner.files_dir / 'original' / 'patchy'
        cell_dir.mkdir(parents=True, exist_ok=True)
        (cell_dir / 'resave.psd').write_bytes(b'resave by patchy')
        entry = dict(name='original.psd', source=str(self.original), groundTruth=dict(state='done'),
                     cells=dict(patchy=dict(state='done', opens='ok',
                                            native=dict(nativeScore=1.0, nativeKept=2, nativeTotal=2))))
        runner.status = dict(state='running', run=dict(name='run', editorOrder=['patchy'],
                                                       scan=dict(thresholdPct=10), compare='strict'),
                             editors=dict(patchy=dict(displayName='Patchy')), files=[entry])
        record = runner._photoshop_run_record()
        if record is not None:
            runner.status['run']['photoshop'] = record
        return runner

    def check(self, runner):
        runner._check_original_in_versions(0, self.staged, {'layers': LAYERS})
        runner._check_resave_in_versions(0, 'patchy', self.staged)
        return runner.status['files'][0]

    def test_a_single_version_run_records_and_counts_exactly_as_before(self):
        runner = self.runner({}, checks=())
        self.assertNotIn('photoshop', runner.status['run'])
        entry = self.check(runner)
        self.assertNotIn('psVersions', entry)
        self.assertNotIn('resaveChecks', entry['cells']['patchy'])
        entry['cells']['patchy']['resaveRejected'] = True
        aggregate = runner._aggregate()['patchy']
        self.assertEqual(aggregate['badSaves'], 1)
        self.assertNotIn('badSavesBy', aggregate)
        self.assertNotIn('photoshop', runner._history_summary())
        reasons = testy.Runner.scan_reasons_for_status(runner.status, entry)
        self.assertIn('Patchy: resave rejected by Photoshop', reasons)

    def test_a_resave_one_version_refuses_is_a_bad_save_named_for_that_version(self):
        crash = dict(ok=False, crashed=True, fileRejected=True,
                     error='Photoshop crashed while opening the file (com-error: ...)')
        runner = self.runner({'original.psd': dict(ok=True, layers=LAYERS), 'resave.psd': crash})
        entry = self.check(runner)
        self.assertEqual(entry['psVersions']['2025']['state'], 'ok')
        verdict = entry['cells']['patchy']['resaveChecks']['2025']
        self.assertEqual(verdict['state'], 'rejected')
        self.assertTrue(verdict['crashed'])
        self.assertEqual(testy.resave_rejected_by(entry['cells']['patchy'], runner.status['run']['photoshop']),
                         ['2025'])
        aggregate = runner._aggregate()['patchy']
        self.assertEqual(aggregate['badSaves'], 1)
        self.assertEqual(aggregate['badSavesBy'], {'2026': 0, '2025': 1})
        self.assertEqual(runner._history_summary()['photoshop'], {'reference': '2026', 'checks': ['2025']})
        reasons = testy.Runner.scan_reasons_for_status(runner.status, entry)
        self.assertIn('Patchy: resave rejected by Photoshop 2025 (it crashed)', reasons)

    def test_a_version_that_cannot_open_the_original_does_not_judge_the_resave(self):
        runner = self.runner({'original.psd': dict(ok=False, fileRejected=True, error='too new'),
                              'resave.psd': dict(ok=False, error='never asked')})
        entry = self.check(runner)
        self.assertEqual(entry['psVersions']['2025']['state'], 'rejected')
        verdict = entry['cells']['patchy']['resaveChecks']['2025']
        self.assertEqual(verdict['state'], 'not checked')
        self.assertIn('could not open the original', verdict['reason'])
        self.assertEqual(runner._aggregate()['patchy']['badSaves'], 0)
        self.assertEqual(runner._check_drivers['2025'].probes, ['original.psd'])

    def test_data_kept_is_measured_against_that_versions_own_original(self):
        runner = self.runner({'original.psd': dict(ok=True, layers=LAYERS),
                              'resave.psd': dict(ok=True, layers=RASTERIZED)})
        verdict = self.check(runner)['cells']['patchy']['resaveChecks']['2025']
        self.assertEqual(verdict['state'], 'ok')
        self.assertEqual((verdict['nativeKept'], verdict['nativeTotal']), (1, 2))

    def test_verdicts_are_cached_by_file_and_version_but_a_version_that_never_ran_is_not(self):
        answers = {'original.psd': dict(ok=True, layers=LAYERS), 'resave.psd': dict(ok=True, layers=LAYERS)}
        self.check(self.runner(answers))
        again = self.runner(answers)
        self.check(again)
        self.assertEqual(again._check_drivers['2025'].probes, [])
        down = self.runner({'original.psd': dict(ok=False, agentFailure=True, error='host down')})
        down.args.fresh = True
        entry = self.check(down)
        self.assertEqual(entry['psVersions']['2025']['state'], 'error')
        self.assertEqual(entry['cells']['patchy']['resaveChecks']['2025']['state'], 'not checked')
        self.assertEqual(testy.reopen_verdict(dict(ok=False, launchFailure=True, error='x'))['state'], 'error')
        cached = sorted(p.name for p in config.CACHE_DIR.iterdir())
        self.assertEqual(len(cached), 2, cached)  # the two good verdicts only

    def test_the_default_reference_keeps_the_configured_progid(self):
        installed = photoshop.PhotoshopInstall('2026', 'Photoshop.Application.200', Path('C:/PS 2026/Photoshop.exe'))
        with mock.patch.object(photoshop, 'current_progid', return_value='Photoshop.Application.200'), \
                mock.patch.object(config, 'PHOTOSHOP', {}):
            reference = config.photoshop_reference({'2026': installed})
        self.assertEqual((reference.label, reference.progid, reference.exe),
                         ('2026', 'Photoshop.Application', installed.exe))
        with mock.patch.object(config, 'PHOTOSHOP', {}):
            with self.assertRaises(SystemExit):
                config.photoshop_reference({'2026': installed}, '2024')

    def test_a_run_can_use_an_older_photoshop_alone(self):
        older = photoshop.PhotoshopInstall('2025', 'Photoshop.Application.190', Path('C:/PS 2025/Photoshop.exe'))
        newer = photoshop.PhotoshopInstall('2026', 'Photoshop.Application.200', Path('C:/PS 2026/Photoshop.exe'))
        args = types.SimpleNamespace(photoshop_reference='2025', photoshop=None)
        runner = object.__new__(testy.Runner)
        runner.resume = False
        runner.status = {}
        with mock.patch.object(config, 'photoshop_installs', return_value={'2025': older, '2026': newer}), \
                mock.patch.object(config, 'PHOTOSHOP', {}):
            runner._setup_photoshop_versions(args)
        self.assertEqual(runner.ps_reference, older)
        self.assertEqual(runner.ps_checks, [])
        driver = photoshop.PhotoshopDriver(install=runner.ps_reference)
        self.assertEqual((driver.install.progid, driver._exe), (older.progid, older.exe))
        with mock.patch.object(photoshop, 'exe_version', return_value='26.9.0.1'):
            record = runner._photoshop_run_record()
        self.assertEqual((record['reference'], record['referenceChosen'], record['checks']), ('2025', True, []))

    def test_configured_installs_add_a_remote_version(self):
        local = photoshop.PhotoshopInstall('2026', 'Photoshop.Application.200', Path('C:/PS/Photoshop.exe'))
        settings = {'installs': [{'label': '2025', 'host': 'box'}, {'label': 'x', 'host': 'nowhere'}],
                    'hosts': {'box': {'share': 'C:/share'}}}
        with mock.patch.object(photoshop, 'discover_installs', return_value=[local]), \
                mock.patch.object(config, 'PHOTOSHOP', settings), mock.patch('builtins.print'):
            installs = config.photoshop_installs()
        self.assertEqual(sorted(installs), ['2025', '2026'])
        self.assertEqual((installs['2025'].host, installs['2025'].progid), ('box', '2025'))


class LocalizedPhotoshopText(unittest.TestCase):
    """A Japanese Photoshop's alerts and errors on a console in a legacy code page (the
    console session of a scheduled task): logging them must never fail the probe."""

    ALERT = 'Adobe Photoshop: "ファイルを開けませんでした。不明なデータが含まれています。" [OK]'

    def cp1252_stdout(self):
        import io
        buffer = io.BytesIO()
        return buffer, io.TextIOWrapper(buffer, encoding='cp1252', errors='strict', write_through=True)

    def test_log_survives_text_its_console_cannot_encode(self):
        buffer, stream = self.cp1252_stdout()
        with mock.patch.object(sys, 'stdout', stream):
            testy.log(self.ALERT)
        self.assertIn(b'\\u30d5', buffer.getvalue())

    def test_a_dismissed_localized_alert_does_not_turn_a_good_probe_into_a_rejection(self):
        import win_dialogs

        class QuietGuard(win_dialogs.DialogGuard):
            """No watcher thread (nothing touches a real Photoshop); one alert already
            dismissed, as the probe that hit this had."""

            def start(self):
                self._pending[0] = LocalizedPhotoshopText.ALERT  # hwnd 0 is never a window

        app = types.SimpleNamespace(DoJavaScript=lambda _jsx: '{"ok":true,"layers":[]}')
        driver = photoshop.PhotoshopDriver(log=testy.log, install=OLDER)
        driver._application = lambda: app
        buffer, stream = self.cp1252_stdout()
        with mock.patch.object(photoshop.win_dialogs, 'DialogGuard', QuietGuard), \
                mock.patch.object(sys, 'stdout', stream):
            result = driver._probe_once(Path('issue397.psd'), None, None)
        self.assertTrue(result['ok'], result)
        self.assertEqual(result['dialogs'], [self.ALERT])

    def test_a_localized_com_error_keeps_its_text(self):
        error = Exception(-2147352567, '例外が発生しました。', (0, 'Adobe Photoshop', 'エラー 8800', None, 0, -2147220261), None)
        text = photoshop.PhotoshopDriver._failure_text(
            types.SimpleNamespace(timed_out=False, blocked=[], dismissed=[]), error)
        self.assertIn('エラー 8800', text)
        buffer, stream = self.cp1252_stdout()
        with mock.patch.object(sys, 'stdout', stream):
            testy.log(text)


class RemoteAgentProtocol(unittest.TestCase):
    """The client and the agent over a folder, with the agent in a thread here."""

    def setUp(self):
        scratch = ROOT / 'build/test-output'
        scratch.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix='testy-psagent-', dir=scratch)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_a_probe_round_trips_and_quit_leaves_only_the_log(self):
        share = self.root / 'share'
        share.mkdir()
        source = self.root / 'sample 日本.psd'
        source.write_bytes(b'psd bytes')
        fake = FakeDriver({})
        fake.install = OLDER
        seen = {}

        def answer(path, render_png=None, resave_psd=None):
            seen['bytes'] = Path(path).read_bytes()
            return dict(ok=False, fileRejected=True, error=f'could not open {path}')

        fake.probe = answer

        class InProcessAgent(photoshop_agent.Agent):
            def find_installs(self):
                self.installs = [OLDER]
                self.install_list = [dict(label='2025', progid=OLDER.progid, version='26.9.0.1')]

            def driver_for(self, spec):
                return fake if spec in (OLDER.progid, OLDER.label) else None

        threads = []

        class Client(photoshop_remote.RemotePhotoshop):
            def _launch_local_agent(self):
                thread = threading.Thread(target=InProcessAgent(self.share).serve, daemon=True)
                thread.start()
                threads.append(thread)

        install = photoshop.PhotoshopInstall('2025', OLDER.progid, None, host='box')
        client = Client(install, {'ssh': '', 'share': str(share)}, 'run1')
        self.assertEqual(client.version(), '26.9.0.1')
        result = client.probe(source, None)
        self.assertEqual(seen['bytes'], b'psd bytes')
        self.assertFalse(result['ok'])
        self.assertEqual(result['error'], 'could not open ' + source.name)
        self.assertEqual(testy.reopen_verdict(result)['state'], 'rejected')
        run_folder = share / 'testy-run1'
        self.assertEqual(list((run_folder / 'inbox').iterdir()), [])
        self.assertEqual(list((run_folder / 'jobs').iterdir()), [])
        client.quit()
        threads[0].join(timeout=10)
        self.assertFalse(threads[0].is_alive())
        self.assertEqual(sorted(p.name for p in run_folder.iterdir()), ['agent.log'])
        with self.assertRaises(ValueError):
            client.probe(source, self.root / 'render.png')


if __name__ == '__main__':
    unittest.main()
