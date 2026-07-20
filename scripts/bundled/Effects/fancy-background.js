// @name Fancy Background
// Library AND standalone script: defines drawFancyBackground(layer, width,
// height, options), which fills a layer with a subtle two-tone vertical
// gradient, a couple of soft color glows, a gentle corner vignette, and sparse
// faint dust specks - atmosphere that stays out of the way. Other scripts use
// it with
//   include("Effects/fancy-background.js");
//   drawFancyBackground(doc.layers[0], doc.width, doc.height, {seed: 7});
// (Breakout does exactly that for its playfield.) Run directly from the menu
// or editor, it renders onto a new "Fancy Background" layer of the active
// document (like every non-game bundled script, it works on the open image);
// patchy.isMainScript() is what tells the two modes apart.
//
// Options (all optional):
//   top, bottom  gradient colors, "#rrggbb" (default deep blue-gray ramp)
//   vignette     corner darkening 0..1 (default 0.35)
//   noise        dust speck probability per pixel 0..1 (default 0.045)
//   glows        number of soft color glows (default 3)
//   seed         integer; the same seed always draws the same background

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

  var bytes = new Uint8Array(width * height * 4);
  var cx = width / 2;
  var cy = height / 2;
  var cornerDist = Math.sqrt(cx * cx + cy * cy) || 1;
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
  }
  layer.setPixels({ x: 0, y: 0, width: width, height: height, data: bytes.buffer });
}

if (patchy.isMainScript()) {
  var doc = app.activeDocument;
  if (!doc) {
    app.alert("Open a document first.");
  } else {
    var layer = doc.addLayer("Fancy Background");
    drawFancyBackground(layer, doc.width, doc.height, {});
    console.log("Fancy background rendered onto the \"Fancy Background\" layer.");
  }
}
