// Vertical (tategaki) type layers and right-to-left paragraphs: the cell layout model in
// ui/text_layout.hpp, its caret/selection/hit-test geometry, the orientation toggle, PSD
// round trips, and the committed Photoshop 2026 captures (photoshop-text-vertical-*.psd).
// See docs/text-tool.md ("Vertical text and paragraph direction").

#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "psd/psd_document_io.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "ui/qt_paths.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/script_engine.hpp"

#include "local_psd_fixtures.hpp"
#include "test_fonts.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_support.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QInputMethodEvent>
#include <QFontComboBox>
#include <QScopeGuard>
#include <QFontDatabase>
#include <QListWidget>
#include <QPushButton>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QTimer>
#include <QTextEdit>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using patchy::test::register_test_fonts;
using patchy::test::TestFontRole;

namespace {

using namespace patchy::test::ui;

// Contiguous inked spans along one axis (rows or columns) of a raster, alpha >= 128.
std::vector<std::pair<int, int>> ink_bands(const patchy::PixelBuffer& pixels, bool columns) {
  const auto outer = columns ? pixels.width() : pixels.height();
  const auto inner = columns ? pixels.height() : pixels.width();
  std::vector<std::pair<int, int>> bands;
  std::optional<int> start;
  for (int index = 0; index < outer; ++index) {
    bool inked = false;
    for (int other = 0; other < inner && !inked; ++other) {
      const auto x = columns ? index : other;
      const auto y = columns ? other : index;
      inked = pixels.pixel(x, y)[3] >= 128;
    }
    if (inked && !start.has_value()) {
      start = index;
    } else if (!inked && start.has_value()) {
      bands.emplace_back(*start, index - 1);
      start.reset();
    }
  }
  if (start.has_value()) {
    bands.emplace_back(*start, outer - 1);
  }
  return bands;
}

// Row bands restricted to one column span of the raster.
std::vector<std::pair<int, int>> ink_row_bands_in_columns(const patchy::PixelBuffer& pixels, int left, int right) {
  std::vector<std::pair<int, int>> bands;
  std::optional<int> start;
  for (int y = 0; y < pixels.height(); ++y) {
    bool inked = false;
    for (int x = std::max(0, left); x <= std::min(pixels.width() - 1, right) && !inked; ++x) {
      inked = pixels.pixel(x, y)[3] >= 128;
    }
    if (inked && !start.has_value()) {
      start = y;
    } else if (!inked && start.has_value()) {
      bands.emplace_back(*start, y - 1);
      start.reset();
    }
  }
  if (start.has_value()) {
    bands.emplace_back(*start, pixels.height() - 1);
  }
  return bands;
}

std::optional<QString> japanese_test_family() {
  register_test_fonts(TestFontRole::JapaneseGothic);
  const auto families = QFontDatabase::families(QFontDatabase::Japanese);
  if (families.isEmpty()) {
    std::cout << "[SKIP] no Japanese-capable font available on this machine\n";
    return std::nullopt;
  }
  for (const auto& family : families) {
    if (family.contains(QStringLiteral("Gothic"), Qt::CaseInsensitive)) {
      return family;
    }
  }
  return families.front();
}

bool layer_is_vertical_metadata(const patchy::Layer& layer) {
  const auto found = layer.metadata().find(patchy::kLayerMetadataTextOrientation);
  return found != layer.metadata().end() && found->second == patchy::kTextOrientationVertical;
}

// Enter the layer with the Type tool at `click`, then apply with no change: the layer
// re-renders through Patchy's own text engine (metadata-built rasters start as 8x8 stubs).
void rerender_through_edit_session(patchy::ui::MainWindow& window, patchy::ui::CanvasWidget& canvas,
                                   QListWidget& layer_list, const QString& row_name, QPoint click) {
  layer_list.setCurrentItem(require_layer_item(layer_list, row_name));
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto hit = canvas.widget_position_for_document_point(click);
  send_mouse(canvas, QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, hit, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  process_events_for(250);
  CHECK(canvas.findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) != nullptr);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  process_events_for(200);
}

// photoshop-text-vertical-point.psd is the ps2026_vtext capture vt_point_ja_multi (MS Gothic,
// three-character columns). The ink comparison is against MS Gothic's metrics, and entering an
// imported layer whose face is missing asks the user to confirm a substitute (a modal dialog that
// hung the Linux and macOS suites), so the test skips without that face.
void ui_vertical_text_matches_photoshop_capture() {
  const auto path = patchy::test::committed_psd_fixture_path("photoshop-text-vertical-point.psd");
  register_test_fonts(TestFontRole::UiDefault);
  if (!japanese_test_family().has_value() ||
      skip_without_font_face(QStringLiteral("MS Gothic"), "Photoshop vertical capture face")) {
    return;
  }
  auto document = patchy::psd::DocumentIo::read_file(path);
  const auto* text_layer = [&document]() -> const patchy::Layer* {
    for (const auto& layer : document.layers()) {
      if (patchy::layer_is_text(layer)) {
        return &layer;
      }
    }
    return nullptr;
  }();
  CHECK(text_layer != nullptr);
  if (text_layer == nullptr) {
    return;
  }
  const auto id = text_layer->id();
  const auto row_name = QString::fromStdString(text_layer->name());
  CHECK(text_layer->metadata().at(patchy::kLayerMetadataTextOrientation) == patchy::kTextOrientationVertical);
  // Photoshop's own raster: layerBounds [198, 42, 265, 134] in the capture manifest.
  const auto imported = text_layer->bounds();
  CHECK(imported.x == 198 && imported.y == 42);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("PS vertical"));
  QApplication::processEvents();
  auto& live_document = patchy::ui::MainWindowTestAccess::document(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  if (layer_list == nullptr) {
    return;
  }
  rerender_through_edit_session(window, *canvas, *layer_list, row_name, QPoint(250, 60));
  const auto* layer = live_document.find_layer(id);
  CHECK(layer != nullptr);
  if (layer == nullptr) {
    return;
  }
  const auto ink = patchy::visible_alpha_local_bounds(layer->pixels());
  CHECK(ink.has_value());
  if (!ink.has_value()) {
    return;
  }
  const patchy::Rect ink_document{layer->bounds().x + ink->x, layer->bounds().y + ink->y, ink->width, ink->height};
  std::printf("  re-rendered ink %d,%d %dx%d vs Photoshop 198,42 67x92\n", ink_document.x, ink_document.y,
              ink_document.width, ink_document.height);
  std::fflush(stdout);
  // Photoshop: columns 32 px wide on a 38.4 px pitch, three 32 px cells; MS Gothic ink fills
  // the em almost exactly, so the whole ink box lands within a few pixels.
  CHECK(std::abs(ink_document.x - 198) <= 4);
  CHECK(std::abs(ink_document.y - 42) <= 4);
  CHECK(std::abs(ink_document.width - 67) <= 4);
  CHECK(std::abs(ink_document.height - 92) <= 4);
  const auto columns = ink_bands(layer->pixels(), true);
  CHECK(columns.size() == 2);
}


// Creates a vertical point-text layer the way a user does: Type tool, orientation toggle on,
// click at `click`, type, optional alignment button, commit. The toggle is restored by the
// caller's guard (it persists as a tool setting).
struct ToolLayer {
  std::optional<patchy::LayerId> id;
};

ToolLayer create_vertical_layer_with_tool(patchy::ui::MainWindow& window, patchy::ui::CanvasWidget& canvas,
                                          QPoint click, const QString& text, const QString& family,
                                          const char* alignment_button = nullptr) {
  ToolLayer result;
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  auto* font_combo = window.findChild<QFontComboBox*>(QStringLiteral("textFontCombo"));
  auto* size_spin = window.findChild<QDoubleSpinBox*>(QStringLiteral("textSizeSpin"));
  auto* toggle = window.findChild<QPushButton*>(QStringLiteral("textOrientationButton"));
  CHECK(font_combo != nullptr && size_spin != nullptr && toggle != nullptr);
  if (font_combo == nullptr || size_spin == nullptr || toggle == nullptr) {
    return result;
  }
  font_combo->setCurrentFont(QFont(family));
  size_spin->setValue(32.0);  // 32 pt at the default 72 ppi = 32 px
  if (!toggle->isChecked()) {
    toggle->click();
  }
  QApplication::processEvents();
  patchy::ui::MainWindowTestAccess::add_text_at(window, click);
  QApplication::processEvents();
  auto* editor = canvas.findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return result;
  }
  editor->setPlainText(text);
  QApplication::processEvents();
  if (alignment_button != nullptr) {
    auto* button = window.findChild<QPushButton*>(QString::fromLatin1(alignment_button));
    CHECK(button != nullptr);
    if (button != nullptr) {
      button->click();
    }
  }
  process_events_for(250);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  process_events_for(200);
  result.id = patchy::ui::MainWindowTestAccess::document(window).active_layer_id();
  return result;
}

// A checked toggle with no session arms the next new layer; leave the suite the way it was found.
auto vertical_toggle_guard(patchy::ui::MainWindow& window) {
  return qScopeGuard([&window] {
    if (auto* toggle = window.findChild<QPushButton*>(QStringLiteral("textOrientationButton"));
        toggle != nullptr && toggle->isChecked()) {
      toggle->click();
      QApplication::processEvents();
    }
  });
}

void ui_vertical_text_lays_out_columns_top_to_bottom_right_to_left() {
  register_test_fonts(TestFontRole::UiDefault);
  const auto family = japanese_test_family();
  if (!family.has_value()) {
    return;
  }
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  canvas->set_primary_color(QColor(0, 0, 0));
  const auto guard = vertical_toggle_guard(window);
  auto& live_document = patchy::ui::MainWindowTestAccess::document(window);
  // Two paragraphs of two full-width kana: two columns, two cells each, anchored at the click.
  const auto created = create_vertical_layer_with_tool(
      window, *canvas, QPoint(200, 40), QString::fromUtf8("\xe3\x81\x82\xe3\x81\x84\n\xe3\x81\x86\xe3\x81\x88"), *family);
  CHECK(created.id.has_value());
  if (!created.id.has_value()) {
    return;
  }
  const auto* layer = live_document.find_layer(*created.id);
  CHECK(layer != nullptr && patchy::layer_is_text(*layer));
  if (layer == nullptr) {
    return;
  }
  CHECK(layer->metadata().at(patchy::kLayerMetadataTextOrientation) == patchy::kTextOrientationVertical);
  const auto& pixels = layer->pixels();
  const auto columns = ink_bands(pixels, true);
  std::printf("  vertical columns:");
  for (const auto& [left, right] : columns) {
    std::printf(" [%d,%d]", left, right);
  }
  std::printf(" bounds=%d,%d %dx%d\n", layer->bounds().x, layer->bounds().y, pixels.width(), pixels.height());
  std::fflush(stdout);
  CHECK(columns.size() == 2);
  if (columns.size() != 2) {
    return;
  }
  // Columns advance leftward by the auto leading (1.2 x 32 = 38.4), each about one em wide.
  const auto left_axis = (columns[0].first + columns[0].second) / 2.0;
  const auto right_axis = (columns[1].first + columns[1].second) / 2.0;
  CHECK(std::abs((right_axis - left_axis) - 38.4) <= 3.0);
  CHECK(columns[0].second - columns[0].first <= 34);
  // Each column stacks its two glyphs one em apart: the ink spans about two cells and both
  // columns start at the same top.
  std::vector<std::pair<int, int>> spans;
  for (const auto& [left, right] : columns) {
    const auto rows = ink_row_bands_in_columns(pixels, left, right);
    CHECK(!rows.empty());
    if (!rows.empty()) {
      spans.emplace_back(rows.front().first, rows.back().second);
    }
  }
  CHECK(spans.size() == 2);
  if (spans.size() == 2) {
    for (const auto& [top, bottom] : spans) {
      CHECK(bottom - top >= 50 && bottom - top <= 66);
    }
    CHECK(std::abs(spans[0].first - spans[1].first) <= 3);
  }
  // Two columns of two cells: wider than tall (2 x 32 + 38.4 pitch + bleed vs 64 + bleed).
  CHECK(pixels.width() > pixels.height());
  // The click is the Photoshop anchor: the first (right) column's axis and the run's top.
  const auto bleed = static_cast<int>(std::ceil(0.25 * 32));
  CHECK(std::abs((layer->bounds().x + pixels.width() - bleed - 16) - 200) <= 2);
  CHECK(std::abs((layer->bounds().y + bleed) - 40) <= 2);
}

void ui_vertical_text_caret_and_selection_follow_the_columns() {
  register_test_fonts(TestFontRole::UiDefault);
  const auto family = japanese_test_family();
  if (!family.has_value()) {
    return;
  }
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  canvas->set_primary_color(QColor(0, 0, 0));
  const auto guard = vertical_toggle_guard(window);
  auto& live_document = patchy::ui::MainWindowTestAccess::document(window);
  const auto created = create_vertical_layer_with_tool(
      window, *canvas, QPoint(200, 40), QString::fromUtf8("\xe3\x81\x82\xe3\x81\x84\n\xe3\x81\x86\xe3\x81\x88"), *family);
  CHECK(created.id.has_value());
  if (!created.id.has_value()) {
    return;
  }
  const auto* layer = live_document.find_layer(*created.id);
  CHECK(layer != nullptr);
  if (layer == nullptr) {
    return;
  }
  // Re-enter at the first column's top cell (the anchor is at 200,40; the cell spans 184..216
  // by 40..72).
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto hit = canvas->widget_position_for_document_point(QPoint(200, 48));
  send_mouse(*canvas, QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  process_events_for(250);
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  // A click in the first cell's upper half puts the caret before it.
  CHECK(editor->textCursor().position() == 0);
  const auto caret = editor->property("patchy.previewCaretRect").toRect();
  std::printf("  vertical caret %d,%d %dx%d\n", caret.x(), caret.y(), caret.width(), caret.height());
  std::fflush(stdout);
  CHECK(!caret.isEmpty());
  // Thin ACROSS the column: wider than tall, about one em wide.
  CHECK(caret.width() > caret.height());
  CHECK(caret.height() <= 4);
  CHECK(std::abs(caret.width() - 32) <= 3);

  // Down steps to the next character in the column; Left jumps to the next (left) column.
  send_key(*editor, Qt::Key_Down);
  QApplication::processEvents();
  CHECK(editor->textCursor().position() == 1);
  const auto second_caret = editor->property("patchy.previewCaretRect").toRect();
  CHECK(std::abs(second_caret.y() - caret.y() - 32) <= 3);
  CHECK(std::abs(second_caret.x() - caret.x()) <= 1);
  send_key(*editor, Qt::Key_Left);
  QApplication::processEvents();
  CHECK(editor->textCursor().position() >= 3);
  const auto next_column_caret = editor->property("patchy.previewCaretRect").toRect();
  CHECK(next_column_caret.x() < caret.x() - 20);

  // Select all: one strip per column, taller than wide, side by side.
  editor->selectAll();
  QApplication::processEvents();
  const auto rects = editor->property("patchy.previewSelectionRects").toList();
  CHECK(rects.size() == 2);
  if (rects.size() == 2) {
    const auto first = rects[0].toRect();
    const auto second = rects[1].toRect();
    CHECK(first.height() > first.width());
    CHECK(second.height() > second.width());
    CHECK(first.left() > second.right() || second.left() > first.right());
    CHECK(std::abs(first.height() - 64) <= 4);
  }
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  process_events_for(150);
}

void ui_vertical_text_recommit_keeps_origin_and_round_trips_psd() {
  register_test_fonts(TestFontRole::UiDefault);
  const auto family = japanese_test_family();
  if (!family.has_value()) {
    return;
  }
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  canvas->set_primary_color(QColor(0, 0, 0));
  const auto guard = vertical_toggle_guard(window);
  auto& live_document = patchy::ui::MainWindowTestAccess::document(window);
  const auto created = create_vertical_layer_with_tool(
      window, *canvas, QPoint(200, 150), QString::fromUtf8("\xe3\x81\x82\xe3\x81\x84\n\xe3\x81\x86\xe3\x81\x88"), *family,
      "textAlignCenterButton");
  CHECK(created.id.has_value());
  if (!created.id.has_value()) {
    return;
  }
  const auto id = *created.id;
  const auto* layer = live_document.find_layer(id);
  CHECK(layer != nullptr);
  if (layer == nullptr) {
    return;
  }
  const auto first_bounds = layer->bounds();
  const auto first_pixels = layer->pixels();
  // Centred text hangs around y = 150: two cells of 32 above and below the anchor.
  const auto bleed = static_cast<int>(std::ceil(0.25 * 32));
  std::printf("  centred bounds %d,%d %dx%d\n", first_bounds.x, first_bounds.y, first_bounds.width,
              first_bounds.height);
  std::fflush(stdout);
  CHECK(std::abs((first_bounds.y + bleed + (first_pixels.height() - 2 * bleed) / 2) - 150) <= 2);
  CHECK(std::abs((first_bounds.x + first_bounds.width - bleed - 16) - 200) <= 2);
  // A no-change re-edit reproduces bounds and pixels exactly.
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  if (layer_list == nullptr) {
    return;
  }
  rerender_through_edit_session(window, *canvas, *layer_list, QString::fromStdString(layer->name()),
                                QPoint(first_bounds.x + first_bounds.width / 2, first_bounds.y + first_bounds.height / 2));
  layer = live_document.find_layer(id);
  CHECK(layer != nullptr);
  if (layer == nullptr) {
    return;
  }
  std::printf("  recommit bounds %d,%d %dx%d -> %d,%d %dx%d\n", first_bounds.x, first_bounds.y, first_bounds.width,
              first_bounds.height, layer->bounds().x, layer->bounds().y, layer->bounds().width, layer->bounds().height);
  std::fflush(stdout);
  CHECK(layer->bounds().x == first_bounds.x && layer->bounds().y == first_bounds.y &&
        layer->bounds().width == first_bounds.width && layer->bounds().height == first_bounds.height);
  CHECK(patchy::ui::pixel_buffers_equal(layer->pixels(), first_pixels));

  // PSD round trip: the orientation survives the writer and the reader, and the type block
  // says Vrtc with the vertical writing direction.
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(live_document);
  {
    // Kept as an artifact so Photoshop's read-back of a Patchy-authored vertical layer can be
    // re-measured by COM (docs/text-render-calibration.md).
    std::filesystem::create_directories("test-artifacts");
    std::ofstream out("test-artifacts/vertical_text_check.psd", std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  const std::string haystack(bytes.begin(), bytes.end());
  CHECK(haystack.find("Vrtc") != std::string::npos);
  CHECK(haystack.find("/WritingDirection 2") != std::string::npos);
  const auto reopened = patchy::psd::DocumentIo::read(bytes);
  bool found = false;
  for (const auto& reopened_layer : reopened.layers()) {
    if (!patchy::layer_is_text(reopened_layer)) {
      continue;
    }
    found = true;
    const auto orientation = reopened_layer.metadata().find(patchy::kLayerMetadataTextOrientation);
    CHECK(orientation != reopened_layer.metadata().end() && orientation->second == patchy::kTextOrientationVertical);
    // The anchor written as the transform translation is the first column's top centre (the
    // Photoshop convention): x = right edge minus bleed minus em/2, y = the centre of the run.
    const auto transform = reopened_layer.metadata().find(patchy::kLayerMetadataPsdTextTransform);
    CHECK(transform != reopened_layer.metadata().end());
    if (transform != reopened_layer.metadata().end()) {
      const auto parsed = patchy::parse_layer_affine_transform(transform->second);
      CHECK(parsed.has_value());
      if (parsed.has_value()) {
        std::printf("  psd vertical anchor %.2f,%.2f\n", (*parsed)[4], (*parsed)[5]);
        std::fflush(stdout);
        CHECK(std::abs((*parsed)[4] - 200.0) <= 2.5);
        CHECK(std::abs((*parsed)[5] - 150.0) <= 2.5);
      }
    }
  }
  CHECK(found);
}

void ui_text_orientation_toggle_converts_layer_and_undoes() {
  register_test_fonts(TestFontRole::UiDefault);
  patchy::Document document(320, 200, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(320, 200, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer layer(document.allocate_layer_id(), "Words",
                      solid_pixels(8, 8, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));
  layer.set_bounds(patchy::Rect{40, 40, 8, 8});
  layer.metadata()[patchy::kLayerMetadataText] = "Hello";
  layer.metadata()[patchy::kLayerMetadataTextFlow] = "point";
  layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  layer.metadata()[patchy::kLayerMetadataTextSize] = "32";
  layer.metadata()[patchy::kLayerMetadataTextColor] = "#000000";
  layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
  layer.metadata()[patchy::kLayerMetadataTextRuns] = "v1\n0\t5\t32\t0\t0\t#000000\tArial";
  const auto id = document.add_layer(std::move(layer)).id();

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Toggle"));
  QApplication::processEvents();
  // The toggle persists as the tool default (tools/textVertical); leave the suite as found.
  const auto guard = vertical_toggle_guard(window);
  auto& live_document = patchy::ui::MainWindowTestAccess::document(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  if (layer_list == nullptr) {
    return;
  }
  rerender_through_edit_session(window, *canvas, *layer_list, QStringLiteral("Words"), QPoint(43, 43));
  const auto* text_layer = live_document.find_layer(id);
  CHECK(text_layer != nullptr);
  if (text_layer == nullptr) {
    return;
  }
  const auto horizontal_bounds = text_layer->bounds();
  CHECK(horizontal_bounds.width > horizontal_bounds.height);

  // The options-bar toggle converts the selected layer with no session open.
  layer_list->setCurrentItem(require_layer_item(*layer_list, QStringLiteral("Words")));
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  auto* toggle = window.findChild<QPushButton*>(QStringLiteral("textOrientationButton"));
  CHECK(toggle != nullptr);
  if (toggle == nullptr) {
    return;
  }
  CHECK(!toggle->isChecked());
  toggle->click();
  QApplication::processEvents();
  process_events_for(200);
  text_layer = live_document.find_layer(id);
  CHECK(text_layer != nullptr);
  if (text_layer == nullptr) {
    return;
  }
  const auto orientation = text_layer->metadata().find(patchy::kLayerMetadataTextOrientation);
  CHECK(orientation != text_layer->metadata().end() && orientation->second == patchy::kTextOrientationVertical);
  // "Hello" stacks upright: five cells of 32 make the raster far taller than it is wide.
  std::printf("  toggled bounds %d,%d %dx%d\n", text_layer->bounds().x, text_layer->bounds().y,
              text_layer->bounds().width, text_layer->bounds().height);
  std::fflush(stdout);
  CHECK(text_layer->bounds().height > text_layer->bounds().width * 2);
  CHECK(std::abs(text_layer->bounds().height - (5 * 32 + 2 * 8)) <= 6);
  CHECK(toggle->isChecked());
  auto* direction = window.findChild<QComboBox*>(QStringLiteral("textDirectionCombo"));
  CHECK(direction != nullptr && direction->currentIndex() == 0);

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  text_layer = live_document.find_layer(id);
  CHECK(text_layer != nullptr);
  if (text_layer == nullptr) {
    return;
  }
  CHECK(!text_layer->metadata().contains(patchy::kLayerMetadataTextOrientation));
  CHECK(text_layer->bounds().x == horizontal_bounds.x && text_layer->bounds().y == horizontal_bounds.y &&
        text_layer->bounds().width == horizontal_bounds.width && text_layer->bounds().height == horizontal_bounds.height);
}

void ui_rtl_hebrew_paragraph_direction_reorders_and_persists() {
  register_test_fonts(TestFontRole::UiDefault);
  if (QFontDatabase::families(QFontDatabase::Hebrew).isEmpty()) {
    std::cout << "[SKIP] no Hebrew-capable font available on this machine\n";
    return;
  }
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  canvas->set_primary_color(QColor(0, 0, 0));
  auto& live_document = patchy::ui::MainWindowTestAccess::document(window);
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  auto* font_combo = window.findChild<QFontComboBox*>(QStringLiteral("textFontCombo"));
  auto* size_spin = window.findChild<QDoubleSpinBox*>(QStringLiteral("textSizeSpin"));
  auto* direction = window.findChild<QComboBox*>(QStringLiteral("textDirectionCombo"));
  CHECK(font_combo != nullptr && size_spin != nullptr && direction != nullptr);
  if (font_combo == nullptr || size_spin == nullptr || direction == nullptr) {
    return;
  }
  font_combo->setCurrentFont(QFont(QStringLiteral("Arial")));
  size_spin->setValue(32.0);
  if (auto* toggle = window.findChild<QPushButton*>(QStringLiteral("textOrientationButton")); toggle != nullptr) {
    std::printf("  orientation toggle at start: %s\n", toggle->isChecked() ? "vertical" : "horizontal");
    std::fflush(stdout);
    if (toggle->isChecked()) {
      toggle->click();
      QApplication::processEvents();
    }
  }
  patchy::ui::MainWindowTestAccess::add_text_at(window, QPoint(60, 60));
  QApplication::processEvents();
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  // Mixed text "abc <hebrew> def": auto direction resolves LEFT-to-right from the first strong
  // character, so the caret at logical position 0 ('a') sits at the visual left end.
  editor->setPlainText(QString::fromUtf8("abc \xd7\x90\xd7\x91\xd7\x92 def"));
  auto cursor = editor->textCursor();
  cursor.setPosition(0);
  editor->setTextCursor(cursor);
  process_events_for(250);
  CHECK(direction->currentIndex() == 0);
  const auto ltr_caret = editor->property("patchy.previewCaretRect").toRect();
  editor->selectAll();
  QApplication::processEvents();
  const auto rects = editor->property("patchy.previewSelectionRects").toList();
  CHECK(rects.size() == 1);
  if (rects.size() != 1) {
    return;
  }
  const auto line_rect = rects[0].toRect();
  std::printf("  auto (ltr) caret x=%d line [%d,%d] height=%d editor vertical=%d text=%d chars\n", ltr_caret.x(),
              line_rect.left(), line_rect.right(), line_rect.height(),
              editor->property("patchy.documentTextOrientation").toString() == QStringLiteral("vertical") ? 1 : 0,
              static_cast<int>(editor->toPlainText().size()));
  std::fflush(stdout);
  CHECK(ltr_caret.x() < line_rect.center().x());

  // Forcing right-to-left reverses the run order: "abc" becomes the rightmost run, so position
  // 0 moves to the visual RIGHT half of the line.
  cursor.setPosition(0);
  editor->setTextCursor(cursor);
  direction->setCurrentIndex(direction->findData(static_cast<int>(Qt::RightToLeft)));
  process_events_for(250);
  const auto rtl_caret = editor->property("patchy.previewCaretRect").toRect();
  std::printf("  rtl caret x=%d\n", rtl_caret.x());
  std::fflush(stdout);
  CHECK(rtl_caret.x() > line_rect.center().x());
  CHECK(editor->textCursor().blockFormat().layoutDirection() == Qt::RightToLeft);

  // Commit: the explicit direction persists as a v4 paragraph run and survives a re-edit.
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  process_events_for(200);
  const auto id = live_document.active_layer_id();
  CHECK(id.has_value());
  if (!id.has_value()) {
    return;
  }
  const auto* layer = live_document.find_layer(*id);
  CHECK(layer != nullptr && patchy::layer_is_text(*layer));
  if (layer == nullptr) {
    return;
  }
  const auto runs = layer->metadata().at(patchy::kLayerMetadataTextParagraphRuns);
  std::printf("  paragraph runs: %s\n", runs.c_str());
  std::fflush(stdout);
  CHECK(runs.rfind("v4\n", 0) == 0);
  CHECK(runs.find("\trtl") != std::string::npos);
  const auto ltr_bounds = layer->bounds();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  if (layer_list == nullptr) {
    return;
  }
  rerender_through_edit_session(window, *canvas, *layer_list, QString::fromStdString(layer->name()),
                                QPoint(ltr_bounds.x + ltr_bounds.width / 2, ltr_bounds.y + ltr_bounds.height / 2));
  layer = live_document.find_layer(*id);
  CHECK(layer != nullptr);
  if (layer == nullptr) {
    return;
  }
  CHECK(layer->metadata().at(patchy::kLayerMetadataTextParagraphRuns) == runs);
  CHECK(layer->bounds().x == ltr_bounds.x && layer->bounds().y == ltr_bounds.y &&
        layer->bounds().width == ltr_bounds.width && layer->bounds().height == ltr_bounds.height);
  // The combo reads the selected layer's direction with no session open.
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  CHECK(direction->currentData().toInt() == static_cast<int>(Qt::RightToLeft));
}


// The Character panel's leading field unlocks when Auto leading is unchecked and the value
// commits into the runs (Photoshop's "Set leading"); it was reported as permanently grey. The
// panel runs a nested non-modal loop, so the scenario is driven from a queued lambda.
void ui_text_character_panel_leading_unlocks_and_applies() {
  register_test_fonts(TestFontRole::UiDefault);
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  canvas->set_primary_color(QColor(0, 0, 0));
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  patchy::ui::MainWindowTestAccess::add_text_at(window, QPoint(40, 40));
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  editor->setPlainText(QStringLiteral("One\nTwo\nThree"));
  process_events_for(250);
  auto* character_button = window.findChild<QPushButton*>(QStringLiteral("textCharacterButton"));
  CHECK(character_button != nullptr);
  if (character_button == nullptr) {
    return;
  }
  bool checks_ran = false;
  QTimer::singleShot(0, [&window, canvas, &checks_ran] {
    try {
      auto* dialog = window.findChild<QDialog*>(QStringLiteral("textCharacterDialog"));
      CHECK(dialog != nullptr);
      if (dialog == nullptr) {
        return;
      }
      auto* auto_leading = dialog->findChild<QCheckBox*>(QStringLiteral("textCharacterAutoLeading"));
      auto* leading = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("textCharacterLeadingSpin"));
      auto* tracking = dialog->findChild<QSpinBox*>(QStringLiteral("textCharacterTrackingSpin"));
      CHECK(auto_leading != nullptr && leading != nullptr && tracking != nullptr);
      if (auto_leading == nullptr || leading == nullptr || tracking == nullptr) {
        dialog->reject();
        return;
      }
      CHECK(auto_leading->isChecked());
      // Never locked (Photoshop): the field shows the auto value and editing it turns Auto off.
      CHECK(leading->isEnabled());
      CHECK(tracking->buttonSymbols() == QAbstractSpinBox::PlusMinus);
      CHECK(!tracking->keyboardTracking());
      leading->setValue(96.0);
      QApplication::processEvents();
      process_events_for(250);
      CHECK(!auto_leading->isChecked());
      CHECK(leading->isEnabled());
      CHECK(std::abs(leading->value() - 96.0) < 0.01);
      require_action_by_text(window, QStringLiteral("Move"))->trigger();
      QApplication::processEvents();
      process_events_for(200);
      auto& live_document = patchy::ui::MainWindowTestAccess::document(window);
      const auto id = live_document.active_layer_id();
      CHECK(id.has_value());
      if (id.has_value()) {
        const auto* layer = live_document.find_layer(*id);
        CHECK(layer != nullptr);
        if (layer != nullptr) {
          const auto runs = layer->metadata().at(patchy::kLayerMetadataTextRuns);
          std::printf("  leading runs: %s | height %d\n", runs.substr(0, 70).c_str(), layer->bounds().height);
          std::fflush(stdout);
          // 96 pt at 72 ppi = 96 px of leading: three lines span at least 2 x 96 plus a glyph.
          CHECK(layer->bounds().height >= 2 * 96 + 30);
          CHECK(runs.find("\t96") != std::string::npos);
        }
      }
      // With the layer selected and no session, the panel still reads fixed leading.
      CHECK(!auto_leading->isChecked());
      CHECK(leading->isEnabled());
      CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
      checks_ran = true;
      dialog->reject();
    } catch (...) {
      patchy::ui::unwind_non_modal_dialog_loop(std::current_exception());
    }
  });
  character_button->click();
  QApplication::processEvents();
  CHECK(checks_ran);
}

// The initial size follows the document until the user types one: 48 px on the default
// 1024x768 document, ~312 px on a 5000x5000 canvas, and a typed size sticks.
void ui_new_text_size_scales_with_the_document() {
  register_test_fonts(TestFontRole::UiDefault);
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& live_document = patchy::ui::MainWindowTestAccess::document(window);
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  auto* size_spin = window.findChild<QDoubleSpinBox*>(QStringLiteral("textSizeSpin"));
  CHECK(size_spin != nullptr);
  if (size_spin == nullptr) {
    return;
  }
  const auto default_size = size_spin->value();
  patchy::ui::MainWindowTestAccess::add_text_at(window, QPoint(40, 40));
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  const auto small_document_size = editor->property("patchy.documentTextSize").toInt();
  std::printf("  %dx%d document: %d px (spin %.1f)\n", live_document.width(), live_document.height(),
              small_document_size, default_size);
  std::fflush(stdout);
  CHECK(std::abs(small_document_size - std::lround(std::min(live_document.width(), live_document.height()) / 16.0)) <= 1);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  process_events_for(150);

  patchy::Document big(5000, 5000, patchy::PixelFormat::rgba8());
  big.add_pixel_layer("Background", solid_pixels(5000, 5000, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  window.add_document_session(std::move(big), QStringLiteral("Big"));
  QApplication::processEvents();
  canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  patchy::ui::MainWindowTestAccess::add_text_at(window, QPoint(400, 400));
  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  const auto big_size = editor->property("patchy.documentTextSize").toInt();
  std::printf("  5000x5000 document: %d px (spin %.1f)\n", big_size, size_spin->value());
  std::fflush(stdout);
  CHECK(std::abs(big_size - 312) <= 1);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  process_events_for(150);

  // A user-typed size sticks for the next new layer.
  patchy::ui::MainWindowTestAccess::document(window).clear_active_layer();
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  size_spin->setValue(20.0);
  QApplication::processEvents();
  patchy::ui::MainWindowTestAccess::add_text_at(window, QPoint(2400, 2400));
  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  // 20 pt on this document's ppi: whatever that is in pixels, it is the typed size, not the
  // automatic one.
  const auto typed_size = editor->property("patchy.documentTextSize").toInt();
  std::printf("  typed 20 pt -> %d px\n", typed_size);
  std::fflush(stdout);
  CHECK(typed_size > 0 && typed_size < 300);
  CHECK(std::abs(size_spin->value() - 20.0) < 0.5);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  process_events_for(150);
}

// A fresh session always starts horizontal. With no session open the toggle reads the text
// layers selected in the Layers panel (the layers a click would convert, GitHub issue 31), even
// when the document has no active layer; only a real deselect (Select > Deselect Layers) makes
// it read the one-shot default for the next new layer, which is horizontal.
void ui_new_text_starts_horizontal_after_vertical_layer() {
  register_test_fonts(TestFontRole::UiDefault);
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  canvas->set_primary_color(QColor(0, 0, 0));
  auto& live_document = patchy::ui::MainWindowTestAccess::document(window);
  const auto guard = vertical_toggle_guard(window);
  const auto created = create_vertical_layer_with_tool(window, *canvas, QPoint(200, 40), QStringLiteral("Hi"),
                                                       QStringLiteral("Arial"));
  CHECK(created.id.has_value());
  auto* toggle = window.findChild<QPushButton*>(QStringLiteral("textOrientationButton"));
  CHECK(toggle != nullptr);
  if (!created.id.has_value() || toggle == nullptr) {
    return;
  }
  const auto* vertical_layer = live_document.find_layer(*created.id);
  CHECK(vertical_layer != nullptr && layer_is_vertical_metadata(*vertical_layer));

  // Clearing only the document's active layer leaves the vertical layer's row selected, so it
  // is still the options-bar target and the toggle reads vertical. A click on empty canvas
  // still opens a NEW session, and that session is horizontal: a new layer never inherits the
  // selected layer's orientation.
  live_document.clear_active_layer();
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  CHECK(toggle->isChecked());
  patchy::ui::MainWindowTestAccess::add_text_at(window, QPoint(40, 200));
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  CHECK(editor->property("patchy.documentTextOrientation").toString().isEmpty());
  CHECK(editor->property("patchy.editingLayerId").toULongLong() != *created.id);
  CHECK(!toggle->isChecked());
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  process_events_for(150);
  vertical_layer = live_document.find_layer(*created.id);
  CHECK(vertical_layer != nullptr && layer_is_vertical_metadata(*vertical_layer));

  // Select > Deselect Layers clears the panel rows and the active layer together: nothing is
  // targeted, the toggle reads horizontal, and the next new session is horizontal.
  auto* deselect_layers = window.findChild<QAction*>(QStringLiteral("selectDeselectLayersAction"));
  CHECK(deselect_layers != nullptr);
  if (deselect_layers == nullptr) {
    return;
  }
  deselect_layers->trigger();
  QApplication::processEvents();
  CHECK(!live_document.active_layer_id().has_value());
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  CHECK(!toggle->isChecked());
  patchy::ui::MainWindowTestAccess::add_text_at(window, QPoint(40, 300));
  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  CHECK(editor->property("patchy.documentTextOrientation").toString().isEmpty());
  CHECK(!toggle->isChecked());
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  process_events_for(150);
  vertical_layer = live_document.find_layer(*created.id);
  CHECK(vertical_layer != nullptr && layer_is_vertical_metadata(*vertical_layer));
}

// The IME candidate list is placed from Qt::ImCursorRectangle. QTextEdit's own answer for a
// vertical session is its internal horizontal line at the widget's top, so the Windows Japanese
// IME's list covered the column being typed. The query now answers the drawn caret's column,
// from the caret down to the widget's bottom, so the list opens below the text.
void ui_vertical_text_input_method_rect_excludes_the_column() {
  register_test_fonts(TestFontRole::UiDefault);
  const auto family = japanese_test_family();
  if (!family.has_value()) {
    return;
  }
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  canvas->set_primary_color(QColor(0, 0, 0));
  const auto guard = vertical_toggle_guard(window);
  const auto created = create_vertical_layer_with_tool(
      window, *canvas, QPoint(200, 40), QString::fromUtf8("\xe3\x81\x82\xe3\x81\x84\xe3\x81\x86"), *family);
  CHECK(created.id.has_value());
  if (!created.id.has_value()) {
    return;
  }
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto hit = canvas->widget_position_for_document_point(QPoint(200, 48));
  send_mouse(*canvas, QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  process_events_for(250);
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  CHECK(editor->textCursor().position() == 0);
  const auto caret = editor->property("patchy.previewCaretRect").toRect();
  CHECK(!caret.isEmpty());
  QInputMethodQueryEvent query(Qt::ImCursorRectangle);
  QApplication::sendEvent(editor, &query);
  const auto ime = query.value(Qt::ImCursorRectangle).toRect();
  std::printf("  caret %d,%d %dx%d  ime %d,%d %dx%d  stock %d,%d %dx%d\n", caret.x(), caret.y(), caret.width(),
              caret.height(), ime.x(), ime.y(), ime.width(), ime.height(), editor->cursorRect().x(),
              editor->cursorRect().y(), editor->cursorRect().width(), editor->cursorRect().height());
  std::fflush(stdout);
  // Same column as the drawn caret, starting at the caret ...
  CHECK(std::abs(ime.x() - caret.x()) <= 1);
  CHECK(std::abs(ime.width() - caret.width()) <= 1);
  CHECK(std::abs(ime.y() - caret.y()) <= 1);
  // ... and reaching past the three 32 px cells below it, so the candidate list opens under them.
  CHECK(ime.bottom() >= caret.y() + 90);
  CHECK(ime.bottom() <= editor->viewport()->rect().bottom());
  // Not the stock answer: QTextEdit's internal line sits at the widget's top-left.
  CHECK(ime != editor->cursorRect());

  // The rect follows the caret down the column.
  send_key(*editor, Qt::Key_Down);
  QApplication::processEvents();
  QInputMethodQueryEvent second_query(Qt::ImCursorRectangle);
  QApplication::sendEvent(editor, &second_query);
  const auto second = second_query.value(Qt::ImCursorRectangle).toRect();
  CHECK(std::abs(second.y() - caret.y() - 32) <= 3);
  CHECK(std::abs(second.x() - caret.x()) <= 1);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  process_events_for(200);
}

// The IME composition (Qt's preedit string, not document text) used to be invisible in a
// previewed session: the render documents came from the QTextDocument alone, so a user typing
// kana saw nothing until the IME committed. The composition now previews at the cursor, moves
// the drawn caret, survives a commit that interrupts it, and clears when the IME commits.
void ui_text_ime_composition_previews_and_commits() {
  register_test_fonts(TestFontRole::UiDefault);
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  canvas->set_primary_color(QColor(0, 0, 0));
  auto& live_document = patchy::ui::MainWindowTestAccess::document(window);
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  auto* font_combo = window.findChild<QFontComboBox*>(QStringLiteral("textFontCombo"));
  CHECK(font_combo != nullptr);
  if (font_combo == nullptr) {
    return;
  }
  font_combo->setCurrentFont(QFont(QStringLiteral("Arial")));
  patchy::ui::MainWindowTestAccess::add_text_at(window, QPoint(40, 60));
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  editor->clear();
  editor->insertPlainText(QStringLiteral("hey"));
  process_events_for(250);
  const auto dark_pixels = [&] {
    const auto image = canvas->grab().toImage();
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        if (qGray(image.pixel(x, y)) < 96) {
          ++count;
        }
      }
    }
    return count;
  };
  const auto caret_before = editor->property("patchy.previewCaretRect").toRect();
  const auto ink_before = dark_pixels();
  CHECK(!caret_before.isEmpty());
  CHECK(ink_before > 0);

  // Composing "abc" with the IME cursor at its end.
  QList<QInputMethodEvent::Attribute> attributes;
  attributes.push_back(QInputMethodEvent::Attribute(QInputMethodEvent::Cursor, 3, 1, QVariant()));
  QInputMethodEvent compose(QStringLiteral("abc"), attributes);
  QApplication::sendEvent(editor, &compose);
  process_events_for(250);
  CHECK(editor->toPlainText() == QStringLiteral("hey"));
  CHECK(editor->property("patchy.preeditText").toString() == QStringLiteral("abc"));
  const auto caret_composing = editor->property("patchy.previewCaretRect").toRect();
  const auto ink_composing = dark_pixels();
  std::printf("  caret %d -> %d, ink %d -> %d\n", caret_before.x(), caret_composing.x(), ink_before, ink_composing);
  std::fflush(stdout);
  // The composition is drawn (more ink) and the caret sits after it (three glyphs right).
  CHECK(ink_composing > ink_before + 40);
  CHECK(caret_composing.x() > caret_before.x() + 30);

  // The IME cursor inside the composition moves the drawn caret back.
  QList<QInputMethodEvent::Attribute> mid_attributes;
  mid_attributes.push_back(QInputMethodEvent::Attribute(QInputMethodEvent::Cursor, 1, 1, QVariant()));
  QInputMethodEvent compose_mid(QStringLiteral("abc"), mid_attributes);
  QApplication::sendEvent(editor, &compose_mid);
  process_events_for(250);
  const auto caret_mid = editor->property("patchy.previewCaretRect").toRect();
  CHECK(caret_mid.x() < caret_composing.x() - 10);
  CHECK(caret_mid.x() > caret_before.x());

  // The IME commits: the document takes the text, the composition clears, the ink stays.
  QInputMethodEvent commit(QString(), {});
  commit.setCommitString(QStringLiteral("abc"));
  QApplication::sendEvent(editor, &commit);
  process_events_for(250);
  CHECK(editor->toPlainText() == QStringLiteral("heyabc"));
  CHECK(!editor->property("patchy.preeditText").isValid());
  CHECK(std::abs(dark_pixels() - ink_composing) < 40);

  // A commit that interrupts a composition keeps the composed text (no platform context
  // offscreen, so this exercises the insert fallback).
  QInputMethodEvent compose_again(QStringLiteral("d"), {});
  QApplication::sendEvent(editor, &compose_again);
  process_events_for(250);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  process_events_for(200);
  const auto id = live_document.active_layer_id();
  CHECK(id.has_value());
  if (!id.has_value()) {
    return;
  }
  const auto* layer = live_document.find_layer(*id);
  CHECK(layer != nullptr && patchy::layer_is_text(*layer));
  if (layer != nullptr) {
    CHECK(layer->metadata().at(patchy::kLayerMetadataText) == "heyabcd");
  }
}

// Horizontal sessions answer the drawn caret too (zoomed, so the widget layout and the render
// disagree on where the line is), one line tall like a text field.
void ui_text_input_method_rect_follows_the_drawn_caret() {
  register_test_fonts(TestFontRole::UiDefault);
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(2.0);
  canvas->set_primary_color(QColor(0, 0, 0));
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  patchy::ui::MainWindowTestAccess::add_text_at(window, QPoint(40, 60));
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  editor->insertPlainText(QStringLiteral("hey"));
  process_events_for(250);
  const auto caret = editor->property("patchy.previewCaretRect").toRect();
  CHECK(!caret.isEmpty());
  QInputMethodQueryEvent query(Qt::ImCursorRectangle);
  QApplication::sendEvent(editor, &query);
  const auto ime = query.value(Qt::ImCursorRectangle).toRect();
  std::printf("  caret %d,%d %dx%d  ime %d,%d %dx%d\n", caret.x(), caret.y(), caret.width(), caret.height(), ime.x(),
              ime.y(), ime.width(), ime.height());
  std::fflush(stdout);
  CHECK(ime == caret);
  CHECK(ime.height() > ime.width());
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  process_events_for(200);
}

// Rotated Roman (runs v7 column 14 = 2, Photoshop's Standard Vertical Roman Alignment): "HH"
// stacks two caps 32 px apart when upright; rotated, each H lies on its side, so the column
// is only a cap tall (~23 px) across and the two glyphs follow each other by their horizontal
// advance (~23 px), never the em.
void ui_vertical_text_rotated_roman_lies_along_the_column() {
  register_test_fonts(TestFontRole::UiDefault);
  patchy::Document document(320, 320, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(320, 320, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  const auto add_layer = [&document](const char* name, bool rotated, int left) {
    patchy::Layer layer(document.allocate_layer_id(), name,
                        solid_pixels(48, 80, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));
    layer.set_bounds(patchy::Rect{left, 40, 48, 80});
    layer.metadata()[patchy::kLayerMetadataText] = "HH";
    layer.metadata()[patchy::kLayerMetadataTextFlow] = "point";
    layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
    layer.metadata()[patchy::kLayerMetadataTextSize] = "32";
    layer.metadata()[patchy::kLayerMetadataTextColor] = "#000000";
    layer.metadata()[patchy::kLayerMetadataTextOrientation] = patchy::kTextOrientationVertical;
    layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
    layer.metadata()[patchy::kLayerMetadataTextRuns] =
        rotated ? "v7\n0\t2\t32\t0\t0\t#000000\tArial\tauto\t0\t1\t1\t0\t\t0\t2" : "v1\n0\t2\t32\t0\t0\t#000000\tArial";
    return document.add_layer(std::move(layer)).id();
  };
  const auto upright_id = add_layer("Upright", false, 60);
  const auto rotated_id = add_layer("Rotated", true, 200);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Rotated Roman"));
  QApplication::processEvents();
  auto& live_document = patchy::ui::MainWindowTestAccess::document(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  if (layer_list == nullptr) {
    return;
  }
  struct Ink {
    int width{0};
    int height{0};
    bool valid{false};
  };
  const auto measure = [&](patchy::LayerId id, const QString& row, QPoint click) {
    rerender_through_edit_session(window, *canvas, *layer_list, row, click);
    Ink ink;
    const auto* layer = live_document.find_layer(id);
    if (layer == nullptr) {
      return ink;
    }
    const auto bounds = patchy::visible_alpha_local_bounds(layer->pixels());
    if (!bounds.has_value()) {
      return ink;
    }
    ink.width = bounds->width;
    ink.height = bounds->height;
    ink.valid = true;
    return ink;
  };
  const auto upright = measure(upright_id, QStringLiteral("Upright"), QPoint(84, 60));
  const auto rotated = measure(rotated_id, QStringLiteral("Rotated"), QPoint(224, 60));
  std::printf("  upright ink %dx%d, rotated ink %dx%d\n", upright.width, upright.height, rotated.width, rotated.height);
  std::fflush(stdout);
  CHECK(upright.valid && rotated.valid);
  if (!upright.valid || !rotated.valid) {
    return;
  }
  // Upright: two 32 px cells, each H ~23 px tall and ~21 px wide.
  CHECK(upright.height >= 50 && upright.height <= 60);
  CHECK(upright.width <= 26);
  // Rotated: the column is one cap tall across (H's height, ~23) and two advances long
  // (~2 x 23), clearly shorter than the upright stack.
  CHECK(rotated.width >= 18 && rotated.width <= 26);
  CHECK(rotated.height >= 38 && rotated.height <= 50);
  CHECK(rotated.height < upright.height);
  // The panel's checkbox reads the flag back for the selected layer.
  layer_list->setCurrentItem(require_layer_item(*layer_list, QStringLiteral("Rotated")));
  QApplication::processEvents();
  const auto* rotated_layer = live_document.find_layer(rotated_id);
  CHECK(rotated_layer != nullptr &&
        rotated_layer->metadata().at(patchy::kLayerMetadataTextRuns).rfind("v7\n", 0) == 0);
}

// Photoshop switches the face of characters the current font cannot draw; Patchy used to draw
// them through Qt's silent fallback and store "Arial" for the kana, which Photoshop then
// re-laid out as empty boxes. Typing kana into Arial now commits a Japanese face for those
// characters, as its own run, and the exported type block names that font.
void ui_typing_uncovered_characters_switches_their_font() {
  register_test_fonts(TestFontRole::UiDefault);
  const auto japanese = japanese_test_family();
  if (!japanese.has_value()) {
    return;
  }
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  canvas->set_primary_color(QColor(0, 0, 0));
  auto& live_document = patchy::ui::MainWindowTestAccess::document(window);
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  auto* font_combo = window.findChild<QFontComboBox*>(QStringLiteral("textFontCombo"));
  CHECK(font_combo != nullptr);
  if (font_combo == nullptr) {
    return;
  }
  font_combo->setCurrentFont(QFont(QStringLiteral("Arial")));
  patchy::ui::MainWindowTestAccess::add_text_at(window, QPoint(40, 60));
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  // "hey" + hiragana ko-ni-chi-ha + " man", the way a user types it: one insert per keystroke.
  editor->clear();
  for (const auto ch : QString::fromUtf8("hey \xe3\x81\x93\xe3\x81\xab\xe3\x81\xa1\xe3\x81\xaf man")) {
    editor->insertPlainText(QString(ch));
    QApplication::processEvents();
  }
  process_events_for(250);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  process_events_for(200);
  const auto id = live_document.active_layer_id();
  CHECK(id.has_value());
  if (!id.has_value()) {
    return;
  }
  const auto* layer = live_document.find_layer(*id);
  CHECK(layer != nullptr && patchy::layer_is_text(*layer));
  if (layer == nullptr) {
    return;
  }
  const auto runs = layer->metadata().at(patchy::kLayerMetadataTextRuns);
  std::printf("  runs:\n%s\n", runs.c_str());
  std::fflush(stdout);
  // Two runs: Arial for "hey ", then the Japanese family from the first kana on (Photoshop keeps
  // the switched face for what follows, and so does the typing format here).
  int arial_runs = 0;
  int japanese_runs = 0;
  for (const auto& line : QString::fromStdString(runs).split(QLatin1Char('\n'))) {
    const auto fields = line.split(QLatin1Char('\t'));
    if (fields.size() < 7) {
      continue;
    }
    const auto family = QString::fromUtf8(QByteArray::fromPercentEncoding(fields[6].toLatin1()));
    if (family == QStringLiteral("Arial")) {
      ++arial_runs;
    } else if (QFontDatabase::families(QFontDatabase::Japanese).contains(family)) {
      ++japanese_runs;
      CHECK(fields[1].toInt() == 8);  // the kana and the " man" typed after them (the typing format follows)
    }
  }
  CHECK(arial_runs == 1);
  CHECK(japanese_runs == 1);
  // The PSD names two fonts, Arial and the substitute. Kept as an artifact for the Photoshop
  // re-render check by COM (the headless build sees no system fonts, so only this offscreen
  // run with a registered Japanese face produces a representative file).
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(live_document);
  {
    std::filesystem::create_directories("test-artifacts");
    std::ofstream out("test-artifacts/mixed_kana_check.psd", std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  const std::string haystack(bytes.begin(), bytes.end());
  CHECK(haystack.find("A\0r\0i\0a\0l\0M\0T", 0) != std::string::npos || haystack.find("ArialMT") != std::string::npos);
  CHECK(haystack.find("\0G\0o\0t\0h\0i\0c", 0) != std::string::npos || haystack.find("Gothic") != std::string::npos);
}

// Scripting: the same run/backlog helpers scripting_tests.cpp uses.
bool run_script_to_end(patchy::ui::MainWindow& window, const QString& source) {
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("vertical-test-script");
  (void)host.run_source(source, std::move(options));
  QElapsedTimer timer;
  timer.start();
  while (host.run_active() && timer.elapsed() < 15000) {
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
  }
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
  CHECK(!host.run_active());
  return !host.last_run_had_error();
}

bool backlog_has(patchy::ui::MainWindow& window, const QString& needle) {
  for (const auto& line : window.script_engine_host().message_backlog()) {
    if (line.contains(needle)) {
      return true;
    }
  }
  return false;
}

void ui_script_add_text_layer_vertical_and_rtl_options() {
  register_test_fonts(TestFontRole::UiDefault);
  patchy::ui::MainWindow window;
  show_window(window);
  auto& live_document = patchy::ui::MainWindowTestAccess::document(window);
  auto* toggle = window.findChild<QPushButton*>(QStringLiteral("textOrientationButton"));
  std::printf("  orientation toggle at start: %s\n",
              toggle != nullptr && toggle->isChecked() ? "vertical" : "horizontal");
  std::fflush(stdout);
  if (toggle != nullptr && toggle->isChecked()) {
    toggle->click();
    QApplication::processEvents();
  }
  CHECK(run_script_to_end(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var v = doc.addTextLayer('Hello', {font: 'Arial', size: 32, x: 200, y: 40, orientation: 'vertical'});
    console.log('v=' + v.textOrientation + ',' + v.textDirection + ',' + (v.bounds.height > v.bounds.width * 2));
    var h = doc.addTextLayer('abc def', {font: 'Arial', size: 24, x: 20, y: 200, direction: 'rtl'});
    console.log('h=' + h.textOrientation + ',' + h.textDirection);
    h.textDirection = 'ltr';
    console.log('h2=' + h.textDirection);
    v.textOrientation = 'horizontal';
    console.log('v2=' + v.textOrientation + ',' + (v.bounds.width > v.bounds.height));
    var bad = false;
    try { v.textOrientation = 'sideways'; } catch (error) { bad = true; }
    var bad2 = false;
    try { doc.addTextLayer('x', {direction: 'up'}); } catch (error) { bad2 = true; }
    console.log('bad=' + bad + ',' + bad2);
  )JS")));
  for (const auto& line : window.script_engine_host().message_backlog()) {
    if (line.startsWith(QStringLiteral("v=")) || line.startsWith(QStringLiteral("h")) ||
        line.startsWith(QStringLiteral("bad="))) {
      std::printf("  backlog: %s\n", line.toUtf8().constData());
    }
  }
  std::fflush(stdout);
  CHECK(backlog_has(window, QStringLiteral("v=vertical,auto,true")));
  CHECK(backlog_has(window, QStringLiteral("h=horizontal,rtl")));
  CHECK(backlog_has(window, QStringLiteral("h2=ltr")));
  CHECK(backlog_has(window, QStringLiteral("v2=horizontal,true")));
  CHECK(backlog_has(window, QStringLiteral("bad=true,true")));
  // Scripted orientation never touches the options-bar default.
  CHECK(toggle != nullptr && !toggle->isChecked());
  int text_layers = 0;
  for (const auto& layer : live_document.layers()) {
    if (patchy::layer_is_text(layer)) {
      ++text_layers;
    }
  }
  CHECK(text_layers == 2);
}

}  // namespace

std::vector<patchy::test::TestCase> text_vertical_rtl_tests() {
  return {
      {"ui_vertical_text_lays_out_columns_top_to_bottom_right_to_left",
       ui_vertical_text_lays_out_columns_top_to_bottom_right_to_left},
      {"ui_vertical_text_matches_photoshop_capture", ui_vertical_text_matches_photoshop_capture},
      {"ui_vertical_text_caret_and_selection_follow_the_columns", ui_vertical_text_caret_and_selection_follow_the_columns},
      {"ui_vertical_text_recommit_keeps_origin_and_round_trips_psd",
       ui_vertical_text_recommit_keeps_origin_and_round_trips_psd},
      {"ui_text_orientation_toggle_converts_layer_and_undoes", ui_text_orientation_toggle_converts_layer_and_undoes},
      {"ui_rtl_hebrew_paragraph_direction_reorders_and_persists", ui_rtl_hebrew_paragraph_direction_reorders_and_persists},
      {"ui_script_add_text_layer_vertical_and_rtl_options", ui_script_add_text_layer_vertical_and_rtl_options},
      {"ui_text_character_panel_leading_unlocks_and_applies", ui_text_character_panel_leading_unlocks_and_applies},
      {"ui_new_text_size_scales_with_the_document", ui_new_text_size_scales_with_the_document},
      {"ui_new_text_starts_horizontal_after_vertical_layer", ui_new_text_starts_horizontal_after_vertical_layer},
      {"ui_vertical_text_rotated_roman_lies_along_the_column", ui_vertical_text_rotated_roman_lies_along_the_column},
      {"ui_vertical_text_input_method_rect_excludes_the_column", ui_vertical_text_input_method_rect_excludes_the_column},
      {"ui_text_input_method_rect_follows_the_drawn_caret", ui_text_input_method_rect_follows_the_drawn_caret},
      {"ui_text_ime_composition_previews_and_commits", ui_text_ime_composition_previews_and_commits},
      {"ui_typing_uncovered_characters_switches_their_font", ui_typing_uncovered_characters_switches_their_font},
  };
}
