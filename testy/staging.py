"""Staging: every editor works on copies inside the run directory, never on corpus files."""

from __future__ import annotations

import hashlib
import shutil
from dataclasses import dataclass
from pathlib import Path

from psd_sections import PsdParseError, write_sentinel_composite


def sha1_of_file(path: Path) -> str:
    digest = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


@dataclass
class StagedPsd:
    source: Path
    sha1: str
    original: Path  # byte-identical copy the editors open
    trap: Path | None  # sentinel-composite variant (None if the walker balked)
    trap_error: str | None = None


def stage_psd(source: Path, staging_dir: Path) -> StagedPsd:
    staging_dir.mkdir(parents=True, exist_ok=True)
    suffix = source.suffix.lower() or ".psd"
    original = staging_dir / f"original{suffix}"
    shutil.copyfile(source, original)

    trap: Path | None = staging_dir / f"trap{suffix}"
    trap_error: str | None = None
    try:
        write_sentinel_composite(str(source), str(trap))
    except (PsdParseError, OSError) as error:
        trap = None
        trap_error = str(error)

    return StagedPsd(
        source=source,
        sha1=sha1_of_file(source),
        original=original,
        trap=trap,
        trap_error=trap_error,
    )
