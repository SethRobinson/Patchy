"""Writes OpenRaster (.ora) fixtures with GIMP 3 and Krita 5 (docs/ora.md, "Fixtures").

The files are made by the apps themselves from layer PNGs this script draws, so the
readers are tested against what real OpenRaster writers produce. Nothing here reads or
uses either app's source code: GIMP is driven through its batch Python console and
Krita only through its command-line export.

Usage (Windows, apps installed in their default places):
    python scripts/dev/ora/make_ora_fixtures.py [out_dir]

out_dir defaults to local-test-fixtures/ora. It receives:
    gimp-modes.ora          one tile per GIMP layer mode over an opaque backdrop
    gimp-structure.ora      nested groups, pass-through group, offsets, opacity, hidden
                            layer, Unicode names, an empty layer (8-bit)
    gimp-structure-16.ora   the same at 16 bits per channel
    gimp-*.psd              the same documents as GIMP's PSD: Krita reads them with their
                            pass-through group and 16-bit samples, which GIMP's .ora drops
    krita-*.ora             Krita's re-export of each of the above
    krita-*.png             Krita's own flattened PNG export of each
The apps' mergedimage.png inside each .ora is their own render.
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
KRITA = os.environ.get("PATCHY_KRITA", r"C:\Program Files\Krita (x64)\bin\krita.com")

TILE = 32
COLUMNS = 8
GIMP_MODES = [
    "NORMAL", "MULTIPLY", "SCREEN", "OVERLAY", "DARKEN_ONLY", "LIGHTEN_ONLY", "DODGE", "BURN",
    "HARDLIGHT", "SOFTLIGHT", "DIFFERENCE", "EXCLUSION", "HSV_HUE", "HSV_SATURATION", "HSL_COLOR",
    "HSV_VALUE", "LCH_HUE", "LCH_CHROMA", "LCH_COLOR", "LCH_LIGHTNESS", "LUMINANCE", "ADDITION",
    "SUBTRACT", "DIVIDE", "LINEAR_BURN", "VIVID_LIGHT", "PIN_LIGHT", "LINEAR_LIGHT", "HARD_MIX",
    "GRAIN_EXTRACT", "GRAIN_MERGE", "DISSOLVE",
]


def png_bytes(width, height, pixel, depth=8):
    """RGBA PNG; pixel(x, y) returns four floats in 0..1."""
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            for value in pixel(x, y):
                value = min(max(value, 0.0), 1.0)
                if depth == 16:
                    rows += struct.pack(">H", round(value * 65535))
                else:
                    rows.append(round(value * 255))

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, depth, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(rows), 9)) +
            chunk(b"IEND", b""))


def backdrop(width, height):
    def pixel(x, y):
        return (x / (width - 1), 0.25 + 0.5 * y / (height - 1), 1.0 - x / (width - 1), 1.0)
    return pixel


def tile(x, y):
    # A vertical ramp with a soft alpha edge, so both color and coverage vary.
    edge = min(x, y, TILE - 1 - x, TILE - 1 - y)
    alpha = min(1.0, 0.35 + edge / 8.0)
    return (0.9 - 0.8 * y / (TILE - 1), 0.2 + 0.6 * x / (TILE - 1), 0.5 + 0.4 * (y / (TILE - 1) - 0.5), alpha)


def write(path, data):
    with open(path, "wb") as handle:
        handle.write(data)


GIMP_SCRIPT = r'''
import gi
gi.require_version('Gimp', '3.0')
from gi.repository import Gimp, Gio

WORK = {work!r}
OUT = {out!r}
MODES = {modes!r}
TILE = {tile}
COLUMNS = {columns}


def load(image, name):
    return Gimp.file_load_layer(Gimp.RunMode.NONINTERACTIVE, image, Gio.File.new_for_path(WORK + "\\" + name))


def save(image, name):
    Gimp.file_save(Gimp.RunMode.NONINTERACTIVE, image, Gio.File.new_for_path(OUT + "\\" + name), None)


def modes_file():
    rows = (len(MODES) + COLUMNS - 1) // COLUMNS
    image = Gimp.Image.new(TILE * COLUMNS, TILE * rows, Gimp.ImageBaseType.RGB)
    base = load(image, "backdrop-modes.png")
    base.set_name("Backdrop")
    image.insert_layer(base, None, 0)
    for index, mode in enumerate(MODES):
        layer = load(image, "tile8.png")
        layer.set_name(mode.lower())
        image.insert_layer(layer, None, 0)
        layer.set_offsets((index % COLUMNS) * TILE, (index // COLUMNS) * TILE)
        layer.set_mode(getattr(Gimp.LayerMode, mode))
    save(image, "gimp-modes.ora")
    save(image, "gimp-modes.psd")


def structure_file(precision, tile_name, backdrop_name, out_name):
    image = Gimp.Image.new_with_precision(160, 120, Gimp.ImageBaseType.RGB, precision)
    base = load(image, backdrop_name)
    base.set_name("Background")
    image.insert_layer(base, None, 0)
    outer = Gimp.GroupLayer.new(image, "Outer group")
    image.insert_layer(outer, None, 0)
    inner = Gimp.GroupLayer.new(image, "Inner pass-through")
    image.insert_layer(inner, outer, 0)
    inner.set_mode(Gimp.LayerMode.PASS_THROUGH)
    a = load(image, tile_name)
    a.set_name("Offset multiply")
    image.insert_layer(a, inner, 0)
    a.set_offsets(20, 30)
    a.set_mode(Gimp.LayerMode.MULTIPLY)
    b = load(image, tile_name)
    b.set_name("Half opacity")
    image.insert_layer(b, outer, 0)
    b.set_offsets(70, 10)
    b.set_opacity(50.0)
    c = load(image, tile_name)
    c.set_name("Hidden layer")
    image.insert_layer(c, None, 0)
    c.set_offsets(100, 60)
    c.set_visible(False)
    d = load(image, tile_name)
    d.set_name("\u30ec\u30a4\u30e4\u30fc caf\u00e9 \U0001f3a8")
    image.insert_layer(d, None, 0)
    d.set_offsets(-10, 80)
    outer.set_opacity(80.0)
    empty = Gimp.Layer.new(image, "Empty layer", 160, 120, Gimp.ImageType.RGBA_IMAGE, 100.0, Gimp.LayerMode.NORMAL)
    image.insert_layer(empty, None, 0)
    empty.fill(Gimp.FillType.TRANSPARENT)
    save(image, out_name)
    save(image, out_name.replace(".ora", ".psd"))


modes_file()
structure_file(Gimp.Precision.U8_NON_LINEAR, "tile8.png", "backdrop8.png", "gimp-structure.ora")
structure_file(Gimp.Precision.U16_NON_LINEAR, "tile16.png", "backdrop16.png", "gimp-structure-16.ora")
'''


def main():
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
    out = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(root, "local-test-fixtures", "ora")
    os.makedirs(out, exist_ok=True)
    work = tempfile.mkdtemp(prefix="patchy-ora-fixtures-")
    try:
        rows = (len(GIMP_MODES) + COLUMNS - 1) // COLUMNS
        write(os.path.join(work, "backdrop-modes.png"), png_bytes(TILE * COLUMNS, TILE * rows,
                                                                  backdrop(TILE * COLUMNS, TILE * rows)))
        write(os.path.join(work, "backdrop8.png"), png_bytes(160, 120, backdrop(160, 120)))
        write(os.path.join(work, "backdrop16.png"), png_bytes(160, 120, backdrop(160, 120), depth=16))
        write(os.path.join(work, "tile8.png"), png_bytes(TILE, TILE, tile))
        write(os.path.join(work, "tile16.png"), png_bytes(TILE, TILE, tile, depth=16))
        script = os.path.join(work, "make.py")
        with open(script, "w", encoding="utf-8") as handle:
            handle.write(GIMP_SCRIPT.format(work=work, out=out, modes=GIMP_MODES, tile=TILE, columns=COLUMNS))
        subprocess.run([GIMP, "-i", "--batch-interpreter=python-fu-eval",
                        "-b", f"exec(open(r'{script}', encoding='utf-8').read())",
                        "-b", "Gimp.get_pdb().lookup_procedure('gimp-quit').run(None)"], check=True, timeout=600)
        for source in ["gimp-modes.ora", "gimp-structure.ora", "gimp-structure-16.ora", "gimp-modes.psd",
                       "gimp-structure.psd", "gimp-structure-16.psd"]:
            stem = os.path.splitext(source)[0].replace("gimp-", "krita-")
            if source.endswith(".psd"):
                stem += "-from-psd"
            for extension in (".ora", ".png"):
                subprocess.run([KRITA, os.path.join(out, source), "--export", "--export-filename",
                                os.path.join(out, stem + extension)], check=True, timeout=600)
    finally:
        if os.path.basename(work).startswith("patchy-ora-fixtures-"):
            shutil.rmtree(work, ignore_errors=True)
    print("wrote fixtures to", out)


if __name__ == "__main__":
    main()
