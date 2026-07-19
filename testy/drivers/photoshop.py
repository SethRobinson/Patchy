"""Photoshop 2026 COM driver: ground truth and resave analysis.

One DoJavaScript call per probed file keeps every script self-contained (open,
inspect, render, close-no-save) so a crash mid-script never leaks documents into
later probes. Techniques follow docs/ps-compat.md: DialogModes.NO, pixel ruler
and type units, close only documents the script opened, and the copy-merged
fallback for files whose smart-object blocks make saveAs report a disk error.
"""

from __future__ import annotations

import json
import subprocess
import threading
import time
from pathlib import Path

# A single scripted probe (open + flatten + save PNG + resave PSD + manifest walk) can
# legitimately take ~20-30s on a 30 MB file, so this hang watchdog is a generous safety
# net for a genuinely stuck modal, NOT a per-op deadline. A wedged engine (the common
# failure) returns error 8000 instantly, so it is handled fast by restart-and-retry.
SCRIPT_WATCHDOG_SECONDS = 120

# ExtendScript is ES3: no JSON object, so the probe builds its JSON by hand via q().
_PROBE_JSX = r"""
(function () {
  app.displayDialogs = DialogModes.NO;
  try { app.preferences.rulerUnits = Units.PIXELS; } catch (e) {}
  try { app.preferences.typeUnits = TypeUnits.PIXELS; } catch (e) {}

  var INPUT = new File(%(input)s);
  var RENDER_PNG = %(render_png)s;
  var RESAVE_PSD = %(resave_psd)s;
  var MUTATE_SUFFIX = %(mutate_suffix)s;
  var MUTATED_PNG = %(mutated_png)s;

  function q(s) {
    s = String(s);
    var r = '';
    for (var i = 0; i < s.length; i++) {
      var c = s.charAt(i);
      var o = s.charCodeAt(i);
      if (c == '"' || c == '\\') { r += '\\' + c; }
      else if (o < 32) { r += '\\u' + ('000' + o.toString(16)).slice(-4); }
      else { r += c; }
    }
    return '"' + r + '"';
  }

  function layerDescriptor(id) {
    var ref = new ActionReference();
    ref.putIdentifier(charIDToTypeID('Lyr '), id);
    return executeActionGet(ref);
  }

  function descBool(d, sid) {
    var t = stringIDToTypeID(sid);
    try { return d.hasKey(t) ? d.getBoolean(t) : false; } catch (e) { return false; }
  }

  function walk(layers, path, out) {
    for (var i = 0; i < layers.length; i++) {
      var L = layers[i];
      var p = path === '' ? String(i) : path + '/' + i;
      var isGroup = (L.typename == 'LayerSet');
      var kind = isGroup ? 'GROUP' : 'UNKNOWN';
      if (!isGroup) {
        try { kind = String(L.kind).replace('LayerKind.', ''); } catch (e) {}
      }
      var d = null;
      try { d = layerDescriptor(L.id); } catch (e) {}
      var hasFX = false, fxVisible = false, userMask = false, vectorMask = false;
      if (d !== null) {
        hasFX = d.hasKey(stringIDToTypeID('layerEffects'));
        fxVisible = descBool(d, 'layerFXVisible');
        userMask = descBool(d, 'hasUserMask');
        vectorMask = descBool(d, 'hasVectorMask');
      }
      var b = [0, 0, 0, 0];
      try {
        b = [L.bounds[0].as('px'), L.bounds[1].as('px'), L.bounds[2].as('px'), L.bounds[3].as('px')];
      } catch (e) {}
      var clipped = false;
      if (!isGroup) { try { clipped = L.grouped; } catch (e) {} }
      var opacity = 100;
      try { opacity = Math.round(L.opacity * 100) / 100; } catch (e) {}
      var blend = '';
      try { blend = String(L.blendMode).replace('BlendMode.', ''); } catch (e) {}
      var entry = '{"path":' + q(p) + ',"name":' + q(L.name) + ',"kind":' + q(kind) +
        ',"group":' + (isGroup ? 'true' : 'false') +
        ',"visible":' + (L.visible ? 'true' : 'false') +
        ',"opacity":' + opacity +
        ',"blend":' + q(blend) +
        ',"clipped":' + (clipped ? 'true' : 'false') +
        ',"bounds":[' + Math.round(b[0]) + ',' + Math.round(b[1]) + ',' + Math.round(b[2]) + ',' + Math.round(b[3]) + ']' +
        ',"userMask":' + (userMask ? 'true' : 'false') +
        ',"vectorMask":' + (vectorMask ? 'true' : 'false') +
        ',"fx":' + ((hasFX && fxVisible) ? 'true' : 'false') +
        ',"fxPresent":' + (hasFX ? 'true' : 'false');
      if (!isGroup && kind == 'TEXT') {
        var contents = '', fontName = '', textSize = 0;
        try { contents = L.textItem.contents; } catch (e) {}
        try { fontName = String(L.textItem.font); } catch (e) {}
        try { textSize = L.textItem.size.as ? L.textItem.size.as('px') : Number(L.textItem.size); } catch (e) {}
        entry += ',"text":' + q(contents) + ',"font":' + q(fontName) +
                 ',"textSize":' + (Math.round(textSize * 100) / 100);
      }
      entry += '}';
      out.push(entry);
      if (isGroup) { walk(L.layers, p, out); }
    }
  }

  function pngOptions() {
    var o = new PNGSaveOptions();
    o.compression = 6;
    o.interlaced = false;
    return o;
  }

  // Render the document's flattened appearance to PNG. Returns 'ok', 'fallback', or
  // an error string. The fallback path is the documented copy-merged workaround for
  // files whose damaged smart-object references make duplicate/saveAs fail.
  function renderTo(doc, pngPath) {
    var dup = null;
    try {
      dup = doc.duplicate();
      dup.flatten();
      if (dup.mode != DocumentMode.RGB && dup.mode != DocumentMode.GRAYSCALE) {
        dup.changeMode(ChangeMode.RGB);
      }
      dup.saveAs(new File(pngPath), pngOptions(), true, Extension.LOWERCASE);
      dup.close(SaveOptions.DONOTSAVECHANGES);
      return 'ok';
    } catch (e) {
      try { if (dup !== null) { dup.close(SaveOptions.DONOTSAVECHANGES); } } catch (e2) {}
      try {
        doc.selection.selectAll();
        doc.selection.copy(true);
        var flat = app.documents.add(doc.width, doc.height, doc.resolution, 'testy_flat',
                                     NewDocumentMode.RGB, DocumentFill.WHITE);
        flat.paste();
        flat.flatten();
        flat.saveAs(new File(pngPath), pngOptions(), true, Extension.LOWERCASE);
        flat.close(SaveOptions.DONOTSAVECHANGES);
        return 'fallback';
      } catch (e3) {
        return 'render-error: ' + e3;
      }
    }
  }

  // Append the suffix to every unlocked text layer; Photoshop re-lays-out on
  // assignment. Mirrors Patchy's --append-text (pixel-locked layers skipped).
  function mutateText(layers, suffix, counter) {
    for (var i = 0; i < layers.length; i++) {
      var L = layers[i];
      if (L.typename == 'LayerSet') { mutateText(L.layers, suffix, counter); continue; }
      var kind = '';
      try { kind = String(L.kind); } catch (e) {}
      if (kind != 'LayerKind.TEXT') { continue; }
      // Only the full lock blocks a contents edit: Photoshop reports pixelsLocked=true
      // for EVERY type layer (painting is inherently locked there), so checking it
      // would skip all text.
      var locked = false;
      try { locked = L.allLocked; } catch (e) {}
      if (locked) { continue; }
      try {
        L.textItem.contents = L.textItem.contents + suffix;
        counter.n++;
      } catch (e) { counter.errors++; }
    }
  }

  var opened = null;
  try {
    opened = app.open(INPUT);
  } catch (e) {
    return '{"ok":false,"error":' + q(e) + '}';
  }
  try {
    var entries = [];
    walk(opened.layers, '', entries);
    var renderStatus = 'skipped';
    if (RENDER_PNG !== null) {
      renderStatus = renderTo(opened, RENDER_PNG);
    }
    var resaveStatus = 'skipped';
    if (RESAVE_PSD !== null) {
      // Before any mutation: the resave must reflect the file as opened.
      try {
        opened.saveAs(new File(RESAVE_PSD), new PhotoshopSaveOptions(), true, Extension.LOWERCASE);
        resaveStatus = 'ok';
      } catch (e) { resaveStatus = 'resave-error: ' + e; }
    }
    var mutateCount = -1, mutateErrors = 0, mutatedStatus = 'skipped';
    if (MUTATE_SUFFIX !== null) {
      var counter = { n: 0, errors: 0 };
      mutateText(opened.layers, MUTATE_SUFFIX, counter);
      mutateCount = counter.n;
      mutateErrors = counter.errors;
      if (MUTATED_PNG !== null) {
        mutatedStatus = renderTo(opened, MUTATED_PNG);
      }
    }
    var result = '{"ok":true,"width":' + opened.width.as('px') + ',"height":' + opened.height.as('px') +
      ',"resolution":' + opened.resolution +
      ',"render":' + q(renderStatus) +
      ',"resave":' + q(resaveStatus) +
      ',"mutated":' + q(mutatedStatus) +
      ',"mutateCount":' + mutateCount +
      ',"mutateErrors":' + mutateErrors +
      ',"layers":[' + entries.join(',') + ']}';
    opened.close(SaveOptions.DONOTSAVECHANGES);
    return result;
  } catch (e) {
    try { opened.close(SaveOptions.DONOTSAVECHANGES); } catch (e2) {}
    return '{"ok":false,"error":' + q('probe-error: ' + e) + '}';
  }
})();
"""


def _js_string(value: str | None) -> str:
    if value is None:
        return "null"
    escaped = str(value).replace("\\", "/").replace('"', '\\"')
    return f'"{escaped}"'


class PhotoshopDriver:
    def __init__(self) -> None:
        self._app = None

    def _application(self):
        if self._app is None:
            import win32com.client

            # The first dispatch launches Photoshop (~30s); subsequent calls reuse it.
            self._app = win32com.client.Dispatch("Photoshop.Application")
        return self._app

    def restart(self) -> None:
        """Fully restart Photoshop. Photoshop's scripting engine wedges during long
        sessions into a state where EVERY app.open returns error 8000 ("open options
        are incorrect") regardless of the file - the only cure is a restart, verified
        July 2026 (a control file that opened fine minutes earlier fails identically
        once wedged, and opens fine again after this)."""
        try:
            if self._app is not None:
                self._app.Quit()
        except Exception:
            pass
        try:
            subprocess.run(["taskkill", "/IM", "Photoshop.exe", "/F"],
                           capture_output=True, timeout=30)
        except Exception:
            pass
        self._app = None
        time.sleep(3.0)
        # Force a fresh launch now so the wait is spent here, not mid-probe.
        try:
            _ = self._application().Version
        except Exception:
            pass

    def version(self) -> str:
        try:
            return str(self._application().Version)
        except Exception:
            return "unknown"

    def probe(
        self,
        psd_path: Path,
        render_png: Path | None,
        mutate_suffix: str | None = None,
        mutated_png: Path | None = None,
        resave_psd: Path | None = None,
    ) -> dict:
        """Open psd_path; return manifest + render statuses as a dict (ok=False on failure).

        On ANY failure the whole Photoshop instance is restarted and the probe retried
        once: the dominant failure mode is a session-wide engine wedge (every open
        fails until restart), not a bad file, so restarting cures it in one shot.
        """
        result = self._probe_once(psd_path, render_png, mutate_suffix, mutated_png, resave_psd)
        if result.get("ok"):
            return result
        self.restart()
        retry = self._probe_once(psd_path, render_png, mutate_suffix, mutated_png, resave_psd)
        if retry.get("ok"):
            return retry
        retry["error"] = (f"{retry.get('error', 'unknown')} "
                          "(persisted even after a full Photoshop restart - this file "
                          "genuinely fails Photoshop's scripted open)")
        return retry

    def _probe_once(
        self,
        psd_path: Path,
        render_png: Path | None,
        mutate_suffix: str | None,
        mutated_png: Path | None,
        resave_psd: Path | None,
    ) -> dict:
        jsx = _PROBE_JSX % {
            "input": _js_string(str(psd_path)),
            "render_png": _js_string(str(render_png)) if render_png is not None else "null",
            "resave_psd": _js_string(str(resave_psd)) if resave_psd is not None else "null",
            "mutate_suffix": _js_string(mutate_suffix),
            "mutated_png": _js_string(str(mutated_png)) if mutated_png is not None else "null",
        }
        # Hang watchdog: a stuck modal would block DoJavaScript forever. A side timer
        # force-kills Photoshop.exe on timeout, which makes the blocked COM call raise
        # instead. (Only trips on true hangs; see SCRIPT_WATCHDOG_SECONDS.)
        hung = {"killed": False}

        def watchdog() -> None:
            hung["killed"] = True
            try:
                subprocess.run(["taskkill", "/IM", "Photoshop.exe", "/F"],
                               capture_output=True, timeout=30)
            except Exception:
                pass

        timer = threading.Timer(SCRIPT_WATCHDOG_SECONDS, watchdog)
        timer.start()
        try:
            app = self._application()
            raw = app.DoJavaScript(jsx)
        except Exception as error:  # COM-level failure (crash, watchdog kill, busy modal)
            self._app = None
            if hung["killed"]:
                return {"ok": False, "error": f"photoshop hung >{SCRIPT_WATCHDOG_SECONDS}s; killed"}
            return {"ok": False, "error": f"com-error: {error}"}
        finally:
            timer.cancel()
        try:
            return json.loads(raw)
        except Exception:
            return {"ok": False, "error": f"unparseable probe result: {raw[:500]!r}"}
