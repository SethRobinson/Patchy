// @name Play Button Overlay
// @description Centers a click-to-watch play-button badge on the active
// @description document, for thumbnails that link to a video: generic dark
// @description rounded or circle styles, or YouTube red.
// @author Seth A. Robinson
//
// Made for thumbnails that open a video link (for example a GitHub README
// image that launches a YouTube video). The badge is rendered by math onto
// its own layer, so afterwards it can be moved, faded, or deleted freely.
//
// The "YouTube red" style approximates the YouTube play button. YouTube is a
// trademark of Google LLC; Patchy is not affiliated with or endorsed by
// Google. Using the logo to indicate a video hosted on YouTube is normally
// fine, but for anything high-stakes follow YouTube's brand guidelines and
// prefer their official downloadable assets:
// https://www.youtube.com/howyoutubeworks/resources/brand-resources/

// ---------------------------------------------------------------------------
// Options - defaults for this script. The options dialog (GUI runs) and
// --script-arg key=value (command line) override them.
var OPTIONS = {
  style: "Dark rounded",  // Dark rounded, Dark circle, YouTube red
  size: 30,               // 10..80 - badge width as a % of the shorter edge
  opacity: 90             // 20..100 layer opacity
};
// ---------------------------------------------------------------------------

var doc = app.activeDocument;
if (!doc) {
  app.alert("Open a document first.");
} else {
  var options = patchy.ui.showOptions({
    title: "Play Button Overlay",
    description: "Centers a play-button badge on this document - the click-to-watch " +
                 "marker for a thumbnail that links to a video. The badge lands on " +
                 "its own \"Play button\" layer, so it can still be moved, faded, or " +
                 "deleted afterwards.",
    fields: [
      { key: "style", label: "Style", type: "choice", value: OPTIONS.style,
        choices: ["Dark rounded", "Dark circle", "YouTube red"] },
      { key: "size", label: "Size (% of short edge)", type: "slider",
        value: OPTIONS.size, min: 10, max: 80 },
      { key: "opacity", label: "Opacity", type: "slider",
        value: OPTIONS.opacity, min: 20, max: 100 }
    ]
  });
  if (options) {
    var isCircle = options.style === "Dark circle";
    var badgeColor = options.style === "YouTube red"
        ? { r: 255, g: 0, b: 0 } : { r: 20, g: 20, b: 20 };

    // Badge geometry, sized from the shorter canvas edge. The rounded-rect
    // styles use a 10:7 box with corners at 24% of its height; the triangle
    // is 42% of the badge height, slightly wider than tall, and nudged a
    // little right of center so it LOOKS centered.
    var short = Math.min(doc.width, doc.height);
    var badgeW = Math.max(8, Math.round(short * options.size / 100));
    var badgeH = isCircle ? badgeW : Math.round(badgeW * 0.7);
    var cornerR = isCircle ? badgeW / 2 : badgeH * 0.24;
    var triH = badgeH * (isCircle ? 0.40 : 0.42);
    var triW = triH * 0.88;
    var nudge = triW * 0.1;

    // Triangle vertices relative to the badge center (y grows downward).
    var tri = [
      { x: -triW / 2 + nudge, y: -triH / 2 },
      { x: -triW / 2 + nudge, y: triH / 2 },
      { x: triW / 2 + nudge, y: 0 }
    ];
    // Outward unit normal per edge (flipped away from the opposite vertex),
    // so the signed distance below is max over the three half-planes.
    var edges = [];
    for (var e = 0; e < 3; e++) {
      var p = tri[e];
      var q = tri[(e + 1) % 3];
      var r = tri[(e + 2) % 3];
      var nx = q.y - p.y;
      var ny = p.x - q.x;
      var len = Math.sqrt(nx * nx + ny * ny);
      nx /= len; ny /= len;
      if ((r.x - p.x) * nx + (r.y - p.y) * ny > 0) { nx = -nx; ny = -ny; }
      edges.push({ px: p.x, py: p.y, nx: nx, ny: ny });
    }

    function coverage(d) { return Math.min(1, Math.max(0, 0.5 - d)); }

    var pad = 2;  // room for the anti-aliased edge
    var bufW = badgeW + pad * 2;
    var bufH = badgeH + pad * 2;
    var data = new Uint8Array(bufW * bufH * 4);
    var halfW = badgeW / 2 - cornerR;
    var halfH = badgeH / 2 - cornerR;
    for (var py = 0; py < bufH; py++) {
      for (var px = 0; px < bufW; px++) {
        var x = px + 0.5 - bufW / 2;
        var y = py + 0.5 - bufH / 2;
        // Signed distance to the badge silhouette (rounded rect or circle).
        var qx = Math.max(0, Math.abs(x) - halfW);
        var qy = Math.max(0, Math.abs(y) - halfH);
        var badgeCov = coverage(Math.sqrt(qx * qx + qy * qy) - cornerR);
        if (badgeCov <= 0) { continue; }
        var triD = -1e9;
        for (var t = 0; t < 3; t++) {
          var ed = edges[t];
          triD = Math.max(triD, (x - ed.px) * ed.nx + (y - ed.py) * ed.ny);
        }
        var triCov = coverage(triD);
        var i = (py * bufW + px) * 4;
        data[i] = Math.round(badgeColor.r + (255 - badgeColor.r) * triCov);
        data[i + 1] = Math.round(badgeColor.g + (255 - badgeColor.g) * triCov);
        data[i + 2] = Math.round(badgeColor.b + (255 - badgeColor.b) * triCov);
        data[i + 3] = Math.round(badgeCov * 255);
      }
    }

    var x0 = Math.round((doc.width - bufW) / 2);
    var y0 = Math.round((doc.height - bufH) / 2);
    var layer = doc.addLayer("Play button");
    layer.fillRect(x0, y0, bufW, bufH, "#00000000");  // allocate the buffer
    layer.setPixels({ x: x0, y: y0, width: bufW, height: bufH, data: data.buffer });
    layer.opacity = options.opacity;
    console.log(options.style + " play button added (" + badgeW + "x" + badgeH +
                " px, opacity " + options.opacity + ").");
  }
}
