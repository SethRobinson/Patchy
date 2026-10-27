// @name Package self-test
// @description Run against an UNPACKED release package to prove its support files work:
// @description image codecs, bundled fonts, scripts, translations, the AI kit, automatic
// @description selection and, on Windows, both legacy plug-in hosts. Throws on the first failure.
// @cli --headless --script-arg work=<scratch folder> --script-arg version=<expected>
//
// Used by scripts/release/verify-windows-package.ps1. Arguments:
//   work     scratch folder for the files this writes (required)
//   version  expected application version (optional)
//   plugins  folder holding the Filter Foundry test plug-ins (optional, Windows)
//   pdfopen  0 for a package without PDF import (Windows ARM64: no Qt PDF); PDF is then
//            only written

function check(condition, message) {
  if (!condition) throw new Error("package self-test: " + message);
}

function folderPath(value) {
  var path = String(value || "").split("\\").join("/");
  if (path && path.charAt(path.length - 1) !== "/") path += "/";
  return path;
}

var work = folderPath(patchy.args.work);
check(work, "pass --script-arg work=<scratch folder>");
check(patchy.io.makeDir(work), "cannot create " + work);

if (patchy.args.version) {
  check(app.version === patchy.args.version,
    "version is " + app.version + ", expected " + patchy.args.version);
}

// --- image codecs: every format plug-in the package ships must write and read back ---
var WIDTH = 64, HEIGHT = 48;
var source = app.newDocument(WIDTH, HEIGHT);
var paint = source.addLayer("paint");
paint.fill("#FEBA20");
paint.fillRect(8, 8, 24, 16, "#363F3B");

var formats = ["png", "jpg", "webp", "tif", "gif", "bmp", "psd", "pdf"];
var pdfOpens = String(patchy.args.pdfopen || "1") !== "0";
for (var i = 0; i < formats.length; i++) {
  var file = work + "selftest." + formats[i];
  check(source.exportAs(file), "could not write " + formats[i]);
  check(patchy.io.fileSize(file) > 0, formats[i] + " file is empty");
  if (formats[i] === "pdf" && !pdfOpens) {
    console.log("codec ok: pdf (written; this package has no PDF import)");
    continue;
  }
  var reopened = app.open(file);
  // A PDF page is sized from the document's resolution, so only its presence is checked.
  if (formats[i] !== "pdf") {
    check(reopened.width === WIDTH && reopened.height === HEIGHT,
      formats[i] + " reopened as " + reopened.width + " x " + reopened.height);
  }
  reopened.close();
  console.log("codec ok: " + formats[i]);
}

var svgFile = work + "selftest.svg";
patchy.io.writeTextFile(svgFile,
  '<svg xmlns="http://www.w3.org/2000/svg" width="40" height="30" viewBox="0 0 40 30">' +
  '<rect width="40" height="30" fill="#FEBA20"/></svg>');
var svg = app.open(svgFile);
check(svg.width === 40 && svg.height === 30, "svg opened as " + svg.width + " x " + svg.height);
svg.close();
console.log("codec ok: svg");

// --- bundled font ---------------------------------------------------------------------
var BUNDLED_FONT = "Noto Naskh Arabic";
var families = app.listFonts().map(function (font) { return font.family; });
check(families.indexOf(BUNDLED_FONT) >= 0, "bundled font is missing: " + BUNDLED_FONT);
// Arabic text on purpose: the font has no Latin letters, so Latin text would render in a
// fallback (and warn) even from a complete package.
var arabic = source.addTextLayer("سلام", {font: BUNDLED_FONT, size: 16, x: 4, y: 40});
check(arabic.textFont === BUNDLED_FONT && arabic.bounds.width > 0, "bundled font did not render");
console.log("font ok: " + BUNDLED_FONT);

// --- package layout (Windows keeps everything beside the executable) ---------------------
var pluginsFolder = folderPath(patchy.plugins.folder);
if (pluginsFolder) {
  var root = pluginsFolder.slice(0, pluginsFolder.length - "plugins/".length);
  var required = [
    "plugins/README.txt",
    "scripts/patchy.d.ts",
    "scripts/scripting-guide.md",
    "ai/patchy-control/SKILL.md",
    "tls/qschannelbackend.dll",
    "platforms/qwindows.dll",
    "platforms/qoffscreen.dll",
    "patchy-mcp.exe",
    "patchy-8bf-host32.exe",
    "patchy-8bf-host64.exe",
    "LICENSE",
    "NOTICE-THIRD-PARTY.md",
    "licenses/gmic/gmic-COPYING.txt",
    "licenses/gmic/pocketfft-LICENSE.md"
  ];
  for (var r = 0; r < required.length; r++) {
    check(patchy.io.fileExists(root + required[r]), "missing file: " + required[r]);
  }
  var scriptFolders = ["Demos", "Effects", "Games", "Utilities"];
  for (var s = 0; s < scriptFolders.length; s++) {
    check(patchy.io.listFiles(root + "scripts/" + scriptFolders[s], "*.js").length > 0,
      "no bundled scripts in scripts/" + scriptFolders[s]);
  }
  var catalogs = patchy.io.listFiles(root + "translations", "patchy_*.qm");
  check(catalogs.length > 0, "no translation catalogs");
  for (var c = 0; c < catalogs.length; c++) {
    var qtCatalog = "qtbase_" + catalogs[c].slice("patchy_".length);
    check(patchy.io.fileExists(root + "translations/" + qtCatalog), "missing " + qtCatalog);
  }
  console.log("layout ok: " + required.length + " files, " + catalogs.length + " languages");
}

// --- legacy plug-in hosts: one 32-bit and one 64-bit filter must run -------------------
if (pluginsFolder && patchy.args.plugins) {
  patchy.plugins.folders = [folderPath(patchy.args.plugins)];
  var plugins = patchy.plugins.rescan().filter(function (plugin) { return plugin.supported; });
  var seen = {};
  for (var p = 0; p < plugins.length; p++) {
    var architecture = plugins[p].architecture;
    if (seen[architecture]) continue;
    var target = source.addLayer("plug-in " + architecture);
    target.fill("#3366CC");
    target.applyPlugin(plugins[p].id, {dialog: false});
    seen[architecture] = plugins[p].name;
    console.log("plug-in host ok: " + architecture + " (" + plugins[p].name + ")");
  }
  check(Object.keys(seen).length >= 2,
    "expected a 32-bit and a 64-bit plug-in to run, ran: " + JSON.stringify(seen));
}

// --- automatic selection: ONNX Runtime and every bundled model load and run ------------
var scene = app.newDocument(96, 72);
var subject = scene.addLayer("subject");
subject.fill("#3C8F45");
subject.fillRect(30, 20, 36, 32, "#D23A2C");
check(scene.selection.automaticSelectionAvailable,
  "automatic selection is unavailable (ONNX Runtime or the ai-models folder is missing)");
scene.selection.objectSelect({point: {x: 48, y: 36}});
check(scene.selection.exists, "Object Selection selected nothing");
scene.selection.deselect();
// Select Subject ships only when PATCHY_ENABLE_SELECT_SUBJECT is on (held back pending patent
// review, docs/ai-selection.md).
if (scene.selection.subjectSelectionAvailable) {
  scene.selection.selectSubject();
  check(scene.selection.exists, "Select Subject selected nothing");
}
scene.close();
console.log("automatic selection ok");

// --- G'MIC Filter Library: the engine is linked in and its filter library loads ----------
check(patchy.gmic.available, "the G'MIC engine is missing from this build");
check(patchy.gmic.list().length > 300, "the G'MIC filter library did not load");
var gmicLayer = source.addLayer("gmic");
gmicLayer.fill("#3C8F45");
gmicLayer.applyGmic("fx_sepia");
console.log("G'MIC ok: " + patchy.gmic.version);

source.close();
console.log("package self-test passed");
