"""Writes Krita .kra fixtures with Krita 5.3 itself (docs/kra.md, "Fixtures").

Every document is built through Krita's Python scripting (kritarunner, no window) from
pixels this script draws and saved as .kra by Krita. Krita's own render of each, the
mergedimage.png it stores in the file, is copied out as <name>.png: the reference the
importer is compared with. Nothing here reads or uses
Krita's source code; Krita runs only as a tool.

Usage (Windows, Krita installed in its default place):
    python scripts/dev/kra/make_kra_fixtures.py [out_dir] [fixture ...]

out_dir defaults to local-test-fixtures/kra; with fixture names only those are rebuilt.
Each fixture <name> writes <name>.kra and <name>.png (Krita's own render). A file layer
fixture also writes the PNG it references beside it.
"""

import os
import shutil
import subprocess
import sys
import tempfile
import zipfile

KRITA_DIR = os.environ.get("PATCHY_KRITA_DIR", r"C:\Program Files\Krita (x64)\bin")
MODULE = "patchy_kra_fixture_maker"


# ---------------------------------------------------------------------------------------
# The part that runs inside Krita (kritarunner imports this file as MODULE).

def _clamp(value):
    return 0.0 if value < 0.0 else 1.0 if value > 1.0 else value


class Painter:
    """Packs float RGBA (0..1, straight alpha) into Krita's native channel order."""

    def __init__(self, model, depth):
        import struct
        self.struct = struct
        self.model = model
        self.depth = depth

    def pack(self, r, g, b, a):
        s = self.struct
        r, g, b, a = _clamp(r), _clamp(g), _clamp(b), _clamp(a)
        if self.model == "RGBA":
            if self.depth == "U8":
                return bytes((round(b * 255), round(g * 255), round(r * 255), round(a * 255)))
            if self.depth == "U16":
                return s.pack("<4H", round(b * 65535), round(g * 65535), round(r * 65535), round(a * 65535))
            if self.depth == "F16":
                return s.pack("<4e", r, g, b, a)
            return s.pack("<4f", r, g, b, a)
        if self.model == "GRAYA":
            y = 0.2126 * r + 0.7152 * g + 0.0722 * b
            if self.depth == "U8":
                return bytes((round(y * 255), round(a * 255)))
            if self.depth == "U16":
                return s.pack("<2H", round(y * 65535), round(a * 65535))
            if self.depth == "F16":
                return s.pack("<2e", y, a)
            return s.pack("<2f", y, a)
        if self.model == "CMYKA":
            k = 1.0 - max(r, g, b)
            c = 0.0 if k >= 1.0 else (1.0 - r - k) / (1.0 - k)
            m = 0.0 if k >= 1.0 else (1.0 - g - k) / (1.0 - k)
            yy = 0.0 if k >= 1.0 else (1.0 - b - k) / (1.0 - k)
            if self.depth == "U8":
                return bytes((round(c * 255), round(m * 255), round(yy * 255), round(k * 255), round(a * 255)))
            return s.pack("<5H", round(c * 65535), round(m * 65535), round(yy * 65535), round(k * 65535),
                          round(a * 65535))
        raise ValueError(self.model)

    def fill(self, node, x, y, w, h, pixel):
        data = bytearray()
        for py in range(h):
            for px in range(w):
                data += self.pack(*pixel(px, py))
        node.setPixelData(bytes(data), x, y, w, h)


def gradient(w, h, alpha=1.0):
    return lambda x, y: (x / max(1, w - 1), y / max(1, h - 1), 0.5 + 0.5 * ((x + y) % 7) / 7.0, alpha)


def disc(w, h, color, soft=True):
    cx, cy, radius = (w - 1) / 2.0, (h - 1) / 2.0, min(w, h) / 2.0

    def pixel(x, y):
        d = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5 / radius
        a = _clamp((1.0 - d) * 4.0) if soft else (1.0 if d <= 1.0 else 0.0)
        return color[0], color[1], color[2], a * (color[3] if len(color) > 3 else 1.0)
    return pixel


def write_png(path, w, h, pixel):
    """An 8-bit RGBA PNG with an sRGB chunk (the file layer's source)."""
    import struct
    import zlib
    rows = bytearray()
    for y in range(h):
        rows.append(0)
        for x in range(w):
            rows += bytes(round(_clamp(v) * 255) for v in pixel(x, y))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    with open(path, "wb") as handle:
        handle.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
                     chunk(b"sRGB", b"\x00") + chunk(b"IDAT", zlib.compress(bytes(rows))) + chunk(b"IEND", b""))


def solid(color):
    return lambda x, y: color


class Builder:
    def __init__(self, out):
        from krita import Krita, InfoObject, Selection
        self.k = Krita.instance()
        self.k.setBatchmode(True)
        self.InfoObject = InfoObject
        self.Selection = Selection
        self.out = out

    def new(self, name, w, h, model="RGBA", depth="U8", profile="", res=72.0, background=True):
        doc = self.k.createDocument(w, h, name, model, depth, profile, res)
        root = doc.rootNode()
        for child in root.childNodes():
            if not background:
                child.remove()
        return doc, root, Painter(model, depth)

    def paint(self, doc, parent, name, above=None):
        node = doc.createNode(name, "paintlayer")
        parent.addChildNode(node, above)
        return node

    def group(self, doc, parent, name, above=None):
        node = doc.createGroupLayer(name)
        parent.addChildNode(node, above)
        return node

    def selection(self, x, y, w, h, value=255):
        selection = self.Selection()
        selection.select(x, y, w, h, value)
        return selection

    def filter(self, name, **properties):
        filt = self.k.filter(name)
        config = filt.configuration()
        for key, value in properties.items():
            config.setProperty(key, value)
        filt.setConfiguration(config)
        return filt

    def settle(self, doc):
        import time
        from PyQt5.QtWidgets import QApplication
        for _ in range(20):
            QApplication.processEvents()
            time.sleep(0.05)
        doc.waitForDone()

    def save(self, doc, name):
        doc.refreshProjection()
        doc.waitForDone()
        path = os.path.join(self.out, name + ".kra")
        doc.setBatchmode(True)
        saved = False
        for _attempt in range(20):
            # Generated (fill) layers and vector layers update asynchronously; Krita refuses to
            # save while an update is still running, so wait and retry.
            self.settle(doc)
            saved = doc.saveAs(path)
            if saved:
                break
        doc.waitForDone()
        doc.close()
        if not saved:
            raise RuntimeError("Krita did not save " + path)


def build_structure(b, name, model="RGBA", depth="U8"):
    doc, root, p = b.new(name, 192, 128, model, depth)
    bottom = root.childNodes()[0]  # Krita's white Background layer (a default pixel, no tiles)
    base = b.paint(doc, root, "Gradient", bottom)
    p.fill(base, 0, 0, 192, 128, gradient(192, 128))
    offset = b.paint(doc, root, "Off canvas", base)
    p.fill(offset, -20, -10, 60, 50, disc(60, 50, (1.0, 0.2, 0.1)))
    half = b.paint(doc, root, "Half opacity", offset)
    p.fill(half, 120, 70, 60, 50, solid((0.1, 0.3, 1.0, 1.0)))
    half.setOpacity(128)
    hidden = b.paint(doc, root, "Hidden", half)
    p.fill(hidden, 0, 0, 192, 128, solid((1.0, 0.0, 1.0, 1.0)))
    hidden.setVisible(False)
    locked = b.paint(doc, root, "Locked", hidden)
    p.fill(locked, 150, 5, 30, 30, solid((1.0, 1.0, 0.0, 1.0)))
    locked.setLocked(True)
    alpha_locked = b.paint(doc, root, "Alpha locked", locked)
    p.fill(alpha_locked, 100, 5, 30, 30, disc(30, 30, (0.0, 1.0, 0.5)))
    alpha_locked.setAlphaLocked(True)
    clip_base = b.paint(doc, root, "Clip base", alpha_locked)
    p.fill(clip_base, 60, 40, 50, 50, disc(50, 50, (0.9, 0.9, 0.9)))
    clipped = b.paint(doc, root, "Inherit alpha", clip_base)
    p.fill(clipped, 50, 50, 70, 20, solid((0.8, 0.0, 0.3, 1.0)))
    clipped.setInheritAlpha(True)
    b.paint(doc, root, "Empty layer", clipped)
    unicode_layer = b.paint(doc, root, "Ünïcødé 層 ✓", root.childNodes()[-1])
    p.fill(unicode_layer, 5, 100, 20, 20, solid((0.0, 0.0, 0.0, 1.0)))

    iso = b.group(doc, root, "Isolated group", unicode_layer)
    iso.setOpacity(200)
    inner = b.paint(doc, iso, "Multiply inside")
    p.fill(inner, 10, 10, 80, 60, solid((0.2, 0.8, 0.8, 1.0)))
    inner.setBlendingMode("multiply")
    inner2 = b.paint(doc, iso, "Second", inner)
    p.fill(inner2, 30, 30, 40, 40, disc(40, 40, (1.0, 0.5, 0.0)))

    through = b.group(doc, root, "Pass-through group", iso)
    through.setPassThroughMode(True)
    screen = b.paint(doc, through, "Screen inside")
    p.fill(screen, 100, 60, 80, 60, solid((0.0, 0.4, 0.9, 1.0)))
    screen.setBlendingMode("screen")
    nested = b.group(doc, through, "Nested", screen)
    nested2 = b.group(doc, nested, "Nested deeper")
    deep = b.paint(doc, nested2, "Deep layer")
    p.fill(deep, 140, 90, 30, 30, solid((0.5, 0.0, 0.5, 0.7)))
    nested.setCollapsed(True)
    b.save(doc, name)


def build_modes(b, name):
    """One tile per Krita blend mode. Each tile is its own isolated group holding a backdrop
    and the mode layer, so modes that erase the backdrop (erase, in, out, destination-in)
    only clear their own tile."""
    import json
    modes = json.loads(os.environ["PATCHY_KRA_MODES"])
    tile, columns = 24, 16
    rows = (len(modes) + columns - 1) // columns
    w, h = tile * columns, tile * rows
    doc, root, p = b.new(name, w, h, background=False)
    above = None
    for index, mode in enumerate(modes):
        x0, y0 = (index % columns) * tile, (index // columns) * tile
        group = b.group(doc, root, mode, above)
        back = b.paint(doc, group, "Backdrop")
        p.fill(back, x0, y0, tile, tile,
               lambda x, y: (x / (tile - 1), y / (tile - 1), (index * 37 % 256) / 255.0,
                             1.0 if y < tile - 4 else 0.5))
        node = b.paint(doc, group, mode, back)
        p.fill(node, x0, y0, tile, tile,
               lambda x, y: (1.0 - y / (tile - 1), (x * 11 % tile) / (tile - 1), x / (tile - 1),
                             1.0 if x < tile - 6 else 0.6))
        node.setBlendingMode(mode)
        above = group
    b.save(doc, name)


def build_masks(b, name):
    doc, root, p = b.new(name, 160, 120)
    bottom = root.childNodes()[0]
    base = b.paint(doc, root, "Masked", bottom)
    p.fill(base, 0, 0, 160, 120, gradient(160, 120))
    mask = doc.createTransparencyMask("Transparency mask")
    mask.setSelection(b.selection(20, 10, 100, 80, 255))
    base.addChildNode(mask, None)
    soft = b.paint(doc, root, "Two masks", base)
    p.fill(soft, 80, 40, 80, 80, solid((1.0, 0.3, 0.0, 1.0)))
    m1 = doc.createTransparencyMask("Mask one")
    m1.setSelection(b.selection(80, 40, 60, 80, 200))
    soft.addChildNode(m1, None)
    m2 = doc.createTransparencyMask("Mask two")
    m2.setSelection(b.selection(100, 40, 60, 60, 128))
    soft.addChildNode(m2, m1)
    grp = b.group(doc, root, "Masked group", soft)
    gl = b.paint(doc, grp, "In group")
    p.fill(gl, 0, 80, 100, 40, solid((0.0, 0.6, 0.2, 1.0)))
    gm = doc.createTransparencyMask("Group mask")
    gm.setSelection(b.selection(0, 90, 70, 30, 255))
    grp.addChildNode(gm, None)
    inv = b.paint(doc, root, "Filter masked", grp)
    p.fill(inv, 10, 10, 50, 50, gradient(50, 50))
    fm = doc.createFilterMask("Invert mask", b.filter("invert"), b.selection(10, 10, 25, 50))
    inv.addChildNode(fm, None)
    sel = b.paint(doc, root, "With selection mask", inv)
    p.fill(sel, 120, 0, 40, 30, solid((0.3, 0.3, 0.3, 1.0)))
    sm = doc.createSelectionMask("Selection mask")
    sm.setSelection(b.selection(120, 0, 20, 20))
    sel.addChildNode(sm, None)
    b.save(doc, name)


FILTERS = [
    ("invert", {}),
    ("desaturate", {}),
    ("hsvadjustment", {"h": 60, "s": 20, "v": 0}),
    ("levels", {}),
    ("colorbalance", {}),
    ("perchannel", {}),
    ("threshold", {}),
    ("posterize", {}),
    ("gaussian blur", {"horizRadius": 3, "vertRadius": 3}),
    ("sharpen", {}),
    ("blur", {}),
    ("unsharp", {}),
    ("pixelize", {}),
    ("emboss", {}),
    ("noise", {}),
    ("autocontrast", {}),
    ("gradientmap", {}),
]


def build_filters(b, name, as_masks=False):
    tile = 40
    columns = 4
    rows = (len(FILTERS) + columns - 1) // columns
    w, h = tile * columns, tile * rows
    doc, root, p = b.new(name, w, h)
    bottom = root.childNodes()[0]
    above = bottom
    for index, (filter_name, properties) in enumerate(FILTERS):
        x0, y0 = (index % columns) * tile, (index // columns) * tile
        node = b.paint(doc, root, "Tile " + filter_name, above)
        p.fill(node, x0, y0, tile, tile, lambda x, y: (x / (tile - 1), y / (tile - 1), ((x * y) % 13) / 12.0, 1.0))
        above = node
        try:
            filt = b.filter(filter_name, **properties)
        except Exception:
            continue
        if as_masks:
            mask = doc.createFilterMask(filter_name, filt, node)
            node.addChildNode(mask, None)
        else:
            layer = doc.createFilterLayer(filter_name, filt, b.selection(x0, y0, tile, tile))
            root.addChildNode(layer, above)
            above = layer
    b.save(doc, name)


# Filter layers with settings that change the picture, one tile each, so each filter's mapping
# can be measured on its own (the defaults in FILTERS mostly do nothing).
CURVE = "0,0;0.5,0.85;1,1;"
FILTER_SETTINGS = (
    [("levels", {"lightness": "0.1;0.85;1.6;0.05;0.95"}),
     ("levels", {"blackvalue": 25, "whitevalue": 220, "gammavalue": 1.6, "outblackvalue": 12, "outwhitevalue": 240,
                 "lightness": "0.1;0.85;1.6;0.05;0.95", "mode": "channels"})] +
    [("perchannel", dict({"nTransfers": 8}, **{"curve%d" % i: (CURVE if i == k else "0,0;1,1;") for i in range(8)}))
     for k in range(8)] +
    [("hsvadjustment", {"h": 60, "s": 30, "v": -10, "type": t}) for t in range(5)] +
    [("hsvadjustment", {"h": 200, "s": 50, "v": 10, "colorize": True})] +
    [("colorbalance", {"cyan_red_midtones": 50, "magenta_blue_midtones": -30, "yellow_green_midtones": 40}),
     ("colorbalance", {"cyan_red_shadows": -40, "magenta_blue_shadows": 30, "yellow_green_shadows": 20,
                       "cyan_red_highlights": 30, "magenta_blue_highlights": 20, "yellow_green_highlights": -40})] +
    [("desaturate", {"type": t}) for t in range(6)] +
    [("threshold", {"threshold": 100}), ("posterize", {"steps": 4}), ("invert", {})]
)


def build_filter_settings(b, name):
    tile, columns = 32, 6
    rows = (len(FILTER_SETTINGS) + columns - 1) // columns
    w, h = tile * columns, tile * rows
    doc, root, p = b.new(name, w, h)
    back = b.paint(doc, root, "Backdrop", root.childNodes()[0])
    p.fill(back, 0, 0, w, h, lambda x, y: (((x % tile) / (tile - 1)), ((y % tile) / (tile - 1)),
                                           0.5 + 0.5 * (((x + y) // 3) % 2) * ((x % tile) / (tile - 1)), 1.0))
    above = back
    for index, (filter_name, properties) in enumerate(FILTER_SETTINGS):
        x0, y0 = (index % columns) * tile, (index // columns) * tile
        layer = doc.createFilterLayer("%02d %s" % (index, filter_name), b.filter(filter_name, **properties),
                                      b.selection(x0, y0, tile, tile))
        root.addChildNode(layer, above)
        above = layer
    b.save(doc, name)


def build_curves(b, name):
    """Per-channel curves, one curve set per tile. Krita's scripting does not take curve
    settings, so the curves are written into the saved file's filter settings, and Krita
    opens and saves that file again: its own render of the curves is the reference."""
    import re
    import zipfile
    tile, count = 32, 8
    doc, root, p = b.new(name, tile * count, tile)
    back = b.paint(doc, root, "Backdrop", root.childNodes()[0])
    p.fill(back, 0, 0, tile * count, tile, lambda x, y: ((x % tile) / (tile - 1), (y % tile) / (tile - 1),
                                                       1.0 - (x % tile) / (tile - 1), 1.0))
    above = back
    for k in range(count):
        layer = doc.createFilterLayer("curve %d" % k, b.filter("perchannel"), b.selection(k * tile, 0, tile, tile))
        root.addChildNode(layer, above)
        above = layer
    b.save(doc, name)
    path = os.path.join(b.out, name + ".kra")
    with zipfile.ZipFile(path) as archive:
        entries = [(info, archive.read(info.filename)) for info in archive.infolist()]
    maindoc = dict((info.filename, data) for info, data in entries)["maindoc.xml"].decode("utf-8")
    files = {}
    for k in range(count):
        found = re.search(r'name="curve %d"[^>]*?filename="([^"]+)"' % k, maindoc) or             re.search(r'filename="([^"]+)"[^>]*?name="curve %d"' % k, maindoc)
        files[found.group(1)] = k
    patched = path + ".tmp"
    with zipfile.ZipFile(patched, "w") as archive:
        for info, data in entries:
            leaf = info.filename.rsplit("/", 1)[-1]
            if leaf.endswith(".filterconfig") and leaf[:-len(".filterconfig")] in files:
                k = files[leaf[:-len(".filterconfig")]]
                curves = "".join('<param name="curve%d">%s</param>' % (i, CURVE if i == k else "0,0;1,1;")
                                 for i in range(8))
                data = ('<!DOCTYPE params><params version="1"><param name="nTransfers">8</param>%s</params>'
                        % curves).encode("utf-8")
            archive.writestr(info, data)
    os.replace(patched, path)
    reopened = b.k.openDocument(path)
    if reopened is None:
        raise RuntimeError("Krita could not reopen " + path)
    b.save(reopened, name)


def build_mode_table(b, name, mode):
    """A blend mode's whole function: the layer's gray level runs along x and the backdrop's
    along y, opaque in the top half and at half alpha in the bottom half. Used to measure a
    Krita mode's formula (docs/kra.md "Blend modes")."""
    doc, root, p = b.new(name, 256, 512, background=False)
    back = b.paint(doc, root, "Backdrop")
    p.fill(back, 0, 0, 256, 512, lambda x, y: ((y % 256) / 255.0,) * 3 + (1.0,))
    layer = b.paint(doc, root, mode, back)
    p.fill(layer, 0, 0, 256, 512, lambda x, y: (x / 255.0,) * 3 + (1.0 if y < 256 else 128 / 255.0,))
    layer.setBlendingMode(mode)
    b.save(doc, name)


def build_channels(b, name):
    """Channel flags: a layer with red disabled, one with blue disabled, one alpha-locked."""
    doc, root, p = b.new(name, 96, 32)
    back = b.paint(doc, root, "Backdrop", root.childNodes()[0])
    p.fill(back, 0, 0, 96, 32, gradient(96, 32))
    above = back
    for index, (label, disabled) in enumerate([("No red", 2), ("No blue", 0), ("No green", 1)]):
        layer = b.paint(doc, root, label, above)
        p.fill(layer, index * 32, 0, 32, 32, solid((0.9, 0.6, 0.3, 1.0)))
        channels = layer.channels()
        # Krita lists the channels in display order (red, green, blue, alpha) here.
        names = [c.name() for c in channels]
        target = {"No red": "Red", "No green": "Green", "No blue": "Blue"}[label]
        for c in channels:
            if c.name() == target:
                c.setVisible(False)
        above = layer
    b.save(doc, name)


def build_fills(b, name):
    doc, root, p = b.new(name, 200, 120)
    bottom = root.childNodes()[0]
    above = bottom
    generators = [
        ("color", {"color": '<!DOCTYPE color><color><RGB r="0.8" g="0.2" b="0.1" space="sRGB-elle-V2-srgbtrc.icc"/></color>'}),
        ("pattern", {}),
        ("gradient", {}),
        ("screentone", {}),
        ("multigrid", {}),
        ("simplexnoise", {}),
    ]
    for index, (gen, properties) in enumerate(generators):
        info = b.InfoObject()
        for key, value in properties.items():
            info.setProperty(key, value)
        x0 = (index % 3) * 66
        y0 = (index // 3) * 60
        try:
            layer = doc.createFillLayer("Fill " + gen, gen, info, b.selection(x0, y0, 64, 58))
        except Exception:
            continue
        if layer is None:
            continue
        root.addChildNode(layer, above)
        above = layer
    b.save(doc, name)


def build_clone_file(b, name):
    doc, root, p = b.new(name, 160, 100)
    bottom = root.childNodes()[0]
    src = b.paint(doc, root, "Source", bottom)
    p.fill(src, 10, 10, 50, 40, disc(50, 40, (0.1, 0.5, 1.0)))
    clone = doc.createCloneLayer("Clone of source", src)
    root.addChildNode(clone, src)
    clone.move(80, 40)
    write_png(os.path.join(b.out, name + "-ref.png"), 40, 30, gradient(40, 30))
    # An absolute path: Krita stores it relative to the saved .kra, so the file sits beside it.
    file_layer = doc.createFileLayer("File layer", os.path.join(b.out, name + "-ref.png"), "None", "Bicubic")
    root.addChildNode(file_layer, clone)
    file_layer.move(100, 5)
    b.save(doc, name)


SVG_SHAPES = """<svg xmlns="http://www.w3.org/2000/svg" width="{w}pt" height="{h}pt" viewBox="0 0 {w} {h}">
<defs><linearGradient id="g1" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#ff0000"/>
<stop offset="1" stop-color="#0000ff"/></linearGradient></defs>
<rect x="10" y="10" width="60" height="40" fill="#20c040" stroke="#003080" stroke-width="3"/>
<ellipse cx="110" cy="35" rx="30" ry="20" fill="url(#g1)"/>
<path d="M 160 10 L 190 50 L 140 50 Z" fill="#ffcc00" fill-opacity="0.7" stroke="#000000" stroke-width="1.5"/>
<path d="M 10 80 C 40 60 70 110 100 80" fill="none" stroke="#c00060" stroke-width="4" stroke-linecap="round"/>
<rect x="120" y="70" width="60" height="30" rx="8" ry="8" fill="#8040ff" opacity="0.5"/>
</svg>"""

SVG_TEXT = """<svg xmlns="http://www.w3.org/2000/svg" width="{w}pt" height="{h}pt" viewBox="0 0 {w} {h}">
<text x="10" y="30" font-family="Arial" font-size="20" fill="#202020">Hello Krita</text>
<text x="10" y="60" font-family="Times New Roman" font-size="16" font-weight="bold" fill="#c03000">Bold serif</text>
<text x="10" y="85" font-family="Arial" font-size="14" font-style="italic" fill="#0040c0">Italic sans</text>
<text x="190" y="110" font-family="Arial" font-size="12" text-anchor="end" fill="#008040">Right aligned</text>
</svg>"""


def build_vector(b, name):
    w, h = 200, 120
    doc, root, p = b.new(name, w, h)
    bottom = root.childNodes()[0]
    shapes = doc.createVectorLayer("Shapes")
    root.addChildNode(shapes, bottom)
    shapes.addShapesFromSvg(SVG_SHAPES.format(w=w, h=h))
    text = doc.createVectorLayer("Text")
    root.addChildNode(text, shapes)
    text.addShapesFromSvg(SVG_TEXT.format(w=w, h=h))
    faded = doc.createVectorLayer("Faded shape")
    root.addChildNode(faded, text)
    faded.addShapesFromSvg('<svg xmlns="http://www.w3.org/2000/svg" width="{w}pt" height="{h}pt" viewBox="0 0 {w} {h}">'
                           '<circle cx="150" cy="90" r="25" fill="#ff00ff"/></svg>'.format(w=w, h=h))
    faded.setOpacity(128)
    faded.setBlendingMode("multiply")
    b.save(doc, name)


def build_document(b, name):
    doc, root, p = b.new(name, 300, 200, res=150.0)
    bottom = root.childNodes()[0]
    layer = b.paint(doc, root, "Content", bottom)
    p.fill(layer, 0, 0, 300, 200, gradient(300, 200))
    doc.setHorizontalGuides([25.0, 100.5])
    doc.setVerticalGuides([40.0, 250.0])
    doc.setGuidesVisible(True)
    info = doc.documentInfo()
    info = info.replace("<title>" + name + "</title>", "<title>Patchy KRA fixture</title>")
    info = info.replace("<full-name></full-name>", "<full-name>Patchy Tests</full-name>")
    info = info.replace("<description></description>", "<description>Guides, 150 ppi and metadata</description>")
    doc.setDocumentInfo(info)
    b.save(doc, name)


def build_profile(b, name, profile):
    doc, root, p = b.new(name, 128, 96, profile=profile)
    bottom = root.childNodes()[0]
    layer = b.paint(doc, root, "Saturated", bottom)
    p.fill(layer, 0, 0, 128, 96,
           lambda x, y: (1.0 if x < 43 else 0.0, 1.0 if 43 <= x < 86 else 0.0, 1.0 if x >= 86 else 0.0,
                         1.0 - y / 191.0))
    b.save(doc, name)


def build_large(b, name):
    import random
    rng = random.Random(4242)
    w, h = 3000, 2000
    doc, root, p = b.new(name, w, h)
    above = root.childNodes()[0]
    for index in range(24):
        layer = b.paint(doc, root, "Layer %d" % (index + 1), above)
        lw, lh = rng.randint(100, 900), rng.randint(100, 700)
        x0, y0 = rng.randint(-200, w - 100), rng.randint(-200, h - 100)
        color = (rng.random(), rng.random(), rng.random(), 0.6 + 0.4 * rng.random())
        p.fill(layer, x0, y0, lw, lh, disc(lw, lh, color) if index % 2 else solid(color))
        layer.setOpacity(rng.randint(120, 255))
        if index % 5 == 3:
            layer.setBlendingMode(["multiply", "screen", "overlay", "add"][index % 4])
        above = layer
    b.save(doc, name)


# Patchy's own PSD regression files (test-fixtures/psd) that Krita opens with their layer
# styles and shapes, saved by Krita as .kra: Krita's layer styles (an ASL inside the .kra)
# and vector layers as Krita itself writes them. (Krita opens Photoshop adjustment layers
# as plain paint layers, so those files are not used.)
PSD_SOURCES = [
    "photoshop-global-light-shadow", "photoshop-inner-shadow", "photoshop-outer-glow", "photoshop-inner-glow",
    "photoshop-emboss-styles", "photoshop-satin-default", "photoshop-stroke-positions",
    "photoshop-gradient-overlay-geometry", "photoshop-pattern-overlay", "photoshop-shape-solid",
    "photoshop-interior-exterior-blending", "photoshop-outer-glow-range", "photoshop-inner-glow-range",
    "photoshop-pillow-emboss", "photoshop-stroke-shapeburst", "photoshop-layer-style-4a-roundtrip",
]


def build_from_psd(b, name, stem):
    repo = os.environ["PATCHY_KRA_REPO"]
    doc = b.k.openDocument(os.path.join(repo, "test-fixtures", "psd", stem + ".psd"))
    if doc is None:
        raise RuntimeError("Krita could not open " + stem)
    b.save(doc, name)


FIXTURES = {
    "structure-u8": lambda b: build_structure(b, "structure-u8"),
    "structure-u16": lambda b: build_structure(b, "structure-u16", depth="U16"),
    "structure-f16": lambda b: build_structure(b, "structure-f16", depth="F16"),
    "structure-f32": lambda b: build_structure(b, "structure-f32", depth="F32"),
    "gray-u8": lambda b: build_structure(b, "gray-u8", model="GRAYA", depth="U8"),
    "gray-u16": lambda b: build_structure(b, "gray-u16", model="GRAYA", depth="U16"),
    "cmyk-u8": lambda b: build_structure(b, "cmyk-u8", model="CMYKA", depth="U8"),
    "modes": lambda b: build_modes(b, "modes"),
    "masks": lambda b: build_masks(b, "masks"),
    "filter-layers": lambda b: build_filters(b, "filter-layers"),
    "filter-masks": lambda b: build_filters(b, "filter-masks", as_masks=True),
    "fill-layers": lambda b: build_fills(b, "fill-layers"),
    "clone-file": lambda b: build_clone_file(b, "clone-file"),
    "vector": lambda b: build_vector(b, "vector"),
    "document": lambda b: build_document(b, "document"),
    "profile-clay": lambda b: build_profile(b, "profile-clay", "ClayRGB-elle-V2-g22.icc"),
    "large": lambda b: build_large(b, "large"),
    "filter-settings": lambda b: build_filter_settings(b, "filter-settings"),
    "channels": lambda b: build_channels(b, "channels"),
    "curves": lambda b: build_curves(b, "curves"),
}
# Mode tables are analysis data, not test fixtures: build them into another folder.
TABLE_MODES = ["soft_light_svg", "soft_light", "luminosity_sai", "additive_subtractive", "arc_tangent",
               "hard overlay", "geometric_mean", "allanon", "parallel", "interpolation", "easy dodge", "easy burn",
               "gamma_light", "gamma_dark", "flat_light", "reflect", "glow", "negation", "xor", "freeze", "heat",
               "grain_merge", "grain_extract", "behind", "erase", "overlay", "color", "luminize", "hue", "saturation"]
for _mode in TABLE_MODES:
    FIXTURES["table-" + _mode.replace(" ", "_")] = (lambda mode: lambda b: build_mode_table(
        b, "table-" + mode.replace(" ", "_"), mode))(_mode)
for _stem in PSD_SOURCES:
    FIXTURES["psd-" + _stem.replace("photoshop-", "")] = (lambda stem: lambda b: build_from_psd(
        b, "psd-" + stem.replace("photoshop-", ""), stem))(_stem)

# Every blend mode id Krita 5.3 accepts (found by rendering each candidate against the
# fallback it uses for unknown ids; see docs/kra.md "Blend modes").
MODES = [
    "normal", "erase", "in", "out", "destination-in", "destination-atop", "xor", "or", "and", "nand", "nor",
    "xnor", "implication", "not_implication", "converse", "not_converse", "add", "subtract", "inverse_subtract",
    "diff", "multiply", "divide", "arc_tangent", "geometric_mean", "additive_subtractive", "negation", "modulo",
    "modulo_continuous", "divisive_modulo", "divisive_modulo_continuous", "modulo_shift",
    "modulo_shift_continuous", "equivalence", "allanon", "parallel", "grain_merge", "grain_extract", "exclusion",
    "hard mix", "hard_mix_photoshop", "hard_mix_softer_photoshop", "overlay", "behind", "greater",
    "hard overlay", "interpolation", "interpolation 2x", "penumbra a", "penumbra b", "penumbra c", "penumbra d",
    "darken", "burn", "linear_burn", "gamma_dark", "shade_ifs_illusions", "fog_darken_ifs_illusions",
    "easy burn", "lighten", "dodge", "linear_dodge", "screen", "hard_light", "soft_light_ifs_illusions",
    "soft_light_pegtop_delphi", "soft_light", "soft_light_svg", "gamma_light", "gamma_illumination",
    "vivid_light", "flat_light", "linear light", "pin_light", "pnorm_a", "pnorm_b", "super_light",
    "tint_ifs_illusions", "fog_lighten_ifs_illusions", "easy dodge", "luminosity_sai", "reflect", "glow",
    "freeze", "heat", "glow_heat", "heat_glow", "reflect_freeze", "freeze_reflect",
    "heat_glow_freeze_reflect_hybrid", "bumpmap", "combine_normal", "dissolve", "copy_red", "copy_green",
    "copy_blue", "copy", "tangent_normalmap", "color", "hue", "saturation", "luminize", "intensity", "value",
    "lightness", "inc_saturation", "dec_saturation", "inc_luminosity", "dec_luminosity", "color_hsi", "hue_hsi",
    "saturation_hsi", "inc_intensity", "dec_intensity", "color_hsl", "hue_hsl", "saturation_hsl",
    "inc_lightness", "dec_lightness", "color_hsv", "hue_hsv", "saturation_hsv", "inc_value", "dec_value",
    "darker color", "lighter color", "lambert_lighting",
]


def krita_main(*_args):
    import traceback
    out = os.environ["PATCHY_KRA_OUT"]
    names = os.environ.get("PATCHY_KRA_ONLY", "").split(",")
    log = open(os.path.join(out, "make-log.txt"), "a", encoding="utf-8")
    builder = Builder(out)
    for name, build in FIXTURES.items():
        if names != [""] and name not in names:
            continue
        try:
            build(builder)
            log.write("ok " + name + "\n")
        except Exception:
            log.write("FAILED " + name + "\n" + traceback.format_exc() + "\n")
        log.flush()
    log.close()


# ---------------------------------------------------------------------------------------
# The driver (plain Python).

def main():
    import json
    repo = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
    out = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(repo, "local-test-fixtures", "kra")
    only = sys.argv[2:]
    unknown = [name for name in only if name not in FIXTURES]
    if unknown:
        sys.exit("unknown fixtures: " + ", ".join(unknown))
    os.makedirs(out, exist_ok=True)
    work = tempfile.mkdtemp(prefix="patchy-kra-fixtures-")
    try:
        shutil.copy(__file__, os.path.join(work, MODULE + ".py"))
        env = dict(os.environ, PATCHY_KRA_OUT=out, PATCHY_KRA_REPO=repo, PATCHY_KRA_ONLY=",".join(only), PATCHY_KRA_MODES=json.dumps(MODES))
        log = os.path.join(out, "make-log.txt")
        if os.path.exists(log):
            os.remove(log)
        subprocess.run([os.path.join(KRITA_DIR, "kritarunner.com"), "-s", MODULE, "-f", "krita_main"], cwd=work,
                       env=env, check=True, timeout=1800, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        with open(log, encoding="utf-8") as handle:
            print(handle.read())
        # The reference render is Krita's own mergedimage.png, which it renders when it saves.
        # Its command-line export is not used: it does not wait for fill and text layers to
        # finish rendering (they come out blank), and it waits on a question for file layers.
        for name in FIXTURES:
            if only and name not in only:
                continue
            kra = os.path.join(out, name + ".kra")
            if os.path.exists(kra):
                with zipfile.ZipFile(kra) as archive, open(os.path.join(out, name + ".png"), "wb") as png:
                    png.write(archive.read("mergedimage.png"))
    finally:
        if os.path.basename(work).startswith("patchy-kra-fixtures-"):
            shutil.rmtree(work, ignore_errors=True)
    print("wrote fixtures to", out)


if __name__ == "__main__":
    main()
