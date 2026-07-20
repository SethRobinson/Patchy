// @name Rename Layers
// Batch-renames the layers of the active document: add a prefix or suffix,
// find & replace text in names, or renumber everything into a clean sequence
// ("Frame 01", "Frame 02", ...). Works on all layers or just the top level.

var doc = app.activeDocument;
if (!doc) {
  app.alert("Open a document first.");
} else {
  var options = patchy.ui.showDialog({
    title: "Rename Layers",
    fields: [
      { key: "mode", label: "Mode", type: "choice", value: "Add prefix",
        choices: ["Add prefix", "Add suffix", "Find & replace", "Number sequence"] },
      { key: "text", label: "Prefix / suffix / text to find", type: "text", value: "" },
      { key: "replacement", label: "Replacement / sequence base name", type: "text", value: "Layer" },
      { key: "scope", label: "Apply to", type: "choice", value: "All layers",
        choices: ["All layers", "Top-level only"] },
      { key: "includeGroups", label: "Rename group layers too", type: "checkbox", value: true }
    ]
  });
  if (options) {
    var needsText = options.mode === "Add prefix" || options.mode === "Add suffix" ||
                    options.mode === "Find & replace";
    if (needsText && !options.text) {
      app.alert("Nothing to do: the \"" + options.mode + "\" mode needs text.");
    } else {
      var counter = 0;
      var renamed = 0;
      function renameIn(layers) {
        for (var i = 0; i < layers.length; i++) {
          var layer = layers[i];
          if (!layer.isGroup || options.includeGroups) {
            var name = layer.name;
            if (options.mode === "Add prefix") {
              name = options.text + name;
            } else if (options.mode === "Add suffix") {
              name = name + options.text;
            } else if (options.mode === "Find & replace") {
              name = name.split(options.text).join(options.replacement);
            } else {  // Number sequence, bottom-to-top like the layers array
              counter++;
              name = options.replacement + " " + ("0" + counter).slice(-2);
            }
            if (name !== layer.name) {
              layer.name = name;
              renamed++;
            }
          }
          if (layer.isGroup && options.scope === "All layers") {
            renameIn(layer.children);
          }
        }
      }
      renameIn(doc.layers);
      console.log(renamed + " layer(s) renamed.");
      if (renamed === 0) {
        app.alert("No layer names changed.");
      }
    }
  }
}
