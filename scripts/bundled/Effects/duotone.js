// @name Duotone
// @description Remaps the active layer to a two-color gradient: dark areas
// @description take the shadow color, bright areas the highlight color. The
// @description classic poster / album-cover look.
// @author Seth A. Robinson
//
// Alpha is preserved; one undo restores the original.

// ---------------------------------------------------------------------------
// Options - defaults for this script. The options dialog (GUI runs) and
// --script-arg key=value (command line) override them.
var OPTIONS = {
  shadow: "#1c2b5a",     // color the dark areas become
  highlight: "#f2b544",  // color the bright areas become
  contrast: 0            // -50..50 push before the remap
};
// ---------------------------------------------------------------------------

var doc = app.activeDocument;
var layer = doc ? doc.activeLayer : undefined;
if (!doc) {
  app.alert("Open a document first.");
} else if (!layer || layer.isGroup) {
  app.alert("Select a pixel layer first.");
} else {
  var image = layer.getPixels();
  if (image.width === 0 || image.height === 0) {
    app.alert("The active layer has no pixels.");
  } else {
    var options = patchy.ui.showOptions({
      title: "Duotone",
      description: "Remaps the active layer (\"" + layer.name + "\") to a two-color gradient: " +
                   "dark areas take the shadow color, bright areas the highlight color. " +
                   "One undo restores the original.",
      fields: [
        { key: "shadow", label: "Shadow color", type: "color", value: OPTIONS.shadow },
        { key: "highlight", label: "Highlight color", type: "color", value: OPTIONS.highlight },
        { key: "contrast", label: "Contrast", type: "slider", value: OPTIONS.contrast,
          min: -50, max: 50 }
      ]
    });
    if (options) {
      function channels(hexColor, fallback) {
        var match = /^#?([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(hexColor);
        if (!match) { return fallback; }
        return [parseInt(match[1], 16), parseInt(match[2], 16), parseInt(match[3], 16)];
      }
      var shadow = channels(options.shadow, [28, 43, 90]);
      var highlight = channels(options.highlight, [242, 181, 68]);
      var gain = 1 + options.contrast / 50;
      var pixels = new Uint8Array(image.data);
      for (var i = 0; i < pixels.length; i += 4) {
        var t = (pixels[i] * 0.2126 + pixels[i + 1] * 0.7152 + pixels[i + 2] * 0.0722) / 255;
        t = Math.min(1, Math.max(0, (t - 0.5) * gain + 0.5));
        pixels[i] = Math.round(shadow[0] + (highlight[0] - shadow[0]) * t);
        pixels[i + 1] = Math.round(shadow[1] + (highlight[1] - shadow[1]) * t);
        pixels[i + 2] = Math.round(shadow[2] + (highlight[2] - shadow[2]) * t);
        // alpha stays
      }
      layer.setPixels(image);
      console.log("Duotone applied to \"" + layer.name + "\" (" +
                  image.width + "x" + image.height + ").");
    }
  }
}
