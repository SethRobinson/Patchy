// Standalone CLI/MCP example. Set patchy.args.out to an existing output folder.
var doc = app.newDocument(32, 32);
doc.activeLayer.fill('#14243b');
var sprite = doc.addLayer('Lantern');
var palette = ['#00000000', '#304b63', '#ffe28b', '#ed9a44'];
var rows = ['00011000', '00111100', '01333310', '01222210',
            '01222210', '01333310', '00111100', '00011000'];
var rgba = new Uint8Array(8 * 8 * 4);
for (var y = 0; y < 8; ++y) {
  for (var x = 0; x < 8; ++x) {
    var index = Number(rows[y][x]);
    var hex = palette[index];
    var offset = (y * 8 + x) * 4;
    if (index) {
      rgba[offset] = parseInt(hex.slice(1, 3), 16);
      rgba[offset + 1] = parseInt(hex.slice(3, 5), 16);
      rgba[offset + 2] = parseInt(hex.slice(5, 7), 16);
      rgba[offset + 3] = 255;
    }
  }
}
sprite.setPixels({x: 12, y: 11, width: 8, height: 8, data: rgba.buffer});
if (patchy.args.out) {
  if (!doc.saveAs(patchy.args.out + '/lantern.psd')) { throw new Error('PSD save failed'); }
  doc.renderPreview(patchy.args.out + '/lantern.png', {maxWidth: 512, maxHeight: 512, nearestNeighbor: true});
}
patchy.setResult({documentId: doc.id, layerId: sprite.id});
