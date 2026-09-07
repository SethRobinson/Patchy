// Inspect get_state and get_preview first. Required documentId and shape layerId.
// action=inspect (default) is read-only. action=create creates a saved path and mask.
// action=selection fits the current selection and explicitly stores it as a work path.
// Inspect another preview after each action; use output to save a reviewed checkpoint.
var doc=app.getDocument(patchy.args.documentId), layer=doc.getLayer(patchy.args.layerId);
var shape=layer.getShape();
var maskTarget=layer;
if (!shape || !shape.path) { throw new Error('Choose a parsed shape layer'); }
if (patchy.args.action === 'create') {
  var saved=doc.addPath('Reusable outline',shape.path);
  maskTarget=doc.groupLayers([layer],'Masked artwork');
  maskTarget.setVectorMask({path:saved.getPath(),enabled:true,linked:true,density:100,feather:0});
  saved.activate();
}
if (patchy.args.action === 'selection') {
  doc.selection.fromPath(shape.path,{operation:'replace',antialias:true,feather:0});
  doc.setWorkPath(doc.selection.toPath({tolerance:1}));
}
if (patchy.args.output && !doc.saveAs(patchy.args.output)) { throw new Error('Save failed'); }
patchy.setResult({documentId:doc.id,layerId:layer.id,maskLayerId:maskTarget.id,mask:maskTarget.getVectorMask(),
  paths:doc.paths.map(function(p){return {id:p.id,name:p.name,kind:p.kind};})});
