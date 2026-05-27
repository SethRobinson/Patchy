#pragma once

#include "ui/image_document_io.hpp"

#include <QString>

#include <optional>

class QWidget;

namespace patchy::ui {

[[nodiscard]] bool image_save_options_apply_to_extension(const QString& extension);
[[nodiscard]] ImageSaveOptions load_image_save_option_defaults();
void save_image_save_option_defaults(const ImageSaveOptions& options);
[[nodiscard]] std::optional<ImageSaveOptions> prompt_image_save_options(QWidget* parent, const QString& extension,
                                                                        ImageSaveOptions options);

}  // namespace patchy::ui
