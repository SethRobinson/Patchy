// @name Batch Export
// @description Converts a whole folder of images: opens every file matching a
// @description pattern, optionally scales each to fit a maximum size, and
// @description exports them into another folder in the chosen format.
// @author Seth A. Robinson
// @cli --script-output result.txt --script-arg folder=C:\photos
// @cli --script-arg out=C:\photos\converted --script-arg format=jpg
//
// Unattended runs (patchy --run-script) pass the options as --script-arg
// folder=... --script-arg out=... [pattern=*.png] [format=jpg] [maxSize=1024].

// ---------------------------------------------------------------------------
// Options - defaults for this script. The options dialog (GUI runs) and
// --script-arg key=value (command line) override them.
var OPTIONS = {
  folder: "",       // source folder; empty = pick in the dialog
  out: "",          // output folder; empty = pick in the dialog
  pattern: "*.png", // which files to convert
  format: "jpg",    // png, jpg, bmp, tif
  maxSize: 0        // scale down to fit this many pixels; 0 = keep size
};
// ---------------------------------------------------------------------------

function planExports(folder, outFolder, files, format) {
  var owners = Object.create(null);
  var targets = [];
  for (var i = 0; i < files.length; i++) {
    var source = folder + "/" + files[i];
    owners[patchy.io.pathKey(source)] = source;
  }
  for (i = 0; i < files.length; i++) {
    var base = files[i].replace(/\.[^.]+$/, "");
    var target = outFolder + "/" + base + "." + format;
    var key = patchy.io.pathKey(target);
    if (owners[key] !== undefined) {
      throw new Error("Batch Export cancelled before writing. " + target +
                      " conflicts with " + owners[key] + ". Choose another output folder or file pattern.");
    }
    owners[key] = folder + "/" + files[i];
    targets.push(target);
  }
  return targets;
}

var options = patchy.ui.showOptions({
  title: "Batch Export",
  description: "Converts a whole folder of images in one go.\n\n" +
               "1. Pick the source folder and which files in it to convert (the pattern).\n" +
               "2. Pick the output folder and format.\n" +
               "3. Optionally scale everything down to fit a maximum size - handy for making " +
               "web-sized copies of large photos.\n\n" +
               "Originals are never modified.",
  fields: [
    { key: "folder", label: "Source folder", type: "folder", value: OPTIONS.folder },
    { key: "pattern", label: "Files matching", type: "text", value: OPTIONS.pattern },
    { key: "out", label: "Output folder", type: "folder", value: OPTIONS.out },
    { key: "format", label: "Convert to", type: "choice", value: OPTIONS.format,
      choices: ["png", "jpg", "bmp", "tif"] },
    { key: "maxSize", label: "Fit within (px, 0 = keep size)", type: "number",
      value: OPTIONS.maxSize, min: 0, max: 16384 }
  ]
});
if (options && (!options.folder || !options.out)) {
  app.alert("Batch export cancelled: both a source and an output folder are needed.\n\n" +
            "Re-run it and use the Browse buttons to pick them.");
} else if (options) {
  var folder = options.folder;
  var outFolder = options.out;
  var files = patchy.io.listFiles(folder, options.pattern);
  if (files.length === 0) {
    app.alert("No files in " + folder + " match " + options.pattern + ".");
  }
  var targets = planExports(folder, outFolder, files, options.format);
  var exported = 0;
  var failed = 0;
  for (var i = 0; i < files.length; i++) {
    var name = files[i];
    var doc = null;
    try {
      doc = app.open(folder + "/" + name);
      if (options.maxSize > 0 && (doc.width > options.maxSize || doc.height > options.maxSize)) {
        var scale = options.maxSize / Math.max(doc.width, doc.height);
        doc.resizeImage(Math.max(1, Math.round(doc.width * scale)),
                        Math.max(1, Math.round(doc.height * scale)));
      }
      var target = targets[i];
      if (doc.exportAs(target)) {
        console.log("Exported " + target);
        exported++;
      } else {
        console.warn("Could not export " + target);
        failed++;
      }
    } catch (error) {
      console.warn("Skipped " + name + ": " + error);
      failed++;
    } finally {
      if (doc) { doc.close(); }
    }
  }
  console.log(exported + " file(s) exported" + (failed ? ", " + failed + " failed." : "."));
}
