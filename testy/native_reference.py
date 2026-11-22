"""Reference renders from the installed authoring app, as Photoshop is for PSD.

With `--reference installed`, GIMP renders each XCF and Aseprite each sprite frame,
cached by source hash and app version. The PNGs supplied with a corpus then become
a cross-check: when the two disagree by the rule that scores an editor, nobody can
say which one an editor should match, so the file's appearance is not scored.
KRA and OpenRaster keep the composite saved in the file (Krita's command-line
export draws fill and text layers blank, docs/kra.md).

GIMP and Aseprite run only as command-line tools; nothing here reads their source.
"""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from PIL import Image

import config
from drivers import aseprite, winproc
import native_import_score as score

GIMP_TIMEOUT_SECONDS = 300
MODES = ("pinned", "installed")
_SAVED = "Composite saved in the file"
_SUPPLIED = "PNG supplied with the corpus"


def _gimp_script() -> str:
    """The reference render docs/xcf.md describes: sRGB, visible layers merged and
    clipped to the canvas, 16-bit for deeper images."""
    path = config.REPO_ROOT / "scripts" / "dev" / "xcf" / "render_with_gimp.py"
    spec = importlib.util.spec_from_file_location("patchy_render_with_gimp", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.SCRIPT


def _gimp_render(exe: Path, source: Path, folder: Path) -> tuple[list[Path], str]:
    target = folder / "frame-1.png"
    script = folder / "render.py"
    script.write_text(_gimp_script().format(jobs=[(source.as_posix(), target.as_posix())],
                                            out=folder.as_posix()), encoding="utf-8")
    command = [str(exe), "-i", "--batch-interpreter=python-fu-eval",
               "-b", f"exec(open(r'{script}', encoding='utf-8').read())",
               "-b", "Gimp.get_pdb().lookup_procedure('gimp-quit').run(None)"]
    try:
        with winproc.suppressed_error_dialogs():
            process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except OSError as error:
        return [], f"GIMP did not start: {error}"
    try:
        process.communicate(timeout=GIMP_TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired:
        # The batch runs in a plug-in process: kill the tree, not only the console.
        subprocess.run(["taskkill", "/f", "/t", "/pid", str(process.pid)], capture_output=True)
        process.communicate()
        return [], f"GIMP timed out after {GIMP_TIMEOUT_SECONDS} s"
    if target.is_file() and target.stat().st_size:
        return [target], ""
    failures = folder / "failures.txt"
    said = failures.read_text(encoding="utf-8", errors="replace").strip() if failures.is_file() else ""
    return [], said[-500:] or "GIMP wrote no render"


class References:
    def __init__(self, mode: str = "pinned", cache: Path | None = None):
        if mode not in MODES:
            raise ValueError("--reference takes pinned or installed")
        self.mode = mode
        self.cache = cache or config.CACHE_DIR / "native-ref"
        self.apps: dict[str, dict] = {}
        if mode == "installed":
            gimp = config.reference_app("gimp")
            if gimp:
                self.apps["xcf"] = {"name": "GIMP", "exe": gimp, "version": config._file_version(gimp),
                                    "render": _gimp_render}
            sprites = config.reference_app("aseprite")
            if sprites:
                self.apps["aseprite"] = {"name": "Aseprite", "exe": sprites, "render": aseprite.render_frames,
                                         "version": aseprite.version(sprites) or config._file_version(sprites)}

    def label(self, fmt: str) -> str | None:
        app = self.apps.get(fmt)
        return f"{app['name']} {app['version']} render" if app else None

    def sources(self) -> dict[str, str]:
        """Where each format's reference comes from, for the report."""
        return {fmt: self.label(fmt) or (_SAVED if fmt in ("kra", "ora") else _SUPPLIED)
                for fmt in ("xcf", "kra", "ora", "aseprite")}

    def _frames(self, app: dict, source: Path, digest: str) -> tuple[list[Path], str]:
        slug = re.sub(r"[^a-z0-9.]+", "-", f"{app['name']}-{app['version']}".lower()).strip("-")
        folder = self.cache / digest[:2] / f"{digest}-{slug}"
        index = folder / "frames.json"
        if index.is_file():
            try:
                frames = [folder / name for name in json.loads(index.read_text(encoding="utf-8"))]
                if frames and all(frame.is_file() for frame in frames):
                    return frames, ""
            except (OSError, ValueError, TypeError):
                pass
        self.cache.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="patchy-native-ref-", dir=self.cache) as temp:
            rendered, error = app["render"](app["exe"], source, Path(temp))
            if not rendered:
                return [], error
            folder.mkdir(parents=True, exist_ok=True)
            names = [f"frame-{number:04}.png" for number in range(1, len(rendered) + 1)]
            for frame, name in zip(rendered, names):
                shutil.copyfile(frame, folder / name)
            # Written last: a folder without its index is rendered again.
            index.write_text(json.dumps(names), encoding="utf-8")
        return [folder / name for name in names], ""

    def resolve(self, case: dict, staged: Path, manifest_dir: Path, fmt: str, digest: str) -> tuple[dict, dict | None]:
        """(the case with the references to score against, what was decided and why)."""
        app = self.apps.get(fmt)
        if app is None:
            return case, None
        label = f"{app['name']} {app['version']}"
        try:
            frames, error = self._frames(app, staged, digest)
        except OSError as failure:
            frames, error = [], str(failure)
        supplied = case.get("frames") or ([case["reference"]] if case.get("reference") else [])
        expected = max(len(supplied), len(case.get("durationsMs", [])))
        if frames and expected > 1 and expected != len(frames):
            frames, error = [], f"it rendered {len(frames)} frames where the manifest lists {expected}"
        if not frames:
            return case, {"source": _SUPPLIED, "note": f"{label} gave no reference for this file ({error})."}
        info = {"source": f"{label} render", "crossCheck": "none"}
        plain = {key: value for key, value in case.items() if key not in ("frames", "reference")}
        for number, (spec, frame) in enumerate(zip(supplied, frames), 1):
            if spec is None:
                continue
            try:
                with Image.open(frame) as render:
                    render.load()
                    checked = score._score_image(spec, {**case, "source": str(staged)}, manifest_dir, render,
                                                 frame, {}, fmt == "aseprite", capture=False)
            except Exception as failure:
                info["note"] = f"The supplied reference could not be compared: {failure}"
                continue
            if checked["status"] == "fail":
                info["crossCheck"] = "differs"
                info["ineligible"] = (f"{label} and the reference supplied with the corpus disagree "
                                      f"(frame {number}: {checked.get('reason', 'different pixels')}).")
                return plain, info
            if checked["status"] == "pass":
                info["crossCheck"] = "agrees"
        if len(frames) > 1:
            plain["frames"] = [str(frame) for frame in frames]
        else:
            plain["reference"] = str(frames[0])
        return plain, info
