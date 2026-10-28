"""Writes GIMP .xcf fixtures with GIMP 3 and GIMP's own renders of them (docs/xcf.md).

The files are made by GIMP itself from layer PNGs this script draws, so the reader is
tested against what GIMP writes. Nothing here reads GIMP's source: GIMP runs only through
its batch Python console.

Usage (Windows, GIMP 3 installed in its default per-user place):
    python -I scripts/dev/xcf/make_xcf_fixtures.py [out_dir] [--only NAME,NAME]

out_dir defaults to local-test-fixtures/xcf. --only makes just the named sets (the names
after want() at the end of GIMP_SCRIPT, for example "edits") and updates their manifest
lines. Every NAME.xcf gets NAME.png beside it:
GIMP's merge of the visible layers clipped to the canvas, with alpha (16-bit PNG for
documents above 8 bits). manifest.txt lists what each file covers.
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

TILE = 32
COLUMNS = 8
DEFAULT_MODES = [
    "NORMAL", "DISSOLVE", "BEHIND", "MULTIPLY", "SCREEN", "OVERLAY", "DIFFERENCE", "ADDITION", "SUBTRACT",
    "DARKEN_ONLY", "LIGHTEN_ONLY", "HSV_HUE", "HSV_SATURATION", "HSL_COLOR", "HSV_VALUE", "DIVIDE", "DODGE",
    "BURN", "HARDLIGHT", "SOFTLIGHT", "GRAIN_EXTRACT", "GRAIN_MERGE", "VIVID_LIGHT", "PIN_LIGHT",
    "LINEAR_LIGHT", "HARD_MIX", "EXCLUSION", "LINEAR_BURN", "LUMA_DARKEN_ONLY", "LUMA_LIGHTEN_ONLY",
    "LUMINANCE", "COLOR_ERASE", "ERASE", "MERGE", "SPLIT", "LCH_HUE", "LCH_CHROMA", "LCH_COLOR",
    "LCH_LIGHTNESS",
]
LEGACY_MODES = [
    "NORMAL_LEGACY", "BEHIND_LEGACY", "MULTIPLY_LEGACY", "SCREEN_LEGACY", "OVERLAY_LEGACY", "DIFFERENCE_LEGACY",
    "ADDITION_LEGACY", "SUBTRACT_LEGACY", "DARKEN_ONLY_LEGACY", "LIGHTEN_ONLY_LEGACY", "HSV_HUE_LEGACY",
    "HSV_SATURATION_LEGACY", "HSL_COLOR_LEGACY", "HSV_VALUE_LEGACY", "DIVIDE_LEGACY", "DODGE_LEGACY",
    "BURN_LEGACY", "HARDLIGHT_LEGACY", "SOFTLIGHT_LEGACY", "GRAIN_EXTRACT_LEGACY", "GRAIN_MERGE_LEGACY",
    "COLOR_ERASE_LEGACY",
]
PRECISIONS = [
    "U8_LINEAR", "U8_NON_LINEAR", "U8_PERCEPTUAL", "U16_LINEAR", "U16_NON_LINEAR", "U32_LINEAR",
    "U32_NON_LINEAR", "HALF_LINEAR", "HALF_NON_LINEAR", "FLOAT_LINEAR", "FLOAT_NON_LINEAR", "DOUBLE_LINEAR",
]


def png_bytes(width, height, pixel, depth=8, channels=4):
    """PNG (RGBA or gray); pixel(x, y) returns `channels` floats in 0..1."""
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            for value in pixel(x, y)[:channels]:
                value = min(max(value, 0.0), 1.0)
                if depth == 16:
                    rows += struct.pack(">H", round(value * 65535))
                else:
                    rows.append(round(value * 255))

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    color_type = {1: 0, 2: 4, 3: 2, 4: 6}[channels]
    header = struct.pack(">IIBBBBB", width, height, depth, color_type, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(rows), 9)) +
            chunk(b"IEND", b""))


def backdrop(width, height, alpha=None):
    def pixel(x, y):
        a = 1.0 if alpha is None else alpha(x, y)
        return (x / (width - 1), 0.25 + 0.5 * y / (height - 1), 1.0 - x / (width - 1), a)
    return pixel


def tile(x, y):
    # A ramp with a soft alpha edge, so both color and coverage vary.
    edge = min(x, y, TILE - 1 - x, TILE - 1 - y)
    alpha = min(1.0, 0.35 + edge / 8.0)
    return (0.9 - 0.8 * y / (TILE - 1), 0.2 + 0.6 * x / (TILE - 1), 0.5 + 0.4 * (y / (TILE - 1) - 0.5), alpha)


def mask_ramp(x, y):
    return (x / 63.0,)


GIMP_SCRIPT = r'''
import gi
gi.require_version('Gimp', '3.0')
gi.require_version('Gegl', '0.4')
from gi.repository import Gimp, Gio, Gegl

WORK = {work!r}
OUT = {out!r}
DEFAULT_MODES = {default_modes!r}
LEGACY_MODES = {legacy_modes!r}
PRECISIONS = {precisions!r}
ONLY = {only!r}
TILE = {tile}
COLUMNS = {columns}
manifest = []


def path(folder, name):
    return Gio.File.new_for_path(folder + "/" + name)


def load(image, name):
    return Gimp.file_load_layer(Gimp.RunMode.NONINTERACTIVE, image, path(WORK, name))


def save(image, name, note):
    ok = Gimp.file_save(Gimp.RunMode.NONINTERACTIVE, image, path(OUT, name), None)
    if not ok:
        raise RuntimeError("could not save " + name)
    render = image.duplicate()
    for layer in render.get_layers():
        pass
    merged = render.merge_visible_layers(Gimp.MergeType.CLIP_TO_IMAGE)
    stem = name.split(".")[0]
    if render.get_base_type() == Gimp.ImageBaseType.INDEXED:
        render.convert_rgb()
    if not Gimp.file_save(Gimp.RunMode.NONINTERACTIVE, render, path(OUT, stem + ".png"), None):
        raise RuntimeError("could not export " + stem + ".png")
    render.delete()
    manifest.append(name + ": " + note)


def mode_tiles(image, modes, tile_name):
    for index, mode in enumerate(modes):
        layer = load(image, tile_name)
        layer.set_name(mode.lower())
        image.insert_layer(layer, None, 0)
        layer.set_offsets((index % COLUMNS) * TILE, (index // COLUMNS) * TILE)
        layer.set_mode(getattr(Gimp.LayerMode, mode))


def modes_file(name, modes, backdrop_name, note, precision=Gimp.Precision.U8_NON_LINEAR):
    rows = (len(modes) + COLUMNS - 1) // COLUMNS
    image = Gimp.Image.new_with_precision(TILE * COLUMNS, TILE * rows, Gimp.ImageBaseType.RGB, precision)
    base = load(image, backdrop_name)
    base.set_name("Backdrop")
    image.insert_layer(base, None, 0)
    mode_tiles(image, modes, "tile.png")
    save(image, name, note)
    image.delete()


def structure_file(name, precision, note, base_type=Gimp.ImageBaseType.RGB):
    image = Gimp.Image.new_with_precision(160, 120, Gimp.ImageBaseType.RGB, precision)
    base = load(image, "backdrop160.png")
    base.set_name("Background")
    image.insert_layer(base, None, 0)
    base.flatten() if False else None
    outer = Gimp.GroupLayer.new(image, "Outer group")
    image.insert_layer(outer, None, 0)
    inner = Gimp.GroupLayer.new(image, "Inner pass-through")
    image.insert_layer(inner, outer, 0)
    inner.set_mode(Gimp.LayerMode.PASS_THROUGH)
    a = load(image, "tile.png")
    a.set_name("Offset multiply")
    image.insert_layer(a, inner, 0)
    a.set_offsets(20, 30)
    a.set_mode(Gimp.LayerMode.MULTIPLY)
    b = load(image, "tile.png")
    b.set_name("Half opacity")
    image.insert_layer(b, outer, 0)
    b.set_offsets(70, 10)
    b.set_opacity(50.0)
    m = load(image, "big-tile.png")
    m.set_name("Masked")
    image.insert_layer(m, None, 0)
    m.set_offsets(96, 8)
    mask = m.create_mask(Gimp.AddMaskType.WHITE)
    m.add_mask(mask)
    ramp = load(image, "mask-ramp.png")
    image.insert_layer(ramp, None, 0)
    Gimp.edit_copy([ramp])
    image.remove_layer(ramp)
    pasted = Gimp.edit_paste(mask, False)
    Gimp.floating_sel_anchor(pasted[0])
    d = load(image, "big-tile.png")
    d.set_name("Mask off")
    image.insert_layer(d, None, 0)
    d.set_offsets(96, 60)
    dm = d.create_mask(Gimp.AddMaskType.BLACK)
    d.add_mask(dm)
    d.set_apply_mask(False)
    c = load(image, "tile.png")
    c.set_name("Hidden layer")
    image.insert_layer(c, None, 0)
    c.set_offsets(100, 60)
    c.set_visible(False)
    e = load(image, "tile.png")
    e.set_name("レイヤー café \U0001f3a8")
    image.insert_layer(e, None, 0)
    e.set_offsets(-10, 80)
    e.set_lock_alpha(True)
    e.set_lock_position(True)
    e.set_color_tag(Gimp.ColorTag.RED)
    outer.set_opacity(80.0)
    empty = Gimp.Layer.new(image, "Empty layer", 160, 120, Gimp.ImageType.RGBA_IMAGE, 100.0, Gimp.LayerMode.NORMAL)
    image.insert_layer(empty, None, 0)
    empty.fill(Gimp.FillType.TRANSPARENT)
    empty.set_lock_content(True)
    if base_type == Gimp.ImageBaseType.GRAY:
        image.convert_grayscale()
    save(image, name, note)
    image.delete()


def group_file():
    image = Gimp.Image.new(160, 120, Gimp.ImageBaseType.RGB)
    base = load(image, "backdrop160.png")
    image.insert_layer(base, None, 0)
    group = Gimp.GroupLayer.new(image, "Masked group")
    image.insert_layer(group, None, 0)
    for i, mode in enumerate(["NORMAL", "SCREEN", "MULTIPLY"]):
        layer = load(image, "big-tile.png")
        layer.set_name("child " + mode.lower())
        image.insert_layer(layer, group, 0)
        layer.set_offsets(10 + 40 * i, 20 + 10 * i)
        layer.set_mode(getattr(Gimp.LayerMode, mode))
    mask = group.create_mask(Gimp.AddMaskType.WHITE)
    group.add_mask(mask)
    Gimp.Selection.none(image)
    image.select_ellipse(Gimp.ChannelOps.REPLACE, 30, 10, 100, 90)
    Gimp.Selection.invert(image)
    mask.edit_fill(Gimp.FillType.BACKGROUND) if False else None
    Gimp.context_set_foreground(Gegl.Color.new("black"))
    mask.edit_fill(Gimp.FillType.FOREGROUND)
    Gimp.Selection.none(image)
    group.set_mode(Gimp.LayerMode.OVERLAY)
    group.set_opacity(90.0)
    pt = Gimp.GroupLayer.new(image, "Pass group")
    image.insert_layer(pt, None, 0)
    pt.set_mode(Gimp.LayerMode.PASS_THROUGH)
    pt.set_opacity(60.0)
    for i in range(2):
        layer = load(image, "big-tile.png")
        layer.set_name("pass child %d" % i)
        image.insert_layer(layer, pt, 0)
        layer.set_offsets(60 + 30 * i, 50)
        layer.set_mode(Gimp.LayerMode.DIFFERENCE)
    save(image, "groups.xcf", "group with a mask and Overlay mode, faded pass-through group of Difference layers")
    image.delete()


def alpha_backdrop_file(name, modes, note):
    rows = (len(modes) + COLUMNS - 1) // COLUMNS
    image = Gimp.Image.new(TILE * COLUMNS, TILE * rows, Gimp.ImageBaseType.RGB)
    base = load(image, "backdrop-alpha.png")
    base.set_name("Half-transparent backdrop")
    image.insert_layer(base, None, 0)
    mode_tiles(image, modes, "tile.png")
    save(image, name, note)
    image.delete()


def text_file():
    image = Gimp.Image.new(320, 200, Gimp.ImageBaseType.RGB)
    bg = Gimp.Layer.new(image, "Background", 320, 200, Gimp.ImageType.RGB_IMAGE, 100.0, Gimp.LayerMode.NORMAL)
    image.insert_layer(bg, None, 0)
    bg.fill(Gimp.FillType.WHITE)
    specs = [
        ("Hello GIMP", "Sans-serif", 24.0, "#c01010", Gimp.TextJustification.LEFT, 0.0, 0.0, (10, 10)),
        ("Serif bold\nsecond line", "Serif Bold", 18.0, "#1040a0", Gimp.TextJustification.CENTER, 2.0, 6.0, (150, 10)),
        ("Monospace", "Monospace", 16.0, "#108020", Gimp.TextJustification.RIGHT, 0.0, 0.0, (10, 80)),
    ]
    for text, font_name, size, color, justify, letter, line, offset in specs:
        font = Gimp.Font.get_by_name(font_name) or Gimp.Font.get_by_name("Sans-serif")
        layer = Gimp.TextLayer.new(image, text, font, size, Gimp.Unit.pixel())
        image.insert_layer(layer, None, 0)
        layer.set_offsets(*offset)
        layer.set_color(Gegl.Color.new(color))
        layer.set_justification(justify)
        layer.set_letter_spacing(letter)
        layer.set_line_spacing(line)
    box = Gimp.TextLayer.new(image, "A fixed box of text that wraps onto more lines", Gimp.Font.get_by_name("Sans-serif"), 14.0, Gimp.Unit.pixel())
    image.insert_layer(box, None, 0)
    box.set_offsets(150, 90)
    box.resize(150, 90)
    box.set_color(Gegl.Color.new("#000000"))
    rich = Gimp.TextLayer.new(image, "x", Gimp.Font.get_by_name("Sans-serif"), 20.0, Gimp.Unit.pixel())
    image.insert_layer(rich, None, 0)
    rich.set_offsets(10, 140)
    rich.set_markup('<markup>Rich <b>bold</b> <span foreground="#ff0000">red</span> <i>it</i></markup>')
    save(image, "text.xcf", "text layers: font, size, color, justification, letter and line spacing, a fixed box, markup")
    image.delete()


def paths_file():
    image = Gimp.Image.new(200, 150, Gimp.ImageBaseType.RGB)
    bg = load(image, "backdrop200.png")
    image.insert_layer(bg, None, 0)
    image.set_resolution(150.0, 150.0)
    image.set_unit(Gimp.Unit.mm())
    image.add_hguide(40)
    image.add_vguide(75)
    image.add_vguide(250)
    p = Gimp.Path.new(image, "Triangle")
    image.insert_path(p, None, 0)
    s = p.bezier_stroke_new_moveto(20, 20)
    p.bezier_stroke_lineto(s, 120, 30)
    p.bezier_stroke_cubicto(s, 140, 60, 100, 110, 40, 100)
    p.stroke_close(s)
    q = Gimp.Path.new(image, "Open curve")
    image.insert_path(q, None, 0)
    s2 = q.bezier_stroke_new_moveto(10, 140)
    q.bezier_stroke_cubicto(s2, 60, 100, 120, 160, 190, 120)
    image.select_ellipse(Gimp.ChannelOps.REPLACE, 50, 40, 80, 60)
    ch = Gimp.Selection.save(image)
    ch.set_name("Saved ellipse")
    image.select_rectangle(Gimp.ChannelOps.REPLACE, 10, 10, 60, 40)
    comment = Gimp.Parasite.new("gimp-comment", Gimp.PARASITE_PERSISTENT, list(b"A comment from GIMP\0"))
    image.attach_parasite(comment)
    save(image, "paths-guides-channels.xcf", "two paths, guides (one off-canvas), a saved channel, an active selection, 150 ppi in mm, a comment")
    image.delete()


def floating_file():
    image = Gimp.Image.new(160, 120, Gimp.ImageBaseType.RGB)
    bg = load(image, "backdrop160.png")
    image.insert_layer(bg, None, 0)
    top = load(image, "big-tile.png")
    top.set_name("Target")
    image.insert_layer(top, None, 0)
    top.set_offsets(20, 20)
    src = load(image, "tile.png")
    image.insert_layer(src, None, 0)
    Gimp.edit_copy([src])
    image.remove_layer(src)
    floating = Gimp.edit_paste(top, False)[0]
    floating.set_offsets(60, 50)
    save(image, "floating.xcf", "a floating selection attached to a layer")
    image.delete()


def indexed_file():
    image = Gimp.Image.new(160, 120, Gimp.ImageBaseType.RGB)
    base = load(image, "backdrop160.png")
    base.set_name("Background")
    image.insert_layer(base, None, 0)
    for i in range(3):
        t = load(image, "tile.png")
        t.set_name("tile %d" % i)
        image.insert_layer(t, None, 0)
        t.set_offsets(20 + 40 * i, 30 + 10 * i)
    image.convert_indexed(Gimp.ConvertDitherType.NONE, Gimp.ConvertPaletteType.GENERATE, 48, False, False, "")
    if image.get_base_type() != Gimp.ImageBaseType.INDEXED:
        raise RuntimeError("indexed conversion failed")
    save(image, "indexed.xcf", "an indexed image (48 colors) with layers that have alpha")
    image.delete()


def blur_file():
    image = Gimp.Image.new(200, 120, Gimp.ImageBaseType.RGB)
    bg = load(image, "backdrop200.png")
    image.insert_layer(bg, None, 0)
    for i, (sigma, clip) in enumerate([(2.0, True), (4.0, False)]):
        layer = load(image, "checker.png")
        layer.set_name("Blur %g%s" % (sigma, "" if clip else " grown"))
        image.insert_layer(layer, None, 0)
        layer.set_offsets(20 + 90 * i, 20)
        blur = Gimp.DrawableFilter.new(layer, "gegl:gaussian-blur", "Blur")
        config = blur.get_config()
        config.set_property("std-dev-x", sigma)
        config.set_property("std-dev-y", sigma)
        config.set_property("clip-extent", clip)
        blur.update()
        layer.append_filter(blur)
    save(image, "effects-blur.xcf", "Gaussian Blur effects on checkered layers, clipped and grown")
    image.delete()


def effects_file():
    image = Gimp.Image.new(200, 150, Gimp.ImageBaseType.RGB)
    bg = load(image, "backdrop200.png")
    image.insert_layer(bg, None, 0)
    layer = load(image, "big-tile.png")
    layer.set_name("Blurred")
    image.insert_layer(layer, None, 0)
    layer.set_offsets(20, 20)
    blur = Gimp.DrawableFilter.new(layer, "gegl:gaussian-blur", "Blur")
    config = blur.get_config()
    config.set_property("std-dev-x", 3.0)
    config.set_property("std-dev-y", 3.0)
    blur.update()
    layer.append_filter(blur)
    shadow_layer = load(image, "big-tile.png")
    shadow_layer.set_name("Shadowed")
    image.insert_layer(shadow_layer, None, 0)
    shadow_layer.set_offsets(110, 40)
    shadow = Gimp.DrawableFilter.new(shadow_layer, "gegl:dropshadow", "Drop Shadow")
    shadow.update()
    shadow_layer.append_filter(shadow)
    save(image, "effects.xcf", "GIMP 3 layer effects: a Gaussian blur and a drop shadow")
    image.delete()


def compressed_files():
    image = Gimp.Image.new(160, 120, Gimp.ImageBaseType.RGB)
    bg = load(image, "backdrop160.png")
    image.insert_layer(bg, None, 0)
    t = load(image, "big-tile.png")
    image.insert_layer(t, None, 0)
    t.set_offsets(30, 30)
    for ext in ["xcf.gz", "xcf.bz2", "xcf.xz"]:
        save(image, "compressed." + ext, "the same small image, " + ext)
    image.delete()


def precision_files():
    for p in PRECISIONS:
        structure_file("structure-" + p.lower().replace("_", "-") + ".xcf", getattr(Gimp.Precision, p),
                       "the structure scene at " + p)


def tile_layer(image, name, parent, x, y, mode="NORMAL"):
    layer = load(image, "big-tile.png")
    layer.set_name(name)
    image.insert_layer(layer, parent, 0)
    layer.set_offsets(x, y)
    layer.set_mode(getattr(Gimp.LayerMode, mode))
    return layer


def edits_files():
    # Files edited the way people edit them: layers and groups created, then deleted, moved
    # between groups and reordered, so the saved tree is not the order things were made in.
    image = Gimp.Image.new(160, 120, Gimp.ImageBaseType.RGB)
    image.insert_layer(load(image, "backdrop160.png"), None, 0)
    outer = Gimp.GroupLayer.new(image, "Outer")
    image.insert_layer(outer, None, 0)
    middle = Gimp.GroupLayer.new(image, "Middle")
    image.insert_layer(middle, outer, 0)
    inner = Gimp.GroupLayer.new(image, "Inner")
    image.insert_layer(inner, middle, 0)
    doomed_group = Gimp.GroupLayer.new(image, "Deleted group")
    image.insert_layer(doomed_group, outer, 0)
    tile_layer(image, "In deleted group", doomed_group, 0, 0)
    layers = []
    for i, mode in enumerate(["NORMAL", "MULTIPLY", "SCREEN", "OVERLAY", "DIFFERENCE", "GRAIN_MERGE", "HSV_HUE",
                              "LCH_COLOR"]):
        parent = (None, outer, middle, inner)[i % 4]
        layers.append(tile_layer(image, "%02d %s" % (i + 1, mode.lower()), parent, 8 + 16 * i, 6 + 12 * i, mode))
    image.remove_layer(layers[6])
    image.remove_layer(layers[1])
    image.remove_layer(doomed_group)
    image.reorder_item(layers[0], middle, 0)
    image.raise_item_to_top(layers[3])
    image.lower_item(layers[2])
    layers[4].set_visible(False)
    empty = Gimp.GroupLayer.new(image, "Empty group")
    image.insert_layer(empty, None, 0)
    image.set_selected_layers([layers[5]])
    save(image, "edits.xcf", "layers and groups created, deleted, moved between groups and reordered; nested groups")
    image.delete()

    # GIMP's coverage modes after deletes, with the active layer one Patchy replaces (issue 487).
    image = Gimp.Image.new(160, 120, Gimp.ImageBaseType.RGB)
    image.insert_layer(load(image, "backdrop160.png"), None, 0)
    group = Gimp.GroupLayer.new(image, "Isolated group")
    image.insert_layer(group, None, 0)
    tile_layer(image, "Erase at group bottom", group, 20, 20, "ERASE")
    tile_layer(image, "Group child", group, 40, 30)
    made = []
    for i, mode in enumerate(["NORMAL", "ERASE", "SCREEN", "SPLIT", "MULTIPLY", "SPLIT"]):
        made.append(tile_layer(image, "%02d %s" % (i + 1, mode.lower()), None, 10 + 18 * i, 10 + 10 * i, mode))
    made[1].set_opacity(60.0)
    image.remove_layer(made[4])
    image.set_selected_layers([made[5]])
    save(image, "edits-erase.xcf", "Erase and Split layers after deletes, one at a group's bottom, a Split layer active")
    image.delete()


def plain_file():
    image = Gimp.Image.new(64, 48, Gimp.ImageBaseType.RGB)
    bg = load(image, "backdrop64.png")
    image.insert_layer(bg, None, 0)
    save(image, "plain-rgba.xcf", "one RGBA layer, nothing else (lowest XCF version GIMP writes)")
    image.delete()


def want(name):
    return not ONLY or name in ONLY


if want("plain"): plain_file()
if want("modes"): modes_file("modes.xcf", DEFAULT_MODES, "backdrop-modes.png", "one tile per GIMP 2.10+ layer mode over an opaque backdrop")
if want("modes"): modes_file("modes-legacy.xcf", LEGACY_MODES, "backdrop-legacy.png", "one tile per legacy layer mode over an opaque backdrop")
if want("modes"): modes_file("modes-linear-16.xcf", DEFAULT_MODES, "backdrop-modes.png", "the default modes in a 16-bit linear image", Gimp.Precision.U16_LINEAR)
if want("alpha_backdrop"): alpha_backdrop_file("modes-alpha.xcf", DEFAULT_MODES, "the default modes over a half-transparent backdrop (composite modes)")
if want("alpha_backdrop"): alpha_backdrop_file("modes-legacy-alpha.xcf", LEGACY_MODES, "the legacy modes over a half-transparent backdrop")
if want("structure"): structure_file("structure.xcf", Gimp.Precision.U8_NON_LINEAR, "groups, pass-through, offsets, opacity, masks, hidden, locks, Unicode name, empty layer")
if want("structure"): structure_file("structure-gray.xcf", Gimp.Precision.U8_NON_LINEAR, "the structure scene in grayscale", Gimp.ImageBaseType.GRAY)
if want("structure"): structure_file("structure-gray-16.xcf", Gimp.Precision.U16_NON_LINEAR, "the structure scene in 16-bit grayscale", Gimp.ImageBaseType.GRAY)
if want("precision"): precision_files()
if want("group"): group_file()
if want("text"): text_file()
if want("paths"): paths_file()
if want("floating"): floating_file()
if want("indexed"): indexed_file()
if want("blur"): blur_file()
if want("effects"): effects_file()
if want("compressed"): compressed_files()
if want("edits"): edits_files()
import os as _os
_path = OUT + "/manifest.txt"
_kept = []
if ONLY and _os.path.exists(_path):
    _made = set(line.split(":", 1)[0] for line in manifest)
    with open(_path, encoding="utf-8") as handle:
        _kept = [line.rstrip("\n") for line in handle if line.strip() and line.split(":", 1)[0] not in _made]
with open(_path, "w", encoding="utf-8") as handle:
    handle.write("\n".join(_kept + manifest) + "\n")
'''


def write(path, data):
    with open(path, "wb") as handle:
        handle.write(data)


def main():
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
    args = sys.argv[1:]
    only = []
    if "--only" in args:
        at = args.index("--only")
        only = [name for name in args[at + 1].split(",") if name]
        del args[at:at + 2]
    out = os.path.abspath(args[0]) if args else os.path.join(root, "local-test-fixtures", "xcf")
    os.makedirs(out, exist_ok=True)
    work = tempfile.mkdtemp(prefix="patchy-xcf-fixtures-")
    try:
        default_rows = (len(DEFAULT_MODES) + COLUMNS - 1) // COLUMNS
        legacy_rows = (len(LEGACY_MODES) + COLUMNS - 1) // COLUMNS
        write(os.path.join(work, "backdrop-modes.png"),
              png_bytes(TILE * COLUMNS, TILE * default_rows, backdrop(TILE * COLUMNS, TILE * default_rows)))
        write(os.path.join(work, "backdrop-legacy.png"),
              png_bytes(TILE * COLUMNS, TILE * legacy_rows, backdrop(TILE * COLUMNS, TILE * legacy_rows)))
        write(os.path.join(work, "backdrop-alpha.png"),
              png_bytes(TILE * COLUMNS, TILE * default_rows,
                        backdrop(TILE * COLUMNS, TILE * default_rows, lambda x, y: 0.15 + 0.7 * ((x // 8 + y // 8) % 2))))
        for w, h in ((160, 120), (200, 150), (64, 48)):
            write(os.path.join(work, f"backdrop{w}.png"), png_bytes(w, h, backdrop(w, h)))
        write(os.path.join(work, "tile.png"), png_bytes(TILE, TILE, tile))
        write(os.path.join(work, "big-tile.png"), png_bytes(64, 48, lambda x, y: (0.2 + 0.6 * x / 63, 0.8 - 0.6 * y / 47, 0.6, 1.0)))
        write(os.path.join(work, "mask-ramp.png"), png_bytes(64, 48, mask_ramp, channels=1))
        write(os.path.join(work, "checker.png"),
              png_bytes(64, 64, lambda x, y: (1.0, 1.0, 1.0, 1.0) if (x // 8 + y // 8) % 2 else (0.1, 0.2, 0.6, 1.0)))
        script = os.path.join(work, "make.py")
        with open(script, "w", encoding="utf-8") as handle:
            handle.write(GIMP_SCRIPT.format(work=work.replace("\\", "/"), out=out.replace("\\", "/"),
                                            default_modes=DEFAULT_MODES, legacy_modes=LEGACY_MODES,
                                            precisions=PRECISIONS, tile=TILE, columns=COLUMNS, only=only))
        result = subprocess.run([GIMP, "-i", "--batch-interpreter=python-fu-eval",
                                 "-b", f"exec(open(r'{script}', encoding='utf-8').read())",
                                 "-b", "Gimp.get_pdb().lookup_procedure('gimp-quit').run(None)"],
                                timeout=1200, capture_output=True, text=True, errors="replace")
        output = (result.stdout or "") + (result.stderr or "")
        for line in output.splitlines():
            if "Traceback" in line or "Error" in line or "error" in line or "line " in line:
                print(line)
    finally:
        if os.path.basename(work).startswith("patchy-xcf-fixtures-"):
            shutil.rmtree(work, ignore_errors=True)
    print("wrote fixtures to", out)


if __name__ == "__main__":
    main()
