// @name Contact Sheet
// @description Builds a contact sheet from a folder of images: thumbnails in a
// @description grid, each on its own layer, with optional filename captions.
// @author Seth A. Robinson
// @window
//
// Unattended runs pass --script-arg folder=... (plus any other option keys).

// ---------------------------------------------------------------------------
// Options - defaults for this script. The options dialog (GUI runs) and
// --script-arg key=value (command line) override them.
var OPTIONS = {
  folder: "",          // image folder; empty = pick in the dialog
  columns: 4,
  cell: 256,           // thumbnail size in pixels
  padding: 16,
  labels: true,        // filename captions under each thumbnail
  background: "#26282c"
};
// ---------------------------------------------------------------------------

var IMAGE_EXTENSION = /\.(png|jpe?g|bmp|gif|tiff?|webp|tga|psd)$/i;
var MAX_IMAGES = 200;

function luminance(hexColor) {
  var match = /^#?([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(hexColor);
  if (!match) { return 0; }
  return (parseInt(match[1], 16) * 0.2126 + parseInt(match[2], 16) * 0.7152 +
          parseInt(match[3], 16) * 0.0722) / 255;
}

var options = patchy.ui.showOptions({
  title: "Contact Sheet",
  description: "Builds a new document showing every image in a folder as a grid of " +
               "thumbnails - handy for browsing a shoot or cataloging sprites.\n\n" +
               "1. Pick the folder of images (PNG, JPEG, PSD, and the other usual formats).\n" +
               "2. Choose the grid: columns, thumbnail size, and padding.\n" +
               "3. OK builds the sheet as its own document; each thumbnail lands on its own " +
               "layer so you can still rearrange them afterwards.",
  fields: [
    { key: "folder", label: "Image folder", type: "folder", value: OPTIONS.folder },
    { key: "columns", label: "Columns", type: "number", value: OPTIONS.columns,
      min: 1, max: 20 },
    { key: "cell", label: "Thumbnail size (px)", type: "number", value: OPTIONS.cell,
      min: 32, max: 1024 },
    { key: "padding", label: "Padding (px)", type: "number", value: OPTIONS.padding,
      min: 0, max: 200 },
    { key: "labels", label: "Filename captions", type: "checkbox", value: OPTIONS.labels },
    { key: "background", label: "Background", type: "color", value: OPTIONS.background }
  ]
});
if (options && !options.folder) {
  app.alert("Contact sheet cancelled: no folder chosen.\n\nRe-run it and use Browse to pick " +
            "the folder of images.");
} else if (options) {
  var folder = options.folder;
  var all = patchy.io.listFiles(folder);
  var files = [];
  for (var i = 0; i < all.length; i++) {
    if (IMAGE_EXTENSION.test(all[i])) { files.push(all[i]); }
  }
  if (files.length === 0) {
    app.alert("No image files found in " + folder + ".");
  } else {
    if (files.length > MAX_IMAGES) {
      console.warn(files.length + " images found; using the first " + MAX_IMAGES + ".");
      files = files.slice(0, MAX_IMAGES);
    }
    {
      var columns = Math.min(options.columns, files.length);
      var rows = Math.ceil(files.length / columns);
      var labelHeight = options.labels ? 18 : 0;
      var cellStepX = options.cell + options.padding;
      var cellStepY = options.cell + labelHeight + options.padding;
      var sheet = app.newDocument(options.padding + columns * cellStepX,
                                  options.padding + rows * cellStepY);
      var background = sheet.addLayer("Background");
      background.fill(options.background);
      var textColor = luminance(options.background) > 0.5 ? "#202020" : "#e8e8e8";
      var placed = 0;
      for (i = 0; i < files.length; i++) {
        var name = files[i];
        try {
          var image = app.open(folder + "/" + name);
          var scale = Math.min(options.cell / image.width, options.cell / image.height, 1);
          image.resizeImage(Math.max(1, Math.round(image.width * scale)),
                            Math.max(1, Math.round(image.height * scale)));
          image.flatten();
          var pixels = image.layers[0].getPixels();
          image.close();
          var column = placed % columns;
          var row = Math.floor(placed / columns);
          var cellX = options.padding + column * cellStepX;
          var cellY = options.padding + row * cellStepY;
          var thumb = sheet.addLayer(name);
          thumb.setPixels({
            x: cellX + Math.floor((options.cell - pixels.width) / 2),
            y: cellY + Math.floor((options.cell - pixels.height) / 2),
            width: pixels.width,
            height: pixels.height,
            data: pixels.data
          });
          if (options.labels) {
            var caption = sheet.addTextLayer(name, {
              size: 11, x: cellX + 2, y: cellY + options.cell + 3, color: textColor
            });
            caption.name = "Caption: " + name;
          }
          placed++;
          console.log("Placed " + name);
        } catch (error) {
          console.warn("Skipped " + name + ": " + error);
        }
      }
      console.log("Contact sheet built: " + placed + " image(s), " + columns + " column(s).");
    }
  }
}
