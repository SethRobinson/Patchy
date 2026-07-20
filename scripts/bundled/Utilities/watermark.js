// @name Watermark
// @description Stamps a semi-transparent text watermark over the active
// @description document: pick the text, corner, size, color, and opacity.
// @author Seth A. Robinson
// @cli --script-arg "text=(c) My Studio" --script-arg opacity=60 example.png
//
// The watermark is its own text layer, so it stays editable and deletable.

// ---------------------------------------------------------------------------
// Options - defaults for this script. The options dialog (GUI runs) and
// --script-arg key=value (command line) override them.
var OPTIONS = {
  text: "(c) " + new Date().getFullYear(),
  corner: "Bottom right",  // Bottom right, Bottom left, Top right, Top left, Center
  size: 4,                 // 1..20 - text height as a percentage of the image width
  color: "#ffffff",
  opacity: 45              // 5..100 layer opacity
};
// ---------------------------------------------------------------------------

var doc = app.activeDocument;
if (!doc) {
  app.alert("Open a document first.");
} else {
  var options = patchy.ui.showOptions({
    title: "Watermark",
    description: "Stamps the text below over this document as a semi-transparent text layer. " +
                 "Size is relative to the image width, so the same settings work on any " +
                 "resolution. Delete or edit the \"Watermark\" layer later like any other.",
    fields: [
      { key: "text", label: "Text", type: "text", value: OPTIONS.text },
      { key: "corner", label: "Position", type: "choice", value: OPTIONS.corner,
        choices: ["Bottom right", "Bottom left", "Top right", "Top left", "Center"] },
      { key: "size", label: "Size (% of width)", type: "slider", value: OPTIONS.size,
        min: 1, max: 20 },
      { key: "color", label: "Color", type: "color", value: OPTIONS.color },
      { key: "opacity", label: "Opacity", type: "slider", value: OPTIONS.opacity,
        min: 5, max: 100 }
    ]
  });
  if (options && options.text) {
    var size = Math.max(10, Math.round(doc.width * options.size / 100));
    var layer = doc.addTextLayer(options.text, {
      size: size,
      x: 0,
      y: Math.round(doc.height / 2),
      color: options.color
    });
    // Position from the rendered bounds, with a small margin.
    var margin = Math.round(size / 2);
    var x = Math.round((doc.width - layer.bounds.width) / 2);
    var y = Math.round((doc.height - layer.bounds.height) / 2);
    if (options.corner.indexOf("left") >= 0) { x = margin; }
    if (options.corner.indexOf("right") >= 0) { x = doc.width - layer.bounds.width - margin; }
    if (options.corner.indexOf("Top") >= 0) { y = margin; }
    if (options.corner.indexOf("Bottom") >= 0) {
      y = doc.height - layer.bounds.height - margin;
    }
    layer.x = x;
    layer.y = y;
    layer.opacity = options.opacity;
    layer.name = "Watermark";
    console.log("Watermark added (" + layer.bounds.width + "x" + layer.bounds.height + ", " +
                options.corner.toLowerCase() + ").");
  }
}
