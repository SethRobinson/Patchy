"""Scores a Patchy build against the deep fixture corpus (docs/high-bit-depth.md).

For every scene and depth that make_deep_fixtures.py built, patchy.exe opens the PSD
and exports a PNG render and a PSD resave through its CLI (`--export`). The render is
compared with Photoshop's (testy/analyze.py): the 8-bit byte-match and perceptual
metrics against render.png, and for 16-bit documents the 16-bit precision metric
against render16.png. The resave's header says whether the bit depth was kept.

    python scripts\\dev\\deep\\score_patchy.py [--exe build\\release\\patchy.exe] [--label name]

Writes build/test-output/deep-score/<label>/summary.json (plus the renders) and prints
one line per document and a total per depth. No Photoshop needed.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "testy"))

import analyze  # noqa: E402
import testy  # noqa: E402

TIMEOUT_SECONDS = 180


def run_patchy(exe: Path, source: Path, output: Path, settings: Path, deep: bool) -> str | None:
    env = dict(os.environ, PATCHY_NO_SINGLE_INSTANCE="1", PATCHY_SETTINGS_DIR=str(settings))
    if deep:
        env["PATCHY_DEEP_EDITING"] = "1"
    try:
        completed = subprocess.run([str(exe), str(source), "--export", str(output)],
                                   capture_output=True, text=True, timeout=TIMEOUT_SECONDS, env=env)
    except subprocess.TimeoutExpired:
        return f"timeout after {TIMEOUT_SECONDS}s"
    if completed.returncode != 0 or not output.exists():
        return f"exit {completed.returncode}: {(completed.stderr or '').strip()[-300:]}"
    return None


def score(entry: dict, corpus: Path, out_dir: Path, exe: Path, worker: int, deep: bool) -> dict:
    scene, depth = entry["scene"], entry["depth"]
    files = entry["files"]
    psd = corpus / files["psd"]
    folder = out_dir / scene / str(depth)
    folder.mkdir(parents=True, exist_ok=True)
    settings = out_dir / f"settings-{worker}"
    settings.mkdir(exist_ok=True)
    result: dict = {"scene": scene, "depth": depth}
    render = folder / "patchy.png"
    resave = folder / "patchy-resave.psd"
    error = run_patchy(exe, psd, render, settings, deep)
    if error:
        result["error"] = error
        return result
    truth = corpus / files["render8"]
    size = (128, 64)
    metrics = analyze.compare_renders(truth, render, size, [], None)
    result["byte"] = metrics["accuracy"]
    result["perceptual"] = metrics["perceptual"]["accuracy"]
    if "render16" in files:
        deep = analyze.compare_deep_renders(corpus / files["render16"], render, size)
        result["deep"] = deep.get("accuracy")
        result["deepRmse"] = deep.get("rmse")
    error = run_patchy(exe, psd, resave, settings, deep)
    if error:
        result["resaveError"] = error
    else:
        result["savedDepth"] = testy.psd_header_depth(resave)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", type=Path, default=ROOT / "build" / "release" / "patchy.exe")
    parser.add_argument("--corpus", type=Path, default=ROOT / "local-test-fixtures" / "deep")
    parser.add_argument("--label", default="current")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--deep", action="store_true", help="run patchy.exe with PATCHY_DEEP_EDITING=1")
    parser.add_argument("--scenes", default="", help="comma-separated scene names (default: all)")
    parser.add_argument("--depths", default="", help="comma-separated depths (default: all)")
    args = parser.parse_args()
    manifest = json.loads((args.corpus / "manifest.json").read_text(encoding="utf-8"))
    scenes = {s for s in args.scenes.split(",") if s}
    depths = {int(d) for d in args.depths.split(",") if d}
    entries = [e for e in manifest["entries"].values()
               if e.get("ok") and (not scenes or e["scene"] in scenes) and (not depths or e["depth"] in depths)]
    entries.sort(key=lambda e: (e["depth"], e["scene"]))
    out_dir = ROOT / "build" / "test-output" / "deep-score" / args.label
    out_dir.mkdir(parents=True, exist_ok=True)

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(
            lambda pair: score(pair[1], args.corpus, out_dir, args.exe, pair[0] % args.jobs, args.deep),
            enumerate(entries)))

    totals: dict = {}
    for r in results:
        t = totals.setdefault(r["depth"], {"documents": 0, "failed": 0, "byte": [], "perceptual": [],
                                           "deep": [], "depthKept": 0})
        t["documents"] += 1
        if "error" in r:
            t["failed"] += 1
            print(f"FAIL {r['scene']:<22} {r['depth']:>2}-bit  {r['error']}")
            continue
        t["byte"].append(r["byte"])
        t["perceptual"].append(r["perceptual"])
        if r.get("deep") is not None:
            t["deep"].append(r["deep"])
        if r.get("savedDepth") is not None and r["savedDepth"] >= r["depth"]:
            t["depthKept"] += 1
        deep = f"  16-bit precise {r['deep'] * 100:5.1f}%" if r.get("deep") is not None else ""
        print(f"     {r['scene']:<22} {r['depth']:>2}-bit  byte {r['byte'] * 100:5.1f}%  "
              f"perceptual {r['perceptual'] * 100:5.1f}%{deep}  saved {r.get('savedDepth', '-')}-bit")

    summary = {"exe": str(args.exe), "photoshop": manifest.get("photoshop"), "results": results, "totals": {}}
    mean = lambda xs: round(sum(xs) / len(xs), 4) if xs else None  # noqa: E731
    for depth in sorted(totals):
        t = totals[depth]
        row = {"documents": t["documents"], "failed": t["failed"], "byte": mean(t["byte"]),
               "perceptual": mean(t["perceptual"]), "deep": mean(t["deep"]), "depthKept": t["depthKept"]}
        summary["totals"][str(depth)] = row
        print(f"{depth:>2}-bit: {row['documents']} documents, {row['failed']} failed, byte {row['byte']}, "
              f"perceptual {row['perceptual']}, 16-bit precise {row['deep']}, depth kept {row['depthKept']}")
    (out_dir / "summary.json").write_text(json.dumps(summary, indent=1), encoding="utf-8")
    print(f"summary: {out_dir / 'summary.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
