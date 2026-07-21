// @name README shot: Affinity import
// @description Stages the tips.af Affinity import and captures the main window
// @description for docs/images/screenshots/affinity_import.png. Dev tooling, never staged.
// @cli --script-arg out=affinity_import.png --script-arg af=local-test-fixtures/af-spike/corpus/tips.af
//
// Run through scripts\make-readme-screenshots.ps1 (which pins DPI, isolates
// settings via PATCHY_SETTINGS_DIR, and sets PATCHY_NO_SINGLE_INSTANCE=1 so a
// fresh unattended instance runs the scene with the real windows platform and
// every installed font). The window appears briefly; the capture never raises
// or focuses it.

var out = patchy.args.out;
var af = patchy.args.af;
if (!out) { throw new Error('pass --script-arg out=<output png path>'); }
if (!af) { throw new Error('pass --script-arg af=<tips.af path>'); }

patchy.ui.setWindowSize(1600, 1000);

var doc = app.open(af);
if (!doc) { throw new Error('open failed: ' + af); }

// Close the empty start tab so only the imported document shows.
var docs = app.documents;
for (var i = 0; i < docs.length; i++) {
  if (docs[i].path === '' && docs[i].name.indexOf('Untitled') === 0) {
    docs[i].close();
  }
}
doc.activate();
patchy.ui.setSidePanelWidth(380);

// Stage the layers panel: reveal a foldered text layer first so the
// group-to-folder story is visible, then select the topmost text layer (the
// document title). Layers come bottom-to-top, so the last hit is the topmost.
function collectTextHits(layers, depth, hits) {
  for (var i = 0; i < layers.length; i++) {
    var layer = layers[i];
    if (layer.isGroup) {
      collectTextHits(layer.children, depth + 1, hits);
      continue;
    }
    if (layer.isText) {
      hits.push({ layer: layer, depth: depth });
    }
  }
}
var hits = [];
collectTextHits(doc.layers, 0, hits);
if (hits.length === 0) { throw new Error('no text layers found in ' + af); }
for (var i = hits.length - 1; i >= 0; i--) {
  if (hits[i].depth >= 1) {
    doc.activeLayer = hits[i].layer;
    break;
  }
}
doc.activeLayer = hits[hits.length - 1].layer;

// Fit and capture on timer turns so the resize/dock layout settles between
// steps (posted layout events run between bursts, not within one).
setTimeout(function () {
  app.runCommand('view.fit_on_screen');
  setTimeout(function () {
    patchy.ui.setStatusMessage('Ready');
    if (!patchy.ui.captureWindow(out)) {
      throw new Error('captureWindow failed: ' + out);
    }
    console.log('captured ' + out);
  }, 250);
}, 250);
