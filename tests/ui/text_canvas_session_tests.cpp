// The on-canvas text edit session: the layer transform it carries through entry and commit
// (GitHub issue 373), the caret against the drawn line (384), the text box outline (385, 371)
// and the options-bar size round trip (371). The reporter's sample is a local fixture
// (local-test-fixtures/psd/text-rotate-issue373.psd); the committed-fixture tests build the
// same shapes from photoshop-text-box-auto-leading.psd so they run everywhere.
#include "core/layer_metadata.hpp"
#include "psd/psd_document_io.hpp"
#include "ui/canvas_widget.hpp"
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
#include <QImage>
#include <QKeyEvent>
#include <QPolygonF>
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
  CHECK(QFontMetricsF(font).leading() >= 20.0);
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

}  // namespace

std::vector<patchy::test::TestCase> text_canvas_session_tests() {
  return {
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
