"""Per-tile differences between Patchy's and GIMP's renders of a modes fixture (docs/xcf.md).

Usage: python -I scripts/dev/xcf/tile_report.py PATCHY.png GIMP.png MODE[,MODE...] [tile] [columns]
MODE names are the fixture's tile order (make_xcf_fixtures.py DEFAULT_MODES or LEGACY_MODES:
pass "default" or "legacy"). Prints mean and worst difference per tile on premultiplied RGBA.
"""

import sys

import numpy as np
from PIL import Image

sys.path.insert(0, __file__.rsplit("\\", 1)[0].rsplit("/", 1)[0])
from make_xcf_fixtures import DEFAULT_MODES, LEGACY_MODES  # noqa: E402


def premultiplied(path):
    image = np.asarray(Image.open(path).convert("RGBA")).astype(np.float64)
    rgb = image[..., :3] * image[..., 3:4] / 255.0
    return np.concatenate([rgb, image[..., 3:4]], axis=2)


def main():
    patchy = premultiplied(sys.argv[1])
    gimp = premultiplied(sys.argv[2])
    modes = {"default": DEFAULT_MODES, "legacy": LEGACY_MODES}.get(sys.argv[3], sys.argv[3].split(","))
    tile = int(sys.argv[4]) if len(sys.argv) > 4 else 32
    columns = int(sys.argv[5]) if len(sys.argv) > 5 else 8
    rows = []
    for index, mode in enumerate(modes):
        x0 = (index % columns) * tile
        y0 = (index // columns) * tile
        a = patchy[y0:y0 + tile, x0:x0 + tile]
        b = gimp[y0:y0 + tile, x0:x0 + tile]
        diff = np.abs(a - b)
        # The opaque core of the tile (alpha 1 there) separates blend math from edge compositing.
        core = diff[8:tile - 8, 8:tile - 8]
        rows.append((mode, diff.mean(), diff.max(), core.mean(), core.max()))
    print(f"{'mode':24} {'mean':>7} {'worst':>6} {'core mean':>10} {'core worst':>10}")
    for mode, mean, worst, core_mean, core_worst in sorted(rows, key=lambda r: -r[1]):
        print(f"{mode:24} {mean:7.2f} {worst:6.0f} {core_mean:10.2f} {core_worst:10.0f}")


if __name__ == "__main__":
    main()
