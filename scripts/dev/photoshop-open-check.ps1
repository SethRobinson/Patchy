# Opens PSD files in Adobe Photoshop over COM and reports whether each one raised the
# "This document contains unknown data which will be discarded to keep layers editable"
# prompt, the sign of a layer block Photoshop cannot read (docs/ps-compat.md). Driving
# Photoshop over COM is always authorized; clicking the prompt is desktop UI automation and
# needs Seth's explicit permission for the session, so -DismissUnknownData is off by default
# and the run parks on the dialog until it is dismissed by hand.
#
#   & scripts\dev\photoshop-open-check.ps1 -Files a.psd, b.psd [-DismissUnknownData] [-InventoryDir <dir>]
#
# Call it from a PowerShell prompt (or `powershell -Command "& ..."`): `powershell -File` cannot
# pass the array parameter.
#
# Output: one line per file, "CLEAN <file>" or "UNKNOWN-DATA <file>", plus "ERROR" when the
# open failed. With -InventoryDir every opened document's layers (depth, kind, name, visible,
# bounds) are written to <dir>\<file>.layers.txt, which shows what Photoshop kept after
# Keep Layers (a discarded fill layer comes back as an empty NORMAL layer).
param(
  [Parameter(Mandatory = $true)][string[]]$Files,
  [switch]$DismissUnknownData,
  [string]$InventoryDir = ''
)
$ErrorActionPreference = 'Stop'

Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public class PsOpenCheck {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
  public static IntPtr Dialog(uint pid) { IntPtr found = IntPtr.Zero; EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); if (p == pid && IsWindowVisible(h)) { var c = new StringBuilder(256); GetClassName(h, c, 256); if (c.ToString() == "PSDialogBox") { found = h; } } return true; }, IntPtr.Zero); return found; }
  public static string DialogText(IntPtr dlg) { var sb = new StringBuilder(); EnumChildWindows(dlg, (h, l) => { var t = new StringBuilder(1024); GetWindowText(h, t, 1024); if (t.Length > 0) { sb.Append(t.ToString()); sb.Append(" | "); } return true; }, IntPtr.Zero); return sb.ToString(); }
  public static bool ClickButton(IntPtr dlg, string caption) { IntPtr btn = IntPtr.Zero; EnumChildWindows(dlg, (h, l) => { var t = new StringBuilder(256); GetWindowText(h, t, 256); var c = new StringBuilder(256); GetClassName(h, c, 256); if (c.ToString() == "Button" && t.ToString() == caption) { btn = h; } return true; }, IntPtr.Zero); if (btn == IntPtr.Zero) return false; SendMessage(btn, 0x00F5, IntPtr.Zero, IntPtr.Zero); return true; }
}
"@

$log = [System.IO.Path]::GetTempFileName()
$inventoryJs = if ($InventoryDir -ne '') { ($InventoryDir -replace '\\', '/') } else { '' }

# The watcher polls Photoshop's top-level windows for the prompt while the COM open blocks.
$watcher = Start-Job -ArgumentList $log, [bool]$DismissUnknownData -ScriptBlock {
  param($LogPath, $Dismiss)
  Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public class PsWatchJob {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
  public static IntPtr Dialog(uint pid) { IntPtr found = IntPtr.Zero; EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); if (p == pid && IsWindowVisible(h)) { var c = new StringBuilder(256); GetClassName(h, c, 256); if (c.ToString() == "PSDialogBox") { found = h; } } return true; }, IntPtr.Zero); return found; }
  public static string DialogText(IntPtr dlg) { var sb = new StringBuilder(); EnumChildWindows(dlg, (h, l) => { var t = new StringBuilder(1024); GetWindowText(h, t, 1024); if (t.Length > 0) { sb.Append(t.ToString()); sb.Append(" | "); } return true; }, IntPtr.Zero); return sb.ToString(); }
  public static bool ClickButton(IntPtr dlg, string caption) { IntPtr btn = IntPtr.Zero; EnumChildWindows(dlg, (h, l) => { var t = new StringBuilder(256); GetWindowText(h, t, 256); var c = new StringBuilder(256); GetClassName(h, c, 256); if (c.ToString() == "Button" && t.ToString() == caption) { btn = h; } return true; }, IntPtr.Zero); if (btn == IntPtr.Zero) return false; SendMessage(btn, 0x00F5, IntPtr.Zero, IntPtr.Zero); return true; }
}
"@
  while ($true) {
    $lines = Get-Content $LogPath -ErrorAction SilentlyContinue
    if ($lines -and $lines[-1] -eq 'DONE') { break }
    $p = Get-Process Photoshop -ErrorAction SilentlyContinue
    if ($p) {
      $dlg = [PsWatchJob]::Dialog([uint32]$p.Id)
      if ($dlg -ne [IntPtr]::Zero -and ([PsWatchJob]::DialogText($dlg) -like '*unknown data*')) {
        $current = ($lines | Where-Object { $_ -like 'OPENING *' } | Select-Object -Last 1)
        if (-not ($lines -contains ('UNKNOWN-DATA ' + $current))) {
          Add-Content -Path $LogPath -Value ('UNKNOWN-DATA ' + $current)
        }
        if ($Dismiss) { [PsWatchJob]::ClickButton($dlg, 'Keep Layers') | Out-Null; Start-Sleep -Milliseconds 800 }
      }
    }
    Start-Sleep -Milliseconds 200
  }
}

$app = New-Object -ComObject Photoshop.Application
foreach ($file in $Files) {
  $full = (Resolve-Path $file).Path
  $js = $full -replace '\\', '/'
  $logJs = $log -replace '\\', '/'
  $inventory = if ($inventoryJs -ne '') { $inventoryJs + '/' + [System.IO.Path]::GetFileName($full) + '.layers.txt' } else { '' }
  $jsx = @"
app.displayDialogs = DialogModes.ERROR;
app.preferences.rulerUnits = Units.PIXELS;
var log = new File("$logJs");
function note(line) { log.open("a"); log.write(line + "\n"); log.close(); }
note("OPENING $js");
var d = null;
try { d = app.open(new File("$js")); note("OPENED $js"); } catch (e) { note("ERROR $js " + e); }
if (d !== null) {
  if ("$inventory" !== "") {
    var lines = [];
    function walk(layers, depth) {
      for (var i = 0; i < layers.length; i++) {
        var l = layers[i]; var b = l.bounds;
        var kind = l.typename === "LayerSet" ? "group" : String(l.kind);
        lines.push(depth + "\t" + kind + "\t" + l.name + "\t" + l.visible + "\t" + Math.round(b[0].value) + "," + Math.round(b[1].value) + "," + Math.round(b[2].value) + "," + Math.round(b[3].value));
        if (l.typename === "LayerSet") { walk(l.layers, depth + 1); }
      }
    }
    walk(d.layers, 0);
    var out = new File("$inventory"); out.open("w"); out.write(lines.join("\n")); out.close();
  }
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
  if ($lines -contains ('UNKNOWN-DATA OPENING ' + $js)) { Write-Output ('UNKNOWN-DATA ' + $file) }
  elseif ($lines | Where-Object { $_ -like ('ERROR ' + $js + '*') }) { Write-Output ('ERROR ' + $file) }
  elseif ($lines -contains ('OPENED ' + $js)) { Write-Output ('CLEAN ' + $file) }
  else { Write-Output ('UNKNOWN ' + $file) }
}
Remove-Item $log -ErrorAction SilentlyContinue
