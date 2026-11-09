"""Writes test-fixtures/readme/mcp_isometric_calls.json: the MCP tool calls that paint the
isometric harbor in the README's MCP animation (shot_readme_mcp_isometric).

The calls were designed by an AI agent working through Patchy's MCP connector.
Solid forms are editable vector shapes with gradient fills (execute_script calls, one per
object, each grouped in the Layers panel); fine detail is native brush work (draw_strokes):
stars, ripples, the tile grid, window glow on a Screen layer. Trees are Smart Objects: the
agent writes one pine and one oak as SVG, places each once, and duplicates that layer for
every other tree, so all copies share one source (edit one tree and they all update). The scene replays the calls through
the real connector in a visible window with Slow mode on, so every shape and stroke shows.

Run from the repository root:  python scripts/dev/readme-shots/make-mcp-isometric-calls.py
Deterministic: the stars, ripples and grass use a fixed seed.
"""

import json
import math
import random
from pathlib import Path

W, H = 1080, 720
CX, CY = 540, 168          # screen position of tile corner (0, 0) at ground level
TW, TH = 30.0, 15.0        # half a tile's width and height on screen
N = 10                     # the island is N x N tiles
SIDE = 52                  # the island's thickness
HORIZON = 420

PRELUDE = """var doc = app.getDocument(patchy.args.documentId); doc.activate();
var INK = '#171a2c', L = [];
function stops(cs) { return cs.map(function (c, i) { return {position: i / (cs.length - 1), color: c}; }); }
function lin(cs, angle) { return {type: 'gradient', gradient: {type: 'linear', angle: angle, colorStops: stops(cs)}}; }
function glow(c, a) { return {type: 'gradient', gradient: {type: 'radial', colorStops: stops([c, c]),
  alphaStops: [{position: 0, opacity: a}, {position: 1, opacity: 0}]}}; }
function rad(cs) { return {type: 'gradient', gradient: {type: 'radial', colorStops: stops(cs)}}; }
function poly(name, p, fill, w, op) {
  var a = []; for (var i = 0; i < p.length; i += 2) a.push({x: p[i], y: p[i + 1]});
  var l = doc.addShape(name, {type: 'path', path: {subpaths: [{closed: true, anchors: a}]}},
    {fill: fill, stroke: {enabled: !!w, width: w || 0, paint: INK, alignment: 'center', join: 'round', cap: 'round'}});
  if (op !== undefined) l.opacity = op; L.push(l); return l;
}
function ell(name, x, y, w, h, fill, sw, op) {
  var l = doc.addShape(name, {type: 'ellipse', x: x, y: y, width: w, height: h},
    {fill: fill, stroke: {enabled: !!sw, width: sw || 0, paint: INK, alignment: 'center'}});
  if (op !== undefined) l.opacity = op; L.push(l); return l;
}
function finish(group, extra) {
  var g = doc.groupLayers(L, group); var r = {group: g.id};
  if (extra) r.layers = extra; patchy.setResult(r);
}
doc.activeLayer = doc.layers[doc.layers.length - 1];
"""


def P(i, j, z=0.0):
    """Screen point of tile coordinate (i, j) at height z (pixels)."""
    return (round(CX + (i - j) * TW, 1), round(CY + (i + j) * TH - z, 1))


def flat(*points):
    return "[" + ", ".join(f"{x:g}, {y:g}" for (x, y) in points) + "]"


def js(s):
    return json.dumps(s)


def lin(colors, angle):
    return "lin([" + ", ".join(js(c) for c in colors) + f"], {angle:g})"


def rad(colors):
    return "rad([" + ", ".join(js(c) for c in colors) + "])"


def glow(color, alpha):
    return f"glow({js(color)}, {alpha:g})"


def solid(color):
    return js(color)


class Script:
    def __init__(self, group):
        self.group = group
        self.lines = []
        self.extra = None  # JS object literal of new pixel layers to report: {Name: var.id}

    def poly(self, name, points, fill, width=1.6, opacity=None):
        op = "" if opacity is None else f", {opacity:g}"
        self.lines.append(f"poly({js(name)}, {flat(*points)}, {fill}, {width:g}{op});")

    def ell(self, name, x, y, w, h, fill, width=0, opacity=None):
        op = "" if opacity is None else f", {opacity:g}"
        self.lines.append(f"ell({js(name)}, {x:g}, {y:g}, {w:g}, {h:g}, {fill}, {width:g}{op});")

    def raw(self, line):
        self.lines.append(line)

    def call(self, note):
        extra = "" if self.extra is None else ", " + self.extra
        return {"tool": "execute_script", "note": note, "name": self.group,
                "code": PRELUDE + "\n".join(self.lines) + f"\nfinish({js(self.group)}{extra});\n"}


# Light from the upper left: faces at j = j1 (front left) take it, faces at i = i1 (front
# right) are in shade. Every face darkens toward the ground (angle 90: first stop at bottom).
def box(s, name, i0, i1, j0, j1, h, left, right, top, z0=0.0):
    s.poly(name + " left wall", [P(i0, j1, z0 + h), P(i0, j1, z0), P(i1, j1, z0), P(i1, j1, z0 + h)],
           lin(left, 90))
    s.poly(name + " right wall", [P(i1, j1, z0 + h), P(i1, j1, z0), P(i1, j0, z0), P(i1, j0, z0 + h)],
           lin(right, 90))
    if top:
        s.poly(name + " top", [P(i0, j0, z0 + h), P(i0, j1, z0 + h), P(i1, j1, z0 + h), P(i1, j0, z0 + h)],
               lin(top, 90))


def gable_roof(s, name, i0, i1, j0, j1, h, rh, near, far, gable):
    jm = (j0 + j1) / 2
    r0, r1 = P(i0, jm, h + rh), P(i1, jm, h + rh)
    e = 0.12
    s.poly(name + " roof back", [r0, P(i0 - e, j0 - e, h - 4), P(i1 + e, j0 - e, h - 4), r1], lin(far, 0))
    s.poly(name + " gable", [r1, P(i1, j0, h), P(i1, j1, h)], lin(gable, 90))
    s.poly(name + " roof front", [r0, P(i0 - e, j1 + e, h - 4), P(i1 + e, j1 + e, h - 4), r1], lin(near, 90))


def windows_left(s, name, i0, i1, j1, z, count, h=18):
    for k in range(count):
        i = i0 + (i1 - i0) * (k + 0.5) / count
        s.poly(f"{name} window {k + 1}", [P(i - 0.16, j1, z + h), P(i - 0.16, j1, z), P(i + 0.16, j1, z),
                                          P(i + 0.16, j1, z + h)], lin(["#ff9d3a", "#ffe9a8"], 90), 1.2)


def windows_right(s, name, j0, j1, i1, z, count, h=18):
    for k in range(count):
        j = j0 + (j1 - j0) * (k + 0.5) / count
        s.poly(f"{name} side window {k + 1}", [P(i1, j + 0.16, z + h), P(i1, j + 0.16, z), P(i1, j - 0.16, z),
                                               P(i1, j - 0.16, z + h)], lin(["#e0782c", "#ffd27a"], 90), 1.2)


def glow_left(i0, i1, j1, z, count):
    return [P(i0 + (i1 - i0) * (k + 0.5) / count, j1, z + 9) for k in range(count)]


def glow_right(j0, j1, i1, z, count):
    return [P(i1, j0 + (j1 - j0) * (k + 0.5) / count, z + 9) for k in range(count)]


PINE_SVG = (
    '<svg xmlns="http://www.w3.org/2000/svg" width="64" height="104" viewBox="0 0 64 104">'
    '<defs>'
    '<radialGradient id="sh"><stop offset="0" stop-color="#0c1a12" stop-opacity="0.55"/>'
    '<stop offset="1" stop-color="#0c1a12" stop-opacity="0"/></radialGradient>'
    '<linearGradient id="lt" x1="0" y1="1" x2="0" y2="0"><stop offset="0" stop-color="#3f8f52"/>'
    '<stop offset="1" stop-color="#7ccb7e"/></linearGradient>'
    '<linearGradient id="dk" x1="0" y1="1" x2="0" y2="0"><stop offset="0" stop-color="#1d4a2e"/>'
    '<stop offset="1" stop-color="#2f6d40"/></linearGradient>'
    '<linearGradient id="tr" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#3a2416"/>'
    '<stop offset="1" stop-color="#6e4a2e"/></linearGradient>'
    '</defs>'
    '<ellipse cx="32" cy="98" rx="28" ry="6" fill="url(#sh)"/>'
    '<rect x="29" y="80" width="6" height="18" fill="url(#tr)" stroke="#171a2c" stroke-width="1"/>'
    + ''.join(
        f'<polygon points="32,{apex} {32 - w},{base} 32,{base + 5}" fill="url(#lt)" stroke="#171a2c" '
        f'stroke-width="1.2" stroke-linejoin="round"/>'
        f'<polygon points="32,{apex} 32,{base + 5} {32 + w},{base}" fill="url(#dk)" stroke="#171a2c" '
        f'stroke-width="1.2" stroke-linejoin="round"/>'
        for apex, base, w in [(30, 84, 24), (16, 64, 19), (4, 44, 14)])
    + '</svg>')

OAK_SVG = (
    '<svg xmlns="http://www.w3.org/2000/svg" width="56" height="74" viewBox="0 0 56 74">'
    '<defs>'
    '<radialGradient id="sh"><stop offset="0" stop-color="#0c1a12" stop-opacity="0.55"/>'
    '<stop offset="1" stop-color="#0c1a12" stop-opacity="0"/></radialGradient>'
    '<radialGradient id="cr" cx="0.38" cy="0.34" r="0.7"><stop offset="0" stop-color="#9fe08a"/>'
    '<stop offset="0.5" stop-color="#4a9a52"/><stop offset="1" stop-color="#24563a"/></radialGradient>'
    '<linearGradient id="tr" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#3a2416"/>'
    '<stop offset="1" stop-color="#6e4a2e"/></linearGradient>'
    '</defs>'
    '<ellipse cx="28" cy="68" rx="26" ry="6" fill="url(#sh)"/>'
    '<rect x="25" y="44" width="6" height="24" fill="url(#tr)" stroke="#171a2c" stroke-width="1"/>'
    '<circle cx="28" cy="25" r="22" fill="url(#cr)" stroke="#171a2c" stroke-width="1.4"/>'
    '</svg>')

# Where each tree's trunk meets the ground, inside its SVG.
TREE_BASE = {"Pine": (32, 98, 64, PINE_SVG), "Oak": (28, 68, 56, OAK_SVG)}


def tree_call(kind, i, j, first, copy_number):
    """The first tree of a kind writes its SVG and places it as an embedded Smart Object;
    every later one duplicates that layer (sharing its source) and moves the copy into place,
    on top of the stack so the back-to-front order holds."""
    ax, ay, width, svg = TREE_BASE[kind]
    bx, by = P(i, j)
    x, y = round(bx - ax, 1), round(by - ay, 1)
    head = "var doc = app.getDocument(patchy.args.documentId); doc.activate();\n"
    if first:
        code = (head +
                f"var file = patchy.args.workDir + '/{kind.lower()}.svg';\n"
                f"patchy.io.writeTextFile(file, {js(svg)});\n"
                f"var tree = doc.addSmartObject(file, {{x: {x:g}, y: {y:g}, width: {width}, name: '{kind}'}});\n"
                "patchy.setResult({layer: tree.id, sourceId: tree.getSmartObject().sourceId});\n")
        return {"tool": "execute_script", "name": f"{kind} Smart Object",
                "note": f"Draw one {kind.lower()} as SVG and place it as a Smart Object", "code": code}
    code = (head +
            f"var source = doc.findLayer('{kind}');\n"
            "var copy = source.duplicate();\n"
            f"copy.name = '{kind} {copy_number}';\n"
            f"copy.moveTo({x:g}, {y:g});\n"
            "doc.moveLayers([copy], {parentId: null});\n"
            "patchy.setResult({layer: copy.id, sharesSource: copy.getSmartObject().sourceId === source.getSmartObject().sourceId});\n")
    return {"tool": "execute_script", "name": f"{kind} copy {copy_number}",
            "note": f"Duplicate the {kind.lower()} Smart Object", "code": code}


def dots(color, size, points, opacity=100, softness=0):
    out = []
    for (x, y) in points:
        st = {"color": color, "size": size,
              "points": [{"x": round(x, 1), "y": round(y, 1)}, {"x": round(x + 0.4, 1), "y": round(y, 1)}]}
        if opacity != 100:
            st["opacity"] = opacity
        if softness:
            st["softness"] = softness
        out.append(st)
    return out


def stroke(color, size, points, opacity=100, softness=0, step=3.0):
    dense = [points[0]]
    for a, b in zip(points, points[1:]):
        n = max(1, int(math.dist(a, b) / step))
        dense += [(a[0] + (b[0] - a[0]) * k / n, a[1] + (b[1] - a[1]) * k / n) for k in range(1, n + 1)]
    out = {"color": color, "size": size, "points": [{"x": round(x, 1), "y": round(y, 1)} for x, y in dense]}
    if opacity != 100:
        out["opacity"] = opacity
    if softness:
        out["softness"] = softness
    return out


def main():
    rng = random.Random(1102)
    calls = []

    calls.append({
        "tool": "execute_script",
        "note": "New document, a gradient dusk sky, and a Stars layer",
        "name": "Set up the harbor",
        "code": (
            f"var doc = app.newDocument({W}, {H}, {{name: 'Isometric Harbor'}});\n"
            "var paper = doc.activeLayer;\n"
            "doc.addFillLayer('Dusk sky', {type: 'gradient', gradient: {type: 'linear', angle: 90,\n"
            "  colorStops: [{position: 0, color: '#d9876f'}, {position: 0.32, color: '#7a5682'},\n"
            "               {position: 0.66, color: '#2f3566'}, {position: 1, color: '#131938'}]}});\n"
            "paper.remove();\n"
            "var stars = doc.addLayer('Stars');\n"
            "patchy.setResult({documentId: doc.id, layers: {Stars: stars.id}});\n"
        ),
    })

    # Stars: a few long strokes with the brush's scatter dynamics strew dabs across the sky,
    # so a whole star field is a handful of strokes; a soft one adds the larger glowing stars.
    def scatter(color, size, path, spacing, scatter_amount, jitter, opacity, softness, seed):
        st = stroke(color, size, path, opacity=opacity, softness=softness, step=6.0)
        st.update({"spacing": spacing, "scatter": scatter_amount, "sizeJitter": jitter, "seed": seed})
        return st
    stars = [
        scatter("#fff6dc", 3, [(20, 40), (760, 30)], 9.0, 10, 0.7, 90, 0, 7),
        scatter("#fff6dc", 3, [(20, 150), (800, 140)], 10.0, 10, 0.7, 85, 0, 11),
        scatter("#fff6dc", 2, [(380, 250), (820, 250)], 10.0, 10, 0.6, 70, 0, 13),
        scatter("#ffe7b0", 10, [(30, 90), (780, 200)], 6.0, 9, 0.5, 30, 100, 17),
    ]
    calls.append({"tool": "draw_strokes", "layer": "Stars", "note": "Stars", "strokes": stars})

    s = Script("Moon")
    s.ell("Moon halo", 60, 20, 240, 240, glow("#ffe9c0", 0.5))
    # A crescent: the moon's disc less a second disc offset up and right, traced as one path.
    mx, my, mr = 180.0, 140.0, 40.0
    ox, oy, orr = 200.0, 128.0, 37.0
    crescent = []
    for k in range(73):  # outer edge, the part outside the cutting disc
        a = math.radians(k * 5)
        x, y = mx + mr * math.cos(a), my + mr * math.sin(a)
        if math.hypot(x - ox, y - oy) >= orr:
            crescent.append((a, x, y))
    # Start from the gap so the outer arc is one continuous run.
    angles = [a for a, _, _ in crescent]
    gaps = [(angles[(k + 1) % len(angles)] - angles[k]) % (2 * math.pi) for k in range(len(angles))]
    start = (gaps.index(max(gaps)) + 1) % len(crescent)
    outer = [(round(x, 1), round(y, 1)) for _, x, y in crescent[start:] + crescent[:start]]
    first, last = outer[0], outer[-1]
    a0 = math.atan2(first[1] - oy, first[0] - ox)
    a1 = math.atan2(last[1] - oy, last[0] - ox)
    span = (a0 - a1) % (2 * math.pi)
    mid = a1 + span / 2  # the inner edge is the cutting circle's arc that lies inside the moon
    if math.hypot(ox + orr * math.cos(mid) - mx, oy + orr * math.sin(mid) - my) > mr:
        span -= 2 * math.pi
    inner = [(round(ox + orr * math.cos(a1 + span * t / 30), 1), round(oy + orr * math.sin(a1 + span * t / 30), 1))
             for t in range(1, 30)]
    s.poly("Moon", outer + inner, rad(["#fffbea", "#f6e3b0", "#d9bf86"]), 0)
    calls.append(s.call("A crescent moon with a soft halo"))

    s = Script("Sea")
    s.poly("Sea", [(0, HORIZON), (W, HORIZON), (W, H), (0, H)], lin(["#0b1830", "#173357", "#35587f"], 90), 0)
    s.poly("Horizon haze", [(0, HORIZON - 34), (W, HORIZON - 34), (W, HORIZON + 30), (0, HORIZON + 30)],
           "{type: 'gradient', gradient: {type: 'linear', angle: 90, colorStops: stops(['#f0aa90', '#f0aa90']),"
           " alphaStops: [{position: 0, opacity: 0}, {position: 0.55, opacity: 0.45}, {position: 1, opacity: 0}]}}", 0)
    s.raw("var ripples = doc.addLayer('Ripples');")
    s.extra = "{Ripples: ripples.id}"
    calls.append(s.call("The sea: a deep gradient with haze on the horizon, and a Ripples layer"))

    ripples = []
    for _ in range(50):
        x, y = rng.uniform(10, W - 10), rng.uniform(HORIZON + 8, H - 8)
        if 230 < x < 850 and y < 540:
            continue
        w = rng.uniform(14, 46) * (0.6 + (y - HORIZON) / (H - HORIZON))
        ripples.append(stroke("#9cc4e4", 2, [(x, y), (x + w, y + 0.5)], opacity=rng.choice([35, 50, 70])))
    for k in range(9):  # the moon's reflection, broken by the waves
        y = HORIZON + 16 + k * 26
        half = 14 + k * 5
        ripples.append(stroke("#f7e6bb", 3, [(180 - half, y), (180 + half, y)], opacity=70 - k * 5, softness=40))
    calls.append({"tool": "draw_strokes", "layer": "Ripples", "note": "Ripples and the moon's reflection",
                  "strokes": ripples})

    s = Script("Island")
    s.ell("Island shadow on the water", 230, 470, 620, 150, glow("#050a16", 0.65))
    s.poly("Earth, west side", [P(0, N, 0), P(0, N, -SIDE), P(N, N, -SIDE), P(N, N, 0)],
           lin(["#3d2416", "#7a5233", "#a5744a"], 90))
    s.poly("Earth, east side", [P(N, N, 0), P(N, N, -SIDE), P(N, 0, -SIDE), P(N, 0, 0)],
           lin(["#24150c", "#4f331f", "#6f4a2d"], 90))
    for k, z in enumerate((-16, -33)):
        s.poly(f"Rock stratum {k + 1}", [P(0, N, z), P(N, N, z), P(N, 0, z), P(N, 0, z - 3), P(N, N, z - 3),
                                         P(0, N, z - 3)], solid("#2a170c"), 0, 25)
    s.poly("Lawn", [P(0, 0), P(0, N), P(N, N), P(N, 0)], rad(["#9edb84", "#62b05a", "#3c8046"]))
    s.poly("Sand road", [P(0, 4.5), P(0, 5.5), P(N, 5.5), P(N, 4.5)], lin(["#c9a96a", "#f0dcaa"], 0), 1.2)
    s.raw("var grass = doc.addLayer('Grass detail');")
    s.extra = "{Grass: grass.id}"
    calls.append(s.call("The island: earth sides, a radial gradient lawn, a sand road"))

    grass = []
    for k in range(N + 1):
        grass.append(stroke("#b6f09a", 1, [P(k, 0), P(k, N)], opacity=22))
        grass.append(stroke("#b6f09a", 1, [P(0, k), P(N, k)], opacity=22))
    calls.append({"tool": "draw_strokes", "layer": "Grass", "note": "The tile grid",
                  "strokes": grass})

    lamp = P(9.05, 1.05, 186)
    s = Script("Lighthouse beams")
    s.poly("Beam north", [lamp, (W, 30), (W, 140)], "{type: 'gradient', gradient: {type: 'linear', angle: 0,"
           " colorStops: stops(['#fff1b8', '#fff1b8']), alphaStops: [{position: 0, opacity: 0.7},"
           " {position: 1, opacity: 0}]}}", 0)
    s.poly("Beam east", [lamp, (W, 170), (W, 250)], "{type: 'gradient', gradient: {type: 'linear', angle: 0,"
           " colorStops: stops(['#fff1b8', '#fff1b8']), alphaStops: [{position: 0, opacity: 0.55},"
           " {position: 1, opacity: 0}]}}", 0)
    calls.append(s.call("The lighthouse beams, fading with distance"))

    glow_spots = []
    objects = []

    s = Script("Clock tower")
    box(s, "Tower", 6.0, 7.4, 1.0, 2.4, 132, ["#5c6782", "#a3b0cc"], ["#3d4560", "#6a7594"], None)
    apex = P(6.7, 1.7, 182)
    s.poly("Roof shade", [apex, P(7.55, 2.55, 132), P(7.55, 0.85, 132)], lin(["#3c2e52", "#5b4678"], 90))
    s.poly("Roof light", [apex, P(5.85, 2.55, 132), P(7.55, 2.55, 132)], lin(["#5f4a86", "#9a7cc6"], 90))
    cx, cy = P(6.7, 2.4, 104)
    s.ell("Clock face", cx - 15, cy - 15, 30, 30, rad(["#fffbe6", "#efe0b8"]), 1.6)
    s.raw(f"poly('Clock hands', [{cx:g}, {cy - 11:g}, {cx + 1.5:g}, {cy:g}, {cx + 9:g}, {cy + 3:g}, "
          f"{cx:g}, {cy + 1.5:g}], '#171a2c', 0);")
    windows_left(s, "Tower", 6.0, 7.4, 2.4, 52, 2, 22)
    windows_right(s, "Tower", 1.0, 2.4, 7.4, 52, 2, 22)
    objects.append((2.4 + 7.4, s.call("Clock tower with a pyramid roof and a clock face")))
    glow_spots += glow_left(6.0, 7.4, 2.4, 52, 2) + glow_right(1.0, 2.4, 7.4, 52, 2)

    s = Script("Lighthouse")
    box(s, "Tower", 8.6, 9.5, 0.6, 1.5, 160, ["#cfc7bb", "#fffaf0"], ["#9e968c", "#e2dbcf"], None)
    for k, z in enumerate((30, 78, 126)):
        s.poly(f"Red band {k + 1}", [P(8.6, 1.5, z + 14), P(8.6, 1.5, z), P(9.5, 1.5, z), P(9.5, 0.6, z),
                                     P(9.5, 0.6, z + 14), P(9.5, 1.5, z + 14)], lin(["#8e2420", "#e0544a"], 90), 1.2)
    box(s, "Gallery", 8.45, 9.65, 0.45, 1.65, 8, ["#3c3f52", "#5c6076"], ["#2c2e3c", "#474a5c"],
        ["#4c5068", "#6b7090"], z0=160)
    s.ell("Lamp glow", lamp[0] - 50, lamp[1] - 44, 100, 88, glow("#fff1b8", 0.95))
    box(s, "Lantern", 8.75, 9.35, 0.75, 1.35, 22, ["#ffe39a", "#fff7d6"], ["#f4c460", "#ffe7a8"], None, z0=168)
    s.poly("Lantern cap", [P(9.05, 1.05, 208), P(8.7, 1.4, 190), P(9.4, 1.4, 190), P(9.4, 0.7, 190)],
           lin(["#7a1f1c", "#c23b35"], 90))
    objects.append((1.5 + 9.5, s.call("Lighthouse with red bands and a glowing lantern")))

    s = Script("Town hall")
    box(s, "Walls", 1.5, 4.5, 1.0, 3.0, 64, ["#b59a72", "#efdcb4"], ["#8a7356", "#c2aa83"], None)
    gable_roof(s, "Town hall", 1.5, 4.5, 1.0, 3.0, 64, 42, ["#7a2a24", "#d0604f"], ["#5a1f1c", "#8e3a33"],
               ["#a88e6a", "#dcc49c"])
    windows_left(s, "Town hall", 1.5, 4.5, 3.0, 22, 4)
    windows_right(s, "Town hall", 1.0, 3.0, 4.5, 22, 2)
    s.poly("Door", [P(2.85, 3.0, 26), P(2.85, 3.0, 0), P(3.15, 3.0, 0), P(3.15, 3.0, 26)],
           lin(["#3d2414", "#7a4a2a"], 90), 1.2)
    objects.append((3.0 + 4.5, s.call("Town hall with a gable roof")))
    glow_spots += glow_left(1.5, 4.5, 3.0, 22, 4) + glow_right(1.0, 3.0, 4.5, 22, 2)

    s = Script("Warehouse")
    box(s, "Walls", 6.8, 9.3, 6.4, 7.8, 46, ["#7a4a30", "#b8825a"], ["#553321", "#8a5c3c"],
        ["#6a4632", "#94684a"])
    for k in range(6):
        i = 6.8 + (9.3 - 6.8) * (k + 1) / 7
        s.poly(f"Plank seam {k + 1}", [P(i, 7.8, 44), P(i, 7.8, 2), P(i + 0.025, 7.8, 2), P(i + 0.025, 7.8, 44)],
               solid("#3b2216"), 0, 60)
    s.poly("Lit doorway", [P(7.7, 7.8, 32), P(7.7, 7.8, 0), P(8.4, 7.8, 0), P(8.4, 7.8, 32)],
           lin(["#ff9d3a", "#ffe2a0"], 90), 1.4)
    windows_right(s, "Warehouse", 6.4, 7.8, 9.3, 18, 2)
    objects.append((7.8 + 9.3, s.call("Warehouse with a lit doorway")))
    glow_spots += [P(8.05, 7.8, 12), P(8.05, 7.8, 26)] + glow_right(6.4, 7.8, 9.3, 18, 2)

    s = Script("Cottage")
    box(s, "Walls", 1.2, 3.6, 6.6, 8.4, 44, ["#c2ad8a", "#f6e9ca"], ["#8f7c60", "#c7b18c"], None)
    box(s, "Chimney", 2.0, 2.4, 6.9, 7.3, 34, ["#7a3a2c", "#b25a44"], ["#5a2a20", "#8a4434"],
        ["#4a2219", "#6a3326"], z0=60)
    gable_roof(s, "Cottage", 1.2, 3.6, 6.6, 8.4, 44, 30, ["#24496b", "#4a86b8"], ["#1a3550", "#2c5678"],
               ["#9e8a6a", "#d8c4a0"])
    windows_left(s, "Cottage", 1.2, 3.6, 8.4, 14, 3)
    windows_right(s, "Cottage", 6.6, 8.4, 3.6, 14, 1)
    objects.append((8.4 + 3.6, s.call("Cottage with a chimney")))
    glow_spots += glow_left(1.2, 3.6, 8.4, 14, 3) + glow_right(6.6, 8.4, 3.6, 14, 1)

    trees = [("Pine", 5.2, 0.6), ("Oak", 8.8, 3.4), ("Pine", 5.0, 3.8), ("Oak", 0.8, 3.9), ("Pine", 9.6, 2.9),
             ("Pine", 6.0, 8.8), ("Oak", 4.4, 9.4), ("Pine", 9.4, 9.5), ("Oak", 0.9, 9.6)]
    # Placed in back-to-front order, so the first of each kind is the farthest one.
    seen = {}
    for kind, i, j in sorted(trees, key=lambda t: t[1] + t[2]):
        seen[kind] = seen.get(kind, 0) + 1
        objects.append((i + j, tree_call(kind, i, j, seen[kind] == 1, seen[kind])))

    objects.sort(key=lambda item: item[0])
    calls += [c for _, c in objects]

    s = Script("Sailboat")
    bx, by = 160, 610
    s.ell("Reflection", bx - 64, by + 10, 128, 36, glow("#050a16", 0.55))
    s.poly("Hull", [(bx - 60, by), (bx - 44, by + 22), (bx + 44, by + 22), (bx + 62, by)],
           lin(["#3c2414", "#7a4a2a", "#b5763f"], 90))
    s.poly("Mast", [(bx - 2, by), (bx - 2, by - 104), (bx + 2, by - 104), (bx + 2, by)], solid("#2a1a10"), 0)
    s.poly("Mainsail", [(bx + 5, by - 100), (bx + 5, by - 8), (bx + 56, by - 8)], lin(["#d8cdb8", "#fffaf0"], 0))
    s.poly("Jib", [(bx - 5, by - 92), (bx - 5, by - 10), (bx - 44, by - 10)], lin(["#c9bea8", "#f2ead8"], 180))
    s.poly("Pennant", [(bx, by - 104), (bx - 26, by - 97), (bx, by - 90)], lin(["#8e2420", "#e04a3e"], 0), 1.0)
    calls.append(s.call("A sailboat"))

    calls.append({
        "tool": "execute_script", "note": "A Screen layer on top for the window glow", "name": "Window glow layer",
        "code": ("var doc = app.getDocument(patchy.args.documentId); doc.activate();\n"
                 "doc.activeLayer = doc.layers[doc.layers.length - 1];\n"
                 "var glow = doc.addLayer('Window glow'); glow.blendMode = 'screen';\n"
                 "patchy.setResult({layers: {Glow: glow.id}});\n"),
    })
    glows = [stroke("#ffb050", 36, [p, (p[0] + 0.5, p[1])], opacity=45, softness=100) for p in glow_spots]
    glows.append(stroke("#ffe7a0", 80, [lamp, (lamp[0] + 0.5, lamp[1])], opacity=55, softness=100))
    calls.append({"tool": "draw_strokes", "layer": "Glow", "note": "Warm light spilling from the windows",
                  "strokes": glows})

    out = Path(__file__).resolve().parents[3] / "test-fixtures" / "readme" / "mcp_isometric_calls.json"
    out.write_text(json.dumps({"document": {"width": W, "height": H}, "calls": calls}, indent=1) + "\n",
                   encoding="utf-8", newline="\n")
    print(out, len(calls), "calls,", sum(len(c.get("strokes", [])) for c in calls), "strokes")


if __name__ == "__main__":
    main()
