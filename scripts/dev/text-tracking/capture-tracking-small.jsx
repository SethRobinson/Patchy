#target photoshop
// Small-tracking sweep (docs/text-render-calibration.md, "Tracking and alignment"): one "A"
// (ArialMT 30 px, Crisp, baseline shift -0.59346) centred and right-aligned in a 32 x 28.02 box,
// the psd-tools type-layer.psd case, at tracking 0 / 15 / 40 / 75 / 200 and box left x 0 to 0.9,
// plus "AV" and "TEXT" lines. Writes each PSD and its document bounds; the stored layer raster is
// Photoshop's own pixel phase. Result: a lone glyph never moves with tracking, but these fresh
// layers align as if the last glyph kerned with a following space (Arial "A" 0.83 px right of
// type-layer.psd's, which PS 2026 re-lays out without it). Creates and closes only its own
// documents. The runner
// (capture-tracking-just.ps1 -Jsx capture-tracking-small.jsx) prepends `var OUT = "<folder>/";`.
app.displayDialogs = DialogModes.NO;
var P = OUT;
var originalRulerUnits = app.preferences.rulerUnits;
var originalTypeUnits = app.preferences.typeUnits;
app.preferences.rulerUnits = Units.PIXELS;
app.preferences.typeUnits = TypeUnits.PIXELS;
var manifest = [];
var log = [];
function j(v) {
  if (v === null || v === undefined) return "null";
  if (typeof v === "number") return "" + v;
  if (typeof v === "boolean") return v ? "true" : "false";
  if (typeof v === "string") return '"' + v.replace(/\\/g, "\\\\").replace(/"/g, '\\"') + '"';
  if (v instanceof Array) { var a = []; for (var i = 0; i < v.length; i++) a.push(j(v[i])); return "[" + a.join(",") + "]"; }
  var o = []; for (var k in v) o.push('"' + k + '":' + j(v[k])); return "{" + o.join(",") + "}";
}
function readTextKey() {
  var lref = new ActionReference();
  lref.putEnumerated(charIDToTypeID("Lyr "), charIDToTypeID("Ordn"), charIDToTypeID("Trgt"));
  var tk = executeActionGet(lref).getObjectValue(stringIDToTypeID("textKey"));
  var info = {};
  function rect(o) {
    var r = tk.getObjectValue(stringIDToTypeID(o));
    return [r.getUnitDoubleValue(stringIDToTypeID("left")), r.getUnitDoubleValue(stringIDToTypeID("top")),
            r.getUnitDoubleValue(stringIDToTypeID("right")), r.getUnitDoubleValue(stringIDToTypeID("bottom"))];
  }
  try { info.bounds = rect("bounds"); } catch (e) {}
  try { info.boundingBox = rect("boundingBox"); } catch (e) {}
  return info;
}
function red() { var s = new SolidColor(); s.rgb.red = 255; s.rgb.green = 0; s.rgb.blue = 0; return s; }
function capture(c) {
  var doc = app.documents.add(c.docW, 32, 72, c.name, NewDocumentMode.RGB, DocumentFill.TRANSPARENT);
  try {
    var layer = doc.artLayers.add();
    layer.kind = LayerKind.TEXT;
    layer.name = c.name;
    var ti = layer.textItem;
    ti.kind = TextType.PARAGRAPHTEXT;
    ti.width = c.boxW;
    ti.height = 28.02;
    ti.font = "ArialMT";
    ti.size = 30;
    ti.antiAliasMethod = AntiAlias.CRISP;
    ti.color = red();
    ti.contents = c.text;
    ti.justification = c.justify;
    ti.tracking = c.tracking;
    ti.baselineShift = -0.59346;
    ti.position = [c.x, 0];
    var info = readTextKey();
    var b = layer.bounds;
    info.layerBounds = [b[0].value, b[1].value, b[2].value, b[3].value];
    info.name = c.name;
    info.text = c.text;
    info.tracking = c.tracking;
    info.justify = c.justifyName;
    info.x = c.x;
    info.boxW = c.boxW;
    manifest.push(j(info));
    var opts = new PhotoshopSaveOptions();
    opts.layers = true;
    opts.embedColorProfile = false;
    opts.maximizeCompatibility = true;
    doc.saveAs(new File(P + c.name + ".psd"), opts, true, Extension.LOWERCASE);
  } catch (e) {
    log.push(c.name + " FAILED: " + e);
  }
  doc.close(SaveOptions.DONOTSAVECHANGES);
  $.sleep(200);  // pace document creation (docs/photoshop-com.md)
}
var cases = [];
var trackings = [0, 15, 40, 75, 200];
var justs = [["center", Justification.CENTER], ["right", Justification.RIGHT]];
for (var ji = 0; ji < justs.length; ji++) {
  for (var t = 0; t < trackings.length; t++) {
    for (var xi = 0; xi < 10; xi++) {
      var x = xi / 10;
      cases.push({name: "a_" + justs[ji][0] + "_t" + trackings[t] + "_x" + xi, text: "A", docW: 40, boxW: 32, x: x,
                  justify: justs[ji][1], justifyName: justs[ji][0], tracking: trackings[t]});
    }
  }
}
var words = ["AV", "TEXT"];
for (var wi = 0; wi < words.length; wi++) {
  for (var t2 = 0; t2 < trackings.length; t2++) {
    for (var xi2 = 0; xi2 < 10; xi2 += 3) {
      cases.push({name: "w" + words[wi] + "_center_t" + trackings[t2] + "_x" + xi2, text: words[wi], docW: 140, boxW: 130,
                  x: xi2 / 10, justify: Justification.CENTER, justifyName: "center", tracking: trackings[t2]});
    }
  }
}
for (var i = 0; i < cases.length; i++) capture(cases[i]);
app.preferences.rulerUnits = originalRulerUnits;
app.preferences.typeUnits = originalTypeUnits;
var mf = new File(P + "manifest.jsonl"); mf.encoding = "UTF-8"; mf.open("w"); mf.write(manifest.join("\n") + "\n"); mf.close();
var lg = new File(P + "capture-log.txt"); lg.encoding = "UTF-8"; lg.open("w"); lg.write(log.join("\n") + "\n"); lg.close();
"done " + manifest.length + " cases, " + log.length + " log lines";
