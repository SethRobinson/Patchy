// @name Export All Layers
// @description Saves every top-level layer of the active document as its own
// @description image file: pick the folder, format, an optional filename
// @description prefix, and numbered ordering.
// @author Seth A. Robinson

// ---------------------------------------------------------------------------
// Options - defaults for this script. The options dialog (GUI runs) and
// --script-arg key=value (command line) override them.
var OPTIONS = {
  folder: "",       // output folder; empty = next to the document (or Browse)
  format: "png",    // png, jpg, bmp, tif
  prefix: "",       // text in front of every filename
  numbered: true    // 01_, 02_, ... in layer order
};
// ---------------------------------------------------------------------------

var doc = app.activeDocument;
if (!doc) {
  app.alert("Open a document first.");
} else {
  // Default the output next to the document when it has been saved somewhere.
  var defaultFolder = OPTIONS.folder;
  if (!defaultFolder && doc.path) {
    defaultFolder = doc.path.replace(/[\\\/][^\\\/]*$/, "");
  }
  var options = patchy.ui.showOptions({
    title: "Export All Layers",
    description: "Saves every top-level layer of this document as its own image file - each " +
                 "layer is shown alone and exported, then everything is restored.\n\n" +
                 "Files are named prefix + number + layer name; the numbered order matches " +
                 "the layer stack, which keeps animation frames in sequence.",
    fields: [
      { key: "folder", label: "Output folder", type: "folder", value: defaultFolder },
      { key: "format", label: "Format", type: "choice", value: OPTIONS.format,
        choices: ["png", "jpg", "bmp", "tif"] },
      { key: "prefix", label: "Filename prefix", type: "text", value: OPTIONS.prefix },
      { key: "numbered", label: "Number files by layer order", type: "checkbox",
        value: OPTIONS.numbered }
    ]
  });
  if (options && !options.folder) {
    app.alert("Export cancelled: no output folder chosen.\n\nRe-run it and use Browse to " +
              "pick where the layer images should go.");
  } else if (options) {
    var folder = options.folder;
    var layers = doc.layers;
    var hidden = [];
    var i;
    // Hide everything once, then show one layer at a time and export the result.
    for (i = 0; i < layers.length; i++) {
      hidden.push(layers[i].visible);
      layers[i].visible = false;
    }
    var exported = 0;
    for (i = 0; i < layers.length; i++) {
      layers[i].visible = true;
      var safe = layers[i].name.replace(/[^A-Za-z0-9_-]+/g, "_");
      var number = options.numbered ? ("00" + (i + 1)).slice(-2) + "_" : "";
      var target = folder + "/" + options.prefix + number + safe + "." + options.format;
      if (doc.exportAs(target)) {
        console.log("Exported " + target);
        exported++;
      } else {
        console.warn("Could not export " + target);
      }
      layers[i].visible = false;
    }
    for (i = 0; i < layers.length; i++) {
      layers[i].visible = hidden[i];
    }
    console.log(exported + " layer(s) exported.");
  }
}
