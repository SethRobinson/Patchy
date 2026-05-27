#pragma once

#include "core/document.hpp"

#include <QPageLayout>
#include <QRect>
#include <QRectF>
#include <QSizeF>
#include <QString>

#include <optional>

class QPainter;
class QWidget;

namespace patchy::ui {

enum class PrintAreaMode {
  Document,
  Selection
};

enum class PrintScaleMode {
  ActualSize,
  FitToPage,
  CustomScale
};

struct PrintSettings {
  PrintAreaMode area_mode{PrintAreaMode::Document};
  QRect selection_bounds;
  PrintScaleMode scale_mode{PrintScaleMode::ActualSize};
  double scale_percent{100.0};
  double print_resolution_ppi{0.0};
  bool center{true};
  double offset_x_inches{0.0};
  double offset_y_inches{0.0};
  bool crop_marks{false};
};

struct PrintPlacement {
  QRect source_rect;
  QRectF target_rect_points;
  double scale_percent{100.0};
  QSizeF print_size_inches;
};

[[nodiscard]] QPageLayout default_print_page_layout();
[[nodiscard]] PrintSettings default_print_settings(const Document& document, std::optional<QRect> selection_bounds);
[[nodiscard]] PrintPlacement calculate_print_placement(const Document& document, const PrintSettings& settings,
                                                       const QPageLayout& page_layout);
void render_print_page(QPainter& painter, const Document& document, const PrintSettings& settings,
                       const QPageLayout& page_layout);
[[nodiscard]] bool write_print_pdf(const QString& path, const Document& document, const PrintSettings& settings,
                                   const QPageLayout& page_layout);
void run_page_setup_dialog(QWidget* parent, QPageLayout* page_layout);
[[nodiscard]] bool run_print_dialog(QWidget* parent, const Document& document, std::optional<QRect> selection_bounds,
                                    QPageLayout* page_layout);

}  // namespace patchy::ui
