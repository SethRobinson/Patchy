// SVG import/export UI coverage: editable-layer opens, the main-thread text
// render pass, data-URI images, save-a-copy semantics, and export/reimport
// visual parity (QSvgRenderer doubles as an independent reference renderer).

#include "core/layer_metadata.hpp"
#include "core/smart_object.hpp"
#include "core/vector_shape.hpp"
#include "formats/svg_document_io.hpp"
#include "render/compositor.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "ui/smart_object_render.hpp"

#include "local_psd_fixtures.hpp"
#include "test_fonts.hpp"
#include "test_harness.hpp"
#include "ui/ui_test_access.hpp"
#include "ui_test_support.hpp"

#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QColor>
#include <QFileInfo>
#include <QImage>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSvgRenderer>
#include <QTemporaryDir>
#include <QTimer>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

using patchy::test::ui::find_top_level_dialog;
using patchy::test::ui::save_widget_artifact;
using patchy::test::ui::show_window;

QString committed_svg_fixture(const char* name) {
  return QString::fromStdWString(patchy::test::committed_format_fixture_path("svg", name).wstring());
}

const patchy::Layer* find_layer_named(const std::vector<patchy::Layer>& layers, const QString& name) {
  for (const auto& layer : layers) {
    if (QString::fromStdString(layer.name()) == name) {
      return &layer;
    }
    if (const auto* nested = find_layer_named(layer.children(), name); nested != nullptr) {
      return nested;
    }
  }
  return nullptr;
}

QImage flatten_to_qimage(const patchy::Document& document) {
  const auto pixels = patchy::Compositor().flatten_rgb8(document);
  QImage image(pixels.width(), pixels.height(), QImage::Format_RGB888);
  for (int y = 0; y < pixels.height(); ++y) {
    std::memcpy(image.scanLine(y), pixels.row(y).data(), static_cast<std::size_t>(pixels.width()) * 3U);
  }
  return image;
}

double mean_rgb_delta(const QImage& a, const QImage& b) {
  CHECK(a.size() == b.size());
  double total = 0.0;
  for (int y = 0; y < a.height(); ++y) {
    for (int x = 0; x < a.width(); ++x) {
      const auto ca = a.pixelColor(x, y);
      const auto cb = b.pixelColor(x, y);
      total += std::abs(ca.red() - cb.red()) + std::abs(ca.green() - cb.green()) + std::abs(ca.blue() - cb.blue());
    }
  }
  return total / (static_cast<double>(a.width()) * a.height() * 3.0);
}

void ui_svg_open_creates_editable_shape_layers() {
  const auto path = committed_svg_fixture("basic-shapes.svg");
  CHECK(QFileInfo::exists(path));
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();

  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.width() == 240 && document.height() == 160);
  // Bottom-to-top: Backdrop first, Kite last (SVG paint order = layers() order).
  CHECK(document.layers().size() == 6);
  CHECK(document.layers().front().name() == "Backdrop");
  CHECK(document.layers().back().name() == "Kite");
  const auto* cluster = find_layer_named(document.layers(), QStringLiteral("Cluster"));
  CHECK(cluster != nullptr && cluster->kind() == patchy::LayerKind::Group);
  CHECK(cluster->children().size() == 3);
  CHECK(std::abs(cluster->opacity() - 0.85F) < 0.001F);
  const auto* backdrop = find_layer_named(document.layers(), QStringLiteral("Backdrop"));
  CHECK(backdrop != nullptr && patchy::layer_is_vector_shape(*backdrop));
  CHECK(backdrop->vector_shape()->fill.kind == patchy::VectorFillKind::Gradient);
  const auto* dot = find_layer_named(document.layers(), QStringLiteral("Dot"));
  CHECK(dot != nullptr);
  CHECK(!dot->vector_shape()->origination.empty());
  CHECK(dot->vector_shape()->origination.front().kind == patchy::LiveShapeKind::Ellipse);
  CHECK(dot->vector_shape()->stroke.enabled);  // the .badge class stroke applied

  save_widget_artifact("svg_basic_shapes_import", *patchy::ui::MainWindowTestAccess::canvas(window));
}

void ui_svg_import_render_matches_qsvg() {
  const auto path = committed_svg_fixture("basic-shapes.svg");
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  QSvgRenderer reference_renderer(path);
  CHECK(reference_renderer.isValid());
  QImage reference(document.width(), document.height(), QImage::Format_RGB888);
  reference.fill(Qt::black);  // the compositor's uncovered background
  {
    QPainter painter(&reference);
    reference_renderer.render(&painter, QRectF(0, 0, document.width(), document.height()));
  }
  const auto composite = flatten_to_qimage(document);
  // Independent renderers with different AA and gradient easing: assert the
  // pictures agree in the mean, not per pixel.
  const auto delta = mean_rgb_delta(composite, reference);
  if (delta >= 14.0) {
    fprintf(stderr, "[svg] qsvg cross-check mean delta %f\n", delta);
  }
  CHECK(delta < 14.0);
}

void ui_svg_text_import_positions_baseline() {
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto path = temp.filePath(QStringLiteral("text.svg"));
  {
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"480\" height=\"120\">"
        "<text id=\"Start\" x=\"240\" y=\"50\" font-family=\"Arial\" font-size=\"24\" fill=\"#000000\">StartHere</text>"
        "<text id=\"Mid\" x=\"240\" y=\"90\" text-anchor=\"middle\" font-family=\"Arial\" font-size=\"24\" "
        "fill=\"#cc0000\">MiddleHere</text>"
        "</svg>");
  }
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.layers().size() == 2);

  const auto* start = find_layer_named(document.layers(), QStringLiteral("Start"));
  const auto* middle = find_layer_named(document.layers(), QStringLiteral("Mid"));
  CHECK(start != nullptr && middle != nullptr);
  CHECK(patchy::layer_is_text(*start));  // Pixel kind + patchy.text metadata
  // Rendered by the post-open pass: pixels exist and the markers are gone.
  CHECK(!std::as_const(*start).pixels().empty());
  CHECK(!start->metadata().contains(patchy::kLayerMetadataSvgPendingText));
  // A start-anchored baseline at x=240 puts the layer's left edge at 240; the
  // baseline sits font-ascent above y=50 (loose bands: font metrics differ
  // slightly across machines).
  CHECK(start->bounds().x >= 235 && start->bounds().x <= 245);
  CHECK(start->bounds().y >= 20 && start->bounds().y <= 35);
  // A middle-anchored run centers on x=240.
  const auto center = middle->bounds().x + middle->bounds().width / 2;
  CHECK(center >= 232 && center <= 248);
  CHECK(middle->bounds().y >= 60 && middle->bounds().y <= 75);
}

void ui_svg_data_uri_image_round_trip() {
  // Build the data URI from a real QImage so the fixture never depends on
  // hand-typed base64.
  QImage source(8, 6, QImage::Format_RGBA8888);
  source.fill(QColor(30, 200, 90, 255));
  QByteArray png_bytes;
  {
    QBuffer buffer(&png_bytes);
    buffer.open(QIODevice::WriteOnly);
    CHECK(source.save(&buffer, "PNG"));
  }
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto path = temp.filePath(QStringLiteral("image.svg"));
  {
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40\" height=\"30\">"
               "<image id=\"Photo\" x=\"4\" y=\"5\" width=\"16\" height=\"12\" href=\"data:image/png;base64,");
    file.write(png_bytes.toBase64());
    file.write("\"/></svg>");
  }
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* photo = find_layer_named(document.layers(), QStringLiteral("Photo"));
  CHECK(photo != nullptr);
  CHECK(!photo->metadata().contains(patchy::kLayerMetadataSvgPendingImage));
  CHECK(photo->bounds().x == 4 && photo->bounds().y == 5);
  CHECK(photo->bounds().width == 16 && photo->bounds().height == 12);
  const auto& pixels = std::as_const(*photo).pixels();
  CHECK(!pixels.empty());
  const auto* sample = pixels.pixel(8, 6);
  CHECK(sample[0] == 30 && sample[1] == 200 && sample[2] == 90 && sample[3] == 255);
}

void ui_svg_save_is_copy_and_reopens_editable() {
  const auto fixture = committed_svg_fixture("basic-shapes.svg");
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, fixture);
  QApplication::processEvents();

  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto out_path = temp.filePath(QStringLiteral("roundtrip.svg"));
  patchy::ui::ImageSaveOptions options;
  bool prompt_seen = false;
  QTimer::singleShot(0, [&prompt_seen] {
    auto* box = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("flattenLayersMessageBox")));
    CHECK(box != nullptr);
    // The SVG wording promises vectors stay vectors, not a flatten.
    CHECK(box->text().contains(QStringLiteral("SVG keeps shape layers as vectors")));
    prompt_seen = true;
    box->button(QMessageBox::Save)->click();
  });
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, out_path, options));
  CHECK(prompt_seen);
  CHECK(QFileInfo::exists(out_path));
  // Save-a-copy: the session keeps pointing at the original file.
  CHECK(patchy::ui::MainWindowTestAccess::active_session_path(window) == fixture);

  // The copy reopens with its vector structure intact.
  patchy::ui::MainWindow reopened_window;
  show_window(reopened_window);
  patchy::ui::MainWindowTestAccess::open_document_path(reopened_window, out_path);
  QApplication::processEvents();
  auto& reopened = patchy::ui::MainWindowTestAccess::document(reopened_window);
  const auto* dot = find_layer_named(reopened.layers(), QStringLiteral("Dot"));
  CHECK(dot != nullptr && patchy::layer_is_vector_shape(*dot));
  CHECK(!dot->vector_shape()->origination.empty());

  // And the composited pictures agree closely (same renderer both sides).
  auto& original = patchy::ui::MainWindowTestAccess::document(window);
  const auto delta = mean_rgb_delta(flatten_to_qimage(original), flatten_to_qimage(reopened));
  if (delta >= 3.0) {
    fprintf(stderr, "[svg] save/reopen mean delta %f\n", delta);
  }
  CHECK(delta < 3.0);
}

void ui_svg_paste_creates_shape_layers() {
  patchy::ui::MainWindow window;
  show_window(window);  // the historical 1024x768 startup document
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layers_before = document.layers().size();
  QApplication::clipboard()->setText(QStringLiteral(
      "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"60\" height=\"40\">"
      "<rect id=\"PastedRect\" x=\"10\" y=\"8\" width=\"30\" height=\"20\" fill=\"#00aa55\"/>"
      "</svg>"));
  patchy::ui::MainWindowTestAccess::paste_clipboard(window);
  QApplication::processEvents();
  CHECK(document.layers().size() == layers_before + 1);
  const auto* pasted = find_layer_named(document.layers(), QStringLiteral("PastedRect"));
  CHECK(pasted != nullptr);
  CHECK(patchy::layer_is_vector_shape(*pasted));
  CHECK(!pasted->vector_shape()->origination.empty());
  CHECK(pasted->vector_shape()->fill.color.green == 0xAA);
  QApplication::clipboard()->clear();
}

void ui_svg_define_custom_shape_from_file() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto fixture = committed_svg_fixture("basic-shapes.svg");
  CHECK(patchy::ui::MainWindowTestAccess::define_custom_shape_from_svg_path(window, fixture));
  auto& library = patchy::ui::MainWindowTestAccess::custom_shape_library(window);
  const patchy::ui::CustomShapeLibraryEntry* imported = nullptr;
  for (const auto& entry : library.entries()) {
    if (entry.name == QStringLiteral("basic-shapes")) {
      imported = &entry;
    }
  }
  CHECK(imported != nullptr);
  CHECK(!imported->path.subpaths.empty());
  // Unit-box normalized geometry, holes preserved as Subtract groups.
  const auto bounds = imported->path.bounds();
  CHECK(bounds.has_value());
  CHECK(bounds->right - bounds->left <= 1.0001 && bounds->bottom - bounds->top <= 1.0001);
  library.remove_shape(imported->storage_id);  // keep repeated runs clean
}

void ui_svg_place_creates_smart_object() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto fixture = committed_svg_fixture("basic-shapes.svg");
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layers_before = document.layers().size();
  patchy::ui::MainWindowTestAccess::place_embedded_file_with_path(window, fixture);
  QApplication::processEvents();
  CHECK(document.layers().size() == layers_before + 1);
  const auto& placed = document.layers().back();
  CHECK(patchy::layer_is_smart_object(placed));
  CHECK(!std::as_const(placed).pixels().empty());
}

// Affinity .af text uses the same main-thread post-open render pass as SVG;
// this lives here beside the SVG text test on purpose.
void ui_af_text_import_renders_post_open() {
  if (patchy::test::ui::skip_without_arial_for_psd_text_preview()) {
    return;  // the committed fixture's face is Arial
  }
  patchy::ui::MainWindow window;
  show_window(window);
  const auto path = QString::fromStdWString(
      patchy::test::committed_format_fixture_path("af", "tiny-text-artistic.af").wstring());
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.layers().size() == 2);  // white Background + the text layer

  const auto& layer = document.layers().back();
  CHECK(patchy::layer_is_text(layer));
  CHECK(!std::as_const(layer).pixels().empty());
  CHECK(!layer.metadata().contains(patchy::kLayerMetadataAfPendingText));
  // Authored: 36px red "Color" anchored at baseline (20, 60). The rendered
  // block's left edge sits at the frame x; the top sits one ascent above the
  // baseline (loose bands: font metrics differ slightly across machines).
  CHECK(layer.bounds().x >= 14 && layer.bounds().x <= 26);
  CHECK(layer.bounds().y >= 18 && layer.bounds().y <= 42);
  // The fill is red.
  const auto& pixels = std::as_const(layer).pixels();
  bool found_red = false;
  for (std::int32_t y = 0; y < pixels.height() && !found_red; ++y) {
    for (std::int32_t x = 0; x < pixels.width() && !found_red; ++x) {
      const std::uint8_t* p = pixels.pixel(x, y);
      if (p[3] > 200 && p[0] > 150 && p[1] < 100 && p[2] < 100) {
        found_red = true;
      }
    }
  }
  CHECK(found_red);
}

// Mixed-run .af text renders per-run styles through the runs metadata: three
// runs (Arial 24 red, Times New Roman 32 blue, Courier New 18 green - the
// last with an emoji, pinning the codepoint run-boundary decode end to end).
void ui_af_mixed_text_runs_render() {
  if (patchy::test::ui::skip_without_arial_for_psd_text_preview()) {
    return;
  }
  patchy::test::register_test_fonts(patchy::test::TestFontRole::TimesNewRoman);
  patchy::test::register_test_fonts(patchy::test::TestFontRole::CourierNew);
  patchy::ui::MainWindow window;
  show_window(window);
  const auto path = QString::fromStdWString(
      patchy::test::committed_format_fixture_path("af", "tiny-text-runs.af").wstring());
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto& layer = document.layers().back();
  CHECK(patchy::layer_is_text(layer));
  CHECK(!std::as_const(layer).pixels().empty());
  CHECK(!layer.metadata().contains(patchy::kLayerMetadataAfPendingText));
  // Per-run fill colors survive into the render regardless of which faces the
  // platform resolves: red glyphs (run 1), blue (run 2), green (run 3).
  const auto& pixels = std::as_const(layer).pixels();
  bool found_red = false;
  bool found_blue = false;
  bool found_green = false;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const std::uint8_t* p = pixels.pixel(x, y);
      if (p[3] < 200) {
        continue;
      }
      found_red = found_red || (p[0] > 150 && p[1] < 90 && p[2] < 90);
      found_blue = found_blue || (p[2] > 150 && p[0] < 90 && p[1] < 90);
      found_green = found_green || (p[1] > 110 && p[0] < 90 && p[2] < 90);
    }
  }
  CHECK(found_red);
  CHECK(found_blue);
  CHECK(found_green);
  patchy::test::ui::save_widget_artifact("af_mixed_text_runs", window);
}

// Rotated .af artistic text renders THROUGH its node affine: the post-open
// pass composes the local anchor with the stored Xfrm, so a -90deg text lands
// portrait, and the layer keeps the standard transformed-text metadata.
void ui_af_rotated_text_renders_through_transform() {
  if (patchy::test::ui::skip_without_arial_for_psd_text_preview()) {
    return;
  }
  patchy::ui::MainWindow window;
  show_window(window);
  const auto path = QString::fromStdWString(
      patchy::test::committed_format_fixture_path("af", "tiny-text-rotated.af").wstring());
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto& layer = document.layers().back();
  CHECK(patchy::layer_is_text(layer));
  CHECK(!std::as_const(layer).pixels().empty());
  CHECK(!layer.metadata().contains(patchy::kLayerMetadataAfTextXfrm));
  CHECK(layer.metadata().contains(patchy::kLayerMetadataTextTransform));
  // "Rotated" at -90deg: the ink is portrait (taller than wide).
  CHECK(layer.bounds().height > layer.bounds().width * 2);
  patchy::test::ui::save_widget_artifact("af_rotated_text", window);
}

// A pristine .af placed image imports as an embedded smart object; the UI can
// re-render it from its source and reproduce the baked pixels.
void ui_af_placed_image_imports_as_smart_object() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto path = QString::fromStdWString(
      patchy::test::committed_format_fixture_path("af", "tiny-embedded-jpeg.af").wstring());
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* layer = &document.layers().back();
  CHECK(patchy::layer_is_smart_object(*layer));

  std::array<std::uint8_t, 3> before{};
  std::memcpy(before.data(), std::as_const(*layer).pixels().pixel(200, 150), 3);
  CHECK(patchy::ui::refresh_smart_object_layer_preview(
      document, *layer, patchy::ui::CanvasWidget::TransformInterpolation::Bilinear));
  CHECK(std::as_const(*layer).pixels().width() == 400);
  CHECK(std::as_const(*layer).pixels().height() == 300);
  const std::uint8_t* after = std::as_const(*layer).pixels().pixel(200, 150);
  for (int channel = 0; channel < 3; ++channel) {
    const int delta = std::abs(static_cast<int>(after[channel]) - before[channel]);
    CHECK(delta <= 3);  // identity placement re-decode of the same JPEG
  }
}

}  // namespace

std::vector<patchy::test::TestCase> svg_ui_tests() {
  return {
      {"ui_af_text_import_renders_post_open", ui_af_text_import_renders_post_open},
      {"ui_af_mixed_text_runs_render", ui_af_mixed_text_runs_render},
      {"ui_af_rotated_text_renders_through_transform", ui_af_rotated_text_renders_through_transform},
      {"ui_af_placed_image_imports_as_smart_object", ui_af_placed_image_imports_as_smart_object},
      {"ui_svg_open_creates_editable_shape_layers", ui_svg_open_creates_editable_shape_layers},
      {"ui_svg_import_render_matches_qsvg", ui_svg_import_render_matches_qsvg},
      {"ui_svg_text_import_positions_baseline", ui_svg_text_import_positions_baseline},
      {"ui_svg_data_uri_image_round_trip", ui_svg_data_uri_image_round_trip},
      {"ui_svg_save_is_copy_and_reopens_editable", ui_svg_save_is_copy_and_reopens_editable},
      {"ui_svg_paste_creates_shape_layers", ui_svg_paste_creates_shape_layers},
      {"ui_svg_define_custom_shape_from_file", ui_svg_define_custom_shape_from_file},
      {"ui_svg_place_creates_smart_object", ui_svg_place_creates_smart_object},
  };
}
