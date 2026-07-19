"""Photopea driver: the web editor (photopea.com) in a headless Chrome.

Photopea has no CLI, but it has an official embedding API: a page that iframes
photopea.com can post script strings (Photoshop-style DOM) and receives export
bytes back as ArrayBuffers. Testy serves photopea_host.html, which loads the
staged PSD from the local server, runs the export/mutation sequence, and POSTs
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
    pass


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
    return str(path.resolve().relative_to(testy_root.resolve())).replace("\\", "/")


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
                raise PhotopeaError(result.get("error", "unknown host-page failure"))
            for output in expected_outputs:
                if not output.exists() or output.stat().st_size == 0:
                    raise PhotopeaError(f"host page reported ok but {output.name} is missing")
            return result
        # Fail fast on stalls: the page logs every step, so a frozen log means a dead run.
        current_log = chrome.execute_script(
            "var d = document.getElementById('log'); return d ? d.textContent : '';") or ""
        if current_log != last_log:
            last_log = current_log
            last_progress = time.time()
        elif time.time() - last_progress > STALL_TIMEOUT:
            last_line = current_log.strip().splitlines()[-1] if current_log.strip() else "no output"
            raise PhotopeaError(f"stalled for {STALL_TIMEOUT}s at: {last_line}")
    raise PhotopeaError("host page timed out")


def export_all(
    base_url: str,
    testy_root: Path,
    original: Path,
    trap: Path | None,
    render_png: Path,
    resave_psd: Path,
    trap_png: Path,
    mutated_png: Path,
    suffix: str,
    progress: Callable[[str], None] = lambda stage: None,
) -> dict:
    upload_base = f"{base_url}/testy-upload?name="
    notes: list[str] = []
    try:
        # The forced-text mutation is deliberately NOT requested: Photopea's script
        # engine hangs on textItem.contents assignment for some documents (no "done"
        # ever returns), and its DOM never matched text layers reliably. Render +
        # structural comparisons are the value here.
        progress("Photopea: open + export")
        result = _run_host_page(
            base_url,
            {
                "file": f"{base_url}/{_rel_url(testy_root, original)}",
                "upload": upload_base,
                "render": _rel_url(testy_root, render_png),
                "resave": _rel_url(testy_root, resave_psd),
            },
            [render_png, resave_psd],
        )
        if trap is not None:
            progress("Photopea: trap render")
            try:
                _run_host_page(
                    base_url,
                    {
                        "file": f"{base_url}/{_rel_url(testy_root, trap)}",
                        "upload": upload_base,
                        "render": _rel_url(testy_root, trap_png),
                    },
                    [trap_png],
                )
            except PhotopeaError as error:
                notes.append(f"trap: {error}")
        return {"ok": True, "opens": "ok", "notes": notes}
    except PhotopeaError as error:
        return {"ok": False, "opens": "fail", "error": str(error), "notes": notes}
    except Exception as error:
        return {"ok": False, "opens": "fail", "error": f"driver crash: {error}", "notes": notes}


def cleanup() -> None:
    global _driver
    if _driver is not None:
        try:
            _driver.quit()
        except Exception:
            pass
        _driver = None
