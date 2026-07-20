// @name Glitch
// Corrupted-signal look for the active layer: horizontal slices tear sideways,
// the color channels drift apart, and optional scanlines darken every third
// row. Every run is different (random slices); one undo restores the original.

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
    var options = patchy.ui.showDialog({
      title: "Glitch",
      fields: [
        { key: "intensity", label: "Intensity", type: "slider", value: 40, min: 1, max: 100 },
        { key: "slices", label: "Slices", type: "number", value: 12, min: 1, max: 64 },
        { key: "channelShift", label: "Color channel drift", type: "checkbox", value: true },
        { key: "scanlines", label: "Scanlines", type: "checkbox", value: true }
      ]
    });
    if (options) {
      var width = image.width;
      var height = image.height;
      var pixels = new Uint8Array(image.data);
      var source = new Uint8Array(pixels);  // untouched copy to sample from

      // Random horizontal slices, each with its own sideways tear.
      var rowShift = new Int32Array(height);
      var maxShift = Math.max(2, Math.round(width * 0.15 * options.intensity / 100));
      for (var s = 0; s < options.slices; s++) {
        var sliceHeight = 2 + Math.floor(Math.random() * Math.max(2, height / 10));
        var start = Math.floor(Math.random() * height);
        var shift = Math.round((Math.random() * 2 - 1) * maxShift);
        for (var r = start; r < Math.min(height, start + sliceHeight); r++) {
          rowShift[r] = shift;
        }
      }
      var drift = options.channelShift
          ? Math.max(1, Math.round(maxShift / 6))
          : 0;

      for (var y = 0; y < height; y++) {
        var shiftX = rowShift[y];
        var rowOffset = y * width * 4;
        for (var x = 0; x < width; x++) {
          var to = rowOffset + x * 4;
          var baseX = x - shiftX;
          var redX = ((baseX - drift) % width + width) % width;
          var mainX = (baseX % width + width) % width;
          var blueX = ((baseX + drift) % width + width) % width;
          pixels[to] = source[rowOffset + redX * 4];
          pixels[to + 1] = source[rowOffset + mainX * 4 + 1];
          pixels[to + 2] = source[rowOffset + blueX * 4 + 2];
          pixels[to + 3] = source[rowOffset + mainX * 4 + 3];
        }
        if (options.scanlines && y % 3 === 2) {
          for (x = 0; x < width; x++) {
            var p = rowOffset + x * 4;
            pixels[p] = (pixels[p] * 210) >> 8;
            pixels[p + 1] = (pixels[p + 1] * 210) >> 8;
            pixels[p + 2] = (pixels[p + 2] * 210) >> 8;
          }
        }
      }
      layer.setPixels(image);
      console.log("Glitch applied to \"" + layer.name + "\" (intensity " +
                  options.intensity + ", " + options.slices + " slices).");
    }
  }
}
