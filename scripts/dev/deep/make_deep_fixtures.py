"""Builds the deep fixture corpus with Photoshop over COM (docs/high-bit-depth.md).

Each scene from make_deep_fixtures.jsx is built directly at 8, 16 and 32 bits per
channel, one DoJavaScript call per scene and depth, into
local-test-fixtures/deep/<scene>/<depth>/ (gitignored):

  <scene>-<depth>.psd   the layered document
  render.png            Photoshop's flatten, 8-bit sRGB
  render16.png          16-bit documents: the same flatten at 16 bits, sRGB
  render32.tif          32-bit documents: the flatten as uncompressed 32-bit float
                        (linear light, little-endian, no profile)

manifest.json beside them records what was built, what failed and Photoshop's version.
A scene Photoshop refuses at a depth (some blend modes and adjustments are not
available in 32-bit) is recorded as failed with Photoshop's message, not retried.

    python scripts\\dev\\deep\\make_deep_fixtures.py [--scenes base,blend-multiply] [--depths 16]

Photoshop must be installed; COM starts it. Only documents the script creates are
touched, each closed without saving changes.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]

BLEND_MODES = ("normal dissolve darken multiply colorburn linearburn darkercolor lighten screen "
               "colordodge lineardodge lightercolor overlay softlight hardlight vividlight "
               "linearlight pinlight hardmix difference exclusion subtract divide hue saturation "
               "color luminosity").split()
SCENES = (["base"] + [f"blend-{mode}" for mode in BLEND_MODES]
          + ["opacity", "alpha-ramp", "mask", "clip", "group-passthrough",
             "adj-levels", "adj-curves", "adj-huesat", "adj-brightness", "adj-exposure",
             "adj-invert", "adj-posterize", "adj-threshold",
             "fill-solid", "fill-gradient", "smart-object", "effects", "channel"])


def js_path(path: Path) -> str:
    """An ExtendScript File() argument: forward slashes, and '%' pre-encoded because
    File() URI-decodes its argument (testy/drivers/photoshop.py, _js_path)."""
    text = str(path).replace("\\", "/").replace("%", "%25").replace('"', '\\"')
    return f'"{text}"'


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, default=ROOT / "local-test-fixtures" / "deep")
    parser.add_argument("--scenes", default=",".join(SCENES))
    parser.add_argument("--depths", default="8,16,32")
    args = parser.parse_args()
    scenes = [s for s in args.scenes.split(",") if s]
    depths = [int(d) for d in args.depths.split(",") if d]
    unknown = [s for s in scenes if s not in SCENES]
    if unknown:
        print("unknown scene(s): " + ", ".join(unknown))
        return 2

    import win32com.client

    app = win32com.client.Dispatch("Photoshop.Application")
    script = (HERE / "make_deep_fixtures.jsx").read_text(encoding="utf-8")
    args.out.mkdir(parents=True, exist_ok=True)
    manifest_path = args.out / "manifest.json"
    manifest = {"photoshop": str(app.Version), "entries": {}}
    if manifest_path.exists():
        manifest["entries"] = json.loads(manifest_path.read_text(encoding="utf-8")).get("entries", {})

    failures = 0
    for scene in scenes:
        for depth in depths:
            folder = args.out / scene / str(depth)
            folder.mkdir(parents=True, exist_ok=True)
            psd = folder / f"{scene}-{depth}.psd"
            files = {"psd": psd, "render8": folder / "render.png",
                     "render16": folder / "render16.png", "render32": folder / "render32.tif"}
            header = (f'var SCENE = "{scene}"; var DEPTH = {depth};\n'
                      f"var OUT_PSD = {js_path(files['psd'])};\n"
                      f"var OUT_RENDER8 = {js_path(files['render8'])};\n"
                      f"var OUT_RENDER16 = {js_path(files['render16'])};\n"
                      f"var OUT_RENDER32 = {js_path(files['render32'])};\n")
            try:
                result = json.loads(app.DoJavaScript(header + script))
            except Exception as error:  # COM failure or unparseable result
                result = {"ok": False, "error": f"com-error: {error}"}
            entry = {"scene": scene, "depth": depth, **result}
            if result.get("ok"):
                entry["files"] = {key: str(path.relative_to(args.out)).replace("\\", "/")
                                  for key, path in files.items() if path.exists()}
            else:
                failures += 1
            manifest["entries"][f"{scene}/{depth}"] = entry
            print(f"{'ok  ' if result.get('ok') else 'FAIL'} {scene:<22} {depth:>2}-bit"
                  + ("" if result.get("ok") else f"  {result.get('error')}"))
            sys.stdout.flush()
            manifest_path.write_text(json.dumps(manifest, indent=1, sort_keys=True), encoding="utf-8")
    print(f"{len(scenes) * len(depths) - failures} built, {failures} failed; manifest: {manifest_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
