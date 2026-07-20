// Contact Sheet
// Builds a contact sheet document from a folder of images: thumbnails in a
// grid, each on its own layer (so you can still rearrange them), with optional
// filename captions. Unattended runs pass --script-arg folder=...

var IMAGE_EXTENSION = /\.(png|jpe?g|bmp|gif|tiff?|webp|tga|psd)$/i;
var MAX_IMAGES = 200;

function luminance(hexColor) {
  var match = /^#?([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(hexColor);
  if (!match) { return 0; }
  return (parseInt(match[1], 16) * 0.2126 + parseInt(match[2], 16) * 0.7152 +
          parseInt(match[3], 16) * 0.0722) / 255;
}

var folder = patchy.args.folder || app.chooseFolder("Choose the folder of images");
if (!folder) {
  app.alert("Contact sheet cancelled: no folder chosen.");
} else {
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
    var options = patchy.ui.showDialog({
      title: "Contact Sheet",
      fields: [
        { key: "columns", label: "Columns", type: "number", value: 4, min: 1, max: 20 },
        { key: "cell", label: "Thumbnail size (px)", type: "number", value: 256, min: 32, max: 1024 },
        { key: "padding", label: "Padding (px)", type: "number", value: 16, min: 0, max: 200 },
        { key: "labels", label: "Filename captions", type: "checkbox", value: true },
        { key: "background", label: "Background", type: "color", value: "#26282c" }
      ]
    });
    if (options) {
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
