// New comparison document: coverage Wet Edges versus native canvas pickup.
var doc = app.newDocument(480, 320);
doc.activeLayer.fill('#eee0c7');
var wash = doc.addLayer('Wet-edge wash');
wash.drawStrokes([{size:48,color:'#487b8a',flow:45,dynamics:{wetEdges:true},
  points:[{x:50,y:65},{x:180,y:45},{x:270,y:90},{x:400,y:60}]}]);
if (patchy.args.watch === 'true') patchy.ui.present(60);
var ground = doc.addLayer('Colors to blend');
ground.fillRect(35,180,205,85,'#a3532a'); ground.fillRect(240,180,205,85,'#d7b870');
var blend = doc.addLayer('Mixer correction');
blend.drawStrokes([{tool:'mixer',size:48,flow:35,color:'#c49051',
  mixer:{wet:55,load:60,mix:80,sampleAllLayers:true},
  points:[{x:90,y:205},{x:200,y:225},{x:300,y:205},{x:400,y:230}]}]);
if (patchy.args.watch === 'true') patchy.ui.present(60);
if(patchy.args.out) {
  doc.renderPreview(patchy.args.out+'/wet-paint.png');
  if(!doc.saveAs(patchy.args.out+'/wet-paint.psd')) throw Error('PSD save failed');
}
patchy.setResult({documentId:doc.id,washLayerId:wash.id,mixerLayerId:blend.id});
