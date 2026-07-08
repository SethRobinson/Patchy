#pragma once

#include "ui/image_document_io.hpp"

#include <QString>

#include <optional>

class QWidget;

namespace patchy::ui {

[[nodiscard]] bool image_save_options_apply_to_extension(const QString& extension);
[[nodiscard]] ImageSaveOptions load_image_save_option_defaults();
void save_image_save_option_defaults(const ImageSaveOptions& options);
// for_export adds the nearest-neighbor Scale combo (1x/2x/4x/8x) to every raster format's
// dialog — including a scale-only dialog for formats that otherwise have no options — and
// is passed only by the Export Flat Image flow, never Save/Save As.
[[nodiscard]] std::optional<ImageSaveOptions> prompt_image_save_options(QWidget* parent, const QString& extension,
                                                                        ImageSaveOptions options,
                                                                        bool for_export = false);

}  // namespace patchy::ui
