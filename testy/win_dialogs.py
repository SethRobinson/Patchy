"""Watch a scripted Windows app for nuisance modal dialogs and acknowledge them.

Photoshop raises modal alerts from inside `app.open` that `DialogModes.NO` does
not suppress - "This file contains file info data which cannot be read and has
been ignored" is the common one (a PSD whose IPTC resource holds something other
than IPTC records). The alert sits on Photoshop's UI thread, the driver's COM
call blocks behind it, and the hang watchdog eventually force-kills Photoshop, so
one warning about metadata Testy never even looks at turned a perfectly readable
PSD into "ground truth failed".

A DialogGuard runs alongside the blocked call: it polls the target process's
windows, clicks the acknowledging button on alerts it recognizes, and records
what it saw so the report can say the file warned rather than pretending nothing
happened. Anything it does not recognize is left alone and reported as blocking,
which turns a bare "hung >120s" into the dialog's actual text.

Deliberate limits, because this drives a window on someone's desktop:

- Only owned windows and standard `#32770` dialogs qualify; a top-level app frame
  (Photoshop's own, class `Photoshop`, owner 0) can never match, and neither can a
  window carrying more than MAX_PUSH_BUTTONS buttons.
- Only real push buttons are clicked, so an alert's "Don't show again" checkbox is
  never touched - dismissing a warning must not quietly change the app's settings.
- Only buttons that acknowledge (OK/Yes/Continue/...) are clicked. Cancel, No,
  Save, Discard and friends are refused, so a guessed click cannot lose work or
  abort an operation.
- Everything goes through messages posted to that dialog's own button: no mouse
  movement, no synthetic keystrokes, nothing that could land in another app.
"""

from __future__ import annotations

import subprocess
import threading
import time
from dataclasses import dataclass
from typing import Callable, Iterable, Sequence

CREATE_NO_WINDOW = 0x08000000

POLL_SECONDS = 1.0
# Photoshop restarts mid-run, so the process list is re-read now and then rather
# than pinned once at the start of a probe.
PID_REFRESH_SECONDS = 5.0
# An alert has one to three buttons. A window with more is a panel or a frame,
# never something this module should be clicking.
MAX_PUSH_BUTTONS = 6

# Buttons that mean "I have read this, carry on". Everything else is left alone;
# in particular Cancel/No/Save/Discard, where a wrong guess aborts the operation
# under test or writes a file.
ACKNOWLEDGE_BUTTONS = ("ok", "yes", "continue", "update", "close", "done", "proceed")

_BS_TYPEMASK = 0x0F
# Plain, default, and owner-drawn buttons all report BN_CLICKED; checkboxes,
# radio buttons, and group boxes must never be "clicked" as if they were buttons.
_PUSH_BUTTON_STYLES = (0x00, 0x01, 0x0B)


def _normalize(text: str) -> str:
    """Button caption reduced to its word: '&Don''t Show Again...' -> "don't show again"."""
    return text.replace("&", "").strip().strip(".").strip().lower()


@dataclass(frozen=True)
class DialogButton:
    hwnd: int
    control_id: int
    text: str

    @property
    def acknowledges(self) -> bool:
        return _normalize(self.text) in ACKNOWLEDGE_BUTTONS


@dataclass(frozen=True)
class Dialog:
    hwnd: int
    title: str
    text: str
    buttons: tuple[DialogButton, ...]

    def acknowledging_button(self) -> DialogButton | None:
        for button in self.buttons:
            if button.acknowledges:
                return button
        return None

    def describe(self) -> str:
        message = " ".join(self.text.split())
        if len(message) > 300:
            message = message[:297] + "..."
        buttons = "/".join(b.text for b in self.buttons)
        head = f'{self.title}: "{message}"' if message else self.title or "(untitled dialog)"
        return f"{head} [{buttons}]" if buttons else head


def process_ids(image_names: Sequence[str]) -> set[int]:
    """PIDs of running processes with any of these executable names."""
    pids: set[int] = set()
    for image in image_names:
        try:
            output = subprocess.run(
                ["tasklist", "/FI", f"IMAGENAME eq {image}", "/FO", "CSV", "/NH"],
                capture_output=True, text=True, timeout=20,
                creationflags=CREATE_NO_WINDOW,
            ).stdout
        except Exception:
            continue
        for line in output.splitlines():
            fields = [field.strip('" ') for field in line.split('","')]
            if len(fields) >= 2 and fields[0].lower() == image.lower():
                try:
                    pids.add(int(fields[1]))
                except ValueError:
                    pass
    return pids


def _is_dialog_window(class_name: str, owner: int) -> bool:
    """A dialog is either a standard dialog class or a window owned by another.

    An application's own frame is a top-level window with no owner, so it can
    never qualify - which is the point: Photoshop's main window is full of
    hidden panel buttons named OK and Cancel.
    """
    if class_name == "#32770":
        return True
    return owner != 0


def find_dialogs(pids: Iterable[int]) -> list[Dialog]:
    """Visible, enabled dialog windows belonging to any of these processes."""
    import win32con
    import win32gui
    import win32process

    wanted = set(pids)
    if not wanted:
        return []
    found: list[Dialog] = []

    def visit(hwnd: int, _: object) -> bool:
        try:
            if not win32gui.IsWindowVisible(hwnd) or not win32gui.IsWindowEnabled(hwnd):
                return True
            owner = win32gui.GetWindow(hwnd, win32con.GW_OWNER)
            if not _is_dialog_window(win32gui.GetClassName(hwnd), owner):
                return True
            if win32process.GetWindowThreadProcessId(hwnd)[1] not in wanted:
                return True
            dialog = _read_dialog(hwnd)
            if dialog is not None:
                found.append(dialog)
        except Exception:
            pass  # a window can die between the enumeration and the query
        return True

    try:
        win32gui.EnumWindows(visit, None)
    except Exception:
        pass
    return found


def _read_dialog(hwnd: int) -> Dialog | None:
    """Title, message text, and push buttons of a candidate window (None if it is not one)."""
    import win32con
    import win32gui

    buttons: list[DialogButton] = []
    texts: list[str] = []

    def visit_child(child: int, _: object) -> bool:
        try:
            if not win32gui.IsWindowVisible(child):
                return True
            class_name = win32gui.GetClassName(child)
            text = win32gui.GetWindowText(child)
            if class_name == "Button":
                style = win32gui.GetWindowLong(child, win32con.GWL_STYLE)
                if (style & _BS_TYPEMASK) in _PUSH_BUTTON_STYLES and win32gui.IsWindowEnabled(child):
                    buttons.append(DialogButton(
                        hwnd=child,
                        control_id=win32gui.GetWindowLong(child, win32con.GWL_ID),
                        text=text,
                    ))
            elif class_name == "Static" and text.strip():
                texts.append(text)
        except Exception:
            pass
        return True

    try:
        win32gui.EnumChildWindows(hwnd, visit_child, None)
    except Exception:
        return None  # no children to enumerate: nothing this module can act on
    if not buttons or len(buttons) > MAX_PUSH_BUTTONS:
        return None
    return Dialog(
        hwnd=hwnd,
        title=win32gui.GetWindowText(hwnd),
        text=" ".join(texts),
        buttons=tuple(buttons),
    )


# How the guard escalates on a dialog that stays up, counted in polls since it
# was first seen. Spacing the steps out gives a busy UI thread time to act on
# the first, gentlest one before anything louder follows.
_COMMAND_TICK = 0
_CLICK_TICK = 2
_CLOSE_TICK = 4
_GIVE_UP_TICK = 7


def _click(dialog: Dialog, button: DialogButton, tick: int) -> None:
    """Ask the dialog to act on `button`, escalating as it keeps standing.

    A posted WM_COMMAND is how a dialog hears its own button, and it works while
    the dialog is not the foreground window (BM_CLICK is documented as
    unreliable then). BM_CLICK follows as a fallback for dialogs with a custom
    window procedure, through SendMessageTimeout so a wedged UI thread cannot
    block this one.
    """
    import win32con
    import win32gui

    if tick == _COMMAND_TICK:
        win32gui.PostMessage(
            dialog.hwnd, win32con.WM_COMMAND,
            (win32con.BN_CLICKED << 16) | (button.control_id & 0xFFFF), button.hwnd)
    elif tick == _CLICK_TICK:
        win32gui.SendMessageTimeout(
            button.hwnd, win32con.BM_CLICK, 0, 0, win32con.SMTO_ABORTIFHUNG, 2000)
    elif tick == _CLOSE_TICK and all(b.acknowledges for b in dialog.buttons):
        # Last resort, and only for a pure alert: closing it says the same "I
        # read this" its only buttons offer. A dialog with a real choice (a
        # refused button among them) never reaches here.
        win32gui.PostMessage(dialog.hwnd, win32con.WM_CLOSE, 0, 0)


class DialogGuard:
    """Background watcher for modal dialogs raised while a scripted call runs.

    Used as a context manager around the blocking call. Dismissed dialogs are
    described in `dismissed`; ones it would not or could not clear are in
    `blocked`. With `watchdog_seconds`, `on_timeout` fires when nothing has
    happened for that long - each dismissal restarts the countdown, since the
    app just made progress.
    """

    def __init__(
        self,
        image_names: Sequence[str],
        watchdog_seconds: float | None = None,
        on_timeout: Callable[[], None] | None = None,
        poll_seconds: float = POLL_SECONDS,
        log: Callable[[str], None] | None = None,
    ) -> None:
        self.image_names = list(image_names)
        self.watchdog_seconds = watchdog_seconds
        self.on_timeout = on_timeout
        self.poll_seconds = poll_seconds
        self.log = log
        self.dismissed: list[str] = []
        self.blocked: list[str] = []
        self.timed_out = False
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        self._ticks: dict[int, int] = {}
        self._pending: dict[int, str] = {}
        self._pids: set[int] = set()
        self._pids_read_at = 0.0

    def __enter__(self) -> "DialogGuard":
        self.start()
        return self

    def __exit__(self, *_exc: object) -> None:
        self.stop()

    def start(self) -> None:
        self._thread = threading.Thread(target=self._run, name="dialog-guard", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=10)
            self._thread = None
        self._reconcile()

    def _reconcile(self) -> None:
        """Account for dialogs cleared between the last poll and the guard stopping.

        The blocked call resumes the instant its dialog closes and the caller
        stops the guard right after, which is almost always sooner than the next
        poll - so without this the dismissal that unblocked the run would go
        unrecorded.
        """
        if self.timed_out:
            return  # the app was killed from under them; they were not dismissed
        import win32gui

        for hwnd, description in list(self._pending.items()):
            try:
                still_up = win32gui.IsWindow(hwnd) and win32gui.IsWindowVisible(hwnd)
            except Exception:
                still_up = False
            if not still_up and self._pending.pop(hwnd, None) is not None:
                self._ticks.pop(hwnd, None)
                self.dismissed.append(description)
                self._note(f"dismissed a dialog: {description}")

    def _note(self, message: str) -> None:
        if self.log is not None:
            self.log(message)

    def _current_pids(self) -> set[int]:
        now = time.monotonic()
        if self._pids_read_at == 0.0 or now - self._pids_read_at >= PID_REFRESH_SECONDS:
            self._pids = process_ids(self.image_names)
            self._pids_read_at = now
        return self._pids

    def _run(self) -> None:
        deadline = None if self.watchdog_seconds is None else time.monotonic() + self.watchdog_seconds
        while not self._stop.wait(self.poll_seconds):
            if self._sweep() and self.watchdog_seconds is not None:
                deadline = time.monotonic() + self.watchdog_seconds  # progress: start over
            # A sweep can outlast the call it was guarding; the deadline must not
            # fire once the caller is done, or it would kill a healthy app.
            if deadline is not None and time.monotonic() >= deadline and not self._stop.is_set():
                self.timed_out = True
                if self.on_timeout is not None:
                    try:
                        self.on_timeout()
                    except Exception:
                        pass
                return

    def _sweep(self) -> bool:
        """One poll. True when a dialog this guard was working on went away."""
        dialogs = find_dialogs(self._current_pids())
        live = {dialog.hwnd for dialog in dialogs}
        progressed = False
        for hwnd, description in list(self._pending.items()):
            if hwnd not in live and self._pending.pop(hwnd, None) is not None:
                self._ticks.pop(hwnd, None)
                self.dismissed.append(description)
                self._note(f"dismissed a dialog: {description}")
                progressed = True

        for dialog in dialogs:
            button = dialog.acknowledging_button()
            if button is None:
                # Nothing here answers "carry on", so this guard has no business
                # picking one. Report it and let the caller's watchdog decide.
                if dialog.hwnd not in self._ticks:
                    self._ticks[dialog.hwnd] = _GIVE_UP_TICK
                    self.blocked.append(dialog.describe())
                    self._note(f"leaving a dialog alone (no button means carry on): "
                               f"{dialog.describe()}")
                continue
            tick = self._ticks.get(dialog.hwnd, 0)
            if tick >= _GIVE_UP_TICK:
                if self._pending.pop(dialog.hwnd, None) is not None:
                    self.blocked.append(dialog.describe())
                    self._note(f"could not dismiss a dialog: {dialog.describe()}")
                continue
            self._pending[dialog.hwnd] = dialog.describe()
            self._ticks[dialog.hwnd] = tick + 1
            try:
                _click(dialog, button, tick)
            except Exception as error:
                self._note(f"click on {dialog.describe()} failed: {error}")
        return progressed


# ---------------------------------------------------------------------------
# Self-test: `python testy\win_dialogs.py --selftest`
#
# Real Win32 dialogs from a helper process stand in for Photoshop's alerts, so
# the message plumbing, the button filter, and the refusal path are all covered
# without needing Photoshop (or a broken PSD) on the machine.
# ---------------------------------------------------------------------------

_MESSAGE_BOX = (
    "import ctypes,sys;"
    "ctypes.windll.user32.MessageBoxW(None, sys.argv[1], 'Adobe Photoshop', int(sys.argv[2]))"
)
_MB_OK = 0
_MB_RETRYCANCEL = 5

_FILE_INFO_WARNING = ("This file contains file info data which cannot be read "
                      "and has been ignored.")


def _selftest() -> int:
    import sys
    from pathlib import Path

    python = sys.executable
    image = Path(python).name
    failures: list[str] = []

    def show(message: str, style: int) -> subprocess.Popen:
        child = subprocess.Popen([python, "-c", _MESSAGE_BOX, message, str(style)])
        time.sleep(2.0)  # let the dialog materialize before the guard looks
        return child

    def check(condition: bool, description: str) -> None:
        print(("  ok   " if condition else "  FAIL ") + description)
        if not condition:
            failures.append(description)

    print("1. an OK-only alert is acknowledged and its text recorded")
    child = show(_FILE_INFO_WARNING, _MB_OK)
    with DialogGuard([image], log=lambda m: print("       " + m)) as guard:
        deadline = time.time() + 20
        while time.time() < deadline and child.poll() is None:
            time.sleep(0.25)
    check(child.poll() is not None, "the dialog was closed")
    check(any("file info data" in d for d in guard.dismissed),
          "the warning text was captured")
    if child.poll() is None:
        child.kill()

    print("2. a dialog offering only refused answers is left alone")
    child = show("Ground truth needs a decision.", _MB_RETRYCANCEL)
    with DialogGuard([image], log=lambda m: print("       " + m)) as guard:
        time.sleep(6.0)
    check(child.poll() is None, "the dialog is still up")
    check(bool(guard.blocked), "it was reported as blocking")
    check(not guard.dismissed, "nothing was clicked")
    child.kill()

    print("3. the watchdog fires when a dialog cannot be cleared")
    child = show("Ground truth needs a decision.", _MB_RETRYCANCEL)
    fired: list[bool] = []
    with DialogGuard([image], watchdog_seconds=3.0, on_timeout=lambda: fired.append(True)) as guard:
        time.sleep(8.0)
    check(bool(fired) and guard.timed_out, "on_timeout ran")
    child.kill()

    print("4. an app's own frame is never treated as a dialog")
    check(not _is_dialog_window("Photoshop", 0), "a top-level frame is skipped")
    check(_is_dialog_window("#32770", 0), "a standard dialog class qualifies")
    check(not DialogButton(0, 1, "Cancel").acknowledges, "Cancel is not an answer")
    check(not DialogButton(0, 1, "Don't Show Again").acknowledges, "a checkbox caption is not an answer")
    check(DialogButton(0, 1, "&OK").acknowledges, "OK is")

    print("FAILED: " + "; ".join(failures) if failures else "all checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    import sys

    if "--selftest" in sys.argv:
        raise SystemExit(_selftest())
    print(__doc__)
    print("run with --selftest to exercise it against real Win32 dialogs")
