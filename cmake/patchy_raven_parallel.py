"""Runs one of Raven's provisioning scripts with a compiler thread per logical CPU.

Raven's scripts/provision-*.py build their guest components with
ThreadPoolExecutor(max_workers=2); this starts the script unchanged (third_party/raven
stays a clean submodule) with ThreadPoolExecutor sized to os.cpu_count(). It only changes
how fast the components build, not what they contain. Used by cmake/patchy_raven.cmake:
    python3 patchy_raven_parallel.py <script.py> [script arguments...]
"""

import concurrent.futures
import os
import runpy
import sys


class _AllCores(concurrent.futures.ThreadPoolExecutor):
    def __init__(self, max_workers=None, *args, **kwargs):
        super().__init__(max(max_workers or 0, os.cpu_count() or 2), *args, **kwargs)


concurrent.futures.ThreadPoolExecutor = _AllCores
script = sys.argv[1]
sys.argv = sys.argv[1:]
runpy.run_path(script, run_name="__main__")
