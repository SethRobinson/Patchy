# Applies native Smart Filters to a probe image in Photoshop over COM and saves,
# per job, the layered PSD (the stored filterFX descriptor and FEid cache) and
# Photoshop's composite as PNG (the render to calibrate against).
#
#   & scripts\dev\smart-filters\ps-probe-filters.ps1 -Source probe.png -Jobs jobs.jsx -OutDir out
#
# -Jobs is a JSX file defining `var JOBS = [{name: "...", run: function () {...}}, ...]`;
# each run() applies its filter to the active (already converted) Smart Object
# layer with executeAction(..., DialogModes.NO). Helpers available to jobs:
# cid(s), sid(s), applyFilter(eventCharOrString, descriptorOrNull).
# Only documents this script opens are touched; each closes without saving. The
# open document names before and after are printed so callers can report them.
# Foreground/background colors are set to black/white for the run and restored.
# With -CanvasWidth/-CanvasHeight the job document is a new transparent canvas
# of that size with the source PLACED (embedded, centered) instead, so the
# Smart Object is smaller or larger than the document.
# With -Combined NAME every job instead runs on its own duplicate of one Smart
# Object (layers sharing a single embedded source, named after the jobs, above
# the unfiltered "Source" instance) and one NAME.psd is saved: the compact form
# committed as a test fixture.
param(
  [Parameter(Mandatory)] [string]$Source,
  [Parameter(Mandatory)] [string]$Jobs,
  [Parameter(Mandatory)] [string]$OutDir,
  [string]$Foreground = '000000',
  [string]$Background = 'FFFFFF',
  [int]$CanvasWidth = 0,
  [int]$CanvasHeight = 0,
  [string]$Combined = ''
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$src = (Resolve-Path $Source).Path -replace '\\', '/'
$out = (Resolve-Path $OutDir).Path -replace '\\', '/'
$jobsText = Get-Content -Raw $Jobs

$jsx = @"
function cid(s) { return charIDToTypeID(s); }
function sid(s) { return stringIDToTypeID(s); }
function applyFilter(ev, d) {
  var id = ev.length == 4 ? cid(ev) : sid(ev);
  executeAction(id, d === null ? undefined : d, DialogModes.NO);
}
function docNames() { var n = []; for (var i = 0; i < app.documents.length; i++) n.push(app.documents[i].name); return n.join('|'); }
var log = [];
var before = docNames();
var savedDialogs = app.displayDialogs;
var savedUnits = app.preferences.rulerUnits;
var savedFg = app.foregroundColor; var savedBg = app.backgroundColor;
app.displayDialogs = DialogModes.NO;
app.preferences.rulerUnits = Units.PIXELS;
var fg = new SolidColor(); fg.rgb.hexValue = '$Foreground'; app.foregroundColor = fg;
var bg = new SolidColor(); bg.rgb.hexValue = '$Background'; app.backgroundColor = bg;
$jobsText
var combined = '$Combined';
if (combined !== '') {
  var cdoc = null;
  try {
    cdoc = app.open(new File('$src'));
    executeAction(sid('newPlacedLayer'), undefined, DialogModes.NO);
    var base = cdoc.activeLayer;
    base.name = 'Source';
    for (var cj = 0; cj < JOBS.length; cj++) {
      cdoc.activeLayer = base;
      var copy = base.duplicate();
      copy.name = JOBS[cj].name;
      cdoc.activeLayer = copy;
      try { JOBS[cj].run(); log.push(JOBS[cj].name + ': ok'); } catch (ce) { log.push(JOBS[cj].name + ': ERROR ' + ce); }
    }
    var cpo = new PhotoshopSaveOptions(); cpo.layers = true; cpo.embedColorProfile = false;
    cdoc.saveAs(new File('$out/' + combined + '.psd'), cpo, true, Extension.LOWERCASE);
  } catch (e) {
    log.push('combined: ERROR ' + e);
  }
  if (cdoc !== null) { try { cdoc.close(SaveOptions.DONOTSAVECHANGES); } catch (e3) { log.push('close failed ' + e3); } }
  JOBS = [];
}
for (var j = 0; j < JOBS.length; j++) {
  var job = JOBS[j];
  var doc = null;
  try {
    if ($CanvasWidth > 0) {
      doc = app.documents.add($CanvasWidth, $CanvasHeight, 72, 'probe', NewDocumentMode.RGB, DocumentFill.TRANSPARENT);
      var pd = new ActionDescriptor();
      pd.putPath(cid('null'), new File('$src'));
      pd.putEnumerated(cid('FTcs'), cid('QCSt'), cid('Qcsa'));
      executeAction(cid('Plc '), pd, DialogModes.NO);
    } else {
      doc = app.open(new File('$src'));
      executeAction(sid('newPlacedLayer'), undefined, DialogModes.NO);
    }
    job.run();
    var po = new PhotoshopSaveOptions(); po.layers = true; po.embedColorProfile = false;
    doc.saveAs(new File('$out/' + job.name + '.psd'), po, true, Extension.LOWERCASE);
    var png = new PNGSaveOptions();
    doc.saveAs(new File('$out/' + job.name + '.png'), png, true, Extension.LOWERCASE);
    log.push(job.name + ': ok');
  } catch (e) {
    log.push(job.name + ': ERROR ' + e);
  }
  if (doc !== null) { try { doc.close(SaveOptions.DONOTSAVECHANGES); } catch (e2) { log.push('close failed ' + e2); } }
}
app.foregroundColor = savedFg; app.backgroundColor = savedBg;
app.preferences.rulerUnits = savedUnits;
app.displayDialogs = savedDialogs;
'before=' + before + '\nafter=' + docNames() + '\n' + log.join('\n');
"@
$ps = New-Object -ComObject Photoshop.Application
$ps.DoJavaScript($jsx)
