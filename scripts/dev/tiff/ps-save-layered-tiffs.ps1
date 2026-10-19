# Saves PSD files as layered TIFFs from Adobe Photoshop over COM (always authorized; see
# docs/photoshop-com.md), in both TIFF byte orders, for the layered TIFF reader's ground truth
# (docs/tiff.md). Each source <name>.psd becomes, in -OutDir:
#   <name>.ibm.tif     IBM PC byte order (little-endian), layers on, RLE layers, LZW image
#   <name>.mac.tif     Macintosh byte order (big-endian), the same settings
#   <name>.ibm-zip.tif IBM PC, ZIP layer compression and ZIP image compression
#   <name>.flat.tif    IBM PC, layers off (no tag 37724: the composite-only fallback)
# plus <name>.layers.txt, Photoshop's layer inventory (depth, kind, name, visible, bounds) of
# the source, to compare with what Patchy opens.
#
#   & scripts\dev\tiff\ps-save-layered-tiffs.ps1 -Files a.psd, b.psd -OutDir <dir>
#
# Call it from a PowerShell prompt (or `powershell -Command "& ..."`): `powershell -File` cannot
# pass the array parameter. Only documents this script opened are closed, without saving.
param(
  [Parameter(Mandatory = $true)][string[]]$Files,
  [Parameter(Mandatory = $true)][string]$OutDir
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$outJs = ((Resolve-Path $OutDir).Path) -replace '\\', '/'
$app = New-Object -ComObject Photoshop.Application
foreach ($file in $Files) {
  $full = (Resolve-Path $file).Path
  $js = $full -replace '\\', '/'
  $stem = [System.IO.Path]::GetFileNameWithoutExtension($full)
  $jsx = @"
app.displayDialogs = DialogModes.NO;
app.preferences.rulerUnits = Units.PIXELS;
var result = [];
var d = null;
try { d = app.open(new File("$js")); } catch (e) { result.push("OPEN-ERROR " + e); }
if (d !== null) {
  var lines = [];
  function walk(layers, depth) {
    for (var i = 0; i < layers.length; i++) {
      var l = layers[i]; var b = l.bounds;
      var kind = l.typename === "LayerSet" ? "group" : String(l.kind);
      lines.push(depth + "\t" + kind + "\t" + l.name + "\t" + l.visible + "\t" + l.blendMode + "\t" + Math.round(l.opacity) + "\t" + Math.round(b[0].value) + "," + Math.round(b[1].value) + "," + Math.round(b[2].value) + "," + Math.round(b[3].value));
      if (l.typename === "LayerSet") { walk(l.layers, depth + 1); }
    }
  }
  walk(d.layers, 0);
  var inv = new File("$outJs/$stem.layers.txt"); inv.open("w"); inv.write("bits " + d.bitsPerChannel + "\n" + lines.join("\n")); inv.close();
  function save(suffix, order, layers, layerCompression, imageCompression) {
    var o = new TiffSaveOptions();
    o.byteOrder = order;
    o.layers = layers;
    if (layers) { o.layerCompression = layerCompression; }
    o.imageCompression = imageCompression;
    o.embedColorProfile = true;
    o.transparency = true;
    o.alphaChannels = true;
    o.annotations = false;
    o.spotColors = false;
    o.saveImagePyramid = false;
    try { d.saveAs(new File("$outJs/$stem." + suffix + ".tif"), o, true, Extension.LOWERCASE); result.push("SAVED $stem." + suffix + ".tif"); }
    catch (e) { result.push("SAVE-ERROR $stem." + suffix + ".tif " + e); }
  }
  save("ibm", ByteOrder.IBM, true, LayerCompression.RLE, TIFFEncoding.TIFFLZW);
  save("mac", ByteOrder.MACOS, true, LayerCompression.RLE, TIFFEncoding.TIFFLZW);
  save("ibm-zip", ByteOrder.IBM, true, LayerCompression.ZIP, TIFFEncoding.TIFFZIP);
  save("flat", ByteOrder.IBM, false, LayerCompression.RLE, TIFFEncoding.TIFFLZW);
  d.close(SaveOptions.DONOTSAVECHANGES);
}
result.join("\n");
"@
  Write-Output ("== " + $file)
  Write-Output ($app.DoJavaScript($jsx))
}
