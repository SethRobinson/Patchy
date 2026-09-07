// Time controls paint; present controls only how long the user sees a frame.
var doc=app.newDocument(320,200);doc.activeLayer.fill('#e9dcc8');
var layer=doc.addLayer('Timed airbrush');
layer.drawStrokes([{size:50,flow:8,color:'#81566e',airbrush:true,
  smoothing:{amount:18,catchUp:true},
  points:[{x:55,y:110,timeMs:0},{x:155,y:65,timeMs:100},
    {x:260,y:110,timeMs:250},{x:260,y:110,timeMs:900}]}]);
if(patchy.args.watch==='true')patchy.ui.present(60);
if(patchy.args.out) {
  doc.renderPreview(patchy.args.out+'/timed-brush.png');
  if(!doc.saveAs(patchy.args.out+'/timed-brush.psd'))throw Error('PSD save failed');
}
patchy.setResult({documentId:doc.id,layerId:layer.id});
