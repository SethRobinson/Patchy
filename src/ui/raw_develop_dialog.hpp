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

// Modal camera-raw develop dialog: live half-size preview with white balance, exposure,
// highlight, brightness, demosaic, and noise-reduction controls, then a full-resolution
// develop on accept. Returns nullopt when the user cancels; throws std::runtime_error with
// a user-facing message when the file cannot be decoded at all. All decoding runs on worker
// threads; the UI thread never blocks on LibRaw.
[[nodiscard]] std::optional<RawDevelopOutcome> run_raw_develop_dialog(QWidget* parent, const QString& file_path);

// The dialog's starting parameters: camera-JPEG-like defaults overlaid with the persisted
// last-used settings (the imports/rawDevelop* keys — persisted contract, never rename).
[[nodiscard]] raw::DevelopParams saved_raw_develop_params();
void save_raw_develop_params(const raw::DevelopParams& params);

}  // namespace patchy::ui
