<#
Self-authored Photoshop path-text fixtures (docs/path-text.md). COM only; preserves existing
documents and preferences, and closes only the documents created here. Each case writes a
layered PSD, an independently flattened BMP (and a PNG copy of it) and a line in report.txt
with the range and flip Photoshop read back and the layer bounds.

  scripts\dev\capture-path-text.ps1 [-OutputDirectory <dir>] [-CaseFilter <substring>]

The committed fixtures (test-fixtures/psd/photoshop-path-<case>.psd and .png) are the cases
open-curve, wave-range, ellipse-range, center, mixed, baseline-shift, rotated, overflow,
cal-arc-range and cal-right-range of this matrix.

Documents are 660 x 520 px, 72 ppi; the text origin is (80, 60) and contours are given in
text-local coordinates. A vertex is [x, y] (a corner) or [x, y, inX, inY, outX, outY].
#>
param([string]$OutputDirectory = '', [string]$CaseFilter = '')
$ErrorActionPreference = 'Stop'
if (-not $OutputDirectory) {
  $OutputDirectory = Join-Path $PSScriptRoot '../../local-test-fixtures/psd/path-text'
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$destination = (Resolve-Path -LiteralPath $OutputDirectory).Path.Replace('\','/').Replace("'", "\'")
$source = @'
(function(out,filter) {
  function sid(s) { return stringIDToTypeID(s); }
  function point(x,y) {
    var p=new ActionDescriptor();
    p.putUnitDouble(sid('horizontal'),sid('pixelsUnit'),x);
    p.putUnitDouble(sid('vertical'),sid('pixelsUnit'),y);
    return p;
  }
  function contour(vertices,closed) {
    var sub=new ActionDescriptor(), points=new ActionList();
    sub.putBoolean(sid('closedSubpath'),closed);
    for(var i=0;i<vertices.length;i++) {
      var v=vertices[i], p=new ActionDescriptor();
      p.putObject(sid('anchor'),sid('paint'),point(v[0],v[1]));
      if(v.length>2) {
        p.putObject(sid('backward'),sid('paint'),point(v[2],v[3]));
        p.putObject(sid('forward'),sid('paint'),point(v[4],v[5]));
        p.putBoolean(sid('smooth'),false);
      }
      points.putObject(sid('pathPoint'),p);
    }
    sub.putList(sid('points'),points);
    return sub;
  }
  function boundary(vertices,closed) {
    var result=new ActionDescriptor(), components=new ActionList();
    var component=new ActionDescriptor(), subs=new ActionList();
    component.putEnumerated(sid('shapeOperation'),sid('shapeOperation'),sid('xor'));
    subs.putObject(sid('subpathsList'),contour(vertices,closed));
    component.putList(sid('subpathListKey'),subs);
    components.putObject(sid('pathComponent'),component);
    result.putList(sid('pathComponents'),components);
    return result;
  }
  function readText(layer) {
    var r=new ActionReference();r.putIdentifier(sid('layer'),layer.id);
    return executeActionGet(r).getObjectValue(sid('textKey'));
  }
  function setText(value) {
    var r=new ActionReference(), d=new ActionDescriptor();
    r.putEnumerated(sid('textLayer'),sid('ordinal'),sid('targetEnum'));
    d.putReference(sid('null'),r);d.putObject(sid('to'),sid('textLayer'),value);
    executeAction(sid('set'),d,DialogModes.NO);
  }
  // spans: [[from, to, {size, shift}], ...] over the story (the last ends at length + 1).
  function runs(layer,spans) {
    var value=readText(layer);
    var style=value.getList(sid('textStyleRange')).getObjectValue(0).getObjectValue(sid('textStyle'));
    var list=new ActionList();
    for(var r=0;r<spans.length;r++) {
      var run=new ActionDescriptor(), s=spans[r][2]||{};
      run.putInteger(sid('from'),spans[r][0]);run.putInteger(sid('to'),spans[r][1]);
      style.putUnitDouble(sid('size'),sid('pixelsUnit'),s.size||24);
      style.putUnitDouble(sid('impliedFontSize'),sid('pixelsUnit'),s.size||24);
      style.putUnitDouble(sid('baselineShift'),sid('pixelsUnit'),s.shift||0);
      style.putUnitDouble(sid('impliedBaselineShift'),sid('pixelsUnit'),s.shift||0);
      run.putObject(sid('textStyle'),sid('textStyle'),style);list.putObject(sid('textStyleRange'),run);
    }
    value=new ActionDescriptor();value.putList(sid('textStyleRange'),list);setText(value);
  }
  var STORY='One two three four five six seven eight nine ten.', L=STORY.length+1;
  var I20='IIIIIIIIIIIIIIIIIIII', G='IIIII IIIII IIIII IIIII IIIII';
  var arc=[[0,300,0,300,120,0],[500,300,380,0,500,300]];
  var wave=[[0,250,0,250,40,120],[150,150,110,150,260,150],[500,250,400,380,500,250]];
  var poly=[[0,300],[200,100],[500,300]];
  var ellipse=[[250,0,111.9288,0,388.0712,0],[500,200,500,89.543,500,310.457],
               [250,400,388.0712,400,111.9288,400],[0,200,0,310.457,0,89.543]];
  var cases=[
    {name:'open-curve',path:arc,start:0,end:1},
    {name:'polyline',path:poly,start:0,end:2},
    {name:'wave',path:wave,start:0,end:2},
    {name:'wave-range',path:wave,start:0.5,end:1.5},
    {name:'ellipse',path:ellipse,closed:true,start:0,end:4},
    {name:'ellipse-range',path:ellipse,closed:true,start:1,end:3},
    {name:'ellipse-wrap',path:ellipse,closed:true,start:3,end:1},
    {name:'flip',path:arc,start:0,end:1,flip:true},
    {name:'ellipse-flip',path:ellipse,closed:true,start:0,end:4,flip:true},
    {name:'center',path:arc,start:0,end:1,align:'center'},
    {name:'right',path:arc,start:0,end:1,align:'right'},
    {name:'range-center',path:arc,start:0.2,end:0.6,align:'center',story:'One two'},
    {name:'baseline-shift',path:arc,start:0,end:1,edit:function(layer){runs(layer,[[0,4],[4,13,{shift:10}],[13,L]]);}},
    {name:'mixed',path:arc,start:0,end:1,edit:function(layer){runs(layer,[[0,8],[8,18,{size:48}],[18,L]]);}},
    {name:'rotated',path:arc,start:0,end:1,edit:function(layer){layer.rotate(15,AnchorPosition.MIDDLECENTER);}},
    {name:'overflow',path:arc,start:0,end:1,story:STORY+' '+STORY+' '+STORY},
    {name:'vertical',path:arc,start:0,end:1,story:'IIIIIAB',size:48,edit:function(layer){layer.textItem.direction=Direction.VERTICAL;}},
    {name:'reversed-range',path:arc,start:0.9,end:0.3,story:'IIIII',size:48},
    {name:'beyond-range',path:arc,start:-0.5,end:1.5,story:'IIIII',size:48},
    {name:'wrap-word',path:arc,start:0,end:0.5,story:G,size:48},
    {name:'wrap-longword',path:arc,start:0,end:0.2,story:I20,size:48},
    {name:'wrap-center',path:arc,start:0,end:0.5,story:G,size:48,align:'center'},
    {name:'wrap-right',path:arc,start:0,end:0.5,story:G,size:48,align:'right'},
    {name:'two-paragraphs',path:arc,start:0,end:1,story:'IIIII\rIII',size:48},
    {name:'trailing-space-right',path:arc,start:0,end:0.5,story:'IIIII   ',size:48,align:'right'},
    {name:'cal-arc',path:arc,start:0,end:1,story:I20,size:48},
    {name:'cal-arc-range',path:arc,start:0.3,end:0.9,story:I20,size:48},
    {name:'cal-wave-range',path:wave,start:0.5,end:1.5,story:I20,size:48},
    {name:'cal-poly',path:poly,start:0,end:2,story:I20+I20,size:24},
    {name:'cal-ellipse',path:ellipse,closed:true,start:0,end:4,story:I20,size:48},
    {name:'cal-ellipse-wrap',path:ellipse,closed:true,start:3.5,end:1,story:I20,size:48},
    {name:'cal-flip',path:arc,start:0,end:1,flip:true,story:I20,size:48},
    {name:'cal-center',path:arc,start:0,end:1,align:'center',story:I20,size:48},
    {name:'cal-right',path:arc,start:0,end:1,align:'right',story:I20,size:48},
    {name:'cal-right-range',path:arc,start:0.3,end:0.9,align:'right',story:'IIIII',size:48},
    {name:'cal-flip-range',path:arc,start:0.3,end:0.9,flip:true,story:'IIIII',size:48}
  ];
  var previous=app.documents.length?app.activeDocument:null;
  var dialogs=app.displayDialogs, units=app.preferences.rulerUnits, typeUnits=app.preferences.typeUnits, report=[];
  try {
    app.displayDialogs=DialogModes.NO;app.preferences.rulerUnits=Units.PIXELS;app.preferences.typeUnits=TypeUnits.PIXELS;
    for(var i=0;i<cases.length;i++) {
      var spec=cases[i];
      if(filter && spec.name.indexOf(filter)<0)continue;
      var doc=app.documents.add(660,520,72,'Path text reference '+spec.name,NewDocumentMode.RGB,DocumentFill.WHITE);
      try {
        var layer=doc.artLayers.add();layer.kind=LayerKind.TEXT;
        var text=layer.textItem;text.position=[80,60];
        text.font='ArialMT';text.size=spec.size||24;
        var black=new SolidColor();black.rgb.hexValue='000000';text.color=black;
        text.contents=spec.story||STORY;
        if(spec.align=='center')text.justification=Justification.CENTER;
        if(spec.align=='right')text.justification=Justification.RIGHT;
        var value=readText(layer), shape=value.getList(sid('textShape')).getObjectValue(0);
        shape.putEnumerated(sid('char'),sid('char'),sid('onACurve'));
        shape.putObject(sid('path'),sid('pathClass'),boundary(spec.path,!!spec.closed));
        var range=new ActionDescriptor();
        range.putDouble(charIDToTypeID('Strt'),spec.start);range.putDouble(charIDToTypeID('End '),spec.end);
        shape.putObject(sid('tRange'),sid('range'),range);
        if(spec.flip!==undefined)shape.putBoolean(sid('flip'),spec.flip);
        var shapes=new ActionList();shapes.putObject(sid('textShape'),shape);value.putList(sid('textShape'),shapes);
        setText(value);
        if(spec.edit)spec.edit(layer,doc);
        var read=readText(layer).getList(sid('textShape')).getObjectValue(0), tr=read.getObjectValue(sid('tRange'));
        report.push(spec.name+': tRange='+tr.getDouble(charIDToTypeID('Strt'))+','+tr.getDouble(charIDToTypeID('End '))+
                    ' flip='+read.getBoolean(sid('flip'))+' bounds='+layer.bounds);
        var options=new PhotoshopSaveOptions();options.layers=true;
        doc.saveAs(new File(out+'/'+spec.name+'.psd'),options,true,Extension.LOWERCASE);
        doc.flatten();
        doc.saveAs(new File(out+'/'+spec.name+'.bmp'),new BMPSaveOptions(),true,Extension.LOWERCASE);
      } catch(error) { report.push(spec.name+': ERROR '+error+' line '+error.line); }
      finally { doc.close(SaveOptions.DONOTSAVECHANGES); }
      $.sleep(300);
    }
  } finally {
    app.displayDialogs=dialogs;app.preferences.rulerUnits=units;app.preferences.typeUnits=typeUnits;
    if(previous)app.activeDocument=previous;
  }
  return report.join('\n');
})('OUTPUT','FILTER');
'@
$app = New-Object -ComObject Photoshop.Application
$app.DoJavaScript($source.Replace('OUTPUT',$destination).Replace('FILTER',$CaseFilter.Replace("'", "\'"))) |
  Tee-Object -FilePath (Join-Path $OutputDirectory 'report.txt')
Add-Type -AssemblyName System.Drawing
Get-ChildItem -LiteralPath $OutputDirectory -Filter *.bmp | ForEach-Object {
  $png = [IO.Path]::ChangeExtension($_.FullName, '.png')
  $bitmap = [System.Drawing.Image]::FromFile($_.FullName)
  try { $bitmap.Save($png, [System.Drawing.Imaging.ImageFormat]::Png) } finally { $bitmap.Dispose() }
}
