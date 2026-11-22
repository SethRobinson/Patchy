"""Other editors as columns of the native import benchmark.

An adapter opens a staged native file in one editor and writes what the run asks
for by extension: a PNG render, then a PSD. Patchy is not an adapter: the runner's
own probe measures it in more detail and saves its PSD in the same process.

Two outcomes are kept apart, as in the PSD benchmark (docs/testy-scoring.md): the
editor refused the file or crashed on it (counts against the editor), and the
harness could not run the editor (no verdict). Every file is attempted, including
in a format an editor seems not to read: a probe file cannot tell "no reader" from
"cannot read these files" (Krita imports XCF, but not the ones GIMP 3 writes), so
the report states what was measured, "opened 0 of N".
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Callable

import config

CONTESTANTS = ("patchy", "gimp", "krita", "photodemon", "photocraft", "photopea")
NAMES = {"patchy": "Patchy", "gimp": "GIMP", "krita": "Krita", "photodemon": "PhotoDemon",
         "photocraft": "PhotoCraft", "photopea": "Photopea"}
# The app whose own format this is. Its render is the reference where one is installed.
AUTHORING = {"xcf": "gimp", "kra": "krita"}
BREAKER_LIMIT = 3


@dataclass
class Adapter:
    key: str
    name: str
    version: str
    export: Callable[[Path, Path], dict]
    failures: int = 0  # consecutive hangs, crashes and launch failures


def parse_editors(text: str | None) -> list[str]:
    keys = [key.strip().lower() for key in (text or "patchy").split(",") if key.strip()]
    unknown = [key for key in keys if key not in CONTESTANTS]
    if unknown or not keys or len(set(keys)) != len(keys):
        raise ValueError("--editors takes a comma-separated list without repeats from: " + ", ".join(CONTESTANTS))
    return keys


def build(keys: list[str], server_url: str | None = None) -> tuple[dict[str, Adapter], dict[str, str]]:
    """Adapters for the requested editors other than Patchy, and why each missing one
    cannot run on this machine."""
    wanted = [key for key in keys if key != "patchy"]
    if not wanted:
        return {}, {}
    from drivers import gimp, krita, photocraft, photodemon, photopea

    drivers = {"gimp": gimp.export, "krita": krita.export, "photodemon": photodemon.export,
               "photocraft": photocraft.export}
    found = config.discover_editors("")
    adapters, missing = {}, {}
    for key in wanted:
        info = found[key]
        if not info.available or info.exe is None:
            missing[key] = "; ".join(info.notes) or f"{NAMES[key]} was not found on this machine"
        elif key == "photopea":
            if not server_url:
                missing[key] = "Photopea needs the Testy dashboard: start the run there or pass --server-url"
                continue
            adapters[key] = Adapter(key, NAMES[key], info.version, lambda source, output, url=server_url:
                                    photopea.export_one(url, config.TESTY_ROOT, source, output))
        else:
            adapters[key] = Adapter(key, NAMES[key], info.version, lambda source, output, run=drivers[key],
                                    exe=info.exe: run(exe, source, output))
    return adapters, missing


def _classify(result: dict) -> tuple[str, str, list[str]]:
    """("ok" | "refused" | "appFailure" | "harness", what the driver said, its notes)."""
    notes = [note for note in [*(result.get("notes") or []), result.get("note")] if note]
    text = str(result.get("stderr") or result.get("error") or "").strip()
    if result.get("ok"):
        return "ok", "", notes
    if result.get("harness"):
        return "harness", text or "the harness could not run the editor", notes
    if result.get("fileRejected"):
        return "refused", text or "the editor refused the file", notes
    if result.get("exitCode") == -1 and not text.startswith("timeout"):
        return "harness", text or "the editor did not start", notes
    return "appFailure", text or f"the editor failed (exit {result.get('exitCode')})", notes


def _run(adapter: Adapter, source: Path, output: Path) -> tuple[str, str, list[str]]:
    output.unlink(missing_ok=True)
    try:
        outcome = _classify(adapter.export(source, output))
    except Exception as error:  # a driver bug says nothing about the file
        outcome = ("harness", f"driver error: {error}", [])
    # A refusal is news about the file. Only the app or the harness failing repeatedly
    # says the column is broken for this run.
    adapter.failures = adapter.failures + 1 if outcome[0] in ("appFailure", "harness") else 0
    return outcome


def convert(adapter: Adapter, source: Path, folder: Path, want_psd: bool) -> dict:
    """One cell: render.png, then converted.psd when asked, both inside `folder`."""
    cell: dict = {"opened": None, "rendered": False, "notes": []}
    if adapter.failures >= BREAKER_LIMIT:
        cell["skipped"] = (f"{adapter.name} failed {BREAKER_LIMIT} files in a row, "
                           "so the rest of its column was skipped")
        return cell
    folder.mkdir(parents=True, exist_ok=True)
    kind, text, notes = _run(adapter, source, folder / "render.png")
    cell["notes"] += notes
    if kind == "harness":
        cell["error"] = text
        return cell
    cell["opened"] = kind == "ok"
    if kind != "ok":
        cell["error"] = text
        if kind == "appFailure":
            cell["appFailure"] = True
        return cell
    cell["rendered"] = True
    if want_psd:
        kind, text, notes = _run(adapter, source, folder / "converted.psd")
        cell["notes"] += [note for note in notes if note not in cell["notes"]]
        cell["savedPsd"] = None if kind == "harness" else kind == "ok"
        if kind != "ok":
            cell["psdError"] = text
    return cell


