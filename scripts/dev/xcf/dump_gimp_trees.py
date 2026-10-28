"""Writes GIMP 3's view of each .xcf file's layer tree, the reference the core test
xcf_layer_trees_match_gimp_if_available compares Patchy's import with (docs/xcf.md, "Corpus").

For every .xcf (also .xcf.gz/.xcf.bz2/.xcf.xz/.xcfgz) under SOURCE_DIR, GIMP opens the file and
writes OUT_DIR/<relative path with / replaced by __>.tree.txt: one line per layer, top first,
depth first, tab-separated:

    depth  kind  mode  visible  opacity  x  y  width  height  mask  name

kind is "group", "text" or "layer"; mode is GIMP's layer mode number (PROP_MODE); opacity is a
percentage rounded to an integer; mask is 1 when the layer has a mask. A first line
"image WIDTH HEIGHT BASE PRECISION" holds the canvas. The floating selection is not listed (Patchy
opens it as a layer of its own). A file GIMP cannot open is listed in OUT_DIR/failures.txt.
GIMP runs only through its batch Python console; nothing here reads its source.

Usage (Windows, GIMP 3 in its default per-user place):
    python -I scripts/dev/xcf/dump_gimp_trees.py SOURCE_DIR [OUT_DIR]

OUT_DIR defaults to SOURCE_DIR/gimp-trees. Files already dumped are skipped.
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


def offsets(layer):
    value = layer.get_offsets()
    return (value[-2], value[-1])


def walk(layers, depth, lines):
    for layer in layers:
        if layer.is_group():
            kind = "group"
        elif isinstance(layer, Gimp.TextLayer):
            kind = "text"
        else:
            kind = "layer"
        x, y = offsets(layer)
        name = layer.get_name().replace("\t", " ").replace("\n", " ")
        lines.append("\t".join(str(v) for v in (
            depth, kind, int(layer.get_mode()), int(layer.get_visible()), int(round(layer.get_opacity())),
            x, y, layer.get_width(), layer.get_height(), int(layer.get_mask() is not None), name)))
        if layer.is_group():
            walk(layer.get_children(), depth + 1, lines)


for source, target in JOBS:
    try:
        image = Gimp.file_load(Gimp.RunMode.NONINTERACTIVE, Gio.File.new_for_path(source))
        if image is None:
            raise RuntimeError("load returned nothing")
        lines = ["image {{}} {{}} {{}} {{}}".format(image.get_width(), image.get_height(), int(image.get_base_type()),
                                             int(image.get_precision()))]
        floating = image.get_floating_sel()
        layers = [layer for layer in image.get_layers() if floating is None or layer.get_id() != floating.get_id()]
        walk(layers, 0, lines)
        with open(target, "w", encoding="utf-8", newline="\n") as handle:
            handle.write("\n".join(lines) + "\n")
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
    out = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else os.path.join(source, "gimp-trees")
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
            target = os.path.join(out, relative.replace("/", "__") + ".tree.txt")
            if not os.path.exists(target):
                jobs.append((path.replace("\\", "/"), target.replace("\\", "/")))
    print(len(jobs), "files to dump")
    for start in range(0, len(jobs), 20):
        batch = jobs[start:start + 20]
        with tempfile.NamedTemporaryFile("w", suffix=".py", delete=False, encoding="utf-8",
                                         prefix="patchy-xcf-tree-") as handle:
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
        print("dumped", min(start + 20, len(jobs)), "of", len(jobs))


if __name__ == "__main__":
    main()
