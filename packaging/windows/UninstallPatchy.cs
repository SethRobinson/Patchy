using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Forms;
using Microsoft.Win32;

internal static class UninstallPatchy
{
    private const string InstallManifestName = "PatchyInstallManifest.txt";
    private const string UninstallKeyPath = @"Software\Microsoft\Windows\CurrentVersion\Uninstall\Patchy";
    private const string ClassesKeyPath = @"Software\Classes";
    private const string OpenWithKeyPath = ClassesKeyPath + @"\Applications\patchy.exe";
    // Matches $PatchyOpenWithProgId in InstallPatchy.ps1.
    private const string OpenWithProgId = "Patchy.Image";
    private const string ShortcutRelativePath = @"Microsoft\Windows\Start Menu\Programs\Patchy.lnk";
    private const string DesktopShortcutName = "Patchy.lnk";
    // Object Selection's downloaded High Quality model (docs/ai-selection.md) and the
    // watermark models (docs/watermarks.md): Qt's AppDataLocation for organization RTsoft,
    // application Patchy, plus "ai-models". Only these exact files are deleted, never a
    // wildcard; keep the names in sync with high_quality_model_files() in
    // src/ui/object_selection_model_download.cpp and watermark_model_files() in
    // src/ui/watermark_model_download.cpp.
    private const string DownloadedModelsRelativePath = @"RTsoft\Patchy\ai-models";
    private static readonly string[] DownloadedModelFileNames = {
        "efficientvit_sam_xl0_encoder_fp16.onnx",
        "efficientvit_sam_xl0_decoder_multi_fp16.onnx",
        "trustmark_decoder_Q.onnx",
        "trustmark_decoder_P.onnx",
        "trustmark_decoder_C.onnx",
        "trustmark_encoder_Q.onnx",
        "trustmark_LICENSE.txt"
    };

    private static readonly string[] LegacyInstalledRelativePaths = {
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
        "iconengines\\qsvgicon.dll",
        "imageformats\\qjpeg.dll",
        "imageformats\\qsvg.dll",
        "imageformats\\qtiff.dll",
        "imageformats\\qwebp.dll",
        "platforms\\qwindows.dll",
        "styles\\qmodernwindowsstyle.dll",
        "licenses\\qt\\qtbase-6.8.3.spdx",
        "licenses\\qt\\qtimageformats-6.8.3.spdx",
        "licenses\\qt\\qtsvg-6.8.3.spdx"
    };

    [STAThread]
    private static int Main(string[] args)
    {
        Application.EnableVisualStyles();

        bool quiet = HasArgument(args, "/quiet") || HasArgument(args, "-quiet") || HasArgument(args, "/s") || HasArgument(args, "-s");
        string installRoot = AppDomain.CurrentDomain.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);

        try
        {
            if (!quiet)
            {
                DialogResult result = MessageBox.Show(
                    "Remove Patchy from this computer?",
                    "Patchy Setup",
                    MessageBoxButtons.YesNo,
                    MessageBoxIcon.Question,
                    MessageBoxDefaultButton.Button2);
                if (result != DialogResult.Yes)
                {
                    return 1602;
                }
            }

            if (!CloseRunningPatchy(installRoot, quiet))
            {
                return 1602;
            }

            string[] installedFiles = ReadInstalledRelativePaths(installRoot);
            RemoveShortcuts();
            RemoveUninstallEntry();
            RemoveOpenWithEntry(installRoot);
            RemoveDownloadedModels();
            StartHiddenCleanup(installRoot, installedFiles, Process.GetCurrentProcess().Id);
            return 0;
        }
        catch (Exception ex)
        {
            if (!quiet)
            {
                MessageBox.Show(ex.Message, "Patchy Setup", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            return 1;
        }
    }

    private static string[] ReadInstalledRelativePaths(string installRoot)
    {
        string manifestPath = Path.Combine(installRoot, InstallManifestName);
        HashSet<string> paths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        if (File.Exists(manifestPath))
        {
            foreach (string line in File.ReadAllLines(manifestPath))
            {
                string relativePath = line.Trim();
                if (IsSafeRelativePath(relativePath))
                {
                    paths.Add(relativePath);
                }
            }
        }
        else
        {
            foreach (string relativePath in LegacyInstalledRelativePaths)
            {
                paths.Add(relativePath);
            }
        }

        paths.Add(InstallManifestName);
        paths.Add("UninstallPatchy.exe");
        string[] result = new string[paths.Count];
        paths.CopyTo(result);
        Array.Sort(result, StringComparer.OrdinalIgnoreCase);
        return result;
    }

    private static bool IsSafeRelativePath(string relativePath)
    {
        if (string.IsNullOrWhiteSpace(relativePath) || Path.IsPathRooted(relativePath))
        {
            return false;
        }

        string[] parts = relativePath.Split(new[] { '\\', '/' }, StringSplitOptions.RemoveEmptyEntries);
        if (parts.Length == 0)
        {
            return false;
        }

        foreach (string part in parts)
        {
            if (part == "." || part == "..")
            {
                return false;
            }
        }

        return true;
    }

    private static bool CloseRunningPatchy(string installRoot, bool quiet)
    {
        foreach (Process process in Process.GetProcessesByName("patchy"))
        {
            if (!IsProcessFromInstallRoot(process, installRoot))
            {
                continue;
            }

            if (!quiet)
            {
                DialogResult result = MessageBox.Show(
                    "Patchy is currently running. Setup needs to close it before uninstalling.",
                    "Patchy Setup",
                    MessageBoxButtons.OKCancel,
                    MessageBoxIcon.Warning);
                if (result != DialogResult.OK)
                {
                    return false;
                }
            }

            try
            {
                if (!process.HasExited && process.MainWindowHandle != IntPtr.Zero)
                {
                    process.CloseMainWindow();
                    process.WaitForExit(5000);
                }
                if (!process.HasExited)
                {
                    process.Kill();
                    process.WaitForExit(5000);
                }
            }
            catch
            {
                if (!quiet)
                {
                    MessageBox.Show(
                        "Patchy could not be closed. Close it manually and run uninstall again.",
                        "Patchy Setup",
                        MessageBoxButtons.OK,
                        MessageBoxIcon.Error);
                }
                return false;
            }
        }

        return true;
    }

    private static bool IsProcessFromInstallRoot(Process process, string installRoot)
    {
        try
        {
            string fileName = process.MainModule.FileName;
            return fileName.StartsWith(installRoot + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase);
        }
        catch
        {
            return false;
        }
    }

    private static void RemoveShortcuts()
    {
        string appData = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
        string shortcutPath = Path.Combine(appData, ShortcutRelativePath);
        if (File.Exists(shortcutPath))
        {
            File.Delete(shortcutPath);
        }

        string desktopDirectory = Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory);
        if (!string.IsNullOrWhiteSpace(desktopDirectory))
        {
            string desktopShortcutPath = Path.Combine(desktopDirectory, DesktopShortcutName);
            if (File.Exists(desktopShortcutPath))
            {
                File.Delete(desktopShortcutPath);
            }
        }
    }

    // Deletes the downloaded model files by their full names, then the ai-models folder only
    // when nothing else is left in it (Directory.Delete without recursion). The rest of the
    // user's Patchy data (fonts, scripts, settings) stays.
    private static void RemoveDownloadedModels()
    {
        try
        {
            string appData = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
            if (string.IsNullOrWhiteSpace(appData))
            {
                return;
            }
            string modelsDirectory = Path.Combine(appData, DownloadedModelsRelativePath);
            foreach (string fileName in DownloadedModelFileNames)
            {
                string modelPath = Path.Combine(modelsDirectory, fileName);
                if (File.Exists(modelPath))
                {
                    File.Delete(modelPath);
                }
            }
            if (Directory.Exists(modelsDirectory) && Directory.GetFileSystemEntries(modelsDirectory).Length == 0)
            {
                Directory.Delete(modelsDirectory, false);
            }
        }
        catch (Exception)
        {
            // A file Patchy no longer uses must not block the uninstall.
        }
    }

    private static void RemoveUninstallEntry()
    {
        using (RegistryKey currentUser = Registry.CurrentUser)
        {
            currentUser.DeleteSubKeyTree(UninstallKeyPath, false);
        }
    }

    [DllImport("shell32.dll")]
    private static extern void SHChangeNotify(int eventId, uint flags, IntPtr item1, IntPtr item2);

    // Removes the "Open with" registration InstallPatchy.ps1 wrote, but only when its
    // command points into this install: Windows creates the same Applications key by
    // itself when a user browses to some other patchy.exe from "Choose another app".
    private static void RemoveOpenWithEntry(string installRoot)
    {
        try
        {
            using (RegistryKey currentUser = Registry.CurrentUser)
            {
                string command = null;
                using (RegistryKey commandKey = currentUser.OpenSubKey(OpenWithKeyPath + @"\shell\open\command"))
                {
                    if (commandKey != null)
                    {
                        command = commandKey.GetValue(null) as string;
                    }
                }

                string ownCommandPrefix = "\"" + installRoot + Path.DirectorySeparatorChar;
                if (command == null || !command.StartsWith(ownCommandPrefix, StringComparison.OrdinalIgnoreCase))
                {
                    return;
                }

                RemoveOpenWithProgIdValues(currentUser);
                currentUser.DeleteSubKeyTree(OpenWithKeyPath, false);
                currentUser.DeleteSubKeyTree(ClassesKeyPath + @"\" + OpenWithProgId, false);
            }

            // SHCNE_ASSOCCHANGED, SHCNF_IDLIST
            SHChangeNotify(0x08000000, 0, IntPtr.Zero, IntPtr.Zero);
        }
        catch
        {
            // A leftover "Open with" entry must not stop the uninstall.
        }
    }

    // Takes Patchy's ProgID out of each registered extension's OpenWithProgids list. An
    // extension key is deleted only when that leaves it empty, so another program's
    // entries and the user's own choices stay.
    private static void RemoveOpenWithProgIdValues(RegistryKey currentUser)
    {
        string[] extensions;
        using (RegistryKey supportedTypes = currentUser.OpenSubKey(OpenWithKeyPath + @"\SupportedTypes"))
        {
            if (supportedTypes == null)
            {
                return;
            }
            extensions = supportedTypes.GetValueNames();
        }

        foreach (string extension in extensions)
        {
            if (extension.Length < 2 || extension[0] != '.' || extension.IndexOf('\\') >= 0)
            {
                continue;
            }

            string extensionPath = ClassesKeyPath + @"\" + extension;
            string progIdsPath = extensionPath + @"\OpenWithProgids";
            using (RegistryKey progIds = currentUser.OpenSubKey(progIdsPath, true))
            {
                if (progIds == null)
                {
                    continue;
                }
                progIds.DeleteValue(OpenWithProgId, false);
            }

            if (IsEmptyKey(currentUser, progIdsPath))
            {
                currentUser.DeleteSubKey(progIdsPath, false);
                if (IsEmptyKey(currentUser, extensionPath))
                {
                    currentUser.DeleteSubKey(extensionPath, false);
                }
            }
        }
    }

    private static bool IsEmptyKey(RegistryKey currentUser, string path)
    {
        using (RegistryKey key = currentUser.OpenSubKey(path))
        {
            return key != null && key.ValueCount == 0 && key.SubKeyCount == 0;
        }
    }

    private static void StartHiddenCleanup(string installRoot, string[] installedFiles, int parentProcessId)
    {
        string powershell = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.System),
            @"WindowsPowerShell\v1.0\powershell.exe");
        if (!File.Exists(powershell))
        {
            powershell = "powershell.exe";
        }

        string command =
            "$root = " + QuotePowerShellString(installRoot) + "; " +
            "$files = " + BuildPowerShellArray(installedFiles) + "; " +
            "$p = Get-Process -Id " + parentProcessId + " -ErrorAction SilentlyContinue; " +
            "if ($p) { Wait-Process -Id " + parentProcessId + " -ErrorAction SilentlyContinue }; " +
            "Start-Sleep -Milliseconds 300; " +
            "foreach ($relative in $files) { " +
            "  $target = Join-Path $root $relative; " +
            "  if (Test-Path -LiteralPath $target -PathType Leaf) { Remove-Item -LiteralPath $target -Force -ErrorAction SilentlyContinue } " +
            "}; " +
            "if (Test-Path -LiteralPath $root -PathType Container) { " +
            "  Get-ChildItem -LiteralPath $root -Directory -Recurse -Force -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | ForEach-Object { " +
            "    if (-not (Get-ChildItem -LiteralPath $_.FullName -Force -ErrorAction SilentlyContinue)) { Remove-Item -LiteralPath $_.FullName -Force -ErrorAction SilentlyContinue } " +
            "  }; " +
            "  if (-not (Get-ChildItem -LiteralPath $root -Force -ErrorAction SilentlyContinue)) { Remove-Item -LiteralPath $root -Force -ErrorAction SilentlyContinue } " +
            "}";
        string encodedCommand = Convert.ToBase64String(Encoding.Unicode.GetBytes(command));

        ProcessStartInfo startInfo = new ProcessStartInfo();
        startInfo.FileName = powershell;
        startInfo.Arguments = "-NoLogo -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -EncodedCommand " + encodedCommand;
        startInfo.UseShellExecute = false;
        startInfo.CreateNoWindow = true;
        startInfo.WindowStyle = ProcessWindowStyle.Hidden;
        Process.Start(startInfo);
    }

    private static string BuildPowerShellArray(string[] values)
    {
        StringBuilder builder = new StringBuilder("@(");
        for (int index = 0; index < values.Length; ++index)
        {
            if (index > 0)
            {
                builder.Append(",");
            }
            builder.Append(QuotePowerShellString(values[index]));
        }
        builder.Append(")");
        return builder.ToString();
    }

    private static string QuotePowerShellString(string value)
    {
        return "'" + value.Replace("'", "''") + "'";
    }

    private static bool HasArgument(string[] args, string name)
    {
        foreach (string arg in args)
        {
            if (string.Equals(arg, name, StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }
        }
        return false;
    }
}
