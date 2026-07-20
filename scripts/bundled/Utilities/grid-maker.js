// Grid Maker
// Draws a grid overlay on its own layer: pick the cell size, line width,
// color, and optional subdivisions (lighter lines splitting each cell).
// Handy for layout guides, pixel-art tile boundaries, and comic panels;
// delete or hide the "Grid" layer when you are done with it.

var doc = app.activeDocument;
if (!doc) {
  app.alert("Open a document first.");
} else {
  var options = patchy.ui.showDialog({
    title: "Grid Maker",
    fields: [
      { key: "cell", label: "Cell size (px)", type: "number", value: 64, min: 2, max: 4096 },
      { key: "lineWidth", label: "Line width (px)", type: "number", value: 1, min: 1, max: 64 },
      { key: "subdivisions", label: "Subdivisions per cell (0 = none)", type: "number",
        value: 0, min: 0, max: 16 },
      { key: "color", label: "Line color", type: "color", value: "#40a0ff" },
      { key: "opacity", label: "Layer opacity", type: "slider", value: 60, min: 5, max: 100 }
    ]
  });
  if (options) {
    var match = /^#?([0-9a-f]{6})$/i.exec(options.color);
    var rgb = match ? match[1] : "40a0ff";
    var subdivisionColor = "#50" + rgb;  // same hue, mostly transparent

    var layer = doc.addLayer("Grid");
    // First write allocates the layer's buffer; cover the whole canvas so the
    // line strips below are not clipped to the first strip's rect.
    layer.fillRect(0, 0, doc.width, doc.height, "#00000000");

    function verticalLine(x, color) {
      if (x < doc.width) { layer.fillRect(x, 0, options.lineWidth, doc.height, color); }
    }
    function horizontalLine(y, color) {
      if (y < doc.height) { layer.fillRect(0, y, doc.width, options.lineWidth, color); }
    }

    var x, y, s;
    if (options.subdivisions > 1) {
      var step = options.cell / options.subdivisions;
      for (x = 0; x <= doc.width; x += options.cell) {
        for (s = 1; s < options.subdivisions; s++) {
          verticalLine(Math.round(x + s * step), subdivisionColor);
        }
      }
      for (y = 0; y <= doc.height; y += options.cell) {
        for (s = 1; s < options.subdivisions; s++) {
          horizontalLine(Math.round(y + s * step), subdivisionColor);
        }
      }
    }
    for (x = 0; x <= doc.width; x += options.cell) { verticalLine(x, options.color); }
    for (y = 0; y <= doc.height; y += options.cell) { horizontalLine(y, options.color); }

    layer.opacity = options.opacity;
    console.log("Grid layer added: " + options.cell + " px cells" +
                (options.subdivisions > 1 ? ", " + options.subdivisions + " subdivisions." : "."));
  }
}
