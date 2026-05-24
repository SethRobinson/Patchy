# macOS Packaging

Planned release shape:

- Build universal or separate Apple Silicon/Intel binaries.
- Bundle Qt frameworks with `macdeployqt` or CMake deployment helpers.
- Code sign the app bundle.
- Notarize with Apple notary service.
- Ship a DMG or PKG.
