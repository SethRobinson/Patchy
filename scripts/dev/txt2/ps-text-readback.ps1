# Reads back, through Photoshop, what each type layer of the given files says and how Photoshop
# lays it out afresh (docs/txt2.md): per file <name>.json with every type layer's anchor (the
# text click point in pixels), transform, anti-aliasing, kerning per style range, old-text flag,
# textWarningLevel, box first-baseline rule and its bounds before and after a re-layout, and
# <name>-relaid.png, the flattened document after Photoshop laid every type layer out again
# (the layer's own textClickPoint set alone, which changes no paragraph or character setting:
# docs/txt2-kept-settings.md, "Line composer"). Compare an original with a Patchy save of it
# with ps-text-readback-compare.py. Files are opened and closed without saving; the open
# documents before and after go into readback.log. Groups are walked.
#   powershell -Command "& scripts\dev\txt2\ps-text-readback.ps1 -Files a.psd, b.psd -OutDir <dir> [-Suffix -save]"
param([Parameter(Mandatory = $true)][string[]]$Files, [Parameter(Mandatory = $true)][string]$OutDir,
      [string]$Suffix = '')
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $OutDir | Out-Null
$list = ($Files | ForEach-Object { '"' + $_.Replace([char]92, [char]47) + '"' }) -join ', '
$outJs = $OutDir.Replace([char]92, [char]47)
$app = New-Object -ComObject Photoshop.Application
$jsx = @"
app.displayDialogs = DialogModes.NO;
try { app.preferences.rulerUnits = Units.PIXELS; } catch (e) {}
var FILES = [$list];
var OUT = "$outJs";
var SUFFIX = "$Suffix";
var log = [];
function tid(s) { return stringIDToTypeID(s); }
function keyName(t) { var s = ''; try { s = typeIDToStringID(t); } catch (e) {} if (s === '') { try { s = typeIDToCharID(t); } catch (e2) { s = String(t); } } return s; }
function openNames() { var n = []; for (var i = 0; i < app.documents.length; i++) { n.push(app.documents[i].name); } return n.join(' | '); }
function quote(s) {
  var out = '"';
  s = String(s);
  for (var i = 0; i < s.length; i++) {
    var c = s.charCodeAt(i);
    if (c == 34 || c == 92) { out += String.fromCharCode(92) + s.charAt(i); }
    else if (c < 32 || c > 126) { var h = c.toString(16); while (h.length < 4) { h = '0' + h; } out += String.fromCharCode(92) + 'u' + h; }
    else { out += s.charAt(i); }
  }
  return out + '"';
}
function layerDesc(id) { var ref = new ActionReference(); ref.putIdentifier(charIDToTypeID('Lyr '), id); return executeActionGet(ref); }
function boundsOf(ld) {
  var b = ld.getObjectValue(tid('bounds'));
  return '[' + [b.getUnitDoubleValue(tid('left')), b.getUnitDoubleValue(tid('top')), b.getUnitDoubleValue(tid('right')), b.getUnitDoubleValue(tid('bottom'))].join(',') + ']';
}
function describe(doc, L, path) {
  var ld = layerDesc(L.id);
  var d = ld.getObjectValue(tid('textKey'));
  var w = doc.width.as('px'), h = doc.height.as('px');
  var parts = [];
  parts.push('"name":' + quote(L.name));
  parts.push('"path":' + quote(path));
  parts.push('"text":' + quote(d.getString(tid('textKey'))));
  if (d.hasKey(tid('textClickPoint'))) {
    var cp = d.getObjectValue(tid('textClickPoint'));
    parts.push('"click":[' + (cp.getUnitDoubleValue(tid('horizontal')) * w / 100) + ',' + (cp.getUnitDoubleValue(tid('vertical')) * h / 100) + ']');
  }
  if (d.hasKey(tid('transform'))) {
    var m = d.getObjectValue(tid('transform'));
    parts.push('"transform":[' + [m.getDouble(tid('xx')), m.getDouble(tid('xy')), m.getDouble(tid('yx')), m.getDouble(tid('yy')), m.getDouble(tid('tx')), m.getDouble(tid('ty'))].join(',') + ']');
  }
  try { parts.push('"antiAlias":' + quote(keyName(d.getEnumerationValue(tid('antiAlias'))))); } catch (e1) {}
  parts.push('"oldText":' + (d.hasKey(tid('oldText')) ? 'true' : 'false'));
  try { parts.push('"warning":' + ld.getInteger(tid('textWarningLevel'))); } catch (e2) {}
  var kern = [];
  try {
    var ranges = d.getList(tid('textStyleRange'));
    for (var r = 0; r < ranges.count; r++) {
      var range = ranges.getObjectValue(r);
      var ts = range.getObjectValue(tid('textStyle'));
      var one = ts.hasKey(tid('autoKern')) ? keyName(ts.getEnumerationValue(tid('autoKern'))) : '(not stated)';
      if (ts.hasKey(tid('kerning'))) { one += ':' + ts.getInteger(tid('kerning')); }
      kern.push(quote(range.getInteger(tid('from')) + '-' + range.getInteger(tid('to')) + ' ' + one));
    }
  } catch (e3) {}
  parts.push('"kerning":[' + kern.join(',') + ']');
  var just = [];
  try {
    var pr = d.getList(tid('paragraphStyleRange'));
    for (var p = 0; p < pr.count; p++) {
      var ps = pr.getObjectValue(p).getObjectValue(tid('paragraphStyle'));
      just.push(quote(ps.hasKey(tid('align')) ? keyName(ps.getEnumerationValue(tid('align'))) : '(not stated)'));
    }
  } catch (e4) {}
  parts.push('"align":[' + just.join(',') + ']');
  try {
    var shapes = d.getList(tid('textShape'));
    if (shapes.count > 0) {
      var shape = shapes.getObjectValue(0);
      parts.push('"shape":' + quote(keyName(shape.getEnumerationValue(charIDToTypeID('TEXT')))));
      if (shape.hasKey(tid('frameBaselineAlignment'))) {
        parts.push('"frameBaseline":' + quote(keyName(shape.getEnumerationValue(tid('frameBaselineAlignment')))));
      }
    }
  } catch (e5) {}
  parts.push('"bounds":' + boundsOf(ld));
  return parts;
}
function relay(id) {
  var textKey = layerDesc(id).getObjectValue(tid('textKey'));
  var clickPoint = tid('textClickPoint');
  if (!textKey.hasKey(clickPoint)) { return false; }
  var target = new ActionReference();
  target.putEnumerated(tid('textLayer'), charIDToTypeID('Ordn'), charIDToTypeID('Trgt'));
  var set = new ActionDescriptor();
  set.putReference(charIDToTypeID('null'), target);
  var same = new ActionDescriptor();
  same.putObject(clickPoint, tid('paint'), textKey.getObjectValue(clickPoint));
  set.putObject(charIDToTypeID('T   '), tid('textLayer'), same);
  executeAction(charIDToTypeID('setd'), set, DialogModes.NO);
  return true;
}
function walk(doc, layers, path, before, after) {
  for (var i = 0; i < layers.length; i++) {
    var L = layers[i];
    var here = path + (path === '' ? '' : '/') + i;
    if (L.typename == 'LayerSet') { walk(doc, L.layers, here, before, after); continue; }
    var kind = '';
    try { kind = String(L.kind); } catch (e) {}
    if (kind != 'LayerKind.TEXT') { continue; }
    var parts;
    try { parts = describe(doc, L, here); } catch (e2) { before.push('{"name":' + quote(L.name) + ',"path":' + quote(here) + ',"error":' + quote(String(e2)) + '}'); continue; }
    var relaid = 'null', relayError = '';
    try {
      var locked = false;
      try { locked = L.allLocked; } catch (e3) {}
      if (!locked) {
        var wasVisible = L.visible;
        doc.activeLayer = L;
        if (relay(L.id)) { relaid = boundsOf(layerDesc(L.id)); }
        if (L.visible != wasVisible) { L.visible = wasVisible; }
      }
    } catch (e4) { relayError = String(e4); }
    parts.push('"relaidBounds":' + relaid);
    if (relayError !== '') { parts.push('"relayError":' + quote(relayError)); }
    before.push('{' + parts.join(',') + '}');
  }
}
log.push('open before: ' + openNames());
for (var f = 0; f < FILES.length; f++) {
  var file = new File(FILES[f]);
  var base = decodeURI(file.name);
  if (base.lastIndexOf('.') > 0) { base = base.substring(0, base.lastIndexOf('.')); }
  base += SUFFIX;
  var doc = null, dup = null;
  try {
    doc = app.open(file);
    dup = doc.duplicate();
    doc.close(SaveOptions.DONOTSAVECHANGES);
    doc = null;
    var layers = [];
    walk(dup, dup.layers, '', layers, null);
    var json = '{"file":' + quote(FILES[f]) + ',"width":' + dup.width.as('px') + ',"height":' + dup.height.as('px') + ',"layers":[\n' + layers.join(',\n') + '\n]}';
    var out = new File(OUT + '/' + base + '.json');
    out.encoding = 'UTF-8'; out.open('w'); out.write(json); out.close();
    dup.flatten();
    // PNG takes RGB (or gray) at 8 or 16 bits; the comparison only needs the picture.
    try { if (dup.mode != DocumentMode.RGB) { dup.changeMode(ChangeMode.RGB); } } catch (eMode) {}
    try { if (dup.bitsPerChannel != BitsPerChannelType.EIGHT) { dup.bitsPerChannel = BitsPerChannelType.EIGHT; } } catch (eBits) {}
    var o = new PNGSaveOptions();
    dup.saveAs(new File(OUT + '/' + base + '-relaid.png'), o, true, Extension.LOWERCASE);
    dup.close(SaveOptions.DONOTSAVECHANGES);
    dup = null;
    log.push(base + ': ' + layers.length + ' type layer(s)');
  } catch (e) {
    log.push(base + ': ERROR ' + e);
    // Only the two documents this run opened.
    try { if (dup !== null) { dup.close(SaveOptions.DONOTSAVECHANGES); } } catch (e2) {}
    try { if (doc !== null) { doc.close(SaveOptions.DONOTSAVECHANGES); } } catch (e3) {}
  }
}
log.push('open after: ' + openNames());
log.join('\n');
"@
$result = $app.DoJavaScript($jsx)
$result | Add-Content -Encoding UTF8 (Join-Path $OutDir 'readback.log')
$result
