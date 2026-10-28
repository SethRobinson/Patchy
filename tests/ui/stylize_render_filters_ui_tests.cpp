// The Stylize and Render filters added for GitHub issue 496 in the real window:
// both submenus, a parameterless filter applying at once, the Lens Flare and
// Flame dialogs with their live preview and draggable center, each as one undo
// step. Captures (test-artifacts/issue_496_*.png) are the review shots.

#include "core/document.hpp"
#include "core/document_path.hpp"
#include "core/vector_shape.hpp"
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
#include <QRegion>
#include <QFont>
#include <QSpinBox>
#include <QPainterPath>
#include <QListWidget>
#include <QLabel>
#include <QPainter>
#include <QTimer>

#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
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


patchy::PixelBuffer photo_pixels(int width) {
  QImage photo(patchy::ui::to_qstring(
      patchy::test::committed_format_fixture_path("readme", "havana_vintage_car_cc0.jpg")));
  CHECK(!photo.isNull());
  if (photo.isNull()) {
    return {};
  }
  photo = photo.scaledToWidth(width, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGBA8888);
  patchy::PixelBuffer pixels(photo.width(), photo.height(), patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < photo.height(); ++y) {
    std::memcpy(pixels.pixel(0, y), photo.constScanLine(y), static_cast<std::size_t>(photo.width()) * 4U);
  }
  return pixels;
}

// A transparent result shown over a dark backdrop, as it sits on a dark layer.
QImage over_dark(const patchy::PixelBuffer& pixels) {
  QImage shown(pixels.width(), pixels.height(), QImage::Format_RGB32);
  shown.fill(QColor(22, 22, 26));
  QPainter painter(&shown);
  painter.drawImage(0, 0, patchy::ui::display_qimage_from_pixel_buffer(pixels));
  return shown;
}

// Labeled cells, `columns` wide, for review sheets.
QImage labeled_sheet(const std::vector<std::pair<QString, QImage>>& cells, int columns) {
  if (cells.empty()) {
    return {};
  }
  const int cell_width = cells.front().second.width() + 16;
  const int cell_height = cells.front().second.height() + 40;
  const int count = static_cast<int>(cells.size());
  QImage sheet(columns * cell_width, ((count + columns - 1) / columns) * cell_height, QImage::Format_RGB32);
  sheet.fill(QColor(30, 32, 36));
  QPainter painter(&sheet);
  painter.setFont(visual_test_font());
  painter.setPen(QColor(225, 230, 238));
  for (int index = 0; index < count; ++index) {
    const QPoint origin((index % columns) * cell_width + 8, (index / columns) * cell_height + 8);
    painter.drawText(QRect(origin, QSize(cell_width - 16, 24)), Qt::AlignLeft | Qt::AlignVCenter,
                     cells[static_cast<std::size_t>(index)].first);
    painter.drawImage(origin + QPoint(0, 28), cells[static_cast<std::size_t>(index)].second);
  }
  return sheet;
}

const std::vector<std::pair<const char*, const char*>>& flame_presets() {
  static const std::vector<std::pair<const char*, const char*>> presets{
      {"classic", "Campfire"},     {"candle", "Candle"},         {"torch", "Torch"},
      {"wildfire", "Wildfire"},    {"fire_wall", "Fire Wall"},   {"fire_line", "Fire Line"},
      {"embers", "Embers"},        {"smoky_fire", "Smoky Fire"}, {"soft_glow", "Soft Glow"},
      {"blue", "Blue Gas"},        {"green", "Green Magic"},     {"purple_spirit", "Purple Spirit"},
      {"cold_flame", "Cold Flame"}, {"colors", "Foreground to Background"}};
  return presets;
}

// Every Flame preset at its defaults, on an empty layer and on the photo.
void ui_issue_496_flame_presets_sheets() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  const auto photo = photo_pixels(320);
  if (photo.empty()) {
    return;
  }
  const patchy::PixelBuffer empty(photo.width(), photo.height(), patchy::PixelFormat::rgba8());
  std::vector<std::pair<QString, QImage>> on_empty;
  std::vector<std::pair<QString, QImage>> on_photo;
  for (const auto& [token, name] : flame_presets()) {
    auto invocation = registry.default_invocation("patchy.filters.flame", patchy::RgbColor{40, 120, 255},
                                                  patchy::RgbColor{255, 255, 255});
    invocation.parameters["colors"] = std::string(token);
    auto over_empty = empty;
    registry.apply(invocation, over_empty);
    auto over_photo = photo;
    registry.apply(invocation, over_photo);
    CHECK(!over_empty.empty());
    on_empty.push_back({QString::fromLatin1(name), over_dark(over_empty)});
    on_photo.push_back({QString::fromLatin1(name), patchy::ui::display_qimage_from_pixel_buffer(over_photo)});
  }
  save_image_artifact("issue_496_flame_presets_empty", labeled_sheet(on_empty, 5));
  save_image_artifact("issue_496_flame_presets_photo", labeled_sheet(on_photo, 5));
}

// Fire tracing text: the letters' outlines as the guide, rising away from them.
void ui_issue_496_flame_follows_text_outlines() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  QFont font = visual_test_font();
  font.setPixelSize(200);
  font.setBold(true);
  QPainterPath text;
  text.addText(QPointF(40.0, 300.0), font, QStringLiteral("FIRE"));
  patchy::PixelBuffer canvas(760, 380, patchy::PixelFormat::rgba8());
  auto invocation = registry.default_invocation("patchy.filters.flame");
  invocation.parameters["mode"] = std::string("follow_path");
  invocation.parameters["rise"] = std::string("from_path");
  invocation.parameters["length"] = std::int64_t{16};
  for (const auto& polygon : text.toSubpathPolygons()) {
    patchy::FilterGuidePolyline polyline;
    for (const auto& point : polygon) {
      polyline.push_back({point.x(), point.y()});
    }
    if (polyline.size() >= 2U) {
      invocation.guide_paths.push_back(std::move(polyline));
    }
  }
  CHECK(!invocation.guide_paths.empty());
  auto fire = canvas;
  registry.apply(invocation, fire);
  CHECK(!same_pixels(fire, canvas));
  // The fire sits on the letters: inside the F's stem and away from them stays clear.
  CHECK(fire.pixel(5, 5)[3] == 0);
  save_image_artifact("issue_496_flame_text_path", over_dark(fire));
  invocation.parameters["colors"] = std::string("blue");
  invocation.parameters["rise"] = std::string("up");
  auto blue = canvas;
  registry.apply(invocation, blue);
  save_image_artifact("issue_496_flame_text_path_blue_up", over_dark(blue));
}

// Follow Path without a path says so in the dialog; with the Paths panel's
// path selected the flames follow it. Wind's dialog shows its angle dial.
void ui_issue_496_flame_follow_path_and_wind_angle_dialogs() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto photo = patchy::test::committed_format_fixture_path("readme", "havana_vintage_car_cc0.jpg");
  patchy::ui::MainWindowTestAccess::open_document_path(window, patchy::ui::to_qstring(photo));
  const auto* layer = active_layer(window);
  CHECK(layer != nullptr);
  if (layer == nullptr) {
    return;
  }
  const auto original = std::as_const(*layer).pixels();

  // Wind: the angle has the dial.
  bool wind_seen = false;
  QTimer::singleShot(50, [&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* angle = dialog->findChild<QSpinBox*>(QStringLiteral("filterAngleSpin"));
    CHECK(angle != nullptr && dialog->findChild<QWidget*>(QStringLiteral("filterAngleDial")) != nullptr);
    if (angle != nullptr) {
      CHECK(angle->value() == 180);
      angle->setValue(30);
    }
    static_cast<void>(process_events_until([] { return false; }, 900));
    save_widget_artifact("issue_496_wind_dialog", *dialog);
    wind_seen = true;
    dialog->reject();
  });
  require_action(window, "filterAction_patchy_filters_wind")->trigger();
  QApplication::processEvents();
  CHECK(wind_seen);

  // Flame with no path: Follow Path shows the notice, and OK changes nothing.
  bool notice_seen = false;
  QTimer::singleShot(50, [&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* notice = dialog->findChild<QLabel*>(QStringLiteral("filterDialogNotice"));
    auto* mode = dialog->findChild<QComboBox*>(QStringLiteral("filterFlameModeCombo"));
    CHECK(notice != nullptr && mode != nullptr);
    if (notice == nullptr || mode == nullptr) {
      dialog->reject();
      return;
    }
    CHECK(!notice->isVisibleTo(dialog));
    mode->setCurrentIndex(mode->findData(QStringLiteral("follow_path")));
    QApplication::processEvents();
    CHECK(notice->isVisibleTo(dialog));
    CHECK(notice->text().contains(QStringLiteral("Follow Path needs a path")));
    static_cast<void>(process_events_until([] { return false; }, 600));
    save_widget_artifact("issue_496_flame_follow_path_notice", *dialog);
    notice_seen = true;
    dialog->accept();
  });
  require_action(window, "filterAction_patchy_filters_flame")->trigger();
  QApplication::processEvents();
  CHECK(notice_seen);
  layer = active_layer(window);
  CHECK(layer != nullptr && same_pixels(std::as_const(*layer).pixels(), original));

  // A path selected in the Paths panel: Follow Path burns along it.
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  patchy::VectorPath path;
  patchy::PathSubpath line;
  line.closed = false;
  for (const auto& [x, y] : {std::pair{300.0, 1500.0}, std::pair{1500.0, 1100.0}, std::pair{2700.0, 1500.0}}) {
    patchy::PathAnchor anchor;
    anchor.anchor_x = anchor.in_x = anchor.out_x = x;
    anchor.anchor_y = anchor.in_y = anchor.out_y = y;
    line.anchors.push_back(anchor);
  }
  path.subpaths.push_back(line);
  patchy::DocumentPath work(document.allocate_path_id(), "Work Path", patchy::DocumentPathKind::Work,
                            std::move(path));
  work.mark_dirty();
  document.add_path(std::move(work));
  patchy::ui::MainWindowTestAccess::refresh_paths_panel(window);
  auto* paths_list = window.findChild<QListWidget*>(QStringLiteral("pathsList"));
  CHECK(paths_list != nullptr);
  if (paths_list == nullptr) {
    return;
  }
  paths_list->setCurrentRow(0);
  QApplication::processEvents();
  bool followed = false;
  QTimer::singleShot(50, [&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* notice = dialog->findChild<QLabel*>(QStringLiteral("filterDialogNotice"));
    auto* mode = dialog->findChild<QComboBox*>(QStringLiteral("filterFlameModeCombo"));
    auto* length = dialog->findChild<QSpinBox*>(QStringLiteral("filterLengthSpin"));
    CHECK(notice != nullptr && mode != nullptr && length != nullptr);
    if (notice == nullptr || mode == nullptr || length == nullptr) {
      dialog->reject();
      return;
    }
    mode->setCurrentIndex(mode->findData(QStringLiteral("follow_path")));
    length->setValue(15);
    QApplication::processEvents();
    CHECK(!notice->isVisibleTo(dialog));
    static_cast<void>(process_events_until([] { return false; }, 1500));
    save_widget_artifact("issue_496_flame_dialog", *dialog);
    followed = true;
    dialog->accept();
  });
  require_action(window, "filterAction_patchy_filters_flame")->trigger();
  QApplication::processEvents();
  CHECK(followed);
  layer = active_layer(window);
  CHECK(layer != nullptr);
  if (layer != nullptr) {
    const auto& burned = std::as_const(*layer).pixels();
    CHECK(!same_pixels(burned, original));
    save_image_artifact("issue_496_flame_follow_path_result", patchy::ui::display_qimage_from_pixel_buffer(burned));
  }
}


// Flame's 17 controls sit in two columns, so its dialog fits a 1080p screen at
// 150 % (720 logical pixels tall); dialogs with fewer controls keep one column.
void ui_issue_496_long_filter_dialogs_use_two_columns() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  const auto photo = photo_pixels(900);
  if (photo.empty()) {
    return;
  }
  const patchy::Rect bounds{0, 0, photo.width(), photo.height()};
  const patchy::ui::FilterDialogPreviewSource source{&photo, bounds, QRegion(), &registry};
  struct Case {
    const char* id;
    const char* first;  // the first control's slider or combo
    const char* last;   // the last control's slider
    bool two_columns;
    std::string artifact;
  };
  const std::vector<Case> cases{
      {"patchy.filters.flame", "filterFlameColorsCombo", "filterCenterYSlider", true,
       "issue_496_flame_dialog_two_columns"},
      {"patchy.filters.lens_flare", "filterFlareTypeCombo", "filterCenterYSlider", false,
       "issue_496_lens_flare_dialog_after"},
      {"patchy.filters.wind", "filterMethodCombo", "filterLengthSlider", false, "issue_496_wind_dialog_after"},
      {"patchy.filters.extrude", "filterBlockShapeCombo", "filterSeedSpin", false,
       "issue_496_extrude_dialog_after"},
  };
  for (const auto& test_case : cases) {
    const auto* filter = registry.find(test_case.id);
    CHECK(filter != nullptr);
    if (filter == nullptr) {
      continue;
    }
    const auto spec = patchy::ui::filter_dialog_spec_for(*filter);
    CHECK((spec.controls.size() >= patchy::ui::kFilterDialogTwoColumnControls) == test_case.two_columns);
    bool checked = false;
    QTimer::singleShot(0, [&] {
      auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
      CHECK(dialog != nullptr);
      if (dialog == nullptr) {
        return;
      }
      auto* first = dialog->findChild<QWidget*>(QString::fromLatin1(test_case.first));
      auto* last = dialog->findChild<QWidget*>(QString::fromLatin1(test_case.last));
      CHECK(first != nullptr && last != nullptr);
      if (first != nullptr && last != nullptr) {
        const auto first_x = first->mapTo(dialog, QPoint(0, 0)).x();
        const auto last_x = last->mapTo(dialog, QPoint(0, 0)).x();
        // The last control starts a column to the right of the first only in
        // the two-column layout.
        CHECK((last_x > first_x + first->width() / 2) == test_case.two_columns);
      }
      const auto hint = dialog->sizeHint();
      std::cout << "  [dialog] " << test_case.id << " size hint " << hint.width() << " x " << hint.height()
                << '\n';
      CHECK(hint.height() <= 680);
      CHECK(hint.width() <= 1200);
      // And it still fits a 1024 px wide screen without squeezing a control.
      CHECK(dialog->minimumSizeHint().width() <= 1000);
      std::cout << "  [dialog] minimum " << dialog->minimumSizeHint().width() << " x "
                << dialog->minimumSizeHint().height() << '\n';
      static_cast<void>(process_events_until(
          [&] {
            auto* preview = dialog->findChild<QWidget*>(QStringLiteral("filterDialogPreview"));
            return preview != nullptr &&
                   preview->property("filterDialogRenderedFilterId").toString() ==
                       QString::fromLatin1(test_case.id);
          },
          8000));
      save_widget_artifact(test_case.artifact, *dialog);
      checked = true;
      dialog->reject();
    });
    static_cast<void>(patchy::ui::request_filter_settings(nullptr, spec, {}, {}, &source));
    CHECK(checked);
  }
}

}  // namespace

std::vector<patchy::test::TestCase> stylize_render_filters_ui_tests() {
  return {
      {"ui_issue_496_stylize_and_render_filters_in_the_window", ui_issue_496_stylize_and_render_filters_in_the_window},
      {"ui_issue_496_new_filters_contact_sheet", ui_issue_496_new_filters_contact_sheet},
      {"ui_issue_496_flame_presets_sheets", ui_issue_496_flame_presets_sheets},
      {"ui_issue_496_flame_follows_text_outlines", ui_issue_496_flame_follows_text_outlines},
      {"ui_issue_496_flame_follow_path_and_wind_angle_dialogs",
       ui_issue_496_flame_follow_path_and_wind_angle_dialogs},
      {"ui_issue_496_long_filter_dialogs_use_two_columns", ui_issue_496_long_filter_dialogs_use_two_columns},
  };
}
