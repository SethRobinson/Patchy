// The on-canvas text edit session: the layer transform it carries through entry and commit
// (GitHub issue 373), the caret against the drawn line (384), the text box outline (385, 371),
// the options-bar size round trip (371), a held Enter and the wheel over the box (413), Esc
// on the untouched placeholder (415) and the Size scrub with its move coalescing (700). The
// reporter's sample is a local fixture
// (local-test-fixtures/psd/text-rotate-issue373.psd); the committed-fixture tests build the
// same shapes from photoshop-text-box-auto-leading.psd so they run everywhere.
#include "core/layer_metadata.hpp"
#include "psd/psd_document_io.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/main_window.hpp"
#include "ui/main_window_shared.hpp"
#include "local_psd_fixtures.hpp"
#include "test_fonts.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include <QAction>
#include <QApplication>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPolygonF>
#include <QScrollBar>
#include <QSpinBox>
#include <QWidget>
#include <QTextCursor>
#include <QTextEdit>
#include <QToolButton>
#include <QTransform>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

using namespace patchy::test::ui;

namespace {

std::optional<patchy::LayerId> find_layer_id(const patchy::Document& document,
                                             const std::function<bool(const patchy::Layer&)>& match) {
  std::optional<patchy::LayerId> found;
  std::function<void(const std::vector<patchy::Layer>&)> walk = [&](const std::vector<patchy::Layer>& layers) {
    for (const auto& layer : layers) {
      if (!found && match(layer)) {
        found = layer.id();
      }
      walk(layer.children());
    }
  };
  walk(document.layers());
  return found;
}

std::optional<patchy::LayerId> find_layer_named(const patchy::Document& document, const std::string& name) {
  return find_layer_id(document, [&](const patchy::Layer& layer) { return layer.name() == name; });
}

// The transform the layer renders through: Patchy's own once it has one, else the import's.
std::optional<patchy::LayerAffineTransform> effective_text_transform(const patchy::Layer& layer) {
  for (const auto* key : {patchy::kLayerMetadataTextTransform, patchy::kLayerMetadataPsdTextTransform}) {
    if (const auto found = layer.metadata().find(key); found != layer.metadata().end()) {
      if (auto parsed = patchy::parse_layer_affine_transform(found->second); parsed.has_value()) {
        return parsed;
      }
    }
  }
  return std::nullopt;
}

std::optional<QRect> document_ink_rect(const patchy::Layer& layer) {
  const auto bounds = alpha_pixel_bounds_in_rows(layer.pixels(), 0, layer.pixels().height());
  if (!bounds.has_value()) {
    return std::nullopt;
  }
  return bounds->translated(layer.bounds().x, layer.bounds().y);
}

QTextEdit* live_editor(patchy::ui::CanvasWidget& canvas) {
  return canvas.findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
}

QTextEdit* open_session(patchy::ui::MainWindow& window, patchy::ui::CanvasWidget& canvas, patchy::LayerId id) {
  accept_missing_psd_text_font_warning_if_present();
  patchy::ui::MainWindowTestAccess::edit_text_layer(window, id);
  QApplication::processEvents();
  process_events_for(350);
  auto* editor = live_editor(canvas);
  if (editor != nullptr) {
    auto cursor = editor->textCursor();
    cursor.movePosition(QTextCursor::End);
    editor->setTextCursor(cursor);
    QApplication::processEvents();
    process_events_for(150);
  }
  return editor;
}

void commit_session(patchy::ui::CanvasWidget& canvas) {
  if (auto* editor = live_editor(canvas); editor != nullptr) {
    send_key(*editor, Qt::Key_Return, Qt::ControlModifier);
    QApplication::processEvents();
    process_events_for(250);
  }
}

void print_transform(const char* label, const patchy::Layer& layer) {
  const auto transform = effective_text_transform(layer);
  const auto ink = document_ink_rect(layer);
  std::printf("  [%s] bounds (%d,%d %dx%d)", label, layer.bounds().x, layer.bounds().y, layer.bounds().width,
              layer.bounds().height);
  if (transform.has_value()) {
    std::printf(" transform [%.5f %.5f %.5f %.5f %.3f %.3f]", (*transform)[0], (*transform)[1], (*transform)[2],
                (*transform)[3], (*transform)[4], (*transform)[5]);
  }
  if (ink.has_value()) {
    std::printf(" ink (%d,%d %dx%d)", ink->x(), ink->y(), ink->width(), ink->height());
  }
  std::printf("\n");
}

bool same_transform(const patchy::LayerAffineTransform& a, const patchy::LayerAffineTransform& b,
                    double translation_tolerance) {
  for (int index = 0; index < 4; ++index) {
    if (std::abs(a[index] - b[index]) > 0.0001) {
      return false;
    }
  }
  return std::abs(a[4] - b[4]) <= translation_tolerance && std::abs(a[5] - b[5]) <= translation_tolerance;
}

bool same_rect_within(const QRect& a, const QRect& b, int tolerance) {
  return std::abs(a.left() - b.left()) <= tolerance && std::abs(a.top() - b.top()) <= tolerance &&
         std::abs(a.right() - b.right()) <= tolerance && std::abs(a.bottom() - b.bottom()) <= tolerance;
}

struct SessionWindow {
  patchy::ui::MainWindow window;
  patchy::ui::CanvasWidget* canvas{nullptr};
};

// The caret as drawn, in canvas widget coordinates: the transformed overlay publishes its
// polygon, the flat editor its viewport rect.
QPolygonF caret_canvas_polygon(patchy::ui::CanvasWidget& canvas, QTextEdit& editor) {
  QPolygonF polygon;
  if (editor.property("patchy.transformedPreviewOverlayActive").toBool()) {
    if (auto* overlay = canvas.findChild<QWidget*>(QStringLiteral("transformedTextEditOverlay")); overlay != nullptr) {
      for (const auto& point : overlay->property("patchy.transformedTextCaretPolygon").toList()) {
        polygon << point.toPointF();
      }
    }
    return polygon;
  }
  const auto caret = editor.property("patchy.previewCaretRect").toRect();
  if (caret.isEmpty()) {
    return polygon;
  }
  const auto top_left = editor.viewport()->mapTo(&canvas, caret.topLeft());
  return QPolygonF(QRectF(QPointF(top_left), QSizeF(caret.size())));
}

// The live preview's ink, in canvas widget coordinates.
std::optional<QRect> preview_ink_canvas_rect(SessionWindow& session, QTextEdit& editor) {
  auto& document = patchy::ui::MainWindowTestAccess::document(session.window);
  auto* preview = preview_layer_for_editor(document, editor);
  if (preview == nullptr) {
    return std::nullopt;
  }
  const auto ink = document_ink_rect(*preview);
  if (!ink.has_value()) {
    return std::nullopt;
  }
  const auto top_left = session.canvas->widget_position_for_document_point(ink->topLeft());
  const auto bottom_right = session.canvas->widget_position_for_document_point(ink->bottomRight() + QPoint(1, 1));
  return QRect(top_left, bottom_right - QPoint(1, 1));
}

void print_caret_against_ink(SessionWindow& session, QTextEdit& editor, const char* label) {
  const auto caret = caret_canvas_polygon(*session.canvas, editor).boundingRect();
  const auto ink = preview_ink_canvas_rect(session, editor);
  std::printf("  [%s caret] caret (%.1f,%.1f %.1fx%.1f)", label, caret.x(), caret.y(), caret.width(), caret.height());
  if (ink.has_value()) {
    std::printf(" preview ink (%d,%d %dx%d)", ink->x(), ink->y(), ink->width(), ink->height());
  }
  QFont font(editor.property("patchy.documentTextFamily").toString());
  font.setPixelSize(std::max(1, editor.property("patchy.documentTextSize").toInt()));
  const QFontMetricsF metrics(font);
  std::printf(" font '%s' %dpx ascent %.1f descent %.1f cap %.1f", QFontInfo(font).family().toStdString().c_str(),
              font.pixelSize(), metrics.ascent(), metrics.descent(), metrics.capHeight());
  if (ink.has_value()) {
    // Capitals only: the ink bottom is the baseline (plus round-letter overshoot).
    std::printf(" | caret top %.1f above ink top, bottom %.1f below ink bottom", ink->top() - caret.top(),
                caret.bottom() - (ink->bottom() + 1));
  }
  std::printf("\n");
}

void open_document(SessionWindow& session, patchy::Document document, const QString& title) {
  show_window(session.window);
  session.window.add_document_session(std::move(document), title);
  session.canvas = require_canvas(session.window);
  session.canvas->set_zoom(1.0);
  QApplication::processEvents();
}

// Enter and commit three times (twice without a change, then after typing), checking that the text
// keeps the transform and placement it had: a rotated box once came back unrotated, and the
// next edit restored the rotation at the bounding box corner instead of the box origin.
void check_edit_keeps_transform(SessionWindow& session, patchy::LayerId id, const char* label,
                                bool compare_with_source_ink, bool point_text) {
  auto& document = patchy::ui::MainWindowTestAccess::document(session.window);
  const auto* source = std::as_const(document).find_layer(id);
  CHECK(source != nullptr);
  if (source == nullptr) {
    return;
  }
  print_transform(label, *source);
  const auto original_transform = effective_text_transform(*source);
  const auto original_ink = document_ink_rect(*source);
  CHECK(original_transform.has_value());
  CHECK(original_ink.has_value());
  if (!original_transform.has_value() || !original_ink.has_value()) {
    return;
  }

  auto* editor = open_session(session.window, *session.canvas, id);
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  // The session shows the text the way it sits: rotated text edits rotated.
  CHECK(editor->property("patchy.transformedPreviewOverlayActive").toBool());
  commit_session(*session.canvas);
  const auto* first = std::as_const(document).find_layer(id);
  CHECK(first != nullptr);
  if (first == nullptr) {
    return;
  }
  print_transform("after first commit", *first);
  const auto first_transform = effective_text_transform(*first);
  const auto first_ink = document_ink_rect(*first);
  // A box keeps its origin. Point text keeps its rotation and ink, but its first commit moves
  // the stored origin from Photoshop's baseline anchor to Patchy's line top.
  CHECK(first_transform.has_value() &&
        same_transform(*first_transform, *original_transform, point_text ? 1.0e9 : 0.51));
  CHECK(first_ink.has_value());
  if (!first_transform.has_value() || !first_ink.has_value()) {
    return;
  }
  if (compare_with_source_ink) {
    // Patchy's own render of Photoshop's text: the same place within a few pixels. A
    // substituted face (the sample's Adobe Hebrew) runs to another length, so point text
    // compares the corner its line starts from (bottom left: the sample climbs to the right).
    if (point_text) {
      CHECK(std::abs(first_ink->left() - original_ink->left()) <= 6 &&
            std::abs(first_ink->bottom() - original_ink->bottom()) <= 6);
    } else {
      CHECK(same_rect_within(*first_ink, *original_ink, 6));
    }
  }

  editor = open_session(session.window, *session.canvas, id);
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  CHECK(editor->property("patchy.transformedPreviewOverlayActive").toBool());
  commit_session(*session.canvas);
  const auto* second = std::as_const(document).find_layer(id);
  CHECK(second != nullptr);
  if (second == nullptr) {
    return;
  }
  print_transform("after second commit", *second);
  const auto second_transform = effective_text_transform(*second);
  const auto second_ink = document_ink_rect(*second);
  CHECK(second_transform.has_value() && same_transform(*second_transform, *first_transform, 0.0001));
  CHECK(second_ink.has_value() && same_rect_within(*second_ink, *first_ink, 0));

  // A real edit: type at the end of the text and commit. The text grows along its own line;
  // the transform does not move.
  editor = open_session(session.window, *session.canvas, id);
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  QKeyEvent press(QEvent::KeyPress, Qt::Key_S, Qt::NoModifier, QStringLiteral("s"));
  QApplication::sendEvent(editor, &press);
  QKeyEvent release(QEvent::KeyRelease, Qt::Key_S, Qt::NoModifier, QStringLiteral("s"));
  QApplication::sendEvent(editor, &release);
  QApplication::processEvents();
  process_events_for(250);
  commit_session(*session.canvas);
  const auto* typed = std::as_const(document).find_layer(id);
  CHECK(typed != nullptr);
  if (typed == nullptr) {
    return;
  }
  print_transform("after typing", *typed);
  const auto typed_transform = effective_text_transform(*typed);
  CHECK(typed_transform.has_value() && same_transform(*typed_transform, *first_transform, 0.0001));
  const auto text = typed->metadata().find(patchy::kLayerMetadataText);
  CHECK(text != typed->metadata().end() && !text->second.empty() && text->second.back() == 's');
}

void ui_rotated_psd_box_text_edit_keeps_transform() {
  // photoshop-text-box-auto-leading.psd's paragraph box, turned the way the issue 373 sample
  // is (about -14 degrees) in both the import's transform and Patchy's, as a rotated
  // Photoshop paragraph imports.
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  auto document =
      patchy::psd::DocumentIo::read_file(patchy::test::committed_psd_fixture_path("photoshop-text-box-auto-leading.psd"));
  const auto id = find_layer_id(document, [](const patchy::Layer& layer) {
    return patchy::layer_is_text(layer) && layer.metadata().contains(patchy::kLayerMetadataPsdTextBoxBounds);
  });
  CHECK(id.has_value());
  if (!id.has_value()) {
    return;
  }
  auto* layer = document.find_layer(*id);
  QTransform rotated;
  rotated.translate(layer->bounds().x + 20.25, layer->bounds().y + 60.5);
  rotated.rotate(-14.26);
  const auto affine = patchy::serialize_layer_affine_transform(
      patchy::LayerAffineTransform{rotated.m11(), rotated.m12(), rotated.m21(), rotated.m22(), rotated.dx(), rotated.dy()});
  layer->metadata()[patchy::kLayerMetadataTextTransform] = affine;
  layer->metadata()[patchy::kLayerMetadataPsdTextTransform] = affine;

  SessionWindow session;
  open_document(session, std::move(document), QStringLiteral("Rotated PSD Box"));
  check_edit_keeps_transform(session, *id, "rotated box", false, false);
}

void ui_text_rotate_sample_edits_keep_transform_if_available() {
  // The issue 373 sample: a rotated paragraph box (Arial) and rotated point text.
  const auto path = patchy::test::local_psd_fixture_path("text-rotate-issue373.psd");
  patchy::test::skip_unless_exists(path);
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  auto document = patchy::psd::DocumentIo::read_file(path);
  const auto box = find_layer_named(document, "Arial - Rotated Text Area");
  const auto point = find_layer_named(document, "Adobe Hebew - Rotated -15");
  CHECK(box.has_value());
  CHECK(point.has_value());
  if (!box.has_value() || !point.has_value()) {
    return;
  }
  SessionWindow session;
  open_document(session, std::move(document), QStringLiteral("Text Rotate"));
  check_edit_keeps_transform(session, *box, "sample rotated box", true, false);
  check_edit_keeps_transform(session, *point, "sample rotated point", true, true);
}

// Canvas captures of each of the sample's three layers mid-session, for the issue cards.
void ui_text_rotate_sample_session_captures_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("text-rotate-issue373.psd");
  patchy::test::skip_unless_exists(path);
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  const auto flash_time = QApplication::cursorFlashTime();
  QApplication::setCursorFlashTime(0);
  auto document = patchy::psd::DocumentIo::read_file(path);
  const std::vector<std::pair<std::string, std::string>> layers = {
      {"Myriad Pro", "tcs_point"},
      {"Adobe Hebew - Rotated -15", "tcs_rotated_point"},
      {"Arial - Rotated Text Area", "tcs_rotated_box"},
  };
  std::vector<patchy::LayerId> ids;
  for (const auto& [name, _] : layers) {
    const auto id = find_layer_named(document, name);
    CHECK(id.has_value());
    ids.push_back(id.value_or(0));
  }
  SessionWindow session;
  open_document(session, std::move(document), QStringLiteral("Text Rotate"));
  session.window.resize(1100, 800);
  QApplication::processEvents();
  for (std::size_t index = 0; index < layers.size(); ++index) {
    auto* editor = open_session(session.window, *session.canvas, ids[index]);
    CHECK(editor != nullptr);
    if (editor == nullptr) {
      continue;
    }
    // Caret in the middle of the line.
    auto cursor = editor->textCursor();
    cursor.setPosition(5);
    editor->setTextCursor(cursor);
    session.window.activateWindow();
    editor->setFocus(Qt::OtherFocusReason);
    QApplication::processEvents();
    process_events_for(200);
    print_caret_against_ink(session, *editor, layers[index].second.c_str());
    save_widget_artifact(layers[index].second + "_session", *session.canvas);
    if (index == 0) {
      // The text color window, for issue 371's question about its buttons.
      if (auto* color = session.window.findChild<QToolButton*>(QStringLiteral("textColorButton")); color != nullptr) {
        color->click();
        QApplication::processEvents();
        process_events_for(150);
        if (auto* panel = find_top_level_dialog(QStringLiteral("patchyColorDialog")); panel != nullptr) {
          save_widget_artifact("tcs_text_color_panel", *panel);
          panel->close();
          QApplication::processEvents();
          process_events_for(100);
        }
      }
      editor = live_editor(*session.canvas);
      if (editor == nullptr) {
        continue;
      }
    }
    commit_session(*session.canvas);
    save_widget_artifact(layers[index].second + "_committed", *session.canvas);
    auto* reopened = open_session(session.window, *session.canvas, ids[index]);
    if (reopened != nullptr) {
      save_widget_artifact(layers[index].second + "_second_session", *session.canvas);
      commit_session(*session.canvas);
    }
  }
  QApplication::setCursorFlashTime(flash_time);
}

void ui_text_size_points_round_trip_at_common_resolutions() {
  // Sizes are whole document pixels shown as points. Every resolution must show a whole-point
  // size it can hold as that whole number, and whatever it shows must name the same pixels
  // (GitHub issue 371: 50 pt came back as 50.26 pt at 3779 pixels per meter, 95.99 ppi).
  patchy::Document document(64, 64, patchy::PixelFormat::rgba8());
  for (const double ppi : {72.0, 96.0, 3779.0 * 0.0254, 150.0, 300.0, 600.0}) {
    document.print_settings().horizontal_ppi = ppi;
    for (const double points : {6.0, 8.0, 9.0, 10.0, 11.0, 12.0, 12.5, 14.0, 18.0, 24.0, 36.0, 48.0, 50.0, 72.0, 144.0}) {
      const int pixels = patchy::ui::text_points_to_pixels(points, document);
      const double shown = patchy::ui::text_pixels_to_points(pixels, document);
      CHECK(patchy::ui::text_points_to_pixels(shown, document) == pixels);
      // The size typed stays what the field shows.
      CHECK(patchy::ui::text_points_for_display(points, pixels, document) == points);
      if (points == std::round(points)) {
        if (std::abs(shown - points) > 1e-9) {
          std::printf("  %.2f ppi: %.2f pt -> %d px shows %.4f pt\n", ppi, points, pixels, shown);
        }
        CHECK(std::abs(shown - points) < 1e-9);
      }
    }
    // Every pixel size shows with at most two decimals and comes back to itself.
    for (int pixels = 1; pixels <= 400; ++pixels) {
      const double shown = patchy::ui::text_pixels_to_points(pixels, document);
      CHECK(patchy::ui::text_points_to_pixels(shown, document) == pixels);
      CHECK(std::abs(shown * 100.0 - std::round(shown * 100.0)) < 1e-6);
    }
  }
}

void ui_text_size_field_keeps_typed_points() {
  // The reporter's case end to end: a 3779 pixels-per-meter document, 50 typed into the size
  // field during a session, the caret moved (which re-reads the bar from the editor), then the
  // commit and the bar mirroring the layer.
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  patchy::Document document(400, 240, patchy::PixelFormat::rgba8());
  document.print_settings().horizontal_ppi = 3779.0 * 0.0254;
  document.print_settings().vertical_ppi = 3779.0 * 0.0254;
  SessionWindow session;
  open_document(session, std::move(document), QStringLiteral("Size Round Trip"));
  require_action_by_text(session.window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  patchy::ui::MainWindowTestAccess::add_text_at(session.window, QPoint(40, 60));
  QApplication::processEvents();
  process_events_for(200);
  auto* editor = live_editor(*session.canvas);
  auto* size = session.window.findChild<QDoubleSpinBox*>(QStringLiteral("textSizeSpin"));
  CHECK(editor != nullptr);
  CHECK(size != nullptr);
  if (editor == nullptr || size == nullptr) {
    return;
  }
  size->setValue(50.0);
  QApplication::processEvents();
  process_events_for(200);
  auto cursor = editor->textCursor();
  cursor.movePosition(QTextCursor::End);
  editor->setTextCursor(cursor);
  cursor.movePosition(QTextCursor::Start);
  editor->setTextCursor(cursor);
  QApplication::processEvents();
  std::printf("  size field after the caret moved: %s\n", size->text().toStdString().c_str());
  CHECK(std::abs(size->value() - 50.0) < 1e-9);
  CHECK(size->text() == QStringLiteral("50 pt"));
  commit_session(*session.canvas);
  auto& document_now = patchy::ui::MainWindowTestAccess::document(session.window);
  const auto text_layer = find_layer_id(document_now, [](const patchy::Layer& layer) { return patchy::layer_is_text(layer); });
  CHECK(text_layer.has_value());
  if (text_layer.has_value()) {
    const auto* layer = std::as_const(document_now).find_layer(*text_layer);
    const auto stored = layer->metadata().find(patchy::kLayerMetadataTextSize);
    CHECK(stored != layer->metadata().end() && stored->second == "67");
  }
  require_action_by_text(session.window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  require_action_by_text(session.window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  process_events_for(100);
  std::printf("  size field mirroring the layer: %s\n", size->text().toStdString().c_str());
  CHECK(size->text() == QStringLiteral("50 pt"));
}

void ui_text_box_outline_shows_on_unrotated_text() {
  // An unrotated session drew no box at all, only the corner handles (GitHub issue 385), and a
  // single blue line vanished over matching ink (issue 371). The outline is a light line under
  // accent dashes along the editor's edge: both tones must be on screen, also where the box
  // runs over other text.
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  patchy::Document document(500, 260, patchy::PixelFormat::rgba8());
  document.print_settings().horizontal_ppi = 72.0;
  document.print_settings().vertical_ppi = 72.0;
  SessionWindow session;
  open_document(session, std::move(document), QStringLiteral("Box Outline"));
  require_action_by_text(session.window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  patchy::ui::MainWindowTestAccess::add_text_at(session.window, QPoint(60, 60));
  QApplication::processEvents();
  process_events_for(300);
  auto* editor = live_editor(*session.canvas);
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  CHECK(!editor->property("patchy.transformedPreviewOverlayActive").toBool());
  const auto grab = session.canvas->grab().toImage().convertToFormat(QImage::Format_ARGB32);
  const auto edge = editor->geometry();
  int light = 0;
  int dark = 0;
  const auto count = [&](int x, int y) {
    if (!grab.rect().contains(x, y)) {
      return;
    }
    const QColor color = grab.pixelColor(x, y);
    if (color.red() > 225 && color.green() > 225 && color.blue() > 225) {
      ++light;
    } else if (color.blue() > 150 && color.red() < 90 && color.blue() - color.red() > 90) {
      ++dark;
    }
  };
  for (int x = edge.left() + 6; x < edge.right() - 6; ++x) {
    count(x, edge.top());
    count(x, edge.bottom());
  }
  for (int y = edge.top() + 6; y < edge.bottom() - 6; ++y) {
    count(edge.left(), y);
    count(edge.right(), y);
  }
  std::printf("  outline pixels on the editor edge: %d light, %d accent dashes\n", light, dark);
  CHECK(light > 20);
  CHECK(dark > 20);
  commit_session(*session.canvas);
}

void ui_text_caret_spans_the_drawn_line() {
  // The caret runs from the line's ascent to its descent around the baseline the glyphs are
  // drawn on. It used to be centred in QTextLine::height(), which counts the font's line gap,
  // so it sat half that gap low (GitHub issue 384: Myriad Pro, a 0.2 em gap). Nanum Gothic
  // uses its typographic metrics with a 0.25 em line gap (75 px at 300 px), and ships in the
  // source tree (third_party/fonts-web), so every platform has it.
  const auto font_id = QFontDatabase::addApplicationFont(QString::fromStdString(
      (patchy::test::source_root_path() / "third_party" / "fonts-web" / "nanum_gothic" / "NanumGothic-Regular.ttf")
          .string()));
  const auto families = QFontDatabase::applicationFontFamilies(font_id);
  CHECK(!families.isEmpty());
  if (families.isEmpty()) {
    return;
  }
  const auto family = families.front();
  QFont font(family);
  font.setPixelSize(300);
  std::printf("  %s at 300 px: line gap %.1f\n", family.toStdString().c_str(), QFontMetricsF(font).leading());
#if !defined(Q_OS_MACOS)
  // CoreText reports no line gap for this font, so on macOS the caret checks below run
  // without one (they still hold there); elsewhere the gap is what this test is about.
  CHECK(QFontMetricsF(font).leading() >= 20.0);
#endif
  const QFontMetricsF metrics(font);
  patchy::Document document(1200, 420, patchy::PixelFormat::rgba8());
  document.print_settings().horizontal_ppi = 72.0;
  document.print_settings().vertical_ppi = 72.0;
  SessionWindow session;
  open_document(session, std::move(document), QStringLiteral("Caret Line"));
  session.window.resize(1400, 900);
  require_action_by_text(session.window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  auto* family_combo = session.window.findChild<QFontComboBox*>(QStringLiteral("textFontCombo"));
  auto* size = session.window.findChild<QDoubleSpinBox*>(QStringLiteral("textSizeSpin"));
  CHECK(family_combo != nullptr && size != nullptr);
  if (family_combo == nullptr || size == nullptr) {
    return;
  }
  family_combo->setCurrentFont(QFont(family));
  size->setValue(300.0);
  QApplication::processEvents();
  patchy::ui::MainWindowTestAccess::add_text_at(session.window, QPoint(40, 40));
  QApplication::processEvents();
  auto* editor = live_editor(*session.canvas);
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  editor->textCursor().insertText(QStringLiteral("HHH"));
  session.window.activateWindow();
  editor->setFocus(Qt::OtherFocusReason);
  QApplication::processEvents();
  process_events_for(400);
  print_caret_against_ink(session, *editor, "caret line");
  const auto flash_time = QApplication::cursorFlashTime();
  QApplication::setCursorFlashTime(0);  // A steady caret for the capture.
  editor->viewport()->update();
  QApplication::processEvents();
  save_widget_artifact("tcs_caret_line", *session.canvas);
  QApplication::setCursorFlashTime(flash_time);
  const auto caret = caret_canvas_polygon(*session.canvas, *editor).boundingRect();
  const auto ink = preview_ink_canvas_rect(session, *editor);
  CHECK(!caret.isEmpty());
  CHECK(ink.has_value());
  if (caret.isEmpty() || !ink.has_value()) {
    return;
  }
  // "H" sits flat on the baseline, so the ink's exclusive bottom is the baseline.
  const double baseline = ink->bottom() + 1.0;
  std::printf("  line gap %.1f: caret top %.1f (baseline - ascent %.1f), bottom %.1f (baseline + descent %.1f)\n",
              metrics.leading(), caret.top(), baseline - metrics.ascent(), caret.bottom(),
              baseline + metrics.descent());
  CHECK(std::abs(caret.top() - (baseline - metrics.ascent())) <= 2.0);
  CHECK(std::abs(caret.bottom() - (baseline + metrics.descent())) <= 2.0);
  commit_session(*session.canvas);
}

// A new session at (60, 60) of a 2000 x 2000 document, so the view can scroll both ways.
QTextEdit* open_new_text_session(SessionWindow& session, const QString& title) {
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  patchy::Document document(2000, 2000, patchy::PixelFormat::rgba8());
  document.print_settings().horizontal_ppi = 72.0;
  document.print_settings().vertical_ppi = 72.0;
  open_document(session, std::move(document), title);
  session.window.resize(1200, 800);
  require_action_by_text(session.window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  patchy::ui::MainWindowTestAccess::add_text_at(session.window, QPoint(60, 60));
  QApplication::processEvents();
  process_events_for(300);
  return live_editor(*session.canvas);
}

void ui_text_held_enter_relayouts_each_repeat() {
  // GitHub issue 413: holding Enter repeats faster than the 33 ms preview debounce, so every
  // repeat pushed the preview back and the glyphs stayed where they were while the caret and
  // the box moved on. Each repeat now draws at once: Enter at the start of the line moves the
  // drawn text down a line per repeat, and the caret keeps its place against the ink.
  SessionWindow session;
  auto* editor = open_new_text_session(session, QStringLiteral("Held Enter"));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  editor->textCursor().insertText(QStringLiteral("./text"));  // replaces the selected placeholder
  auto cursor = editor->textCursor();
  cursor.movePosition(QTextCursor::Start);
  editor->setTextCursor(cursor);
  QApplication::processEvents();
  process_events_for(300);
  auto previous_ink = preview_ink_canvas_rect(session, *editor);
  const auto first_caret = caret_canvas_polygon(*session.canvas, *editor).boundingRect();
  CHECK(previous_ink.has_value());
  CHECK(!first_caret.isEmpty());
  if (!previous_ink.has_value() || first_caret.isEmpty()) {
    commit_session(*session.canvas);
    return;
  }
  const double ink_below_caret = previous_ink->top() - first_caret.top();
  for (int repeat = 1; repeat <= 6; ++repeat) {
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"), true);
    QApplication::sendEvent(editor, &press);
    QApplication::processEvents();  // no wait: the repeat must already be drawn
    const auto blocks = editor->document()->blockCount();
    const bool pending = editor->property("patchy.textPreviewPending").toBool();
    const auto ink = preview_ink_canvas_rect(session, *editor);
    const auto caret = caret_canvas_polygon(*session.canvas, *editor).boundingRect();
    std::printf("  repeat %d: %d lines, preview pending %d, ink top %d, caret top %.1f\n", repeat, blocks,
                pending ? 1 : 0, ink.has_value() ? ink->top() : -1, caret.top());
    CHECK(blocks == repeat + 1);
    CHECK(!pending);
    CHECK(ink.has_value());
    if (!ink.has_value()) {
      break;
    }
    CHECK(ink->top() > previous_ink->top());
    CHECK(std::abs((ink->top() - caret.top()) - ink_below_caret) <= 2.0);
    previous_ink = ink;
  }
  commit_session(*session.canvas);
}

void ui_text_wheel_over_the_editor_moves_the_canvas() {
  // GitHub issue 413: with the pointer inside the text box the wheel did nothing (only Alt+wheel
  // zoomed). It now does exactly what it does over the rest of the canvas: each step is sent
  // once over empty canvas and once over the box, and both must move the view the same way
  // (plain wheel up and down, Ctrl+wheel sideways, Alt+wheel and wheel-zoom mode zoom), with
  // the box following.
  SessionWindow session;
  auto* editor = open_new_text_session(session, QStringLiteral("Wheel Over Text"));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  auto* canvas = session.canvas;
  canvas->set_zoom(0.5);
  QApplication::processEvents();
  process_events_for(300);
  const auto origin = [&] { return canvas->widget_position_for_document_point(QPoint(0, 0)); };
  const auto inside = [&] { return editor->viewport()->rect().center(); };
  const QPoint outside(canvas->width() - 40, canvas->height() - 40);
  CHECK(!editor->geometry().contains(outside));
  canvas->set_wheel_zooms(false);  // pans move the view the same distance wherever the pointer is

  for (const auto modifiers : {Qt::KeyboardModifiers(Qt::NoModifier), Qt::KeyboardModifiers(Qt::ControlModifier)}) {
    const auto label = modifiers == Qt::NoModifier ? "plain" : "Ctrl";
    auto before = origin();
    send_wheel(*canvas, outside, -120, modifiers);
    QApplication::processEvents();
    const auto on_canvas = origin() - before;
    before = origin();
    const auto box_before = editor->geometry().topLeft();
    send_wheel(*editor->viewport(), inside(), -120, modifiers);
    QApplication::processEvents();
    const auto on_box = origin() - before;
    const auto box_moved = editor->geometry().topLeft() - box_before;
    std::printf("  %s wheel: over canvas (%d,%d), over the box (%d,%d), box moved (%d,%d)\n", label, on_canvas.x(),
                on_canvas.y(), on_box.x(), on_box.y(), box_moved.x(), box_moved.y());
    CHECK(!on_canvas.isNull());
    CHECK(on_box == on_canvas);
    CHECK(std::abs(box_moved.x() - on_box.x()) <= 1 && std::abs(box_moved.y() - on_box.y()) <= 1);
    CHECK(editor->verticalScrollBar()->value() == 0);
  }

  // Zooms anchor at the pointer, so only the factor compares.
  for (const bool wheel_zooms : {false, true}) {
    canvas->set_wheel_zooms(wheel_zooms);
    const auto modifiers = wheel_zooms ? Qt::KeyboardModifiers(Qt::NoModifier) : Qt::KeyboardModifiers(Qt::AltModifier);
    auto zoom_before = canvas->zoom();
    send_wheel(*canvas, outside, 120, modifiers);
    QApplication::processEvents();
    const auto canvas_factor = canvas->zoom() / zoom_before;
    zoom_before = canvas->zoom();
    send_wheel(*editor, inside(), 120, modifiers);
    QApplication::processEvents();
    process_events_for(100);
    const auto box_factor = canvas->zoom() / zoom_before;
    std::printf("  %s: zoom x%.3f over canvas, x%.3f over the box\n", wheel_zooms ? "wheel-zoom mode" : "Alt+wheel",
                canvas_factor, box_factor);
    CHECK(canvas_factor > 1.0);
    CHECK(std::abs(box_factor - canvas_factor) < 0.001);
    CHECK(live_editor(*canvas) == editor);
  }
  commit_session(*canvas);
}

void ui_text_escape_drops_the_untouched_placeholder() {
  // GitHub issue 415: Esc on a new session still showing the "Type" placeholder leaves no
  // layer and no undo step, also with "Use Esc to commit text" on; Esc after typing keeps its
  // usual meaning (cancel, or commit with the preference on).
  for (const bool escape_commits : {false, true}) {
    SessionWindow session;
    auto* editor = open_new_text_session(session, QStringLiteral("Escape Placeholder"));
    patchy::ui::MainWindowTestAccess::set_escape_commits_text(session.window, escape_commits);
    auto& document = patchy::ui::MainWindowTestAccess::document(session.window);
    auto* undo_action = require_action_by_text(session.window, QStringLiteral("Undo"));
    const auto layer_count = [&] { return std::as_const(document).layers().size(); };
    // open_new_text_session's document starts with no layers (the open session adds its
    // provisional layer and the preview).
    const std::size_t layers_before = 0;
    CHECK(editor != nullptr);
    if (editor == nullptr) {
      return;
    }
    CHECK(editor->toPlainText() == QStringLiteral("Type"));
    send_key(*editor, Qt::Key_Escape);
    process_events_for(150);
    std::printf("  Esc commits %d, placeholder: session open %d, layers %zu (before %zu), undo %d\n",
                escape_commits ? 1 : 0, live_editor(*session.canvas) != nullptr ? 1 : 0, layer_count(),
                layers_before, undo_action->isEnabled() ? 1 : 0);
    CHECK(live_editor(*session.canvas) == nullptr);
    CHECK(layer_count() == layers_before);
    CHECK(!undo_action->isEnabled());

    require_action_by_text(session.window, QStringLiteral("Type"))->trigger();
    patchy::ui::MainWindowTestAccess::add_text_at(session.window, QPoint(60, 200));
    QApplication::processEvents();
    process_events_for(200);
    editor = live_editor(*session.canvas);
    CHECK(editor != nullptr);
    if (editor == nullptr) {
      return;
    }
    editor->textCursor().insertText(QStringLiteral("Typed"));
    QApplication::processEvents();
    send_key(*editor, Qt::Key_Escape);
    process_events_for(250);
    std::printf("  Esc commits %d, typed text: layers %zu, undo %d\n", escape_commits ? 1 : 0, layer_count(),
                undo_action->isEnabled() ? 1 : 0);
    CHECK(live_editor(*session.canvas) == nullptr);
    CHECK(layer_count() == layers_before + (escape_commits ? 1U : 0U));
    CHECK(undo_action->isEnabled() == escape_commits);
  }
}

// A mouse move that waits in the event queue, as window-system moves do behind a slow
// step. The event is built when its turn comes: a QMouseEvent shares its point with the
// pointing device until the next one is built, so on Qt 6.8 a batch of events built up
// front and posted reports the wrong global positions (each the next one's, and the last
// the one the first delivery rebuilt). A scrub reads the global position.
void queue_mouse_move(QWidget& widget, QPoint position, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QMetaObject::invokeMethod(
      &widget,
      [target = &widget, position, modifiers] {
        QMouseEvent move(QEvent::MouseMove, QPointF(position), target->mapToGlobal(QPointF(position)), Qt::NoButton,
                         Qt::LeftButton, modifiers);
        QApplication::sendEvent(target, &move);
      },
      Qt::QueuedConnection);
}

// Scrub handles apply only the newest move of an event-loop pass (GitHub #700,
// dialog_utils.hpp): a queued burst costs two value changes instead of one per move and
// ends on the last position; a release or a key right after a burst first applies the
// recorded move; Shift pressed mid-burst continues from the value under the pointer.
// Moves sent one by one (the other scrub tests) are each applied unless the test switch
// says otherwise.
void ui_scrub_moves_coalesce_to_the_latest_position() {
  QWidget panel;
  auto* row = new QHBoxLayout(&panel);
  auto* label = new QLabel(QStringLiteral("Amount"), &panel);
  auto* amount = new QSpinBox(&panel);
  amount->setRange(0, 100000);
  row->addWidget(label);
  row->addWidget(amount);
  row->addStretch(1);
  patchy::ui::install_scrub_labels_in(&panel);
  panel.resize(420, 60);
  panel.show();
  QApplication::processEvents();
  struct RestoreCoalescing {
    ~RestoreCoalescing() {
      patchy::ui::set_scrub_move_coalescing_for_testing(patchy::ui::ScrubMoveCoalescing::Default);
    }
  } restore;
  int changes = 0;
  int finished = 0;
  int value_at_finish = -1;
  QObject::connect(amount, &QSpinBox::valueChanged, &panel, [&changes](int) { ++changes; });
  QObject::connect(amount, &QAbstractSpinBox::editingFinished, &panel, [&] {
    ++finished;
    value_at_finish = amount->value();
  });
  const int drag_start = QApplication::startDragDistance();
  const QPoint origin = label->rect().center();
  const auto at = [&](int dx) { return origin + QPoint(drag_start + dx, 0); };
  // Sent with no event-loop turn after it (the suite's send_mouse takes one, which ends the pass).
  const auto send_move = [&](int dx, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    const QPointF position(at(dx));
    QMouseEvent move(QEvent::MouseMove, position, label->mapToGlobal(position), Qt::NoButton, Qt::LeftButton,
                     modifiers);
    QApplication::sendEvent(label, &move);
  };
  const auto release = [&](int dx, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    send_mouse(*label, QEvent::MouseButtonRelease, at(dx), Qt::LeftButton, Qt::NoButton, modifiers);
  };
  // Press, then the move that crosses the drag distance (always applied): 500 + drag_start.
  const auto begin = [&](patchy::ui::ScrubMoveCoalescing mode) {
    patchy::ui::set_scrub_move_coalescing_for_testing(mode);
    amount->setValue(500);
    QApplication::processEvents();
    QApplication::processEvents();  // a pass a previous scrub left open has ended
    send_mouse(*label, QEvent::MouseButtonPress, origin, Qt::LeftButton, Qt::LeftButton);
    send_move(0);
    CHECK(amount->value() == 500 + drag_start);
    changes = 0;
    finished = 0;
    value_at_finish = -1;
  };
  const int base = 500 + drag_start;

  // A queued burst of 20 moves, plain and coalesced.
  const auto burst = [&](patchy::ui::ScrubMoveCoalescing mode) {
    begin(mode);
    const auto coalesced_before = patchy::ui::scrub_moves_coalesced_for_testing();
    for (int step = 1; step <= 20; ++step) {
      queue_mouse_move(*label, at(step * 7));
    }
    QApplication::processEvents();
    QApplication::processEvents();
    const int during = changes;
    CHECK(amount->value() == base + 140);
    release(140);
    CHECK(finished == 1);
    CHECK(value_at_finish == base + 140);
    return std::pair{during, patchy::ui::scrub_moves_coalesced_for_testing() - coalesced_before};
  };
  const auto [plain_changes, plain_coalesced] = burst(patchy::ui::ScrubMoveCoalescing::Default);
  CHECK(plain_changes == 20);
  CHECK(plain_coalesced == 0);
  const auto [coalesced_changes, coalesced] = burst(patchy::ui::ScrubMoveCoalescing::AllMoves);
  std::printf("  20 queued scrub moves: %d value changes plain, %d coalesced (%d moves recorded)\n", plain_changes,
              coalesced_changes, coalesced);
  // The first move applies at once; the other 19 arrive as one, the newest.
  CHECK(coalesced_changes == 2);
  CHECK(coalesced == 19);

  // A release right after a burst: the recorded move is applied before editingFinished.
  begin(patchy::ui::ScrubMoveCoalescing::AllMoves);
  for (int step = 1; step <= 6; ++step) {
    send_move(step * 5);
  }
  CHECK(changes == 1);
  CHECK(amount->value() == base + 5);
  release(30);
  CHECK(amount->value() == base + 30);
  CHECK(finished == 1);
  CHECK(value_at_finish == base + 30);
  CHECK(!patchy::ui::spin_box_scrub_active(amount));
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(changes == 2);
  CHECK(amount->value() == base + 30);
  CHECK(finished == 1);

  // A key acts where the pointer is: it applies the recorded move first.
  begin(patchy::ui::ScrubMoveCoalescing::AllMoves);
  for (int step = 1; step <= 4; ++step) {
    send_move(step * 6);
  }
  CHECK(amount->value() == base + 6);
  QKeyEvent key(QEvent::KeyPress, Qt::Key_Control, Qt::ControlModifier);
  QApplication::sendEvent(&panel, &key);
  CHECK(amount->value() == base + 24);
  release(24);
  CHECK(finished == 1);

  // Shift pressed in the middle of a burst: the plain moves end where the last one was,
  // and the Shift moves count ten steps a pixel from the first of them.
  begin(patchy::ui::ScrubMoveCoalescing::AllMoves);
  send_move(4);
  send_move(10);
  CHECK(amount->value() == base + 4);
  send_move(10, Qt::ShiftModifier);
  CHECK(amount->value() == base + 10);
  send_move(13, Qt::ShiftModifier);
  send_move(16, Qt::ShiftModifier);
  CHECK(amount->value() == base + 10);
  release(16, Qt::ShiftModifier);
  CHECK(amount->value() == base + 10 + 60);
  CHECK(finished == 1);
  CHECK(value_at_finish == base + 70);

  // Off applies every move, whatever its source.
  begin(patchy::ui::ScrubMoveCoalescing::Off);
  for (int step = 1; step <= 5; ++step) {
    send_move(step * 3);
  }
  CHECK(changes == 5);
  release(15);
}

// The text Size label scrubbed during a live edit (GitHub #700): a burst of queued moves
// sets the size twice instead of once per move, the session takes the size under the last
// move and its preview follows, steps that land on the same pixel size are not applied
// again, the scrub ends with one editingFinished, and the commit is one undo step.
void ui_text_size_scrub_during_edit_follows_the_latest_move() {
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  patchy::Document document(900, 600, patchy::PixelFormat::rgba8());
  document.print_settings().horizontal_ppi = 72.0;
  document.print_settings().vertical_ppi = 72.0;
  SessionWindow session;
  open_document(session, std::move(document), QStringLiteral("Size Scrub"));
  session.window.resize(1400, 900);
  require_action_by_text(session.window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  auto* undo = require_action_by_text(session.window, QStringLiteral("Undo"));
  CHECK(!undo->isEnabled());
  // A text box dragged out with the Type tool.
  const auto at = [&](int x, int y) { return session.canvas->widget_position_for_document_point(QPoint(x, y)); };
  send_mouse(*session.canvas, QEvent::MouseButtonPress, at(60, 60), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*session.canvas, QEvent::MouseMove, at(400, 300), Qt::NoButton, Qt::LeftButton);
  send_mouse(*session.canvas, QEvent::MouseMove, at(840, 540), Qt::NoButton, Qt::LeftButton);
  send_mouse(*session.canvas, QEvent::MouseButtonRelease, at(840, 540), Qt::LeftButton, Qt::NoButton);
  process_events_for(300);
  auto* editor = live_editor(*session.canvas);
  auto* size = session.window.findChild<QDoubleSpinBox*>(QStringLiteral("textSizeSpin"));
  CHECK(editor != nullptr);
  CHECK(size != nullptr);
  if (editor == nullptr || size == nullptr) {
    return;
  }
  QLabel* label = nullptr;
  for (auto* candidate : session.window.findChildren<QLabel*>()) {
    if (candidate->isVisible() && candidate->property("optionLabel").toBool() &&
        candidate->text() == QStringLiteral("Size")) {
      label = candidate;
    }
  }
  CHECK(label != nullptr);
  if (label == nullptr) {
    return;
  }
  editor->textCursor().insertText(QStringLiteral("Type"));
  process_events_for(100);
  editor->selectAll();
  size->setValue(40.0);
  process_events_for(200);
  editor->selectAll();
  CHECK(editor->property("patchy.documentTextSize").toInt() == 40);
  const auto ink_before = preview_ink_canvas_rect(session, *editor);
  CHECK(ink_before.has_value());

  struct RestoreCoalescing {
    ~RestoreCoalescing() {
      patchy::ui::set_scrub_move_coalescing_for_testing(patchy::ui::ScrubMoveCoalescing::Default);
    }
  } restore;
  int values = 0;
  int finished = 0;
  QObject::connect(size, &QDoubleSpinBox::valueChanged, editor, [&values](double) { ++values; });
  QObject::connect(size, &QAbstractSpinBox::editingFinished, editor, [&finished] { ++finished; });
  const int drag_start = QApplication::startDragDistance();
  const QPoint origin = label->rect().center();
  const auto generation = [&] { return editor->property("patchy.caretLayoutGeneration").toInt(); };

  // Forty queued moves of four pixels (a point each): two sizes applied, the last one kept.
  patchy::ui::set_scrub_move_coalescing_for_testing(patchy::ui::ScrubMoveCoalescing::AllMoves);
  send_mouse(*label, QEvent::MouseButtonPress, origin, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*label, QEvent::MouseMove, origin + QPoint(drag_start, 0), Qt::NoButton, Qt::LeftButton);
  CHECK(patchy::ui::spin_box_scrub_active(size));
  values = 0;
  const auto coalesced_before = patchy::ui::scrub_moves_coalesced_for_testing();
  const auto generation_before = generation();
  for (int step = 1; step <= 40; ++step) {
    queue_mouse_move(*label, origin + QPoint(drag_start + step * 4, 0));
  }
  QApplication::processEvents();
  QApplication::processEvents();
  const double expected_points = 40.0 + 0.25 * (drag_start + 160);
  std::printf("  40 queued moves: %d sizes applied, %d recorded, size %.2f pt (expected %.2f), text %d px\n", values,
              patchy::ui::scrub_moves_coalesced_for_testing() - coalesced_before, size->value(), expected_points,
              editor->property("patchy.documentTextSize").toInt());
  CHECK(values == 2);
  CHECK(patchy::ui::scrub_moves_coalesced_for_testing() - coalesced_before == 39);
  // Each applied size marks the session changed the same number of times.
  const int marks_per_size = (generation() - generation_before) / 2;
  CHECK(marks_per_size >= 1);
  CHECK(generation() - generation_before == 2 * marks_per_size);
  CHECK(std::abs(size->value() - expected_points) < 1e-6);
  const auto& document_now = std::as_const(patchy::ui::MainWindowTestAccess::document(session.window));
  const int expected_pixels = patchy::ui::text_points_to_pixels(expected_points, document_now);
  CHECK(editor->property("patchy.documentTextSize").toInt() == expected_pixels);
  CHECK(finished == 0);
  send_mouse(*label, QEvent::MouseButtonRelease, origin + QPoint(drag_start + 160, 0), Qt::LeftButton,
             Qt::NoButton);
  CHECK(finished == 1);
  CHECK(!patchy::ui::spin_box_scrub_active(size));
  CHECK(std::abs(size->value() - expected_points) < 1e-6);
  // The preview follows without a commit: the drawn word is about twice as tall.
  process_events_for(200);
  CHECK(live_editor(*session.canvas) == editor);
  const auto ink_after = preview_ink_canvas_rect(session, *editor);
  CHECK(ink_after.has_value());
  if (ink_before.has_value() && ink_after.has_value()) {
    std::printf("  preview ink height %d px at 40 pt, %d px after the scrub\n", ink_before->height(),
                ink_after->height());
    CHECK(ink_after->height() > ink_before->height() * 3 / 2);
  }

  // Eight one-pixel moves, each applied (a quarter point a move): the text has whole
  // pixels, so only the steps that reach a new pixel size touch the session.
  patchy::ui::set_scrub_move_coalescing_for_testing(patchy::ui::ScrubMoveCoalescing::Off);
  editor->selectAll();
  size->setValue(60.0);
  process_events_for(100);
  editor->selectAll();
  send_mouse(*label, QEvent::MouseButtonPress, origin, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*label, QEvent::MouseMove, origin + QPoint(drag_start, 0), Qt::NoButton, Qt::LeftButton);
  values = 0;
  const auto pixels_at_start = editor->property("patchy.documentTextSize").toInt();
  const auto generation_at_start = generation();
  int pixel_sizes = 0;
  int last_pixels = pixels_at_start;
  for (int step = 1; step <= 8; ++step) {
    send_mouse(*label, QEvent::MouseMove, origin + QPoint(drag_start + step, 0), Qt::NoButton, Qt::LeftButton);
    const int pixels = patchy::ui::text_points_to_pixels(size->value(), document_now);
    CHECK(editor->property("patchy.documentTextSize").toInt() == pixels);
    if (pixels != last_pixels) {
      ++pixel_sizes;
      last_pixels = pixels;
    }
  }
  std::printf("  8 quarter-point steps: %d values, %d new pixel sizes, %d session updates\n", values, pixel_sizes,
              generation() - generation_at_start);
  CHECK(values == 8);
  CHECK(pixel_sizes >= 1 && pixel_sizes < 8);
  CHECK(generation() - generation_at_start == pixel_sizes * marks_per_size);
  send_mouse(*label, QEvent::MouseButtonRelease, origin + QPoint(drag_start + 8, 0), Qt::LeftButton, Qt::NoButton);
  CHECK(finished == 2);
  const double final_points = size->value();
  const int final_pixels = patchy::ui::text_points_to_pixels(final_points, document_now);
  CHECK(std::abs(final_points - (60.0 + 0.25 * (drag_start + 8))) < 1e-6);
  // The selection carries the size the field shows.
  QTextCursor probe(editor->document());
  probe.setPosition(2);
  CHECK(probe.charFormat().property(QTextFormat::FontPixelSize).toInt() ==
        std::max(8, static_cast<int>(std::round(final_pixels * session.canvas->zoom()))));

  // The whole session, scrubs included, commits as one undo step.
  commit_session(*session.canvas);
  CHECK(live_editor(*session.canvas) == nullptr);
  auto& committed = patchy::ui::MainWindowTestAccess::document(session.window);
  const auto text_layer =
      find_layer_id(committed, [](const patchy::Layer& layer) { return patchy::layer_is_text(layer); });
  CHECK(text_layer.has_value());
  if (text_layer.has_value()) {
    const auto* layer = std::as_const(committed).find_layer(*text_layer);
    const auto stored = layer->metadata().find(patchy::kLayerMetadataTextSize);
    CHECK(stored != layer->metadata().end() && stored->second == std::to_string(final_pixels));
  }
  CHECK(undo->isEnabled());
  undo->trigger();
  QApplication::processEvents();
  process_events_for(100);
  CHECK(!find_layer_id(std::as_const(patchy::ui::MainWindowTestAccess::document(session.window)),
                       [](const patchy::Layer& layer) { return patchy::layer_is_text(layer); })
             .has_value());
  CHECK(!undo->isEnabled());
}

}  // namespace

std::vector<patchy::test::TestCase> text_canvas_session_tests() {
  return {
      {"ui_scrub_moves_coalesce_to_the_latest_position", ui_scrub_moves_coalesce_to_the_latest_position},
      {"ui_text_size_scrub_during_edit_follows_the_latest_move",
       ui_text_size_scrub_during_edit_follows_the_latest_move},
      {"ui_text_held_enter_relayouts_each_repeat", ui_text_held_enter_relayouts_each_repeat},
      {"ui_text_wheel_over_the_editor_moves_the_canvas", ui_text_wheel_over_the_editor_moves_the_canvas},
      {"ui_text_escape_drops_the_untouched_placeholder", ui_text_escape_drops_the_untouched_placeholder},
      {"ui_text_box_outline_shows_on_unrotated_text", ui_text_box_outline_shows_on_unrotated_text},
      {"ui_text_caret_spans_the_drawn_line", ui_text_caret_spans_the_drawn_line},
      {"ui_text_size_points_round_trip_at_common_resolutions", ui_text_size_points_round_trip_at_common_resolutions},
      {"ui_text_size_field_keeps_typed_points", ui_text_size_field_keeps_typed_points},
      {"ui_rotated_psd_box_text_edit_keeps_transform", ui_rotated_psd_box_text_edit_keeps_transform},
      {"ui_text_rotate_sample_edits_keep_transform_if_available",
       ui_text_rotate_sample_edits_keep_transform_if_available},
      {"ui_text_rotate_sample_session_captures_if_available", ui_text_rotate_sample_session_captures_if_available},
  };
}
