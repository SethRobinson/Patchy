"""Writes extra OpenRaster corpus files with GIMP 3 and Krita 5 (docs/ora.md, "Corpus").

Usage: python scripts/dev/ora/make_corpus_files.py OUT_FOLDER

GIMP builds each document through its batch Python console from layer PNGs drawn here and
saves it as .ora and .psd; Krita re-exports every one of them as .ora (Krita reads the PSD
with its pass-through groups). Each file lands in its own folder, as corpus_check.py expects.
Documents: a large canvas with many layers placed on and off the canvas, deep nesting with
pass-through and isolated groups, opacity and visibility mixes, empty layers, non-ASCII
names, and every GIMP layer mode with partial opacity.
"""

import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

GIMP = os.environ.get("PATCHY_GIMP_CONSOLE",
                      os.path.expandvars(r"%LOCALAPPDATA%\Programs\GIMP 3\bin\gimp-console-3.exe"))
KRITA = os.environ.get("PATCHY_KRITA", r"C:\Program Files\Krita (x64)\bin\krita.com")


def png_bytes(width, height, pixel):
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            rows += bytes(min(255, max(0, round(v * 255))) for v in pixel(x, y))

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(rows), 6)) +
            chunk(b"IEND", b""))


def blob(seed, size):
    r = random.Random(seed)
    red, green, blue = r.random(), r.random(), r.random()

    def pixel(x, y):
        dx = (x - size / 2) / (size / 2)
        dy = (y - size / 2) / (size / 2)
        alpha = max(0.0, 1.0 - (dx * dx + dy * dy))
        return (red * (0.5 + 0.5 * x / size), green, blue * (0.5 + 0.5 * y / size), alpha)
    return pixel


GIMP_SCRIPT = r'''
import random
import gi
gi.require_version('Gimp', '3.0')
from gi.repository import Gimp, Gio

WORK = {work!r}
OUT = {out!r}


def load(image, name):
    return Gimp.file_load_layer(Gimp.RunMode.NONINTERACTIVE, image, Gio.File.new_for_path(WORK + "\\" + name))


def save(image, stem):
    for extension in (".ora", ".psd"):
        Gimp.file_save(Gimp.RunMode.NONINTERACTIVE, image, Gio.File.new_for_path(OUT + "\\" + stem + extension), None)


def big():
    r = random.Random(7)
    image = Gimp.Image.new(4000, 3000, Gimp.ImageBaseType.RGB)
    base = load(image, "backdrop.png")
    base.set_name("Backdrop")
    image.insert_layer(base, None, 0)
    for index in range(48):
        layer = load(image, "blob%d.png" % (index % 8))
        layer.set_name("Blob %02d" % index)
        image.insert_layer(layer, None, 0)
        layer.set_offsets(r.randint(-300, 3900), r.randint(-300, 2900))
        layer.set_opacity(float(r.choice([100, 100, 80, 50, 25])))
        if index % 11 == 5:
            layer.set_visible(False)
    save(image, "gimp-big-48-layers")


def deep():
    image = Gimp.Image.new(256, 192, Gimp.ImageBaseType.RGB)
    base = load(image, "backdrop-small.png")
    base.set_name("Backdrop")
    image.insert_layer(base, None, 0)
    parent = None
    for level in range(9):
        group = Gimp.GroupLayer.new(image, "Level %d" % level)
        image.insert_layer(group, parent, 0)
        if level % 2 == 1:
            group.set_mode(Gimp.LayerMode.PASS_THROUGH)
        group.set_opacity(90.0 if level % 3 == 0 else 100.0)
        layer = load(image, "blob%d.png" % (level % 8))
        layer.set_name("Paint %d" % level)
        image.insert_layer(layer, group, 0)
        layer.set_offsets(level * 20, level * 12)
        layer.set_mode([Gimp.LayerMode.MULTIPLY, Gimp.LayerMode.SCREEN, Gimp.LayerMode.OVERLAY,
                        Gimp.LayerMode.NORMAL][level % 4])
        parent = group
    empty = Gimp.Layer.new(image, "Empty", 256, 192, Gimp.ImageType.RGBA_IMAGE, 100.0, Gimp.LayerMode.NORMAL)
    image.insert_layer(empty, parent, 0)
    empty.fill(Gimp.FillType.TRANSPARENT)
    save(image, "gimp-deep-nesting")


def names_and_mixes():
    image = Gimp.Image.new(300, 200, Gimp.ImageBaseType.RGB)
    base = load(image, "backdrop-small.png")
    base.set_name("Hintergrund \u00fc\u00df")
    image.insert_layer(base, None, 0)
    names = ["\u30c6\u30b9\u30c8 \u30ec\u30a4\u30e4\u30fc", "\u0421\u043b\u043e\u0439 \u2116 1", "\u56fe\u5c42 \U0001f600",
             "Ebene & <xml> \"quotes\" 'apos'", "  spaces  ", "tab\tname", "\u0645\u0631\u062d\u0628\u0627"]
    for index, name in enumerate(names):
        layer = load(image, "blob%d.png" % index)
        layer.set_name(name)
        image.insert_layer(layer, None, 0)
        layer.set_offsets(20 + index * 35, 10 + (index % 3) * 50)
        layer.set_opacity([100.0, 0.0, 37.0, 63.0, 100.0, 12.5, 99.0][index])
        layer.set_visible(index % 3 != 2)
    save(image, "gimp-names-and-mixes")


def modes_partial():
    modes = ["NORMAL", "MULTIPLY", "SCREEN", "OVERLAY", "DARKEN_ONLY", "LIGHTEN_ONLY", "DODGE", "BURN",
             "HARDLIGHT", "SOFTLIGHT", "DIFFERENCE", "HSV_HUE", "HSV_SATURATION", "HSL_COLOR", "HSV_VALUE",
             "ADDITION"]
    image = Gimp.Image.new(256, 128, Gimp.ImageBaseType.RGB)
    base = load(image, "backdrop-modes.png")
    base.set_name("Backdrop")
    image.insert_layer(base, None, 0)
    for index, mode in enumerate(modes):
        layer = load(image, "blob%d.png" % (index % 8))
        layer.set_name(mode.lower())
        image.insert_layer(layer, None, 0)
        layer.set_offsets((index % 8) * 32, (index // 8) * 64)
        layer.set_mode(getattr(Gimp.LayerMode, mode))
        layer.set_opacity(70.0)
    save(image, "gimp-modes-70-percent")


big()
deep()
names_and_mixes()
modes_partial()
print("CORPUS-DONE")
'''


def main():
    out = os.path.abspath(sys.argv[1])
    os.makedirs(out, exist_ok=True)
    work = tempfile.mkdtemp(prefix="patchy-ora-corpus-make-")
    staging = os.path.join(work, "out")
    os.makedirs(staging)
    try:
        def write(name, data):
            with open(os.path.join(work, name), "wb") as handle:
                handle.write(data)

        write("backdrop.png", png_bytes(400, 300, lambda x, y: (x / 399, y / 299, 0.5, 1.0)))
        write("backdrop-small.png", png_bytes(256, 192, lambda x, y: (x / 255, 0.4, y / 191, 1.0)))
        write("backdrop-modes.png", png_bytes(256, 128, lambda x, y: (x / 255, 0.3 + 0.4 * y / 127, 1 - x / 255, 1.0)))
        for index in range(8):
            write("blob%d.png" % index, png_bytes(64, 64, blob(index, 64)))
        script = os.path.join(work, "make.py")
        with open(script, "w", encoding="utf-8") as handle:
            handle.write(GIMP_SCRIPT.format(work=work, out=staging))
        subprocess.run([GIMP, "-i", "--batch-interpreter=python-fu-eval",
                        "-b", "exec(open(r'%s', encoding='utf-8').read())" % script,
                        "-b", "Gimp.get_pdb().lookup_procedure('gimp-quit').run(None)"], check=True, timeout=1200)
        for name in sorted(os.listdir(staging)):
            stem, extension = os.path.splitext(name)
            source = os.path.join(staging, name)
            if extension == ".ora":
                folder = os.path.join(out, stem)
                os.makedirs(folder, exist_ok=True)
                shutil.copy(source, folder)
            krita_stem = stem.replace("gimp-", "krita-") + ("-from-psd" if extension == ".psd" else "")
            folder = os.path.join(out, krita_stem)
            os.makedirs(folder, exist_ok=True)
            process = subprocess.Popen([KRITA, source, "--export", "--export-filename",
                                        os.path.join(folder, krita_stem + ".ora")],
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            try:
                process.wait(timeout=600)
            except subprocess.TimeoutExpired:
                process.kill()  # this run's own Krita only
                process.wait()
                print("krita timed out on", name)
    finally:
        if os.path.basename(work).startswith("patchy-ora-corpus-make-"):
            shutil.rmtree(work, ignore_errors=True)
    print("wrote", out)


if __name__ == "__main__":
    main()
