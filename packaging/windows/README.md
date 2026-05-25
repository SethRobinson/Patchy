# Windows Packaging

Planned release shape:

- Build with MSVC 2022.
- Bundle Qt runtime dependencies using `windeployqt`.
- Sign binaries and installer.
- Produce an installer with WiX Toolset or a commercial installer system.
- Generate optional MSIX after core installer behavior stabilizes.

## Local zip package

Run the root script from a Developer Command Prompt or regular `cmd.exe`:

```bat
build-release.bat
```

The script configures and builds the `release` preset, deploys the Qt runtime, stages runtime files under `build\package`, and creates:

```text
build\package\photoslop-<version>-windows-x64.zip
```

When Seth's local signing setup is available, the script signs `build\release\photoslop.exe` before staging it:

- `RT_PROJECTS` must point at the RT projects root.
- The script calls `%RT_PROJECTS%\Signing\sign.bat "%EXE%" "Photoslop" "rtsoft.com"`.
- Signature verification uses `C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe`.

If `RT_PROJECTS` or the signing script is missing, signing is skipped and the unsigned zip is still created.

Installer creation and publishing are not implemented yet; the current local handoff artifact is the zip package.
