"""Windows process helpers shared by the CLI drivers."""

from __future__ import annotations

import contextlib
import ctypes

_SEM_FAILCRITICALERRORS = 0x0001
_SEM_NOGPFAULTERRORBOX = 0x0002
_SEM_NOOPENFILEERRORBOX = 0x8000
_QUIET = _SEM_FAILCRITICALERRORS | _SEM_NOGPFAULTERRORBOX | _SEM_NOOPENFILEERRORBOX


@contextlib.contextmanager
def suppressed_error_dialogs():
    """Child processes spawned inside this context inherit an error mode with
    Windows Error Reporting dialogs disabled. Without it, a crashing editor sits
    at "<app> has stopped working" until a human clicks the dialog away, so the
    cell burns its whole timeout and the run needs baby-sitting (PhotoDemon,
    July 2026). The error mode is process-wide on the parent while the context
    is held, so keep it wrapped around just the Popen/run call."""
    kernel32 = ctypes.windll.kernel32
    previous = kernel32.SetErrorMode(_QUIET)
    try:
        yield
    finally:
        kernel32.SetErrorMode(previous)
