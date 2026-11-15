<#
Photoshop probe for paragraph settings on text along a path (docs/path-text.md, "The line is
justified and indented like a one-line box"). COM only; preserves existing documents and
preferences, and closes only the documents created or opened here, without saving.

  scripts\dev\probe-path-text-paragraph.ps1 [-OutputDirectory <dir>] [-CaseFilter <substring>]
                                            [-RelayFile <psd>]

Each case is a 660 x 320 px document at 72 ppi with black Arial 48 px text on a straight path
from (80, 160) to (580, 160) ("point" cases: point text at (80, 60)), with one paragraph style
set through the layer's paragraphStyle property. report.txt gets the layer bounds and the
paragraph style Photoshop reads back; each case also saves a layered PSD and a flattened PNG.

-RelayFile opens a copy of a file (make the copy first), reports each type layer as opened and
again after a re-layout that leaves the paragraph style alone (tracking 20, then 0), and saves a
flatten of each state.
#>
param([string]$OutputDirectory = '', [string]$CaseFilter = '', [string]$RelayFile = '')
$ErrorActionPreference = 'Stop'
if (-not $OutputDirectory) {
  $OutputDirectory = Join-Path $PSScriptRoot '../../local-test-fixtures/psd/path-text-paragraph'
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
function JsPath([string]$p) { return $p.Replace('\','/').Replace("'", "\'") }
$destination = JsPath (Resolve-Path -LiteralPath $OutputDirectory).Path
$relay = if ($RelayFile) { JsPath (Resolve-Path -LiteralPath $RelayFile).Path } else { '' }
$source = @'
(function(out,filter,relayFile) {
  function sid(s) { return stringIDToTypeID(s); }
  function point(x,y) {
    var p=new ActionDescriptor();
    p.putUnitDouble(sid('horizontal'),sid('pixelsUnit'),x);
    p.putUnitDouble(sid('vertical'),sid('pixelsUnit'),y);
    return p;
  }
  function boundary(vertices) {
    var sub=new ActionDescriptor(), points=new ActionList();
    sub.putBoolean(sid('closedSubpath'),false);
    for(var i=0;i<vertices.length;i++) {
      var p=new ActionDescriptor();
      p.putObject(sid('anchor'),sid('paint'),point(vertices[i][0],vertices[i][1]));
      points.putObject(sid('pathPoint'),p);
    }
    sub.putList(sid('points'),points);
    var result=new ActionDescriptor(), components=new ActionList();
    var component=new ActionDescriptor(), subs=new ActionList();
    component.putEnumerated(sid('shapeOperation'),sid('shapeOperation'),sid('xor'));
    subs.putObject(sid('subpathsList'),sub);
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
  // The paragraphStyle property of the target text layer: only the keys given change. (Writing
  // the layer's whole textKey back is not a clean way to do this: it also rewrites the composer
  // and the direction a paragraph does not state.)
  function setParagraph(opts) {
    var ref=new ActionReference();
    ref.putProperty(sid('property'),sid('paragraphStyle'));
    ref.putEnumerated(sid('textLayer'),sid('ordinal'),sid('targetEnum'));
    var desc=new ActionDescriptor();desc.putReference(sid('null'),ref);
    var ps=new ActionDescriptor();
    if(opts.align)ps.putEnumerated(sid('align'),sid('alignmentType'),sid(opts.align));
    if(opts.dir)ps.putEnumerated(sid('directionType'),sid('directionType'),sid(opts.dir));
    if(opts.first!==undefined)ps.putUnitDouble(sid('firstLineIndent'),sid('pixelsUnit'),opts.first);
    if(opts.start!==undefined)ps.putUnitDouble(sid('startIndent'),sid('pixelsUnit'),opts.start);
    if(opts.end!==undefined)ps.putUnitDouble(sid('endIndent'),sid('pixelsUnit'),opts.end);
    desc.putObject(sid('to'),sid('paragraphStyle'),ps);
    executeAction(sid('set'),desc,DialogModes.NO);
  }
  function relayTracking(amount) {
    var ref=new ActionReference();
    ref.putProperty(sid('property'),sid('textStyle'));
    ref.putEnumerated(sid('textLayer'),sid('ordinal'),sid('targetEnum'));
    var desc=new ActionDescriptor();desc.putReference(sid('null'),ref);
    var ts=new ActionDescriptor();ts.putInteger(sid('tracking'),amount);
    desc.putObject(sid('to'),sid('textStyle'),ts);
    executeAction(sid('set'),desc,DialogModes.NO);
  }
  function num(style,key) {
    try { return style.hasKey(sid(key)) ? style.getUnitDoubleValue(sid(key)) : 'none'; } catch(e) { return 'err'; }
  }
  function paraInfo(layer) {
    var value=readText(layer), list=value.getList(sid('paragraphStyleRange')), parts=[];
    for(var i=0;i<list.count;i++) {
      var style=list.getObjectValue(i).getObjectValue(sid('paragraphStyle'));
      var align=style.hasKey(sid('align'))?typeIDToStringID(style.getEnumerationValue(sid('align'))):'none';
      var dir=style.hasKey(sid('directionType'))?typeIDToStringID(style.getEnumerationValue(sid('directionType'))):'none';
      parts.push('align='+align+' dir='+dir+' first='+num(style,'firstLineIndent')+' start='+num(style,'startIndent')+
                 ' end='+num(style,'endIndent'));
    }
    var shape=value.getList(sid('textShape')).getObjectValue(0);
    return 'char='+typeIDToStringID(shape.getEnumerationValue(sid('char')))+' '+parts.join(' | ');
  }
  function flatBmp(doc,file) {
    var copy=doc.duplicate();
    try { copy.flatten(); copy.saveAs(new File(file),new BMPSaveOptions(),true,Extension.LOWERCASE); }
    finally { copy.close(SaveOptions.DONOTSAVECHANGES); }
    app.activeDocument=doc;
  }
  var report=[], log=new File(out+'/report.txt');
  log.open('w');log.close();
  function say(line) { report.push(line); log.open('a'); log.writeln(line); log.close(); }
  function openNames() { var n=[];for(var b=0;b<app.documents.length;b++)n.push(app.documents[b].name);return n.join(', '); }
  say('open documents before: '+openNames());
  var previous=app.documents.length?app.activeDocument:null;
  var dialogs=app.displayDialogs, units=app.preferences.rulerUnits, typeUnits=app.preferences.typeUnits;
  var line=[[0,100],[500,100]];
  var FITS='IIIII IIIII', OVER='IIIII IIIII IIIII IIIII IIIII IIIII IIIII IIIII IIIII IIIII';
  var HEBREW='שלום עולם';
  var cases=[
    {name:'left'},
    {name:'center',p:{align:'center'}},
    {name:'right',p:{align:'right'}},
    {name:'justify-left',p:{align:'justifyLeft'}},
    {name:'justify-center',p:{align:'justifyCenter'}},
    {name:'justify-right',p:{align:'justifyRight'}},
    {name:'justify-all',p:{align:'justifyAll'}},
    {name:'over-left',story:OVER},
    {name:'over-right',story:OVER,p:{align:'right'}},
    {name:'over-justify-left',story:OVER,p:{align:'justifyLeft'}},
    {name:'over-justify-all',story:OVER,p:{align:'justifyAll'}},
    {name:'left-first20',p:{first:20}},
    {name:'left-first-minus20',p:{first:-20}},
    {name:'left-start30',p:{start:30}},
    {name:'left-end40',p:{end:40}},
    {name:'left-start30-first20',p:{start:30,first:20}},
    {name:'right-end40',p:{align:'right',end:40}},
    {name:'right-start30',p:{align:'right',start:30}},
    {name:'right-first20',p:{align:'right',first:20}},
    {name:'center-start30',p:{align:'center',start:30}},
    {name:'center-first20',p:{align:'center',first:20}},
    {name:'center-end40',p:{align:'center',end:40}},
    {name:'justify-left-first20-end40',p:{align:'justifyLeft',first:20,end:40}},
    {name:'justify-all-first20-end40',p:{align:'justifyAll',first:20,end:40}},
    {name:'over-left-end40',story:OVER,p:{end:40}},
    {name:'over-left-first20',story:OVER,p:{first:20}},
    {name:'over-left-start30',story:OVER,p:{start:30}},
    {name:'rtl-left',story:HEBREW,p:{dir:'dirRightToLeft'}},
    {name:'rtl-right',story:HEBREW,p:{dir:'dirRightToLeft',align:'right'}},
    {name:'rtl-justify-left',story:HEBREW,p:{dir:'dirRightToLeft',align:'justifyLeft'}},
    {name:'rtl-left-first20',story:HEBREW,p:{dir:'dirRightToLeft',first:20}},
    {name:'point-left',point:true},
    {name:'point-first20',point:true,p:{first:20}},
    {name:'point-start30',point:true,p:{start:30}},
    {name:'point-justify-right',point:true,p:{align:'justifyRight'}},
    {name:'point-justify-all',point:true,p:{align:'justifyAll'}}
  ];
  try {
    app.displayDialogs=DialogModes.NO;app.preferences.rulerUnits=Units.PIXELS;app.preferences.typeUnits=TypeUnits.PIXELS;
    for(var i=0;i<cases.length;i++) {
      var spec=cases[i];
      if(filter && spec.name.indexOf(filter)<0)continue;
      var doc=app.documents.add(660,320,72,'Path paragraph probe '+spec.name,NewDocumentMode.RGB,DocumentFill.WHITE);
      try {
        var layer=doc.artLayers.add();layer.kind=LayerKind.TEXT;
        var text=layer.textItem;text.position=[80,60];
        text.font='ArialMT';text.size=48;
        var black=new SolidColor();black.rgb.hexValue='000000';text.color=black;
        text.contents=spec.story||FITS;
        if(!spec.point) {
          var value=readText(layer), shape=value.getList(sid('textShape')).getObjectValue(0);
          shape.putEnumerated(sid('char'),sid('char'),sid('onACurve'));
          shape.putObject(sid('path'),sid('pathClass'),boundary(line));
          var range=new ActionDescriptor();
          range.putDouble(charIDToTypeID('Strt'),0);range.putDouble(charIDToTypeID('End '),1);
          shape.putObject(sid('tRange'),sid('range'),range);
          var shapes=new ActionList();shapes.putObject(sid('textShape'),shape);value.putList(sid('textShape'),shapes);
          setText(value);
        }
        doc.activeLayer=layer;
        if(spec.p)setParagraph(spec.p);
        say(spec.name+': bounds='+layer.bounds+' '+paraInfo(layer));
        var options=new PhotoshopSaveOptions();options.layers=true;
        doc.saveAs(new File(out+'/'+spec.name+'.psd'),options,true,Extension.LOWERCASE);
        doc.flatten();
        doc.saveAs(new File(out+'/'+spec.name+'.bmp'),new BMPSaveOptions(),true,Extension.LOWERCASE);
      } catch(error) { say(spec.name+': ERROR '+error+' line '+error.line); }
      finally { doc.close(SaveOptions.DONOTSAVECHANGES); }
      $.sleep(400);
    }
    if(relayFile) {
      var pdoc=app.open(new File(relayFile));
      try {
        say('--- '+pdoc.name+' '+pdoc.width+' x '+pdoc.height+' at '+pdoc.resolution+' ppi');
        flatBmp(pdoc,out+'/relay-0-as-opened.bmp');
        for(var l=0;l<pdoc.artLayers.length;l++) {
          var tl=pdoc.artLayers[l];
          if(tl.kind!=LayerKind.TEXT)continue;
          pdoc.activeLayer=tl;
          say('as opened "'+tl.name+'": bounds='+tl.bounds+' '+paraInfo(tl));
          try { relayTracking(20);relayTracking(0);say('re-laid "'+tl.name+'": bounds='+tl.bounds+' '+paraInfo(tl)); }
          catch(e1) { say('re-lay "'+tl.name+'" ERROR '+e1); }
        }
        flatBmp(pdoc,out+'/relay-1-relaid.bmp');
      } catch(error) { say('relay file: ERROR '+error+' line '+error.line); }
      finally { pdoc.close(SaveOptions.DONOTSAVECHANGES); }
    }
  } finally {
    app.displayDialogs=dialogs;app.preferences.rulerUnits=units;app.preferences.typeUnits=typeUnits;
    if(previous)app.activeDocument=previous;
  }
  say('open documents after: '+openNames());
  return report.join('\n');
})('OUTPUT','FILTER','RELAY');
'@
$app = New-Object -ComObject Photoshop.Application
$app.DoJavaScript($source.Replace('OUTPUT',$destination).Replace('FILTER',$CaseFilter.Replace("'", "\'")).Replace('RELAY',$relay))
Add-Type -AssemblyName System.Drawing
Get-ChildItem -LiteralPath $OutputDirectory -Filter *.bmp | ForEach-Object {
  $png = [IO.Path]::ChangeExtension($_.FullName, '.png')
  $bitmap = [System.Drawing.Image]::FromFile($_.FullName)
  try { $bitmap.Save($png, [System.Drawing.Imaging.ImageFormat]::Png) } finally { $bitmap.Dispose() }
}
