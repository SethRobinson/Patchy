"""Judge the PSD an editor converted from a native file.

Photoshop opens the PSD. Three things are scored against what the source held:
whether Photoshop accepts the file, whether its render still matches the native
reference, and how many of the source's layers are still layers of the same kind.
The layer list is Photoshop's own (drivers/photoshop.py), so every editor is read
the same way.
"""

from __future__ import annotations

from pathlib import Path
import re

from PIL import Image

import manifest as manifest_mod
import native_import_score as scoring

# Photoshop's BlendMode names. A source mode outside this list has no place in a PSD,
# so it is never part of the expectation: an editor may keep the layer or bake the look.
PHOTOSHOP_BLENDS = frozenset((
    "NORMAL", "DISSOLVE", "DARKEN", "MULTIPLY", "COLORBURN", "LINEARBURN", "DARKERCOLOR", "LIGHTEN",
    "SCREEN", "COLORDODGE", "LINEARDODGE", "LIGHTERCOLOR", "OVERLAY", "SOFTLIGHT", "HARDLIGHT",
    "VIVIDLIGHT", "LINEARLIGHT", "PINLIGHT", "HARDMIX", "DIFFERENCE", "EXCLUSION", "SUBTRACT",
    "DIVIDE", "HUE", "SATURATION", "COLOR", "LUMINOSITY", "PASSTHROUGH"))
_ATTRIBUTES = ("nesting", "visible", "mask", "opacity", "blend")
OPACITY_TOLERANCE = 1.0  # percent: an 8-bit opacity is 0.4% wide


def _unscored(reason: str) -> dict:
    return {"status": "unscored", "reason": reason}


def photoshop_blend(mode) -> str | None:
    name = re.sub(r"[^A-Z]", "", str(mode).upper())
    return name if name in PHOTOSHOP_BLENDS else None


def expected_layers(tree: list[dict]) -> list[dict]:
    """The manifest's bottom-to-top tree in Photoshop's order: top first, depth first."""
    rows: list[dict] = []

    def walk(layers, parent):
        for layer in reversed(layers):
            row = {"name": layer.get("name", ""), "want": layer, "parent": parent,
                   "group": layer.get("kind") in ("group", "artboard") or "children" in layer}
            rows.append(row)
            walk(layer.get("children") or [], row)

    walk(tree, None)
    return rows


def _kind_kept(row: dict, got: dict) -> bool:
    if row["group"]:
        return bool(got.get("group"))
    if row["want"].get("kind") == "text":
        return got.get("kind") == "TEXT"
    return not got.get("group")


def layers_kept(tree, actual: list[dict]) -> dict:
    """How many of the source's layers Photoshop still finds as a layer of that kind."""
    if not isinstance(tree, list):
        return _unscored("No layer tree in the manifest")
    rows = expected_layers(tree)
    if not rows:
        return _unscored("No layers in the manifest")
    pairs, renamed = manifest_mod._pair_layers(rows, actual)
    partner = {id(row): got for row, got in pairs}
    attributes = {name: {"kept": 0, "total": 0} for name in _ATTRIBUTES}

    def count(name: str, kept: bool) -> None:
        attributes[name]["total"] += 1
        attributes[name]["kept"] += bool(kept)

    kept, lost = 0, []
    for row in rows:
        got = partner.get(id(row))
        if got is None or not _kind_kept(row, got):
            lost.append({"name": row["name"], "kind": row["want"].get("kind") or ("group" if row["group"] else "layer"),
                         "became": None if got is None else got.get("kind")})
        else:
            kept += 1
        if got is None:
            continue
        want = row["want"]
        folder = str(got.get("path", "")).rpartition("/")[0]
        parent = partner.get(id(row["parent"])) if row["parent"] else None
        count("nesting", folder == "" if row["parent"] is None
              else parent is not None and str(parent.get("path", "")) == folder)
        if "visible" in want:
            shown = got.get("layerVisible")
            count("visible", (got.get("visible") if shown is None else shown) == want["visible"])
        if want.get("hasMask"):
            count("mask", got.get("userMask") or got.get("vectorMask"))
        if type(want.get("opacity")) in (int, float) and type(got.get("opacity")) in (int, float):
            count("opacity", abs(got["opacity"] - want["opacity"]) <= OPACITY_TOLERANCE)
        blend = photoshop_blend(want["blendMode"]) if "blendMode" in want else None
        if blend:
            count("blend", got.get("blend") == blend)
    result = {"status": "pass" if kept == len(rows) else "fail", "layersKept": kept, "layersTotal": len(rows),
              "attributes": {name: value for name, value in attributes.items() if value["total"]},
              "psdLayers": len(actual), "renamed": renamed}
    if lost:
        result["lost"] = lost[:40]
        first = lost[0]
        result["reason"] = (f"{len(lost)} of {len(rows)} layers were not kept. First: {first['name']!r} "
                            + ("is missing" if first["became"] is None else f"became {first['became']}"))
    return result


def _still(case: dict) -> tuple[str | None, str]:
    """(the single reference a PSD's one picture can be held to, why there is none)."""
    frames = case.get("frames")
    if frames is not None and len(frames) > 1 or len(case.get("durationsMs", [])) > 1:
        return None, "An animation has no single picture for a PSD to match"
    spec = frames[0] if frames else case.get("reference")
    return (spec, "") if spec else (None, "No reference PNG")


def score(probe: dict, version: str, case: dict, folder: Path, manifest_dir: Path) -> dict:
    """Score Photoshop's probe of folder/converted.psd (PhotoshopDriver.probe, with its
    render in folder/photoshop.png).

    `case` is the effective case of the cell (its `source` the staged copy, its
    references the ones the render leg used)."""
    render = folder / "photoshop.png"
    result: dict = {"photoshop": version}
    warnings = list(dict.fromkeys([*(probe.get("dialogs") or []), *(probe.get("operationDialogs") or [])]))
    if warnings:
        result["warnings"] = warnings
    if not probe.get("ok"):
        error = str(probe.get("error") or "Photoshop did not open the file")
        if not probe.get("fileRejected"):
            # A Photoshop that hung, crashed or never started says nothing about the PSD.
            blank = _unscored(f"Photoshop could not be asked: {error}")
            return {**result, "accepted": blank, "appearance": blank, "layers": blank}
        blank = _unscored("Photoshop rejected the PSD")
        return {**result, "accepted": {"status": "fail", "reason": error}, "appearance": blank, "layers": blank}
    result["accepted"] = {"status": "pass"}
    result["layers"] = layers_kept(case.get("expect", {}).get("layers"), probe.get("layers") or [])
    spec, reason = _still(case)
    if spec is None:
        result["appearance"] = _unscored(reason)
    elif probe.get("render") not in ("ok", "fallback") or not render.is_file():
        # "fallback" is Photoshop's copy-merged render: where every 32-bit document lands.
        result["appearance"] = _unscored(f"Photoshop wrote no render ({probe.get('render')})")
    else:
        try:
            with Image.open(render) as image:
                image.load()
                # A PSD holds no sprite cels: Photoshop's picture is held to the still rule.
                # Photoshop renders a flattened copy, so its PNG has no transparency to compare.
                result["appearance"] = scoring._score_image(spec, case, manifest_dir, image, render, {},
                                                            exact=False, capture=False, alpha=False)
        except Exception as error:
            result["appearance"] = _unscored(f"Cannot compare Photoshop's render: {error}")
    return result
