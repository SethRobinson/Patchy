# Linked smart object ground-truth capture: drives Adobe Photoshop over COM to place SVG and PNG
# files as linked (and, for comparison, embedded) smart objects, saves each case as a PSD, and
# records what Photoshop's Action Manager reports for every placed layer. The findings this
# produced are recorded in docs/smart-objects-ground-truth.md ("Place Linked ground truth").
#
#   powershell -Command "& scripts\dev\smart-objects\ps-capture-linked.ps1 -OutDir local-test-fixtures\psd\ps2026_linked -Svg a.svg -Svg2 b.svg -Png c.png"
#
# Output in <OutDir>: the copied sources (logo.svg, logo2.svg, logo.png, and a Unicode-named
# copy), one PSD per case, a PNG of Photoshop's own render beside each PSD, capture.json (the
# smartObject / smartObjectMore descriptors per layer), and dialogs.log (the text of any
# PSDialogBox that appeared while a COM call was blocked).
#
# Hygiene (docs/photoshop-com.md): dialogs off, only documents this script created are closed,
# the previously active document is reactivated, and the ruler units and the
# placeRasterSmartObject preference are restored.
param(
  [Parameter(Mandatory = $true)][string]$OutDir,
  [Parameter(Mandatory = $true)][string]$Svg,
  [Parameter(Mandatory = $true)][string]$Svg2,
  [Parameter(Mandatory = $true)][string]$Png
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $OutDir | Out-Null
New-Item -ItemType Directory -Force (Join-Path $OutDir 'sub') | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$unicodeName = "logo " + [char]0x00FC + "n" + [char]0x00EF + " " + [char]0x65E5 + [char]0x672C + ".svg"
Copy-Item $Svg (Join-Path $OutDir 'logo.svg') -Force
Copy-Item $Svg2 (Join-Path $OutDir 'logo2.svg') -Force
Copy-Item $Png (Join-Path $OutDir 'logo.png') -Force
Copy-Item $Svg (Join-Path $OutDir $unicodeName) -Force
Copy-Item $Svg (Join-Path $OutDir 'edit.svg') -Force
Set-Content -Path (Join-Path $OutDir 'wide.svg') -Encoding utf8 -Value '<svg xmlns="http://www.w3.org/2000/svg" width="512" height="256" viewBox="0 0 512 256"><rect width="512" height="256" fill="#3366cc"/><circle cx="128" cy="128" r="96" fill="#ffcc00"/></svg>'

$log = Join-Path $OutDir 'dialogs.log'
Set-Content -Path $log -Value 'START'
$watcher = Start-Job -ArgumentList $log -ScriptBlock {
  param($LogPath)
  Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public class PsLinkWatch {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  public static IntPtr Dialog(uint pid) { IntPtr found = IntPtr.Zero; EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); if (p == pid && IsWindowVisible(h)) { var c = new StringBuilder(256); GetClassName(h, c, 256); if (c.ToString() == "PSDialogBox") { found = h; } } return true; }, IntPtr.Zero); return found; }
  public static string DialogText(IntPtr dlg) { var sb = new StringBuilder(); var t0 = new StringBuilder(512); GetWindowText(dlg, t0, 512); sb.Append("[" + t0.ToString() + "] "); EnumChildWindows(dlg, (h, l) => { var t = new StringBuilder(1024); GetWindowText(h, t, 1024); if (t.Length > 0) { sb.Append(t.ToString()); sb.Append(" | "); } return true; }, IntPtr.Zero); return sb.ToString(); }
}
"@
  $last = ''
  while ($true) {
    $lines = Get-Content $LogPath -ErrorAction SilentlyContinue
    if ($lines -and $lines[-1] -eq 'DONE') { break }
    $p = Get-Process Photoshop -ErrorAction SilentlyContinue
    if ($p) {
      $dlg = [PsLinkWatch]::Dialog([uint32]$p.Id)
      if ($dlg -ne [IntPtr]::Zero) {
        $text = [PsLinkWatch]::DialogText($dlg)
        if ($text -ne $last) { Add-Content -Path $LogPath -Value ('DIALOG ' + $text); $last = $text }
      } else { $last = '' }
    }
    Start-Sleep -Milliseconds 150
  }
}

$outJs = $OutDir -replace '\\', '/'
$unicodeJs = -join ($unicodeName.ToCharArray() | ForEach-Object { if ([int]$_ -gt 127) { '\u{0:X4}' -f [int]$_ } else { [string]$_ } })
$jsx = @"
var outDir = "$outJs";
var unicodeName = "$unicodeJs";
function cTID(s) { return charIDToTypeID(s); }
function sTID(s) { return stringIDToTypeID(s); }
function quote(s) {
  var r = '"';
  for (var i = 0; i < s.length; i++) {
    var c = s.charCodeAt(i);
    if (c === 34) { r += '\\"'; } else if (c === 92) { r += '\\\\'; } else if (c < 32 || c > 126) {
      var h = c.toString(16); while (h.length < 4) { h = '0' + h; } r += '\\u' + h;
    } else { r += s.charAt(i); }
  }
  return r + '"';
}
function idName(id) { var s = typeIDToStringID(id); return s !== '' ? s : "'" + typeIDToCharID(id) + "'"; }
function descJson(d) {
  var parts = [];
  for (var i = 0; i < d.count; i++) {
    var key = d.getKey(i);
    parts.push(quote(idName(key)) + ':' + valueJson(d, key, d.getType(key)));
  }
  return '{' + parts.join(',') + '}';
}
function listJson(l) {
  var parts = [];
  for (var i = 0; i < l.count; i++) { parts.push(valueJson(l, i, l.getType(i))); }
  return '[' + parts.join(',') + ']';
}
function valueJson(c, k, t) {
  try {
    switch (t) {
      case DescValueType.INTEGERTYPE: return String(c.getInteger(k));
      case DescValueType.LARGEINTEGERTYPE: return String(c.getLargeInteger(k));
      case DescValueType.DOUBLETYPE: return String(c.getDouble(k));
      case DescValueType.UNITDOUBLE: return '{"unit":' + quote(idName(c.getUnitDoubleType(k))) + ',"value":' + c.getUnitDoubleValue(k) + '}';
      case DescValueType.STRINGTYPE: return quote(c.getString(k));
      case DescValueType.BOOLEANTYPE: return c.getBoolean(k) ? 'true' : 'false';
      case DescValueType.ENUMERATEDTYPE: return quote(idName(c.getEnumerationType(k)) + '.' + idName(c.getEnumerationValue(k)));
      case DescValueType.OBJECTTYPE: return '{"class":' + quote(idName(c.getObjectType(k))) + ',"value":' + descJson(c.getObjectValue(k)) + '}';
      case DescValueType.LISTTYPE: return listJson(c.getList(k));
      case DescValueType.ALIASTYPE: return quote('path:' + c.getPath(k).fsName);
      case DescValueType.RAWTYPE: return quote('raw:' + c.getData(k).length);
      default: return quote('type:' + t);
    }
  } catch (e) { return quote('error:' + e); }
}
function layerDescriptor(property) {
  var r = new ActionReference();
  r.putProperty(cTID('Prpr'), sTID(property));
  r.putEnumerated(cTID('Lyr '), cTID('Ordn'), cTID('Trgt'));
  var d = executeActionGet(r);
  return d.hasKey(sTID(property)) ? descJson(d.getObjectValue(sTID(property))) : 'null';
}
function place(path, linked, percent, dx, dy) {
  var d = new ActionDescriptor();
  d.putPath(cTID('null'), new File(path));
  if (linked) { d.putBoolean(cTID('Lnkd'), true); }
  d.putEnumerated(cTID('FTcs'), cTID('QCSt'), cTID('Qcsa'));
  var o = new ActionDescriptor();
  o.putUnitDouble(cTID('Hrzn'), cTID('#Pxl'), dx);
  o.putUnitDouble(cTID('Vrtc'), cTID('#Pxl'), dy);
  d.putObject(cTID('Ofst'), cTID('Ofst'), o);
  if (percent !== 100) {
    d.putUnitDouble(cTID('Wdth'), cTID('#Prc'), percent);
    d.putUnitDouble(cTID('Hght'), cTID('#Prc'), percent);
  }
  executeAction(cTID('Plc '), d, DialogModes.NO);
}
function getPlaceRaster() {
  var r = new ActionReference();
  r.putProperty(cTID('Prpr'), sTID('generalPreferences'));
  r.putEnumerated(cTID('capp'), cTID('Ordn'), cTID('Trgt'));
  return executeActionGet(r).getObjectValue(sTID('generalPreferences')).getBoolean(sTID('placeRasterSmartObject'));
}
function setPlaceRaster(value) {
  var d = new ActionDescriptor();
  var r = new ActionReference();
  r.putProperty(cTID('Prpr'), sTID('generalPreferences'));
  r.putEnumerated(cTID('capp'), cTID('Ordn'), cTID('Trgt'));
  d.putReference(cTID('null'), r);
  var p = new ActionDescriptor();
  p.putBoolean(sTID('placeRasterSmartObject'), value);
  d.putObject(cTID('T   '), sTID('generalPreferences'), p);
  executeAction(cTID('setd'), d, DialogModes.NO);
}
var results = [];
function record(caseName, doc) {
  var layers = [];
  for (var i = 0; i < doc.artLayers.length; i++) {
    var layer = doc.artLayers[i];
    doc.activeLayer = layer;
    var b = layer.bounds;
    layers.push('{"name":' + quote(layer.name) + ',"kind":' + quote(String(layer.kind)) +
      ',"bounds":[' + b[0].value + ',' + b[1].value + ',' + b[2].value + ',' + b[3].value + ']' +
      ',"smartObject":' + layerDescriptor('smartObject') + ',"smartObjectMore":' + layerDescriptor('smartObjectMore') + '}');
  }
  results.push('{"case":' + quote(caseName) + ',"layers":[' + layers.join(',') + ']}');
}
function finish(caseName, doc, psdPath) {
  var options = new PhotoshopSaveOptions();
  options.layers = true;
  doc.saveAs(new File(psdPath), options, false, Extension.LOWERCASE);
  record(caseName, doc);
  // Photoshop's own render of the placed content, transparency kept.
  doc.saveAs(new File(psdPath.replace(/\.psd$/, '.png')), new PNGSaveOptions(), true, Extension.LOWERCASE);
  doc.close(SaveOptions.DONOTSAVECHANGES);
}
function fresh(caseName) {
  return app.documents.add(1200, 800, 72, caseName, NewDocumentMode.RGB, DocumentFill.TRANSPARENT);
}
var savedDialogs = app.displayDialogs;
var savedUnits = app.preferences.rulerUnits;
var prior = app.documents.length > 0 ? app.activeDocument : null;
app.displayDialogs = DialogModes.NO;
app.preferences.rulerUnits = Units.PIXELS;
var savedPlaceRaster = getPlaceRaster();
var error = '';
try {
  setPlaceRaster(true);
  var d = fresh('linked_svg'); place(outDir + '/logo.svg', true, 100, 0, 0); finish('linked_svg', d, outDir + '/linked_svg.psd');
  d = fresh('embedded_svg'); place(outDir + '/logo.svg', false, 100, 0, 0); finish('embedded_svg', d, outDir + '/embedded_svg.psd');
  d = fresh('linked_png'); place(outDir + '/logo.png', true, 100, 0, 0); finish('linked_png', d, outDir + '/linked_png.psd');
  d = fresh('embedded_png'); place(outDir + '/logo.png', false, 100, 0, 0); finish('embedded_png', d, outDir + '/embedded_png.psd');
  d = fresh('linked_svg_multi');
  place(outDir + '/logo.svg', true, 100, 0, 0);
  place(outDir + '/logo.svg', true, 50, -300, -100);
  place(outDir + '/logo2.svg', true, 25, 400, 200);
  finish('linked_svg_multi', d, outDir + '/linked_svg_multi.psd');
  d = fresh('linked_svg_parent'); place(outDir + '/logo.svg', true, 100, 0, 0); finish('linked_svg_parent', d, outDir + '/sub/linked_svg_parent.psd');
  d = fresh('linked_svg_unicode'); place(outDir + '/' + unicodeName, true, 100, 0, 0); finish('linked_svg_unicode', d, outDir + '/linked_svg_unicode.psd');
  // A canvas larger than the artwork, at 72 and at 144 ppi: where the vector base rectangle lands.
  d = app.documents.add(2400, 2000, 72, 'linked_svg_bigdoc', NewDocumentMode.RGB, DocumentFill.TRANSPARENT);
  place(outDir + '/logo.svg', true, 100, 0, 0);
  place(outDir + '/logo.svg', true, 50, 100, 50);
  finish('linked_svg_bigdoc', d, outDir + '/linked_svg_bigdoc.psd');
  d = app.documents.add(3000, 3000, 144, 'linked_svg_144', NewDocumentMode.RGB, DocumentFill.TRANSPARENT);
  place(outDir + '/logo.svg', true, 100, 0, 0);
  finish('linked_svg_144', d, outDir + '/linked_svg_144.psd');
  // Duplicate (shared element?) then move and rotate the copy.
  d = app.documents.add(2400, 2000, 72, 'linked_svg_dup', NewDocumentMode.RGB, DocumentFill.TRANSPARENT);
  place(outDir + '/logo.svg', true, 100, 0, 0);
  var copy = d.activeLayer.duplicate();
  d.activeLayer = copy;
  copy.translate(300, 200);
  copy.rotate(30);
  finish('linked_svg_dup', d, outDir + '/linked_svg_dup.psd');
  // Relink to a file of another size.
  d = app.documents.add(2400, 2000, 72, 'linked_svg_relinked', NewDocumentMode.RGB, DocumentFill.TRANSPARENT);
  place(outDir + '/logo.svg', true, 50, 100, 50);
  var relink = new ActionDescriptor();
  relink.putPath(cTID('null'), new File(outDir + '/wide.svg'));
  executeAction(sTID('placedLayerRelinkToFile'), relink, DialogModes.NO);
  finish('linked_svg_relinked', d, outDir + '/linked_svg_relinked.psd');
  // Change the linked file on disk, read the flags, then Update Modified Content.
  d = app.documents.add(2400, 2000, 72, 'linked_svg_update', NewDocumentMode.RGB, DocumentFill.TRANSPARENT);
  place(outDir + '/edit.svg', true, 100, 0, 0);
  place(outDir + '/edit.svg', true, 50, 100, 50);
  var before = new PhotoshopSaveOptions();
  before.layers = true;
  d.saveAs(new File(outDir + '/linked_svg_update_before.psd'), before, true, Extension.LOWERCASE);
  var source = new File(outDir + '/logo2.svg');
  source.encoding = 'UTF-8';
  source.open('r');
  var text = source.read();
  source.close();
  var target = new File(outDir + '/edit.svg');
  target.encoding = 'UTF-8';
  target.open('w');
  target.write(text + '\n<!-- edited -->\n');
  target.close();
  `$.sleep(1500);
  record('linked_svg_update_stale', d);
  d.activeLayer = d.artLayers[0];
  try { executeAction(sTID('placedLayerUpdateModified'), new ActionDescriptor(), DialogModes.NO); }
  catch (updateError) { error += ' update: ' + updateError; }
  record('linked_svg_update_one', d);
  try { executeAction(sTID('placedLayerUpdateAllModified'), new ActionDescriptor(), DialogModes.NO); }
  catch (updateAllError) { error += ' updateAll: ' + updateAllError; }
  finish('linked_svg_update', d, outDir + '/linked_svg_update.psd');
} catch (e) { error = String(e) + ' line ' + e.line; }
try { setPlaceRaster(savedPlaceRaster); } catch (e2) { error += ' restore: ' + e2; }
app.preferences.rulerUnits = savedUnits;
app.displayDialogs = savedDialogs;
if (prior !== null) { try { app.activeDocument = prior; } catch (e3) {} }
var out = new File(outDir + '/capture.json');
out.encoding = 'UTF-8';
out.open('w');
out.write('{"placeRasterSmartObject":' + (savedPlaceRaster ? 'true' : 'false') + ',"error":' + quote(error) + ',"cases":[' + results.join(',\n') + ']}');
out.close();
error === '' ? 'OK' : error;
"@

$app = New-Object -ComObject Photoshop.Application
try {
  $result = $app.DoJavaScript($jsx)
  Write-Output ("Photoshop: " + $result)
} finally {
  Add-Content -Path $log -Value 'DONE'
  Wait-Job $watcher -Timeout 10 | Out-Null
  Remove-Job $watcher -Force -ErrorAction SilentlyContinue
}
Get-Content $log | Where-Object { $_ -like 'DIALOG *' }
