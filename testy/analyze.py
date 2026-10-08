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

The strict metric runs at document resolution. The perceptual one runs on copies
area-averaged down to PERCEPTUAL_MAX_PIXELS, and is skipped outright when the two
renders match pixel for pixel; `python testy\\analyze.py --selftest` covers both.
"""

from __future__ import annotations

import math
from pathlib import Path

import numpy as np
from PIL import Image

# These are the run's own renders of the corpus, not untrusted input, and a PSB
# composite legitimately passes Pillow's 89 MP decompression-bomb ceiling.
Image.MAX_IMAGE_PIXELS = None

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
# The perceptual legs answer "would a human see something wrong", which does not
# need document resolution: both already run on blurred copies to forgive
# sub-pixel jitter, and nobody views an 18000px banner at 1:1. They cost about a
# second and 150 MB of numpy temporaries per megapixel: one comparison of an
# 18000x3508 banner took 66s and 12.3 GB, and 10.9s and 3.5 GB within this budget
# (most of what is left is decoding two 63 MP PNGs). Above the budget both
# renders are area-averaged down to it first; the median corpus file (0.1 MP) is
# untouched. The strict metric stays at document resolution - it is the
# byte-honest one, and it costs 1.5s on that same banner.
PERCEPTUAL_MAX_PIXELS = 4_000_000


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


def _perceptual_scale(shape: tuple[int, ...]) -> float:
    """Linear factor bringing an image within PERCEPTUAL_MAX_PIXELS (1.0 = as-is)."""
    pixels = shape[0] * shape[1]
    if pixels <= PERCEPTUAL_MAX_PIXELS:
        return 1.0
    return math.sqrt(PERCEPTUAL_MAX_PIXELS / pixels)


def _downsample(image: np.ndarray, scale: float) -> np.ndarray:
    """Area-average an HxWx3 float array down by `scale`, one channel at a time.

    BOX (not bilinear) is the point: it keeps each block's mean colour, so a
    small solid defect survives as a colour shift over the pixels that remain
    instead of being smeared into its background. Resampling happens in "F"
    mode, so compositing's fractional values never take a trip through uint8.
    """
    height, width = image.shape[:2]
    size = (max(1, int(width * scale)), max(1, int(height * scale)))
    planes = [
        np.asarray(
            Image.fromarray(np.ascontiguousarray(image[:, :, channel]), mode="F")
            .resize(size, Image.BOX),
            dtype=np.float32,
        )
        for channel in range(image.shape[2])
    ]
    return np.stack(planes, axis=2)


def _region_mean(bad_map: np.ndarray, bounds: tuple[int, int, int, int], scale: float) -> float:
    """Mean of a document-resolution rectangle inside a map computed at `scale`."""
    left, top, right, bottom = bounds
    if scale != 1.0:
        height, width = bad_map.shape[:2]
        left = min(width - 1, int(left * scale))
        top = min(height - 1, int(top * scale))
        # At least one pixel each way: an empty slice would make mean() NaN.
        right = min(width, max(left + 1, int(round(right * scale))))
        bottom = min(height, max(top + 1, int(round(bottom * scale))))
    return float(bad_map[top:bottom, left:right].mean())


def has_embedded_profile(path: Path) -> bool:
    """True when the PNG carries an ICC profile (Affinity and psd-tools write one)."""
    try:
        with Image.open(path) as image:
            return bool(image.info.get("icc_profile"))
    except OSError:
        return False


def load_srgb_rgba(path: Path) -> Image.Image:
    """The image as 8-bit sRGB RGBA. An embedded ICC profile is honored: its pixel
    values are in that profile's space, and comparing them raw against Photoshop's
    sRGB reference scores a correctly tagged render as wrong. 16-bit gray PNGs are
    scaled down first (Pillow's own conversion clips them to white)."""
    image = Image.open(path)
    if image.mode.startswith("I;16") or image.mode == "I":
        samples = np.asarray(image, dtype=np.float64)
        image = Image.fromarray(np.clip(np.rint(samples / 257.0), 0, 255).astype(np.uint8), "L")
        return image.convert("RGBA")
    profile = image.info.get("icc_profile")
    if profile and image.mode in ("L", "LA", "RGB", "RGBA"):
        try:
            from io import BytesIO

            from PIL import ImageCms

            alpha = image.getchannel("A") if "A" in image.getbands() else None
            base = image.convert("L" if image.mode in ("L", "LA") else "RGB")
            converted = ImageCms.profileToProfile(
                base, ImageCms.ImageCmsProfile(BytesIO(profile)), ImageCms.createProfile("sRGB"),
                renderingIntent=ImageCms.Intent.RELATIVE_COLORIMETRIC, outputMode="RGB")
            if converted is not None:
                image = converted
                if alpha is not None:
                    image.putalpha(alpha)
        except Exception:
            pass  # an unusable profile: fall back to the raw values
    return image.convert("RGBA")


# A pixel "changed" between two renders past this per-channel difference.
RENDER_CHANGE_TOLERANCE = 12


def _rgb_over_white(image: Image.Image) -> np.ndarray:
    rgba = np.asarray(image, dtype=np.float32)
    alpha = rgba[:, :, 3:4] / 255.0
    return rgba[:, :, :3] * alpha + 255.0 * (1.0 - alpha)


def _clipped_box(bounds: list[int], size: tuple[int, int]) -> tuple[int, int, int, int] | None:
    left, top = max(0, int(bounds[0])), max(0, int(bounds[1]))
    right, bottom = min(size[0], int(bounds[2])), min(size[1], int(bounds[3]))
    return (left, top, right, bottom) if right > left and bottom > top else None


def _changed_map(first_png: Path, second_png: Path) -> np.ndarray | None:
    """Per pixel: do two renders of one document differ visibly? None when their
    sizes differ (they are then not two renders of one document)."""
    first = load_srgb_rgba(first_png)
    second = load_srgb_rgba(second_png)
    if second.size != first.size:
        return None
    return (np.abs(_rgb_over_white(first) - _rgb_over_white(second)).max(axis=2)
            > RENDER_CHANGE_TOLERANCE)


def boxes_changed(first_png: Path, second_png: Path, boxes: list[list[int]]) -> list[float] | None:
    """Per box ([left, top, right, bottom], document pixels): the fraction of its
    pixels that differ between two renders of one document, -1.0 for a box off the
    canvas. None when the renders are not the same size."""
    changed = _changed_map(first_png, second_png)
    if changed is None:
        return None
    size = (changed.shape[1], changed.shape[0])
    fractions = []
    for bounds in boxes:
        box = _clipped_box(bounds, size)
        fractions.append(-1.0 if box is None
                         else float(changed[box[1]:box[3], box[0]:box[2]].mean()))
    return fractions


def changed_outside_boxes(first_png: Path, second_png: Path, boxes: list[list[int]]) -> float | None:
    """The fraction of pixels OUTSIDE every box that differ between two renders of one
    document. Each box is first grown by a quarter of its size plus 8 pixels, the room
    an effect or an editor's own text metrics may take. 0.0 when the grown boxes cover
    the canvas; None when the renders are not the same size."""
    changed = _changed_map(first_png, second_png)
    if changed is None:
        return None
    height, width = changed.shape
    outside = np.ones(changed.shape, dtype=bool)
    for bounds in boxes:
        grow_x = (int(bounds[2]) - int(bounds[0])) // 4 + 8
        grow_y = (int(bounds[3]) - int(bounds[1])) // 4 + 8
        box = _clipped_box([bounds[0] - grow_x, bounds[1] - grow_y, bounds[2] + grow_x, bounds[3] + grow_y],
                           (width, height))
        if box is not None:
            outside[box[1]:box[3], box[0]:box[2]] = False
    if not outside.any():
        return 0.0
    return float(changed[outside].mean())


def compose_scored_render(output_png: Path, base_png: Path, placeholders: list[tuple[list[int], str]],
                          restore_from_png: Path | None = None,
                          restore_boxes: list[list[int]] | None = None,
                          clear_placeholders: bool = False) -> None:
    """Write the render that gets scored: `base_png` (the editor's render of the file
    with its caches removed), with each placeholder box outlined and labeled, and each
    restore box copied back from `restore_from_png` (the editor's render as opened).
    `clear_placeholders` empties each placeholder box first, for a base that still
    shows cached pixels there."""
    from PIL import ImageDraw

    scored = load_srgb_rgba(base_png)
    if restore_from_png is not None and restore_boxes:
        opened = load_srgb_rgba(restore_from_png)
        if opened.size != scored.size:
            opened = opened.resize(scored.size, Image.NEAREST)
        for bounds in restore_boxes:
            box = _clipped_box(bounds, scored.size)
            if box is not None:
                scored.paste(opened.crop(box), (box[0], box[1]))
    draw = ImageDraw.Draw(scored)
    for bounds, label in placeholders:
        box = _clipped_box(bounds, scored.size)
        if box is None:
            continue
        left, top, right, bottom = box
        if clear_placeholders:
            scored.paste((0, 0, 0, 0), box)
        draw.rectangle((left, top, right - 1, bottom - 1), outline=(200, 0, 0, 255), width=1)
        label_box = draw.textbbox((0, 0), label)
        label_width, label_height = label_box[2] - label_box[0], label_box[3] - label_box[1]
        if right - left >= label_width + 6 and bottom - top >= label_height + 6:
            draw.rectangle((left + 1, top + 1, left + label_width + 5, top + label_height + 5),
                           fill=(255, 255, 255, 255))
            draw.text((left + 3, top + 2), label, fill=(200, 0, 0, 255))
    scored.save(output_png)


# ---------------------------------------------------------------------------
# Deep precision (16-bit files)
#
# The metrics above work on 8-bit sRGB, where an editor that edits a 16-bit file in
# 8 bits loses nothing visible. For 16-bit files Photoshop also saves a 16-bit
# reference render (render16.png), and this comparison measures at that precision.
# An 8-bit render is compared too (scaled by 257): its rounding is exactly the loss
# being measured. Pillow reads 16-bit RGB(A) PNGs as 8-bit, so they are decoded here.
# ---------------------------------------------------------------------------

# A pixel is imprecise when a channel differs by more than this many 16-bit levels
# over white: a quarter of one 8-bit step. An 8-bit render of a smooth 16-bit image
# misses it on most pixels (7 in 8 for three independent channels); Photoshop's own
# 15-bit internal math, or a float pipeline rounding back to 16 bits, stays within a
# few levels.
DEEP_TOLERANCE = 64


def _png_chunks(data: bytes):
    import struct

    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    offset = 8
    while offset + 8 <= len(data):
        length, kind = struct.unpack(">I4s", data[offset:offset + 8])
        yield kind, data[offset + 8:offset + 8 + length]
        offset += 12 + length


def png_bit_depth(path: Path) -> int | None:
    """Bits per sample from the IHDR chunk (8 or 16 for the renders here)."""
    try:
        with open(path, "rb") as handle:
            head = handle.read(33)
        if len(head) < 33 or head[12:16] != b"IHDR":
            return None
        return head[24]
    except OSError:
        return None


def read_png16_rgba(path: Path) -> np.ndarray:
    """A 16-bit gray/gray+alpha/RGB/RGBA PNG as float64 RGBA in 0..1. Embedded
    profiles are ignored: every 16-bit render here is sRGB. OpenCV decodes it when
    installed (optional; much faster on large renders), else the decoder below."""
    try:
        import cv2
    except ImportError:
        cv2 = None
    if cv2 is not None:
        decoded = cv2.imread(str(path), cv2.IMREAD_UNCHANGED)
        if decoded is not None and decoded.dtype == np.uint16:
            values = decoded.astype(np.float64) / 65535.0
            if values.ndim == 2:
                values = values[:, :, None]
            channels = values.shape[2]
            height, width = values.shape[:2]
            if channels == 1:
                return np.concatenate([np.repeat(values, 3, axis=2), np.ones((height, width, 1))], axis=2)
            if channels == 2:
                return np.concatenate([np.repeat(values[:, :, :1], 3, axis=2), values[:, :, 1:2]], axis=2)
            if channels == 3:
                return np.concatenate([values[:, :, ::-1], np.ones((height, width, 1))], axis=2)
            return np.concatenate([values[:, :, 2::-1], values[:, :, 3:4]], axis=2)
    return _decode_png16_rgba(path)


def _decode_png16_rgba(path: Path) -> np.ndarray:
    """The dependency-free decoder: non-interlaced 16-bit PNGs, every filter type."""
    import zlib

    data = Path(path).read_bytes()
    header = None
    compressed = bytearray()
    for kind, body in _png_chunks(data):
        if kind == b"IHDR":
            header = body
        elif kind == b"IDAT":
            compressed += body
        elif kind == b"IEND":
            break
    if header is None:
        raise ValueError(f"{path}: no IHDR")
    width = int.from_bytes(header[0:4], "big")
    height = int.from_bytes(header[4:8], "big")
    depth, color_type, interlace = header[8], header[9], header[12]
    channels = {0: 1, 2: 3, 4: 2, 6: 4}.get(color_type)
    if depth != 16 or channels is None or interlace != 0:
        raise ValueError(f"{path}: not a plain 16-bit PNG (depth {depth}, type {color_type})")
    bpp = 2 * channels
    stride = width * bpp
    raw = np.frombuffer(zlib.decompress(bytes(compressed)), dtype=np.uint8)
    rows = raw.reshape(height, stride + 1)
    out = np.zeros((height, stride), dtype=np.uint8)
    previous = np.zeros((width, bpp), dtype=np.int32)
    for y in range(height):
        kind = int(rows[y, 0])
        line = rows[y, 1:].astype(np.int32).reshape(width, bpp)
        if kind == 0:
            current = line
        elif kind == 1:
            # Sub: each byte adds the reconstructed byte one pixel left, which is a
            # running sum per byte lane.
            current = np.cumsum(line, axis=0) & 0xFF
        elif kind == 2:
            current = (line + previous) & 0xFF
        elif kind in (3, 4):
            # Average and Paeth depend on the reconstructed left neighbour, so they
            # run pixel by pixel (vectorized across the pixel's bytes).
            current = np.zeros((width, bpp), dtype=np.int32)
            left = np.zeros(bpp, dtype=np.int32)
            upper_left = np.zeros(bpp, dtype=np.int32)
            for x in range(width):
                up = previous[x]
                if kind == 3:
                    predictor = (left + up) >> 1
                else:
                    estimate = left + up - upper_left
                    pa = np.abs(estimate - left)
                    pb = np.abs(estimate - up)
                    pc = np.abs(estimate - upper_left)
                    predictor = np.where((pa <= pb) & (pa <= pc), left,
                                         np.where(pb <= pc, up, upper_left))
                left = (line[x] + predictor) & 0xFF
                current[x] = left
                upper_left = up
        else:
            raise ValueError(f"{path}: bad PNG filter {kind}")
        out[y] = current.reshape(stride)
        previous = current
    samples = out.reshape(height, width * channels, 2)
    values = (samples[:, :, 0].astype(np.float64) * 256.0 + samples[:, :, 1]) / 65535.0
    values = values.reshape(height, width, channels)
    if channels == 1:
        return np.concatenate([np.repeat(values, 3, axis=2), np.ones((height, width, 1))], axis=2)
    if channels == 2:
        return np.concatenate([np.repeat(values[:, :, :1], 3, axis=2), values[:, :, 1:2]], axis=2)
    if channels == 3:
        return np.concatenate([values, np.ones((height, width, 1))], axis=2)
    return values


def write_png16_rgb(path: Path, rgb: np.ndarray) -> None:
    """A 16-bit RGB PNG from integer samples 0..65535 (self-tests and fixtures)."""
    import struct
    import zlib

    samples = np.clip(np.rint(rgb), 0, 65535).astype(">u2")
    height, width = samples.shape[:2]
    rows = b"".join(b"\x00" + samples[y].tobytes() for y in range(height))

    def chunk(kind: bytes, body: bytes) -> bytes:
        return (struct.pack(">I", len(body)) + kind + body
                + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF))

    header = struct.pack(">IIBBBBB", width, height, 16, 2, 0, 0, 0)
    Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
                           + chunk(b"IDAT", zlib.compress(rows, 6)) + chunk(b"IEND", b""))


def split_deep_png(path: Path) -> Path | None:
    """An editor's 16-bit PNG render becomes two files: the 16-bit original moves to
    `<stem>16.png` beside it (what the 16-bit precision metric reads) and `path` is
    rewritten as its 8-bit narrowing, round(v / 257), so every 8-bit metric sees the
    same kind of file as before. Returns the 16-bit path, or None for an 8-bit PNG."""
    path = Path(path)
    if png_bit_depth(path) != 16:
        return None
    rgba = read_png16_rgba(path)
    deep = path.with_name(path.stem + "16.png")
    deep.unlink(missing_ok=True)
    path.replace(deep)
    narrow = np.clip(np.rint(rgba * 255.0), 0, 255).astype(np.uint8)
    Image.fromarray(narrow, "RGBA").save(path)
    return deep


def _load_deep_over_white(path: Path, size: tuple[int, int]) -> tuple[np.ndarray, int, tuple[int, int]]:
    """RGB over white in 16-bit levels (float64 0..65535), the PNG's bit depth, and its
    native size. A size mismatch is resized like the 8-bit comparison does."""
    bits = png_bit_depth(path) or 8
    if bits == 16:
        rgba = read_png16_rgba(path)
        native = (rgba.shape[1], rgba.shape[0])
        if native != tuple(size):
            image = Image.fromarray(np.clip(np.rint(rgba * 255.0), 0, 255).astype(np.uint8), "RGBA")
            rgba = np.asarray(image.resize(size, Image.BILINEAR), dtype=np.float64) / 255.0
    else:
        image = load_srgb_rgba(path)
        native = image.size
        if image.size != tuple(size):
            image = image.resize(size, Image.BILINEAR)
        rgba = np.asarray(image, dtype=np.float64) / 255.0
    alpha = rgba[:, :, 3:4]
    rgb = rgba[:, :, :3] * alpha + (1.0 - alpha)
    return rgb * 65535.0, bits, native


def compare_deep_renders(truth_png: Path, editor_png: Path, document_size: tuple[int, int]) -> dict:
    """Precision of `editor_png` against a 16-bit reference: RMSE and worst error in
    8-bit steps (one step = 257 levels), and the fraction of pixels whose worst channel
    is more than DEEP_TOLERANCE 16-bit levels off."""
    truth, truth_bits, _ = _load_deep_over_white(truth_png, document_size)
    if truth_bits != 16:
        return {"state": "not measured", "reason": "the reference render is not 16-bit"}
    editor, editor_bits, editor_native = _load_deep_over_white(editor_png, document_size)
    diff = np.abs(truth - editor)
    worst = diff.max(axis=2)
    bad_fraction = float((worst > DEEP_TOLERANCE).mean())
    return {
        "state": "done",
        "editorBits": editor_bits,
        "rmse": round(float(math.sqrt(float((diff ** 2).mean()))) / 257.0, 4),
        "maxError": round(float(worst.max()) / 257.0, 3),
        "badFraction": round(bad_fraction, 5),
        "accuracy": round(max(0.0, 1.0 - bad_fraction), 4),
        "tolerance": DEEP_TOLERANCE,
        "sizeMismatch": tuple(editor_native) != tuple(document_size),
    }


def _load_over_white(path: Path, size: tuple[int, int] | None) -> tuple[np.ndarray, tuple[int, int]]:
    image = load_srgb_rgba(path)
    native_size = image.size
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

    # Identical pixels cannot look different, so say so instead of spending a
    # minute proving it. This is the Photoshop column's normal case: its cell
    # render and the ground-truth render come from two probes of the same file
    # and match to the byte.
    identical = not bool(max_channel_diff.any())
    perceptual_scale = 1.0
    if identical:
        perceptual_bad = np.zeros((1, 1), dtype=bool)
        perceptual_stats = {"ssimMean": 1.0, "deltaEMean": 0.0, "deltaEP95": 0.0}
    else:
        perceptual_scale = _perceptual_scale(truth.shape)
        if perceptual_scale < 1.0:
            perceptual_bad, perceptual_stats = _perceptual_bad_map(
                _downsample(truth, perceptual_scale), _downsample(editor, perceptual_scale)
            )
        else:
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
        region_perceptual = 0.0 if identical else _region_mean(
            perceptual_bad, (left, top, right, bottom), perceptual_scale)
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


# ---------------------------------------------------------------------------
# Self-test: `python testy\analyze.py --selftest`
#
# Synthetic render pairs written to a temp directory - no Photoshop, no corpus -
# pin the two properties the metric exists for (a global color shift is not a
# visual difference; a wrongly rendered object is) and the two shortcuts that
# make it affordable on large documents.
# ---------------------------------------------------------------------------

_SELFTEST_SIZE = (1600, 900)
_DEFECT = (900, 400, 12, 4)  # left, top, width, height


def _selftest_canvas(size: tuple[int, int]) -> np.ndarray:
    """A smooth, deterministic texture: SSIM needs structure to judge."""
    width, height = size
    yy, xx = np.mgrid[0:height, 0:width].astype(np.float32)
    return np.stack(
        [
            128.0 + 100.0 * np.sin(xx / 40.0),
            128.0 + 100.0 * np.cos(yy / 30.0),
            128.0 + 80.0 * np.sin((xx + yy) / 50.0),
        ],
        axis=2,
    ).astype(np.float32)


def _selftest() -> int:
    import tempfile
    import time

    global _perceptual_bad_map, PERCEPTUAL_MAX_PIXELS

    failures: list[str] = []

    def check(condition: bool, description: str) -> None:
        print(("  ok   " if condition else "  FAIL ") + description)
        if not condition:
            failures.append(description)

    width, height = _SELFTEST_SIZE
    left, top, defect_w, defect_h = _DEFECT
    # One object covering the defect, one clean object elsewhere.
    objects = [
        {"path": "0", "name": "defect", "kind": "SMARTOBJECT", "visible": True,
         "bounds": [left, top, left + defect_w, top + defect_h]},
        {"path": "1", "name": "clean", "kind": "NORMAL", "visible": True,
         "bounds": [100, 100, 500, 500]},
    ]

    with tempfile.TemporaryDirectory() as work:
        work_dir = Path(work)

        def write(name: str, array: np.ndarray) -> Path:
            path = work_dir / name
            Image.fromarray(np.clip(array, 0, 255).astype(np.uint8)).save(path)
            return path

        base = _selftest_canvas(_SELFTEST_SIZE)
        truth_png = write("truth.png", base)
        same_png = write("same.png", base)
        defect = base.copy()
        defect[top:top + defect_h, left:left + defect_w, :] = [220.0, 40.0, 40.0]
        defect_png = write("defect.png", defect)
        shifted_png = write("shifted.png", base + 8.0)

        print("1. identical renders skip the perceptual pass entirely")
        saved = _perceptual_bad_map

        def refuse(*_args: object) -> tuple:
            raise AssertionError("the perceptual pass ran on identical renders")

        _perceptual_bad_map = refuse
        try:
            started = time.perf_counter()
            result = compare_renders(truth_png, same_png, _SELFTEST_SIZE, objects)
            elapsed = time.perf_counter() - started
        except AssertionError as error:
            result, elapsed = {"perceptual": {}}, 0.0
            check(False, str(error))
        finally:
            _perceptual_bad_map = saved
        check(result["badFraction"] == 0.0, "strict reports no difference")
        check(result["perceptual"]["badFraction"] == 0.0 and
              result["perceptual"]["ssimMean"] == 1.0,
              f"perceptual reports a perfect match ({elapsed * 1000:.0f}ms)")
        check(all(o["perceptualOk"] for o in result["perObject"]), "every object scores ok")

        print("2. a uniform 8/255 shift is a byte difference, not a perceptual one")
        result = compare_renders(truth_png, shifted_png, _SELFTEST_SIZE, objects)
        check(result["badFraction"] > 0.9,
              f"byte match calls it {result['badFraction'] * 100:.0f}% different")
        check(result["perceptual"]["badFraction"] < 0.01,
              f"perceptual calls it {result['perceptual']['badFraction'] * 100:.2f}% different")

        print(f"3. a {defect_w}x{defect_h}px defect survives an aggressive cap")
        full = compare_renders(truth_png, defect_png, _SELFTEST_SIZE, objects)
        check(_perceptual_scale((height, width)) == 1.0,
              "an image under the budget is not resampled at all")
        defect_object = next(o for o in full["perObject"] if o["name"] == "defect")
        check(not defect_object["perceptualOk"],
              f"uncapped: the defect object flags ({defect_object['perceptualBadFraction'] * 100:.0f}%)")

        original_cap = PERCEPTUAL_MAX_PIXELS
        PERCEPTUAL_MAX_PIXELS = (width * height) // 16  # scale 0.25, as a 63 MP doc gets
        try:
            scale = _perceptual_scale((height, width))
            started = time.perf_counter()
            capped = compare_renders(truth_png, defect_png, _SELFTEST_SIZE, objects)
            capped_seconds = time.perf_counter() - started
        finally:
            PERCEPTUAL_MAX_PIXELS = original_cap
        capped_object = next(o for o in capped["perObject"] if o["name"] == "defect")
        check(abs(scale - 0.25) < 0.01, f"the cap resamples by {scale:.2f}")
        check(not capped_object["perceptualOk"],
              f"capped: the defect object still flags "
              f"({capped_object['perceptualBadFraction'] * 100:.0f}%)")
        check(next(o for o in capped["perObject"] if o["name"] == "clean")["perceptualOk"],
              "capped: the clean object still scores ok")
        check(capped["badFraction"] == full["badFraction"],
              "the strict metric is unaffected by the cap")

        print("5. 16-bit precision: decoding and the deep comparison")
        deep_size = (300, 200)
        yy, xx = np.mgrid[0:deep_size[1], 0:deep_size[0]].astype(np.float64)
        deep = np.stack([xx / (deep_size[0] - 1) * 65535.0,
                         yy / (deep_size[1] - 1) * 65535.0,
                         (xx + yy) / (deep_size[0] + deep_size[1] - 2) * 40000.0 + 9000.0], axis=2)
        deep = np.rint(deep)
        deep_truth = work_dir / "deep_truth.png"
        write_png16_rgb(deep_truth, deep)
        filtered = work_dir / "deep_filtered.png"
        _write_png16_filtered(filtered, deep)
        check(np.array_equal(np.rint(_decode_png16_rgba(filtered)[:, :, :3] * 65535.0), deep),
              "the decoder undoes every PNG filter type exactly")
        check(np.array_equal(np.rint(read_png16_rgba(filtered)[:, :, :3] * 65535.0), deep),
              "read_png16_rgba (OpenCV when installed) agrees")
        check(png_bit_depth(deep_truth) == 16, "png_bit_depth reads 16")
        same = compare_deep_renders(deep_truth, filtered, deep_size)
        check(same["state"] == "done" and same["badFraction"] == 0.0 and same["rmse"] == 0.0
              and same["editorBits"] == 16, "an identical 16-bit render is exact")
        noisy = work_dir / "deep_noisy.png"
        jitter = np.random.default_rng(7).integers(-3, 4, deep.shape)
        write_png16_rgb(noisy, deep + jitter)
        result = compare_deep_renders(deep_truth, noisy, deep_size)
        check(result["badFraction"] == 0.0, "a few 16-bit levels of noise stay precise")
        eight = work_dir / "deep_8bit.png"
        Image.fromarray(np.clip(np.rint(deep / 257.0), 0, 255).astype(np.uint8)).save(eight)
        result = compare_deep_renders(deep_truth, eight, deep_size)
        check(result["editorBits"] == 8 and 0.8 < result["badFraction"] < 0.95,
              f"an 8-bit render of it is imprecise on {result['badFraction'] * 100:.0f}% of pixels")
        check(result["maxError"] <= 0.51 and 0.2 < result["rmse"] < 0.35,
              f"...by rounding only (rmse {result['rmse']}, worst {result['maxError']} steps)")
        check(compare_renders(deep_truth, eight, deep_size, [])["badFraction"] == 0.0,
              "the 8-bit metrics cannot see that loss")
        check(compare_deep_renders(eight, deep_truth, deep_size)["state"] == "not measured",
              "an 8-bit reference is not measured")
        export = work_dir / "export.png"
        write_png16_rgb(export, deep)
        moved = split_deep_png(export)
        check(moved is not None and moved.name == "export16.png" and png_bit_depth(moved) == 16
              and png_bit_depth(export) == 8, "a 16-bit export splits into export16.png and an 8-bit export.png")
        check(compare_deep_renders(deep_truth, moved, deep_size)["rmse"] == 0.0,
              "...keeping the 16-bit samples")
        narrow = np.asarray(Image.open(export).convert("RGB"), dtype=np.float64)
        check(np.array_equal(narrow, np.clip(np.rint(deep / 257.0), 0, 255)),
              "...and narrowing the 8-bit one as round(v / 257)")
        check(split_deep_png(export) is None, "an 8-bit PNG is left alone")

        print("6. cost")
        started = time.perf_counter()
        compare_renders(truth_png, defect_png, _SELFTEST_SIZE, objects)
        uncapped_seconds = time.perf_counter() - started
        megapixels = width * height / 1e6
        print(f"       {megapixels:.2f} MP pair: {uncapped_seconds:.2f}s uncapped, "
              f"{capped_seconds:.2f}s at a quarter scale "
              f"({uncapped_seconds / max(capped_seconds, 1e-6):.1f}x)")

    print("FAILED: " + "; ".join(failures) if failures else "all checks passed")
    return 1 if failures else 0


def _write_png16_filtered(path: Path, rgb: np.ndarray) -> None:
    """A 16-bit RGB PNG whose rows cycle through all five filter types, so the
    self-test exercises every branch of the decoder."""
    import struct
    import zlib

    samples = np.clip(np.rint(rgb), 0, 65535).astype(">u2")
    height, width = samples.shape[:2]
    bpp = 6
    previous = np.zeros(width * bpp, dtype=np.int32)
    body = bytearray()
    for y in range(height):
        current = np.frombuffer(samples[y].tobytes(), dtype=np.uint8).astype(np.int32)
        kind = y % 5
        left = np.concatenate([np.zeros(bpp, dtype=np.int32), current[:-bpp]])
        upper_left = np.concatenate([np.zeros(bpp, dtype=np.int32), previous[:-bpp]])
        if kind == 0:
            filtered = current
        elif kind == 1:
            filtered = current - left
        elif kind == 2:
            filtered = current - previous
        elif kind == 3:
            filtered = current - ((left + previous) >> 1)
        else:
            estimate = left + previous - upper_left
            pa, pb, pc = np.abs(estimate - left), np.abs(estimate - previous), np.abs(estimate - upper_left)
            filtered = current - np.where((pa <= pb) & (pa <= pc), left, np.where(pb <= pc, previous, upper_left))
        body += bytes([kind]) + (filtered & 0xFF).astype(np.uint8).tobytes()
        previous = current

    def chunk(kind: bytes, payload: bytes) -> bytes:
        return (struct.pack(">I", len(payload)) + kind + payload
                + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))

    header = struct.pack(">IIBBBBB", width, height, 16, 2, 0, 0, 0)
    Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
                           + chunk(b"IDAT", zlib.compress(bytes(body), 6)) + chunk(b"IEND", b""))


def make_thumbnail(source_png: Path, out_png: Path, max_width: int = 480) -> None:
    image = Image.open(source_png)
    image = image.convert("RGBA")
    if image.width > max_width:
        scale = max_width / image.width
        image = image.resize((max_width, max(1, int(image.height * scale))))
    image.save(out_png)


if __name__ == "__main__":
    import sys

    if "--selftest" in sys.argv:
        raise SystemExit(_selftest())
    print(__doc__)
    print("run with --selftest to exercise the metrics against synthetic renders")
