# Photoshop acceptance check for script layer moves of text (docs/scripting-engine.md, "A
# script move is a Move tool move"). A text layer made with doc.addTextLayer and moved with
# layer.moveTo or the x / y setters must stay where the script put it when Photoshop lays
# the text out again. Photoshop reads the anchor from the text transform, so a move that
# shifts only the pixels looks right on open and jumps back to the creation anchor at the
# first edit.
#
#   & scripts\dev\photoshop-text-move-check.ps1 [-Patchy build\release\patchy.exe] [-OutDir <dir>] [-Tolerance 3]
#
# -Patchy defaults to the release build, which only releases refresh: pass
# build\opt-dev\patchy.exe to check a development build.
#
# The script writes the PSD with `patchy --headless --run-script` (isolated settings, so
# the run leaves no recent-file entries), opens it in Photoshop over COM, records every
# text layer's bounds, appends a period to its contents (which forces the re-layout), and
# compares the top-left corner against an unmoved control layer's. It prints
# "STAYED <layer>" or "JUMPED <layer>" with the offsets and exits 1 when a layer moved
# more than -Tolerance pixels further than the control did. Driving Photoshop
# over COM is always authorized (docs/photoshop-com.md); only the document this script
# opened is closed, without saving.
param(
  [string]$Patchy = '',
  [string]$OutDir = '',
  [int]$Tolerance = 3
)
$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if ($Patchy -eq '') { $Patchy = Join-Path $repo 'build\release\patchy.exe' }
if ($OutDir -eq '') { $OutDir = Join-Path $repo 'build\test-output\text-move-check' }
$Patchy = (Resolve-Path $Patchy).Path
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$psd = Join-Path $OutDir 'moved-text.psd'
$scriptPath = Join-Path $OutDir 'make-moved-text.js'
$scriptLog = Join-Path $OutDir 'make-moved-text.log'
Remove-Item $psd, $scriptLog -ErrorAction SilentlyContinue

# Every layer is created near the top-left corner and moved far away, so a layer that
# falls back to its creation anchor is unmistakable. "Control" never moves.
$makeJs = @'
var doc = app.newDocument(1600, 1000);
doc.activeLayer.fill('#ffffff');
var control = doc.addTextLayer('Control', {font: 'Arial', size: 48, x: 60, y: 60, color: '#202020'});
var point = doc.addTextLayer('Moved point', {font: 'Arial', size: 48, x: 60, y: 160, color: '#202020'});
point.moveTo(point.x + 900, point.y + 600);
var setter = doc.addTextLayer('Moved setter', {font: 'Arial', size: 48, x: 60, y: 260, color: '#202020'});
setter.x = setter.x + 700;
setter.y = setter.y + 250;
var box = doc.addTextLayer('Moved box text wraps', {font: 'Arial', size: 36, x: 60, y: 360, color: '#202020',
                                                   box: {width: 260, height: 160}});
box.moveTo(box.x + 1100, box.y - 200);
var inGroup = doc.addTextLayer('Moved group', {font: 'Arial', size: 48, x: 60, y: 560, color: '#202020'});
var group = doc.groupLayers([inGroup], 'Group');
group.moveTo(group.x + 500, group.y + 300);
if (!doc.saveAs(patchy.args.out)) throw new Error('save failed');
console.log('saved ' + patchy.args.out);
'@
Set-Content -LiteralPath $scriptPath -Value $makeJs -Encoding UTF8

$previousSettings = $env:PATCHY_SETTINGS_DIR
$env:PATCHY_SETTINGS_DIR = Join-Path $OutDir 'settings'
try {
  $arguments = @('--headless', '--run-script', $scriptPath, '--script-output', $scriptLog,
                 '--script-arg', ('out=' + ($psd -replace '\\', '/')))
  $process = Start-Process -FilePath $Patchy -ArgumentList $arguments -Wait -PassThru -NoNewWindow
  if ($process.ExitCode -ne 0 -or -not (Test-Path $psd)) {
    if (Test-Path $scriptLog) { Get-Content $scriptLog | Write-Output }
    throw "Patchy did not write $psd (exit $($process.ExitCode))"
  }
} finally {
  $env:PATCHY_SETTINGS_DIR = $previousSettings
}

$psdJs = $psd -replace '\\', '/'
$jsx = @"
app.displayDialogs = DialogModes.NO;
var savedUnits = app.preferences.rulerUnits;
app.preferences.rulerUnits = Units.PIXELS;
var lines = [];
var d = app.open(new File("$psdJs"));
function corner(layer) { var b = layer.bounds; return [Math.round(b[0].value), Math.round(b[1].value)]; }
function walk(layers) {
  for (var i = 0; i < layers.length; i++) {
    var layer = layers[i];
    if (layer.typename === "LayerSet") { walk(layer.layers); continue; }
    if (layer.kind !== LayerKind.TEXT) { continue; }
    var before = corner(layer);
    try {
      layer.textItem.contents = layer.textItem.contents + ".";
      var after = corner(layer);
      lines.push(layer.name + "\t" + before[0] + "," + before[1] + "\t" + after[0] + "," + after[1]);
    } catch (e) {
      lines.push(layer.name + "\t" + before[0] + "," + before[1] + "\tERROR " + e);
    }
  }
}
try { walk(d.layers); } finally {
  d.close(SaveOptions.DONOTSAVECHANGES);
  app.preferences.rulerUnits = savedUnits;
}
lines.join("\n");
"@
$app = New-Object -ComObject Photoshop.Application
$result = [string]$app.DoJavaScript($jsx)

$failed = $false
$rows = @()
foreach ($line in ($result -split "`n")) {
  $fields = $line.Trim() -split "`t"
  if ($fields.Count -lt 3) { continue }
  if ($fields[2] -like 'ERROR*') {
    Write-Output ("ERROR  {0}: {1}" -f $fields[0], $fields[2])
    $failed = $true
    continue
  }
  $before = $fields[1] -split ','
  $after = $fields[2] -split ','
  $rows += [pscustomobject]@{
    Name = $fields[0]; Before = $fields[1]; After = $fields[2]
    Dx = [int]$after[0] - [int]$before[0]; Dy = [int]$after[1] - [int]$before[1]
  }
}
# The corner Photoshop reports on open is Patchy's raster rect, and after the re-layout
# it is Photoshop's own ink rect, so every layer shifts by the same few pixels of raster
# padding. The unmoved Control layer measures that shift; the moved layers must match it.
$control = $rows | Where-Object { $_.Name -eq 'Control' } | Select-Object -First 1
if (-not $control) {
  Write-Output 'Photoshop reported no Control text layer'
  exit 1
}
foreach ($row in $rows) {
  $dx = $row.Dx - $control.Dx
  $dy = $row.Dy - $control.Dy
  $moved = ([Math]::Abs($dx) -gt $Tolerance) -or ([Math]::Abs($dy) -gt $Tolerance)
  if ($moved) { $failed = $true }
  Write-Output ("{0} {1}: opened at {2}, re-laid out at {3} (dx {4}, dy {5} against the control)" -f `
    $(if ($moved) { 'JUMPED' } else { 'STAYED' }), $row.Name, $row.Before, $row.After, $dx, $dy)
}
if ($rows.Count -lt 5) {
  Write-Output "Expected 5 text layers, Photoshop reported $($rows.Count)"
  $failed = $true
}
if ($failed) { exit 1 }
exit 0
