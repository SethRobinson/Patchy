// @name Quick Export DDS
// @description Writes the active document next to its file as a .dds texture
// @description with the compression and mipmap choices below. No dialog: one
// @description keypress, one file. Shows how to pass save options from a script.
// @author Seth A. Robinson
// @cli --script-arg compression=bc1 example.png
//
// Runs instantly. Open a saved document, run the script (or press its hotkey)
// and <name>.dds lands beside the document's file, overwriting an older one.
// The document itself is untouched: exportAs writes a copy and never changes
// the document's own path or modified state.
//
// PUTTING IT ON A HOTKEY
//
// Bundled scripts never ship with a shortcut (it could collide with yours), so
// make the script your own first: open it in the Script Manager (File >
// Scripts > Script Manager...), change OPTIONS below if you like, and press
// Save. That writes a copy into your scripts folder, and the copy is what the
// menu runs from then on. Then pick a key, in either of two ways:
//
//   1. Add a header line to the copy, for example
//        // @hotkey Ctrl+Alt+D
//      That is the script's default shortcut. A key Patchy already uses stays
//      with Patchy, and Preferences shows a note explaining which command won.
//   2. Preferences > Hotkeys lists every script under "Scripts": click the
//      shortcut chip and press the key. (Right-click a script in the Script
//      Manager and choose "Assign Hotkey..." to land on its row.) A choice made
//      here always wins over the @hotkey line.
//
// Either way the binding is stored in your Patchy settings under a stable id
// built from the script's path in the scripts folder, so it survives updating
// Patchy and editing the script. Renaming or moving the script file starts a
// new id (assign the key again).
//
// SAVE OPTIONS
//
// doc.saveAs(path, options) and doc.exportAs(path, options) take the same
// choices the Save Options dialog offers for that format, with no dialog.
// Keys that do not apply to the extension throw, so typos never save with
// defaults. Some examples (see patchy.d.ts and the scripting guide for every
// format):
//
//   doc.exportAs("tex.dds",  {compression: "bc3", mipmaps: "on"});
//   doc.exportAs("web.jpg",  {quality: 85});
//   doc.exportAs("web.webp", {quality: 80, lossless: false});
//   doc.exportAs("app.ico",  {sizes: [16, 32, 48, 256], resample: "smooth"});
//   doc.exportAs("page.pdf", {imageQuality: "high", editableLayers: true});

// ---------------------------------------------------------------------------
// Options - tweak here, or override with --script-arg compression=bc1 etc.
var OPTIONS = {
  compression: "bc3",  // "auto" (BC1 when opaque, BC3 with transparency; a .dds
                       // source keeps its own format), "uncompressed", "bc1",
                       // "bc3", "bc4", "bc5", "bc7"
  mipmaps: "on",       // "on" (full chain), "off" (level 0 only), "auto"
                       // (follow the opened .dds, otherwise a chain)
  suffix: ""           // added before ".dds": "_tex" turns hero.png into hero_tex.dds
};
// ---------------------------------------------------------------------------

var doc = app.activeDocument;
if (!doc) {
  app.alert("Open a document first.");
} else if (!doc.path) {
  app.alert("Save the document first so Quick Export DDS knows which folder to write to.");
} else {
  var compression = patchy.args.compression || OPTIONS.compression;
  var mipmaps = patchy.args.mipmaps || OPTIONS.mipmaps;
  var suffix = patchy.args.suffix !== undefined ? patchy.args.suffix : OPTIONS.suffix;
  var target = doc.path.replace(/\.[^.\\\/]+$/, "") + suffix + ".dds";
  if (doc.exportAs(target, {compression: compression, mipmaps: mipmaps})) {
    console.log("Exported " + target + " (" + compression + ", mipmaps " + mipmaps + ")");
    patchy.ui.setStatusMessage("Exported " + target);
  } else {
    app.alert("Could not write " + target);
  }
}
