"""Affinity (Canva unified app, 3.2+) driver: built-in JavaScript automation.

Since 3.2 the app ships a JavaScript SDK reachable through a local MCP endpoint
(plain JSON-RPC over SSE on [::1]:6767 - see testy/affinity_js.py; no AI or
tokens involved). This driver runs one script per document: Document.load()
the staged PSD, doc.export() a full-res PNG render and a PSD resave, and report
per-leg success plus timings through console output. That replaces the old
background-UIA quick-export automation (toggle-pattern popups, format chips,
shell Save As dialogs), which was slow (50s relaunch cooldowns, 8s+ panel
materialization) and brittle. The old driver's hard-won lifecycle rules still
apply and live on in affinity_js.quit_app().

Requirements and constraints (verified on 3.2.3.4646, July 2026):

- The user must enable Affinity's connector (Settings > AI connector) with
  scripting + filesystem access allowed; the driver fails each cell with an
  actionable message when the port is closed or scripts return NOT_ALLOWED.
- Affinity scripting may only touch files under the Desktop, so inputs are
  staged to Desktop/testy-affinity-work/ and outputs moved back to the run dir.
- Document.close is NOT_IMPLEMENTED on Windows: opened documents accumulate as
  tabs. An instance this driver launched is restarted after MAX_OPEN_DOCS
  documents and quit at cleanup() (imported docs raise per-document save
  prompts on quit; quit_app dismisses them). A pre-existing user instance is
  reused but never quit, so its tabs stay open - a note says so.
- The PSD resave uses the "PSD (preserve editability)" export preset (closest
  to "keep native objects native", which is what the preservation leg scores);
  preset names are resolved by enumeration with a prefix fallback in case a
  future version renames them.
- The trap leg stays skipped: Affinity re-renders layers by design, so the
  baked-composite trap proves nothing (same policy as the UIA driver).
"""

from __future__ import annotations

import shutil
import time
from pathlib import Path
from typing import Callable

import affinity_js
from affinity_js import AffinityJs, AffinityJsError, ConnectorOffError

# Restart an owned instance after this many accumulated document tabs (close is
# unavailable; a restart bounds memory). Relaunches are cheap now: no file
# argument rides along, so the old 50s argument-drop cooldown does not apply.
MAX_OPEN_DOCS = 10
# One script covers load + both export legs; generous ceiling for huge PSDs.
CELL_TIMEOUT_SECONDS = 240

STAGING_DIR_NAME = "testy-affinity-work"

_client: AffinityJs | None = None
_we_own_instance = False
_notes_on_reuse_emitted = False


def _staging_dir() -> Path:
    directory = Path.home() / "Desktop" / STAGING_DIR_NAME
    directory.mkdir(parents=True, exist_ok=True)
    return directory


def _ensure_client(exe: Path, log: Callable[[str], None]) -> AffinityJs:
    global _client, _we_own_instance
    if _client is not None and _client.alive and affinity_js.port_open():
        return _client
    _client = None
    will_launch = not affinity_js.port_open() and not affinity_js.app_running()
    client = affinity_js.connect(log=log, exe=exe)
    if will_launch:
        _we_own_instance = True
    _client = client
    return client


def _restart(exe: Path, log: Callable[[str], None]) -> None:
    global _client
    log("restarting Affinity (document-tab limit reached)")
    affinity_js.quit_app(log=log)
    _client = None
    time.sleep(3.0)


def _cell_script(src: Path, png_out: Path, psd_out: Path) -> str:
    s = affinity_js.js_string
    return f"""
const {{ app }} = require('/application');
const {{ Document, FileExportOptions }} = require('/document.js');
const R = {{ legs: {{}} }};
function pickPreset(prefix, exact) {{
    const names = [];
    FileExportOptions.enumeratePresetNames(function (n) {{ names.push(n); return 0; }});
    if (names.indexOf(exact) >= 0) return exact;
    for (let i = 0; i < names.length; i++) {{
        if (names[i].indexOf(prefix) === 0) return names[i];
    }}
    return null;
}}
function runLeg(doc, key, out, prefix, exact) {{
    const t = Date.now();
    const entry = {{ ok: false }};
    R.legs[key] = entry;
    try {{
        const name = pickPreset(prefix, exact);
        if (!name) {{ entry.error = "no export preset matching " + prefix; return; }}
        if (name !== exact) entry.preset = name;
        const recs = doc.export(out, FileExportOptions.createWithPresetName(name),
                                null, null);
        recs.enumerate(function (r) {{
            entry.ok = r.isSuccess;
            if (r.hasWarnings) entry.warn = true;
            if (!r.isSuccess) {{
                try {{
                    entry.error = r.errorMessage.title + ": " + r.errorMessage.reason;
                }} catch (e) {{ entry.error = "export reported failure"; }}
            }}
            return 0;
        }});
        entry.ms = Date.now() - t;
    }} catch (e) {{ entry.error = String(e); }}
}}
try {{
    const t0 = Date.now();
    const doc = Document.load({s(src)});
    R.loadMs = Date.now() - t0;
    runLeg(doc, "png", {s(png_out)}, "PNG", "PNG");
    runLeg(doc, "psd", {s(psd_out)}, "PSD", "PSD (preserve editability)");
    R.openDocs = app.documents.all.length;
}} catch (e) {{
    R.error = String(e);
}}
console.log("@@RESULT " + JSON.stringify(R));
"""


def _interpret_error(message: str) -> str:
    if "NOT_ALLOWED" in message:
        return ("Affinity scripting refused (NOT_ALLOWED): allow scripting and "
                "filesystem access in Affinity's connector settings")
    if "PERMISSION_DENIED" in message:
        return ("Affinity denied file access (PERMISSION_DENIED): scripting can "
                "only touch paths under the Desktop")
    if "INAPPROPRIATE_FILE_TYPE_OR_FORMAT" in message:
        return ("Affinity cannot open this file "
                "(INAPPROPRIATE_FILE_TYPE_OR_FORMAT; the app's own import "
                "refuses it, not an automation failure)")
    return message


def export_all(
    exe: Path,
    original: Path,
    trap: Path | None,
    render_png: Path,
    resave_psd: Path,
    trap_png: Path,
    progress: Callable[[str], None] = lambda stage: None,
) -> dict:
    global _notes_on_reuse_emitted
    log_lines: list[str] = []

    def log(message: str) -> None:
        log_lines.append(message)

    # Stage under the real document stem (staged copies are all "original.psd";
    # the layout is files/<real stem>/_staged/original.psd) - meaningful tab
    # titles and unique staging names.
    real_stem = original.parent.parent.name if original.stem == "original" else original.stem
    staging = _staging_dir()
    input_file = staging / f"{real_stem}{original.suffix}"
    png_staged = staging / f"{real_stem}-render.png"
    psd_staged = staging / f"{real_stem}-resave.psd"
    for stale in (png_staged, psd_staged):
        stale.unlink(missing_ok=True)
    shutil.copyfile(original, input_file)

    try:
        progress("Affinity: connecting")
        client = _ensure_client(exe, log)
        if not _we_own_instance and not _notes_on_reuse_emitted:
            log("reusing the running Affinity instance; documents opened by this "
                "run will stay open as tabs (close is unavailable to scripts)")
            _notes_on_reuse_emitted = True

        progress(f"Affinity: processing {real_stem}")
        script = _cell_script(input_file, png_staged, psd_staged)
        try:
            result = client.execute_json(script, timeout=CELL_TIMEOUT_SECONDS)
        except AffinityJsError:
            # One reconnect+retry: the SSE stream can drop (app settling after
            # a cold start, connector hiccup). A dead stream during a load can
            # leave a duplicate tab behind, which is harmless.
            if client.alive:
                raise
            log("MCP stream dropped; reconnecting for one retry")
            global _client
            _client = None
            client = _ensure_client(exe, log)
            result = client.execute_json(script, timeout=CELL_TIMEOUT_SECONDS)

        if "error" in result:
            return {"ok": False, "opens": "fail",
                    "error": _interpret_error(result["error"]), "notes": log_lines}

        legs = result.get("legs", {})
        png = legs.get("png", {})
        psd = legs.get("psd", {})
        log(f"load {result.get('loadMs', '?')}ms, "
            f"png {png.get('ms', '?')}ms, psd {psd.get('ms', '?')}ms, "
            f"tabs {result.get('openDocs', '?')}")
        for key, entry in legs.items():
            if entry.get("preset"):
                log(f"{key} leg used preset '{entry['preset']}'")

        moved_ok = True
        if png.get("ok") and png_staged.exists() and png_staged.stat().st_size > 0:
            render_png.parent.mkdir(parents=True, exist_ok=True)
            shutil.move(str(png_staged), render_png)
        else:
            moved_ok = False
        if psd.get("ok") and psd_staged.exists() and psd_staged.stat().st_size > 0:
            resave_psd.parent.mkdir(parents=True, exist_ok=True)
            shutil.move(str(psd_staged), resave_psd)

        if trap is not None:
            log("trap: skipped (Affinity re-renders layers by design; the "
                "baked-composite trap proves nothing)")

        # Restart an owned instance once tabs pile up (close is unavailable).
        open_docs = result.get("openDocs")
        if (_we_own_instance and isinstance(open_docs, int)
                and open_docs >= MAX_OPEN_DOCS):
            _restart(exe, log)

        if not moved_ok:
            detail = png.get("error", "no output produced")
            return {"ok": False, "opens": "ok",
                    "error": f"PNG leg: {_interpret_error(detail)}",
                    "notes": log_lines}
        notes = list(log_lines)
        if not psd.get("ok"):
            notes.append(f"psd: {_interpret_error(psd.get('error', 'failed'))}")
        # Partial successes (a failed PSD leg) must not be cached, or the
        # missing scores freeze into every later run.
        return {"ok": True, "opens": "ok", "notes": notes,
                "cacheable": bool(png.get("ok") and psd.get("ok")
                                  and resave_psd.exists())}
    except ConnectorOffError as error:
        return {"ok": False, "opens": "fail",
                "error": f"Affinity connector unavailable: {error}",
                "notes": log_lines}
    except AffinityJsError as error:
        return {"ok": False, "opens": "fail",
                "error": _interpret_error(str(error)), "notes": log_lines}
    except Exception as error:  # never let the harness die on driver bugs
        return {"ok": False, "opens": "fail",
                "error": f"driver crash: {error}", "notes": log_lines}
    finally:
        input_file.unlink(missing_ok=True)


def cleanup() -> None:
    """Quit an instance this run launched (never a user's own) and clear the
    Desktop staging folder."""
    global _client, _we_own_instance, _notes_on_reuse_emitted
    if _we_own_instance:
        try:
            affinity_js.quit_app()
        except Exception:
            pass
        _we_own_instance = False
    _client = None
    _notes_on_reuse_emitted = False
    staging = Path.home() / "Desktop" / STAGING_DIR_NAME
    if staging.exists():
        shutil.rmtree(staging, ignore_errors=True)
