# Builds one Photoshop capture of many native Smart Filters over COM: opens the probe image,
# converts it to a Smart Object named "Source", and for every job duplicates that layer
# (all duplicates share the one embedded source), names the copy after the job and applies
# the job's filter to it with DialogModes.NO. Saves the layered result as -Out (.psd).
#
#   & scripts\dev\smart-filters\ps-probe-smart-filter-stack.ps1 -Source probe.png -Jobs jobs.jsx -Out out.psd
#
# -Jobs is a JSX file defining `var JOBS = [{name: "...", run: function () {...}}, ...]`;
# helpers: cid(s), sid(s), apply(event, descriptor), chnl(charId) (a channel reference).
# Photoshop stores the applying descriptor verbatim as the entry's Fltr object, so jobs
# pass the descriptor forms Photoshop's own dialogs record.
# Only the document this script opens is touched; it closes without saving. Open document
# names before and after are printed. Take photoshop-lock.ps1 around every run on Seth's PC.
param(
  [Parameter(Mandatory)] [string]$Source,
  [Parameter(Mandatory)] [string]$Jobs,
  [Parameter(Mandatory)] [string]$Out
)
$ErrorActionPreference = 'Stop'
$src = (Resolve-Path $Source).Path -replace '\\', '/'
$outPath = [IO.Path]::GetFullPath($Out) -replace '\\', '/'
$jobsText = Get-Content -Raw $Jobs

$jsx = @"
function cid(s) { return charIDToTypeID(s); }
function sid(s) { return stringIDToTypeID(s); }
function apply(ev, d) {
  var id = ev.length == 4 ? cid(ev) : sid(ev);
  executeAction(id, d, DialogModes.NO);
}
function chnl(c) { var r = new ActionReference(); r.putEnumerated(cid('Chnl'), cid('Chnl'), cid(c)); return r; }
function docNames() { var n = []; for (var i = 0; i < app.documents.length; i++) n.push(app.documents[i].name); return n.join('|'); }
var log = [];
var before = docNames();
var savedDialogs = app.displayDialogs;
var savedUnits = app.preferences.rulerUnits;
app.displayDialogs = DialogModes.NO;
app.preferences.rulerUnits = Units.PIXELS;
$jobsText
var doc = null;
try {
  doc = app.open(new File('$src'));
  executeAction(sid('newPlacedLayer'), undefined, DialogModes.NO);
  var base = doc.activeLayer;
  base.name = 'Source';
  for (var j = 0; j < JOBS.length; j++) {
    doc.activeLayer = base;
    var copy = base.duplicate();
    copy.name = JOBS[j].name;
    doc.activeLayer = copy;
    try { JOBS[j].run(); log.push(JOBS[j].name + ': ok'); } catch (e) { log.push(JOBS[j].name + ': ERROR ' + e); }
  }
  var po = new PhotoshopSaveOptions(); po.layers = true; po.embedColorProfile = false;
  doc.saveAs(new File('$outPath'), po, true, Extension.LOWERCASE);
} catch (e2) {
  log.push('ERROR ' + e2);
}
if (doc !== null) { try { doc.close(SaveOptions.DONOTSAVECHANGES); } catch (e3) { log.push('close failed ' + e3); } }
app.preferences.rulerUnits = savedUnits;
app.displayDialogs = savedDialogs;
'before=' + before + '\nafter=' + docNames() + '\n' + log.join('\n');
"@
$ps = New-Object -ComObject Photoshop.Application
$ps.DoJavaScript($jsx)
