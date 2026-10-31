"""Builds test-fixtures/xcf/vector-layers.xcf with GIMP 3.2 and its reference render (docs/xcf.md, "Vector layers").

GIMP 3.2 vector layers: fill and stroke, fill only (a path of two strokes, filled even-odd),
stroke only (square caps, bevel joins), a miter-joined triangle, a dashed stroke, and one a Patchy
shape layer cannot hold (pixels painted on after GIMP drew the layer), over an alpha-less
Background. Beside it, vector-layers.png is GIMP's merge of the visible layers.
GIMP runs only through its batch Python console; nothing here reads its source.

Usage (Windows, GIMP 3.2 in its default per-user place):
    python -I scripts/dev/xcf/make_vector_layer_fixture.py [OUT_DIR]

OUT_DIR defaults to test-fixtures/xcf.
"""

import os
import subprocess
import sys
import tempfile

GIMP = os.environ.get("PATCHY_GIMP_CONSOLE",
                      os.path.expandvars(r"%LOCALAPPDATA%\Programs\GIMP 3\bin\gimp-console-3.exe"))

SCRIPT = r'''
import gi, traceback
gi.require_version('Gimp', '3.0')
gi.require_version('Gegl', '0.4')
from gi.repository import Gimp, Gio, Gegl
OUT = {out!r}
log = open(OUT + '/vector-layers-log.txt', 'w')
try:
    image = Gimp.Image.new(120, 100, Gimp.ImageBaseType.RGB)
    background = Gimp.Layer.new(image, "Background", 120, 100, Gimp.ImageType.RGB_IMAGE, 100.0,
                                Gimp.LayerMode.NORMAL)
    image.insert_layer(background, None, 0)
    Gimp.context_set_background(Gegl.Color.new("white"))
    background.fill(Gimp.FillType.BACKGROUND)

    def make_path(name, strokes):
        path = Gimp.Path.new(image, name)
        image.insert_path(path, None, 0)
        for closed, start, *segments in strokes:
            stroke = path.bezier_stroke_new_moveto(*start)
            for segment in segments:
                if len(segment) == 2:
                    path.bezier_stroke_lineto(stroke, *segment)
                else:
                    path.bezier_stroke_cubicto(stroke, *segment)
            if closed:
                path.stroke_close(stroke)
        return path

    def vector(name, path, fill=None, stroke=None, width=1.0, cap=None, join=None, miter=None, dashes=None):
        layer = Gimp.VectorLayer.new(image, path)
        layer.set_name(name)
        image.insert_layer(layer, None, 0)
        layer.set_enable_fill(fill is not None)
        if fill is not None:
            layer.set_fill_color(Gegl.Color.new(fill))
        layer.set_enable_stroke(stroke is not None)
        if stroke is not None:
            layer.set_stroke_color(Gegl.Color.new(stroke))
            layer.set_stroke_width(width)
            if cap is not None:
                layer.set_stroke_cap_style(cap)
            if join is not None:
                layer.set_stroke_join_style(join)
            if miter is not None:
                layer.set_stroke_miter_limit(miter)
            if dashes is not None:
                layer.set_stroke_dash_pattern(dashes)
        layer.refresh()
        return layer

    vector("Fill and stroke", make_path("Blob", [(True, (10, 10), (50, 12), (60, 30, 40, 50, 30, 45), (12, 40))]),
           fill="#3080e0", stroke="#202020", width=4.0, cap=Gimp.CapStyle.ROUND, join=Gimp.JoinStyle.ROUND)
    vector("Fill only", make_path("Ring", [(True, (70, 8), (112, 8), (112, 48), (70, 48)),
                                           (True, (82, 20), (100, 20), (100, 36), (82, 36))]),
           fill="rgb(0.9, 0.6, 0.1)")
    vector("Stroke only", make_path("Zigzag", [(False, (10, 62), (30, 90), (50, 62), (70, 90, 80, 60, 95, 70))]),
           stroke="#c02040", width=6.0, cap=Gimp.CapStyle.SQUARE, join=Gimp.JoinStyle.BEVEL)
    vector("Miter stroke", make_path("Triangle", [(True, (100, 60), (115, 95), (85, 95))]),
           stroke="#108040", width=3.0, cap=Gimp.CapStyle.BUTT, join=Gimp.JoinStyle.MITER, miter=10.0)
    vector("Dashed stroke", make_path("Dash line", [(False, (8, 54), (112, 54))]),
           stroke="#6040a0", width=2.0, dashes=[3.0, 2.0])
    painted = vector("Painted on", make_path("Square", [(True, (64, 72), (80, 72), (80, 96), (64, 96))]),
                     fill="#f0d020")
    image.select_rectangle(Gimp.ChannelOps.REPLACE, 66, 80, 12, 6)
    Gimp.context_set_foreground(Gegl.Color.new("black"))
    painted.edit_fill(Gimp.FillType.FOREGROUND)
    Gimp.Selection.none(image)

    Gimp.file_save(Gimp.RunMode.NONINTERACTIVE, image, Gio.File.new_for_path(OUT + '/vector-layers.xcf'), None)
    merged = image.duplicate()
    merged.merge_visible_layers(Gimp.MergeType.CLIP_TO_IMAGE)
    Gimp.file_save(Gimp.RunMode.NONINTERACTIVE, merged, Gio.File.new_for_path(OUT + '/vector-layers.png'), None)
    for layer in image.get_layers():
        log.write(layer.get_name() + " vector=" + str(layer.is_vector_layer()) + "\n")
except Exception:
    log.write(traceback.format_exc())
log.close()
'''


def main():
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
    out = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(root, "test-fixtures", "xcf")
    with tempfile.NamedTemporaryFile("w", suffix=".py", delete=False, encoding="utf-8",
                                     prefix="patchy-xcf-vector-") as handle:
        handle.write(SCRIPT.format(out=out.replace("\\", "/")))
        script = handle.name
    try:
        subprocess.run([GIMP, "-i", "--batch-interpreter=python-fu-eval",
                        "-b", f"exec(open(r'{script}', encoding='utf-8').read())",
                        "-b", "Gimp.get_pdb().lookup_procedure('gimp-quit').run(None)"],
                       timeout=600, capture_output=True)
    finally:
        os.remove(script)
    log = os.path.join(out, "vector-layers-log.txt")
    with open(log, encoding="utf-8") as handle:
        print(handle.read(), end="")
    os.remove(log)


if __name__ == "__main__":
    main()
