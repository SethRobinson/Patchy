<#
Self-authored Photoshop area-text fixtures. COM only; preserves existing documents
and preferences, and closes only the documents created here. Output is a layered
PSD plus an independently recomposed BMP and a native contour readback report.
#>
param([string]$OutputDirectory = '', [string]$CaseFilter = '')
$ErrorActionPreference = 'Stop'
if (-not $OutputDirectory) {
  $OutputDirectory = Join-Path $PSScriptRoot '../../local-test-fixtures/psd/area-text'
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$destination = (Resolve-Path -LiteralPath $OutputDirectory).Path.Replace('\','/').Replace("'", "\'")
$source = @'
(function(out,filter) {
  function sid(s) { return stringIDToTypeID(s); }
  function point(x,y) {
    var p=new ActionDescriptor();
    p.putUnitDouble(sid('horizontal'),sid('pixelsUnit'),x+80);
    p.putUnitDouble(sid('vertical'),sid('pixelsUnit'),y+60);
    return p;
  }
  function contour(vertices) {
    var sub=new ActionDescriptor(), points=new ActionList();
    sub.putBoolean(sid('closedSubpath'),true);
    for(var i=0;i<vertices.length;i++) {
      var v=vertices[i], p=new ActionDescriptor();
      p.putObject(sid('anchor'),sid('paint'),point(v[0],v[1]));
      if(v.length>2) {
        p.putObject(sid('backward'),sid('paint'),point(v[2],v[3]));
        p.putObject(sid('forward'),sid('paint'),point(v[4],v[5]));
        p.putBoolean(sid('smooth'),true);
      }
      points.putObject(sid('pathPoint'),p);
    }
    sub.putList(sid('points'),points);
    return sub;
  }
  function boundary(contours,sameComponent) {
    var result=new ActionDescriptor(), components=new ActionList(), all=new ActionList();
    for(var i=0;i<contours.length;i++) {
      var component=new ActionDescriptor(), subs=new ActionList();
      component.putEnumerated(sid('shapeOperation'),sid('shapeOperation'),sid(contours[i].op||'xor'));
      subs.putObject(sid('subpathsList'),contour(contours[i].p));
      all.putObject(sid('subpathsList'),contour(contours[i].p));
      component.putList(sid('subpathListKey'),subs);
      components.putObject(sid('pathComponent'),component);
    }
    if(sameComponent) {
      var first=components.getObjectValue(0); first.putList(sid('subpathListKey'),all);
      components=new ActionList(); components.putObject(sid('pathComponent'),first);
    }
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
  var rect=[[0,0],[500,0],[500,400],[0,400]];
  var inner=[[160,120],[340,120],[340,280],[160,280]];
  var triangle=[{p:[[250,0],[500,400],[0,400]]}];
  var ellipse=[{p:[[250,0,111.9288,0,388.0712,0],[500,200,500,89.543,500,310.457],
                   [250,400,388.0712,400,111.9288,400],[0,200,0,310.457,0,89.543]]}];
  var cases=[
    ['rectangle',[{p:rect}]],['triangle',triangle],['ellipse',ellipse],
    ['concave',[{p:[[0,0],[500,0],[500,400],[320,400],[320,150],[180,150],[180,400],[0,400]]}]],
    ['hole-components',[{p:rect},{p:inner}]],['hole-subpaths',[{p:rect},{p:inner}],true],
    ['nested',[{p:rect},{p:inner},{p:[[200,160],[300,160],[300,240],[200,240]]}],true],
    ['subtract',[{p:rect,op:'add'},{p:inner,op:'subtract'}]],
    ['overlap',[{p:rect,op:'add'},{p:[[250,0],[600,0],[600,400],[250,400]],op:'intersect'}]],
    ['disconnected',[{p:[[0,0],[180,0],[180,400],[0,400]]},{p:[[320,0],[500,0],[500,400],[320,400]]}]],
    ['self-intersection',[{p:[[0,0],[500,400],[500,0],[0,400]]}]],
    ['reverse-winding',[{p:[[0,400],[500,400],[250,0]]}]],
    ['vertical',triangle,false,24,8,true],['16bit',triangle,false,24,16],['32bit',triangle,false,24,32],
    ['size12',triangle,false,12],['size48',triangle,false,48],['size96',triangle,false,96],
    ['mixed',triangle],['center',triangle],['right',triangle],['justify',triangle],
    ['rotated',triangle],['edited-boundary',triangle],['empty',[{p:[[0,0],[1,0],[1,1],[0,1]]}]]
  ];
  var previous=app.documents.length?app.activeDocument:null;
  var dialogs=app.displayDialogs, units=app.preferences.rulerUnits, report=[];
  try {
    app.displayDialogs=DialogModes.NO;app.preferences.rulerUnits=Units.PIXELS;
    for(var i=0;i<cases.length;i++) {
      if(filter && cases[i][0].indexOf(filter)<0)continue;
      var spec=cases[i], doc=app.documents.add(660,520,72,'Area reference '+spec[0],NewDocumentMode.RGB,DocumentFill.WHITE);
      try {
        if(spec[4]==16)doc.bitsPerChannel=BitsPerChannelType.SIXTEEN;
        if(spec[4]==32)doc.bitsPerChannel=BitsPerChannelType.THIRTYTWO;
        var layer=doc.artLayers.add();layer.kind=LayerKind.TEXT;
        var text=layer.textItem;text.kind=TextType.PARAGRAPHTEXT;text.position=[80,60];
        text.width=500;text.height=400;text.font='ArialMT';text.size=spec[3]||24;
        if(spec[5])text.direction=Direction.VERTICAL;
        var black=new SolidColor();black.rgb.hexValue='000000';text.color=black;
        var story='';for(var j=0;j<8;j++)story+=(j?' ':'')+'One two three four five six seven eight nine ten.';
        text.contents=story;
        if(spec[0]=='center')text.justification=Justification.CENTER;
        if(spec[0]=='right')text.justification=Justification.RIGHT;
        if(spec[0]=='justify')text.justification=Justification.LEFTJUSTIFIED;
        var value=readText(layer), shape=value.getList(sid('textShape')).getObjectValue(0);
        shape.putObject(sid('path'),sid('pathClass'),boundary(spec[1],spec[2]));
        var matrix=new ActionDescriptor();
        matrix.putDouble(sid('xx'),1);matrix.putDouble(sid('xy'),0);matrix.putDouble(sid('yx'),0);matrix.putDouble(sid('yy'),1);
        matrix.putDouble(sid('tx'),-80);matrix.putDouble(sid('ty'),-60);
        shape.putObject(sid('transform'),sid('transform'),matrix);
        var shapes=new ActionList();shapes.putObject(sid('textShape'),shape);value.putList(sid('textShape'),shapes);setText(value);
        if(spec[0]=='mixed') {
          value=readText(layer);
          var style=value.getList(sid('textStyleRange')).getObjectValue(0).getObjectValue(sid('textStyle'));
          var runs=new ActionList(), offsets=[0,12,40,story.length+1], sizes=[24,48,24];
          for(var r=0;r<3;r++) {
            var run=new ActionDescriptor();run.putInteger(sid('from'),offsets[r]);run.putInteger(sid('to'),offsets[r+1]);
            style.putUnitDouble(sid('size'),sid('pointsUnit'),sizes[r]);
            style.putUnitDouble(sid('impliedFontSize'),sid('pointsUnit'),sizes[r]);
            run.putObject(sid('textStyle'),sid('textStyle'),style);runs.putObject(sid('textStyleRange'),run);
          }
          value=new ActionDescriptor();value.putList(sid('textStyleRange'),runs);setText(value);
        }
        if(spec[0]=='rotated')layer.rotate(15,AnchorPosition.MIDDLECENTER);
        if(spec[0]=='edited-boundary') {
          value=readText(layer);shape=value.getList(sid('textShape')).getObjectValue(0);
          shape.putObject(sid('path'),sid('pathClass'),boundary([{p:rect}]));
          shapes=new ActionList();shapes.putObject(sid('textShape'),shape);value.putList(sid('textShape'),shapes);setText(value);
        }
        var actual=readText(layer).getList(sid('textShape')).getObjectValue(0).getObjectValue(sid('path')).getList(sid('pathComponents'));
        report.push(spec[0]+': components='+actual.count+' contours='+actual.getObjectValue(0).getList(sid('subpathListKey')).count+' bounds='+layer.bounds);
        var options=new PhotoshopSaveOptions();options.layers=true;
        doc.saveAs(new File(out+'/'+spec[0]+'.psd'),options,true,Extension.LOWERCASE);
        doc.flatten();
        if(doc.bitsPerChannel!=BitsPerChannelType.EIGHT)doc.bitsPerChannel=BitsPerChannelType.EIGHT;
        doc.saveAs(new File(out+'/'+spec[0]+'.bmp'),new BMPSaveOptions(),true,Extension.LOWERCASE);
      } catch(error) { report.push(spec[0]+': ERROR '+error+' line '+error.line); }
      finally { doc.close(SaveOptions.DONOTSAVECHANGES); }
    }
  } finally {
    app.displayDialogs=dialogs;app.preferences.rulerUnits=units;if(previous)app.activeDocument=previous;
  }
  return report.join('\n');
})('OUTPUT','FILTER');
'@
$app = New-Object -ComObject Photoshop.Application
$app.DoJavaScript($source.Replace('OUTPUT',$destination).Replace('FILTER',$CaseFilter.Replace("'", "\'"))) | Tee-Object -FilePath (Join-Path $OutputDirectory 'report.txt')
