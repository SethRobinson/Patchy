// Generative Art
// Paints a flow-field artwork into a new layer: thousands of particles wander
// through a smooth sine-based vector field, leaving glowing additive trails.
// Every run is different. A nice starting point for creative coding.

var W = 800;
var H = 500;
var doc = app.newDocument(W, H);
var layer = doc.addLayer("Flow Field");

var pixels = new Uint8Array(W * H * 4);
for (var i = 0; i < W * H; i++) {
  pixels[i * 4] = 10;
  pixels[i * 4 + 1] = 10;
  pixels[i * 4 + 2] = 18;
  pixels[i * 4 + 3] = 255;
}

var seedA = Math.random() * 10;
var seedB = Math.random() * 10;
var palette = [
  [90, 160, 255],
  [255, 120, 90],
  [120, 255, 170],
  [255, 210, 100]
];

function field(x, y) {
  var nx = x / W * 4;
  var ny = y / H * 4;
  return Math.sin(nx + seedA + Math.cos(ny * 1.3 + seedB)) +
         Math.cos(ny * 1.7 + seedA * 0.7 + Math.sin(nx * 0.9));
}

function deposit(x, y, color, strength) {
  var xi = x | 0;
  var yi = y | 0;
  if (xi < 0 || yi < 0 || xi >= W || yi >= H) { return; }
  var offset = (yi * W + xi) * 4;
  pixels[offset] = Math.min(255, pixels[offset] + color[0] * strength);
  pixels[offset + 1] = Math.min(255, pixels[offset + 1] + color[1] * strength);
  pixels[offset + 2] = Math.min(255, pixels[offset + 2] + color[2] * strength);
}

var PARTICLES = 900;
var STEPS = 260;
console.log("Tracing " + PARTICLES + " particles...");
for (var p = 0; p < PARTICLES; p++) {
  var x = Math.random() * W;
  var y = Math.random() * H;
  var color = palette[(Math.random() * palette.length) | 0];
  for (var s = 0; s < STEPS; s++) {
    var angle = field(x, y) * Math.PI;
    x += Math.cos(angle) * 1.6;
    y += Math.sin(angle) * 1.6;
    if (x < 0 || y < 0 || x >= W || y >= H) { break; }
    deposit(x, y, color, 0.055);
  }
}

layer.setPixels({ x: 0, y: 0, width: W, height: H, data: pixels.buffer });
console.log("Done. Re-run for a different composition.");
