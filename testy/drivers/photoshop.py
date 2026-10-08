"""Photoshop 2026 COM driver: ground truth and resave analysis.

One DoJavaScript call per probed file keeps every script self-contained (open,
inspect, render, close-no-save) so a crash mid-script never leaks documents into
later probes. Techniques follow docs/ps-compat.md: DialogModes.NO, pixel ruler
and type units, close only documents the script opened, and the copy-merged
fallback for files whose smart-object blocks make saveAs report a disk error.

DialogModes.NO does not reach every dialog: some files make Photoshop raise a
modal alert from inside app.open ("This file contains file info data which
cannot be read and has been ignored"), and a blocked DoJavaScript call has no
way to answer it. Every probe therefore runs under a win_dialogs.DialogGuard,
which acknowledges recognized alerts from outside the COM call and carries the
hang watchdog.
"""

from __future__ import annotations

import json
import hashlib
import subprocess
import time
from pathlib import Path
from typing import Callable

try:
    import win_dialogs
except ModuleNotFoundError:  # running this file directly, e.g. for --selftest
    import sys

    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
    import win_dialogs

IMAGE_NAME = "Photoshop.exe"

# A single scripted probe (open + flatten + save PNG + resave PSD + manifest walk) can
# legitimately take ~20-30s on a 30 MB file, so this hang watchdog is a generous safety
# net for a genuinely stuck modal, NOT a per-op deadline. A wedged engine (the common
# failure) returns error 8000 instantly, so it is handled fast by restart-and-retry.
# Every dialog the guard clears restarts the countdown: that is Photoshop making
# progress, not hanging.
SCRIPT_WATCHDOG_SECONDS = 120

# Photoshop saves its preferences while it shuts down, so a force-kill inside that
# window truncates them. Every launch afterwards dies at init with "Could not
# initialize Photoshop because an unexpected end-of-file was encountered" while COM
# reports nothing but CO_E_SERVER_EXEC_FAILURE. That cost an overnight run in July
# 2026: a zero-length Workspace Prefs.psp made 120 files measure against a Photoshop
# that could no longer start (the cure was deleting the empty prefs so Photoshop
# rebuilds them). A restart therefore asks for a quit and gives the process this long
# to finish writing before anything forces it.
QUIT_GRACE_SECONDS = 30.0

# HRESULTs that mean the COM server never started, as opposed to Photoshop running and
# rejecting the file. They are a property of the machine, never of the PSD.
LAUNCH_FAILURE_HRESULTS = frozenset({
    -2146959355,  # 0x80080005 CO_E_SERVER_EXEC_FAILURE
    -2147221164,  # 0x80040154 REGDB_E_CLASSNOTREG
    -2147221005,  # 0x800401F3 CO_E_CLASSSTRING (the ProgID stopped resolving)
})

# Probes that died at launch before the driver declares Photoshop unavailable and stops
# trying. Each probe already retries once behind a full restart, so two of them is four
# launch attempts: enough to tell a broken Photoshop from a transient, and cheap enough
# that a run notices in minutes instead of hours.
LAUNCH_FAILURE_LIMIT = 2

# ExtendScript is ES3: no JSON object, so the probe builds its JSON by hand via q().
_PROBE_JSX = r"""
(function () {
  app.displayDialogs = DialogModes.NO;
  try { app.preferences.rulerUnits = Units.PIXELS; } catch (e) {}
  try { app.preferences.typeUnits = TypeUnits.PIXELS; } catch (e) {}

  var INPUT = new File(%(input)s);
  var RENDER_PNG = %(render_png)s;
  // A 16-bit document also gets its render saved at 16 bits here, for the deep
  // precision comparison (analyze.compare_deep_renders).
  var RENDER16_PNG = %(render16_png)s;
  var deepStatus = 'skipped';
  var RESAVE_PSD = %(resave_psd)s;

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
        // Every face the layer uses, not just the first range's (textItem.font): the
        // Photopea driver hands these fonts over, and a mixed-font layer needs all.
        var faces = [];
        try {
          var styleRanges = layerDescriptor(L.id).getObjectValue(stringIDToTypeID('textKey'))
                                                 .getList(stringIDToTypeID('textStyleRange'));
          for (var r = 0; r < styleRanges.count; r++) {
            var rangeStyle = styleRanges.getObjectValue(r).getObjectValue(stringIDToTypeID('textStyle'));
            if (!rangeStyle.hasKey(stringIDToTypeID('fontPostScriptName'))) { continue; }
            var face = q(rangeStyle.getString(stringIDToTypeID('fontPostScriptName')));
            var known = false;
            for (var f = 0; f < faces.length; f++) { if (faces[f] == face) { known = true; } }
            if (!known) { faces.push(face); }
          }
        } catch (e) {}
        entry += ',"fonts":[' + faces.join(',') + ']';
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
  // The reference PNG is always 8-bit sRGB: what Photoshop shows, in the space every
  // comparison assumes. Saved as-is, a Grayscale document's PNG holds raw dot-gain
  // values with no profile (an editor that color-manages correctly then reads as
  // wrong), a 16-bit one a format the comparison misreads, and a Bitmap one cannot
  // be rendered at all (no direct Bitmap-to-RGB change, no Copy Merged). Each step
  // is best effort; 32-bit keeps Photoshop's own conversion on save. A 16-bit
  // document is saved to deepPath (when given) in sRGB at 16 bits first, just before
  // the drop to 8 bits, so both renders are the same picture.
  function normalizeForPng(dup, deepPath) {
    try { if (dup.mode == DocumentMode.BITMAP) { dup.changeMode(ChangeMode.GRAYSCALE); } } catch (e1) {}
    try { if (dup.mode != DocumentMode.RGB) { dup.changeMode(ChangeMode.RGB); } } catch (e2) {}
    if (dup.bitsPerChannel != BitsPerChannelType.THIRTYTWO) {
      try {
        dup.convertProfile('sRGB IEC61966-2.1', Intent.RELATIVECOLORIMETRIC, true, false);
      } catch (e3) {}
    }
    if (deepPath !== null && dup.bitsPerChannel == BitsPerChannelType.SIXTEEN) {
      try {
        dup.saveAs(new File(deepPath), pngOptions(), true, Extension.LOWERCASE);
        deepStatus = 'ok';
      } catch (e5) { deepStatus = 'deep-render-error: ' + e5; }
    }
    try {
      if (dup.bitsPerChannel == BitsPerChannelType.SIXTEEN) { dup.bitsPerChannel = BitsPerChannelType.EIGHT; }
    } catch (e4) {}
  }

  // Bits per channel as a number (Bitmap documents are 1).
  function depthOf(doc) {
    var bits = String(doc.bitsPerChannel);
    if (bits == 'BitsPerChannelType.ONE') { return 1; }
    if (bits == 'BitsPerChannelType.SIXTEEN') { return 16; }
    if (bits == 'BitsPerChannelType.THIRTYTWO') { return 32; }
    return 8;
  }

  // Photoshop shows a type layer from the raster cached in the file until the text
  // changes, and an old file's cache can differ from what this Photoshop would draw.
  // Writing each layer's own text descriptor back to it makes Photoshop lay the text
  // out afresh with nothing altered (style runs, transform and warp all ride in the
  // descriptor). Run on the flattened-for-render duplicate only, so the document
  // itself, and any resave of it, stays exactly as opened.
  function refreshText(doc, layers, counter) {
    for (var i = 0; i < layers.length; i++) {
      var L = layers[i];
      if (L.typename == 'LayerSet') { refreshText(doc, L.layers, counter); continue; }
      var kind = '';
      try { kind = String(L.kind); } catch (e) {}
      if (kind != 'LayerKind.TEXT') { continue; }
      var locked = false;
      try { locked = L.allLocked; } catch (e) {}
      if (locked) { continue; }
      try {
        var wasVisible = L.visible;
        doc.activeLayer = L;
        var ref = new ActionReference();
        ref.putEnumerated(charIDToTypeID('Lyr '), charIDToTypeID('Ordn'), charIDToTypeID('Trgt'));
        var textKey = executeActionGet(ref).getObjectValue(stringIDToTypeID('textKey'));
        var target = new ActionReference();
        target.putEnumerated(stringIDToTypeID('textLayer'), charIDToTypeID('Ordn'), charIDToTypeID('Trgt'));
        var set = new ActionDescriptor();
        set.putReference(charIDToTypeID('null'), target);
        set.putObject(charIDToTypeID('T   '), stringIDToTypeID('textLayer'), textKey);
        executeAction(charIDToTypeID('setd'), set, DialogModes.NO);
        // Selecting a hidden layer shows it; put it back.
        if (L.visible != wasVisible) { L.visible = wasVisible; }
        counter.n++;
      } catch (e) { counter.errors++; }
    }
  }

  // The reference render also re-renders embedded smart objects, whose pixels in the
  // file are a cache as well: opening the contents, making a change that leaves
  // nothing behind (a layer added and removed) and saving makes Photoshop render the
  // layer from its contents again, smart filters included. Only where that is
  // lossless: raster contents stored in a format a save does not degrade. Linked
  // files (nothing embedded to render) and vector contents (they open in another
  // app) keep their cache. Run on the duplicate only, like refreshText.
  // Layers that share one embedded document are all re-rendered by the first of them,
  // so each document is opened once (a 54-layer file with two embedded documents made
  // Photoshop open and save them 54 times); and the work stops after 30 seconds, well
  // inside the probe's hang watchdog, leaving the remaining layers on their cache.
  function refreshSmartObjects(doc, layers, counter) {
    for (var i = 0; i < layers.length; i++) {
      var L = layers[i];
      if (L.typename == 'LayerSet') { refreshSmartObjects(doc, L.layers, counter); continue; }
      var kind = '';
      try { kind = String(L.kind); } catch (e) {}
      if (kind != 'LayerKind.SMARTOBJECT') { continue; }
      var locked = false;
      try { locked = L.allLocked; } catch (e) {}
      if (locked) { counter.skipped++; continue; }
      var inner = null;
      try {
        var wasVisible = L.visible;
        doc.activeLayer = L;
        var ref = new ActionReference();
        ref.putEnumerated(charIDToTypeID('Lyr '), charIDToTypeID('Ordn'), charIDToTypeID('Trgt'));
        var so = executeActionGet(ref).getObjectValue(stringIDToTypeID('smartObject'));
        var placed = typeIDToStringID(so.getEnumerationValue(stringIDToTypeID('placed')));
        var linked = so.hasKey(stringIDToTypeID('linked')) && so.getBoolean(stringIDToTypeID('linked'));
        var name = so.hasKey(stringIDToTypeID('fileReference')) ? so.getString(stringIDToTypeID('fileReference')) : '';
        var contentId = so.hasKey(stringIDToTypeID('documentID')) ? so.getString(stringIDToTypeID('documentID')) : '';
        var dot = name.lastIndexOf('.');
        var ext = dot < 0 ? '' : name.substring(dot + 1).toLowerCase();
        var lossless = ext == '' || ext == 'psd' || ext == 'psb' || ext == 'png' || ext == 'tif' || ext == 'tiff';
        var shared = contentId != '' && counter.seen[contentId] === true;
        var late = (new Date()).getTime() - counter.started > 30000;
        if (placed != 'rasterizeContent' || linked || !lossless || shared || late) {
          if (L.visible != wasVisible) { L.visible = wasVisible; }
          if (shared) { counter.n++; } else { counter.skipped++; }
          continue;
        }
        if (contentId != '') { counter.seen[contentId] = true; }
        executeAction(stringIDToTypeID('placedLayerEditContents'), new ActionDescriptor(), DialogModes.NO);
        if (app.activeDocument == doc) { throw new Error('contents did not open'); }
        inner = app.activeDocument;
        var scratch = inner.artLayers.add();
        scratch.remove();
        inner.save();
        inner.close(SaveOptions.SAVECHANGES);
        inner = null;
        app.activeDocument = doc;
        if (L.visible != wasVisible) { L.visible = wasVisible; }
        counter.n++;
      } catch (e) {
        counter.errors++;
        try { if (inner !== null) { inner.close(SaveOptions.DONOTSAVECHANGES); } } catch (e2) {}
        try { app.activeDocument = doc; } catch (e3) {}
      }
    }
  }

  function renderTo(doc, pngPath, freshText, freshSmart) {
    var dup = null;
    try {
      dup = doc.duplicate();
      if (freshText) { refreshText(dup, dup.layers, freshText); }
      if (freshSmart) { refreshSmartObjects(dup, dup.layers, freshSmart); }
      dup.flatten();
      normalizeForPng(dup, RENDER16_PNG);
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

  // Inspect every style range before assigning contents. A missing face in a
  // later range can prompt even when textItem.font (the first range) is present.
  function textFontProblems(layers, out) {
    function problem(label) {
      for (var k = 0; k < out.length; k++) { if (out[k] == label) { return; } }
      out.push(label);
    }
    function checkFace(face, unavailable) {
      if (!face) { problem('unidentified text font'); return; }
      if (unavailable) { problem(face); return; }
      try { app.fonts.getByName(face); } catch (e) { problem(face); }
    }
    for (var i = 0; i < layers.length; i++) {
      var L = layers[i];
      if (L.typename == 'LayerSet') { textFontProblems(L.layers, out); continue; }
      var kind = '';
      try { kind = String(L.kind); } catch (e) {}
      if (kind != 'LayerKind.TEXT') { continue; }
      try { if (L.allLocked) { continue; } } catch (e) {}
      try {
        var d = layerDescriptor(L.id).getObjectValue(stringIDToTypeID('textKey'));
        var ranges = d.getList(stringIDToTypeID('textStyleRange'));
        if (ranges.count == 0) { problem('unidentified text font'); }
        for (var j = 0; j < ranges.count; j++) {
          var style = ranges.getObjectValue(j).getObjectValue(stringIDToTypeID('textStyle'));
          var fontKey = stringIDToTypeID('fontPostScriptName');
          var availableKey = stringIDToTypeID('fontAvailable');
          var face = style.hasKey(fontKey) ? style.getString(fontKey) : String(L.textItem.font);
          checkFace(face, style.hasKey(availableKey) && !style.getBoolean(availableKey));
        }
      } catch (e) {
        // Do not guess that a partially inspected mixed-font layer is safe.
        problem('could not inspect fonts in ' + L.name);
      }
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
    // The reference render re-renders text, unless a font the text needs is missing:
    // then Photoshop cannot draw it faithfully either and the cache is the reference.
    var freshText = {n: 0, errors: 0}, freshFonts = [];
    textFontProblems(opened.layers, freshFonts);
    var freshSmart = {n: 0, errors: 0, skipped: 0, seen: {}, started: (new Date()).getTime()};
    if (RENDER_PNG !== null) {
      renderStatus = renderTo(opened, RENDER_PNG, freshFonts.length ? null : freshText, freshSmart);
    }
    var resaveStatus = 'skipped';
    if (RESAVE_PSD !== null) {
      try {
        opened.saveAs(new File(RESAVE_PSD), new PhotoshopSaveOptions(), true, Extension.LOWERCASE);
        resaveStatus = 'ok';
      } catch (e) { resaveStatus = 'resave-error: ' + e; }
    }
    // Fonts the text needs that Photoshop lacks: it then cannot draw that text
    // faithfully either, so the reference keeps the baked pixels and nobody's own
    // text render is scored for this file.
    var missingFonts = freshFonts;
    var textFontsMissing = missingFonts.length ? 'Required fonts unavailable: ' + missingFonts.join(', ') : null;
    var missingJson = [];
    for (var m = 0; m < missingFonts.length; m++) { missingJson.push(q(missingFonts[m])); }
    var result = '{"ok":true,"width":' + opened.width.as('px') + ',"height":' + opened.height.as('px') +
      ',"resolution":' + opened.resolution +
      ',"depth":' + depthOf(opened) +
      ',"deepRender":' + q(deepStatus) +
      ',"render":' + q(renderStatus) +
      ',"resave":' + q(resaveStatus) +
      ',"textFontsMissing":' + (textFontsMissing === null ? 'null' : q(textFontsMissing)) +
      ',"freshText":' + freshText.n + ',"freshTextErrors":' + freshText.errors +
      ',"freshSmart":' + freshSmart.n + ',"freshSmartErrors":' + freshSmart.errors +
      ',"freshSmartSkipped":' + freshSmart.skipped +
      ',"missingFonts":[' + missingJson.join(',') + ']' +
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


def _js_path(path: Path | None) -> str:
    """A path as an ExtendScript `new File(...)` argument.

    The File constructor URI-DECODES what it is given, so a literal percent sign in a
    name is read as an escape sequence: `eco%20beret.psd` resolves to `eco beret.psd`,
    which does not exist, and app.open answers "Expected a reference to an existing
    File/Folder" for a file that is sitting right there. Pre-encoding every '%' as
    '%25' makes File() decode back to the real name (verified: File(".../eco%2520beret/
    x").fsName ends in `eco%20beret\\x` and exists=true). The run directory inherits the
    corpus file's stem, so this bites the render/resave targets too, not just the input.
    """
    if path is None:
        return "null"
    return _js_string(str(path).replace("%", "%25"))


def deep_render_path(render_png: Path | None) -> Path | None:
    """Where a probe writes the 16-bit render of a 16-bit document, beside the 8-bit
    one: render.png -> render16.png, roundtrip.png -> roundtrip16.png."""
    if render_png is None:
        return None
    return render_png.with_name(render_png.stem + "16" + render_png.suffix)


def _kill_photoshop() -> None:
    try:
        subprocess.run(["taskkill", "/IM", IMAGE_NAME, "/F"], capture_output=True, timeout=30)
    except Exception:
        pass


def _photoshop_is_running() -> bool:
    try:
        completed = subprocess.run(
            ["tasklist", "/FI", f"IMAGENAME eq {IMAGE_NAME}", "/NH"],
            capture_output=True, text=True, timeout=30)
    except Exception:
        return True  # cannot tell; say yes so the caller still force-kills
    return IMAGE_NAME.lower() in (completed.stdout or "").lower()


def _wait_for_photoshop_exit(seconds: float) -> bool:
    """True once no Photoshop.exe is left, False if one is still up at the deadline."""
    deadline = time.monotonic() + seconds
    while _photoshop_is_running():
        if time.monotonic() >= deadline:
            return False
        time.sleep(0.5)
    return True


def _is_launch_failure(error: Exception) -> bool:
    """Did this COM error mean Photoshop never started? pywin32 puts the HRESULT first."""
    args = getattr(error, "args", ())
    return bool(args) and args[0] in LAUNCH_FAILURE_HRESULTS


class PhotoshopDriver:
    def __init__(self, log: Callable[[str], None] | None = None) -> None:
        self._app = None
        self._log = log
        self._version: str | None = None
        self._font_cache_key: str | None = None
        self._launch_failures = 0
        self._unavailable_reason: str | None = None

    @property
    def unavailable(self) -> bool:
        """Photoshop will not start on this machine, so every further probe is waste."""
        return self._unavailable_reason is not None

    @property
    def unavailable_reason(self) -> str | None:
        return self._unavailable_reason

    def _application(self):
        if self._app is None:
            import win32com.client

            import config

            # The first dispatch launches Photoshop (~30s); subsequent calls reuse it.
            self._app = win32com.client.Dispatch(config.PHOTOSHOP_PROGID)
        return self._app

    def restart(self) -> None:
        """Fully restart Photoshop. Photoshop's scripting engine wedges during long
        sessions into a state where EVERY app.open returns error 8000 ("open options
        are incorrect") regardless of the file - the only cure is a restart, verified
        July 2026 (a control file that opened fine minutes earlier fails identically
        once wedged, and opens fine again after this)."""
        quit_sent = False
        try:
            if self._app is not None:
                self._app.Quit()
                quit_sent = True
        except Exception:
            pass
        # A clean quit is worth waiting for: Photoshop writes its preferences on the way
        # out, and a kill landing inside that write breaks every later launch (see
        # QUIT_GRACE_SECONDS). A wedged engine never reaches the write, so nothing is
        # lost by killing it at once, which is also what happens when Quit is refused.
        if not (quit_sent and _wait_for_photoshop_exit(QUIT_GRACE_SECONDS)):
            _kill_photoshop()
        self._app = None
        time.sleep(3.0)
        # Force a fresh launch now so the wait is spent here, not mid-probe.
        try:
            _ = self._application().Version
        except Exception:
            pass

    def version(self) -> str:
        """Memoized: the version is fixed for the session, and the ground-truth cache key
        asks for it once per file - which is one more launch attempt per file whenever
        Photoshop is down, and an "unknown" key that misses every cached result."""
        if self._version is not None:
            return self._version
        if self.unavailable:
            return "unknown"
        try:
            self._version = str(self._application().Version)
        except Exception:
            return "unknown"
        return self._version

    def font_cache_key(self) -> str:
        """Installing a missing font must invalidate the forced-text ground truth."""
        if self._font_cache_key is None:
            inventory = self._application().DoJavaScript(
                "(function(){var a=[];for(var i=0;i<app.fonts.length;i++){"
                "a.push(app.fonts[i].postScriptName);}a.sort();return a.join('\\n');})();")
            self._font_cache_key = hashlib.sha256(str(inventory).encode("utf-8")).hexdigest()[:16]
        return self._font_cache_key

    def probe(
        self,
        psd_path: Path,
        render_png: Path | None,
        resave_psd: Path | None = None,
    ) -> dict:
        """Open psd_path; return manifest + render statuses as a dict (ok=False on failure).

        On ANY failure the whole Photoshop instance is restarted and the probe retried
        once: the dominant failure mode is a session-wide engine wedge (every open
        fails until restart), not a bad file, so restarting cures it in one shot.

        The exception is a Photoshop that will not launch at all, which no restart can
        cure. Those failures are reported as the machine problem they are, and after
        LAUNCH_FAILURE_LIMIT of them the driver declares itself unavailable and answers
        instantly, so a caller stops buying two launch timeouts per file.
        """
        if self.unavailable:
            return {"ok": False, "launchFailure": True, "error": self._unavailable_reason}
        result = self._probe_once(psd_path, render_png, resave_psd)
        if result.get("ok"):
            self._launch_failures = 0
            return result
        self.restart()
        retry = self._probe_once(psd_path, render_png, resave_psd)
        # Dialogs the first attempt reported are part of this file's story even when
        # the retry is the one that carries the result.
        dialogs = list(result.get("dialogs") or [])
        for entry in retry.get("dialogs") or []:
            if entry not in dialogs:
                dialogs.append(entry)
        if dialogs:
            retry["dialogs"] = dialogs
        if retry.get("ok"):
            self._launch_failures = 0
            return retry
        if retry.get("launchFailure"):
            # Photoshop never ran, so this says nothing about the file and the "bad file"
            # wording below would be a lie. Count it instead: enough of these in a row
            # and there is no point starting another one.
            self._launch_failures += 1
            if self._launch_failures >= LAUNCH_FAILURE_LIMIT:
                self._unavailable_reason = (
                    f"Photoshop failed to start on {self._launch_failures} files in a row - "
                    f"{retry.get('error', 'unknown')}")
            return retry
        self._launch_failures = 0
        retry["error"] = (f"{retry.get('error', 'unknown')} "
                          "(persisted even after a full Photoshop restart - this file "
                          "genuinely fails Photoshop's scripted open)")
        # Photoshop ran and refused this file: news about the file, not evidence the app
        # is broken, so the orchestrator's circuit breaker skips it. A hang is the app.
        retry["fileRejected"] = not retry.get("hung")
        return retry

    def _probe_once(
        self,
        psd_path: Path,
        render_png: Path | None,
        resave_psd: Path | None,
    ) -> dict:
        jsx = _PROBE_JSX % {
            "input": _js_path(psd_path),
            "render_png": _js_path(render_png),
            "render16_png": _js_path(deep_render_path(render_png)),
            "resave_psd": _js_path(resave_psd),
        }
        # The guard answers modal alerts Photoshop raises behind the blocked COM call
        # (see the module docstring) and force-kills Photoshop.exe once nothing has
        # moved for SCRIPT_WATCHDOG_SECONDS, which makes that call raise instead of
        # blocking forever.
        guard = win_dialogs.DialogGuard(
            [IMAGE_NAME],
            watchdog_seconds=SCRIPT_WATCHDOG_SECONDS,
            on_timeout=_kill_photoshop,
            log=self._log,
        )
        try:
            with guard:
                app = self._application()
                raw = app.DoJavaScript(jsx)
        except Exception as error:  # COM-level failure (crash, watchdog kill, busy modal)
            self._app = None
            failure = {"ok": False, "error": self._failure_text(guard, error)}
            if guard.timed_out:
                failure["hung"] = True  # the app, not the file: a stuck modal or a wedge
            elif _is_launch_failure(error):
                failure["launchFailure"] = True
            return self._with_dialogs(guard, failure)
        try:
            return self._with_dialogs(guard, json.loads(raw))
        except Exception:
            return self._with_dialogs(
                guard, {"ok": False, "error": f"unparseable probe result: {raw[:500]!r}"})

    @staticmethod
    def _with_dialogs(guard: win_dialogs.DialogGuard, result: dict) -> dict:
        """Record the alerts Photoshop raised, so a warning it shrugged off is
        visible in the report instead of silently costing a few seconds."""
        if guard.dismissed:
            result["dialogs"] = list(guard.dismissed)
        return result

    @staticmethod
    def _failure_text(guard: win_dialogs.DialogGuard, error: Exception) -> str:
        if guard.timed_out:
            if guard.blocked:
                return (f"photoshop hung >{SCRIPT_WATCHDOG_SECONDS}s behind a dialog with no "
                        f"safe answer: {guard.blocked[-1]}; killed")
            return f"photoshop hung >{SCRIPT_WATCHDOG_SECONDS}s; killed"
        if _is_launch_failure(error):
            # The HRESULT only says the server never started. WHY it could not start is
            # in the alert the guard just cleared on the way through, and that is the
            # difference between a report someone can act on and a mystery.
            said = f"; Photoshop said: {guard.dismissed[-1]}" if guard.dismissed else ""
            return (f"Photoshop itself failed to start (com-error: {error}{said}) - a broken "
                    "Photoshop or machine state, not a property of this file")
        return f"com-error: {error}"


def _selftest() -> int:
    """Pin the decisions that keep a run honest, with no Photoshop and no corpus: which
    COM errors mean "the app never started", what a restart is allowed to force-kill,
    and when the driver stops trying. All three were wrong in July 2026 and cost a
    120-file overnight run."""
    import types

    failures: list[str] = []

    def check(name: str, ok: bool, detail: str = "") -> None:
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            failures.append(name)
            if detail:
                print(f"       {detail}")

    class _FakeComError(Exception):
        """pywin32 raises com_error with the HRESULT first; that is all we read."""

    launch_error = _FakeComError(-2146959355, "Server execution failed", None, None)
    script_error = _FakeComError(-2147352567, "Exception occurred.", None, None)

    print("classifying failures:")
    check("CO_E_SERVER_EXEC_FAILURE means Photoshop never started",
          _is_launch_failure(launch_error))
    check("REGDB_E_CLASSNOTREG means Photoshop never started",
          _is_launch_failure(_FakeComError(-2147221164, "Class not registered")))
    check("a scripting error does not", not _is_launch_failure(script_error))
    check("a plain exception does not", not _is_launch_failure(RuntimeError("boom")))

    print("wording:")
    alert = ('Adobe Photoshop: "Could not initialize Photoshop because an unexpected '
             'end-of-file was encountered." [OK]')
    guard = types.SimpleNamespace(timed_out=False, blocked=[], dismissed=[alert])
    text = PhotoshopDriver._failure_text(guard, launch_error)
    check("a launch failure names the alert Photoshop showed", "end-of-file" in text, text)
    check("a launch failure does not blame the file",
          "not a property of this file" in text, text)
    hung = types.SimpleNamespace(timed_out=True, blocked=[], dismissed=[])
    check("a watchdog kill still reads as a hang",
          "hung" in PhotoshopDriver._failure_text(hung, launch_error))

    print("paths into ExtendScript:")
    check("a percent in a name survives File()'s URI decoding",
          _js_path(Path(r"D:\runs\1\files\eco%20beret\_staged\original.psd"))
          == '"D:/runs/1/files/eco%2520beret/_staged/original.psd"',
          _js_path(Path(r"D:\runs\1\files\eco%20beret\_staged\original.psd")))
    check("an ordinary path is only slash-normalized",
          _js_path(Path(r"D:\runs\1\files\plain\render.png")) == '"D:/runs/1/files/plain/render.png"')
    check("a missing path is still null", _js_path(None) == "null")
    check("the text suffix is NOT path-encoded", _js_string("~50%~") == '"~50%~"')

    print("giving up:")
    driver = PhotoshopDriver()
    counts = {"probes": 0, "restarts": 0}

    def stub_launch_failure(*_args, **_kwargs) -> dict:
        counts["probes"] += 1
        return {"ok": False, "launchFailure": True,
                "error": "Photoshop itself failed to start (com-error: ...)"}

    driver._probe_once = stub_launch_failure
    driver.restart = lambda: counts.__setitem__("restarts", counts["restarts"] + 1)
    for _ in range(LAUNCH_FAILURE_LIMIT):
        driver.probe(Path("whatever.psd"), None)
    check(f"unavailable after {LAUNCH_FAILURE_LIMIT} files", driver.unavailable)
    spent = counts["probes"]
    later = driver.probe(Path("next.psd"), None)
    check("further probes cost no launch attempt", counts["probes"] == spent,
          f"{counts['probes']} probes, expected {spent}")
    check("further probes still report a launch failure", later.get("launchFailure") is True)
    check("... and say what happened", "failed to start" in (later.get("error") or ""))

    bad_file = PhotoshopDriver()
    bad_file._probe_once = lambda *a, **k: {"ok": False, "error": "com-error: (-2147352567,)"}
    bad_file.restart = lambda: None
    outcome = bad_file.probe(Path("bad.psd"), None)
    check("a file Photoshop rejects keeps its own wording",
          "genuinely fails" in outcome["error"], outcome["error"])
    check("a file Photoshop rejects does not disable the driver", not bad_file.unavailable)
    check("a file Photoshop rejects is the file's fault, not the app's",
          outcome.get("fileRejected") is True)

    hung_driver = PhotoshopDriver()
    hung_driver._probe_once = lambda *a, **k: {
        "ok": False, "hung": True, "error": f"photoshop hung >{SCRIPT_WATCHDOG_SECONDS}s; killed"}
    hung_driver.restart = lambda: None
    check("a hang is the app's fault, so the breaker still counts it",
          not hung_driver.probe(Path("slow.psd"), None).get("fileRejected"))

    print("shutting down:")
    saved = (globals()["_kill_photoshop"], globals()["_photoshop_is_running"],
             globals()["QUIT_GRACE_SECONDS"], time.sleep)
    killed: list[bool] = []
    globals()["_kill_photoshop"] = lambda: killed.append(True)
    globals()["QUIT_GRACE_SECONDS"] = 0.05
    time.sleep = lambda _seconds: None
    try:
        def driver_with_quit(quit_action: Callable[[], None]) -> PhotoshopDriver:
            made = PhotoshopDriver()
            made._app = types.SimpleNamespace(Quit=quit_action)
            made._application = lambda: types.SimpleNamespace(Version="27.8.0")
            return made

        quits: list[bool] = []
        globals()["_photoshop_is_running"] = lambda: False
        driver_with_quit(lambda: quits.append(True)).restart()
        check("a quit that lands is never force-killed", quits and not killed)

        killed.clear()
        globals()["_photoshop_is_running"] = lambda: True
        driver_with_quit(lambda: quits.append(True)).restart()
        check("a process still up at the deadline is force-killed", bool(killed))

        killed.clear()

        def refuse() -> None:
            raise _FakeComError(-2147418113, "Catastrophic failure")

        driver_with_quit(refuse).restart()
        check("a refused quit is force-killed at once", bool(killed))
    finally:
        (globals()["_kill_photoshop"], globals()["_photoshop_is_running"],
         globals()["QUIT_GRACE_SECONDS"], time.sleep) = saved

    print(f"\n{'FAILED: ' + ', '.join(failures) if failures else 'all checks passed'}")
    return 1 if failures else 0


if __name__ == "__main__":
    import sys

    if "--selftest" in sys.argv:
        raise SystemExit(_selftest())
    print("usage: python testy\\drivers\\photoshop.py --selftest")
    raise SystemExit(2)
