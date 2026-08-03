#pragma once

// Wires the Qt font database into the PSD reader's PostScript font-name
// resolution (psd::set_photoshop_font_resolver). Off Windows the reader has no
// DirectWrite, and its suffix heuristic flattens names like "Arial-Black" to
// family Arial + the bold flag, ~20% narrower than the real face even where
// Arial Black is installed; the installed resolver asks the font database
// first, the same family-or-"family style" split the text renderer resolves
// through. Call once at startup after QGuiApplication exists and before any
// document is opened; the app (src/app/main.cpp) and the UI test harness
// (tests/ui/main.cpp) both do. A no-op on Windows (the reader keeps its
// DirectWrite -> registry -> heuristic chain) and on wasm (the bundled alias
// families must not be baked into imported metadata; aliases apply at render
// and edit time instead, see docs/fonts.md).
//
// Defined in main_window.cpp beside the rest of the font resolution.

namespace patchy::ui {

void install_font_database_psd_font_resolver();

}  // namespace patchy::ui
