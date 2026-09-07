// First run with documentId and layerId to inspect. Preview before changing it.
// Repeat with action=revise to edit the same native shape, then preview again.
// Pass output only after reviewing the result. MCP supplies expectedState separately.
var doc=app.getDocument(patchy.args.documentId), layer=doc.getLayer(patchy.args.layerId);
var before=layer.getShape();
if (!before || !before.editable) { throw new Error('Choose an editable shape layer'); }
if (patchy.args.action === 'revise') {
  layer.updateShape({fill:'#efb674',stroke:{enabled:true,width:4,paint:'#733f32',alignment:'inside'}});
}
if (patchy.args.output && !doc.saveAs(patchy.args.output)) { throw new Error('Save failed'); }
patchy.setResult({documentId:doc.id,layerId:layer.id,before:before,after:layer.getShape()});
