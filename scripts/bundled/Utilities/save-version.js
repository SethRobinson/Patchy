// Save Version
// Saves a numbered snapshot of the active document next to its file:
// mydoc.psd becomes mydoc_v001.psd, then mydoc_v002.psd, and so on (the next
// free number is found automatically). The document keeps its own path, so
// you keep working in the same file.

var doc = app.activeDocument;
if (!doc) {
  app.alert("Open a document first.");
} else {
  var folder = "";
  var base = "";
  if (doc.path) {
    folder = doc.path.replace(/[\\\/][^\\\/]*$/, "");
    base = doc.path.replace(/^.*[\\\/]/, "").replace(/\.[^.]+$/, "");
  } else {
    folder = app.chooseFolder("Folder to keep the version snapshots in");
    if (folder) {
      var suggested = (doc.name || "untitled").replace(/[^A-Za-z0-9_-]+/g, "_") || "untitled";
      base = app.prompt("Base name for the versions:", suggested) || "";
    }
  }
  if (!folder || !base) {
    app.alert("Save Version cancelled.");
  } else {
    base = base.replace(/_v\d+$/i, "");  // saving from mydoc_v003 continues the series
    var existing = patchy.io.listFiles(folder, "*.psd");
    var highest = 0;
    var pattern = new RegExp("^" + base.replace(/[.*+?^${}()|[\]\\]/g, "\\$&") +
                             "_v(\\d+)\\.psd$", "i");
    for (var i = 0; i < existing.length; i++) {
      var match = pattern.exec(existing[i]);
      if (match) { highest = Math.max(highest, parseInt(match[1], 10)); }
    }
    var next = highest + 1;
    var target = folder + "/" + base + "_v" + ("00" + next).slice(-3) + ".psd";
    if (doc.exportAs(target)) {
      console.log("Saved version " + next + ": " + target);
      app.alert("Saved version " + next + ":\n" + target);
    } else {
      app.alert("Could not write " + target);
    }
  }
}
