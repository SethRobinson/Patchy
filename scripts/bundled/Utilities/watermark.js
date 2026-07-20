// @name Watermark
// Stamps a semi-transparent text watermark in the bottom-right corner of the
// active document.

var doc = app.activeDocument;
if (!doc) {
  app.alert("Open a document first.");
} else {
  var message = app.prompt("Watermark text:", "(c) " + new Date().getFullYear());
  if (message) {
    var size = Math.max(12, Math.round(doc.width / 24));
    var layer = doc.addTextLayer(message, {
      size: size,
      x: 0,
      y: doc.height - Math.round(size * 0.8),
      color: "#ffffff"
    });
    // Position from the rendered bounds: bottom-right with a small margin.
    var margin = Math.round(size / 2);
    layer.x = doc.width - layer.bounds.width - margin;
    layer.y = doc.height - layer.bounds.height - margin;
    layer.opacity = 45;
    layer.name = "Watermark";
    console.log("Watermark added (" + layer.bounds.width + "x" + layer.bounds.height + ")");
  }
}
