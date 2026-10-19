#include <QStatusBar>
#include "ui/canvas_widget.hpp"
#include "core/layer_metadata.hpp"
#include "core/pattern_resource.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
#include "core/smart_object.hpp"
#include "core/text_warp.hpp"
#include "ui/pdf_export.hpp"
#include "ui/pdf_import.hpp"
#include "ui/ui_font.hpp"
#include "formats/pdf_document_io.hpp"
#include "formats/pdf_file.hpp"
#include "ui/image_document_io.hpp"
#include "ui/image_save_options_dialog.hpp"
#include "ui/main_window.hpp"
#include "ui/print_dialog.hpp"
#include "ui/app_settings.hpp"
#include "ui/script_engine.hpp"
#include "synthetic_dng.hpp"
#include "test_fonts.hpp"
#include "ui/qt_paths.hpp"
#include "unicode_path_names.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"
#include <QApplication>
#include <QBuffer>
#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QImage>
#include <QImageWriter>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QIODevice>
#if defined(PATCHY_HAVE_QT_PDF)
#include <QPdfDocument>
#endif
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QTabWidget>
#include <QTimer>
#include <algorithm>
#include <atomic>
#include <array>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "import_print_resolution_test_support.hpp"

namespace {
using namespace patchy::test::ui;
using namespace patchy::test::ui::import_print_resolution_support;

#if defined(PATCHY_HAVE_QT_PDF)
// A two-page PDF built byte by byte, the way the other adversarial format fixtures are
// synthesized in-test: no binary fixture to maintain and no external generator. Page 1 is
// 144 x 72 pt of red, page 2 is 72 x 144 pt of blue, so the layer canvas has to come out
// 144 x 144 (the per-axis maximum) with the second page top-left aligned.
QByteArray two_page_pdf_bytes();

// The bounding box of the dark ink inside `region` of an image flattened over white
// (nullopt when the region holds no ink).
std::optional<QRect> dark_ink_bounds(const QImage& image, QRect region);

// A horizontal ramp with a little vertical variation: gray when `gray`, else with
// distinct channels, so a channel mix-up cannot pass by accident.
patchy::PixelBuffer ramp_pixels(int width, int height, bool gray);

patchy::Document ramp_document(int width, int height, bool gray);

QImage render_pdf_page(const QString& path, int page, QSize size);

// Two visible pixel layers: an editable export of this cannot be one image, so it goes
// through Qt's engine (QtPdfOutput) rather than the image writer.
patchy::Document two_layer_pdf_page(const QColor& color);

QByteArray gray_jpeg_bytes(int width, int height, int quality);

// A scanned PDF in miniature: every page is one gray JPEG over the whole page. Page
// boxes are in points; `rotate` goes on every page.
QByteArray scanned_pdf_bytes(const std::vector<QByteArray>& jpegs, QSize pixels, QSizeF points, int rotate);

// The image XObject streams of a written PDF, in page order, through Patchy's Qt-free
// reader (which shares nothing with either writer).
std::vector<QByteArray> page_image_streams(const QString& path, std::vector<int>* rotations = nullptr);


// A two-page editable PDF: a blue rectangle on page 1, a red one on page 2.
QByteArray two_page_editable_pdf_bytes();

// A one-page PDF with a filled rectangle and a text run, xref offsets computed.
QByteArray editable_pdf_bytes();

QByteArray two_page_pdf_bytes() {
  const QByteArray objects[6] = {
      QByteArrayLiteral("<</Type/Catalog/Pages 2 0 R>>"),
      QByteArrayLiteral("<</Type/Pages/Kids[3 0 R 5 0 R]/Count 2>>"),
      QByteArrayLiteral("<</Type/Page/Parent 2 0 R/MediaBox[0 0 144 72]/Contents 4 0 R>>"),
      QByteArrayLiteral("<</Length 24>>\nstream\n1 0 0 rg 0 0 144 72 re f\nendstream"),
      QByteArrayLiteral("<</Type/Page/Parent 2 0 R/MediaBox[0 0 72 144]/Contents 6 0 R>>"),
      QByteArrayLiteral("<</Length 24>>\nstream\n0 0 1 rg 0 0 72 144 re f\nendstream"),
  };
  QByteArray pdf = QByteArrayLiteral("%PDF-1.4\n");
  std::array<int, 6> offsets{};
  for (int index = 0; index < 6; ++index) {
    offsets[static_cast<std::size_t>(index)] = static_cast<int>(pdf.size());
    pdf += QByteArray::number(index + 1) + " 0 obj\n" + objects[index] + "\nendobj\n";
  }
  const int xref_offset = static_cast<int>(pdf.size());
  pdf += "xref\n0 7\n0000000000 65535 f \n";
  for (const int offset : offsets) {
    pdf += QStringLiteral("%1 00000 n \n").arg(offset, 10, 10, QLatin1Char('0')).toLatin1();
  }
  pdf += "trailer\n<</Size 7/Root 1 0 R>>\nstartxref\n" + QByteArray::number(xref_offset) + "\n%%EOF\n";
  return pdf;
}

std::optional<QRect> dark_ink_bounds(const QImage& image, QRect region) {
  const QImage flat = over_white(image);
  region = region.intersected(flat.rect());
  int left = region.right() + 1;
  int top = region.bottom() + 1;
  int right = region.left() - 1;
  int bottom = region.top() - 1;
  for (int y = region.top(); y <= region.bottom(); ++y) {
    for (int x = region.left(); x <= region.right(); ++x) {
      const auto color = flat.pixelColor(x, y);
      if (color.red() + color.green() + color.blue() < 384) {
        left = std::min(left, x);
        right = std::max(right, x);
        top = std::min(top, y);
        bottom = std::max(bottom, y);
      }
    }
  }
  if (right < left || bottom < top) {
    return std::nullopt;
  }
  return QRect(QPoint(left, top), QPoint(right, bottom));
}

patchy::PixelBuffer ramp_pixels(int width, int height, bool gray) {
  auto pixels = solid_pixels(width, height, patchy::PixelFormat::rgba8(), QColor(0, 0, 0));
  for (std::int32_t y = 0; y < height; ++y) {
    auto row = pixels.row(y);
    for (std::int32_t x = 0; x < width; ++x) {
      const auto value = static_cast<std::uint8_t>((x * 255) / std::max(1, width - 1));
      const auto other = static_cast<std::uint8_t>((y * 255) / std::max(1, height - 1));
      row[static_cast<std::size_t>(x) * 4 + 0] = value;
      row[static_cast<std::size_t>(x) * 4 + 1] = gray ? value : other;
      row[static_cast<std::size_t>(x) * 4 + 2] = gray ? value : static_cast<std::uint8_t>(255 - value);
      row[static_cast<std::size_t>(x) * 4 + 3] = 255;
    }
  }
  return pixels;
}

patchy::Document ramp_document(int width, int height, bool gray) {
  patchy::Document document(width, height, patchy::PixelFormat::rgba8());
  document.print_settings().horizontal_ppi = 100.0;
  document.print_settings().vertical_ppi = 100.0;
  document.add_pixel_layer("Ramp", ramp_pixels(width, height, gray));
  return document;
}

QImage render_pdf_page(const QString& path, int page, QSize size) {
  QPdfDocument reader;
  CHECK(reader.load(path) == QPdfDocument::Error::None);
  return reader.render(page, size);
}

patchy::Document two_layer_pdf_page(const QColor& color) {
  patchy::Document document(60, 40, patchy::PixelFormat::rgba8());
  document.print_settings().horizontal_ppi = 72.0;
  document.print_settings().vertical_ppi = 72.0;
  document.add_pixel_layer("Base", solid_pixels(60, 40, patchy::PixelFormat::rgba8(), color));
  document.add_pixel_layer("Top", solid_pixels(60, 40, patchy::PixelFormat::rgba8(), QColor(10, 10, 10, 96)));
  return document;
}

QByteArray gray_jpeg_bytes(int width, int height, int quality) {
  QImage image(width, height, QImage::Format_Grayscale8);
  for (int y = 0; y < height; ++y) {
    auto* row = image.scanLine(y);
    for (int x = 0; x < width; ++x) {
      row[x] = static_cast<uchar>(((x * 3) ^ (y * 5)) & 0xFF);
    }
  }
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  QImageWriter writer(&buffer, "JPEG");
  writer.setQuality(quality);
  CHECK(writer.write(image));
  return bytes;
}

QByteArray scanned_pdf_bytes(const std::vector<QByteArray>& jpegs, QSize pixels, QSizeF points, int rotate) {
  std::vector<QByteArray> objects;
  objects.push_back("<</Type/Catalog/Pages 2 0 R>>");
  QByteArray kids;
  for (std::size_t index = 0; index < jpegs.size(); ++index) {
    kids += QByteArray::number(static_cast<int>(3 + index * 3)) + " 0 R ";
  }
  objects.push_back("<</Type/Pages/Kids[" + kids + "]/Count " + QByteArray::number(static_cast<int>(jpegs.size())) +
                    ">>");
  for (std::size_t index = 0; index < jpegs.size(); ++index) {
    const int first = static_cast<int>(3 + index * 3);
    const QByteArray content = "q " + QByteArray::number(points.width()) + " 0 0 " +
                               QByteArray::number(points.height()) + " 0 0 cm /Im0 Do Q";
    objects.push_back("<</Type/Page/Parent 2 0 R/MediaBox[0 0 " + QByteArray::number(points.width()) + " " +
                      QByteArray::number(points.height()) + "]/Rotate " + QByteArray::number(rotate) +
                      "/Resources<</XObject<</Im0 " + QByteArray::number(first + 2) + " 0 R>>>>/Contents " +
                      QByteArray::number(first + 1) + " 0 R>>");
    objects.push_back("<</Length " + QByteArray::number(content.size()) + ">>\nstream\n" + content + "\nendstream");
    objects.push_back("<</Type/XObject/Subtype/Image/Width " + QByteArray::number(pixels.width()) + "/Height " +
                      QByteArray::number(pixels.height()) +
                      "/ColorSpace/DeviceGray/BitsPerComponent 8/Filter/DCTDecode/Length " +
                      QByteArray::number(jpegs[index].size()) + ">>\nstream\n" + jpegs[index] + "\nendstream");
  }
  QByteArray pdf = QByteArrayLiteral("%PDF-1.4\n");
  std::vector<qsizetype> offsets;
  for (std::size_t index = 0; index < objects.size(); ++index) {
    offsets.push_back(pdf.size());
    pdf += QByteArray::number(static_cast<int>(index + 1)) + " 0 obj\n" + objects[index] + "\nendobj\n";
  }
  const auto xref_offset = pdf.size();
  pdf += "xref\n0 " + QByteArray::number(static_cast<int>(objects.size() + 1)) + "\n0000000000 65535 f \n";
  for (const auto offset : offsets) {
    pdf += QStringLiteral("%1 00000 n \n").arg(offset, 10, 10, QLatin1Char('0')).toLatin1();
  }
  pdf += "trailer\n<</Size " + QByteArray::number(static_cast<int>(objects.size() + 1)) +
         "/Root 1 0 R>>\nstartxref\n" + QByteArray::number(xref_offset) + "\n%%EOF\n";
  return pdf;
}

std::vector<QByteArray> page_image_streams(const QString& path, std::vector<int>* rotations) {
  const QByteArray bytes = read_file_bytes(path);
  auto file = patchy::pdf::File::open(
      std::vector<std::uint8_t>(bytes.begin(), bytes.end()), nullptr);
  std::vector<QByteArray> streams;
  if (!file.has_value()) {
    return streams;
  }
  for (const auto& page : file->pages()) {
    if (rotations != nullptr) {
      rotations->push_back(page.rotate);
    }
    const auto* xobjects = file->get(page.resources, "XObject").dictionary();
    if (xobjects == nullptr || xobjects->empty()) {
      streams.emplace_back();
      continue;
    }
    const auto data = file->stream_data(file->resolve(xobjects->begin()->second));
    streams.emplace_back(reinterpret_cast<const char*>(data.data.data()), static_cast<qsizetype>(data.data.size()));
  }
  return streams;
}

QByteArray two_page_editable_pdf_bytes() {
  const std::string first = "0 0 1 rg 10 20 100 50 re f";
  const std::string second = "1 0 0 rg 5 5 40 40 re f";
  const std::vector<std::string> objects = {
      "<</Type/Catalog/Pages 2 0 R>>",
      "<</Type/Pages/Kids[3 0 R 5 0 R]/Count 2>>",
      "<</Type/Page/Parent 2 0 R/MediaBox[0 0 200 100]/Contents 4 0 R>>",
      "<</Length " + std::to_string(first.size()) + ">>\nstream\n" + first + "\nendstream",
      "<</Type/Page/Parent 2 0 R/MediaBox[0 0 50 50]/Contents 6 0 R>>",
      "<</Length " + std::to_string(second.size()) + ">>\nstream\n" + second + "\nendstream",
  };
  std::string pdf = "%PDF-1.7\n";
  std::vector<std::size_t> offsets;
  for (std::size_t index = 0; index < objects.size(); ++index) {
    offsets.push_back(pdf.size());
    pdf += std::to_string(index + 1) + " 0 obj\n" + objects[index] + "\nendobj\n";
  }
  const std::size_t xref_offset = pdf.size();
  pdf += "xref\n0 " + std::to_string(objects.size() + 1) + "\n0000000000 65535 f \n";
  for (const auto offset : offsets) {
    pdf += QStringLiteral("%1 00000 n \n").arg(offset, 10, 10, QLatin1Char('0')).toStdString();
  }
  pdf += "trailer\n<</Size " + std::to_string(objects.size() + 1) +
         "/Root 1 0 R>>\nstartxref\n" + std::to_string(xref_offset) + "\n%%EOF\n";
  return QByteArray::fromStdString(pdf);
}

QByteArray editable_pdf_bytes() {
  const std::string content =
      "0 0 1 rg 10 20 100 50 re f "
      "BT /F1 18 Tf 1 0 0 1 20 60 Tm 1 0 0 rg (Hello PDF) Tj ET";
  const std::vector<std::string> objects = {
      "<</Type/Catalog/Pages 2 0 R>>",
      "<</Type/Pages/Kids[3 0 R]/Count 1>>",
      "<</Type/Page/Parent 2 0 R/MediaBox[0 0 200 100]/Contents 4 0 R"
      "/Resources<</Font<</F1 5 0 R>>>>>>",
      "<</Length " + std::to_string(content.size()) + ">>\nstream\n" + content + "\nendstream",
      "<</Type/Font/Subtype/Type1/BaseFont/Helvetica/Encoding/WinAnsiEncoding"
      "/FirstChar 32/LastChar 122/Widths[500]>>",
  };
  std::string pdf = "%PDF-1.7\n";
  std::vector<std::size_t> offsets;
  for (std::size_t index = 0; index < objects.size(); ++index) {
    offsets.push_back(pdf.size());
    pdf += std::to_string(index + 1) + " 0 obj\n" + objects[index] + "\nendobj\n";
  }
  const std::size_t xref_offset = pdf.size();
  pdf += "xref\n0 " + std::to_string(objects.size() + 1) + "\n0000000000 65535 f \n";
  for (const auto offset : offsets) {
    pdf += QStringLiteral("%1 00000 n \n").arg(offset, 10, 10, QLatin1Char('0')).toStdString();
  }
  pdf += "trailer\n<</Size " + std::to_string(objects.size() + 1) +
         "/Root 1 0 R>>\nstartxref\n" + std::to_string(xref_offset) + "\n%%EOF\n";
  return QByteArray::fromStdString(pdf);
}

#endif

#if defined(PATCHY_HAVE_QT_PDF)
// An untouched Photoshop type layer (raster status "psd_raster_preview", transform anchored
// at the baseline) exports as real text placed on its raster's ink, and a layer whose font is
// not installed is drawn in the substitute face with a notice naming the font, unless the
// missing-fonts-as-images option asks for its pixels instead.
void ui_pdf_export_editable_keeps_psd_preview_text_and_substitutes_missing_fonts() {
  ensure_artifact_dir();
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  const auto installed_family = patchy::test::visual_test_font().family();
  if (installed_family.isEmpty() || !QFontDatabase::families().contains(installed_family)) {
    patchy::test::skip("no registered UI-default family for the PSD preview text export test");
  }

  patchy::Document built(320, 200, patchy::PixelFormat::rgba8());
  built.print_settings().horizontal_ppi = 72.0;
  built.print_settings().vertical_ppi = 72.0;
  built.add_pixel_layer("Paper", solid_pixels(320, 200, patchy::PixelFormat::rgba8(), QColor(255, 255, 255)));
  const auto add_text_layer = [&built](const char* name, const char* text, const QString& family, int top) {
    patchy::Layer layer(built.allocate_layer_id(), name,
                        solid_pixels(1, 1, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));
    layer.set_bounds(patchy::Rect{24, top, 1, 1});
    layer.metadata()[patchy::kLayerMetadataText] = text;
    layer.metadata()[patchy::kLayerMetadataTextFlow] = "point";
    layer.metadata()[patchy::kLayerMetadataTextFont] = family.toStdString();
    layer.metadata()[patchy::kLayerMetadataTextSize] = "28";
    layer.metadata()[patchy::kLayerMetadataTextColor] = "#101010";
    layer.metadata()[patchy::kLayerMetadataTextLayoutMode] = patchy::kTextLayoutModePhotoshop;
    const auto id = layer.id();
    built.add_layer(std::move(layer));
    return id;
  };
  const auto title_id = add_text_layer("Title", "Poster Title", installed_family, 30);
  const auto missing_id = add_text_layer("Missing", "Missing", QStringLiteral("PatchyDefinitelyMissingFont123456"), 110);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(built), QStringLiteral("PSD preview text PDF"));
  show_window(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  // Give both layers a real raster (Patchy's render stands in for Photoshop's: the missing
  // family renders in the same fallback face the PDF will draw), then make them look like
  // untouched imports: preview status and a PSD transform anchored one ascent BELOW the
  // layout top, which is where Photoshop puts the first baseline. Drawing through that
  // transform as-is would land the text an ascent low.
  for (const auto id : {title_id, missing_id}) {
    auto* layer = document.find_layer(id);
    CHECK(layer != nullptr);
    if (layer == nullptr) {
      return;
    }
    CHECK(patchy::ui::MainWindowTestAccess::apply_text_warp(window, *layer, patchy::TextWarp{}));
    CHECK(!layer->pixels().empty());
    const auto baseline_anchored = patchy::serialize_layer_affine_transform(
        {1.0, 0.0, 0.0, 1.0, static_cast<double>(layer->bounds().x), static_cast<double>(layer->bounds().y) + 24.0});
    layer->metadata()[patchy::kLayerMetadataTextTransform] = baseline_anchored;
    layer->metadata()[patchy::kLayerMetadataPsdTextTransform] = baseline_anchored;
    layer->metadata()[patchy::kLayerMetadataTextRasterStatus] = "psd_raster_preview";
  }
  const auto* title = std::as_const(document).find_layer(title_id);
  const auto* missing = std::as_const(document).find_layer(missing_id);
  CHECK(title != nullptr && missing != nullptr);
  if (title == nullptr || missing == nullptr) {
    return;
  }
  const QRect title_region(title->bounds().x - 4, title->bounds().y - 4, title->bounds().width + 8,
                           title->bounds().height + 8);
  const QRect missing_region(missing->bounds().x - 4, missing->bounds().y - 4, missing->bounds().width + 8,
                             missing->bounds().height + 8);
  const QImage composite = patchy::ui::qimage_from_document(std::as_const(document), true);
  const auto composite_title_ink = dark_ink_bounds(composite, title_region);
  const auto composite_missing_ink = dark_ink_bounds(composite, missing_region);
  CHECK(composite_title_ink.has_value() && composite_missing_ink.has_value());
  if (!composite_title_ink.has_value() || !composite_missing_ink.has_value()) {
    return;
  }

  const auto rects_close = [](const QRect& a, const QRect& b, int tolerance) {
    return std::abs(a.left() - b.left()) <= tolerance && std::abs(a.top() - b.top()) <= tolerance &&
           std::abs(a.right() - b.right()) <= tolerance && std::abs(a.bottom() - b.bottom()) <= tolerance;
  };
  const auto export_and_check = [&](bool missing_fonts_as_images, const char* suffix) {
    const auto path = QStringLiteral("test-artifacts/ui_pdf_export_editable_psd_preview_%1.pdf").arg(QLatin1String(suffix));
    QFile::remove(path);
    std::vector<std::string> notices;
    patchy::ui::write_pdf_document_file(std::as_const(document), path,
                                        patchy::ui::PdfExportOptions{true, true, missing_fonts_as_images}, &notices);
    CHECK(QFileInfo(path).isFile());
    for (const auto& notice : notices) {
      std::printf("[pdf] %s notice: %s\n", suffix, notice.c_str());
    }
    // Exactly one notice either way, about the missing family, never about the title.
    CHECK(notices.size() == 1);
    bool names_missing_font = false;
    for (const auto& notice : notices) {
      CHECK(notice.find("Title") == std::string::npos);
      names_missing_font = names_missing_font || (notice.find("'Missing'") != std::string::npos &&
                                                  notice.find("PatchyDefinitelyMissingFont123456") != std::string::npos &&
                                                  notice.find("not installed") != std::string::npos);
      if (missing_fonts_as_images) {
        CHECK(notice.find("flattened to an image") != std::string::npos);
      } else {
        CHECK(notice.find("substitute font") != std::string::npos);
      }
    }
    CHECK(names_missing_font);

    const QByteArray bytes = read_file_bytes(path);
    CHECK(bytes.contains("/FontFile"));
    patchy::pdf::VectorReadOptions read_options;
    read_options.pixels_per_point = 1.0;
    const auto read = patchy::pdf::read_page_as_vectors(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                      static_cast<std::size_t>(bytes.size())),
        read_options);
    std::vector<std::string> texts;
    const std::function<void(const std::vector<patchy::Layer>&)> visit = [&](const std::vector<patchy::Layer>& layers) {
      for (const auto& layer : layers) {
        if (const auto found = layer.metadata().find(patchy::kLayerMetadataText); found != layer.metadata().end()) {
          texts.push_back(found->second);
        }
        visit(layer.children());
      }
    };
    visit(std::as_const(read.document).layers());
    for (const auto& text : texts) {
      std::printf("[pdf] %s text read back: '%s'\n", suffix, text.c_str());
    }
    CHECK(read.text_layers == (missing_fonts_as_images ? 1 : 2));
    CHECK(std::find(texts.begin(), texts.end(), "Poster Title") != texts.end());
    CHECK((std::find(texts.begin(), texts.end(), "Missing") != texts.end()) == !missing_fonts_as_images);
    CHECK(read.image_layers >= (missing_fonts_as_images ? 2 : 1));

    QPdfDocument reader;
    CHECK(reader.load(path) == QPdfDocument::Error::None);
    const QImage rendered = reader.render(0, QSize(320, 200));
    CHECK(!rendered.isNull());
    const double delta = mean_rgb_delta_over_white(rendered, composite);
    if (delta >= 6.0) {
      std::fprintf(stderr, "[pdf] psd preview export (%s) mean delta %f\n", suffix, delta);
      rendered.save(QStringLiteral("test-artifacts/ui_pdf_export_editable_psd_preview_%1_pdfium.png").arg(QLatin1String(suffix)));
      composite.save(QStringLiteral("test-artifacts/ui_pdf_export_editable_psd_preview_%1_composite.png").arg(QLatin1String(suffix)));
    }
    CHECK(delta < 6.0);
    // Placement, directly: the real text's ink box sits on the raster's ink box. A draw
    // through the baseline-anchored transform would put it ~24 px lower.
    const auto rendered_title_ink = dark_ink_bounds(rendered, title_region);
    const auto rendered_missing_ink = dark_ink_bounds(rendered, missing_region);
    CHECK(rendered_title_ink.has_value() && rendered_missing_ink.has_value());
    if (rendered_title_ink.has_value()) {
      if (!rects_close(*rendered_title_ink, *composite_title_ink, 2)) {
        std::fprintf(stderr, "[pdf] title ink %d,%d-%d,%d vs composite %d,%d-%d,%d\n", rendered_title_ink->left(),
                     rendered_title_ink->top(), rendered_title_ink->right(), rendered_title_ink->bottom(),
                     composite_title_ink->left(), composite_title_ink->top(), composite_title_ink->right(),
                     composite_title_ink->bottom());
      }
      CHECK(rects_close(*rendered_title_ink, *composite_title_ink, 2));
    }
    if (rendered_missing_ink.has_value()) {
      CHECK(rects_close(*rendered_missing_ink, *composite_missing_ink, 2));
    }
  };
  export_and_check(false, "substitute");
  export_and_check(true, "images");
}
#endif

#if defined(PATCHY_HAVE_QT_PDF)
// The features that ride Qt's PDF engine rather than an image: a gradient fill, an
// inside-aligned stroke (double width under a self clip), a group clipped by a vector
// mask, and constant layer opacity. No notices, and PDFium agrees with the canvas.
void ui_pdf_export_editable_gradients_clips_and_opacity_render_like_canvas() {
  ensure_artifact_dir();
  patchy::Document document(240, 160, patchy::PixelFormat::rgba8());
  document.print_settings().horizontal_ppi = 72.0;
  document.print_settings().vertical_ppi = 72.0;
  document.add_pixel_layer("Paper", solid_pixels(240, 160, patchy::PixelFormat::rgba8(), QColor(250, 250, 245)));

  // A left-to-right red -> blue gradient inside an ellipse with an inside stroke.
  {
    patchy::LiveShapeParams params;
    params.kind = patchy::LiveShapeKind::Ellipse;
    params.left = 20;
    params.top = 20;
    params.right = 120;
    params.bottom = 100;
    params.index = 0;
    patchy::populate_live_shape_box_corners(params);
    patchy::VectorShapeContent content;
    content.path.subpaths = patchy::generate_live_shape_subpaths(params);
    content.origination = {params};
    content.fill.kind = patchy::VectorFillKind::Gradient;
    content.fill.gradient.type = patchy::LayerStyleGradientType::Linear;
    content.fill.gradient.angle_degrees = 0.0F;
    content.fill.gradient.interpolation = patchy::GradientInterpolationMethod::Linear;
    content.fill.gradient.color_stops = {{0.0F, {220, 30, 30}}, {1.0F, {30, 30, 220}}};
    content.fill.gradient.alpha_stops = {{0.0F, 1.0F}, {1.0F, 1.0F}};
    content.stroke.enabled = true;
    content.stroke.width = 6.0;
    content.stroke.alignment = patchy::VectorStrokeAlignment::Inside;
    content.stroke.content.kind = patchy::VectorFillKind::Solid;
    content.stroke.content.color = {20, 120, 20};
    patchy::Layer layer(document.allocate_layer_id(), "Gradient Ellipse", patchy::LayerKind::Pixel);
    layer.metadata()[patchy::kLayerMetadataVectorShape] = "1";
    patchy::mark_layer_vector_block_dirty(layer);
    layer.set_vector_shape(std::move(content));
    patchy::update_vector_shape_raster(layer, patchy::Rect::from_size(document.width(), document.height()),
                                       &document.metadata().patterns);
    document.add_layer(std::move(layer));
  }

  // A group whose vector mask (a square) clips a larger shape child.
  {
    patchy::Layer group(document.allocate_layer_id(), "Clipped Group", patchy::LayerKind::Group);
    group.add_child(solid_rect_shape_layer(document, "Wide", 130, 20, 230, 100, {240, 180, 20}, 0.0));
    patchy::LiveShapeParams mask_params;
    mask_params.kind = patchy::LiveShapeKind::Rectangle;
    mask_params.left = 150;
    mask_params.top = 40;
    mask_params.right = 210;
    mask_params.bottom = 80;
    mask_params.index = 0;
    patchy::populate_live_shape_box_corners(mask_params);
    patchy::LayerVectorMask mask;
    mask.path.subpaths = patchy::generate_live_shape_subpaths(mask_params);
    group.set_vector_mask(std::move(mask));
    // The compositor reads the mask's baked coverage cache, never the path.
    patchy::update_vector_mask_raster(group, patchy::Rect::from_size(document.width(), document.height()));
    document.add_layer(std::move(group));
  }

  // A half-transparent shape over the paper.
  {
    auto layer = solid_rect_shape_layer(document, "Ghost", 20, 110, 220, 150, {0, 0, 0}, 0.0);
    layer.set_opacity(0.5F);
    document.add_layer(std::move(layer));
  }

  const auto path = QStringLiteral("test-artifacts/ui_pdf_export_editable_paint.pdf");
  QFile::remove(path);
  std::vector<std::string> notices;
  patchy::ui::write_pdf_document_file(document, path, patchy::ui::PdfExportOptions{true, true}, &notices);
  for (const auto& notice : notices) {
    std::printf("[pdf] unexpected notice: %s\n", notice.c_str());
  }
  CHECK(notices.empty());
  const QByteArray bytes = read_file_bytes(path);
  CHECK(bytes.contains("/Shading"));  // the gradient is a real shading, not a picture

  QPdfDocument reader;
  CHECK(reader.load(path) == QPdfDocument::Error::None);
  const QImage rendered = reader.render(0, QSize(240, 160));
  CHECK(!rendered.isNull());
  const QImage composite = patchy::ui::qimage_from_document(document, true);
  const double delta = mean_rgb_delta_over_white(rendered, composite);
  if (delta >= 6.0) {
    std::fprintf(stderr, "[pdf] editable paint export mean delta %f\n", delta);
    rendered.save(QStringLiteral("test-artifacts/ui_pdf_export_editable_paint_pdfium.png"));
    composite.save(QStringLiteral("test-artifacts/ui_pdf_export_editable_paint_composite.png"));
  }
  CHECK(delta < 6.0);
  // Gradient ends inside the ellipse, inside stroke at the rim, mask clip, and opacity.
  CHECK(rendered.pixelColor(34, 60).red() > rendered.pixelColor(106, 60).red());
  CHECK(rendered.pixelColor(106, 60).blue() > rendered.pixelColor(34, 60).blue());
  CHECK(color_close(rendered.pixelColor(70, 22), QColor(20, 120, 20), 6));  // the inside stroke band
  CHECK(color_close(rendered.pixelColor(180, 60), QColor(240, 180, 20), 2));  // inside the mask
  CHECK(color_close(rendered.pixelColor(140, 30), QColor(250, 250, 245), 2));  // clipped away
  CHECK(color_close(over_white(rendered).pixelColor(120, 130), QColor(125, 125, 122), 3));
}
#endif

#if defined(PATCHY_HAVE_QT_PDF)
// A pattern fill's angle is counterclockwise-positive (the Photoshop dial, PatternTileSampler)
// while QTransform::rotate() is clockwise on the y-down page, so the exported tiling pattern
// must turn the same way as the layer's own raster: bands that start horizontal climb to the
// right at +30 degrees.
void ui_pdf_export_editable_pattern_fill_rotates_like_canvas() {
  ensure_artifact_dir();
  patchy::Document document(200, 200, patchy::PixelFormat::rgba8());
  document.print_settings().horizontal_ppi = 72.0;
  document.print_settings().vertical_ppi = 72.0;
  document.add_pixel_layer("Paper", solid_pixels(200, 200, patchy::PixelFormat::rgba8(), QColor(250, 250, 245)));

  // Horizontal bands: the top half of the tile dark, the bottom half light.
  const QColor dark(20, 40, 160);
  const QColor light(240, 220, 60);
  patchy::PatternResource bands;
  bands.id = "3f0e9d52-6c1b-4a77-9e21-5b8c4d1a7e90";
  bands.name = "Bands";
  bands.tile = solid_pixels(16, 32, patchy::PixelFormat::rgba8(), light);
  fill_pixel_rect(bands.tile, QRect(0, 0, 16, 16), dark);
  document.metadata().patterns.adopt(bands);

  constexpr double kAngleDegrees = 30.0;
  constexpr double kPhaseX = 5.0;
  constexpr double kPhaseY = 3.0;
  {
    patchy::LiveShapeParams params;
    params.kind = patchy::LiveShapeKind::Rectangle;
    params.left = 20;
    params.top = 20;
    params.right = 180;
    params.bottom = 180;
    params.index = 0;
    patchy::populate_live_shape_box_corners(params);
    patchy::VectorShapeContent content;
    content.path.subpaths = patchy::generate_live_shape_subpaths(params);
    content.origination = {params};
    content.fill.kind = patchy::VectorFillKind::Pattern;
    content.fill.pattern_id = bands.id;
    content.fill.pattern_name = bands.name;
    content.fill.pattern_angle_degrees = kAngleDegrees;
    content.fill.pattern_linked = false;
    content.fill.pattern_phase_x = kPhaseX;
    content.fill.pattern_phase_y = kPhaseY;
    patchy::Layer layer(document.allocate_layer_id(), "Banded", patchy::LayerKind::Pixel);
    layer.metadata()[patchy::kLayerMetadataVectorShape] = "1";
    patchy::mark_layer_vector_block_dirty(layer);
    layer.set_vector_shape(std::move(content));
    patchy::update_vector_shape_raster(layer, patchy::Rect::from_size(document.width(), document.height()),
                                       &document.metadata().patterns);
    document.add_layer(std::move(layer));
  }

  const auto path = QStringLiteral("test-artifacts/ui_pdf_export_editable_pattern.pdf");
  QFile::remove(path);
  std::vector<std::string> notices;
  patchy::ui::write_pdf_document_file(document, path, patchy::ui::PdfExportOptions{true, true}, &notices);
  for (const auto& notice : notices) {
    std::printf("[pdf] unexpected notice: %s\n", notice.c_str());
  }
  CHECK(notices.empty());
  CHECK(read_file_bytes(path).contains("/PatternType"));  // a real tiling pattern, not a picture

  QPdfDocument reader;
  CHECK(reader.load(path) == QPdfDocument::Error::None);
  const QImage page = reader.render(0, QSize(200, 200));
  CHECK(!page.isNull());
  const QImage rendered = over_white(page);
  const QImage composite = over_white(patchy::ui::qimage_from_document(document, true));

  // The middle of a dark band, from the sampler's document-to-tile mapping
  // (tile = R(angle) @ (p - phase)): tile row 7.5 of the fourth repeat.
  constexpr double kPi = 3.14159265358979323846;
  const double cosine = std::cos(kAngleDegrees * kPi / 180.0);
  const double sine = std::sin(kAngleDegrees * kPi / 180.0);
  const double tile_row = 7.5 + 32.0 * 3.0;
  const QPointF start(kPhaseX + tile_row * sine, kPhaseY + tile_row * cosine);
  // The share of dark samples 40 px either way from `start` along `direction`.
  const auto dark_share = [&](const QImage& image, QPointF direction) {
    int dark_samples = 0;
    int samples = 0;
    for (int step = -40; step <= 40; ++step) {
      const QPoint pixel(static_cast<int>(std::floor(start.x() + direction.x() * step)),
                         static_cast<int>(std::floor(start.y() + direction.y() * step)));
      dark_samples += image.pixelColor(pixel).red() < 130 ? 1 : 0;
      ++samples;
    }
    return static_cast<double>(dark_samples) / samples;
  };
  const QPointF climbing(cosine, -sine);  // counterclockwise on screen: up to the right
  const QPointF falling(cosine, sine);
  const double composite_climbing = dark_share(composite, climbing);
  const double composite_falling = dark_share(composite, falling);
  const double rendered_climbing = dark_share(rendered, climbing);
  const double rendered_falling = dark_share(rendered, falling);
  const double delta = mean_rgb_delta_over_white(rendered, composite);
  // Band edges antialias differently and PDFium draws faint tile seams: the delta is
  // about 4 when the bands line up and about 54 when they are mirrored.
  constexpr double kMaxDelta = 12.0;
  const bool matches = rendered_climbing > 0.95 && rendered_falling < 0.75 && delta < kMaxDelta;
  if (!matches) {
    std::fprintf(stderr,
                 "[pdf] pattern export: dark share along the climbing band %f (canvas %f), across it %f (canvas %f), "
                 "mean delta %f\n",
                 rendered_climbing, composite_climbing, rendered_falling, composite_falling, delta);
    rendered.save(QStringLiteral("test-artifacts/ui_pdf_export_editable_pattern_pdfium.png"));
    composite.save(QStringLiteral("test-artifacts/ui_pdf_export_editable_pattern_composite.png"));
  }
  // The layer's own raster is the reference: its bands climb to the right.
  CHECK(composite_climbing > 0.95);
  CHECK(composite_falling < 0.75);
  CHECK(rendered_climbing > 0.95);
  CHECK(rendered_falling < 0.75);
  CHECK(delta < kMaxDelta);
}
#endif

#if defined(PATCHY_HAVE_QT_PDF)
// Qt's PDF engine writes no blend modes, so a Multiply layer is a barrier: everything
// below it merges into one image (reported), hidden layers vanish, and the page still
// composites exactly like the canvas.
void ui_pdf_export_editable_flattens_blend_modes_with_notice() {
  ensure_artifact_dir();
  patchy::Document document(100, 100, patchy::PixelFormat::rgba8());
  document.print_settings().horizontal_ppi = 72.0;
  document.print_settings().vertical_ppi = 72.0;
  document.add_pixel_layer("Base", solid_pixels(100, 100, patchy::PixelFormat::rgba8(), QColor(200, 200, 200)));
  auto hidden_pixels = solid_pixels(100, 100, patchy::PixelFormat::rgba8(), QColor(255, 0, 0));
  patchy::Layer hidden(document.allocate_layer_id(), "Hidden", std::move(hidden_pixels));
  hidden.set_visible(false);
  document.add_layer(std::move(hidden));
  auto multiply_pixels = solid_pixels(100, 100, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(multiply_pixels, QRect(20, 20, 60, 60), QColor(128, 128, 128));
  patchy::Layer multiply(document.allocate_layer_id(), "Darken", std::move(multiply_pixels));
  multiply.set_blend_mode(patchy::BlendMode::Multiply);
  document.add_layer(std::move(multiply));
  document.add_layer(solid_rect_shape_layer(document, "Badge", 70, 70, 95, 95, {10, 200, 30}, 0.0));

  const auto path = QStringLiteral("test-artifacts/ui_pdf_export_editable_blend.pdf");
  QFile::remove(path);
  std::vector<std::string> notices;
  patchy::ui::write_pdf_document_file(document, path, patchy::ui::PdfExportOptions{true, true}, &notices);
  bool merged_notice = false;
  for (const auto& notice : notices) {
    merged_notice = merged_notice || (notice.find("Merged") != std::string::npos &&
                                      notice.find("Darken") != std::string::npos);
    CHECK(notice.find("Hidden") == std::string::npos);  // hidden layers are not "lost", they were never drawn
  }
  CHECK(merged_notice);

  const QByteArray bytes = read_file_bytes(path);
  CHECK(bytes.contains("/Image"));
  CHECK(!bytes.contains("/FontFile"));  // no text on this page, so no embedded face

  QPdfDocument reader;
  CHECK(reader.load(path) == QPdfDocument::Error::None);
  const QImage rendered = reader.render(0, QSize(100, 100));
  CHECK(!rendered.isNull());
  // 200 x 128 / 255 = 100: the multiply result survives inside the merged chunk.
  CHECK(color_close(rendered.pixelColor(50, 50), QColor(100, 100, 100), 2));
  CHECK(color_close(rendered.pixelColor(5, 5), QColor(200, 200, 200), 2));
  // The shape above the barrier stays a real path drawn on top.
  CHECK(color_close(rendered.pixelColor(82, 82), QColor(10, 200, 30), 2));
  // The flat mode is untouched by the option: no notices, still one picture.
  std::vector<std::string> flat_notices;
  patchy::ui::write_pdf_document_file(document, QStringLiteral("test-artifacts/ui_pdf_export_flat_check.pdf"),
                                      patchy::ui::PdfExportOptions{true, false}, &flat_notices);
  CHECK(flat_notices.empty());
}
#endif

#if defined(PATCHY_HAVE_QT_PDF)
// Registering a system font FILE as an application font makes Qt unable to embed that
// family in a PDF: QPdfEngine draws every glyph as a filled path instead, so editable
// PDF export loses its text (August 2026 - Segoe UI text from a PDF import came back out
// as shape layers). The UI-font bootstrap must therefore register nothing when the family
// is already installed.
void ui_font_bootstrap_never_registers_installed_families() {
  const QStringList with_arial = {QStringLiteral("Arial"), QStringLiteral("Consolas"), QStringLiteral("Tahoma")};
  CHECK(patchy::ui::installed_ui_font_family(with_arial) == QStringLiteral("Arial"));
  CHECK(patchy::ui::ui_font_files_to_register(with_arial).isEmpty());

  // Case-insensitive, and the preference order falls through to the next family.
  const QStringList with_segoe = {QStringLiteral("segoe ui"), QStringLiteral("Consolas")};
  CHECK(patchy::ui::installed_ui_font_family(with_segoe) == QStringLiteral("Segoe UI"));
  CHECK(patchy::ui::ui_font_files_to_register(with_segoe).isEmpty());
  const QStringList with_calibri = {QStringLiteral("Calibri")};
  CHECK(patchy::ui::installed_ui_font_family(with_calibri) == QStringLiteral("Calibri"));
  CHECK(patchy::ui::ui_font_files_to_register(with_calibri).isEmpty());

  // Only a Windows install with none of them falls back to registering files, where a UI
  // font at all beats PDF-embeddable text.
  const QStringList without = {QStringLiteral("Consolas"), QStringLiteral("Tahoma")};
  CHECK(patchy::ui::installed_ui_font_family(without).isEmpty());
  const auto files = patchy::ui::ui_font_files_to_register(without);
  CHECK(!files.isEmpty());
  CHECK(files.front().endsWith(QStringLiteral("arial.ttf")));

  // The real font database this suite runs against must not need any of them either:
  // the fixture fonts the harness registers are exactly the ones tests use.
  CHECK(patchy::ui::windows_ui_font_candidates().size() == 3);
}
#endif

#if defined(PATCHY_HAVE_QT_PDF)
// The codec and colour space that land in the file, and the pixels a decoder that is
// not the writer (PDFium) reads back. File sizes are never asserted: they flip with the
// content.
void ui_pdf_export_image_pages_pick_codec_and_channels() {
  ensure_artifact_dir();
  const auto gray = ramp_document(200, 100, true);
  const auto color = ramp_document(200, 100, false);
  const QImage gray_reference = patchy::ui::flat_export_qimage(gray, true);
  const QImage color_reference = patchy::ui::flat_export_qimage(color, true);

  // Gray, lossy: one-channel JPEG, no mask (every pixel is opaque).
  {
    const auto path = QStringLiteral("test-artifacts/ui_pdf_image_gray_jpeg.pdf");
    QFile::remove(path);
    patchy::ui::PdfExportOptions options;
    CHECK(patchy::ui::apply_pdf_image_quality(QStringLiteral("high"), options));
    patchy::ui::write_pdf_document_file(gray, path, options);
    const auto bytes = read_file_bytes(path);
    CHECK(bytes.contains("/DCTDecode"));
    CHECK(bytes.contains("/DeviceGray"));
    CHECK(!bytes.contains("/DeviceRGB"));
    CHECK(!bytes.contains("/SMask"));
    CHECK(!bytes.contains("/FlateDecode"));
    CHECK(bytes.contains("/Producer (Patchy)"));
    CHECK(mean_rgb_delta_over_white(render_pdf_page(path, 0, QSize(200, 100)), gray_reference) < 2.0);
  }
  // Gray, lossless: one-channel Flate, pixel exact.
  {
    const auto path = QStringLiteral("test-artifacts/ui_pdf_image_gray_flate.pdf");
    QFile::remove(path);
    patchy::ui::write_pdf_document_file(gray, path, patchy::ui::PdfExportOptions{true});
    const auto bytes = read_file_bytes(path);
    CHECK(bytes.contains("/FlateDecode"));
    CHECK(bytes.contains("/DeviceGray"));
    CHECK(!bytes.contains("/DeviceRGB"));
    CHECK(!bytes.contains("/DCTDecode"));
    CHECK(mean_rgb_delta_over_white(render_pdf_page(path, 0, QSize(200, 100)), gray_reference) < 0.01);
  }
  // Colour stays three channels in both modes; auto_grayscale off keeps a gray page RGB.
  {
    const auto path = QStringLiteral("test-artifacts/ui_pdf_image_color_flate.pdf");
    QFile::remove(path);
    patchy::ui::write_pdf_document_file(color, path, patchy::ui::PdfExportOptions{true});
    const auto bytes = read_file_bytes(path);
    CHECK(bytes.contains("/DeviceRGB"));
    CHECK(!bytes.contains("/DeviceGray"));
    CHECK(mean_rgb_delta_over_white(render_pdf_page(path, 0, QSize(200, 100)), color_reference) < 0.01);

    const auto lossy_path = QStringLiteral("test-artifacts/ui_pdf_image_color_jpeg.pdf");
    QFile::remove(lossy_path);
    patchy::ui::PdfExportOptions lossy{false};
    lossy.jpeg_quality = 90;
    patchy::ui::write_pdf_document_file(color, lossy_path, lossy);
    const auto lossy_bytes = read_file_bytes(lossy_path);
    CHECK(lossy_bytes.contains("/DCTDecode"));
    CHECK(lossy_bytes.contains("/DeviceRGB"));
    CHECK(mean_rgb_delta_over_white(render_pdf_page(lossy_path, 0, QSize(200, 100)), color_reference) < 4.0);

    const auto forced_path = QStringLiteral("test-artifacts/ui_pdf_image_gray_forced_rgb.pdf");
    QFile::remove(forced_path);
    patchy::ui::PdfExportOptions forced{true};
    forced.auto_grayscale = false;
    patchy::ui::write_pdf_document_file(gray, forced_path, forced);
    CHECK(read_file_bytes(forced_path).contains("/DeviceRGB"));
  }
  // A lower quality writes fewer bytes for the same page: the knob is wired through.
  {
    const auto high_path = QStringLiteral("test-artifacts/ui_pdf_image_q_high.pdf");
    const auto low_path = QStringLiteral("test-artifacts/ui_pdf_image_q_low.pdf");
    patchy::ui::PdfExportOptions high;
    patchy::ui::PdfExportOptions low;
    CHECK(patchy::ui::apply_pdf_image_quality(QStringLiteral("high"), high));
    CHECK(patchy::ui::apply_pdf_image_quality(QStringLiteral("low"), low));
    patchy::ui::write_pdf_document_file(color, high_path, high);
    patchy::ui::write_pdf_document_file(color, low_path, low);
    CHECK(QFileInfo(low_path).size() < QFileInfo(high_path).size());
  }
  // The lossless page also reads back through Patchy's own Qt-free reader as one image.
  {
    const auto bytes = read_file_bytes(QStringLiteral("test-artifacts/ui_pdf_image_gray_flate.pdf"));
    const std::span<const std::uint8_t> span(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                             static_cast<std::size_t>(bytes.size()));
    patchy::pdf::VectorReadOptions read_options;
    read_options.pixels_per_point = 100.0 / 72.0;
    const auto result = patchy::pdf::read_page_as_vectors(span, read_options);
    CHECK(result.image_layers == 1);
    CHECK(result.shape_layers == 0);
    CHECK(result.document.width() == 200);
    CHECK(result.document.height() == 100);
  }
}
#endif

#if defined(PATCHY_HAVE_QT_PDF)
// Editable mode routes by content: a page that is one pixel layer can only ever be one
// image, so it takes the image writer (and its codecs); a page with a shape keeps Qt's
// engine, and in a multi-page file one such page sends every page that way.
void ui_pdf_export_editable_routes_single_raster_pages_to_image_writer() {
  ensure_artifact_dir();
  const auto raster = ramp_document(120, 80, true);
  CHECK(patchy::ui::pdf_detail::document_is_single_raster_layer(raster));

  patchy::Document with_shape(120, 80, patchy::PixelFormat::rgba8());
  with_shape.add_pixel_layer("Ramp", ramp_pixels(120, 80, true));
  with_shape.add_layer(solid_rect_shape_layer(with_shape, "Box", 10, 10, 60, 40, {200, 30, 30}, 0.0));
  CHECK(!patchy::ui::pdf_detail::document_is_single_raster_layer(with_shape));

  patchy::Document two_layers(120, 80, patchy::PixelFormat::rgba8());
  two_layers.add_pixel_layer("Bottom", ramp_pixels(120, 80, true));
  two_layers.add_pixel_layer("Top", ramp_pixels(120, 80, false));
  CHECK(!patchy::ui::pdf_detail::document_is_single_raster_layer(two_layers));
  // A hidden second layer is not content: the page is one image again.
  two_layers.layers()[1].set_visible(false);
  CHECK(patchy::ui::pdf_detail::document_is_single_raster_layer(two_layers));
  patchy::Document empty(120, 80, patchy::PixelFormat::rgba8());
  CHECK(!patchy::ui::pdf_detail::document_is_single_raster_layer(empty));

  patchy::ui::PdfExportOptions editable;
  CHECK(patchy::ui::apply_pdf_image_quality(QStringLiteral("medium"), editable));
  editable.editable_layers = true;

  const auto raster_path = QStringLiteral("test-artifacts/ui_pdf_editable_raster_page.pdf");
  QFile::remove(raster_path);
  patchy::ui::write_pdf_document_file(raster, raster_path, editable);
  const auto raster_bytes = read_file_bytes(raster_path);
  CHECK(raster_bytes.contains("/Producer (Patchy)"));
  CHECK(raster_bytes.contains("/DeviceGray"));
  CHECK(raster_bytes.contains("/DCTDecode"));

  const auto shape_path = QStringLiteral("test-artifacts/ui_pdf_editable_shape_page.pdf");
  QFile::remove(shape_path);
  patchy::ui::write_pdf_document_file(with_shape, shape_path, editable);
  CHECK(!read_file_bytes(shape_path).contains("/Producer (Patchy)"));

  const auto all_raster_path = QStringLiteral("test-artifacts/ui_pdf_editable_all_raster.pdf");
  const auto mixed_path = QStringLiteral("test-artifacts/ui_pdf_editable_mixed.pdf");
  QFile::remove(all_raster_path);
  QFile::remove(mixed_path);
  const std::array<const patchy::Document*, 2> all_raster{&raster, &raster};
  const std::array<const patchy::Document*, 2> mixed{&raster, &with_shape};
  CHECK(patchy::ui::write_multipage_pdf_file(all_raster, all_raster_path, editable));
  CHECK(patchy::ui::write_multipage_pdf_file(mixed, mixed_path, editable));
  CHECK(read_file_bytes(all_raster_path).contains("/Producer (Patchy)"));
  CHECK(!read_file_bytes(mixed_path).contains("/Producer (Patchy)"));
  QPdfDocument reader;
  CHECK(reader.load(all_raster_path) == QPdfDocument::Error::None);
  CHECK(reader.pageCount() == 2);
  CHECK(reader.load(mixed_path) == QPdfDocument::Error::None);
  CHECK(reader.pageCount() == 2);
  // The mixed file still holds the shape as a path.
  const auto mixed_bytes = read_file_bytes(mixed_path);
  const std::span<const std::uint8_t> mixed_span(reinterpret_cast<const std::uint8_t*>(mixed_bytes.constData()),
                                                 static_cast<std::size_t>(mixed_bytes.size()));
  patchy::pdf::VectorReadOptions second_page;
  second_page.page = 1;
  CHECK(patchy::pdf::read_page_as_vectors(mixed_span, second_page).shape_layers >= 1);
}
#endif

#if defined(PATCHY_HAVE_QT_PDF)
// Many pages through the encode window (more pages than workers in flight), in order,
// each at its own size, and a cancel that leaves no file behind.
void ui_pdf_export_image_pages_keep_order_and_cancel_cleanly() {
  ensure_artifact_dir();
  std::vector<patchy::Document> documents;
  for (int index = 0; index < 9; ++index) {
    patchy::Document document(40 + index * 10, 60, patchy::PixelFormat::rgba8());
    document.print_settings().horizontal_ppi = 72.0;
    document.print_settings().vertical_ppi = 72.0;
    const auto shade = 20 + index * 25;
    document.add_pixel_layer("Page", solid_pixels(document.width(), 60, patchy::PixelFormat::rgba8(),
                                                  QColor(shade, shade, shade)));
    documents.push_back(std::move(document));
  }
  std::vector<const patchy::Document*> pages;
  for (const auto& document : documents) {
    pages.push_back(&document);
  }
  const auto path = QStringLiteral("test-artifacts/ui_pdf_image_pages_order.pdf");
  QFile::remove(path);
  std::vector<int> reported;
  CHECK(patchy::ui::write_multipage_pdf_file(pages, path, patchy::ui::PdfExportOptions{true}, nullptr,
                                             [&reported](int page, int count) {
                                               CHECK(count == 9);
                                               reported.push_back(page);
                                               return true;
                                             }));
  CHECK((reported == std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8, 9}));
  QPdfDocument reader;
  CHECK(reader.load(path) == QPdfDocument::Error::None);
  CHECK(reader.pageCount() == 9);
  for (int index = 0; index < reader.pageCount(); ++index) {
    CHECK(std::abs(reader.pagePointSize(index).width() - (40.0 + index * 10.0)) < 0.5);
    const auto shade = 20 + index * 25;
    const QImage rendered = reader.render(index, QSize(40 + index * 10, 60));
    CHECK(color_close(rendered.pixelColor(20, 30), QColor(shade, shade, shade), 1));
  }

  const auto cancelled_path = QStringLiteral("test-artifacts/ui_pdf_image_pages_cancelled.pdf");
  QFile::remove(cancelled_path);
  CHECK(!patchy::ui::write_multipage_pdf_file(pages, cancelled_path, patchy::ui::PdfExportOptions{true}, nullptr,
                                              [](int page, int) { return page < 6; }));
  CHECK(!QFileInfo::exists(cancelled_path));
}
#endif

#if defined(PATCHY_HAVE_QT_PDF)
// AGENTS.md "Never truncate a user's file in place": every PDF writer goes through a
// sibling temporary file. A cancelled or failed export, through the image writer or Qt's
// engine, leaves the file already at the destination byte for byte and no temporary;
// a finished one replaces it.
void ui_pdf_export_failure_keeps_existing_file() {
  ensure_artifact_dir();
  const auto dir = QFileInfo(QStringLiteral("test-artifacts")).absoluteFilePath() + QStringLiteral("/pdf-atomic");
  remove_test_scratch_dir(dir);
  CHECK(QDir().mkpath(dir));
  const auto entries = [&dir] {
    return QDir(dir).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name);
  };
  const auto red = two_layer_pdf_page(QColor(200, 20, 30));
  const auto blue = two_layer_pdf_page(QColor(30, 20, 200));
  const auto path = dir + QStringLiteral("/existing.pdf");
  patchy::ui::write_pdf_document_file(red, path, patchy::ui::PdfExportOptions{true});
  const QByteArray original = read_file_bytes(path);
  CHECK(original.startsWith("%PDF-"));

  std::array<const patchy::Document*, 2> pages{&blue, &blue};
  const auto cancel_on_page_two = [](int page, int) { return page < 2; };
  // Image writer (flat) and Qt's engine (editable, two layers), each cancelled mid-file.
  CHECK(!patchy::ui::write_multipage_pdf_file(pages, path, patchy::ui::PdfExportOptions{true}, nullptr,
                                              cancel_on_page_two));
  CHECK(read_file_bytes(path) == original);
  CHECK(!patchy::ui::write_multipage_pdf_file(pages, path, patchy::ui::PdfExportOptions{true, true}, nullptr,
                                              cancel_on_page_two));
  CHECK(read_file_bytes(path) == original);
  CHECK(entries() == QStringList{QStringLiteral("existing.pdf")});

  // A destination the rename cannot replace (a folder stands in its place) fails through
  // both engines, keeps the folder, and leaves no temporary.
  const auto blocked = dir + QStringLiteral("/blocked.pdf");
  CHECK(QDir().mkpath(blocked + QStringLiteral("/child")));
  const auto throws = [](auto&& call) {
    try {
      call();
    } catch (const std::exception&) {
      return true;
    }
    return false;
  };
  CHECK(throws([&] { patchy::ui::write_pdf_document_file(blue, blocked, patchy::ui::PdfExportOptions{true}); }));
  CHECK(throws([&] {
    patchy::ui::write_pdf_document_file(blue, blocked, patchy::ui::PdfExportOptions{true, true});
  }));
  CHECK(throws([&] {
    patchy::ui::write_multipage_pdf_file(pages, blocked, patchy::ui::PdfExportOptions{true, true});
  }));
  CHECK(QFileInfo(blocked + QStringLiteral("/child")).isDir());
  // A missing folder cannot even hold the temporary.
  const auto missing = dir + QStringLiteral("/missing/out.pdf");
  CHECK(throws([&] { patchy::ui::write_pdf_document_file(blue, missing, patchy::ui::PdfExportOptions{true}); }));
  CHECK(throws([&] {
    patchy::ui::write_pdf_document_file(blue, missing, patchy::ui::PdfExportOptions{true, true});
  }));
  CHECK(entries() == (QStringList{QStringLiteral("blocked.pdf"), QStringLiteral("existing.pdf")}));

  // Finished exports replace the file, through the flat-save entry point and both engines.
  patchy::ui::ImageSaveOptions save_options;
  save_options.pdf_lossless = true;
  patchy::ui::write_flat_image_file(blue, path, QStringLiteral("pdf"), save_options);
  const QByteArray flat = read_file_bytes(path);
  CHECK(flat.startsWith("%PDF-") && flat != original);
  CHECK(patchy::ui::write_multipage_pdf_file(pages, path, patchy::ui::PdfExportOptions{true, true}));
  QPdfDocument reader;
  CHECK(reader.load(path) == QPdfDocument::Error::None);
  CHECK(reader.pageCount() == 2);
  reader.close();
  CHECK(entries() == (QStringList{QStringLiteral("blocked.pdf"), QStringLiteral("existing.pdf")}));
}
#endif

#if defined(PATCHY_HAVE_QT_PDF)
// AGENTS.md: every file-writing entry point gets a Unicode-path test. The temporary
// file takes the destination's name plus a suffix, so it must survive the same names.
void ui_pdf_export_writes_unicode_paths() {
  const auto dir = patchy::test::unicode_artifact_dir(u8"pdf-export");
  const auto page = two_layer_pdf_page(QColor(20, 160, 40));
  std::array<const patchy::Document*, 2> pages{&page, &page};
  std::vector<std::filesystem::path> expected;
  for (const auto stem : patchy::test::kUnicodePathStems) {
    const auto base = dir / patchy::test::unicode_path_piece(stem);
    auto flat = base;
    flat += ".pdf";
    auto editable = base;
    editable += " editable.pdf";
    auto multipage = base;
    multipage += " pages.pdf";
    patchy::ui::write_pdf_document_file(page, patchy::ui::to_qstring(flat), patchy::ui::PdfExportOptions{true});
    patchy::ui::write_pdf_document_file(page, patchy::ui::to_qstring(editable),
                                        patchy::ui::PdfExportOptions{true, true});
    CHECK(patchy::ui::write_multipage_pdf_file(pages, patchy::ui::to_qstring(multipage),
                                               patchy::ui::PdfExportOptions{true, true}));
    for (const auto& path : {flat, editable, multipage}) {
      QPdfDocument reader;
      CHECK(reader.load(patchy::ui::to_qstring(path)) == QPdfDocument::Error::None);
      CHECK(reader.pageCount() == (path == multipage ? 2 : 1));
      expected.push_back(path);
    }
  }
  auto printed = dir / patchy::test::unicode_path_piece(patchy::test::kUnicodeCombinedStem);
  printed += " print.pdf";
  auto settings = patchy::ui::default_print_settings(page, std::nullopt);
  CHECK(patchy::ui::write_print_pdf(patchy::ui::to_qstring(printed), page, settings,
                                    patchy::ui::default_print_page_layout(), QStringLiteral("page.psd")));
  CHECK(read_file_bytes(patchy::ui::to_qstring(printed)).startsWith("%PDF-"));
  expected.push_back(printed);
  CHECK(patchy::test::directory_holds_only(dir, expected));
}
#endif

#if defined(PATCHY_HAVE_QT_PDF)
// The preset ids are persisted and scripted. The older saveOptions/pdfLossless bool was
// rewritten to true after every flat save of any format, so it cannot be read as a
// choice: with the new key absent the answer is the default preset.
void ui_pdf_image_quality_presets_and_settings() {
  const auto presets = patchy::ui::pdf_image_quality_presets();
  CHECK(presets.size() == 4);
  CHECK(QLatin1String(presets[0].id) == QLatin1String("lossless"));
  CHECK(QLatin1String(presets[1].id) == QLatin1String("high"));
  CHECK(QLatin1String(presets[2].id) == QLatin1String("medium"));
  CHECK(QLatin1String(presets[3].id) == QLatin1String("low"));

  patchy::ui::PdfExportOptions options;
  CHECK(options.lossless);  // the struct default serves scripts and the CLI
  CHECK(patchy::ui::apply_pdf_image_quality(QStringLiteral("medium"), options));
  CHECK(!options.lossless);
  CHECK(options.jpeg_quality == 75);
  CHECK(!patchy::ui::apply_pdf_image_quality(QStringLiteral("ultra"), options));
  CHECK(options.jpeg_quality == 75);  // an unknown id changes nothing
  CHECK(patchy::ui::pdf_image_quality_id(true, 10) == QStringLiteral("lossless"));
  CHECK(patchy::ui::pdf_image_quality_id(false, 94) == QStringLiteral("high"));
  CHECK(patchy::ui::pdf_image_quality_id(false, 70) == QStringLiteral("medium"));
  CHECK(patchy::ui::pdf_image_quality_id(false, 1) == QStringLiteral("low"));

  auto settings = patchy::ui::app_settings();
  const auto previous_quality = settings.value(QStringLiteral("saveOptions/pdfImageQuality"));
  const auto previous_lossless = settings.value(QStringLiteral("saveOptions/pdfLossless"));
  const auto restore = qScopeGuard([previous_quality, previous_lossless] {
    auto restored = patchy::ui::app_settings();
    for (const auto& [key, value] : {std::pair{QStringLiteral("saveOptions/pdfImageQuality"), previous_quality},
                                     std::pair{QStringLiteral("saveOptions/pdfLossless"), previous_lossless}}) {
      if (value.isValid()) {
        restored.setValue(key, value);
      } else {
        restored.remove(key);
      }
    }
  });

  settings.remove(QStringLiteral("saveOptions/pdfImageQuality"));
  settings.setValue(QStringLiteral("saveOptions/pdfLossless"), true);
  CHECK(patchy::ui::stored_pdf_image_quality_id() == QStringLiteral("high"));
  auto defaults = patchy::ui::load_image_save_option_defaults();
  CHECK(!defaults.pdf_lossless);
  CHECK(defaults.pdf_jpeg_quality == 90);

  settings.setValue(QStringLiteral("saveOptions/pdfImageQuality"), QStringLiteral("nonsense"));
  CHECK(patchy::ui::stored_pdf_image_quality_id() == QStringLiteral("high"));

  patchy::ui::store_pdf_image_quality_id(QStringLiteral("lossless"));
  CHECK(settings.value(QStringLiteral("saveOptions/pdfImageQuality")).toString() == QStringLiteral("lossless"));
  CHECK(settings.value(QStringLiteral("saveOptions/pdfLossless")).toBool());  // older builds read this
  CHECK(patchy::ui::load_image_save_option_defaults().pdf_lossless);

  // Saving the defaults after any flat save round-trips the choice instead of resetting it.
  defaults = patchy::ui::load_image_save_option_defaults();
  defaults.pdf_lossless = false;
  defaults.pdf_jpeg_quality = 50;
  patchy::ui::save_image_save_option_defaults(defaults);
  CHECK(patchy::ui::stored_pdf_image_quality_id() == QStringLiteral("low"));
  CHECK(!settings.value(QStringLiteral("saveOptions/pdfLossless")).toBool());
  CHECK(patchy::ui::load_image_save_option_defaults().pdf_jpeg_quality == 50);
  patchy::ui::store_pdf_image_quality_id(QStringLiteral("ultra"));  // ignored
  CHECK(patchy::ui::stored_pdf_image_quality_id() == QStringLiteral("low"));
}
#endif

#if defined(PATCHY_HAVE_QT_PDF)
// The PDF Options dialog offers the presets, starts on the options it was given, and
// returns the pair the chosen preset stands for.
void ui_pdf_options_dialog_offers_image_quality_presets() {
  patchy::ui::ImageSaveOptions defaults;
  defaults.pdf_lossless = false;
  defaults.pdf_jpeg_quality = 90;
  bool saw_dialog = false;
  QTimer::singleShot(0, [&saw_dialog] {
    auto* dialog = find_top_level_dialog(QStringLiteral("pdfSaveOptionsDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* quality = dialog->findChild<QComboBox*>(QStringLiteral("pdfImageQualityCombo"));
    CHECK(quality != nullptr);
    CHECK(dialog->findChild<QCheckBox*>(QStringLiteral("pdfLosslessCheck")) == nullptr);
    if (quality == nullptr) {
      dialog->reject();
      return;
    }
    CHECK(quality->count() == 4);
    CHECK(quality->currentData().toString() == QStringLiteral("high"));
    CHECK(quality->findData(QStringLiteral("lossless")) == 0);
    quality->setCurrentIndex(quality->findData(QStringLiteral("medium")));
    saw_dialog = true;
    dialog->accept();
  });
  const auto chosen = patchy::ui::prompt_image_save_options(nullptr, QStringLiteral("pdf"), defaults);
  CHECK(saw_dialog);
  CHECK(chosen.has_value());
  if (chosen.has_value()) {
    CHECK(!chosen->pdf_lossless);
    CHECK(chosen->pdf_jpeg_quality == 75);
  }
}
#endif

#if defined(PATCHY_HAVE_QT_PDF)
// The point of the whole feature: a scanned PDF opened and exported again comes out
// with the image bytes it went in with. An edited page re-encodes, and only that page;
// undoing the edit brings its original bytes back.
void ui_pdf_pass_through_keeps_original_image_bytes() {
  ensure_artifact_dir();
  const std::vector<QByteArray> jpegs{gray_jpeg_bytes(300, 200, 60), gray_jpeg_bytes(300, 200, 35)};
  CHECK(jpegs[0] != jpegs[1]);
  const auto source_path = QStringLiteral("test-artifacts/ui_pdf_pass_through_source.pdf");
  {
    QFile file(source_path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(scanned_pdf_bytes(jpegs, QSize(300, 200), QSizeF(144.0, 96.0), 90));
  }

  patchy::ui::PdfImportOptions import_options;
  import_options.pages = {0, 1};
  import_options.resolution_ppi = 150;
  import_options.separate_documents = true;
  QString error;
  auto imported = patchy::ui::load_pdf_document(source_path, import_options, QString(), &error);
  CHECK(imported.has_value());
  if (!imported.has_value() || imported->extra_documents.size() != 1) {
    CHECK(false);
    return;
  }
  auto& first = imported->document;
  auto& second = imported->extra_documents.front().document;
  // /Rotate 90 swaps the axes: 96 x 144 pt at 150 ppi.
  CHECK(first.width() == 200 && first.height() == 300);
  const auto first_source = std::as_const(first).metadata().pdf_source_page;
  CHECK(first_source != nullptr);
  if (first_source == nullptr) {
    return;
  }
  CHECK(first_source->composite_hash_valid);
  CHECK(first_source->filter == "DCTDecode");
  CHECK(first_source->rotate == 90);
  CHECK(first_source->document_width == 200 && first_source->document_height == 300);
  CHECK(std::as_const(second).metadata().pdf_source_page != nullptr);

  const std::array<const patchy::Document*, 2> pages{&first, &second};
  patchy::ui::PdfExportOptions options;
  CHECK(patchy::ui::apply_pdf_image_quality(QStringLiteral("low"), options));
  options.editable_layers = true;  // the Export Multi-Page PDF default
  CHECK(options.keep_original_image_data);

  const auto out_path = QStringLiteral("test-artifacts/ui_pdf_pass_through_out.pdf");
  QFile::remove(out_path);
  CHECK(patchy::ui::write_multipage_pdf_file(pages, out_path, options));
  std::vector<int> rotations;
  auto streams = page_image_streams(out_path, &rotations);
  CHECK(streams.size() == 2);
  if (streams.size() != 2) {
    return;
  }
  CHECK(streams[0] == jpegs[0]);
  CHECK(streams[1] == jpegs[1]);
  CHECK((rotations == std::vector<int>{90, 90}));
  // And it still looks like the source to a decoder that is neither writer.
  const QImage original = render_pdf_page(source_path, 0, QSize(200, 300));
  const QImage round_trip = render_pdf_page(out_path, 0, QSize(200, 300));
  CHECK(mean_rgb_delta_over_white(original, round_trip) < 0.01);

  // A second generation passes through too: the written file probes like the source.
  auto reimported = patchy::ui::load_pdf_document(out_path, import_options, QString(), &error);
  CHECK(reimported.has_value());
  if (reimported.has_value()) {
    const auto again = std::as_const(reimported->document).metadata().pdf_source_page;
    CHECK(again != nullptr && again->image_bytes != nullptr &&
          QByteArray(reinterpret_cast<const char*>(again->image_bytes->data()),
                     static_cast<qsizetype>(again->image_bytes->size())) == jpegs[0]);
  }

  // Edit one pixel of page 1: that page re-encodes (gray JPEG at the chosen quality),
  // page 2 still passes through.
  {
    // The page is opaque on its white paper (an RGB layer) or not (RGBA): edit pixel 10.
    auto& layer = first.layers().front();
    const auto pixel = static_cast<std::size_t>(10U * std::as_const(layer).pixels().format().channels);
    auto row = layer.pixels().row(10);
    const auto before = row[pixel];
    row[pixel] = static_cast<std::uint8_t>(before ^ 0xFFU);
    row[pixel + 1U] = row[pixel];
    row[pixel + 2U] = row[pixel];
    const auto edited_path = QStringLiteral("test-artifacts/ui_pdf_pass_through_edited.pdf");
    QFile::remove(edited_path);
    CHECK(patchy::ui::write_multipage_pdf_file(pages, edited_path, options));
    const auto edited = page_image_streams(edited_path);
    CHECK(edited.size() == 2);
    if (edited.size() == 2) {
      CHECK(edited[0] != jpegs[0]);
      CHECK(edited[0].startsWith("\xFF\xD8"));  // a fresh JPEG, not the source's
      CHECK(edited[1] == jpegs[1]);
    }
    // Undo: the same pixels as at import are the same page again.
    row[pixel] = before;
    row[pixel + 1U] = before;
    row[pixel + 2U] = before;
    const auto undone_path = QStringLiteral("test-artifacts/ui_pdf_pass_through_undone.pdf");
    QFile::remove(undone_path);
    CHECK(patchy::ui::write_multipage_pdf_file(pages, undone_path, options));
    const auto undone = page_image_streams(undone_path);
    CHECK(undone.size() == 2 && undone[0] == jpegs[0]);
  }

  // A hidden extra layer changes nothing anyone can see; a visible one does.
  {
    first.add_pixel_layer("Note", solid_pixels(200, 300, patchy::PixelFormat::rgba8(), QColor(255, 0, 0, 90)));
    first.layers().back().set_visible(false);
    const auto hidden_path = QStringLiteral("test-artifacts/ui_pdf_pass_through_hidden_layer.pdf");
    QFile::remove(hidden_path);
    patchy::ui::write_pdf_document_file(first, hidden_path, options);
    const auto hidden = page_image_streams(hidden_path);
    CHECK(hidden.size() == 1 && hidden[0] == jpegs[0]);
    first.layers().back().set_visible(true);
    const auto visible_path = QStringLiteral("test-artifacts/ui_pdf_pass_through_visible_layer.pdf");
    QFile::remove(visible_path);
    patchy::ui::write_pdf_document_file(first, visible_path, options);
    CHECK(!read_file_bytes(visible_path).contains(jpegs[0]));
    first.layers().back().set_visible(false);
  }

  // Off means off, and a changed resolution is a different page even with the same pixels.
  {
    auto forced = options;
    forced.keep_original_image_data = false;
    const auto forced_path = QStringLiteral("test-artifacts/ui_pdf_pass_through_forced.pdf");
    QFile::remove(forced_path);
    patchy::ui::write_pdf_document_file(second, forced_path, forced);
    const auto streams_forced = page_image_streams(forced_path);
    CHECK(streams_forced.size() == 1 && streams_forced[0] != jpegs[1]);

    second.print_settings().horizontal_ppi = 300.0;
    second.print_settings().vertical_ppi = 300.0;
    const auto resized_path = QStringLiteral("test-artifacts/ui_pdf_pass_through_resized.pdf");
    QFile::remove(resized_path);
    patchy::ui::write_pdf_document_file(second, resized_path, options);
    const auto streams_resized = page_image_streams(resized_path);
    CHECK(streams_resized.size() == 1 && streams_resized[0] != jpegs[1]);
  }
}
#endif

#if defined(PATCHY_HAVE_QT_PDF)
// Imports that are not "this document is that page" keep no original data: a trimmed
// page, and several pages stacked as layers.
void ui_pdf_pass_through_is_not_captured_for_trimmed_or_stacked_pages() {
  ensure_artifact_dir();
  // A white border around a dark block, so the trim has something to remove.
  QImage bordered(300, 200, QImage::Format_Grayscale8);
  bordered.fill(255);
  for (int y = 40; y < 160; ++y) {
    std::memset(bordered.scanLine(y) + 60, 30, 180);
  }
  QByteArray jpeg;
  {
    QBuffer buffer(&jpeg);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, "JPEG");
    writer.setQuality(95);
    CHECK(writer.write(bordered));
  }
  const auto source_path = QStringLiteral("test-artifacts/ui_pdf_pass_through_trim_source.pdf");
  {
    QFile file(source_path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(scanned_pdf_bytes({jpeg, jpeg}, QSize(300, 200), QSizeF(144.0, 96.0), 0));
  }
  QString error;
  patchy::ui::PdfImportOptions trimmed;
  trimmed.pages = {0};
  trimmed.resolution_ppi = 150;
  trimmed.separate_documents = true;
  trimmed.trim_to_bounding_box = true;
  const auto trimmed_result = patchy::ui::load_pdf_document(source_path, trimmed, QString(), &error);
  CHECK(trimmed_result.has_value());
  if (trimmed_result.has_value()) {
    CHECK(trimmed_result->document.width() < 300);
    CHECK(std::as_const(trimmed_result->document).metadata().pdf_source_page == nullptr);
  }
  patchy::ui::PdfImportOptions stacked;
  stacked.pages = {0, 1};
  stacked.resolution_ppi = 150;
  const auto stacked_result = patchy::ui::load_pdf_document(source_path, stacked, QString(), &error);
  CHECK(stacked_result.has_value());
  if (stacked_result.has_value()) {
    CHECK(stacked_result->document.layers().size() == 2);
    CHECK(std::as_const(stacked_result->document).metadata().pdf_source_page == nullptr);
  }
  // One page as a layer IS that page.
  stacked.pages = {1};
  const auto single = patchy::ui::load_pdf_document(source_path, stacked, QString(), &error);
  CHECK(single.has_value() && std::as_const(single->document).metadata().pdf_source_page != nullptr);
}
#endif

#if defined(PATCHY_HAVE_QT_PDF)
// The page picker shows before any thumbnail exists (every thumbnail is a PDFium page
// render, and a long scanned PDF used to render them all first), fills them in from a
// timer, and says how much memory the selection will take when that is a real number.
void ui_pdf_import_dialog_fills_thumbnails_lazily_and_estimates_memory() {
  ensure_artifact_dir();
  const auto path = QStringLiteral("test-artifacts/ui_pdf_import_lazy_thumbnails.pdf");
  {
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    // Two 200 x 200 inch pages: at 300 ppi each clamps to 20000 x 20000 px, 1.5 GB of RGBA.
    const std::vector<QByteArray> jpegs{gray_jpeg_bytes(64, 64, 80), gray_jpeg_bytes(64, 64, 70)};
    file.write(scanned_pdf_bytes(jpegs, QSize(64, 64), QSizeF(14400.0, 14400.0), 0));
  }
  patchy::ui::MainWindow window;
  show_window(window);

  constexpr int kPendingRole = Qt::UserRole + 1;
  auto first_look = std::make_shared<bool>(false);
  auto done = std::make_shared<bool>(false);
  auto step = std::make_shared<std::function<bool()>>();
  *step = [first_look, done] {
    auto* dialog = find_top_level_dialog(QStringLiteral("pdfImportDialog"));
    if (dialog == nullptr) {
      return false;
    }
    auto* pages = dialog->findChild<QListWidget*>(QStringLiteral("pdfImportPagesList"));
    auto* size_label = dialog->findChild<QLabel*>(QStringLiteral("pdfImportSizeLabel"));
    auto* resolution = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportResolutionCombo"));
    if (pages == nullptr || size_label == nullptr || resolution == nullptr) {
      return false;
    }
    if (!*first_look) {
      *first_look = true;
      CHECK(pages->count() == 2);
      // Every row has its placeholder icon from the start, so text never shifts.
      for (int row = 0; row < pages->count(); ++row) {
        CHECK(!pages->item(row)->icon().isNull());
      }
      resolution->setCurrentText(QStringLiteral("300"));
      pages->selectAll();
      CHECK(size_label->text().contains(QStringLiteral("20000")));
      CHECK(size_label->text().contains(QStringLiteral("GB")));
      resolution->setCurrentText(QStringLiteral("12"));
      CHECK(!size_label->text().contains(QStringLiteral("GB")));  // 2400 px pages: not worth a line
    }
    for (int row = 0; row < pages->count(); ++row) {
      if (pages->item(row)->data(kPendingRole).toBool()) {
        return false;  // the timer has not reached this row yet
      }
    }
    // With nothing left to render the timer stops itself, one tick after the last row.
    auto* timer = dialog->findChild<QTimer*>(QStringLiteral("pdfImportThumbnailTimer"));
    if (timer == nullptr || timer->isActive()) {
      return false;
    }
    // Filled in: a rendered thumbnail is not the transparent placeholder.
    const QImage thumbnail = pages->item(0)->icon().pixmap(96, 96).toImage();
    bool any_opaque = false;
    for (int y = 0; y < thumbnail.height() && !any_opaque; ++y) {
      for (int x = 0; x < thumbnail.width() && !any_opaque; ++x) {
        any_opaque = qAlpha(thumbnail.pixel(x, y)) != 0;
      }
    }
    CHECK(any_opaque);
    *done = true;
    dialog->reject();
    return true;
  };
  drive_modal_dialog(step);
  const auto sessions_before = patchy::ui::MainWindowTestAccess::session_count(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  CHECK(*first_look);
  CHECK(*done);
  CHECK(patchy::ui::MainWindowTestAccess::session_count(window) == sessions_before);  // cancelled: nothing opened
}
#endif

#if defined(PATCHY_HAVE_QT_PDF)
// The checkbox is a question only for a document that still carries original data.
void ui_pdf_options_dialog_offers_keep_original_only_with_source_data() {
  for (const bool available : {true, false}) {
    patchy::ui::ImageSaveOptions defaults;
    defaults.pdf_original_image_data_available = available;
    defaults.pdf_keep_original_images = true;
    bool saw_dialog = false;
    QTimer::singleShot(0, [&saw_dialog, available] {
      auto* dialog = find_top_level_dialog(QStringLiteral("pdfSaveOptionsDialog"));
      CHECK(dialog != nullptr);
      if (dialog == nullptr) {
        return;
      }
      auto* keep = dialog->findChild<QCheckBox*>(QStringLiteral("pdfKeepOriginalCheck"));
      CHECK(keep != nullptr);
      if (keep != nullptr) {
        CHECK(keep->isVisible() == available);
        CHECK(keep->isChecked());
        keep->setChecked(false);
      }
      saw_dialog = true;
      dialog->accept();
    });
    const auto chosen = patchy::ui::prompt_image_save_options(nullptr, QStringLiteral("pdf"), defaults);
    CHECK(saw_dialog);
    CHECK(chosen.has_value());
    if (chosen.has_value()) {
      // The hidden checkbox of a document with no original data changes nothing.
      CHECK(chosen->pdf_keep_original_images == !available);
    }
  }
}
#endif

#if defined(PATCHY_HAVE_QT_PDF)
// The PDF Options dialog after the flatten-or-keep choice: the fidelity warning is
// visible exactly when layers are kept, and the export Scale combo (pixel-only) grays out
// with it; the choice itself is never persisted as a save default.
void ui_pdf_options_dialog_shows_editable_warning() {
  for (const bool keep_layers : {true, false}) {
    patchy::ui::ImageSaveOptions defaults;
    defaults.pdf_editable_layers = keep_layers;
    bool saw_dialog = false;
    QTimer::singleShot(0, [&saw_dialog, keep_layers] {
      auto* dialog = find_top_level_dialog(QStringLiteral("pdfSaveOptionsDialog"));
      CHECK(dialog != nullptr);
      if (dialog == nullptr) {
        return;
      }
      auto* warning = dialog->findChild<QLabel*>(QStringLiteral("pdfEditableLayersWarning"));
      auto* scale = dialog->findChild<QComboBox*>(QStringLiteral("exportScaleCombo"));
      CHECK(warning != nullptr);
      CHECK(scale != nullptr);
      if (warning == nullptr || scale == nullptr) {
        dialog->reject();
        return;
      }
      CHECK(dialog->findChild<QCheckBox*>(QStringLiteral("pdfEditableLayersCheck")) == nullptr);
      CHECK(warning->isVisible() == keep_layers);
      CHECK(warning->text().contains(QStringLiteral("may not look")));
      CHECK(scale->isEnabled() == !keep_layers);
      scale->setCurrentIndex(std::max(0, scale->findData(4)));
      // Missing-font handling only matters while text is kept as text.
      auto* missing_fonts = dialog->findChild<QCheckBox*>(QStringLiteral("pdfMissingFontsAsImagesCheck"));
      CHECK(missing_fonts != nullptr);
      if (missing_fonts != nullptr) {
        CHECK(missing_fonts->isVisible() == keep_layers);
        CHECK(!missing_fonts->isChecked());
        missing_fonts->setChecked(true);
      }
      saw_dialog = true;
      dialog->accept();
    });
    const auto chosen =
        patchy::ui::prompt_image_save_options(nullptr, QStringLiteral("pdf"), defaults, /*for_export*/ true);
    CHECK(saw_dialog);
    CHECK(chosen.has_value());
    if (!chosen.has_value()) {
      continue;
    }
    CHECK(chosen->pdf_editable_layers == keep_layers);
    // Vectors scale with the page; the pixel scale only applies to the flattened image.
    CHECK(chosen->export_scale == (keep_layers ? 1 : 4));
    // The hidden checkbox of a flattened save changes nothing.
    CHECK(chosen->pdf_missing_fonts_as_images == keep_layers);
  }
  patchy::ui::app_settings().setValue(QStringLiteral("saveOptions/exportScale"), 1);  // leave no 4x behind

  // The choice persists with the other save-option defaults under its own key.
  {
    auto settings = patchy::ui::app_settings();
    const auto previous = settings.value(QStringLiteral("saveOptions/pdfMissingFontsAsImages"));
    const auto current = patchy::ui::load_image_save_option_defaults();
    auto to_save = current;
    to_save.pdf_missing_fonts_as_images = true;
    patchy::ui::save_image_save_option_defaults(to_save);
    CHECK(settings.value(QStringLiteral("saveOptions/pdfMissingFontsAsImages")).toBool());
    CHECK(patchy::ui::load_image_save_option_defaults().pdf_missing_fonts_as_images);
    to_save.pdf_missing_fonts_as_images = false;
    patchy::ui::save_image_save_option_defaults(to_save);
    CHECK(!patchy::ui::load_image_save_option_defaults().pdf_missing_fonts_as_images);
    patchy::ui::save_image_save_option_defaults(current);
    if (previous.isValid()) {
      settings.setValue(QStringLiteral("saveOptions/pdfMissingFontsAsImages"), previous);
    } else {
      settings.remove(QStringLiteral("saveOptions/pdfMissingFontsAsImages"));
    }
  }
}
#endif

#if defined(PATCHY_HAVE_QT_PDF)
// The flatten-or-keep question and its preference: "ask" raises the three-way dialog
// (Remember writes the policy), a set policy answers silently, Cancel answers nothing.
void ui_pdf_layer_choice_dialog_and_preference() {
  auto settings = patchy::ui::app_settings();
  const auto previous = settings.value(QStringLiteral("saveOptions/pdfLayerPolicy"));
  settings.setValue(QStringLiteral("saveOptions/pdfLayerPolicy"), QStringLiteral("ask"));
  patchy::ui::MainWindow window;
  show_window(window);

  const auto drive = [](const char* button_text, bool remember) {
    QTimer::singleShot(0, [button_text, remember] {
      auto* box = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("pdfLayersMessageBox")));
      CHECK(box != nullptr);
      if (box == nullptr) {
        return;
      }
      CHECK(box->informativeText().contains(QStringLiteral("may not look exactly like the canvas")));
      if (auto* check = box->checkBox()) {
        check->setChecked(remember);
      }
      if (button_text == nullptr) {
        box->reject();
        return;
      }
      for (auto* button : box->buttons()) {
        if (button->text().contains(QString::fromUtf8(button_text))) {
          button->click();
          return;
        }
      }
      CHECK(false);  // button not found
    });
  };

  drive(nullptr, false);
  auto choice = patchy::ui::MainWindowTestAccess::resolve_pdf_layer_choice(window, false);
  CHECK(!choice.has_value());
  CHECK(settings.value(QStringLiteral("saveOptions/pdfLayerPolicy")).toString() == QStringLiteral("ask"));

  drive("Flatten", false);
  choice = patchy::ui::MainWindowTestAccess::resolve_pdf_layer_choice(window, true);
  CHECK(choice.has_value() && !*choice);
  CHECK(settings.value(QStringLiteral("saveOptions/pdfLayerPolicy")).toString() == QStringLiteral("ask"));

  drive("Keep", true);
  choice = patchy::ui::MainWindowTestAccess::resolve_pdf_layer_choice(window, false);
  CHECK(choice.has_value() && *choice);
  CHECK(settings.value(QStringLiteral("saveOptions/pdfLayerPolicy")).toString() == QStringLiteral("editable"));

  // A remembered policy answers without any dialog (a stray box would fail the lookup below).
  bool stray_dialog = false;
  QTimer::singleShot(0, [&stray_dialog] {
    stray_dialog = find_top_level_dialog(QStringLiteral("pdfLayersMessageBox")) != nullptr;
  });
  choice = patchy::ui::MainWindowTestAccess::resolve_pdf_layer_choice(window, false);
  QApplication::processEvents();
  CHECK(choice.has_value() && *choice);
  CHECK(!stray_dialog);
  settings.setValue(QStringLiteral("saveOptions/pdfLayerPolicy"), QStringLiteral("flatten"));
  choice = patchy::ui::MainWindowTestAccess::resolve_pdf_layer_choice(window, true);
  CHECK(choice.has_value() && !*choice);

  if (previous.isValid()) {
    settings.setValue(QStringLiteral("saveOptions/pdfLayerPolicy"), previous);
  } else {
    settings.remove(QStringLiteral("saveOptions/pdfLayerPolicy"));
  }
}
#endif

#if defined(PATCHY_HAVE_QT_PDF)
// End to end through save_document_to_path: a layered document saved to .pdf follows the
// policy. "editable" writes the shape as a path; "flatten" writes one picture.
void ui_pdf_save_follows_layer_policy() {
  ensure_artifact_dir();
  auto settings = patchy::ui::app_settings();
  const auto previous = settings.value(QStringLiteral("saveOptions/pdfLayerPolicy"));
  patchy::Document built(120, 80, patchy::PixelFormat::rgba8());
  built.print_settings().horizontal_ppi = 72.0;
  built.print_settings().vertical_ppi = 72.0;
  built.add_pixel_layer("Paper", solid_pixels(120, 80, patchy::PixelFormat::rgba8(), QColor(240, 240, 240)));
  built.add_layer(solid_rect_shape_layer(built, "Badge", 10, 10, 60, 50, {200, 30, 30}, 0.0));
  patchy::ui::MainWindow window;
  window.add_document_session(std::move(built), QStringLiteral("PDF Policy"));
  show_window(window);

  const auto read_back = [](const QString& path) {
    const QByteArray bytes = read_file_bytes(path);
    patchy::pdf::VectorReadOptions read_options;
    read_options.pixels_per_point = 1.0;
    return patchy::pdf::read_page_as_vectors(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                      static_cast<std::size_t>(bytes.size())),
        read_options);
  };
  // No options object: the save consults the preference, the way a scripted or
  // already-confirmed save does.
  settings.setValue(QStringLiteral("saveOptions/pdfLayerPolicy"), QStringLiteral("editable"));
  const auto editable_path = QStringLiteral("test-artifacts/ui_pdf_policy_editable.pdf");
  QFile::remove(editable_path);
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, editable_path));
  CHECK(QFileInfo::exists(editable_path));
  const auto editable = read_back(editable_path);
  CHECK(editable.shape_layers >= 1);
  CHECK(editable.image_layers >= 1);
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("copy")));

  settings.setValue(QStringLiteral("saveOptions/pdfLayerPolicy"), QStringLiteral("flatten"));
  const auto flat_path = QStringLiteral("test-artifacts/ui_pdf_policy_flat.pdf");
  QFile::remove(flat_path);
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, flat_path));
  CHECK(QFileInfo::exists(flat_path));
  const auto flat = read_back(flat_path);
  CHECK(flat.shape_layers == 0);
  CHECK(flat.image_layers == 1);

  if (previous.isValid()) {
    settings.setValue(QStringLiteral("saveOptions/pdfLayerPolicy"), previous);
  } else {
    settings.remove(QStringLiteral("saveOptions/pdfLayerPolicy"));
  }
}
#endif

#if defined(PATCHY_HAVE_QT_PDF)
void ui_pdf_import_builds_one_layer_per_page() {
  ensure_artifact_dir();
  const auto path = QStringLiteral("test-artifacts/ui_pdf_import_two_pages.pdf");
  QFile::remove(path);
  {
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(two_page_pdf_bytes());
  }
  CHECK(patchy::ui::pdf_import_is_available());
  CHECK(patchy::ui::is_pdf_extension(QStringLiteral("PDF")));
  const auto bytes = two_page_pdf_bytes();
  CHECK(patchy::ui::bytes_look_like_pdf(
      std::span(reinterpret_cast<const std::uint8_t*>(bytes.constData()), static_cast<std::size_t>(bytes.size()))));

  patchy::ui::PdfImportOptions options;
  options.pages = {0, 1};
  options.resolution_ppi = 72;
  QString error;
  auto result = patchy::ui::load_pdf_document(path, options, QString(), &error);
  CHECK(result.has_value());
  if (!result.has_value()) {
    return;
  }
  const auto& document = result->document;
  // Canvas is the per-axis maximum across pages; page 2 is narrower and taller.
  CHECK(document.width() == 144);
  CHECK(document.height() == 144);
  CHECK(document.layers().size() == 2);
  CHECK(document.layers()[0].name() == "Page 1");
  CHECK(document.layers()[1].name() == "Page 2");
  // Only the first page starts visible, matching the image-sequence import.
  CHECK(document.layers()[0].visible());
  CHECK(!document.layers()[1].visible());
  // The import resolution becomes the document's, so Image Size and print agree with it.
  CHECK(std::abs(document.print_settings().horizontal_ppi - 72.0) < 0.01);
  CHECK(std::abs(document.print_settings().vertical_ppi - 72.0) < 0.01);
  CHECK(!result->notices.empty());
  CHECK(result->notices.front().find("rasterized") != std::string::npos);

  // Progress reports every selected page in order; cancelling keeps the pages done so
  // far and says where it stopped.
  patchy::ui::PdfImportOptions separate;
  separate.pages = {0, 1};
  separate.resolution_ppi = 72;
  separate.separate_documents = true;
  std::vector<std::pair<int, int>> reported;
  separate.progress = [&reported](int position, int count) {
    reported.emplace_back(position, count);
    return position < 2;
  };
  auto partial = patchy::ui::load_pdf_document(path, separate, QString(), &error);
  CHECK(partial.has_value());
  if (partial.has_value()) {
    CHECK(reported == (std::vector<std::pair<int, int>>{{1, 2}, {2, 2}}));
    CHECK(partial->document.width() == 144);
    CHECK(partial->extra_documents.empty());
    CHECK(partial->document_title == QStringLiteral("Page 1"));
    bool stopped_notice = false;
    for (const auto& notice : partial->notices) {
      stopped_notice = stopped_notice || notice.find("stopped after 1 of 2") != std::string::npos;
    }
    CHECK(stopped_notice);
  }

  // A single-page selection still works and names the layer after the real page number.
  patchy::ui::PdfImportOptions second_only;
  second_only.pages = {1};
  second_only.resolution_ppi = 72;
  auto single = patchy::ui::load_pdf_document(path, second_only, QString(), &error);
  CHECK(single.has_value());
  if (single.has_value()) {
    CHECK(single->document.layers().size() == 1);
    CHECK(single->document.layers()[0].name() == "Page 2");
    CHECK(single->document.width() == 72);
    CHECK(single->document.height() == 144);
  }

}
#endif

#if defined(PATCHY_HAVE_QT_PDF)
void ui_pdf_import_dialog_opens_selected_pages() {
  const auto path = QStringLiteral("test-artifacts/ui_pdf_import_dialog.pdf");
  QFile::remove(path);
  {
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(two_page_pdf_bytes());
  }
  patchy::ui::MainWindow window;
  show_window(window);

  // The Open command routes a .pdf through the page picker, so the dialog has to be
  // driven from a timer while open_document_path blocks in its exec() loop.
  auto clicked = std::make_shared<bool>(false);
  auto step = std::make_shared<std::function<bool()>>();
  *step = [clicked] {
    auto* dialog = find_top_level_dialog(QStringLiteral("pdfImportDialog"));
    if (dialog == nullptr) {
      return false;
    }
    auto* pages = dialog->findChild<QListWidget*>(QStringLiteral("pdfImportPagesList"));
    auto* resolution = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportResolutionCombo"));
    auto* mode = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportModeCombo"));
    auto* import_button = dialog->findChild<QPushButton*>(QStringLiteral("pdfImportButton"));
    if (pages == nullptr || resolution == nullptr || mode == nullptr || import_button == nullptr) {
      return false;
    }
    CHECK(pages->count() == 2);
    // This test pins the FLATTEN path (layer-per-page raster); the mode persists in
    // settings and editable is the default, so it is selected explicitly. Likewise the
    // pages target: separate documents is the default, layers is what this pins.
    auto* pages_target = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportPagesTargetCombo"));
    CHECK(pages_target != nullptr);
    if (pages_target != nullptr) {
      pages_target->setCurrentIndex(std::max(0, pages_target->findData(QStringLiteral("layers"))));
    }
    mode->setCurrentIndex(std::max(0, mode->findData(QStringLiteral("flatten"))));
    resolution->setCurrentText(QStringLiteral("72"));
    pages->selectAll();
    CHECK(import_button->isEnabled());
    import_button->click();
    *clicked = true;
    return true;
  };
  drive_modal_dialog(step);
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  CHECK(*clicked);

  auto& opened = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(opened.width() == 144);
  CHECK(opened.height() == 144);
  CHECK(opened.layers().size() == 2);
  // One folder per page, page 1 on top and the only one shown, each holding its page.
  if (opened.layers().size() == 2) {
    const auto& top = std::as_const(opened).layers()[1];
    const auto& bottom = std::as_const(opened).layers()[0];
    CHECK(top.name() == "Page 1" && top.kind() == patchy::LayerKind::Group && top.visible());
    CHECK(bottom.name() == "Page 2" && bottom.kind() == patchy::LayerKind::Group && !bottom.visible());
    CHECK(top.children().size() == 1U && bottom.children().size() == 1U);
    CHECK(top.metadata().at(patchy::kLayerMetadataPdfPageRect) == "0,0,144,72");
  }
  CHECK(patchy::ui::MainWindowTestAccess::active_session_path(window) == path);
}
#endif

#if defined(PATCHY_HAVE_QT_PDF)
// The default target: every selected page opens as its own document, page 1 first and
// active, each at its own size, titled after the file and page.
void ui_pdf_import_dialog_opens_pages_as_documents() {
  const auto path = QStringLiteral("test-artifacts/ui_pdf_import_documents.pdf");
  QFile::remove(path);
  {
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(two_page_pdf_bytes());
  }
  patchy::ui::MainWindow window;
  show_window(window);
  const auto sessions_before = patchy::ui::MainWindowTestAccess::session_count(window);

  auto clicked = std::make_shared<bool>(false);
  auto step = std::make_shared<std::function<bool()>>();
  *step = [clicked] {
    auto* dialog = find_top_level_dialog(QStringLiteral("pdfImportDialog"));
    if (dialog == nullptr) {
      return false;
    }
    auto* pages = dialog->findChild<QListWidget*>(QStringLiteral("pdfImportPagesList"));
    auto* resolution = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportResolutionCombo"));
    auto* mode = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportModeCombo"));
    auto* pages_target = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportPagesTargetCombo"));
    auto* size_label = dialog->findChild<QLabel*>(QStringLiteral("pdfImportSizeLabel"));
    auto* import_button = dialog->findChild<QPushButton*>(QStringLiteral("pdfImportButton"));
    if (pages == nullptr || resolution == nullptr || mode == nullptr || pages_target == nullptr ||
        size_label == nullptr || import_button == nullptr) {
      return false;
    }
    mode->setCurrentIndex(std::max(0, mode->findData(QStringLiteral("flatten"))));
    pages_target->setCurrentIndex(std::max(0, pages_target->findData(QStringLiteral("documents"))));
    resolution->setCurrentText(QStringLiteral("72"));
    pages->selectAll();
    QApplication::processEvents();
    CHECK(size_label->text().startsWith(QStringLiteral("2 document")));
    import_button->click();
    *clicked = true;
    return true;
  };
  drive_modal_dialog(step);
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  CHECK(*clicked);

  CHECK(patchy::ui::MainWindowTestAccess::session_count(window) == sessions_before + 2);
  if (patchy::ui::MainWindowTestAccess::session_count(window) != sessions_before + 2) {
    return;
  }
  // Page 1 (144 x 72 pt at 72 ppi) is the active session and carries the file path;
  // page 2 (72 x 144) is its own pathless session.
  auto& first = patchy::ui::MainWindowTestAccess::session_document(window, sessions_before);
  auto& second = patchy::ui::MainWindowTestAccess::session_document(window, sessions_before + 1);
  CHECK(&patchy::ui::MainWindowTestAccess::document(window) == &first);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_path(window) == path);
  CHECK(first.width() == 144 && first.height() == 72);
  CHECK(second.width() == 72 && second.height() == 144);
  CHECK(first.layers().size() == 1 && second.layers().size() == 1);
  CHECK(first.layers().size() == 1 && first.layers()[0].name() == "Page 1");
  CHECK(second.layers().size() == 1 && second.layers()[0].name() == "Page 2");
  CHECK(second.layers().size() == 1 && second.layers()[0].visible());
  CHECK(std::abs(second.print_settings().horizontal_ppi - 72.0) < 0.01);
  CHECK(patchy::ui::MainWindowTestAccess::session_title(window, sessions_before)
            .endsWith(QStringLiteral("ui_pdf_import_documents.pdf - Page 1")));
  CHECK(patchy::ui::MainWindowTestAccess::session_title(window, sessions_before + 1)
            .endsWith(QStringLiteral("ui_pdf_import_documents.pdf - Page 2")));

  // Pages after the first open in the BACKGROUND: a tab each, but page 1 never stopped
  // being the current tab and the active canvas. Selecting page 2's tab (the real user
  // path, through the tab widget) activates it and fits its never-shown canvas.
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);
  if (tabs == nullptr) {
    return;
  }
  CHECK(tabs->count() == static_cast<int>(sessions_before + 2));
  auto* first_canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  CHECK(tabs->currentWidget() == first_canvas);
  CHECK(patchy::ui::MainWindowTestAccess::document_for_canvas(window, first_canvas) == &first);
  const int second_tab = tabs->count() - 1;
  tabs->setCurrentIndex(second_tab);
  QApplication::processEvents();
  QApplication::processEvents();
  auto* second_canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  CHECK(second_canvas != first_canvas);
  CHECK(patchy::ui::MainWindowTestAccess::document_for_canvas(window, second_canvas) == &second);
  CHECK(&patchy::ui::MainWindowTestAccess::document(window) == &second);
  // Fitted to the size it actually has: fitting again changes nothing.
  const double fitted_zoom = second_canvas->zoom();
  second_canvas->fit_to_view();
  CHECK(std::abs(second_canvas->zoom() - fitted_zoom) < 1e-9);
  // A 72 x 144 px page in a window-sized canvas fits well above 100%: the 640 x 480
  // default size of a never-shown widget would not have produced this zoom.
  CHECK(fitted_zoom > 1.0);
  // Going back and forth does not refit a view the user may have changed.
  second_canvas->set_zoom(0.5);
  tabs->setCurrentIndex(static_cast<int>(sessions_before));
  QApplication::processEvents();
  tabs->setCurrentIndex(second_tab);
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(std::abs(patchy::ui::MainWindowTestAccess::canvas(window)->zoom() - 0.5) < 1e-9);
}
#endif

#if defined(PATCHY_HAVE_QT_PDF)
// Editable import used to bring in one page no matter the selection; with separate
// documents every selected page gets the vector reader.
void ui_pdf_import_editable_opens_every_selected_page() {
  const auto path = QStringLiteral("test-artifacts/ui_pdf_import_editable_pages.pdf");
  QFile::remove(path);
  {
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(two_page_editable_pdf_bytes());
  }
  patchy::ui::MainWindow window;
  show_window(window);
  const auto sessions_before = patchy::ui::MainWindowTestAccess::session_count(window);

  auto clicked = std::make_shared<bool>(false);
  auto step = std::make_shared<std::function<bool()>>();
  *step = [clicked] {
    auto* dialog = find_top_level_dialog(QStringLiteral("pdfImportDialog"));
    if (dialog == nullptr) {
      return false;
    }
    auto* pages = dialog->findChild<QListWidget*>(QStringLiteral("pdfImportPagesList"));
    auto* resolution = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportResolutionCombo"));
    auto* mode = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportModeCombo"));
    auto* pages_target = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportPagesTargetCombo"));
    auto* import_button = dialog->findChild<QPushButton*>(QStringLiteral("pdfImportButton"));
    if (pages == nullptr || resolution == nullptr || mode == nullptr || pages_target == nullptr ||
        import_button == nullptr) {
      return false;
    }
    mode->setCurrentIndex(std::max(0, mode->findData(QStringLiteral("editable"))));
    pages_target->setCurrentIndex(std::max(0, pages_target->findData(QStringLiteral("documents"))));
    resolution->setCurrentText(QStringLiteral("72"));
    if (auto* paper = dialog->findChild<QCheckBox*>(QStringLiteral("pdfImportWhiteBackgroundCheck"))) {
      paper->setChecked(false);
    }
    pages->selectAll();
    import_button->click();
    *clicked = true;
    return true;
  };
  drive_modal_dialog(step);
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  CHECK(*clicked);

  CHECK(patchy::ui::MainWindowTestAccess::session_count(window) == sessions_before + 2);
  if (patchy::ui::MainWindowTestAccess::session_count(window) != sessions_before + 2) {
    return;
  }
  auto& first = patchy::ui::MainWindowTestAccess::session_document(window, sessions_before);
  auto& second = patchy::ui::MainWindowTestAccess::session_document(window, sessions_before + 1);
  CHECK(&patchy::ui::MainWindowTestAccess::document(window) == &first);
  CHECK(first.width() == 200 && first.height() == 100);
  CHECK(second.width() == 50 && second.height() == 50);
  CHECK(first.layers().size() == 1 && patchy::layer_is_vector_shape(first.layers()[0]));
  CHECK(second.layers().size() == 1 && patchy::layer_is_vector_shape(second.layers()[0]));
  if (second.layers().size() == 1 && second.layers()[0].vector_shape() != nullptr) {
    CHECK(second.layers()[0].vector_shape()->fill.color.red == 255);
  }
}
#endif

#if defined(PATCHY_HAVE_QT_PDF)
void ui_pdf_import_editable_mode_builds_vector_and_text_layers() {
  const auto path = QStringLiteral("test-artifacts/ui_pdf_import_editable.pdf");
  QFile::remove(path);
  {
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(editable_pdf_bytes());
  }
  patchy::ui::MainWindow window;
  show_window(window);

  auto clicked = std::make_shared<bool>(false);
  auto step = std::make_shared<std::function<bool()>>();
  *step = [clicked] {
    auto* dialog = find_top_level_dialog(QStringLiteral("pdfImportDialog"));
    if (dialog == nullptr) {
      return false;
    }
    auto* resolution = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportResolutionCombo"));
    auto* mode = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportModeCombo"));
    auto* annotations = dialog->findChild<QCheckBox*>(QStringLiteral("pdfImportAnnotationsCheck"));
    auto* import_button = dialog->findChild<QPushButton*>(QStringLiteral("pdfImportButton"));
    if (resolution == nullptr || mode == nullptr || annotations == nullptr || import_button == nullptr) {
      return false;
    }
    mode->setCurrentIndex(std::max(0, mode->findData(QStringLiteral("editable"))));
    // The raster-only toggles gray out in editable mode.
    CHECK(!annotations->isEnabled());
    resolution->setCurrentText(QStringLiteral("144"));
    // The paper layer stays on (the default): an editable page gets a white Background
    // under its artwork (GitHub issue 153).
    if (auto* paper = dialog->findChild<QCheckBox*>(QStringLiteral("pdfImportWhiteBackgroundCheck"))) {
      paper->setChecked(true);
    }
    import_button->click();
    *clicked = true;
    return true;
  };
  drive_modal_dialog(step);
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  CHECK(*clicked);

  auto& opened = patchy::ui::MainWindowTestAccess::document(window);
  // 200 x 100 points at 144 ppi doubles the canvas, and the resolution rides along.
  CHECK(opened.width() == 400);
  CHECK(opened.height() == 200);
  CHECK(std::abs(opened.print_settings().horizontal_ppi - 144.0) < 0.01);
  CHECK(opened.layers().size() == 3);
  if (opened.layers().size() != 3) {
    return;
  }
  const auto& paper = opened.layers()[0];
  CHECK(paper.name() == "Background");
  CHECK(paper.bounds().width == 400 && paper.bounds().height == 200);
  if (!paper.pixels().empty()) {
    const auto* corner = paper.pixels().pixel(0, 0);
    CHECK(corner[0] == 255 && corner[1] == 255 && corner[2] == 255);
  }

  const auto& shape = opened.layers()[1];
  CHECK(patchy::layer_is_vector_shape(shape));
  CHECK(shape.vector_shape() != nullptr);
  if (shape.vector_shape() != nullptr) {
    CHECK(shape.vector_shape()->fill.color.blue == 255);
  }

  // The text layer arrived rendered: the post-open pass rasterized it through the
  // reader's matrix and stamped the standard transformed-text metadata.
  const auto& text = opened.layers()[2];
  CHECK(patchy::layer_is_text(text));
  CHECK(text.metadata().at(patchy::kLayerMetadataText) == "Hello PDF");
  CHECK(!text.metadata().contains(patchy::kLayerMetadataPdfPendingText));
  CHECK(text.metadata().contains(patchy::kLayerMetadataTextTransform));
  CHECK(!text.pixels().empty());
  // 18 pt at 144 ppi renders 36 px tall, so the glyph block must be at least that
  // wide for a nine-character run, and its top must sit above the baseline row.
  CHECK(text.bounds().width > 36);
  const double baseline_document_y = 200.0 - 60.0 * 2.0;  // flipped, then scaled
  CHECK(text.bounds().y < static_cast<std::int32_t>(baseline_document_y));
}
#endif

#if defined(PATCHY_HAVE_QT_PDF)
// The whole feature end to end on a real document: the untracked brochure fixture
// imports editable, and the composite is saved as a visual-QA artifact.
void ui_pdf_local_brochure_editable_import_composites_if_available() {
  const auto fixture_path = patchy::test::local_format_fixture_path("pdf", "HoloVCS_C2_A4_Brochure.pdf");
  patchy::test::skip_unless_exists(fixture_path);
  const auto fixture = QString::fromStdString(fixture_path.string());
  ensure_artifact_dir();
  patchy::ui::MainWindow window;
  show_window(window);

  auto clicked = std::make_shared<bool>(false);
  auto step = std::make_shared<std::function<bool()>>();
  *step = [clicked] {
    auto* dialog = find_top_level_dialog(QStringLiteral("pdfImportDialog"));
    if (dialog == nullptr) {
      return false;
    }
    auto* resolution = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportResolutionCombo"));
    auto* mode = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportModeCombo"));
    auto* import_button = dialog->findChild<QPushButton*>(QStringLiteral("pdfImportButton"));
    if (resolution == nullptr || mode == nullptr || import_button == nullptr) {
      return false;
    }
    mode->setCurrentIndex(std::max(0, mode->findData(QStringLiteral("editable"))));
    resolution->setCurrentText(QStringLiteral("96"));
    import_button->click();
    *clicked = true;
    return true;
  };
  drive_modal_dialog(step);
  patchy::ui::MainWindowTestAccess::open_document_path(window, fixture);
  CHECK(*clicked);

  auto& opened = patchy::ui::MainWindowTestAccess::document(window);
  // A4 at 96 ppi.
  CHECK(std::abs(opened.width() - 794) <= 2);
  CHECK(std::abs(opened.height() - 1123) <= 2);
  CHECK(opened.layers().size() > 400);

  int rendered_text = 0;
  int pending_text = 0;
  int smart_objects_with_pixels = 0;
  for (const auto& layer : opened.layers()) {
    if (patchy::layer_is_text(layer)) {
      if (layer.metadata().contains(patchy::kLayerMetadataPdfPendingText)) {
        ++pending_text;
      } else if (!layer.pixels().empty()) {
        ++rendered_text;
      }
    } else if (patchy::layer_is_smart_object(layer) && !layer.pixels().empty()) {
      ++smart_objects_with_pixels;
    }
  }
  // Every text layer got rasterized by the post-open pass, and both images decoded.
  CHECK(pending_text == 0);
  CHECK(rendered_text >= 40);
  CHECK(smart_objects_with_pixels == 2);

  const auto composite = patchy::ui::qimage_from_document(opened, false);
  CHECK(!composite.isNull());
  CHECK(composite.save(QStringLiteral("test-artifacts/ui_pdf_brochure_editable.png")));
  // Not blank: the page background is warm off-white with dark panels and text.
  int dark_pixels = 0;
  for (int y = 0; y < composite.height(); y += 7) {
    for (int x = 0; x < composite.width(); x += 7) {
      if (qGray(composite.pixel(x, y)) < 96) {
        ++dark_pixels;
      }
    }
  }
  CHECK(dark_pixels > 200);
}
#endif

void ui_print_dialog_exposes_printer_and_visible_checkboxes() {
  patchy::ui::MainWindow window;
  show_window(window);
  // The dialog's opening state depends on the print size: pin the startup document
  // (72 ppi since the New Document redesign) to 300 ppi so 1024x768 px is
  // 3.41 x 2.56 in, which fits Letter at actual size.
  {
    auto& document = patchy::ui::MainWindowTestAccess::document(window);
    document.print_settings().horizontal_ppi = 300.0;
    document.print_settings().vertical_ppi = 300.0;
  }

  QTimer::singleShot(0, [&window] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyPrintDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* printer = dialog->findChild<QComboBox*>(QStringLiteral("printPrinterCombo"));
      auto* print_button = dialog->findChild<QPushButton*>(QStringLiteral("printDialogPrintButton"));
      auto* copies = dialog->findChild<QSpinBox*>(QStringLiteral("printCopiesSpin"));
      auto* scale_to_fit = dialog->findChild<QCheckBox*>(QStringLiteral("printScaleToFitCheck"));
      auto* scale = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("printScalePercentSpin"));
      auto* resolution = dialog->findChild<QLabel*>(QStringLiteral("printResolutionValueLabel"));
      auto* units = dialog->findChild<QComboBox*>(QStringLiteral("printUnitsCombo"));
      auto* scale_size = dialog->findChild<QLabel*>(QStringLiteral("printScaleSizeLabel"));
      auto* image_size = dialog->findChild<QLabel*>(QStringLiteral("printImageSizeLabel"));
      auto* center = dialog->findChild<QCheckBox*>(QStringLiteral("printCenterCheck"));
      auto* crop_marks = dialog->findChild<QCheckBox*>(QStringLiteral("printCropMarksCheck"));
      auto* system_dialog = dialog->findChild<QPushButton*>(QStringLiteral("printSystemDialogButton"));
      CHECK(printer != nullptr);
      CHECK(print_button != nullptr);
      // Present and gated like Print; never clicked here, it would park a nested
      // modal loop inside this driver lambda.
      CHECK(system_dialog != nullptr);
      CHECK(system_dialog->isEnabled() == printer->isEnabled());
      CHECK(copies != nullptr);
      CHECK(scale_to_fit != nullptr);
      CHECK(scale != nullptr);
      CHECK(resolution != nullptr);
      CHECK(units != nullptr);
      CHECK(scale_size != nullptr);
      CHECK(image_size != nullptr);
      CHECK(center != nullptr);
      CHECK(crop_marks != nullptr);
      CHECK(printer->count() >= 1);
      CHECK(!printer->currentText().isEmpty());
      CHECK(print_button->isEnabled() == printer->isEnabled());
      // Copies opens at one and never goes below it, matching Photoshop's Print dialog.
      CHECK(copies->value() == 1);
      CHECK(copies->minimum() == 1);
      CHECK(copies->maximum() >= 99);
      copies->setValue(0);
      CHECK(copies->value() == 1);
      copies->setValue(12);
      CHECK(copies->value() == 12);
      copies->setValue(1);
      // The 1024x768 document pinned to 300 ppi fits Letter at actual size, so the
      // dialog opens at 100% (Photoshop's default) with fit-to-media unchecked and
      // the derived print resolution equal to the document resolution.
      CHECK(!scale_to_fit->isChecked());
      CHECK(scale->isEnabled());
      CHECK(std::abs(scale->value() - 100.0) < 0.01);
      CHECK(resolution->text().contains(QStringLiteral("300")));
      CHECK(units->currentData().toString() == QStringLiteral("in"));
      CHECK(scale_size->text().contains(QStringLiteral("in")));
      CHECK(image_size->text().contains(QStringLiteral("in")));
      scale_to_fit->setChecked(true);
      QApplication::processEvents();
      CHECK(!scale->isEnabled());
      // Fit-to-media on Letter enlarges the 3.41 x 2.56 in document, and the derived
      // print resolution drops below the stored 300 accordingly.
      CHECK(scale->value() > 100.0);
      CHECK(!resolution->text().startsWith(QStringLiteral("300")));
      scale_to_fit->setChecked(false);
      QApplication::processEvents();
      CHECK(scale->isEnabled());
      CHECK(std::abs(scale->value() - 100.0) < 0.01);
      // Paper controls: Letter portrait to start, the custom size spins locked until
      // Custom is chosen, then editable and pre-filled with the sheet on screen.
      auto* page_size = dialog->findChild<QComboBox*>(QStringLiteral("printPageSizeCombo"));
      auto* orientation = dialog->findChild<QComboBox*>(QStringLiteral("printOrientationCombo"));
      auto* page_width = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("printPageWidthSpin"));
      auto* page_height = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("printPageHeightSpin"));
      CHECK(page_size != nullptr && orientation != nullptr && page_width != nullptr && page_height != nullptr);
      if (page_size != nullptr && orientation != nullptr && page_width != nullptr && page_height != nullptr) {
        CHECK(page_size->currentData().toInt() == static_cast<int>(QPageSize::Letter));
        CHECK(orientation->currentData().toInt() == static_cast<int>(QPageLayout::Portrait));
        CHECK(!page_width->isEnabled());
        CHECK(std::abs(page_width->value() - 8.5) < 0.01);
        CHECK(std::abs(page_height->value() - 11.0) < 0.01);
        page_size->setCurrentIndex(page_size->findData(static_cast<int>(QPageSize::Custom)));
        QApplication::processEvents();
        CHECK(page_width->isEnabled());
        CHECK(std::abs(page_width->value() - 8.5) < 0.01);
        page_width->setValue(4.0);
        page_height->setValue(3.25);
        QApplication::processEvents();
        CHECK(page_size->currentData().toInt() == static_cast<int>(QPageSize::Custom));
        CHECK(std::abs(page_width->value() - 4.0) < 0.01);
        // The 3.41 x 2.56 in document no longer fits a 4 x 3.25 in sheet inside its
        // margins at actual size; the placement math sees the new sheet at once.
        scale_to_fit->setChecked(true);
        QApplication::processEvents();
        CHECK(scale->value() < 100.0);
        scale_to_fit->setChecked(false);
        // Units switch the custom spins' unit; the sheet itself is unchanged.
        units->setCurrentIndex(units->findData(QStringLiteral("mm")));
        QApplication::processEvents();
        CHECK(std::abs(page_width->value() - 101.6) < 0.1);
        units->setCurrentIndex(units->findData(QStringLiteral("in")));
        orientation->setCurrentIndex(orientation->findData(static_cast<int>(QPageLayout::Landscape)));
        QApplication::processEvents();
        CHECK(orientation->currentData().toInt() == static_cast<int>(QPageLayout::Landscape));
        // Back to Letter portrait so nothing leaks into the next test.
        orientation->setCurrentIndex(orientation->findData(static_cast<int>(QPageLayout::Portrait)));
        page_size->setCurrentIndex(page_size->findData(static_cast<int>(QPageSize::Letter)));
        QApplication::processEvents();
        CHECK(!page_width->isEnabled());
        CHECK(std::abs(page_width->value() - 8.5) < 0.01);
      }
      CHECK(window.styleSheet().contains(QStringLiteral("QCheckBox::indicator:checked")));
      CHECK(window.styleSheet().contains(QStringLiteral("checkmark.svg")));
      CHECK(window.styleSheet().contains(QStringLiteral("border-color: #9ccfff")));
      dialog->reject();
      return;
    }
    CHECK(false);
  });

  require_action(window, "filePrintAction")->trigger();
  QApplication::processEvents();
}

}  // namespace

std::vector<patchy::test::TestCase> import_print_resolution_tests_part2() {
  return {
#if defined(PATCHY_HAVE_QT_PDF)
      {"ui_pdf_export_editable_keeps_psd_preview_text_and_substitutes_missing_fonts", ui_pdf_export_editable_keeps_psd_preview_text_and_substitutes_missing_fonts},
#endif
#if defined(PATCHY_HAVE_QT_PDF)
      {"ui_pdf_export_editable_gradients_clips_and_opacity_render_like_canvas", ui_pdf_export_editable_gradients_clips_and_opacity_render_like_canvas},
#endif
#if defined(PATCHY_HAVE_QT_PDF)
      {"ui_pdf_export_editable_pattern_fill_rotates_like_canvas", ui_pdf_export_editable_pattern_fill_rotates_like_canvas},
#endif
#if defined(PATCHY_HAVE_QT_PDF)
      {"ui_pdf_export_editable_flattens_blend_modes_with_notice", ui_pdf_export_editable_flattens_blend_modes_with_notice},
#endif
#if defined(PATCHY_HAVE_QT_PDF)
      {"ui_font_bootstrap_never_registers_installed_families", ui_font_bootstrap_never_registers_installed_families},
#endif
#if defined(PATCHY_HAVE_QT_PDF)
      {"ui_pdf_export_image_pages_pick_codec_and_channels", ui_pdf_export_image_pages_pick_codec_and_channels},
#endif
#if defined(PATCHY_HAVE_QT_PDF)
      {"ui_pdf_export_editable_routes_single_raster_pages_to_image_writer", ui_pdf_export_editable_routes_single_raster_pages_to_image_writer},
#endif
#if defined(PATCHY_HAVE_QT_PDF)
      {"ui_pdf_export_image_pages_keep_order_and_cancel_cleanly", ui_pdf_export_image_pages_keep_order_and_cancel_cleanly},
#endif
#if defined(PATCHY_HAVE_QT_PDF)
      {"ui_pdf_export_failure_keeps_existing_file", ui_pdf_export_failure_keeps_existing_file},
#endif
#if defined(PATCHY_HAVE_QT_PDF)
      {"ui_pdf_export_writes_unicode_paths", ui_pdf_export_writes_unicode_paths},
#endif
#if defined(PATCHY_HAVE_QT_PDF)
      {"ui_pdf_image_quality_presets_and_settings", ui_pdf_image_quality_presets_and_settings},
#endif
#if defined(PATCHY_HAVE_QT_PDF)
      {"ui_pdf_options_dialog_offers_image_quality_presets", ui_pdf_options_dialog_offers_image_quality_presets},
#endif
#if defined(PATCHY_HAVE_QT_PDF)
      {"ui_pdf_pass_through_keeps_original_image_bytes", ui_pdf_pass_through_keeps_original_image_bytes},
#endif
#if defined(PATCHY_HAVE_QT_PDF)
      {"ui_pdf_pass_through_is_not_captured_for_trimmed_or_stacked_pages", ui_pdf_pass_through_is_not_captured_for_trimmed_or_stacked_pages},
#endif
#if defined(PATCHY_HAVE_QT_PDF)
      {"ui_pdf_import_dialog_fills_thumbnails_lazily_and_estimates_memory", ui_pdf_import_dialog_fills_thumbnails_lazily_and_estimates_memory},
#endif
#if defined(PATCHY_HAVE_QT_PDF)
      {"ui_pdf_options_dialog_offers_keep_original_only_with_source_data", ui_pdf_options_dialog_offers_keep_original_only_with_source_data},
#endif
#if defined(PATCHY_HAVE_QT_PDF)
      {"ui_pdf_options_dialog_shows_editable_warning", ui_pdf_options_dialog_shows_editable_warning},
#endif
#if defined(PATCHY_HAVE_QT_PDF)
      {"ui_pdf_layer_choice_dialog_and_preference", ui_pdf_layer_choice_dialog_and_preference},
#endif
#if defined(PATCHY_HAVE_QT_PDF)
      {"ui_pdf_save_follows_layer_policy", ui_pdf_save_follows_layer_policy},
#endif
#if defined(PATCHY_HAVE_QT_PDF)
      {"ui_pdf_import_builds_one_layer_per_page", ui_pdf_import_builds_one_layer_per_page},
#endif
#if defined(PATCHY_HAVE_QT_PDF)
      {"ui_pdf_import_dialog_opens_selected_pages", ui_pdf_import_dialog_opens_selected_pages},
#endif
#if defined(PATCHY_HAVE_QT_PDF)
      {"ui_pdf_import_dialog_opens_pages_as_documents", ui_pdf_import_dialog_opens_pages_as_documents},
#endif
#if defined(PATCHY_HAVE_QT_PDF)
      {"ui_pdf_import_editable_opens_every_selected_page", ui_pdf_import_editable_opens_every_selected_page},
#endif
#if defined(PATCHY_HAVE_QT_PDF)
      {"ui_pdf_import_editable_mode_builds_vector_and_text_layers", ui_pdf_import_editable_mode_builds_vector_and_text_layers},
#endif
#if defined(PATCHY_HAVE_QT_PDF)
      {"ui_pdf_local_brochure_editable_import_composites_if_available", ui_pdf_local_brochure_editable_import_composites_if_available},
#endif
      {"ui_print_dialog_exposes_printer_and_visible_checkboxes", ui_print_dialog_exposes_printer_and_visible_checkboxes},
  };
}
