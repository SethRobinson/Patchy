// Run by Testy through `patchy.exe --run-script` (drivers/patchy.py render_text_afresh
// and save_text_afresh): make Patchy draw every type layer and embedded smart object
// itself, then export a render, or save the document as a .psd (`out` ending in .psd or
// .psb), which then carries the text data Patchy writes itself.
//
// Like Photoshop, Patchy shows the pixels saved in the file for a type layer or a
// smart object until the layer is edited. layer.rerenderText() and
// layer.rerenderSmartObject() replace them with Patchy's own render and change nothing
// else. The layer names reached are printed as one JSON line. layer.rerenderText() is
// the per-layer form of Type > Update All Text Layers (`type.update_all_text_layers`):
// both open a hidden text session on the layer, mark it changed and commit it. The
// per-layer call is used because it says which layers were reached.
// Arguments: out=<path>, text=0 (leave type layers alone), smart=0 (leave smart objects
// alone).
const testyDocument = app.activeDocument;
const testyDone = [];
const testyFailed = [];
const testySmartDone = [];
const testySmartFailed = [];
const testySources = {};
function testyWalk(layers) {
  for (const layer of layers) {
    if (layer.isGroup) {
      testyWalk(layer.children);
      continue;
    }
    if (layer.isText) {
      // text=0: a font this text needs is missing on this machine, so nobody's own
      // text render is scored and the pixels saved in the file stay.
      if (patchy.args.text === "0") continue;
      try {
        // (The driver reads a font warning logged after this line as this layer's.)
        console.log(JSON.stringify({testyLayer: layer.name}));
        layer.rerenderText();
        testyDone.push(layer.name);
      } catch (error) {
        testyFailed.push(layer.name);
      }
    } else if (layer.isSmartObject) {
      if (patchy.args.smart === "0") continue;
      const state = layer.getSmartObject();
      // A linked smart object has nothing embedded to render from: not this leg's business.
      if (!state || state.linked) continue;
      // One call re-renders every layer sharing the source.
      if (testySources[state.sourceId] === undefined) {
        try {
          testySources[state.sourceId] = layer.rerenderSmartObject() > 0;
        } catch (error) {
          testySources[state.sourceId] = false;
        }
      }
      (testySources[state.sourceId] ? testySmartDone : testySmartFailed).push(layer.name);
    }
  }
}
testyWalk(testyDocument.layers);
const testyOut = String(patchy.args.out);
const testyExported = /\.ps[db]$/i.test(testyOut) ? testyDocument.saveAs(testyOut) : testyDocument.exportAs(testyOut);
console.log(JSON.stringify({testyTextAfresh: true, exported: testyExported, done: testyDone, failed: testyFailed,
                            smartDone: testySmartDone, smartFailed: testySmartFailed}));
