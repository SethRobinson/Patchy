"""Testy's Patchy-only benchmark for layered native documents and sprites."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path, PureWindowsPath
import shutil
import subprocess
import tempfile
import uuid

import config
from console_output import log
from native_import_report import write_report
from native_import_score import score_case, validate_linear_samples
from report import write_json_atomic

_FORMATS = {
    ".ase": "aseprite", ".aseprite": "aseprite", ".kra": "kra", ".krz": "kra", ".ora": "ora",
    **{ext: "xcf" for ext in (".xcf", ".xcf.gz", ".xcf.xz", ".xcf.bz2", ".xcfgz", ".xcfxz", ".xcfbz2")},
}
PROBE = config.TESTY_ROOT / "drivers" / "native_import.js"


def now() -> str:
    return dt.datetime.now().astimezone().isoformat(timespec="seconds")


def next_run_dir() -> Path:
    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    return config.RUNS_DIR / f"native-{stamp}-{uuid.uuid4().hex[:8]}"


def file_digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def format_of(source: str) -> str:
    return next((fmt for ext, fmt in _FORMATS.items() if source.lower().endswith(ext)), "")


def local_path(text: str, base: Path) -> Path:
    if not isinstance(text, str) or not text.strip() or "\0" in text:
        raise ValueError("expected a nonempty local file path")
    if text.replace("\\", "/").startswith("//") or "://" in text or text.lower().startswith("file:"):
        raise ValueError("manifest paths must be local files")
    win = PureWindowsPath(text)
    if win.drive and not win.is_absolute():
        raise ValueError("drive-relative manifest path")
    return base / text


def load_manifest(path: Path) -> dict:
    data = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(data, dict) or not isinstance(data.get("cases"), list) or not data["cases"]:
        raise ValueError("manifest must contain a nonempty cases array")
    if "name" in data and not isinstance(data["name"], str):
        raise ValueError("manifest name must be a string")
    if "sources" in data and (not isinstance(data["sources"], list)
                              or not all(isinstance(s, dict) for s in data["sources"])):
        raise ValueError("manifest sources must be an array of attribution objects")
    ids = set()
    for pos, case in enumerate(data["cases"], 1):
        try:
            if not isinstance(case, dict):
                raise ValueError("expected an object")
            source = local_path(case.get("source"), path.parent)
            if not format_of(source.name):
                raise ValueError(f"unsupported native file type: {source.name}")
            key = case.get("id", case["source"])
            if not isinstance(key, str) or not key.strip() or key in ids:
                raise ValueError("case IDs must be nonempty, unique strings")
            ids.add(key)
            if "expect" in case and not isinstance(case["expect"], dict):
                raise ValueError("expect must be an object")
            if "linearSamples" in case:
                validate_linear_samples(case["linearSamples"])
            if "assets" in case and not isinstance(case["assets"], list):
                raise ValueError("assets must be an array")
            for asset in case.get("assets", []):
                dep = local_path(asset, path.parent).resolve()
                if not dep.is_relative_to(source.resolve().parent) or dep == source.resolve():
                    raise ValueError("asset must be a separate file in the source directory or below it")
            if "frames" in case:
                if "reference" in case or not isinstance(case["frames"], list) or not case["frames"]:
                    raise ValueError("use an ordered nonempty frames array or a single reference")
                for frame in case["frames"]:
                    if frame is not None:
                        local_path(frame, path.parent)
            if "reference" in case and case["reference"] != "@mergedimage":
                local_path(case["reference"], path.parent)
            if "reopenReference" in case:
                reopen = case["reopenReference"]
                if not isinstance(reopen, dict) or not isinstance(reopen.get("credit"), str) or not reopen["credit"].strip():
                    raise ValueError("reopenReference needs a PNG path and the name of the editor that reopened the file")
                if "frames" in case or "durationsMs" in case:
                    raise ValueError("reopenReference is a single still-image comparison")
                if local_path(reopen.get("png"), path.parent).suffix.lower() != ".png":
                    raise ValueError("reopenReference needs a PNG file")
            if case.get("reference") == "@mergedimage" and format_of(source.name) not in ("kra", "ora"):
                raise ValueError("@mergedimage requires a Krita or OpenRaster document")
            if "durationsMs" in case:
                delays = case["durationsMs"]
                if (not isinstance(delays, list) or len(delays) < 2
                        or any(type(n) is not int or n < 0 for n in delays) or "reference" in case):
                    raise ValueError("use durationsMs for multiple animation frames, with frames or without PNGs")
                if "frames" in case and len(delays) != len(case["frames"]):
                    raise ValueError("durationsMs and frames must have the same length")
        except (ValueError, OSError) as error:
            raise ValueError(f"case {pos}: {error}") from error
    return data


def _needs_masks(expect) -> bool:
    if isinstance(expect, dict):
        return "hasMask" in expect or any(_needs_masks(value) for value in expect.values())
    return isinstance(expect, list) and any(_needs_masks(value) for value in expect)


def _probe(case: dict, source: Path, cell: Path, exe: Path, settings: Path, timeout: int) -> dict:
    animated = max(len(case.get("frames", [])), len(case.get("durationsMs", []))) > 1
    log = cell / "probe.txt"
    checkpoint = cell / "checkpoint.json"
    args = [str(exe), "--headless", "--run-script", str(PROBE), "--script-output", str(log),
            "--script-arg", "input=" + source.as_posix(),
            "--script-arg", "output=" + (cell / "render.png").as_posix(),
            "--script-arg", "checkpoint=" + checkpoint.as_posix(),
            "--script-arg", "animated=" + ("yes" if animated else "no"),
            "--script-arg", "masks=" + ("yes" if _needs_masks(case.get("expect")) else "no")]
    if "linearSamples" in case:
        args += ["--script-arg", "floatOutput=" + (cell / "render32.tif").as_posix()]
    env = dict(os.environ, PATCHY_SETTINGS_DIR=str(settings), PATCHY_NO_SINGLE_INSTANCE="1")
    try:
        proc = subprocess.run(args, env=env, cwd=exe.parent, stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL, timeout=timeout)
        error = f"No probe result (Patchy exit {proc.returncode})."
    except subprocess.TimeoutExpired:
        error = f"Patchy timed out after {timeout} s."
    else:
        for line in (log.read_text(encoding="utf-8-sig", errors="replace").splitlines() if log.exists() else []):
            if line.startswith("TESTY_NATIVE "):
                observed = json.loads(line.removeprefix("TESTY_NATIVE "))
                if proc.returncode:
                    observed.update(exported=False, error=f"Patchy exited with code {proc.returncode}. "
                                    + observed.get("error", ""))
                return observed
    observed = json.loads(checkpoint.read_text(encoding="utf-8-sig")) if checkpoint.is_file() else {"opened": None}
    return {**observed, "exported": observed.get("exported", False), "error": error}


def measure(case: dict, cell: Path, manifest_dir: Path, exe: Path, settings: Path, timeout: int) -> dict:
    cell.mkdir(parents=True)
    source = local_path(case["source"], manifest_dir).resolve()
    row = {"id": case.get("id", case["source"]), "format": format_of(source.name), "source": source.name,
           "folder": "cells/" + cell.name, "opened": None, "exported": False, "notes": []}
    if case.get("referenceNote"):
        row["referenceNote"] = case["referenceNote"]
    if "linearSamples" in case:
        row["linearSamples"] = {"status": "unscored", "reason": "No float render to compare"}
    if "reopenReference" in case:
        row["reopenAppearance"] = {"status": "unscored", "reason": "No render to compare",
                                   "credit": case["reopenReference"]["credit"]}
    observed = {}
    try:
        with tempfile.TemporaryDirectory(prefix="patchy-native-input-", dir=cell) as temp:
            staged = Path(temp) / source.name
            shutil.copyfile(source, staged)
            row["sourceSHA256"] = file_digest(staged)
            for asset in case.get("assets", []):
                dep = local_path(asset, manifest_dir).resolve()
                dest = Path(temp) / dep.relative_to(source.parent)
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(dep, dest)
            observed = _probe(case, staged, cell, exe, settings, timeout)
            row.update({k: observed[k] for k in ("opened", "exported", "notes", "error") if k in observed})
            if file_digest(source) != row["sourceSHA256"]:
                raise ValueError("Source changed during this measurement.")
            row.update(score_case({**case, "source": str(staged)}, observed, cell, manifest_dir))
            if observed.get("opened") and observed.get("inspected") is False:
                for check in ("document", "structure"):
                    row[check] = {"status": "unscored", "reason": "Document inspection did not finish."}
    except (OSError, ValueError, RuntimeError) as error:
        row["error"] = str(error)
    row["observed"] = observed
    if row["exported"] and (cell / "render.png").is_file():
        row["render"] = row["folder"] + "/render.png"
    for check in ("document", "appearance", "structure", "timing"):
        if check not in row:
            row[check] = {"status": "unscored", "reason": row.get("error") or "No result."}
    return row


def run(manifest_path: Path, exe: Path, out: Path, timeout: int = 180, case_ids: list[str] | None = None) -> dict:
    manifest_path, exe, out = manifest_path.resolve(), exe.resolve(), out.resolve()
    manifest = load_manifest(manifest_path)
    cases = manifest["cases"]
    if case_ids:
        selected = set(case_ids)
        missing = selected.difference(case.get("id", case["source"]) for case in cases)
        if missing:
            raise ValueError("Unknown case IDs: " + ", ".join(sorted(missing)))
        cases = [case for case in cases if case.get("id", case["source"]) in selected]
    if not exe.is_file():
        raise ValueError(f"Patchy executable missing: {exe}")
    out.mkdir(parents=True, exist_ok=False)
    stamp = exe.stat()
    result = {"name": manifest.get("name") or manifest_path.stem, "state": "running", "startedAt": now(),
              "total": len(cases), "cases": [], "manifestSHA256": file_digest(manifest_path),
              "patchyHash": file_digest(exe), "patchyVersion": None,
              "sources": manifest.get("sources", [])}

    def progress():
        state = {k: result[k] for k in ("name", "state", "total")}
        write_json_atomic(out / "progress.json", {**state, "completed": len(result["cases"])})

    log(f"Native import report: {out / 'report.html'}")
    stop = os.environ.get("TESTY_SHUTDOWN_FILE")
    try:
        progress()
        with tempfile.TemporaryDirectory(prefix="patchy-native-settings-") as folder:
            for index, case in enumerate(cases, 1):
                if (stop and Path(stop).is_file()) or (out / "stop.flag").exists():
                    result["state"] = "stopped"
                    break
                current = exe.stat()
                if (current.st_mtime_ns, current.st_size) != (stamp.st_mtime_ns, stamp.st_size):
                    raise ValueError("Patchy executable changed during the run.")
                cell_name = f"{index:04}"
                settings = Path(folder) / cell_name
                settings.mkdir()
                row = measure(case, out / "cells" / cell_name, manifest_path.parent, exe, settings, timeout)
                result["cases"].append(row)
                result["patchyVersion"] = row["observed"].get("version") or result["patchyVersion"]
                checks = ("document", "appearance", "structure", "timing", "reopenAppearance", "linearSamples")
                log(f"{index}/{result['total']} {row['id']}: " + ", ".join(
                    f"{k} {row[k]['status']}" for k in checks if k in row))
                progress()
            else:
                result["state"] = "complete"
    except KeyboardInterrupt:
        result["state"] = "stopped"
    except Exception as error:
        result.update(state="failed", error=str(error))
        raise
    finally:
        result["finishedAt"] = now()
        write_json_atomic(out / "results.json", result)
        write_report(out, result)
        progress()
    return result


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path, help="native import JSON manifest")
    parser.add_argument("--patchy", type=Path, default=config.REPO_ROOT / "build/opt-dev/patchy.exe",
                        help="Patchy executable to measure (default: opt-dev)")
    parser.add_argument("--output", type=Path, help="new run directory (default: testy/runs/native-...)")
    parser.add_argument("--timeout", type=int, default=180, help="seconds allowed per Patchy process")
    parser.add_argument("--case", action="append", dest="case_ids", help="measure only this exact case ID (repeatable)")
    args = parser.parse_args(argv)
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    try:
        result = run(args.manifest, args.patchy, args.output or next_run_dir(), args.timeout, args.case_ids)
    except (OSError, ValueError) as error:
        parser.exit(2, f"Native import run failed: {error}\n")
    return 0 if result["state"] == "complete" else 1
