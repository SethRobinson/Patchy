// @name Letter Physics
// @description Turns a word into one text layer per letter and drops them onto
// @description the bottom of the document with gravity and bounce. The letters
// @description stay editable text layers after they settle.
// @author Seth A. Robinson
//
// Undo removes the whole run in one step.

// ---------------------------------------------------------------------------
// Options - defaults for this script. The options dialog (GUI runs) and
// --script-arg key=value (command line) override them.
var OPTIONS = {
  word: "PATCHY",
  gravity: 60,   // 10..100 - how hard the letters fall
  bounce: 55     // 0..90 - how much energy a bounce keeps
};
// ---------------------------------------------------------------------------

var doc = app.activeDocument;
if (!doc) {
  app.alert("Open a document first.");
} else {
  var choice = patchy.ui.showOptions({
    title: "Letter Physics",
    description: "Drops your word onto the bottom of this document, one bouncing letter at a " +
                 "time. Each letter is a real text layer, so after the dust settles you can " +
                 "still edit, restyle, or rearrange them. Undo removes the whole drop.",
    fields: [
      { key: "word", label: "Word to drop", type: "text", value: OPTIONS.word },
      { key: "gravity", label: "Gravity", type: "slider", value: OPTIONS.gravity,
        min: 10, max: 100 },
      { key: "bounce", label: "Bounciness", type: "slider", value: OPTIONS.bounce,
        min: 0, max: 90 }
    ]
  });
  var word = choice ? (choice.word || "PATCHY") : "";
  if (word) {

  var size = Math.max(24, Math.round(doc.width / (word.length + 2)));
  var letters = [];
  var startX = Math.round(doc.width * 0.08);
  var x = startX;
  for (var i = 0; i < word.length; i++) {
    var ch = word.charAt(i);
    if (ch === " ") {
      x += Math.round(size * 0.5);
      continue;
    }
    var layer = doc.addTextLayer(ch, {
      size: size,
      x: x,
      y: Math.round(size * 1.2),
      color: "#" + ((Math.random() * 0xffffff) | 0).toString(16).padStart(6, "0"),
      bold: true
    });
    letters.push({
      layer: layer,
      x: layer.x,
      y: layer.y,
      vx: (Math.random() - 0.5) * 3,
      vy: Math.random() * 2,
      height: layer.bounds.height
    });
    x += layer.bounds.width + Math.round(size * 0.1);
  }

  if (letters.length === 0) {
    console.log("Nothing to drop.");
  } else {
    console.log("Dropping " + letters.length + " letters...");
    var gravity = choice.gravity / 100;
    var bounce = choice.bounce / 100;
    var floor = doc.height;
    var elapsed = 0;
    var timer = setInterval(function (dt) {
      elapsed += dt;
      var moving = false;
      for (var i = 0; i < letters.length; i++) {
        var l = letters[i];
        l.vy += gravity;
        l.x += l.vx;
        l.y += l.vy;
        if (l.y + l.height >= floor) {
          l.y = floor - l.height;
          l.vy = -l.vy * bounce;
          l.vx *= 0.9;
          if (Math.abs(l.vy) < 1.5) {
            l.vy = 0;
          }
        }
        if (l.x < 0) { l.x = 0; l.vx = Math.abs(l.vx); }
        if (l.x + l.layer.bounds.width > doc.width) {
          l.x = doc.width - l.layer.bounds.width;
          l.vx = -Math.abs(l.vx);
        }
        l.layer.moveTo(Math.round(l.x), Math.round(l.y));
        if (Math.abs(l.vx) > 0.1 || l.vy !== 0 || l.y + l.height < floor - 1) {
          moving = true;
        }
      }
      if (!moving || elapsed > 12000) {
        clearInterval(timer);
        console.log("Letters settled after " + Math.round(elapsed / 1000) + "s. Undo removes the drop in one step.");
      }
    }, 16);
  }
  }
}
