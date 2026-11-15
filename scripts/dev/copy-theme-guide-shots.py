#!/usr/bin/env python3
"""Copies the theme guide's pictures from a UI test run into docs/images/themes/.

The ui_theme_guide_shot_* scenes of patchy_ui_visual_tests (tests/ui/theme_guide_screenshot_tests.cpp)
write test-artifacts/theme_guide_<name>.png; the guide (docs/making-themes.md and its
advanced pages) shows docs/images/themes/<name>.png. Run the scenes, then this:

    cd build\\opt-dev
    ..\\..\\scripts\\run-throttled.bat .\\patchy_ui_visual_tests.exe ui_theme_guide_shot
    python ..\\..\\scripts\\dev\\copy-theme-guide-shots.py test-artifacts

Usage: python scripts/dev/copy-theme-guide-shots.py <folder holding theme_guide_*.png>
Look at every picture before committing it: nothing in one may show a local path.
"""
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
TARGET = ROOT / "docs" / "images" / "themes"
PREFIX = "theme_guide_"


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    source = Path(sys.argv[1])
    shots = sorted(source.glob(PREFIX + "*.png"))
    if not shots:
        print(f"no {PREFIX}*.png in {source}", file=sys.stderr)
        return 1
    TARGET.mkdir(parents=True, exist_ok=True)
    for shot in shots:
        target = TARGET / shot.name[len(PREFIX):]
        shutil.copyfile(shot, target)
        print(f"{target.relative_to(ROOT).as_posix()} ({target.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
