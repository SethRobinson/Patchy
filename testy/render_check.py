"""Photoshop's renders are Testy's yardstick, and Photoshop does not always draw a
file the same way twice: 28.0 sometimes renders psd-tools' layer_mask_data.psd with
part of a layer mask ignored, on the original and on reopened resaves alike. Every
Photoshop render that something is scored against goes through settle_render: a
render that disagrees with the reference render, or the reference itself, is drawn
again from a fresh open, and the picture two opens agree on is kept.

Two renders agree when no pixel differs by more than analyze.PIXEL_TOLERANCE (6/255)
per channel. Photoshop's own renders of one file meet that on every corpus file
(373 files, November 2026), so any wider difference is either the file or a glitch,
and a third open tells the two apart."""
from __future__ import annotations

from pathlib import Path
from typing import Callable

# The first render plus at most this many more opens.
MAX_RENDERS = 3


def source_exclusion(truth: dict | None) -> str | None:
    """Why this source cannot supply a Photoshop compatibility verdict."""
    if not truth:
        return None
    if truth.get("dialogs"):
        return "Photoshop displayed a warning while opening the source"
    if truth.get("state") == "failed":
        return "Photoshop refused the source" if truth.get("fileRejected") else "Photoshop reference unavailable"
    if str(truth.get("render", "")).startswith("render-error"):
        return "Photoshop could not render the source"
    outcome = (truth.get("renderCheck") or {}).get("outcome")
    if outcome in ("unstable", "unconfirmed"):
        return "Photoshop reference render " + outcome
    return None


def deep_companion(render_png: Path) -> Path:
    """The 16-bit render a probe writes beside an 8-bit one (drivers.photoshop's
    deep_render_path): render.png -> render16.png."""
    return render_png.with_name(render_png.stem + "16" + render_png.suffix)


def again_path(render_png: Path, number: int) -> Path:
    """Where the number-th open of the same file is rendered: again2-render.png."""
    return render_png.with_name(f"again{number}-{render_png.name}")


def scratch_names(*stems: str, float32: bool = False) -> tuple[str, ...]:
    """Every file name settle_render can leave behind if a run dies mid-check, for
    the run-deletion and scan-scrub lists (exact names only, never a wildcard)."""
    names = []
    for stem in stems:
        for number in range(2, MAX_RENDERS + 1):
            again = again_path(Path(stem + ".png"), number)
            names += [again.name, deep_companion(again).name]
            if float32:
                names.append(again.stem + "32.tif")
    return tuple(names)


def settle(first: Path, render_again: Callable[[int], Path | None],
           agree: Callable[[Path, Path], bool], reference: Path | None = None) -> dict:
    """Decide which render of one file to keep.

    `first` is the render already made; `render_again(n)` makes the n-th one from a
    fresh open (None when Photoshop could not). With a `reference` (the confirmed
    render of the original), a first render that agrees with it needs nothing more;
    without one (the reference itself) a second open always confirms it.

    Returns {"kept": path, "renders": n, "outcome": ...}:
      "agrees"      the first render matches the reference;
      "repeatable"  another open drew the first render again (a difference from the
                    reference is the file's own);
      "replaced"    the first render was the odd one out: another open matched the
                    reference ("basis": "reference") or two later opens agreed
                    ("basis": "majority");
      "unstable"    every open drew something different; the first is kept;
      "unconfirmed" Photoshop could not draw it again; the first is kept.
    """
    if reference is not None and agree(first, reference):
        return {"kept": first, "renders": 1, "outcome": "agrees"}
    renders = [first]
    while len(renders) < MAX_RENDERS:
        again = render_again(len(renders) + 1)
        if again is None:
            return {"kept": first, "renders": len(renders), "outcome": "unconfirmed"}
        renders.append(again)
        for earlier in renders[:-1]:
            if agree(again, earlier):
                if earlier == first:
                    return {"kept": first, "renders": len(renders), "outcome": "repeatable"}
                return {"kept": again, "renders": len(renders), "outcome": "replaced", "basis": "majority"}
        if reference is not None and agree(again, reference):
            return {"kept": again, "renders": len(renders), "outcome": "replaced", "basis": "reference"}
    return {"kept": first, "renders": len(renders), "outcome": "unstable"}


def note(verdict: dict, what: str) -> str | None:
    """One line for the report, or None when nothing unusual happened."""
    renders, outcome = verdict.get("renders", 1), verdict.get("outcome")
    if outcome == "repeatable" and renders > 2:
        return (f"Photoshop drew {what} differently on one of {renders} opens; "
                "the render the other two agree on is used")
    if outcome == "replaced" and verdict.get("basis") == "majority":
        return (f"Photoshop drew {what} differently on one of {renders} opens; "
                "the render the other two agree on is used")
    if outcome == "replaced":
        return (f"Photoshop drew {what} differently on {renders - 1} of {renders} opens; "
                "the open that matches the reference render is used")
    if outcome == "unstable":
        return f"Photoshop drew {what} differently on each of {renders} opens; the first is used"
    if outcome == "unconfirmed":
        return f"Photoshop could not draw {what} again to check its render; the first is used"
    return None


def settle_render(probe: Callable[[Path, Path], dict], source: Path, render_png: Path,
                  reference: Path | None, agree: Callable[[Path, Path], bool],
                  what: str, *, float_size: tuple[int, int] | None = None) -> dict | None:
    """settle() for a render a Photoshop probe of `source` wrote to `render_png`.

    `probe(source, out_png)` is PhotoshopDriver.probe (it also writes the 16-bit
    companion of a 16-bit document). The kept render, with its companion, ends up at
    render_png; the other opens' files are deleted. Returns the record for the cell
    ({"renders", "outcome", "note"?}), or None when the first render matched the
    reference and nothing more was drawn."""
    made: list[Path] = []
    successful = [render_png]
    dialogs = []
    float_check = None

    def artifacts(path: Path) -> list[Path]:
        files = [path, deep_companion(path)]
        if float_size is not None:
            files.append(path.with_name(path.stem + "32.tif"))
        return files

    def render_again(number: int) -> Path | None:
        out = again_path(render_png, number)
        for stale in artifacts(out):
            stale.unlink(missing_ok=True)
        made.append(out)
        result = probe(source, out)
        for warning in result.get("dialogs") or []:
            if warning not in dialogs:
                dialogs.append(warning)
        if result.get("ok") and out.exists():
            successful.append(out)
            return out
        return None

    try:
        verdict = settle(render_png, render_again, agree, reference)
        kept = verdict["kept"]
        if float_size is not None:
            import float32_check
            float_check = float32_check.confirm_reference(kept, successful, float_size)
        if kept != render_png:
            for source_art, dest_art in zip(artifacts(kept), artifacts(render_png)):
                if source_art.exists():
                    source_art.replace(dest_art)
                elif float_size is not None and dest_art.suffix == ".tif":
                    dest_art.unlink(missing_ok=True)
    finally:
        for path in made:
            for leftover in artifacts(path):
                leftover.unlink(missing_ok=True)
    if verdict["outcome"] == "agrees":
        return None
    record = {"renders": verdict["renders"], "outcome": verdict["outcome"]}
    if float_check:
        record["float32"] = float_check
    if dialogs:
        record["dialogs"] = dialogs
    line = note(verdict, what)
    if line:
        record["note"] = line
    return record
