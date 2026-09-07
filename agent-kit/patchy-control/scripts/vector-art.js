// Native editable artwork, in small batches. No raster imports or SVG parsing.
// Run stage=1, inspect get_preview, then run stages 2..7 with the returned documentId.
// Stage 7 revises an existing curve and appearance. Save with stage=8 and output=<folder>.
// MCP execute_script arguments go in args, all values strings. CLI uses --script-arg.
var stage = Number(patchy.args.stage || '1');
var doc = stage === 1 ? app.newDocument(960, 960) : app.getDocument(patchy.args.documentId);
if (!doc || stage < 1 || stage > 8) { throw new Error('Choose stage 1..8 and an existing documentId after stage 1'); }
doc.activate();
var batch = [];
var ink = '#733f32', orange = '#efaa60', cream = '#fff0ce', stripe = '#cf7944';
function shape(name, geometry, fill, width) {
  var layer = doc.addShape(name, geometry, {fill: fill,
    stroke: {enabled: !!width, width: width || 0, paint: ink, alignment: 'center', cap: 'round', join: 'round'}});
  batch.push(layer); return layer;
}
function ellipse(name, x, y, w, h, fill, width) {
  return shape(name, {type: 'ellipse', x:x, y:y, width:w, height:h}, fill, width);
}
// Cubic segments: [control1 x,y, control2 x,y, end x,y]. Anchors and handles
// remain document-pixel numbers that getShape() can inspect and update later.
function curve(name, start, segments, closed, fill, width) {
  var anchors = [{x:start[0], y:start[1]}];
  segments.forEach(function(s) {
    var previous = anchors[anchors.length - 1]; previous.outX=s[0]; previous.outY=s[1];
    anchors.push({x:s[4], y:s[5], inX:s[2], inY:s[3]});
  });
  if (closed && anchors.length > 2) {
    var last=anchors[anchors.length-1];
    if (last.x===anchors[0].x && last.y===anchors[0].y) {
      anchors[0].inX=last.inX; anchors[0].inY=last.inY; anchors.pop();
    }
  }
  return shape(name, {type:'path',path:{subpaths:[{closed:closed,anchors:anchors}]}}, fill, width);
}
if (stage === 1) {
  doc.addFillLayer('Mint backdrop', '#d9ebe4');
  ellipse('Soft halo', 105, 88, 750, 750, '#f2f5e7');
  ellipse('Ground shadow', 237, 786, 485, 63, '#bbd5c8');
  ellipse('Halo dot left', 147, 433, 17, 17, '#97bfb2');
  ellipse('Halo dot right', 807, 485, 13, 13, '#97bfb2');
}
if (stage === 2) {
  curve('Curled tail', [623,759], [[777,783,853,676,800,598],[767,551,708,588,737,621],
    [763,650,744,710,667,688],[629,683,598,729,623,759]], true, orange, 8);
  ellipse('Round body', 287, 478, 386, 339, orange, 8);
  ellipse('Cream tummy', 352, 570, 253, 226, cream);
  curve('Left ear', [276,344], [[251,298,232,188,257,169],[279,153,362,226,395,278],
    [389,313,319,351,276,344]], true, orange, 8);
  curve('Right ear', [569,278], [[600,227,681,155,705,171],[730,190,710,294,691,341],
    [649,356,580,318,569,278]], true, orange, 8);
  curve('Left inner ear', [279,278], [[269,251,262,208,271,202],[283,199,325,239,341,261],
    [323,269,298,278,279,278]], true, '#e89b91');
  curve('Right inner ear', [621,261], [[638,239,679,199,691,203],[700,210,692,252,681,280],
    [659,278,639,272,621,261]], true, '#e89b91');
  curve('Head', [481,237], [[623,235,713,314,717,426],[726,531,636,601,483,603],
    [326,606,235,541,241,431],[244,323,337,240,481,237]], true, orange, 8);
}
if (stage === 3) {
  ellipse('Left cheek cream', 298, 435, 198, 119, cream);
  ellipse('Right cheek cream', 467, 435, 198, 119, cream);
  ellipse('Left eye', 340, 354, 84, 103, '#56372f');
  ellipse('Right eye', 541, 354, 84, 103, '#56372f');
  ellipse('Left eye warm base', 355, 419, 52, 25, '#ac7046');
  ellipse('Right eye warm base', 556, 419, 52, 25, '#ac7046');
  ellipse('Left eye sparkle', 352, 366, 30, 34, '#fffaf0');
  ellipse('Right eye sparkle', 553, 366, 30, 34, '#fffaf0');
  ellipse('Left eye glint', 392, 406, 13, 13, '#fffaf0');
  ellipse('Right eye glint', 593, 406, 13, 13, '#fffaf0');
  ellipse('Left blush', 299, 459, 70, 36, '#eb9990');
  ellipse('Right blush', 597, 459, 70, 36, '#eb9990');
  curve('Heart nose', [466,466], [[453,451,460,442,473,447],[481,451,482,451,491,447],
    [506,441,512,453,499,466],[490,475,485,480,482,480],[478,480,472,472,466,466]], true, '#a65850');
  curve('Smile left', [482,478], [[485,504,457,517,440,497]], false, 'none', 5);
  curve('Smile right', [482,478], [[479,504,508,517,525,497]], false, 'none', 5);
  ellipse('Nose shine', 464, 449, 10, 6, '#f5b9a6');
}
if (stage === 4) {
  curve('Forehead stripe middle', [463,243], [[468,293,475,316,482,318],[493,317,500,285,501,242]], true, stripe);
  curve('Forehead stripe left', [400,250], [[414,279,430,300,441,298],[448,288,441,267,435,244]], true, stripe);
  curve('Forehead stripe right', [528,244], [[521,268,516,288,523,298],[536,300,552,278,565,251]], true, stripe);
  curve('Left cheek stripe', [245,419], [[278,421,298,434,297,441],[286,450,263,447,241,443]], true, stripe);
  curve('Right cheek stripe', [716,419], [[683,421,666,434,667,441],[678,450,700,447,718,443]], true, stripe);
  curve('Left lower stripe', [250,477], [[268,473,281,477,282,484],[278,492,267,497,259,498]], true, stripe);
  curve('Right lower stripe', [712,477], [[694,473,681,477,680,484],[684,492,695,497,703,498]], true, stripe);
  curve('Left whisker upper', [321,487], [[305,480,283,477,268,480]], false, 'none', 4);
  curve('Left whisker lower', [326,504], [[303,504,286,510,277,516]], false, 'none', 4);
  curve('Right whisker upper', [642,487], [[658,480,680,477,695,480]], false, 'none', 4);
  curve('Right whisker lower', [637,504], [[660,504,677,510,686,516]], false, 'none', 4);
}
if (stage === 5) {
  ellipse('Left foot', 291, 742, 151, 80, cream, 7);
  ellipse('Right foot', 519, 742, 151, 80, cream, 7);
  curve('Resting paw', [355,610], [[322,650,325,736,366,746],[407,752,422,675,400,631]], false, orange, 7);
  curve('Waving paw', [601,654], [[680,657,705,579,697,533],[688,493,631,502,629,540],
    [627,568,602,583,581,591]], false, orange, 7);
  ellipse('Paw pad', 649, 557, 32, 40, '#d98780');
  ellipse('Toe bean one', 638, 539, 13, 17, '#d98780');
  ellipse('Toe bean two', 656, 527, 14, 18, '#d98780');
  ellipse('Toe bean three', 675, 536, 13, 17, '#d98780');
  curve('Left toe one', [333,792], [[331,784,333,776,336,774]], false, 'none', 4);
  curve('Left toe two', [357,796], [[355,787,357,779,360,777]], false, 'none', 4);
  curve('Right toe one', [583,796], [[581,787,583,779,586,777]], false, 'none', 4);
  curve('Right toe two', [608,792], [[606,784,608,776,611,774]], false, 'none', 4);
}
if (stage === 6) {
  curve('Bow left', [477,609], [[456,587,420,584,424,612],[421,640,457,637,477,618]], true, '#5d9f90', 4);
  curve('Bow right', [485,609], [[506,587,541,584,538,612],[541,640,505,637,485,618]], true, '#5d9f90', 4);
  ellipse('Bow knot', 469, 603, 26, 24, '#8bc1aa', 4);
  shape('Sparkle right', {type:'polygon',cx:774,cy:354,radius:27,sides:4,starInset:70}, '#e6b861');
  shape('Sparkle left', {type:'polygon',cx:180,cy:294,radius:19,sides:4,starInset:70}, '#e6b861');
  ellipse('Sparkle dot', 751, 312, 12, 12, '#e6b861');
}
if (batch.length) { doc.groupLayers(batch, ['','Backdrop','Silhouette','Face','Markings','Paws','Details'][stage]); }
if (stage === 7) {
  // Fresh detached snapshot: soften the left ear tip and warm the head color.
  var ear = doc.findLayer('Left ear'), path = ear.getShape().path;
  path.subpaths[0].anchors[1].inX += 12;
  path.subpaths[0].anchors[1].inY += 8;
  ear.updateShape({path:path});
  doc.findLayer('Head').updateShape({fill:'#f2af69'});
}
if (stage === 8) {
  if (!patchy.args.output) { throw new Error('Pass output folder'); }
  var stem=patchy.args.output+'/marmalade';
  if (!patchy.io.makeDir(patchy.args.output) || !doc.saveAs(stem+'.svg')) { throw new Error('SVG save failed'); }
  doc.renderPreview(stem+'.png',{maxWidth:960,maxHeight:960});
  if (!doc.saveAs(stem+'.psd')) { throw new Error('PSD save failed'); }
}
patchy.ui.fitOnScreen();
patchy.setResult({documentId:doc.id,stage:stage,path:doc.path,
  next:stage<7?'Inspect get_preview before the next batch':stage===7?'Preview, verify Undo/Redo, then save stage 8':'Saved'});
