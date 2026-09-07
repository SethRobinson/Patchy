// Persistent resources: explicitly opt into saving a new preset with save=true.
// Pass abr=<path> to import an ABR and inspect all warnings.
if (patchy.args.abr) console.log(JSON.stringify(patchy.brushes.importAbr(patchy.args.abr)));
var created = null;
if(patchy.args.save === 'true') {
  var data = new Uint8Array(24*12);
  for(var y=1;y<11;y++) for(var x=1;x<23;x++) data[y*24+x]=(y%3===0)?40:220;
  var tip=patchy.brushes.createTip('Directional bristle study',{width:24,height:12,data:data.buffer},{spacing:.16});
  created=patchy.brushes.savePreset('Fur study brush',{tipId:tip.id,size:22,flow:35,
    dynamics:{angleControl:'direction',sizeControl:'penPressure',opacityControl:'off',textureEnabled:true,textureDepth:.25}});
}
patchy.setResult({created:created,presets:patchy.brushes.listPresets()});
