#!/usr/bin/env python
"""Generates the committed DDS fixtures under test-fixtures/dds and prints the sample table
the core tests pin.

    python -I scripts/dev/dds/make_dds_fixtures.py test-fixtures/dds [--texconv path/to/texconv.exe]

Everything is self-authored procedural art. Pillow writes the files it can (uncompressed
RGB/RGBA, DXT1/DXT3/DXT5, BC5 under a DX10 header); the rest are assembled here with struct
(masked 16-bit formats, DX10 alpha-mode flags, cubemap, volume, array, a mip chain). texconv
(DirectXTex, MIT) produces the BC4, BC7 and BC6H fixtures when its path is given; Pillow then
decodes every file independently for the expected values. The decoders differ by at most 1
on interpolated BC texels, which is the tolerance the test uses for them.
"""

import argparse
import io
import json
import os
import struct
import subprocess
import sys
import tempfile

from PIL import Image

DDS_MAGIC = b"DDS "
DDSD_CAPS, DDSD_HEIGHT, DDSD_WIDTH, DDSD_PITCH = 0x1, 0x2, 0x4, 0x8
DDSD_PIXELFORMAT, DDSD_MIPMAPCOUNT, DDSD_LINEARSIZE, DDSD_DEPTH = 0x1000, 0x20000, 0x80000, 0x800000
DDPF_ALPHAPIXELS, DDPF_ALPHA, DDPF_FOURCC, DDPF_RGB, DDPF_LUMINANCE = 0x1, 0x2, 0x4, 0x40, 0x20000
DDSCAPS_COMPLEX, DDSCAPS_TEXTURE, DDSCAPS_MIPMAP = 0x8, 0x1000, 0x400000
DDSCAPS2_CUBEMAP, DDSCAPS2_ALL_FACES, DDSCAPS2_VOLUME = 0x200, 0xFC00, 0x200000


def fourcc(text):
    return struct.unpack("<I", text.encode("ascii"))[0]


def header(width, height, pf, *, flags_extra=0, pitch=0, depth=0, mips=0, caps=DDSCAPS_TEXTURE, caps2=0, dx10=None):
    flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | flags_extra
    out = DDS_MAGIC + struct.pack("<7I", 124, flags, height, width, pitch, depth, mips) + b"\0" * 44
    out += struct.pack("<8I", 32, *pf)
    out += struct.pack("<5I", caps, caps2, 0, 0, 0)
    if dx10 is not None:
        out += struct.pack("<5I", *dx10)
    return out


def pf_masks(flags, bits, r, g, b, a):
    return (flags, 0, bits, r, g, b, a)


def pf_fourcc(code):
    return (DDPF_FOURCC, fourcc(code), 0, 0, 0, 0, 0)


PF_DX10 = pf_fourcc("DX10")


def art(width, height, translucent, seed=0):
    """A gradient with a distinct top-left pixel; alpha ramps left to right when translucent."""
    image = Image.new("RGBA", (width, height))
    pixels = image.load()
    for y in range(height):
        for x in range(width):
            r = (x * 37 + 5 + seed) & 255
            g = (y * 59 + 9 + seed) & 255
            b = ((x + y) * 13 + 200 + seed) & 255
            a = 255
            if translucent:
                a = min(255, (x * 255) // max(1, width - 1))
                if y == height - 1:
                    a = 0  # a fully transparent bottom row
            pixels[x, y] = (r, g, b, a)
    pixels[0, 0] = (250, 10, 20, pixels[0, 0][3])
    return image


def pack_masked(image, bits, r_mask, g_mask, b_mask, a_mask, luminance=False):
    def place(value8, mask):
        if mask == 0:
            return 0
        shift = (mask & -mask).bit_length() - 1
        width = bin(mask).count("1")
        top = (1 << width) - 1
        quantized = (value8 * top + 127) // 255
        return (quantized << shift) & mask

    data = bytearray()
    for y in range(image.height):
        for x in range(image.width):
            r, g, b, a = image.getpixel((x, y))
            if luminance:
                r = g = b = (r * 299 + g * 587 + b * 114) // 1000
            value = place(r, r_mask) | place(g, g_mask) | place(b, b_mask) | place(a, a_mask)
            data += value.to_bytes(bits // 8, "little")
    return bytes(data)


def replicate(value, bits):
    out, filled = 0, 0
    while filled < 8:
        out = (out << bits) | value
        filled += bits
    return out >> (filled - 8)


def expected_masked(image, r_mask, g_mask, b_mask, a_mask, luminance=False, alpha_only=False):
    """What Patchy should decode: the quantized code expanded by bit replication (n < 8) or
    rounded rescale (n > 8)."""

    def code(value8, mask):
        width = bin(mask).count("1")
        top = (1 << width) - 1
        return (value8 * top + 127) // 255, width

    def expand(value8, mask):
        if mask == 0:
            return None
        c, width = code(value8, mask)
        if width == 8:
            return c
        if width < 8:
            return replicate(c, width)
        top = (1 << width) - 1
        return (c * 255 + top // 2) // top

    out = {}
    for x, y in sample_points(image.width, image.height):
        r, g, b, a = image.getpixel((x, y))
        if luminance:
            lum = (r * 299 + g * 587 + b * 114) // 1000
            value = expand(lum, r_mask)
            rgb = (value, value, value)
        elif alpha_only:
            rgb = (255, 255, 255)
        else:
            rgb = (expand(r, r_mask), expand(g, g_mask), expand(b, b_mask))
        alpha = expand(a, a_mask) if a_mask else 255
        out[(x, y)] = (*rgb, alpha)
    return out


def sample_points(width, height):
    return [(0, 0), (width - 1, 0), (width // 2, height // 2), (0, height - 1), (width - 1, height - 1)]


def pillow_save(image, pixel_format=None):
    buffer = io.BytesIO()
    if pixel_format:
        image.save(buffer, "DDS", pixel_format=pixel_format)
    else:
        image.save(buffer, "DDS")
    return buffer.getvalue()


def pillow_samples(data, points=None, mode="RGBA"):
    with Image.open(io.BytesIO(data)) as decoded:
        decoded.load()
        rgba = decoded.convert(mode)
        points = points or sample_points(rgba.width, rgba.height)
        def as_tuple(value):
            return tuple(value) if isinstance(value, tuple) else (value,)

        return {(x, y): as_tuple(rgba.getpixel((x, y))) for x, y in points}


def write(path, data):
    with open(path, "wb") as handle:
        handle.write(data)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("out_dir")
    parser.add_argument("--texconv", default=None)
    args = parser.parse_args()
    os.makedirs(args.out_dir, exist_ok=True)
    table = []

    def record(name, data, samples, tolerance=0, note="", size=None):
        write(os.path.join(args.out_dir, name), data)
        if size is None:
            with Image.open(io.BytesIO(data)) as decoded:
                size = decoded.size
        table.append({"name": name, "width": size[0], "height": size[1], "tolerance": tolerance,
                      "samples": [{"x": x, "y": y, "rgba": list(v)} for (x, y), v in sorted(samples.items())],
                      "note": note})

    # --- Pillow-written files ---
    translucent = art(9, 7, True)
    opaque = art(16, 16, False)
    translucent16 = art(16, 16, True)

    data = pillow_save(translucent)                       # A8R8G8B8, 32-bit masks
    record("pillow-a8r8g8b8-9x7.dds", data, pillow_samples(data))
    data = pillow_save(art(9, 7, False).convert("RGB"))   # R8G8B8, 24-bit masks
    record("pillow-r8g8b8-9x7.dds", data, pillow_samples(data))
    data = pillow_save(art(8, 8, False).convert("L"))     # Pillow's out-of-range luminance mask
    record("pillow-l8-8x8.dds", data, pillow_samples(data))
    data = pillow_save(art(8, 8, True).convert("LA"))     # and its LA alpha mask
    record("pillow-l8a8-8x8.dds", data, pillow_samples(data))
    data = pillow_save(opaque.convert("RGB"), "DXT1")
    record("pillow-dxt1-opaque-16x16.dds", data, pillow_samples(data), 1)
    data = pillow_save(translucent16, "DXT3")
    record("pillow-dxt3-16x16.dds", data, pillow_samples(data), 1)
    data = pillow_save(translucent16, "DXT5")
    record("pillow-dxt5-16x16.dds", data, pillow_samples(data), 1)
    data = pillow_save(art(8, 8, False).convert("RGB"), "BC5")   # DX10 header, BC5_TYPELESS
    bc5 = pillow_samples(data, mode="RGB")
    record("pillow-bc5-typeless-8x8.dds", data, {k: (v[0], v[1], 0, 255) for k, v in bc5.items()}, 1,
           "Pillow writes DXGI BC5_TYPELESS; blue reads as 0")

    # Premultiplied DXT2 / DXT4: encode premultiplied pixels with Pillow, patch the FourCC.
    premultiplied = Image.new("RGBA", (8, 8))
    straight = art(8, 8, True, seed=3)
    for y in range(8):
        for x in range(8):
            r, g, b, a = straight.getpixel((x, y))
            premultiplied.putpixel((x, y), ((r * a + 127) // 255, (g * a + 127) // 255, (b * a + 127) // 255, a))
    for code, source in (("DXT2", "DXT3"), ("DXT4", "DXT5")):
        data = bytearray(pillow_save(premultiplied, source))
        data[84:88] = code.encode("ascii")
        # Expected: Pillow's decode of the (premultiplied) blocks, un-premultiplied with
        # Patchy's integer rule.
        raw = pillow_samples(bytes(data).replace(code.encode("ascii"), source.encode("ascii"), 1))
        samples = {}
        for key, (r, g, b, a) in raw.items():
            if a == 0:
                samples[key] = (0, 0, 0, 0)
            else:
                samples[key] = tuple(min(255, (c * 255 + a // 2) // a) for c in (r, g, b)) + (a,)
        record(f"synth-{code.lower()}-premultiplied-8x8.dds", bytes(data), samples, 2,
               "premultiplied blocks, FourCC patched; expected = unpremultiply(Pillow decode)", size=(8, 8))

    # --- Masked formats assembled here ---
    masked = [
        ("synth-x8r8g8b8-5x3.dds", DDPF_RGB, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0, False, False),
        ("synth-r5g6b5-5x3.dds", DDPF_RGB, 16, 0xF800, 0x07E0, 0x001F, 0, False, False),
        ("synth-a1r5g5b5-5x3.dds", DDPF_RGB | DDPF_ALPHAPIXELS, 16, 0x7C00, 0x03E0, 0x001F, 0x8000, False, False),
        ("synth-a4r4g4b4-5x3.dds", DDPF_RGB | DDPF_ALPHAPIXELS, 16, 0x0F00, 0x00F0, 0x000F, 0xF000, False, False),
        ("synth-a2r10g10b10-5x3.dds", DDPF_RGB | DDPF_ALPHAPIXELS, 32, 0x3FF00000, 0x000FFC00, 0x000003FF, 0xC0000000,
         False, False),
        ("synth-l8-5x3.dds", DDPF_LUMINANCE, 8, 0xFF, 0, 0, 0, True, False),
        ("synth-a8l8-5x3.dds", DDPF_LUMINANCE | DDPF_ALPHAPIXELS, 16, 0x00FF, 0, 0, 0xFF00, True, False),
        ("synth-a8-5x3.dds", DDPF_ALPHA, 8, 0, 0, 0, 0xFF, False, True),
    ]
    for name, flags, bits, r, g, b, a, luminance, alpha_only in masked:
        image = art(5, 3, a != 0 or alpha_only, seed=7)
        data = header(5, 3, pf_masks(flags, bits, r, g, b, a), flags_extra=DDSD_PITCH, pitch=5 * bits // 8)
        data += pack_masked(image, bits, r, g, b, a, luminance)
        write(os.path.join(args.out_dir, name), data)
        samples = expected_masked(image, r, g, b, a, luminance, alpha_only)
        table.append({"name": name, "width": 5, "height": 3, "tolerance": 0,
                      "samples": [{"x": x, "y": y, "rgba": list(v)} for (x, y), v in sorted(samples.items())],
                      "note": "expected values computed from the quantized codes"})

    # --- DX10 files ---
    image = art(4, 4, True, seed=11)
    rgba_bytes = image.tobytes()
    for name, mode_bits, note in (("synth-dx10-r8g8b8a8-premultiplied-flag-4x4.dds", 2, "alpha mode 2"),
                                  ("synth-dx10-r8g8b8a8-opaque-flag-4x4.dds", 3, "alpha mode 3"),
                                  ("synth-dx10-r8g8b8a8-custom-flag-4x4.dds", 4, "alpha mode 4")):
        source = image
        if mode_bits == 2:
            source = Image.new("RGBA", (4, 4))
            for y in range(4):
                for x in range(4):
                    r, g, b, a = image.getpixel((x, y))
                    source.putpixel((x, y), ((r * a + 127) // 255, (g * a + 127) // 255, (b * a + 127) // 255, a))
        data = header(4, 4, PF_DX10, flags_extra=DDSD_PITCH, pitch=16, dx10=(28, 3, 0, 1, mode_bits)) + source.tobytes()
        samples = {}
        for x, y in sample_points(4, 4):
            r, g, b, a = source.getpixel((x, y))
            if mode_bits == 2:
                samples[(x, y)] = (0, 0, 0, 0) if a == 0 else tuple(min(255, (c * 255 + a // 2) // a) for c in (r, g, b)) + (a,)
            elif mode_bits == 3:
                samples[(x, y)] = (r, g, b, 255)
            else:
                samples[(x, y)] = (r, g, b, a)
        write(os.path.join(args.out_dir, name), data)
        table.append({"name": name, "width": 4, "height": 4, "tolerance": 0,
                      "samples": [{"x": x, "y": y, "rgba": list(v)} for (x, y), v in sorted(samples.items())], "note": note})

    # R16G16B16A16_UNORM: 16-bit codes chosen so the 8-bit rounding is exact for the samples.
    data = header(4, 4, PF_DX10, flags_extra=DDSD_PITCH, pitch=32, dx10=(11, 3, 0, 1, 0))
    samples = {}
    for y in range(4):
        for x in range(4):
            r, g, b, a = image.getpixel((x, y))
            data += struct.pack("<4H", r * 257, g * 257, b * 257, a * 257)
            samples[(x, y)] = (r, g, b, a)
    write(os.path.join(args.out_dir, "synth-dx10-r16g16b16a16-unorm-4x4.dds"), data)
    table.append({"name": "synth-dx10-r16g16b16a16-unorm-4x4.dds", "width": 4, "height": 4, "tolerance": 0,
                  "samples": [{"x": x, "y": y, "rgba": list(v)} for (x, y), v in sorted(samples.items()) if (x, y) in sample_points(4, 4)],
                  "note": "value * 257 round trips exactly"})

    # Float ramp (R16G16B16A16_FLOAT): row 0 walks 0..1, row 1 walks 1..12.5, alpha 1.
    values = []
    for y in range(2):
        for x in range(8):
            v = x / 7.0 if y == 0 else 1.0 + x * (11.5 / 7.0)
            values.append((v, v * 0.5, v * 0.25, 1.0))
    halves = b"".join(struct.pack("<4e", *v) for v in values)
    # The test compares against the tone map of the half-rounded values, not the doubles.
    values = [struct.unpack("<4e", struct.pack("<4e", *v)) for v in values]
    data = header(8, 2, PF_DX10, flags_extra=DDSD_PITCH, pitch=64, dx10=(10, 3, 0, 1, 0)) + halves
    write(os.path.join(args.out_dir, "synth-dx10-r16g16b16a16-float-ramp-8x2.dds"), data)
    table.append({"name": "synth-dx10-r16g16b16a16-float-ramp-8x2.dds", "width": 8, "height": 2, "tolerance": 0,
                  "floats": [list(v) for v in values], "note": "expected = jxr tone map of these floats"})

    # Cubemap: six solid faces, legacy header, all face bits.
    faces = [(255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255), (255, 255, 0, 255), (0, 255, 255, 255), (255, 0, 255, 255)]
    data = header(4, 4, pf_masks(DDPF_RGB | DDPF_ALPHAPIXELS, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000),
                  flags_extra=DDSD_PITCH, pitch=16, caps=DDSCAPS_TEXTURE | DDSCAPS_COMPLEX,
                  caps2=DDSCAPS2_CUBEMAP | DDSCAPS2_ALL_FACES)
    for r, g, b, a in faces:
        data += struct.pack("<4B", b, g, r, a) * 16
    write(os.path.join(args.out_dir, "synth-cubemap-a8r8g8b8-4x4.dds"), data)

    # Volume: 3 slices of 4x4, legacy header.
    data = header(4, 4, pf_masks(DDPF_RGB, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0), flags_extra=DDSD_PITCH | DDSD_DEPTH,
                  pitch=16, depth=3, caps=DDSCAPS_TEXTURE | DDSCAPS_COMPLEX, caps2=DDSCAPS2_VOLUME)
    for slice_index in range(3):
        data += struct.pack("<4B", 10 * (slice_index + 1), 20 * (slice_index + 1), 30 * (slice_index + 1), 0) * 16
    write(os.path.join(args.out_dir, "synth-volume-x8r8g8b8-4x4x3.dds"), data)

    # DX10 array of 3 B8G8R8A8 elements.
    data = header(4, 4, PF_DX10, flags_extra=DDSD_PITCH, pitch=16, dx10=(87, 3, 0, 3, 1))
    for element in range(3):
        data += struct.pack("<4B", 40 * (element + 1), 50 * (element + 1), 60 * (element + 1), 255) * 16
    write(os.path.join(args.out_dir, "synth-dx10-array3-b8g8r8a8-4x4.dds"), data)

    # Mipmapped DXT1: Pillow encodes each level separately; the payloads are concatenated.
    base = art(16, 16, False, seed=5).convert("RGB")
    levels = [base]
    while levels[-1].width > 1 or levels[-1].height > 1:
        levels.append(levels[-1].resize((max(1, levels[-1].width // 2), max(1, levels[-1].height // 2)), Image.BOX))
    payload = b"".join(pillow_save(level, "DXT1")[128:] for level in levels)
    data = header(16, 16, pf_fourcc("DXT1"), flags_extra=DDSD_LINEARSIZE | DDSD_MIPMAPCOUNT, pitch=128, mips=len(levels),
                  caps=DDSCAPS_TEXTURE | DDSCAPS_COMPLEX | DDSCAPS_MIPMAP) + payload
    samples = pillow_samples(pillow_save(base, "DXT1"))
    record("synth-dxt1-mipmapped-16x16.dds", data, samples, 1, f"{len(levels)} levels")

    # --- texconv-written BC4 / BC7 / BC6H ---
    if args.texconv:
        with tempfile.TemporaryDirectory(prefix="patchy-dds-") as scratch:
            png = os.path.join(scratch, "source.png")
            art(16, 16, True, seed=9).save(png)
            gray_png = os.path.join(scratch, "gray.png")
            art(8, 8, False, seed=13).convert("L").save(gray_png)
            for name, fmt, source, extra in (("texconv-bc7-16x16.dds", "BC7_UNORM", png, []),
                                             ("texconv-bc4-8x8.dds", "BC4_UNORM", gray_png, []),
                                             ("texconv-bc6h-8x8.dds", "BC6H_UF16", gray_png, [])):
                out_dir = os.path.join(scratch, fmt)
                os.makedirs(out_dir)
                subprocess.run([args.texconv, "-nologo", "-y", "-m", "1", "-f", fmt, "-o", out_dir, source],
                               check=True, stdout=subprocess.DEVNULL)
                produced = os.path.join(out_dir, os.path.splitext(os.path.basename(source))[0] + ".dds")
                with open(produced, "rb") as handle:
                    data = handle.read()
                if fmt == "BC6H_UF16":
                    with Image.open(io.BytesIO(data)) as decoded:
                        decoded.load()
                        floats = {}
                        for x, y in sample_points(decoded.width, decoded.height):
                            floats[(x, y)] = decoded.getpixel((x, y))
                    write(os.path.join(args.out_dir, name), data)
                    table.append({"name": name, "width": 8, "height": 8, "tolerance": 2,
                                  "float_samples": [{"x": x, "y": y, "rgb": [float(c) for c in v[:3]]} for (x, y), v in sorted(floats.items())],
                                  "note": "Pillow's BC6H decode; expected = tone map"})
                elif fmt == "BC4_UNORM":
                    gray = pillow_samples(data, mode="L")
                    record(name, data, {k: (v[0], v[0], v[0], 255) for k, v in gray.items()}, 1)
                else:
                    record(name, data, pillow_samples(data), 1)

    print(json.dumps(table, indent=1))


if __name__ == "__main__":
    sys.exit(main())
