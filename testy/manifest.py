"""Structural manifest comparison: how many objects survived a resave natively.

Both manifests come from Photoshop reopening the file (drivers/photoshop.py), so
"native" means "Photoshop still recognizes the object as what it was" - text still
a type layer, a curves adjustment still curves, a smart object still smart, live
effects still live. Matching pairs layers by name sequence (difflib) because
editors renumber, drop, and occasionally rename layers.
"""

from __future__ import annotations

from difflib import SequenceMatcher

ADJUSTMENT_KINDS = {
    "BRIGHTNESSCONTRAST",
    "LEVELS",
    "CURVES",
    "COLORBALANCE",
    "HUESATURATION",
    "SELECTIVECOLOR",
    "CHANNELMIXER",
    "GRADIENTMAP",
    "INVERSION",
    "THRESHOLD",
    "POSTERIZE",
    "PHOTOFILTER",
    "EXPOSURE",
    "BLACKANDWHITE",
    "VIBRANCE",
    "COLORLOOKUP",
}

FILL_KINDS = {"SOLIDFILL", "GRADIENTFILL", "PATTERNFILL"}


def _category(layer: dict) -> str:
    if layer.get("group"):
        return "group"
    kind = layer.get("kind", "UNKNOWN")
    if kind == "TEXT":
        return "text"
    if kind == "SMARTOBJECT":
        return "smartObject"
    if kind in ADJUSTMENT_KINDS:
        return "adjustment"
    if kind in FILL_KINDS:
        return "fill"
    return "raster"


def _kind_preserved(original: dict, resaved: dict) -> bool:
    category = _category(original)
    if category in ("group", "text", "smartObject"):
        return _category(resaved) == category
    if category in ("adjustment", "fill"):
        # Same specific kind: a Curves that came back as Levels is not preserved.
        return resaved.get("kind") == original.get("kind")
    # Raster content: any non-group raster-ish layer counts (fills/smart wrappers are
    # acceptable upgrades nobody performs in practice; text/adjustment are not raster).
    return not resaved.get("group") and _category(resaved) in ("raster", "fill", "smartObject")


def compare_manifests(original_layers: list[dict], resaved_layers: list[dict]) -> dict:
    matcher = SequenceMatcher(
        a=[layer.get("name", "") for layer in original_layers],
        b=[layer.get("name", "") for layer in resaved_layers],
        autojunk=False,
    )
    pairs: list[tuple[dict, dict]] = []
    for block in matcher.get_matching_blocks():
        for offset in range(block.size):
            pairs.append((original_layers[block.a + offset], resaved_layers[block.b + offset]))

    matched_names = {id(a) for a, _ in pairs}
    lost = [
        {"path": layer.get("path"), "name": layer.get("name"), "kind": layer.get("kind")}
        for layer in original_layers
        if id(layer) not in matched_names
    ]

    categories = ("text", "adjustment", "smartObject", "group", "fill", "raster")
    per_category = {name: {"kept": 0, "total": 0} for name in categories}
    attributes = {
        "userMask": {"kept": 0, "total": 0},
        "vectorMask": {"kept": 0, "total": 0},
        "fx": {"kept": 0, "total": 0},
        "clipped": {"kept": 0, "total": 0},
        "blend": {"kept": 0, "total": 0},
    }
    native_kept = 0
    changed: list[dict] = []

    matched_lookup = {id(a): b for a, b in pairs}
    for original in original_layers:
        category = _category(original)
        per_category[category]["total"] += 1
        resaved = matched_lookup.get(id(original))
        preserved = resaved is not None and _kind_preserved(original, resaved)
        if preserved:
            per_category[category]["kept"] += 1
            native_kept += 1
        else:
            changed.append(
                {
                    "path": original.get("path"),
                    "name": original.get("name"),
                    "kind": original.get("kind"),
                    "became": None if resaved is None else resaved.get("kind"),
                    "category": category,
                }
            )
        if resaved is not None:
            for key in ("userMask", "vectorMask", "fx", "clipped"):
                if original.get(key):
                    attributes[key]["total"] += 1
                    if resaved.get(key):
                        attributes[key]["kept"] += 1
            if original.get("blend"):
                attributes["blend"]["total"] += 1
                if resaved.get("blend") == original.get("blend"):
                    attributes["blend"]["kept"] += 1

    total = len(original_layers)
    return {
        "nativeKept": native_kept,
        "nativeTotal": total,
        "nativeScore": round(native_kept / total, 4) if total else 1.0,
        "perCategory": per_category,
        "attributes": attributes,
        "lostLayers": lost[:40],
        "changedLayers": changed[:40],
        "resavedLayerCount": len(resaved_layers),
    }
