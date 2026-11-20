"""Full-canvas linear RGB comparison of Testy's local, uncompressed float TIFFs."""

from __future__ import annotations

from contextlib import ExitStack
import hashlib
from pathlib import Path
import struct

import numpy as np
import tifffile

MAX_PIXELS = 64_000_000
TOLERANCE = 0.001
BLOCK_PIXELS = 131_072


def companion(png: Path) -> Path:
    return png.with_name(png.stem + "32.tif")


def confirm_reference(kept_png: Path, successful_pngs: list[Path], size: tuple[int, int]) -> dict:
    """Confirm the unclipped samples of the same open that supplied the display reference."""
    if size[0] * size[1] > MAX_PIXELS:
        return {"state": "unscored", "reason": "Source canvas exceeds the float comparison size limit"}
    reason, retry = "No independently confirmed Photoshop float reference", True
    for other in successful_pngs:
        if other == kept_png:
            continue
        numeric = compare(companion(kept_png), companion(other), size, repeat=True)
        if numeric.get("identical"):
            return {"state": "confirmed", "sampleSHA256": numeric["sampleSHA256"]}
        reason = numeric.get("reason", "Photoshop's float samples differ between source opens")
        retry = numeric.get("state") == "done" or bool(numeric.get("retry"))
    return {"state": "unscored", "reason": reason, "retry": retry}


def _linear_srgb(profile: bytes) -> bool:
    """Recognize D50-adapted sRGB primaries and identity TRCs, independent of the ICC name."""
    if len(profile) < 132 or profile[16:24] != b"RGB XYZ " or profile[36:40] != b"acsp":
        return False
    count = int.from_bytes(profile[128:132], "big")
    if count > 256 or 132 + 12 * count > len(profile):
        return False
    tags = {}
    for i in range(count):
        key, offset, size = struct.unpack_from(">4sII", profile, 132 + 12 * i)
        if offset + size > len(profile):
            return False
        tags[key] = memoryview(profile)[offset:offset + size]
    if any(key[:3] in (b"A2B", b"B2A", b"D2B", b"B2D") for key in tags):
        return False
    primaries = ((.43607, .22250, .01393), (.38506, .71688, .09710), (.14308, .06062, .71417))
    for channel, expected in zip((b"r", b"g", b"b"), primaries):
        color = tags.get(channel + b"XYZ", b"")
        if len(color) < 20 or color[:4] != b"XYZ ":
            return False
        if any(abs(v / 65536 - t) > .001 for v, t in zip(struct.unpack_from(">iii", color, 8), expected)):
            return False
        curve = tags.get(channel + b"TRC", b"")
        if len(curve) < 12:
            return False
        if curve[:4] == b"curv":
            points = int.from_bytes(curve[8:12], "big")
            if points != 0 and not (points == 1 and bytes(curve[12:14]) == b"\x01\x00"):
                return False
        elif not (curve[:4] == b"para" and bytes(curve[8:16]) == b"\x00\x00\x00\x00\x00\x01\x00\x00"):
            return False
    return True


class _FloatPage:
    def __init__(self, tiff: tifffile.TiffFile, size: tuple[int, int], reference: bool):
        page = tiff.pages[0]
        self.width, self.height = page.imagewidth, page.imagelength
        if (self.width, self.height) != tuple(size):
            raise ValueError("TIFF dimensions differ from the source canvas")
        if self.width * self.height > MAX_PIXELS:
            raise ValueError("source canvas exceeds the float comparison size limit")
        self.channels = page.samplesperpixel
        if page.dtype != np.dtype("float32") or page.photometric != 2 or self.channels not in (3, 4):
            raise ValueError("a 32-bit floating-point RGB TIFF is required")
        if (page.compression != 1 or page.planarconfig != 1 or page.is_tiled or page.predictor != 1
                or int(page.tags[274].value if 274 in page.tags else 1) != 1):
            raise ValueError("unsupported TIFF storage or orientation")
        if self.channels == 4 and tuple(page.extrasamples) not in ((1,), (2,)):
            raise ValueError("TIFF alpha association is unspecified")
        self.associated = tuple(page.extrasamples) == (1,)
        icc = page.tags.get(34675)
        if icc is not None:
            if icc.count > 4_000_000 or not _linear_srgb(icc.value):
                raise ValueError("TIFF has no recognized linear sRGB profile")
        elif reference:
            raise ValueError("Photoshop float TIFF has no embedded color profile")
        self.file = tiff.filehandle
        self.dtype = np.dtype(tiff.byteorder + "f4")
        self.strip_rows = page.rowsperstrip
        self.offsets, self.counts = page.dataoffsets, page.databytecounts
        if (self.strip_rows <= 0 or len(self.offsets) != (self.height + self.strip_rows - 1) // self.strip_rows
                or len(self.counts) != len(self.offsets)):
            raise ValueError("invalid float TIFF strip table")

        self.strip_pixels = self.strip_rows * self.width

    def pixels(self, start: int, count: int) -> np.ndarray:
        strip, within = divmod(start, self.strip_pixels)
        stride = self.channels * 4
        length = count * stride
        if (within + count) * stride > self.counts[strip]:
            raise ValueError("truncated float TIFF strip")
        self.file.seek(self.offsets[strip] + within * stride)
        data = self.file.read(length)
        if len(data) != length:
            raise ValueError("truncated float TIFF pixels")
        rgba = np.frombuffer(data, dtype=self.dtype).reshape(-1, self.channels).astype(np.float64)
        rgb = rgba[:, :3]
        if self.channels == 4:
            alpha = rgba[:, 3:4]
            rgb[alpha[:, 0] == 0] = 0
            if not self.associated:
                rgb *= alpha
            # Photoshop flattens its duplicate over white in linear light.
            rgb += 1 - alpha
        return rgb


def compare(reference: Path, actual: Path, size: tuple[int, int], *, repeat=False,
            expected_digest: str | None = None) -> dict:
    """Separate numeric diagnostic. The display score always retains its existing rules."""
    if not reference.is_file() or not actual.is_file():
        return {"state": "unscored", "reason": "The required float TIFF is unavailable", "retry": True}
    with ExitStack() as stack:
        try:
            a = _FloatPage(stack.enter_context(tifffile.TiffFile(reference)), size, True)
            b = _FloatPage(stack.enter_context(tifffile.TiffFile(actual)), size, repeat)
            total = maximum = 0.0
            bad = invalid = finite_elements = 0
            bounds = [[float("inf"), float("-inf")], [float("inf"), float("-inf")]]
            digest = hashlib.sha256()
            pos = 0
            while pos < a.width * a.height:
                count = min(BLOCK_PIXELS, a.width * a.height - pos,
                            a.strip_pixels - pos % a.strip_pixels, b.strip_pixels - pos % b.strip_pixels)
                ref, image = a.pixels(pos, count), b.pixels(pos, count)
                if not np.all(np.isfinite(ref)):
                    return {"state": "unscored", "reason": "Photoshop reference has nonfinite RGB values"}
                if repeat or expected_digest:
                    digest.update(ref.astype("<f8", copy=False).tobytes())
                finite = np.isfinite(image)
                invalid_pixels = ~finite.all(axis=1)
                delta = np.abs(ref - image)
                bad += int(np.count_nonzero(invalid_pixels | np.any(delta > TOLERANCE, axis=1)))
                invalid += int(np.count_nonzero(invalid_pixels))
                for out, samples in zip(bounds, (ref, image[finite])):
                    if samples.size:
                        out[0] = min(out[0], float(samples.min()))
                        out[1] = max(out[1], float(samples.max()))
                valid_delta = delta[finite]
                if valid_delta.size:
                    total += float(valid_delta.sum())
                    maximum = max(maximum, float(valid_delta.max()))
                    finite_elements += valid_delta.size
                pos += count
        except (OSError, ValueError, IndexError, struct.error) as error:
            return {"state": "unscored", "reason": str(error)}
    if expected_digest and expected_digest != digest.hexdigest():
        return {"state": "unscored", "reason": "The saved TIFF differs from the confirmed Photoshop samples",
                "retry": True}
    result = {"state": "done", "pixels": a.width * a.height, "threshold": TOLERANCE,
              "pixelsOverTolerance": bad, "fractionOverTolerance": bad / (a.width * a.height),
              "nonfinitePixels": invalid, "meanAbsoluteError": total / finite_elements if finite_elements else None,
              "maxAbsoluteError": maximum if finite_elements else None, "referenceRange": bounds[0],
              "patchyRange": bounds[1] if finite_elements else None}
    if repeat:
        result["identical"] = maximum == 0 and invalid == 0
        result["sampleSHA256"] = digest.hexdigest()
    return result
