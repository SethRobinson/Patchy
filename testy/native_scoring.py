"""Scores for the cross-editor native import benchmark, computed from a run's stored
measurements (results.json), so a rule change never needs the editors again.

Every way a file can go wrong has one place in the score:

- A file an editor does not open (a refusal, a crash, a hang) scores 0 on every
  component, as in the PSD benchmark (docs/testy-scoring.md, "Files an editor refuses").
- A cell the harness could not measure (the editor did not start, a skipped column,
  a Photoshop that could not be asked) stays out of that editor's averages.
- A component is eligible for a file only when some editor got a scored result on
  it: a reference nobody could be held to marks nobody down.
- A composite stored in a file by any writer is a reference only while some editor
  that composes the layers reproduces it (unreliable_reference): one that matches
  nothing but itself leaves both picture components out for every editor.

Components, each from 0 to 1 per file:

  render     the editor's own picture: 1 minus the share of pixels that look wrong
             (stills: perceptual or alpha, whichever is worse; sprites: visible
             pixels that differ at all), averaged over the frames with a reference
  psdValid   the editor saved a PSD and Photoshop opened it without a warning
  psdRender  Photoshop's picture of that PSD, 1 minus the share of wrong pixels
  layers     the share of the source's layers still layers of their kind in the PSD

Two headline scores per editor and format, each a mean over files:

  import      the render component (how well the editor shows the file)
  conversion  the mean of psdValid, psdRender and layers that are eligible for the
              file (how well the editor turns the file into a PSD)

Each cell also gets one outcome, its first problem in pipeline order, for the
"what went wrong" table.
"""

from __future__ import annotations

SCORING_VERSION = 2
PSD_COMPONENTS = ("psdValid", "psdRender", "layers")
COMPONENTS = ("render", *PSD_COMPONENTS)
PICTURE_COMPONENTS = ("render", "psdRender")
# Formats whose stored composite can come from any writer, so it is checked before it
# is believed, each with the editors that show that stored picture instead of composing
# the layers: their match says nothing about the layers. Photopea opens an OpenRaster
# file as its stored picture (one flat layer, whatever the stack holds). A KRA's
# composite is Krita's own and stays trusted.
COMPOSITE_VIEWERS = {"ora": ("photopea",)}
# How many editors must have composed the layers before their verdict counts.
MIN_LAYER_READERS = 3
# Pipeline order: a cell's outcome is the first of these that applies.
OUTCOMES = (
    ("notMeasured", "Not measured"),
    ("crashed", "Crashed or hung"),
    ("refused", "Did not open"),
    ("noRender", "Opened, no picture"),
    ("renderMismatch", "Picture differs"),
    ("noPsd", "No PSD saved"),
    ("psdRejected", "Photoshop rejects the PSD"),
    ("psdWarning", "PSD opens with a warning"),
    ("psdRenderMismatch", "PSD picture differs"),
    ("layersLost", "Layers lost in the PSD"),
    ("clean", "No problem found"),
)


def _status(check) -> str:
    status = check.get("status") if isinstance(check, dict) else None
    return status if status in ("pass", "fail") else "unscored"


def _frame_accuracy(row: dict) -> float | None:
    status = _status(row)
    if status == "unscored":
        return None
    pixels = row.get("pixels")
    if "differentPixels" in row and pixels:
        return max(0.0, 1.0 - row["differentPixels"] / pixels)
    if isinstance(row.get("perceptual"), dict) and "badFraction" in row["perceptual"]:
        return max(0.0, 1.0 - max(row["perceptual"]["badFraction"], row.get("alphaBadFraction") or 0.0))
    return 1.0 if status == "pass" else 0.0


def accuracy(check) -> float | None:
    """A picture check as a number: None when nothing in it was scored."""
    if not isinstance(check, dict):
        return None
    if isinstance(check.get("frames"), list):
        scored = [value for value in map(_frame_accuracy, check["frames"]) if value is not None]
        if scored:
            # A frame the editor never rendered is in the list as a failure already.
            return sum(scored) / len(scored)
        return 0.0 if _status(check) == "fail" else None
    return _frame_accuracy(check)


def _psd_state(cell: dict) -> str:
    """"none" (not asked or no verdict), "missing" (no PSD saved), "rejected" or "open"."""
    if cell.get("savedPsd") is False:
        return "missing"
    psd = cell.get("psd") if isinstance(cell.get("psd"), dict) else {}
    return {"pass": "open", "fail": "rejected"}.get(_status(psd.get("accepted")), "none")


def _measured(cell: dict, judged: bool) -> dict:
    """What one cell really measured: component -> 0..1, or None without a scored result."""
    values: dict = dict.fromkeys(COMPONENTS)
    if cell.get("opened") is not True:
        return values
    if cell.get("rendered"):
        values["render"] = accuracy(cell.get("appearance"))
    if judged:
        state = _psd_state(cell)
        if state in ("missing", "rejected"):
            values["psdValid"] = 0.0
        elif state == "open":
            psd = cell["psd"]
            values["psdValid"] = 0.0 if psd.get("warnings") else 1.0
            values["psdRender"] = accuracy(psd.get("appearance"))
            layers = psd.get("layers") or {}
            if _status(layers) != "unscored" and layers.get("layersTotal"):
                values["layers"] = layers.get("layersKept", 0) / layers["layersTotal"]
    return values


def _failed_before(cell: dict, name: str, judged: bool) -> bool:
    """Did this cell fail upstream of a component, so that it scores 0 there?"""
    if cell.get("opened") is False:
        return True  # a file it cannot open is a file it cannot show or convert
    if name == "render":
        return not cell.get("rendered")
    return judged and _psd_state(cell) in ("missing", "rejected")


def unreliable_reference(case: dict) -> str | None:
    """Why this file's stored composite is not a reference, or None while it is one.

    A composite that does not come from the format's own app is whatever its writer put
    there: a thumbnail of an earlier state, a picture flattened onto a background, the
    composite of another file. It is dropped when at least MIN_LAYER_READERS editors
    composed the layers themselves and not one of them passes against it. One pass
    keeps it: the others then failed a picture that can be reproduced."""
    viewers = COMPOSITE_VIEWERS.get(case.get("format"))
    if viewers is None or not case.get("storedComposite"):
        return None
    verdicts = [_status(cell.get("appearance")) for key, cell in (case.get("cells") or {}).items()
                if key not in viewers and cell.get("opened") is True and cell.get("rendered")]
    verdicts = [verdict for verdict in verdicts if verdict != "unscored"]
    if len(verdicts) < MIN_LAYER_READERS or "pass" in verdicts:
        return None
    return (f"The composite stored in this file is not used as a reference: none of the {len(verdicts)} "
            "editors that composed its layers matches it.")


def outcome(cell: dict, judged: bool, pictures: bool = True) -> str:
    """`pictures` is False for a file whose reference is not used: a picture that
    differs from it is then no problem of the editor's."""
    if cell.get("opened") is None:
        return "notMeasured"
    if cell["opened"] is False:
        return "crashed" if cell.get("appFailure") else "refused"
    if not cell.get("rendered"):
        return "noRender"
    if pictures and _status(cell.get("appearance")) == "fail":
        return "renderMismatch"
    if judged:
        if cell.get("savedPsd") is False:
            return "noPsd"
        psd = cell.get("psd") if isinstance(cell.get("psd"), dict) else {}
        if _status(psd.get("accepted")) == "fail":
            return "psdRejected"
        if psd.get("warnings"):
            return "psdWarning"
        if pictures and _status(psd.get("appearance")) == "fail":
            return "psdRenderMismatch"
        if _status(psd.get("layers")) == "fail":
            return "layersLost"
    return "clean"


def score_case(case: dict, judged: bool) -> dict:
    """Per editor: the components (None = not eligible or not measured), the two
    headline scores and the outcome for one file."""
    cells = case.get("cells") or {}
    measured = {key: _measured(cell, judged) for key, cell in cells.items()}
    eligible = {name: any(values[name] is not None for values in measured.values()) for name in COMPONENTS}
    pictures = unreliable_reference(case) is None
    if not pictures:
        eligible.update(dict.fromkeys(PICTURE_COMPONENTS, False))
    scores = {}
    for key, cell in cells.items():
        entry: dict = {"outcome": outcome(cell, judged, pictures)}
        if cell.get("opened") is None:
            scores[key] = entry  # the harness has no verdict: out of every average
            continue
        for name in COMPONENTS:
            if not eligible[name] or (name in PSD_COMPONENTS and not judged):
                continue
            value = measured[key][name]
            if value is None and _failed_before(cell, name, judged):
                value = 0.0
            if value is not None:  # None: opened, but this check has no verdict for this editor
                entry[name] = round(value, 4)
        if "render" in entry:
            entry["import"] = entry["render"]
        parts = [entry[name] for name in PSD_COMPONENTS if name in entry]
        if parts:
            entry["conversion"] = round(sum(parts) / len(parts), 4)
        scores[key] = entry
    return scores


def _mean(values: list[float]) -> float | None:
    return round(sum(values) / len(values), 4) if values else None


def _aggregate(rows: list[dict]) -> dict:
    """One editor's per-file entries into its averages and outcome counts."""
    attempted = [row for row in rows if row["outcome"] != "notMeasured"]
    result = {"files": len(rows), "measured": len(attempted),
              "opened": sum(row["outcome"] not in ("crashed", "refused") for row in attempted),
              "outcomes": {key: count for key, _ in OUTCOMES
                           if (count := sum(row["outcome"] == key for row in rows))}}
    for name in ("import", "conversion", *COMPONENTS):
        values = [row[name] for row in attempted if name in row]
        result[name] = _mean(values)
        result[name + "Files"] = len(values)
    return result


def apply(run: dict) -> dict | None:
    """Score a cross-editor run in place: run["scores"] = {"version", "judged", "formats":
    {format: {editor: averages}}, "all": {editor: averages}, "referencesNotUsed": {format:
    files}} and each case's entries in case["scores"], with case["referenceNotUsed"] saying
    why where a stored composite was dropped. Returns run["scores"]; None, changing
    nothing, for a plain run."""
    editors = run.get("editors")
    if not isinstance(editors, dict):
        return None
    judged = "psdJudge" in run
    by_format: dict = {}
    everything: dict = {key: [] for key in editors}
    dropped: dict = {}
    for case in run.get("cases", []):
        scored = case["scores"] = score_case(case, judged)
        reason = unreliable_reference(case)
        case.pop("referenceNotUsed", None)
        if reason:
            case["referenceNotUsed"] = reason
            dropped[case.get("format", "")] = dropped.get(case.get("format", ""), 0) + 1
        for key, entry in scored.items():
            by_format.setdefault(case.get("format", ""), {}).setdefault(key, []).append(entry)
            everything.setdefault(key, []).append(entry)
    run["scores"] = {"version": SCORING_VERSION, "judged": judged, "referencesNotUsed": dropped,
                     "formats": {fmt: {key: _aggregate(rows) for key, rows in rows_by_editor.items()}
                                 for fmt, rows_by_editor in sorted(by_format.items())},
                     "all": {key: _aggregate(rows) for key, rows in everything.items() if rows}}
    return run["scores"]
