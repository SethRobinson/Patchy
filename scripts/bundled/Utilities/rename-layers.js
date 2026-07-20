// @name Rename Layers
// @description Batch-renames the layers of the active document: add a prefix
// @description or suffix, find & replace text in names, or renumber everything
// @description into a clean sequence like "Frame 01", "Frame 02", ...
// @author Seth A. Robinson
// @cli --script-arg "mode=Add prefix" --script-arg text=BG_ example.psd

// ---------------------------------------------------------------------------
// Options - defaults for this script. The options dialog (GUI runs) and
// --script-arg key=value (command line) override them.
var OPTIONS = {
  mode: "Add prefix",     // Add prefix, Add suffix, Find & replace, Number sequence
  text: "",               // the prefix/suffix, or the text to find
  replacement: "Layer",   // replacement text, or the sequence base name
  scope: "All layers",    // All layers, Top-level only
  includeGroups: true
};
// ---------------------------------------------------------------------------

var doc = app.activeDocument;
if (!doc) {
  app.alert("Open a document first.");
} else {
  var options = patchy.ui.showOptions({
    title: "Rename Layers",
    description: "Batch-renames this document's layers in one undoable step.\n\n" +
                 "Add prefix/suffix put the text before or after every name; Find & replace " +
                 "swaps text inside names; Number sequence renames everything to the base " +
                 "name plus a counter (\"Frame 01\", \"Frame 02\", ... in layer-stack order).",
    fields: [
      { key: "mode", label: "Mode", type: "choice", value: OPTIONS.mode,
        choices: ["Add prefix", "Add suffix", "Find & replace", "Number sequence"] },
      { key: "text", label: "Prefix / suffix / text to find", type: "text",
        value: OPTIONS.text },
      { key: "replacement", label: "Replacement / sequence base name", type: "text",
        value: OPTIONS.replacement },
      { key: "scope", label: "Apply to", type: "choice", value: OPTIONS.scope,
        choices: ["All layers", "Top-level only"] },
      { key: "includeGroups", label: "Rename group layers too", type: "checkbox",
        value: OPTIONS.includeGroups }
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
