#target photoshop
// Box-text first baseline sweep (docs/text-render-calibration.md, "Tracking and alignment"):
// paragraph text "HEH" (flat tops and bottoms, Sharp) at fractional box tops and several sizes
// and faces. Photoshop rasterizes the first baseline on a whole pixel; the layer's ink bottom is
// that baseline. Writes manifest-baseline.jsonl only; creates and closes its own documents.
// The runner prepends `var OUT = "<folder>/";`.
app.displayDialogs = DialogModes.NO;
var originalRulerUnits = app.preferences.rulerUnits;
var originalTypeUnits = app.preferences.typeUnits;
app.preferences.rulerUnits = Units.PIXELS;
app.preferences.typeUnits = TypeUnits.PIXELS;
var count = 0;
var mf = new File(OUT + "manifest-baseline.jsonl");
mf.encoding = "UTF-8";
mf.open("w");
mf.close();
// Each row is appended as it is measured, so a stopped run keeps what it measured.
function emit(row) {
  mf.open("a");
  mf.write(row + "\n");
  mf.close();
  count++;
}
function black() { var s = new SolidColor(); s.rgb.red = 0; s.rgb.green = 0; s.rgb.blue = 0; return s; }
function probe(font, size, top) {
  var doc = app.documents.add(400, 200, 72, "baseline", NewDocumentMode.RGB, DocumentFill.WHITE);
  try {
    var layer = doc.artLayers.add();
    layer.kind = LayerKind.TEXT;
    var ti = layer.textItem;
    ti.kind = TextType.PARAGRAPHTEXT;
    ti.width = 300;
    ti.height = 120;
    ti.font = font;
    ti.size = size;
    ti.antiAliasMethod = AntiAlias.SHARP;
    ti.color = black();
    ti.contents = "HEH";
    ti.position = [50, top];
    var b = layer.bounds;
    emit('{"font":"' + font + '","size":' + size + ',"top":' + top + ',"ink":[' + b[0].value + ',' + b[1].value + ',' +
         b[2].value + ',' + b[3].value + ']}');
  } catch (e) {
    emit('{"font":"' + font + '","size":' + size + ',"top":' + top + ',"error":"' + e + '"}');
  }
  doc.close(SaveOptions.DONOTSAVECHANGES);
  // A burst of hundreds of new documents overflowed Photoshop 2026's UXP document queue and
  // ended the process (October 2026): keep the sweep small and paced.
  $.sleep(400);
}
var cases = [];
var tops48 = [40, 40.05, 40.1, 40.15, 40.3, 40.6, 40.9];
for (var t = 0; t < tops48.length; t++) cases.push(["ArialMT", 48, tops48[t]]);
cases.push(["ArialMT", 43.75, 40], ["ArialMT", 43.75, 40.2], ["ArialMT", 30, 40], ["ArialMT", 30, 40.4],
           ["ArialMT", 72, 40], ["ArialMT", 72, 40.5], ["TimesNewRomanPSMT", 48, 40], ["TimesNewRomanPSMT", 48, 40.4],
           ["Verdana", 48, 40], ["Verdana", 48, 40.4]);
// The runner may prepend `var CASES = [[font, size, top], ...];` to measure other faces.
if (typeof CASES != "undefined") cases = CASES;
for (var i = 0; i < cases.length; i++) probe(cases[i][0], cases[i][1], cases[i][2]);
app.preferences.rulerUnits = originalRulerUnits;
app.preferences.typeUnits = originalTypeUnits;
"done " + count;
