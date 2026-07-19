"""Affinity (Canva unified app, 3.x) driver: background UI-Automation, no input theft.

The app is WPF, so nearly everything is reachable through UIA patterns, which are
COM calls into the app and work while it stays unfocused/behind other windows -
verified empirically on 3.2.3 (July 2026):

- The quick-export dropdown button exposes the Toggle pattern; toggling opens the
  export panel as a child ``Popup`` that SURVIVES in the background (real mouse
  clicks and posted messages are useless: WPF re-reads the physical cursor, and
  light-dismiss popups close on activation changes).
- Format chips are ListBox items whose SelectionItem.Select() works and is
  reflected in the quick button's label ("Export PNG" -> "Export PSD"), giving a
  reliable feedback loop.
- Invoking the Export button opens a completely standard shell Save As dialog
  (#32770) that appears WITHOUT the app being foregrounded and automates through
  patterns: the filename box is the classic automation-id 1001 edit (ValuePattern
  SetValue) and the Save/Yes buttons expose Invoke.

Strict-targeting policy (after an early probe once grabbed a file-LIST edit cell
and started renaming an unrelated file): every element is matched by exact
automation id or exact title, exactly-one matches are required where ambiguity
would be dangerous, and any miss ABORTS the leg with a diagnostic window grab
instead of guessing.

The driver stages a private input copy per document (real filename keeps tab
titles unique; the trap variant gets a __trap suffix). Focus impact: launching
the app (or forwarding a file into it) is the only foreground-touching event. A
pre-existing user instance is reused and NEVER killed; instances this module
launched are closed at cleanup().
"""

from __future__ import annotations

import shutil
import subprocess
import time
from pathlib import Path
from typing import Callable

# True once this run launched the app itself (no pre-existing instance): cleanup()
# may then terminate Affinity processes. A reused user instance is never touched.
# (Tracking launcher pids is useless: the WindowsApps alias forwards and exits.)
_we_own_instance = False
# Affinity drops launch file arguments when relaunched too soon after the previous
# instance closed (and forwards into a warm instance drop them too); launches are
# reliable again after a cooldown. Empirically ~60-90s worked and ~10-20s failed.
_last_close_time = 0.0
LAUNCH_COOLDOWN_SECONDS = 50

# Cold-launch document opens include full app init + parse: deko_test (10 MB) was
# observed finishing just past 45s, so this stays generous while still bounded.
DOCUMENT_TIMEOUT = 90
DIALOG_TIMEOUT = 15
EXPORT_TIMEOUT = 45
# Hard wall-clock budgets per document (project rule: loading/saving that takes much
# longer than ~10s means something else is wrong - fail honestly instead of grinding
# through stacked retries). The main budget covers launch + open + two export legs.
FILE_BUDGET_SECONDS = 180
TRAP_BUDGET_SECONDS = 75


class AffinityError(RuntimeError):
    pass


def _desktop():
    from pywinauto import Desktop

    return Desktop(backend="uia")


def _legacy(element):
    from pywinauto.uia_defines import get_elem_interface

    return get_elem_interface(element.element_info.element, "LegacyIAccessible")


def _find_main():
    """The main window, via UIA with a win32 fallback (UIA enumeration can
    transiently miss it while a modal dialog is up)."""
    for window in _desktop().windows():
        try:
            if (window.window_text() or "") == "Affinity" and window.class_name() == "Window":
                return window
        except Exception:
            continue
    try:
        import win32gui

        found: list[int] = []

        def callback(hwnd, _):
            try:
                if (win32gui.IsWindowVisible(hwnd)
                        and win32gui.GetWindowText(hwnd) == "Affinity"
                        and "HwndWrapper[Affinity.exe" in win32gui.GetClassName(hwnd)):
                    found.append(hwnd)
            except Exception:
                pass
            return True

        win32gui.EnumWindows(callback, None)
        if found:
            return _desktop().window(handle=found[0]).wrapper_object()
    except Exception:
        pass
    return None


def _find_dialog(pid: int, title: str) -> int | None:
    """A visible #32770 dialog with this exact title, belonging to the process."""
    import win32gui
    import win32process

    found: list[int] = []

    def callback(hwnd, _):
        try:
            _, wpid = win32process.GetWindowThreadProcessId(hwnd)
            if (wpid == pid and win32gui.GetClassName(hwnd) == "#32770"
                    and win32gui.IsWindowVisible(hwnd)
                    and win32gui.GetWindowText(hwnd) == title):
                found.append(hwnd)
        except Exception:
            pass
        return True

    win32gui.EnumWindows(callback, None)
    return found[0] if found else None


def _tab_matches(title: str, tab_stem: str) -> bool:
    """Affinity's WPF tab headers consume underscores as mnemonic markers
    ("deko_test.psd" displays as "dekotest.psd", "__trap" as "_trap"), so compare
    underscore-stripped forms with an exact-prefix match up to the extension dot."""
    return title.replace("_", "").startswith(tab_stem.replace("_", "") + ".")


def _item_label(item) -> str:
    try:
        inner = [t.window_text() for t in item.descendants(control_type="Text")]
        return inner[0] if inner else (item.window_text() or "")
    except Exception:
        return ""


def _activate_item(item) -> bool:
    for attempt in ("select", "invoke", "msaa_select", "msaa_action"):
        try:
            if attempt == "select":
                item.select()
            elif attempt == "invoke":
                item.invoke()
            elif attempt == "msaa_select":
                _legacy(item).Select(2)  # SELFLAG_TAKESELECTION
            else:
                _legacy(item).DoDefaultAction()
            return True
        except Exception:
            continue
    return False


def _press_button(button) -> None:
    try:
        button.invoke()
        return
    except Exception:
        pass
    _legacy(button).DoDefaultAction()


class AffinitySession:
    """One running Affinity instance; accessors re-fetch elements fresh because the
    WPF tree churns and cached wrappers go stale."""

    def __init__(self, exe: Path, log: Callable[[str], None]) -> None:
        self.exe = exe
        self.log = log
        self.main = None
        self.panel = None
        self._quick = None
        self.deadline: float | None = None

    def set_budget(self, seconds: float) -> None:
        self.deadline = time.time() + seconds

    def _check_budget(self, stage: str) -> None:
        if self.deadline is not None and time.time() > self.deadline:
            raise AffinityError(f"file time budget exceeded during: {stage}")

    # ---------- lifecycle ----------

    def ensure_running(self, file_to_open: Path | None = None) -> None:
        global _we_own_instance
        arguments = [str(self.exe)] + ([str(file_to_open)] if file_to_open else [])
        existing = _find_main()
        if existing is not None:
            # A pre-existing instance we do not own (the user's own Affinity): forward
            # and hope - killing their session is off the table. Owned instances never
            # reach this branch (export_all closes them before relaunching).
            self.main = existing
            self.log("reusing running Affinity instance")
            if file_to_open is not None:
                subprocess.Popen(arguments)
                self.log("forwarded file to running Affinity (unreliable; close the "
                         "running Affinity for dependable runs)")
            return
        # Respect the relaunch cooldown, or the launch comes up without the document.
        # The wait is dead time, not work: extend the file budget by it.
        remaining = _last_close_time + LAUNCH_COOLDOWN_SECONDS - time.time()
        if remaining > 0:
            self.log(f"cooldown {int(remaining)}s before relaunch")
            time.sleep(remaining)
            if self.deadline is not None:
                self.deadline += remaining
        # Launch with retries: right after an instance dies, the MSIX broker can drop
        # a launch on the floor (observed empirically), so one attempt is not enough.
        for attempt in range(2):
            self._check_budget("launch")
            subprocess.Popen(arguments)
            _we_own_instance = True
            self.log(f"launched Affinity (attempt {attempt + 1})")
            deadline = time.time() + 30
            while time.time() < deadline:
                self.main = _find_main()
                if self.main is not None:
                    return
                time.sleep(2.0)
        raise AffinityError("Affinity main window never appeared")

    def _refresh_main(self) -> None:
        refreshed = _find_main()
        if refreshed is not None:
            self.main = refreshed
        if self.main is None:
            raise AffinityError("Affinity main window lost")

    def wait_for_document(self, tab_stem: str, file_to_reforward: Path | None = None) -> None:
        deadline = time.time() + DOCUMENT_TIMEOUT
        reforwarded = False
        while time.time() < deadline:
            self._check_budget("waiting for document tab")
            try:
                self._refresh_main()
                for tab in self.main.descendants(control_type="TabItem"):
                    title = tab.window_text() or ""
                    if _tab_matches(title, tab_stem) and "StudioPage" not in title:
                        return
            except Exception:
                pass
            # A forward that raced a busy instance can get dropped; try once more
            # halfway through the wait.
            if (not reforwarded and file_to_reforward is not None
                    and time.time() > deadline - DOCUMENT_TIMEOUT / 2):
                subprocess.Popen([str(self.exe), str(file_to_reforward)])
                self.log(f"re-forwarded {file_to_reforward.name}")
                reforwarded = True
            time.sleep(2.0)
        raise AffinityError(
            f"Affinity never opened the document (no '{tab_stem}' tab within {DOCUMENT_TIMEOUT}s)")

    def select_document_tab(self, tab_stem: str) -> None:
        """Make the wanted document active (the quick export acts on the active tab)."""
        self._refresh_main()
        for tab in self.main.descendants(control_type="TabItem"):
            title = tab.window_text() or ""
            if _tab_matches(title, tab_stem) and "StudioPage" not in title:
                if not _activate_item(tab):
                    raise AffinityError(f"could not activate tab '{title}'")
                time.sleep(1.0)
                return
        raise AffinityError(f"document tab '{tab_stem}' not found")

    def close_panel(self) -> None:
        """Collapse the quick-export popup: an open light-dismiss popup can swallow
        the single-instance file forward for the next document."""
        try:
            if self._find_panel() is not None:
                self._quick_control().children(control_type="Button")[-1].toggle()
                time.sleep(1.0)
        except Exception:
            pass
        self.panel = None

    def wait_until_export_ready(self) -> None:
        """On a cold launch the toolbars populate well after the document tab shows;
        wait until the quick-export control actually has its two buttons."""
        deadline = time.time() + 30
        while time.time() < deadline:
            self._check_budget("waiting for export toolbar")
            try:
                controls = self.main.descendants(class_name="QuickExportControl")
                if controls and len(controls[0].children(control_type="Button")) >= 2:
                    return
            except Exception:
                pass
            time.sleep(2.5)
            self._refresh_main()
        raise AffinityError("quick-export control never became ready")

    # ---------- quick-export panel ----------

    def _quick_control(self):
        # Cached: the full-tree class_name scan takes seconds and quick_text() runs
        # once per chip probe; the wrapper survives within one app instance.
        if self._quick is not None:
            try:
                if self._quick.rectangle().width() > 0:
                    return self._quick
            except Exception:
                self._quick = None
        self._refresh_main()
        controls = self.main.descendants(class_name="QuickExportControl")
        if not controls:
            raise AffinityError("QuickExportControl not found")
        self._quick = controls[0]
        return self._quick

    def quick_text(self) -> str:
        try:
            texts = [t.window_text() for t in self._quick_control().descendants(control_type="Text")]
        except AffinityError:
            raise
        except Exception:
            self._quick = None
            texts = [t.window_text() for t in self._quick_control().descendants(control_type="Text")]
        return texts[0] if texts else ""

    def _find_panel(self):
        self._refresh_main()
        for child in self.main.children():
            try:
                if child.element_info.class_name == "Popup":
                    if any((b.window_text() or "").startswith("Export")
                           for b in child.descendants(control_type="Button")):
                        return child
            except Exception:
                continue
        return None

    def open_panel(self):
        """Open the quick-export panel and remember it. Toggles are paced: a cold
        instance takes several seconds per UIA scan, and re-toggling too soon just
        closes the popup the previous toggle opened."""
        deadline = time.time() + 35
        while time.time() < deadline:
            self._check_budget("opening export panel")
            panel = self._find_panel()
            if panel is not None:
                self.panel = panel
                return panel
            try:
                dropdown = self._quick_control().children(control_type="Button")[-1]
                dropdown.toggle()
            except Exception as error:
                self.log(f"panel toggle hiccup: {type(error).__name__}")
                time.sleep(2.0)
                continue
            settle = time.time() + 8
            while time.time() < settle:
                time.sleep(1.0)
                panel = self._find_panel()
                if panel is not None:
                    self.panel = panel
                    return panel
        raise AffinityError("quick-export panel did not open")

    def _live_panel(self):
        """The remembered panel if still alive, else one reopen attempt."""
        if self.panel is not None:
            try:
                if self.panel.rectangle().width() > 0:
                    return self.panel
            except Exception:
                pass
            self.panel = None
        return self.open_panel()

    def _chips(self, panel):
        chip_lists = panel.descendants(control_type="List")
        if not chip_lists:
            raise AffinityError("format chip list not found")
        return chip_lists[0]

    def select_format(self, format_name: str) -> bool:
        if self.quick_text() == f"Export {format_name}":
            return True
        for attempt in range(2):
            self._check_budget(f"selecting {format_name} chip")
            try:
                panel = self._live_panel()
                for chip in self._chips(panel).children(control_type="ListItem"):
                    chip.select()
                    time.sleep(0.7)
                    if self.quick_text() == f"Export {format_name}":
                        return True
            except AffinityError:
                raise
            except Exception as error:
                self.log(f"chip pass failed: {type(error).__name__}")
                self.panel = None  # stale panel: refresh on the next pass
                time.sleep(1.0)
        return False

    def ensure_format_chip(self, format_name: str) -> None:
        """Toggle the format on in the '+' list so a chip for it exists."""
        if self.select_format(format_name):
            return
        for attempt in range(2):
            self._check_budget(f"enabling {format_name} chip")
            try:
                panel = self._live_panel()
                chip_rect = self._chips(panel).rectangle()
                plus = [b for b in panel.descendants(control_type="Button")
                        if b.rectangle().left >= chip_rect.right - 6
                        and b.rectangle().top <= chip_rect.bottom
                        and b.rectangle().bottom >= chip_rect.top]
                if len(plus) != 1:
                    time.sleep(1.0)
                    continue
                try:
                    plus[0].toggle()
                except Exception:
                    _press_button(plus[0])
                time.sleep(1.5)
                labels = [t for t in panel.descendants(control_type="Text")
                          if t.window_text() == format_name]
                if not labels:
                    continue
                row = labels[0].rectangle()
                toggles = [b for b in panel.descendants(control_type="Button")
                           if abs(b.rectangle().top - row.top) < 16
                           and b.rectangle().left > row.right]
                if len(toggles) != 1:
                    continue
                try:
                    toggles[0].toggle()
                except Exception:
                    _press_button(toggles[0])
                time.sleep(1.2)
                if self.select_format(format_name):
                    return
            except AffinityError:
                raise
            except Exception as error:
                self.log(f"format-chip attempt failed: {type(error).__name__}")
                time.sleep(1.0)
        raise AffinityError(f"could not enable a {format_name} chip")

    # ---------- export leg ----------

    def export_active_document(self, out_path: Path) -> None:
        """Invoke Export and drive the shell Save As dialog, strictly targeted."""
        panel = self._live_panel()
        export_buttons = [b for b in panel.descendants(control_type="Button")
                          if (b.window_text() or "").startswith("Export")]
        if len(export_buttons) != 1:
            raise AffinityError(f"expected exactly one Export button, found {len(export_buttons)}")
        pid = self.main.process_id()
        _press_button(export_buttons[0])

        dialog_hwnd = None
        deadline = time.time() + DIALOG_TIMEOUT
        while time.time() < deadline and dialog_hwnd is None:
            self._check_budget("waiting for Save As dialog")
            time.sleep(1.0)
            dialog_hwnd = _find_dialog(pid, "Save As")
        if dialog_hwnd is None:
            raise AffinityError("Save As dialog never appeared")

        dialog = _desktop().window(handle=dialog_hwnd).wrapper_object()
        # THE filename box: the shell dialog's automation id 1001, nothing else ever.
        filename_edits = [e for e in dialog.descendants(control_type="Edit")
                          if e.element_info.automation_id == "1001"]
        if len(filename_edits) != 1:
            raise AffinityError(f"expected exactly one filename box, found {len(filename_edits)}")
        filename_edits[0].set_edit_text(str(out_path))
        time.sleep(0.5)
        written = ""
        try:
            written = filename_edits[0].get_value()
        except Exception:
            try:
                written = filename_edits[0].window_text()
            except Exception:
                pass
        if written.strip('"') != str(out_path):
            raise AffinityError(f"filename box verification failed: {written!r}")

        save_buttons = [b for b in dialog.descendants(control_type="Button")
                        if (b.window_text() or "").strip() == "Save"]
        if len(save_buttons) != 1:
            raise AffinityError(f"expected exactly one Save button, found {len(save_buttons)}")
        _press_button(save_buttons[0])

        deadline = time.time() + EXPORT_TIMEOUT
        while time.time() < deadline:
            self._check_budget("waiting for exported file")
            time.sleep(1.5)
            confirm_hwnd = _find_dialog(pid, "Confirm Save As")
            if confirm_hwnd is not None:
                confirm = _desktop().window(handle=confirm_hwnd).wrapper_object()
                yes_buttons = [b for b in confirm.descendants(control_type="Button")
                               if (b.window_text() or "").strip() == "Yes"]
                if len(yes_buttons) == 1:
                    _press_button(yes_buttons[0])
            if out_path.exists() and out_path.stat().st_size > 0:
                size = out_path.stat().st_size
                time.sleep(1.5)
                if out_path.stat().st_size == size:
                    return
        # Leave no half-open dialog behind on failure.
        stray = _find_dialog(pid, "Save As")
        if stray is not None:
            stray_dialog = _desktop().window(handle=stray).wrapper_object()
            cancels = [b for b in stray_dialog.descendants(control_type="Button")
                       if (b.window_text() or "").strip() == "Cancel"]
            if len(cancels) == 1:
                _press_button(cancels[0])
        raise AffinityError("export produced no output file")

    def diagnostic_grab(self, out_path: Path) -> None:
        """PrintWindow capture of the (possibly background) window for failure triage."""
        try:
            import ctypes

            import win32gui
            import win32ui
            from PIL import Image

            self._refresh_main()
            rect = self.main.rectangle()
            hwnd = self.main.handle
            hdc = win32gui.GetWindowDC(hwnd)
            src = win32ui.CreateDCFromHandle(hdc)
            mem = src.CreateCompatibleDC()
            bitmap = win32ui.CreateBitmap()
            bitmap.CreateCompatibleBitmap(src, rect.width(), rect.height())
            mem.SelectObject(bitmap)
            ctypes.windll.user32.PrintWindow(hwnd, mem.GetSafeHdc(), 2)
            info = bitmap.GetInfo()
            image = Image.frombuffer("RGB", (info["bmWidth"], info["bmHeight"]),
                                     bitmap.GetBitmapBits(True), "raw", "BGRX", 0, 1)
            image.save(out_path)
            mem.DeleteDC()
            src.DeleteDC()
            win32gui.ReleaseDC(hwnd, hdc)
            win32gui.DeleteObject(bitmap.GetHandle())
        except Exception:
            pass


def _export_document(session: AffinitySession, input_file: Path, tab_stem: str,
                     legs: list[tuple[str, Path]], budget_seconds: float,
                     progress: Callable[[str], None]) -> dict[str, str]:
    """Open input_file (fresh tab), then run each (format, out_path) leg."""
    results: dict[str, str] = {}
    session.set_budget(budget_seconds)
    session.ensure_running(input_file)
    progress(f"Affinity: waiting for {tab_stem}")
    session.wait_for_document(tab_stem, file_to_reforward=input_file)
    session.select_document_tab(tab_stem)
    session.wait_until_export_ready()
    session.panel = None
    session.open_panel()  # one cold open per document; legs reuse the live panel
    for format_name, out_path in legs:
        progress(f"Affinity: exporting {format_name}")
        try:
            session.ensure_format_chip(format_name)
            if not session.select_format(format_name):
                results[format_name] = "could not select format"
                continue
            session.export_active_document(out_path)
            results[format_name] = "ok"
            time.sleep(3.0)  # let the app settle before the next panel round-trip
        except AffinityError as error:
            results[format_name] = str(error)
    session.close_panel()  # an open popup can swallow the next document's forward
    return results


def export_all(
    exe: Path,
    original: Path,
    trap: Path | None,
    render_png: Path,
    resave_psd: Path,
    trap_png: Path,
    progress: Callable[[str], None] = lambda stage: None,
) -> dict:
    log_lines: list[str] = []

    def log(message: str) -> None:
        log_lines.append(message)

    session = AffinitySession(exe, log)
    cell_dir = render_png.parent
    input_dir = cell_dir / "input"
    input_dir.mkdir(parents=True, exist_ok=True)

    # One document per app session: forwards into a running instance drop the file
    # (as do relaunches without the cooldown), so each document gets a fresh launch
    # carrying it as the argument, after gracefully closing the previous session.
    if _we_own_instance:
        cleanup()

    # Name the input after the real document (staged copies are all "original.psd";
    # the staging layout is files/<real stem>/_staged/original.psd) so tab titles are
    # meaningful and prefix-matching stays unambiguous.
    real_stem = original.parent.parent.name if original.stem == "original" else original.stem
    input_file = input_dir / f"{real_stem}{original.suffix}"
    shutil.copyfile(original, input_file)

    try:
        legs = _export_document(
            session, input_file, input_file.stem,
            [("PNG", render_png), ("PSD", resave_psd)], FILE_BUDGET_SECONDS, progress,
        )
        # No trap leg for Affinity: opening a second document needs either a forward
        # (drops the file) or a relaunch (needs the cooldown, doubling per-file cost),
        # and the trap adds nothing here - Affinity re-renders text/layers by design,
        # which a baked-composite cheat could not fake at its observed accuracy.
        if trap is not None:
            legs["trap"] = "skipped (single-document driver)"
        if legs.get("PNG") != "ok":
            session.diagnostic_grab(cell_dir / "affinity_failure.png")
            return {"ok": False, "opens": "ok",
                    "error": f"PNG leg: {legs.get('PNG', 'unknown')}", "notes": log_lines}
        notes = log_lines + [f"{key}: {value}" for key, value in legs.items() if value != "ok"]
        # Partial successes (a failed PSD/trap leg) are automation-transient: they must
        # not be cached, or the missing scores freeze into every later run.
        return {"ok": True, "opens": "ok", "notes": notes,
                "cacheable": all(value == "ok" for value in legs.values())}
    except AffinityError as error:
        if session.main is not None:
            session.diagnostic_grab(cell_dir / "affinity_failure.png")
        return {"ok": False, "opens": "fail", "error": str(error), "notes": log_lines}
    except Exception as error:  # never let the harness die on driver bugs
        return {"ok": False, "opens": "fail", "error": f"driver crash: {error}", "notes": log_lines}


def cleanup() -> None:
    """Close Affinity only when this run launched it (never a user's instance).

    Graceful close first (WM_CLOSE + pattern-invoking any "Don't Save" prompt): after a
    force-kill, the MSIX single-instance broker keeps a zombie registration, and every
    later launch/forward silently drops its file argument until the app exits cleanly
    once. taskkill stays as the last resort.
    """
    global _we_own_instance
    if not _we_own_instance:
        return
    main = _find_main()
    if main is not None:
        try:
            import win32con
            import win32gui

            win32gui.PostMessage(main.handle, win32con.WM_CLOSE, 0, 0)
            deadline = time.time() + 12
            while time.time() < deadline and _find_main() is not None:
                time.sleep(1.0)
                _dismiss_dont_save_prompt()
        except Exception:
            pass
    if _find_main() is not None:
        try:
            subprocess.run(["taskkill", "/IM", "Affinity.exe", "/F"],
                           capture_output=True, timeout=30)
        except Exception:
            pass
        deadline = time.time() + 8
        while time.time() < deadline and _find_main() is not None:
            time.sleep(1.0)
    global _last_close_time
    _last_close_time = time.time()
    _we_own_instance = False


def _dismiss_dont_save_prompt() -> None:
    """Invoke a 'Don't Save' button on any Affinity prompt (WPF; pattern-friendly)."""
    try:
        for window in _desktop().windows():
            try:
                if "Affinity" not in (window.class_name() or "") and \
                        "HwndWrapper[Affinity" not in (window.class_name() or ""):
                    continue
                for button in window.descendants(control_type="Button"):
                    label = (button.window_text() or "").strip()
                    if label in ("Don't Save", "Don&apos;t Save", "No"):
                        try:
                            button.invoke()
                        except Exception:
                            _legacy(button).DoDefaultAction()
                        return
            except Exception:
                continue
    except Exception:
        pass
