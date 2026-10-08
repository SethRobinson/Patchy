// Deep fixture builder (docs/high-bit-depth.md, Phase 0). Run through
// make_deep_fixtures.py, which prepends the SCENE, DEPTH and OUT_* variables and runs
// one scene at one depth per DoJavaScript call. ExtendScript is ES3: no JSON, no let.
//
// Every scene is built directly at its depth (a 16-bit gradient carries 16-bit
// precision; a document built at 8 bits and converted would not). The script saves
// the layered PSD and Photoshop's own renders of it: render.png (8-bit sRGB) always,
// render16.png (16-bit sRGB) for 16-bit documents, and render32.tif (uncompressed
// 32-bit float, linear) for 32-bit documents. It returns a JSON line.

var W = 128, H = 64;

function cTID(s) { return charIDToTypeID(s); }
function sTID(s) { return stringIDToTypeID(s); }
function q(s) {
  s = String(s);
  var out = '"';
  for (var i = 0; i < s.length; i++) {
    var c = s.charAt(i), code = s.charCodeAt(i);
    if (c == '"' || c == '\\') { out += '\\' + c; }
    else if (code < 32) { out += ' '; }
    else { out += c; }
  }
  return out + '"';
}

// Scene colors are written as sRGB 0..255. A 32-bit document takes descriptor and
// SolidColor values as LINEAR light on the same 0..255 scale (250 is stored as
// 0.98), so they are linearized there, keeping the picture the same at every depth.
function channel(v) {
  if (DEPTH != 32) { return v; }
  var x = v / 255;
  return 255 * (x <= 0.04045 ? x / 12.92 : Math.pow((x + 0.055) / 1.055, 2.4));
}

function rgbDesc(r, g, b) {
  var c = new ActionDescriptor();
  c.putDouble(cTID('Rd  '), channel(r));
  c.putDouble(cTID('Grn '), channel(g));
  c.putDouble(cTID('Bl  '), channel(b));
  return c;
}

function solid(r, g, b) {
  var c = new SolidColor();
  c.rgb.red = channel(r); c.rgb.green = channel(g); c.rgb.blue = channel(b);
  return c;
}

// A gradient descriptor: stops = [[r, g, b, location 0..4096], ...], alpha stops
// = [[opacity percent, location], ...] (default fully opaque). Smooth (Intr 4096).
function gradientDesc(stops, alphas) {
  var g = new ActionDescriptor();
  g.putString(cTID('Nm  '), 'deep');
  g.putEnumerated(cTID('GrdF'), cTID('GrdF'), cTID('CstS'));
  g.putDouble(cTID('Intr'), 4096);
  var colors = new ActionList();
  for (var i = 0; i < stops.length; i++) {
    var s = new ActionDescriptor();
    s.putObject(cTID('Clr '), cTID('RGBC'), rgbDesc(stops[i][0], stops[i][1], stops[i][2]));
    s.putEnumerated(cTID('Type'), cTID('Clry'), cTID('UsrS'));
    s.putInteger(cTID('Lctn'), stops[i][3]);
    s.putInteger(cTID('Mdpn'), 50);
    colors.putObject(cTID('Clrt'), s);
  }
  g.putList(cTID('Clrs'), colors);
  alphas = alphas || [[100, 0], [100, 4096]];
  var trans = new ActionList();
  for (var j = 0; j < alphas.length; j++) {
    var t = new ActionDescriptor();
    t.putUnitDouble(cTID('Opct'), cTID('#Prc'), alphas[j][0]);
    t.putInteger(cTID('Lctn'), alphas[j][1]);
    t.putInteger(cTID('Mdpn'), 50);
    trans.putObject(cTID('TrnS'), t);
  }
  g.putList(cTID('Trns'), trans);
  return g;
}

function point(x, y) {
  var p = new ActionDescriptor();
  p.putUnitDouble(cTID('Hrzn'), cTID('#Pxl'), x);
  p.putUnitDouble(cTID('Vrtc'), cTID('#Pxl'), y);
  return p;
}

// Draws a linear gradient with the Gradient tool into the active layer (or the
// active channel, e.g. a selected layer mask), without dither.
function drawGradient(x1, y1, x2, y2, stops, alphas) {
  var d = new ActionDescriptor();
  d.putObject(cTID('From'), cTID('Pnt '), point(x1, y1));
  d.putObject(cTID('T   '), cTID('Pnt '), point(x2, y2));
  d.putEnumerated(cTID('Type'), cTID('GrdT'), cTID('Lnr '));
  d.putBoolean(cTID('Dthr'), false);
  d.putBoolean(cTID('UsMs'), true);
  d.putObject(cTID('Grad'), cTID('Grdn'), gradientDesc(stops, alphas));
  executeAction(cTID('Grdn'), d, DialogModes.NO);
}

function newLayer(doc, name) {
  var layer = doc.artLayers.add();
  layer.name = name;
  doc.activeLayer = layer;
  return layer;
}

// The shared base: an opaque warm-to-cool horizontal ramp on the background.
function baseRamp(doc) {
  doc.activeLayer = doc.backgroundLayer;
  drawGradient(0, 0, W, 0, [[250, 40, 10, 0], [20, 200, 90, 2048], [30, 60, 240, 4096]]);
}

// The shared top: a vertical ramp, so every blend pair across the canvas is sampled.
function topRamp(doc, name, alphas) {
  var layer = newLayer(doc, name);
  drawGradient(0, 0, 0, H, [[255, 255, 255, 0], [128, 128, 128, 2048], [0, 0, 0, 4096]], alphas);
  return layer;
}

function addAdjustment(typeClass, settings) {
  var d = new ActionDescriptor();
  var r = new ActionReference();
  r.putClass(cTID('AdjL'));
  d.putReference(cTID('null'), r);
  var u = new ActionDescriptor();
  if (settings === null) { u.putClass(cTID('Type'), typeClass); }
  else { u.putObject(cTID('Type'), typeClass, settings); }
  d.putObject(cTID('Usng'), cTID('AdjL'), u);
  executeAction(cTID('Mk  '), d, DialogModes.NO);
}

function compositeChannelRef() {
  var r = new ActionReference();
  r.putEnumerated(cTID('Chnl'), cTID('Chnl'), cTID('Cmps'));
  return r;
}

function addContentLayer(typeKey, settings) {
  var d = new ActionDescriptor();
  var r = new ActionReference();
  r.putClass(sTID('contentLayer'));
  d.putReference(cTID('null'), r);
  var u = new ActionDescriptor();
  u.putObject(cTID('Type'), typeKey, settings);
  d.putObject(cTID('Usng'), sTID('contentLayer'), u);
  executeAction(cTID('Mk  '), d, DialogModes.NO);
}

// Adds a layer mask revealing all, selects it, and draws a horizontal black-to-white ramp.
function gradientMask() {
  var d = new ActionDescriptor();
  d.putClass(cTID('Nw  '), cTID('Chnl'));
  var at = new ActionReference();
  at.putEnumerated(cTID('Chnl'), cTID('Chnl'), cTID('Msk '));
  d.putReference(cTID('At  '), at);
  d.putEnumerated(cTID('Usng'), cTID('UsrM'), cTID('RvlA'));
  executeAction(cTID('Mk  '), d, DialogModes.NO);
  drawGradient(0, 0, W, 0, [[0, 0, 0, 0], [255, 255, 255, 4096]]);
}

var BLEND_MODES = {
  normal: 'NORMAL', dissolve: 'DISSOLVE', darken: 'DARKEN', multiply: 'MULTIPLY',
  colorburn: 'COLORBURN', linearburn: 'LINEARBURN', darkercolor: 'DARKERCOLOR',
  lighten: 'LIGHTEN', screen: 'SCREEN', colordodge: 'COLORDODGE', lineardodge: 'LINEARDODGE',
  lightercolor: 'LIGHTERCOLOR', overlay: 'OVERLAY', softlight: 'SOFTLIGHT',
  hardlight: 'HARDLIGHT', vividlight: 'VIVIDLIGHT', linearlight: 'LINEARLIGHT',
  pinlight: 'PINLIGHT', hardmix: 'HARDMIX', difference: 'DIFFERENCE', exclusion: 'EXCLUSION',
  subtract: 'SUBTRACT', divide: 'DIVIDE', hue: 'HUE', saturation: 'SATURATION',
  color: 'COLORBLEND', luminosity: 'LUMINOSITY'
};

function buildScene(doc, scene) {
  baseRamp(doc);
  if (scene == 'base') { return; }
  if (scene.indexOf('blend-') == 0) {
    var mode = BLEND_MODES[scene.substring(6)];
    var top = topRamp(doc, 'Top');
    top.blendMode = BlendMode[mode];
    return;
  }
  if (scene == 'opacity') {
    var t1 = topRamp(doc, 'Top');
    t1.blendMode = BlendMode.MULTIPLY;
    t1.opacity = 50;
    t1.fillOpacity = 70;
    return;
  }
  if (scene == 'alpha-ramp') {
    topRamp(doc, 'Top', [[0, 0], [100, 4096]]);
    return;
  }
  if (scene == 'mask') {
    topRamp(doc, 'Top');
    gradientMask();
    return;
  }
  if (scene == 'clip') {
    var shape = newLayer(doc, 'Shape');
    doc.selection.select([[16, 8], [112, 8], [112, 56], [16, 56]]);
    doc.selection.fill(solid(30, 30, 30));
    doc.selection.deselect();
    var clipped = topRamp(doc, 'Clipped');
    clipped.grouped = true;
    clipped.blendMode = BlendMode.LIGHTEN;  // available at every depth (Screen is not in 32-bit)
    return;
  }
  if (scene == 'group-passthrough') {
    var set = doc.layerSets.add();
    set.name = 'Group';
    var a = topRamp(doc, 'A');
    a.move(set, ElementPlacement.INSIDE);
    a.blendMode = BlendMode.MULTIPLY;
    set.opacity = 60;
    return;
  }
  if (scene == 'adj-levels') {
    var lv = new ActionDescriptor();
    lv.putEnumerated(sTID('presetKind'), sTID('presetKindType'), sTID('presetKindCustom'));
    var adjs = new ActionList();
    var ch = new ActionDescriptor();
    ch.putReference(cTID('Chnl'), compositeChannelRef());
    var inp = new ActionList(); inp.putInteger(20); inp.putInteger(235);
    ch.putList(cTID('Inpt'), inp);
    ch.putDouble(cTID('Gmm '), 1.6);
    adjs.putObject(cTID('LvlA'), ch);
    lv.putList(cTID('Adjs'), adjs);
    addAdjustment(cTID('Lvls'), lv);
    return;
  }
  if (scene == 'adj-curves') {
    var cv = new ActionDescriptor();
    cv.putEnumerated(sTID('presetKind'), sTID('presetKindType'), sTID('presetKindCustom'));
    var cadjs = new ActionList();
    var cch = new ActionDescriptor();
    cch.putReference(cTID('Chnl'), compositeChannelRef());
    var pts = new ActionList();
    var p0 = new ActionDescriptor(); p0.putDouble(cTID('Hrzn'), 0); p0.putDouble(cTID('Vrtc'), 0);
    var p1 = new ActionDescriptor(); p1.putDouble(cTID('Hrzn'), 96); p1.putDouble(cTID('Vrtc'), 150);
    var p2 = new ActionDescriptor(); p2.putDouble(cTID('Hrzn'), 255); p2.putDouble(cTID('Vrtc'), 255);
    pts.putObject(cTID('Pnt '), p0); pts.putObject(cTID('Pnt '), p1); pts.putObject(cTID('Pnt '), p2);
    cch.putList(cTID('Crv '), pts);
    cadjs.putObject(cTID('CrvA'), cch);
    cv.putList(cTID('Adjs'), cadjs);
    addAdjustment(cTID('Crvs'), cv);
    return;
  }
  if (scene == 'adj-huesat') {
    var hs = new ActionDescriptor();
    hs.putEnumerated(sTID('presetKind'), sTID('presetKindType'), sTID('presetKindCustom'));
    hs.putBoolean(cTID('Clrz'), false);
    var hadjs = new ActionList();
    var h = new ActionDescriptor();
    h.putInteger(cTID('H   '), 30);
    h.putInteger(cTID('Strt'), 20);
    h.putInteger(cTID('Lght'), -10);
    hadjs.putObject(cTID('Hst2'), h);
    hs.putList(cTID('Adjs'), hadjs);
    addAdjustment(cTID('HStr'), hs);
    return;
  }
  if (scene == 'adj-brightness') {
    var bc = new ActionDescriptor();
    bc.putInteger(cTID('Brgh'), 25);
    bc.putInteger(cTID('Cntr'), 30);
    bc.putBoolean(sTID('useLegacy'), false);
    addAdjustment(cTID('BrgC'), bc);
    return;
  }
  if (scene == 'adj-exposure') {
    var ex = new ActionDescriptor();
    ex.putEnumerated(sTID('presetKind'), sTID('presetKindType'), sTID('presetKindCustom'));
    ex.putDouble(sTID('exposure'), 0.6);
    ex.putDouble(sTID('offset'), -0.02);
    ex.putDouble(sTID('gammaCorrection'), 1.2);
    addAdjustment(sTID('exposure'), ex);
    return;
  }
  if (scene == 'adj-invert') {
    addAdjustment(cTID('Invr'), null);
    return;
  }
  if (scene == 'adj-posterize') {
    var ps = new ActionDescriptor();
    ps.putInteger(cTID('Lvls'), 6);
    addAdjustment(cTID('Pstr'), ps);
    return;
  }
  if (scene == 'adj-threshold') {
    var th = new ActionDescriptor();
    th.putInteger(cTID('Lvl '), 128);
    addAdjustment(cTID('Thrs'), th);
    return;
  }
  if (scene == 'fill-solid') {
    var sc = new ActionDescriptor();
    sc.putObject(cTID('Clr '), cTID('RGBC'), rgbDesc(40, 120, 220));
    addContentLayer(sTID('solidColorLayer'), sc);
    doc.activeLayer.blendMode = BlendMode.MULTIPLY;
    doc.activeLayer.opacity = 60;
    return;
  }
  if (scene == 'fill-gradient') {
    var gl = new ActionDescriptor();
    gl.putUnitDouble(cTID('Angl'), cTID('#Ang'), 90);
    gl.putEnumerated(cTID('Type'), cTID('GrdT'), cTID('Lnr '));
    gl.putObject(cTID('Grad'), cTID('Grdn'),
                 gradientDesc([[255, 220, 0, 0], [120, 0, 160, 4096]], [[100, 0], [30, 4096]]));
    addContentLayer(sTID('gradientLayer'), gl);
    return;
  }
  if (scene == 'smart-object') {
    topRamp(doc, 'Smart');
    executeAction(sTID('newPlacedLayer'), new ActionDescriptor(), DialogModes.NO);
    doc.activeLayer.resize(75, 75, AnchorPosition.MIDDLECENTER);
    return;
  }
  if (scene == 'effects') {
    var fx = newLayer(doc, 'Effects');
    doc.selection.select([[32, 16], [96, 16], [96, 48], [32, 48]]);
    doc.selection.fill(solid(240, 240, 240));
    doc.selection.deselect();
    var style = new ActionDescriptor();
    style.putUnitDouble(cTID('Scl '), cTID('#Prc'), 100);
    var shadow = new ActionDescriptor();
    shadow.putBoolean(cTID('enab'), true);
    shadow.putEnumerated(cTID('Md  '), cTID('BlnM'), cTID('Mltp'));
    shadow.putObject(cTID('Clr '), cTID('RGBC'), rgbDesc(0, 0, 0));
    shadow.putUnitDouble(cTID('Opct'), cTID('#Prc'), 75);
    shadow.putBoolean(cTID('uglg'), false);
    shadow.putUnitDouble(cTID('lagl'), cTID('#Ang'), 120);
    shadow.putUnitDouble(cTID('Dstn'), cTID('#Pxl'), 5);
    shadow.putUnitDouble(cTID('Ckmt'), cTID('#Pxl'), 0);
    shadow.putUnitDouble(cTID('blur'), cTID('#Pxl'), 6);
    style.putObject(cTID('DrSh'), cTID('DrSh'), shadow);
    var stroke = new ActionDescriptor();
    stroke.putBoolean(cTID('enab'), true);
    stroke.putEnumerated(cTID('Styl'), cTID('FStl'), cTID('OutF'));
    stroke.putEnumerated(cTID('PntT'), cTID('FrFl'), cTID('SClr'));
    stroke.putEnumerated(cTID('Md  '), cTID('BlnM'), cTID('Nrml'));
    stroke.putUnitDouble(cTID('Opct'), cTID('#Prc'), 100);
    stroke.putUnitDouble(cTID('Sz  '), cTID('#Pxl'), 3);
    stroke.putObject(cTID('Clr '), cTID('RGBC'), rgbDesc(200, 30, 30));
    style.putObject(cTID('FrFX'), cTID('FrFX'), stroke);
    var set2 = new ActionDescriptor();
    var target = new ActionReference();
    target.putProperty(cTID('Prpr'), cTID('Lefx'));
    target.putEnumerated(cTID('Lyr '), cTID('Ordn'), cTID('Trgt'));
    set2.putReference(cTID('null'), target);
    set2.putObject(cTID('T   '), cTID('Lefx'), style);
    executeAction(cTID('setd'), set2, DialogModes.NO);
    return;
  }
  if (scene == 'channel') {
    topRamp(doc, 'Top');
    var alpha = doc.channels.add();
    alpha.name = 'Alpha 1';
    alpha.kind = ChannelType.MASKEDAREA;
    doc.activeChannels = [alpha];
    drawGradient(0, 0, W, H, [[0, 0, 0, 0], [255, 255, 255, 4096]]);
    doc.activeChannels = doc.componentChannels;
    return;
  }
  throw new Error('unknown scene ' + scene);
}

function depthEnum(depth) {
  if (depth == 16) { return BitsPerChannelType.SIXTEEN; }
  if (depth == 32) { return BitsPerChannelType.THIRTYTWO; }
  return BitsPerChannelType.EIGHT;
}

function pngOptions() {
  var o = new PNGSaveOptions();
  o.compression = 6;
  o.interlaced = false;
  return o;
}

// Photoshop's own flatten (never the embedded composite, which a scripted save
// can leave stale or white; docs/photoshop-com.md).
function saveRenders(doc, depth) {
  var notes = [];
  var dup = doc.duplicate();
  try {
    dup.flatten();
    if (depth == 32) {
      var tif = new TiffSaveOptions();
      tif.imageCompression = TIFFEncoding.NONE;
      tif.byteOrder = ByteOrder.IBM;
      tif.alphaChannels = false;
      tif.layers = false;
      tif.embedColorProfile = false;
      dup.saveAs(new File(OUT_RENDER32), tif, true, Extension.LOWERCASE);
      notes.push('render32');
    }
    if (depth != 32) {
      dup.convertProfile('sRGB IEC61966-2.1', Intent.RELATIVECOLORIMETRIC, true, false);
    }
    if (depth == 16) {
      dup.saveAs(new File(OUT_RENDER16), pngOptions(), true, Extension.LOWERCASE);
      notes.push('render16');
    }
    if (depth != 8) { dup.bitsPerChannel = BitsPerChannelType.EIGHT; }
    dup.saveAs(new File(OUT_RENDER8), pngOptions(), true, Extension.LOWERCASE);
    notes.push('render8');
  } finally {
    dup.close(SaveOptions.DONOTSAVECHANGES);
  }
  return notes;
}

(function () {
  app.displayDialogs = DialogModes.NO;
  var oldUnits = app.preferences.rulerUnits;
  app.preferences.rulerUnits = Units.PIXELS;
  var doc = null;
  try {
    doc = app.documents.add(W, H, 72, 'deep-' + SCENE + '-' + DEPTH, NewDocumentMode.RGB,
                            DocumentFill.WHITE, 1, depthEnum(DEPTH), 'sRGB IEC61966-2.1');
    buildScene(doc, SCENE);
    var psd = new PhotoshopSaveOptions();
    psd.layers = true;
    psd.maximizeCompatibility = true;
    psd.embedColorProfile = true;
    doc.saveAs(new File(OUT_PSD), psd, true, Extension.LOWERCASE);
    var notes = saveRenders(doc, DEPTH);
    var layers = doc.layers.length;
    doc.close(SaveOptions.DONOTSAVECHANGES);
    doc = null;
    return '{"ok":true,"layers":' + layers + ',"renders":' + q(notes.join(',')) + '}';
  } catch (e) {
    try { if (doc !== null) { doc.close(SaveOptions.DONOTSAVECHANGES); } } catch (e2) {}
    return '{"ok":false,"error":' + q(e + (e.line ? ' (line ' + e.line + ')' : '')) + '}';
  } finally {
    app.preferences.rulerUnits = oldUnits;
  }
})();
