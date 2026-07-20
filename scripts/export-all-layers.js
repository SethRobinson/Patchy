// Export All Layers
// Saves every top-level layer of the active document as its own PNG, next to
// the document file (or in a folder you type in when the document is unsaved).

var doc = app.activeDocument;
if (!doc) {
  app.alert("Open a document first.");
} else {
  var folder = "";
  if (doc.path) {
    folder = doc.path.replace(/[\\\/][^\\\/]*$/, "");
  } else {
    folder = app.prompt("Folder to export the layer PNGs into:", "");
  }
  if (folder) {
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
      var target = folder + "/" + ("00" + (i + 1)).slice(-2) + "_" + safe + ".png";
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
