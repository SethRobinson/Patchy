// @name Make Script Icons
// Dev tool (not bundled): regenerates the 128x128 icon PNGs that sit next to
// every bundled script (docs/scripting.md "Script icons"). Each icon is
// deliberate procedural mini-artwork drawn with the scripting API itself and
// exported with doc.exportAs, so the set is code-generated (the repo rule:
// bundled art never comes from AI image generation) and regenerable.
//
// Run from the repo root:
//   patchy --run-script scripts/dev/make-script-icons.js ^
//          --script-arg out=D:/projects/AI/codex/Patchy/scripts/bundled
// then commit the PNGs. The Fancy Background icon is that script's REAL
// output at 64x64 (via include), everything else is drawn below.

app.undoEnabled = false;

var OUT = (patchy.args && patchy.args.out) || "";
if (!OUT) {
  console.error("Pass --script-arg out=<path to scripts/bundled>");
} else {

// ---------------------------------------------------------------------------
// Painting kit: the icons are DESIGNED in a 64-unit space (every primitive
// scales its geometry by SCALE internally) but RENDERED at 128x128, so the
// Script Manager's hover card can show them big while the tree scales down.

var SIZE = 128;
var SCALE = SIZE / 64;
var CORNER = 9 * SCALE;  // shared rounded-corner radius so the set reads as one family

function hex(s) {
  var n = parseInt(s.slice(1), 16);
  return { r: (n >> 16) & 255, g: (n >> 8) & 255, b: n & 255 };
}

function Surface() {
  this.d = new Uint8Array(SIZE * SIZE * 4);
}

// Source-over blend of straight (non-premultiplied) RGBA.
Surface.prototype.blend = function (x, y, rgb, a) {
  if (x < 0 || y < 0 || x >= SIZE || y >= SIZE || a <= 0) { return; }
  var i = (y * SIZE + x) * 4;
  var da = this.d[i + 3] / 255;
  var outA = a + da * (1 - a);
  if (outA <= 0) { return; }
  var k = da * (1 - a);
  this.d[i] = Math.round((rgb.r * a + this.d[i] * k) / outA);
  this.d[i + 1] = Math.round((rgb.g * a + this.d[i + 1] * k) / outA);
  this.d[i + 2] = Math.round((rgb.b * a + this.d[i + 2] * k) / outA);
  this.d[i + 3] = Math.round(outA * 255);
};

// Axis-aligned rect with fractional-edge coverage. Like every primitive
// below, coordinates are in 64-unit design space.
Surface.prototype.rect = function (x, y, w, h, rgb, a) {
  if (a === undefined) { a = 1; }
  x *= SCALE; y *= SCALE; w *= SCALE; h *= SCALE;
  var x1 = x + w;
  var y1 = y + h;
  for (var py = Math.floor(y); py < y1; py++) {
    for (var px = Math.floor(x); px < x1; px++) {
      var cov = (Math.min(px + 1, x1) - Math.max(px, x)) *
                (Math.min(py + 1, y1) - Math.max(py, y));
      if (cov > 0) { this.blend(px, py, rgb, a * Math.min(1, cov)); }
    }
  }
};

// Vertical gradient, overwriting (used for backgrounds).
Surface.prototype.vGradient = function (top, bottom) {
  for (var y = 0; y < SIZE; y++) {
    var t = y / (SIZE - 1);
    var rgb = { r: Math.round(top.r + (bottom.r - top.r) * t),
                g: Math.round(top.g + (bottom.g - top.g) * t),
                b: Math.round(top.b + (bottom.b - top.b) * t) };
    for (var x = 0; x < SIZE; x++) {
      var i = (y * SIZE + x) * 4;
      this.d[i] = rgb.r; this.d[i + 1] = rgb.g; this.d[i + 2] = rgb.b; this.d[i + 3] = 255;
    }
  }
};

Surface.prototype.fill = function (rgb) { this.vGradient(rgb, rgb); };

Surface.prototype.circle = function (cx, cy, radius, rgb, a) {
  if (a === undefined) { a = 1; }
  cx *= SCALE; cy *= SCALE; radius *= SCALE;
  var r1 = radius + 1;
  for (var y = Math.floor(cy - r1); y <= cy + r1; y++) {
    for (var x = Math.floor(cx - r1); x <= cx + r1; x++) {
      var d = Math.sqrt((x + 0.5 - cx) * (x + 0.5 - cx) + (y + 0.5 - cy) * (y + 0.5 - cy));
      var cov = Math.min(1, Math.max(0, radius - d + 0.5));
      if (cov > 0) { this.blend(x, y, rgb, a * cov); }
    }
  }
};

// Ring (circle outline); a0/a1 (radians) limit it to an arc when given.
Surface.prototype.ring = function (cx, cy, radius, thickness, rgb, a, a0, a1) {
  if (a === undefined) { a = 1; }
  cx *= SCALE; cy *= SCALE; radius *= SCALE; thickness *= SCALE;
  var r1 = radius + thickness;
  for (var y = Math.floor(cy - r1); y <= cy + r1; y++) {
    for (var x = Math.floor(cx - r1); x <= cx + r1; x++) {
      var dx = x + 0.5 - cx;
      var dy = y + 0.5 - cy;
      var d = Math.sqrt(dx * dx + dy * dy);
      var cov = Math.min(1, Math.max(0, thickness / 2 - Math.abs(d - radius) + 0.5));
      if (cov <= 0) { continue; }
      if (a0 !== undefined) {
        var ang = Math.atan2(dy, dx);
        while (ang < a0) { ang += Math.PI * 2; }
        if (ang > a1) { continue; }
      }
      this.blend(x, y, rgb, a * cov);
    }
  }
};

// Thick anti-aliased line segment.
Surface.prototype.line = function (x0, y0, x1, y1, thickness, rgb, a) {
  if (a === undefined) { a = 1; }
  x0 *= SCALE; y0 *= SCALE; x1 *= SCALE; y1 *= SCALE; thickness *= SCALE;
  var half = thickness / 2;
  var minX = Math.floor(Math.min(x0, x1) - half - 1);
  var maxX = Math.ceil(Math.max(x0, x1) + half + 1);
  var minY = Math.floor(Math.min(y0, y1) - half - 1);
  var maxY = Math.ceil(Math.max(y0, y1) + half + 1);
  var vx = x1 - x0;
  var vy = y1 - y0;
  var lenSq = vx * vx + vy * vy;
  for (var y = minY; y <= maxY; y++) {
    for (var x = minX; x <= maxX; x++) {
      var px = x + 0.5 - x0;
      var py = y + 0.5 - y0;
      var t = lenSq > 0 ? Math.max(0, Math.min(1, (px * vx + py * vy) / lenSq)) : 0;
      var dx = px - t * vx;
      var dy = py - t * vy;
      var d = Math.sqrt(dx * dx + dy * dy);
      var cov = Math.min(1, Math.max(0, half - d + 0.5));
      if (cov > 0) { this.blend(x, y, rgb, a * cov); }
    }
  }
};

// Rounded rect with AA (signed-distance coverage).
Surface.prototype.rrect = function (x, y, w, h, radius, rgb, a) {
  if (a === undefined) { a = 1; }
  x *= SCALE; y *= SCALE; w *= SCALE; h *= SCALE; radius *= SCALE;
  var cx0 = x + radius;
  var cy0 = y + radius;
  var cx1 = x + w - radius;
  var cy1 = y + h - radius;
  for (var py = Math.floor(y) - 1; py <= y + h + 1; py++) {
    for (var px = Math.floor(x) - 1; px <= x + w + 1; px++) {
      var qx = Math.max(cx0, Math.min(cx1, px + 0.5));
      var qy = Math.max(cy0, Math.min(cy1, py + 0.5));
      var dx = px + 0.5 - qx;
      var dy = py + 0.5 - qy;
      var d = Math.sqrt(dx * dx + dy * dy);
      var cov = Math.min(1, Math.max(0, radius - d + 0.5));
      if (cov > 0) { this.blend(px, py, rgb, a * cov); }
    }
  }
};

// Filled triangle, 2x2 supersampled.
Surface.prototype.tri = function (x0, y0, x1, y1, x2, y2, rgb, a) {
  if (a === undefined) { a = 1; }
  x0 *= SCALE; y0 *= SCALE; x1 *= SCALE; y1 *= SCALE; x2 *= SCALE; y2 *= SCALE;
  function side(px, py, ax, ay, bx, by) { return (bx - ax) * (py - ay) - (by - ay) * (px - ax); }
  var minX = Math.floor(Math.min(x0, x1, x2));
  var maxX = Math.ceil(Math.max(x0, x1, x2));
  var minY = Math.floor(Math.min(y0, y1, y2));
  var maxY = Math.ceil(Math.max(y0, y1, y2));
  var offs = [0.25, 0.75];
  for (var y = minY; y <= maxY; y++) {
    for (var x = minX; x <= maxX; x++) {
      var hits = 0;
      for (var sy = 0; sy < 2; sy++) {
        for (var sx = 0; sx < 2; sx++) {
          var px = x + offs[sx];
          var py = y + offs[sy];
          var s0 = side(px, py, x0, y0, x1, y1);
          var s1 = side(px, py, x1, y1, x2, y2);
          var s2 = side(px, py, x2, y2, x0, y0);
          if ((s0 >= 0 && s1 >= 0 && s2 >= 0) || (s0 <= 0 && s1 <= 0 && s2 <= 0)) { hits++; }
        }
      }
      if (hits > 0) { this.blend(x, y, rgb, a * hits / 4); }
    }
  }
};

// Thick right-pointing arrow (shaft + head), the shared "export" motif.
Surface.prototype.arrowRight = function (x, y, length, size, rgb, a) {
  var headLen = size * 1.2;
  this.rect(x, y - size / 2, length - headLen, size, rgb, a);
  this.tri(x + length - headLen, y - size, x + length, y, x + length - headLen, y + size, rgb, a);
};

// Multiplies alpha by rounded-rect coverage; call last so every icon shares
// the same rounded silhouette.
function roundCorners(data) {
  var radius = CORNER;
  for (var y = 0; y < SIZE; y++) {
    for (var x = 0; x < SIZE; x++) {
      var qx = Math.max(radius, Math.min(SIZE - radius, x + 0.5));
      var qy = Math.max(radius, Math.min(SIZE - radius, y + 0.5));
      var dx = x + 0.5 - qx;
      var dy = y + 0.5 - qy;
      var cov = Math.min(1, Math.max(0, radius - Math.sqrt(dx * dx + dy * dy) + 0.5));
      if (cov < 1) {
        var i = (y * SIZE + x) * 4 + 3;
        data[i] = Math.round(data[i] * cov);
      }
    }
  }
}

// Deterministic PRNG so every regeneration draws the same icons.
function makeRand(seed) {
  var state = seed >>> 0;
  return function () {
    state = (state * 1664525 + 1013904223) >>> 0;
    return state / 4294967296;
  };
}

// Shared palette (matches the app's dark theme accents).
var BLUE = hex("#6fb1e8");
var GREEN = hex("#5ac85a");
var ORANGE = hex("#e6a03c");
var RED = hex("#e65050");
var YELLOW = hex("#e6dc46");
var TEAL = hex("#46c8b4");
var PINK = hex("#e86fb1");
var WHITE = hex("#f0f2f4");
var PAPER = hex("#eceee8");
var INK = hex("#3a4356");
var BG_TOP = hex("#1a2030");
var BG_BOTTOM = hex("#10141c");

function background(s) { s.vGradient(BG_TOP, BG_BOTTOM); }

// ---------------------------------------------------------------------------
// The icons.

var ICONS = {};

ICONS["Games/breakout"] = function (s) {
  s.vGradient(hex("#141b2e"), hex("#0a0e17"));
  var rows = [RED, ORANGE, YELLOW, GREEN];
  for (var r = 0; r < 4; r++) {
    for (var c = 0; c < 4; c++) {
      if (r === 1 && c === 2) { continue; }  // one brick already gone
      s.rrect(7 + c * 13, 9 + r * 7, 11, 5, 1.5, rows[r]);
    }
  }
  s.rrect(22, 52, 20, 4, 2, BLUE);
  s.circle(40, 43, 3, WHITE);
  s.line(43, 36, 47, 30, 1.5, hex("#8a939f"), 0.7);  // bounce trail
};

ICONS["Games/pong"] = function (s) {
  s.fill(hex("#0b0d13"));
  for (var y = 6; y < 60; y += 8) {
    s.rect(31, y, 2, 4, hex("#3a4356"));
  }
  s.rrect(7, 20, 4, 16, 1.5, WHITE);
  s.rrect(53, 28, 4, 16, 1.5, WHITE);
  s.rect(37, 18, 5, 5, YELLOW);
  s.line(34, 24, 28, 31, 1.5, hex("#6b7480"), 0.6);  // ball trail
};

ICONS["Games/game-of-life"] = function (s) {
  s.fill(hex("#10141c"));
  var grid = hex("#202839");
  for (var i = 0; i <= 8; i++) {
    s.rect(i * 8 - 0.5, 0, 1, 64, grid, 0.9);
    s.rect(0, i * 8 - 0.5, 64, 1, grid, 0.9);
  }
  function cell(cx, cy, rgb, a) { s.rrect(cx * 8 + 1, cy * 8 + 1, 6, 6, 1, rgb, a); }
  // The glider.
  cell(3, 1, GREEN); cell(4, 2, GREEN); cell(2, 3, GREEN); cell(3, 3, GREEN); cell(4, 3, GREEN);
  // A block and a fading loner elsewhere.
  cell(6, 5, GREEN, 0.85); cell(6, 6, GREEN, 0.85); cell(5, 5, GREEN, 0.85); cell(5, 6, GREEN, 0.85);
  cell(1, 6, GREEN, 0.35);
};

ICONS["Demos/letter-physics"] = function (s) {
  background(s);
  s.rect(6, 52, 52, 2.5, hex("#3a4356"));
  // A chunky "A" resting on the ground...
  var a = hex("#f0f2f4");
  s.line(18, 51, 26, 26, 5, a);
  s.line(26, 26, 34, 51, 5, a);
  s.rect(21, 40, 10, 4, a);
  // ...and a smaller "o" still falling, with motion lines.
  s.ring(46, 30, 5.5, 4, YELLOW);
  s.line(45, 8, 44, 17, 1.5, hex("#8a939f"), 0.7);
  s.line(51, 11, 50, 18, 1.5, hex("#8a939f"), 0.5);
};

ICONS["Demos/dialog-showcase"] = function (s) {
  background(s);
  s.rrect(11, 11, 42, 42, 3, hex("#000000"), 0.3);  // drop shadow
  s.rrect(10, 10, 42, 42, 3, PAPER);
  s.rrect(10, 10, 42, 9, 3, hex("#3b4c66"));
  s.rect(10, 15, 42, 4, hex("#3b4c66"));
  s.circle(47, 14.5, 2, hex("#e65050"), 0.9);  // close button
  // Slider row.
  s.rect(15, 26, 32, 2.5, hex("#aab2be"));
  s.rect(15, 26, 17, 2.5, BLUE);
  s.circle(32, 27, 3.5, hex("#3b6ea5"));
  // Checkbox row.
  s.rrect(15, 34, 8, 8, 1.5, hex("#3b6ea5"));
  s.line(17, 38, 19, 40, 1.6, WHITE);
  s.line(19, 40, 22, 35.5, 1.6, WHITE);
  s.rect(26, 37, 18, 2.5, hex("#aab2be"));
  // OK button.
  s.rrect(33, 44, 14, 6, 2, hex("#3b6ea5"));
};

ICONS["Effects/generative-art"] = function (s) {
  s.vGradient(hex("#12161f"), hex("#0d1016"));
  // A real (tiny) flow field: strokes follow a sine field, like the script.
  var rand = makeRand(11);
  var colors = [TEAL, ORANGE, PINK, BLUE, YELLOW];
  for (var k = 0; k < 14; k++) {
    var x = 4 + rand() * 56;
    var y = 4 + rand() * 56;
    var rgb = colors[k % colors.length];
    for (var step = 0; step < 26; step++) {
      var angle = Math.sin(x * 0.11) * 1.8 + Math.cos(y * 0.13) * 1.8;
      x += Math.cos(angle) * 1.8;
      y += Math.sin(angle) * 1.8;
      if (x < 2 || y < 2 || x > 62 || y > 62) { break; }
      s.circle(x, y, 1.1, rgb, 0.8);
    }
  }
};

ICONS["Effects/duotone"] = function (s) {
  s.vGradient(hex("#3a1a5e"), hex("#e8743c"));
  var dark = hex("#1c1030");
  s.circle(22, 22, 9, hex("#f4e8d0"), 0.95);
  s.tri(4, 58, 26, 30, 48, 58, dark, 0.95);
  s.tri(30, 58, 46, 38, 62, 58, dark, 0.85);
};

ICONS["Effects/glitch"] = function (s) {
  s.fill(hex("#0e1018"));
  var body = hex("#c8ccd4");
  var cyan = hex("#46c8e8");
  var slices = [
    { y: 13, h: 6, dx: 0 },
    { y: 19, h: 4, dx: -5 },
    { y: 23, h: 7, dx: 3 },
    { y: 30, h: 5, dx: -2 },
    { y: 35, h: 4, dx: 6 },
    { y: 39, h: 6, dx: -4 },
    { y: 45, h: 6, dx: 1 },
  ];
  for (var i = 0; i < slices.length; i++) {
    var sl = slices[i];
    var x = 16 + sl.dx;
    s.rect(x - 3, sl.y, 32, sl.h, RED, 0.55);
    s.rect(x + 3, sl.y, 32, sl.h, cyan, 0.55);
    s.rect(x, sl.y, 32, sl.h, body);
  }
  for (var y = 13; y < 51; y += 3) {
    s.rect(8, y, 48, 1, hex("#000000"), 0.18);
  }
};

ICONS["Effects/photo-frame"] = function (s) {
  background(s);
  s.rrect(16, 10, 36, 48, 2, hex("#000000"), 0.35);
  s.rrect(14, 8, 36, 48, 2, hex("#f4f4f0"));
  // The photo: sky, sun, dunes; the polaroid's wide bottom stays bare.
  s.rect(18, 12, 28, 30, hex("#6fb1d8"));
  s.circle(26, 20, 4, hex("#f4e8c0"));
  s.tri(16, 42, 34, 28, 50, 42, hex("#d8b878"));
  s.tri(14, 42, 22, 34, 38, 42, hex("#c8a058"));
};

ICONS["Utilities/watermark"] = function (s) {
  // A muted "photo" with the big transparent (c) stamped over its corner.
  s.vGradient(hex("#3a4556"), hex("#232c3a"));
  s.circle(18, 18, 6, hex("#8a96a8"), 0.5);
  s.tri(2, 52, 24, 32, 46, 52, hex("#2c3547"), 0.8);
  var mark = hex("#e6ecf4");
  s.ring(38, 38, 14, 3, mark, 0.75);
  s.ring(38, 38, 6.5, 2.6, mark, 0.75, Math.PI * 0.35, Math.PI * 1.65);
};

ICONS["Utilities/trim-to-content"] = function (s) {
  background(s);
  // The content worth keeping...
  s.rrect(24, 24, 16, 16, 1.5, TEAL);
  s.circle(29, 30, 2.2, hex("#f4e8c0"));
  s.tri(24, 40, 33, 31, 40, 40, hex("#2c8a78"));
  // ...and the crop marks closing in on it. dx/dy point INTO the content, so
  // the L-arms grow toward the anchor's inside corner.
  var mark = WHITE;
  function corner(x, y, dx, dy) {
    s.rect(dx > 0 ? x : x - 9, dy > 0 ? y : y - 2.5, 9, 2.5, mark);
    s.rect(dx > 0 ? x : x - 2.5, dy > 0 ? y : y - 9, 2.5, 9, mark);
  }
  corner(17, 17, 1, 1);
  corner(47, 17, -1, 1);
  corner(17, 47, 1, -1);
  corner(47, 47, -1, -1);
};

ICONS["Utilities/batch-export"] = function (s) {
  background(s);
  // A stack of photos...
  s.rrect(8, 12, 22, 17, 2, hex("#5a6a80"));
  s.rrect(11, 16, 22, 17, 2, hex("#8496ac"));
  s.rrect(14, 20, 22, 17, 2, hex("#eceee8"));
  s.rect(16, 22, 18, 9, hex("#6fb1d8"));
  s.circle(21, 25, 2, hex("#f4e8c0"));
  s.tri(15, 31, 24, 24, 34, 31, hex("#d8b878"));
  // ...heading out the door.
  s.arrowRight(38, 44, 20, 7, GREEN);
};

ICONS["Utilities/export-all-layers"] = function (s) {
  background(s);
  // The layer stack, panel-style...
  s.rrect(9, 10, 30, 11, 2, BLUE);
  s.rrect(13, 24, 30, 11, 2, GREEN);
  s.rrect(17, 38, 30, 11, 2, ORANGE);
  // ...each peeling off to its own file.
  s.arrowRight(42, 15.5, 15, 5, WHITE, 0.9);
  s.arrowRight(46, 29.5, 11, 5, WHITE, 0.9);
  s.arrowRight(50, 43.5, 7, 5, WHITE, 0.9);
};

ICONS["Utilities/data-merge"] = function (s) {
  background(s);
  // The CSV table...
  s.rrect(7, 14, 24, 28, 1.5, PAPER);
  s.rect(7, 14, 24, 7, hex("#3b6ea5"));
  var line = hex("#9aa4b2");
  s.rect(7, 27.5, 24, 1, line);
  s.rect(7, 34.5, 24, 1, line);
  s.rect(18.5, 21, 1, 21, line);
  // ...merging into a finished card.
  s.arrowRight(33, 28, 9, 5, ORANGE);
  s.rrect(44, 16, 14, 24, 2, hex("#f4f4f0"));
  s.rect(46, 20, 10, 3, hex("#3b6ea5"));
  s.rect(46, 27, 10, 1.6, line);
  s.rect(46, 31, 7, 1.6, line);
  s.rect(46, 35, 9, 1.6, line);
};

ICONS["Utilities/contact-sheet"] = function (s) {
  background(s);
  s.rrect(12, 7, 40, 50, 2, PAPER);
  var thumbs = [hex("#6fb1d8"), hex("#d8b878"), hex("#5ac85a"),
                hex("#e86fb1"), hex("#46c8b4"), hex("#e6a03c"),
                hex("#8a6fe8"), hex("#e65050"), hex("#3b6ea5")];
  for (var r = 0; r < 3; r++) {
    for (var c = 0; c < 3; c++) {
      var x = 16 + c * 12;
      var y = 11 + r * 15;
      s.rect(x, y, 10, 8, thumbs[r * 3 + c]);
      s.rect(x + 1, y + 9.5, 8, 1.2, hex("#b0b6ae"));  // caption line
    }
  }
};

ICONS["Utilities/icon-export"] = function (s) {
  background(s);
  // One artwork, every size: large to favicon.
  function app_icon(x, y, size, radius) {
    s.rrect(x, y, size, size, radius, BLUE);
    s.circle(x + size * 0.38, y + size * 0.38, size * 0.16, WHITE);
    s.tri(x + size * 0.15, y + size * 0.85, x + size * 0.55, y + size * 0.45,
          x + size * 0.85, y + size * 0.85, hex("#2c5a8a"));
  }
  app_icon(7, 7, 32, 6);
  app_icon(43, 21, 18, 3.5);
  app_icon(48, 45, 10, 2);
};

ICONS["Utilities/save-version"] = function (s) {
  background(s);
  // Older versions behind, the new save in front.
  s.rrect(22, 8, 30, 30, 2, hex("#33415a"));
  s.rrect(17, 13, 30, 30, 2, hex("#41537a"));
  var body = hex("#3b6ea5");
  s.rrect(11, 19, 32, 34, 2, body);
  s.rect(19, 19, 14, 10, hex("#dde2e8"));
  s.rect(27, 21, 4, 6, body);  // shutter slot
  s.rrect(16, 34, 22, 15, 1.5, hex("#f4f4f0"));
  s.rect(19, 38, 16, 1.6, hex("#9aa4b2"));
  s.rect(19, 42, 12, 1.6, hex("#9aa4b2"));
};

ICONS["Utilities/rename-layers"] = function (s) {
  background(s);
  // Two layer rows; the second is mid-rename with a caret...
  s.rrect(9, 14, 34, 12, 2, hex("#33415a"));
  s.rect(13, 19, 20, 2.2, hex("#8a939f"));
  s.rrect(9, 32, 34, 12, 2, hex("#33415a"));
  s.rect(13, 37, 15, 2.2, WHITE);
  s.rect(30.5, 34.5, 1.6, 7, BLUE);
  // ...under the pencil doing it.
  s.line(40, 50, 53, 24, 5, YELLOW);
  s.line(40, 50, 43.5, 43, 5, hex("#d8a868"));
  s.tri(38.2, 53.4, 42.7, 51.2, 39.8, 47.5, hex("#33415a"));
};

ICONS["Utilities/grid-maker"] = function (s) {
  s.fill(hex("#10141c"));
  var minor = BLUE;
  for (var i = 0; i <= 3; i++) {
    s.rect(8 + i * 16 - 0.75, 8, 1.5, 48, minor, 0.95);
    s.rect(8, 8 + i * 16 - 0.75, 48, 1.5, minor, 0.95);
  }
  for (var j = 0; j < 3; j++) {
    s.rect(16 + j * 16 - 0.5, 8, 1, 48, minor, 0.3);
    s.rect(8, 16 + j * 16 - 0.5, 48, 1, minor, 0.3);
  }
};

// ---------------------------------------------------------------------------
// Export.

function exportSurface(relative, surface) {
  roundCorners(surface.d);
  var doc = app.newDocument(SIZE, SIZE);
  doc.layers[0].setPixels({ x: 0, y: 0, width: SIZE, height: SIZE, data: surface.d.buffer });
  var path = OUT + "/" + relative + ".png";
  var ok = doc.exportAs(path);
  console.log((ok ? "wrote " : "FAILED ") + path);
  doc.close();
}

for (var key in ICONS) {
  var surface = new Surface();
  ICONS[key](surface);
  exportSurface(key, surface);
}

// Fancy Background's icon is the script's real output at 64x64 (same seed as
// its documentation example), with the shared rounded corners applied.
(function () {
  var doc = app.newDocument(SIZE, SIZE);
  include("Effects/fancy-background.js");
  // Showier options than the defaults: at 64px the subtle deep-blue ramp
  // reads as plain black, so brighten the gradient and glows.
  drawFancyBackground(doc.layers[0], SIZE, SIZE,
                      { top: "#3c5a8c", bottom: "#141b2e", glows: 4, vignette: 0.25,
                        noise: 0.06, seed: 3 });
  var img = doc.layers[0].getPixels();
  var data = new Uint8Array(img.data);
  roundCorners(data);
  doc.layers[0].setPixels({ x: 0, y: 0, width: SIZE, height: SIZE, data: data.buffer });
  var path = OUT + "/Effects/fancy-background.png";
  var ok = doc.exportAs(path);
  console.log((ok ? "wrote " : "FAILED ") + path);
  doc.close();
})();

console.log("done: " + (Object.keys(ICONS).length + 1) + " icons");

}
