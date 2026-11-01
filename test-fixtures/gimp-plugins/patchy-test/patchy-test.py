#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Seth A. Robinson
#
# Patchy's own GIMP 3 test plug-in (docs/gimp-plugins.md, "Tests"). It is written
# against GIMP 3's public Python API (developer.gimp.org) and exercises what Patchy's
# plug-in host serves: tiles in and out through the shadow buffer, the selection
# bounds, progress, a few PDB calls, a dialog with a live preview, and a call Patchy
# does not support. It runs in GIMP too, which is how its behaviour was checked first.

import sys

import gi

gi.require_version("Gimp", "3.0")
gi.require_version("GimpUi", "3.0")
gi.require_version("Gegl", "0.4")
from gi.repository import Gegl, Gimp, GimpUi, GLib, GObject  # noqa: E402

FORMAT = "R'G'B'A u8"


def invert_bands(drawable, band, preview=None):
    """Inverts every other band of `band` rows inside the selection bounds."""
    ok, x, y, w, h = drawable.mask_intersect()
    if not ok or w <= 0 or h <= 0:
        return False
    source = drawable.get_buffer()
    rect = Gegl.Rectangle.new(x, y, w, h)
    data = bytearray(source.get(rect, 1.0, FORMAT, Gegl.AbyssPolicy.CLAMP))
    stride = w * 4
    for row in range(h):
        if (row // band) % 2 == 0:
            start = row * stride
            for i in range(start, start + stride, 4):
                data[i] = 255 - data[i]
                data[i + 1] = 255 - data[i + 1]
                data[i + 2] = 255 - data[i + 2]
        if row % 64 == 0:
            Gimp.progress_update(row / h)
    if preview is not None:
        preview.draw_buffer(bytes(data), stride)
        return True
    shadow = drawable.get_shadow_buffer()
    shadow.set(rect, FORMAT, bytes(data))
    shadow.flush()
    drawable.merge_shadow(True)
    drawable.update(x, y, w, h)
    Gimp.progress_update(1.0)
    return True


class PatchyTest(Gimp.PlugIn):
    def do_query_procedures(self):
        return ["patchy-test-invert-bands", "patchy-test-context", "patchy-test-unsupported"]

    def do_set_i18n(self, name):
        return False

    def do_create_procedure(self, name):
        procedure = Gimp.ImageProcedure.new(self, name, Gimp.PDBProcType.PLUGIN, self.run, None)
        procedure.set_image_types("RGB*")
        procedure.set_sensitivity_mask(Gimp.ProcedureSensitivityMask.DRAWABLE)
        procedure.set_attribution("Patchy", "Patchy (MIT)", "2026")
        if name == "patchy-test-invert-bands":
            procedure.set_menu_label("Invert Bands...")
            procedure.set_documentation("Inverts every other band of rows",
                                        "Patchy's test plug-in: tiles in and out, selection bounds, a preview.", name)
            procedure.add_int_argument("band", "_Band height", "Rows per band", 1, 4096, 8,
                                       GObject.ParamFlags.READWRITE)
        elif name == "patchy-test-context":
            procedure.set_menu_label("Paint Context")
            procedure.set_documentation("Fills the selection with the foreground color",
                                        "Patchy's test plug-in: image, layer and context PDB calls.", name)
        else:
            procedure.set_menu_label("Needs a New Layer")
            procedure.set_documentation("Adds a layer (Patchy refuses this)",
                                        "Patchy's test plug-in: a procedure Patchy does not support.", name)
        # GIMP wants the label before the menu path.
        procedure.add_menu_path("<Image>/Filters/Patchy Test")
        return procedure

    def run(self, procedure, run_mode, image, drawables, config, run_data):
        if len(drawables) != 1:
            return procedure.new_return_values(Gimp.PDBStatusType.CALLING_ERROR, GLib.Error())
        drawable = drawables[0]
        name = procedure.get_name()
        if name == "patchy-test-invert-bands":
            if run_mode == Gimp.RunMode.INTERACTIVE:
                GimpUi.init("patchy-test")
                dialog = GimpUi.ProcedureDialog.new(procedure, config, "Invert Bands")
                preview = GimpUi.DrawablePreview.new_from_drawable(drawable)
                dialog.get_content_area().pack_start(preview, True, True, 0)
                preview.show()

                def redraw(*_args):
                    invert_bands(drawable, max(1, config.get_property("band")), preview)

                preview.connect("invalidated", redraw)
                config.connect("notify::band", lambda *_a: preview.invalidate())
                dialog.fill(None)
                accepted = dialog.run()
                dialog.destroy()
                if not accepted:
                    return procedure.new_return_values(Gimp.PDBStatusType.CANCEL, GLib.Error())
            Gimp.progress_init("Inverting bands")
            invert_bands(drawable, max(1, config.get_property("band")))
            Gimp.displays_flush()
        elif name == "patchy-test-context":
            # Several PDB calls; the result shows they answered: the foreground color
            # fills the selection bounds.
            width, height = image.get_width(), image.get_height()
            if width <= 0 or height <= 0 or not drawable.is_rgb():
                return procedure.new_return_values(Gimp.PDBStatusType.EXECUTION_ERROR, GLib.Error())
            color = Gimp.context_get_foreground()
            ok, x, y, w, h = drawable.mask_intersect()
            if not ok:
                return procedure.new_return_values(Gimp.PDBStatusType.SUCCESS, GLib.Error())
            rgba = color.get_rgba()
            pixel = bytes([round(rgba[0] * 255), round(rgba[1] * 255), round(rgba[2] * 255), 255])
            shadow = drawable.get_shadow_buffer()
            shadow.set(Gegl.Rectangle.new(x, y, w, h), FORMAT, pixel * (w * h))
            shadow.flush()
            drawable.merge_shadow(True)
            drawable.update(x, y, w, h)
            Gimp.message("Patchy test: painted %dx%d at %d,%d" % (w, h, x, y))
        else:
            layer = Gimp.Layer.new(image, "added", 8, 8, Gimp.ImageType.RGBA_IMAGE, 100.0, Gimp.LayerMode.NORMAL)
            if layer is None:
                return procedure.new_return_values(Gimp.PDBStatusType.EXECUTION_ERROR, GLib.Error())
            image.insert_layer(layer, None, 0)
        return procedure.new_return_values(Gimp.PDBStatusType.SUCCESS, GLib.Error())


Gimp.main(PatchyTest.__gtype__, sys.argv)
