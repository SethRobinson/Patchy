#target photoshop
// Tracking x justification sweep (docs/text-render-calibration.md, "Tracking and alignment"):
// where Photoshop puts the raster of tracked point and box text, Arial 48 px, Sharp, for left /
// center / right justification at tracking -80, 0, +75 and +200, plus box-text first-baseline
// probes with a negative baseline shift and fixed leading (the effect-stroke-gradient and
// adjustment-fillers layers). Creates and closes only its own documents. The runner
// (capture-tracking-just.ps1) prepends `var OUT = "<folder>/";`.
app.displayDialogs = DialogModes.NO;
var P = OUT;
var originalRulerUnits = app.preferences.rulerUnits;
var originalTypeUnits = app.preferences.typeUnits;
app.preferences.rulerUnits = Units.PIXELS;
app.preferences.typeUnits = TypeUnits.PIXELS;
var manifest = [];
var log = [];
function getPrefs(cls) {
  var r = new ActionReference();
  r.putProperty(charIDToTypeID("Prpr"), stringIDToTypeID(cls));
  r.putEnumerated(charIDToTypeID("capp"), charIDToTypeID("Ordn"), charIDToTypeID("Trgt"));
  return executeActionGet(r).getObjectValue(stringIDToTypeID(cls));
}
function setMax(value) {
  var d = new ActionDescriptor();
  var r = new ActionReference();
  r.putProperty(charIDToTypeID("Prpr"), stringIDToTypeID("fileSavePrefs"));
  r.putEnumerated(charIDToTypeID("capp"), charIDToTypeID("Ordn"), charIDToTypeID("Trgt"));
  d.putReference(charIDToTypeID("null"), r);
  var v = new ActionDescriptor();
  v.putEnumerated(stringIDToTypeID("maximizeCompatibility"), stringIDToTypeID("maximizeCompatibility"), stringIDToTypeID(value));
  d.putObject(charIDToTypeID("T   "), stringIDToTypeID("fileSavePrefs"), v);
  executeAction(charIDToTypeID("setd"), d, DialogModes.NO);
}
var beforeMax = null;
try { beforeMax = typeIDToStringID(getPrefs("fileSavePrefs").getEnumerationValue(stringIDToTypeID("maximizeCompatibility"))); } catch (e) {}
function readTextKey() {
  var lref = new ActionReference();
  lref.putEnumerated(charIDToTypeID("Lyr "), charIDToTypeID("Ordn"), charIDToTypeID("Trgt"));
  var ld = executeActionGet(lref);
  var tk = ld.getObjectValue(stringIDToTypeID("textKey"));
  var info = {};
  function rect(o) {
    var r = tk.getObjectValue(stringIDToTypeID(o));
    return [r.getUnitDoubleValue(stringIDToTypeID("left")), r.getUnitDoubleValue(stringIDToTypeID("top")),
            r.getUnitDoubleValue(stringIDToTypeID("right")), r.getUnitDoubleValue(stringIDToTypeID("bottom"))];
  }
  try { info.bounds = rect("bounds"); } catch (e) {}
  try { info.boundingBox = rect("boundingBox"); } catch (e) {}
  try {
    var shape = tk.getList(stringIDToTypeID("textShape")).getObjectValue(0);
    var tr = shape.getObjectValue(stringIDToTypeID("transform"));
    info.transform = [tr.getDouble(stringIDToTypeID("xx")), tr.getDouble(stringIDToTypeID("xy")), tr.getDouble(stringIDToTypeID("yx")),
                      tr.getDouble(stringIDToTypeID("yy")), tr.getDouble(stringIDToTypeID("tx")), tr.getDouble(stringIDToTypeID("ty"))];
  } catch (e) {}
  return info;
}
function j(v) {
  if (v === null || v === undefined) return "null";
  if (typeof v === "number") return "" + v;
  if (typeof v === "boolean") return v ? "true" : "false";
  if (typeof v === "string") return '"' + v.replace(/\\/g, "\\\\").replace(/"/g, '\\"') + '"';
  if (v instanceof Array) { var a = []; for (var i = 0; i < v.length; i++) a.push(j(v[i])); return "[" + a.join(",") + "]"; }
  var o = []; for (var k in v) o.push('"' + k + '":' + j(v[k])); return "{" + o.join(",") + "}";
}
function black() { var s = new SolidColor(); s.rgb.red = 0; s.rgb.green = 0; s.rgb.blue = 0; return s; }
function capture(c) {
  var doc = app.documents.add(c.docW || 400, c.docH || 200, 72, c.name, NewDocumentMode.RGB, DocumentFill.WHITE);
  try {
    var layer = doc.artLayers.add();
    layer.kind = LayerKind.TEXT;
    layer.name = c.name;
    var ti = layer.textItem;
    if (c.box) {
      ti.kind = TextType.PARAGRAPHTEXT;
      ti.width = c.box[2];
      ti.height = c.box[3];
    }
    ti.font = c.font;
    ti.size = c.size;
    ti.antiAliasMethod = AntiAlias.SHARP;
    ti.color = black();
    ti.contents = c.text;
    ti.justification = c.justify;
    ti.tracking = c.tracking || 0;
    if (c.leading) { ti.useAutoLeading = false; ti.leading = c.leading; }
    if (c.shift) ti.baselineShift = c.shift;
    ti.position = c.box ? [c.box[0], c.box[1]] : c.position;
    var info = readTextKey();
    var b = layer.bounds;
    info.layerBounds = [b[0].value, b[1].value, b[2].value, b[3].value];
    info.name = c.name;
    info.text = c.text;
    info.tracking = c.tracking || 0;
    info.justify = c.justifyName;
    if (c.box) info.box = c.box; else info.position = c.position;
    if (c.leading) info.leading = c.leading;
    if (c.shift) info.shift = c.shift;
    manifest.push(j(info));
    setMax("queryAlways");
    var opts = new PhotoshopSaveOptions();
    opts.layers = true;
    opts.embedColorProfile = false;
    opts.maximizeCompatibility = true;
    doc.saveAs(new File(P + c.name + ".psd"), opts, true, Extension.LOWERCASE);
  } catch (e) {
    log.push(c.name + " FAILED: " + e);
  }
  doc.close(SaveOptions.DONOTSAVECHANGES);
  $.sleep(300);  // pace document creation (docs/photoshop-com.md)
}
var justs = [["left", Justification.LEFT], ["center", Justification.CENTER], ["right", Justification.RIGHT]];
var trackings = [-80, 0, 75, 200];
var cases = [];
for (var ji = 0; ji < justs.length; ji++) {
  for (var t = 0; t < trackings.length; t++) {
    var tag = (trackings[t] < 0 ? "m" : "p") + Math.abs(trackings[t]);
    cases.push({name: "point_" + justs[ji][0] + "_t" + tag, font: "ArialMT", size: 48, text: "TEXT",
                justify: justs[ji][1], justifyName: justs[ji][0], tracking: trackings[t], position: [200, 100]});
    cases.push({name: "box_" + justs[ji][0] + "_t" + tag, font: "ArialMT", size: 48, text: "TEXT",
                justify: justs[ji][1], justifyName: justs[ji][0], tracking: trackings[t], box: [50, 40, 300, 100]});
  }
}
// First-baseline probes: box text, Arial Bold 48 px, fixed leading 42.3, baseline shift 0 / -8
// (effect-stroke-gradient's layers), and ArialMT 43.75 tracking +75 with shift 0 / -4.3
// (adjustment-fillers' "TEXT"). Box tops at whole and fractional y.
var shifts = [0, -8];
var tops = [40, 40.4];
for (var si = 0; si < shifts.length; si++) {
  for (var ti2 = 0; ti2 < tops.length; ti2++) {
    var stag = "s" + (shifts[si] < 0 ? "m" : "p") + Math.abs(shifts[si]) + "_y" + ("" + tops[ti2]).replace(".", "");
    cases.push({name: "shift_bold_center_tm80_" + stag, font: "Arial-BoldMT", size: 48, text: "Text 1",
                justify: Justification.CENTER, justifyName: "center", tracking: -80, leading: 42.3, shift: shifts[si],
                box: [20, tops[ti2], 300, 100]});
    cases.push({name: "shift_bold_left_t0_" + stag, font: "Arial-BoldMT", size: 48, text: "Text 1",
                justify: Justification.LEFT, justifyName: "left", tracking: 0, leading: 42.3, shift: shifts[si],
                box: [20, tops[ti2], 300, 100]});
  }
}
var shifts2 = [0, -4.3];
for (var s2 = 0; s2 < shifts2.length; s2++) {
  cases.push({name: "shift_arial_center_tp75_s" + (shifts2[s2] < 0 ? "m43" : "p0"), font: "ArialMT", size: 43.75,
              text: "TEXT", justify: Justification.CENTER, justifyName: "center", tracking: 75, shift: shifts2[s2],
              box: [50, 40, 300, 100]});
}
for (var i = 0; i < cases.length; i++) capture(cases[i]);
if (beforeMax) { try { setMax(beforeMax); } catch (e) {} }
app.preferences.rulerUnits = originalRulerUnits;
app.preferences.typeUnits = originalTypeUnits;
var mf = new File(P + "manifest.jsonl"); mf.encoding = "UTF-8"; mf.open("w"); mf.write(manifest.join("\n") + "\n"); mf.close();
var lg = new File(P + "capture-log.txt"); lg.encoding = "UTF-8"; lg.open("w"); lg.write(log.join("\n") + "\n"); lg.close();
"done " + manifest.length + " cases, " + log.length + " log lines";
