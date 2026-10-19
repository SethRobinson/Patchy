# Opens PSD/PSB files in Adobe Photoshop over COM, records every Photoshop dialog that comes
# up while each one opens, and saves Photoshop's own flatten of each next to -OutDir as
# <name>.ps-flat.psd, so its merged image can be compared with the merged image Patchy wrote
# (scripts\dev\psd-merged-diff.py). Used for the CMYK Proof PSD acceptance (docs/cmyk.md).
# Driving Photoshop over COM is always authorized; clicking a dialog is desktop UI automation
# and needs Seth's explicit permission, so a file that raises one parks the run until the
# dialog is dismissed by hand (the watcher has already logged its text).
#
#   & scripts\dev\photoshop-flatten-check.ps1 -Files a.psd, b.psb -OutDir <dir>
#
# Output: one line per file, "CLEAN", "DIALOG" (with the dialog text) or "ERROR", followed by
# the mode, depth and top-level layer count Photoshop reports.
param(
  [Parameter(Mandatory = $true)][string[]]$Files,
  [Parameter(Mandatory = $true)][string]$OutDir
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $OutDir | Out-Null
$log = [System.IO.Path]::GetTempFileName()

$watcher = Start-Job -ArgumentList $log -ScriptBlock {
  param($LogPath)
  Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public class PsFlattenWatch {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  public static IntPtr Dialog(uint pid) { IntPtr found = IntPtr.Zero; EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); if (p == pid && IsWindowVisible(h)) { var c = new StringBuilder(256); GetClassName(h, c, 256); if (c.ToString() == "PSDialogBox") { found = h; } } return true; }, IntPtr.Zero); return found; }
  public static string DialogText(IntPtr dlg) { var sb = new StringBuilder(); var t0 = new StringBuilder(1024); GetWindowText(dlg, t0, 1024); sb.Append(t0.ToString()); sb.Append(" | "); EnumChildWindows(dlg, (h, l) => { var t = new StringBuilder(1024); GetWindowText(h, t, 1024); if (t.Length > 0) { sb.Append(t.ToString()); sb.Append(" | "); } return true; }, IntPtr.Zero); return sb.ToString(); }
}
"@
  while ($true) {
    $lines = Get-Content $LogPath -ErrorAction SilentlyContinue
    if ($lines -and $lines[-1] -eq 'DONE') { break }
    $p = Get-Process Photoshop -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($p) {
      $dlg = [PsFlattenWatch]::Dialog([uint32]$p.Id)
      if ($dlg -ne [IntPtr]::Zero) {
        $current = ($lines | Where-Object { $_ -like 'OPENING *' } | Select-Object -Last 1)
        $entry = 'DIALOG ' + $current + ' :: ' + ([PsFlattenWatch]::DialogText($dlg) -replace "[\r\n]+", ' ')
        if (-not ($lines -contains $entry)) { Add-Content -Path $LogPath -Value $entry }
      }
    }
    Start-Sleep -Milliseconds 200
  }
}

$app = New-Object -ComObject Photoshop.Application
$outJs = (Resolve-Path $OutDir).Path -replace '\\', '/'
$logJs = $log -replace '\\', '/'
foreach ($file in $Files) {
  $full = (Resolve-Path $file).Path
  $js = $full -replace '\\', '/'
  $base = [System.IO.Path]::GetFileNameWithoutExtension($full) + [System.IO.Path]::GetExtension($full).Replace('.', '-')
  $jsx = @"
app.displayDialogs = DialogModes.ERROR;
app.preferences.rulerUnits = Units.PIXELS;
var log = new File("$logJs");
function note(line) { log.open("a"); log.write(line + "\n"); log.close(); }
note("OPENING $js");
var d = null;
try { d = app.open(new File("$js")); } catch (e) { note("ERROR $js " + e); }
if (d !== null) {
  note("OPENED $js mode=" + String(d.mode) + " bits=" + String(d.bitsPerChannel) + " layers=" + d.layers.length);
  try {
    d.flatten();
    var options = new PhotoshopSaveOptions();
    options.layers = false;
    d.saveAs(new File("$outJs/$base.ps-flat.psd"), options, true);
    note("FLATTENED $js");
  } catch (e) { note("ERROR $js flatten " + e); }
  d.close(SaveOptions.DONOTSAVECHANGES);
}
"@
  $app.DoJavaScript($jsx) | Out-Null
}
Add-Content -Path $log -Value 'DONE'
Wait-Job $watcher -Timeout 10 | Out-Null
Remove-Job $watcher -Force -ErrorAction SilentlyContinue

$lines = Get-Content $log
foreach ($file in $Files) {
  $js = ((Resolve-Path $file).Path) -replace '\\', '/'
  $opened = $lines | Where-Object { $_ -like ('OPENED ' + $js + ' *') } | Select-Object -First 1
  $dialogs = $lines | Where-Object { $_ -like ('DIALOG OPENING ' + $js + ' ::*') }
  $errors = $lines | Where-Object { $_ -like ('ERROR ' + $js + '*') }
  $info = if ($opened) { $opened.Substring(('OPENED ' + $js).Length) } else { '' }
  if ($dialogs) { foreach ($d in $dialogs) { Write-Output ('DIALOG ' + $file + $info + ' ' + $d.Substring($d.IndexOf('::'))) } }
  elseif ($errors) { foreach ($e in $errors) { Write-Output ('ERROR ' + $file + ' ' + $e.Substring(('ERROR ' + $js).Length)) } }
  elseif ($opened) { Write-Output ('CLEAN ' + $file + $info) }
  else { Write-Output ('UNKNOWN ' + $file) }
}
Remove-Item $log -ErrorAction SilentlyContinue
