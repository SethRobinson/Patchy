#pragma once

#include "core/document.hpp"

#include <QImage>

#include <string>
#include <string_view>

namespace photoslop::ui {

[[nodiscard]] Document document_from_qimage(const QImage& image, std::string layer_name);
[[nodiscard]] QImage qimage_from_document(const Document& document, bool preserve_alpha);
[[nodiscard]] bool image_format_preserves_alpha(std::string_view extension) noexcept;

}  // namespace photoslop::ui
