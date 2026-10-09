"""Report/dashboard writer.

report.html is one self-contained page dropped into each run directory. While the
run is live it polls status.json and fills the file-by-editor matrix in place;
after the run the same page (same file) is the frozen report. No external assets.
"""

from __future__ import annotations

import json
import os
import time
from pathlib import Path

_PAGE = r"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>Testy - PSD compatibility run</title>
<style>
  :root {
    --bg: #14161a; --panel: #1d2026; --panel2: #23262d; --text: #d8dade; --dim: #8b8f98;
    --good: #4fc26b; --warn: #d9a13c; --bad: #d95c4a; --accent: #5aa2e0; --line: #2e323a;
  }
  * { box-sizing: border-box; }
  /* The page is a fixed-height row: everything (header, summary, matrix) in a scroll
     container on the left and the detail panel beside it on the right. An open panel
     narrows the left side instead of covering its right-hand columns, so the matrix's
     horizontal scrollbar stays reachable. */
  body { margin: 0; background: var(--bg); color: var(--text);
         font: 13px/1.5 "Segoe UI", system-ui, sans-serif;
         display: flex; height: 100vh; overflow: hidden; }
  #page { flex: 1 1 auto; min-width: 0; overflow: auto; }
  header { padding: 14px 22px; border-bottom: 1px solid var(--line); display: flex;
           align-items: baseline; gap: 18px; flex-wrap: wrap; }
  header h1 { font-size: 17px; margin: 0; letter-spacing: .4px; }
  header .meta { color: var(--dim); font-size: 12px; }
  header .version { color: var(--dim); font-size: 12px; font-weight: 400; }
  #about { color: var(--dim); font-size: 12px; padding: 8px 22px 0; max-width: 1100px; }
  #about a { color: var(--accent); }
  #state-pill { padding: 2px 10px; border-radius: 10px; font-size: 11px; background: var(--panel2); }
  #state-pill.running { color: var(--warn); }
  #state-pill.done { color: var(--good); }
  #state-pill.canceled, #state-pill.interrupted { color: var(--bad); }
  #state-pill.paused { color: var(--accent); }
  #run-controls button { background: var(--panel2); color: var(--text); font-size: 12px;
    border: 1px solid var(--line); border-radius: 6px; padding: 3px 12px; margin-left: 6px;
    cursor: pointer; }
  #run-controls button:hover:enabled { border-color: var(--accent); }
  #run-controls button:disabled { color: var(--dim); cursor: default; }
  #run-controls .ctl-note { color: var(--dim); font-size: 11.5px; margin-left: 8px; }
  #back-link { color: var(--dim); text-decoration: none; font-size: 12px; padding: 2px 10px;
               border: 1px solid var(--line); border-radius: 6px; }
  #back-link:hover { color: var(--accent); border-color: var(--accent); }
  #summary { display: flex; gap: 12px; padding: 14px 22px; flex-wrap: wrap; }
  .card { background: var(--panel); border: 1px solid var(--line); border-radius: 8px;
          padding: 10px 14px; min-width: 168px; }
  .card h3 { margin: 0 0 4px; font-size: 13px; }
  .card .ver { color: var(--dim); font-size: 11px; margin-bottom: 6px; }
  .card .row { display: flex; justify-content: space-between; gap: 12px; font-size: 12px; }
  .card .row b { font-variant-numeric: tabular-nums; }
  .card.standing { min-width: 210px; }
  .card.standing .row { font-size: 13px; padding: 2px 0; }
  .card.standing .row.me span, .card.standing .row.me b { color: var(--accent); }
  main { padding: 0 22px 40px; }
  /* Every cell draws its own grid lines rather than collapsing them into the table's:
     a collapsed border belongs to the table, so Chromium leaves it behind when the
     header row pins itself and the pinned row arrives with no lines at all. */
  table.matrix { border-collapse: separate; border-spacing: 0; width: 100%; }
  .matrix th, .matrix td { border: 0 solid var(--line); border-width: 0 1px 1px 0;
                           padding: 7px 10px; text-align: left; vertical-align: top; }
  .matrix tr > :first-child { border-left-width: 1px; }
  .matrix th { background: var(--panel); font-weight: 600; }
  /* The column header row rides along at the top of the viewport, so a file far down
     the matrix still shows which editor each cell belongs to. */
  .matrix thead th { position: sticky; top: 0; z-index: 3; border-top-width: 1px; }
  .matrix td.file { max-width: 260px; overflow-wrap: anywhere; color: var(--text); }
  .matrix td.cell { min-width: 150px; cursor: pointer; background: var(--panel2); }
  .matrix td.cell:hover { outline: 1px solid var(--accent); }
  .status-line { display: flex; align-items: center; gap: 6px; }
  .dot { width: 9px; height: 9px; border-radius: 50%; background: var(--dim); flex: none; }
  .dot.ok { background: var(--good); } .dot.warn { background: var(--warn); }
  .dot.bad { background: var(--bad); } .dot.run { background: var(--accent);
    animation: pulse 1.1s ease-in-out infinite; }
  @keyframes pulse { 50% { opacity: .35; } }
  .nums { color: var(--dim); font-size: 11.5px; margin-top: 2px; }
  .flag { color: var(--bad); font-size: 11px; }
  .loss-banner { border: 1px solid var(--bad); background: rgba(217,92,74,.10); border-radius: 6px;
                 padding: 8px 12px; margin: 6px 0 12px; }
  .loss-banner b { color: var(--bad); }
  .keep-banner { border: 1px solid var(--good); background: rgba(79,194,107,.08); border-radius: 6px;
                 padding: 8px 12px; margin: 6px 0 12px; }
  .keep-banner b { color: var(--good); }
  /* The panel itself never scrolls (so the resize handle and close button stay put);
     #detail-body inside it is the scroll container. Closed, it takes no room. */
  #detail { position: relative; flex: none; width: min(880px, 92vw); max-width: 96vw;
            background: var(--panel); border-left: 1px solid var(--line); display: none; }
  #detail.open { display: block; }
  #detail-body { height: 100%; overflow: auto; padding: 18px 22px; }
  #detail-resizer { position: absolute; left: 0; top: 0; bottom: 0; width: 7px;
                    cursor: ew-resize; }
  #detail-resizer:hover, body.resizing #detail-resizer { background: var(--accent); opacity: .4; }
  body.resizing { user-select: none; cursor: ew-resize; }
  #detail h2 { margin: 0 0 2px; font-size: 15px; }
  #detail .sub { color: var(--dim); margin-bottom: 12px; font-size: 12px; }
  #detail .imgs { display: flex; gap: 10px; flex-wrap: wrap; margin: 10px 0 16px; }
  #detail figure { margin: 0; }
  #detail figcaption { color: var(--dim); font-size: 11px; margin-top: 3px; }
  #detail img { max-width: 260px; border: 1px solid var(--line); border-radius: 4px;
                background: #fff; image-rendering: auto; display: block; cursor: zoom-in; }
  /* A tiny render (a 4x4 blend-mode probe, a 1x1 file) is blown up by a whole number so
     there is something to see, with hard pixels and the factor in its caption. */
  #detail img.tiny { image-rendering: pixelated; }
  #detail figcaption .zoom { color: var(--accent); margin-left: 4px; }
  #detail table { border-collapse: collapse; margin: 6px 0 14px; width: 100%; font-size: 12px; }
  #detail th, #detail td { border: 1px solid var(--line); padding: 4px 8px; text-align: left; }
  #detail th { background: var(--panel2); }
  #detail .close { position: absolute; top: 10px; right: 14px; cursor: pointer; color: var(--dim);
                   font-size: 20px; border: 0; background: none; }
  #detail .retest-btn { background: var(--panel2); color: var(--text); font-size: 12px;
    border: 1px solid var(--line); border-radius: 6px; padding: 3px 12px; cursor: pointer; }
  #detail .retest-btn:hover:enabled { border-color: var(--accent); }
  #detail .retest-btn:disabled { color: var(--dim); cursor: default; }
  #detail .retest-note { color: var(--dim); font-size: 11.5px; margin-left: 8px; }
  .rerun-btn, .rerun-scope { background: var(--panel2); color: var(--text); border: 1px solid var(--line);
    border-radius: 4px; padding: 3px 7px; margin-top: 6px; font: inherit; }
  .rerun-btn { cursor: pointer; margin-left: 5px; }
  .rerun-btn:disabled, .rerun-scope:disabled { color: var(--dim); cursor: default; }
  .nums a { color: var(--accent); }
  .ok-text { color: var(--good); } .bad-text { color: var(--bad); } .warn-text { color: var(--warn); }
  .copyable { cursor: pointer; border-bottom: 1px dotted var(--dim); }
  .copyable:hover { color: var(--accent); }
  .copied-flash { color: var(--good); font-size: 11px; margin-left: 6px; }
  #known-toggle { padding: 0 22px 10px; font-size: 12px; color: var(--dim); }
  #known-toggle label { cursor: pointer; color: var(--text); }
  tr.known-limit td.file b { color: var(--dim); }
  #groups { margin: 0 0 18px; }
  #groups h2 { font-size: 14px; margin: 0 0 6px; }
  #groups table { border-collapse: collapse; font-size: 12px; }
  #groups th, #groups td { border: 1px solid var(--line); padding: 4px 10px; text-align: left; }
  #groups th { background: var(--panel); }
  #groups tr.pick { cursor: pointer; }
  #groups tr.pick:hover td, #groups tr.on td { background: var(--panel2); }
  #groups tr.on td:first-child { color: var(--accent); }
  #history { margin-top: 28px; }
  #history h2 { font-size: 14px; }
  #history table { border-collapse: collapse; font-size: 12px; }
  #history th, #history td { border: 1px solid var(--line); padding: 4px 10px; }
  code { background: var(--panel2); padding: 1px 5px; border-radius: 4px; }
</style>
</head>
<body>
<div id="page">
<header>
  <a id="back-link" href="/" title="back to the Testy control panel" style="display:none">&larr; Back</a>
  <h1>Testy <span style="color:var(--dim)">PSD compatibility</span> <span class="version">v2</span></h1>
  <span id="state-pill">loading</span>
  <span class="meta" id="run-meta"></span>
  <span id="run-controls"></span>
</header>
<div id="about">This measures one thing: how faithfully each program loads, renders and saves Photoshop
PSD and PSB files, against Photoshop's own output. It is not a rating of the programs themselves. A low
score means keeping documents as PSDs and moving them to and from Photoshop will lose things with that
program, nothing more. <a href="https://github.com/SethRobinson/Patchy/blob/main/docs/testy.md">How the test works</a> &middot;
<a href="https://github.com/SethRobinson/Patchy">Testy and Patchy on GitHub</a><span id="corpus-credit"></span></div>
<div id="summary"></div>
<div id="known-toggle"></div>
<main>
  <section id="groups"></section>
  <table class="matrix"><thead id="matrix-head"></thead><tbody id="matrix-body"></tbody></table>
  <section id="history"></section>
</main>
</div>
<aside id="detail"><div id="detail-resizer"
  title="drag to resize; double-click to reset"></div><button class="close"
  onclick="closeDetail()">&times;</button><div id="detail-body"></div></aside>
<script>
"use strict";
let S = null;
let selected = null;
// This run's identity plus the server's live-run view, for the pause/resume/cancel
// controls. runState stays null when the page is a frozen file opened from disk (or
// served by something other than testy.py), which hides every control.
const RUN_ID = (location.pathname.match(/\/runs\/([^/]+)\//) || [])[1] || null;
let runState = null;
let rowRerunState = null;
let rowRerunPending = null;
let rowRerunError = "";
const rowRerunScopes = {};
// The Back link only makes sense while the Testy server is serving this page; a
// frozen report.html opened from disk has no control panel at "/" to go back to.
if (location.protocol === "http:" || location.protocol === "https:") {
  const back = document.getElementById("back-link");
  back.style.display = "";
  // A copy published by export_static.py sits beside its own index.html, on a host
  // whose "/" is somebody's home page, not the Testy control panel.
  if (!RUN_ID) { back.href = "index.html"; back.title = "back to the overview"; }
}

function pct(x, digits) { return (100 * x).toFixed(digits === undefined ? 1 : digits) + "%"; }
function esc(s) { return String(s == null ? "" : s).replace(/[&<>"]/g,
  c => ({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;"}[c])); }

// PSDs read naturally in MB (0.2 MB, 20.3 MB); a tiny icon file stays honest in KB
// and a whole corpus rolls up into GB.
function fmtSize(bytes) {
  if (bytes == null) return "";
  if (bytes < 1024) return bytes + " B";
  const kb = bytes / 1024;
  if (kb < 102.4) return Math.round(kb) + " KB";
  const mb = kb / 1024;
  return mb < 1024 ? mb.toFixed(1) + " MB" : (mb / 1024).toFixed(1) + " GB";
}

function totalSize(files) {
  return files.reduce((sum, f) => sum + (f.sizeBytes || 0), 0);
}

// What the file itself is, wherever its name is shown. Size comes from the corpus
// entry, so it is there even when the ground truth (and with it the document size
// and layer count) failed; runs from before sizes were recorded show none.
function fileFacts(f) {
  const facts = [];
  if (f.docSize) facts.push(f.docSize[0] + "x" + f.docSize[1]);
  if (f.layerCount) facts.push(f.layerCount + " layers");
  if (f.sizeBytes != null) facts.push(fmtSize(f.sizeBytes));
  return facts.join(" &middot; ");
}

// Skim thresholds for the matrix dot. A render that misses the run's scan threshold
// (10% by default) is "poor matching" in yellow; a resave Photoshop cannot reopen,
// or a render wrong on more than SEVERE_MISMATCH of the pixels, is red.
const SEVERE_MISMATCH = 0.30;

function poorMatchLimit() {
  return S.run.scan ? S.run.scan.thresholdPct / 100 : 0.10;
}

// The bad-pixel fraction the run's comparison mode judges by - the same number scan
// mode flags on (cells cached before the perceptual metric only carry the byte one).
function compareBadFraction(cell) {
  const m = cell.renderMetrics;
  if (!m) return null;
  if (S.run.compare === "perceptual" && m.perceptual) return m.perceptual.badFraction;
  return m.badFraction;
}

// A corpus spread over several folders (psd-tools sorts its files by feature:
// adjustments, blend-modes, effects, ...) is grouped by the first folder below the
// one all its files share. Derived from the source paths, so every run has it.
const TOP_GROUP = "(top level)";
function fileGroups(files) {
  const dirs = files.map(f => String(f.source || "").split(/[\\/]/).slice(0, -1));
  let common = dirs.length ? dirs[0].length : 0;
  dirs.forEach(d => {
    let i = 0;
    while (i < common && i < d.length && d[i] === dirs[0][i]) i++;
    common = i;
  });
  return dirs.map(d => d.length > common ? d[common] : TOP_GROUP);
}

// Per group and editor: cells measured, opened, renders within the poor-match limit,
// mean share of data kept in the resave, and resaves Photoshop rejected.
function groupRollup(files, groups, editors, badFraction, limit) {
  const out = {};
  files.forEach((f, i) => {
    const row = out[groups[i]] = out[groups[i]] || { files: 0, editors: {} };
    row.files++;
    editors.forEach(k => {
      const a = row.editors[k] = row.editors[k] ||
        { total: 0, opened: 0, matched: 0, compared: 0, badSaves: 0, native: [] };
      const c = (f.cells || {})[k];
      if (!c || c.state === "pending" || c.state === "running" || c.state === "skipped") return;
      a.total++;
      if (c.state === "done" && c.opens !== "fail") a.opened++;
      if (c.resaveRejected) a.badSaves++;
      const bad = badFraction(c);
      if (bad != null) { a.compared++; if (bad <= limit) a.matched++; }
      if (c.native && typeof c.native.nativeScore === "number") a.native.push(c.native.nativeScore);
    });
  });
  return out;
}

// Files exercising things Patchy deliberately does not do: 16/32-bit documents (it
// converts to 8-bit on open) and artboards. The header switch leaves them out of
// every editor's totals, so the scores read as "of the files in scope".
function knownLimit(f) {
  const t = f.traits;
  if (!t) return "";
  const why = [];
  if (t.depth > 8) why.push(t.depth + "-bit");
  if (t.artboards) why.push("artboards");
  return why.join(", ");
}
let skipKnown = false;
try { skipKnown = localStorage.getItem("testy.skipKnown") === "1"; } catch (e) {}
function setSkipKnown(on) {
  skipKnown = !!on;
  try { localStorage.setItem("testy.skipKnown", skipKnown ? "1" : "0"); } catch (e) {}
  render();
}
function scoredFiles() { return skipKnown ? S.files.filter(f => !knownLimit(f)) : S.files; }
function renderKnownToggle() {
  const box = document.getElementById("known-toggle");
  const known = S.files.filter(f => knownLimit(f)).length;
  if (!S.files.some(f => f.traits)) { box.innerHTML = ""; return; }
  box.innerHTML = '<label><input type="checkbox"' + (skipKnown ? " checked" : "") +
    ' onchange="setSkipKnown(this.checked)"> Score without known limitations</label> (16/32-bit and artboard files: ' +
    known + " of " + S.files.length + (skipKnown ? ", left out of the totals above and the folder table" : "") + ")";
}

// The one-glance ranking: every editor but the ground truth, best render match first,
// by the run's comparison mode. scores is {editorKey: mean accuracy or null}.
function standingRows(scores, editors) {
  return editors.filter(k => k !== "photoshop" && scores[k] != null)
    .map(k => ({ key: k, score: scores[k] }))
    .sort((a, b) => b.score - a.score || a.key.localeCompare(b.key));
}

// The Standing card's "psd text handling" block. Strict on purpose: a file with type
// layers scores 0 for an editor that cannot render its Photoshop text objects (it
// only shows the pixels baked into the file) or cannot save them back into the .psd as
// text; otherwise the file scores what the editor's own text render scored. `tally`
// maps editor -> {scores: [per-file score], files, noRender, noSave} (files counted
// where the editor produced a result). Returns the ranked rows, each with its label
// ("0% (FAIL *)" when every file failed) and the footnotes those marks point at
// (editors with the same failure share a mark).
function psdTextStanding(tally, editors, names, helpNotes) {
  const notes = [];
  const helps = [];
  const markFor = text => {
    let note = notes.find(n => n.text === text);
    if (!note) { note = { mark: "*".repeat(notes.length + 1), text: text }; notes.push(note); }
    return note.mark;
  };
  const rows = [];
  editors.forEach(k => {
    const t = tally[k];
    if (k === "photoshop" || !t || !t.scores.length) return;
    const name = names[k] || k;
    const score = t.scores.reduce((a, b) => a + b, 0) / t.scores.length;
    const marks = [];
    const reasons = [];
    const mark = text => { reasons.push(text); marks.push(markFor(text)); };
    if (t.noRender)
      mark((t.noRender >= t.files
        ? "Cannot render psd text objects, only uses the baked pixels saved in the file"
        : name + " could not render the psd text objects in " + t.noRender + " of " + t.files +
          " files (baked pixels only); those files count as 0"));
    if (t.noSave)
      mark((t.noSave >= t.files
        ? "Cannot save text objects back out into the .psd as text"
        : name + " did not save the text objects back into the .psd as text in " + t.noSave + " of " +
          t.files + " files; those files count as 0"));
    const failed = score === 0 && marks.length > 0;
    // Help Testy gives this editor (Photopea: the fonts), marked with a dagger.
    const help = (helpNotes || {})[k] || "";
    let helpMark = "";
    if (help && !failed) {
      helpMark = " " + "†".repeat(helps.length + 1);
      helps.push({ mark: helpMark.trim(), text: help });
      reasons.push(help);
    }
    rows.push({ key: k, score: score, failed: failed, reasons: reasons,
                label: (failed ? "0% (FAIL " + marks.join(" ") + ")"
                               : Math.round(100 * score) + "%" + (marks.length ? " " + marks.join(" ") : "") +
                                 helpMark) });
  });
  rows.sort((a, b) => b.score - a.score || a.key.localeCompare(b.key));
  return { rows: rows, notes: notes.concat(helps) };
}

// The editor refused a file Photoshop rendered: counted as a zero in the averages.
// A harness failure (timeout, dead app, skipped cell) is not the editor's verdict on
// the file and stays out, as does a file with no reference render at all.
function refusedWithReference(f, c) {
  const gt = f.groundTruth || {};
  return !!c && c.opens === "fail" && gt.state === "done" && !!(gt.artifacts || {}).render;
}

// The two text rules that zero a score outright, each with the sentence that says why:
// an editor that cannot render a Photoshop text object (it can only show the pixels
// Photoshop cached in the file) gets 0% for that file's render, and one that cannot
// save a text object back out as text gets 0% for the file's "data kept in .psd save".
function textZeroReasons(cell) {
  const reasons = [];
  if (!cell) return reasons;
  const m = cell.renderMetrics;
  if (m && m.textNotRendered && m.textNotRendered.length) {
    const measured = m.measured || {};
    const was = S.run.compare === "perceptual" && measured.perceptualAccuracy != null
      ? measured.perceptualAccuracy : measured.accuracy;
    reasons.push({
      kind: "render",
      short: "render 0%: cannot render Photoshop text objects",
      long: "Render scored 0%: this editor cannot render " + m.textNotRendered.length +
            " Photoshop text object(s) in this file (" + m.textNotRendered.join(", ") +
            "). It can only show the pixels Photoshop cached in the .psd, so with that cache " +
            "removed it draws nothing for them." +
            (was != null ? " The rest of the picture measured " + pct(was) + "." : ""),
    });
  }
  const n = cell.native;
  if (n && n.textNotSaved) {
    reasons.push({
      kind: "save",
      short: "data kept 0%: text not saved as text",
      long: "Data kept scored 0%: " + n.textNotSaved.lost + " of " + n.textNotSaved.total +
            " Photoshop text object(s) did not come back as text in this editor's .psd save " +
            "(rasterized, converted or dropped), so the text is no longer editable." +
            (n.nativeScoreMeasured != null
              ? " Counting every object, " + n.nativeKept + "/" + n.nativeTotal + " (" +
                pct(n.nativeScoreMeasured) + ") survived."
              : ""),
    });
  }
  return reasons;
}

// Files with cached layers are scored on the editor's render with those caches
// removed (no-cache leg). Say on the cell which layers it drew nothing for, and which
// could not be measured, with the layers' names on hover.
function replayNote(f, k) {
  const leg = ((f.cells || {})[k] || {}).noCache;
  if (!leg) return "";
  if (leg.state !== "done")
    return '<div class="nums" title="' + esc(leg.reason || "") + '">own rendering not measured (scored as opened, with any cached pixels)</div>';
  let html = "";
  if (leg.notRendered && leg.notRendered.length)
    html += '<div class="flag" title="' + esc(leg.notRendered.join(", ")) + '">cannot render ' +
            leg.notRendered.length + " of " + leg.cachedLayers + " Photoshop text/shape/smart layer(s)</div>";
  if (leg.notMeasured && leg.notMeasured.length)
    html += '<div class="nums" title="' + esc((leg.notMeasuredReason || "") + ": " + leg.notMeasured.join(", ")) +
            '">' + leg.notMeasured.length + " layer(s) not measured (cache shown)</div>";
  return html;
}

let groupFilter = null;
function pickGroup(index) {
  const names = [...new Set(fileGroups(S.files))].sort();
  groupFilter = index < 0 || groupFilter === names[index] ? null : names[index];
  render();
}

function renderGroups(groups, editors) {
  const box = document.getElementById("groups");
  const names = [...new Set(groups)].sort();
  if (names.length < 2) { box.innerHTML = ""; groupFilter = null; return; }
  const scored = S.files.map((f, i) => i).filter(i => !(skipKnown && knownLimit(S.files[i])));
  const roll = groupRollup(scored.map(i => S.files[i]), scored.map(i => groups[i]), editors,
                           compareBadFraction, poorMatchLimit());
  names.forEach(name => { roll[name] = roll[name] || { files: 0, editors: {} }; });
  const cell = a => {
    if (!a || !a.total) return "<td>-</td>";
    const failed = a.total - a.opened;
    const parts = [];
    if (a.compared) parts.push('<span class="' + (a.matched < a.total ? "" : "ok-text") + '">' +
                               a.matched + "/" + a.total + " match</span>");
    if (failed) parts.push('<span class="bad-text">' + failed + " not opened</span>");
    if (a.native.length) parts.push("kept " + pct(a.native.reduce((p, c) => p + c, 0) / a.native.length, 0));
    if (a.badSaves) parts.push('<span class="bad-text">' + a.badSaves + " bad save" + (a.badSaves > 1 ? "s" : "") + "</span>");
    return "<td>" + (parts.join(" · ") || "-") + "</td>";
  };
  box.innerHTML = "<h2>By folder" + (groupFilter == null ? "" :
      ' <span class="nums">showing ' + esc(groupFilter) + ' only · <a href="#" onclick="pickGroup(-1);return false">show all</a></span>') +
    "</h2><table><tr><th>Folder</th><th>Files</th>" +
    editors.map(k => "<th>" + esc((S.editors[k] || {}).displayName || k) + "</th>").join("") + "</tr>" +
    names.map((name, i) => "<tr class='pick" + (groupFilter === name ? " on" : "") +
      "' title='click to show only these files' onclick='pickGroup(" + i + ")'><td>" + esc(name) +
      "</td><td>" + roll[name].files + "</td>" +
      editors.map(k => cell(roll[name].editors[k])).join("") + "</tr>").join("") +
    '</table><div class="nums">match: render within ' + pct(poorMatchLimit(), 0) +
    " of Photoshop's pixels (" + (S.run.compare === "perceptual" ? "perceptual" : "byte") +
    "); kept: mean share of layer data surviving a .psd resave.</div>";
}

const LOSS_LABELS = [
  ["cat", "text", "text layers"],
  ["cat", "adjustment", "adjustment layers"],
  ["cat", "smartObject", "smart objects"],
  ["cat", "group", "groups"],
  ["cat", "fill", "fill layers"],
  ["cat", "raster", "raster layers"],
  ["attr", "fx", "live effects"],
  ["attr", "userMask", "layer masks"],
  ["attr", "vectorMask", "vector masks"],
  ["attr", "clipped", "clipping masks"],
  ["attr", "blend", "blend modes"],
];

// Everything the resave failed to keep, as [{lost, total, label}] - categories where
// the object kind died plus attributes stripped from surviving layers.
function lossSummary(n) {
  if (!n || !n.perCategory) return [];
  const out = [];
  LOSS_LABELS.forEach(([src, key, label]) => {
    const v = (src === "cat" ? n.perCategory : n.attributes || {})[key];
    if (v && v.total && v.kept < v.total) out.push({ lost: v.total - v.kept, total: v.total, label: label });
  });
  return out;
}

// lossSummary plus a lost bit depth: a 16-bit file saved as 8-bit keeps every layer
// and still loses its extra precision (cell.saveDepth, read from the two headers).
function cellLosses(cell) {
  const out = lossSummary(cell.native);
  const d = cell.saveDepth;
  if (d && !d.kept) out.push({ lost: 1, total: 1, label: "bit depth (" + d.original + " to " + d.resaved + "-bit)" });
  return out;
}

function lossText(losses) {
  return losses.map(l => l.lost + "/" + l.total + " " + l.label).join(", ");
}

function cellSummary(cell, psCell) {
  if (!cell || cell.state === "pending") return '<div class="status-line"><span class="dot"></span>queued</div>';
  if (cell.state === "running")
    return '<div class="status-line"><span class="dot run"></span>' + esc(cell.stage || "working") + '</div>';
  if (cell.state === "unsupported")
    return '<div class="status-line"><span class="dot warn"></span>no PSD support</div>' +
           '<div class="nums">' + esc(cell.error || "") + '</div>';
  if (cell.state === "skipped")
    return '<div class="status-line"><span class="dot warn"></span>skipped</div>' +
           '<div class="nums">' + esc(cell.error || "") + '</div>';
  if (cell.state === "failed")
    return '<div class="status-line"><span class="dot bad"></span>failed</div>' +
           '<div class="nums">' + esc((cell.error || "").slice(0, 90)) + '</div>';
  const bits = [];
  if (cell.renderMetrics) bits.push("byte match " + pct(cell.renderMetrics.accuracy));
  if (cell.renderMetrics && cell.renderMetrics.perceptual)
    bits.push("perceptual " + pct(cell.renderMetrics.perceptual.accuracy));
  if (cell.native && cell.native.perCategory)
    bits.push("kept in .psd save " + cell.native.nativeKept + "/" + cell.native.nativeTotal +
              (cell.native.textNotSaved ? " = 0% (text lost)" : ""));
  if (cell.renderMetrics && cell.renderMetrics.objectsScored) {
    const m = cell.renderMetrics;
    const objectsOk = S.run.compare === "perceptual" && m.objectsRenderedOkPerceptual != null
      ? m.objectsRenderedOkPerceptual : m.objectsRenderedOk;
    bits.push("objects " + objectsOk + "/" + m.objectsScored);
  }
  const flags = [];
  // A sentinel hit matching Photoshop's own trap render is not a cheat: the file has
  // layers even the ground truth cannot re-render, so both fall back to the composite.
  const sentinel = cell.trapSentinelFraction || 0;
  const psSentinel = (psCell && psCell.trapSentinelFraction) || 0;
  let compositeNote = "";
  if (sentinel > 0.05) {
    if (psSentinel <= 0.05)
      flags.push("flat-composite cheat");
    else if (sentinel > psSentinel + 0.05)
      flags.push("flat-composite cheat (beyond Photoshop's " + pct(psSentinel) + ")");
    else
      compositeNote = cell === psCell
        ? "renders from the baked composite"
        : "renders from the baked composite (so does Photoshop)";
  }
  if (cell.renderMetrics && cell.renderMetrics.sizeMismatch) flags.push("size mismatch");
  textZeroReasons(cell).forEach(reason => flags.push(reason.short));
  if (cell.opens === "fallback-render") flags.push("PS needed fallback render");
  // The two verdicts worth reading from across the matrix ride on the status line
  // itself, next to the dot, instead of down in the flag list.
  const notes = [];
  let severity = 0;  // 0 fine, 1 yellow, 2 red
  const badFraction = compareBadFraction(cell);
  if (badFraction != null && badFraction > poorMatchLimit()) {
    notes.push("poor matching");
    severity = badFraction > Math.max(SEVERE_MISMATCH, poorMatchLimit()) ? 2 : 1;
  }
  if (cell.resaveRejected) { notes.push("saves corrupted .psd"); severity = 2; }
  const losses = cellLosses(cell);
  const lossLine = losses.length
    ? '<div class="flag">lost: ' + lossText(losses.slice(0, 3)) +
      (losses.length > 3 ? " +" + (losses.length - 3) + " more" : "") + "</div>"
    : "";
  const cls = cell.opens === "fail" || severity === 2 ? "bad"
    : (severity || flags.length || losses.length) ? "warn" : "ok";
  // One span holds the whole label: .status-line is a flex row with a gap, so a bare
  // text node beside the note span would be spaced apart from it.
  const label = cell.opens === "fail" ? "failed to open"
    : "opened" + (notes.length ? ' - <span class="' + (severity === 2 ? "bad-text" : "warn-text") +
                                 '">' + notes.join(" - ") + "</span>" : "");
  return '<div class="status-line"><span class="dot ' + cls + '"></span><span>' + label + '</span></div>' +
         '<div class="nums">' + bits.join(" &middot; ") + '</div>' + lossLine +
         (flags.length ? '<div class="flag">' + flags.join(" &middot; ") + '</div>' : "") +
         (compositeNote ? '<div class="nums">' + compositeNote + '</div>' : "");
}

function renderControls() {
  const holder = document.getElementById("run-controls");
  if (!runState || !RUN_ID || !S) { holder.innerHTML = ""; return; }
  const liveHere = runState.running && runState.run === RUN_ID;
  let html = "";
  if (liveHere) {
    html = (runState.pausePending
      ? "<button disabled>Pausing...</button>"
      : "<button onclick=\"controlRun('pause')\">Pause</button>") +
      "<button onclick=\"controlRun('cancel')\">Cancel</button>";
    if (runState.pausePending)
      html += '<span class="ctl-note">stops after the current step; everything done so far is kept</span>';
  } else if (!runState.running && S.state !== "done") {
    // paused, canceled, or interrupted (a "running" status whose process is gone)
    html = "<button onclick=\"controlRun('resume')\">Resume</button>";
    if (S.state === "paused" || S.state === "running")
      html += "<button onclick=\"controlRun('cancel')\">Discard</button>";
  } else if (runState.running && S.state !== "done") {
    html = '<span class="ctl-note">another run is live; this one can be resumed after it</span>';
  }
  holder.innerHTML = html;
}

function controlRun(action) {
  fetch("/testy-" + action + "-run", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ run: RUN_ID }),
  }).then(async response => {
    const result = await response.json();
    if (!response.ok) {
      document.getElementById("run-controls").innerHTML =
        '<span class="ctl-note">' + esc((result.errors || [action + " failed"]).join("; ")) + "</span>";
      setTimeout(pollSoon, 1500);
      return;
    }
    pollSoon();
  }).catch(e => { /* server gone mid-click; the next poll reconciles */ });
}

// One-file retest: POSTs the source path back to the server, which spawns a
// fresh run for just that file (rebuilding Patchy by default; every other cell
// comes from the caches). Once the child's status.json appears the page jumps
// to the new run's report.
let retestNote = "";
let retestPending = false;

function retestFile(fi) {
  retestPending = true;
  retestNote = "starting retest...";
  render();
  fetch("/testy-retest-file", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ run: RUN_ID, source: S.files[fi].source }),
  }).then(async response => {
    const result = await response.json();
    if (!response.ok) {
      retestPending = false;
      retestNote = (result.errors || ["retest failed"]).join("; ");
      render();
      return;
    }
    retestNote = "rebuilding and retesting - this page will jump to the new run";
    render();
    waitForRetestRun(Date.now() + 90000);
  }).catch(e => {
    retestPending = false;
    retestNote = "the server is unreachable";
    render();
  });
}

function waitForRetestRun(deadline) {
  fetch("/testy-run-state", { cache: "no-store" }).then(async response => {
    const state = response.ok ? await response.json() : null;
    if (state && state.running && state.run && state.run !== RUN_ID) {
      location.href = "/runs/" + state.run + "/report.html";
      return;
    }
    if (Date.now() > deadline) {
      retestPending = false;
      retestNote = "retest started - find it at the top of the control panel's run table";
      render();
      return;
    }
    setTimeout(() => waitForRetestRun(deadline), 1000);
  }).catch(() => setTimeout(() => waitForRetestRun(deadline), 1500));
}

function rerunRow(fi) {
  if (rowRerunPending !== null || (runState && runState.running)) return;
  const source = S.files[fi].source;
  rowRerunPending = source;
  rowRerunError = "";
  rowRerunState = { source, state: "starting" };
  render();
  fetch("/testy-rerun-file", {
    method: "POST", headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ run: RUN_ID, source, scope: rowRerunScopes[fi] || "patchy" }),
  }).then(async response => {
    const result = await response.json();
    rowRerunPending = null;
    if (!response.ok) {
      rowRerunError = (result.errors || ["Rerun failed"]).join("; ");
      rowRerunState = { source, state: "failed", error: rowRerunError };
      render();
    } else { pollSoon(); }
  }).catch(() => {
    rowRerunPending = null;
    rowRerunError = "The server is unreachable. Check the row before trying again.";
    render();
  });
}

function rerunRowControls(f, fi) {
  if (!RUN_ID || !runState || S.state !== "done") return "";
  const thisRow = rowRerunState && rowRerunState.source === f.source;
  const running = thisRow && ["starting", "running"].includes(rowRerunState.state);
  const busy = runState.running || rowRerunPending !== null ||
    (rowRerunState && rowRerunState.state === "running");
  const hasPatchy = S.run.editorOrder.includes("patchy");
  const scope = rowRerunScopes[fi] || (hasPatchy ? "patchy" : "all");
  rowRerunScopes[fi] = scope;
  const disabled = busy ? " disabled" : "";
  let controls = '<div><select class="rerun-scope" aria-label="Editors to rerun for ' + esc(f.name) +
    '" onchange="rowRerunScopes[' + fi + ']=this.value"' + disabled + '>' +
    (hasPatchy ? '<option value="patchy"' + (scope === "patchy" ? " selected" : "") + '>Patchy</option>' : "") +
    '<option value="all"' + (scope === "all" ? " selected" : "") + '>All editors</option></select>' +
    '<button class="rerun-btn" onclick="rerunRow(' + fi + ')"' + disabled +
    ' title="Rerun this image and update this batch">' + (running ? "Rerunning..." : "Rerun") + '</button></div>';
  if (thisRow) {
    const note = rowRerunError || rowRerunState.error || (running ? "Previous results shown until completion." :
      rowRerunState.state === "applied" ? "Results and totals updated." : "");
    controls += '<div class="nums" role="status">' + esc(note) + '</div>';
  }
  return controls;
}

function editorVersionLabel(key) {
  const versions = new Set([(S.editors[key] || {}).version || '?']);
  (S.run.reruns || []).forEach(r => {
    if (r.editors[key]) versions.add(r.editors[key].version || '?');
  });
  return versions.size > 1 ? 'mixed versions; see updated rows' : [...versions][0];
}

// The test files are somebody else's work: name them, with a link, next to the
// explanation of the test.
function renderCorpusCredit() {
  const credit = S.run.corpus;
  const span = document.getElementById("corpus-credit");
  if (!credit || !span) return;
  const link = (url, text) => { const a = document.createElement("a"); a.href = url; a.textContent = text; return a; };
  span.textContent = "";
  span.append(document.createElement("br"), "Test files: the ");
  span.append(link(credit.url, credit.name));
  span.append(" of the ", link(credit.projectUrl, credit.project + " project"),
              " (" + credit.license + " license)" +
              (credit.commit ? ", commit " + credit.commit.slice(0, 12) : "") +
              ". Only the renders are shown here, never the files.");
}

function render() {
  if (!S) return;
  const pill = document.getElementById("state-pill");
  // A "running" status whose run process is gone (crash, reboot, kill) is shown as
  // interrupted; resuming it turns it genuinely live again.
  const interrupted = S.state === "running" && runState && !runState.running;
  pill.textContent = interrupted ? "interrupted" : S.state;
  pill.className = interrupted ? "interrupted" : S.state;
  renderControls();
  const compareWord = S.run.compare === "perceptual" ? "perceptual" : "byte";
  const corpusBytes = totalSize(S.files);
  renderCorpusCredit();
  document.getElementById("run-meta").textContent =
    S.run.startedAt + "  -  " + S.files.length + " file(s)" +
    (corpusBytes ? ", " + fmtSize(corpusBytes) : "") + "  -  Patchy " + (S.run.patchyVersion || "?") +
    (S.run.compare === "perceptual" ? "  -  compare: perceptual" : "") +
    (S.run.scan ? "  -  scan mode: flag over " + S.run.scan.thresholdPct + "% " + compareWord + " difference" : "") +
    (S.run.reruns && S.run.reruns.length ? "  -  partial refresh: " + S.run.reruns.length + " image rerun(s); build details on each row" : "") +
    (S.run.addedEditors || []).map(a => "  -  " + a.editors.map(k => (S.editors[k] || {}).displayName || k).join(", ") +
      " added " + a.at).join("");

  const editors = S.run.editorOrder;
  document.getElementById("matrix-head").innerHTML =
    "<tr><th>PSD</th>" + editors.map(k => {
      const e = S.editors[k] || {};
      return "<th>" + esc(e.displayName || k) +
             '<div class="nums">' + esc(editorVersionLabel(k)) + "</div>" +
             (e.addedAt ? '<div class="nums" title="Column added to this finished run, scored against its recorded Photoshop ground truth">added ' +
              esc(e.addedAt.slice(0, 10)) + "</div>" : "") + "</th>";
    }).join("") + "</tr>";

  const groups = fileGroups(S.files);
  renderGroups(groups, editors);
  document.getElementById("matrix-body").innerHTML = S.files.map((f, fi) => {
    if (groupFilter != null && groups[fi] !== groupFilter) return "";
    const gt = f.groundTruth || {};
    const gtNote = gt.state === "failed" ? '<div class="flag">ground truth failed</div>'
      : (gt.state === "running" ? '<div class="nums">ground truth: ' + esc(gt.stage || "...") + "</div>" : "");
    const scanNote = !f.scan ? "" : (f.scan.flagged
      ? '<div class="flag">FLAGGED: ' + esc(f.scan.reasons[0] || "") +
        (f.scan.reasons.length > 1 ? " (+" + (f.scan.reasons.length - 1) + " more)" : "") + "</div>"
      : '<div class="nums ok-text">scan: passed' + (f.scan.artifactsKept ? "" : " (images discarded)") + '</div>');
    const latest = f.reruns && f.reruns.length ? f.reruns[f.reruns.length - 1] : null;
    const revision = latest ? '<div class="nums">Updated ' + esc(latest.at) + ' (' +
      Object.entries(latest.editors).map(([k, v]) => esc(v.displayName || k) + ' ' + esc(v.version || '?')).join(', ') +
      ') · <a href="' + esc(artUrl(latest.previous)) + '" target="_blank">Previous results</a></div>' : "";
    const limit = knownLimit(f);
    const limitNote = limit ? '<div class="nums">known limitation: ' + esc(limit) + "</div>" : "";
    return "<tr" + (limit ? " class='known-limit'" : "") + "><td class='file'><b class='copyable' title='" + esc(f.source) +
      " (click to copy path)' onclick='copyPath(" + fi + ", this)'>" + esc(f.name) + "</b>" +
      '<div class="nums">' + fileFacts(f) + "</div>" + limitNote + gtNote + scanNote + revision + rerunRowControls(f, fi) + "</td>" +
      editors.map(k => "<td class='cell' onclick='openDetail(" + fi + ",\"" + k + "\")'>" +
                       cellSummary((f.cells || {})[k], (f.cells || {}).photoshop) + replayNote(f, k) +
                       "</td>").join("") + "</tr>";
  }).join("");

  const agg = {};
  editors.forEach(k => agg[k] = { opened: 0, total: 0, badSaves: 0, acc: [], vis: [], native: [], text: [0, 0], adj: [0, 0], smart: [0, 0], fx: [0, 0], textFiles: [],
                                   depth: [0, 0], deepRoundtrip: [],
                                   textTally: { scores: [], files: 0, noRender: 0, noSave: 0 } });
  renderKnownToggle();
  scoredFiles().forEach(f => editors.forEach(k => {
    const c = (f.cells || {})[k];
    if (!c || c.state === "pending" || c.state === "running" || c.state === "skipped") return;
    const a = agg[k];
    a.total++;
    if (c.state === "done" && c.opens !== "fail") a.opened++;
    if (c.resaveRejected) a.badSaves++;
    if (c.saveDepth && c.saveDepth.original > 8) { a.depth[1]++; if (c.saveDepth.kept) a.depth[0]++; }
    if (c.deepRoundtrip && c.deepRoundtrip.state === "done") a.deepRoundtrip.push(c.deepRoundtrip.accuracy);
    if (c.renderMetrics) a.acc.push(c.renderMetrics.accuracy);
    if (c.renderMetrics && c.renderMetrics.perceptual) a.vis.push(c.renderMetrics.perceptual.accuracy);
    if (!c.renderMetrics && refusedWithReference(f, c)) { a.acc.push(0); a.vis.push(0); }
    // (A file whose text needs a font Photoshop lacks says nothing about anyone's
    // text handling: the baked pixels are the reference for everybody.)
    const gtFonts = f.groundTruth || {};
    if (f.textLayers && !(gtFonts.textFontsMissing || gtFonts.mutateSkipped)) {
      // The text score is the scored render of a file with type layers, for an
      // editor whose own text engine drew that text (on open, or after Testy's
      // scripted re-render). Anything else is not a text score.
      const basis = (S.editors[k] || {}).textBasis;
      const metrics = basis === "open" ? c.renderMetrics : null;
      const score = metrics
        ? (S.run.compare === "perceptual" && metrics.perceptual ? metrics.perceptual.accuracy : metrics.accuracy)
        : (basis === "open" && refusedWithReference(f, c) ? 0 : null);
      if (score != null) a.textFiles.push(score);
      // psd text handling (Standing card): the same score, but a file whose Photoshop
      // text the editor cannot render, or cannot save back as text, counts as 0.
      const m = c.renderMetrics;
      const noRender = basis === "replay" || !!(m && m.textNotRendered && m.textNotRendered.length);
      const noSave = !!(c.native && c.native.textNotSaved);
      const judged = score != null || ((noRender || noSave) && c.state === "done");
      if (judged) {
        const t = a.textTally;
        t.files++;
        if (noRender) t.noRender++;
        if (noSave) t.noSave++;
        t.scores.push(noRender || noSave ? 0 : score);
      }
    }
    if (c.native && typeof c.native.nativeScore === "number") {
      a.native.push(c.native.nativeScore);
      const pc = c.native.perCategory || {};
      [["text","text"],["adjustment","adj"],["smartObject","smart"]].forEach(([src, dst]) => {
        if (pc[src]) { a[dst][0] += pc[src].kept; a[dst][1] += pc[src].total; }
      });
      const at = c.native.attributes || {};
      if (at.fx) { a.fx[0] += at.fx.kept; a.fx[1] += at.fx.total; }
    }
  }));
  const mean = xs => xs.length ? xs.reduce((p, c) => p + c, 0) / xs.length : null;
  let scanCard = "";
  if (S.run.scan) {
    const done = S.files.filter(f => f.scan);
    const decided = done.length;
    const flagged = S.files.filter(f => f.scan && f.scan.flagged).length;
    // Bytes get through at nothing like a steady files-per-hour rate, so the size
    // done/total is the honest read on how far a big scan actually is.
    const sizeRow = corpusBytes
      ? '<div class="row"><span>size</span><b>' + fmtSize(totalSize(done)) + " / " +
        fmtSize(corpusBytes) + "</b></div>"
      : "";
    scanCard = '<div class="card"><h3>Scan</h3><div class="ver">flag over ' +
      S.run.scan.thresholdPct + "% " + compareWord + " difference or any failure</div>" +
      '<div class="row"><span>scanned</span><b>' + decided + "/" + S.files.length + "</b></div>" +
      '<div class="row"><span>flagged</span><b class="' + (flagged ? "bad-text" : "ok-text") + '">' +
      flagged + "</b></div>" +
      '<div class="row"><span>passed</span><b>' + (decided - flagged) + "</b></div>" +
      sizeRow + "</div>";
  }
  document.getElementById("summary").innerHTML = scanCard + editors.map(k => {
    const e = S.editors[k] || {}, a = agg[k];
    const rows = [];
    rows.push(["opened", a.total ? a.opened + "/" + a.total : "-"]);
    const acc = mean(a.acc); rows.push(["byte match", acc == null ? "-" : pct(acc)]);
    const vis = mean(a.vis); if (vis != null) rows.push(["perceptual", pct(vis)]);
    const nat = mean(a.native); rows.push(["data kept in .psd save", nat == null ? "-" : pct(nat)]);
    // How many resaves Photoshop refused to reopen - the "saves corrupted .psd"
    // cells, rolled up. Red the moment it is not 0.
    rows.push(["bad .psd saves", !a.total ? "-"
      : a.badSaves ? '<span class="bad-text">' + a.badSaves + "</span>" : "0"]);
    [["text", "text kept"], ["adj", "adjustments"], ["smart", "smart objects"], ["fx", "live effects"],
     ["depth", "16/32-bit saves kept depth"]].forEach(([key, label]) => {
      const v = a[key];
      if (v[1]) rows.push([label, v[0] < v[1] ? '<span class="bad-text">' + v[0] + "/" + v[1] + "</span>"
                                              : v[0] + "/" + v[1]]);
    });
    const deepRt = mean(a.deepRoundtrip);
    if (deepRt != null) rows.push(["16-bit precision after save", pct(deepRt)]);
    return '<div class="card"><h3>' + esc(e.displayName || k) + '</h3><div class="ver">' +
      esc(editorVersionLabel(k)) + "</div>" +
      rows.map(r => '<div class="row"><span>' + r[0] + "</span><b>" + r[1] + "</b></div>").join("") +
      "</div>";
  }).join("") + (() => {
    const perceptual = S.run.compare === "perceptual";
    const scores = {};
    editors.forEach(k => { scores[k] = mean(perceptual && agg[k].vis.length ? agg[k].vis : agg[k].acc); });
    const ranked = standingRows(scores, editors);
    if (ranked.length < 2) return "";
    // psd text handling, ranked on its own and strict (see psdTextStanding): an editor
    // that cannot render Photoshop text objects, or cannot save them back as text,
    // scores 0 on that file, and the reason is spelled out under the list.
    const textFiles = scoredFiles().filter(f => f.textLayers &&
      !((f.groundTruth || {}).textFontsMissing || (f.groundTruth || {}).mutateSkipped)).length;
    const tally = {}, names = {};
    editors.forEach(k => { tally[k] = agg[k].textTally; names[k] = (S.editors[k] || {}).displayName || k; });
    const helpNotes = {};
    editors.forEach(k => { helpNotes[k] = (S.editors[k] || {}).textHelpNote || ""; });
    const text = psdTextStanding(tally, editors, names, helpNotes);
    const textBlock = !textFiles || !text.rows.length ? "" :
      '<div class="ver" style="margin-top:8px">psd text handling (' + textFiles + " files with type layers)</div>" +
      text.rows.map((r, i) => '<div class="row' + (r.key === "patchy" ? " me" : "") + '" title="' +
        esc([(S.editors[r.key] || {}).textBasisNote || ""].concat(r.reasons).filter(x => x).join(". ")) +
        '"><span>' + (i + 1) + ". " +
        esc(names[r.key]) + "</span><b" + (r.failed ? ' class="bad-text"' : "") + ">" + esc(r.label) +
        "</b></div>").join("") +
      text.notes.map(n => '<div class="nums" style="margin-top:4px">' + esc(n.mark) + " - " + esc(n.text) +
        "</div>").join("");
    return '<div class="card standing"><h3>Standing</h3><div class="ver">' +
      (perceptual ? "perceptual" : "byte") + " match to Photoshop" +
      (skipKnown ? ", known limitations left out" : "") + "; unopened files count as 0</div>" +
      ranked.map((r, i) => '<div class="row' + (r.key === "patchy" ? " me" : "") + '"><span>' + (i + 1) + ". " +
        esc((S.editors[r.key] || {}).displayName || r.key) + "</span><b>" + pct(r.score, 0) + "</b></div>").join("") +
      textBlock + "</div>";
  })();
  renderHistory();
  if (selected) openDetail(selected[0], selected[1], true);
}

// status.json stores artifact paths exactly as they sit on disk, and a run directory
// inherits its corpus file's stem: files/eco%20beret/patchy/render.png. Served raw the
// server unquotes the escape, looks for "eco beret" and 404s, so every segment is
// encoded (per segment, so the '/' separators survive).
function artUrl(p) { return String(p).split("/").map(encodeURIComponent).join("/"); }

function img(fig, cap, full) {
  if (!fig) return "";
  const version = "?v=" + (S.run.updateCounter || 0);
  return "<figure><a href='" + artUrl(full || fig) + version + "' target='_blank' title='open full size'>" +
         "<img src='" + artUrl(fig) + version + "' onload='zoomTiny(this)'></a>" +
         "<figcaption>" + cap + "</figcaption></figure>";
}

// Images whose longer side is under 64 px are scaled up by a whole number (at most
// 40x) until that side is at least 120 px, with nearest-neighbour sampling, and the
// caption says so; everything larger keeps its natural size.
function zoomTiny(image) {
  const w = image.naturalWidth, h = image.naturalHeight;
  if (!w || !h || Math.max(w, h) >= 64) return;
  const factor = Math.max(2, Math.min(40, Math.ceil(120 / Math.max(w, h))));
  image.classList.add("tiny");
  image.style.width = (w * factor) + "px";
  image.style.height = (h * factor) + "px";
  const caption = image.closest("figure") && image.closest("figure").querySelector("figcaption");
  if (caption && !caption.querySelector(".zoom")) {
    const note = document.createElement("span");
    note.className = "zoom";
    note.textContent = "(" + w + "\u00d7" + h + " shown at " + factor + "\u00d7)";
    caption.append(" ", note);
  }
}

function openDetail(fi, ek, keep) {
  selected = [fi, ek];
  const f = S.files[fi], cell = (f.cells || {})[ek] || {}, gt = cell.groundTruth || f.groundTruth || {};
  const art = cell.artifacts || {}, gart = gt.artifacts || {};
  const editorName = esc((S.editors[ek] || {}).displayName || ek);
  let html = "<h2><span class='copyable' title='" + esc(f.source) +
    " (click to copy path)' onclick='copyPath(" + fi + ", this)'>" + esc(f.name) + "</span>" +
    " &middot; " + editorName + "</h2>" +
    '<div class="nums">' + fileFacts(f) + "</div>" +
    '<div class="sub">' + esc(cell.state) + (cell.stage ? " - " + esc(cell.stage) : "") +
    (cell.error ? ' - <span class="bad-text">' + esc(cell.error) + "</span>" : "") + "</div>";
  // Retest only exists while testy.py itself serves the page; a frozen report
  // opened from disk has no server to spawn the run.
  if (runState && RUN_ID) {
    const busy = runState.running || retestPending;
    html += '<div class="sub"><button class="retest-btn" onclick="retestFile(' + fi + ')"' +
      (busy ? " disabled" : "") +
      (runState.running ? ' title="a run is in progress; retest when it finishes"' : "") +
      '>Retest file</button>' +
      (retestNote ? '<span class="retest-note">' + esc(retestNote) + "</span>" : "") + "</div>";
  }
  if (f.scan) {
    html += f.scan.flagged
      ? '<div class="loss-banner"><b>Flagged by the scan</b><div class="nums">' +
        f.scan.reasons.map(esc).join("<br>") + "</div></div>"
      : '<div class="keep-banner"><b>Passed the scan</b><div class="nums">Every editor stayed ' +
        'within the threshold. ' + (f.scan.artifactsKept ? 'Rerun artifacts are retained.' :
        'Images and resaves were discarded; measurements are retained.') + '</div></div>';
  }
  if (f.trapSkipped)
    html += '<div class="nums">honest-rendering trap not run: ' + esc(f.trapSkipped) + "</div>";
  // Modal alerts Photoshop raised on open. The driver acknowledges them so the
  // probe can finish, but they are worth showing: on the original they say the
  // file has something Photoshop cannot read, and on an editor's resave they say
  // that editor wrote it.
  (gt.dialogs || []).forEach(d => {
    html += '<div class="nums">Photoshop warned while opening this file: ' + esc(d) + "</div>";
  });
  (cell.dialogs || []).forEach(d => {
    html += '<div class="nums">Photoshop warned during this cell (its probe also '
      + "re-saves the file, and save-time warnings are modal too): " + esc(d) + "</div>";
  });
  (cell.resaveDialogs || []).forEach(d => {
    html += '<div class="nums">Photoshop warned while reopening ' + editorName +
      "'s resave: " + esc(d) + "</div>";
  });
  const psTrap = ((f.cells || {}).photoshop || {}).trapSentinelFraction || 0;
  if (cell.trapSentinelFraction > 0.05 && psTrap > 0.05 &&
      !(cell.trapSentinelFraction > psTrap + 0.05))
    html += '<div class="nums">trap: ' + pct(cell.trapSentinelFraction) +
      " of pixels come from the baked composite, but Photoshop's own trap render shows " +
      pct(psTrap) + " - the file has layers even the ground truth cannot re-render " +
      "(e.g. missing fonts), so this is not counted as a cheat</div>";
  const fontsMissing = gt.textFontsMissing || gt.mutateSkipped || cell.textRenderSkipped;
  if (fontsMissing)
    html += '<div class="nums">Text is scored from the baked pixels for this file: ' + esc(fontsMissing) +
            " (Photoshop cannot draw this text faithfully either).</div>";
  html += '<div class="imgs">' +
    img(gart.renderThumb, "Photoshop ground truth", gart.render) +
    img(art.renderThumb, editorName + (cell.noCache && cell.noCache.state === "done"
        ? " render, baked pixels removed (scored)" : " render"), art.render) +
    (art.renderAsOpened ? img(art.renderAsOpened, editorName + " render as opened (includes Photoshop's baked pixels)",
                              art.renderAsOpened) : "") +
    img(art.heatmap, "Difference heatmap") +
    img(art.trapThumb, editorName + " trap render (sentinel = used baked composite)", art.trap) +
    img(art.roundtripThumb, editorName + " resave reopened in Photoshop", art.roundtripRender) +
    "</div>";
  textZeroReasons(cell).filter(r => r.kind === "render").forEach(r => {
    html += '<div class="loss-banner"><b>' + esc(r.long) + "</b></div>";
  });
  // The placeholder boxes in the scored render, in words (a small layer has no room
  // for the label inside its box).
  const leg = cell.noCache;
  if (leg && leg.state === "done" && leg.notRendered && leg.notRendered.length)
    html += '<div class="loss-banner"><b>The red outline(s) in this render mark ' + leg.notRendered.length +
            " Photoshop text, shape or smart-object layer(s) this editor drew nothing for: " +
            esc(leg.notRendered.join(", ")) + "</b>" +
            '<div class="nums">Photoshop keeps a baked copy of such a layer in the .psd. This render is of ' +
            "the file with those baked pixels removed, so it shows only what the editor draws itself." +
            "</div></div>";
  if (leg && leg.state === "done" && leg.notMeasured && leg.notMeasured.length)
    html += '<div class="nums">Not measured (baked pixels shown): ' + esc(leg.notMeasured.join(", ")) +
            ". " + esc(leg.notMeasuredReason || "") + "</div>";
  if (leg && leg.state !== "done")
    html += '<div class="nums">Own rendering not measured, scored as opened: ' + esc(leg.reason || "") + "</div>";
  if (cell.renderMetrics) {
    const m = cell.renderMetrics;
    const p = m.perceptual;
    const perceptualMode = S.run.compare === "perceptual";
    const objectsOk = perceptualMode && m.objectsRenderedOkPerceptual != null
      ? m.objectsRenderedOkPerceptual : m.objectsRenderedOk;
    html += "<table><tr><th>Byte match</th><th>Perceptual match</th><th>RMSE</th>" +
      "<th>Pixels off</th><th>Perceptually off</th><th>Mean deltaE</th><th>Objects ok</th><th>Trap sentinel</th></tr>" +
      "<tr><td>" + pct(m.accuracy) + "</td><td>" + (p ? pct(p.accuracy) : "-") + "</td><td>" + m.rmse +
      "</td><td>" + pct(m.badFraction) + "</td><td>" + (p ? pct(p.badFraction) : "-") +
      "</td><td>" + (p ? p.deltaEMean : "-") +
      "</td><td>" + objectsOk + "/" + m.objectsScored + "</td><td>" +
      (cell.trapSentinelFraction == null ? "-" : pct(cell.trapSentinelFraction)) + "</td></tr></table>";
    // "Worst" follows the run's comparison mode when the perceptual fields exist
    // (cells cached before the metric only carry the strict ones).
    const isBad = o => perceptualMode && o.perceptualOk !== undefined ? !o.perceptualOk : !o.ok;
    const fracOf = o => perceptualMode && o.perceptualBadFraction !== undefined
      ? o.perceptualBadFraction : o.badFraction;
    const worst = (m.perObject || []).filter(isBad).sort((a, b) => fracOf(b) - fracOf(a)).slice(0, 12);
    if (worst.length) {
      html += "<h3>Worst-rendered objects</h3><table><tr><th>Layer</th><th>Kind</th><th>Bad pixels</th><th>Visually bad</th></tr>" +
        worst.map(o => "<tr><td>" + esc(o.name) + "</td><td>" + esc(o.kind) + "</td><td>" +
                       pct(o.badFraction) + "</td><td>" +
                       (o.perceptualBadFraction == null ? "-" : pct(o.perceptualBadFraction)) +
                       "</td></tr>").join("") + "</table>";
    }
  }
  if (cell.native && cell.native.perCategory) {
    const n = cell.native, pc = n.perCategory, at = n.attributes;
    html += "<h3>Data kept in .psd save (via Photoshop reopen): " + n.nativeKept + "/" + n.nativeTotal + "</h3>";
    textZeroReasons(cell).filter(r => r.kind === "save").forEach(r => {
      html += '<div class="loss-banner"><b>' + esc(r.long) + "</b></div>";
    });
    const losses = cellLosses(cell);
    if (losses.length) {
      const changed = n.changedLayers || [];
      const gone = changed.filter(c => c.became == null).length;
      const converted = changed.filter(c => c.became != null).length;
      let detail;
      if (!gone && !converted && !lossSummary(n).length)
        detail = "every object kept its kind; the file was saved at a lower bit depth";
      else if (!gone && !converted)
        detail = "every object kept its kind; the losses are attributes stripped from surviving layers";
      else
        detail = (gone ? gone + " gone from the file entirely" : "") +
                 (gone && converted ? "; " : "") +
                 (converted ? converted + " still in the file but converted to a different kind " +
                              "(no longer editable as what they were)" : "");
      html += '<div class="loss-banner"><b>Lost in resave: ' + lossText(losses) + "</b>" +
              '<div class="nums">' + detail + "</div></div>";
    } else {
      html += '<div class="keep-banner"><b>Everything survived: all ' + n.nativeTotal +
              " object(s) plus effects, masks, and blend modes</b></div>";
    }
    html += "<table><tr><th>Category</th><th>Kept</th></tr>" +
      Object.keys(pc).filter(k => pc[k].total).map(k =>
        "<tr><td>" + k + "</td><td>" + pc[k].kept + "/" + pc[k].total + "</td></tr>").join("") +
      Object.keys(at).filter(k => at[k].total).map(k =>
        "<tr><td><i>" + k + "</i></td><td>" + at[k].kept + "/" + at[k].total + "</td></tr>").join("") +
      "</table>";
    if ((n.changedLayers || []).length) {
      html += "<h3>Objects lost or converted</h3><table><tr><th>Layer</th><th>Was</th><th>Became</th></tr>" +
        n.changedLayers.slice(0, 15).map(c => "<tr><td>" + esc(c.name) + "</td><td>" + esc(c.kind) +
          "</td><td>" + (c.became == null ? '<span class="bad-text">gone - missing from the resaved file</span>'
            : esc(c.became) + (c.became === "NORMAL" && c.kind !== "NORMAL" ? " (rasterized)" : "")) +
          "</td></tr>").join("") + "</table>";
    }
  } else if (cell.native && cell.native.error) {
    html += "<h3>Data kept in .psd save (via Photoshop reopen)</h3>" +
      '<div class="loss-banner"><b>Resave rejected: Photoshop could not open this editor&#39;s PSD</b>' +
      '<div class="nums">' + esc(cell.native.error) + "</div></div>";
  }
  if (cell.roundtripRender) {
    const rp = cell.roundtripRender.perceptual;
    html += "<h3>Round trip back into Photoshop</h3><table><tr><th>Byte match vs original</th><th>Perceptual match</th><th>Pixels off</th></tr>" +
      "<tr><td>" + pct(cell.roundtripRender.accuracy) + "</td><td>" + (rp ? pct(rp.accuracy) : "-") +
      "</td><td>" + pct(cell.roundtripRender.badFraction) + "</td></tr></table>";
  }
  // 16-bit files: precision at 16 bits against Photoshop's 16-bit render, which the
  // 8-bit metrics above cannot see (analyze.compare_deep_renders).
  const deepRows = [["render", cell.deepRender], ["resave reopened in Photoshop", cell.deepRoundtrip]]
    .filter(([, d]) => d && d.state === "done");
  if (deepRows.length || cell.saveDepth) {
    html += "<h3>Bit depth and 16-bit precision</h3>";
    if (cell.saveDepth)
      html += '<div class="nums">.psd save: ' + cell.saveDepth.resaved + "-bit (original " +
        cell.saveDepth.original + "-bit)" + (cell.saveDepth.kept ? "" :
        ' <span class="bad-text">bit depth lost</span>') + "</div>";
    if (deepRows.length)
      html += "<table><tr><th>Compared</th><th>Precise pixels</th><th>RMSE</th><th>Worst</th><th>Bits</th></tr>" +
        deepRows.map(([label, d]) => "<tr><td>" + label + "</td><td>" + pct(d.accuracy) + "</td><td>" +
          d.rmse.toFixed(3) + "</td><td>" + d.maxError.toFixed(2) + "</td><td>" + d.editorBits + "</td></tr>").join("") +
        "</table>" + '<div class="nums">RMSE and worst error are in 8-bit steps; a pixel is precise within ' +
        (deepRows[0][1].tolerance || 64) + " 16-bit levels (a quarter step).</div>";
  }
  document.getElementById("detail-body").innerHTML = html;
  document.getElementById("detail").classList.add("open");
  if (!keep) document.getElementById("detail-body").scrollTop = 0;
}
function closeDetail() { selected = null; document.getElementById("detail").classList.remove("open"); }

// The detail panel's left edge is a drag handle so the image grid can be widened
// (e.g. to line up the text-appended renders side by side); the chosen width sticks
// across runs via localStorage. Double-click resets to the default width.
(function () {
  const detail = document.getElementById("detail");
  const resizer = document.getElementById("detail-resizer");
  const clampWidth = w => Math.max(360, Math.min(Math.round(window.innerWidth * 0.96), w));
  let saved = null;
  try { saved = parseInt(localStorage.getItem("testyDetailWidth"), 10) || null; } catch (e) {}
  if (saved) detail.style.width = clampWidth(saved) + "px";
  resizer.addEventListener("mousedown", event => {
    event.preventDefault();
    document.body.classList.add("resizing");
    const move = e => { detail.style.width = clampWidth(window.innerWidth - e.clientX) + "px"; };
    const up = () => {
      document.removeEventListener("mousemove", move);
      document.removeEventListener("mouseup", up);
      document.body.classList.remove("resizing");
      try { localStorage.setItem("testyDetailWidth", parseInt(detail.style.width, 10)); } catch (e) {}
    };
    document.addEventListener("mousemove", move);
    document.addEventListener("mouseup", up);
  });
  resizer.addEventListener("dblclick", () => {
    detail.style.width = "";
    try { localStorage.removeItem("testyDetailWidth"); } catch (e) {}
  });
})();

function copyPath(fi, element) {
  const path = S.files[fi].source;
  const flash = () => {
    const tag = document.createElement("span");
    tag.className = "copied-flash";
    tag.textContent = "copied";
    element.after(tag);
    setTimeout(() => tag.remove(), 1400);
  };
  if (navigator.clipboard && navigator.clipboard.writeText) {
    navigator.clipboard.writeText(path).then(flash, () => fallbackCopy(path, flash));
  } else {
    fallbackCopy(path, flash);
  }
  if (window.event) window.event.stopPropagation();
}

function fallbackCopy(text, done) {
  const area = document.createElement("textarea");
  area.value = text;
  area.style.position = "fixed";
  area.style.opacity = "0";
  document.body.appendChild(area);
  area.select();
  let ok = false;
  try { ok = document.execCommand("copy"); } catch (e) { /* nothing more to try */ }
  area.remove();
  if (ok) done();
}

async function renderHistory() {
  try {
    const response = await fetch("../history.jsonl", { cache: "no-store" });
    if (!response.ok) return;
    const lines = (await response.text()).trim().split("\n").filter(Boolean).map(l => JSON.parse(l));
    if (!lines.length) return;
    const editors = S.run.editorOrder;
    document.getElementById("history").innerHTML = "<h2>Past runs</h2><table><tr><th>Run</th><th>Files</th>" +
      editors.map(k => "<th>" + esc((S.editors[k] || {}).displayName || k) + " byte match / kept in .psd save</th>").join("") + "</tr>" +
      lines.slice(-14).reverse().map(r => "<tr><td>" + esc(r.run) + "</td><td>" + r.files + "</td>" +
        editors.map(k => {
          const e = (r.editors || {})[k];
          return "<td>" + (e ? pct(e.render, 0) + " / " + (e.nativeMeasured === false ? "-" : pct(e.native, 0)) : "-") + "</td>";
        }).join("") + "</tr>").join("") + "</table>";
  } catch (e) { /* history is optional */ }
}

let tickTimer = null;

async function tick() {
  try {
    const response = await fetch("status.json", { cache: "no-store" });
    if (response.ok) { S = await response.json(); }
  } catch (e) { /* server restarting between polls is fine */ }
  if (RUN_ID) {
    try {
      const response = await fetch("/testy-run-state", { cache: "no-store" });
      runState = response.ok ? await response.json() : null;
    } catch (e) { runState = null; /* frozen page opened from disk */ }
    if (runState && rowRerunPending === null) {
      try {
        const response = await fetch("/testy-rerun-state?run=" + encodeURIComponent(RUN_ID), { cache: "no-store" });
        if (response.ok) { rowRerunState = await response.json(); rowRerunError = ""; }
      } catch (e) { /* retain the last known state until the server returns */ }
    }
  }
  if (S) render();
  // A finished run on a plain web host never changes: stop asking for it.
  if (!RUN_ID && S && S.state !== "running") return;
  tickTimer = setTimeout(tick, (rowRerunState && rowRerunState.state === "running") || (S && S.state === "running") ? 1200 : 5000);
}

function pollSoon() { clearTimeout(tickTimer); tick(); }
tick();
</script>
</body>
</html>
"""


def write_report_page(run_dir: Path) -> None:
    (run_dir / "report.html").write_text(_PAGE, encoding="utf-8")


def write_status(run_dir: Path, status: dict) -> None:
    """Atomic-ish status update so the polling page never reads a half-written file.

    The swap must retry: on Windows os.replace is denied (WinError 5/32) while ANY
    reader still holds status.json open, and the dashboard server or a polling
    report page reads it constantly during a live run. Readers hold it only
    briefly, so a short retry loop rides out the collision."""
    status["run"]["updateCounter"] = status["run"].get("updateCounter", 0) + 1
    temp_path = run_dir / "status.json.tmp"
    temp_path.write_text(json.dumps(status), encoding="utf-8")
    deadline = time.monotonic() + 5.0
    delay = 0.01
    while True:
        try:
            os.replace(temp_path, run_dir / "status.json")
            return
        except PermissionError:
            if time.monotonic() >= deadline:
                raise
            time.sleep(delay)
            delay = min(delay * 2, 0.25)


def append_history(testy_root: Path, summary: dict) -> None:
    runs_dir = testy_root / "runs"
    runs_dir.mkdir(parents=True, exist_ok=True)
    with open(runs_dir / "history.jsonl", "a", encoding="utf-8") as f:
        f.write(json.dumps(summary) + "\n")


def replace_history(testy_root: Path, summary: dict) -> None:
    """Swap the run's history line for `summary` (columns added to a finished run), so
    the over-time view never lists one run twice; appended when it has no line."""
    path = testy_root / "runs" / "history.jsonl"
    lines = path.read_text(encoding="utf-8").splitlines() if path.exists() else []
    revised, found = [], False
    for line in lines:
        try:
            same = json.loads(line).get("run") == summary["run"]
        except (ValueError, AttributeError):
            same = False
        if not same:
            revised.append(line)
        elif not found:
            revised.append(json.dumps(summary))
            found = True
    if not found:
        revised.append(json.dumps(summary))
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text("\n".join(revised) + "\n", encoding="utf-8")
    os.replace(temporary, path)


def append_run_index(testy_root: Path, run_name: str) -> None:
    """One line per STARTED run (history.jsonl only lists finished ones); the landing
    page merges both so live runs are clickable too."""
    runs_dir = testy_root / "runs"
    runs_dir.mkdir(parents=True, exist_ok=True)
    with open(runs_dir / "index.jsonl", "a", encoding="utf-8") as f:
        f.write(json.dumps({"run": run_name}) + "\n")
