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

# Bumped when the pairing rules change, so cells cached under older rules are compared
# again from their stored manifests (2: renamed layers are paired by stack order).
MATCHING_VERSION = 2


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


def _pair_layers(original_layers: list[dict], resaved_layers: list[dict]) -> tuple[list[tuple[dict, dict]], int]:
    """(pairs of (original, resaved) layers, how many were paired by stack order after
    a rename)."""
    matcher = SequenceMatcher(
        a=[layer.get("name", "") for layer in original_layers],
        b=[layer.get("name", "") for layer in resaved_layers],
        autojunk=False,
    )
    pairs: list[tuple[dict, dict]] = []
    for block in matcher.get_matching_blocks():
        for offset in range(block.size):
            pairs.append((original_layers[block.a + offset], resaved_layers[block.b + offset]))

    # A layer that only changed its name is still that layer. Editors rename on save
    # (PhotoDemon writes its own name for the background), and Photoshop itself names
    # the single layer of a file saved without layer records in its interface
    # language. Whatever the name pass left over on both sides is paired in stack
    # order; the kind check below still decides whether the object survived.
    paired_originals = {id(a) for a, _ in pairs}
    paired_resaved = {id(b) for _, b in pairs}
    left_originals = [layer for layer in original_layers if id(layer) not in paired_originals]
    left_resaved = [layer for layer in resaved_layers if id(layer) not in paired_resaved]
    renamed = list(zip(left_originals, left_resaved))
    pairs.extend(renamed)
    return pairs, len(renamed)


def compare_manifests(original_layers: list[dict], resaved_layers: list[dict]) -> dict:
    pairs, renamed_count = _pair_layers(original_layers, resaved_layers)

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
    native = {
        "nativeKept": native_kept,
        "nativeTotal": total,
        "nativeScore": round(native_kept / total, 4) if total else 1.0,
        "perCategory": per_category,
        "attributes": attributes,
        "lostLayers": lost[:40],
        "changedLayers": changed[:40],
        "resavedLayerCount": len(resaved_layers),
        "renamedLayers": renamed_count,
        "matching": MATCHING_VERSION,
    }
    apply_text_save_rule(native)
    return native


def apply_text_save_rule(native: dict | None) -> bool:
    """An editor that cannot save a Photoshop text object back out as text scores 0%
    for the file's "data kept in .psd save", whatever else survived: the text is no
    longer editable, which is the loss people do not expect. The counts stay as
    measured; the score that was replaced is kept as nativeScoreMeasured, and
    textNotSaved says how many text objects were lost. Returns True when it changed
    `native` (also used to bring cells cached before the rule up to date)."""
    if not native or "textNotSaved" in native or "nativeScore" not in native:
        return False
    text = (native.get("perCategory") or {}).get("text") or {}
    total, kept = int(text.get("total", 0)), int(text.get("kept", 0))
    if total == 0 or kept >= total:
        return False
    native["nativeScoreMeasured"] = native["nativeScore"]
    native["nativeScore"] = 0.0
    native["textNotSaved"] = {"lost": total - kept, "total": total}
    return True


# The settings of a type layer the text-save leg compares between the original and the
# .psd an editor saved after its text update (testy.py _text_save_leg), with the words
# the report uses. None is a value Photoshop could not give: the layer leaves it to the
# document's default and the probe could not resolve it (drivers/photoshop.py
# resolveTextSettings: a locked layer, a file with a font missing).
PARAGRAPH_SETTINGS = (("align", "justification"), ("hyphenate", "Hyphenate"),
                      ("direction", "paragraph direction"), ("everyLineComposer", "line composer"))
LAYER_SETTINGS = (("orientation", "orientation"), ("antiAlias", "anti-aliasing"),
                  ("textKind", "point or paragraph text"))


def _text_contents(layer: dict) -> str | None:
    """The layer's text with line ends unified and trailing breaks dropped, or None
    when the manifest has none (Photoshop could not read it)."""
    contents = layer.get("text")
    if not contents:
        return None
    return contents.replace("\r\n", "\r").replace("\n", "\r").replace("\x03", "\r").rstrip()


def text_setting_changes(original: dict, resaved: dict) -> tuple[list[dict], list[dict]]:
    """(changed, unstated) settings of one type layer between two Photoshop manifests,
    each a list of {"setting", "was", "now"}.

    A setting counts as changed only when both files state it and the values differ.
    One that only one of them states goes to `unstated`: it may or may not have
    changed, and reading it as a change would blame the editor for something Testy
    cannot see. Paragraph settings are compared wherever the two files' paragraph
    ranges overlap, so an editor that splits or merges ranges is not marked down."""
    changed: list[dict] = []
    unstated: list[dict] = []

    def judge(label: str, was, now) -> None:
        if was == now:
            return
        row = {"setting": label, "was": was, "now": now}
        target = unstated if was is None or now is None else changed
        if row not in target:
            target.append(row)

    for key, label in LAYER_SETTINGS:
        judge(label, original.get(key), resaved.get(key))
    judge("text", _text_contents(original), _text_contents(resaved))
    fonts_before = sorted(str(face) for face in original.get("fonts") or [])
    fonts_after = sorted(str(face) for face in resaved.get("fonts") or [])
    judge("fonts", ", ".join(fonts_before) or None, ", ".join(fonts_after) or None)
    for before in original.get("paragraphs") or []:
        for after in resaved.get("paragraphs") or []:
            if min(before.get("to", 0), after.get("to", 0)) <= max(before.get("from", 0), after.get("from", 0)):
                continue  # no characters in common
            for key, label in PARAGRAPH_SETTINGS:
                judge(label, before.get(key), after.get(key))
    return changed, unstated


def compare_text_layers(original_layers: list[dict], resaved_layers: list[dict]) -> list[dict]:
    """One row per type layer of the original, against the same layer in a resave:
    {"path", "name", "bounds", "visible", "kept" (still a type layer), "became" (its
    kind now, None when it is gone), "savedBounds", "settingsChanged", "settingsUnstated"}.
    Layers are paired exactly as compare_manifests pairs them."""
    pairs, _renamed = _pair_layers(original_layers, resaved_layers)
    lookup = {id(original): resaved for original, resaved in pairs}
    rows = []
    for original in original_layers:
        if original.get("group") or original.get("kind") != "TEXT":
            continue
        resaved = lookup.get(id(original))
        kept = resaved is not None and _category(resaved) == "text"
        row = {
            "path": original.get("path"), "name": original.get("name", ""),
            "bounds": original.get("bounds"), "visible": bool(original.get("visible", True)),
            "kept": kept, "became": None if resaved is None else resaved.get("kind"),
            "savedBounds": None if resaved is None else resaved.get("bounds"),
            "settingsChanged": [], "settingsUnstated": [],
        }
        if kept:
            row["settingsChanged"], row["settingsUnstated"] = text_setting_changes(original, resaved)
        rows.append(row)
    return rows
