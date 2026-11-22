"""What a native import run measures beyond Patchy's own import: the other editors'
cells, fresh references, the Aseprite reopen of Patchy's sprite save, and Photoshop's
verdict on each converted PSD.

Editor output is cached in testy/cache/native by source hash and editor version, so
a rerun after a Patchy fix costs one column. Only a cell that opened, rendered and
(when asked) saved its PSD is cached; scores are computed again on every run.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import shutil
import tempfile

from PIL import Image

import config
from drivers import aseprite
import native_editors
import native_psd_score
from native_import_score import _score_image, score_case
from native_reference import References
from report import write_json_atomic


def file_digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def _unscored(reason: str) -> dict:
    return {"status": "unscored", "reason": reason}


def is_animation(case: dict) -> bool:
    return max(len(case.get("frames") or []), len(case.get("durationsMs") or [])) > 1


def first_frame_appearance(case: dict, folder: Path, manifest_dir: Path) -> dict:
    """An editor's single PNG against the still reference, or an animation's first frame."""
    still = {key: case[key] for key in ("source", "reference") if key in case}
    frames = case.get("frames")
    if frames and frames[0]:
        still["reference"] = frames[0]
    return score_case(still, {"opened": True, "exported": True}, folder, manifest_dir)["appearance"]


class Cross:
    def __init__(self, adapters: dict | None = None, missing: dict | None = None,
                 references: References | None = None, photoshop=None, *, psd_judge: bool = False,
                 fresh: bool = False, cache: Path | None = None):
        self.adapters = adapters or {}
        self.missing = missing or {}
        self.references = references or References()
        self.photoshop = photoshop
        self.psd_judge = psd_judge
        self.fresh = fresh
        self.cache = cache or config.CACHE_DIR / "native"

    @classmethod
    def create(cls, editors: list[str], *, psd_judge: bool, reference: str, fresh: bool,
               server_url: str | None, log) -> "Cross":
        adapters, missing = native_editors.build(editors, server_url)
        photoshop = None
        if psd_judge:
            from drivers.photoshop import PhotoshopDriver

            photoshop = PhotoshopDriver(log=log, install=config.photoshop_reference(config.photoshop_installs()))
        return cls(adapters, missing, References(reference), photoshop, psd_judge=psd_judge, fresh=fresh)

    def close(self) -> None:
        if "photopea" in self.adapters:
            from drivers import photopea

            photopea.cleanup()
        if self.photoshop is not None and hasattr(self.photoshop, "quit"):
            self.photoshop.quit()  # only a Photoshop this run launched

    def describe(self, patchy_version) -> dict:
        editors = {"patchy": {"name": "Patchy", "version": patchy_version or "unknown"}}
        for key, adapter in self.adapters.items():
            editors[key] = {"name": adapter.name, "version": adapter.version}
        facts = {"editors": editors, "unavailable": self.missing, "references": self.references.sources(),
                 "referenceMode": self.references.mode, "authoring": native_editors.AUTHORING}
        if self.psd_judge:
            facts["psdJudge"] = {"photoshop": self.photoshop.version()}
        return facts

    def patchy_outputs(self, case: dict, cell: Path, fmt: str) -> dict:
        """Extra files Patchy's own probe writes in the same process."""
        extra = {}
        if self.psd_judge:
            extra["psdOutput"] = cell / "patchy" / "converted.psd"
        if fmt == "aseprite" and "aseprite" in self.references.apps and not is_animation(case):
            extra["aseOutput"] = cell / "patchy" / "resaved.aseprite"
        if extra:
            (cell / "patchy").mkdir(parents=True, exist_ok=True)
        return extra

    def _store(self, adapter, key: str) -> Path:
        slug = re.sub(r"[^a-z0-9.]+", "-", f"{adapter.key}-{adapter.version}".lower()).strip("-")
        return self.cache / key[:2] / f"{key}-{slug}"

    def _convert(self, adapter, staged: Path, folder: Path, store: Path) -> dict:
        record = store / "cell.json"
        if not self.fresh and record.is_file():
            try:
                entry = json.loads(record.read_text(encoding="utf-8"))
                if entry["opened"] is False:
                    return {**entry, "cached": True}
                if not self.psd_judge or (store / "converted.psd").is_file():
                    folder.mkdir(parents=True, exist_ok=True)
                    for name in ("render.png", "converted.psd")[:2 if self.psd_judge else 1]:
                        shutil.copyfile(store / name, folder / name)
                    if not self.psd_judge:
                        entry.pop("savedPsd", None)
                    return {**entry, "cached": True}
            except (OSError, ValueError, KeyError):
                pass
        entry = native_editors.convert(adapter, staged, folder, self.psd_judge)
        done = entry["opened"] and entry["rendered"] and (not self.psd_judge or entry.get("savedPsd"))
        # A clean refusal is as repeatable as a success, and every file of a format an
        # editor cannot read is one. A crash or a hang is measured again.
        refused = entry["opened"] is False and not entry.get("appFailure")
        if done or refused:
            try:
                store.mkdir(parents=True, exist_ok=True)
                for name in ("render.png", "converted.psd"):
                    if done and (folder / name).is_file():
                        shutil.copyfile(folder / name, store / name)
                (store / "photoshop.json").unlink(missing_ok=True)
                write_json_atomic(record, entry)  # last: a folder without it is measured again
            except OSError:
                pass
        return entry

    def _photoshop_probe(self, folder: Path, store: Path | None) -> dict:
        psd, render = folder / "converted.psd", folder / "photoshop.png"
        digest, version = file_digest(psd), self.photoshop.version()
        record = store / "photoshop.json" if store is not None else None
        if record is not None and not self.fresh and record.is_file():
            try:
                saved = json.loads(record.read_text(encoding="utf-8"))
                if saved["psd"] == digest and saved["version"] == version:
                    if (store / "photoshop.png").is_file():
                        shutil.copyfile(store / "photoshop.png", render)
                    return saved["probe"]
            except (OSError, ValueError, KeyError):
                pass
        render.unlink(missing_ok=True)
        probe = self.photoshop.probe(psd, render)
        if record is not None and (store / "cell.json").is_file() and (probe.get("ok") or probe.get("fileRejected")):
            try:
                if render.is_file():
                    shutil.copyfile(render, store / "photoshop.png")
                write_json_atomic(record, {"psd": digest, "version": version, "probe": probe})
            except OSError:
                pass
        return probe

    def _judge(self, case: dict, folder: Path, manifest_dir: Path, store: Path | None) -> dict:
        if self.photoshop.unavailable:
            blank = _unscored(f"Photoshop is unavailable: {self.photoshop.unavailable_reason}")
            return {"accepted": blank, "appearance": blank, "layers": blank}
        probe = self._photoshop_probe(folder, store)
        return native_psd_score.score(probe, self.photoshop.version(), case, folder, manifest_dir)

    def _save_reopen(self, case: dict, observed: dict, folder: Path, manifest_dir: Path) -> dict:
        """Aseprite renders the .aseprite Patchy saved from a still sprite."""
        app = self.references.apps["aseprite"]
        credit = {"credit": f"Aseprite {app['version']}"}
        spec = (case.get("frames") or [case.get("reference")])[0]
        if not observed.get("aseSaved"):
            if observed.get("opened"):
                return {"status": "fail", "reason": observed.get("aseError") or "Patchy saved no .aseprite", **credit}
            return {**_unscored("Source did not open"), **credit}
        if not spec:
            return {**_unscored("No reference PNG"), **credit}
        try:
            with tempfile.TemporaryDirectory(prefix="patchy-native-reopen-", dir=folder) as temp:
                frames, error = aseprite.render_frames(app["exe"], folder / "resaved.aseprite", Path(temp))
                if len(frames) != 1:
                    return {"status": "fail", **credit,
                            "reason": error or f"Aseprite found {len(frames)} frames in Patchy's save"}
                shutil.copyfile(frames[0], folder / "reopened.png")
            with Image.open(folder / "reopened.png") as image:
                image.load()
                return {**_score_image(spec, case, manifest_dir, image, folder / "reopened.png", {},
                                       exact=True, capture=False), **credit}
        except Exception as error:
            return {**_unscored(f"Cannot compare Aseprite's render: {error}"), **credit}

    def measure(self, row: dict, case: dict, observed: dict, staged: Path, key: str, cell: Path,
                manifest_dir: Path) -> None:
        """Fill row["cells"]. `case` is the effective case, its source the staged copy."""
        fmt = row["format"]
        ineligible = (row.get("reference") or {}).get("ineligible")
        patchy = {"opened": row["opened"], "rendered": bool(row["exported"]), "notes": [],
                  "appearance": row["appearance"]}
        if row["exported"] and (cell / "render.png").is_file():
            patchy["render"] = row["folder"] + "/render.png"
        if self.psd_judge and observed.get("opened"):
            patchy["savedPsd"] = bool(observed.get("psdSaved")) and (cell / "patchy" / "converted.psd").is_file()
            if not patchy["savedPsd"]:
                patchy["psdError"] = observed.get("psdError") or "Patchy saved no PSD"
        if "aseOutput" in self.patchy_outputs(case, cell, fmt):
            row["saveReopen"] = self._save_reopen(case, observed, cell / "patchy", manifest_dir)
        cells = {"patchy": patchy}
        stores: dict[str, Path | None] = {"patchy": None}
        for name, adapter in self.adapters.items():
            stores[name] = self._store(adapter, key)
            entry = self._convert(adapter, staged, cell / name, stores[name])
            if entry["rendered"]:
                entry["render"] = f"{row['folder']}/{name}/render.png"
                entry["appearance"] = (_unscored(ineligible) if ineligible
                                       else first_frame_appearance(case, cell / name, manifest_dir))
                if is_animation(case):
                    entry["notes"] = [*entry["notes"], "Only the first frame of an animation is compared."]
            cells[name] = entry
        if self.psd_judge:
            for name, entry in cells.items():
                if entry.get("savedPsd"):
                    entry["psd"] = self._judge(case, cell / name, manifest_dir, stores.get(name))
        row["cells"] = cells
