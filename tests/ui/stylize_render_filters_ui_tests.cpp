// The Stylize and Render filters added for GitHub issue 496 in the real window:
// both submenus, a parameterless filter applying at once, the Lens Flare and
// Flame dialogs with their live preview and draggable center, each as one undo
// step. Captures (test-artifacts/issue_496_*.png) are the review shots.

#include "filters/builtin_filters.hpp"
#include "filters/filter_registry.hpp"
#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/main_window.hpp"
#include "ui/qt_paths.hpp"
#include "ui/zoomable_image_preview.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QImage>
#include <QMenu>
#include <QPainter>
#include <QTimer>

#include <cmath>
#include <cstring>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace patchy::test::ui;

bool same_pixels(const patchy::PixelBuffer& a, const patchy::PixelBuffer& b) {
  if (a.width() != b.width() || a.height() != b.height() || a.format() != b.format()) {
    return false;
  }
  for (std::int32_t y = 0; y < a.height(); ++y) {
    if (std::memcmp(a.row(y).data(), b.row(y).data(), a.row(y).size()) != 0) {
      return false;
    }
  }
  return true;
}

const patchy::Layer* active_layer(patchy::ui::MainWindow& window) {
  const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  const auto id = document.active_layer_id();
  return id.has_value() ? document.find_layer(*id) : nullptr;
}

// Waits for the direct filter dialog's preview of `filter_id`, moves its center
// overlay to (x, y) of the displayed image, optionally picks an option, saves
// the dialog as `artifact` and accepts it.
void drive_filter_dialog(const QString& filter_id, QPointF target, const QString& option_combo,
                         int option_index, const std::string& artifact, bool* drove, int attempts = 400) {
  QTimer::singleShot(25, [=] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    if (dialog == nullptr || !dialog->isVisible()) {
      CHECK(attempts > 0);
      if (attempts > 0) {
        drive_filter_dialog(filter_id, target, option_combo, option_index, artifact, drove, attempts - 1);
      }
      return;
    }
    // ZoomableImagePreview has no Q_OBJECT macro, so findChild cannot cast to it.
    auto* preview = static_cast<patchy::ui::ZoomableImagePreview*>(
        dialog->findChild<QWidget*>(QStringLiteral("filterDialogPreview")));
    auto* center_x = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("filterCenterXSpin"));
    auto* center_y = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("filterCenterYSpin"));
    CHECK(preview != nullptr && center_x != nullptr && center_y != nullptr);
    if (preview == nullptr || center_x == nullptr || center_y == nullptr) {
      dialog->reject();
      return;
    }
    if (!option_combo.isEmpty()) {
      auto* combo = dialog->findChild<QComboBox*>(option_combo);
      CHECK(combo != nullptr);
      if (combo != nullptr) {
        combo->setCurrentIndex(option_index);
      }
    }
    CHECK(process_events_until(
        [&] { return preview->property("filterDialogRenderedFilterId").toString() == filter_id; }, 8000));
    CHECK(process_events_until([&] { return preview->property("filterSpatialOverlayVisible").toBool(); },
                               3000));
    const QSizeF shown(preview->image().width() * preview->zoom(), preview->image().height() * preview->zoom());
    const QRectF displayed(QPointF((preview->width() - shown.width()) / 2.0,
                                   (preview->height() - shown.height()) / 2.0),
                           shown);
    const QPointF from(displayed.left() + preview->property("filterCenterXNormalized").toDouble() * displayed.width(),
                       displayed.top() + preview->property("filterCenterYNormalized").toDouble() * displayed.height());
    const QPoint to = QPointF(displayed.left() + displayed.width() * target.x(),
                              displayed.top() + displayed.height() * target.y())
                          .toPoint();
    send_mouse(*preview, QEvent::MouseButtonPress, from.toPoint(), Qt::LeftButton, Qt::LeftButton);
    send_mouse(*preview, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
    send_mouse(*preview, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    CHECK(std::abs(center_x->value() - target.x() * 100.0) <= 1.0);
    CHECK(std::abs(center_y->value() - target.y() * 100.0) <= 1.0);
    // The preview re-renders for the moved center before the capture.
    static_cast<void>(process_events_until([] { return false; }, 600));
    save_widget_artifact(artifact, *dialog);
    *drove = true;
    dialog->accept();
  });
}

void ui_issue_496_stylize_and_render_filters_in_the_window() {
  patchy::ui::MainWindow window;
  show_window(window);
  for (const char* name : {"filterStylizeMenu", "filterRenderMenu"}) {
    auto* menu = window.findChild<QMenu*>(QString::fromLatin1(name));
    CHECK(menu != nullptr);
    if (menu == nullptr) {
      continue;
    }
    menu->popup(window.mapToGlobal(QPoint(40, 40)));
    QApplication::processEvents();
    save_widget_artifact(std::string("issue_496_") + name, *menu);
    menu->hide();
  }

  const auto photo = patchy::test::committed_format_fixture_path("readme", "havana_vintage_car_cc0.jpg");
  patchy::ui::MainWindowTestAccess::open_document_path(window, patchy::ui::to_qstring(photo));
  const auto* layer = active_layer(window);
  CHECK(layer != nullptr);
  if (layer == nullptr) {
    return;
  }
  const auto original = std::as_const(*layer).pixels();
  const auto depth = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  // Solarize has no settings: one click, one undo step.
  require_action(window, "filterAction_patchy_filters_solarize")->trigger();
  QApplication::processEvents();
  layer = active_layer(window);
  CHECK(layer != nullptr && !same_pixels(std::as_const(*layer).pixels(), original));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth + 1U);
  patchy::ui::MainWindowTestAccess::undo(window);
  layer = active_layer(window);
  CHECK(layer != nullptr && same_pixels(std::as_const(*layer).pixels(), original));

  struct DialogCase {
    const char* action;
    QString filter_id;
    QPointF center;
    QString combo;
    int option;
    std::string name;
  };
  const std::vector<DialogCase> dialogs{
      {"filterAction_patchy_filters_lens_flare", QStringLiteral("patchy.filters.lens_flare"), {0.72, 0.22},
       QString(), 0, "issue_496_lens_flare"},
      {"filterAction_patchy_filters_flame", QStringLiteral("patchy.filters.flame"), {0.5, 0.96}, QString(), 0,
       "issue_496_flame"},
  };
  for (const auto& test_case : dialogs) {
    bool drove = false;
    drive_filter_dialog(test_case.filter_id, test_case.center, test_case.combo, test_case.option,
                        test_case.name + "_dialog", &drove);
    require_action(window, test_case.action)->trigger();
    QApplication::processEvents();
    CHECK(drove);
    layer = active_layer(window);
    CHECK(layer != nullptr);
    if (layer == nullptr) {
      return;
    }
    const auto& applied = std::as_const(*layer).pixels();
    CHECK(!same_pixels(applied, original));
    CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth + 1U);
    save_image_artifact(test_case.name + "_result", patchy::ui::display_qimage_from_pixel_buffer(applied));
    patchy::ui::MainWindowTestAccess::undo(window);
    layer = active_layer(window);
    CHECK(layer != nullptr && same_pixels(std::as_const(*layer).pixels(), original));
  }
}

// Every new filter at its defaults on the photo, labeled, for review.
void ui_issue_496_new_filters_contact_sheet() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  QImage photo(patchy::ui::to_qstring(
      patchy::test::committed_format_fixture_path("readme", "havana_vintage_car_cc0.jpg")));
  CHECK(!photo.isNull());
  if (photo.isNull()) {
    return;
  }
  photo = photo.scaledToWidth(360, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGBA8888);
  patchy::PixelBuffer source(photo.width(), photo.height(), patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < photo.height(); ++y) {
    std::memcpy(source.pixel(0, y), photo.constScanLine(y), static_cast<std::size_t>(photo.width()) * 4U);
  }
  const char* ids[] = {"diffuse",    "extrude",           "find_edges", "solarize", "tiles",     "trace_contour",
                       "wind",       "difference_clouds", "fibers",     "flame",    "lens_flare"};
  constexpr int kColumns = 4;
  const int cell_width = photo.width() + 16;
  const int cell_height = photo.height() + 40;
  const int count = static_cast<int>(std::size(ids)) + 1;
  QImage sheet(kColumns * cell_width, ((count + kColumns - 1) / kColumns) * cell_height, QImage::Format_RGB32);
  sheet.fill(QColor(30, 32, 36));
  QPainter painter(&sheet);
  painter.setFont(visual_test_font());
  painter.setPen(QColor(225, 230, 238));
  for (int index = 0; index < count; ++index) {
    auto pixels = source;
    QString label = QStringLiteral("Original");
    if (index > 0) {
      const auto id = std::string("patchy.filters.") + ids[index - 1];
      const auto* filter = registry.find(id);
      CHECK(filter != nullptr);
      registry.apply(registry.default_invocation(id, patchy::RgbColor{0, 0, 0}, patchy::RgbColor{255, 255, 255}),
                     pixels);
      label = patchy::ui::filter_display_name(*filter);
    }
    const QPoint origin((index % kColumns) * cell_width + 8, (index / kColumns) * cell_height + 8);
    painter.drawText(QRect(origin, QSize(cell_width - 16, 24)), Qt::AlignLeft | Qt::AlignVCenter, label);
    painter.drawImage(origin + QPoint(0, 28), patchy::ui::display_qimage_from_pixel_buffer(pixels));
  }
  painter.end();
  save_image_artifact("issue_496_new_filters_sheet", sheet);
}

}  // namespace

std::vector<patchy::test::TestCase> stylize_render_filters_ui_tests() {
  return {
      {"ui_issue_496_stylize_and_render_filters_in_the_window", ui_issue_496_stylize_and_render_filters_in_the_window},
      {"ui_issue_496_new_filters_contact_sheet", ui_issue_496_new_filters_contact_sheet},
  };
}
