# Txt2 probe driver: opens each PSD in Photoshop over COM (DialogModes.ERROR), selects every type
# layer (the "update text layers" prompt fires on first selection), reads the type layers back
# through Action Manager (textKey, textStyleRange, paragraphStyleRange), optionally renders a
# flattened PNG and resaves a PSD, then closes the document. A watcher job logs the text of any
# PSDialogBox that appears while the COM call blocks. Output: <OutDir>\<name>.probe.json per file
# and one summary line per file on stdout.
#
#   powershell -Command "& scripts\dev\txt2\ps-probe.ps1 -Files a.psd,b.psd -OutDir <dir> [-Render] [-Resave] [-Mutate]"
param(
  [Parameter(Mandatory = $true)][string[]]$Files,
  [Parameter(Mandatory = $true)][string]$OutDir,
  [switch]$Render,
  [switch]$Resave,
  [switch]$Mutate,
  [int]$TimeoutSeconds = 120
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $OutDir | Out-Null
$log = Join-Path $OutDir 'dialogs.log'
if (Test-Path $log) { Remove-Item $log }
New-Item -ItemType File $log | Out-Null

$watcher = Start-Job -ArgumentList $log -ScriptBlock {
  param($LogPath)
  Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public class PsWatch2 {
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
      $dlg = [PsWatch2]::Dialog([uint32]$p.Id)
      if ($dlg -ne [IntPtr]::Zero) {
        $text = [PsWatch2]::DialogText($dlg)
        if ($text -ne $last) {
          $current = ($lines | Where-Object { $_ -like 'OPENING *' } | Select-Object -Last 1)
          Add-Content -Path $LogPath -Value ('DIALOG ' + $current + ' :: ' + $text)
          $last = $text
        }
      } else { $last = '' }
    }
    Start-Sleep -Milliseconds 150
  }
}

$app = New-Object -ComObject Photoshop.Application
$results = @()
foreach ($file in $Files) {
  $full = (Resolve-Path $file).Path
  $name = [System.IO.Path]::GetFileNameWithoutExtension($full)
  $js = $full -replace '\\', '/'
  $logJs = $log -replace '\\', '/'
  $outJson = (Join-Path $OutDir ($name + '.probe.json')) -replace '\\', '/'
  $renderPng = if ($Render) { (Join-Path $OutDir ($name + '.render.png')) -replace '\\', '/' } else { '' }
  $resavePsd = if ($Resave) { (Join-Path $OutDir ($name + '.resave.psd')) -replace '\\', '/' } else { '' }
  $mutatePsd = if ($Mutate) { (Join-Path $OutDir ($name + '.mutated.psd')) -replace '\\', '/' } else { '' }
  $mutatePng = if ($Mutate) { (Join-Path $OutDir ($name + '.mutated.png')) -replace '\\', '/' } else { '' }
  $jsx = @"
app.displayDialogs = DialogModes.NO;
try { app.preferences.rulerUnits = Units.PIXELS; } catch (e) {}
try { app.preferences.typeUnits = TypeUnits.PIXELS; } catch (e) {}
var log = new File("$logJs");
function note(line) { log.open("a"); log.write(line + "\n"); log.close(); }
function q(s) { s = String(s); var r = ''; for (var i = 0; i < s.length; i++) { var c = s.charAt(i); var o = s.charCodeAt(i); if (c == '"' || c == '\\') { r += '\\' + c; } else if (o < 32 || o > 126) { r += '\\u' + ('000' + o.toString(16)).slice(-4); } else { r += c; } } return '"' + r + '"'; }
function tid(s) { return stringIDToTypeID(s); }
function keyName(t) { var s = ''; try { s = typeIDToStringID(t); } catch (e) {} if (s === '') { try { s = typeIDToCharID(t); } catch (e2) { s = String(t); } } return s; }
function descToJson(d, depth) {
  if (depth > 12) return '"<deep>"';
  var parts = [];
  for (var i = 0; i < d.count; i++) {
    var k = d.getKey(i);
    parts.push(q(keyName(k)) + ':' + valueToJson(d, k, depth));
  }
  return '{' + parts.join(',') + '}';
}
function listToJson(l, depth) {
  var parts = [];
  for (var i = 0; i < l.count; i++) { parts.push(listValueToJson(l, i, depth)); }
  return '[' + parts.join(',') + ']';
}
function valueToJson(d, k, depth) {
  var t = d.getType(k);
  try {
    switch (t) {
      case DescValueType.BOOLEANTYPE: return d.getBoolean(k) ? 'true' : 'false';
      case DescValueType.DOUBLETYPE: return String(d.getDouble(k));
      case DescValueType.INTEGERTYPE: return String(d.getInteger(k));
      case DescValueType.LARGEINTEGERTYPE: return String(d.getLargeInteger(k));
      case DescValueType.STRINGTYPE: return q(d.getString(k));
      case DescValueType.UNITDOUBLE: return '{"unit":' + q(keyName(d.getUnitDoubleType(k))) + ',"value":' + d.getUnitDoubleValue(k) + '}';
      case DescValueType.ENUMERATEDTYPE: return q(keyName(d.getEnumerationType(k)) + '.' + keyName(d.getEnumerationValue(k)));
      case DescValueType.OBJECTTYPE: return '{"_class":' + q(keyName(d.getObjectType(k))) + ',"_v":' + descToJson(d.getObjectValue(k), depth + 1) + '}';
      case DescValueType.LISTTYPE: return listToJson(d.getList(k), depth + 1);
      case DescValueType.REFERENCETYPE: return '"<ref>"';
      case DescValueType.RAWTYPE: return q('<raw ' + d.getData(k).length + '>');
      case DescValueType.CLASSTYPE: return q('class ' + keyName(d.getClass(k)));
      default: return q('<' + t + '>');
    }
  } catch (e) { return q('<err ' + e + '>'); }
}
function listValueToJson(l, i, depth) {
  var t = l.getType(i);
  try {
    switch (t) {
      case DescValueType.BOOLEANTYPE: return l.getBoolean(i) ? 'true' : 'false';
      case DescValueType.DOUBLETYPE: return String(l.getDouble(i));
      case DescValueType.INTEGERTYPE: return String(l.getInteger(i));
      case DescValueType.STRINGTYPE: return q(l.getString(i));
      case DescValueType.UNITDOUBLE: return '{"unit":' + q(keyName(l.getUnitDoubleType(i))) + ',"value":' + l.getUnitDoubleValue(i) + '}';
      case DescValueType.ENUMERATEDTYPE: return q(keyName(l.getEnumerationType(i)) + '.' + keyName(l.getEnumerationValue(i)));
      case DescValueType.OBJECTTYPE: return '{"_class":' + q(keyName(l.getObjectType(i))) + ',"_v":' + descToJson(l.getObjectValue(i), depth + 1) + '}';
      case DescValueType.LISTTYPE: return listToJson(l.getList(i), depth + 1);
      default: return q('<' + t + '>');
    }
  } catch (e) { return q('<err ' + e + '>'); }
}
function layerDesc(id) { var ref = new ActionReference(); ref.putIdentifier(charIDToTypeID('Lyr '), id); return executeActionGet(ref); }
function pngOptions() { var o = new PNGSaveOptions(); o.compression = 6; o.interlaced = false; return o; }
function renderTo(doc, pngPath) {
  var dup = null;
  try { dup = doc.duplicate(); dup.flatten(); if (dup.mode != DocumentMode.RGB && dup.mode != DocumentMode.GRAYSCALE) { dup.changeMode(ChangeMode.RGB); } dup.saveAs(new File(pngPath), pngOptions(), true, Extension.LOWERCASE); dup.close(SaveOptions.DONOTSAVECHANGES); return 'ok'; }
  catch (e) { try { if (dup !== null) { dup.close(SaveOptions.DONOTSAVECHANGES); } } catch (e2) {} return 'render-error: ' + e; }
}
function collectText(layers, out) {
  for (var i = 0; i < layers.length; i++) {
    var L = layers[i];
    if (L.typename == 'LayerSet') { collectText(L.layers, out); continue; }
    var kind = ''; try { kind = String(L.kind); } catch (e) {}
    if (kind != 'LayerKind.TEXT') continue;
    out.push(L);
  }
}
note("OPENING $js");
var doc = null; var result = '';
try { doc = app.open(new File("$js")); note("OPENED $js"); } catch (e) { note("ERROR $js " + e); result = '{"ok":false,"error":' + q(e) + '}'; }
if (doc !== null) {
  try {
    var textLayers = []; collectText(doc.layers, textLayers);
    var entries = [];
    for (var i = 0; i < textLayers.length; i++) {
      var L = textLayers[i];
      var selErr = '';
      try { doc.activeLayer = L; } catch (e) { selErr = String(e); }
      note("SELECTED " + L.name);
      var entry = '{"name":' + q(L.name) + ',"selectError":' + q(selErr);
      try { entry += ',"contents":' + q(L.textItem.contents); } catch (e) { entry += ',"contentsError":' + q(e); }
      try { entry += ',"font":' + q(L.textItem.font); } catch (e) {}
      try { entry += ',"size":' + L.textItem.size.as('px'); } catch (e) {}
      try { entry += ',"kind":' + q(L.textItem.kind); } catch (e) {}
      try { entry += ',"justification":' + q(L.textItem.justification); } catch (e) {}
      try { entry += ',"firstLineIndent":' + L.textItem.firstLineIndent.as('px'); } catch (e) {}
      try { entry += ',"leftIndent":' + L.textItem.leftIndent.as('px'); } catch (e) {}
      try { entry += ',"rightIndent":' + L.textItem.rightIndent.as('px'); } catch (e) {}
      try { entry += ',"spaceBefore":' + L.textItem.spaceBefore.as('px'); } catch (e) {}
      try { entry += ',"spaceAfter":' + L.textItem.spaceAfter.as('px'); } catch (e) {}
      try { var b = L.bounds; entry += ',"bounds":[' + b[0].as('px') + ',' + b[1].as('px') + ',' + b[2].as('px') + ',' + b[3].as('px') + ']'; } catch (e) {}
      try {
        var d = layerDesc(L.id);
        if (d.hasKey(tid('textKey'))) { entry += ',"textKey":' + descToJson(d.getObjectValue(tid('textKey')), 0); }
        var otherKeys = [];
        for (var ki = 0; ki < d.count; ki++) { var kk = d.getKey(ki); var kn = keyName(kk); if (kn != 'textKey' && kn != 'layerEffects' && kn != 'smartObject') { otherKeys.push(q(kn) + ':' + valueToJson(d, kk, 0)); } }
        entry += ',"layerDesc":{' + otherKeys.join(',') + '}';
      } catch (e) { entry += ',"amError":' + q(e); }
      entry += '}';
      entries.push(entry);
    }
    var renderStatus = 'skipped';
    if ("$renderPng" !== '') { renderStatus = renderTo(doc, "$renderPng"); }
    var resaveStatus = 'skipped';
    if ("$resavePsd" !== '') { try { doc.saveAs(new File("$resavePsd"), new PhotoshopSaveOptions(), true, Extension.LOWERCASE); resaveStatus = 'ok'; } catch (e) { resaveStatus = 'resave-error: ' + e; } }
    var mutateStatus = 'skipped';
    if ("$mutatePsd" !== '') {
      try {
        for (var m = 0; m < textLayers.length; m++) { textLayers[m].textItem.contents = textLayers[m].textItem.contents; }
        doc.saveAs(new File("$mutatePsd"), new PhotoshopSaveOptions(), true, Extension.LOWERCASE);
        mutateStatus = renderTo(doc, "$mutatePng");
      } catch (e) { mutateStatus = 'mutate-error: ' + e; }
    }
    result = '{"ok":true,"render":' + q(renderStatus) + ',"resave":' + q(resaveStatus) + ',"mutate":' + q(mutateStatus) + ',"layers":[' + entries.join(',') + ']}';
  } catch (e) { result = '{"ok":false,"error":' + q('probe-error: ' + e) + '}'; }
  try { doc.close(SaveOptions.DONOTSAVECHANGES); } catch (e) {}
}
var out = new File("$outJson"); out.encoding = 'UTF-8'; out.open('w'); out.write(result); out.close();
note("CLOSED $js");
"@
  $started = Get-Date
  $ok = $true
  try { $app.DoJavaScript($jsx) | Out-Null } catch { $ok = $false; Add-Content -Path $log -Value ("COMERROR $js " + $_.Exception.Message) }
  $elapsed = [int]((Get-Date) - $started).TotalSeconds
  $results += [pscustomobject]@{ file = $file; ok = $ok; seconds = $elapsed }
}
Add-Content -Path $log -Value 'DONE'
Wait-Job $watcher -Timeout 10 | Out-Null
Remove-Job $watcher -Force -ErrorAction SilentlyContinue

$lines = Get-Content $log
foreach ($r in $results) {
  $js = ((Resolve-Path $r.file).Path) -replace '\\', '/'
  $dialogs = @($lines | Where-Object { $_ -like ('DIALOG OPENING ' + $js + ' ::*') } | ForEach-Object { ($_ -split ' :: ', 2)[1] })
  $errors = @($lines | Where-Object { $_ -like ('ERROR ' + $js + '*') -or $_ -like ('COMERROR ' + $js + '*') })
  $state = if ($errors.Count -gt 0) { 'ERROR' } elseif ($dialogs.Count -gt 0) { 'DIALOG' } else { 'CLEAN' }
  Write-Output ("{0} {1} ({2}s)" -f $state, $r.file, $r.seconds)
  foreach ($d in $dialogs) { Write-Output ("    dialog: " + $d) }
  foreach ($e in $errors) { Write-Output ("    " + $e) }
}
