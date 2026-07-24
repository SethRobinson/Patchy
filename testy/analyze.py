"""Pixel-level comparison of editor renders against the Photoshop ground truth.

All comparisons happen at the document's pixel size, composited over white so
alpha-vs-flattened export differences don't read as color errors. Two metrics
come out of every comparison:

- strict: fraction of pixels whose per-channel difference exceeds 6/255. Loose
  enough to forgive anti-aliasing and rounding noise, but a subtle global color
  shift (color management, rounding a hair over 6/255 on most pixels) marks a
  visually identical render as ~100% different.
- perceptual ("visual"): a pixel is bad only when local SSIM drops below
  SSIM_LOCAL_THRESHOLD or CIEDE2000 deltaE (computed on lightly blurred copies)
  exceeds DELTAE_BAD. SSIM discounts uniform luminance/color offsets; the deltaE
  leg catches flat regions rendered in a flatly wrong color, which SSIM's
  structure term under-penalizes. The blur and the Gaussian SSIM window forgive
  1px anti-aliasing jitter on glyph and shape edges.
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
# Perceptual comparison: a pixel is visually bad when its local structural
# similarity (SSIM's contrast-structure term, min over the three channels) falls
# below this...
SSIM_LOCAL_THRESHOLD = 0.85
SSIM_SIGMA = 1.5
# ...or its CIEDE2000 deltaE, measured on copies blurred by this sigma to kill
# single-pixel anti-aliasing jitter, exceeds this. 6.0 is well above the ~2.3
# just-noticeable difference so global color-management drift stays quiet, while
# a genuinely wrong color (deltaE 15+) still fires.
DELTAE_BLUR_SIGMA = 1.0
DELTAE_BAD = 6.0
# Contrast masking: strong local contrast hides moderate color differences (and a
# sub-pixel-shifted hard edge leaves a blur halo with deltaE in the 6-15 range that
# a human cannot see). The deltaE threshold scales with the local luminance sigma,
# capped at twice the flat-region value so a genuinely wrong color still fires
# even on busy texture.
DELTAE_MASKING_DIVISOR = 50.0
DELTAE_BAD_CAP = 2.0 * DELTAE_BAD


def _gaussian_kernel(sigma: float) -> np.ndarray:
    radius = max(1, int(round(3.0 * sigma)))
    offsets = np.arange(-radius, radius + 1, dtype=np.float32)
    kernel = np.exp(-(offsets * offsets) / (2.0 * sigma * sigma))
    return (kernel / kernel.sum()).astype(np.float32)


def _gaussian_blur(channel: np.ndarray, sigma: float) -> np.ndarray:
    """Separable Gaussian blur of one 2D float32 array (edge-padded, no scipy)."""
    kernel = _gaussian_kernel(sigma)
    radius = len(kernel) // 2
    for axis in (0, 1):
        pad = [(radius, radius) if a == axis else (0, 0) for a in (0, 1)]
        padded = np.pad(channel, pad, mode="edge")
        blurred = np.zeros_like(channel)
        for i, weight in enumerate(kernel):
            if axis == 0:
                blurred += weight * padded[i:i + channel.shape[0], :]
            else:
                blurred += weight * padded[:, i:i + channel.shape[1]]
        channel = blurred
    return channel


def _ssim_map(x: np.ndarray, y: np.ndarray) -> np.ndarray:
    """Local structural-similarity map of two 2D arrays in 0-255 (Gaussian window).

    This is SSIM's contrast-structure term only. The luminance term is deliberately
    dropped: its tiny C1 stabilizer makes a small uniform offset near black read as
    a catastrophic difference (0 vs 8 scores ~0.1), which is exactly the global-shift
    noise this metric exists to forgive. Luminance and color errors are the deltaE
    leg's job; this term answers "is the same structure present here".
    """
    c2 = (0.03 * 255.0) ** 2
    mu_x = _gaussian_blur(x, SSIM_SIGMA)
    mu_y = _gaussian_blur(y, SSIM_SIGMA)
    var_x = _gaussian_blur(x * x, SSIM_SIGMA) - mu_x * mu_x
    var_y = _gaussian_blur(y * y, SSIM_SIGMA) - mu_y * mu_y
    cov = _gaussian_blur(x * y, SSIM_SIGMA) - mu_x * mu_y
    return (2.0 * cov + c2) / (var_x + var_y + c2)


_SRGB_TO_XYZ = np.array(
    [
        [0.4124564, 0.3575761, 0.1804375],
        [0.2126729, 0.7151522, 0.0721750],
        [0.0193339, 0.1191920, 0.9503041],
    ],
    dtype=np.float32,
)
_D65_WHITE = np.array([0.95047, 1.0, 1.08883], dtype=np.float32)


def _srgb_to_lab(rgb: np.ndarray) -> np.ndarray:
    """HxWx3 sRGB in 0-255 -> CIELAB (D65)."""
    c = rgb / np.float32(255.0)
    linear = np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
    xyz = (linear @ _SRGB_TO_XYZ.T) / _D65_WHITE
    epsilon = (6.0 / 29.0) ** 3
    f = np.where(xyz > epsilon, np.cbrt(xyz), xyz / (3.0 * (6.0 / 29.0) ** 2) + 4.0 / 29.0)
    lab = np.empty_like(f)
    lab[..., 0] = 116.0 * f[..., 1] - 16.0
    lab[..., 1] = 500.0 * (f[..., 0] - f[..., 1])
    lab[..., 2] = 200.0 * (f[..., 1] - f[..., 2])
    return lab


def _ciede2000(lab1: np.ndarray, lab2: np.ndarray) -> np.ndarray:
    """Vectorized CIEDE2000 (Sharma et al. 2005) over HxWx3 Lab arrays."""
    l1, a1, b1 = lab1[..., 0], lab1[..., 1], lab1[..., 2]
    l2, a2, b2 = lab2[..., 0], lab2[..., 1], lab2[..., 2]
    pow25_7 = 25.0 ** 7
    c_bar = 0.5 * (np.hypot(a1, b1) + np.hypot(a2, b2))
    c_bar7 = c_bar ** 7
    g = 0.5 * (1.0 - np.sqrt(c_bar7 / (c_bar7 + pow25_7)))
    a1p = (1.0 + g) * a1
    a2p = (1.0 + g) * a2
    c1p = np.hypot(a1p, b1)
    c2p = np.hypot(a2p, b2)
    h1p = np.degrees(np.arctan2(b1, a1p)) % 360.0
    h2p = np.degrees(np.arctan2(b2, a2p)) % 360.0
    zero_chroma = (c1p * c2p) == 0.0

    dh = h2p - h1p
    dh = np.where(dh > 180.0, dh - 360.0, dh)
    dh = np.where(dh < -180.0, dh + 360.0, dh)
    dh = np.where(zero_chroma, 0.0, dh)
    delta_l = l2 - l1
    delta_c = c2p - c1p
    delta_h = 2.0 * np.sqrt(c1p * c2p) * np.sin(np.radians(dh) / 2.0)

    l_bar = 0.5 * (l1 + l2)
    cp_bar = 0.5 * (c1p + c2p)
    h_sum = h1p + h2p
    h_bar = np.where(
        np.abs(h1p - h2p) <= 180.0,
        0.5 * h_sum,
        np.where(h_sum < 360.0, 0.5 * (h_sum + 360.0), 0.5 * (h_sum - 360.0)),
    )
    h_bar = np.where(zero_chroma, h_sum, h_bar)
    t = (
        1.0
        - 0.17 * np.cos(np.radians(h_bar - 30.0))
        + 0.24 * np.cos(np.radians(2.0 * h_bar))
        + 0.32 * np.cos(np.radians(3.0 * h_bar + 6.0))
        - 0.20 * np.cos(np.radians(4.0 * h_bar - 63.0))
    )
    l_term = (l_bar - 50.0) ** 2
    s_l = 1.0 + 0.015 * l_term / np.sqrt(20.0 + l_term)
    s_c = 1.0 + 0.045 * cp_bar
    s_h = 1.0 + 0.015 * cp_bar * t
    cp_bar7 = cp_bar ** 7
    r_t = (
        -np.sin(np.radians(60.0 * np.exp(-(((h_bar - 275.0) / 25.0) ** 2))))
        * 2.0 * np.sqrt(cp_bar7 / (cp_bar7 + pow25_7))
    )
    return np.sqrt(
        np.maximum(
            (delta_l / s_l) ** 2
            + (delta_c / s_c) ** 2
            + (delta_h / s_h) ** 2
            + r_t * (delta_c / s_c) * (delta_h / s_h),
            0.0,
        )
    )


def _perceptual_bad_map(truth: np.ndarray, editor: np.ndarray) -> tuple[np.ndarray, dict]:
    """Boolean map of visually-wrong pixels plus its summary stats.

    Both legs run on lightly blurred copies: a half-pixel edge shift (different
    text rasterizers landing glyph edges differently) leaves a large one-pixel
    discrepancy that would otherwise fail the structure term along every edge.
    """

    def blur(image: np.ndarray) -> np.ndarray:
        return np.stack(
            [_gaussian_blur(image[:, :, c], DELTAE_BLUR_SIGMA) for c in range(3)], axis=2
        )

    truth_soft = blur(truth)
    editor_soft = blur(editor)
    ssim = None
    for channel in range(3):
        channel_map = _ssim_map(truth_soft[:, :, channel], editor_soft[:, :, channel])
        ssim = channel_map if ssim is None else np.minimum(ssim, channel_map)
    delta_e = _ciede2000(_srgb_to_lab(truth_soft), _srgb_to_lab(editor_soft))
    luma = truth_soft.mean(axis=2)
    mu = _gaussian_blur(luma, SSIM_SIGMA)
    sigma_local = np.sqrt(np.maximum(_gaussian_blur(luma * luma, SSIM_SIGMA) - mu * mu, 0.0))
    delta_e_limit = np.minimum(
        DELTAE_BAD * (1.0 + sigma_local / DELTAE_MASKING_DIVISOR), DELTAE_BAD_CAP
    )
    bad = (ssim < SSIM_LOCAL_THRESHOLD) | (delta_e > delta_e_limit)
    stats = {
        "ssimMean": round(float(ssim.mean()), 4),
        "deltaEMean": round(float(delta_e.mean()), 2),
        "deltaEP95": round(float(np.percentile(delta_e, 95)), 2),
    }
    return bad, stats


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
    perceptual_bad, perceptual_stats = _perceptual_bad_map(truth, editor)
    perceptual_fraction = float(perceptual_bad.mean())

    width, height = document_size
    per_object: list[dict] = []
    rendered_ok = 0
    rendered_ok_perceptual = 0
    scored = 0
    for layer in objects:
        if layer.get("group") or not layer.get("visible", True):
            continue
        left, top, right, bottom = layer.get("bounds", [0, 0, 0, 0])
        left, top = max(0, int(left)), max(0, int(top))
        right, bottom = min(width, int(right)), min(height, int(bottom))
        if right - left < 2 or bottom - top < 2:
            continue
        region_bad = float(bad[top:bottom, left:right].mean())
        ok = region_bad <= OBJECT_BAD_FRACTION
        region_perceptual = float(perceptual_bad[top:bottom, left:right].mean())
        perceptual_ok = region_perceptual <= OBJECT_BAD_FRACTION
        scored += 1
        rendered_ok += 1 if ok else 0
        rendered_ok_perceptual += 1 if perceptual_ok else 0
        per_object.append(
            {
                "path": layer.get("path"),
                "name": layer.get("name"),
                "kind": layer.get("kind"),
                "badFraction": round(region_bad, 4),
                "ok": ok,
                "perceptualBadFraction": round(region_perceptual, 4),
                "perceptualOk": perceptual_ok,
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
        "perceptual": {
            "badFraction": round(perceptual_fraction, 5),
            "accuracy": round(max(0.0, 1.0 - perceptual_fraction), 4),
            **perceptual_stats,
        },
        "sizeMismatch": size_mismatch,
        "editorSize": list(editor_native),
        "objectsScored": scored,
        "objectsRenderedOk": rendered_ok,
        "objectsRenderedOkPerceptual": rendered_ok_perceptual,
        "perObject": per_object[:40],
    }


def make_thumbnail(source_png: Path, out_png: Path, max_width: int = 480) -> None:
    image = Image.open(source_png)
    image = image.convert("RGBA")
    if image.width > max_width:
        scale = max_width / image.width
        image = image.resize((max_width, max(1, int(image.height * scale))))
    image.save(out_png)
