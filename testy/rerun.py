"""Apply a completed one-image rerun without losing the batch's previous results."""

from __future__ import annotations

import copy
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile


STATE_FILE = "rerun-state.json"


def write_json(path: Path, value: dict) -> None:
    write_text(path, json.dumps(value, indent=1))


def write_text(path: Path, value: str) -> None:
    """Replace generated data atomically, preserving the old file on failure."""
    fd, temporary = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            stream.write(value)
        os.replace(temporary, path)
    finally:
        Path(temporary).unlink(missing_ok=True)


def entry_digest(entry: dict) -> str:
    return hashlib.sha256(json.dumps(entry, sort_keys=True).encode("utf-8")).hexdigest()


def run_path(runs_root: Path, name: str) -> Path:
    target = (runs_root / name).resolve()
    if not name or target.parent != runs_root.resolve() or not (target / "status.json").is_file():
        raise ValueError(f"no run named {name}")
    return target


def prepare(runs_root: Path, name: str, source: str, editors: list[str]) -> dict:
    target = run_path(runs_root, name)
    status = json.loads((target / "status.json").read_text(encoding="utf-8"))
    if status.get("state") != "done":
        raise ValueError("finish this batch before rerunning an image")
    entries = [f for f in status.get("files", []) if f.get("source") == source]
    if len(entries) != 1:
        raise ValueError("that file is not a unique image in this run")
    path = Path(source)
    if path.suffix.lower() not in (".psd", ".psb") or not path.is_file():
        raise ValueError("the source PSD/PSB is no longer available")
    if not editors or any(e not in status["run"]["editorOrder"] for e in editors):
        raise ValueError("choose editors present in this batch")
    entry = entries[0]
    if entry.get("sha1"):
        with path.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha1").hexdigest()
        if digest != entry["sha1"]:
            raise ValueError("the source changed since this batch; start a new run for the changed file")
    return {"target": name, "source": source, "expectedEntry": entry_digest(entry),
            "editors": editors, "options": status["run"]}


def note_state(runs_root: Path, request: dict, state: str, **extra) -> None:
    target = run_path(runs_root, request["target"])
    write_json(target / STATE_FILE, {"state": state, "source": request["source"],
               "updatedAt": dt.datetime.now().isoformat(timespec="seconds"), **extra})


def _relocate_artifacts(entry: dict, prefix: str) -> dict:
    result = copy.deepcopy(entry)
    for obj in [result.get("groundTruth", {}), *result.get("cells", {}).values()]:
        for key, value in obj.get("artifacts", {}).items():
            path = Path(value)
            if path.is_absolute() or ".." in path.parts:
                raise ValueError("rerun contains an invalid artifact path")
            obj["artifacts"][key] = prefix + value.replace("\\", "/")
    return result


def apply(runs_root: Path, child_dir: Path, request: dict, *, summarize, scan_reasons) -> dict:
    """Only replace measured editor cells. Keep other rows, columns and history."""
    target = run_path(runs_root, request["target"])
    current = json.loads((target / "status.json").read_text(encoding="utf-8"))
    child = json.loads((child_dir / "results.json").read_text(encoding="utf-8"))
    if child.get("state") != "done" or len(child.get("files", [])) != 1:
        raise ValueError("the one-image rerun did not complete")
    if not child["run"].get("sourcesUntouched"):
        raise ValueError("source integrity could not be verified")
    fresh = child["files"][0]
    if fresh.get("source") != request["source"]:
        raise ValueError("rerun source does not match the selected image")
    matches = [i for i, f in enumerate(current["files"]) if f.get("source") == request["source"]]
    if current.get("state") != "done" or len(matches) != 1:
        raise ValueError("the target batch is no longer ready for an update")
    index = matches[0]
    old = current["files"][index]
    if entry_digest(old) != request["expectedEntry"]:
        raise ValueError("this image's results changed during the rerun; previous results kept")
    if old.get("sha1") and old["sha1"] != fresh.get("sha1"):
        raise ValueError("source contents changed during the rerun")
    if fresh.get("groundTruth", {}).get("state") != "done":
        raise ValueError("Photoshop ground truth failed; previous results kept")
    for editor in request["editors"]:
        cell = fresh["cells"].get(editor, {})
        if cell.get("state") != "done" and not (cell.get("state") == "failed" and cell.get("fileRejected")):
            raise ValueError(f"{editor} did not finish; previous results kept")
        if cell.get("uncacheable") or cell.get("trapError"):
            raise ValueError(f"{editor} verification was incomplete; previous results kept")
        if cell.get("opens") != "fail" and not cell.get("renderMetrics"):
            raise ValueError(f"{editor} render comparison is missing; previous results kept")
        if (cell.get("native", {}).get("error") and not cell.get("resaveRejected")
                and not cell.get("resaveError")):
            raise ValueError(f"{editor} resave verification was incomplete; previous results kept")
        if any(check.get("state") == "error" for check in (cell.get("resaveChecks") or {}).values()):
            raise ValueError(f"{editor} resave was not reopened in every Photoshop version; "
                             "previous results kept")

    revision = target / "reruns" / child_dir.name
    revision.mkdir(parents=True, exist_ok=False)
    # The old artifacts remain at their original paths; the new artifacts are
    # owned by the target too, so deleting the standalone child cannot break it.
    write_json(revision / "before.json", current)
    files = child_dir / "files"
    if files.exists():
        shutil.copytree(files, revision / "files")
    relocated = _relocate_artifacts(fresh, "reruns/" + child_dir.name + "/")
    updated = copy.deepcopy(old)
    for key in ("docSize", "layerCount", "sizeBytes", "sha1", "psVersions"):
        if key in relocated:
            updated[key] = relocated[key]
    for editor in request["editors"]:
        updated["cells"][editor] = relocated["cells"][editor]
        updated["cells"][editor]["groundTruth"] = copy.deepcopy(relocated["groundTruth"])
    if set(request["editors"]) == set(current["run"]["editorOrder"]):
        updated["groundTruth"] = relocated["groundTruth"]
    record = {"run": child_dir.name, "at": child["run"]["finishedAt"],
              "editors": {e: child["editors"][e] for e in request["editors"]},
              "patchyGit": child["run"].get("patchyGit"),
              "previous": "reruns/" + child_dir.name + "/before.json"}
    updated.setdefault("reruns", []).append(record)
    if current["run"].get("scan"):
        reasons = scan_reasons(current, updated)
        updated["scan"] = {"flagged": bool(reasons), "reasons": reasons, "artifactsKept": True}
    current["files"][index] = updated
    current["run"].setdefault("reruns", []).append({"source": request["source"], **record})
    current["run"]["updatedAt"] = record["at"]
    current["run"]["updateCounter"] = current["run"].get("updateCounter", 0) + 1
    write_json(revision / "after.json", current)
    # History's latest entry for this batch is replaced, never duplicated.
    history = runs_root / "history.jsonl"
    lines = history.read_text(encoding="utf-8").splitlines() if history.exists() else []
    replacement = json.dumps(summarize(current))
    revised = []
    found = False
    for line in lines:
        try:
            same = json.loads(line).get("run") == target.name
        except (ValueError, AttributeError):
            same = False
        if same:
            if not found:
                revised.append(replacement)
                found = True
        else:
            revised.append(line)
    if not found:
        revised.append(replacement)
    writes = {target / "results.json": json.dumps(current, indent=1), history: "\n".join(revised) + "\n"}
    if current["run"].get("scan"):
        flagged = [f for f in current["files"] if f.get("scan", {}).get("flagged")]
        writes[target / "flagged.txt"] = "".join(
            "# " + "; ".join(f["scan"]["reasons"]).replace("\n", " ") + "\n" + f["source"] + "\n"
            for f in flagged)
    # Publish status last: readers see either the previous complete row or the
    # new complete row. If a write fails, restore the supporting result files.
    writes[target / "status.json"] = json.dumps(current, indent=1)
    originals = {path: path.read_text(encoding="utf-8") if path.exists() else None for path in writes}
    written = []
    try:
        for path, value in writes.items():
            write_text(path, value)
            written.append(path)
    except OSError:
        for path in reversed(written):
            if originals[path] is None:
                path.unlink(missing_ok=True)
            else:
                write_text(path, originals[path])
        raise
    note_state(runs_root, request, "applied", childRun=child_dir.name)
    return current
