// @name README shot: Layered imports
// @description Six actual layered imports, arranged as a labeled 3 x 2 artboard grid.
// @cli --script-arg out=layered_import.png --script-arg photo=city.jpg --script-arg pdn=demo.pdn --script-arg psd=mow_master.psd --script-arg affinity=tips.af
//
// PSD, Affinity and PDN use existing showcase/calibration fixtures. ORA, layered
// TIFF and Aseprite sources are authored below in the driver's scratch directory,
// saved in their real formats, then read back through Files as Layers. No source
// fixture is overwritten. Presentation labels and the introduction are added after import.

var out = patchy.args.out;
var photo = patchy.args.photo;
var pdn = patchy.args.pdn;
var psd = patchy.args.psd;
var affinity = patchy.args.affinity;
if (!out || !photo || !pdn || !psd || !affinity) {
  throw new Error('pass out, photo, pdn, psd and affinity script arguments');
}
patchy.ui.setWindowSize(1600, 1000);
patchy.ui.setSidePanelWidth(320);

function newSource(width, height, color) {
  var source = app.newDocument(width, height);
  app.documents.forEach(function (other) {
    if (other.id !== source.id && !other.path && !other.modified) { other.close(); }
  });
  if (color) { source.activeLayer.fill(color); }
  else { source.layers[0].remove(); }
  return source;
}
function saveSource(source, suffix, options) {
  var path = out + suffix;
  if (!source.saveAs(path, options || {})) { throw new Error('source save failed: ' + suffix); }
  source.close();
  return path;
}

// OpenRaster: a photographic poster, with a nested lettering group and opacity.
var source = newSource(640, 400);
source.addSmartObject(photo, {name: 'City photograph', width: 640, x: 0, y: -14});
var shade = source.addShape('Title backdrop',
  {type: 'rectangle', x: 0, y: 236, width: 640, height: 164}, {fill: '#122a3c'});
shade.opacity = 82;
var title = source.addTextLayer('CITY STUDY', {font: 'Arial Black', size: 62,
  color: '#ffffff', x: 28, y: 258, box: {width: 590, height: 92}});
var subtitle = source.addTextLayer('SAN FRANCISCO  /  FIELD NOTES 01', {font: 'Segoe UI', size: 19,
  color: '#f4cd70', x: 32, y: 352, box: {width: 570, height: 38}});
source.groupLayers([shade, title, subtitle], 'Poster lettering');
var ora = saveSource(source, '.ora');

// Layered TIFF: live vector geometry and typography in an abstract print design.
source = newSource(640, 400, '#f3e5cb');
source.activeLayer.name = 'Cream paper';
source.addShape('Orange disc', {type: 'ellipse', x: 244, y: 34, width: 350, height: 350}, {fill: '#e76a32'});
source.addShape('Teal disc', {type: 'ellipse', x: 340, y: 126, width: 250, height: 250}, {fill: '#07555e'});
source.addShape('Paper aperture', {type: 'ellipse', x: 406, y: 192, width: 118, height: 118}, {fill: '#f3e5cb'});
source.addTextLayer('FORM\n& COLOR', {font: 'Arial Black', size: 63, color: '#17333d',
  x: 34, y: 70, box: {width: 425, height: 184}});
source.addTextLayer('STUDIO SERIES     /     03', {font: 'Segoe UI', bold: true, size: 20,
  color: '#17333d', x: 38, y: 338, box: {width: 540, height: 36}});
var tiff = saveSource(source, '.tif', {layers: true});

// Aseprite: authored at sprite resolution, with separate stars, planet and ship.
source = newSource(64, 40, '#171a39');
source.activeLayer.name = 'Deep space';
var stars = source.addLayer('Stars');
stars.fillRect(0, 0, 64, 40, '#00000000');
[[4,5],[12,18],[19,3],[27,13],[38,4],[47,31],[57,12],[60,35],[7,32],[30,35]].forEach(function (p) {
  stars.fillRect(p[0], p[1], 1, 1, '#b5c9e5');
});
var planet = source.addLayer('Ringed planet');
planet.fillRect(0, 0, 64, 40, '#00000000');
for (var y = -9; y <= 9; y++) {
  var span = Math.floor(Math.sqrt(81 - y * y));
  planet.fillRect(47 - span, 12 + y, span * 2 + 1, 1, y < -2 ? '#f7c37c' : '#e78d68');
}
for (var r = 0; r < 3; r++) {
  planet.fillRect(32 + r * 2, 17 + r, 30 - r * 3, 1, '#a8d7d0');
}
var ship = source.addLayer('Scout ship');
ship.fillRect(0, 0, 64, 40, '#00000000');
var sprite = [
  '.......aa.......',
  '......abba......',
  '......abba......',
  '.....abccba.....',
  '.....abccba.....',
  '..aaabbbbbbaaa..',
  '.abbbbbbbbbbbba.',
  'abbbabbbbbbabbba',
  'aaaaabbbbbbaaaaa',
  '.....adddda.....',
  '.....aee.eea....',
  '......ef.fe.....',
  '......ff.ff.....'
];
var colors = {a:'#344c6b', b:'#e4eee5', c:'#56bcc7', d:'#df6b62', e:'#f5b563', f:'#f9df97'};
sprite.forEach(function (row, rowIndex) {
  for (var x = 0; x < row.length; x++) {
    if (colors[row[x]]) { ship.fillRect(12 + x, 19 + rowIndex, 1, 1, colors[row[x]]); }
  }
});
var aseprite = saveSource(source, '.aseprite');

var formats = [
  {name: 'PSD / PSB', detail: 'Layered Photoshop artwork', path: psd, color: '#a9d5f5'},
  {name: 'Affinity', detail: '.af / .afphoto / .afdesign / .afpub', path: affinity, color: '#d2b6ed'},
  {name: 'OpenRaster', detail: '.ora  /  pixel layers and folders', path: ora, color: '#95d6c3'},
  {name: 'Paint.NET', detail: '.pdn  /  pixel layers and blend modes', path: pdn, color: '#99b9f2'},
  {name: 'Layered TIFF', detail: '.tif  /  text, shapes and pixel layers', path: tiff, color: '#f3c386'},
  {name: 'Aseprite', detail: '.aseprite  /  layered pixel art', path: aseprite, color: '#e4a8b8', pixelArt: true}
];
// Measure the actual files, including the generated formats, without resaving them.
function referenceLeaf(layers, path) {
  for (var i = 0; i < layers.length; i++) {
    var layer = layers[i];
    var childPath = path.concat([i]);
    if (layer.isGroup) {
      var found = referenceLeaf(layer.children, childPath);
      if (found) { return found; }
    } else if (layer.bounds.width > 0 && layer.bounds.height > 0) {
      return {path: childPath, bounds: layer.bounds};
    }
  }
  return null;
}
formats.forEach(function (format) {
  var original = app.open(format.path);
  format.width = original.width;
  format.height = original.height;
  format.reference = referenceLeaf(original.layers, []);
  if (!format.reference) { throw new Error(format.name + ': missing source bounds'); }
  original.close();
});

var headerHeight = 256;
var doc = app.newDocument(2048, 1024 + headerHeight);
var anchor = doc.activeLayer;
anchor.visible = false;
var boards = [];
function unlockForLayout(layer) {
  layer.locked = false;
  layer.children.forEach(unlockForLayout);
}
formats.forEach(function (format, index) {
  doc.activate();
  doc.activeLayer = anchor; // Always import at the root, outside previous artboards.
  var roots = doc.importFilesAsLayers(format.path);
  var group = roots[0];
  if (roots.length !== 1 || !group.isGroup || group.children.length < 2) {
    throw new Error(format.name + ': expected a genuine layered import');
  }
  // The presentation rescales and moves the imports, including locked backgrounds.
  // Unlock this copy's layers; the original source files stay untouched.
  unlockForLayout(group);
  if (format.path === pdn) {
    // Hide the calibration overlay, retaining the imported layer in the stack.
    group.children.forEach(function (layer) {
      if (layer.name === 'Xor checks') { layer.visible = false; }
    });
  }
  var x = (index % 3) * 704;
  var y = headerHeight + Math.floor(index / 3) * 544;
  var scale = Math.min(640 / format.width, 400 / format.height);
  var left = x + (640 - format.width * scale) / 2;
  var top = y + 80 + (400 - format.height * scale) / 2;
  // Files as Layers centers the content extent, which can extend past the source
  // canvas. Track one corresponding leaf to recover the actual import offset.
  var reference = group;
  format.reference.path.forEach(function (i) { reference = reference.children[i]; });
  var dx = reference.bounds.x - format.reference.bounds.x;
  var dy = reference.bounds.y - format.reference.bounds.y;
  group.transform({matrix: [scale, 0, 0, scale,
    left - scale * dx, top - scale * dy],
    interpolation: format.pixelArt ? 'nearest' : 'bicubic'});
  // Preserve the source canvas crop, including files with artwork off-canvas.
  var right = left + format.width * scale;
  var bottom = top + format.height * scale;
  group.setVectorMask({path: {subpaths: [{anchors: [
    {x: left, y: top}, {x: right, y: top}, {x: right, y: bottom}, {x: left, y: bottom}
  ]}]}});
  group.name = 'Imported ' + format.name;
  group.blendMode = 'normal';
  var board = doc.addArtboard(format.name, {x: x, y: y, width: 640, height: 480},
    {background: 'color', color: '#f3ede2'});
  doc.moveLayers([group], {parentId: board.id});
  boards.push(board);
  console.log(format.name + ': imported ' + group.children.length + ' children from ' + format.width + ' x ' + format.height);
  var band = doc.addShape('Format label background',
    {type: 'rectangle', x: x, y: y, width: 640, height: 80}, {fill: '#172f40'});
  var label = doc.addTextLayer(format.name, {font: 'Arial Black', size: 34,
    color: format.color, x: x + 24, y: y + 8, box: {width: 590, height: 46}});
  var detail = doc.addTextLayer(format.detail, {font: 'Segoe UI', size: 18,
    color: '#ffffff', x: x + 25, y: y + 52, box: {width: 590, height: 27}});
  doc.moveLayers([band, label, detail], {parentId: board.id});
});
// Loose document layers above the six artboards form the introduction. These
// are real editable text and shapes, not an overlay added to the screenshot.
doc.activeLayer = anchor;
var accent = doc.addShape('Introduction accent',
  {type: 'rectangle', x: 0, y: 0, width: 112, height: 6}, {fill: '#95d6c3'});
var eyebrow = doc.addTextLayer('LAYERED IMPORTS', {font: 'Segoe UI', bold: true, size: 24,
  color: '#a8bbc8', x: 0, y: 20, box: {width: 1400, height: 36}});
var headline = doc.addTextLayer([
  {text: 'Why settle for only ', color: '#f3ede2'},
  {text: '.psd?', color: '#95d6c3'}
], {font: 'Arial Black', size: 80, x: 0, y: 60, box: {width: 2048, height: 110}});
var explanation = doc.addTextLayer('Import all these layered formats into the same file!',
  {font: 'Segoe UI', size: 34, color: '#d4dee5', x: 3, y: 178,
    box: {width: 2040, height: 55}});
var badge = doc.addShape('Format count badge',
  {type: 'roundedRectangle', x: 1660, y: 20, width: 388, height: 56, radius: 28}, {fill: '#163d44'});
var badgeText = doc.addTextLayer('6 FORMATS  /  1 FILE', {font: 'Segoe UI', bold: true, size: 26,
  color: '#95d6c3', x: 1660, y: 29, align: 'center', box: {width: 388, height: 42}});
doc.groupLayers([accent, eyebrow, headline, explanation, badge, badgeText], 'Showcase introduction');
anchor.remove();
if (doc.artboards.length !== 6 || doc.layers.length !== 7) {
  throw new Error('expected six top-level artboards and the introduction group');
}
doc.fitCanvasToArtboards();
doc.activeLayer = boards[5];
app.runCommand('tools.artboard');
['color', 'palette', 'properties', 'info', 'history'].forEach(function (panel) {
  patchy.ui.closePanel(panel);
});
setTimeout(function () {
  patchy.ui.fitOnScreen();
  setTimeout(function () {
    doc.activeLayer = boards[5];
    patchy.ui.setStatusMessage('Six formats, six artboards');
    if (!patchy.ui.captureWindow(out)) { throw new Error('captureWindow failed: ' + out); }
    console.log('captured six imported formats in a 3 x 2 artboard grid: ' + out);
  }, 400);
}, 400);
