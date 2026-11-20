"""Write the offline HTML report for a native document import run."""

from __future__ import annotations

from collections import defaultdict
from html import escape
import json
from pathlib import Path
import re
from urllib.parse import quote, urlsplit

from rerun import write_text

_CHECKS = (("appearance", "Appearance"), ("document", "Document properties"), ("structure", "Editable structure"),
           ("timing", "Animation timing"))
_OPTIONAL_CHECKS = (("reopenAppearance", "Appearance after reopen"), ("linearSamples", "Linear HDR samples"))
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


def _check_details(label: str, check: dict) -> str:
    status = _status(check)
    parts = [f'<h3>{label} <span class="{status}">{_STATUS[status]}</span></h3>']
    if check.get("reason"):
        parts.append(f'<p>{_text(check["reason"])}</p>')
    metrics = {k: v for k, v in check.items() if k not in ("status", "reason")}
    if metrics:
        parts.append(f"<pre>{_text(json.dumps(metrics, ensure_ascii=False, indent=2))}</pre>")
    return "".join(parts)


def _case_html(outdir: Path, case: dict) -> str:
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
        page.extend(_case_html(outdir, case) for case in cases)
    if run.get("sources"):
        page.append("<h2>Corpus sources</h2><ul>")
        page.extend(_source_html(source) for source in run["sources"])
        page.append("</ul>")
    page.append("</main></body></html>")
    write_text(outdir / "report.html", "\n".join(page))
