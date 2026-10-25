// @name README shot: Layered imports
// @description Imports an OpenRaster poster and a Paint.NET landscape as real layer groups.
// @cli --script-arg out=layered_import.png --script-arg photo=test-fixtures/readme/san_francisco_cityscape_cc0.jpg --script-arg pdn=test-fixtures/pdn/demo.pdn
//
// The ORA source is generated in the driver's scratch folder, then read back through
// Files as Layers. The PDN fixture was saved by Paint.NET; neither import is flattened.
// Text and shapes in the ORA source correctly become pixel layers on export.

var out = patchy.args.out;
var photo = patchy.args.photo;
var pdn = patchy.args.pdn;
if (!out || !photo || !pdn) { throw new Error('pass out, photo and pdn script arguments'); }

patchy.ui.setWindowSize(1600, 1000);
patchy.ui.setSidePanelWidth(330);
var source = app.newDocument(640, 400);
app.documents.forEach(function (other) {
  if (other.id !== source.id && !other.path && !other.modified) { other.close(); }
});
source.layers[0].remove();
source.addSmartObject(photo, {name: 'City photograph', width: 640, x: 0, y: -14});
var shade = source.addShape('Title backdrop',
  {type: 'rectangle', x: 0, y: 236, width: 640, height: 164}, {fill: '#122a3c'});
shade.opacity = 82;
var title = source.addTextLayer('CITY STUDY', {font: 'Arial Black', size: 62,
  color: '#ffffff', x: 28, y: 258, box: {width: 590, height: 92}});
var subtitle = source.addTextLayer('SAN FRANCISCO  /  FIELD NOTES 01', {font: 'Segoe UI', size: 19,
  color: '#f4cd70', x: 32, y: 352, box: {width: 570, height: 38}});
source.groupLayers([shade, title, subtitle], 'Poster lettering');
var ora = out + '.ora';
if (!source.saveAs(ora)) { throw new Error('could not write the OpenRaster source'); }
source.close();

var doc = app.newDocument(1392, 656);
doc.activeLayer.name = 'Paper';
doc.activeLayer.fill('#f2ede4');
var header = doc.addTextLayer('TWO FORMATS. REAL LAYERS.', {font: 'Arial Black', size: 44,
  color: '#172f40', x: 40, y: 24, box: {width: 1310, height: 70}});
var intro = doc.addTextLayer('OpenRaster and Paint.NET artwork, together in Patchy', {font: 'Segoe UI', size: 24,
  color: '#4f626c', x: 42, y: 94, box: {width: 1290, height: 40}});
var oraLabel = doc.addTextLayer('OPENRASTER  .ora', {font: 'Segoe UI', bold: true, size: 27,
  color: '#172f40', x: 40, y: 578, box: {width: 640, height: 48}});
var pdnLabel = doc.addTextLayer('PAINT.NET  .pdn', {font: 'Segoe UI', bold: true, size: 27,
  color: '#172f40', x: 712, y: 578, box: {width: 640, height: 48}});
doc.groupLayers([header, intro, oraLabel, pdnLabel], 'Showcase captions');
var imported = doc.importFilesAsLayers([ora, pdn]);
if (imported.length !== 2 || !imported[0].isGroup || !imported[1].isGroup) {
  throw new Error('expected two imported layer groups');
}
// The PDN is a calibration fixture. Hide its checker-pattern test overlay for
// this composition, keeping that imported layer available in the stack.
imported[1].children.forEach(function (layer) {
  if (layer.name === 'Xor checks') { layer.visible = false; }
});
imported.forEach(function (group, index) {
  if (group.children.length < 2) { throw new Error('import unexpectedly flattened'); }
  // Both source canvases are 640 x 400. Files as Layers centers them in this
  // 1392 x 656 document; a group's own bounds are empty, not its child bounds.
  group.moveTo((index ? 712 : 40) - 376, 160 - 128);
  group.name = index ? 'Paint.NET (.pdn)' : 'OpenRaster (.ora)';
  // Keep each source's blend stack independent of the presentation's paper.
  group.blendMode = 'normal';
  console.log(group.name + ': ' + group.children.length + ' imported children');
  // Selecting a child expands the real imported group in the Layers panel.
  doc.activeLayer = group.children[0];
});
doc.activeLayer = imported[1];
app.runCommand('tools.hand');
['color', 'palette', 'properties', 'info', 'history'].forEach(function (panel) {
  patchy.ui.closePanel(panel);
});
setTimeout(function () {
  patchy.ui.fitOnScreen();
  setTimeout(function () {
    patchy.ui.setStatusMessage('Ready');
    if (!patchy.ui.captureWindow(out)) { throw new Error('captureWindow failed: ' + out); }
    console.log('captured ' + out);
  }, 250);
}, 250);
