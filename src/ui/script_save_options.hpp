#pragma once

// The options object of doc.saveAs(path, options) / doc.exportAs(path, options)
// (docs/scripting.md "Explicit save options"). The keys mirror the fields of each
// format's Save Options dialog and reuse the persisted token spellings from
// image_save_option_keys.hpp, so a script states exactly what the dialog would.

#include "ui/image_document_io.hpp"

#include <QJSValue>
#include <QString>

namespace patchy::ui {

// Applies `object` (the script's options argument) onto `options` for a save to a file
// with `extension` ("dds", ".JPG", ...). Strict by design: an unknown key, a wrong type, an
// out-of-range value, or a key that does not apply to the extension is an error, so a typo
// never silently saves with defaults. An undefined `object` changes nothing. Returns the
// translated error sentence (prefixed with `method`, "saveAs" or "exportAs") or an empty
// string on success.
[[nodiscard]] QString apply_script_image_save_options(const QString& method, const QString& extension,
                                                      const QJSValue& object, ImageSaveOptions& options);

}  // namespace patchy::ui
