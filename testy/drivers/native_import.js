// Each invocation runs in its own headless Patchy instance on a staged source.
function describeLayer(layer, masks) {
    var kind = "layer";
    if (layer.isArtboard) kind = "artboard";
    else if (layer.isGroup) kind = "group";
    else if (layer.isText) kind = "text";
    else if (layer.isShape) kind = "shape";
    else if (layer.isSmartObject) kind = "smartObject";
    var result = {
        name: layer.name, kind: kind, visible: layer.visible, opacity: layer.opacity,
        blendMode: layer.blendMode, locked: layer.locked, bounds: layer.bounds,
        children: layer.children.map(function (child) { return describeLayer(child, masks); })
    };
    // getLayerMask() also reads its pixels. Only do that work for a mask assertion.
    if (masks) result.hasMask = layer.getLayerMask() !== null;
    return result;
}

var result = {version: app.version, opened: false, inspected: false, exported: false, notes: []};
function checkpoint() {
    patchy.io.writeTextFile(patchy.args.checkpoint, JSON.stringify(result));
}
checkpoint();
try {
    var doc = app.open(patchy.args.input);
    result.opened = true;
    checkpoint();
    result.notes = doc.importNotes;
    result.width = doc.width;
    result.height = doc.height;
    result.depth = doc.bitDepth;
    result.profile = doc.colorProfile;
    result.layers = doc.layers.map(function (layer) {
        return describeLayer(layer, patchy.args.masks === "yes");
    });
    result.layerCount = result.layers.length;
    result.paths = doc.paths.map(function (p) { return {name: p.name, kind: p.kind}; });
    result.inspected = true;
    checkpoint();
    result.exported = patchy.args.animated === "yes"
        ? doc.exportAnimatedPng(patchy.args.output)
        : doc.exportAs(patchy.args.output);
    if (!result.exported) result.error = "Patchy could not export a PNG.";
    if (patchy.args.floatOutput && doc.bitDepth === 32) {
        checkpoint();
        result.floatExported = doc.exportAs(patchy.args.floatOutput);
    }
} catch (error) {
    result.error = String(error);
}
console.log("TESTY_NATIVE " + JSON.stringify(result));
