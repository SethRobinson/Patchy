#pragma once

#include "core/document.hpp"

#include <QImage>
#include <QRect>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

class QString;

namespace patchy::ui {

struct ImageSaveOptions {
  int jpeg_quality{95};
  bool bmp_preserve_alpha{true};
};

[[nodiscard]] Document document_from_qimage(const QImage& image, std::string layer_name);
[[nodiscard]] PixelBuffer pixels_from_image_rgba(const QImage& image);
[[nodiscard]] QImage qimage_from_document(const Document& document, bool preserve_alpha);
[[nodiscard]] QImage qimage_from_document_rect(const Document& document, QRect document_rect, bool preserve_alpha);
[[nodiscard]] QImage qimage_from_document_rect_with_layer_bounds(
    const Document& document, QRect document_rect, bool preserve_alpha,
    const std::vector<std::pair<LayerId, Rect>>& layer_bounds);
[[nodiscard]] QImage qimage_from_document_rect_with_layer_bounds(const Document& document, QRect document_rect,
                                                                 bool preserve_alpha, LayerId layer_id,
                                                                 Rect layer_bounds);
[[nodiscard]] bool image_format_preserves_alpha(std::string_view extension) noexcept;
void write_flat_image_file(const Document& document, const QString& path, const QString& extension,
                           const ImageSaveOptions& options = {});

}  // namespace patchy::ui
