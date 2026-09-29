[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PayloadZip,

    [string]$Version = "0.0.0",

    [switch]$Quiet
)

$ErrorActionPreference = "Stop"

function ConvertFrom-LienzoUnicodeEscapes {
    param([Parameter(Mandatory = $true)][string]$Text)

    return [regex]::Replace($Text, "\\u([0-9A-Fa-f]{4})", {
        param($Match)
        [string][char][Convert]::ToInt32($Match.Groups[1].Value, 16)
    })
}

$LienzoInstallerText = @{
    en = @{
        RunningLienzoRetryMessage = "Lienzo is currently running. Save your work, close Lienzo, then click Retry to continue installation."
        RunningLienzoQuietMessage = "Lienzo is currently running. Close Lienzo and run setup again."
        FileInUseQuietMessage = "Lienzo could not be updated because installed files are in use. Close Lienzo and run setup again."
        InstallationCanceled = "Installation canceled."
    }
    ja = @{
        RunningLienzoRetryMessage = ConvertFrom-LienzoUnicodeEscapes "Lienzo \u306F\u73FE\u5728\u5B9F\u884C\u4E2D\u3067\u3059\u3002\u4F5C\u696D\u3092\u4FDD\u5B58\u3057\u3066 Lienzo \u3092\u9589\u3058\u3066\u304B\u3089\u3001[\u518D\u8A66\u884C] \u3092\u30AF\u30EA\u30C3\u30AF\u3057\u3066\u30A4\u30F3\u30B9\u30C8\u30FC\u30EB\u3092\u7D9A\u884C\u3057\u3066\u304F\u3060\u3055\u3044\u3002"
        RunningLienzoQuietMessage = ConvertFrom-LienzoUnicodeEscapes "Lienzo \u306F\u73FE\u5728\u5B9F\u884C\u4E2D\u3067\u3059\u3002Lienzo \u3092\u9589\u3058\u3066\u304B\u3089\u30BB\u30C3\u30C8\u30A2\u30C3\u30D7\u3092\u3082\u3046\u4E00\u5EA6\u5B9F\u884C\u3057\u3066\u304F\u3060\u3055\u3044\u3002"
        FileInUseQuietMessage = ConvertFrom-LienzoUnicodeEscapes "Lienzo \u3092\u66F4\u65B0\u3067\u304D\u307E\u305B\u3093\u3067\u3057\u305F\u3002Lienzo \u3092\u9589\u3058\u3066\u304B\u3089\u30BB\u30C3\u30C8\u30A2\u30C3\u30D7\u3092\u3082\u3046\u4E00\u5EA6\u5B9F\u884C\u3057\u3066\u304F\u3060\u3055\u3044\u3002"
        InstallationCanceled = ConvertFrom-LienzoUnicodeEscapes "\u30A4\u30F3\u30B9\u30C8\u30FC\u30EB\u306F\u30AD\u30E3\u30F3\u30BB\u30EB\u3055\u308C\u307E\u3057\u305F\u3002"
    }
    de = @{
        RunningLienzoRetryMessage = ConvertFrom-LienzoUnicodeEscapes "Lienzo wird derzeit ausgef\u00FChrt. Speichern Sie Ihre Arbeit, schlie\u00DFen Sie Lienzo und klicken Sie dann auf \u201EWiederholen\u201C, um die Installation fortzusetzen."
        RunningLienzoQuietMessage = ConvertFrom-LienzoUnicodeEscapes "Lienzo wird derzeit ausgef\u00FChrt. Schlie\u00DFen Sie Lienzo und f\u00FChren Sie das Setup erneut aus."
        FileInUseQuietMessage = ConvertFrom-LienzoUnicodeEscapes "Lienzo konnte nicht aktualisiert werden, weil installierte Dateien gerade verwendet werden. Schlie\u00DFen Sie Lienzo und f\u00FChren Sie das Setup erneut aus."
        InstallationCanceled = ConvertFrom-LienzoUnicodeEscapes "Installation abgebrochen."
    }
    es = @{
        RunningLienzoRetryMessage = ConvertFrom-LienzoUnicodeEscapes "Lienzo se est\u00E1 ejecutando. Guarde su trabajo, cierre Lienzo y haga clic en Reintentar para continuar con la instalaci\u00F3n."
        RunningLienzoQuietMessage = ConvertFrom-LienzoUnicodeEscapes "Lienzo se est\u00E1 ejecutando. Cierre Lienzo y vuelva a ejecutar el instalador."
        FileInUseQuietMessage = ConvertFrom-LienzoUnicodeEscapes "No se pudo actualizar Lienzo porque los archivos instalados est\u00E1n en uso. Cierre Lienzo y vuelva a ejecutar el instalador."
        InstallationCanceled = ConvertFrom-LienzoUnicodeEscapes "Instalaci\u00F3n cancelada."
    }
    fr = @{
        RunningLienzoRetryMessage = ConvertFrom-LienzoUnicodeEscapes "Lienzo est en cours d'ex\u00E9cution. Enregistrez votre travail, fermez Lienzo, puis cliquez sur R\u00E9essayer pour poursuivre l'installation."
        RunningLienzoQuietMessage = ConvertFrom-LienzoUnicodeEscapes "Lienzo est en cours d'ex\u00E9cution. Fermez Lienzo, puis relancez le programme d'installation."
        FileInUseQuietMessage = ConvertFrom-LienzoUnicodeEscapes "Impossible de mettre \u00E0 jour Lienzo, car des fichiers install\u00E9s sont en cours d'utilisation. Fermez Lienzo, puis relancez le programme d'installation."
        InstallationCanceled = ConvertFrom-LienzoUnicodeEscapes "Installation annul\u00E9e."
    }
    it = @{
        RunningLienzoRetryMessage = ConvertFrom-LienzoUnicodeEscapes "Lienzo \u00E8 in esecuzione. Salva il lavoro, chiudi Lienzo e fai clic su Riprova per continuare l'installazione."
        RunningLienzoQuietMessage = ConvertFrom-LienzoUnicodeEscapes "Lienzo \u00E8 in esecuzione. Chiudi Lienzo ed esegui di nuovo il programma di installazione."
        FileInUseQuietMessage = ConvertFrom-LienzoUnicodeEscapes "Impossibile aggiornare Lienzo perch\u00E9 i file installati sono in uso. Chiudi Lienzo ed esegui di nuovo il programma di installazione."
        InstallationCanceled = ConvertFrom-LienzoUnicodeEscapes "Installazione annullata."
    }
    zh_CN = @{
        RunningLienzoRetryMessage = ConvertFrom-LienzoUnicodeEscapes "Lienzo \u6B63\u5728\u8FD0\u884C\u3002\u8BF7\u4FDD\u5B58\u60A8\u7684\u5DE5\u4F5C\uFF0C\u5173\u95ED Lienzo\uFF0C\u7136\u540E\u5355\u51FB\u201C\u91CD\u8BD5\u201D\u7EE7\u7EED\u5B89\u88C5\u3002"
        RunningLienzoQuietMessage = ConvertFrom-LienzoUnicodeEscapes "Lienzo \u6B63\u5728\u8FD0\u884C\u3002\u8BF7\u5173\u95ED Lienzo\uFF0C\u7136\u540E\u91CD\u65B0\u8FD0\u884C\u5B89\u88C5\u7A0B\u5E8F\u3002"
        FileInUseQuietMessage = ConvertFrom-LienzoUnicodeEscapes "\u65E0\u6CD5\u66F4\u65B0 Lienzo\uFF0C\u56E0\u4E3A\u5DF2\u5B89\u88C5\u7684\u6587\u4EF6\u6B63\u5728\u4F7F\u7528\u4E2D\u3002\u8BF7\u5173\u95ED Lienzo\uFF0C\u7136\u540E\u91CD\u65B0\u8FD0\u884C\u5B89\u88C5\u7A0B\u5E8F\u3002"
        InstallationCanceled = ConvertFrom-LienzoUnicodeEscapes "\u5B89\u88C5\u5DF2\u53D6\u6D88\u3002"
    }
    zh_TW = @{
        RunningLienzoRetryMessage = ConvertFrom-LienzoUnicodeEscapes "Lienzo \u6B63\u5728\u57F7\u884C\u4E2D\u3002\u8ACB\u5132\u5B58\u60A8\u7684\u5DE5\u4F5C\uFF0C\u95DC\u9589 Lienzo\uFF0C\u7136\u5F8C\u6309\u4E00\u4E0B\u300C\u91CD\u8A66\u300D\u4EE5\u7E7C\u7E8C\u5B89\u88DD\u3002"
        RunningLienzoQuietMessage = ConvertFrom-LienzoUnicodeEscapes "Lienzo \u6B63\u5728\u57F7\u884C\u4E2D\u3002\u8ACB\u95DC\u9589 Lienzo\uFF0C\u7136\u5F8C\u91CD\u65B0\u57F7\u884C\u5B89\u88DD\u7A0B\u5F0F\u3002"
        FileInUseQuietMessage = ConvertFrom-LienzoUnicodeEscapes "\u7121\u6CD5\u66F4\u65B0 Lienzo\uFF0C\u56E0\u70BA\u5DF2\u5B89\u88DD\u7684\u6A94\u6848\u6B63\u5728\u4F7F\u7528\u4E2D\u3002\u8ACB\u95DC\u9589 Lienzo\uFF0C\u7136\u5F8C\u91CD\u65B0\u57F7\u884C\u5B89\u88DD\u7A0B\u5F0F\u3002"
        InstallationCanceled = ConvertFrom-LienzoUnicodeEscapes "\u5B89\u88DD\u5DF2\u53D6\u6D88\u3002"
    }
}

function Get-LienzoInstallerLanguage {
    # Maps the Windows UI culture to one of the shipped installer languages; English is the fallback.
    $culture = [Globalization.CultureInfo]::CurrentUICulture.Name
    if ([string]::IsNullOrWhiteSpace($culture)) {
        return "en"
    }

    $tags = $culture -split "-"
    $language = $tags[0].ToLowerInvariant()
    if ($language -eq "zh") {
        # Traditional Chinese for Taiwan, Hong Kong, Macao, and any Hant script tag (zh-TW, zh-Hant-HK, ...).
        # Every other Chinese culture (zh-CN, zh-Hans-CN, zh-SG, ...) reads Simplified.
        $traditionalTags = @("tw", "hk", "mo", "hant")
        for ($i = 1; $i -lt $tags.Length; $i++) {
            if ($traditionalTags -contains $tags[$i].ToLowerInvariant()) {
                return "zh_TW"
            }
        }
        return "zh_CN"
    }

    if (@("de", "es", "fr", "it", "ja") -contains $language) {
        return $language
    }
    return "en"
}

function Get-LienzoInstallerText {
    param([Parameter(Mandatory = $true)][string]$Key)

    $language = Get-LienzoInstallerLanguage
    if ($LienzoInstallerText.ContainsKey($language) -and $LienzoInstallerText[$language].ContainsKey($Key)) {
        return $LienzoInstallerText[$language][$Key]
    }
    return $LienzoInstallerText["en"][$Key]
}

function Test-PathInsideRoot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Root
    )

    $fullPath = [IO.Path]::GetFullPath($Path)
    $fullRoot = [IO.Path]::GetFullPath($Root)
    return $fullPath.StartsWith($fullRoot + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)
}

function Test-PathAtOrInsideRoot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Root
    )

    $trimChars = [char[]]@([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd($trimChars)
    $fullRoot = [IO.Path]::GetFullPath($Root).TrimEnd($trimChars)
    return $fullPath.Equals($fullRoot, [StringComparison]::OrdinalIgnoreCase) -or
        $fullPath.StartsWith($fullRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)
}

function Get-RunningInstalledLienzoProcess {
    param([Parameter(Mandatory = $true)][string]$InstallRoot)

    Get-Process -Name "patchy" -ErrorAction SilentlyContinue | Where-Object {
        $processPath = $null
        try {
            $processPath = $_.MainModule.FileName
        } catch {
            $processPath = $null
        }
        $processPath -and (Test-PathAtOrInsideRoot -Path $processPath -Root $InstallRoot)
    }
}

function Request-CloseLienzoRetry {
    param([object]$Owner = $null)

    Add-Type -AssemblyName System.Windows.Forms
    $message = Get-LienzoInstallerText "RunningLienzoRetryMessage"
    if ($Owner -ne $null) {
        $result = [System.Windows.Forms.MessageBox]::Show(
            $Owner,
            $message,
            "Lienzo Setup",
            [System.Windows.Forms.MessageBoxButtons]::RetryCancel,
            [System.Windows.Forms.MessageBoxIcon]::Warning,
            [System.Windows.Forms.MessageBoxDefaultButton]::Button1
        )
    } else {
        $result = [System.Windows.Forms.MessageBox]::Show(
            $message,
            "Lienzo Setup",
            [System.Windows.Forms.MessageBoxButtons]::RetryCancel,
            [System.Windows.Forms.MessageBoxIcon]::Warning,
            [System.Windows.Forms.MessageBoxDefaultButton]::Button1
        )
    }
    return $result -eq [System.Windows.Forms.DialogResult]::Retry
}

function Confirm-LienzoClosedForInstall {
    param(
        [Parameter(Mandatory = $true)]
        [string]$InstallRoot,

        [bool]$Quiet = $false,

        [object]$Owner = $null
    )

    while (@(Get-RunningInstalledLienzoProcess -InstallRoot $InstallRoot).Count -gt 0) {
        if ($Quiet -or -not [Environment]::UserInteractive) {
            throw (Get-LienzoInstallerText "RunningLienzoQuietMessage")
        }
        if (-not (Request-CloseLienzoRetry -Owner $Owner)) {
            throw (New-Object System.OperationCanceledException (Get-LienzoInstallerText "InstallationCanceled"))
        }
    }
}

function Test-LienzoFileInUseInstallError {
    param([Parameter(Mandatory = $true)]$ErrorRecord)

    $exception = $ErrorRecord.Exception
    while ($exception -ne $null) {
        if ($exception -is [System.IO.IOException]) {
            $win32Code = $exception.HResult -band 0xffff
            if ($win32Code -eq 32 -or $win32Code -eq 33) {
                return $true
            }
        }
        if ($exception.Message -match "being used by another process") {
            return $true
        }
        $exception = $exception.InnerException
    }
    return $false
}

function New-LienzoShortcut {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ShortcutPath,

        [Parameter(Mandatory = $true)]
        [string]$TargetPath,

        [Parameter(Mandatory = $true)]
        [string]$WorkingDirectory,

        [string]$IconPath = ""
    )

    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($ShortcutPath)
    $shortcut.TargetPath = $TargetPath
    $shortcut.WorkingDirectory = $WorkingDirectory
    if ($IconPath -and (Test-Path -LiteralPath $IconPath -PathType Leaf)) {
        $shortcut.IconLocation = "$IconPath,0"
    } else {
        $shortcut.IconLocation = "$TargetPath,0"
    }
    $shortcut.Description = "Lienzo"
    $shortcut.Save()
}

$ManifestFileName = "LienzoInstallManifest.txt"
$LegacyInstalledRelativePaths = @(
    "patchy.exe",
    "Lienzo.ico",
    "UninstallLienzo.exe",
    "UninstallLienzo.ps1",
    "LICENSE",
    "README.md",
    "NOTICE-THIRD-PARTY.md",
    "Qt6Core.dll",
    "Qt6Gui.dll",
    "Qt6PrintSupport.dll",
    "Qt6Svg.dll",
    "Qt6Widgets.dll",
    "concrt140.dll",
    "msvcp140.dll",
    "msvcp140_1.dll",
    "msvcp140_2.dll",
    "msvcp140_atomic_wait.dll",
    "msvcp140_codecvt_ids.dll",
    "vccorlib140.dll",
    "vcruntime140.dll",
    "vcruntime140_1.dll",
    "vcruntime140_threads.dll",
    "iconengines\qsvgicon.dll",
    "imageformats\qjpeg.dll",
    "imageformats\qsvg.dll",
    "imageformats\qtiff.dll",
    "imageformats\qwebp.dll",
    "platforms\qwindows.dll",
    "styles\qmodernwindowsstyle.dll",
    "licenses\qt\qtbase-6.8.3.spdx",
    "licenses\qt\qtimageformats-6.8.3.spdx",
    "licenses\qt\qtsvg-6.8.3.spdx"
)

function Test-SafeRelativeInstallPath {
    param([Parameter(Mandatory = $true)][string]$RelativePath)

    if ([IO.Path]::IsPathRooted($RelativePath)) {
        return $false
    }

    $parts = $RelativePath -split '[\\/]'
    foreach ($part in $parts) {
        if ([string]::IsNullOrWhiteSpace($part) -or $part -eq "." -or $part -eq "..") {
            return $false
        }
    }
    return $true
}

function Get-InstalledRelativePaths {
    param([Parameter(Mandatory = $true)][string]$InstallRoot)

    $manifest = Join-Path $InstallRoot $ManifestFileName
    if (Test-Path -LiteralPath $manifest -PathType Leaf) {
        $paths = Get-Content -LiteralPath $manifest | Where-Object {
            -not [string]::IsNullOrWhiteSpace($_) -and (Test-SafeRelativeInstallPath -RelativePath $_)
        }
        if ($paths -notcontains $ManifestFileName) {
            $paths = @($paths) + $ManifestFileName
        }
        return $paths
    }

    return $LegacyInstalledRelativePaths
}

function Remove-EmptyInstallDirectories {
    param([Parameter(Mandatory = $true)][string]$InstallRoot)

    if (-not (Test-Path -LiteralPath $InstallRoot -PathType Container)) {
        return
    }

    Get-ChildItem -LiteralPath $InstallRoot -Directory -Recurse -Force |
        Sort-Object FullName -Descending |
        ForEach-Object {
            if (-not (Get-ChildItem -LiteralPath $_.FullName -Force -ErrorAction SilentlyContinue)) {
                Remove-Item -LiteralPath $_.FullName -Force -ErrorAction SilentlyContinue
            }
        }
}

function Remove-LienzoInstalledFiles {
    param([Parameter(Mandatory = $true)][string]$InstallRoot)

    if (-not (Test-Path -LiteralPath $InstallRoot -PathType Container)) {
        return
    }

    foreach ($relativePath in Get-InstalledRelativePaths -InstallRoot $InstallRoot) {
        if (-not (Test-SafeRelativeInstallPath -RelativePath $relativePath)) {
            continue
        }
        $target = Join-Path $InstallRoot $relativePath
        if (Test-Path -LiteralPath $target -PathType Leaf) {
            Remove-Item -LiteralPath $target -Force -ErrorAction SilentlyContinue
        }
    }

    Remove-EmptyInstallDirectories -InstallRoot $InstallRoot
}

function Add-LienzoInstalledRelativePath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$InstallRoot,

        [Parameter(Mandatory = $true)]
        [string]$RelativePath
    )

    if (-not (Test-SafeRelativeInstallPath -RelativePath $RelativePath)) {
        throw "Unsafe install manifest path: $RelativePath"
    }

    $manifest = Join-Path $InstallRoot $ManifestFileName
    $paths = @()
    if (Test-Path -LiteralPath $manifest -PathType Leaf) {
        $paths = @(Get-Content -LiteralPath $manifest | Where-Object {
            -not [string]::IsNullOrWhiteSpace($_) -and (Test-SafeRelativeInstallPath -RelativePath $_)
        })
    }

    if ($paths -notcontains $RelativePath) {
        $paths = @($paths) + $RelativePath
        Set-Content -LiteralPath $manifest -Value ($paths | Sort-Object -Unique) -Encoding ASCII
    }
}

function Invoke-LienzoInstall {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PayloadZip,

        [Parameter(Mandatory = $true)]
        [string]$InstallParent,

        [Parameter(Mandatory = $true)]
        [string]$InstallRoot,

        [Parameter(Mandatory = $true)]
        [string]$StartMenuDirectory,

        [Parameter(Mandatory = $true)]
        [string]$StartMenuShortcut,

        [Parameter(Mandatory = $true)]
        [string]$DesktopShortcut,

        [bool]$CreateDesktopShortcut = $true,

        [Parameter(Mandatory = $true)]
        [string]$UninstallKey,

        [Parameter(Mandatory = $true)]
        [string]$Version,

        [bool]$Quiet = $false,

        [object]$Owner = $null
    )

    $tempRoot = Join-Path ([IO.Path]::GetTempPath()) ("LienzoInstall-" + [guid]::NewGuid().ToString("N"))

    try {
        if (-not (Test-Path -LiteralPath $PayloadZip -PathType Leaf)) {
            throw "Installer payload was not found: $PayloadZip"
        }

        if (-not (Test-PathInsideRoot -Path $InstallRoot -Root $InstallParent)) {
            throw "Refusing to install outside the per-user Programs directory: $InstallRoot"
        }

        New-Item -ItemType Directory -Path $InstallParent -Force | Out-Null
        Confirm-LienzoClosedForInstall -InstallRoot $InstallRoot -Quiet $Quiet -Owner $Owner
        New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null

        Expand-Archive -LiteralPath $PayloadZip -DestinationPath $tempRoot -Force
        $sourceRoot = Join-Path $tempRoot "Lienzo"
        $sourceExe = Join-Path $sourceRoot "patchy.exe"
        if (-not (Test-Path -LiteralPath $sourceExe -PathType Leaf)) {
            throw "Installer payload does not contain Lienzo\patchy.exe."
        }

        Remove-LienzoInstalledFiles -InstallRoot $InstallRoot
        New-Item -ItemType Directory -Path $InstallRoot -Force | Out-Null
        Get-ChildItem -LiteralPath $sourceRoot -Force | ForEach-Object {
            Copy-Item -LiteralPath $_.FullName -Destination $InstallRoot -Recurse -Force
        }

        $payloadDirectory = Split-Path -Parent $PayloadZip
        $uninstallerSource = Join-Path $payloadDirectory "UninstallLienzo.exe"
        $installedExe = Join-Path $InstallRoot "patchy.exe"
        $installedIcon = Join-Path $InstallRoot "Lienzo.ico"
        $uninstallerExe = Join-Path $InstallRoot "UninstallLienzo.exe"
        if (Test-Path -LiteralPath $uninstallerSource -PathType Leaf) {
            Copy-Item -LiteralPath $uninstallerSource -Destination $uninstallerExe -Force
        }
        if (-not (Test-Path -LiteralPath $uninstallerExe -PathType Leaf)) {
            throw "Installer payload does not contain Lienzo\UninstallLienzo.exe."
        }
        Add-LienzoInstalledRelativePath -InstallRoot $InstallRoot -RelativePath "UninstallLienzo.exe"

        New-Item -ItemType Directory -Path $StartMenuDirectory -Force | Out-Null
        New-LienzoShortcut -ShortcutPath $StartMenuShortcut -TargetPath $installedExe -WorkingDirectory $InstallRoot -IconPath $installedIcon
        if ($CreateDesktopShortcut) {
            try {
                $desktopDirectory = Split-Path -Parent $DesktopShortcut
                if (-not [string]::IsNullOrWhiteSpace($desktopDirectory)) {
                    New-Item -ItemType Directory -Path $desktopDirectory -Force | Out-Null
                }
                New-LienzoShortcut -ShortcutPath $DesktopShortcut -TargetPath $installedExe -WorkingDirectory $InstallRoot -IconPath $installedIcon
            } catch {
                Write-Warning "Could not create the desktop shortcut: $($_.Exception.Message)"
            }
        }

        $estimatedSizeKb = [int][math]::Ceiling(
            ((Get-ChildItem -LiteralPath $InstallRoot -Recurse -File | Measure-Object -Property Length -Sum).Sum) / 1KB
        )

        New-Item -Path $UninstallKey -Force | Out-Null
        New-ItemProperty -Path $UninstallKey -Name "DisplayName" -Value "Lienzo" -PropertyType String -Force | Out-Null
        New-ItemProperty -Path $UninstallKey -Name "DisplayVersion" -Value $Version -PropertyType String -Force | Out-Null
        New-ItemProperty -Path $UninstallKey -Name "Publisher" -Value "Seth A. Robinson" -PropertyType String -Force | Out-Null
        New-ItemProperty -Path $UninstallKey -Name "DisplayIcon" -Value $installedIcon -PropertyType String -Force | Out-Null
        New-ItemProperty -Path $UninstallKey -Name "InstallLocation" -Value $InstallRoot -PropertyType String -Force | Out-Null
        New-ItemProperty -Path $UninstallKey -Name "UninstallString" -Value "`"$uninstallerExe`"" -PropertyType String -Force | Out-Null
        New-ItemProperty -Path $UninstallKey -Name "QuietUninstallString" -Value "`"$uninstallerExe`" /quiet" -PropertyType String -Force | Out-Null
        New-ItemProperty -Path $UninstallKey -Name "NoModify" -Value 1 -PropertyType DWord -Force | Out-Null
        New-ItemProperty -Path $UninstallKey -Name "NoRepair" -Value 1 -PropertyType DWord -Force | Out-Null
        New-ItemProperty -Path $UninstallKey -Name "EstimatedSize" -Value $estimatedSizeKb -PropertyType DWord -Force | Out-Null

        return $installedExe
    } finally {
        if (Test-Path -LiteralPath $tempRoot) {
            Remove-Item -LiteralPath $tempRoot -Recurse -Force
        }
    }
}

function Invoke-LienzoInstallWithRetry {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PayloadZip,

        [Parameter(Mandatory = $true)]
        [string]$InstallParent,

        [Parameter(Mandatory = $true)]
        [string]$InstallRoot,

        [Parameter(Mandatory = $true)]
        [string]$StartMenuDirectory,

        [Parameter(Mandatory = $true)]
        [string]$StartMenuShortcut,

        [Parameter(Mandatory = $true)]
        [string]$DesktopShortcut,

        [bool]$CreateDesktopShortcut = $true,

        [Parameter(Mandatory = $true)]
        [string]$UninstallKey,

        [Parameter(Mandatory = $true)]
        [string]$Version,

        [bool]$Quiet = $false,

        [object]$Owner = $null
    )

    while ($true) {
        try {
            return Invoke-LienzoInstall `
                -PayloadZip $PayloadZip `
                -InstallParent $InstallParent `
                -InstallRoot $InstallRoot `
                -StartMenuDirectory $StartMenuDirectory `
                -StartMenuShortcut $StartMenuShortcut `
                -DesktopShortcut $DesktopShortcut `
                -CreateDesktopShortcut $CreateDesktopShortcut `
                -UninstallKey $UninstallKey `
                -Version $Version `
                -Quiet $Quiet `
                -Owner $Owner
        } catch [System.OperationCanceledException] {
            throw
        } catch {
            if (-not (Test-LienzoFileInUseInstallError -ErrorRecord $_)) {
                throw
            }
            if ($Quiet -or -not [Environment]::UserInteractive) {
                throw (Get-LienzoInstallerText "FileInUseQuietMessage")
            }
            if (-not (Request-CloseLienzoRetry -Owner $Owner)) {
                throw (New-Object System.OperationCanceledException (Get-LienzoInstallerText "InstallationCanceled"))
            }
        }
    }
}

function New-LienzoLogoBitmap {
    param([int]$Size = 64)

    $bitmap = New-Object System.Drawing.Bitmap $Size, $Size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $scale = $Size / 64.0
    $sx = { param([double]$value) [single]($value * $scale) }

    $tile = New-Object System.Drawing.RectangleF (& $sx 7), (& $sx 7), (& $sx 50), (& $sx 50)
    $gradient = New-Object System.Drawing.Drawing2D.LinearGradientBrush $tile,
        ([System.Drawing.Color]::FromArgb(88, 170, 235)),
        ([System.Drawing.Color]::FromArgb(242, 177, 92)),
        45
    $blend = New-Object System.Drawing.Drawing2D.ColorBlend 3
    $blend.Positions = [single[]](0.0, 0.55, 1.0)
    $blend.Colors = [System.Drawing.Color[]](
        [System.Drawing.Color]::FromArgb(88, 170, 235),
        [System.Drawing.Color]::FromArgb(132, 214, 169),
        [System.Drawing.Color]::FromArgb(242, 177, 92)
    )
    $gradient.InterpolationColors = $blend
    $graphics.FillRectangle($gradient, $tile)
    $gradient.Dispose()

    $inner = New-Object System.Drawing.RectangleF (& $sx 11), (& $sx 11), (& $sx 42), (& $sx 42)
    $graphics.FillRectangle((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(23, 30, 40))), $inner)

    $canvas = New-Object System.Drawing.RectangleF (& $sx 19), (& $sx 19), (& $sx 26), (& $sx 24)
    $graphics.FillRectangle((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(247, 249, 252))), $canvas)
    $graphics.FillRectangle((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(88, 170, 235))), (& $sx 24), (& $sx 24), (& $sx 13), (& $sx 12))
    $graphics.FillRectangle((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(132, 214, 169))), (& $sx 27), (& $sx 31), (& $sx 13), (& $sx 12))

    $patch = New-Object System.Drawing.Drawing2D.GraphicsPath
    $patch.AddPolygon([System.Drawing.PointF[]]@(
        (New-Object System.Drawing.PointF (& $sx 27), (& $sx 35)),
        (New-Object System.Drawing.PointF (& $sx 36), (& $sx 28)),
        (New-Object System.Drawing.PointF (& $sx 49), (& $sx 36)),
        (New-Object System.Drawing.PointF (& $sx 39), (& $sx 41))
    ))
    $graphics.FillPath((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(242, 177, 92))), $patch)
    $patch.Dispose()
    $graphics.Dispose()

    return $bitmap
}

function Show-LienzoInstallerWizard {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PayloadZip,

        [Parameter(Mandatory = $true)]
        [string]$InstallParent,

        [Parameter(Mandatory = $true)]
        [string]$InstallRoot,

        [Parameter(Mandatory = $true)]
        [string]$StartMenuDirectory,

        [Parameter(Mandatory = $true)]
        [string]$StartMenuShortcut,

        [Parameter(Mandatory = $true)]
        [string]$DesktopShortcut,

        [Parameter(Mandatory = $true)]
        [string]$UninstallKey,

        [Parameter(Mandatory = $true)]
        [string]$Version
    )

    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    [System.Windows.Forms.Application]::EnableVisualStyles()

    $state = @{
        InstalledExe = $null
        Launch = $false
        Completed = $false
    }

    $form = New-Object System.Windows.Forms.Form
    $form.Text = "Lienzo Setup"
    $form.StartPosition = "CenterScreen"
    $form.FormBorderStyle = "FixedDialog"
    $form.MaximizeBox = $false
    $form.MinimizeBox = $true
    $form.ClientSize = New-Object System.Drawing.Size 560, 340
    $form.Font = New-Object System.Drawing.Font "Segoe UI", 9
    $form.BackColor = [System.Drawing.Color]::White
    $form.Tag = "ready"
    $formIcon = $null
    $installerIconPath = Join-Path (Split-Path -Parent $PayloadZip) "Lienzo.ico"
    if (Test-Path -LiteralPath $installerIconPath -PathType Leaf) {
        $formIcon = New-Object System.Drawing.Icon $installerIconPath
        $form.Icon = $formIcon
    }

    $leftPanel = New-Object System.Windows.Forms.Panel
    $leftPanel.BackColor = [System.Drawing.Color]::FromArgb(23, 30, 40)
    $leftPanel.Dock = [System.Windows.Forms.DockStyle]::Left
    $leftPanel.Width = 148
    $form.Controls.Add($leftPanel)

    $logo = New-Object System.Windows.Forms.PictureBox
    $logo.Size = New-Object System.Drawing.Size 74, 74
    $logo.Location = New-Object System.Drawing.Point 37, 42
    $logo.Image = New-LienzoLogoBitmap 74
    $logo.SizeMode = [System.Windows.Forms.PictureBoxSizeMode]::CenterImage
    $leftPanel.Controls.Add($logo)

    $brand = New-Object System.Windows.Forms.Label
    $brand.Text = "Lienzo"
    $brand.ForeColor = [System.Drawing.Color]::White
    $brand.BackColor = [System.Drawing.Color]::Transparent
    $brand.Font = New-Object System.Drawing.Font "Segoe UI Semibold", 18
    $brand.AutoSize = $true
    $brand.Location = New-Object System.Drawing.Point 36, 130
    $leftPanel.Controls.Add($brand)

    $contentLeft = 176
    $title = New-Object System.Windows.Forms.Label
    $title.Text = "Install Lienzo"
    $title.Font = New-Object System.Drawing.Font "Segoe UI Semibold", 15
    $title.ForeColor = [System.Drawing.Color]::FromArgb(23, 30, 40)
    $title.AutoSize = $true
    $title.Location = New-Object System.Drawing.Point $contentLeft, 34
    $form.Controls.Add($title)

    $body = New-Object System.Windows.Forms.Label
    $body.Text = "Setup will install Lienzo for the current Windows user and add a Start Menu shortcut."
    $body.ForeColor = [System.Drawing.Color]::FromArgb(63, 72, 84)
    $body.Size = New-Object System.Drawing.Size 340, 42
    $body.Location = New-Object System.Drawing.Point $contentLeft, 74
    $form.Controls.Add($body)

    $pathLabel = New-Object System.Windows.Forms.Label
    $pathLabel.Text = "Install location"
    $pathLabel.ForeColor = [System.Drawing.Color]::FromArgb(63, 72, 84)
    $pathLabel.AutoSize = $true
    $pathLabel.Location = New-Object System.Drawing.Point $contentLeft, 132
    $form.Controls.Add($pathLabel)

    $pathBox = New-Object System.Windows.Forms.TextBox
    $pathBox.Text = $InstallRoot
    $pathBox.ReadOnly = $true
    $pathBox.BorderStyle = [System.Windows.Forms.BorderStyle]::FixedSingle
    $pathBox.Location = New-Object System.Drawing.Point $contentLeft, 156
    $pathBox.Size = New-Object System.Drawing.Size 344, 24
    $form.Controls.Add($pathBox)

    $desktopShortcutCheck = New-Object System.Windows.Forms.CheckBox
    $desktopShortcutCheck.Text = "Create a desktop shortcut"
    $desktopShortcutCheck.Checked = $true
    $desktopShortcutCheck.AutoSize = $true
    $desktopShortcutCheck.Location = New-Object System.Drawing.Point $contentLeft, 190
    $form.Controls.Add($desktopShortcutCheck)

    $legalNotice = New-Object System.Windows.Forms.Label
    $legalNotice.Text = "Lienzo is provided under the MIT License as-is, without warranty. Keep backups of important files."
    $legalNotice.ForeColor = [System.Drawing.Color]::FromArgb(83, 92, 104)
    $legalNotice.Size = New-Object System.Drawing.Size 344, 36
    $legalNotice.Location = New-Object System.Drawing.Point $contentLeft, 218
    $form.Controls.Add($legalNotice)

    $status = New-Object System.Windows.Forms.Label
    $status.Text = ""
    $status.ForeColor = [System.Drawing.Color]::FromArgb(63, 72, 84)
    $status.Size = New-Object System.Drawing.Size 344, 24
    $status.Location = New-Object System.Drawing.Point $contentLeft, 248
    $form.Controls.Add($status)

    $progress = New-Object System.Windows.Forms.ProgressBar
    $progress.Location = New-Object System.Drawing.Point $contentLeft, 274
    $progress.Size = New-Object System.Drawing.Size 344, 18
    $progress.Style = [System.Windows.Forms.ProgressBarStyle]::Marquee
    $progress.MarqueeAnimationSpeed = 30
    $progress.Visible = $false
    $form.Controls.Add($progress)

    $launchCheck = New-Object System.Windows.Forms.CheckBox
    $launchCheck.Text = "Launch Lienzo now"
    $launchCheck.Checked = $true
    $launchCheck.AutoSize = $true
    $launchCheck.Location = New-Object System.Drawing.Point $contentLeft, 158
    $launchCheck.Visible = $false
    $form.Controls.Add($launchCheck)

    $buttonPanel = New-Object System.Windows.Forms.Panel
    $buttonPanel.Height = 58
    $buttonPanel.Dock = [System.Windows.Forms.DockStyle]::Bottom
    $buttonPanel.BackColor = [System.Drawing.Color]::FromArgb(246, 248, 251)
    $form.Controls.Add($buttonPanel)

    $installButton = New-Object System.Windows.Forms.Button
    $installButton.Text = "Install"
    $installButton.Size = New-Object System.Drawing.Size 92, 30
    $installButton.Location = New-Object System.Drawing.Point 356, 14
    $installButton.UseVisualStyleBackColor = $true
    $buttonPanel.Controls.Add($installButton)

    $cancelButton = New-Object System.Windows.Forms.Button
    $cancelButton.Text = "Cancel"
    $cancelButton.Size = New-Object System.Drawing.Size 92, 30
    $cancelButton.Location = New-Object System.Drawing.Point 456, 14
    $cancelButton.UseVisualStyleBackColor = $true
    $buttonPanel.Controls.Add($cancelButton)

    $installButton.Add_Click({
        if ($form.Tag -eq "complete") {
            $state.Launch = $launchCheck.Checked
            $form.DialogResult = [System.Windows.Forms.DialogResult]::OK
            $form.Close()
            return
        }

        $installButton.Enabled = $false
        $cancelButton.Enabled = $false
        $progress.Visible = $true
        $status.Text = "Installing Lienzo..."
        $form.UseWaitCursor = $true
        $form.Refresh()
        [System.Windows.Forms.Application]::DoEvents()

        try {
            $state.InstalledExe = Invoke-LienzoInstallWithRetry `
                -PayloadZip $PayloadZip `
                -InstallParent $InstallParent `
                -InstallRoot $InstallRoot `
                -StartMenuDirectory $StartMenuDirectory `
                -StartMenuShortcut $StartMenuShortcut `
                -DesktopShortcut $DesktopShortcut `
                -CreateDesktopShortcut $desktopShortcutCheck.Checked `
                -UninstallKey $UninstallKey `
                -Version $Version `
                -Owner $form

            $state.Completed = $true
            $form.Tag = "complete"
            $title.Text = "Lienzo has been installed"
            $body.Text = "Setup finished installing Lienzo on this computer."
            $pathLabel.Visible = $false
            $pathBox.Visible = $false
            $desktopShortcutCheck.Visible = $false
            $status.Text = ""
            $launchCheck.Visible = $true
            $installButton.Text = "Finish"
            $cancelButton.Visible = $false
        } catch [System.OperationCanceledException] {
            $status.Text = Get-LienzoInstallerText "InstallationCanceled"
            $cancelButton.Enabled = $true
        } catch {
            $status.Text = "Installation failed."
            $cancelButton.Enabled = $true
            [System.Windows.Forms.MessageBox]::Show(
                $form,
                $_.Exception.Message,
                "Lienzo Setup",
                [System.Windows.Forms.MessageBoxButtons]::OK,
                [System.Windows.Forms.MessageBoxIcon]::Error
            ) | Out-Null
        } finally {
            $progress.Visible = $false
            $form.UseWaitCursor = $false
            $installButton.Enabled = $true
        }
    })

    $cancelButton.Add_Click({
        $form.DialogResult = [System.Windows.Forms.DialogResult]::Cancel
        $form.Close()
    })

    $form.AcceptButton = $installButton
    $form.CancelButton = $cancelButton
    [void]$form.ShowDialog()

    if ($logo.Image) {
        $logo.Image.Dispose()
    }
    if ($formIcon) {
        $formIcon.Dispose()
    }
    $form.Dispose()

    return [pscustomobject]$state
}

$installParent = Join-Path $env:LOCALAPPDATA "Programs"
$installRoot = Join-Path $installParent "Lienzo"
$startMenuDirectory = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs"
$startMenuShortcut = Join-Path $startMenuDirectory "Lienzo.lnk"
$desktopShortcut = Join-Path ([System.Environment]::GetFolderPath([System.Environment+SpecialFolder]::DesktopDirectory)) "Lienzo.lnk"
$uninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Lienzo"

try {
    if ($Quiet -or -not [Environment]::UserInteractive) {
        $installedExe = Invoke-LienzoInstallWithRetry `
            -PayloadZip $PayloadZip `
            -InstallParent $installParent `
            -InstallRoot $installRoot `
            -StartMenuDirectory $startMenuDirectory `
            -StartMenuShortcut $startMenuShortcut `
            -DesktopShortcut $desktopShortcut `
            -CreateDesktopShortcut $true `
            -UninstallKey $uninstallKey `
            -Version $Version `
            -Quiet $true
        Write-Host "Lienzo installed to $installRoot"
        exit 0
    }

    $result = Show-LienzoInstallerWizard `
        -PayloadZip $PayloadZip `
        -InstallParent $installParent `
        -InstallRoot $installRoot `
        -StartMenuDirectory $startMenuDirectory `
        -StartMenuShortcut $startMenuShortcut `
        -DesktopShortcut $desktopShortcut `
        -UninstallKey $uninstallKey `
        -Version $Version

    if ($result.Completed) {
        Write-Host "Lienzo installed to $installRoot"
        if ($result.Launch -and (Test-Path -LiteralPath $result.InstalledExe -PathType Leaf)) {
            Start-Process -FilePath $result.InstalledExe -WorkingDirectory $installRoot
        }
    }

    exit 0
} catch {
    if ([Environment]::UserInteractive) {
        try {
            Add-Type -AssemblyName System.Windows.Forms
            [System.Windows.Forms.MessageBox]::Show(
                $_.Exception.Message,
                "Lienzo Setup",
                [System.Windows.Forms.MessageBoxButtons]::OK,
                [System.Windows.Forms.MessageBoxIcon]::Error
            ) | Out-Null
        } catch {
        }
    }
    Write-Error $_
    exit 1
}
