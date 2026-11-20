"""Export one finished Testy run as a folder any web server can host.

    python testy/export_static.py <run name or path> [--out DIR] [--title TEXT]

The folder holds index.html (a short landing page), report.html (the same page the
dashboard serves; off the Testy server it hides its run controls and reads status.json
beside it), status.json, results.json, and every image the report links to. Copy the
folder as it is; nothing in it refers back to this machine.

What is left out on purpose:
- every .psd/.psb (the staged copies of the corpus files and each editor's resave):
  the corpus is third-party work with mixed origins, and the report does not need them;
- renders the report never links (the cache-free leg's working files);
- local paths: each file's source becomes its path below the corpus folder, and any
  other string that names this machine's folders or user fails the export instead of
  slipping through.

Nothing here uploads anything. Default output: testy/public/<run name>/ (gitignored).
"""

from __future__ import annotations

import argparse
import html
import json
import os
import shutil
import sys
from pathlib import Path

import config
import report

IMAGE_SUFFIXES = (".png", ".jpg", ".jpeg", ".webp")
DOCUMENT_SUFFIXES = (".psd", ".psb")
EXPORT_MARKER = "testy-export.txt"


def _common_parent(paths: list[str]) -> list[str]:
    """The folder parts every source path shares."""
    split = [p.replace("\\", "/").split("/")[:-1] for p in paths if p]
    if not split:
        return []
    common = split[0]
    for parts in split[1:]:
        size = 0
        while size < len(common) and size < len(parts) and common[size] == parts[size]:
            size += 1
        common = common[:size]
    return common


def public_status(status: dict) -> tuple[dict, list[str]]:
    """(a copy of `status` fit to publish, the artifact paths it still links to).

    Sources become paths below the folder all of them share, prefixed with that folder's
    own name (so the report's "By folder" grouping is unchanged); links to PSD/PSB
    artifacts are removed."""
    public = json.loads(json.dumps(status))
    files = public.get("files") or []
    common = _common_parent([str(entry.get("source") or "") for entry in files])
    for entry in files:
        source = str(entry.get("source") or "").replace("\\", "/")
        if source:
            parts = source.split("/")
            kept = parts[max(len(common) - 1, 0):] if common else parts[-1:]
            entry["source"] = "/".join(kept)
    artifacts: list[str] = []

    def scrub(block: dict | None) -> None:
        links = (block or {}).get("artifacts")
        if not isinstance(links, dict):
            return
        for key in list(links):
            target = links[key]
            # Local float TIFFs can carry the original document's metadata.
            # Their numeric measurements travel with the public report.
            if key == "render32" or not isinstance(target, str) or target.lower().endswith(DOCUMENT_SUFFIXES):
                del links[key]
            else:
                artifacts.append(target)

    for entry in files:
        scrub(entry.get("groundTruth"))
        for cell in (entry.get("cells") or {}).values():
            scrub(cell)
            scrub(cell.get("groundTruth"))
    return public, artifacts


def scrub_paths(value, roots: list[tuple[str, str]]):
    """`value` with every occurrence of a root folder replaced by its label, in strings
    at any depth (error messages and driver notes quote full paths). Matching ignores
    case and the direction of the slashes."""
    if isinstance(value, dict):
        return {key: scrub_paths(item, roots) for key, item in value.items()}
    if isinstance(value, list):
        return [scrub_paths(item, roots) for item in value]
    if not isinstance(value, str):
        return value
    for root, label in roots:
        if not root:
            continue
        for variant in {root.replace("/", "\\"), root.replace("\\", "/")}:
            position = value.lower().find(variant.lower())
            while position >= 0:
                value = value[:position] + label + value[position + len(variant):]
                position = value.lower().find(variant.lower(), position + len(label))
    return value


def private_strings(value, found: list[str] | None = None) -> list[str]:
    """Strings anywhere in `value` that still name this machine's user or the
    repository's location. (An installed program's path, such as one under Program
    Files, is not private.)"""
    found = [] if found is None else found
    markers = {str(Path.home()).lower(), str(config.TESTY_ROOT.parent).lower()}
    user = os.environ.get("USERNAME") or os.environ.get("USER") or ""
    if isinstance(value, dict):
        for item in value.values():
            private_strings(item, found)
    elif isinstance(value, list):
        for item in value:
            private_strings(item, found)
    elif isinstance(value, str):
        normalized = value.lower().replace("/", "\\")
        if (any(marker and marker in normalized for marker in markers)
                or (user and f"\\users\\{user.lower()}" in normalized)):
            found.append(value)
    return found


def _index_page(title: str, status: dict) -> str:
    run = status.get("run") or {}
    editors = status.get("editors") or {}
    order = run.get("editorOrder") or list(editors)
    rows = "".join(
        f"<tr><td>{html.escape(str((editors.get(key) or {}).get('displayName') or key))}</td>"
        f"<td>{html.escape(str((editors.get(key) or {}).get('version') or ''))}</td></tr>"
        for key in order)
    files = len(status.get("files") or [])
    started = html.escape(str(run.get("startedAt") or ""))
    finished = html.escape(str(run.get("finishedAt") or ""))
    credit = run.get("corpus") or {}
    if credit:
        corpus_note = (
            f'<p><b>The test files.</b> They are the <a href="{html.escape(str(credit.get("url") or ""))}">'
            f'{html.escape(str(credit.get("name") or "test collection"))}</a> of the '
            f'<a href="{html.escape(str(credit.get("projectUrl") or ""))}">{html.escape(str(credit.get("project") or ""))} '
            f'project</a> ({html.escape(str(credit.get("license") or ""))} license), commit '
            f'<code>{html.escape(str(credit.get("commit") or "")[:12])}</code>: several hundred small single-feature '
            f'PSD and PSB files, which is what makes a failing cell name a feature. Thanks to its authors. '
            f'The files themselves are not included here; only the renders are.</p>')
    else:
        corpus_note = ('<p class="dim">The test files are not included here; only the renders are.</p>')
    return f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{html.escape(title)}</title>
<style>
  body {{ font: 16px/1.5 system-ui, sans-serif; background: #1f2126; color: #e6e6e6; margin: 0; }}
  main {{ max-width: 760px; margin: 0 auto; padding: 40px 20px 60px; }}
  h1 {{ font-size: 28px; margin: 0 0 6px; }}
  p {{ margin: 12px 0; }}
  a {{ color: #6db3ff; }}
  .open {{ display: inline-block; margin: 18px 0; padding: 12px 22px; background: #2f6fd0; color: #fff;
           border-radius: 6px; text-decoration: none; font-weight: 600; }}
  table {{ border-collapse: collapse; margin: 10px 0; }}
  td {{ padding: 4px 18px 4px 0; border-bottom: 1px solid #353941; }}
  .dim {{ color: #a0a4ad; font-size: 14px; }}
</style>
</head>
<body>
<main>
<h1>{html.escape(title)}</h1>
<p class="dim">Testy v2. {files} PSD and PSB files. Run started {started}, finished {finished}.</p>
<p><b>What this measures, and what it does not.</b> This is a test of one thing: how faithfully each
program loads, renders and saves Photoshop PSD and PSB files. It says nothing about how good a program
is at anything else. A low score here means that keeping your documents as PSDs and moving them between
that program and Photoshop will lose things; it is not a verdict on the program itself. Testy is open
source and part of the Patchy project: <a href="https://github.com/SethRobinson/Patchy/blob/main/docs/testy.md">how the test works</a> and
<a href="https://github.com/SethRobinson/Patchy">the code on GitHub</a>.</p>
<p>Each file was opened in every editor below. Photoshop's own render is the reference. An editor is
scored on what it draws itself: the baked pixels Photoshop stores in a PSD for text, shapes, fills
and smart objects are removed first, so showing those is not counted as rendering. The report also
shows what each editor keeps when it saves the file back to PSD.</p>
<a class="open" href="report.html">Open the full report</a>
<p class="dim">In the report, click any cell for the images, the difference map and the details.</p>
<table>
<tr><td><b>Editor</b></td><td><b>Version tested</b></td></tr>
{rows}
</table>
{corpus_note}
</main>
</body>
</html>
"""


def export_run(run_dir: Path, out_dir: Path, title: str) -> dict:
    status = json.loads((run_dir / "status.json").read_text(encoding="utf-8"))
    public, artifacts = public_status(status)
    # Longest first, so the run folder is named before the repository that holds it.
    roots = [(str(run_dir), "<run>"), (str(config.TESTY_ROOT), "<testy>"),
             (str(config.TESTY_ROOT.parent), "<patchy>"), (str(Path.home()), "<home>")]
    public = scrub_paths(public, roots)
    leaks = private_strings(public)
    if leaks:
        raise SystemExit("refusing to export: the status still names this machine, e.g. " + leaks[0][:160])

    if out_dir.exists() and any(out_dir.iterdir()):
        # Only ever replace a folder an earlier export made (it carries the marker below);
        # anything else at that path is somebody's data.
        if not (out_dir / EXPORT_MARKER).is_file():
            raise SystemExit(f"refusing to write into {out_dir}: it is not empty and not an earlier export")
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / EXPORT_MARKER).write_text("Written by testy/export_static.py; safe to replace.\n", encoding="utf-8")
    copied = 0
    missing = 0
    total_bytes = 0
    for relative in sorted(set(artifacts)):
        if not relative.lower().endswith(IMAGE_SUFFIXES) or ".." in Path(relative).parts:
            continue
        source = run_dir / relative
        if not source.is_file():
            missing += 1
            continue
        target = out_dir / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        copied += 1
        total_bytes += target.stat().st_size

    (out_dir / "status.json").write_text(json.dumps(public), encoding="utf-8")
    results = run_dir / "results.json"
    if results.is_file():
        results_public, _ = public_status(json.loads(results.read_text(encoding="utf-8")))
        results_public = scrub_paths(results_public, roots)
        if not private_strings(results_public):
            (out_dir / "results.json").write_text(json.dumps(results_public), encoding="utf-8")
    # The current page, not the copy written when the run started: later fixes to the
    # report (and its behavior off the Testy server) apply to older runs too.
    report.write_report_page(out_dir)
    (out_dir / "index.html").write_text(_index_page(title, public), encoding="utf-8")
    return {"out": str(out_dir), "images": copied, "missing": missing, "bytes": total_bytes,
            "files": len(public.get("files") or [])}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("run", help="a run name under testy/runs, or a path to a run folder")
    parser.add_argument("--out", help="output folder (default: testy/public/<run name>)")
    parser.add_argument("--title", default="Testy: PSD compatibility test", help="heading of index.html")
    args = parser.parse_args(argv)

    run_dir = Path(args.run)
    if not (run_dir / "status.json").is_file():
        run_dir = config.RUNS_DIR / args.run
    if not (run_dir / "status.json").is_file():
        print(f"no status.json in {run_dir}", file=sys.stderr)
        return 2
    out_dir = Path(args.out) if args.out else config.TESTY_ROOT / "public" / run_dir.name
    summary = export_run(run_dir, out_dir, args.title)
    print(f"exported {summary['files']} files, {summary['images']} images, "
          f"{summary['bytes'] / 1_048_576:.1f} MB to {summary['out']}"
          + (f" ({summary['missing']} linked images were not on disk)" if summary["missing"] else ""))
    print("Nothing was uploaded. Copy that folder to a web server to publish it.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
