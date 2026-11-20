"""Score a single native import against the references its manifest supplies."""
from __future__ import annotations

import io
import math
from pathlib import Path
import xml.etree.ElementTree as ET
from zipfile import ZipFile

import numpy as np
from PIL import Image

import analyze


MAX_MERGED_BYTES = 256 * 1024 * 1024
MAX_PIXELS = 64_000_000  # analyze disables Pillow's limit for trusted Photoshop renders.
STILL_BAD_FRACTION = 0.10


def _unscored(reason):
    return {"status": "unscored", "reason": reason}


def _properties(expected, observed, surface):
    if not expected:
        return _unscored(f"No {surface} expectations")
    problems = []
    checks = 0

    def compare(want, have, path):
        nonlocal checks
        if isinstance(want, dict) and isinstance(have, dict):
            for key, value in want.items():
                child = f"{path}.{key}" if path else key
                if key in have:
                    compare(value, have[key], child)
                else:
                    checks += 1
                    problems.append(f"{child}: missing")
        elif isinstance(want, list) and isinstance(have, list):
            compare(len(want), len(have), path + ".length")
            for index, (w, h) in enumerate(zip(want, have)):
                compare(w, h, f"{path}[{index}]")
        else:
            checks += 1
            if want != have or isinstance(want, bool) != isinstance(have, bool):
                problems.append(f"{path}: expected {want!r}, got {have!r}")

    compare(expected, observed, "")
    if not checks:
        return _unscored(f"No {surface} properties to compare")
    result = {"status": "fail" if problems else "pass", "checked": checks, "mismatches": problems}
    if problems:
        result["reason"] = problems[0]
    return result


def _reference(spec, case, manifest_dir):
    if spec != "@mergedimage":
        return manifest_dir / spec, None
    source = manifest_dir / case["source"]
    if source.suffix.lower() not in {".kra", ".krz", ".ora"}:
        raise ValueError("@mergedimage requires a KRA or ORA source")
    with ZipFile(source) as archive:
        entry = archive.getinfo("mergedimage.png")
        if entry.file_size > MAX_MERGED_BYTES:
            raise ValueError("mergedimage.png exceeds the extraction limit")
        with archive.open(entry) as stream:
            data = stream.read(MAX_MERGED_BYTES + 1)
        if len(data) > MAX_MERGED_BYTES:
            raise ValueError("mergedimage.png exceeds the extraction limit")
        # A Krita rendering can be padded to its tile boundary. Only the declared
        # source canvas tells us whether that PNG is a suitable full-canvas reference.
        ora = source.suffix.lower() == ".ora"
        name = "stack.xml" if ora else "maindoc.xml"
        keys = ("w", "h") if ora else ("width", "height")
        with archive.open(name) as stream:
            parser = ET.XMLPullParser(events=("start",))
            # The canvas is at the root (ORA) or its immediate child (KRA).
            # Bound the header independently of the number of layers in the XML.
            for _ in range(256):
                chunk = stream.read(4096)
                if not chunk:
                    break
                parser.feed(chunk)
                for _, el in parser.read_events():
                    if el.tag.rsplit("}", 1)[-1].lower() == "image":
                        canvas = tuple(int(el.attrib[key]) for key in keys)
                        return io.BytesIO(data), canvas
        raise ValueError(f"missing canvas size in {name}")


def _check_png(image):
    if image.format != "PNG":
        raise ValueError("expected a PNG image")
    if image.width * image.height > MAX_PIXELS:
        raise ValueError(f"image exceeds the {MAX_PIXELS:,}-pixel scoring limit")


def _display_png(source):
    """Narrow deep originals to 8-bit display samples before Testy's ICC conversion."""
    if isinstance(source, io.BytesIO):
        data = source.getvalue()
        head = data[:26]
    else:
        data = source
        with source.open("rb") as stream:
            head = stream.read(26)
    if head[24] != 16:
        return source
    with Image.open(source) as original:
        profile = original.info.get("icc_profile")
        transparent = original.info.get("transparency")
    rgba = analyze.read_png16_rgba(data)
    color_type = head[25]
    if transparent is not None and color_type in (0, 2):
        channels = 1 if color_type == 0 else 3
        transparent_pixels = np.all(np.rint(rgba[:, :, :channels] * 65535) == transparent, axis=2)
        rgba[transparent_pixels, 3] = 0
    image = Image.fromarray(np.rint(255.0 * rgba).astype(np.uint8))
    if color_type in (0, 4):
        # Keep grayscale profiles attached to gray samples. The decoder duplicates gray into RGB.
        image = Image.merge("LA", (image.getchannel("R"), image.getchannel("A")))
    buf = io.BytesIO()
    image.save(buf, "PNG", icc_profile=profile)
    return buf


def _score_image(spec, case, manifest_dir, render, render_path, observed, exact, capture):
    if not spec:
        return _unscored("No reference PNG for this frame")
    reference, source_size = _reference(spec, case, manifest_dir)
    with Image.open(reference) as image:
        _check_png(image)
        if image.n_frames != 1:
            raise ValueError("each reference must be a single-frame PNG")
        ref_size = image.size
        # Fully decode the reference before scoring any aspect of it.
        image.load()
        if exact:
            ref = np.asarray(image.convert("RGBA"))
    dimensions = {"referenceSize": list(ref_size), "renderSize": list(render.size)}
    if source_size is not None and ref_size != source_size:
        return {"status": "unscored", "reason": "Embedded reference dimensions differ from the source canvas",
                "sourceSize": list(source_size), **dimensions}
    if render.size != ref_size:
        return {"status": "fail", "reason": "Reference and render dimensions differ", **dimensions}
    if "width" in observed and "height" in observed:
        doc_size = (observed["width"], observed["height"])
        if doc_size != ref_size:
            return {"status": "fail", "reason": "Imported canvas dimensions differ from reference",
                    "documentSize": list(doc_size), **dimensions}
    if exact:
        actual = np.asarray(render.convert("RGBA"))
        visible = (ref[:, :, 3] != 0) | (actual[:, :, 3] != 0)
        different = np.count_nonzero(np.any(ref != actual, axis=2) & visible)
        result = {"status": "fail" if different else "pass", "differentPixels": int(different),
                  "pixels": ref_size[0] * ref_size[1], "metric": "visible RGBA8"}
        if different:
            result["reason"] = f"{different} visible RGBA8 pixels differ"
            if capture:
                _capture_pair(result, Image.fromarray(ref), Image.fromarray(actual), render_path.parent)
        return result

    # compare_renders can resize to its requested canvas. Both sizes were checked above.
    reference = _display_png(reference)
    actual_png = _display_png(render_path)
    metrics = analyze.compare_renders(reference, actual_png, ref_size, [])
    ref_image = analyze.load_srgb_rgba(reference)
    actual_image = analyze.load_srgb_rgba(actual_png)
    ref_alpha = np.asarray(ref_image.getchannel("A"))
    render_alpha = np.asarray(actual_image.getchannel("A"))
    alpha_delta = np.abs(ref_alpha.astype(np.int16) - render_alpha)
    alpha_diff = int(np.count_nonzero(alpha_delta))
    alpha_bad = int(np.count_nonzero(alpha_delta > analyze.PIXEL_TOLERANCE))
    alpha_bad_fraction = alpha_bad / alpha_delta.size
    bad = metrics["perceptual"]["badFraction"]
    passed = bad <= STILL_BAD_FRACTION and alpha_bad_fraction <= STILL_BAD_FRACTION
    result = {"status": "pass" if passed else "fail", "metric": "8-bit sRGB appearance and alpha",
              "perceptual": metrics["perceptual"], "strictBadFraction": metrics["badFraction"],
              "perceptualBadFractionLimit": STILL_BAD_FRACTION,
              "differentAlphaPixels": alpha_diff, "alphaBadPixels": alpha_bad,
              "alphaTolerance": analyze.PIXEL_TOLERANCE, "alphaBadFraction": alpha_bad_fraction,
              "pixels": ref_size[0] * ref_size[1]}
    if not passed:
        result["reason"] = f"{bad:.2%} perceptual bad pixels, {alpha_bad_fraction:.2%} beyond alpha tolerance"
        if capture:
            _capture_pair(result, ref_image, actual_image, render_path.parent)
    return result


def _capture_pair(result, reference, actual, folder):
    try:
        reference.save(folder / "reference.png")
        actual.save(folder / "actual.png")
        result["comparisonImages"] = True
    except OSError as error:
        result["imageError"] = str(error)


def _summarize(rows, expected_count, actual_count):
    counts = {key: sum(row["status"] == key for row in rows) for key in ("pass", "fail", "unscored")}
    count_mismatch = expected_count != actual_count
    if count_mismatch or counts["fail"]:
        status = "fail"
    elif counts["unscored"] or not rows:
        status = "unscored"
    else:
        status = "pass"
    result = {"status": status, "frames": rows, "counts": counts,
              "expectedFrames": expected_count, "actualFrames": actual_count}
    if count_mismatch:
        result["reason"] = f"Expected {expected_count} frames, rendered {actual_count}"
    elif status != "pass":
        first = next((row for row in rows if row["status"] == status), {})
        result["reason"] = f"Frame {first['frame']}: {first['reason']}" if first else "No frames to score"
    return result


def score_case(case: dict, observed: dict, folder: Path, manifest_dir: Path) -> dict:
    """Return independent document, structure, appearance and timing checks. Frame numbers start at 1."""
    if not observed.get("opened"):
        return {key: _unscored("Source did not open") for key in ("appearance", "document", "structure", "timing")}
    expect = case.get("expect", {})
    doc_keys = {"width", "height", "depth", "profile"}
    result = {"structure": _properties({k: v for k, v in expect.items() if k not in doc_keys}, observed, "structure"),
              "document": _properties({k: v for k, v in expect.items() if k in doc_keys}, observed, "document"),
              "appearance": _unscored("No reference PNG"), "timing": _unscored("No expected frame durations")}
    refs = case.get("frames")
    if refs is None and case.get("reference"):
        refs = [case["reference"]]
    durations = case.get("durationsMs")
    if refs is None and durations is None:
        return result
    checks = [key for key, wanted in (("appearance", refs), ("timing", durations)) if wanted is not None]
    if not observed.get("exported"):
        for key in checks:
            result[key] = _unscored("No render was exported")
        return result

    path = folder / "render.png"
    exact = "frames" in case or Path(case["source"]).suffix.lower() in {".ase", ".aseprite"}
    try:
        with Image.open(path) as render:
            _check_png(render)
            first = int(bool(render.info.get("default_image")))  # APNG's separate fallback still
            n_frames = render.n_frames - first
            appearance, timing = [], []
            captured = False
            for index in range(max(len(refs or []), len(durations or []))):
                if index < n_frames:
                    render.seek(first + index)
                    render.load()
                if refs is not None and index < len(refs):
                    if index >= n_frames:
                        row = {"status": "fail", "reason": "Missing rendered frame"}
                    else:
                        try:
                            row = _score_image(refs[index], case, manifest_dir, render, path, observed, exact,
                                               capture=not captured)
                            captured = captured or bool(row.get("comparisonImages"))
                        except Exception as error:
                            row = _unscored(f"Cannot compare reference: {error}")
                    appearance.append({"frame": index + 1, **row})
                if durations is not None and index < len(durations):
                    actual = render.info.get("duration") if index < n_frames else None
                    expected = durations[index]
                    row = {"frame": index + 1, "expectedMs": expected, "actualMs": actual}
                    # Pillow returns floating milliseconds, e.g. 1001/1000 s as 1000.9999999999999 ms.
                    matches = actual is not None and math.isclose(actual, expected, rel_tol=0, abs_tol=1e-7)
                    row["status"] = "pass" if matches else "fail"
                    if row["status"] == "fail":
                        row["reason"] = f"Expected {expected} ms, got {actual} ms" if actual is not None else "No frame duration"
                    timing.append(row)
            if refs is not None:
                result["appearance"] = _summarize(appearance, len(refs), n_frames)
                if captured:
                    result["appearance"]["comparisonImages"] = True
            if durations is not None:
                result["timing"] = _summarize(timing, len(durations), n_frames)
    except Exception as error:
        for key in checks:
            result[key] = _unscored(f"Cannot read render: {error}")
    return result
