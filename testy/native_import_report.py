"""Write the offline HTML report for a native document import run."""

from __future__ import annotations

from collections import defaultdict
from html import escape
import json
from pathlib import Path
import re
from urllib.parse import quote, urlsplit

import native_scoring
from rerun import write_text

_CHECKS = (("appearance", "Appearance"), ("document", "Document properties"), ("structure", "Editable structure"),
           ("timing", "Animation timing"))
_OPTIONAL_CHECKS = (("reopenAppearance", "Appearance after reopen"), ("linearSamples", "Linear HDR samples"),
                    ("saveReopen", "Patchy save reopened in Aseprite"))
# Columns of the cross-editor tables: where a cell keeps each check.
_CELL_CHECKS = (("Render match", ("appearance",)),)
_PSD_CHECKS = (("Photoshop opens the PSD", ("psd", "accepted")), ("PSD render match", ("psd", "appearance")),
               ("All layers kept in the PSD", ("psd", "layers")))
_STATUS = {"pass": "Pass", "fail": "Fail", "unscored": "Unscored"}
_FORMATS = {"xcf": "GIMP XCF", "kra": "Krita KRA", "ora": "OpenRaster",
            "aseprite": "Aseprite"}
_STYLE = """
:root {color-scheme:dark; --bg:#14161a; --panel:#1d2026; --line:#373b44; --text:#d8dade;
  --dim:#adb2bc; --good:#69ce82; --bad:#f38677; --warn:#e1b461; --accent:#7bb9ef}
* {box-sizing:border-box}
body {margin:0; background:var(--bg); color:var(--text); font:14px/1.5 "Segoe UI",system-ui,sans-serif}
main {max-width:1260px; margin:auto; padding:24px clamp(14px,3vw,36px) 48px}
h1 {font-size:22px; margin:0} h2 {font-size:18px; margin:24px 0 8px}
h3 {font-size:15px; margin:16px 0 6px} p {margin:5px 0 10px}
.meta,.unscored,small {color:var(--dim)} a {color:var(--accent)}
.pass {color:var(--good)} .fail {color:var(--bad)} .notice {color:var(--warn)}
.table-scroll {overflow-x:auto} table {border-collapse:collapse; width:100%}
th,td {border:1px solid var(--line); padding:7px 10px; text-align:left; vertical-align:top}
th {background:var(--panel)} small {display:block; font-size:12px}
details.case {border-top:1px solid var(--line)} details.case:last-of-type {border-bottom:1px solid var(--line)}
summary {padding:8px 0; cursor:pointer; overflow-wrap:anywhere}
summary strong {margin-right:12px} .state {display:inline-block; font-size:12px; margin-right:12px}
.body {background:var(--panel); border-radius:5px; padding:8px 16px 12px; margin-bottom:8px; overflow-wrap:anywhere}
pre {white-space:pre-wrap; overflow-wrap:anywhere; font:12px/1.5 Consolas,monospace; margin:4px 0 8px}
.body details {color:var(--dim)} .body ul {margin:0 0 10px; padding-left:20px}
:focus-visible {outline:2px solid var(--accent); outline-offset:2px}
"""


def _text(value) -> str:
    return escape(str(value))


def _status(check: dict) -> str:
    status = check.get("status")
    return status if status in _STATUS else "unscored"


def _render_href(outdir: Path, folder, name) -> str | None:
    """Only link a PNG inside the case's generated directory in this run."""
    if not isinstance(name, str) or not isinstance(folder, str):
        return None
    name, folder = name.replace("\\", "/"), folder.replace("\\", "/")
    if (not re.fullmatch(r"cells/[0-9]+", folder) or not name.startswith(folder + "/")
            or not name.lower().endswith(".png") or ":" in name
            or any(part in ("", ".", "..") for part in name.split("/"))
            or any(ord(ch) < 32 for ch in name)):
        return None
    root = outdir.resolve()
    try:
        target = (root / name).resolve(strict=True)
        if not target.is_relative_to(root / folder) or not target.is_file():
            return None
    except (OSError, RuntimeError, ValueError):
        return None
    return quote(name)


def _format_summary(cases: list[dict], checks) -> str:
    attempted = sum(case["opened"] is not None for case in cases)
    opened = sum(case["opened"] is True for case in cases)
    exported = sum(bool(case["exported"]) for case in cases)
    missing = len(cases) - attempted
    skipped = f"<small>{missing} not measured</small>" if missing else ""
    cells = [f"<td>{opened} / {attempted}{skipped}</td>", f"<td>{exported} / {opened}</td>"]
    cells.append(f'<td>{sum(bool(case.get("notes")) for case in cases)}</td>')
    for check in checks:
        key = check[0]
        statuses = [_status(case.get(key, {})) for case in cases if check in _CHECKS or key in case]
        if not statuses:
            cells.append("<td>No reference</td>")
            continue
        passed, failed = statuses.count("pass"), statuses.count("fail")
        unscored = statuses.count("unscored")
        cells.append(f"<td>{passed} / {passed + failed}<small>{failed} failed"
                     f" &middot; {unscored} unscored</small></td>")
    return "".join(cells)


def _dig(cell: dict, path) -> dict | None:
    for key in path:
        cell = cell.get(key) if isinstance(cell, dict) else None
    return cell if isinstance(cell, dict) else None


def _tally(checks: list[dict]) -> str:
    """Passes out of scored results, as the single-editor table words it."""
    if not checks:
        return "<td>Not measured</td>"
    statuses = [_status(check) for check in checks]
    passed, failed = statuses.count("pass"), statuses.count("fail")
    return (f"<td>{passed} / {passed + failed}<small>{failed} failed &middot; "
            f"{statuses.count('unscored')} unscored</small></td>")


def _editor_label(run: dict, key: str, fmt: str | None = None) -> str:
    info = run["editors"].get(key, {})
    label = f'{_text(info.get("name", key))} <small>{_text(info.get("version", ""))}</small>'
    if fmt is not None and run.get("authoring", {}).get(fmt) == key:
        label += "<small>the format's own app</small>"
    return label


def _percent(value, files=None) -> str:
    if value is None:
        return '<td class="unscored">-</td>'
    shade = "pass" if value >= 0.9 else "fail" if value < 0.5 else "notice"
    count = f"<small>{files} {'file' if files == 1 else 'files'}</small>" if files is not None else ""
    return f'<td><strong class="{shade}">{value:.1%}</strong>{count}</td>'


def _score_overview(run: dict, scores: dict) -> str:
    """Editors down, formats across: the two headline scores, then every file together."""
    judged = scores.get("judged")
    columns = [(_FORMATS.get(fmt, fmt), rows) for fmt, rows in scores["formats"].items()]
    columns.append(("All files", scores["all"]))
    span = 2 if judged else 1
    head = "<th>Editor</th>" + "".join(f'<th colspan="{span}">{_text(label)}</th>' for label, _ in columns)
    sub = "<th></th>" + ("<th>Import</th>" + ("<th>To PSD</th>" if judged else "")) * len(columns)
    body = []
    for key in run["editors"]:
        cells = []
        for _, rows in columns:
            row = rows.get(key) or {}
            cells.append(_percent(row.get("import"), row.get("importFiles")))
            if judged:
                cells.append(_percent(row.get("conversion"), row.get("conversionFiles")))
        body.append(f'<tr><th scope="row">{_editor_label(run, key)}</th>{"".join(cells)}</tr>')
    return (f'<div class="table-scroll"><table><thead><tr>{head}</tr><tr>{sub}</tr></thead>'
            f'<tbody>{"".join(body)}</tbody></table></div>')


def _outcome_table(run: dict, rows: dict) -> str:
    """Where each editor's files ended up: every file in exactly one column, its first
    problem in pipeline order."""
    present = [(key, label) for key, label in native_scoring.OUTCOMES
               if any(row.get("outcomes", {}).get(key) for row in rows.values())]
    head = "<th>Editor</th>" + "".join(f"<th>{_text(label)}</th>" for _, label in present)
    body = []
    for key in run["editors"]:
        counts = (rows.get(key) or {}).get("outcomes", {})
        cells = "".join(f'<td class="{"" if counts.get(name) else "unscored"}">{counts.get(name, 0)}</td>'
                        for name, _ in present)
        body.append(f'<tr><th scope="row">{_text(run["editors"][key].get("name", key))}</th>{cells}</tr>')
    return (f'<div class="table-scroll"><table><thead><tr>{head}</tr></thead>'
            f'<tbody>{"".join(body)}</tbody></table></div>')


def _editor_summary(run: dict, fmt: str, cases: list[dict], judged: bool, scores: dict | None = None) -> str:
    """One row per editor for a format. Every file was attempted, so an editor that
    reads none of a format shows the measured 0 of N, never a guess at why."""
    rows = []
    for key in run["editors"]:
        cells = [case["cells"][key] for case in cases if key in case.get("cells", {})]
        rows.append(f'<tr><th scope="row">{_editor_label(run, key, fmt)}</th>')
        if scores is not None:
            row = scores.get(key) or {}
            rows.append(_percent(row.get("import"), row.get("importFiles")))
            if judged:
                rows.append(_percent(row.get("conversion"), row.get("conversionFiles")))
        tried = [cell for cell in cells if cell.get("opened") is not None]
        opened = sum(cell["opened"] is True for cell in tried)
        skipped = len(cells) - len(tried)
        none = '<small class="notice">Opened none of this format</small>' if tried and not opened else ""
        rows.append(f"<td>{opened} / {len(tried)}{none}"
                    + (f"<small>{skipped} not measured</small>" if skipped else "") + "</td>")
        rows.extend(_tally([check for cell in tried if (check := _dig(cell, path))]) for _, path in _CELL_CHECKS)
        if judged:
            saves = [cell["savedPsd"] for cell in tried if cell.get("savedPsd") is not None]
            rows.append(f"<td>{sum(saves)} / {len(saves)}</td>" if saves else "<td>Not measured</td>")
            for label, path in _PSD_CHECKS:
                found = [check for cell in tried if (check := _dig(cell, path))]
                cell_html = _tally(found)
                if path[-1] == "layers":
                    kept = sum(check.get("layersKept", 0) for check in found)
                    total = sum(check.get("layersTotal", 0) for check in found)
                    if total:
                        cell_html = cell_html.replace("</small></td>", f" &middot; {kept} / {total} layers</small></td>")
                rows.append(cell_html)
        rows.append("</tr>")
    head = "<th>Editor</th>"
    if scores is not None:
        head += "<th>Import score</th>" + ("<th>To PSD score</th>" if judged else "")
    head += "<th>Opened / attempted</th>"
    head += "".join(f"<th>{label}</th>" for label, _ in _CELL_CHECKS)
    if judged:
        head += "<th>PSD saved</th>" + "".join(f"<th>{label}</th>" for label, _ in _PSD_CHECKS)
    return (f'<div class="table-scroll"><table><thead><tr>{head}</tr></thead>'
            f'<tbody>{"".join(rows)}</tbody></table></div>')


def _cell_state(cell: dict) -> tuple[str, str]:
    if cell.get("skipped"):
        return "unscored", cell["skipped"]
    if cell.get("opened") is None:
        return "unscored", "Not measured"
    if not cell["opened"]:
        return "fail", "Crashed or hung" if cell.get("appFailure") else "Did not open"
    return ("", "Opened") if cell.get("rendered") else ("fail", "Opened, no render")


def _check_cell(check: dict | None) -> str:
    if check is None:
        return '<td class="unscored">-</td>'
    status = _status(check)
    reason = f'<small>{_text(check["reason"])}</small>' if check.get("reason") else ""
    return f'<td><span class="{status}">{_STATUS[status]}</span>{reason}</td>'


def _cells_html(outdir: Path, run: dict, case: dict) -> str:
    judged = "psdJudge" in run
    scored = case.get("scores") or {}
    head = "<th>Editor</th><th>Result</th>" + "".join(f"<th>{label}</th>" for label, _ in _CELL_CHECKS)
    if judged:
        head += "".join(f"<th>{label}</th>" for label, _ in _PSD_CHECKS)
    rows = []
    for key, cell in case["cells"].items():
        state, outcome = _cell_state(cell)
        detail = [f'<span class="{state}">{_text(outcome)}</span>']
        marks = [f"{label} {scored[key][name]:.1%}" for name, label in (("import", "Import"), ("conversion", "To PSD"))
                 if name in scored.get(key, {})]
        if marks:
            detail.append(f'<small>{" &middot; ".join(marks)}</small>')
        for field in ("error", "psdError"):
            if cell.get(field):
                detail.append(f"<small>{_text(cell[field])}</small>")
        detail.extend(f'<small class="notice">{_text(note)}</small>' for note in cell.get("notes") or [])
        links = [(cell.get("render"), "render")]
        if key != "patchy" and (cell.get("appearance") or {}).get("comparisonImages"):
            links += [(f'{case.get("folder")}/{key}/reference.png', "reference frame"),
                      (f'{case.get("folder")}/{key}/actual.png', "its frame as compared")]
        if _dig(cell, ("psd", "accepted")) and _status(cell["psd"]["accepted"]) == "pass":
            links.append((f'{case.get("folder")}/{key}/photoshop.png', "Photoshop's render of the PSD"))
        for name, label in links:
            href = _render_href(outdir, case.get("folder"), name)
            if href:
                detail.append(f'<small><a href="{href}">{label}</a></small>')
        for warning in _dig(cell, ("psd",)) and cell["psd"].get("warnings") or []:
            detail.append(f'<small class="notice">Photoshop said: {_text(warning)}</small>')
        rows.append(f'<tr><th scope="row">{_editor_label(run, key)}</th><td>{"".join(detail)}</td>'
                    + "".join(_check_cell(_dig(cell, path)) for _, path in
                              _CELL_CHECKS + (_PSD_CHECKS if judged else ())) + "</tr>")
    return (f'<h3>Editors</h3><div class="table-scroll"><table><thead><tr>{head}</tr></thead>'
            f'<tbody>{"".join(rows)}</tbody></table></div>')


def _check_details(label: str, check: dict) -> str:
    status = _status(check)
    parts = [f'<h3>{label} <span class="{status}">{_STATUS[status]}</span></h3>']
    if check.get("reason"):
        parts.append(f'<p>{_text(check["reason"])}</p>')
    metrics = {k: v for k, v in check.items() if k not in ("status", "reason")}
    if metrics:
        parts.append(f"<pre>{_text(json.dumps(metrics, ensure_ascii=False, indent=2))}</pre>")
    return "".join(parts)


def _case_html(outdir: Path, case: dict, run: dict | None = None) -> str:
    if case["opened"] is None:
        state, outcome = "unscored", "Open not measured"
    elif not case["opened"]:
        state, outcome = "fail", "Open failed"
    elif not case["exported"]:
        state, outcome = "fail", "Export failed"
    else:
        state, outcome = "", "Opened and exported"
    parts = [f'<details class="case"><summary><strong>{_text(case["id"])}</strong>',
             f'<span class="state {state}">{outcome}</span>']
    checks = _CHECKS + tuple(check for check in _OPTIONAL_CHECKS if check[0] in case)
    for key, label in checks:
        status = _status(case.get(key, {}))
        parts.append(f'<span class="state {status}">{label} · {_STATUS[status]}</span>')
    notes = case.get("notes") or []
    if notes:
        parts.append(f'<span class="state notice">Notes ({len(notes)})</span>')
    parts.append(f'</summary><div class="body"><p>Source <strong>{_text(case["source"])}</strong></p>')
    if case.get("error"):
        parts.append(f'<p class="{state}">{_text(case["error"])}</p>')
    if notes:
        items = "".join(f"<li>{_text(note)}</li>" for note in notes)
        parts.append(f'<h3 class="notice">Notes</h3><ul>{items}</ul>')
    if case.get("referenceNote"):
        parts.append(f'<p><strong>Reference</strong> {_text(case["referenceNote"])}</p>')
    chosen = case.get("reference")
    if isinstance(chosen, dict):
        checked = {"agrees": "The reference supplied with the corpus agrees.",
                   "differs": "The reference supplied with the corpus disagrees."}.get(chosen.get("crossCheck"), "")
        source = str(chosen.get("source") or "").rstrip(".")
        said = " ".join(_text(text) for text in (source and source + ".", checked, chosen.get("note")) if text)
        parts.append(f'<p><strong>Reference used</strong> {said}</p>')
    if run is not None and case.get("cells"):
        parts.append(_cells_html(outdir, run, case))
    folder = case.get("folder")
    href = _render_href(outdir, folder, case.get("render"))
    if href:
        parts.append(f'<p><a href="{href}">View Patchy render</a></p>')
    if case["appearance"].get("comparisonImages") and folder:
        for name, label in (("reference.png", "Reference frame"), ("actual.png", "Patchy frame")):
            href = _render_href(outdir, folder, f"{folder}/{name}")
            if href:
                parts.append(f'<p><a href="{href}">{label} from first appearance failure</a></p>')
    for key, label in checks:
        parts.append(_check_details(label, case.get(key, {})))
    if case.get("observed"):
        facts = json.dumps(case["observed"], ensure_ascii=False, indent=2)
        parts.append(f'<details><summary>Imported document properties</summary><pre>{_text(facts)}</pre></details>')
    parts.append("</div></details>")
    return "".join(parts)


def _source_html(source) -> str:
    if not isinstance(source, dict):
        return f"<li>{_text(source)}</li>"
    credit = [_text(source[field]) for field in ("credit", "license") if source.get(field)]
    url = str(source.get("url") or "")
    if url:
        try:
            parsed = urlsplit(url)
            web = parsed.scheme in ("https", "http") and parsed.hostname
        except ValueError:
            web = False
        if web and "\\" not in url and all(ord(ch) >= 32 for ch in url):
            credit.append(f'<a href="{_text(url)}">{_text(url)}</a>')
        else:
            credit.append(_text(url))
    return f'<li>{" &middot; ".join(credit)}</li>'


def write_report(outdir: Path, run: dict) -> None:
    by_format = defaultdict(list)
    for case in run["cases"]:
        by_format[case["format"]].append(case)
    groups = sorted(by_format.items())
    name = _text(run["name"])
    page = ['<!doctype html><html lang="en"><head><meta charset="utf-8">',
            '<meta name="viewport" content="width=device-width, initial-scale=1">',
            f"<title>{name} | Testy native import</title><style>{_STYLE}</style></head><body><main>",
            f"<h1>Testy native import · {name}</h1>"]
    meta = [f'Patchy {_text(run.get("patchyVersion") or "version unknown")}']
    if run.get("patchyHash"):
        meta.append(f'Build <code>{_text(run["patchyHash"])}</code>')
    if run.get("state"):
        meta.append(f'Run {_text(run["state"])}')
    if run.get("total") is not None:
        meta.append(f'{len(run["cases"])} / {_text(run["total"])} files processed')
    for key, label in (("startedAt", "Started"), ("finishedAt", "Finished")):
        if run.get(key):
            meta.append(f'{label} {_text(run[key])}')
    page.append(f'<p class="meta">{" &middot; ".join(meta)}</p>')
    if run.get("error"):
        page.append(f'<p class="fail">{_text(run["error"])}</p>')
    compared = run if isinstance(run.get("editors"), dict) else None
    if compared:
        for key, reason in (run.get("unavailable") or {}).items():
            page.append(f'<p class="notice">{_text(key)} was not measured: {_text(reason)}</p>')
        if run.get("psdJudge"):
            page.append(f'<p class="meta">Converted PSDs were opened in Photoshop '
                        f'{_text(run["psdJudge"].get("photoshop") or "version unknown")}.</p>')
        scores = run.get("scores") or native_scoring.apply(run)
        judged = "psdJudge" in run
        page.append("<h2>Scores</h2>")
        page.append("<p><strong>Import</strong> is how well an editor shows a file: the share of its own "
                    "picture that matches the reference, averaged over files. "
                    + ("<strong>To PSD</strong> is how well it converts one: the average of whether Photoshop "
                       "opens its PSD without a warning, how much of Photoshop's picture of that PSD matches, and "
                       "the share of the source's layers still layers of their kind. " if judged else "")
                    + "A file an editor does not open, or crashes on, scores 0 on everything. A file the harness "
                    "could not measure, and a check no editor could be scored on, are left out.</p>")
        page.append(_score_overview(run, scores))
        page.append("<h2>Editors by format</h2>")
        page.append("<p>Each editor was given the same files. Render match compares its own PNG with the "
                    "reference, for the files it opened. Layers kept counts "
                    "the source's layers that Photoshop still finds as layers of the same kind in the "
                    "converted PSD; an editor may instead bake a look a PSD cannot hold, which keeps the "
                    "render and loses the layer. The second table puts every file in one column: its first "
                    "problem, in the order a file moves through an editor.</p>")
        for fmt, cases in groups:
            source = (run.get("references") or {}).get(fmt)
            page.append(f"<h3>{_text(_FORMATS.get(fmt, fmt))}</h3>"
                        + (f'<p class="meta">Reference: {_text(source)}</p>' if source else ""))
            page.append(_editor_summary(run, fmt, cases, judged, scores["formats"].get(fmt, {})))
            page.append(_outcome_table(run, scores["formats"].get(fmt, {})))
        page.append("<h2>Patchy import detail by format</h2>")
    else:
        page.append("<h2>Results by format</h2>")
    page.append("<p>Checks show passes out of files with a scored result. Expand a file to see its evidence.</p>")
    page.append('<div class="table-scroll"><table><thead><tr><th>Format</th>'
                '<th>Opened / attempted</th><th>PNG exports / opened</th><th>Files with import notes</th>')
    checks = _CHECKS + tuple(check for check in _OPTIONAL_CHECKS if any(check[0] in row for row in run["cases"]))
    page.extend(f"<th>{label}</th>" for _, label in checks)
    page.append("</tr></thead><tbody>")
    for index, (fmt, cases) in enumerate(groups):
        label = _text(_FORMATS.get(fmt, fmt))
        page.append(f'<tr><th scope="row"><a href="#format-{index}">{label}</a></th>'
                    f'{_format_summary(cases, checks)}</tr>')
    page.append("</tbody></table></div>")
    if not groups:
        page.append("<p>No files in this run.</p>")
    for index, (fmt, cases) in enumerate(groups):
        page.append(f'<h2 id="format-{index}">{_text(_FORMATS.get(fmt, fmt))} ({len(cases)})</h2>')
        page.extend(_case_html(outdir, case, compared) for case in cases)
    if run.get("sources"):
        page.append("<h2>Corpus sources</h2><ul>")
        page.extend(_source_html(source) for source in run["sources"])
        page.append("</ul>")
    page.append("</main></body></html>")
    write_text(outdir / "report.html", "\n".join(page))
