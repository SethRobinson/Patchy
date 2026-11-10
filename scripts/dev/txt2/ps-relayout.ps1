# Makes Photoshop lay the type layers of each file out again and saves what it draws
# (docs/txt2-kept-settings.md, "Line composer"). Per file: <name>-stored.png (the pixels as
# opened), then, after the re-layout, <name>-relaid-<mode>.png and <name>-relaid-<mode>.psd, and
# relay-<mode>.log with each layer's paragraphs before and after (textEveryLineComposer and
# hyphenate as the layer states them, textWarningLevel, oldText, bounds) and the open documents
# before and after the run. Top-level type layers only.
#   powershell -Command "& scripts\dev\txt2\ps-relayout.ps1 -Files a.psd, b.psd -OutDir <dir>"
# -Mode style (default) sets each layer's tracking to 20 and back to 0 through the textStyle
# property: Photoshop composes the text again and the paragraph style is not touched. Use it for
# reference renders. -Mode textkey writes the layer's own textKey back, which turns Adobe
# Every-line Composer on for a paragraph that does not state its composer: only for showing that.
# -Mode none only saves the stored render.
param([Parameter(Mandatory = $true)][string[]]$Files, [Parameter(Mandatory = $true)][string]$OutDir, [string]$Mode = 'style')
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
var MODE = "$Mode";
var log = [];
function tid(s) { return stringIDToTypeID(s); }
function openNames() { var n = []; for (var i = 0; i < app.documents.length; i++) { n.push(app.documents[i].name); } return n.join(' | '); }
log.push('open before: ' + openNames());
function layerDesc(id) { var ref = new ActionReference(); ref.putIdentifier(charIDToTypeID('Lyr '), id); return executeActionGet(ref); }
function paragraphInfo(id) {
  var out = [];
  var ld = layerDesc(id);
  var d = ld.getObjectValue(tid('textKey'));
  var ranges = d.getList(tid('paragraphStyleRange'));
  for (var r = 0; r < ranges.count; r++) {
    var ps = ranges.getObjectValue(r).getObjectValue(tid('paragraphStyle'));
    var one = 'keys=' + ps.count;
    one += ps.hasKey(tid('textEveryLineComposer')) ? ' everyLine=' + ps.getBoolean(tid('textEveryLineComposer')) : ' everyLine=(absent)';
    one += ps.hasKey(tid('hyphenate')) ? ' hyph=' + ps.getBoolean(tid('hyphenate')) : ' hyph=(absent)';
    out.push(one);
  }
  var w = '';
  try { w = ' warn=' + ld.getInteger(tid('textWarningLevel')); } catch (e4) {}
  try { w += d.hasKey(tid('oldText')) ? ' oldText' : ''; } catch (e5) {}
  var b = ld.getObjectValue(tid('bounds'));
  w += ' bounds=' + [b.getUnitDoubleValue(tid('left')), b.getUnitDoubleValue(tid('top')), b.getUnitDoubleValue(tid('right')), b.getUnitDoubleValue(tid('bottom'))].join(',');
  return out.join(' ; ') + w;
}
function png(doc, base) {
  var dup = doc.duplicate();
  dup.flatten();
  var o = new PNGSaveOptions();
  dup.saveAs(new File(OUT + '/' + base + '.png'), o, true, Extension.LOWERCASE);
  dup.close(SaveOptions.DONOTSAVECHANGES);
  app.activeDocument = doc;
}
function relayStyle() { relayTracking(20); relayTracking(0); }
function relayTracking(amount) {
  var ref = new ActionReference();
  ref.putProperty(tid('property'), tid('textStyle'));
  ref.putEnumerated(tid('textLayer'), tid('ordinal'), tid('targetEnum'));
  var desc = new ActionDescriptor(); desc.putReference(tid('null'), ref);
  var ts = new ActionDescriptor();
  ts.putInteger(tid('tracking'), amount);
  desc.putObject(tid('to'), tid('textStyle'), ts);
  executeAction(tid('set'), desc, DialogModes.NO);
}
function relayTextKey() {
  var ref = new ActionReference();
  ref.putEnumerated(charIDToTypeID('Lyr '), charIDToTypeID('Ordn'), charIDToTypeID('Trgt'));
  var textKey = executeActionGet(ref).getObjectValue(tid('textKey'));
  var target = new ActionReference();
  target.putEnumerated(tid('textLayer'), charIDToTypeID('Ordn'), charIDToTypeID('Trgt'));
  var set = new ActionDescriptor();
  set.putReference(charIDToTypeID('null'), target);
  set.putObject(charIDToTypeID('T   '), tid('textLayer'), textKey);
  executeAction(charIDToTypeID('setd'), set, DialogModes.NO);
}
try {
  for (var f = 0; f < FILES.length; f++) {
    var doc = app.open(new File(FILES[f]));
    var tag = doc.name.replace(/[.]psd`$/i, '');
    png(doc, tag + '-stored');
    if (MODE != 'none') {
      for (var i = 0; i < doc.artLayers.length; i++) {
        var L = doc.artLayers[i];
        if (L.kind != LayerKind.TEXT) { continue; }
        doc.activeLayer = L;
        var nm = L.name.substr(0, 16);
        log.push(tag + ' [' + nm + '] before: ' + paragraphInfo(L.id));
        try {
          if (MODE == 'textkey') { relayTextKey(); } else { relayStyle(); }
          log.push(tag + ' [' + nm + '] after:  ' + paragraphInfo(L.id));
        } catch (e) { log.push(tag + ' [' + nm + '] threw ' + e); }
      }
      png(doc, tag + '-relaid-' + MODE);
      var o = new PhotoshopSaveOptions(); o.layers = true; o.embedColorProfile = true;
      doc.saveAs(new File(OUT + '/' + tag + '-relaid-' + MODE + '.psd'), o, true, Extension.LOWERCASE);
    }
    doc.close(SaveOptions.DONOTSAVECHANGES);
    `$.sleep(300);
  }
} catch (e) { log.push('FAILED ' + e + ' line ' + e.line); }
log.push('open after: ' + openNames());
var lf = new File(OUT + '/relay-' + MODE + '.log'); lf.encoding = 'UTF-8'; lf.open('w'); lf.write(log.join('\n')); lf.close();
"@
$app.DoJavaScript($jsx) | Out-Null
Get-Content (Join-Path $OutDir "relay-$Mode.log")
