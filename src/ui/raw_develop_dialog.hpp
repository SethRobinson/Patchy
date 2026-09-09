#pragma once

#include "core/document.hpp"
#include "formats/raw_document_io.hpp"

#include <QString>

#include <optional>

class QWidget;

namespace patchy::ui {

struct RawDevelopOutcome {
  Document document;
  raw::DevelopParams params;
};

// Modal camera-raw develop dialog: draft and accurate previews with white balance, exposure,
// highlight, brightness, demosaic, and noise-reduction controls, then a full-resolution
// develop on accept. Returns nullopt on Done or when the user cancels; throws std::runtime_error with
// a user-facing message when the file cannot be decoded at all. All decoding runs on worker
// threads; the UI thread never blocks on LibRaw.
[[nodiscard]] std::optional<RawDevelopOutcome> run_raw_develop_dialog(QWidget* parent, const QString& file_path);

}  // namespace patchy::ui
