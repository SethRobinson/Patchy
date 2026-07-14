#include "ui/canvas_widget.hpp"
#include "core/adjustment_layer.hpp"
#include "core/contour_presets.hpp"
#include "core/gradient_presets.hpp"
#include "core/layer_metadata.hpp"
#include "core/pattern_presets.hpp"
#include "core/smart_filter.hpp"
#include "core/smart_filter_effects.hpp"
#include "core/smart_object.hpp"
#include "core/text_warp.hpp"
#include "ui/smart_object_render.hpp"
#include "core/layer_tree.hpp"
#include "core/palette.hpp"
#include "core/palette_presets.hpp"
#include "ui/palette_panel.hpp"
#include "ui/pattern_library.hpp"
#include "ui/pattern_manager_dialog.hpp"
#include "ui/photo_pattern_presets.hpp"
#include "ui/style_browser.hpp"
#include "ui/style_library.hpp"
#include "ui/style_manager_dialog.hpp"
#include "psd/asl_io.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_layer_effects.hpp"
#include "core/style_presets.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/brush_tip_manager_dialog.hpp"
#include "ui/brush_tip_picker.hpp"
#include "ui/blend_if_range_editor.hpp"
#include "ui/color_panel.hpp"
#include "ui/default_brush_tips.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/document_float_window.hpp"
#include "ui/compatibility_report.hpp"
#include "ui/curves_editor.hpp"
#include "ui/curves_presets.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/filter_look_library.hpp"
#include "ui/font_picker.hpp"
#include "ui/gradient_stops_editor.hpp"
#include "ui/gradient_library.hpp"
#include "ui/gradient_manager_dialog.hpp"
#include "formats/acv_curves_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/tga_document_io.hpp"
#include "ui/image_document_io.hpp"
#include "ui/image_save_options_dialog.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/localization.hpp"
#include "ui/main_window.hpp"
#include "ui/print_dialog.hpp"
#include "ui/selection_outline.hpp"
#include "ui/sprite_sheet_dialog.hpp"
#include "ui/splash_dialog.hpp"
#include "ui/app_settings.hpp"
#include "ui/update_checker.hpp"
#include "ui/visual_filter_gallery_dialog.hpp"
#include "ui/zoomable_image_preview.hpp"
#include "ui/zoom_status_bar.hpp"
#include "filters/builtin_filters.hpp"
#include "psd/psd_document_io.hpp"
#include "psd/psd_filter_effects.hpp"
#include "render/compositor.hpp"
#include "synthetic_dng.hpp"
#include "test_fonts.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"

#include <QAbstractItemModel>
#include <QAbstractSpinBox>
#include <QAbstractItemView>
#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QByteArray>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDataStream>
#include <QDockWidget>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFrame>
#include <QGroupBox>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QInputDevice>
#include <QInputDialog>
#include <QKeyEvent>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListView>
#include <QLayout>
#include <QListWidget>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QLocale>
#include <QSizeGrip>
#include <QMetaObject>
#include <QMouseEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QMessageBox>
#include <QIODevice>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPolygonF>
#include <QThread>
#include <QPaintEvent>
#include <QPixmap>
#include <QPointingDevice>
#include <QProgressDialog>
#include <QPushButton>
#include <QStackedWidget>
#include <QRadioButton>
#include <QSpinBox>
#include <QStringList>
#include <QScrollBar>
#include <QScreen>
#include <QSettings>
#include <QSlider>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QStyleOptionSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTabletEvent>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextLayout>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVariant>
#include <QWheelEvent>
#include <QWindow>
#include <QWidget>

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

namespace {

using namespace patchy::test::ui;

void ui_qimage_import_export_preserves_alpha_and_formats() {
  ensure_artifact_dir();
  QImage source(3, 2, QImage::Format_RGBA8888);
  source.fill(Qt::transparent);
  source.setPixelColor(0, 0, QColor(255, 0, 0, 128));
  source.setPixelColor(1, 0, QColor(0, 255, 0, 255));
  source.setPixelColor(2, 0, QColor(0, 0, 255, 32));
  source.setDotsPerMeterX(11811);
  source.setDotsPerMeterY(5906);

  const auto document = patchy::ui::document_from_qimage(source, "Alpha Import");
  CHECK(std::abs(document.print_settings().horizontal_ppi - 300.0) < 0.02);
  CHECK(std::abs(document.print_settings().vertical_ppi - 150.0) < 0.02);
  const auto exported = patchy::ui::qimage_from_document(document, true);
  CHECK(exported.hasAlphaChannel());
  CHECK(std::abs(exported.dotsPerMeterX() - 11811) <= 1);
  CHECK(std::abs(exported.dotsPerMeterY() - 5906) <= 1);
  CHECK(exported.pixelColor(0, 0).alpha() == 128);
  CHECK(exported.pixelColor(1, 0).green() == 255);

  CHECK(exported.save(QStringLiteral("test-artifacts/format_alpha.png")));
  CHECK(patchy::ui::qimage_from_document(document, false).save(QStringLiteral("test-artifacts/format_flat.jpg")));
  CHECK(patchy::ui::qimage_from_document(document, false).save(QStringLiteral("test-artifacts/format_flat.bmp")));
  CHECK(QImage(QStringLiteral("test-artifacts/format_alpha.png")).pixelColor(0, 0).alpha() == 128);
}

void ui_qimage_import_export_writes_tiff_and_webp() {
  ensure_artifact_dir();
  const auto has_format = [](const QList<QByteArray>& formats, const char* expected) {
    return std::any_of(formats.begin(), formats.end(), [expected](QByteArray format) {
      return format.toLower() == QByteArray(expected);
    });
  };

  CHECK(has_format(QImageReader::supportedImageFormats(), "tiff") ||
        has_format(QImageReader::supportedImageFormats(), "tif"));
  CHECK(has_format(QImageWriter::supportedImageFormats(), "tiff") ||
        has_format(QImageWriter::supportedImageFormats(), "tif"));
  CHECK(has_format(QImageReader::supportedImageFormats(), "webp"));
  CHECK(has_format(QImageWriter::supportedImageFormats(), "webp"));

  QImage source(6, 4, QImage::Format_RGBA8888);
  source.fill(QColor(12, 34, 56, 90));
  source.setPixelColor(1, 1, QColor(220, 40, 90, 180));
  source.setPixelColor(3, 2, QColor(40, 210, 110, 255));
  source.setDotsPerMeterX(11811);
  source.setDotsPerMeterY(11811);

  const auto document = patchy::ui::document_from_qimage(source, "Codec Alpha");
  patchy::ui::ImageSaveOptions options;
  const QStringList extensions{QStringLiteral("tif"), QStringLiteral("webp")};
  for (const auto& extension : extensions) {
    const auto path = QStringLiteral("test-artifacts/format_alpha.%1").arg(extension);
    patchy::ui::write_flat_image_file(document, path, extension, options);
    const QImage written(path);
    CHECK(!written.isNull());
    CHECK(written.size() == source.size());
    CHECK(written.hasAlphaChannel());
    CHECK(written.pixelColor(0, 0).alpha() < 255);
  }
}

void ui_image_save_options_write_bmp_alpha_and_jpeg_quality() {
  ensure_artifact_dir();

  QImage source(4, 3, QImage::Format_RGBA8888);
  source.fill(Qt::transparent);
  source.setPixelColor(0, 0, QColor(255, 0, 0, 64));
  source.setPixelColor(1, 0, QColor(0, 255, 0, 128));
  source.setPixelColor(2, 1, QColor(0, 0, 255, 255));
  source.setDotsPerMeterX(11811);
  source.setDotsPerMeterY(5906);

  const auto alpha_document = patchy::ui::document_from_qimage(source, "BMP Alpha");
  patchy::ui::ImageSaveOptions options;
  options.bmp_encoding = patchy::bmp::BmpEncoding::Rgba32;
  patchy::ui::write_flat_image_file(alpha_document, QStringLiteral("test-artifacts/format_alpha.bmp"),
                                    QStringLiteral("bmp"), options);
  const QImage alpha_bmp(QStringLiteral("test-artifacts/format_alpha.bmp"));
  CHECK(!alpha_bmp.isNull());
  CHECK(alpha_bmp.hasAlphaChannel());
  CHECK(alpha_bmp.pixelColor(0, 0).alpha() == 64);
  CHECK(alpha_bmp.pixelColor(1, 0).alpha() == 128);
  CHECK(std::abs(alpha_bmp.dotsPerMeterX() - 11811) <= 1);
  CHECK(std::abs(alpha_bmp.dotsPerMeterY() - 5906) <= 1);

  options.bmp_encoding = patchy::bmp::BmpEncoding::Rgb24;
  patchy::ui::write_flat_image_file(alpha_document, QStringLiteral("test-artifacts/format_flat_no_alpha.bmp"),
                                    QStringLiteral("bmp"), options);
  const QImage flat_bmp(QStringLiteral("test-artifacts/format_flat_no_alpha.bmp"));
  CHECK(!flat_bmp.isNull());
  CHECK(flat_bmp.pixelColor(0, 0).alpha() == 255);

  QImage indexed_source(4, 1, QImage::Format_RGB888);
  indexed_source.setPixelColor(0, 0, QColor(0, 0, 0));
  indexed_source.setPixelColor(1, 0, QColor(255, 0, 0));
  indexed_source.setPixelColor(2, 0, QColor(0, 255, 0));
  indexed_source.setPixelColor(3, 0, QColor(0, 0, 255));
  const auto indexed_document = patchy::ui::document_from_qimage(indexed_source, "Indexed BMP");
  options.bmp_encoding = patchy::bmp::BmpEncoding::Indexed4;
  options.bmp_palette_mode = patchy::bmp::BmpPaletteMode::Exact;
  patchy::ui::write_flat_image_file(indexed_document, QStringLiteral("test-artifacts/format_indexed4.bmp"),
                                    QStringLiteral("bmp"), options);
  const auto indexed_read = patchy::bmp::DocumentIo::read_file("test-artifacts/format_indexed4.bmp");
  CHECK(indexed_read.indexed_palette().has_value());
  CHECK(indexed_read.indexed_palette()->source_bit_depth == 4);
  CHECK(indexed_read.layers().front().pixels().pixel(3, 0)[2] == 255);

  QImage quantized_source(18, 18, QImage::Format_RGB888);
  for (int y = 0; y < quantized_source.height(); ++y) {
    for (int x = 0; x < quantized_source.width(); ++x) {
      quantized_source.setPixelColor(x, y, QColor((x * 31 + y * 7) % 256, (x * 11 + y * 19) % 256,
                                                  (x * 5 + y * 29) % 256));
    }
  }
  const auto quantized_document = patchy::ui::document_from_qimage(quantized_source, "Quantized BMP");
  options.bmp_encoding = patchy::bmp::BmpEncoding::Indexed8;
  options.bmp_palette_mode = patchy::bmp::BmpPaletteMode::Quantize;
  patchy::ui::write_flat_image_file(quantized_document, QStringLiteral("test-artifacts/format_indexed8_quantized.bmp"),
                                    QStringLiteral("bmp"), options);
  const auto quantized_read = patchy::bmp::DocumentIo::read_file("test-artifacts/format_indexed8_quantized.bmp");
  CHECK(quantized_read.indexed_palette().has_value());
  CHECK(quantized_read.indexed_palette()->colors.size() <= 256);

  const QString palette_path = QStringLiteral("test-artifacts/save_palette.pal");
  {
    QFile palette_file(palette_path);
    CHECK(palette_file.open(QIODevice::WriteOnly | QIODevice::Text));
    CHECK(palette_file.write("JASC-PAL\n0100\n4\n0 0 0\n255 0 0\n0 255 0\n0 0 255\n") > 0);
  }
  QImage palette_mapped_source(2, 1, QImage::Format_RGB888);
  palette_mapped_source.setPixelColor(0, 0, QColor(245, 12, 12));
  palette_mapped_source.setPixelColor(1, 0, QColor(8, 10, 240));
  const auto palette_mapped_document = patchy::ui::document_from_qimage(palette_mapped_source, "Palette BMP");
  options.bmp_encoding = patchy::bmp::BmpEncoding::Indexed4;
  options.bmp_palette_mode = patchy::bmp::BmpPaletteMode::PaletteFile;
  options.bmp_palette_path = palette_path;
  patchy::ui::write_flat_image_file(palette_mapped_document, QStringLiteral("test-artifacts/format_indexed4_palette.bmp"),
                                    QStringLiteral("bmp"), options);
  const auto palette_mapped_read = patchy::bmp::DocumentIo::read_file("test-artifacts/format_indexed4_palette.bmp");
  CHECK(palette_mapped_read.indexed_palette().has_value());
  CHECK(palette_mapped_read.indexed_palette()->colors.size() == 4);
  CHECK(palette_mapped_read.layers().front().pixels().pixel(0, 0)[0] == 255);
  CHECK(palette_mapped_read.layers().front().pixels().pixel(1, 0)[2] == 255);

  patchy::Document jpeg_document(128, 128, patchy::PixelFormat::rgb8());
  patchy::PixelBuffer jpeg_pixels(128, 128, patchy::PixelFormat::rgb8());
  for (std::int32_t y = 0; y < jpeg_pixels.height(); ++y) {
    for (std::int32_t x = 0; x < jpeg_pixels.width(); ++x) {
      auto* px = jpeg_pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>((x * 29 + y * 11 + (x * y) % 251) % 256);
      px[1] = static_cast<std::uint8_t>((x * 7 + y * 37 + (x + y) * 13) % 256);
      px[2] = static_cast<std::uint8_t>((x * 41 + y * 5 + (x * 3) % 197) % 256);
    }
  }
  jpeg_document.add_pixel_layer("JPEG Pattern", std::move(jpeg_pixels));

  options.jpeg_quality = 5;
  patchy::ui::write_flat_image_file(jpeg_document, QStringLiteral("test-artifacts/quality_low.jpg"),
                                    QStringLiteral("jpg"), options);
  options.jpeg_quality = 100;
  patchy::ui::write_flat_image_file(jpeg_document, QStringLiteral("test-artifacts/quality_high.jpg"),
                                    QStringLiteral("jpg"), options);
  const auto low_size = QFileInfo(QStringLiteral("test-artifacts/quality_low.jpg")).size();
  const auto high_size = QFileInfo(QStringLiteral("test-artifacts/quality_high.jpg")).size();
  CHECK(low_size > 0);
  CHECK(high_size > low_size + 1000);
}

void ui_flat_alpha_round_trips_as_editable_mask() {
  ensure_artifact_dir();

  // A 32-bit BI_RGB BMP (compression 0) whose fourth byte carries a non-uniform mask. The
  // original colors are uniform so we can later confirm the mask is non-destructive.
  constexpr std::int32_t kWidth = 4;
  constexpr std::int32_t kHeight = 4;
  const auto mask_alpha_at = [](std::int32_t x, std::int32_t y) -> std::uint8_t {
    if (x == 0 && y == 0) {
      return 0;  // fully masked corner
    }
    if (x == 1 && y == 1) {
      return 128;  // partial
    }
    return 255;  // opaque elsewhere
  };
  std::vector<std::uint8_t> bmp;
  const auto push_u16 = [&bmp](std::uint16_t value) {
    bmp.push_back(static_cast<std::uint8_t>(value & 0xFF));
    bmp.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
  };
  const auto push_u32 = [&bmp](std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
      bmp.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
    }
  };
  const std::uint32_t pixel_offset = 14 + 40;
  const std::uint32_t pixel_bytes = static_cast<std::uint32_t>(kWidth) * kHeight * 4U;
  bmp.push_back('B');
  bmp.push_back('M');
  push_u32(pixel_offset + pixel_bytes);
  push_u32(0);
  push_u32(pixel_offset);
  push_u32(40);                                    // DIB header size
  push_u32(static_cast<std::uint32_t>(kWidth));
  push_u32(static_cast<std::uint32_t>(kHeight));   // positive height -> bottom-up
  push_u16(1);
  push_u16(32);
  push_u32(0);                                     // BI_RGB
  push_u32(pixel_bytes);
  push_u32(0);
  push_u32(0);
  push_u32(0);
  push_u32(0);
  for (std::int32_t file_y = 0; file_y < kHeight; ++file_y) {
    const std::int32_t doc_y = kHeight - 1 - file_y;  // bottom-up storage
    for (std::int32_t x = 0; x < kWidth; ++x) {
      bmp.push_back(50);                              // B
      bmp.push_back(100);                             // G
      bmp.push_back(200);                             // R
      bmp.push_back(mask_alpha_at(x, doc_y));         // A
    }
  }

  auto bmp_document = patchy::bmp::DocumentIo::read(bmp);
  CHECK(bmp_document.layers().front().pixels().format() == patchy::PixelFormat::rgba8());

  // The shared load step turns the alpha into an editable grayscale mask and makes the
  // pixels opaque RGB, preserving the original colors everywhere.
  const bool created_mask = patchy::ui::promote_flat_alpha_to_layer_mask(bmp_document);
  CHECK(created_mask);
  CHECK(bmp_document.layers().size() == 1);
  const auto& masked_layer = bmp_document.layers().front();
  CHECK(masked_layer.pixels().format() == patchy::PixelFormat::rgb8());
  CHECK(masked_layer.pixels().pixel(0, 0)[0] == 200);  // colors kept under the mask
  CHECK(masked_layer.mask().has_value());
  CHECK(masked_layer.mask()->pixels.format() == patchy::PixelFormat::gray8());
  CHECK(masked_layer.mask()->pixels.pixel(0, 0)[0] == 0);
  CHECK(masked_layer.mask()->pixels.pixel(1, 1)[0] == 128);
  CHECK(masked_layer.mask()->pixels.pixel(2, 2)[0] == 255);

  // Uniformly opaque alpha must not create a mask.
  patchy::Document opaque(kWidth, kHeight, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer opaque_pixels(kWidth, kHeight, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < kHeight; ++y) {
    for (std::int32_t x = 0; x < kWidth; ++x) {
      auto* px = opaque_pixels.pixel(x, y);
      px[0] = 10;
      px[1] = 20;
      px[2] = 30;
      px[3] = 255;
    }
  }
  opaque.add_pixel_layer("Opaque", std::move(opaque_pixels));
  CHECK(!patchy::ui::promote_flat_alpha_to_layer_mask(opaque));
  CHECK(!opaque.layers().front().mask().has_value());
  CHECK(opaque.layers().front().pixels().format() == patchy::PixelFormat::rgb8());

  // BMP round-trip: the mask becomes 32-bit alpha and the colors stay intact.
  patchy::ui::ImageSaveOptions options;
  options.bmp_encoding = patchy::bmp::BmpEncoding::Rgba32;
  patchy::ui::write_flat_image_file(bmp_document, QStringLiteral("test-artifacts/alpha_mask_round_trip.bmp"),
                                    QStringLiteral("bmp"), options);
  const QImage bmp_reloaded(QStringLiteral("test-artifacts/alpha_mask_round_trip.bmp"));
  CHECK(!bmp_reloaded.isNull());
  CHECK(bmp_reloaded.hasAlphaChannel());
  CHECK(bmp_reloaded.pixelColor(0, 0).alpha() == 0);
  CHECK(bmp_reloaded.pixelColor(1, 1).alpha() == 128);
  CHECK(bmp_reloaded.pixelColor(2, 2).alpha() == 255);
  CHECK(bmp_reloaded.pixelColor(0, 0).red() == 200);  // colors preserved under the mask

  // PNG round-trip via Qt keeps the mask as alpha and the colors intact.
  patchy::ui::write_flat_image_file(bmp_document, QStringLiteral("test-artifacts/alpha_mask_round_trip.png"),
                                    QStringLiteral("png"), options);
  const QImage png_reloaded(QStringLiteral("test-artifacts/alpha_mask_round_trip.png"));
  CHECK(!png_reloaded.isNull());
  CHECK(png_reloaded.pixelColor(0, 0).alpha() == 0);
  CHECK(png_reloaded.pixelColor(0, 0).red() == 200);

  // A flat PSD has no layer record to own the mask, so its positive extra
  // plane reloads as a real saved alpha channel and does not affect the image.
  const auto flat_bytes = patchy::psd::DocumentIo::write_flat_rgb8(bmp_document);
  const QByteArray flat_raw(reinterpret_cast<const char*>(flat_bytes.data()), static_cast<int>(flat_bytes.size()));
  CHECK(flat_raw.contains(QByteArrayLiteral("Alpha 1")));
  const auto flat_reloaded =
      patchy::psd::DocumentIo::read(flat_bytes, patchy::psd::ReadOptions{true, false, true});
  CHECK(flat_reloaded.layers().size() == 1);
  CHECK(!flat_reloaded.layers().front().mask().has_value());
  CHECK(flat_reloaded.layers().front().pixels().pixel(0, 0)[0] == 200);
  CHECK(flat_reloaded.channels().size() == 1);
  CHECK(flat_reloaded.channels().front().name() == "Alpha 1");
  CHECK(flat_reloaded.channels().front().pixels().pixel(0, 0)[0] == 0);
  CHECK(flat_reloaded.channels().front().pixels().pixel(1, 1)[0] == 128);
  CHECK(flat_reloaded.channels().front().pixels().pixel(2, 2)[0] == 255);

  // A layered PSD keeps the applied raster mask as layer channel -2. It is
  // neither promoted nor duplicated as a saved document channel.
  const auto layered_bytes = patchy::psd::DocumentIo::write_layered_rgb8(bmp_document);
  const QByteArray layered_raw(reinterpret_cast<const char*>(layered_bytes.data()),
                               static_cast<int>(layered_bytes.size()));
  CHECK(!layered_raw.contains(QByteArrayLiteral("Alpha 1")));
  const auto layered_reloaded =
      patchy::psd::DocumentIo::read(layered_bytes, patchy::psd::ReadOptions{true, false, true});
  CHECK(layered_reloaded.layers().size() == 1);
  CHECK(layered_reloaded.channels().empty());
  const auto& layered_layer = layered_reloaded.layers().front();
  CHECK(layered_layer.mask().has_value());
  CHECK(layered_layer.mask()->pixels.format() == patchy::PixelFormat::gray8());
  CHECK(layered_layer.mask()->pixels.pixel(0, 0)[0] == 0);
  CHECK(layered_layer.mask()->pixels.pixel(1, 1)[0] == 128);
  CHECK(layered_layer.pixels().pixel(0, 0)[0] == 200);
}

void ui_image_save_options_defaults_and_dialogs() {
  auto settings = patchy::ui::app_settings();
  settings.remove(QStringLiteral("saveOptions"));
  settings.sync();

  auto defaults = patchy::ui::load_image_save_option_defaults();
  CHECK(defaults.jpeg_quality == 95);
  CHECK(defaults.bmp_encoding == patchy::bmp::BmpEncoding::Rgba32);
  CHECK(defaults.bmp_palette_mode == patchy::bmp::BmpPaletteMode::Exact);
  CHECK(defaults.bmp_palette_path.isEmpty());
  CHECK(patchy::ui::image_save_options_apply_to_extension(QStringLiteral("jpg")));
  CHECK(patchy::ui::image_save_options_apply_to_extension(QStringLiteral(".bmp")));
  CHECK(!patchy::ui::image_save_options_apply_to_extension(QStringLiteral("png")));

  settings.setValue(QStringLiteral("saveOptions/bmpPreserveAlpha"), false);
  settings.sync();
  const auto migrated = patchy::ui::load_image_save_option_defaults();
  CHECK(migrated.bmp_encoding == patchy::bmp::BmpEncoding::Rgb24);

  defaults.jpeg_quality = 37;
  defaults.bmp_encoding = patchy::bmp::BmpEncoding::Indexed4;
  defaults.bmp_palette_mode = patchy::bmp::BmpPaletteMode::PaletteFile;
  defaults.bmp_palette_path = QStringLiteral("test-artifacts/save_palette.pal");
  patchy::ui::save_image_save_option_defaults(defaults);
  const auto loaded = patchy::ui::load_image_save_option_defaults();
  CHECK(loaded.jpeg_quality == 37);
  CHECK(loaded.bmp_encoding == patchy::bmp::BmpEncoding::Indexed4);
  CHECK(loaded.bmp_palette_mode == patchy::bmp::BmpPaletteMode::PaletteFile);
  CHECK(loaded.bmp_palette_path == defaults.bmp_palette_path);

  bool saw_jpeg_dialog = false;
  QTimer::singleShot(0, [&saw_jpeg_dialog] {
    auto* dialog = find_top_level_dialog(QStringLiteral("jpegSaveOptionsDialog"));
    CHECK(dialog != nullptr);
    auto* quality = dialog->findChild<QSpinBox*>(QStringLiteral("jpegQualitySpin"));
    CHECK(quality != nullptr);
    CHECK(quality->value() == 37);
    auto* quality_slider = dialog->findChild<QSlider*>(QStringLiteral("jpegQualitySlider"));
    CHECK(quality_slider != nullptr);
    CHECK(quality_slider->value() == 37);
    quality_slider->setValue(64);
    CHECK(quality->value() == 64);
    quality->setValue(82);
    CHECK(quality_slider->value() == 82);
    saw_jpeg_dialog = true;
    dialog->accept();
  });
  auto jpeg_options = patchy::ui::prompt_image_save_options(nullptr, QStringLiteral("jpeg"), loaded);
  CHECK(saw_jpeg_dialog);
  CHECK(jpeg_options.has_value());
  CHECK(jpeg_options->jpeg_quality == 82);
  CHECK(jpeg_options->bmp_encoding == patchy::bmp::BmpEncoding::Indexed4);

  bool saw_bmp_dialog = false;
  QTimer::singleShot(0, [&saw_bmp_dialog] {
    auto* dialog = find_top_level_dialog(QStringLiteral("bmpSaveOptionsDialog"));
    CHECK(dialog != nullptr);
    auto* indexed4 = dialog->findChild<QRadioButton*>(QStringLiteral("bmpEncodingIndexed4Radio"));
    CHECK(indexed4 != nullptr);
    CHECK(indexed4->isChecked());
    auto* palette_file = dialog->findChild<QRadioButton*>(QStringLiteral("bmpPaletteFileRadio"));
    CHECK(palette_file != nullptr);
    CHECK(palette_file->isChecked());
    auto* palette_path = dialog->findChild<QLineEdit*>(QStringLiteral("bmpPalettePathEdit"));
    CHECK(palette_path != nullptr);
    CHECK(palette_path->text() == QStringLiteral("test-artifacts/save_palette.pal"));
    auto* indexed2 = dialog->findChild<QRadioButton*>(QStringLiteral("bmpEncodingIndexed2Radio"));
    CHECK(indexed2 != nullptr);
    indexed2->click();
    auto* exact = dialog->findChild<QRadioButton*>(QStringLiteral("bmpPaletteExactRadio"));
    CHECK(exact != nullptr);
    CHECK(exact->isChecked());
    CHECK(!palette_file->isEnabled());
    CHECK(!palette_path->isEnabled());
    auto* browse = dialog->findChild<QPushButton*>(QStringLiteral("bmpPaletteBrowseButton"));
    CHECK(browse != nullptr);
    CHECK(browse->isEnabled());
    auto* indexed8 = dialog->findChild<QRadioButton*>(QStringLiteral("bmpEncodingIndexed8Radio"));
    CHECK(indexed8 != nullptr);
    indexed8->click();
    CHECK(palette_file->isEnabled());
    palette_file->click();
    CHECK(palette_path->isEnabled());
    palette_path->setText(QStringLiteral("test-artifacts/other_palette.pal"));
    saw_bmp_dialog = true;
    dialog->accept();
  });
  auto bmp_options = patchy::ui::prompt_image_save_options(nullptr, QStringLiteral(".bmp"), *jpeg_options);
  CHECK(saw_bmp_dialog);
  CHECK(bmp_options.has_value());
  CHECK(bmp_options->bmp_encoding == patchy::bmp::BmpEncoding::Indexed8);
  CHECK(bmp_options->bmp_palette_mode == patchy::bmp::BmpPaletteMode::PaletteFile);
  CHECK(bmp_options->bmp_palette_path == QStringLiteral("test-artifacts/other_palette.pal"));

  settings.remove(QStringLiteral("saveOptions"));
  settings.sync();
}

void ui_ico_export_dialog_sizes_and_resample() {
  auto settings = patchy::ui::app_settings();
  settings.remove(QStringLiteral("saveOptions"));
  settings.sync();

  CHECK(patchy::ui::image_save_options_apply_to_extension(QStringLiteral("ico")));
  CHECK(patchy::ui::image_save_options_apply_to_extension(QStringLiteral(".cur")));

  auto defaults = patchy::ui::load_image_save_option_defaults();
  CHECK(defaults.ico_sizes == (std::vector<int>{16, 24, 32, 48, 64, 128, 256}));
  CHECK(defaults.ico_resample == patchy::ui::IcoResample::Auto);

  bool saw_ico_dialog = false;
  QTimer::singleShot(0, [&saw_ico_dialog] {
    auto* dialog = find_top_level_dialog(QStringLiteral("icoSaveOptionsDialog"));
    CHECK(dialog != nullptr);
    auto* ok_button = dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
    CHECK(ok_button != nullptr);
    CHECK(ok_button->isEnabled());
    // Unchecking every size disables OK; one size re-enables it.
    for (const auto size : {16, 24, 32, 48, 64, 128, 256}) {
      auto* check = dialog->findChild<QCheckBox*>(QStringLiteral("icoSize%1Check").arg(size));
      CHECK(check != nullptr);
      CHECK(check->isChecked());
      check->setChecked(false);
    }
    CHECK(!ok_button->isEnabled());
    auto* size32 = dialog->findChild<QCheckBox*>(QStringLiteral("icoSize32Check"));
    size32->setChecked(true);
    CHECK(ok_button->isEnabled());
    auto* resample = dialog->findChild<QComboBox*>(QStringLiteral("icoResampleCombo"));
    CHECK(resample != nullptr);
    resample->setCurrentIndex(resample->findData(static_cast<int>(patchy::ui::IcoResample::Nearest)));
    // The ICO dialog has no hotspot spins.
    CHECK(dialog->findChild<QSpinBox*>(QStringLiteral("curHotspotXSpin")) == nullptr);
    saw_ico_dialog = true;
    dialog->accept();
  });
  const auto ico_options = patchy::ui::prompt_image_save_options(nullptr, QStringLiteral("ico"), defaults);
  CHECK(saw_ico_dialog);
  CHECK(ico_options.has_value());
  CHECK(ico_options->ico_sizes == (std::vector<int>{32}));
  CHECK(ico_options->ico_resample == patchy::ui::IcoResample::Nearest);

  bool saw_cur_dialog = false;
  QTimer::singleShot(0, [&saw_cur_dialog] {
    auto* dialog = find_top_level_dialog(QStringLiteral("curSaveOptionsDialog"));
    CHECK(dialog != nullptr);
    auto* hotspot_x = dialog->findChild<QSpinBox*>(QStringLiteral("curHotspotXSpin"));
    auto* hotspot_y = dialog->findChild<QSpinBox*>(QStringLiteral("curHotspotYSpin"));
    CHECK(hotspot_x != nullptr);
    CHECK(hotspot_y != nullptr);
    hotspot_x->setValue(3);
    hotspot_y->setValue(4);
    saw_cur_dialog = true;
    dialog->accept();
  });
  const auto cur_options = patchy::ui::prompt_image_save_options(nullptr, QStringLiteral("cur"), *ico_options);
  CHECK(saw_cur_dialog);
  CHECK(cur_options.has_value());
  CHECK(cur_options->cur_hotspot_x == 3);
  CHECK(cur_options->cur_hotspot_y == 4);

  // End to end: the export path writes a real multi-size ICO that reopens with named layers.
  patchy::Document document(32, 32, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Icon Art", solid_pixels(32, 32, patchy::PixelFormat::rgba8(), QColor(20, 90, 200, 255)));
  auto write_options = *cur_options;
  write_options.ico_sizes = {16, 32};
  const auto path = QStringLiteral("test-artifacts/ui_ico_export.ico");
  patchy::ui::write_flat_image_file(document, path, QStringLiteral("ico"), write_options);
  std::vector<std::string> notices;
  const auto reopened = patchy::ico::DocumentIo::read_file(path.toStdString(), &notices);
  CHECK(reopened.width() == 32);
  CHECK(reopened.layers().size() == 2);
  CHECK(reopened.layers().front().name() == "16x16");
  CHECK(reopened.layers().back().name() == "32x32");
  CHECK(reopened.layers().back().pixels().pixel(16, 16)[2] == 200);

  settings.remove(QStringLiteral("saveOptions"));
  settings.sync();
}

void ui_ico_real_world_fixtures_decode_png_entries() {
  // The core suite reads these fixtures without a PNG decoder (PNG entries skip with a
  // notice); with the Qt codec installed every entry must decode, including the 256 px
  // PNG-compressed ones in the real-world icons.
  patchy::ui::install_ico_png_codec();
  const std::array<const char*, 3> names = {"pillow-multisize-png.ico", "cpython-py.ico", "vscode-code.ico"};
  for (const auto* name : names) {
    std::vector<std::string> notices;
    const auto document =
        patchy::ico::DocumentIo::read_file(patchy::test::committed_format_fixture_path("ico", name), &notices);
    CHECK(!document.layers().empty());
    for (const auto& notice : notices) {
      CHECK(notice.find("PNG") == std::string::npos);
    }
  }
  const auto multisize = patchy::ico::DocumentIo::read_file(
      patchy::test::committed_format_fixture_path("ico", "pillow-multisize-png.ico"));
  CHECK(multisize.width() == 256);
  CHECK(multisize.layers().back().name() == "256x256");
  CHECK(multisize.layers().back().pixels().pixel(128, 128)[3] == 255);
}

void ui_gif_export_round_trips_through_qt_reader() {
  std::filesystem::create_directories("test-artifacts");

  // RGB document with transparency: quantized write, read back through Qt's qgif plugin.
  patchy::Document document(64, 48, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer pixels(64, 48, patchy::PixelFormat::rgba8());
  const std::array<patchy::RgbColor, 4> colors = {{{10, 200, 50}, {240, 240, 240}, {60, 60, 220}, {200, 30, 40}}};
  for (std::int32_t y = 0; y < 48; ++y) {
    for (std::int32_t x = 0; x < 64; ++x) {
      auto* px = pixels.pixel(x, y);
      if (x < 4 && y < 4) {
        px[3] = 0;
        continue;
      }
      const auto& color = colors[static_cast<std::size_t>((x / 8 + y / 8) % colors.size())];
      px[0] = color.red;
      px[1] = color.green;
      px[2] = color.blue;
      px[3] = 255;
    }
  }
  document.add_pixel_layer("Art", std::move(pixels));
  const auto path = QStringLiteral("test-artifacts/ui_gif_export.gif");
  patchy::ui::write_flat_image_file(document, path, QStringLiteral("gif"));

  QImageReader reader(path);
  const auto image = reader.read().convertToFormat(QImage::Format_RGBA8888);
  CHECK(!image.isNull());
  CHECK(image.width() == 64);
  CHECK(image.height() == 48);
  CHECK(image.pixelColor(1, 1).alpha() == 0);  // transparent corner survives
  for (std::int32_t y = 6; y < 48; y += 9) {
    for (std::int32_t x = 6; x < 64; x += 9) {
      const auto expected = colors[static_cast<std::size_t>((x / 8 + y / 8) % colors.size())];
      const auto actual = image.pixelColor(x, y);
      CHECK(actual.alpha() == 255);
      CHECK(actual.red() == expected.red);
      CHECK(actual.green() == expected.green);
      CHECK(actual.blue() == expected.blue);
    }
  }

  // Palette-mode document: the file's color table is the document palette in order.
  patchy::Document indexed_doc(16, 16, patchy::PixelFormat::rgb8());
  const auto* preset = patchy::find_builtin_palette_preset("gameboy");
  CHECK(preset != nullptr);
  patchy::DocumentPaletteEditing editing;
  editing.palette.colors.assign(preset->colors.begin(), preset->colors.end());
  editing.palette_revision = 1;
  indexed_doc.palette_editing() = editing;
  patchy::PixelBuffer indexed_pixels(16, 16, patchy::PixelFormat::rgb8());
  for (std::int32_t y = 0; y < 16; ++y) {
    for (std::int32_t x = 0; x < 16; ++x) {
      const auto& color = preset->colors[static_cast<std::size_t>(x % preset->colors.size())];
      auto* px = indexed_pixels.pixel(x, y);
      px[0] = color.red;
      px[1] = color.green;
      px[2] = color.blue;
    }
  }
  indexed_doc.add_pixel_layer("Pixels", std::move(indexed_pixels));
  const auto indexed_path = QStringLiteral("test-artifacts/ui_gif_export_indexed.gif");
  patchy::ui::write_flat_image_file(indexed_doc, indexed_path, QStringLiteral("gif"));
  QImageReader indexed_reader(indexed_path);
  const auto indexed_image = indexed_reader.read().convertToFormat(QImage::Format_RGB888);
  CHECK(!indexed_image.isNull());
  for (std::int32_t x = 0; x < 16; ++x) {
    const auto expected = preset->colors[static_cast<std::size_t>(x % preset->colors.size())];
    const auto actual = indexed_image.pixelColor(x, 8);
    CHECK(actual.red() == expected.red);
    CHECK(actual.green() == expected.green);
    CHECK(actual.blue() == expected.blue);
  }
}

void ui_animated_gif_open_notes_first_frame_only() {
  const auto path = QString::fromStdWString(
      patchy::test::committed_format_fixture_path("gif", "pillow-animated.gif").wstring());
  CHECK(QFileInfo::exists(path));

  SettingsValueRestorer notes_setting(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);

  // Import notes ride the status bar by default (no popup unless the preference is on).
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();

  const auto status = window.statusBar()->currentMessage();
  CHECK(status.contains(QStringLiteral("first frame")));
  CHECK(status.contains(QStringLiteral("3")));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.width() == 32);
  CHECK(document.height() == 24);
  CHECK(document.layers().size() == 1);
}

void ui_import_notices_dialog_shown_when_setting_enabled() {
  const auto path = QString::fromStdWString(
      patchy::test::committed_format_fixture_path("gif", "pillow-animated.gif").wstring());
  CHECK(QFileInfo::exists(path));

  SettingsValueRestorer notes_setting(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().setValue(QStringLiteral("imports/showPsdWarningsAndInfo"), true);
  patchy::ui::MainWindow window;
  show_window(window);

  // With the preference on the popup appears; it needs the repeating-timer dismissal
  // (a one-shot fires too early, during the open-progress phase, and the suite hangs).
  bool saw_notice = false;
  QString notice_text;
  int poll_attempts = 0;
  QTimer poller;
  QObject::connect(&poller, &QTimer::timeout, [&saw_notice, &notice_text, &poll_attempts, &poller] {
    if (++poll_attempts > 500) {
      poller.stop();
      return;
    }
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* box = qobject_cast<QMessageBox*>(widget);
      if (box != nullptr && box->objectName() == QStringLiteral("importNoticesMessageBox") && box->isVisible()) {
        saw_notice = true;
        notice_text = box->text();
        box->accept();
        poller.stop();
        return;
      }
    }
  });
  poller.start(10);
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  poller.stop();

  CHECK(saw_notice);
  CHECK(notice_text.contains(QStringLiteral("first frame")));
  // The status bar carries the note either way.
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("first frame")));
}

}  // namespace

std::vector<patchy::test::TestCase> flat_image_format_tests() {
  return {
      {"ui_qimage_import_export_preserves_alpha_and_formats", ui_qimage_import_export_preserves_alpha_and_formats},
      {"ui_qimage_import_export_writes_tiff_and_webp", ui_qimage_import_export_writes_tiff_and_webp},
      {"ui_image_save_options_write_bmp_alpha_and_jpeg_quality",
       ui_image_save_options_write_bmp_alpha_and_jpeg_quality},
      {"ui_flat_alpha_round_trips_as_editable_mask", ui_flat_alpha_round_trips_as_editable_mask},
      {"ui_image_save_options_defaults_and_dialogs", ui_image_save_options_defaults_and_dialogs},
      {"ui_ico_export_dialog_sizes_and_resample", ui_ico_export_dialog_sizes_and_resample},
      {"ui_ico_real_world_fixtures_decode_png_entries", ui_ico_real_world_fixtures_decode_png_entries},
      {"ui_gif_export_round_trips_through_qt_reader", ui_gif_export_round_trips_through_qt_reader},
      {"ui_animated_gif_open_notes_first_frame_only", ui_animated_gif_open_notes_first_frame_only},
      {"ui_import_notices_dialog_shown_when_setting_enabled",
       ui_import_notices_dialog_shown_when_setting_enabled},
  };
}
