#!/usr/bin/env python3
"""Writes themes/example-package.zip from themes/example-package/.

The theme guide (docs/making-themes.md) offers the zip as a download, so readers can
import it or unpack it and start from it. Run this after any change to the example
package and commit the result: ui_theme_package_icon_template_and_example_package fails
when the zip and the folder differ.

The zip is written the same way every time (sorted entries, one fixed date), so an
unchanged package gives an unchanged file. Its entries sit inside one top folder, which
Patchy's package loader accepts and which unpacks to a ready theme folder.

Usage: python scripts/dev/make-example-theme-zip.py
"""
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SOURCE = ROOT / "themes" / "example-package"
TARGET = ROOT / "themes" / "example-package.zip"
TOP_FOLDER = "Example Package"
# Already compressed: storing them keeps the zip small and quick to write.
STORED = {".png", ".jpg", ".jpeg"}
FIXED_DATE = (2026, 1, 1, 0, 0, 0)


def main() -> int:
    if not (SOURCE / "theme.json").is_file():
        print(f"no theme.json in {SOURCE}", file=sys.stderr)
        return 1
    files = sorted(p for p in SOURCE.rglob("*") if p.is_file())
    with zipfile.ZipFile(TARGET, "w") as archive:
        for path in files:
            name = f"{TOP_FOLDER}/{path.relative_to(SOURCE).as_posix()}"
            info = zipfile.ZipInfo(name, date_time=FIXED_DATE)
            info.compress_type = zipfile.ZIP_STORED if path.suffix.lower() in STORED else zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            archive.writestr(info, path.read_bytes(), compresslevel=9)
    print(f"wrote {TARGET.relative_to(ROOT).as_posix()} ({len(files)} files, {TARGET.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
