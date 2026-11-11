# Captures test-fixtures/psd/photoshop-text-point-anchors.psd: point text whose anchor is not the
# line box's left edge (right-aligned, centered with two paragraphs, right-aligned and rotated,
# centered and scaled, a baseline shift on a fractional anchor), two kerning settings (manual 0,
# Optical) and a Smooth layer. Photoshop 2026 over COM, one task-owned document; capture.log
# records each layer's anchor, kerning and anti-aliasing as Photoshop reads them back.
#   powershell -Command "& scripts\dev\txt2\ps-capture-point-anchors.ps1 -OutDir <dir>"
param([Parameter(Mandatory = $true)][string]$OutDir)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $OutDir | Out-Null
$outJs = $OutDir -replace '\\', '/'
$app = New-Object -ComObject Photoshop.Application
$jsx = @"
app.displayDialogs = DialogModes.NO;
try { app.preferences.rulerUnits = Units.PIXELS; } catch (e) {}
try { app.preferences.typeUnits = TypeUnits.PIXELS; } catch (e) {}
var OUT = "$outJs";
var log = [];
function tid(s) { return stringIDToTypeID(s); }
function keyName(t) { var s = ''; try { s = typeIDToStringID(t); } catch (e) {} if (s === '') { try { s = typeIDToCharID(t); } catch (e2) { s = String(t); } } return s; }
function openNames() { var n = []; for (var i = 0; i < app.documents.length; i++) { n.push(app.documents[i].name); } return n.join(' | '); }
log.push('open before: ' + openNames());
var doc = app.documents.add(360, 280, 72, 'point-anchors', NewDocumentMode.RGB, DocumentFill.WHITE);
function addLayer(label, text, x, y, justification) {
  var L = doc.artLayers.add(); L.kind = LayerKind.TEXT; L.name = label;
  var t = L.textItem; t.kind = TextType.POINTTEXT;
  t.font = 'ArialMT'; t.size = 24; t.contents = text;
  t.justification = justification;
  t.position = [x, y];
  doc.activeLayer = L;
  return L;
}
function setStyle(fill) {
  var ref = new ActionReference();
  ref.putProperty(tid('property'), tid('textStyle'));
  ref.putEnumerated(tid('textLayer'), tid('ordinal'), tid('targetEnum'));
  var desc = new ActionDescriptor(); desc.putReference(tid('null'), ref);
  var ts = new ActionDescriptor();
  fill(ts);
  desc.putObject(tid('to'), tid('textStyle'), ts);
  executeAction(tid('set'), desc, DialogModes.NO);
}
function describe(L) {
  var ref = new ActionReference(); ref.putIdentifier(charIDToTypeID('Lyr '), L.id);
  var ld = executeActionGet(ref);
  var d = ld.getObjectValue(tid('textKey'));
  var out = L.name + ':';
  var cp = d.getObjectValue(tid('textClickPoint'));
  out += ' click=' + cp.getUnitDoubleValue(tid('horizontal')) + '%,' + cp.getUnitDoubleValue(tid('vertical')) + '%';
  if (d.hasKey(tid('transform'))) {
    var m = d.getObjectValue(tid('transform'));
    out += ' transform=' + [m.getDouble(tid('xx')), m.getDouble(tid('xy')), m.getDouble(tid('yx')), m.getDouble(tid('yy')), m.getDouble(tid('tx')), m.getDouble(tid('ty'))].join(',');
  }
  out += ' antiAlias=' + keyName(d.getEnumerationValue(tid('antiAlias')));
  var ranges = d.getList(tid('textStyleRange'));
  for (var r = 0; r < ranges.count; r++) {
    var ts = ranges.getObjectValue(r).getObjectValue(tid('textStyle'));
    out += ' autoKern' + r + '=' + (ts.hasKey(tid('autoKern')) ? keyName(ts.getEnumerationValue(tid('autoKern'))) : '(absent)');
  }
  var b = ld.getObjectValue(tid('bounds'));
  out += ' bounds=' + [b.getUnitDoubleValue(tid('left')), b.getUnitDoubleValue(tid('top')), b.getUnitDoubleValue(tid('right')), b.getUnitDoubleValue(tid('bottom'))].join(',');
  return out;
}
var layers = [];
layers.push(addLayer('right', 'Right 12', 300.4, 40, Justification.RIGHT));
layers.push(addLayer('center-lines', 'Center\rline two!', 180.7, 80, Justification.CENTER));
var turned = addLayer('right-rotated', 'Turned', 300, 170, Justification.RIGHT);
turned.rotate(-15, AnchorPosition.MIDDLECENTER);
layers.push(turned);
var scaled = addLayer('center-scaled', 'Scaled', 110, 230, Justification.CENTER);
scaled.resize(130, 80, AnchorPosition.MIDDLECENTER);
layers.push(scaled);
var shifted = addLayer('left-shift', 'ab', 20.3, 262.6, Justification.LEFT);
shifted.textItem.baselineShift = -1.5;
layers.push(shifted);
var manual = addLayer('kern-manual', 'AV To', 20, 150, Justification.LEFT);
setStyle(function (ts) { ts.putEnumerated(tid('autoKern'), tid('autoKern'), tid('manual')); ts.putInteger(tid('kerning'), 0); });
layers.push(manual);
var optical = addLayer('kern-optical', 'AV To', 20, 190, Justification.LEFT);
setStyle(function (ts) { ts.putEnumerated(tid('autoKern'), tid('autoKern'), tid('opticalKern')); });
layers.push(optical);
var smooth = addLayer('smooth', 'Smooth', 230, 262, Justification.LEFT);
smooth.textItem.antiAliasMethod = AntiAlias.SMOOTH;
layers.push(smooth);
for (var i = 0; i < layers.length; i++) { log.push(describe(layers[i])); }
var o = new PhotoshopSaveOptions(); o.layers = true; o.embedColorProfile = false;
doc.saveAs(new File(OUT + '/photoshop-text-point-anchors.psd'), o, true, Extension.LOWERCASE);
doc.close(SaveOptions.DONOTSAVECHANGES);
log.push('open after: ' + openNames());
log.join('\n');
"@
$result = $app.DoJavaScript($jsx)
$result | Set-Content -Encoding UTF8 (Join-Path $OutDir 'capture.log')
$result
