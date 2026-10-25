"""Renders .xcf files with GIMP 3 as references for Patchy's composites (docs/xcf.md, "Corpus").

For every .xcf (also .xcf.gz/.xcf.bz2/.xcf.xz/.xcfgz) under SOURCE_DIR, GIMP opens the file,
merges the visible layers clipped to the canvas and exports the result with alpha as
OUT_DIR/<relative path with / replaced by __>.png, 16-bit for images above 8 bits. A file
GIMP cannot open is listed in OUT_DIR/failures.txt. GIMP runs only through its batch Python
console; nothing here reads its source.

Usage (Windows, GIMP 3 in its default per-user place):
    python -I scripts/dev/xcf/render_with_gimp.py SOURCE_DIR [OUT_DIR]

OUT_DIR defaults to SOURCE_DIR/gimp-renders. Files already rendered are skipped.
"""

import os
import subprocess
import sys
import tempfile

GIMP = os.environ.get("PATCHY_GIMP_CONSOLE",
                      os.path.expandvars(r"%LOCALAPPDATA%\Programs\GIMP 3\bin\gimp-console-3.exe"))

SCRIPT = r'''
import gi
gi.require_version('Gimp', '3.0')
from gi.repository import Gimp, Gio

JOBS = {jobs!r}
OUT = {out!r}
failures = []
for source, target in JOBS:
    try:
        image = Gimp.file_load(Gimp.RunMode.NONINTERACTIVE, Gio.File.new_for_path(source))
        if image is None:
            raise RuntimeError("load returned nothing")
        if image.get_base_type() == Gimp.ImageBaseType.INDEXED:
            image.convert_rgb()
        if image.get_color_profile() is not None and image.get_base_type() == Gimp.ImageBaseType.RGB:
            # Patchy shows every image in sRGB: so does the reference.
            image.convert_color_profile(Gimp.ColorProfile.new_rgb_srgb(), Gimp.ColorRenderingIntent.RELATIVE_COLORIMETRIC, True)
        if image.get_floating_sel() is not None:
            Gimp.floating_sel_anchor(image.get_floating_sel())
        image.merge_visible_layers(Gimp.MergeType.CLIP_TO_IMAGE)
        if not Gimp.file_save(Gimp.RunMode.NONINTERACTIVE, image, Gio.File.new_for_path(target), None):
            raise RuntimeError("export failed")
        image.delete()
    except Exception as error:
        failures.append(source + ": " + str(error))
with open(OUT + "/failures.txt", "a", encoding="utf-8") as handle:
    for line in failures:
        handle.write(line + "\n")
'''


def is_xcf(name):
    lower = name.lower()
    return lower.endswith((".xcf", ".xcf.gz", ".xcf.bz2", ".xcf.xz", ".xcfgz", ".xcfbz2", ".xcfxz"))


def main():
    source = os.path.abspath(sys.argv[1])
    out = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else os.path.join(source, "gimp-renders")
    os.makedirs(out, exist_ok=True)
    jobs = []
    for root, _, files in os.walk(source):
        if os.path.abspath(root).startswith(out):
            continue
        for name in sorted(files):
            if not is_xcf(name):
                continue
            path = os.path.join(root, name)
            relative = os.path.relpath(path, source).replace("\\", "/")
            target = os.path.join(out, relative.replace("/", "__") + ".png")
            if not os.path.exists(target):
                jobs.append((path.replace("\\", "/"), target.replace("\\", "/")))
    print(len(jobs), "files to render")
    for start in range(0, len(jobs), 10):
        batch = jobs[start:start + 10]
        with tempfile.NamedTemporaryFile("w", suffix=".py", delete=False, encoding="utf-8",
                                         prefix="patchy-xcf-render-") as handle:
            handle.write(SCRIPT.format(jobs=batch, out=out.replace("\\", "/")))
            script = handle.name
        try:
            subprocess.run([GIMP, "-i", "--batch-interpreter=python-fu-eval",
                            "-b", f"exec(open(r'{script}', encoding='utf-8').read())",
                            "-b", "Gimp.get_pdb().lookup_procedure('gimp-quit').run(None)"],
                           timeout=1800, capture_output=True)
        except subprocess.TimeoutExpired:
            with open(os.path.join(out, "failures.txt"), "a", encoding="utf-8") as handle:
                handle.write("timeout in batch starting " + batch[0][0] + "\n")
        finally:
            os.remove(script)
        print("rendered", min(start + 10, len(jobs)), "of", len(jobs))


if __name__ == "__main__":
    main()
