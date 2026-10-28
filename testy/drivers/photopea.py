"""Photopea driver: the web editor (photopea.com) in a headless Chrome.

Photopea has no CLI, but it has an official embedding API: a page that iframes
photopea.com can post script strings (Photoshop-style DOM) and receives export
bytes back as ArrayBuffers. Testy serves photopea_host.html, which loads the
staged PSD from the local server, runs the export sequence, and POSTs
each artifact to the server's /testy-upload endpoint. This driver just steers a
headless Chrome at that page and polls window.__testyResult.

Requires internet (photopea.com loads live) and selenium (chromedriver is
resolved automatically by Selenium Manager on first use).
"""

from __future__ import annotations

import json
import time
import urllib.parse
from pathlib import Path
from typing import Callable

_driver = None

# Hard ceiling per host-page run; the stall detector below usually fires far sooner.
PAGE_TIMEOUT = 240
# The host page logs every step; if the log stops advancing this long, the run is dead.
STALL_TIMEOUT = 45


class PhotopeaError(RuntimeError):
    def __init__(self, message: str, harness: bool = False):
        super().__init__(message)
        self.harness = harness


# Failures on Testy's side of the host page: the page never ran, photopea.com never
# loaded, a staged file could not be fetched, an export never reached the server. They
# say nothing about whether Photopea can open the file, so the cell records no "opens"
# verdict and the file stays out of Photopea's averages (refused_with_reference). A
# stall after the file was posted into Photopea is Photopea's own (three psd-tools files
# hang there on every run) and still counts as a refusal.
_HARNESS_ERRORS = ("missing required parameters", "Photopea never became ready",
                   "file fetch failed", "upload failed")
_HARNESS_STALL_STEPS = ("no output", "waiting for the Photopea editor", "loading font",
                        "font not loaded", "editor ready; fetching")


def _chrome():
    global _driver
    if _driver is not None:
        try:
            _ = _driver.current_url  # liveness probe
            return _driver
        except Exception:
            try:
                _driver.quit()
            except Exception:
                pass
            _driver = None
    from selenium import webdriver
    from selenium.webdriver.chrome.options import Options

    options = Options()
    options.add_argument("--headless=new")
    options.add_argument("--window-size=1600,1000")
    options.add_argument("--disable-extensions")
    options.add_argument("--no-first-run")
    _driver = webdriver.Chrome(options=options)
    _driver.set_page_load_timeout(120)
    return _driver


def _rel_url(testy_root: Path, path: Path) -> str:
    """The run-relative path, raw. This is also the /testy-upload `name`, which rides in
    a query parameter and is decoded exactly once on the way in, so it must stay raw."""
    return str(path.resolve().relative_to(testy_root.resolve())).replace("\\", "/")


def _file_url(base_url: str, testy_root: Path, path: Path) -> str:
    """A GET-able URL for a staged file.

    Percent-encoded, unlike the upload names above: the server unquotes the request
    path, so a corpus file whose own name contains a '%' (eco%20beret.psd, whose run
    directory inherits the stem) would otherwise be looked up as `eco beret` and
    answered with a 404. quote() leaves '/' alone, so the separators survive.
    """
    return f"{base_url}/{urllib.parse.quote(_rel_url(testy_root, path))}"


def _run_host_page(base_url: str, query: dict[str, str],
                   expected_outputs: list[Path]) -> dict:
    chrome = _chrome()
    url = f"{base_url}/photopea_host.html?{urllib.parse.urlencode(query)}"
    chrome.get(url)
    deadline = time.time() + PAGE_TIMEOUT
    last_log = ""
    last_progress = time.time()
    while time.time() < deadline:
        time.sleep(2.0)
        raw = chrome.execute_script("return window.__testyResult || null;")
        if raw:
            result = json.loads(raw)
            if not result.get("ok"):
                error = str(result.get("error", "unknown host-page failure"))
                raise PhotopeaError(error, harness=error.startswith(_HARNESS_ERRORS))
            for output in expected_outputs:
                if not output.exists() or output.stat().st_size == 0:
                    raise PhotopeaError(f"host page reported ok but {output.name} is missing",
                                        harness=True)
            return result
        # Fail fast on stalls: the page logs every step, so a frozen log means a dead run.
        current_log = chrome.execute_script(
            "var d = document.getElementById('log'); return d ? d.textContent : '';") or ""
        if current_log != last_log:
            last_log = current_log
            last_progress = time.time()
        elif time.time() - last_progress > STALL_TIMEOUT:
            last_line = current_log.strip().splitlines()[-1] if current_log.strip() else "no output"
            raise PhotopeaError(f"stalled for {STALL_TIMEOUT}s at: {last_line}",
                                harness=last_line.startswith(_HARNESS_STALL_STEPS))
    raise PhotopeaError("host page timed out")


def _fonts_query(base_url: str, testy_root: Path, text_fonts: list[str] | None) -> dict[str, str]:
    """The host page's `fonts` parameter for the PostScript names a document's text
    uses: Photopea only has its own web fonts, so it gets the files Photoshop used
    (see fonts.py). Empty when none of the names is installed here."""
    import fonts as font_files

    urls = [_file_url(base_url, testy_root, path) for path in font_files.font_files(list(text_fonts or []))]
    return {"fonts": "|".join(urls)} if urls else {}


def export_all(
    base_url: str,
    testy_root: Path,
    original: Path,
    trap: Path | None,
    render_png: Path,
    resave_psd: Path,
    trap_png: Path,
    progress: Callable[[str], None] = lambda stage: None,
    text_fonts: list[str] | None = None,
) -> dict:
    upload_base = f"{base_url}/testy-upload?name="
    notes: list[str] = []
    try:
        progress("Photopea: open + export")
        result = _run_host_page(
            base_url,
            {
                "file": _file_url(base_url, testy_root, original),
                "upload": upload_base,
                "render": _rel_url(testy_root, render_png),
                "resave": _rel_url(testy_root, resave_psd),
                **_fonts_query(base_url, testy_root, text_fonts),
            },
            [render_png, resave_psd],
        )
        if trap is not None:
            progress("Photopea: trap render")
            try:
                _run_host_page(
                    base_url,
                    {
                        "file": _file_url(base_url, testy_root, trap),
                        "upload": upload_base,
                        "render": _rel_url(testy_root, trap_png),
                    },
                    [trap_png],
                )
            except PhotopeaError as error:
                notes.append(f"trap: {error}")
        return {"ok": True, "opens": "ok", "notes": notes}
    except PhotopeaError as error:
        return {"ok": False, "opens": None if error.harness else "fail", "error": str(error),
                "notes": notes}
    except Exception as error:  # a driver bug or a dead Chrome: no verdict on the file
        return {"ok": False, "opens": None, "error": f"driver crash: {error}", "notes": notes}


def render_text_afresh(base_url: str, testy_root: Path, source: Path, render_png: Path,
                        text_fonts: list[str] | None = None, rerender_text: bool = True) -> dict:
    """Render `source` after making Photopea lay out every text layer itself (it shows
    the raster cached in the file until a text layer is edited; the host page assigns
    each one a property's own value, which changes nothing else). Returns
    {"ok", "done": [layer names], "failed": [layer names], "error"}."""
    try:
        result = _run_host_page(
            base_url,
            {
                "file": _file_url(base_url, testy_root, source),
                "upload": f"{base_url}/testy-upload?name=",
                "render": _rel_url(testy_root, render_png),
                # (No re-layout when a font the text needs is missing on this machine:
                # nobody's own text render is scored then.)
                **({"nudge": "1"} if rerender_text else {}),
                **_fonts_query(base_url, testy_root, text_fonts),
            },
            [render_png],
        )
    except PhotopeaError as error:
        return {"ok": False, "done": [], "failed": [], "error": str(error)}
    report = result.get("nudge") or {}
    return {"ok": True, "done": list(report.get("done") or []),
            "failed": list(report.get("failed") or []), "error": str(report.get("error") or ""),
            "fonts": result.get("fonts") or {}}


def cleanup() -> None:
    global _driver
    if _driver is not None:
        try:
            _driver.quit()
        except Exception:
            pass
        _driver = None
