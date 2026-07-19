"""Pixel-level comparison of editor renders against the Photoshop ground truth.

All comparisons happen at the document's pixel size, composited over white so
alpha-vs-flattened export differences don't read as color errors. Tolerances are
deliberately loose enough (6/255) to forgive anti-aliasing and rounding noise
while still catching real rendering divergence.
"""

from __future__ import annotations

import math
from pathlib import Path

import numpy as np
from PIL import Image

# Per-channel difference below this is treated as AA/rounding noise.
PIXEL_TOLERANCE = 6
# An object region whose bad-pixel fraction stays under this renders "correctly".
# Text layers legitimately differ on every glyph edge (10-20% of their bbox) even when
# rendered correctly, so this budget is set to catch missing/misplaced/wrong objects
# (those blow past 50%) rather than anti-aliasing jitter.
OBJECT_BAD_FRACTION = 0.25
# Sentinel (flat-composite cheat) detection: how close to pure magenta counts.
SENTINEL_TOLERANCE = 12


def _load_over_white(path: Path, size: tuple[int, int] | None) -> tuple[np.ndarray, tuple[int, int]]:
    image = Image.open(path)
    native_size = image.size
    image = image.convert("RGBA")
    if size is not None and image.size != size:
        image = image.resize(size, Image.BILINEAR)
    rgba = np.asarray(image, dtype=np.float32)
    alpha = rgba[:, :, 3:4] / 255.0
    rgb = rgba[:, :, :3] * alpha + 255.0 * (1.0 - alpha)
    return rgb, native_size


def sentinel_fraction(render_png: Path) -> float:
    """Fraction of pixels that are (near-)pure magenta - the trap composite color."""
    rgb, _ = _load_over_white(render_png, None)
    r, g, b = rgb[:, :, 0], rgb[:, :, 1], rgb[:, :, 2]
    hits = (
        (np.abs(r - 255.0) <= SENTINEL_TOLERANCE)
        & (g <= SENTINEL_TOLERANCE)
        & (np.abs(b - 255.0) <= SENTINEL_TOLERANCE)
    )
    return float(hits.mean())


def compare_renders(
    truth_png: Path,
    editor_png: Path,
    document_size: tuple[int, int],
    objects: list[dict],
    heatmap_out: Path | None = None,
) -> dict:
    """Global + per-object render metrics. `objects` is the manifest layer list."""
    truth, _ = _load_over_white(truth_png, document_size)
    editor, editor_native = _load_over_white(editor_png, document_size)
    size_mismatch = tuple(editor_native) != tuple(document_size)

    diff = np.abs(truth - editor)
    max_channel_diff = diff.max(axis=2)
    bad = max_channel_diff > PIXEL_TOLERANCE
    rmse = float(math.sqrt(float((diff**2).mean())))
    bad_fraction = float(bad.mean())

    width, height = document_size
    per_object: list[dict] = []
    rendered_ok = 0
    scored = 0
    for layer in objects:
        if layer.get("group") or not layer.get("visible", True):
            continue
        left, top, right, bottom = layer.get("bounds", [0, 0, 0, 0])
        left, top = max(0, int(left)), max(0, int(top))
        right, bottom = min(width, int(right)), min(height, int(bottom))
        if right - left < 2 or bottom - top < 2:
            continue
        region = bad[top:bottom, left:right]
        region_bad = float(region.mean())
        ok = region_bad <= OBJECT_BAD_FRACTION
        scored += 1
        rendered_ok += 1 if ok else 0
        per_object.append(
            {
                "path": layer.get("path"),
                "name": layer.get("name"),
                "kind": layer.get("kind"),
                "badFraction": round(region_bad, 4),
                "ok": ok,
            }
        )
    per_object.sort(key=lambda o: -o["badFraction"])

    if heatmap_out is not None:
        # Amplified difference at full document resolution: the report shows it small,
        # but clicking through opens it full size for close inspection.
        heat = np.clip(max_channel_diff * 4.0, 0, 255).astype(np.uint8)
        Image.fromarray(heat, mode="L").save(heatmap_out)

    accuracy = max(0.0, 1.0 - bad_fraction)
    return {
        "rmse": round(rmse, 3),
        "badFraction": round(bad_fraction, 5),
        "accuracy": round(accuracy, 4),
        "sizeMismatch": size_mismatch,
        "editorSize": list(editor_native),
        "objectsScored": scored,
        "objectsRenderedOk": rendered_ok,
        "perObject": per_object[:40],
    }


def make_thumbnail(source_png: Path, out_png: Path, max_width: int = 480) -> None:
    image = Image.open(source_png)
    image = image.convert("RGBA")
    if image.width > max_width:
        scale = max_width / image.width
        image = image.resize((max_width, max(1, int(image.height * scale))))
    image.save(out_png)
