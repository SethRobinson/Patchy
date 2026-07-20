// @name Generative Art
// @description Paints a colorful flow-field artwork onto a new layer: glowing
// @description particle trails wander a smooth vector field. Re-run (or change
// @description the seed) for endless variations.
// @author Seth A. Robinson
//
// The trails draw in batches so the canvas shows the artwork building up and
// the app stays responsive (each batch write pumps the busy indicator).

// ---------------------------------------------------------------------------
// Options - defaults for this script. The options dialog (GUI runs) and
// --script-arg key=value (command line) override them.
var OPTIONS = {
  palette: "Aurora",     // Aurora, Sunset, Ocean, Neon, Mono
  density: 55,           // 1..100 - how many particle trails
  glow: 60,              // 1..100 - how bright the trails burn
  background: "#0d1020", // canvas behind the trails
  seed: 0                // 0 = a fresh drawing every run; any other number repeats
};
// ---------------------------------------------------------------------------

var PALETTES = {
  "Aurora": [[90, 160, 255], [120, 255, 170], [190, 120, 255], [255, 210, 100]],
  "Sunset": [[255, 120, 90], [255, 180, 70], [255, 90, 140], [255, 220, 150]],
  "Ocean": [[70, 200, 230], [90, 140, 255], [120, 230, 190], [210, 240, 255]],
  "Neon": [[255, 60, 180], [60, 255, 140], [80, 160, 255], [255, 240, 80]],
  "Mono": [[230, 235, 245], [170, 180, 200], [120, 130, 150], [250, 250, 255]]
};

var doc = app.activeDocument;
if (!doc) {
  app.alert("Open a document first.");
} else {
  var options = patchy.ui.showOptions({
    title: "Generative Art",
    description: "Paints a flow-field artwork onto a new layer of this document: particles " +
                 "wander a smooth vector field, leaving glowing trails.\n\n" +
                 "Density fills the canvas with more trails, Glow makes them burn brighter. " +
                 "Seed 0 draws something new every run; any other seed repeats the same " +
                 "composition.",
    fields: [
      { key: "palette", label: "Palette", type: "choice", value: OPTIONS.palette,
        choices: ["Aurora", "Sunset", "Ocean", "Neon", "Mono"] },
      { key: "density", label: "Density", type: "slider", value: OPTIONS.density,
        min: 1, max: 100 },
      { key: "glow", label: "Glow", type: "slider", value: OPTIONS.glow, min: 1, max: 100 },
      { key: "background", label: "Background", type: "color", value: OPTIONS.background },
      { key: "seed", label: "Seed (0 = random)", type: "number", value: OPTIONS.seed,
        min: 0, max: 999999999 }
    ]
  });
  if (options) {
    var W = doc.width;
    var H = doc.height;
    var layer = doc.addLayer("Flow Field");

    // Deterministic PRNG so a non-zero seed always draws the same picture.
    var state = (options.seed > 0 ? options.seed : Math.floor(Math.random() * 4294967296)) >>> 0;
    var usedSeed = state;
    function rand() {
      state = (state * 1664525 + 1013904223) >>> 0;
      return state / 4294967296;
    }

    var bgMatch = /^#?([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(options.background);
    var bg = bgMatch
        ? [parseInt(bgMatch[1], 16), parseInt(bgMatch[2], 16), parseInt(bgMatch[3], 16)]
        : [13, 16, 32];
    var pixels = new Uint8Array(W * H * 4);
    for (var i = 0; i < W * H; i++) {
      pixels[i * 4] = bg[0];
      pixels[i * 4 + 1] = bg[1];
      pixels[i * 4 + 2] = bg[2];
      pixels[i * 4 + 3] = 255;
    }

    var seedA = rand() * 10;
    var seedB = rand() * 10;
    var palette = PALETTES[options.palette] || PALETTES["Aurora"];

    function field(x, y) {
      var nx = x / W * 4;
      var ny = y / H * 4;
      return Math.sin(nx + seedA + Math.cos(ny * 1.3 + seedB)) +
             Math.cos(ny * 1.7 + seedA * 0.7 + Math.sin(nx * 0.9));
    }

    // Additive deposit with a soft halo on the 4 neighbors, so trails glow
    // instead of scratching thin dark lines.
    function deposit(x, y, color, strength) {
      var xi = x | 0;
      var yi = y | 0;
      if (xi < 1 || yi < 1 || xi >= W - 1 || yi >= H - 1) { return; }
      var offset = (yi * W + xi) * 4;
      var halo = strength * 0.35;
      var stride = W * 4;
      pixels[offset] = Math.min(255, pixels[offset] + color[0] * strength);
      pixels[offset + 1] = Math.min(255, pixels[offset + 1] + color[1] * strength);
      pixels[offset + 2] = Math.min(255, pixels[offset + 2] + color[2] * strength);
      var sides = [offset - 4, offset + 4, offset - stride, offset + stride];
      for (var n = 0; n < 4; n++) {
        var at = sides[n];
        pixels[at] = Math.min(255, pixels[at] + color[0] * halo);
        pixels[at + 1] = Math.min(255, pixels[at + 1] + color[1] * halo);
        pixels[at + 2] = Math.min(255, pixels[at + 2] + color[2] * halo);
      }
    }

    // Scale the trail count with the canvas AND the density option; respawn
    // particles that drift off the edge so every trail spends its full life
    // painting.
    var PARTICLES = Math.max(120,
        Math.min(9000, Math.round(W * H / 440 * options.density / 55)));
    var STEPS = 300;
    var strength = 0.05 + options.glow / 100 * 0.25;
    var BATCHES = 8;
    var perBatch = Math.ceil(PARTICLES / BATCHES);
    console.log("Tracing " + PARTICLES + " trails (" + options.palette + ", seed " + usedSeed +
                ")...");
    var traced = 0;
    for (var b = 0; b < BATCHES; b++) {
      for (var p = 0; p < perBatch && traced < PARTICLES; p++, traced++) {
        var x = rand() * W;
        var y = rand() * H;
        var color = palette[(rand() * palette.length) | 0];
        for (var s = 0; s < STEPS; s++) {
          var angle = field(x, y) * Math.PI;
          x += Math.cos(angle) * 1.6;
          y += Math.sin(angle) * 1.6;
          if (x < 1 || y < 1 || x >= W - 1 || y >= H - 1) {
            x = rand() * W;
            y = rand() * H;
            continue;
          }
          deposit(x, y, color, strength);
        }
      }
      // Progressive write: the artwork appears batch by batch.
      layer.setPixels({ x: 0, y: 0, width: W, height: H, data: pixels.buffer });
    }
    console.log("Done. Re-run for a different composition, or set a seed to keep this one.");
  }
}
