// @name Dialog Showcase
// A tour of every form-dialog field type a script can ask for - number,
// slider, checkbox, choice, text, and color - plus app.prompt and the file
// pickers. Whatever you answer is rendered as a name badge in the active
// document, so you can see exactly how the values come back to the script.
// (See patchy.d.ts for the full patchy.ui.showDialog reference.)

var doc = app.activeDocument;
if (!doc) {
  app.alert("Open a document first.");
} else {
  var options = patchy.ui.showDialog({
    title: "Dialog Showcase",
    fields: [
      { key: "name", label: "Your name (text)", type: "text", value: "Patchy Artist" },
      { key: "title", label: "Job title (choice)", type: "choice", value: "Pixel Wizard",
        choices: ["Pixel Wizard", "Layer Wrangler", "Brush Whisperer", "Undo Enthusiast"] },
      { key: "accent", label: "Accent color (color)", type: "color", value: "#3fa7ff" },
      { key: "stars", label: "Skill stars (number)", type: "number", value: 3, min: 0, max: 5 },
      { key: "badgeWidth", label: "Badge width (slider)", type: "slider",
        value: 340, min: 240, max: 600 },
      { key: "darkCard", label: "Dark card (checkbox)", type: "checkbox", value: true },
      { key: "demoPickers", label: "Also demo the file pickers", type: "checkbox", value: false }
    ]
  });
  if (!options) {
    console.log("showDialog returned null: the user cancelled.");
  } else {
    console.log("showDialog returned: " + JSON.stringify(options));

    var tagline = app.prompt("app.prompt example - badge tagline:", "Makes pixels behave");
    console.log("app.prompt returned: " + JSON.stringify(tagline));

    if (options.demoPickers) {
      var picked = app.chooseOpenFile("app.chooseOpenFile demo (cancel is fine)");
      console.log("chooseOpenFile returned: " + JSON.stringify(picked) +
                  (picked ? "" : " (cancelled pickers return an empty string)"));
    }

    // Render the answers as a badge card in the document's top-left corner.
    var x = 24;
    var y = 24;
    var width = options.badgeWidth;
    var height = 120;
    var cardColor = options.darkCard ? "#262a30" : "#f2f2f0";
    var inkColor = options.darkCard ? "#eceff2" : "#24282e";
    var subColor = options.darkCard ? "#9aa4ae" : "#5c646d";

    var card = doc.addLayer("Badge card");
    card.fillRect(x, y, width, height, options.accent);
    card.fillRect(x + 3, y + 3, width - 6, height - 6, cardColor);
    card.fillRect(x, y, 8, height, options.accent);  // spine

    var name = doc.addTextLayer(options.name, {
      size: 22, bold: true, x: x + 24, y: y + 16, color: inkColor
    });
    name.name = "Badge name";
    var title = doc.addTextLayer(options.title, {
      size: 13, x: x + 24, y: y + 48, color: subColor
    });
    title.name = "Badge title";
    if (tagline) {
      var motto = doc.addTextLayer("\"" + tagline + "\"", {
        size: 12, italic: true, x: x + 24, y: y + 70, color: subColor
      });
      motto.name = "Badge tagline";
    }
    var starText = "";
    for (var s = 0; s < 5; s++) { starText += s < options.stars ? "★" : "☆"; }
    var stars = doc.addTextLayer(starText, {
      size: 16, x: x + 24, y: y + 92, color: options.accent
    });
    stars.name = "Badge stars";

    console.log("Badge rendered. One undo removes the whole thing.");
  }
}
