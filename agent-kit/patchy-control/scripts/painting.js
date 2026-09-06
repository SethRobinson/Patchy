// Native strokes; pass patchy.args.out to save an editable checkpoint and preview.
var doc = app.newDocument(320, 240);
doc.activeLayer.fill('#f8ecd5');
var ink = doc.addLayer('Blue ink');
ink.drawStrokes([
  {color: '#275474', size: 24, softness: 35, flow: 35, opacity: 80, seed: 7,
   points: [{x: 35, y: 150, pressure: 0.2}, {x: 95, y: 65, pressure: 0.8},
            {x: 180, y: 180, pressure: 1}, {x: 285, y: 70, pressure: 0.15}]},
  {color: '#bc6945', size: 10, opacity: 100, flow: 100, softness: 0, seed: 8,
   points: [{x: 40, y: 200}, {x: 280, y: 200}]}
]);
if (patchy.args.out) {
  if (!doc.saveAs(patchy.args.out + '/painting.psd')) { throw new Error('PSD save failed'); }
  doc.renderPreview(patchy.args.out + '/painting.png');
}
patchy.setResult({documentId: doc.id, layerId: ink.id});
