// Required string arguments: input (existing file), output (new PSD path).
if (!patchy.args.input || !patchy.args.output) { throw new Error('Pass input and output paths'); }
var doc = app.open(patchy.args.input);
var layer = doc.addLayer('Accent');
layer.drawStrokes([{color: '#e8ad55', size: 3, opacity: 100, flow: 100, softness: 0, seed: 0,
  points: [{x: 4, y: 4}, {x: Math.max(4, doc.width - 5), y: 4}]}]);
if (!doc.saveAs(patchy.args.output)) { throw new Error('PSD save failed'); }
patchy.setResult({documentId: doc.id, path: doc.path});
