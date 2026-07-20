// @name Save Version
// @description Saves a numbered snapshot next to the document's file: mydoc.psd
// @description becomes mydoc_v001.psd, then mydoc_v002.psd - the next free
// @description number is found automatically. You keep working in the same file.
// @author Seth A. Robinson
// @cli --script-arg folder=C:\versions example.psd
//
// Runs instantly when the document has a file; unsaved documents get a small
// dialog asking where the snapshots should live.

// ---------------------------------------------------------------------------
// Options - defaults for UNSAVED documents (saved ones version next to their
// own file). The dialog and --script-arg key=value override them.
var OPTIONS = {
  folder: "",  // where the snapshots go; empty = pick in the dialog
  base: ""     // snapshot base name; empty = derived from the document name
};
// ---------------------------------------------------------------------------

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
    var suggested = OPTIONS.base ||
        (doc.name || "untitled").replace(/[^A-Za-z0-9_-]+/g, "_") || "untitled";
    var setup = patchy.ui.showOptions({
      title: "Save Version",
      description: "This document has not been saved anywhere yet, so pick where its numbered " +
                   "version snapshots should live. Once you save the document normally, " +
                   "future versions land next to its file automatically.",
      fields: [
        { key: "folder", label: "Snapshot folder", type: "folder", value: OPTIONS.folder },
        { key: "base", label: "Base name", type: "text", value: suggested }
      ]
    });
    if (setup) {
      folder = setup.folder;
      base = setup.base;
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
