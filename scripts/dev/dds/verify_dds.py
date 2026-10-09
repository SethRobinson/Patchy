#!/usr/bin/env python
"""Cross-checks Patchy's DDS writer with Pillow's independent decoders.

    python -I scripts/dev/dds/verify_dds.py build/release/test-artifacts/dds

`dds_writes_inspection_artifacts` (tests/core/dds_tests.cpp) leaves one texture per
compression under test-artifacts/dds beside the core test binary, plus the flattened sources
as BMP files. This script decodes every .dds with Pillow and compares it with the matching
source: exact for the uncompressed files and for BC1's cut-out alpha, a PSNR floor for the
block-compressed colours, and a bounded alpha error for BC3 (20, since blocks mixing
alpha 0 with high alpha spread BC3's eight levels over the whole range). Exit code 0 means every file
passed; the report prints the measured numbers either way. See docs/dds.md.
"""

import math
import os
import sys

from PIL import Image


def psnr(a, b, channels, skip_transparent=None):
    squared = 0.0
    count = 0
    for pa, pb in zip(a, b):
        if skip_transparent is not None and pb[3] < skip_transparent:
            continue
        for c in range(channels):
            d = pa[c] - pb[c]
            squared += d * d
            count += 1
    if count == 0:
        return float("inf")
    mse = squared / count
    return float("inf") if mse == 0 else 10.0 * math.log10(255.0 * 255.0 / mse)


def check(name, condition, detail):
    print(f"  {'ok  ' if condition else 'FAIL'} {name}: {detail}")
    return condition


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    folder = sys.argv[1]
    sources = {}
    for stem in ("translucent", "opaque"):
        with Image.open(os.path.join(folder, f"source-{stem}.bmp")) as source:
            sources[stem] = source.convert("RGBA")
    ok = True
    for name in sorted(os.listdir(folder)):
        if not name.endswith(".dds"):
            continue
        path = os.path.join(folder, name)
        with Image.open(path) as decoded:
            decoded.load()
            mips = decoded.info.get("mipmaps", None)
            rgba = decoded.convert("RGBA")
        source = sources["translucent" if "translucent" in name or "cutout" in name else "opaque"]
        if rgba.size != source.size:
            ok = check(name, False, f"size {rgba.size} != {source.size}") and ok
            continue
        got = list(rgba.getdata())
        want = list(source.getdata())
        if name.startswith("uncompressed"):
            ok = check(name, got == want, "pixels identical" if got == want else "pixels differ") and ok
        elif name.startswith("bc1"):
            colour = psnr(got, want, 3, skip_transparent=128)
            alpha_ok = all((g[3] == 255) == (w[3] >= 128) and g[3] in (0, 255) for g, w in zip(got, want))
            ok = check(name, colour >= 30.0 and alpha_ok, f"colour PSNR {colour:.1f} dB, cut-out alpha exact: {alpha_ok}") and ok
        elif name.startswith("bc7"):
            colour = psnr(got, want, 3, skip_transparent=1)
            alpha_error = max(abs(g[3] - w[3]) for g, w in zip(got, want))
            # Blocks mixing the transparent bottom row with high alpha land within about 14.
            ok = check(name, colour >= 35.0 and alpha_error <= 16, f"colour PSNR {colour:.1f} dB, alpha max error {alpha_error}") and ok
        elif name.startswith("bc4"):
            # Pillow decodes ATI1 as a gray image; compare with the source luminance.
            gray_error = max(abs(g[0] - ((w[0] * 299 + w[1] * 587 + w[2] * 114 + 500) // 1000)) for g, w in zip(got, want))
            ok = check(name, gray_error <= 3, f"gray max error {gray_error}") and ok
        elif name.startswith("bc5"):
            rg_error = max(max(abs(g[0] - w[0]), abs(g[1] - w[1])) for g, w in zip(got, want))
            blue_zero = all(g[2] == 0 for g in got)
            ok = check(name, rg_error <= 3 and blue_zero, f"red/green max error {rg_error}, blue zero: {blue_zero}") and ok
        elif name.startswith("bc3"):
            colour = psnr(got, want, 3, skip_transparent=1)
            alpha_error = max(abs(g[3] - w[3]) for g, w in zip(got, want))
            # Blocks that mix the transparent bottom row with high alpha spread BC3's 8 levels
            # over the whole range, so the bound is 20 there (a smooth ramp stays within 4).
            ok = check(name, colour >= 30.0 and alpha_error <= 20, f"colour PSNR {colour:.1f} dB, alpha max error {alpha_error}") and ok
        else:
            ok = check(name, False, "unexpected artifact name") and ok
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
