"""Editor discovery and machine configuration for Testy.

Machine-specific settings live in testy/config.local.json (gitignored; see
config.example.json for the schema): python path, dashboard port, corpus source,
explicit editor paths, and the Patchy build command. Everything not configured
falls back to discovery against standard install locations, so the harness keeps
working across editor upgrades and on other people's machines, and honestly
reports editors that are missing rather than crashing.
"""

from __future__ import annotations

import json
import os
import subprocess
from dataclasses import dataclass, field
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TESTY_ROOT = Path(__file__).resolve().parent
RUNS_DIR = TESTY_ROOT / "runs"
CACHE_DIR = TESTY_ROOT / "cache"
CONFIG_PATH = TESTY_ROOT / "config.local.json"


def _load_local_config() -> dict:
    try:
        return json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return {}
    except Exception as error:
        print(f"[testy] WARNING: {CONFIG_PATH.name} is unreadable ({error}); using defaults")
        return {}


LOCAL = _load_local_config()
PORT = int(LOCAL.get("port", 8901))
PHOTOSHOP_PROGID = (LOCAL.get("editors") or {}).get("photoshop_progid") or "Photoshop.Application"
BUILD_COMMAND = LOCAL.get("build_command") or ""


def _configured_editor_path(key: str) -> Path | None:
    value = (LOCAL.get("editors") or {}).get(key) or ""
    if not value:
        return None
    path = Path(os.path.expandvars(value))
    return path if path.exists() else None


def default_corpus() -> list[Path]:
    """The configured default corpus: corpus_file, else corpus_dir, else the repo's
    local-test-fixtures/psd directory when present."""
    corpus_file = LOCAL.get("corpus_file") or ""
    if corpus_file:
        path = Path(corpus_file)
        if not path.is_absolute():
            path = TESTY_ROOT / path
        if path.exists():
            files: list[Path] = []
            for line in path.read_text(encoding="utf-8").splitlines():
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                entry = Path(line)
                if not entry.is_absolute():
                    entry = REPO_ROOT / entry
                files.append(entry.resolve())
            return files
        print(f"[testy] WARNING: configured corpus_file not found: {path}")
    corpus_dir = LOCAL.get("corpus_dir") or ""
    scan_dir = Path(os.path.expandvars(corpus_dir)) if corpus_dir else REPO_ROOT / "local-test-fixtures" / "psd"
    if scan_dir.is_dir():
        return sorted(p.resolve() for ext in ("*.psd", "*.psb") for p in scan_dir.glob(ext))
    return []


PATCHY_EXE = _configured_editor_path("patchy") or REPO_ROOT / "build" / "release" / "patchy.exe"

KRITA_CANDIDATES = [
    Path(r"C:\Program Files\Krita (x64)\bin\krita.com"),
    Path(r"C:\Program Files\Krita (x64)\bin\krita.exe"),
]

AFFINITY_CANDIDATES = [
    Path(os.path.expandvars(r"%LOCALAPPDATA%\Microsoft\WindowsApps\Affinity.exe")),
]

CHROME_CANDIDATES = [
    Path(r"C:\Program Files\Google\Chrome\Application\chrome.exe"),
    Path(r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe"),
]

# The whole suite assumes Photoshop is COM-registered (PHOTOSHOP_PROGID above).


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

    krita = EditorInfo("krita", "Krita",
                       _configured_editor_path("krita") or _first_existing(KRITA_CANDIDATES))
    if krita.exe is not None:
        krita.available = True
        # krita.com is a console shim next to krita.exe; version rides the exe.
        krita.version = _file_version(krita.exe.with_name("krita.exe"))
    editors["krita"] = krita

    affinity = EditorInfo("affinity", "Affinity",
                          _configured_editor_path("affinity") or _first_existing(AFFINITY_CANDIDATES))
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

    # Photopea runs inside a headless Chrome via its embedding API; available when
    # Chrome is installed (needs internet at run time to load photopea.com).
    photopea = EditorInfo("photopea", "Photopea",
                          _configured_editor_path("chrome") or _first_existing(CHROME_CANDIDATES))
    if photopea.exe is not None:
        photopea.available = True
        photopea.version = "web (Chrome host)"
        photopea.notes.append("loads photopea.com at run time")
    editors["photopea"] = photopea

    return editors
