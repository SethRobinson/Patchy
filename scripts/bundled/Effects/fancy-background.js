// @name Fancy Background
// @description Fills a new layer with an atmospheric backdrop: a two-tone
// @description gradient, soft color glows, a gentle vignette, and faint dust
// @description specks. Also a library other scripts include() for their own
// @description backgrounds.
// @author Seth A. Robinson
//
// Library AND standalone script: defines drawFancyBackground(layer, width,
// height, options). Other scripts use it with
//   include("Effects/fancy-background.js");
//   drawFancyBackground(doc.layers[0], doc.width, doc.height, {seed: 7});
// (Breakout does exactly that for its playfield.) Run directly, it renders
// onto a new "Fancy Background" layer of the active document;
// patchy.isMainScript() is what tells the two modes apart. It writes in row
// strips so big canvases paint progressively instead of freezing the app.
//
// drawFancyBackground options (all optional):
//   top, bottom  gradient colors, "#rrggbb" (default deep blue-gray ramp)
//   vignette     corner darkening 0..1 (default 0.35)
//   noise        dust speck probability per pixel 0..1 (default 0.045)
//   glows        number of soft color glows (default 3)
//   seed         integer; the same seed always draws the same background

// ---------------------------------------------------------------------------
// Options - defaults for the STANDALONE run (the dialog and --script-arg
// key=value override them; sliders are percentages of the ranges above).
var OPTIONS = {
  top: "#161c30",     // gradient top color
  bottom: "#0a0d17",  // gradient bottom color
  glows: 3,           // 0..8 soft color glows
  vignette: 35,       // 0..100 corner darkening
  noise: 4,           // 0..20 dust speck amount
  seed: 0             // 0 = random each run; any other number repeats
};
// ---------------------------------------------------------------------------

function drawFancyBackground(layer, width, height, options) {
  options = options || {};

  function hexToRgb(text, fallback) {
    var m = /^#?([0-9a-f]{6})$/i.exec(String(text || ""));
    if (!m) { return fallback; }
    var n = parseInt(m[1], 16);
    return { r: (n >> 16) & 255, g: (n >> 8) & 255, b: n & 255 };
  }

  var top = hexToRgb(options.top, { r: 0x16, g: 0x1c, b: 0x30 });
  var bottom = hexToRgb(options.bottom, { r: 0x0a, g: 0x0d, b: 0x17 });
  var vignette = options.vignette === undefined ? 0.35 : Number(options.vignette);
  var noise = options.noise === undefined ? 0.045 : Number(options.noise);
  var glowCount = options.glows === undefined ? 3 : options.glows | 0;

  // Small deterministic PRNG: the same seed always yields the same picture.
  var state = (options.seed === undefined ? 123456789 : options.seed) >>> 0;
  function rand() {
    state = (state * 1664525 + 1013904223) >>> 0;
    return state / 4294967296;
  }

  // Soft glows in gentle blue/violet/teal hues, kept dim on purpose.
  var glowHues = [
    { r: 70, g: 90, b: 160 },
    { r: 110, g: 70, b: 150 },
    { r: 60, g: 120, b: 130 }
  ];
  var glows = [];
  for (var gi = 0; gi < glowCount; gi++) {
    glows.push({
      x: width * (0.15 + 0.7 * rand()),
      y: height * (0.15 + 0.7 * rand()),
      radius: Math.max(width, height) * (0.25 + 0.35 * rand()),
      color: glowHues[gi % glowHues.length],
      strength: 0.05 + 0.05 * rand()
    });
  }

  // Render top-down and push the whole buffer to the layer a few times along
  // the way (setPixels REPLACES a layer's pixels, so partial-rect strips
  // would not merge): on a big canvas the background visibly paints in and
  // each write pumps the app's busy indicator instead of one long freeze.
  var bytes = new Uint8Array(width * height * 4);
  var cx = width / 2;
  var cy = height / 2;
  var cornerDist = Math.sqrt(cx * cx + cy * cy) || 1;
  var CHUNKS = 5;
  var chunkRows = Math.max(1, Math.ceil(height / CHUNKS));
  var i = 0;
  for (var y = 0; y < height; y++) {
    var t = height <= 1 ? 0 : y / (height - 1);
    var baseR = top.r + (bottom.r - top.r) * t;
    var baseG = top.g + (bottom.g - top.g) * t;
    var baseB = top.b + (bottom.b - top.b) * t;
    for (var x = 0; x < width; x++) {
      var r = baseR;
      var g = baseG;
      var b = baseB;
      for (var k = 0; k < glows.length; k++) {
        var glow = glows[k];
        var gx = (x - glow.x) / glow.radius;
        var gy = (y - glow.y) / glow.radius;
        var falloff = Math.exp(-(gx * gx + gy * gy) * 2.5) * glow.strength;
        r += glow.color.r * falloff;
        g += glow.color.g * falloff;
        b += glow.color.b * falloff;
      }
      var dx = (x - cx) / cornerDist;
      var dy = (y - cy) / cornerDist;
      var dim = 1 - vignette * (dx * dx + dy * dy) * 2;
      r *= dim;
      g *= dim;
      b *= dim;
      if (rand() < noise) {
        var lift = 6 + rand() * 14;  // faint dust, slightly cool
        r += lift;
        g += lift;
        b += lift * 1.2;
      }
      bytes[i++] = r < 0 ? 0 : (r > 255 ? 255 : Math.round(r));
      bytes[i++] = g < 0 ? 0 : (g > 255 ? 255 : Math.round(g));
      bytes[i++] = b < 0 ? 0 : (b > 255 ? 255 : Math.round(b));
      bytes[i++] = 255;
    }
    if ((y + 1) % chunkRows === 0 && y + 1 < height) {
      layer.setPixels({ x: 0, y: 0, width: width, height: height, data: bytes.buffer });
    }
  }
  layer.setPixels({ x: 0, y: 0, width: width, height: height, data: bytes.buffer });
}

if (patchy.isMainScript()) {
  var doc = app.activeDocument;
  if (!doc) {
    app.alert("Open a document first.");
  } else {
    var choice = patchy.ui.showOptions({
      title: "Fancy Background",
      description: "Renders an atmospheric backdrop onto a new \"Fancy Background\" layer of " +
                   "this document: a vertical gradient between the two colors, soft glows, a " +
                   "corner vignette, and faint dust.\n\n" +
                   "Seed 0 places the glows differently every run; any other seed repeats the " +
                   "same background.",
      fields: [
        { key: "top", label: "Top color", type: "color", value: OPTIONS.top },
        { key: "bottom", label: "Bottom color", type: "color", value: OPTIONS.bottom },
        { key: "glows", label: "Color glows", type: "number", value: OPTIONS.glows,
          min: 0, max: 8 },
        { key: "vignette", label: "Vignette", type: "slider", value: OPTIONS.vignette,
          min: 0, max: 100 },
        { key: "noise", label: "Dust", type: "slider", value: OPTIONS.noise, min: 0, max: 20 },
        { key: "seed", label: "Seed (0 = random)", type: "number", value: OPTIONS.seed,
          min: 0, max: 999999999 }
      ]
    });
    if (choice) {
      var layer = doc.addLayer("Fancy Background");
      drawFancyBackground(layer, doc.width, doc.height, {
        top: choice.top,
        bottom: choice.bottom,
        glows: choice.glows,
        vignette: choice.vignette / 100,
        noise: choice.noise / 100,
        seed: choice.seed > 0 ? choice.seed : Math.floor(Math.random() * 4294967296)
      });
      console.log("Fancy background rendered onto the \"Fancy Background\" layer.");
    }
  }
}
