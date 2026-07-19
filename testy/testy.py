"""Testy: PSD compatibility benchmark across the editors installed on this machine.

Photoshop is ground truth. For every corpus PSD and every editor, Testy measures:
opening, honest rendering accuracy (with a sentinel-composite trap against baked
flat previews), native object preservation after a resave (checked by reopening
the editor's PSD in Photoshop), the round-trip render, and a forced text
re-render where the editor is scriptable. A live browser dashboard shows the
matrix filling in; results persist per run for over-time comparison.

Run with the miniconda python (pywin32/Pillow/numpy/pywinauto preinstalled):
  C:\\Users\\Seth\\miniconda3\\python.exe testy\\testy.py [options]
See docs/testy.md.
"""

from __future__ import annotations

import argparse
import datetime as _dt
import functools
import http.server
import json
import re
import shutil
import socket
import subprocess
import sys
import threading
import webbrowser
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import analyze
import config
import manifest as manifest_mod
import report
import staging
from drivers import aseprite as aseprite_driver
from drivers import krita as krita_driver
from drivers import patchy as patchy_driver
from drivers.photoshop import PhotoshopDriver

DEFAULT_SUFFIX = "~TESTY~"
# Affinity is deliberately opt-in (--editors photoshop,patchy,krita,aseprite,affinity):
# its driver is background-UIA best-effort and the app's cold-start timing is flaky,
# so default runs stay fast and reliable without it.
DEFAULT_EDITORS = ["photoshop", "patchy", "krita", "aseprite"]

BUILD_COMMAND = (
    '"C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\Common7\\Tools\\VsDevCmd.bat"'
    " -arch=x64 -host_arch=x64 >nul && "
    '"C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\Common7\\IDE\\CommonExtensions\\'
    'Microsoft\\CMake\\CMake\\bin\\cmake.exe" --build --preset release'
)


def log(message: str) -> None:
    print(f"[testy] {message}", flush=True)


def git_hash() -> str:
    try:
        head = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"], cwd=config.REPO_ROOT,
            capture_output=True, text=True, timeout=30,
        ).stdout.strip()
        dirty = subprocess.run(
            ["git", "status", "--porcelain"], cwd=config.REPO_ROOT,
            capture_output=True, text=True, timeout=30,
        ).stdout.strip()
        return head + ("+dirty" if dirty else "")
    except Exception:
        return "unknown"


def refresh_patchy_build() -> bool:
    """Run the canonical release build; trust compile/link evidence, not exit codes."""
    log("refreshing Patchy release build (use --no-build to skip)...")
    completed = subprocess.run(
        ["cmd", "/s", "/c", BUILD_COMMAND], cwd=config.REPO_ROOT,
        capture_output=True, text=True, timeout=1200,
    )
    output = (completed.stdout or "") + (completed.stderr or "")
    built = ("ninja: no work to do" in output) or ("Linking CXX executable" in output) or (
        "Building CXX object" in output
    )
    if not built:
        log("WARNING: build produced no compile/link evidence; patchy.exe may be stale")
        log(output[-1500:])
    return built


def resolve_corpus(args: argparse.Namespace) -> list[Path]:
    files: list[Path] = []
    if args.files:
        for entry in args.files:
            files.append(Path(entry).resolve())
    else:
        corpus_path = Path(args.corpus)
        if not corpus_path.is_absolute():
            corpus_path = config.TESTY_ROOT / corpus_path
        for line in corpus_path.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            path = Path(line)
            if not path.is_absolute():
                path = config.REPO_ROOT / path
            files.append(path.resolve())
    missing = [f for f in files if not f.exists()]
    for f in missing:
        log(f"WARNING: corpus file missing, skipped: {f}")
    return [f for f in files if f.exists()]


def start_server(port: int) -> tuple[http.server.ThreadingHTTPServer, int]:
    handler = functools.partial(
        http.server.SimpleHTTPRequestHandler, directory=str(config.TESTY_ROOT)
    )
    handler.log_message = lambda *a, **k: None  # type: ignore[attr-defined]
    for candidate in range(port, port + 20):
        try:
            server = http.server.ThreadingHTTPServer(("127.0.0.1", candidate), handler)
        except OSError:
            continue
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        return server, candidate
    raise RuntimeError("no free port found for the dashboard")


def _thumb(source: Path, out: Path) -> str | None:
    try:
        analyze.make_thumbnail(source, out)
        return out.name
    except Exception:
        return None


def _version_slug(text: str) -> str:
    return re.sub(r"[^A-Za-z0-9.@+-]+", "_", text)[:60]


class Runner:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.suffix = args.suffix
        self.patchy_hash = git_hash()
        self.editors = config.discover_editors(self.patchy_hash)
        self.editor_order = [e for e in args.editors.split(",") if e in self.editors]
        self.ps = PhotoshopDriver()
        timestamp = _dt.datetime.now().strftime("%Y%m%d-%H%M%S")
        self.run_dir = config.RUNS_DIR / timestamp
        self.files_dir = self.run_dir / "files"
        self.files_dir.mkdir(parents=True, exist_ok=True)
        config.CACHE_DIR.mkdir(parents=True, exist_ok=True)
        self.run_name = timestamp
        self.status: dict = {}

    # ---------- status plumbing ----------

    def init_status(self, corpus: list[Path]) -> None:
        self.status = {
            "state": "running",
            "run": {
                "startedAt": _dt.datetime.now().isoformat(timespec="seconds"),
                "patchyVersion": self.editors["patchy"].version,
                "patchyGit": self.patchy_hash,
                "suffix": self.suffix,
                "editorOrder": self.editor_order,
                "name": self.run_name,
            },
            "editors": {
                key: {
                    "displayName": info.display_name,
                    "version": info.version,
                    "available": info.available,
                    "notes": info.notes,
                }
                for key, info in self.editors.items()
            },
            "files": [
                {
                    "name": path.name,
                    "source": str(path),
                    "groundTruth": {"state": "pending"},
                    "cells": {key: {"state": "pending"} for key in self.editor_order},
                }
                for path in corpus
            ],
        }
        self.push()

    def push(self) -> None:
        report.write_status(self.run_dir, self.status)

    def file_entry(self, index: int) -> dict:
        return self.status["files"][index]

    # ---------- ground truth ----------

    def ground_truth(self, index: int, staged: staging.StagedPsd) -> dict | None:
        entry = self.file_entry(index)
        gt_dir = self.files_dir / Path(entry["name"]).stem / "_truth"
        gt_dir.mkdir(parents=True, exist_ok=True)
        cache_dir = config.CACHE_DIR / f"gt-{staged.sha1}-{_version_slug(self.ps.version())}-{self.suffix}"
        result_path = cache_dir / "result.json"

        entry["groundTruth"] = {"state": "running", "stage": "Photoshop ground truth"}
        self.push()

        if result_path.exists() and not self.args.fresh:
            result = json.loads(result_path.read_text(encoding="utf-8"))
            for name in ("render.png", "mutated.png"):
                if (cache_dir / name).exists():
                    shutil.copyfile(cache_dir / name, gt_dir / name)
        else:
            result = self.ps.probe(
                staged.original,
                gt_dir / "render.png",
                mutate_suffix=self.suffix,
                mutated_png=gt_dir / "mutated.png",
            )
            if result.get("ok"):
                cache_dir.mkdir(parents=True, exist_ok=True)
                result_path.write_text(json.dumps(result), encoding="utf-8")
                for name in ("render.png", "mutated.png"):
                    if (gt_dir / name).exists():
                        shutil.copyfile(gt_dir / name, cache_dir / name)

        if not result.get("ok"):
            entry["groundTruth"] = {"state": "failed", "error": result.get("error", "unknown")}
            self.push()
            return None

        entry["docSize"] = [int(result["width"]), int(result["height"])]
        entry["layerCount"] = len(result["layers"])
        artifacts = {}
        if (gt_dir / "render.png").exists():
            artifacts["render"] = self._rel(gt_dir / "render.png")
            thumb = _thumb(gt_dir / "render.png", gt_dir / "render_thumb.png")
            if thumb:
                artifacts["renderThumb"] = self._rel(gt_dir / thumb)
        if (gt_dir / "mutated.png").exists():
            artifacts["mutated"] = self._rel(gt_dir / "mutated.png")
            thumb = _thumb(gt_dir / "mutated.png", gt_dir / "mutated_thumb.png")
            if thumb:
                artifacts["mutatedThumb"] = self._rel(gt_dir / thumb)
        entry["groundTruth"] = {
            "state": "done",
            "render": result.get("render"),
            "mutated": result.get("mutated"),
            "mutateCount": result.get("mutateCount"),
            "artifacts": artifacts,
        }
        (gt_dir / "manifest.json").write_text(json.dumps(result["layers"], indent=1), encoding="utf-8")
        self.push()
        return result

    def _rel(self, path: Path) -> str:
        return str(path.relative_to(self.run_dir)).replace("\\", "/")

    # ---------- editor cells ----------

    def run_cell(self, index: int, editor_key: str, staged: staging.StagedPsd, truth: dict | None) -> None:
        entry = self.file_entry(index)
        cell = entry["cells"][editor_key]
        info = self.editors[editor_key]
        cell_dir = self.files_dir / Path(entry["name"]).stem / editor_key
        cell_dir.mkdir(parents=True, exist_ok=True)

        if not info.available:
            cell.update({"state": "failed", "error": "editor not found on this machine"})
            self.push()
            return

        version_key = self.patchy_hash if editor_key == "patchy" else info.version
        cache_dir = config.CACHE_DIR / (
            f"cell-{staged.sha1}-{editor_key}-{_version_slug(version_key)}-{self.suffix}"
        )
        if cache_dir.exists() and not self.args.fresh:
            cached_cell = json.loads((cache_dir / "cell.json").read_text(encoding="utf-8"))
            for item in cache_dir.iterdir():
                if item.name != "cell.json":
                    shutil.copyfile(item, cell_dir / item.name)
            cell.clear()
            cell.update(cached_cell)
            cell["cached"] = True
            self.push()
            return

        cell.update({"state": "running", "stage": "opening + exporting"})
        self.push()

        render_png = cell_dir / "render.png"
        resave_psd = cell_dir / "resave.psd"
        trap_png = cell_dir / "trap.png"
        mutated_png = cell_dir / "mutated.png"
        artifacts: dict = {}
        cell["artifacts"] = artifacts

        try:
            self._drive_editor(editor_key, info, staged, cell, render_png, resave_psd, trap_png, mutated_png)
        except Exception as error:
            cell.update({"state": "failed", "error": f"driver error: {error}"})
            self.push()
            return

        if cell.get("state") in ("failed", "unsupported"):
            self.push()
            return

        for path, key, thumb_key in (
            (render_png, "render", "renderThumb"),
            (trap_png, "trap", "trapThumb"),
            (mutated_png, "mutated", "mutatedThumb"),
        ):
            if path.exists():
                artifacts[key] = self._rel(path)
                thumb = _thumb(path, path.with_name(path.stem + "_thumb.png"))
                if thumb:
                    artifacts[thumb_key] = self._rel(path.with_name(thumb))
        if resave_psd.exists():
            artifacts["resavePsd"] = self._rel(resave_psd)

        truth_render = self.files_dir / Path(entry["name"]).stem / "_truth" / "render.png"
        document_size = tuple(entry.get("docSize", (0, 0)))

        if truth is not None and render_png.exists() and truth_render.exists() and document_size[0]:
            cell["stage"] = "comparing renders"
            self.push()
            cell["renderMetrics"] = analyze.compare_renders(
                truth_render, render_png, document_size, truth["layers"], cell_dir / "heatmap.png"
            )
            if (cell_dir / "heatmap.png").exists():
                artifacts["heatmap"] = self._rel(cell_dir / "heatmap.png")

        if trap_png.exists():
            cell["trapSentinelFraction"] = round(analyze.sentinel_fraction(trap_png), 4)

        truth_mutated = self.files_dir / Path(entry["name"]).stem / "_truth" / "mutated.png"
        if mutated_png.exists() and truth_mutated.exists() and truth is not None and document_size[0]:
            text_objects = [l for l in truth["layers"] if l.get("kind") == "TEXT"]
            cell["textRender"] = analyze.compare_renders(
                truth_mutated, mutated_png, document_size, text_objects, None
            )

        if resave_psd.exists() and truth is not None:
            cell["stage"] = "reopening resave in Photoshop"
            self.push()
            roundtrip = self.ps.probe(resave_psd, cell_dir / "roundtrip.png")
            if roundtrip.get("ok"):
                (cell_dir / "roundtrip_manifest.json").write_text(
                    json.dumps(roundtrip["layers"], indent=1), encoding="utf-8"
                )
                cell["native"] = manifest_mod.compare_manifests(truth["layers"], roundtrip["layers"])
                if (cell_dir / "roundtrip.png").exists():
                    artifacts["roundtripRender"] = self._rel(cell_dir / "roundtrip.png")
                    thumb = _thumb(cell_dir / "roundtrip.png", cell_dir / "roundtrip_thumb.png")
                    if thumb:
                        artifacts["roundtripThumb"] = self._rel(cell_dir / "roundtrip_thumb.png")
                    if truth_render.exists() and document_size[0]:
                        cell["roundtripRender"] = analyze.compare_renders(
                            truth_render, cell_dir / "roundtrip.png", document_size, [], None
                        )
            else:
                cell["native"] = {"error": f"Photoshop could not open the resave: {roundtrip.get('error')}"}
                cell["resaveRejected"] = True

        cell["state"] = "done"
        cell.pop("stage", None)
        self.push()

        cache_dir.mkdir(parents=True, exist_ok=True)
        for item in cell_dir.iterdir():
            shutil.copyfile(item, cache_dir / item.name)
        cacheable = {k: v for k, v in cell.items() if k != "cached"}
        (cache_dir / "cell.json").write_text(json.dumps(cacheable), encoding="utf-8")

    def _drive_editor(
        self,
        editor_key: str,
        info: config.EditorInfo,
        staged: staging.StagedPsd,
        cell: dict,
        render_png: Path,
        resave_psd: Path,
        trap_png: Path,
        mutated_png: Path,
    ) -> None:
        if editor_key == "photoshop":
            result = self.ps.probe(staged.original, render_png, resave_psd=resave_psd)
            if not result.get("ok"):
                cell.update({"state": "failed", "opens": "fail", "error": result.get("error")})
                return
            cell["opens"] = "ok" if result.get("render") == "ok" else "fallback-render"
            if staged.trap is not None:
                trap_result = self.ps.probe(staged.trap, trap_png)
                if not trap_result.get("ok"):
                    cell["trapError"] = trap_result.get("error")
            return

        if editor_key == "patchy":
            exported = patchy_driver.export(info.exe, staged.original, render_png)
            if not exported["ok"]:
                cell.update({"state": "failed", "opens": "fail", "error": exported["stderr"] or "export failed"})
                return
            cell["opens"] = "ok"
            resaved = patchy_driver.export(info.exe, staged.original, resave_psd)
            if not resaved["ok"]:
                cell["resaveError"] = resaved["stderr"] or "resave failed"
            if staged.trap is not None:
                patchy_driver.export(info.exe, staged.trap, trap_png)
            mutated = patchy_driver.export(info.exe, staged.original, mutated_png, append_text=self.suffix)
            if not mutated["ok"]:
                cell["mutateError"] = mutated["stderr"] or "append-text export failed"
            return

        if editor_key == "krita":
            exported = krita_driver.export(info.exe, staged.original, render_png)
            if not exported["ok"]:
                cell.update({"state": "failed", "opens": "fail", "error": exported["stderr"] or "export failed"})
                return
            cell["opens"] = "ok"
            resaved = krita_driver.export(info.exe, staged.original, resave_psd)
            if not resaved["ok"]:
                cell["resaveError"] = resaved["stderr"] or "resave failed"
            if staged.trap is not None:
                krita_driver.export(info.exe, staged.trap, trap_png)
            return

        if editor_key == "aseprite":
            exported = aseprite_driver.export(info.exe, staged.original, render_png)
            if exported.get("unsupported"):
                cell.update({"state": "unsupported", "opens": "fail",
                             "error": "Aseprite has no PSD import (verified this run)"})
                return
            if not exported["ok"]:
                cell.update({"state": "failed", "opens": "fail", "error": exported["stderr"] or "export failed"})
                return
            cell["opens"] = "ok"
            aseprite_driver.export(info.exe, staged.original, resave_psd)
            if staged.trap is not None:
                aseprite_driver.export(info.exe, staged.trap, trap_png)
            return

        if editor_key == "affinity":
            from drivers import affinity as affinity_driver

            result = affinity_driver.export_all(
                info.exe, staged.original, staged.trap, render_png, resave_psd, trap_png,
                progress=lambda stage: (cell.__setitem__("stage", stage), self.push()),
            )
            if not result["ok"]:
                cell.update({"state": "failed", "opens": result.get("opens", "fail"),
                             "error": result.get("error", "automation failed")})
                return
            cell["opens"] = result.get("opens", "ok")
            if result.get("notes"):
                cell["driverNotes"] = result["notes"]
            return

        cell.update({"state": "failed", "error": f"no driver for editor '{editor_key}'"})

    # ---------- top level ----------

    def run(self) -> int:
        corpus = resolve_corpus(self.args)
        if not corpus:
            log("no corpus files found; nothing to do")
            return 2
        report.write_report_page(self.run_dir)
        self.init_status(corpus)

        server = None
        if not self.args.no_serve:
            server, port = start_server(self.args.port)
            url = f"http://127.0.0.1:{port}/runs/{self.run_name}/report.html"
            log(f"dashboard: {url}")
            if not self.args.no_browser:
                webbrowser.open(url)

        if not self.args.no_build and "patchy" in self.editor_order:
            refresh_patchy_build()
            self.patchy_hash = git_hash()
            self.editors = config.discover_editors(self.patchy_hash)
            info = self.editors["patchy"]
            self.status["editors"]["patchy"].update(
                {"version": info.version, "available": info.available, "notes": info.notes}
            )
            self.status["run"]["patchyVersion"] = info.version
            self.status["run"]["patchyGit"] = self.patchy_hash
            self.push()

        source_hashes = {str(path): staging.sha1_of_file(path) for path in corpus}

        for index, source in enumerate(corpus):
            entry = self.file_entry(index)
            log(f"[{index + 1}/{len(corpus)}] {source.name}")
            staged = staging.stage_psd(source, self.files_dir / source.stem / "_staged")
            entry["sha1"] = staged.sha1
            if staged.trap_error:
                entry["trapError"] = staged.trap_error
            truth = self.ground_truth(index, staged)
            for editor_key in self.editor_order:
                log(f"    {editor_key}...")
                self.run_cell(index, editor_key, staged, truth)

        # Prove the corpus originals were never touched.
        corruption = [
            path for path in corpus if staging.sha1_of_file(path) != source_hashes[str(path)]
        ]
        if corruption:
            log(f"ERROR: source files changed during the run: {corruption}")
        self.status["run"]["sourcesUntouched"] = not corruption

        if "affinity" in self.editor_order:
            from drivers import affinity as affinity_driver

            affinity_driver.cleanup()

        self.status["state"] = "done"
        self.status["run"]["finishedAt"] = _dt.datetime.now().isoformat(timespec="seconds")
        self.push()
        (self.run_dir / "results.json").write_text(json.dumps(self.status, indent=1), encoding="utf-8")
        report.append_history(config.TESTY_ROOT, self._history_summary())
        self._print_summary()

        if server is not None and not self.args.exit_when_done:
            log("run complete - dashboard stays up; press Enter to quit")
            try:
                input()
            except (EOFError, KeyboardInterrupt):
                pass
        return 0

    def _aggregate(self) -> dict:
        aggregate: dict = {}
        for editor_key in self.editor_order:
            opened = total = 0
            render_scores: list[float] = []
            native_scores: list[float] = []
            for entry in self.status["files"]:
                cell = entry["cells"].get(editor_key)
                if not cell or cell.get("state") in ("pending", "running"):
                    continue
                total += 1
                if cell.get("state") == "done" and cell.get("opens") != "fail":
                    opened += 1
                metrics = cell.get("renderMetrics")
                if metrics:
                    render_scores.append(metrics["accuracy"])
                native = cell.get("native")
                if native and "nativeScore" in native:
                    native_scores.append(native["nativeScore"])
            aggregate[editor_key] = {
                "opened": opened,
                "total": total,
                "render": sum(render_scores) / len(render_scores) if render_scores else 0.0,
                "native": sum(native_scores) / len(native_scores) if native_scores else 0.0,
            }
        return aggregate

    def _history_summary(self) -> dict:
        return {
            "run": self.run_name,
            "files": len(self.status["files"]),
            "patchy": self.patchy_hash,
            "editors": self._aggregate(),
        }

    def _print_summary(self) -> None:
        aggregate = self._aggregate()
        log("summary (mean render accuracy / mean native preservation / opened):")
        for editor_key in self.editor_order:
            a = aggregate[editor_key]
            log(
                f"  {self.editors[editor_key].display_name:<10} "
                f"render {a['render'] * 100:5.1f}%   native {a['native'] * 100:5.1f}%   "
                f"opened {a['opened']}/{a['total']}"
            )
        log(f"report: {self.run_dir / 'report.html'}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Testy PSD compatibility benchmark")
    parser.add_argument("--files", nargs="*", help="explicit PSD paths (overrides --corpus)")
    parser.add_argument("--corpus", default="corpus/default.txt", help="corpus list file")
    parser.add_argument("--editors", default=",".join(DEFAULT_EDITORS),
                        help="comma-separated editor keys to run")
    parser.add_argument("--suffix", default=DEFAULT_SUFFIX, help="text appended by the forced re-render test")
    parser.add_argument("--no-build", action="store_true", help="skip the Patchy release build refresh")
    parser.add_argument("--fresh", action="store_true", help="ignore cached ground truth / cells")
    parser.add_argument("--port", type=int, default=8765, help="dashboard port")
    parser.add_argument("--no-browser", action="store_true", help="do not auto-open the dashboard")
    parser.add_argument("--no-serve", action="store_true", help="write reports without the local server")
    parser.add_argument("--exit-when-done", action="store_true", help="do not wait for Enter at the end")
    args = parser.parse_args()
    return Runner(args).run()


if __name__ == "__main__":
    sys.exit(main())
