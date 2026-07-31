#pragma once

// Internal wasm-only seam between user_fonts.cpp (shared registration logic)
// and user_fonts_wasm.cpp (the EM_JS IndexedDB glue). Only include under
// Q_OS_WASM. The glue follows the pinned wasm rule (docs/wasm.md): page-side
// JS callbacks only write plain JS state and never call a wasm export; the
// C++ side polls that state from a QTimer.

#include <QByteArray>
#include <QString>

namespace patchy::ui::user_fonts::wasm_store {

// Fire-and-forget IndexedDB put/clear (DB "PatchyUserFonts", store "fonts"
// keyed by file name, so a same-named font overwrites across sessions).
void put(const QString& name, const QByteArray& bytes);
void clear();

// Starts the page-side IndexedDB read and arms the QTimer that polls it,
// registering each stored font when the read completes.
void begin_restore();

}  // namespace patchy::ui::user_fonts::wasm_store

namespace patchy::ui::user_fonts {

// Implemented in user_fonts.cpp; called by the restore poll for each stored
// entry (registers without re-persisting).
void register_restored_font(const QString& name, const QByteArray& bytes);

}  // namespace patchy::ui::user_fonts
