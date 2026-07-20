// Photo Frame
// Puts a frame around the whole image: grows the canvas and adds a frame
// layer in your choice of style - a solid border, a double matte with an
// accent line, or a polaroid with a wide bottom and an optional caption.
// The picture itself is untouched (the frame is its own layer on top).

var doc = app.activeDocument;
if (!doc) {
  app.alert("Open a document first.");
} else {
  var options = patchy.ui.showDialog({
    title: "Photo Frame",
    fields: [
      { key: "style", label: "Style", type: "choice", value: "Solid",
        choices: ["Solid", "Double matte", "Polaroid"] },
      { key: "size", label: "Frame width (px)", type: "number", value: 40, min: 4, max: 500 },
      { key: "color", label: "Frame color", type: "color", value: "#f4f1e8" },
      { key: "caption", label: "Caption (Polaroid)", type: "text", value: "" }
    ]
  });
  if (options) {
    function shade(hexColor, factor) {
      var match = /^#?([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(hexColor);
      if (!match) { return "#808080"; }
      var parts = [parseInt(match[1], 16), parseInt(match[2], 16), parseInt(match[3], 16)];
      var out = "#";
      for (var c = 0; c < 3; c++) {
        var value = Math.min(255, Math.round(parts[c] * factor));
        out += ("0" + value.toString(16)).slice(-2);
      }
      return out;
    }

    var oldWidth = doc.width;
    var oldHeight = doc.height;
    var border = options.size;
    var bottom = options.style === "Polaroid" ? border * 4 : border;

    // Grow the canvas (anchored top-left), then shift the content into the
    // middle of the new space.
    doc.resizeCanvas(oldWidth + border * 2, oldHeight + border + bottom);
    var layers = doc.layers;
    for (var i = 0; i < layers.length; i++) {
      layers[i].moveTo(layers[i].x + border, layers[i].y + border);
    }

    var width = doc.width;
    var height = doc.height;
    var frame = doc.addLayer("Frame");
    frame.fillRect(0, 0, width, height, "#00000000");  // allocate the full buffer
    frame.fillRect(0, 0, width, border, options.color);                      // top
    frame.fillRect(0, height - bottom, width, bottom, options.color);        // bottom
    frame.fillRect(0, border, border, oldHeight, options.color);             // left
    frame.fillRect(width - border, border, border, oldHeight, options.color);  // right

    if (options.style === "Double matte") {
      var accent = shade(options.color, 0.55);
      var line = Math.max(2, Math.round(border / 10));
      var gap = Math.max(2, Math.round(border / 5));
      frame.fillRect(border - gap - line, border - gap - line,
                     oldWidth + (gap + line) * 2, line, accent);
      frame.fillRect(border - gap - line, border + oldHeight + gap,
                     oldWidth + (gap + line) * 2, line, accent);
      frame.fillRect(border - gap - line, border - gap,
                     line, oldHeight + gap * 2, accent);
      frame.fillRect(border + oldWidth + gap, border - gap,
                     line, oldHeight + gap * 2, accent);
    }

    if (options.style === "Polaroid" && options.caption) {
      var caption = doc.addTextLayer(options.caption, {
        size: Math.min(60, Math.max(14, Math.round(bottom / 3.5))),
        x: 0, y: height - bottom + Math.round(bottom / 4),
        color: shade(options.color, 0.3)
      });
      caption.name = "Caption";
      caption.x = Math.round((width - caption.bounds.width) / 2);
      caption.y = height - bottom + Math.round((bottom - caption.bounds.height) / 2);
    }

    console.log(options.style + " frame added (" + border + " px); canvas is now " +
                width + "x" + height + ".");
  }
}
