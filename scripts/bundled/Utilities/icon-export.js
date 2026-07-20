// @name Icon Export
// @description Exports the active document as a set of square PNG icons in
// @description the standard sizes (app icons, favicons, store art), each
// @description resampled from the full-resolution master so they stay sharp.
// @author Seth A. Robinson
// @cli --script-arg out=C:\icons example.png
//
// Each size is resampled from the master in one step, never chained.
// Unattended runs pass --script-arg out=... (plus any other option keys).

// ---------------------------------------------------------------------------
// Options - defaults for this script. The options dialog (GUI runs) and
// --script-arg key=value (command line) override them.
var OPTIONS = {
  out: "",       // output folder; empty = pick in the dialog
  base: "",      // base filename; empty = derived from the document name
  s16: true, s32: true, s48: true, s64: true,
  s128: true, s256: true, s512: false, s1024: false,
  pad: true      // pad non-square images to square before scaling
};
// ---------------------------------------------------------------------------

var doc = app.activeDocument;
if (!doc) {
  app.alert("Open a document first.");
} else {
  var defaultBase = OPTIONS.base || (doc.name || "icon").replace(/\.[^.]+$/, "")
      .replace(/[^A-Za-z0-9_-]+/g, "_") || "icon";
  var options = patchy.ui.showOptions({
    title: "Icon Export",
    description: "Exports this document as a set of square PNG icons - the sizes app stores, " +
                 "favicons, and installers ask for.\n\n" +
                 "Pick the output folder and tick the sizes you need. Every size is scaled " +
                 "straight from the full-resolution image (never from another icon), so even " +
                 "16 px stays as sharp as possible.",
    fields: [
      { key: "out", label: "Output folder", type: "folder", value: OPTIONS.out },
      { key: "base", label: "Base filename", type: "text", value: defaultBase },
      { key: "s16", label: "16 px", type: "checkbox", value: OPTIONS.s16 },
      { key: "s32", label: "32 px", type: "checkbox", value: OPTIONS.s32 },
      { key: "s48", label: "48 px", type: "checkbox", value: OPTIONS.s48 },
      { key: "s64", label: "64 px", type: "checkbox", value: OPTIONS.s64 },
      { key: "s128", label: "128 px", type: "checkbox", value: OPTIONS.s128 },
      { key: "s256", label: "256 px", type: "checkbox", value: OPTIONS.s256 },
      { key: "s512", label: "512 px", type: "checkbox", value: OPTIONS.s512 },
      { key: "s1024", label: "1024 px", type: "checkbox", value: OPTIONS.s1024 },
      { key: "pad", label: "Pad non-square images to square", type: "checkbox",
        value: OPTIONS.pad }
    ]
  });
  if (options && !options.out) {
    app.alert("Icon export cancelled: no output folder chosen.\n\nRe-run it and use Browse " +
              "to pick where the icons should go.");
  } else if (options) {
    var outFolder = options.out;
    {
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
