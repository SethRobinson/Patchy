"""Testy: a driver failure on the harness side carries no verdict on the file.

A Photopea or Affinity leg that fails because of Testy itself (a driver crash, a dead
browser, a host page that never got the file into Photopea) must not count as the editor
refusing the file, which would score it 0% in every average. No editors are launched."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'testy'))
import testy
from drivers import photopea


class FakeChrome:
    """The host page as the driver polls it: a final result, or a log that stops moving."""
    def __init__(self, result=None, log=''):
        self.result, self.log = result, log

    def get(self, url):
        pass

    def execute_script(self, script):
        if '__testyResult' in script:
            return json.dumps(self.result) if self.result is not None else None
        return self.log


def run_photopea(chrome):
    with tempfile.TemporaryDirectory(prefix='testy-pea-', dir=ROOT / 'build/test-output') as temp:
        root = Path(temp)
        original = root / 'a.psd'
        original.write_bytes(b'8BPS')
        with mock.patch.object(photopea, '_chrome', return_value=chrome), \
                mock.patch.object(photopea.time, 'sleep'), \
                mock.patch.object(photopea, 'STALL_TIMEOUT', -1):
            return photopea.export_all('http://127.0.0.1:1', root, original, None,
                                       root / 'render.png', root / 'resave.psd', root / 'trap.png')


def scored_as_refusal(result):
    cell = {}
    testy.Runner._record_driver_failure(cell, result, 'failed')
    entry = {'groundTruth': {'state': 'done', 'artifacts': {'render': 'files/a/photoshop/render.png'}}}
    return cell, testy.refused_with_reference(entry, cell)


class DriverFailureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        (ROOT / 'build/test-output').mkdir(parents=True, exist_ok=True)

    def test_driver_crash_is_not_a_refusal(self):
        with mock.patch.object(photopea, '_chrome', side_effect=ModuleNotFoundError("No module named 'fontTools'")):
            result = run_photopea(None)
        self.assertFalse(result['ok'])
        self.assertIsNone(result['opens'])
        cell, refused = scored_as_refusal(result)
        self.assertEqual(cell['state'], 'failed')
        self.assertNotIn('opens', cell)
        self.assertFalse(refused)

    def test_host_page_harness_errors_are_not_refusals(self):
        for error in ('file fetch failed: 404', 'upload failed for x: 500',
                      'Photopea never became ready (offline? file failed to load?)'):
            with self.subTest(error=error):
                result = run_photopea(FakeChrome(result={'ok': False, 'error': error}))
                self.assertIsNone(result['opens'])
                self.assertFalse(scored_as_refusal(result)[1])

    def test_photopea_failing_on_the_file_is_a_refusal(self):
        result = run_photopea(FakeChrome(result={'ok': False, 'error': 'file open timed out'}))
        self.assertEqual(result['opens'], 'fail')
        cell, refused = scored_as_refusal(result)
        self.assertEqual(cell['opens'], 'fail')
        self.assertTrue(refused)

    def test_stall_is_judged_by_the_step_it_stopped_at(self):
        before = run_photopea(FakeChrome(log='waiting for the Photopea editor'))
        self.assertIsNone(before['opens'])
        inside = run_photopea(FakeChrome(log='editor ready; fetching x\nposting 1906 bytes into Photopea'))
        self.assertEqual(inside['opens'], 'fail')
        self.assertIn('stalled', inside['error'])

    def test_driver_that_gives_no_verdict_still_counts_as_fail(self):
        cell, refused = scored_as_refusal({'ok': False, 'error': 'x'})
        self.assertEqual(cell['opens'], 'fail')
        self.assertTrue(refused)


if __name__ == '__main__':
    unittest.main(verbosity=2)
