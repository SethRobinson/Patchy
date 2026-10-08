[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PayloadZip,

    [string]$Version = "0.0.0",

    [switch]$Quiet,

    # Packaging check: load the wizard logo from the Patchy.ico beside the payload and exit.
    [switch]$CheckLogo,

    # Packaging check: build the whole wizard, show it invisibly, close it, and exit
    # without installing. Used by scripts\release\verify-windows-package.ps1.
    [switch]$SmokeTest,

    # Packaging check: write the "Open with" registration under this scratch registry key
    # instead of HKCU:\Software\Classes and exit without installing. Used by
    # scripts\release\verify-windows-package.ps1.
    [string]$OpenWithCheckRoot = ""
)

$ErrorActionPreference = "Stop"

function ConvertFrom-PatchyUnicodeEscapes {
    param([Parameter(Mandatory = $true)][string]$Text)

    return [regex]::Replace($Text, "\\u([0-9A-Fa-f]{4})", {
        param($Match)
        [string][char][Convert]::ToInt32($Match.Groups[1].Value, 16)
    })
}

$PatchyInstallerText = @{
    en = @{
        RunningPatchyRetryMessage = "Patchy is currently running. Save your work, close Patchy, then click Retry to continue installation."
        RunningPatchyQuietMessage = "Patchy is currently running. Close Patchy and run setup again."
        FileInUseQuietMessage = "Patchy could not be updated because installed files are in use. Close Patchy and run setup again."
        InstallationCanceled = "Installation canceled."
    }
    ja = @{
        RunningPatchyRetryMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy \u306F\u73FE\u5728\u5B9F\u884C\u4E2D\u3067\u3059\u3002\u4F5C\u696D\u3092\u4FDD\u5B58\u3057\u3066 Patchy \u3092\u9589\u3058\u3066\u304B\u3089\u3001[\u518D\u8A66\u884C] \u3092\u30AF\u30EA\u30C3\u30AF\u3057\u3066\u30A4\u30F3\u30B9\u30C8\u30FC\u30EB\u3092\u7D9A\u884C\u3057\u3066\u304F\u3060\u3055\u3044\u3002"
        RunningPatchyQuietMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy \u306F\u73FE\u5728\u5B9F\u884C\u4E2D\u3067\u3059\u3002Patchy \u3092\u9589\u3058\u3066\u304B\u3089\u30BB\u30C3\u30C8\u30A2\u30C3\u30D7\u3092\u3082\u3046\u4E00\u5EA6\u5B9F\u884C\u3057\u3066\u304F\u3060\u3055\u3044\u3002"
        FileInUseQuietMessage = ConvertFrom-PatchyUnicodeEscapes "\u30A4\u30F3\u30B9\u30C8\u30FC\u30EB\u6E08\u307F\u306E\u30D5\u30A1\u30A4\u30EB\u304C\u4F7F\u7528\u4E2D\u306E\u305F\u3081\u3001Patchy \u3092\u66F4\u65B0\u3067\u304D\u307E\u305B\u3093\u3067\u3057\u305F\u3002Patchy \u3092\u9589\u3058\u3066\u304B\u3089\u30BB\u30C3\u30C8\u30A2\u30C3\u30D7\u3092\u3082\u3046\u4E00\u5EA6\u5B9F\u884C\u3057\u3066\u304F\u3060\u3055\u3044\u3002"
        InstallationCanceled = ConvertFrom-PatchyUnicodeEscapes "\u30A4\u30F3\u30B9\u30C8\u30FC\u30EB\u306F\u30AD\u30E3\u30F3\u30BB\u30EB\u3055\u308C\u307E\u3057\u305F\u3002"
    }
    de = @{
        RunningPatchyRetryMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy wird derzeit ausgef\u00FChrt. Speichern Sie Ihre Arbeit, schlie\u00DFen Sie Patchy und klicken Sie dann auf \u201EWiederholen\u201C, um die Installation fortzusetzen."
        RunningPatchyQuietMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy wird derzeit ausgef\u00FChrt. Schlie\u00DFen Sie Patchy und f\u00FChren Sie das Setup erneut aus."
        FileInUseQuietMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy konnte nicht aktualisiert werden, weil installierte Dateien gerade verwendet werden. Schlie\u00DFen Sie Patchy und f\u00FChren Sie das Setup erneut aus."
        InstallationCanceled = ConvertFrom-PatchyUnicodeEscapes "Installation abgebrochen."
    }
    es = @{
        RunningPatchyRetryMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy se est\u00E1 ejecutando. Guarde su trabajo, cierre Patchy y haga clic en Reintentar para continuar con la instalaci\u00F3n."
        RunningPatchyQuietMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy se est\u00E1 ejecutando. Cierre Patchy y vuelva a ejecutar el instalador."
        FileInUseQuietMessage = ConvertFrom-PatchyUnicodeEscapes "No se pudo actualizar Patchy porque los archivos instalados est\u00E1n en uso. Cierre Patchy y vuelva a ejecutar el instalador."
        InstallationCanceled = ConvertFrom-PatchyUnicodeEscapes "Instalaci\u00F3n cancelada."
    }
    fr = @{
        RunningPatchyRetryMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy est en cours d'ex\u00E9cution. Enregistrez votre travail, fermez Patchy, puis cliquez sur R\u00E9essayer pour poursuivre l'installation."
        RunningPatchyQuietMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy est en cours d'ex\u00E9cution. Fermez Patchy, puis relancez le programme d'installation."
        FileInUseQuietMessage = ConvertFrom-PatchyUnicodeEscapes "Impossible de mettre \u00E0 jour Patchy, car des fichiers install\u00E9s sont en cours d'utilisation. Fermez Patchy, puis relancez le programme d'installation."
        InstallationCanceled = ConvertFrom-PatchyUnicodeEscapes "Installation annul\u00E9e."
    }
    it = @{
        RunningPatchyRetryMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy \u00E8 in esecuzione. Salva il lavoro, chiudi Patchy e fai clic su Riprova per continuare l'installazione."
        RunningPatchyQuietMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy \u00E8 in esecuzione. Chiudi Patchy ed esegui di nuovo il programma di installazione."
        FileInUseQuietMessage = ConvertFrom-PatchyUnicodeEscapes "Impossibile aggiornare Patchy perch\u00E9 i file installati sono in uso. Chiudi Patchy ed esegui di nuovo il programma di installazione."
        InstallationCanceled = ConvertFrom-PatchyUnicodeEscapes "Installazione annullata."
    }
    ko = @{
        RunningPatchyRetryMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy\uAC00 \uC2E4\uD589 \uC911\uC785\uB2C8\uB2E4. \uC791\uC5C5\uC744 \uC800\uC7A5\uD558\uACE0 Patchy\uB97C \uB2EB\uC740 \uD6C4 \uB2E4\uC2DC \uC2DC\uB3C4\uB97C \uD074\uB9AD\uD558\uC5EC \uC124\uCE58\uB97C \uACC4\uC18D\uD558\uC138\uC694."
        RunningPatchyQuietMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy\uAC00 \uC2E4\uD589 \uC911\uC785\uB2C8\uB2E4. Patchy\uB97C \uB2EB\uACE0 \uC124\uCE58 \uD504\uB85C\uADF8\uB7A8\uC744 \uB2E4\uC2DC \uC2E4\uD589\uD558\uC138\uC694."
        FileInUseQuietMessage = ConvertFrom-PatchyUnicodeEscapes "\uC124\uCE58\uB41C \uD30C\uC77C\uC774 \uC0AC\uC6A9 \uC911\uC774\uC5B4\uC11C Patchy\uB97C \uC5C5\uB370\uC774\uD2B8\uD560 \uC218 \uC5C6\uC2B5\uB2C8\uB2E4. Patchy\uB97C \uB2EB\uACE0 \uC124\uCE58 \uD504\uB85C\uADF8\uB7A8\uC744 \uB2E4\uC2DC \uC2E4\uD589\uD558\uC138\uC694."
        InstallationCanceled = ConvertFrom-PatchyUnicodeEscapes "\uC124\uCE58\uAC00 \uCDE8\uC18C\uB418\uC5C8\uC2B5\uB2C8\uB2E4."
    }
    pl = @{
        RunningPatchyRetryMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy jest uruchomiony. Zapisz swoj\u0105 prac\u0119, zamknij Patchy, a nast\u0119pnie kliknij Pon\u00F3w pr\u00F3b\u0119, aby kontynuowa\u0107 instalacj\u0119."
        RunningPatchyQuietMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy jest uruchomiony. Zamknij Patchy i ponownie uruchom instalator."
        FileInUseQuietMessage = ConvertFrom-PatchyUnicodeEscapes "Nie mo\u017Cna zaktualizowa\u0107 Patchy, poniewa\u017C zainstalowane pliki s\u0105 u\u017Cywane. Zamknij Patchy i ponownie uruchom instalator."
        InstallationCanceled = ConvertFrom-PatchyUnicodeEscapes "Instalacja zosta\u0142a anulowana."
    }
    pt_BR = @{
        RunningPatchyRetryMessage = ConvertFrom-PatchyUnicodeEscapes "O Patchy est\u00E1 em execu\u00E7\u00E3o. Salve seu trabalho, feche o Patchy e clique em Repetir para continuar a instala\u00E7\u00E3o."
        RunningPatchyQuietMessage = ConvertFrom-PatchyUnicodeEscapes "O Patchy est\u00E1 em execu\u00E7\u00E3o. Feche o Patchy e execute o instalador novamente."
        FileInUseQuietMessage = ConvertFrom-PatchyUnicodeEscapes "N\u00E3o foi poss\u00EDvel atualizar o Patchy porque os arquivos instalados est\u00E3o em uso. Feche o Patchy e execute o instalador novamente."
        InstallationCanceled = ConvertFrom-PatchyUnicodeEscapes "Instala\u00E7\u00E3o cancelada."
    }
    ru = @{
        RunningPatchyRetryMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy \u0443\u0436\u0435 \u0437\u0430\u043F\u0443\u0449\u0435\u043D. \u0421\u043E\u0445\u0440\u0430\u043D\u0438\u0442\u0435 \u0440\u0430\u0431\u043E\u0442\u0443, \u0437\u0430\u043A\u0440\u043E\u0439\u0442\u0435 Patchy \u0438 \u043D\u0430\u0436\u043C\u0438\u0442\u0435 \u00AB\u041F\u043E\u0432\u0442\u043E\u0440\u0438\u0442\u044C\u00BB, \u0447\u0442\u043E\u0431\u044B \u043F\u0440\u043E\u0434\u043E\u043B\u0436\u0438\u0442\u044C \u0443\u0441\u0442\u0430\u043D\u043E\u0432\u043A\u0443."
        RunningPatchyQuietMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy \u0443\u0436\u0435 \u0437\u0430\u043F\u0443\u0449\u0435\u043D. \u0417\u0430\u043A\u0440\u043E\u0439\u0442\u0435 Patchy \u0438 \u0437\u0430\u043F\u0443\u0441\u0442\u0438\u0442\u0435 \u0443\u0441\u0442\u0430\u043D\u043E\u0432\u043A\u0443 \u0441\u043D\u043E\u0432\u0430."
        FileInUseQuietMessage = ConvertFrom-PatchyUnicodeEscapes "\u041D\u0435 \u0443\u0434\u0430\u043B\u043E\u0441\u044C \u043E\u0431\u043D\u043E\u0432\u0438\u0442\u044C Patchy, \u043F\u043E\u0441\u043A\u043E\u043B\u044C\u043A\u0443 \u0443\u0441\u0442\u0430\u043D\u043E\u0432\u043B\u0435\u043D\u043D\u044B\u0435 \u0444\u0430\u0439\u043B\u044B \u0438\u0441\u043F\u043E\u043B\u044C\u0437\u0443\u044E\u0442\u0441\u044F. \u0417\u0430\u043A\u0440\u043E\u0439\u0442\u0435 Patchy \u0438 \u0437\u0430\u043F\u0443\u0441\u0442\u0438\u0442\u0435 \u0443\u0441\u0442\u0430\u043D\u043E\u0432\u043A\u0443 \u0441\u043D\u043E\u0432\u0430."
        InstallationCanceled = ConvertFrom-PatchyUnicodeEscapes "\u0423\u0441\u0442\u0430\u043D\u043E\u0432\u043A\u0430 \u043E\u0442\u043C\u0435\u043D\u0435\u043D\u0430."
    }
    zh_CN = @{
        RunningPatchyRetryMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy \u6B63\u5728\u8FD0\u884C\u3002\u8BF7\u4FDD\u5B58\u60A8\u7684\u5DE5\u4F5C\uFF0C\u5173\u95ED Patchy\uFF0C\u7136\u540E\u5355\u51FB\u201C\u91CD\u8BD5\u201D\u7EE7\u7EED\u5B89\u88C5\u3002"
        RunningPatchyQuietMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy \u6B63\u5728\u8FD0\u884C\u3002\u8BF7\u5173\u95ED Patchy\uFF0C\u7136\u540E\u91CD\u65B0\u8FD0\u884C\u5B89\u88C5\u7A0B\u5E8F\u3002"
        FileInUseQuietMessage = ConvertFrom-PatchyUnicodeEscapes "\u65E0\u6CD5\u66F4\u65B0 Patchy\uFF0C\u56E0\u4E3A\u5DF2\u5B89\u88C5\u7684\u6587\u4EF6\u6B63\u5728\u4F7F\u7528\u4E2D\u3002\u8BF7\u5173\u95ED Patchy\uFF0C\u7136\u540E\u91CD\u65B0\u8FD0\u884C\u5B89\u88C5\u7A0B\u5E8F\u3002"
        InstallationCanceled = ConvertFrom-PatchyUnicodeEscapes "\u5B89\u88C5\u5DF2\u53D6\u6D88\u3002"
    }
    zh_TW = @{
        RunningPatchyRetryMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy \u6B63\u5728\u57F7\u884C\u4E2D\u3002\u8ACB\u5132\u5B58\u60A8\u7684\u5DE5\u4F5C\uFF0C\u95DC\u9589 Patchy\uFF0C\u7136\u5F8C\u6309\u4E00\u4E0B\u300C\u91CD\u8A66\u300D\u4EE5\u7E7C\u7E8C\u5B89\u88DD\u3002"
        RunningPatchyQuietMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy \u6B63\u5728\u57F7\u884C\u4E2D\u3002\u8ACB\u95DC\u9589 Patchy\uFF0C\u7136\u5F8C\u91CD\u65B0\u57F7\u884C\u5B89\u88DD\u7A0B\u5F0F\u3002"
        FileInUseQuietMessage = ConvertFrom-PatchyUnicodeEscapes "\u7121\u6CD5\u66F4\u65B0 Patchy\uFF0C\u56E0\u70BA\u5DF2\u5B89\u88DD\u7684\u6A94\u6848\u6B63\u5728\u4F7F\u7528\u4E2D\u3002\u8ACB\u95DC\u9589 Patchy\uFF0C\u7136\u5F8C\u91CD\u65B0\u57F7\u884C\u5B89\u88DD\u7A0B\u5F0F\u3002"
        InstallationCanceled = ConvertFrom-PatchyUnicodeEscapes "\u5B89\u88DD\u5DF2\u53D6\u6D88\u3002"
    }
}

function Get-PatchyInstallerLanguage {
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

    if ($language -eq "pt") {
        return "pt_BR"
    }
    if (@("de", "es", "fr", "it", "ja", "ko", "pl", "ru") -contains $language) {
        return $language
    }
    return "en"
}

function Get-PatchyInstallerText {
    param([Parameter(Mandatory = $true)][string]$Key)

    $language = Get-PatchyInstallerLanguage
    if ($PatchyInstallerText.ContainsKey($language) -and $PatchyInstallerText[$language].ContainsKey($Key)) {
        return $PatchyInstallerText[$language][$Key]
    }
    return $PatchyInstallerText["en"][$Key]
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

function Get-RunningInstalledPatchyProcess {
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

function Request-ClosePatchyRetry {
    param([object]$Owner = $null)

    Add-Type -AssemblyName System.Windows.Forms
    $message = Get-PatchyInstallerText "RunningPatchyRetryMessage"
    if ($Owner -ne $null) {
        $result = [System.Windows.Forms.MessageBox]::Show(
            $Owner,
            $message,
            "Patchy Setup",
            [System.Windows.Forms.MessageBoxButtons]::RetryCancel,
            [System.Windows.Forms.MessageBoxIcon]::Warning,
            [System.Windows.Forms.MessageBoxDefaultButton]::Button1
        )
    } else {
        $result = [System.Windows.Forms.MessageBox]::Show(
            $message,
            "Patchy Setup",
            [System.Windows.Forms.MessageBoxButtons]::RetryCancel,
            [System.Windows.Forms.MessageBoxIcon]::Warning,
            [System.Windows.Forms.MessageBoxDefaultButton]::Button1
        )
    }
    return $result -eq [System.Windows.Forms.DialogResult]::Retry
}

function Confirm-PatchyClosedForInstall {
    param(
        [Parameter(Mandatory = $true)]
        [string]$InstallRoot,

        [bool]$Quiet = $false,

        [object]$Owner = $null
    )

    while (@(Get-RunningInstalledPatchyProcess -InstallRoot $InstallRoot).Count -gt 0) {
        if ($Quiet -or -not [Environment]::UserInteractive) {
            throw (Get-PatchyInstallerText "RunningPatchyQuietMessage")
        }
        if (-not (Request-ClosePatchyRetry -Owner $Owner)) {
            throw (New-Object System.OperationCanceledException (Get-PatchyInstallerText "InstallationCanceled"))
        }
    }
}

function Test-PatchyFileInUseInstallError {
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

function New-PatchyShortcut {
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
    $shortcut.Description = "Patchy"
    $shortcut.Save()
}

$ManifestFileName = "PatchyInstallManifest.txt"
$LegacyInstalledRelativePaths = @(
    "patchy.exe",
    "Patchy.ico",
    "UninstallPatchy.exe",
    "UninstallPatchy.ps1",
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

function Remove-PatchyInstalledFiles {
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

function Add-PatchyInstalledRelativePath {
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

# Extensions Patchy is offered for in Explorer's "Open with" list. Keep in step with
# file_format_entries() in src/ui/main_window_files.cpp and the camera raw list in
# src/formats/raw_document_io.cpp. PDF is left out on purpose.
$PatchyOpenWithExtensions = @(
    "psd", "psb", "png", "jpg", "jpeg", "bmp", "tif", "tiff", "webp", "gif",
    "aseprite", "ase", "tga", "ico", "cur", "pcx", "lbm", "iff", "bbm", "svg", "svgz",
    "heic", "heif", "hif", "jxr", "wdp", "hdp", "rttex",
    "af", "afphoto", "afdesign", "afpub",
    "dng", "cr2", "cr3", "crw", "nef", "nrw", "arw", "sr2", "srf", "orf", "raf", "rw2",
    "pef", "srw", "mrw", "3fr", "fff", "iiq", "erf", "kdc", "dcr", "mos", "rwl", "x3f"
)

# The ProgID named in each extension's OpenWithProgids list. A persisted identifier:
# UninstallPatchy.cs and installed machines know it by this name.
$PatchyOpenWithProgId = "Patchy.Image"

# Removes what Register-PatchyOpenWith wrote. The extensions come from the registered
# SupportedTypes list, so an upgrade also clears types a newer list no longer has. An
# extension key is deleted only when removing Patchy's value leaves it empty.
function Unregister-PatchyOpenWith {
    param(
        [string]$ClassesRoot = "HKCU:\Software\Classes"
    )

    $applicationKey = Join-Path $ClassesRoot "Applications\patchy.exe"
    $supportedTypesKey = Join-Path $applicationKey "SupportedTypes"
    if (Test-Path -LiteralPath $supportedTypesKey) {
        foreach ($extension in @((Get-Item -LiteralPath $supportedTypesKey).GetValueNames())) {
            if ($extension -notmatch '^\.[A-Za-z0-9]+$') { continue }
            $extensionKey = Join-Path $ClassesRoot $extension
            $progIdsKey = Join-Path $extensionKey "OpenWithProgids"
            if (-not (Test-Path -LiteralPath $progIdsKey)) { continue }
            Remove-ItemProperty -LiteralPath $progIdsKey -Name $PatchyOpenWithProgId -ErrorAction SilentlyContinue
            foreach ($emptyCandidate in @($progIdsKey, $extensionKey)) {
                $key = Get-Item -LiteralPath $emptyCandidate
                if ($key.ValueCount -eq 0 -and $key.SubKeyCount -eq 0) {
                    Remove-Item -LiteralPath $emptyCandidate -Force
                } else {
                    break
                }
            }
        }
    }

    foreach ($ownedKey in @($applicationKey, (Join-Path $ClassesRoot $PatchyOpenWithProgId))) {
        if (Test-Path -LiteralPath $ownedKey) {
            Remove-Item -LiteralPath $ownedKey -Recurse -Force
        }
    }
}

# Lists Patchy as a choice under "Open with" for the types above, without making it the
# default for any of them. Two parts: the Applications\<exe> key names the app, and a
# Patchy ProgID added to each extension's OpenWithProgids list is what puts it in the
# "Open with" submenu (the Applications key alone only reaches "Choose another app").
# An extension's own default value and the user's choice are never written.
# UninstallPatchy.exe removes all of it.
function Register-PatchyOpenWith {
    param(
        [Parameter(Mandatory = $true)]
        [string]$InstalledExe,

        [string]$ClassesRoot = "HKCU:\Software\Classes"
    )

    Unregister-PatchyOpenWith -ClassesRoot $ClassesRoot

    $command = "`"$InstalledExe`" `"%1`""
    $applicationKey = Join-Path $ClassesRoot "Applications\patchy.exe"
    $supportedTypesKey = Join-Path $applicationKey "SupportedTypes"
    $progIdKey = Join-Path $ClassesRoot $PatchyOpenWithProgId
    foreach ($commandOwner in @($applicationKey, $progIdKey)) {
        $commandKey = Join-Path $commandOwner "shell\open\command"
        New-Item -Path $commandKey -Force | Out-Null
        Set-ItemProperty -Path $commandKey -Name "(default)" -Value $command
    }
    New-Item -Path $supportedTypesKey -Force | Out-Null
    New-ItemProperty -Path $applicationKey -Name "FriendlyAppName" -Value "Patchy Image Editor" -PropertyType String -Force | Out-Null
    foreach ($extension in $PatchyOpenWithExtensions) {
        New-ItemProperty -Path $supportedTypesKey -Name ".$extension" -Value "" -PropertyType String -Force | Out-Null
        $progIdsKey = Join-Path $ClassesRoot ".$extension\OpenWithProgids"
        if (-not (Test-Path -LiteralPath $progIdsKey)) {
            New-Item -Path $progIdsKey -Force | Out-Null
        }
        New-ItemProperty -Path $progIdsKey -Name $PatchyOpenWithProgId -Value "" -PropertyType String -Force | Out-Null
    }
}

# Tells Explorer the registration changed, so the entry appears without a sign-out.
function Send-PatchyAssociationChanged {
    Add-Type -Namespace PatchySetup -Name Shell -MemberDefinition @"
[DllImport("shell32.dll")]
public static extern void SHChangeNotify(int eventId, uint flags, IntPtr item1, IntPtr item2);
"@
    # SHCNE_ASSOCCHANGED, SHCNF_IDLIST
    [PatchySetup.Shell]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)
}

function Invoke-PatchyInstall {
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

    $tempRoot = Join-Path ([IO.Path]::GetTempPath()) ("PatchyInstall-" + [guid]::NewGuid().ToString("N"))

    try {
        if (-not (Test-Path -LiteralPath $PayloadZip -PathType Leaf)) {
            throw "Installer payload was not found: $PayloadZip"
        }

        if (-not (Test-PathInsideRoot -Path $InstallRoot -Root $InstallParent)) {
            throw "Refusing to install outside the per-user Programs directory: $InstallRoot"
        }

        New-Item -ItemType Directory -Path $InstallParent -Force | Out-Null
        Confirm-PatchyClosedForInstall -InstallRoot $InstallRoot -Quiet $Quiet -Owner $Owner
        New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null

        Expand-Archive -LiteralPath $PayloadZip -DestinationPath $tempRoot -Force
        $sourceRoot = Join-Path $tempRoot "Patchy"
        $sourceExe = Join-Path $sourceRoot "patchy.exe"
        if (-not (Test-Path -LiteralPath $sourceExe -PathType Leaf)) {
            throw "Installer payload does not contain Patchy\patchy.exe."
        }

        Remove-PatchyInstalledFiles -InstallRoot $InstallRoot
        New-Item -ItemType Directory -Path $InstallRoot -Force | Out-Null
        Get-ChildItem -LiteralPath $sourceRoot -Force | ForEach-Object {
            Copy-Item -LiteralPath $_.FullName -Destination $InstallRoot -Recurse -Force
        }

        $payloadDirectory = Split-Path -Parent $PayloadZip
        $uninstallerSource = Join-Path $payloadDirectory "UninstallPatchy.exe"
        $installedExe = Join-Path $InstallRoot "patchy.exe"
        $installedIcon = Join-Path $InstallRoot "Patchy.ico"
        $uninstallerExe = Join-Path $InstallRoot "UninstallPatchy.exe"
        if (Test-Path -LiteralPath $uninstallerSource -PathType Leaf) {
            Copy-Item -LiteralPath $uninstallerSource -Destination $uninstallerExe -Force
        }
        if (-not (Test-Path -LiteralPath $uninstallerExe -PathType Leaf)) {
            throw "Installer payload does not contain Patchy\UninstallPatchy.exe."
        }
        Add-PatchyInstalledRelativePath -InstallRoot $InstallRoot -RelativePath "UninstallPatchy.exe"

        New-Item -ItemType Directory -Path $StartMenuDirectory -Force | Out-Null
        New-PatchyShortcut -ShortcutPath $StartMenuShortcut -TargetPath $installedExe -WorkingDirectory $InstallRoot -IconPath $installedIcon
        if ($CreateDesktopShortcut) {
            try {
                $desktopDirectory = Split-Path -Parent $DesktopShortcut
                if (-not [string]::IsNullOrWhiteSpace($desktopDirectory)) {
                    New-Item -ItemType Directory -Path $desktopDirectory -Force | Out-Null
                }
                New-PatchyShortcut -ShortcutPath $DesktopShortcut -TargetPath $installedExe -WorkingDirectory $InstallRoot -IconPath $installedIcon
            } catch {
                Write-Warning "Could not create the desktop shortcut: $($_.Exception.Message)"
            }
        }

        $estimatedSizeKb = [int][math]::Ceiling(
            ((Get-ChildItem -LiteralPath $InstallRoot -Recurse -File | Measure-Object -Property Length -Sum).Sum) / 1KB
        )

        New-Item -Path $UninstallKey -Force | Out-Null
        New-ItemProperty -Path $UninstallKey -Name "DisplayName" -Value "Patchy" -PropertyType String -Force | Out-Null
        New-ItemProperty -Path $UninstallKey -Name "DisplayVersion" -Value $Version -PropertyType String -Force | Out-Null
        New-ItemProperty -Path $UninstallKey -Name "Publisher" -Value "Seth A. Robinson" -PropertyType String -Force | Out-Null
        New-ItemProperty -Path $UninstallKey -Name "DisplayIcon" -Value $installedIcon -PropertyType String -Force | Out-Null
        New-ItemProperty -Path $UninstallKey -Name "InstallLocation" -Value $InstallRoot -PropertyType String -Force | Out-Null
        New-ItemProperty -Path $UninstallKey -Name "UninstallString" -Value "`"$uninstallerExe`"" -PropertyType String -Force | Out-Null
        New-ItemProperty -Path $UninstallKey -Name "QuietUninstallString" -Value "`"$uninstallerExe`" /quiet" -PropertyType String -Force | Out-Null
        New-ItemProperty -Path $UninstallKey -Name "NoModify" -Value 1 -PropertyType DWord -Force | Out-Null
        New-ItemProperty -Path $UninstallKey -Name "NoRepair" -Value 1 -PropertyType DWord -Force | Out-Null
        New-ItemProperty -Path $UninstallKey -Name "EstimatedSize" -Value $estimatedSizeKb -PropertyType DWord -Force | Out-Null

        try {
            Register-PatchyOpenWith -InstalledExe $installedExe
            Send-PatchyAssociationChanged
        } catch {
            Write-Warning "Could not add Patchy to the Open with list: $($_.Exception.Message)"
        }

        return $installedExe
    } finally {
        if (Test-Path -LiteralPath $tempRoot) {
            Remove-Item -LiteralPath $tempRoot -Recurse -Force
        }
    }
}

function Invoke-PatchyInstallWithRetry {
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
            return Invoke-PatchyInstall `
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
            if (-not (Test-PatchyFileInUseInstallError -ErrorRecord $_)) {
                throw
            }
            if ($Quiet -or -not [Environment]::UserInteractive) {
                throw (Get-PatchyInstallerText "FileInUseQuietMessage")
            }
            if (-not (Request-ClosePatchyRetry -Owner $Owner)) {
                throw (New-Object System.OperationCanceledException (Get-PatchyInstallerText "InstallationCanceled"))
            }
        }
    }
}

function New-PatchyLogoBitmap {
    param([string]$IconPath, [int]$Size = 64)

    # Use the same authored artwork as the executable and installed shortcuts.
    # Decode the largest ICO frame directly before scaling it to the wizard's slot:
    # Icon.ToBitmap() cannot read PNG-compressed frames in Windows PowerShell 5.1 and
    # throws "Requested range extends past the end of the array" (issue 55).
    $bytes = [System.IO.File]::ReadAllBytes($IconPath)
    $count = [System.BitConverter]::ToUInt16($bytes, 4)
    $best = -1
    $bestWidth = 0
    for ($i = 0; $i -lt $count; $i++) {
        $width = [int]$bytes[6 + 16 * $i]
        if ($width -eq 0) { $width = 256 }
        if ($width -gt $bestWidth) { $bestWidth = $width; $best = $i }
    }
    if ($best -lt 0) { throw "No frames in $IconPath" }
    $length = [System.BitConverter]::ToInt32($bytes, 6 + 16 * $best + 8)
    $offset = [System.BitConverter]::ToInt32($bytes, 6 + 16 * $best + 12)
    $icon = $null
    $stream = $null
    if ($bytes[$offset] -eq 0x89 -and $bytes[$offset + 1] -eq 0x50 -and $bytes[$offset + 2] -eq 0x4E -and $bytes[$offset + 3] -eq 0x47) {
        $stream = New-Object System.IO.MemoryStream (, [byte[]]$bytes[$offset..($offset + $length - 1)])
        $source = [System.Drawing.Image]::FromStream($stream)
    }
    else {
        $icon = New-Object System.Drawing.Icon $IconPath, $bestWidth, $bestWidth
        $source = $icon.ToBitmap()
    }
    $bitmap = New-Object System.Drawing.Bitmap $Size, $Size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.DrawImage($source, 0, 0, $Size, $Size)
    }
    finally {
        $graphics.Dispose()
        $source.Dispose()
        if ($stream) { $stream.Dispose() }
        if ($icon) { $icon.Dispose() }
    }
    return $bitmap
}

function Show-PatchyInstallerWizard {
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
        [string]$Version,

        [switch]$SmokeTest
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
    $form.Text = "Patchy Setup"
    $form.StartPosition = "CenterScreen"
    $form.FormBorderStyle = "FixedDialog"
    $form.MaximizeBox = $false
    $form.MinimizeBox = $true
    $form.ClientSize = New-Object System.Drawing.Size 560, 380
    $form.Font = New-Object System.Drawing.Font "Segoe UI", 9
    $form.BackColor = [System.Drawing.Color]::White
    $form.Tag = "ready"
    $formIcon = $null
    $installerIconPath = Join-Path (Split-Path -Parent $PayloadZip) "Patchy.ico"
    if (Test-Path -LiteralPath $installerIconPath -PathType Leaf) {
        # Artwork is decoration: a logo that fails to load must never stop setup.
        try {
            $formIcon = New-Object System.Drawing.Icon $installerIconPath
            $form.Icon = $formIcon
        }
        catch {
            if ($SmokeTest) { throw }
            $formIcon = $null
        }
    }

    $leftPanel = New-Object System.Windows.Forms.Panel
    $leftPanel.BackColor = [System.Drawing.Color]::FromArgb(23, 30, 40)
    $leftPanel.Dock = [System.Windows.Forms.DockStyle]::Left
    $leftPanel.Width = 148
    $form.Controls.Add($leftPanel)

    $logo = New-Object System.Windows.Forms.PictureBox
    $logo.Size = New-Object System.Drawing.Size 74, 74
    $logo.Location = New-Object System.Drawing.Point 37, 42
    if (Test-Path -LiteralPath $installerIconPath -PathType Leaf) {
        try {
            $logo.Image = New-PatchyLogoBitmap -IconPath $installerIconPath -Size 74
        }
        catch {
            if ($SmokeTest) { throw }
            $logo.Image = $null
        }
    }
    $logo.SizeMode = [System.Windows.Forms.PictureBoxSizeMode]::CenterImage
    $leftPanel.Controls.Add($logo)

    $brand = New-Object System.Windows.Forms.Label
    $brand.Text = "Patchy"
    $brand.ForeColor = [System.Drawing.Color]::White
    $brand.BackColor = [System.Drawing.Color]::Transparent
    $brand.Font = New-Object System.Drawing.Font "Segoe UI Semibold", 18
    $brand.AutoSize = $true
    $brand.Location = New-Object System.Drawing.Point 36, 130
    $leftPanel.Controls.Add($brand)

    $contentLeft = 176
    $title = New-Object System.Windows.Forms.Label
    $title.Text = "Install Patchy $Version"
    $title.Font = New-Object System.Drawing.Font "Segoe UI Semibold", 15
    $title.ForeColor = [System.Drawing.Color]::FromArgb(23, 30, 40)
    $title.AutoSize = $true
    $title.Location = New-Object System.Drawing.Point $contentLeft, 34
    $form.Controls.Add($title)

    $body = New-Object System.Windows.Forms.Label
    $body.Text = "Setup will install Patchy for the current Windows user and add a Start Menu shortcut."
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
    $legalNotice.Text = "Patchy is provided under the MIT License as-is, without warranty. Keep backups of important files."
    $legalNotice.ForeColor = [System.Drawing.Color]::FromArgb(83, 92, 104)
    $legalNotice.Size = New-Object System.Drawing.Size 344, 40
    $legalNotice.Location = New-Object System.Drawing.Point $contentLeft, 218
    $form.Controls.Add($legalNotice)

    $status = New-Object System.Windows.Forms.Label
    $status.Text = ""
    $status.ForeColor = [System.Drawing.Color]::FromArgb(63, 72, 84)
    $status.Size = New-Object System.Drawing.Size 344, 24
    # Keep separate rows for the legal notice, status, and progress bar.
    $status.Location = New-Object System.Drawing.Point $contentLeft, ($legalNotice.Bottom + 8)
    $form.Controls.Add($status)

    $progress = New-Object System.Windows.Forms.ProgressBar
    $progress.Location = New-Object System.Drawing.Point $contentLeft, ($status.Bottom + 6)
    $progress.Size = New-Object System.Drawing.Size 344, 18
    $progress.Style = [System.Windows.Forms.ProgressBarStyle]::Marquee
    $progress.MarqueeAnimationSpeed = 30
    $progress.Visible = $false
    $form.Controls.Add($progress)

    $launchCheck = New-Object System.Windows.Forms.CheckBox
    $launchCheck.Text = "Launch Patchy now"
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
        $status.Text = "Installing Patchy..."
        $form.UseWaitCursor = $true
        $form.Refresh()
        [System.Windows.Forms.Application]::DoEvents()

        try {
            $state.InstalledExe = Invoke-PatchyInstallWithRetry `
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
            $title.Text = "Patchy has been installed"
            $body.Text = "Setup finished installing Patchy on this computer."
            $pathLabel.Visible = $false
            $pathBox.Visible = $false
            $desktopShortcutCheck.Visible = $false
            $status.Text = ""
            $launchCheck.Visible = $true
            $installButton.Text = "Finish"
            $cancelButton.Visible = $false
        } catch [System.OperationCanceledException] {
            $status.Text = Get-PatchyInstallerText "InstallationCanceled"
            $cancelButton.Enabled = $true
        } catch {
            $status.Text = "Installation failed."
            $cancelButton.Enabled = $true
            [System.Windows.Forms.MessageBox]::Show(
                $form,
                $_.Exception.Message,
                "Patchy Setup",
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
    if ($SmokeTest) {
        # Same form and controls as a real run, invisible, closed as soon as it is shown.
        $form.Opacity = 0
        $form.ShowInTaskbar = $false
        $form.Add_Shown({
            $form.DialogResult = [System.Windows.Forms.DialogResult]::Cancel
            $form.Close()
        })
    }
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
$installRoot = Join-Path $installParent "Patchy"
$startMenuDirectory = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs"
$startMenuShortcut = Join-Path $startMenuDirectory "Patchy.lnk"
$desktopShortcut = Join-Path ([System.Environment]::GetFolderPath([System.Environment+SpecialFolder]::DesktopDirectory)) "Patchy.lnk"
$uninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Patchy"

if ($CheckLogo) {
    # build-release.bat runs this in Windows PowerShell 5.1, the host the installer uses.
    Add-Type -AssemblyName System.Drawing
    $checkIcon = Join-Path (Split-Path -Parent $PayloadZip) "Patchy.ico"
    $logoBitmap = New-PatchyLogoBitmap -IconPath $checkIcon -Size 74
    $windowIcon = New-Object System.Drawing.Icon $checkIcon
    Write-Host "Installer logo check passed ($($logoBitmap.Width) x $($logoBitmap.Height))."
    exit 0
}

if ($OpenWithCheckRoot) {
    # Only a scratch key: the real Classes tree is never a valid target for the check.
    if ($OpenWithCheckRoot -notlike "HKCU:\Software\PatchyVerify-*") {
        Write-Host "Open with check needs a HKCU:\Software\PatchyVerify-* scratch key."
        exit 1
    }
    try {
        Register-PatchyOpenWith -InstalledExe (Join-Path $installRoot "patchy.exe") -ClassesRoot $OpenWithCheckRoot
        Write-Host "Open with registration check wrote $OpenWithCheckRoot."
        exit 0
    } catch {
        Write-Host "Open with registration check FAILED: $($_.Exception.Message)"
        exit 1
    }
}

if ($SmokeTest) {
    # Handled before the quiet branch below so a smoke test can never install, and
    # without the message box the real error path shows (it would wait for a click).
    if (-not [Environment]::UserInteractive) {
        Write-Host "Installer wizard smoke test needs an interactive desktop session."
        exit 3
    }
    try {
        $smoke = Show-PatchyInstallerWizard `
            -PayloadZip $PayloadZip `
            -InstallParent $installParent `
            -InstallRoot $installRoot `
            -StartMenuDirectory $startMenuDirectory `
            -StartMenuShortcut $startMenuShortcut `
            -DesktopShortcut $desktopShortcut `
            -UninstallKey $uninstallKey `
            -Version $Version `
            -SmokeTest
        if ($smoke.Completed) { throw "The smoke test must not install." }
        Write-Host "Installer wizard smoke test passed."
        exit 0
    } catch {
        Write-Host "Installer wizard smoke test FAILED: $($_.Exception.Message)"
        exit 1
    }
}

try {
    if ($Quiet -or -not [Environment]::UserInteractive) {
        $installedExe = Invoke-PatchyInstallWithRetry `
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
        Write-Host "Patchy installed to $installRoot"
        exit 0
    }

    $result = Show-PatchyInstallerWizard `
        -PayloadZip $PayloadZip `
        -InstallParent $installParent `
        -InstallRoot $installRoot `
        -StartMenuDirectory $startMenuDirectory `
        -StartMenuShortcut $startMenuShortcut `
        -DesktopShortcut $desktopShortcut `
        -UninstallKey $uninstallKey `
        -Version $Version

    if ($result.Completed) {
        Write-Host "Patchy installed to $installRoot"
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
                "Patchy Setup",
                [System.Windows.Forms.MessageBoxButtons]::OK,
                [System.Windows.Forms.MessageBoxIcon]::Error
            ) | Out-Null
        } catch {
        }
    }
    Write-Error $_
    exit 1
}
