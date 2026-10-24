"""Writes the small self-authored OpenRaster files checked into test-fixtures/ora/.

They come from Python's own zipfile and zlib, not from Patchy's writer, so the tests read
a file no Patchy code produced (docs/ora.md, "Fixtures"). Deterministic: fixed timestamps.

Usage: python scripts/dev/ora/make_committed_fixtures.py
"""

import os
import struct
import zipfile
import zlib


def png(width, height, pixel, depth=8):
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            for value in pixel(x, y):
                rows += struct.pack(">H", value) if depth == 16 else bytes([value])

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, depth, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + chunk(b"IEND", b""))


def write_ora(path, stack_xml, files):
    with zipfile.ZipFile(path, "w") as archive:
        info = zipfile.ZipInfo("mimetype", date_time=(1980, 1, 1, 0, 0, 0))
        archive.writestr(info, "image/openraster", compress_type=zipfile.ZIP_STORED)
        for name, data in [("stack.xml", stack_xml.encode("utf-8"))] + files:
            info = zipfile.ZipInfo(name, date_time=(2026, 10, 24, 12, 0, 0))
            archive.writestr(info, data, compress_type=zipfile.ZIP_DEFLATED)


def main():
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
    out = os.path.join(root, "test-fixtures", "ora")
    os.makedirs(out, exist_ok=True)

    background = png(32, 24, lambda x, y: (40 + x * 6, 60 + y * 7, 200 - x * 4, 255))
    top = png(8, 8, lambda x, y: (250, 30 * (x % 4), 20, 255 if (x + y) % 3 else 128))
    inner = png(10, 10, lambda x, y: (120 + x * 10, 220 - y * 10, 90, 200))
    write_ora(os.path.join(out, "python-layers.ora"),
              '<image version="0.0.3" w="32" h="24" xres="96" yres="96">'
              '<stack>'
              '<layer src="data/top.png" name="Top" x="4" y="2" opacity="0.5" selected="true"/>'
              '<stack name="Folder" isolation="auto">'
              '<layer src="data/inner.png" name="Inner" x="10" y="10" composite-op="svg:multiply"/>'
              '</stack>'
              '<layer src="data/background.png" name="Background"/>'
              '</stack></image>',
              [("data/top.png", top), ("data/inner.png", inner), ("data/background.png", background),
               ("mergedimage.png", background), ("Thumbnails/thumbnail.png", background)])

    single = png(16, 16, lambda x, y: (1000 + x * 4000, 60000 - y * 3000, 32768, x * 4369), depth=16)
    write_ora(os.path.join(out, "python-single-layer-16.ora"),
              '<image w="16" h="16"><stack><layer src="data/layer.png" name="Only"/></stack></image>',
              [("data/layer.png", single)])
    print("wrote", out)


if __name__ == "__main__":
    main()
