// @name README shot: Artboards
// @description Lays out a fictional car-weekend campaign as four artboards (story, banner,
// @description post, icon) in one document, one per background type, and captures the main
// @description window with the Artboard tool on the post and the Properties panel open.
// @cli --script-arg out=artboards.png --script-arg photo=test-fixtures/readme/havana_vintage_car_cc0.jpg
//
// The photo is placed larger than each artboard, so every artboard visibly clips it. The
// icon artboard is transparent: the checkerboard shows only inside it. The driver supplies
// isolated settings and 96-DPI geometry on the real windows platform.

var out = patchy.args.out;
var photo = patchy.args.photo;
if (!out) { throw new Error('pass --script-arg out=<output png path>'); }
if (!photo) { throw new Error('pass --script-arg photo=<Havana photo path>'); }

var TEAL = '#0e5e6f';
var CORAL = '#e8553d';
var CREAM = '#f3e6cf';
var INK = '#1b1b1b';
var HEADLINE = 'Arial Black';
var BODY = 'Segoe UI';

patchy.ui.setWindowSize(1600, 1000);
var doc = app.newDocument(1080, 1920);
app.documents.forEach(function (other) {
  if (other.id !== doc.id && !other.path && !other.modified) { other.close(); }
});
doc.activate();
patchy.ui.setSidePanelWidth(300);

// Story on the left; banner across the top right; post and icon below the banner.
var story = doc.addArtboard('Story', {x: 0, y: 0, width: 1080, height: 1920}, {background: 'white'});
var banner = doc.addArtboard('Banner', {x: 1180, y: 0, width: 1920, height: 640}, {background: 'black'});
var icon = doc.addArtboard('Icon', {x: 2360, y: 740, width: 512, height: 512}, {background: 'transparent'});
var post = doc.addArtboard('Post', {x: 1180, y: 740, width: 1080, height: 1080}, {background: 'color', color: CREAM});
doc.fitCanvasToArtboards();
// The new document's own layer sits outside every artboard; drop it.
doc.layers.forEach(function (layer) {
  if (!layer.isArtboard) { layer.remove(); }
});
if (doc.artboards.length !== 4) { throw new Error('expected 4 artboards, got ' + doc.artboards.length); }

function into(artboard, layer) {
  doc.moveLayers([layer], {parentId: artboard.id});
  return layer;
}
function rectPath(x, y, w, h) {
  return {subpaths: [{anchors: [{x: x, y: y}, {x: x + w, y: y}, {x: x + w, y: y + h}, {x: x, y: y + h}]}]};
}
// The photo is 3000 x 1975; the car's center sits near (1820, 1650).
function placePhoto(artboard, name, width, carX, carY) {
  var scale = width / 3000;
  var layer = doc.addSmartObject(photo, {name: name, width: width,
    x: Math.round(carX - 1820 * scale), y: Math.round(carY - 1650 * scale)});
  return into(artboard, layer);
}
function text(artboard, value, options) {
  return into(artboard, doc.addTextLayer(value, options));
}
function shape(artboard, name, geometry, appearance) {
  return into(artboard, doc.addShape(name, geometry, appearance));
}

// Story: the car fills the top, the pitch sits on white below it.
var storyPhoto = placePhoto(story, 'Malecon photo', 2400, 540, 1080);
storyPhoto.setVectorMask({path: rectPath(0, 0, 1080, 1300)});
text(story, 'HAVANA\nCLASSIC CARS', {font: HEADLINE, size: 112, color: INK,
  x: 72, y: 1350, box: {width: 940, height: 300}});
text(story, 'Weekend rally on the Malecon  /  May 14-16', {font: BODY, size: 40, color: TEAL,
  x: 72, y: 1670, box: {width: 940, height: 60}});
shape(story, 'Tickets button', {type: 'roundedRectangle', x: 72, y: 1760, width: 380, height: 92, radius: 46},
  {fill: CORAL});
text(story, 'Get tickets', {font: BODY, bold: true, size: 40, color: '#ffffff', align: 'center',
  x: 72, y: 1780, box: {width: 380, height: 60}});

// Banner: photo on the left half, the line on black at the right.
var bannerPhoto = placePhoto(banner, 'Malecon photo', 1800, 1180 + 480, 460);
bannerPhoto.setVectorMask({path: rectPath(1180, 0, 980, 640)});
text(banner, 'HAVANA\nCLASSIC CARS', {font: HEADLINE, size: 96, color: '#ffffff',
  x: 2230, y: 150, box: {width: 820, height: 260}});
text(banner, 'May 14-16  /  Malecon', {font: BODY, size: 40, color: CREAM,
  x: 2230, y: 400, box: {width: 820, height: 60}});
shape(banner, 'Arrow button', {type: 'ellipse', x: 2930, y: 470, width: 110, height: 110}, {fill: CORAL});
shape(banner, 'Arrow', {type: 'polygon', cx: 2990, cy: 525, radius: 28, sides: 3, angle: 0}, {fill: '#ffffff'});

// Post: framed photo on cream, a short line under it.
var postPhoto = placePhoto(post, 'Malecon photo', 2700, 1720, 1170);
postPhoto.setVectorMask({path: rectPath(1260, 820, 920, 640)});
text(post, 'Weekend rally', {font: HEADLINE, size: 96, color: TEAL,
  x: 1260, y: 1500, box: {width: 920, height: 130}});
text(post, '120 classic cars along the Malecon, May 14-16', {font: BODY, size: 38, color: INK,
  x: 1260, y: 1650, box: {width: 920, height: 110}});

// Icon: a vector badge on transparency.
shape(icon, 'Badge', {type: 'ellipse', x: 2400, y: 780, width: 432, height: 432},
  {fill: TEAL, stroke: {enabled: true, width: 18, paint: CREAM, alignment: 'inside'}});
text(icon, 'HC', {font: HEADLINE, size: 170, color: CREAM, align: 'center',
  x: 2400, y: 900, box: {width: 432, height: 220}});

// Select the post: its child first, so the Layers panel expands it, then the artboard itself,
// so the Artboard tool draws its handles and Properties shows its rect and background.
doc.activeLayer = postPhoto;
doc.activeLayer = post;
app.runCommand('tools.artboard');
patchy.ui.expandPanel('properties');

setTimeout(function () {
  patchy.ui.fitOnScreen();
  setTimeout(function () {
    patchy.ui.setStatusMessage('Ready');
    if (!patchy.ui.captureWindow(out)) {
      throw new Error('captureWindow failed: ' + out);
    }
    console.log('captured ' + out);
  }, 250);
}, 250);
