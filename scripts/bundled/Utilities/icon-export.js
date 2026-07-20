// Icon Export
// Exports the active document as a set of square PNG icons in the standard
// sizes (app icons, favicons, store art). Each size is resampled from a
// full-resolution master in one step, never chained, so small sizes stay
// sharp. Unattended runs pass --script-arg out=...

var doc = app.activeDocument;
if (!doc) {
  app.alert("Open a document first.");
} else {
  var outFolder = patchy.args.out || app.chooseFolder("Choose the folder for the icon files");
  if (!outFolder) {
    app.alert("Icon export cancelled: no output folder chosen.");
  } else {
    var defaultBase = (doc.name || "icon").replace(/\.[^.]+$/, "")
        .replace(/[^A-Za-z0-9_-]+/g, "_") || "icon";
    var options = patchy.ui.showDialog({
      title: "Icon Export",
      fields: [
        { key: "base", label: "Base filename", type: "text", value: defaultBase },
        { key: "s16", label: "16 px", type: "checkbox", value: true },
        { key: "s32", label: "32 px", type: "checkbox", value: true },
        { key: "s48", label: "48 px", type: "checkbox", value: true },
        { key: "s64", label: "64 px", type: "checkbox", value: true },
        { key: "s128", label: "128 px", type: "checkbox", value: true },
        { key: "s256", label: "256 px", type: "checkbox", value: true },
        { key: "s512", label: "512 px", type: "checkbox", value: false },
        { key: "s1024", label: "1024 px", type: "checkbox", value: false },
        { key: "pad", label: "Pad non-square images to square", type: "checkbox", value: true }
      ]
    });
    if (options) {
      var sizes = [];
      var candidates = [16, 32, 48, 64, 128, 256, 512, 1024];
      for (var i = 0; i < candidates.length; i++) {
        if (options["s" + candidates[i]]) { sizes.push(candidates[i]); }
      }
      if (sizes.length === 0) {
        app.alert("No sizes selected.");
      } else {
        var base = options.base.replace(/[^A-Za-z0-9_-]+/g, "_") || "icon";
        var master = outFolder + "/" + base + "-master.png";
        if (!doc.exportAs(master)) {
          app.alert("Could not write " + master);
        } else {
          var exported = 0;
          for (i = 0; i < sizes.length; i++) {
            var size = sizes[i];
            try {
              var work = app.open(master);
              if (options.pad && work.width !== work.height) {
                var square = Math.max(work.width, work.height);
                var offsetX = Math.floor((square - work.width) / 2);
                var offsetY = Math.floor((square - work.height) / 2);
                work.resizeCanvas(square, square);
                var layers = work.layers;
                for (var l = 0; l < layers.length; l++) {
                  layers[l].moveTo(layers[l].x + offsetX, layers[l].y + offsetY);
                }
              }
              if (work.width !== work.height) {
                // Not padding: fit within the size box instead of distorting.
                var scale = size / Math.max(work.width, work.height);
                work.resizeImage(Math.max(1, Math.round(work.width * scale)),
                                 Math.max(1, Math.round(work.height * scale)));
              } else {
                work.resizeImage(size, size);
              }
              var target = outFolder + "/" + base + "-" + size + ".png";
              if (work.exportAs(target)) {
                console.log("Exported " + target);
                exported++;
              } else {
                console.warn("Could not export " + target);
              }
              work.close();
            } catch (error) {
              console.warn("Skipped " + size + " px: " + error);
            }
          }
          console.log(exported + " icon(s) exported. The full-size master is " + master + ".");
        }
      }
    }
  }
}
