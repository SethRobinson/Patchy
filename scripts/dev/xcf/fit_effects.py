"""Fits Patchy's formulas for GIMP's color filters against GIMP's renders (docs/xcf.md).

Reads the files make_effect_fixtures.py writes (the 4096-color sweep with one GIMP filter
each) and prints, per filter and candidate formula, the mean and worst difference in 8-bit
levels. A development aid; the chosen formulas live in src/formats/xcf_document_io.cpp.

Usage: python -I scripts/dev/xcf/fit_effects.py [fixture_dir]
"""

import colorsys
import math
import os
import sys

import numpy as np
from PIL import Image


def dec(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def enc(c):
    c = min(max(c, 0.0), 1.0)
    return 12.92 * c if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055


def sweep():
    out = []
    for index in range(4096):
        r, g, b = index % 16, (index // 16) % 16, index // 256
        out.append((r * 17 / 255, g * 17 / 255, b * 17 / 255))
    return out


Y = (0.2225045, 0.7168786, 0.0606169)


def gray(v):
    return (v, v, v)


CANDIDATES = {
    "invert-gamma": {"1-x": lambda c: tuple(1 - x for x in c)},
    "invert-linear": {"enc(1-dec)": lambda c: tuple(enc(1 - dec(x)) for x in c)},
    "desaturate-0": {
        "hsl lightness perceptual": lambda c: gray((max(c) + min(c)) / 2),
        "hsl lightness linear": lambda c: gray(enc((max(map(dec, c)) + min(map(dec, c))) / 2)),
    },
    "desaturate-1": {
        "luma rec709 perceptual": lambda c: gray(0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]),
        "luma D50 perceptual": lambda c: gray(sum(w * x for w, x in zip(Y, c))),
    },
    "desaturate-2": {
        "average perceptual": lambda c: gray(sum(c) / 3),
        "average linear": lambda c: gray(enc(sum(map(dec, c)) / 3)),
    },
    "desaturate-3": {
        "luminance linear D50": lambda c: gray(enc(sum(w * dec(x) for w, x in zip(Y, c)))),
        "luminance linear 709": lambda c: gray(enc(0.2126 * dec(c[0]) + 0.7152 * dec(c[1]) + 0.0722 * dec(c[2]))),
    },
    "desaturate-4": {
        "value perceptual": lambda c: gray(max(c)),
        "value linear": lambda c: gray(enc(max(map(dec, c)))),
    },
    "threshold": {
        "max perceptual": lambda c: gray(1.0 if 0.4 <= max(c) <= 0.9 else 0.0),
        "max linear": lambda c: gray(1.0 if 0.4 <= max(map(dec, c)) <= 0.9 else 0.0),
    },
    "posterize": {
        "round perceptual": lambda c: tuple(round(x * 3) / 3 for x in c),
        "round linear": lambda c: tuple(enc(round(dec(x) * 3) / 3) for x in c),
    },
}


def brightness_contrast(brightness, contrast, linear):
    def apply(x):
        v = dec(x) if linear else x
        if brightness < 0:
            v = v * (1 + brightness)
        else:
            v = v + (1 - v) * brightness
        slant = math.tan((contrast + 1) * math.pi / 4)
        v = (v - 0.5) * slant + 0.5
        v = min(max(v, 0.0), 1.0)
        return enc(v) if linear else v
    return lambda c: tuple(apply(x) for x in c)


for stem, b, k in (("brightness-up", 0.3, 0.0), ("brightness-down", -0.3, 0.0), ("contrast-up", 0.0, 0.4),
                   ("contrast-down", 0.0, -0.4), ("brightness-contrast", 0.2, 0.3)):
    CANDIDATES[stem] = {"curve perceptual": brightness_contrast(b, k, False),
                        "curve linear": brightness_contrast(b, k, True)}


def levels(lin, gamma, lout, hout, linear):
    def apply(x):
        v = dec(x) if linear else x
        v = (v - 0.1) / 0.8
        v = min(max(v, 0.0), 1.0) ** (1 / gamma) if v > 0 else 0.0
        v = lout + (hout - lout) * v
        return enc(v) if linear else v
    return lambda c: tuple(apply(x) for x in c)


CANDIDATES["levels-linear"] = {"linear": levels(0.1, 1.5, 0.05, 0.95, True), "perceptual": levels(0.1, 1.5, 0.05, 0.95, False)}
CANDIDATES["levels-perceptual"] = {"linear": levels(0.1, 1.5, 0.05, 0.95, True), "perceptual": levels(0.1, 1.5, 0.05, 0.95, False)}


def hue_saturation(hue, saturation, lightness):
    def apply(c):
        h, l, s = colorsys.rgb_to_hls(*c)
        h = (h + hue / 2.0) % 1.0
        s = min(max(s * (1 + saturation), 0.0), 1.0)
        if lightness < 0:
            l = l * (1 + lightness)
        else:
            l = l + (1 - l) * lightness
        return colorsys.hls_to_rgb(h, l, s)
    return apply


for stem, args in (("hue-30", (30 / 180, 0, 0)), ("saturation-up", (0, 0.5, 0)), ("saturation-down", (0, -0.5, 0)),
                   ("lightness-up", (0, 0, 0.3)), ("lightness-down", (0, 0, -0.3)),
                   ("hue-saturation", (-60 / 180, 0.2, 0.1))):
    CANDIDATES[stem] = {"hsl": hue_saturation(*args)}


def colorize(hue, saturation, lightness, mode):
    def apply(c):
        if mode == "luminance":
            lum = enc(sum(w * dec(x) for w, x in zip(Y, c)))
        else:
            lum = (max(c) + min(c)) / 2
        if lightness > 0:
            lum = lum * (1 - lightness) + lightness
        elif lightness < 0:
            lum = lum * (lightness + 1)
        return colorsys.hls_to_rgb(hue, lum, saturation)
    return apply


CANDIDATES["colorize"] = {m: colorize(0.5, 0.5, 0.0, m) for m in ("luminance", "lightness")}
CANDIDATES["colorize-2"] = {m: colorize(0.1, 0.8, 0.2, m) for m in ("luminance", "lightness")}
CANDIDATES["exposure"] = {
    "linear gain": lambda c: tuple(enc((dec(x) - 0.02) * 2 ** 0.7) for x in c),
    "linear gain over white": lambda c: tuple(enc((dec(x) - 0.02) * 2 ** 0.7 / (1 - 0.02)) for x in c),
}
CANDIDATES["mono-mixer"] = {
    "linear": lambda c: gray(enc(0.5 * dec(c[0]) + 0.3 * dec(c[1]) + 0.2 * dec(c[2]))),
    "perceptual": lambda c: gray(0.5 * c[0] + 0.3 * c[1] + 0.2 * c[2]),
}


def main():
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
    folder = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, "local-test-fixtures", "xcf", "effects")
    inputs = sweep()
    for stem, candidates in CANDIDATES.items():
        path = os.path.join(folder, stem + ".png")
        if not os.path.exists(path):
            continue
        gimp = np.asarray(Image.open(path).convert("RGB")).reshape(-1, 3).astype(float)
        for name, fn in candidates.items():
            expected = np.array([[min(max(v, 0.0), 1.0) * 255 for v in fn(c)] for c in inputs])
            diff = np.abs(np.round(expected) - gimp)
            print(f"{stem:22} {name:28} mean {diff.mean():7.3f} worst {diff.max():5.0f}")


if __name__ == "__main__":
    main()
