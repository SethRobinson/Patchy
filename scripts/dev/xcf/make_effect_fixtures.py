"""Writes GIMP 3 layer-effect calibration files (docs/xcf.md, "Layer effects").

One 64 x 64 image of 4096 colors (16 levels of red, green and blue) gets one GIMP filter as
a layer effect per file; GIMP saves the .xcf and exports its render as a PNG beside it, so
Patchy's baked effect can be fitted and checked against GIMP's own output. GIMP runs only
through its batch Python console; nothing here reads its source.

Usage: python -I scripts/dev/xcf/make_effect_fixtures.py [out_dir]
out_dir defaults to local-test-fixtures/xcf/effects.
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

# (file stem, operation, {property: value})
EFFECTS = [
    ("brightness-up", "gimp:brightness-contrast", {"brightness": 0.3, "contrast": 0.0}),
    ("brightness-down", "gimp:brightness-contrast", {"brightness": -0.3, "contrast": 0.0}),
    ("contrast-up", "gimp:brightness-contrast", {"brightness": 0.0, "contrast": 0.4}),
    ("contrast-down", "gimp:brightness-contrast", {"brightness": 0.0, "contrast": -0.4}),
    ("brightness-contrast", "gimp:brightness-contrast", {"brightness": 0.2, "contrast": 0.3}),
    ("hue-30", "gimp:hue-saturation", {"hue": 30.0 / 180.0}),
    ("saturation-up", "gimp:hue-saturation", {"saturation": 0.5}),
    ("saturation-down", "gimp:hue-saturation", {"saturation": -0.5}),
    ("lightness-up", "gimp:hue-saturation", {"lightness": 0.3}),
    ("lightness-down", "gimp:hue-saturation", {"lightness": -0.3}),
    ("hue-saturation", "gimp:hue-saturation", {"hue": -60.0 / 180.0, "saturation": 0.2, "lightness": 0.1}),
    ("colorize", "gimp:colorize", {"hue": 0.5, "saturation": 0.5, "lightness": 0.0}),
    ("colorize-2", "gimp:colorize", {"hue": 0.1, "saturation": 0.8, "lightness": 0.2}),
    ("desaturate-0", "gimp:desaturate", {"mode": 0}),
    ("desaturate-1", "gimp:desaturate", {"mode": 1}),
    ("desaturate-2", "gimp:desaturate", {"mode": 2}),
    ("desaturate-3", "gimp:desaturate", {"mode": 3}),
    ("desaturate-4", "gimp:desaturate", {"mode": 4}),
    ("threshold", "gimp:threshold", {"low": 0.4, "high": 0.9}),
    ("posterize", "gimp:posterize", {"levels": 4}),
    ("levels-linear", "gimp:levels", {"low-input": 0.1, "high-input": 0.9, "gamma": 1.5, "low-output": 0.05,
                                      "high-output": 0.95, "linear": True}),
    ("levels-perceptual", "gimp:levels", {"low-input": 0.1, "high-input": 0.9, "gamma": 1.5, "low-output": 0.05,
                                          "high-output": 0.95, "linear": False}),
    ("invert-gamma", "gegl:invert-gamma", {}),
    ("invert-linear", "gegl:invert-linear", {}),
    ("exposure", "gegl:exposure", {"exposure": 0.7, "black-level": 0.02}),
    ("mono-mixer", "gegl:mono-mixer", {"red": 0.5, "green": 0.3, "blue": 0.2, "preserve-luminosity": False}),
    ("color-balance", "gimp:color-balance", {"range": 1, "cyan-red": 0.3, "magenta-green": -0.2,
                                             "yellow-blue": 0.1, "preserve-luminosity": True}),
    ("half-opacity-desaturate", "gimp:desaturate", {"mode": 1, "@opacity": 0.5}),
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


def sweep(x, y):
    index = y * 64 + x
    r, g, b = index % 16, (index // 16) % 16, index // 256
    return (r * 17, g * 17, b * 17, 255)


SCRIPT = r'''
import gi
gi.require_version('Gimp', '3.0')
from gi.repository import Gimp, Gio

WORK = {work!r}
OUT = {out!r}
EFFECTS = {effects!r}
for stem, op, props in EFFECTS:
    image = Gimp.Image.new(64, 64, Gimp.ImageBaseType.RGB)
    layer = Gimp.file_load_layer(Gimp.RunMode.NONINTERACTIVE, image, Gio.File.new_for_path(WORK + "/sweep.png"))
    image.insert_layer(layer, None, 0)
    flt = Gimp.DrawableFilter.new(layer, op, stem)
    config = flt.get_config()
    for key, value in props.items():
        if key == "@opacity":
            flt.set_opacity(value)
        else:
            config.set_property(key, value)
    flt.update()
    layer.append_filter(flt)
    Gimp.file_save(Gimp.RunMode.NONINTERACTIVE, image, Gio.File.new_for_path(OUT + "/" + stem + ".xcf"), None)
    render = image.duplicate()
    render.merge_visible_layers(Gimp.MergeType.CLIP_TO_IMAGE)
    Gimp.file_save(Gimp.RunMode.NONINTERACTIVE, render, Gio.File.new_for_path(OUT + "/" + stem + ".png"), None)
    render.delete()
    image.delete()
'''


def main():
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
    out = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(root, "local-test-fixtures", "xcf",
                                                                            "effects")
    os.makedirs(out, exist_ok=True)
    work = tempfile.mkdtemp(prefix="patchy-xcf-effects-")
    try:
        with open(os.path.join(work, "sweep.png"), "wb") as handle:
            handle.write(png_bytes(64, 64, sweep))
        script = os.path.join(work, "make.py")
        with open(script, "w", encoding="utf-8") as handle:
            handle.write(SCRIPT.format(work=work.replace("\\", "/"), out=out.replace("\\", "/"), effects=EFFECTS))
        result = subprocess.run([GIMP, "-i", "--batch-interpreter=python-fu-eval",
                                 "-b", f"exec(open(r'{script}', encoding='utf-8').read())",
                                 "-b", "Gimp.get_pdb().lookup_procedure('gimp-quit').run(None)"],
                                timeout=1200, capture_output=True, text=True, errors="replace")
        for line in ((result.stdout or "") + (result.stderr or "")).splitlines():
            if "Error" in line or "Traceback" in line or "line " in line:
                print(line)
    finally:
        if os.path.basename(work).startswith("patchy-xcf-effects-"):
            shutil.rmtree(work, ignore_errors=True)
    print("wrote effect fixtures to", out)


if __name__ == "__main__":
    main()
