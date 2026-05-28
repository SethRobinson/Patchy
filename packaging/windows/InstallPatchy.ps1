[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PayloadZip,

    [string]$Version = "0.0.0",

    [switch]$Quiet
)

$ErrorActionPreference = "Stop"

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
        [string]$UninstallKey,

        [Parameter(Mandatory = $true)]
        [string]$Version
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

        $installedExe = Join-Path $InstallRoot "patchy.exe"
        $installedIcon = Join-Path $InstallRoot "Patchy.ico"
        $uninstallerExe = Join-Path $InstallRoot "UninstallPatchy.exe"
        if (-not (Test-Path -LiteralPath $uninstallerExe -PathType Leaf)) {
            throw "Installer payload does not contain Patchy\UninstallPatchy.exe."
        }

        New-Item -ItemType Directory -Path $StartMenuDirectory -Force | Out-Null
        New-PatchyShortcut -ShortcutPath $StartMenuShortcut -TargetPath $installedExe -WorkingDirectory $InstallRoot -IconPath $installedIcon

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

        return $installedExe
    } finally {
        if (Test-Path -LiteralPath $tempRoot) {
            Remove-Item -LiteralPath $tempRoot -Recurse -Force
        }
    }
}

function New-PatchyLogoBitmap {
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
    $form.Text = "Patchy Setup"
    $form.StartPosition = "CenterScreen"
    $form.FormBorderStyle = "FixedDialog"
    $form.MaximizeBox = $false
    $form.MinimizeBox = $true
    $form.ClientSize = New-Object System.Drawing.Size 560, 340
    $form.Font = New-Object System.Drawing.Font "Segoe UI", 9
    $form.BackColor = [System.Drawing.Color]::White
    $form.Tag = "ready"
    $formIcon = $null
    $installerIconPath = Join-Path (Split-Path -Parent $PayloadZip) "Patchy.ico"
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
    $logo.Image = New-PatchyLogoBitmap 74
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
    $title.Text = "Install Patchy"
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

    $status = New-Object System.Windows.Forms.Label
    $status.Text = ""
    $status.ForeColor = [System.Drawing.Color]::FromArgb(63, 72, 84)
    $status.Size = New-Object System.Drawing.Size 344, 24
    $status.Location = New-Object System.Drawing.Point $contentLeft, 202
    $form.Controls.Add($status)

    $progress = New-Object System.Windows.Forms.ProgressBar
    $progress.Location = New-Object System.Drawing.Point $contentLeft, 230
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
            $state.InstalledExe = Invoke-PatchyInstall `
                -PayloadZip $PayloadZip `
                -InstallParent $InstallParent `
                -InstallRoot $InstallRoot `
                -StartMenuDirectory $StartMenuDirectory `
                -StartMenuShortcut $StartMenuShortcut `
                -UninstallKey $UninstallKey `
                -Version $Version

            $state.Completed = $true
            $form.Tag = "complete"
            $title.Text = "Patchy has been installed"
            $body.Text = "Setup finished installing Patchy on this computer."
            $pathLabel.Visible = $false
            $pathBox.Visible = $false
            $status.Text = ""
            $launchCheck.Visible = $true
            $installButton.Text = "Finish"
            $cancelButton.Visible = $false
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
$uninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Patchy"

try {
    if ($Quiet -or -not [Environment]::UserInteractive) {
        $installedExe = Invoke-PatchyInstall `
            -PayloadZip $PayloadZip `
            -InstallParent $installParent `
            -InstallRoot $installRoot `
            -StartMenuDirectory $startMenuDirectory `
            -StartMenuShortcut $startMenuShortcut `
            -UninstallKey $uninstallKey `
            -Version $Version
        Write-Host "Patchy installed to $installRoot"
        exit 0
    }

    $result = Show-PatchyInstallerWizard `
        -PayloadZip $PayloadZip `
        -InstallParent $installParent `
        -InstallRoot $installRoot `
        -StartMenuDirectory $startMenuDirectory `
        -StartMenuShortcut $startMenuShortcut `
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
