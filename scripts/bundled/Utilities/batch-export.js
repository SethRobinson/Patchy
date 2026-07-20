// Batch Export
// Converts a whole folder of images: opens every file matching the pattern,
// optionally scales each one down to fit a maximum size, and exports it into
// the output folder in the chosen format. Interactive runs pick the folders
// and options in dialogs; unattended runs (patchy --run-script) pass them as
// --script-arg folder=... --script-arg out=... [--script-arg pattern=*.png]
// [--script-arg format=jpg] [--script-arg maxSize=1024].

var folder = patchy.args.folder || app.chooseFolder("Choose the folder of images to convert");
var outFolder = folder ? (patchy.args.out || app.chooseFolder("Choose the output folder")) : "";
if (!folder || !outFolder) {
  app.alert("Batch export cancelled: both a source and an output folder are needed.");
} else {
  var options = patchy.ui.showDialog({
    title: "Batch Export",
    fields: [
      { key: "pattern", label: "Files matching", type: "text",
        value: patchy.args.pattern || "*.png" },
      { key: "format", label: "Convert to", type: "choice",
        value: patchy.args.format || "jpg", choices: ["png", "jpg", "bmp", "tif"] },
      { key: "maxSize", label: "Fit within (px, 0 = keep size)", type: "number",
        value: Number(patchy.args.maxSize || 0), min: 0, max: 16384 }
    ]
  });
  if (options) {
    var files = patchy.io.listFiles(folder, options.pattern);
    if (files.length === 0) {
      app.alert("No files in " + folder + " match " + options.pattern + ".");
    }
    var exported = 0;
    var failed = 0;
    for (var i = 0; i < files.length; i++) {
      var name = files[i];
      try {
        var doc = app.open(folder + "/" + name);
        if (options.maxSize > 0 && (doc.width > options.maxSize || doc.height > options.maxSize)) {
          var scale = options.maxSize / Math.max(doc.width, doc.height);
          doc.resizeImage(Math.max(1, Math.round(doc.width * scale)),
                          Math.max(1, Math.round(doc.height * scale)));
        }
        var base = name.replace(/\.[^.]+$/, "");
        var target = outFolder + "/" + base + "." + options.format;
        if (doc.exportAs(target)) {
          console.log("Exported " + target);
          exported++;
        } else {
          console.warn("Could not export " + target);
          failed++;
        }
        doc.close();
      } catch (error) {
        console.warn("Skipped " + name + ": " + error);
        failed++;
      }
    }
    console.log(exported + " file(s) exported" + (failed ? ", " + failed + " failed." : "."));
  }
}
