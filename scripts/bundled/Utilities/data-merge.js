// @name Data Merge
// @description Mail-merge for images: fills the active document's text layers
// @description from a CSV file and exports one image per data row - name
// @description badges, cards, certificates.
// @author Seth A. Robinson
// @cli --script-arg csv=C:\data\people.csv --script-arg out=C:\data\output template.psd
//
// The CSV's header row names the text layers to fill; each following row
// becomes one exported file. The template document is restored afterwards.
// Unattended runs (patchy --run-script) pass --script-arg csv=... and
// --script-arg out=... (plus any other option keys).

// ---------------------------------------------------------------------------
// Options - defaults for this script. The options dialog (GUI runs) and
// --script-arg key=value (command line) override them.
var OPTIONS = {
  csv: "",              // CSV data file; empty = pick in the dialog
  out: "",              // output folder; empty = pick in the dialog
  pattern: "merge-{n}", // filename: {n} = row number, {Column} = that row's value
  format: "png"         // png, jpg, bmp, tif
};
// ---------------------------------------------------------------------------

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
  var setup = patchy.ui.showOptions({
    title: "Data Merge",
    description: "Mail-merge for images. This document is the template: every text layer " +
                 "whose NAME matches a CSV column header gets filled with that column's " +
                 "value, and each data row is exported as its own image.\n\n" +
                 "1. Pick a CSV file - its first row holds the column names.\n" +
                 "2. Pick the output folder.\n" +
                 "3. The filename pattern can use {n} for the row number and {Column} for a " +
                 "value (e.g. badge-{Name}).\n\n" +
                 "The template's own text is restored when the merge finishes.",
    fields: [
      { key: "csv", label: "CSV data file", type: "file", value: OPTIONS.csv,
        filter: "CSV files (*.csv *.txt)" },
      { key: "out", label: "Output folder", type: "folder", value: OPTIONS.out },
      { key: "pattern", label: "Filename ({n} = row number, {Column} = value)",
        type: "text", value: OPTIONS.pattern },
      { key: "format", label: "Format", type: "choice", value: OPTIONS.format,
        choices: ["png", "jpg", "bmp", "tif"] }
    ]
  });
  var csvPath = setup ? setup.csv : "";
  var outFolder = setup ? setup.out : "";
  if (setup && (!csvPath || !outFolder)) {
    app.alert("Data merge cancelled: a CSV file and an output folder are needed.\n\n" +
              "Re-run it and use the Browse buttons to pick them.");
  } else if (setup) {
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
        var options = { pattern: setup.pattern, format: setup.format };
        {
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
