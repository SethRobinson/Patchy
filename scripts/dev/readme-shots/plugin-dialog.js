// @name README shot: plug-in dialog
// @description Runs a third-party Photoshop .8bf plug-in and captures its own settings
// @description dialog for docs/images/screenshots/plugin_dialog.png. Dev tooling, never staged.
// @cli --script-arg out=plugin_dialog.png --script-arg plugins=local-test-fixtures/photoshop-plugins/mehdi --script-arg photo=test-fixtures/readme/san_francisco_cityscape_cc0.jpg --script-arg name=Absolute Color
//
// Windows only, and manual: the plug-in (Mehdi's Absolute Color, see agents_local.md)
// is not committed, so this scene is not in scripts\make-readme-screenshots.ps1's table.
// Run it from a real (not offscreen) session, for example:
//
//   set PATCHY_NO_SINGLE_INSTANCE=1
//   build\release\patchy.exe --run-script scripts\dev\readme-shots\plugin-dialog.js ^
//     --script-arg out=docs\images\screenshots\plugin_dialog.png ^
//     --script-arg plugins=local-test-fixtures\photoshop-plugins\mehdi ^
//     --script-arg photo=test-fixtures\readme\san_francisco_cityscape_cc0.jpg
//
// The plug-in's dialog opens on screen for about a second (it is captured through
// layer.applyPlugin's captureDialog option, then accepted); the document is not saved.

var out = patchy.args.out;
var plugins = patchy.args.plugins;
var photo = patchy.args.photo;
// Which plug-in to shoot (a prefix of its menu name); any bitness.
var wantName = patchy.args.name || 'Absolute Color';
if (!out) { throw new Error('pass --script-arg out=<output png path>'); }
if (!plugins) { throw new Error('pass --script-arg plugins=<folder holding the .8bf files>'); }

var doc = photo ? app.open(photo) : app.newDocument(1024, 768);
if (!doc) { throw new Error('open failed: ' + photo); }
if (!photo) {
  doc.activeLayer.fillRect(0, 0, 1024, 768, '#4a7fc0');
}
doc.activate();
// A camera-sized photo previews as a blurry 8% thumbnail; a screen-sized one
// shows detail in the plug-in's preview box.
if (doc.width > 1400) {
  doc.resizeImage(1400, Math.round(doc.height * 1400 / doc.width));
}

// The plug-in previews the active layer: pick the largest pixel layer so the
// preview shows a real picture rather than a sliver.
function largestPixelLayer(layers, best) {
  for (var i = 0; i < layers.length; i++) {
    var layer = layers[i];
    if (layer.isGroup) {
      best = largestPixelLayer(layer.children, best);
      continue;
    }
    if (layer.isText || !layer.bounds) { continue; }
    var area = layer.bounds.width * layer.bounds.height;
    if (!best || area > best.area) { best = { layer: layer, area: area }; }
  }
  return best;
}
var best = largestPixelLayer(doc.layers, null);
if (best) { doc.activeLayer = best.layer; }

var previous = patchy.plugins.folders;
patchy.plugins.folders = [plugins];
var list = patchy.plugins.list();
var id = null;
for (var i = 0; i < list.length; i++) {
  if (list[i].supported && list[i].name.replace(/\s+/g, ' ').indexOf(wantName) >= 0) {
    id = list[i].id;
    break;
  }
}
if (!id) { throw new Error(wantName + ' not found under ' + plugins); }

// dialog: true so plug-ins that build their UI in the Parameters selector
// (KPT) show it; this unattended run captures the dialog and then answers it.
doc.activeLayer.applyPlugin(id, { dialog: true, captureDialog: out });
console.log('captured ' + id + ' to ' + out);
patchy.plugins.folders = previous;
