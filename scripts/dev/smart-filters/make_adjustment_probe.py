"""Writes the RGBA probe image the adjustment Smart Filter captures filter.

256 x 32 pixels: rows 0-3 a 256-step gray ramp, rows 4-19 every color of a
16-level RGB cube (red = x % 16, green = x // 16, blue = row), rows 20-27 a
horizontal color ramp whose alpha climbs from 0 to 255 down the columns
(partial transparency), rows 28-31 fully transparent with colored hidden RGB
zeroed (PNG keeps them black).

usage: python make_adjustment_probe.py <out.png>
"""

import sys

from PIL import Image


def level(index):
    return index * 17


def main():
    image = Image.new("RGBA", (256, 32), (0, 0, 0, 0))
    pixels = image.load()
    for x in range(256):
        for y in range(4):
            pixels[x, y] = (x, x, x, 255)
        for b in range(16):
            pixels[x, 4 + b] = (level(x % 16), level(x // 16), level(b), 255)
        for row in range(8):
            r = (x * 7 + row * 31) % 256
            g = (x * 3 + 64) % 256
            b = (255 - x + row * 13) % 256
            pixels[x, 20 + row] = (r, g, b, x)
    image.save(sys.argv[1])


if __name__ == "__main__":
    main()
