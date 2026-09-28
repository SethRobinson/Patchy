# P2b captures: single-setting paragraph changes applied through Action Manager (the DOM
# textItem indent setters did not reach the file), including list styles with the enum
# spellings found in Photoshop.exe. Each capture is one PSD; capture.log records readbacks.
#   powershell -Command "& scripts\dev\txt2\ps-capture-p2b.ps1 -OutDir <dir>"
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
function savePsd(doc, name) {
  var o = new PhotoshopSaveOptions(); o.layers = true; o.embedColorProfile = true;
  doc.saveAs(new File(OUT + '/' + name + '.psd'), o, true, Extension.LOWERCASE);
  log.push('saved ' + name);
}
function makeDoc(text) {
  var doc = app.documents.add(600, 400, 72, 'p2b', NewDocumentMode.RGB, DocumentFill.WHITE);
  var L = doc.artLayers.add(); L.kind = LayerKind.TEXT;
  var t = L.textItem;
  t.kind = TextType.PARAGRAPHTEXT; t.position = [40, 60]; t.width = 400; t.height = 260;
  t.font = 'ArialMT'; t.size = 24; t.contents = text;
  return doc;
}
function paragraphStyleOf(doc) {
  var ref = new ActionReference(); ref.putIdentifier(charIDToTypeID('Lyr '), doc.activeLayer.id);
  var d = executeActionGet(ref).getObjectValue(tid('textKey'));
  return d.getList(tid('paragraphStyleRange')).getObjectValue(0).getObjectValue(tid('paragraphStyle'));
}
function describe(ps) {
  var out = [];
  try { var ls = ps.getObjectValue(tid('listStyle')); out.push('list=' + keyName(ls.getEnumerationValue(tid('listStylePredefinedTag'))) + '/' + keyName(ls.getEnumerationValue(tid('bulletListStyleTag')))); } catch (e) { out.push('list=?'); }
  try { out.push('tier=' + ps.getInteger(tid('listTier'))); } catch (e) {}
  try { out.push('fli=' + ps.getUnitDoubleValue(tid('firstLineIndent'))); } catch (e) {}
  try { out.push('si=' + ps.getUnitDoubleValue(tid('startIndent'))); } catch (e) {}
  try { out.push('hyph=' + ps.getBoolean(tid('hyphenate'))); } catch (e) {}
  try { out.push('dir=' + keyName(ps.getEnumerationValue(tid('directionType')))); } catch (e) {}
  return out.join(' ');
}
function setParagraph(fill) {
  var ref = new ActionReference();
  ref.putProperty(tid('property'), tid('paragraphStyle'));
  ref.putEnumerated(tid('textLayer'), tid('ordinal'), tid('targetEnum'));
  var desc = new ActionDescriptor(); desc.putReference(tid('null'), ref);
  var ps = new ActionDescriptor();
  fill(ps);
  desc.putObject(tid('to'), tid('paragraphStyle'), ps);
  executeAction(tid('set'), desc, DialogModes.NO);
}
function px(ps, key, v) { ps.putUnitDouble(tid(key), tid('pixelsUnit'), v); }
function listDesc(ps, predefined, bullet, tier) {
  var ls = new ActionDescriptor();
  ls.putEnumerated(tid('listStylePredefinedTag'), tid('listStylePredefinedTag'), tid(predefined));
  ls.putEnumerated(tid('bulletListStyleTag'), tid('bulletListStyleTag'), tid(bullet));
  ps.putObject(tid('listStyle'), tid('listStyleDetails'), ls);
  if (tier !== null) { ps.putInteger(tid('listTier'), tier); }
}
var TEXT = 'First paragraph of the box.\rSecond paragraph of the box.';
function capture(name, fill) {
  var d = makeDoc(TEXT);
  try { setParagraph(fill); log.push(name + ': ' + describe(paragraphStyleOf(d))); }
  catch (e) { log.push(name + ' threw ' + e); }
  savePsd(d, name);
  d.close(SaveOptions.DONOTSAVECHANGES);
}
var d0 = makeDoc(TEXT); log.push('base: ' + describe(paragraphStyleOf(d0))); savePsd(d0, 'base'); d0.close(SaveOptions.DONOTSAVECHANGES);
capture('am_firstLineIndent24', function (ps) { px(ps, 'firstLineIndent', 24); });
capture('am_startIndent30', function (ps) { px(ps, 'startIndent', 30); });
capture('am_endIndent18', function (ps) { px(ps, 'endIndent', 18); });
capture('am_spaceBefore12', function (ps) { px(ps, 'spaceBefore', 12); });
capture('am_spaceAfter9', function (ps) { px(ps, 'spaceAfter', 9); });
capture('am_hanging20', function (ps) { px(ps, 'firstLineIndent', -20); px(ps, 'startIndent', 20); });
capture('am_hyphenateOff', function (ps) { ps.putBoolean(tid('hyphenate'), false); });
capture('am_dirRtl', function (ps) { ps.putEnumerated(tid('directionType'), tid('directionType'), tid('dirRightToLeft')); });
capture('am_bullet', function (ps) { listDesc(ps, 'predefinedBulletListStyleTag', 'predefinedBulletListStyleTag', null); });
capture('am_bullet_tier1', function (ps) { listDesc(ps, 'predefinedBulletListStyleTag', 'predefinedBulletListStyleTag', 1); });
capture('am_bullet_tier2', function (ps) { listDesc(ps, 'predefinedBulletListStyleTag', 'predefinedBulletListStyleTag', 2); });
capture('am_dashBullet', function (ps) { listDesc(ps, 'predefinedBulletListStyleTag', 'predefinedDashBulletListStyleTag', 1); });
capture('am_numeric', function (ps) { listDesc(ps, 'predefinedNumericListStyleTag', 'listStylePredefinedTagNone', 1); });
capture('am_numeric_noneBullet', function (ps) { listDesc(ps, 'predefinedNumericListStyleTag', 'predefinedBulletListStyleTag', 1); });
capture('am_upperAlpha', function (ps) { listDesc(ps, 'predefinedUppercaseAlphaListStyleTag', 'listStylePredefinedTagNone', 1); });
var f = new File(OUT + '/capture.log'); f.encoding = 'UTF-8'; f.open('w'); f.write(log.join('\n')); f.close();
"@
$app.DoJavaScript($jsx) | Out-Null
Get-Content (Join-Path $OutDir 'capture.log')
