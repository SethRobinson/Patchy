using System;
using System.Diagnostics;
using System.IO;
using System.Windows.Forms;

internal static class InstallPatchyLauncher
{
    [STAThread]
    private static int Main()
    {
        Application.EnableVisualStyles();

        string payloadDirectory = AppDomain.CurrentDomain.BaseDirectory;
        string scriptPath = Path.Combine(payloadDirectory, "InstallPatchy.ps1");
        string payloadZip = Path.Combine(payloadDirectory, "PatchyWindows.zip");
        string versionPath = Path.Combine(payloadDirectory, "PatchyVersion.txt");
        string version = File.Exists(versionPath) ? File.ReadAllText(versionPath).Trim() : "0.0.0";

        if (!File.Exists(scriptPath) || !File.Exists(payloadZip))
        {
            MessageBox.Show("The Patchy installer payload is incomplete.", "Patchy Setup",
                MessageBoxButtons.OK, MessageBoxIcon.Error);
            return 1;
        }

        string powershell = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.System),
            @"WindowsPowerShell\v1.0\powershell.exe");
        if (!File.Exists(powershell))
        {
            powershell = "powershell.exe";
        }

        ProcessStartInfo startInfo = new ProcessStartInfo();
        startInfo.FileName = powershell;
        startInfo.Arguments =
            "-NoLogo -NoProfile -ExecutionPolicy Bypass -STA " +
            "-File " + Quote(scriptPath) + " " +
            "-PayloadZip " + Quote(payloadZip) + " " +
            "-Version " + Quote(version);
        startInfo.WorkingDirectory = payloadDirectory;
        startInfo.UseShellExecute = false;
        startInfo.CreateNoWindow = true;
        startInfo.WindowStyle = ProcessWindowStyle.Hidden;

        try
        {
            using (Process process = Process.Start(startInfo))
            {
                process.WaitForExit();
                return process.ExitCode;
            }
        }
        catch (Exception ex)
        {
            MessageBox.Show(ex.Message, "Patchy Setup", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return 1;
        }
    }

    private static string Quote(string value)
    {
        return "\"" + value.Replace("\"", "\\\"") + "\"";
    }
}
