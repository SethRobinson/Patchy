// Export All Layers
// Saves every top-level layer of the active document as its own image file,
// next to the document (or in a folder you pick when the document is unsaved).
// A small options form chooses the format, an optional filename prefix, and
// whether files get a numbered order prefix.

var doc = app.activeDocument;
if (!doc) {
  app.alert("Open a document first.");
} else {
  var folder = "";
  if (doc.path) {
    folder = doc.path.replace(/[\\\/][^\\\/]*$/, "");
  } else {
    folder = app.chooseFolder("Folder to export the layer images into");
  }
  var options = folder ? patchy.ui.showDialog({
    title: "Export All Layers",
    fields: [
      { key: "format", label: "Format", type: "choice", value: "png",
        choices: ["png", "jpg", "bmp", "tif"] },
      { key: "prefix", label: "Filename prefix", type: "text", value: "" },
      { key: "numbered", label: "Number files by layer order", type: "checkbox", value: true }
    ]
  }) : null;
  if (options) {
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
