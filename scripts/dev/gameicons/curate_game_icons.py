#!/usr/bin/env python3
"""Curates and packs the bundled game-icons.net custom shapes (GitHub #521).

Two steps, both deterministic:

  select  Reads the game-icons.net repository (one folder per author, 512x512 SVGs) and the
          site's tag pages, and writes the reviewed pick list, game_icons_manifest.tsv:
          id, source file, author, folder, name. Edit the manifest by hand to add, drop,
          rename or move a shape; never change an existing id (shape ids are permanent).
  build   Reads the manifest and the repository and writes src/ui/shapes/game-icons.json,
          the pack Patchy embeds: per shape its id, name, folder, author and the outline as
          SVG path data in the 512 box (the black background square removed, numbers rounded
          to 1/100 px).

  python -I scripts/dev/gameicons/curate_game_icons.py select --source <icons clone> --tags <tag pages>
  python -I scripts/dev/gameicons/curate_game_icons.py build --source <icons clone>

<icons clone> is https://github.com/game-icons/icons at the commit in PINNED_COMMIT; <tag pages>
is a folder of https://game-icons.net/tags/<tag>.html pages. Both are untrusted data: run this
script from the repository (never from inside them) with python -I.

The shapes are CC BY 3.0 (Viscious Speed and Zeromancer: CC0), drawn by people; credits are in
NOTICE-THIRD-PARTY.md, the rules in docs/vector-tools.md ("Bundled game-icons.net shapes").
"""
from __future__ import annotations

import argparse
import html
import json
import math
import re
import sys
from pathlib import Path

PINNED_COMMIT = "82d948812bfe3f269ef8f731dcdb07b08160edc4"
HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
MANIFEST = HERE / "game_icons_manifest.tsv"
PACK = REPO / "src" / "ui" / "shapes" / "game-icons.json"

# Folder slug -> the credited name in the repository's license.txt. badges/ and
# various-artists/ name no author, so they are left out.
AUTHORS = {
    "lorc": "Lorc", "delapouite": "Delapouite", "john-colburn": "John Colburn",
    "felbrigg": "Felbrigg", "john-redman": "John Redman", "carl-olsen": "Carl Olsen",
    "sbed": "Sbed", "priorblue": "PriorBlue", "willdabeast": "Willdabeast",
    "viscious-speed": "Viscious Speed", "lord-berandas": "Lord Berandas", "irongamer": "Irongamer",
    "heavenly-dog": "HeavenlyDog", "lucasms": "Lucas", "faithtoken": "Faithtoken", "skoll": "Skoll",
    "andymeneely": "Andy Meneely", "cathelineau": "Cathelineau", "kier-heyl": "Kier Heyl",
    "aussiesim": "Aussiesim", "sparker": "Sparker", "zeromancer": "Zeromancer", "rihlsul": "Rihlsul",
    "quoting": "Quoting", "guard13007": "Guard13007", "darkzaitzev": "DarkZaitzev",
    "spencerdub": "SpencerDub", "generalace135": "GeneralAce135", "zajkonur": "Zajkonur",
    "catsu": "Catsu", "starseeker": "Starseeker", "pepijn-poolman": "Pepijn Poolman",
    "pierre-leducq": "Pierre Leducq", "caro-asercion": "Caro Asercion", "seregacthtuf": "SeregaCthtuf",
}
CC0_AUTHORS = {"viscious-speed", "zeromancer"}

# Our folders, in the order a shape is tried: the first folder with a matching tag takes it,
# up to its cap. English names; Patchy translates them (custom_shape_folder_display_name).
FOLDERS = [
    ("Hearts and Stars", {"heart", "star"}, 40),
    ("Animals", {"animal", "bird", "fish", "insect", "mammal", "reptile", "shell"}, 70),
    ("Nature", {"plant", "mushroom", "nature", "mineral", "wood"}, 45),
    ("Weather and Elements", {"sky", "fire", "ice", "lightning", "sea", "liquid", "smoke", "energy"}, 50),
    ("Space and Science", {"space", "science-fiction", "chemical", "electronic", "robot"}, 40),
    ("People and Body", {"body", "hand", "eye", "head", "mouth", "anatomy", "skull", "bone"}, 50),
    ("Food and Drink", {"food", "fruit", "egg", "meat", "bottle", "glass", "kitchenware"}, 50),
    ("Music", {"sound", "musical-instrument"}, 30),
    ("Clothing", {"clothing", "hat", "boot", "mask", "jewellery"}, 45),
    ("Vehicles and Machines", {"vehicle", "boat", "machine"}, 45),
    ("Buildings", {"building", "door", "tower", "statue"}, 35),
    ("Tools and Objects", {"tool", "office", "book", "metal"}, 50),
    ("Fantasy", {"creature", "vampire", "tentacle", "wing", "claw"}, 35),
    ("Weapons and Armor", {"weapon", "blade", "axe", "gun", "shield", "armor", "bomb", "explosion"}, 50),
    ("Symbols and Signs", {"cross", "target", "dice", "tarot", "board"}, 40),
    ("Abstract and Ornaments", {"abstract", "celtic"}, 30),
]

# Left out whatever else they carry: other companies' properties, game-UI glyphs, gore.
EXCLUDED_TAGS = {"super-mario", "zelda", "catan", "cluedo", "game-of-thrones", "arimaa", "video-game",
                 "gui", "blood", "zombie"}
# Name fragments for brands, franchises, logos, political or hate symbols, and gore.
EXCLUDED_WORDS = [
    "mario", "luigi", "zelda", "triforce", "sheikah", "hylian", "catan", "cluedo", "pokecog", "pokeball",
    "pikachu", "pacman", "pac-man", "tetris", "invader", "rubik", "lego", "tardis", "dalek", "lightsaber",
    "light-sabers", "stormtrooper", "darth", "vader", "yoda", "jedi", "death-star", "falcon-moon",
    "x-wing", "tie-fighter", "enterprise", "batman", "superman", "spider-man", "spiderman", "hulk",
    "iron-man", "transformers", "autobot", "decepticon", "minecraft", "creeper", "companion-cube",
    "half-life", "halo", "master-chief", "monopoly", "scrabble", "warhammer", "magic-the-gathering",
    "android", "apple-logo", "windows", "tux", "github", "twitter", "facebook", "youtube", "bitcoin",
    "rss", "bluetooth", "usb", "hammer-sickle", "swastika", "nazi", "iron-cross", "confederate",
    "dna", "barbed", "blood", "bleed", "gore", "decapitat", "severed", "guts", "corpse", "dismember",
    "gibbet", "impaled", "entrails", "behead", "vomit", "suicide", "torture", "execution", "noose",
    "gallows", "hanged", "carnage", "slaughter", "mutilat", "stab", "throat", "bullet-impact",
    "grenade", "pistol", "rifle", "revolver", "machine-gun", "submachine", "shotgun", "sniper",
    "mp5", "ak47", "uzi", "colt", "luger", "glock", "thompson", "winchester", "beretta", "lee-enfield",
    "ouija", "pentagram", "inverted",
    # After a look at the contact sheets: franchise pieces and game-UI glyphs without those tags.
    "american-shield", "batwing", "wolverine", "samus", "bolter", "spock", "death-note", "tesla", "vhs",
    "balkenkreuz", "union-jack", "guillotine", "panzerfaust", "life-bar", "upgrade", "downgrade",
    "disabled", "temporary", "player-", "health-", "head-shot", "target-laser", "up-card",
    "virtual-marker", "swords-power", "telefrag", "overkill", "static-guard", "ghost-ally",
    "extra-vision", "coa-de-jima", "skull-in-jar", "heart-stake", "bat-mask", "spider-mask",
    "converse", "stahlhelm", "cultist", "kevlar", "cadillac", "hemp", "cigarette", "otamatone",
]
VARIANT_SUFFIX = re.compile(r"^(?P<stem>.+?)-(?:\d+|alt|alt-?\d*|bis)$")
MAX_PATH_DATA = 9000   # longer outlines are too fine to read at picker size
ABSTRACT_KEEP = 10     # abstract-NNN: a handful of the run


def title_case(text: str) -> str:
    words = re.split(r"[\s-]+", text.strip())
    small = {"and", "of", "the", "a", "an", "in", "on", "with", "to", "for", "or", "at", "by"}
    out = []
    for index, word in enumerate(words):
        if not word:
            continue
        lower = word.lower()
        out.append(lower if index > 0 and lower in small else lower[:1].upper() + lower[1:])
    return " ".join(out)


# --- SVG path data: parse to absolute cubics (for the fill-rule check and the rounded output) ---

TOKEN = re.compile(r"[MmZzLlHhVvCcSsQqTtAa]|[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?")


def parse_path(data: str):
    """Absolute subpaths: each a list of ('M', x, y) / ('C', x1, y1, x2, y2, x, y) and a closed flag."""
    tokens = []
    position = 0
    # Arc flags may be written without separators ("a5 5 0 015 5"): a tokenizer that knows the
    # grammar reads them as single digits.
    while position < len(data):
        ch = data[position]
        if ch in " ,\t\r\n":
            position += 1
            continue
        match = TOKEN.match(data, position)
        if match is None:
            raise ValueError(f"bad path data near {data[position:position + 20]!r}")
        tokens.append(match.group(0))
        position = match.end()
    subpaths = []
    current = None
    x = y = start_x = start_y = 0.0
    last_c2 = None
    last_q = None
    command = None
    i = 0

    def number():
        nonlocal i
        value = float(tokens[i])
        i += 1
        return value

    def flag():
        nonlocal i
        token = tokens[i]
        if len(token) > 1:
            # Flags are one character: "015" holds a flag and the start of the next number.
            tokens[i] = token[1:]
            return token[0] == "1"
        i += 1
        return token == "1"

    def line_to(nx, ny):
        nonlocal x, y
        current["segments"].append(("C", x, y, nx, ny, nx, ny))
        x, y = nx, ny

    while i < len(tokens):
        if re.match(r"[A-Za-z]", tokens[i]):
            command = tokens[i]
            i += 1
        elif command is None:
            raise ValueError("path data does not start with a command")
        relative = command.islower()
        op = command.upper()
        if op == "Z":
            if current is not None:
                current["closed"] = True
                x, y = start_x, start_y
            last_c2 = last_q = None
            command = None if i >= len(tokens) or re.match(r"[A-Za-z]", tokens[i]) else command
            if command is None:
                continue
            raise ValueError("numbers after Z")
        if op == "M":
            nx, ny = number(), number()
            if relative:
                nx += x
                ny += y
            current = {"start": (nx, ny), "segments": [], "closed": False}
            subpaths.append(current)
            x, y = start_x, start_y = nx, ny
            command = "l" if relative else "L"
            last_c2 = last_q = None
            continue
        if current is None:
            current = {"start": (x, y), "segments": [], "closed": False}
            subpaths.append(current)
        if op == "L":
            nx, ny = number(), number()
            if relative:
                nx += x
                ny += y
            line_to(nx, ny)
            last_c2 = last_q = None
        elif op == "H":
            nx = number() + (x if relative else 0.0)
            line_to(nx, y)
            last_c2 = last_q = None
        elif op == "V":
            ny = number() + (y if relative else 0.0)
            line_to(x, ny)
            last_c2 = last_q = None
        elif op in "CS":
            if op == "C":
                x1, y1 = number(), number()
                if relative:
                    x1 += x
                    y1 += y
            else:
                x1, y1 = (2 * x - last_c2[0], 2 * y - last_c2[1]) if last_c2 else (x, y)
            x2, y2, nx, ny = number(), number(), number(), number()
            if relative:
                x2 += x
                y2 += y
                nx += x
                ny += y
            current["segments"].append(("C", x1, y1, x2, y2, nx, ny))
            last_c2 = (x2, y2)
            last_q = None
            x, y = nx, ny
        elif op in "QT":
            if op == "Q":
                qx, qy = number(), number()
                if relative:
                    qx += x
                    qy += y
            else:
                qx, qy = (2 * x - last_q[0], 2 * y - last_q[1]) if last_q else (x, y)
            nx, ny = number(), number()
            if op == "Q" and relative:
                nx += x
                ny += y
            elif op == "T" and relative:
                nx += x
                ny += y
            current["segments"].append(("C", x + 2 / 3 * (qx - x), y + 2 / 3 * (qy - y),
                                        nx + 2 / 3 * (qx - nx), ny + 2 / 3 * (qy - ny), nx, ny))
            last_q = (qx, qy)
            last_c2 = None
            x, y = nx, ny
        elif op == "A":
            rx, ry, rotation = abs(number()), abs(number()), number()
            large, sweep = flag(), flag()
            nx, ny = number(), number()
            if relative:
                nx += x
                ny += y
            for segment in arc_to_cubics(x, y, rx, ry, rotation, large, sweep, nx, ny):
                current["segments"].append(segment)
            x, y = nx, ny
            last_c2 = last_q = None
        else:
            raise ValueError(f"unsupported command {command}")
    return subpaths


def arc_to_cubics(x1, y1, rx, ry, rotation, large, sweep, x2, y2):
    if (x1 == x2 and y1 == y2):
        return []
    if rx == 0 or ry == 0:
        return [("C", x1, y1, x2, y2, x2, y2)]
    phi = math.radians(rotation)
    cos_phi, sin_phi = math.cos(phi), math.sin(phi)
    dx, dy = (x1 - x2) / 2, (y1 - y2) / 2
    x1p = cos_phi * dx + sin_phi * dy
    y1p = -sin_phi * dx + cos_phi * dy
    scale = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry)
    if scale > 1:
        rx *= math.sqrt(scale)
        ry *= math.sqrt(scale)
    numerator = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p
    denominator = rx * rx * y1p * y1p + ry * ry * x1p * x1p
    factor = math.sqrt(max(0.0, numerator / denominator)) if denominator else 0.0
    if large == sweep:
        factor = -factor
    cxp = factor * rx * y1p / ry
    cyp = -factor * ry * x1p / rx
    cx = cos_phi * cxp - sin_phi * cyp + (x1 + x2) / 2
    cy = sin_phi * cxp + cos_phi * cyp + (y1 + y2) / 2

    def angle(ux, uy, vx, vy):
        return math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)

    theta1 = angle(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    delta = angle((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry)
    if not sweep and delta > 0:
        delta -= 2 * math.pi
    elif sweep and delta < 0:
        delta += 2 * math.pi
    count = max(1, int(math.ceil(abs(delta) / (math.pi / 2))))
    step = delta / count
    out = []
    for k in range(count):
        a1 = theta1 + k * step
        a2 = a1 + step
        t = 4 / 3 * math.tan(step / 4)

        def point(a):
            px, py = rx * math.cos(a), ry * math.sin(a)
            return cos_phi * px - sin_phi * py + cx, sin_phi * px + cos_phi * py + cy

        def derivative(a):
            px, py = -rx * math.sin(a), ry * math.cos(a)
            return cos_phi * px - sin_phi * py, sin_phi * px + cos_phi * py

        p1, p2 = point(a1), point(a2)
        d1, d2 = derivative(a1), derivative(a2)
        out.append(("C", p1[0] + t * d1[0], p1[1] + t * d1[1], p2[0] - t * d2[0], p2[1] - t * d2[1],
                    p2[0], p2[1]))
    return out


def flatten(subpaths, steps=8):
    polygons = []
    for subpath in subpaths:
        points = [subpath["start"]]
        x, y = subpath["start"]
        for _, x1, y1, x2, y2, nx, ny in subpath["segments"]:
            for s in range(1, steps + 1):
                t = s / steps
                mt = 1 - t
                points.append((mt ** 3 * x + 3 * mt * mt * t * x1 + 3 * mt * t * t * x2 + t ** 3 * nx,
                               mt ** 3 * y + 3 * mt * mt * t * y1 + 3 * mt * t * t * y2 + t ** 3 * ny))
            x, y = nx, ny
        if len(points) >= 3:
            polygons.append(points)
    return polygons


def fill_rules_agree(subpaths, grid=64, extent=512.0):
    """True when even-odd and nonzero fill the same samples (custom shapes fill even-odd)."""
    polygons = flatten(subpaths)
    edges = []
    for polygon in polygons:
        count = len(polygon)
        for k in range(count):
            edges.append((polygon[k], polygon[(k + 1) % count]))
    differ = filled = 0
    for row in range(grid):
        py = (row + 0.5) * extent / grid
        crossings = []
        for (ax, ay), (bx, by) in edges:
            if (ay <= py) != (by <= py):
                crossings.append((ax + (py - ay) * (bx - ax) / (by - ay), 1 if by > ay else -1))
        crossings.sort()
        # Walk the samples left to right; everything right of a sample is the suffix.
        total_winding = sum(direction for _, direction in crossings)
        winding_left = 0
        index = 0
        for column in range(grid):
            px = (column + 0.5) * extent / grid
            while index < len(crossings) and crossings[index][0] <= px:
                winding_left += crossings[index][1]
                index += 1
            right_count = len(crossings) - index
            winding = total_winding - winding_left
            nonzero = winding != 0
            even_odd = right_count % 2 == 1
            filled += nonzero
            differ += even_odd != nonzero
    return filled > 0 and differ <= max(2, filled // 200)


def format_number(value: float) -> str:
    text = f"{value:.2f}".rstrip("0").rstrip(".")
    return "0" if text in ("-0", "") else text


def compact_path_data(subpaths) -> str:
    parts = []
    for subpath in subpaths:
        sx, sy = subpath["start"]
        parts.append(f"M{format_number(sx)} {format_number(sy)}")
        for _, x1, y1, x2, y2, nx, ny in subpath["segments"]:
            parts.append("C" + " ".join(format_number(v) for v in (x1, y1, x2, y2, nx, ny)))
        if subpath["closed"]:
            parts.append("Z")
    return "".join(parts)


def icon_outline(svg_text: str) -> str | None:
    """The white outline of an icon: every path but the black background square."""
    paths = re.findall(r"<path([^>]*)/?>", svg_text)
    outlines = []
    for attributes in paths:
        match = re.search(r'\sd="([^"]*)"', attributes)
        if match is None:
            continue
        data = match.group(1).strip()
        if re.fullmatch(r"M0 0h512v512H0z", data) or 'fill="#fff"' not in attributes:
            continue
        outlines.append(data)
    return " ".join(outlines) if outlines else None


def read_tags(tag_dir: Path):
    tags: dict[str, set[str]] = {}
    names: dict[str, str] = {}
    for page in sorted(tag_dir.glob("*.html")):
        tag = page.stem
        text = page.read_text(encoding="utf-8", errors="replace")
        for hint, author, name in re.findall(
                r'data-hint="([^"]*)"[^>]*><a href="/1x1/([^/"]+)/([^".]+)\.html"', text):
            key = f"{author}/{name}"
            tags.setdefault(key, set()).add(tag)
            names.setdefault(key, html.unescape(hint))
    return tags, names


def select(source: Path, tag_dir: Path) -> None:
    tags, hints = read_tags(tag_dir)
    candidates = []
    for author in sorted(AUTHORS):
        for svg in sorted((source / author).glob("*.svg")):
            key = f"{author}/{svg.stem}"
            icon_tags = tags.get(key)
            if not icon_tags or icon_tags & EXCLUDED_TAGS:
                continue
            if any(word in svg.stem for word in EXCLUDED_WORDS):
                continue
            outline = icon_outline(svg.read_text(encoding="utf-8"))
            if outline is None or len(outline) > MAX_PATH_DATA:
                continue
            candidates.append((key, author, svg.stem, icon_tags, outline))
    # Variant runs: one per stem (the simplest), a handful of abstract-NNN.
    by_stem: dict[str, list] = {}
    for candidate in candidates:
        match = VARIANT_SUFFIX.match(candidate[2])
        stem = match.group("stem") if match else candidate[2]
        by_stem.setdefault(stem, []).append(candidate)
    kept = []
    for stem, group in by_stem.items():
        group.sort(key=lambda c: (len(c[4]), c[0]))
        kept.extend(group[:ABSTRACT_KEEP] if stem == "abstract" else group[:1])
    picked = []
    counts = {name: 0 for name, _, _ in FOLDERS}
    # Simplest outlines first: bold silhouettes read best as thumbnails and stamps.
    for key, author, stem, icon_tags, outline in sorted(kept, key=lambda c: (len(c[4]), c[0])):
        folder = next((name for name, folder_tags, cap in FOLDERS
                       if icon_tags & folder_tags and counts[name] < cap), None)
        if folder is None:
            continue
        try:
            subpaths = parse_path(outline)
        except (ValueError, IndexError) as error:
            print(f"skip {key}: {error}", file=sys.stderr)
            continue
        if not fill_rules_agree(subpaths):
            continue
        counts[folder] += 1
        picked.append((key, author, stem, folder))
    used_names: dict[str, int] = {}
    rows = []
    for key, author, stem, folder in sorted(picked, key=lambda p: (p[3], p[2], p[1])):
        name = title_case(hints.get(key, stem.replace("-", " ")))
        if name in used_names:
            name = f"{name} ({AUTHORS[author]})"
        used_names[name] = 1
        rows.append((f"shape.gameicons.{author}.{stem}", f"{author}/{stem}.svg", author, folder, name))
    lines = ["# id\tsource\tauthor\tfolder\tname  (game-icons.net, commit " + PINNED_COMMIT + ")"]
    lines += ["\t".join(row) for row in rows]
    MANIFEST.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    for name, _, cap in FOLDERS:
        print(f"{name}: {counts[name]} (cap {cap})")
    print(f"{len(rows)} shapes -> {MANIFEST}")


def read_manifest():
    rows = []
    for line in MANIFEST.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        shape_id, source, author, folder, name = line.split("\t")
        rows.append((shape_id, source, author, folder, name))
    return rows


def build(source: Path) -> None:
    shapes = []
    used_authors = set()
    for shape_id, file, author, folder, name in read_manifest():
        outline = icon_outline((source / file).read_text(encoding="utf-8"))
        if outline is None:
            raise SystemExit(f"{file}: no outline")
        shapes.append({"id": shape_id, "name": name, "folder": folder, "author": author,
                       "d": compact_path_data(parse_path(outline))})
        used_authors.add(author)
    pack = {
        "format": "patchy-shape-pack", "version": 1,
        "source": "https://game-icons.net", "commit": PINNED_COMMIT,
        "license": "CC BY 3.0 (Viscious Speed, Zeromancer: CC0)",
        "authors": {slug: AUTHORS[slug] for slug in sorted(used_authors)},
        "shapes": shapes,
    }
    PACK.parent.mkdir(parents=True, exist_ok=True)
    PACK.write_text(json.dumps(pack, ensure_ascii=False, separators=(",", ":"), indent=None)
                    .replace('},{"id"', '},\n{"id"') + "\n", encoding="utf-8", newline="\n")
    print(f"{len(shapes)} shapes, {len(used_authors)} authors -> {PACK} ({PACK.stat().st_size} bytes)")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("step", choices=["select", "build"])
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--tags", type=Path)
    args = parser.parse_args()
    if args.step == "select":
        if args.tags is None:
            parser.error("select needs --tags")
        select(args.source, args.tags)
    else:
        build(args.source)


if __name__ == "__main__":
    main()
