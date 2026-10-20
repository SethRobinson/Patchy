"""Writes the RGBA probe image the native Smart Filter captures filter.

Deterministic content (splitmix64, no numpy random): a smooth two-axis
gradient, a high-frequency block region, saturated patches, a half-alpha
band, and a transparent margin, so one capture exercises flat areas, edges,
color and alpha handling.

usage: python make_probe_source.py <out.png> [width height [v2]]

v2 lets the content touch the left and top canvas edges (margin only on the
right and bottom), cuts a transparent disc and a transparent diagonal stripe
into it, and adds a horizontal alpha ramp, so transparency rules and canvas
edge handling become observable.
"""

import sys

from PIL import Image


def splitmix64(state):
    state = (state + 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF
    z = state
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & 0xFFFFFFFFFFFFFFFF
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & 0xFFFFFFFFFFFFFFFF
    return state, z ^ (z >> 31)


def main():
    out = sys.argv[1]
    width = int(sys.argv[2]) if len(sys.argv) > 2 else 128
    height = int(sys.argv[3]) if len(sys.argv) > 3 else 96
    v2 = len(sys.argv) > 4 and sys.argv[4] == "v2"
    image = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    pixels = image.load()
    margin = 8
    state = 12345
    half = width // 2
    start = 0 if v2 else margin
    for y in range(start, height - margin):
        for x in range(start, width - margin):
            if x < half:
                r = max(0, (x - margin) * 255 // max(1, half - margin - 1))
                g = max(0, (y - margin) * 255 // max(1, height - 2 * margin - 1))
                b = 255 - (r + g) // 2
            else:
                # 4x4 pseudo-random blocks
                key = ((y // 4) * 1000 + (x // 4)) & 0xFFFFFFFF
                _, v = splitmix64(key * 0x10001 + 7)
                r, g, b = v & 255, (v >> 8) & 255, (v >> 16) & 255
            a = 255
            if y >= height - margin - 10:
                a = 128
            if v2:
                if (x - 40) ** 2 + (y - 40) ** 2 <= 36:
                    a = 0
                if 0 <= (x - y) - 60 < 3:
                    a = 0
                if 20 <= y < 30 and x >= half:
                    a = min(a, (x - half) * 255 // max(1, width - margin - half))
            pixels[x, y] = (r, g, b, a)
    # saturated patches
    for (cx, cy, color) in ((24, 24, (255, 0, 0)), (40, 56, (0, 255, 0)),
                            (88, 30, (0, 0, 255)), (100, 60, (255, 255, 0))):
        for y in range(cy - 5, cy + 5):
            for x in range(cx - 5, cx + 5):
                if 0 <= x < width and 0 <= y < height:
                    pixels[x, y] = color + (255,)
    image.save(out)


if __name__ == "__main__":
    main()
