"""Writes the GIMP 3 layer-effect fixture for effects Patchy runs through its own GEGL
(docs/xcf.md, "Layer effects (GIMP 3 filters)").

One image holds a grid of 48 x 48 tiles over a gray background; each tile is its own layer
with one GIMP filter as a layer effect. GIMP saves the .xcf and exports its render as a PNG
beside it, so the test can compare Patchy's import tile by tile. GIMP runs only through its
batch Python console; nothing here reads its source.

Usage: python -I scripts/dev/xcf/make_gegl_effect_fixture.py [out_dir]
out_dir defaults to test-fixtures/xcf (the committed gegl-effects.xcf and .png).
"""

import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

GIMP = os.environ.get("PATCHY_GIMP_CONSOLE",
                      os.path.expandvars(r"%LOCALAPPDATA%\Programs\GIMP 3\bin\gimp-console-3.exe"))

TILE = 48
PITCH = 64
COLUMNS = 8

# (layer name, operation, {property: value}); "@opacity" is the filter's blending opacity.
EFFECTS = [
    ("unsharp-mask", "gegl:unsharp-mask", {"std-dev": 2.0, "scale": 1.5}),
    ("pixelize", "gegl:pixelize", {"size-x": 6, "size-y": 6}),
    ("bloom", "gegl:bloom", {"threshold": 40.0, "softness": 25.0, "radius": 6.0, "strength": 60.0}),
    ("noise-rgb", "gegl:noise-rgb", {"seed": 5, "red": 0.2, "green": 0.2, "blue": 0.2}),
    ("noise-hsv", "gegl:noise-hsv", {"seed": 5, "hue-distance": 10.0, "saturation-distance": 0.1,
                                     "value-distance": 0.1}),
    ("noise-spread", "gegl:noise-spread", {"seed": 5, "amount-x": 4, "amount-y": 4}),
    ("noise-hurl", "gegl:noise-hurl", {"seed": 5, "pct-random": 20.0}),
    ("noise-pick", "gegl:noise-pick", {"seed": 5}),
    ("noise-slur", "gegl:noise-slur", {"seed": 5}),
    ("noise-cie-lch", "gegl:noise-cie-lch", {"seed": 5}),
    ("shadows-highlights", "gegl:shadows-highlights", {"shadows": 40.0, "highlights": -30.0}),
    ("color-temperature", "gegl:color-temperature", {"original-temperature": 6500.0,
                                                     "intended-temperature": 4000.0}),
    ("channel-mixer", "gegl:channel-mixer", {"rr-gain": 0.8, "rg-gain": 0.2, "rb-gain": 0.0}),
    ("mean-curvature-blur", "gegl:mean-curvature-blur", {"iterations": 10}),
    ("motion-blur-linear", "gegl:motion-blur-linear", {"length": 10.0, "angle": 30.0}),
    ("emboss", "gegl:emboss", {"azimuth": 30.0, "elevation": 45.0, "depth": 3}),
    ("cartoon", "gegl:cartoon", {"mask-radius": 4.0, "pct-black": 0.2}),
    ("vignette", "gegl:vignette", {"radius": 1.0, "softness": 0.5}),
    ("edge-sobel", "gegl:edge-sobel", {}),
    ("high-pass", "gegl:high-pass", {"std-dev": 4.0, "contrast": 1.0}),
    ("snn-mean", "gegl:snn-mean", {"radius": 4}),
    ("softglow", "gegl:softglow", {"glow-radius": 6.0}),
    ("ripple", "gegl:ripple", {"amplitude": 4.0, "period": 16.0}),
    ("color-enhance", "gegl:color-enhance", {}),
    ("stretch-contrast", "gegl:stretch-contrast", {}),
    ("difference-of-gaussians", "gegl:difference-of-gaussians", {"radius1": 1.0, "radius2": 3.0}),
    ("noise-reduction", "gegl:noise-reduction", {"iterations": 4}),
    ("photocopy", "gegl:photocopy", {}),
    ("sepia", "gegl:sepia", {"scale": 0.8}),
    ("waves", "gegl:waves", {"amplitude": 4.0, "period": 20.0}),
    ("whirl-pinch", "gegl:whirl-pinch", {"whirl": 90.0}),
    ("cubism", "gegl:cubism", {"tile-size": 8.0, "seed": 3}),
    ("color-to-alpha", "gegl:color-to-alpha", {}),
    ("edge", "gegl:edge", {}),
    ("antialias", "gegl:antialias", {}),
    ("value-invert", "gegl:value-invert", {}),
    ("pixelize-half", "gegl:pixelize", {"size-x": 8, "size-y": 8, "@opacity": 0.5}),
    ("lens-distortion", "gegl:lens-distortion", {"main": 30.0}),
    ("wind", "gegl:wind", {"seed": 4, "strength": 5}),
    ("median-blur", "gegl:median-blur", {"radius": 3}),
    ("curves", "gimp:curves", {}),
]


def png_bytes(width, height, pixel):
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            rows += bytes(pixel(x, y))

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(rows), 9)) +
            chunk(b"IEND", b""))


def tile(x, y):
    # Two ramps, a bright disc and a dark bar: smooth areas, hard edges and highlights.
    r = x * 255 // (TILE - 1)
    g = y * 255 // (TILE - 1)
    b = 60
    if (x - 30) ** 2 + (y - 18) ** 2 <= 100:
        r, g, b = 250, 240, 200
    if 8 <= x <= 14 and 20 <= y <= 42:
        r, g, b = 20, 30, 90
    return (r, g, b, 255)


def background(x, y):
    return (128, 128, 128, 255)


SCRIPT = r'''
import gi
gi.require_version('Gimp', '3.0')
from gi.repository import Gimp, Gio

WORK = {work!r}
OUT = {out!r}
EFFECTS = {effects!r}
PITCH = {pitch!r}
COLUMNS = {columns!r}
rows = (len(EFFECTS) + COLUMNS - 1) // COLUMNS
image = Gimp.Image.new(PITCH * COLUMNS, PITCH * rows, Gimp.ImageBaseType.RGB)
back = Gimp.file_load_layer(Gimp.RunMode.NONINTERACTIVE, image, Gio.File.new_for_path(WORK + "/background.png"))
image.insert_layer(back, None, 0)
back.set_name("background")
for index, (name, op, props) in enumerate(EFFECTS):
    layer = Gimp.file_load_layer(Gimp.RunMode.NONINTERACTIVE, image, Gio.File.new_for_path(WORK + "/tile.png"))
    image.insert_layer(layer, None, 0)
    layer.set_name(name)
    layer.set_offsets((index % COLUMNS) * PITCH + 8, (index // COLUMNS) * PITCH + 8)
    try:
        flt = Gimp.DrawableFilter.new(layer, op, name)
        config = flt.get_config()
        for key, value in props.items():
            if key == "@opacity":
                flt.set_opacity(value)
            else:
                config.set_property(key, value)
        flt.update()
        layer.append_filter(flt)
    except Exception as error:
        print("Error: " + name + ": " + str(error))
Gimp.file_save(Gimp.RunMode.NONINTERACTIVE, image, Gio.File.new_for_path(OUT + "/gegl-effects.xcf"), None)
render = image.duplicate()
render.merge_visible_layers(Gimp.MergeType.CLIP_TO_IMAGE)
Gimp.file_save(Gimp.RunMode.NONINTERACTIVE, render, Gio.File.new_for_path(OUT + "/gegl-effects.png"), None)
render.delete()
image.delete()
'''


def main():
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
    out = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(root, "test-fixtures", "xcf")
    os.makedirs(out, exist_ok=True)
    work = tempfile.mkdtemp(prefix="patchy-xcf-gegl-effects-")
    try:
        with open(os.path.join(work, "tile.png"), "wb") as handle:
            handle.write(png_bytes(TILE, TILE, tile))
        rows = (len(EFFECTS) + COLUMNS - 1) // COLUMNS
        with open(os.path.join(work, "background.png"), "wb") as handle:
            handle.write(png_bytes(PITCH * COLUMNS, PITCH * rows, background))
        script = os.path.join(work, "make.py")
        with open(script, "w", encoding="utf-8") as handle:
            handle.write(SCRIPT.format(work=work.replace("\\", "/"), out=out.replace("\\", "/"), effects=EFFECTS,
                                       pitch=PITCH, columns=COLUMNS))
        result = subprocess.run([GIMP, "-i", "--batch-interpreter=python-fu-eval",
                                 "-b", f"exec(open(r'{script}', encoding='utf-8').read())",
                                 "-b", "Gimp.get_pdb().lookup_procedure('gimp-quit').run(None)"],
                                timeout=1200, capture_output=True, text=True, errors="replace")
        for line in ((result.stdout or "") + (result.stderr or "")).splitlines():
            if "Error" in line or "Traceback" in line or "line " in line:
                print(line)
    finally:
        if os.path.basename(work).startswith("patchy-xcf-gegl-effects-"):
            shutil.rmtree(work, ignore_errors=True)
    print("wrote gegl-effects.xcf and .png to", out)


if __name__ == "__main__":
    main()
