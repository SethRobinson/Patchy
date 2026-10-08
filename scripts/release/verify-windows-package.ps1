# Verifies the BUILT Windows packages without installing anything: unpacks the installer
# and the portable zip, runs the installer wizard in its self-closing smoke mode, checks
# that the unpacked tree is self-contained, and runs packaging\package-selftest.js on the
# unpacked patchy.exe. build-release.bat runs it after signing; it also runs standalone,
# for example against files downloaded from a published release:
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\release\verify-windows-package.ps1
#
# Windows PowerShell 5.1 compatible (the installer's own host). Needs an interactive
# desktop session for the wizard step and dumpbin for the DLL step (found on PATH, or
# through scripts\vs-env.bat). See docs/release-process.md, "Release safety checks".
[CmdletBinding()]
param(
    [string]$Installer,
    [string]$Zip,
    # Expected version; defaults to the installer payload's PatchyVersion.txt.
    [string]$Version,
    # Scratch folder, cleared up front. Only a folder this script created is ever cleared.
    [string]$WorkDir
)

$ErrorActionPreference = "Stop"
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
if (-not $Installer) { $Installer = Join-Path $repo "build\package\PatchyWindowsInstaller.exe" }
if (-not $Zip) { $Zip = Join-Path $repo "build\package\PatchyWindowsNoInstaller.zip" }
if (-not $WorkDir) { $WorkDir = Join-Path $repo "build\package\verify" }
$Installer = [IO.Path]::GetFullPath($Installer)
$Zip = [IO.Path]::GetFullPath($Zip)
$WorkDir = [IO.Path]::GetFullPath($WorkDir)
$marker = Join-Path $WorkDir ".patchy-package-verify"

$script:failures = @()
function Write-Step {
    param([bool]$Ok, [string]$Name, [string]$Detail = "")
    $line = $(if ($Ok) { "PASS" } else { "FAIL" }) + "  " + $Name
    if ($Detail) { $line += ": " + $Detail }
    Write-Host $line
    if (-not $Ok) { $script:failures += $Name }
}

function Get-Sha256 {
    param([string]$Path)
    $stream = [IO.File]::OpenRead($Path)
    try {
        $sha = [Security.Cryptography.SHA256]::Create()
        return -join ($sha.ComputeHash($stream) | ForEach-Object { $_.ToString("x2") })
    } finally {
        $stream.Dispose()
    }
}

# Start-Process -PassThru loses the exit code under Windows PowerShell 5.1 when output is
# redirected, so processes are run through System.Diagnostics.Process with a deadline.
function Invoke-Bounded {
    param([string]$FilePath, [string]$Arguments, [int]$TimeoutSeconds, [string]$WorkingDirectory = $WorkDir)
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $FilePath
    $info.Arguments = $Arguments
    $info.WorkingDirectory = $WorkingDirectory
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.CreateNoWindow = $true
    $process = [System.Diagnostics.Process]::Start($info)
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        try { $process.Kill() } catch { }
        return [pscustomobject]@{ ExitCode = $null; TimedOut = $true; Output = "" }
    }
    $process.WaitForExit()
    return [pscustomobject]@{
        ExitCode = $process.ExitCode
        TimedOut = $false
        Output = ($stdout.Result + $stderr.Result).Trim()
    }
}

function Quote { param([string]$Value) return '"' + $Value + '"' }

foreach ($file in @($Installer, $Zip)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        Write-Host "FAIL  package file not found: $file"
        exit 1
    }
}

if (Test-Path -LiteralPath $WorkDir) {
    $existing = @(Get-ChildItem -LiteralPath $WorkDir -Force)
    if ($existing.Count -gt 0 -and -not (Test-Path -LiteralPath $marker -PathType Leaf)) {
        Write-Host "FAIL  refusing to clear a folder this script did not create: $WorkDir"
        exit 1
    }
    Remove-Item -LiteralPath $WorkDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null
Set-Content -LiteralPath $marker -Value "Scratch folder of verify-windows-package.ps1; safe to delete." -Encoding ASCII

$payloadDir = Join-Path $WorkDir "payload"
$appParent = Join-Path $WorkDir "app"
$appDir = Join-Path $appParent "Patchy"
$selfTestDir = Join-Path $WorkDir "selftest"
$system32 = [Environment]::GetFolderPath([Environment+SpecialFolder]::System)
$powershellExe = Join-Path $system32 "WindowsPowerShell\v1.0\powershell.exe"
New-Item -ItemType Directory -Force -Path $payloadDir, $appParent, $selfTestDir | Out-Null

Write-Host "Verifying $Installer"
Write-Host "      and $Zip"

# 1. Unpack the installer with IExpress's extract-only switches. Nothing is launched.
$extract = Invoke-Bounded -FilePath $Installer -Arguments ("/Q /C /T:" + (Quote $payloadDir)) -TimeoutSeconds 180
$payloadFiles = @("InstallPatchy.exe", "InstallPatchy.ps1", "PatchyWindowsNoInstaller.zip", "Patchy.ico", "PatchyVersion.txt", "UninstallPatchy.exe")
$missingPayload = @($payloadFiles | Where-Object { -not (Test-Path -LiteralPath (Join-Path $payloadDir $_) -PathType Leaf) })
$payloadOk = (-not $extract.TimedOut) -and ($extract.ExitCode -eq 0) -and ($missingPayload.Count -eq 0)
Write-Step $payloadOk "installer payload unpacks" $(if ($extract.TimedOut) { "timed out" } elseif ($missingPayload.Count) { "missing " + ($missingPayload -join ", ") } elseif ($extract.ExitCode -ne 0) { "exit code " + $extract.ExitCode } else { "" })
if (-not $payloadOk) { exit 1 }

# 2. The installer carries exactly the portable zip, and the expected version.
$payloadZip = Join-Path $payloadDir "PatchyWindowsNoInstaller.zip"
$sameZip = (Get-Sha256 $payloadZip) -eq (Get-Sha256 $Zip)
Write-Step $sameZip "installer payload matches the portable zip"
$payloadVersion = (Get-Content -LiteralPath (Join-Path $payloadDir "PatchyVersion.txt") -Raw).Trim()
if (-not $Version) { $Version = $payloadVersion }
Write-Step ($payloadVersion -eq $Version) "installer version is $Version" $(if ($payloadVersion -ne $Version) { "payload says " + $payloadVersion } else { "" })

# 3. The wizard builds and shows: the unpacked script with the launcher's own arguments
#    (packaging\windows\InstallPatchyLauncher.cs) plus -SmokeTest, which never installs.
$wizardArgs = "-NoLogo -NoProfile -ExecutionPolicy Bypass -STA -File " + (Quote (Join-Path $payloadDir "InstallPatchy.ps1")) +
    " -PayloadZip " + (Quote $payloadZip) + " -Version " + (Quote $payloadVersion) + " -SmokeTest"
$wizard = Invoke-Bounded -FilePath $powershellExe -Arguments $wizardArgs -TimeoutSeconds 60 -WorkingDirectory $payloadDir
$wizardOk = (-not $wizard.TimedOut) -and ($wizard.ExitCode -eq 0) -and ($wizard.Output -match "smoke test passed")
Write-Step $wizardOk "installer wizard smoke test" $(if ($wizard.TimedOut) { "timed out (a dialog is probably waiting)" } elseif (-not $wizardOk) { "exit code " + $wizard.ExitCode + "; " + $wizard.Output } else { "" })

# 3b. The "Open with" registration, written under a scratch key so the real
#     HKCU:\Software\Classes tree is never touched, then removed again.
$openWithRoot = "HKCU:\Software\PatchyVerify-" + [guid]::NewGuid().ToString("N")
try {
    $openWithArgs = "-NoLogo -NoProfile -ExecutionPolicy Bypass -File " + (Quote (Join-Path $payloadDir "InstallPatchy.ps1")) +
        " -PayloadZip " + (Quote $payloadZip) + " -OpenWithCheckRoot " + (Quote $openWithRoot)
    $openWith = Invoke-Bounded -FilePath $powershellExe -Arguments $openWithArgs -TimeoutSeconds 60 -WorkingDirectory $payloadDir
    $openWithProblem = ""
    if ($openWith.TimedOut) {
        $openWithProblem = "timed out"
    } elseif ($openWith.ExitCode -ne 0) {
        $openWithProblem = "exit code " + $openWith.ExitCode + "; " + $openWith.Output
    } else {
        $applicationKey = Join-Path $openWithRoot "Applications\patchy.exe"
        $friendlyName = (Get-ItemProperty -Path $applicationKey).FriendlyAppName
        $command = (Get-ItemProperty -Path (Join-Path $applicationKey "shell\open\command")).'(default)'
        $types = @((Get-Item -Path (Join-Path $applicationKey "SupportedTypes")).GetValueNames())
        if ($friendlyName -ne "Patchy Image Editor") {
            $openWithProblem = "FriendlyAppName is '$friendlyName'"
        } elseif ($command -notmatch '^"[^"]+\\patchy\.exe" "%1"$') {
            $openWithProblem = "command is $command"
        } elseif (($types -notcontains ".psd") -or ($types -notcontains ".png") -or ($types -contains ".pdf")) {
            $openWithProblem = "unexpected SupportedTypes: " + ($types -join " ")
        } else {
            # The ProgID in each extension's OpenWithProgids list is what reaches the
            # "Open with" submenu; the extension's own default value must stay unset.
            $progIdCommand = (Get-ItemProperty -Path (Join-Path $openWithRoot "Patchy.Image\shell\open\command")).'(default)'
            $psdKey = Get-Item -Path (Join-Path $openWithRoot ".psd")
            $psdProgIds = @((Get-Item -Path (Join-Path $openWithRoot ".psd\OpenWithProgids")).GetValueNames())
            if ($progIdCommand -ne $command) {
                $openWithProblem = "ProgID command is $progIdCommand"
            } elseif ($psdProgIds -notcontains "Patchy.Image") {
                $openWithProblem = ".psd OpenWithProgids lacks Patchy.Image"
            } elseif ($psdKey.ValueCount -ne 0) {
                $openWithProblem = ".psd key has values: " + ($psdKey.GetValueNames() -join " ")
            } elseif (Test-Path -LiteralPath (Join-Path $openWithRoot ".pdf")) {
                $openWithProblem = ".pdf is registered"
            }
        }
    }
    Write-Step ($openWithProblem -eq "") "installer Open with registration" $openWithProblem
} finally {
    if (Test-Path -LiteralPath $openWithRoot) {
        Remove-Item -LiteralPath $openWithRoot -Recurse -Force
    }
}

# 4. Unpack the zip and compare it with its own manifest, before anything runs in it.
Expand-Archive -LiteralPath $Zip -DestinationPath $appParent -Force
$manifestPath = Join-Path $appDir "PatchyInstallManifest.txt"
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    Write-Step $false "zip unpacks to Patchy\ with an install manifest"
    exit 1
}
$prefix = $appDir.TrimEnd("\") + "\"
$listed = @(Get-Content -LiteralPath $manifestPath | Where-Object { $_ -ne "" })
$actual = @(Get-ChildItem -LiteralPath $appDir -Recurse -File | ForEach-Object { $_.FullName.Substring($prefix.Length) })
$difference = @(Compare-Object -ReferenceObject $listed -DifferenceObject $actual)
Write-Step ($difference.Count -eq 0) "zip contents match the install manifest ($($actual.Count) files)" $(if ($difference.Count) { (($difference | Select-Object -First 5 | ForEach-Object { $_.SideIndicator + " " + $_.InputObject }) -join "; ") } else { "" })

# 4b. Explorer must also have a friendly name for portable copies, which never run
#     the installer. Read the actual executable resource, not the source template.
$appVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo((Join-Path $appDir "patchy.exe"))
$metadataOk = $appVersion.FileDescription -eq "Patchy Image Editor" -and
    $appVersion.ProductName -eq "Patchy" -and
    $appVersion.FileVersion -eq $Version -and
    $appVersion.ProductVersion -eq $Version -and
    $appVersion.OriginalFilename -eq "patchy.exe"
Write-Step $metadataOk "executable display name and version" $(if (-not $metadataOk) {
    "description='$($appVersion.FileDescription)', product='$($appVersion.ProductName)', file version='$($appVersion.FileVersion)', product version='$($appVersion.ProductVersion)', original name='$($appVersion.OriginalFilename)'"
} else { "" })

# 5. The tree is self-contained. Every import of every executable and DLL must be in the
#    tree or be part of Windows. Redistributables found only in System32 do not count: the
#    dev box has them, a clean machine does not.
$dumpbin = (Get-Command dumpbin.exe -ErrorAction SilentlyContinue | Select-Object -First 1).Source
if (-not $dumpbin) {
    $vsEnv = Join-Path $repo "scripts\vs-env.bat"
    if (Test-Path -LiteralPath $vsEnv -PathType Leaf) {
        $found = & cmd.exe /s /c ('"' + $vsEnv + '" -arch=x64 -host_arch=x64 >nul 2>&1 && where dumpbin.exe') 2>$null
        $dumpbin = @($found | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) })[0]
    }
}
if (-not $dumpbin) {
    Write-Step $false "package is self-contained" "dumpbin.exe was not found (run from a Visual Studio build environment)"
} else {
    $inTree = @{}
    Get-ChildItem -LiteralPath $appDir -Recurse -File | ForEach-Object { $inTree[$_.Name.ToLowerInvariant()] = $true }
    $redistributable = '^(qt6|msvcp|vcruntime|concrt|vccorlib|vcomp|mfc|atl)'
    $systemDirs = @($system32, (Join-Path $env:WINDIR "SysWOW64"))
    $unresolved = @()
    $binaries = @(Get-ChildItem -LiteralPath $appDir -Recurse -File | Where-Object { $_.Extension -in ".exe", ".dll" })
    foreach ($binary in $binaries) {
        $lines = & $dumpbin /nologo /dependents $binary.FullName 2>$null
        foreach ($line in $lines) {
            if ($line -notmatch '^\s+(\S+\.(dll|drv|exe))\s*$') { continue }
            $import = $Matches[1].ToLowerInvariant()
            if ($inTree.ContainsKey($import)) { continue }
            if ($import -match '^(api-ms-|ext-ms-)') { continue }
            $inWindows = @($systemDirs | Where-Object { Test-Path -LiteralPath (Join-Path $_ $import) -PathType Leaf }).Count -gt 0
            if ($inWindows -and $import -notmatch $redistributable) { continue }
            $unresolved += ($binary.FullName.Substring($prefix.Length) + " -> " + $import)
        }
    }
    Write-Step ($unresolved.Count -eq 0) "package is self-contained ($($binaries.Count) binaries)" (($unresolved | Select-Object -First 8) -join "; ")
}

# 6. The unpacked application runs the support-file self-test with nothing but Windows on
#    PATH, its own settings folder, and a bounded wait (a missing-plugin message box has
#    no console to print to).
$selfTestScript = Join-Path $repo "packaging\package-selftest.js"
$selfTestOutput = Join-Path $selfTestDir "output.txt"
$fixturePlugins = Join-Path $repo "test-fixtures\photoshop-plugins"
$savedEnv = @{}
foreach ($name in "PATH", "PATCHY_SETTINGS_DIR", "PATCHY_NO_SOUND", "QT_COMMAND_LINE_PARSER_NO_GUI_MESSAGE_BOXES") {
    $savedEnv[$name] = [Environment]::GetEnvironmentVariable($name, "Process")
}
try {
    $env:PATH = $system32 + ";" + $env:WINDIR
    $env:PATCHY_SETTINGS_DIR = Join-Path $selfTestDir "settings"
    $env:PATCHY_NO_SOUND = "1"
    $env:QT_COMMAND_LINE_PARSER_NO_GUI_MESSAGE_BOXES = "1"
    New-Item -ItemType Directory -Force -Path $env:PATCHY_SETTINGS_DIR | Out-Null
    $appArgs = "--headless --run-script " + (Quote $selfTestScript) + " --script-output " + (Quote $selfTestOutput) +
        " --script-arg " + (Quote ("work=" + (Join-Path $selfTestDir "work"))) + " --script-arg " + (Quote ("version=" + $Version))
    if (Test-Path -LiteralPath $fixturePlugins -PathType Container) {
        $appArgs += " --script-arg " + (Quote ("plugins=" + $fixturePlugins))
    }
    $run = Invoke-Bounded -FilePath (Join-Path $appDir "patchy.exe") -Arguments $appArgs -TimeoutSeconds 240 -WorkingDirectory $appDir
    $outputLines = @()
    if (Test-Path -LiteralPath $selfTestOutput -PathType Leaf) {
        $outputLines = @(Get-Content -LiteralPath $selfTestOutput | Where-Object { $_ -ne "" })
    }
    $last = $(if ($outputLines.Count) { $outputLines[-1] } else { "(no output file)" })
    $selfTestOk = (-not $run.TimedOut) -and ($run.ExitCode -eq 0) -and ($last -eq "[done]")
    $problem = ""
    if ($run.TimedOut) {
        $problem = "timed out"
    } elseif (-not $selfTestOk) {
        $problem = "exit code " + $run.ExitCode + "; " + (($outputLines | Select-Object -Last 3) -join " | ")
    }
    Write-Step $selfTestOk "unpacked patchy.exe passes the package self-test" $problem

    # 7. The connector's own package check, from the unpacked tree.
    $connector = Invoke-Bounded -FilePath (Join-Path $appDir "patchy-mcp.exe") -Arguments "--check" -TimeoutSeconds 120 -WorkingDirectory $appDir
    Write-Step ((-not $connector.TimedOut) -and ($connector.ExitCode -eq 0)) "unpacked patchy-mcp.exe --check" $(if ($connector.TimedOut) { "timed out" } elseif ($connector.ExitCode -ne 0) { "exit code " + $connector.ExitCode } else { "" })
} finally {
    foreach ($name in $savedEnv.Keys) { [Environment]::SetEnvironmentVariable($name, $savedEnv[$name], "Process") }
}

if ($script:failures.Count -gt 0) {
    Write-Host ""
    Write-Host "Windows package verification FAILED: $($script:failures -join '; ')"
    exit 1
}
Write-Host ""
Write-Host "Windows package verification passed."
exit 0
