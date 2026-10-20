"""Decodes the first FEid/FXid Smart Filter cache record of a PSD into an RGBA PNG.

The cache holds the UNFILTERED placed Smart Object in document space, including
the straight RGB Photoshop keeps under fully transparent pixels: what its
filters actually read.

usage: python read_feid_cache.py file.psd out.png
"""

import struct
import sys

import numpy as np
from PIL import Image


def packbits(data, size):
    out = bytearray()
    i = 0
    while len(out) < size:
        n = data[i]
        i += 1
        if n < 128:
            out += data[i:i + n + 1]
            i += n + 1
        elif n > 128:
            out += bytes([data[i]]) * (257 - n)
            i += 1
    return bytes(out[:size])


def main():
    data = open(sys.argv[1], "rb").read()
    pos = data.find(b"8BIMFEid")
    if pos < 0:
        pos = data.find(b"8BIMFXid")
    p = pos + 8 + 4  # key + u32 length (PSD)
    version = struct.unpack_from(">I", data, p)[0]
    p += 4
    record_length = struct.unpack_from(">Q", data, p)[0]
    p += 8
    id_length = data[p]
    p += 1 + id_length
    record_version, cache_length = struct.unpack_from(">IQ", data, p)
    p += 12
    top, left, bottom, right, depth, channels = struct.unpack_from(">iiiiII", data, p)
    p += 24
    height, width = bottom - top, right - left
    planes = {}
    for slot in range(channels + 2):
        written = struct.unpack_from(">I", data, p)[0]
        p += 4
        if not written:
            continue
        length = struct.unpack_from(">Q", data, p)[0]
        p += 8
        body = data[p:p + length]
        p += length
        compression = struct.unpack_from(">H", body, 0)[0]
        if compression == 0:
            plane = np.frombuffer(body[2:2 + width * height], np.uint8)
        else:
            rows = struct.unpack_from(">%dI" % height, body, 2)
            q = 2 + 4 * height
            out = bytearray()
            for row in rows:
                out += packbits(body[q:q + row], width)
                q += row
            plane = np.frombuffer(bytes(out), np.uint8)
        planes[slot] = plane.reshape(height, width)
    alpha = planes.get(25, planes.get(3))
    rgba = np.dstack([planes[0], planes[1], planes[2], alpha])
    Image.fromarray(rgba, "RGBA").save(sys.argv[2])
    print("version", version, "bounds", (top, left, bottom, right), "depth", depth,
          "channels", channels, "slots", sorted(planes))


if __name__ == "__main__":
    main()
