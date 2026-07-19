"""Report/dashboard writer.

report.html is one self-contained page dropped into each run directory. While the
run is live it polls status.json and fills the file-by-editor matrix in place;
after the run the same page (same file) is the frozen report. No external assets.
"""

from __future__ import annotations

import json
import os
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
  body { margin: 0; background: var(--bg); color: var(--text);
         font: 13px/1.5 "Segoe UI", system-ui, sans-serif; }
  header { padding: 14px 22px; border-bottom: 1px solid var(--line); display: flex;
           align-items: baseline; gap: 18px; flex-wrap: wrap; }
  header h1 { font-size: 17px; margin: 0; letter-spacing: .4px; }
  header .meta { color: var(--dim); font-size: 12px; }
  #state-pill { padding: 2px 10px; border-radius: 10px; font-size: 11px; background: var(--panel2); }
  #state-pill.running { color: var(--warn); }
  #state-pill.done { color: var(--good); }
  #state-pill.canceled { color: var(--bad); }
  #summary { display: flex; gap: 12px; padding: 14px 22px; flex-wrap: wrap; }
  .card { background: var(--panel); border: 1px solid var(--line); border-radius: 8px;
          padding: 10px 14px; min-width: 168px; }
  .card h3 { margin: 0 0 4px; font-size: 13px; }
  .card .ver { color: var(--dim); font-size: 11px; margin-bottom: 6px; }
  .card .row { display: flex; justify-content: space-between; gap: 12px; font-size: 12px; }
  .card .row b { font-variant-numeric: tabular-nums; }
  main { padding: 0 22px 40px; }
  table.matrix { border-collapse: collapse; width: 100%; }
  .matrix th, .matrix td { border: 1px solid var(--line); padding: 7px 10px; text-align: left;
                           vertical-align: top; }
  .matrix th { background: var(--panel); font-weight: 600; }
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
  #detail { position: fixed; right: 0; top: 0; bottom: 0; width: min(880px, 92vw);
            background: var(--panel); border-left: 1px solid var(--line); padding: 18px 22px;
            overflow: auto; transform: translateX(102%); transition: transform .18s ease; z-index: 5; }
  #detail.open { transform: none; }
  #detail h2 { margin: 0 0 2px; font-size: 15px; }
  #detail .sub { color: var(--dim); margin-bottom: 12px; font-size: 12px; }
  #detail .imgs { display: flex; gap: 10px; flex-wrap: wrap; margin: 10px 0 16px; }
  #detail figure { margin: 0; }
  #detail figcaption { color: var(--dim); font-size: 11px; margin-top: 3px; }
  #detail img { max-width: 260px; border: 1px solid var(--line); border-radius: 4px;
                background: #fff; image-rendering: auto; display: block; cursor: zoom-in; }
  #detail table { border-collapse: collapse; margin: 6px 0 14px; width: 100%; font-size: 12px; }
  #detail th, #detail td { border: 1px solid var(--line); padding: 4px 8px; text-align: left; }
  #detail th { background: var(--panel2); }
  #detail .close { position: absolute; top: 10px; right: 14px; cursor: pointer; color: var(--dim);
                   font-size: 20px; border: 0; background: none; }
  .ok-text { color: var(--good); } .bad-text { color: var(--bad); } .warn-text { color: var(--warn); }
  .copyable { cursor: pointer; border-bottom: 1px dotted var(--dim); }
  .copyable:hover { color: var(--accent); }
  .copied-flash { color: var(--good); font-size: 11px; margin-left: 6px; }
  #history { margin-top: 28px; }
  #history h2 { font-size: 14px; }
  #history table { border-collapse: collapse; font-size: 12px; }
  #history th, #history td { border: 1px solid var(--line); padding: 4px 10px; }
  code { background: var(--panel2); padding: 1px 5px; border-radius: 4px; }
</style>
</head>
<body>
<header>
  <h1>Testy <span style="color:var(--dim)">PSD compatibility</span></h1>
  <span id="state-pill">loading</span>
  <span class="meta" id="run-meta"></span>
</header>
<div id="summary"></div>
<main>
  <table class="matrix"><thead id="matrix-head"></thead><tbody id="matrix-body"></tbody></table>
  <section id="history"></section>
</main>
<aside id="detail"><button class="close" onclick="closeDetail()">&times;</button><div id="detail-body"></div></aside>
<script>
"use strict";
let S = null;
let selected = null;

function pct(x, digits) { return (100 * x).toFixed(digits === undefined ? 1 : digits) + "%"; }
function esc(s) { return String(s == null ? "" : s).replace(/[&<>"]/g,
  c => ({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;"}[c])); }

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

function lossText(losses) {
  return losses.map(l => l.lost + "/" + l.total + " " + l.label).join(", ");
}

function cellSummary(cell) {
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
  if (cell.renderMetrics) bits.push("render " + pct(cell.renderMetrics.accuracy));
  if (cell.native && cell.native.perCategory)
    bits.push("native " + cell.native.nativeKept + "/" + cell.native.nativeTotal);
  if (cell.renderMetrics && cell.renderMetrics.objectsScored)
    bits.push("objects " + cell.renderMetrics.objectsRenderedOk + "/" + cell.renderMetrics.objectsScored);
  const flags = [];
  if (cell.trapSentinelFraction > 0.05) flags.push("flat-composite cheat");
  if (cell.renderMetrics && cell.renderMetrics.sizeMismatch) flags.push("size mismatch");
  if (cell.opens === "fallback-render") flags.push("PS needed fallback render");
  if (cell.resaveRejected) flags.push("resave rejected by Photoshop");
  const losses = lossSummary(cell.native);
  const lossLine = losses.length
    ? '<div class="flag">lost: ' + lossText(losses.slice(0, 3)) +
      (losses.length > 3 ? " +" + (losses.length - 3) + " more" : "") + "</div>"
    : "";
  const cls = cell.opens === "fail" ? "bad" : (flags.length || losses.length ? "warn" : "ok");
  return '<div class="status-line"><span class="dot ' + cls + '"></span>' +
         (cell.opens === "fail" ? "failed to open" : "opened") + '</div>' +
         '<div class="nums">' + bits.join(" &middot; ") + '</div>' + lossLine +
         (flags.length ? '<div class="flag">' + flags.join(" &middot; ") + '</div>' : "");
}

function render() {
  if (!S) return;
  const pill = document.getElementById("state-pill");
  pill.textContent = S.state;
  pill.className = S.state;
  document.getElementById("run-meta").textContent =
    S.run.startedAt + "  -  " + S.files.length + " file(s)  -  Patchy " + (S.run.patchyVersion || "?");

  const editors = S.run.editorOrder;
  document.getElementById("matrix-head").innerHTML =
    "<tr><th>PSD</th>" + editors.map(k => {
      const e = S.editors[k] || {};
      return "<th>" + esc(e.displayName || k) +
             '<div class="nums">' + esc(e.version || "") + "</div></th>";
    }).join("") + "</tr>";

  document.getElementById("matrix-body").innerHTML = S.files.map((f, fi) => {
    const gt = f.groundTruth || {};
    const gtNote = gt.state === "failed" ? '<div class="flag">ground truth failed</div>'
      : (gt.state === "running" ? '<div class="nums">ground truth: ' + esc(gt.stage || "...") + "</div>" : "");
    return "<tr><td class='file'><b class='copyable' title='" + esc(f.source) +
      " (click to copy path)' onclick='copyPath(" + fi + ", this)'>" + esc(f.name) + "</b>" +
      '<div class="nums">' + (f.docSize ? f.docSize[0] + "x" + f.docSize[1] : "") +
      (f.layerCount ? " &middot; " + f.layerCount + " layers" : "") + "</div>" + gtNote + "</td>" +
      editors.map(k => "<td class='cell' onclick='openDetail(" + fi + ",\"" + k + "\")'>" +
                       cellSummary((f.cells || {})[k]) + "</td>").join("") + "</tr>";
  }).join("");

  const agg = {};
  editors.forEach(k => agg[k] = { opened: 0, total: 0, acc: [], native: [], text: [0, 0], adj: [0, 0], smart: [0, 0], fx: [0, 0] });
  S.files.forEach(f => editors.forEach(k => {
    const c = (f.cells || {})[k];
    if (!c || c.state === "pending" || c.state === "running" || c.state === "skipped") return;
    const a = agg[k];
    a.total++;
    if (c.state === "done" && c.opens !== "fail") a.opened++;
    if (c.renderMetrics) a.acc.push(c.renderMetrics.accuracy);
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
  document.getElementById("summary").innerHTML = editors.map(k => {
    const e = S.editors[k] || {}, a = agg[k];
    const rows = [];
    rows.push(["opened", a.total ? a.opened + "/" + a.total : "-"]);
    const acc = mean(a.acc); rows.push(["render", acc == null ? "-" : pct(acc)]);
    const nat = mean(a.native); rows.push(["native", nat == null ? "-" : pct(nat)]);
    [["text", "text kept"], ["adj", "adjustments"], ["smart", "smart objects"], ["fx", "live effects"]].forEach(([key, label]) => {
      const v = a[key];
      if (v[1]) rows.push([label, v[0] < v[1] ? '<span class="bad-text">' + v[0] + "/" + v[1] + "</span>"
                                              : v[0] + "/" + v[1]]);
    });
    return '<div class="card"><h3>' + esc(e.displayName || k) + '</h3><div class="ver">' +
      esc(e.version || "") + "</div>" +
      rows.map(r => '<div class="row"><span>' + r[0] + "</span><b>" + r[1] + "</b></div>").join("") +
      "</div>";
  }).join("");
  renderHistory();
  if (selected) openDetail(selected[0], selected[1], true);
}

function img(fig, cap, full) {
  if (!fig) return "";
  const target = (full || fig) + "?v=" + (S.run.updateCounter || 0);
  return "<figure><a href='" + target + "' target='_blank' title='open full size'>" +
         "<img src='" + fig + "?v=" + (S.run.updateCounter || 0) + "'></a>" +
         "<figcaption>" + cap + "</figcaption></figure>";
}

function openDetail(fi, ek, keep) {
  selected = [fi, ek];
  const f = S.files[fi], cell = (f.cells || {})[ek] || {}, gt = f.groundTruth || {};
  const art = cell.artifacts || {}, gart = gt.artifacts || {};
  let html = "<h2><span class='copyable' title='" + esc(f.source) +
    " (click to copy path)' onclick='copyPath(" + fi + ", this)'>" + esc(f.name) + "</span>" +
    " &middot; " + esc((S.editors[ek] || {}).displayName || ek) + "</h2>" +
    '<div class="sub">' + esc(cell.state) + (cell.stage ? " - " + esc(cell.stage) : "") +
    (cell.error ? ' - <span class="bad-text">' + esc(cell.error) + "</span>" : "") + "</div>";
  html += '<div class="imgs">' +
    img(gart.renderThumb, "Photoshop ground truth", gart.render) +
    img(art.renderThumb, "Editor render", art.render) +
    img(art.heatmap, "Difference heatmap") +
    img(art.trapThumb, "Trap render (sentinel = used baked composite)", art.trap) +
    img(art.roundtripThumb, "Resave reopened in Photoshop", art.roundtripRender) +
    img(gart.mutatedThumb, "PS render, text appended", gart.mutated) +
    img(art.mutatedThumb, "Editor render, text appended", art.mutated) +
    "</div>";
  if (cell.renderMetrics) {
    const m = cell.renderMetrics;
    html += "<table><tr><th>Render accuracy</th><th>RMSE</th><th>Pixels off</th><th>Objects ok</th><th>Trap sentinel</th></tr>" +
      "<tr><td>" + pct(m.accuracy) + "</td><td>" + m.rmse + "</td><td>" + pct(m.badFraction) +
      "</td><td>" + m.objectsRenderedOk + "/" + m.objectsScored + "</td><td>" +
      (cell.trapSentinelFraction == null ? "-" : pct(cell.trapSentinelFraction)) + "</td></tr></table>";
    const worst = (m.perObject || []).filter(o => !o.ok).slice(0, 12);
    if (worst.length) {
      html += "<h3>Worst-rendered objects</h3><table><tr><th>Layer</th><th>Kind</th><th>Bad pixels</th></tr>" +
        worst.map(o => "<tr><td>" + esc(o.name) + "</td><td>" + esc(o.kind) + "</td><td>" +
                       pct(o.badFraction) + "</td></tr>").join("") + "</table>";
    }
  }
  if (cell.native && cell.native.perCategory) {
    const n = cell.native, pc = n.perCategory, at = n.attributes;
    html += "<h3>Native preservation (via Photoshop reopen): " + n.nativeKept + "/" + n.nativeTotal + "</h3>";
    const losses = lossSummary(n);
    if (losses.length) {
      const changed = n.changedLayers || [];
      const gone = changed.filter(c => c.became == null).length;
      const converted = changed.filter(c => c.became != null).length;
      let detail;
      if (!gone && !converted)
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
    html += "<h3>Native preservation (via Photoshop reopen)</h3>" +
      '<div class="loss-banner"><b>Resave rejected: Photoshop could not open this editor&#39;s PSD</b>' +
      '<div class="nums">' + esc(cell.native.error) + "</div></div>";
  }
  if (cell.roundtripRender) {
    html += "<h3>Round trip back into Photoshop</h3><table><tr><th>Accuracy vs original</th><th>Pixels off</th></tr>" +
      "<tr><td>" + pct(cell.roundtripRender.accuracy) + "</td><td>" + pct(cell.roundtripRender.badFraction) +
      "</td></tr></table>";
  }
  if (cell.textRender) {
    html += "<h3>Forced text re-render vs Photoshop</h3><table><tr><th>Accuracy</th><th>Pixels off</th></tr>" +
      "<tr><td>" + pct(cell.textRender.accuracy) + "</td><td>" + pct(cell.textRender.badFraction) + "</td></tr></table>";
  }
  document.getElementById("detail-body").innerHTML = html;
  document.getElementById("detail").classList.add("open");
  if (!keep) document.getElementById("detail").scrollTop = 0;
}
function closeDetail() { selected = null; document.getElementById("detail").classList.remove("open"); }

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
      editors.map(k => "<th>" + esc((S.editors[k] || {}).displayName || k) + " render / native</th>").join("") + "</tr>" +
      lines.slice(-14).reverse().map(r => "<tr><td>" + esc(r.run) + "</td><td>" + r.files + "</td>" +
        editors.map(k => {
          const e = (r.editors || {})[k];
          return "<td>" + (e ? pct(e.render, 0) + " / " + pct(e.native, 0) : "-") + "</td>";
        }).join("") + "</tr>").join("") + "</table>";
  } catch (e) { /* history is optional */ }
}

async function tick() {
  try {
    const response = await fetch("status.json", { cache: "no-store" });
    if (response.ok) { S = await response.json(); render(); }
  } catch (e) { /* server restarting between polls is fine */ }
  setTimeout(tick, S && S.state === "done" ? 5000 : 1200);
}
tick();
</script>
</body>
</html>
"""


def write_report_page(run_dir: Path) -> None:
    (run_dir / "report.html").write_text(_PAGE, encoding="utf-8")


def write_status(run_dir: Path, status: dict) -> None:
    """Atomic-ish status update so the polling page never reads a half-written file."""
    status["run"]["updateCounter"] = status["run"].get("updateCounter", 0) + 1
    temp_path = run_dir / "status.json.tmp"
    temp_path.write_text(json.dumps(status), encoding="utf-8")
    os.replace(temp_path, run_dir / "status.json")


def append_history(testy_root: Path, summary: dict) -> None:
    runs_dir = testy_root / "runs"
    runs_dir.mkdir(parents=True, exist_ok=True)
    with open(runs_dir / "history.jsonl", "a", encoding="utf-8") as f:
        f.write(json.dumps(summary) + "\n")


def append_run_index(testy_root: Path, run_name: str) -> None:
    """One line per STARTED run (history.jsonl only lists finished ones); the landing
    page merges both so live runs are clickable too."""
    runs_dir = testy_root / "runs"
    runs_dir.mkdir(parents=True, exist_ok=True)
    with open(runs_dir / "index.jsonl", "a", encoding="utf-8") as f:
        f.write(json.dumps({"run": run_name}) + "\n")
