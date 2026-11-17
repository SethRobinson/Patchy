// @name Text recognition models check
// @description A package check for the downloadable PP-OCR engine (docs/ocr-engines.md):
// @description the engine is in the build, and with its models in the downloaded models
// @description folder it reads text this script draws. Throws on the first failure.
//
// Run with --headless --run-script against a built or installed Patchy, with
// PATCHY_DOWNLOADED_MODELS_DIR pointing at a folder that holds the pinned model files (a
// build host has them in .deps/ai-models after scripts/setup-ai-deps), so the user's own
// folder is neither read nor written. Nothing is downloaded: no script call ever does.
//
//   --script-arg models=absent   point the variable at an empty folder instead: the engine
//                                must be listed as a download that is not there yet.

function check(condition, message) {
  if (!condition) throw new Error("text recognition models check: " + message);
}

function squeezed(text) {
  return String(text).replace(/\s+/g, "").toLowerCase();
}

var listed = patchy.ocr.engines();
var engine = null;
for (var i = 0; i < listed.length; i++) {
  if (listed[i].id === "ppocr") engine = listed[i];
}
check(engine !== null, "the PP-OCR engine is not listed: this build cannot run the models (engines: " +
  listed.map(function (e) { return e.id; }).join(", ") + ")");
check(engine.languages.length >= 15, "only " + engine.languages.length + " languages are listed");

if (patchy.args.models === "absent") {
  check(!engine.available, "the engine reports models in a folder that should be empty");
  check(/download/i.test(engine.unavailableReason),
    "the reason does not say the models are a download: " + engine.unavailableReason);
  console.log("text recognition engine ok: listed, models not downloaded (" + engine.unavailableReason + ")");
} else {
  check(engine.available, "PP-OCR cannot run: " + engine.unavailableReason);
  var lines = ["Patchy reads this line 2468", "after the models download"];
  var doc = app.newDocument(1100, 320, {name: "text recognition check"});
  doc.activeLayer.fillRect(0, 0, 1100, 320, "#ffffff");
  doc.addTextLayer(lines[0], {size: 48, x: 40, y: 50, color: "#000000"});
  doc.addTextLayer(lines[1], {size: 48, x: 40, y: 180, color: "#000000"});
  var read = doc.recognizeText({engine: "ppocr", language: "en"});
  check(read.engine === "ppocr" && read.language === "en", "ran " + read.engine + " " + read.language);
  check(read.lines.length === 2, "read " + read.lines.length + " lines, expected 2: " + JSON.stringify(read.text));
  check(squeezed(read.text) === squeezed(lines.join("")),
    "read " + JSON.stringify(read.text) + ", expected " + JSON.stringify(lines.join("\n")));
  // A second language runs its own recognizer file.
  var korean = doc.recognizeText({engine: "ppocr", language: "ko"});
  check(korean.language === "ko", "the Korean recognizer did not run");
  doc.close();
  console.log("text recognition models ok: " + JSON.stringify(read.text));
}
