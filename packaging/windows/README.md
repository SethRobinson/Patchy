# Windows Packaging

Planned release shape:

- Build with MSVC 2022.
- Bundle Qt runtime dependencies using `windeployqt`.
- Sign binaries and installer.
- Produce an installer with WiX Toolset or a commercial installer system.
- Generate optional MSIX after core installer behavior stabilizes.
