// Data Merge
// Mail-merge for images: fills the active document's text layers from a CSV
// file and exports one image per data row (name badges, cards, certificates).
// The CSV's header row names the text layers to fill; each following row
// becomes one exported file. The template document is restored afterwards.
// Unattended runs (patchy --run-script) pass --script-arg csv=... and
// --script-arg out=... instead of using the pickers.

function parseCsv(text) {
  var rows = [];
  var row = [];
  var field = "";
  var inQuotes = false;
  for (var i = 0; i < text.length; i++) {
    var ch = text[i];
    if (inQuotes) {
      if (ch === '"') {
        if (text[i + 1] === '"') { field += '"'; i++; }
        else { inQuotes = false; }
      } else { field += ch; }
    } else if (ch === '"') {
      inQuotes = true;
    } else if (ch === ',') {
      row.push(field); field = "";
    } else if (ch === '\n') {
      row.push(field); field = "";
      rows.push(row); row = [];
    } else if (ch !== '\r') {
      field += ch;
    }
  }
  if (field.length > 0 || row.length > 0) { row.push(field); rows.push(row); }
  return rows;
}

function sanitize(value) {
  return String(value).replace(/[^A-Za-z0-9_-]+/g, "_").replace(/^_+|_+$/g, "");
}

var doc = app.activeDocument;
if (!doc) {
  app.alert("Open a document first. Its text layers are the merge template.");
} else {
  var csvPath = patchy.args.csv || app.chooseOpenFile("Choose the CSV data file", "CSV files (*.csv *.txt)");
  var outFolder = csvPath ? (patchy.args.out || app.chooseFolder("Choose the output folder")) : "";
  if (!csvPath || !outFolder) {
    app.alert("Data merge cancelled: a CSV file and an output folder are needed.");
  } else {
    var rows = parseCsv(patchy.io.readTextFile(csvPath));
    if (rows.length < 2) {
      app.alert("The CSV needs a header row plus at least one data row.");
    } else {
      var header = rows[0];
      var columns = [];  // {name, layer} for header columns that match a text layer
      var missing = [];
      for (var c = 0; c < header.length; c++) {
        var name = header[c].trim();
        if (!name) { continue; }
        var layer = doc.findLayer(name);
        if (layer && layer.isText) {
          columns.push({ index: c, name: name, layer: layer });
        } else {
          missing.push(name);
        }
      }
      if (missing.length > 0) {
        console.warn("No text layer found for column(s): " + missing.join(", "));
      }
      if (columns.length === 0) {
        app.alert("None of the CSV columns match a text layer name in this document.\n" +
                  "Columns: " + header.join(", "));
      } else {
        var options = patchy.ui.showDialog({
          title: "Data Merge",
          fields: [
            { key: "pattern", label: "Filename ({n} = row number, {Column} = value)",
              type: "text", value: "merge-{n}" },
            { key: "format", label: "Format", type: "choice", value: "png",
              choices: ["png", "jpg", "bmp", "tif"] }
          ]
        });
        if (options) {
          // Remember the template's text so it can be restored at the end.
          var original = [];
          for (var i = 0; i < columns.length; i++) {
            original.push(columns[i].layer.text);
          }
          var exported = 0;
          for (var r = 1; r < rows.length; r++) {
            var row = rows[r];
            if (row.length === 1 && row[0].trim() === "") { continue; }  // blank line
            var fileName = options.pattern.replace(/\{n\}/g, ("00" + r).slice(-3));
            for (i = 0; i < columns.length; i++) {
              var value = columns[i].index < row.length ? row[columns[i].index] : "";
              columns[i].layer.text = value;
              fileName = fileName.split("{" + columns[i].name + "}").join(sanitize(value));
            }
            var target = outFolder + "/" + (sanitize(fileName) || "merge-" + r) + "." + options.format;
            if (doc.exportAs(target)) {
              console.log("Exported " + target);
              exported++;
            } else {
              console.warn("Could not export " + target);
            }
          }
          for (i = 0; i < columns.length; i++) {
            columns[i].layer.text = original[i];
          }
          console.log(exported + " file(s) merged from " + (rows.length - 1) +
                      " data row(s); template text restored.");
        }
      }
    }
  }
}
