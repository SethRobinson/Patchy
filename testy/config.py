"""Editor discovery and machine configuration for Testy.

Paths are probed at run time so the harness keeps working across editor upgrades
(and honestly reports editors that are missing rather than crashing).
"""

from __future__ import annotations

import os
import subprocess
from dataclasses import dataclass, field
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TESTY_ROOT = Path(__file__).resolve().parent
RUNS_DIR = TESTY_ROOT / "runs"
CACHE_DIR = TESTY_ROOT / "cache"

PATCHY_EXE = REPO_ROOT / "build" / "release" / "patchy.exe"

KRITA_CANDIDATES = [
    Path(r"C:\Program Files\Krita (x64)\bin\krita.com"),
    Path(r"C:\Program Files\Krita (x64)\bin\krita.exe"),
]

ASEPRITE_CANDIDATES = [
    Path(r"C:\Program Files\Aseprite\Aseprite.exe"),
    Path(r"C:\Program Files (x86)\Steam\steamapps\common\Aseprite\Aseprite.exe"),
]

AFFINITY_CANDIDATES = [
    Path(os.path.expandvars(r"%LOCALAPPDATA%\Microsoft\WindowsApps\Affinity.exe")),
]

# The whole suite assumes Photoshop is COM-registered; config only records a version label.
PHOTOSHOP_PROGID = "Photoshop.Application"


def _file_version(path: Path) -> str:
    """ProductVersion via PowerShell (no pywin32 version APIs needed)."""
    try:
        out = subprocess.run(
            [
                "powershell",
                "-NoProfile",
                "-Command",
                f"(Get-Item '{path}').VersionInfo.ProductVersion",
            ],
            capture_output=True,
            text=True,
            timeout=30,
        )
        version = (out.stdout or "").strip()
        return version if version else "unknown"
    except Exception:
        return "unknown"


@dataclass
class EditorInfo:
    key: str
    display_name: str
    exe: Path | None
    version: str = "unknown"
    available: bool = False
    notes: list[str] = field(default_factory=list)


def _first_existing(candidates: list[Path]) -> Path | None:
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


def discover_editors(patchy_git_hash: str) -> dict[str, EditorInfo]:
    editors: dict[str, EditorInfo] = {}

    patchy = EditorInfo("patchy", "Patchy", PATCHY_EXE if PATCHY_EXE.exists() else None)
    if patchy.exe is not None:
        patchy.available = True
        file_version = _file_version(patchy.exe)
        patchy.version = (f"{file_version} @ {patchy_git_hash}"
                          if file_version not in ("", "unknown") else patchy_git_hash)
    else:
        patchy.notes.append("build\\release\\patchy.exe missing - run the release build")
    editors["patchy"] = patchy

    krita = EditorInfo("krita", "Krita", _first_existing(KRITA_CANDIDATES))
    if krita.exe is not None:
        krita.available = True
        # krita.com is a console shim next to krita.exe; version rides the exe.
        krita.version = _file_version(krita.exe.with_name("krita.exe"))
    editors["krita"] = krita

    aseprite = EditorInfo("aseprite", "Aseprite", _first_existing(ASEPRITE_CANDIDATES))
    if aseprite.exe is not None:
        aseprite.available = True
        aseprite.version = _file_version(aseprite.exe)
    editors["aseprite"] = aseprite

    affinity = EditorInfo("affinity", "Affinity", _first_existing(AFFINITY_CANDIDATES))
    if affinity.exe is not None:
        affinity.available = True
        try:
            out = subprocess.run(
                [
                    "powershell",
                    "-NoProfile",
                    "-Command",
                    "(Get-AppxPackage Canva.Affinity).Version",
                ],
                capture_output=True,
                text=True,
                timeout=30,
            )
            version = (out.stdout or "").strip()
            affinity.version = version if version else "unknown"
        except Exception:
            pass
    editors["affinity"] = affinity

    photoshop = EditorInfo("photoshop", "Photoshop", None)
    photoshop.available = True  # verified when the COM driver connects
    editors["photoshop"] = photoshop

    return editors
