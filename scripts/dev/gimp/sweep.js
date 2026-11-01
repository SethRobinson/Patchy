// Runs every GIMP 3 procedure Patchy offers, non-interactively, on a test layer
// (docs/gimp-plugins.md, development tools). Development only, never staged:
//
//   patchy --headless --run-script scripts/dev/gimp/sweep.js --script-output sweep.txt
//          [--script-arg only=plug-in-blinds,plug-in-sparkle] [--script-arg depth=16]
//          [--script-arg dialogs=<folder>]   (Windows: interactive, each dialog saved and answered OK)
//
// One JSON line per procedure: {procedure, result, ms, changed, error}. `result` is
// "applied" (pixels changed inside the selection only), "leaked" (pixels outside the
// selection changed too), "unchanged", "failed" or "refused".

var only = patchy.args.only ? String(patchy.args.only).split(",") : null;
var depth = patchy.args.depth ? Number(patchy.args.depth) : 8;
var dialogs = patchy.args.dialogs ? String(patchy.args.dialogs) : "";
var list = patchy.gimpPlugins.list();
var install = patchy.gimpPlugins.install();
console.log(JSON.stringify({gimp: install.version, folder: install.folder, procedures: list.length}));

function pattern(layer) {
  for (var y = 0; y < 192; y += 16) {
    for (var x = 0; x < 256; x += 16) {
      var r = (x * 255 / 256) | 0, g = (y * 255 / 192) | 0, b = ((x ^ y) & 255);
      var hex = "#" + [r, g, b].map(function (v) { return ("0" + v.toString(16)).slice(-2); }).join("");
      layer.fillRect(x, y, 16, 16, hex);
    }
  }
}

for (var i = 0; i < list.length; ++i) {
  var p = list[i];
  if (p.hidden) continue;
  if (only && only.indexOf(p.procedure) < 0) continue;
  if (!p.supported) {
    console.log(JSON.stringify({procedure: p.procedure, result: "refused", reason: p.reasonCode, menu: p.menu}));
    continue;
  }
  var doc = app.newDocument(256, 192, {name: p.procedure});
  var layer = doc.activeLayer;
  pattern(layer);
  if (depth !== 8) doc.convertBitDepth(depth);
  doc.selection.selectRect(32, 24, 160, 120);
  var before = new Uint8Array(layer.getPixels().data);
  var started = Date.now();
  var entry = {procedure: p.procedure, menu: p.menu, name: p.name};
  try {
    var options = {timeoutMs: 60000};
    if (dialogs) {
      options = {dialog: true, captureDialog: dialogs + "/" + p.procedure + ".png", answerDialog: "ok", timeoutMs: 90000};
    }
    layer.applyGimpPlugin(p.id, options);
    var after = new Uint8Array(layer.getPixels().data);
    var inside = 0, outside = 0;
    for (var k = 0; k < Math.min(before.length, after.length); k += 4) {
      var px = (k / 4) % 256, py = ((k / 4) / 256) | 0;
      var differs = before[k] !== after[k] || before[k + 1] !== after[k + 1] || before[k + 2] !== after[k + 2] || before[k + 3] !== after[k + 3];
      if (!differs) continue;
      if (px >= 32 && px < 192 && py >= 24 && py < 144) ++inside; else ++outside;
    }
    entry.changed = inside;
    entry.outside = outside;
    entry.result = outside > 0 ? "leaked" : inside > 0 ? "applied" : "unchanged";
  } catch (e) {
    entry.result = "failed";
    entry.error = String(e.message || e).slice(0, 600);
  }
  entry.ms = Date.now() - started;
  console.log(JSON.stringify(entry));
  doc.close();
}
console.log(JSON.stringify({done: true}));
