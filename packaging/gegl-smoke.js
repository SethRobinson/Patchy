// @name GEGL smoke check
// @description The packagers' headless check that Patchy's own GEGL ships and runs
// @description (docs/gegl-filters.md): filters list, a removed one is absent, the GPL-3
// @description module loads, and a blur changes the pixels. Throws on the first failure.
//
// Run with --headless --run-script by packaging/macos/make-dmg.sh,
// packaging/linux/make-flatpak.sh and packaging/linux/appimage/build-in-container.sh,
// before any artifact is written. Windows packages run the same checks in
// packaging/package-selftest.js.

function check(condition, message) {
  if (!condition) throw new Error("GEGL smoke check: " + message);
}

console.log("headless smoke");
var gegl = patchy.gegl.filters();
function offered(id) {
  return gegl.some(function (filter) { return filter.id === id; });
}
check(gegl.length >= 100, "only " + gegl.length + " GEGL filters are listed");
check(offered("gegl.gaussian-blur"), "gegl.gaussian-blur is missing");
// The GPL-3 module ships (docs/legal-licensing.md) and a removed operation does not.
check(offered("gegl.cartoon"), "GEGL's GPL-3 filters are missing");
check(!offered("gegl.median-blur"), "an excluded GEGL filter is offered");

var doc = app.newDocument(32, 16, {name: "gegl smoke"});
var layer = doc.activeLayer;
layer.fillRect(0, 0, 16, 16, "#ff0000");
layer.fillRect(16, 0, 16, 16, "#0000ff");
layer.applyFilter("gegl.gaussian-blur", {"std-dev-x": 3, "std-dev-y": 3});
var block = layer.getPixels();
var pixels = new Uint8Array(block.data);
// The last red column before the edge: the blur brings blue into it.
var edge = ((8 - block.y) * block.width + (15 - block.x)) * 4;
check(pixels[edge + 2] > 0 && pixels[edge] < 255,
  "gegl.gaussian-blur left the pixels unchanged (" + pixels[edge] + ", " + pixels[edge + 2] + ")");
layer.applyFilter("gegl.cartoon", {});
console.log("gegl ok: " + gegl.length + " filters");
