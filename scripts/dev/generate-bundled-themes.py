"""Generates the bundled .patchytheme files in themes/bundled from the Dark palette.

Run from the repository root after changing src/ui/theme_palette.cpp or a theme
definition below:

    python scripts/dev/generate-bundled-themes.py

Every theme is a full-role file (like an Export), built from the authored Dark
palette in src/ui/theme_palette.cpp:

- Neutral roles (HSL saturation under 0.15) map their lightness through the
  theme's tone ramp, a list of (lightness, color) anchors interpolated in RGB.
  The ramp keeps every "this is two steps darker than that" relationship of the
  Dark palette while moving the whole surface family onto the theme's tones.
- Chromatic roles near Patchy's blue accent (hue within 35 degrees of 207) take
  the theme's accent hue and saturation scale. Other hues (warnings, script
  syntax colors, the red/green/yellow status roles) are left alone so their
  meaning survives.
- Alpha is preserved.

The Dark anchors below are the lightness landmarks of the Dark palette: borders
(0.12), window (0.15), panels (0.21 to 0.37), disabled text (0.45), mid grays,
body text (0.90).
"""

from __future__ import annotations

import colorsys
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PALETTE = ROOT / "src" / "ui" / "theme_palette.cpp"
OUT = ROOT / "themes" / "bundled"

PATCHY_BLUE_HUE = 207.0
BLUE_FAMILY_HALF_WIDTH = 35.0
NEUTRAL_SATURATION = 0.15

# (file stem, display name, tone ramp, accent hue or None, accent saturation scale)
THEMES = [
    ("darkest", "Darkest",
     [(0.00, "#000000"), (0.12, "#141414"), (0.15, "#1a1a1a"), (0.21, "#262626"), (0.30, "#3a3a3a"),
      (0.37, "#484848"), (0.45, "#666666"), (0.50, "#7a7a7a"), (0.60, "#999999"), (0.75, "#bfbfbf"),
      (0.90, "#e6e6e6"), (1.00, "#ffffff")], None, 1.0),
    ("medium-gray", "Medium Gray",
     [(0.00, "#1c1c1c"), (0.12, "#404040"), (0.15, "#535353"), (0.21, "#606060"), (0.30, "#727272"),
      (0.37, "#7e7e7e"), (0.45, "#9a9a9a"), (0.50, "#a4a4a4"), (0.60, "#b4b4b4"), (0.75, "#cfcfcf"),
      (0.90, "#f0f0f0"), (1.00, "#ffffff")], None, 1.0),
    ("solarized-dark", "Solarized Dark",
     [(0.00, "#001b22"), (0.12, "#00232c"), (0.15, "#002b36"), (0.21, "#073642"), (0.30, "#0e4552"),
      (0.37, "#1a535f"), (0.45, "#586e75"), (0.50, "#657b83"), (0.60, "#839496"), (0.75, "#93a1a1"),
      (0.90, "#eee8d5"), (1.00, "#fdf6e3")], 205.0, 1.0),
    ("nord", "Nord",
     [(0.00, "#1f232c"), (0.12, "#242933"), (0.15, "#2e3440"), (0.21, "#3b4252"), (0.30, "#434c5e"),
      (0.37, "#4c566a"), (0.45, "#616e88"), (0.50, "#7b88a1"), (0.60, "#9aa5bb"), (0.75, "#c2c9d6"),
      (0.90, "#d8dee9"), (1.00, "#eceff4")], 193.0, 0.75),
    ("dracula", "Dracula",
     [(0.00, "#191a21"), (0.12, "#21222c"), (0.15, "#282a36"), (0.21, "#343746"), (0.30, "#44475a"),
      (0.37, "#4f5266"), (0.45, "#6272a4"), (0.50, "#7482ad"), (0.60, "#8f9bbf"), (0.75, "#b9bfd6"),
      (0.90, "#f8f8f2"), (1.00, "#ffffff")], 265.0, 0.9),
    ("gruvbox-dark", "Gruvbox Dark",
     [(0.00, "#1d2021"), (0.12, "#232627"), (0.15, "#282828"), (0.21, "#3c3836"), (0.30, "#504945"),
      (0.37, "#665c54"), (0.45, "#7c6f64"), (0.50, "#928374"), (0.60, "#a89984"), (0.75, "#bdae93"),
      (0.90, "#ebdbb2"), (1.00, "#fbf1c7")], 162.0, 0.6),
    ("high-contrast", "High Contrast",
     [(0.00, "#000000"), (0.12, "#000000"), (0.15, "#0a0a0a"), (0.21, "#141414"), (0.30, "#2a2a2a"),
      (0.37, "#3c3c3c"), (0.45, "#9a9a9a"), (0.50, "#b0b0b0"), (0.60, "#c8c8c8"), (0.75, "#e0e0e0"),
      (0.90, "#ffffff"), (1.00, "#ffffff")], 41.0, 1.0),
]

# The "description" each theme shows in Preferences (docs/theme-files.md, "Format").
DESCRIPTIONS = {
    "darkest": "A near-black take on the Dark scheme, for dim rooms.",
    "medium-gray": "A mid-gray interface, between Dark and Light.",
    "solarized-dark": "Patchy in the Solarized Dark colors.",
    "nord": "Patchy in the cool blue-gray Nord colors.",
    "dracula": "Patchy in the purple-tinted Dracula colors.",
    "gruvbox-dark": "Patchy in the warm Gruvbox Dark colors.",
    "high-contrast": "Black panels, white text and amber highlights, for the most contrast.",
}


def parse_dark_palette() -> dict[str, tuple[int, int, int, int]]:
    text = PALETTE.read_text(encoding="utf-8")
    start = text.index("const ThemePalette& dark_palette()")
    end = text.index("derive_light_palette", start)
    body = text[start:end]
    roles: dict[str, tuple[int, int, int, int]] = {}
    for line in body.splitlines():
        line = line.strip()
        if not line.startswith("."):
            continue
        name = line[1:line.index(" ")]
        m = re.search(r"rgb\(0x([0-9a-fA-F]{6})\)", line)
        if m:
            v = int(m.group(1), 16)
            roles[name] = ((v >> 16) & 255, (v >> 8) & 255, v & 255, 255)
            continue
        m = re.search(r"QColor\((\d+),\s*(\d+),\s*(\d+),\s*(\d+)\)", line)
        if m:
            roles[name] = tuple(int(x) for x in m.groups())  # type: ignore[assignment]
            continue
        raise SystemExit(f"unparsed palette entry: {line}")
    if len(roles) < 250:
        raise SystemExit(f"only {len(roles)} roles parsed; the palette layout changed")
    return roles


def hex_to_rgb(h: str) -> tuple[int, int, int]:
    return int(h[1:3], 16), int(h[3:5], 16), int(h[5:7], 16)


def ramp(anchors: list[tuple[float, str]], lightness: float) -> tuple[int, int, int]:
    points = [(l, hex_to_rgb(h)) for l, h in anchors]
    if lightness <= points[0][0]:
        return points[0][1]
    for (l0, c0), (l1, c1) in zip(points, points[1:]):
        if lightness <= l1:
            t = 0.0 if l1 == l0 else (lightness - l0) / (l1 - l0)
            return tuple(round(a + (b - a) * t) for a, b in zip(c0, c1))  # type: ignore[return-value]
    return points[-1][1]


def hue_distance(a: float, b: float) -> float:
    d = abs(a - b) % 360.0
    return min(d, 360.0 - d)


def transform(rgba, anchors, accent_hue, accent_sat_scale):
    r, g, b, a = rgba
    h, l, s = colorsys.rgb_to_hls(r / 255.0, g / 255.0, b / 255.0)
    if s < NEUTRAL_SATURATION:
        out = ramp(anchors, l)
        return (*out, a)
    hue = h * 360.0
    if accent_hue is not None and hue_distance(hue, PATCHY_BLUE_HUE) <= BLUE_FAMILY_HALF_WIDTH:
        hue = (accent_hue + (hue - PATCHY_BLUE_HUE)) % 360.0
        s = min(1.0, s * accent_sat_scale)
    rr, gg, bb = colorsys.hls_to_rgb(hue / 360.0, l, s)
    return (round(rr * 255), round(gg * 255), round(bb * 255), a)


def color_hex(rgba) -> str:
    r, g, b, a = rgba
    text = f"#{r:02x}{g:02x}{b:02x}"
    return text if a == 255 else text + f"{a:02x}"


def main() -> None:
    dark = parse_dark_palette()
    OUT.mkdir(parents=True, exist_ok=True)
    for stem, name, anchors, accent_hue, sat_scale in THEMES:
        roles = {role: color_hex(transform(value, anchors, accent_hue, sat_scale)) for role, value in dark.items()}
        document = {"format": 1, "name": name, "description": DESCRIPTIONS[stem], "base": "dark", "roles": roles}
        path = OUT / f"{stem}.patchytheme"
        path.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8", newline="\n")
        print(f"wrote {path.relative_to(ROOT)} ({len(roles)} roles)")


if __name__ == "__main__":
    main()
