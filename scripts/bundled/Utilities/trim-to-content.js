// @name Trim to Content
// @description Crops the document to the smallest rectangle that still
// @description contains every visible pixel of every layer.
// @author Seth A. Robinson
//
// Runs instantly (no dialog) - it needs no choices beyond the options below.

// ---------------------------------------------------------------------------
// Options - tweak here, or override with --script-arg padding=8.
var OPTIONS = {
  padding: 0  // extra transparent pixels to keep around the content
};
// ---------------------------------------------------------------------------
var PADDING = Math.max(0, Number(patchy.args.padding || OPTIONS.padding) || 0);

var doc = app.activeDocument;
if (!doc) {
  app.alert("Open a document first.");
} else {
  var minX = doc.width;
  var minY = doc.height;
  var maxX = -1;
  var maxY = -1;

  function scanLayer(layer) {
    var i;
    if (layer.isGroup) {
      var children = layer.children;
      for (i = 0; i < children.length; i++) {
        scanLayer(children[i]);
      }
      return;
    }
    if (!layer.visible) {
      return;
    }
    var img = layer.getPixels();
    if (img.width < 1 || img.height < 1) {
      return;
    }
    var data = new Uint8Array(img.data);
    for (var y = 0; y < img.height; y++) {
      for (var x = 0; x < img.width; x++) {
        if (data[(y * img.width + x) * 4 + 3] > 0) {
          var docX = img.x + x;
          var docY = img.y + y;
          if (docX < minX) { minX = docX; }
          if (docY < minY) { minY = docY; }
          if (docX > maxX) { maxX = docX; }
          if (docY > maxY) { maxY = docY; }
        }
      }
    }
  }

  var layers = doc.layers;
  for (var i = 0; i < layers.length; i++) {
    scanLayer(layers[i]);
  }

  if (maxX < 0) {
    app.alert("No visible pixels found; nothing to trim.");
  } else {
    minX = Math.max(0, minX - PADDING);
    minY = Math.max(0, minY - PADDING);
    maxX = Math.min(doc.width - 1, maxX + PADDING);
    maxY = Math.min(doc.height - 1, maxY + PADDING);
    var w = maxX - minX + 1;
    var h = maxY - minY + 1;
    if (w === doc.width && h === doc.height && minX === 0 && minY === 0) {
      console.log("Document is already trimmed.");
    } else {
      doc.crop(minX, minY, w, h);
      console.log("Trimmed to " + w + "x" + h + " at " + minX + "," + minY);
    }
  }
}
