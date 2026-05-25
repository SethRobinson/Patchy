#pragma once

#include "core/document.hpp"

#include <QImage>
#include <QRect>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace photoslop::ui {

[[nodiscard]] Document document_from_qimage(const QImage& image, std::string layer_name);
[[nodiscard]] QImage qimage_from_document(const Document& document, bool preserve_alpha);
[[nodiscard]] QImage qimage_from_document_rect(const Document& document, QRect document_rect, bool preserve_alpha);
[[nodiscard]] QImage qimage_from_document_rect_with_layer_bounds(
    const Document& document, QRect document_rect, bool preserve_alpha,
    const std::vector<std::pair<LayerId, Rect>>& layer_bounds);
[[nodiscard]] QImage qimage_from_document_rect_with_layer_bounds(const Document& document, QRect document_rect,
                                                                 bool preserve_alpha, LayerId layer_id,
                                                                 Rect layer_bounds);
[[nodiscard]] bool image_format_preserves_alpha(std::string_view extension) noexcept;

}  // namespace photoslop::ui
