// Compare direction and pressure before painting a large subject. No library writes.
var tips = patchy.brushes.listTips();
var tip = tips.filter(function(t) { return /bristle|chalk|oil/i.test(t.name); })[0];
var doc = app.newDocument(640, 400);
doc.activeLayer.fill('#c6b196');
var fur = doc.addLayer('Directional fur tests');
var strokes = [];
for (var tuft = 0; tuft < 4; tuft++) {
  for (var i = 0; i < 55; i++) {
    var x = 90 + tuft * 150 + Math.sin(i * 2.4) * 42;
    var y = 90 + (i % 11) * 15;
    var s = {
      color: i % 3 ? '#9c6137' : '#ead5aa', size: tuft < 2 ? 15 : 8,
      flow: 75, spacing: .12, seed: i, roundness: 40,
      dynamics: {angleControl: 'direction', sizeControl: 'penPressure',
        minimumDiameter: .2, opacityControl: 'off',
        textureEnabled: tuft % 2 === 1, textureStyle: 'canvas', textureDepth: .5},
      points: [{x: x, y: y, pressure: .3}, {x: x + 6, y: y + 18, pressure: 1},
               {x: x + 12, y: y + 34, pressure: .08}]
    };
    if (tip) s.tipId = tip.id;
    strokes.push(s);
  }
}
fur.drawStrokes(strokes);
if (patchy.args.watch === 'true') patchy.ui.present(60);
if (patchy.args.out) {
  doc.saveAs(patchy.args.out + '/fur-strokes.psd');
  doc.renderPreview(patchy.args.out + '/fur-strokes.png');
}
patchy.setResult({documentId: doc.id, tip: tip || 'Round',
  compare: 'Left pair: broad locks. Right pair: fine strands. Each pair: smooth versus textured.'});
