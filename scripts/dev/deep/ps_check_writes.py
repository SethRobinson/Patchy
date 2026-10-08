"""Checks deep PSDs Patchy wrote against Photoshop (docs/high-bit-depth.md).

For every scene and depth of the deep fixture corpus, patchy.exe (deep editing on)
opens the Photoshop-made PSD and saves it again (`--export x.psd`). Photoshop then
opens Patchy's file through Testy's driver (testy/drivers/photoshop.py: dialog guard,
hang watchdog, restart on a wedge) and renders it. Reported per file: whether
Photoshop opened it, any alert it raised, its depth, and how its render compares with
Photoshop's render of the original (16-bit precision for 16-bit files).

    python scripts\\dev\\deep\\ps_check_writes.py [--scenes base,mask] [--depths 16,32] [--label name]

Writes build/test-output/deep-writes/<label>/summary.json. Photoshop must be installed.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "testy"))

import analyze  # noqa: E402
import testy  # noqa: E402
from drivers import photoshop  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", type=Path, default=ROOT / "build" / "release" / "patchy.exe")
    parser.add_argument("--corpus", type=Path, default=ROOT / "local-test-fixtures" / "deep")
    parser.add_argument("--scenes", default="")
    parser.add_argument("--depths", default="16,32")
    parser.add_argument("--label", default="current")
    args = parser.parse_args()
    manifest = json.loads((args.corpus / "manifest.json").read_text(encoding="utf-8"))
    scenes = {s for s in args.scenes.split(",") if s}
    depths = {int(d) for d in args.depths.split(",") if d}
    out_dir = ROOT / "build" / "test-output" / "deep-writes" / args.label
    out_dir.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, PATCHY_DEEP_EDITING="1", PATCHY_NO_SINGLE_INSTANCE="1",
               PATCHY_SETTINGS_DIR=str(out_dir / "settings"))
    driver = photoshop.PhotoshopDriver(log=lambda line: print("    " + line))
    results = []
    for key, entry in sorted(manifest["entries"].items()):
        if not entry.get("ok") or entry["depth"] not in depths or (scenes and entry["scene"] not in scenes):
            continue
        scene, depth = entry["scene"], entry["depth"]
        source = args.corpus / entry["files"]["psd"]
        written = out_dir / f"{scene}-{depth}.psd"
        written.unlink(missing_ok=True)
        completed = subprocess.run([str(args.exe), str(source), "--export", str(written)],
                                   capture_output=True, text=True, timeout=300, env=env)
        result = {"scene": scene, "depth": depth}
        if completed.returncode != 0 or not written.exists():
            result["patchyError"] = f"exit {completed.returncode}: {(completed.stderr or '').strip()[-300:]}"
            results.append(result)
            print(f"FAIL {scene:<22} {depth:>2}  Patchy did not write it: {result['patchyError']}")
            continue
        result["writtenDepth"] = testy.psd_header_depth(written)
        render = out_dir / f"{scene}-{depth}.png"
        probe = driver.probe(written, render)
        result["opened"] = bool(probe.get("ok"))
        if probe.get("dialogs"):
            result["dialogs"] = probe["dialogs"]
        if not probe.get("ok"):
            result["error"] = probe.get("error")
        if render.exists():
            truth = args.corpus / entry["files"]["render8"]
            size = (128, 64)
            metrics = analyze.compare_renders(truth, render, size, [], None)
            result["byte"] = metrics["accuracy"]
            result["perceptual"] = metrics["perceptual"]["accuracy"]
            deep_render = photoshop.deep_render_path(render)
            if "render16" in entry["files"] and deep_render is not None and deep_render.exists():
                deep = analyze.compare_deep_renders(args.corpus / entry["files"]["render16"], deep_render, size)
                result["deep"] = deep.get("accuracy")
        results.append(result)
        verdict = "ok  " if result["opened"] and not result.get("dialogs") else "FAIL"
        detail = (f"byte {result.get('byte', 0) * 100:5.1f}%  perceptual {result.get('perceptual', 0) * 100:5.1f}%"
                  + (f"  16-bit {result['deep'] * 100:5.1f}%" if result.get("deep") is not None else "")
                  if "byte" in result else result.get("error", ""))
        print(f"{verdict} {scene:<22} {depth:>2}  written {result['writtenDepth']}-bit  {detail}"
              + (f"  dialogs: {result['dialogs']}" if result.get("dialogs") else ""))
        sys.stdout.flush()
    (out_dir / "summary.json").write_text(json.dumps(results, indent=1), encoding="utf-8")
    failures = [r for r in results if not r.get("opened") or r.get("dialogs")]
    print(f"{len(results)} files, {len(failures)} refused or warned; summary: {out_dir / 'summary.json'}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
