#include <QStatusBar>

#include "formats/animation_timing.hpp"
#include "formats/webp_animation_io.hpp"
#include "formats/apng_writer.hpp"
#include "color/color_management.hpp"
#include "ui/tiff_jpeg_reader.hpp"
#include "ui/apng_reader.hpp"
#include "ui/qt_paths.hpp"
#include "unicode_path_names.hpp"
#include "core/adjustment_layer.hpp"
#include "core/document_depth.hpp"
#include "core/pixel_depth.hpp"
#include "core/layer_metadata.hpp"
#include "core/palette.hpp"
#include "core/palette_presets.hpp"
#include "psd/psd_binary.hpp"
#include "ui/color_panel.hpp"
#include "formats/acv_curves_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/gif_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/tga_document_io.hpp"
#include "ui/image_document_io.hpp"
#include "formats/avif_document_io.hpp"
#include "formats/format_file_io.hpp"
#include "formats/jxl_document_io.hpp"
#include "formats/jxr_document_io.hpp"
#include "formats/dds_document_io.hpp"
#include "formats/rttex_document_io.hpp"
#include "ui/image_save_options_dialog.hpp"
#include "ui/main_window.hpp"
#include "ui/app_settings.hpp"
#include "psd/psd_document_io.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"
#include <QApplication>
#include <QBuffer>
#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QTemporaryDir>
#include <QMessageBox>
#include <QPainter>
#include <QIODevice>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStringList>
#include <QSlider>
#include <QStandardItemModel>
#include <QTimer>
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

const QString kFlatAlphaMaskSetting = QStringLiteral("imports/flatAlphaAsLayerMask");

// The imports/flatAlphaAsLayerMask preference for one test (GitHub issue 534). The format
// tests that pin the document-alpha mask round trip turn it on; it is off by default.
class ScopedFlatAlphaAsLayerMask {
public:
  explicit ScopedFlatAlphaAsLayerMask(bool enabled) : restore_(kFlatAlphaMaskSetting) {
    auto settings = patchy::ui::app_settings();
    settings.setValue(kFlatAlphaMaskSetting, enabled);
    settings.sync();
  }

private:
  SettingsValueRestorer restore_;
};

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

// With deep editing on, a 16-bit PNG opens as a 16-bit document with its samples intact,
// a float image as a 32-bit one (values above 1.0 kept), and the flat-alpha promotion
// keeps both the mask and the color at depth (docs/high-bit-depth.md, Phase 8).
void ui_deep_images_import_at_their_depth() {
  ensure_artifact_dir();
  QImage source(4, 2, QImage::Format_RGBA64);
  for (int y = 0; y < 2; ++y) {
    for (int x = 0; x < 4; ++x) {
      source.setPixelColor(x, y, QColor::fromRgba64(0x1234, 0x8001, 0xfffe, x == 3 ? 0x7fff : 0xffff));
    }
  }
  const auto path = QStringLiteral("test-artifacts/deep_import_16.png");
  CHECK(source.save(path));
  const QImage loaded(path);
  CHECK(loaded.format() == QImage::Format_RGBA64 || loaded.format() == QImage::Format_RGBA64_Premultiplied);

  patchy::set_deep_editing_override(true);
  auto document = patchy::ui::document_from_qimage(loaded, "Deep");
  CHECK(document.color_state().bit_depth == patchy::BitDepth::UInt16);
  CHECK(patchy::document_depth_problems(document).empty());
  const auto& pixels = document.layers().front().pixels();
  CHECK(pixels.format().bit_depth == patchy::BitDepth::UInt16);
  const auto values = patchy::load_pixel(pixels.format(), pixels.pixel(0, 0));
  CHECK(std::abs(values[0] * 257.0F - static_cast<float>(0x1234)) < 0.5F);
  CHECK(std::abs(values[1] * 257.0F - static_cast<float>(0x8001)) < 0.5F);
  CHECK(patchy::ui::promote_flat_alpha_to_layer_mask(document));
  const auto& promoted = document.layers().front();
  CHECK(promoted.mask().has_value());
  CHECK(promoted.mask().has_value() && promoted.mask()->pixels.format().bit_depth == patchy::BitDepth::UInt16);
  CHECK(promoted.pixels().format().channels == 3 &&
        promoted.pixels().format().bit_depth == patchy::BitDepth::UInt16);
  const auto rgb = patchy::load_pixel(promoted.pixels().format(), promoted.pixels().pixel(3, 1));
  CHECK(std::abs(rgb[2] * 257.0F - static_cast<float>(0xfffe)) < 0.5F);
  CHECK(promoted.mask().has_value() &&
        std::abs(patchy::coverage_at(promoted.mask()->pixels, 3, 1) - 32767.0F / 65535.0F) < 1.0F / 65535.0F);
  CHECK(patchy::document_depth_problems(document).empty());

  QImage hdr(2, 1, QImage::Format_RGBX32FPx4);
  hdr.setPixelColor(0, 0, QColor::fromRgbF(0.25F, 0.5F, 1.0F));
  auto* row = reinterpret_cast<float*>(hdr.scanLine(0));
  row[4] = 2.5F;  // above 1.0: kept as linear light
  row[5] = 0.1F;
  row[6] = 0.0F;
  row[7] = 1.0F;
  const auto hdr_document = patchy::ui::document_from_qimage(hdr, "HDR");
  CHECK(hdr_document.color_state().bit_depth == patchy::BitDepth::Float32);
  const auto& hdr_pixels = hdr_document.layers().front().pixels();
  CHECK(hdr_pixels.format().channels == 3);
  CHECK(std::abs(patchy::load_pixel(hdr_pixels.format(), hdr_pixels.pixel(1, 0))[0] - 2.5F * 255.0F) < 0.01F);
  // A 32-bit document saves TIFF as floats (values above 1.0 kept) and reopens at 32 bits.
  const auto tiff_path = QStringLiteral("test-artifacts/deep_export_32.tif");
  patchy::ui::write_flat_image_file(hdr_document, tiff_path, QStringLiteral("tif"));
  const QImage tiff(tiff_path);
  CHECK(tiff.format() == QImage::Format_RGBA32FPx4 || tiff.format() == QImage::Format_RGBX32FPx4 ||
        tiff.format() == QImage::Format_RGBA32FPx4_Premultiplied);
  const auto reopened = patchy::ui::document_from_qimage(tiff, "Reopened");
  CHECK(reopened.color_state().bit_depth == patchy::BitDepth::Float32);
  const auto& reopened_pixels = reopened.layers().front().pixels();
  CHECK(std::abs(patchy::load_pixel(reopened_pixels.format(), reopened_pixels.pixel(1, 0))[0] - 2.5F * 255.0F) <
        0.01F);
  CHECK(std::abs(patchy::load_pixel(reopened_pixels.format(), reopened_pixels.pixel(0, 0))[1] - 0.5F * 255.0F) <
        0.01F);

  patchy::set_deep_editing_override(false);
  const auto eight_bit = patchy::ui::document_from_qimage(loaded, "Eight");
  CHECK(eight_bit.color_state().bit_depth == patchy::BitDepth::UInt8);
  CHECK(eight_bit.layers().front().pixels().format().bit_depth == patchy::BitDepth::UInt8);
  patchy::set_deep_editing_override(std::nullopt);
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

// A 6x4 PNG with a fully transparent pixel at (0,0) over a red the file still stores, a
// half-transparent green at (1,0), and opaque blue elsewhere.
QString write_translucent_png(const QString& name) {
  QImage image(6, 4, QImage::Format_RGBA8888);
  image.fill(QColor(30, 60, 90, 255));
  image.setPixelColor(0, 0, QColor(250, 10, 10, 0));
  image.setPixelColor(1, 0, QColor(20, 200, 40, 128));
  const auto path = QStringLiteral("test-artifacts/") + name;
  QFile::remove(path);
  CHECK(image.save(path, "PNG"));
  return path;
}

// GitHub issue 534: a transparent flat image opens as one ordinary transparent layer (no
// layer mask), so painting into the transparent area shows at once, and a flat save writes
// the same alpha back.
void ui_flat_alpha_opens_as_plain_layer_transparency() {
  ensure_artifact_dir();
  SettingsValueRestorer restore(kFlatAlphaMaskSetting);
  patchy::ui::app_settings().remove(kFlatAlphaMaskSetting);
  const auto source = write_translucent_png(QStringLiteral("flat_alpha_plain_source.png"));

  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, source);
  QApplication::processEvents();
  const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  CHECK(document.layers().size() == 1);
  const auto& layer = document.layers().front();
  CHECK(layer.kind() == patchy::LayerKind::Pixel);
  CHECK(!layer.mask().has_value());
  CHECK(!patchy::layer_mask_is_document_alpha(layer));
  CHECK(layer.pixels().format() == patchy::PixelFormat::rgba8());
  CHECK(layer.pixels().pixel(0, 0)[3] == 0);
  CHECK(layer.pixels().pixel(1, 0)[3] == 128);
  CHECK(layer.pixels().pixel(1, 0)[1] == 200);
  CHECK(layer.pixels().pixel(3, 2)[3] == 255);
  CHECK(layer.pixels().pixel(3, 2)[2] == 90);
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));

  // Save as PNG: the straight alpha and every visible color come back unchanged.
  const QString saved = QStringLiteral("test-artifacts/flat_alpha_plain_saved.png");
  QFile::remove(saved);
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, saved, patchy::ui::ImageSaveOptions{}));
  const QImage original(source);
  const QImage reloaded(saved);
  CHECK(!reloaded.isNull() && reloaded.size() == original.size());
  for (int y = 0; y < original.height(); ++y) {
    for (int x = 0; x < original.width(); ++x) {
      const auto want = original.pixelColor(x, y);
      const auto got = reloaded.pixelColor(x, y);
      CHECK(got.alpha() == want.alpha());
      if (want.alpha() > 0) {
        CHECK(got.red() == want.red() && got.green() == want.green() && got.blue() == want.blue());
      }
    }
  }

  // A transparent WebP opens the same way (the #534 follow-up sample was one), so moving the
  // layer reveals no color stored under its transparent pixels.
  if (QImageWriter::supportedImageFormats().contains("webp")) {
    const QImage webp(source);
    const QString webp_path = QStringLiteral("test-artifacts/flat_alpha_plain_source.webp");
    QFile::remove(webp_path);
    QImageWriter writer(webp_path, "webp");
    writer.setQuality(100);  // lossless
    CHECK(writer.write(webp));
    patchy::ui::MainWindow webp_window;
    show_window(webp_window);
    patchy::ui::MainWindowTestAccess::open_document_path(webp_window, webp_path);
    QApplication::processEvents();
    const auto& webp_document = std::as_const(patchy::ui::MainWindowTestAccess::document(webp_window));
    CHECK(webp_document.layers().size() == 1);
    CHECK(!webp_document.layers().front().mask().has_value());
    CHECK(webp_document.layers().front().pixels().format().channels == 4);
    CHECK(webp_document.layers().front().pixels().pixel(0, 0)[3] == 0);
    CHECK(webp_document.layers().front().pixels().pixel(3, 2)[3] == 255);
  }

  // Uniform alpha is still not transparency: an all-opaque PNG opens as plain RGB.
  QImage opaque(4, 4, QImage::Format_RGBA8888);
  opaque.fill(QColor(10, 20, 30, 255));
  const QString opaque_path = QStringLiteral("test-artifacts/flat_alpha_plain_opaque.png");
  QFile::remove(opaque_path);
  CHECK(opaque.save(opaque_path, "PNG"));
  patchy::ui::MainWindow opaque_window;
  show_window(opaque_window);
  patchy::ui::MainWindowTestAccess::open_document_path(opaque_window, opaque_path);
  QApplication::processEvents();
  const auto& opaque_layer = std::as_const(patchy::ui::MainWindowTestAccess::document(opaque_window)).layers().front();
  CHECK(!opaque_layer.mask().has_value());
  CHECK(opaque_layer.pixels().format() == patchy::PixelFormat::rgb8());
}

// The preference brings back the older import: the alpha becomes an editable layer mask with
// the document-alpha marker over opaque RGB, and a flat save keeps the color under it.
void ui_flat_alpha_layer_mask_preference_keeps_the_mask() {
  ensure_artifact_dir();
  const ScopedFlatAlphaAsLayerMask mask_import(true);
  const auto source = write_translucent_png(QStringLiteral("flat_alpha_mask_source.png"));

  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, source);
  QApplication::processEvents();
  const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  CHECK(document.layers().size() == 1);
  const auto& layer = document.layers().front();
  CHECK(layer.pixels().format() == patchy::PixelFormat::rgb8());
  CHECK(layer.mask().has_value());
  CHECK(patchy::layer_mask_is_document_alpha(layer));
  if (!layer.mask().has_value()) {
    return;
  }
  CHECK(layer.mask()->pixels.pixel(0, 0)[0] == 0);
  CHECK(layer.mask()->pixels.pixel(1, 0)[0] == 128);
  CHECK(layer.mask()->pixels.pixel(3, 2)[0] == 255);
  CHECK(layer.pixels().pixel(0, 0)[0] == 250);  // the color under zero alpha survives

  const QString saved = QStringLiteral("test-artifacts/flat_alpha_mask_saved.png");
  QFile::remove(saved);
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, saved, patchy::ui::ImageSaveOptions{}));
  const QImage reloaded(saved);
  CHECK(!reloaded.isNull());
  CHECK(reloaded.pixelColor(0, 0).alpha() == 0);
  CHECK(reloaded.pixelColor(0, 0).red() == 250);
  CHECK(reloaded.pixelColor(1, 0).alpha() == 128);
  CHECK(reloaded.pixelColor(3, 2).alpha() == 255);
}

// The Preferences checkbox: off by default, and it writes imports/flatAlphaAsLayerMask.
// Saves a crop of the Application page around it for the issue card.
void ui_preferences_flat_alpha_as_layer_mask_checkbox() {
  ensure_artifact_dir();
  SettingsValueRestorer restore(kFlatAlphaMaskSetting);
  patchy::ui::app_settings().remove(kFlatAlphaMaskSetting);
  patchy::ui::MainWindow window;
  show_window(window);
  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) return;
    auto* check = dialog->findChild<QCheckBox*>(QStringLiteral("preferencesFlatAlphaAsLayerMaskCheck"));
    CHECK(check != nullptr);
    if (check != nullptr) {
      CHECK(!check->isChecked());
      CHECK(check->text() == QStringLiteral("Open image transparency as a layer mask"));
      check->setChecked(true);
      if (auto* group = check->parentWidget(); group != nullptr) {
        const auto area = check->geometry().adjusted(-8, -70, 8, 70).intersected(group->rect());
        group->grab(area).save(QStringLiteral("test-artifacts/flat-alpha-preference.png"));
      }
    }
    saw_dialog = true;
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
  CHECK(patchy::ui::app_settings().value(kFlatAlphaMaskSetting, false).toBool());
}

// Window captures of a transparent PNG opened both ways (test-artifacts/flat-alpha-*.png).
void ui_flat_alpha_open_captures() {
  ensure_artifact_dir();
  SettingsValueRestorer restore(kFlatAlphaMaskSetting);
  QImage image(320, 220, QImage::Format_RGBA8888);
  image.fill(QColor(0, 0, 0, 0));
  {
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(230, 120, 40));
    painter.drawEllipse(QRectF(40, 30, 160, 160));
    painter.setBrush(QColor(40, 140, 220, 160));
    painter.drawRoundedRect(QRectF(150, 70, 140, 110), 18, 18);
  }
  const QString path = QStringLiteral("test-artifacts/flat-alpha-sticker.png");
  QFile::remove(path);
  CHECK(image.save(path, "PNG"));
  for (const bool as_mask : {true, false}) {
    auto settings = patchy::ui::app_settings();
    settings.setValue(kFlatAlphaMaskSetting, as_mask);
    settings.sync();
    patchy::ui::MainWindow window;
    window.resize(1000, 640);
    show_window(window);
    patchy::ui::MainWindowTestAccess::open_document_path(window, path);
    QApplication::processEvents();
    CHECK(std::as_const(patchy::ui::MainWindowTestAccess::document(window)).layers().front().mask().has_value() ==
          as_mask);
    CHECK(window.grab().save(as_mask ? QStringLiteral("test-artifacts/flat-alpha-before-mask.png")
                                     : QStringLiteral("test-artifacts/flat-alpha-after-plain.png")));
  }
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

// The Export dialog's Color row (issue 90): offered for JPEG, TIFF and PDF, remembered per
// format, never part of Save As.
void ui_export_dialog_offers_cmyk_for_print_formats() {
  auto settings = patchy::ui::app_settings();
  settings.remove(QStringLiteral("saveOptions"));
  settings.sync();
  const auto previous = patchy::working_cmyk_space();
  const auto restore = qScopeGuard([&previous] { patchy::set_working_cmyk_space(previous); });
  patchy::set_working_cmyk_space(patchy::CmykWorkingSpace::from_icc_profile(
      patchy::ui::bundled_cmyk_profile(), patchy::RenderingIntent::RelativeColorimetric, true));

  const auto run_export_dialog = [](const QString& extension, const std::function<void(QDialog&)>& drive) {
    bool seen = false;
    QTimer::singleShot(0, [&] {
      for (auto* widget : QApplication::topLevelWidgets()) {
        auto* dialog = qobject_cast<QDialog*>(widget);
        if (dialog != nullptr && dialog->isVisible()) {
          seen = true;
          drive(*dialog);
          return;
        }
      }
    });
    auto result = patchy::ui::prompt_image_save_options(nullptr, extension, patchy::ui::ImageSaveOptions{}, true,
                                                        QSize(40, 30));
    CHECK(seen);
    return result;
  };

  const auto jpeg = run_export_dialog(QStringLiteral("jpg"), [](QDialog& dialog) {
    auto* color = dialog.findChild<QComboBox*>(QStringLiteral("exportColorCombo"));
    CHECK(color != nullptr);
    CHECK(color->count() == 2);
    CHECK(color->currentData().toString() == QStringLiteral("rgb"));
    CHECK(color->itemText(1).contains(QStringLiteral("ISO Coated v2")));
    auto* black_ink_only = dialog.findChild<QCheckBox*>(QStringLiteral("exportBlackInkOnlyCheck"));
    CHECK(black_ink_only != nullptr);
    CHECK(!black_ink_only->isChecked() && !black_ink_only->isEnabled());
    color->setCurrentIndex(1);
    CHECK(black_ink_only->isEnabled());
    black_ink_only->setChecked(true);
    dialog.accept();
  });
  CHECK(jpeg.has_value() && jpeg->export_cmyk && jpeg->export_black_ink_only);
  CHECK(settings.value(QStringLiteral("saveOptions/exportColorJpeg")).toString() == QStringLiteral("cmyk"));
  CHECK(settings.value(QStringLiteral("saveOptions/exportBlackInkOnlyJpeg")).toBool());

  // Remembered for JPEG, not carried over to TIFF.
  const auto tiff = run_export_dialog(QStringLiteral("tif"), [](QDialog& dialog) {
    auto* color = dialog.findChild<QComboBox*>(QStringLiteral("exportColorCombo"));
    CHECK(color != nullptr);
    CHECK(color->currentData().toString() == QStringLiteral("rgb"));
    dialog.accept();
  });
  CHECK(tiff.has_value() && !tiff->export_cmyk && !tiff->export_black_ink_only);
  const auto jpeg_again = run_export_dialog(QStringLiteral("jpeg"), [](QDialog& dialog) {
    CHECK(dialog.findChild<QComboBox*>(QStringLiteral("exportColorCombo"))->currentData().toString() ==
          QStringLiteral("cmyk"));
    CHECK(dialog.findChild<QCheckBox*>(QStringLiteral("exportBlackInkOnlyCheck"))->isChecked());
    dialog.accept();
  });
  CHECK(jpeg_again.has_value() && jpeg_again->export_cmyk && jpeg_again->export_black_ink_only);

  // PNG has no CMYK form, and a Save As prompt never offers the row.
  run_export_dialog(QStringLiteral("png"), [](QDialog& dialog) {
    CHECK(dialog.findChild<QComboBox*>(QStringLiteral("exportColorCombo")) == nullptr);
    dialog.accept();
  });
  bool save_as_seen = false;
  QTimer::singleShot(0, [&save_as_seen] {
    auto* dialog = find_top_level_dialog(QStringLiteral("jpegSaveOptionsDialog"));
    CHECK(dialog != nullptr);
    CHECK(dialog->findChild<QComboBox*>(QStringLiteral("exportColorCombo")) == nullptr);
    save_as_seen = true;
    dialog->accept();
  });
  const auto save_as = patchy::ui::prompt_image_save_options(nullptr, QStringLiteral("jpg"), *jpeg_again);
  CHECK(save_as_seen);
  CHECK(save_as.has_value() && !save_as->export_cmyk && !save_as->export_black_ink_only);

  // Without a profile the CMYK entry is there but cannot be chosen.
  patchy::set_working_cmyk_space(patchy::CmykWorkingSpace::formula());
  const auto formula = run_export_dialog(QStringLiteral("jpg"), [](QDialog& dialog) {
    auto* color = dialog.findChild<QComboBox*>(QStringLiteral("exportColorCombo"));
    CHECK(color->currentData().toString() == QStringLiteral("rgb"));
    auto* model = qobject_cast<QStandardItemModel*>(color->model());
    CHECK(model != nullptr && !model->item(1)->isEnabled());
    dialog.accept();
  });
  CHECK(formula.has_value() && !formula->export_cmyk);

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

void ui_animated_gif_opens_frames_as_layers() {
  const auto path = QString::fromStdWString(
      patchy::test::committed_format_fixture_path("gif", "pillow-animated.gif").wstring());
  CHECK(QFileInfo::exists(path));

  // Expected per-frame delays come from Qt's own reader so the check tracks the fixture.
  std::vector<int> delays_ms;
  {
    QImageReader probe(path);
    CHECK(probe.imageCount() == 3);
    while (!probe.read().isNull()) {
      delays_ms.push_back(probe.nextImageDelay());
      if (delays_ms.size() >= 3) {
        break;
      }
    }
  }
  CHECK(delays_ms.size() == 3);

  SettingsValueRestorer notes_setting(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);

  // Import notes ride the status bar by default (no popup unless the preference is on).
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();

  const auto status = window.statusBar()->currentMessage();
  CHECK(status.contains(QStringLiteral("frame")));
  CHECK(status.contains(QStringLiteral("3")));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.width() == 32);
  CHECK(document.height() == 24);
  // Every frame becomes a visible layer, frame 1 on TOP, its delay stamped as a trailing
  // seconds token the animated export parses back.
  CHECK(document.layers().size() == 3);
  for (std::size_t frame = 0; frame < 3; ++frame) {
    const auto& layer = std::as_const(document).layers()[2 - frame];  // index 0 is the bottom
    CHECK(layer.visible());
    const auto name = QString::fromStdString(layer.name());
    CHECK(name.startsWith(QStringLiteral("Frame %1 ").arg(frame + 1)));
    const auto expected_cs =
        static_cast<std::uint16_t>(std::clamp<long long>(std::llround(delays_ms[frame] / 10.0), 0, 0xffff));
    CHECK(patchy::gif::parse_layer_name_delay_cs(layer.name()) == expected_cs);
  }
  // Frames carry per-frame local palettes, so no document palette is adopted, and GIFs
  // are untagged: 72 PPI.
  CHECK(!document.indexed_palette().has_value());
  CHECK(document.print_settings().horizontal_ppi == 72.0);
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
  CHECK(notice_text.contains(QStringLiteral("frames as layers")));
  // The status bar carries the note either way.
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("frames as layers")));
}

// A 16-bit source converts to 8-bit and every save then writes 8-bit, so that note pops
// up even with the popup preference off (GitHub issue 52). The compatibility report,
// which shares the preference, stays hidden: only the notes box is forced.
void ui_deep_psd_import_forces_notices_popup() {
  ensure_artifact_dir();
  const auto path = QFileInfo(QStringLiteral("test-artifacts/ui_deep_psd_16_bit.psd")).absoluteFilePath();
  {
    // The core suite's synthetic 16-bit flat file: a 5x1 RGB raw composite.
    patchy::psd::BigEndianWriter writer;
    patchy::psd::write_header(writer, patchy::psd::Header{false, 3, 1, 5, 16, 3});
    writer.write_u32(0);
    writer.write_u32(0);
    writer.write_u32(0);
    writer.write_u16(0);
    constexpr std::array<std::uint16_t, 5> samples{0, 128, 256, 32768, 65535};
    for (int channel = 0; channel < 3; ++channel) {
      for (const auto sample : samples) {
        writer.write_u16(sample);
      }
    }
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const auto bytes = writer.bytes();
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size()));
  }

  SettingsValueRestorer notes_setting(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);

  const auto compatibility_report_done = std::make_shared<bool>(false);
  accept_compatibility_report_when_present(compatibility_report_done);

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
        CHECK(box->icon() == QMessageBox::Warning);
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
  const bool saw_compatibility_report = *compatibility_report_done;
  *compatibility_report_done = true;

  CHECK(saw_notice);
  CHECK(notice_text.contains(QStringLiteral("16-bit")));
  CHECK(notice_text.contains(QStringLiteral("8-bit file")));
  CHECK(!saw_compatibility_report);
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("16-bit")));
  const auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.metadata().values.at("psd.depth") == "16");
}

// Opens `path` with the notes preference off and returns the forced Import Notes text
// ("" when no box appeared).
QString forced_import_notice_text(const QString& path) {
  SettingsValueRestorer notes_setting(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);
  const auto compatibility_report_done = std::make_shared<bool>(false);
  accept_compatibility_report_when_present(compatibility_report_done);

  QString notice_text;
  int poll_attempts = 0;
  QTimer poller;
  QObject::connect(&poller, &QTimer::timeout, [&notice_text, &poll_attempts, &poller] {
    if (++poll_attempts > 500) {
      poller.stop();
      return;
    }
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* box = qobject_cast<QMessageBox*>(widget);
      if (box != nullptr && box->objectName() == QStringLiteral("importNoticesMessageBox") && box->isVisible()) {
        notice_text = box->text();
        CHECK(box->icon() == QMessageBox::Warning);
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
  *compatibility_report_done = true;
  return notice_text;
}

// Unsupported color modes and CMYK ink adjustments force a notice even with
// optional import notes disabled. Unchanged supported CMYK content can retain
// its source mode, so the ink notice must explain the conditional RGB fallback.
void ui_color_mode_and_ink_adjustment_imports_force_notices_popup() {
  ensure_artifact_dir();
  const auto path = QFileInfo(QStringLiteral("test-artifacts/ui_indexed_mode.psd")).absoluteFilePath();
  {
    // A 2x1 Indexed file: a 768-byte color table and one plane of indices.
    patchy::psd::BigEndianWriter writer;
    patchy::psd::write_header(writer, patchy::psd::Header{false, 1, 1, 2, 8, 2});
    writer.write_u32(768);
    for (int entry = 0; entry < 768; ++entry) {
      writer.write_u8(static_cast<std::uint8_t>(entry % 256));
    }
    writer.write_u32(0);
    writer.write_u32(0);
    writer.write_u16(0);
    writer.write_u8(0);
    writer.write_u8(200);
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const auto bytes = writer.bytes();
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size()));
  }
  const auto indexed = forced_import_notice_text(path);
  CHECK(indexed.contains(QStringLiteral("Indexed")));
  CHECK(indexed.contains(QStringLiteral("RGB file")));

  // psd-tools' CMYK Levels file (testy/fetch_psd_tools_corpus.py), when it is there.
  const auto cmyk = patchy::test::source_root_path() / "local-test-fixtures" / "psd-tools" / "tests" /
                    "psd_files" / "adjustments" / "levels_cmyk.psd";
  if (!std::filesystem::exists(cmyk)) {
    std::cout << "  note: optional fixture missing: " << patchy::test::fixture_display_path(cmyk) << '\n';
    return;
  }
  const auto ink = forced_import_notice_text(patchy::ui::to_qstring(cmyk));
  CHECK(ink.contains(QStringLiteral("Unchanged supported layers")));
  CHECK(ink.contains(QStringLiteral("original color mode")));
  CHECK(ink.contains(QStringLiteral("Content edits or unsupported layers require RGB saving")));
  CHECK(!ink.contains(QStringLiteral("saves RGB files")));
}

void ui_animated_gif_export_round_trips() {
  std::filesystem::create_directories("test-artifacts");
  // Four layers bottom to top: a base, a hidden layer that must be skipped, a name-token
  // delay override, and the top layer (= frame 1). The top layer covers only part of the
  // canvas so its frame keeps transparency.
  patchy::Document document(24, 16, patchy::PixelFormat::rgba8());
  const auto add_layer = [&document](const std::string& name, QColor color, bool visible, int layer_width) {
    patchy::PixelBuffer pixels(24, 16, patchy::PixelFormat::rgba8());
    pixels.clear(0);
    for (std::int32_t y = 0; y < 16; ++y) {
      for (std::int32_t x = 0; x < layer_width; ++x) {
        auto* px = pixels.pixel(x, y);
        px[0] = static_cast<std::uint8_t>(color.red());
        px[1] = static_cast<std::uint8_t>(color.green());
        px[2] = static_cast<std::uint8_t>(color.blue());
        px[3] = 255;
      }
    }
    document.add_pixel_layer(name, std::move(pixels));
    document.layers().back().set_visible(visible);
  };
  add_layer("Base", QColor(10, 200, 50), true, 24);
  add_layer("Skipped", QColor(255, 0, 255), false, 24);
  add_layer("blink 0.25s", QColor(60, 60, 220), true, 24);
  add_layer("Top 0.1s", QColor(200, 30, 40), true, 12);

  patchy::ui::ImageSaveOptions options;
  options.gif_animate = true;
  options.gif_frame_delay_cs = 7;  // only "Base" lacks a name token
  const auto path = QStringLiteral("test-artifacts/ui_animated_gif_export.gif");
  patchy::ui::write_flat_image_file(document, path, QStringLiteral("gif"), options);

  QImageReader reader(path);
  CHECK(reader.imageCount() == 3);  // the hidden layer exported no frame
  // Top to bottom: frame 1 is the top layer, and Qt reports each frame's delay in ms.
  const std::array<int, 3> expected_delays_ms = {100, 250, 70};
  const std::array<QColor, 3> expected_colors = {QColor(200, 30, 40), QColor(60, 60, 220), QColor(10, 200, 50)};
  for (int frame = 0; frame < 3; ++frame) {
    const auto image = reader.read().convertToFormat(QImage::Format_RGBA8888);
    CHECK(!image.isNull());
    const auto actual = image.pixelColor(4, 8);
    CHECK(actual.alpha() == 255);
    CHECK(actual.red() == expected_colors[static_cast<std::size_t>(frame)].red());
    CHECK(actual.green() == expected_colors[static_cast<std::size_t>(frame)].green());
    CHECK(actual.blue() == expected_colors[static_cast<std::size_t>(frame)].blue());
    CHECK(reader.nextImageDelay() == expected_delays_ms[static_cast<std::size_t>(frame)]);
    if (frame == 0) {
      // The top layer covers only the left half; the rest of its frame is transparent.
      CHECK(image.pixelColor(20, 8).alpha() == 0);
    }
  }
}

void ui_animated_gif_open_save_round_trip() {
  // The headline round trip: open an animated GIF (frames become layers with stamped
  // delays), save it back as an animation, and the frame count and delays survive.
  const auto path = QString::fromStdWString(
      patchy::test::committed_format_fixture_path("gif", "pillow-animated.gif").wstring());
  CHECK(QFileInfo::exists(path));
  std::vector<int> source_delays_ms;
  {
    QImageReader probe(path);
    while (!probe.read().isNull()) {
      source_delays_ms.push_back(probe.nextImageDelay());
      if (source_delays_ms.size() >= 16) {
        break;
      }
    }
  }
  CHECK(source_delays_ms.size() == 3);

  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.layers().size() == 3);

  std::filesystem::create_directories("test-artifacts");
  patchy::ui::ImageSaveOptions options;
  options.gif_animate = true;
  const auto saved = QStringLiteral("test-artifacts/ui_animated_gif_round_trip.gif");
  patchy::ui::write_flat_image_file(document, saved, QStringLiteral("gif"), options);

  QImageReader reader(saved);
  CHECK(reader.imageCount() == 3);
  for (std::size_t frame = 0; frame < 3; ++frame) {
    CHECK(!reader.read().isNull());
    // Delays round-trip through the layer-name tokens at centisecond precision.
    const auto expected_cs =
        std::clamp<long long>(std::llround(source_delays_ms[frame] / 10.0), 0, 0xffff);
    CHECK(reader.nextImageDelay() == static_cast<int>(expected_cs) * 10);
  }
}

void ui_gif_save_options_dialog_choices() {
  auto settings = patchy::ui::app_settings();
  settings.remove(QStringLiteral("saveOptions"));
  settings.sync();

  // GIF stays outside the generic per-extension prompt gate: only the dedicated call
  // sites raise its dialog.
  CHECK(!patchy::ui::image_save_options_apply_to_extension(QStringLiteral("gif")));
  const auto defaults = patchy::ui::load_image_save_option_defaults();
  CHECK(defaults.gif_frame_delay_cs == 10);
  CHECK(!defaults.gif_animate);

  // Save As form: animation is the default, flatten + a new delay persist for next time.
  bool saw_dialog = false;
  QTimer::singleShot(0, [&saw_dialog] {
    auto* dialog = find_top_level_dialog(QStringLiteral("gifSaveOptionsDialog"));
    CHECK(dialog != nullptr);
    auto* animation = dialog->findChild<QRadioButton*>(QStringLiteral("gifAnimationRadio"));
    auto* flatten = dialog->findChild<QRadioButton*>(QStringLiteral("gifFlattenRadio"));
    auto* delay = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("gifFrameDelaySpin"));
    auto* explanation = dialog->findChild<QLabel*>(QStringLiteral("gifAnimationExplanationLabel"));
    CHECK(animation != nullptr);
    CHECK(flatten != nullptr);
    CHECK(delay != nullptr);
    CHECK(explanation != nullptr);
    CHECK(animation->isChecked());
    CHECK(delay->value() == 0.10);
    CHECK(delay->isEnabled());
    delay->setValue(0.25);
    flatten->click();
    CHECK(!delay->isEnabled());  // the delay only applies to animations
    saw_dialog = true;
    dialog->accept();
  });
  auto options = patchy::ui::prompt_gif_save_options(nullptr, defaults, /*offer_flatten_choice*/ true,
                                                     /*for_export*/ false, /*has_visible_frames*/ true);
  CHECK(saw_dialog);
  CHECK(options.has_value());
  CHECK(!options->gif_animate);
  CHECK(options->gif_frame_delay_cs == 25);

  // Second invocation: the flatten choice and the delay were remembered.
  saw_dialog = false;
  QTimer::singleShot(0, [&saw_dialog] {
    auto* dialog = find_top_level_dialog(QStringLiteral("gifSaveOptionsDialog"));
    CHECK(dialog != nullptr);
    auto* animation = dialog->findChild<QRadioButton*>(QStringLiteral("gifAnimationRadio"));
    auto* delay = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("gifFrameDelaySpin"));
    CHECK(animation != nullptr);
    CHECK(!animation->isChecked());
    CHECK(delay->value() == 0.25);
    animation->click();
    CHECK(delay->isEnabled());
    saw_dialog = true;
    dialog->accept();
  });
  options = patchy::ui::prompt_gif_save_options(nullptr, patchy::ui::load_image_save_option_defaults(),
                                                /*offer_flatten_choice*/ true, /*for_export*/ false,
                                                /*has_visible_frames*/ true);
  CHECK(saw_dialog);
  CHECK(options.has_value());
  CHECK(options->gif_animate);

  // With nothing visible the animation radio disables and flatten is forced.
  saw_dialog = false;
  QTimer::singleShot(0, [&saw_dialog] {
    auto* dialog = find_top_level_dialog(QStringLiteral("gifSaveOptionsDialog"));
    CHECK(dialog != nullptr);
    auto* animation = dialog->findChild<QRadioButton*>(QStringLiteral("gifAnimationRadio"));
    auto* flatten = dialog->findChild<QRadioButton*>(QStringLiteral("gifFlattenRadio"));
    CHECK(animation != nullptr);
    CHECK(!animation->isEnabled());
    CHECK(flatten->isChecked());
    saw_dialog = true;
    dialog->accept();
  });
  options = patchy::ui::prompt_gif_save_options(nullptr, patchy::ui::load_image_save_option_defaults(),
                                                /*offer_flatten_choice*/ true, /*for_export*/ false,
                                                /*has_visible_frames*/ false);
  CHECK(saw_dialog);
  CHECK(options.has_value());
  CHECK(!options->gif_animate);

  // The Export Layers as Animated GIF form: no radios, always an animation, and the
  // export flow adds the scale combo.
  saw_dialog = false;
  QTimer::singleShot(0, [&saw_dialog] {
    auto* dialog = find_top_level_dialog(QStringLiteral("gifSaveOptionsDialog"));
    CHECK(dialog != nullptr);
    CHECK(dialog->findChild<QRadioButton*>(QStringLiteral("gifAnimationRadio")) == nullptr);
    CHECK(dialog->findChild<QRadioButton*>(QStringLiteral("gifFlattenRadio")) == nullptr);
    auto* delay = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("gifFrameDelaySpin"));
    CHECK(delay != nullptr);
    CHECK(delay->isEnabled());
    auto* scale = dialog->findChild<QComboBox*>(QStringLiteral("exportScaleCombo"));
    CHECK(scale != nullptr);
    scale->setCurrentIndex(std::max(0, scale->findData(2)));
    saw_dialog = true;
    dialog->accept();
  });
  options = patchy::ui::prompt_gif_save_options(nullptr, patchy::ui::load_image_save_option_defaults(),
                                                /*offer_flatten_choice*/ false, /*for_export*/ true,
                                                /*has_visible_frames*/ true);
  CHECK(saw_dialog);
  CHECK(options.has_value());
  CHECK(options->gif_animate);  // the animation-only form never flattens
  CHECK(options->export_scale == 2);
  // The animation-only form must not flip the remembered Save As mode (still animation
  // from the second invocation).
  CHECK(settings.value(QStringLiteral("saveOptions/gifSaveMode")).toString() == QStringLiteral("animation"));

  settings.remove(QStringLiteral("saveOptions"));
  settings.sync();
}

// A 12-bit JPEG (libjpeg-turbo's testorig12.jpg) opens as a 16-bit RGB document through the
// vendored libjpeg-turbo once Qt's 8-bit plug-in gives up (#438), from a Unicode folder and
// name as every file-opening path requires. Values: imagecodecs' 12-bit decode, scaled.
void ui_twelve_bit_jpeg_opens_as_16_bit_document() {
  patchy::set_deep_editing_override(true);
  const auto dir = std::filesystem::absolute(patchy::test::unicode_artifact_dir(u8"jpeg-12-bit"));
  const auto path = dir / patchy::test::unicode_path_piece(std::u8string(patchy::test::kUnicodeCombinedStem) + u8".jpg");
  std::filesystem::copy_file(patchy::test::committed_format_fixture_path("jpeg", "testorig12.jpg"), path);
  patchy::ui::MainWindow window;
  show_window(window);
  const auto sessions_before = patchy::ui::MainWindowTestAccess::session_count(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, patchy::ui::to_qstring(path));
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::session_count(window) == sessions_before + 1);
  const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  CHECK(document.width() == 227 && document.height() == 149);
  CHECK(document.color_state().bit_depth == patchy::BitDepth::UInt16);
  CHECK(!document.layers().empty());
  if (!document.layers().empty()) {
    const auto& pixels = document.layers().front().pixels();
    CHECK(pixels.format().channels == 3 && pixels.format().bit_depth == patchy::BitDepth::UInt16);
    const auto at16 = [&](int x, int y, int c) {
      return static_cast<int>(std::lround(patchy::load_pixel(pixels.format(), pixels.pixel(x, y))[c] * 257.0F));
    };
    const auto scale12 = [](int v) { return (v * 65535 + 2047) / 4095; };
    CHECK(at16(100, 50, 0) == scale12(3551) && at16(100, 50, 1) == scale12(851) && at16(100, 50, 2) == scale12(682));
    CHECK(at16(50, 120, 0) == scale12(3535) && at16(50, 120, 1) == scale12(3274) && at16(50, 120, 2) == scale12(2956));
  }
  // The issue's after image: the same file under its own name.
  ensure_artifact_dir();
  const auto plain = QStringLiteral("test-artifacts/testorig12.jpg");
  QFile::remove(plain);
  CHECK(QFile::copy(patchy::ui::to_qstring(path), plain));
  patchy::ui::MainWindow shown;
  show_window_empty(shown);
  patchy::ui::MainWindowTestAccess::open_document_path(shown, plain);
  QApplication::processEvents();
  shown.grab().save(QStringLiteral("test-artifacts/issue_438_after.png"));
  patchy::set_deep_editing_override(std::nullopt);
}

void ui_jxr_opens_and_saves_as_a_read_write_format() {
  ensure_artifact_dir();
  const auto fixture = QString::fromStdWString(
      patchy::test::committed_format_fixture_path("jxr", "hdr-ramp.jxr").wstring());
  CHECK(QFileInfo::exists(fixture));

  // JPEG XR is Windows-only: everywhere else the filter table hides it and the reader
  // throws, which is the documented behavior rather than a failure.
  if (!patchy::jxr::is_available()) {
    patchy::test::skip("no in-box Windows codec");
  }
  // Alpha survives a .jxr export, unlike the formats that drop it.
  CHECK(patchy::ui::image_format_preserves_alpha("jxr"));
  CHECK(patchy::ui::image_format_preserves_alpha("wdp"));

  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, fixture);
  QApplication::processEvents();

  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.width() == 8);
  CHECK(document.height() == 2);
  CHECK(std::as_const(document).layers().front().name() == "Background");
  CHECK(patchy::ui::MainWindowTestAccess::active_session_path(window) == fixture);
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));

  // The contrast with HEIF: the registry handler HAS a writer, so Save writes the file in
  // place instead of routing to Save As with a .psd default.
  const QString saved = QStringLiteral("test-artifacts/jxr_round_trip.jxr");
  QFile::remove(saved);
  patchy::ui::ImageSaveOptions options;
  options.jxr_lossless = true;
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, saved, options));
  CHECK(QFileInfo::exists(saved));

  patchy::ui::MainWindow reopened;
  show_window(reopened);
  patchy::ui::MainWindowTestAccess::open_document_path(reopened, saved);
  QApplication::processEvents();
  auto& round_tripped = patchy::ui::MainWindowTestAccess::document(reopened);
  CHECK(round_tripped.width() == 8);
  CHECK(round_tripped.height() == 2);
  // Lossless, and the second file is already 8-bit, so the tone map must not run again:
  // the pixels come back exactly as they were written.
  const auto& first = std::as_const(document.layers().front()).pixels();
  const auto& second = std::as_const(round_tripped.layers().front()).pixels();
  CHECK(first.format().channels == second.format().channels);
  for (std::int32_t x = 0; x < 8; ++x) {
    CHECK(second.pixel(x, 0)[0] == first.pixel(x, 0)[0]);
    CHECK(second.pixel(x, 1)[0] == first.pixel(x, 1)[0]);
  }
}

void ui_jxr_save_options_persist_quality_and_lossless() {
  auto settings = patchy::ui::app_settings();
  settings.remove(QStringLiteral("saveOptions/jxrQuality"));
  settings.remove(QStringLiteral("saveOptions/jxrLossless"));
  settings.sync();

  // Defaults before anything is stored.
  auto defaults = patchy::ui::load_image_save_option_defaults();
  CHECK(defaults.jxr_quality == 90);
  CHECK(!defaults.jxr_lossless);

  // .jxr raises its own options dialog (quality plus lossless), unlike PNG or TGA.
  CHECK(patchy::ui::image_save_options_apply_to_extension(QStringLiteral("jxr")));
  CHECK(patchy::ui::image_save_options_apply_to_extension(QStringLiteral(".JXR")));
  CHECK(patchy::ui::image_save_options_apply_to_extension(QStringLiteral("wdp")));
  CHECK(!patchy::ui::image_save_options_apply_to_extension(QStringLiteral("png")));

  defaults.jxr_quality = 55;
  defaults.jxr_lossless = true;
  patchy::ui::save_image_save_option_defaults(defaults);
  const auto reloaded = patchy::ui::load_image_save_option_defaults();
  CHECK(reloaded.jxr_quality == 55);
  CHECK(reloaded.jxr_lossless);

  // Out-of-range stored values clamp instead of reaching the encoder.
  settings.setValue(QStringLiteral("saveOptions/jxrQuality"), 0);
  settings.sync();
  CHECK(patchy::ui::load_image_save_option_defaults().jxr_quality == 1);
  settings.setValue(QStringLiteral("saveOptions/jxrQuality"), 1000);
  settings.sync();
  CHECK(patchy::ui::load_image_save_option_defaults().jxr_quality == 100);

  settings.remove(QStringLiteral("saveOptions"));
  settings.sync();
}

void ui_rttex_opens_and_saves_as_a_read_write_format() {
  const ScopedFlatAlphaAsLayerMask mask_import(true);
  ensure_artifact_dir();
  const auto fixture = QString::fromStdWString(
      patchy::test::committed_format_fixture_path("rttex", "rgba-10x10-in-16x16.rttex").wstring());
  CHECK(QFileInfo::exists(fixture));

  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, fixture);
  QApplication::processEvents();

  // The 16x16 texture opens at its true 10x10 size, at the untagged 72 PPI, with its alpha
  // promoted to an editable document-alpha mask like every other flat format.
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.width() == 10);
  CHECK(document.height() == 10);
  CHECK(std::as_const(document).layers().size() == 1);
  const auto& layer = std::as_const(document).layers().front();
  CHECK(layer.name() == "Background");
  CHECK(layer.pixels().format() == patchy::PixelFormat::rgb8());
  CHECK(layer.mask().has_value());
  CHECK(patchy::layer_mask_is_document_alpha(layer));
  CHECK(layer.mask()->pixels.pixel(0, 0)[0] == 0);
  CHECK(layer.mask()->pixels.pixel(5, 5)[0] == 255);
  CHECK(document.print_settings().horizontal_ppi == 72.0);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_path(window) == fixture);
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));

  // The registry handler has a writer, so Save writes the file in place instead of routing
  // to Save As with a .psd default (the contrast with HEIF and camera raw).
  const QString saved = QStringLiteral("test-artifacts/rttex_round_trip.rttex");
  QFile::remove(saved);
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, saved, patchy::ui::ImageSaveOptions{}));
  CHECK(QFileInfo::exists(saved));

  patchy::ui::MainWindow reopened;
  show_window(reopened);
  patchy::ui::MainWindowTestAccess::open_document_path(reopened, saved);
  QApplication::processEvents();
  auto& round_tripped = patchy::ui::MainWindowTestAccess::document(reopened);
  CHECK(round_tripped.width() == 10);
  CHECK(round_tripped.height() == 10);
  const auto& second = std::as_const(round_tripped).layers().front();
  CHECK(second.mask().has_value());
  for (std::int32_t y = 0; y < 10; ++y) {
    for (std::int32_t x = 0; x < 10; ++x) {
      for (int channel = 0; channel < 3; ++channel) {
        CHECK(second.pixels().pixel(x, y)[channel] == layer.pixels().pixel(x, y)[channel]);
      }
      CHECK(second.mask()->pixels.pixel(x, y)[0] == layer.mask()->pixels.pixel(x, y)[0]);
    }
  }
}

void ui_rttex_jpeg_save_round_trips_through_qt_encoder() {
  ensure_artifact_dir();
  // Four flat quadrants: JPEG keeps flat areas close, so interiors compare with a tolerance
  // and nothing is byte-pinned (libjpeg's output is not a contract).
  patchy::Document document(32, 24, patchy::PixelFormat::rgb8());
  patchy::PixelBuffer pixels(32, 24, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < 24; ++y) {
    for (std::int32_t x = 0; x < 32; ++x) {
      auto* px = pixels.pixel(x, y);
      const bool right = x >= 16;
      const bool bottom = y >= 12;
      px[0] = right ? 220 : 30;
      px[1] = bottom ? 200 : 40;
      px[2] = (right == bottom) ? 180 : 60;
      px[3] = 255;
    }
  }
  document.add_pixel_layer("Background", std::move(pixels));

  patchy::ui::ImageSaveOptions options;
  options.rttex_encoding = patchy::rttex::Encoding::Jpeg;
  options.rttex_jpeg_quality = 90;
  const QString path = QStringLiteral("test-artifacts/rttex_jpeg.rttex");
  QFile::remove(path);
  std::vector<std::string> notices;
  patchy::ui::write_flat_image_file(document, path, QStringLiteral("rttex"), options, &notices);
  CHECK(notices.empty());

  const auto read_bytes = [](const QString& file_path) {
    QFile file(file_path);
    CHECK(file.open(QIODevice::ReadOnly));
    const auto data = file.readAll();
    return std::vector<std::uint8_t>(data.begin(), data.end());
  };
  const auto decoded = patchy::rttex::read_rttex(read_bytes(path));
  CHECK(decoded.document.width() == 32);
  CHECK(decoded.document.height() == 24);
  CHECK(decoded.document.metadata().values.at(patchy::rttex::kMetadataEncoding) == "jpeg");
  const auto& reloaded = std::as_const(decoded.document.layers().front()).pixels();
  CHECK(reloaded.format().channels == 3);
  const auto& original = std::as_const(document.layers().front()).pixels();
  for (const auto& [x, y] : {std::pair{4, 4}, std::pair{28, 4}, std::pair{4, 20}, std::pair{28, 20}}) {
    const auto* expected = original.pixel(x, y);
    const auto* actual = reloaded.pixel(x, y);
    for (int channel = 0; channel < 3; ++channel) {
      CHECK(std::abs(actual[channel] - expected[channel]) <= 12);
    }
  }

  // RTPack's rule: transparency means no JPEG. The writer falls back to lossless RGBA and
  // reports it through the notices that become the status message suffix.
  patchy::Document translucent(8, 8, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer translucent_pixels(8, 8, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < 8; ++y) {
    for (std::int32_t x = 0; x < 8; ++x) {
      auto* px = translucent_pixels.pixel(x, y);
      px[0] = 200;
      px[1] = 100;
      px[2] = 50;
      px[3] = x == 0 ? 0 : 255;
    }
  }
  translucent.add_pixel_layer("Background", std::move(translucent_pixels));
  const QString fallback_path = QStringLiteral("test-artifacts/rttex_jpeg_fallback.rttex");
  QFile::remove(fallback_path);
  notices.clear();
  patchy::ui::write_flat_image_file(translucent, fallback_path, QStringLiteral("rttex"), options, &notices);
  CHECK(notices.size() == 1);
  CHECK(notices.front().find("JPEG") != std::string::npos);
  const auto fallback = patchy::rttex::read_rttex(read_bytes(fallback_path));
  CHECK(fallback.document.metadata().values.at(patchy::rttex::kMetadataEncoding) == "rgba8");
  const auto& fallback_pixels = std::as_const(fallback.document.layers().front()).pixels();
  CHECK(fallback_pixels.format().channels == 4);
  CHECK(fallback_pixels.pixel(0, 3)[3] == 0);
  CHECK(fallback_pixels.pixel(3, 3)[3] == 255);
  CHECK(fallback_pixels.pixel(3, 3)[0] == 200);
}

void ui_rttex_save_options_persist_and_dialog_prefills_from_source() {
  auto settings = patchy::ui::app_settings();
  settings.remove(QStringLiteral("saveOptions"));
  settings.sync();

  auto defaults = patchy::ui::load_image_save_option_defaults();
  CHECK(defaults.rttex_encoding == patchy::rttex::Encoding::Rgba8);
  CHECK(defaults.rttex_jpeg_quality == 90);
  CHECK(defaults.rttex_power_of_two == patchy::rttex::PowerOfTwo::Pad);
  CHECK(!defaults.rttex_force_square);
  CHECK(!defaults.rttex_force_alpha);
  CHECK(defaults.rttex_compress);

  // .rttex raises its own options dialog, unlike PNG or TGA.
  CHECK(patchy::ui::image_save_options_apply_to_extension(QStringLiteral("rttex")));
  CHECK(patchy::ui::image_save_options_apply_to_extension(QStringLiteral(".RTTEX")));

  defaults.rttex_encoding = patchy::rttex::Encoding::Rgba4444;
  defaults.rttex_jpeg_quality = 42;
  defaults.rttex_power_of_two = patchy::rttex::PowerOfTwo::Stretch;
  defaults.rttex_force_square = true;
  defaults.rttex_force_alpha = true;
  defaults.rttex_compress = false;
  patchy::ui::save_image_save_option_defaults(defaults);
  const auto reloaded = patchy::ui::load_image_save_option_defaults();
  CHECK(reloaded.rttex_encoding == patchy::rttex::Encoding::Rgba4444);
  CHECK(reloaded.rttex_jpeg_quality == 42);
  CHECK(reloaded.rttex_power_of_two == patchy::rttex::PowerOfTwo::Stretch);
  CHECK(reloaded.rttex_force_square);
  CHECK(reloaded.rttex_force_alpha);
  CHECK(!reloaded.rttex_compress);

  // Out-of-range or unknown stored values clamp or fall back instead of reaching the writer.
  settings.setValue(QStringLiteral("saveOptions/rttexJpegQuality"), 0);
  settings.setValue(QStringLiteral("saveOptions/rttexEncoding"), QStringLiteral("bogus"));
  settings.setValue(QStringLiteral("saveOptions/rttexPowerOfTwo"), QStringLiteral("bogus"));
  settings.sync();
  CHECK(patchy::ui::load_image_save_option_defaults().rttex_jpeg_quality == 1);
  CHECK(patchy::ui::load_image_save_option_defaults().rttex_encoding == patchy::rttex::Encoding::Rgba8);
  CHECK(patchy::ui::load_image_save_option_defaults().rttex_power_of_two == patchy::rttex::PowerOfTwo::Pad);
  settings.setValue(QStringLiteral("saveOptions/rttexJpegQuality"), 1000);
  settings.sync();
  CHECK(patchy::ui::load_image_save_option_defaults().rttex_jpeg_quality == 100);

  // The dialog: its controls carry the documented object names, and the JPEG quality row
  // follows the encoding combo.
  bool saw_dialog = false;
  QTimer::singleShot(0, [&saw_dialog] {
    auto* dialog = find_top_level_dialog(QStringLiteral("rttexSaveOptionsDialog"));
    CHECK(dialog != nullptr);
    auto* encoding = dialog->findChild<QComboBox*>(QStringLiteral("rttexEncodingCombo"));
    auto* quality = dialog->findChild<QSpinBox*>(QStringLiteral("rttexJpegQualitySpin"));
    auto* power_of_two = dialog->findChild<QComboBox*>(QStringLiteral("rttexPowerOfTwoCombo"));
    auto* force_square = dialog->findChild<QCheckBox*>(QStringLiteral("rttexForceSquareCheck"));
    auto* compress = dialog->findChild<QCheckBox*>(QStringLiteral("rttexCompressCheck"));
    CHECK(encoding != nullptr);
    CHECK(quality != nullptr);
    CHECK(power_of_two != nullptr);
    CHECK(force_square != nullptr);
    CHECK(compress != nullptr);
    CHECK(!quality->isEnabled());  // lossless selected: the JPEG quality is dead
    encoding->setCurrentIndex(encoding->findData(QStringLiteral("jpeg")));
    CHECK(quality->isEnabled());
    quality->setValue(65);
    power_of_two->setCurrentIndex(power_of_two->findData(QStringLiteral("none")));
    force_square->setChecked(true);
    compress->setChecked(false);
    saw_dialog = true;
    dialog->accept();
  });
  const auto chosen = patchy::ui::prompt_image_save_options(nullptr, QStringLiteral("rttex"),
                                                            patchy::ui::load_image_save_option_defaults());
  CHECK(saw_dialog);
  CHECK(chosen.has_value());
  CHECK(chosen->rttex_encoding == patchy::rttex::Encoding::Jpeg);
  CHECK(chosen->rttex_jpeg_quality == 65);
  CHECK(chosen->rttex_power_of_two == patchy::rttex::PowerOfTwo::None);
  CHECK(chosen->rttex_force_square);
  CHECK(!chosen->rttex_compress);

  // A document opened from a 16-bit texture prefills its own encoding so a plain Save keeps
  // it, while the other options stay at the persisted defaults.
  settings.remove(QStringLiteral("saveOptions"));
  settings.sync();
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(
      window, QString::fromStdWString(
                  patchy::test::committed_format_fixture_path("rttex", "rgba4444-61x80-in-64x128.rttex").wstring()));
  QApplication::processEvents();
  const auto prefilled = patchy::ui::MainWindowTestAccess::image_save_defaults(window);
  CHECK(prefilled.rttex_encoding == patchy::rttex::Encoding::Rgba4444);
  CHECK(prefilled.rttex_power_of_two == patchy::rttex::PowerOfTwo::Pad);
  CHECK(prefilled.rttex_compress);
  CHECK(!prefilled.rttex_force_alpha);

  settings.remove(QStringLiteral("saveOptions"));
  settings.sync();
}


[[nodiscard]] std::uint32_t dds_u32_at(const QByteArray& bytes, int offset) {
  const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.constData()) + offset;
  return static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8U) |
         (static_cast<std::uint32_t>(data[2]) << 16U) | (static_cast<std::uint32_t>(data[3]) << 24U);
}

void ui_dds_opens_and_saves_as_a_read_write_format() {
  const ScopedFlatAlphaAsLayerMask mask_import(true);
  ensure_artifact_dir();
  const auto fixture =
      QString::fromStdWString(patchy::test::committed_format_fixture_path("dds", "pillow-a8r8g8b8-9x7.dds").wstring());
  CHECK(QFileInfo::exists(fixture));

  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, fixture);
  QApplication::processEvents();

  // The translucent texture opens at the untagged 72 PPI with its alpha promoted to an
  // editable document-alpha mask, exactly like every other flat format with transparency.
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.width() == 9);
  CHECK(document.height() == 7);
  CHECK(std::as_const(document).layers().size() == 1);
  const auto& layer = std::as_const(document).layers().front();
  CHECK(layer.name() == "Background");
  CHECK(layer.pixels().format() == patchy::PixelFormat::rgb8());
  CHECK(layer.mask().has_value());
  CHECK(patchy::layer_mask_is_document_alpha(layer));
  CHECK(layer.mask()->pixels.pixel(0, 0)[0] == 0);
  CHECK(layer.mask()->pixels.pixel(4, 3)[0] == 127);
  CHECK(layer.mask()->pixels.pixel(8, 0)[0] == 255);
  CHECK(layer.pixels().pixel(0, 0)[0] == 250);  // the colour under zero alpha survives the import
  CHECK(document.print_settings().horizontal_ppi == 72.0);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_path(window) == fixture);
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
  // The source was uncompressed with no mip chain: the user's choices stay Automatic and
  // resolve against that source, so a plain Save keeps it uncompressed and lossless.
  const auto defaults = patchy::ui::MainWindowTestAccess::image_save_defaults(window);
  CHECK(defaults.dds_compression == patchy::dds::Compression::Automatic);
  CHECK(defaults.dds_mipmaps == patchy::dds::MipmapChoice::Automatic);
  CHECK(defaults.dds_source.compression == patchy::dds::Compression::Uncompressed);
  CHECK(defaults.dds_source.mipmaps == false);

  // The registry handler has a writer, so Save writes the file in place instead of routing to
  // Save As; the reopened file keeps every colour and the whole alpha mask.
  const QString saved = QStringLiteral("test-artifacts/dds_round_trip.dds");
  QFile::remove(saved);
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, saved, defaults));
  CHECK(QFileInfo::exists(saved));

  patchy::ui::MainWindow reopened;
  show_window(reopened);
  patchy::ui::MainWindowTestAccess::open_document_path(reopened, saved);
  QApplication::processEvents();
  auto& round_tripped = patchy::ui::MainWindowTestAccess::document(reopened);
  CHECK(round_tripped.width() == 9);
  CHECK(round_tripped.height() == 7);
  const auto& second = std::as_const(round_tripped).layers().front();
  CHECK(second.mask().has_value());
  for (std::int32_t y = 0; y < 7; ++y) {
    for (std::int32_t x = 0; x < 9; ++x) {
      CHECK(second.mask()->pixels.pixel(x, y)[0] == layer.mask()->pixels.pixel(x, y)[0]);
      if (layer.mask()->pixels.pixel(x, y)[0] == 0) {
        continue;  // colours under zero alpha do not survive a flatten
      }
      for (int channel = 0; channel < 3; ++channel) {
        CHECK(second.pixels().pixel(x, y)[channel] == layer.pixels().pixel(x, y)[channel]);
      }
    }
  }
}

void ui_dds_bc3_source_resaves_as_bc3_with_alpha_intact() {
  const ScopedFlatAlphaAsLayerMask mask_import(true);
  ensure_artifact_dir();
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(
      window, QString::fromStdWString(
                  patchy::test::committed_format_fixture_path("dds", "pillow-dxt5-16x16.dds").wstring()));
  QApplication::processEvents();
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto& layer = std::as_const(document).layers().front();
  CHECK(layer.mask().has_value());
  CHECK(patchy::layer_mask_is_document_alpha(layer));

  // A BC3 source without mipmaps is the shape Automatic resolves against, so Save keeps
  // the compression and its 8-bit alpha and writes no chain.
  const auto prefilled = patchy::ui::MainWindowTestAccess::image_save_defaults(window);
  CHECK(prefilled.dds_compression == patchy::dds::Compression::Automatic);
  CHECK(prefilled.dds_source.compression == patchy::dds::Compression::Bc3);
  CHECK(prefilled.dds_source.mipmaps == false);
  const QString saved = QStringLiteral("test-artifacts/dds_bc3_resave.dds");
  QFile::remove(saved);
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, saved, prefilled));
  QFile saved_file(saved);
  CHECK(saved_file.open(QIODevice::ReadOnly));
  const QByteArray bytes = saved_file.readAll();
  CHECK(bytes.size() > 128);
  CHECK(std::memcmp(bytes.constData() + 84, "DXT5", 4) == 0);
  CHECK(dds_u32_at(bytes, 28) == 0);  // dwMipMapCount: no chain, like the opened file

  const auto decoded = patchy::dds::read_dds(
      std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.constData()), bytes.size()));
  CHECK(decoded.document.layers().front().pixels().format() == patchy::PixelFormat::rgba8());
  int alpha_max_error = 0;
  for (std::int32_t y = 0; y < 16; ++y) {
    for (std::int32_t x = 0; x < 16; ++x) {
      const int expected = layer.mask()->pixels.pixel(x, y)[0];
      const int actual = decoded.document.layers().front().pixels().pixel(x, y)[3];
      alpha_max_error = std::max(alpha_max_error, std::abs(actual - expected));
    }
  }
  // Pillow encoded the fixture's mixed blocks (a transparent bottom row under high alpha) in
  // BC3's 6-level alpha mode with explicit 0 and 255; stb_dxt re-encodes them in the 8-level
  // mode spread over the whole range, so those blocks move by up to 18 (docs/dds.md).
  CHECK(alpha_max_error <= 20);
}

void ui_dds_save_options_persist_and_dialog_prefills_from_source() {
  auto settings = patchy::ui::app_settings();
  settings.remove(QStringLiteral("saveOptions"));
  settings.sync();

  auto defaults = patchy::ui::load_image_save_option_defaults();
  CHECK(defaults.dds_compression == patchy::dds::Compression::Automatic);
  CHECK(defaults.dds_mipmaps == patchy::dds::MipmapChoice::Automatic);
  CHECK(!defaults.dds_source.compression.has_value());
  CHECK(patchy::ui::image_save_options_apply_to_extension(QStringLiteral("dds")));
  CHECK(patchy::ui::image_save_options_apply_to_extension(QStringLiteral(".DDS")));

  // Persisted under the permanent saveOptions/dds* keys; a bogus token falls back.
  defaults.dds_compression = patchy::dds::Compression::Bc1;
  defaults.dds_mipmaps = patchy::dds::MipmapChoice::Generate;
  patchy::ui::save_image_save_option_defaults(defaults);
  CHECK(settings.value(QStringLiteral("saveOptions/ddsCompression")).toString() == QStringLiteral("bc1"));
  CHECK(settings.value(QStringLiteral("saveOptions/ddsMipmapMode")).toString() == QStringLiteral("on"));
  const auto reloaded = patchy::ui::load_image_save_option_defaults();
  CHECK(reloaded.dds_compression == patchy::dds::Compression::Bc1);
  CHECK(reloaded.dds_mipmaps == patchy::dds::MipmapChoice::Generate);
  settings.setValue(QStringLiteral("saveOptions/ddsCompression"), QStringLiteral("bogus"));
  settings.setValue(QStringLiteral("saveOptions/ddsMipmapMode"), QStringLiteral("bogus"));
  settings.sync();
  CHECK(patchy::ui::load_image_save_option_defaults().dds_compression == patchy::dds::Compression::Automatic);
  CHECK(patchy::ui::load_image_save_option_defaults().dds_mipmaps == patchy::dds::MipmapChoice::Automatic);
  // The 1.07 checkbox key migrates: a user who had it on keeps generating, off is Automatic.
  settings.remove(QStringLiteral("saveOptions"));
  settings.setValue(QStringLiteral("saveOptions/ddsMipmaps"), true);
  settings.sync();
  CHECK(patchy::ui::load_image_save_option_defaults().dds_mipmaps == patchy::dds::MipmapChoice::Generate);
  settings.setValue(QStringLiteral("saveOptions/ddsMipmaps"), false);
  settings.sync();
  CHECK(patchy::ui::load_image_save_option_defaults().dds_mipmaps == patchy::dds::MipmapChoice::Automatic);
  settings.remove(QStringLiteral("saveOptions"));
  settings.sync();

  // The dialog carries the documented object names and hands back the chosen values.
  bool saw_dialog = false;
  QTimer::singleShot(0, [&saw_dialog] {
    auto* dialog = find_top_level_dialog(QStringLiteral("ddsSaveOptionsDialog"));
    CHECK(dialog != nullptr);
    auto* compression = dialog->findChild<QComboBox*>(QStringLiteral("ddsCompressionCombo"));
    auto* mipmaps = dialog->findChild<QComboBox*>(QStringLiteral("ddsMipmapsCombo"));
    auto* preview = dialog->findChild<QPushButton*>(QStringLiteral("ddsMipmapPreviewButton"));
    CHECK(compression != nullptr);
    CHECK(mipmaps != nullptr);
    CHECK(preview != nullptr);
    CHECK(!preview->isVisible());  // no document to preview from this call
    CHECK(compression->count() == 7);
    CHECK(compression->currentData().toString() == QStringLiteral("auto"));
    CHECK(compression->currentText().contains(QStringLiteral("BC1 when opaque")));
    CHECK(mipmaps->count() == 3);
    CHECK(mipmaps->currentData().toString() == QStringLiteral("auto"));
    CHECK(mipmaps->currentText().contains(QStringLiteral("generate mipmaps")));
    compression->setCurrentIndex(compression->findData(QStringLiteral("bc3")));
    mipmaps->setCurrentIndex(mipmaps->findData(QStringLiteral("on")));
    saw_dialog = true;
    dialog->accept();
  });
  const auto chosen = patchy::ui::prompt_image_save_options(nullptr, QStringLiteral("dds"),
                                                            patchy::ui::load_image_save_option_defaults());
  CHECK(saw_dialog);
  CHECK(chosen.has_value());
  CHECK(chosen->dds_compression == patchy::dds::Compression::Bc3);
  CHECK(chosen->dds_mipmaps == patchy::dds::MipmapChoice::Generate);

  // A mipmapped BC1 source leaves the persisted choices alone (both Automatic) and carries
  // its shape for them to resolve against; the dialog's Automatic items name that shape.
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(
      window, QString::fromStdWString(
                  patchy::test::committed_format_fixture_path("dds", "synth-dxt1-mipmapped-16x16.dds").wstring()));
  QApplication::processEvents();
  const auto prefilled = patchy::ui::MainWindowTestAccess::image_save_defaults(window);
  CHECK(prefilled.dds_compression == patchy::dds::Compression::Automatic);
  CHECK(prefilled.dds_mipmaps == patchy::dds::MipmapChoice::Automatic);
  CHECK(prefilled.dds_source.compression == patchy::dds::Compression::Bc1);
  CHECK(prefilled.dds_source.mipmaps == true);
  bool saw_prefilled_dialog = false;
  QTimer::singleShot(0, [&saw_prefilled_dialog] {
    auto* dialog = find_top_level_dialog(QStringLiteral("ddsSaveOptionsDialog"));
    CHECK(dialog != nullptr);
    auto* compression = dialog->findChild<QComboBox*>(QStringLiteral("ddsCompressionCombo"));
    auto* mipmaps = dialog->findChild<QComboBox*>(QStringLiteral("ddsMipmapsCombo"));
    CHECK(compression != nullptr);
    CHECK(mipmaps != nullptr);
    CHECK(compression->currentText().contains(QStringLiteral("BC1 / DXT1, as the opened file")));
    CHECK(mipmaps->currentText().contains(QStringLiteral("generate, as the opened file")));
    saw_prefilled_dialog = true;
    dialog->reject();
  });
  CHECK(!patchy::ui::prompt_image_save_options(nullptr, QStringLiteral("dds"), prefilled).has_value());
  CHECK(saw_prefilled_dialog);

  settings.remove(QStringLiteral("saveOptions"));
  settings.sync();
}

void ui_dds_automatic_follows_opened_file_and_previews_the_chain() {
  ensure_artifact_dir();
  auto settings = patchy::ui::app_settings();
  settings.remove(QStringLiteral("saveOptions"));
  settings.sync();

  // A document with no .dds behind it: Automatic writes BC1 for opaque pixels plus a chain.
  {
    patchy::Document fresh(20, 12, patchy::PixelFormat::rgb8());
    patchy::PixelBuffer pixels(20, 12, patchy::PixelFormat::rgb8());
    for (std::int32_t y = 0; y < 12; ++y) {
      for (std::int32_t x = 0; x < 20; ++x) {
        auto* pixel = pixels.pixel(x, y);
        pixel[0] = static_cast<std::uint8_t>(x * 12);
        pixel[1] = static_cast<std::uint8_t>(y * 20);
        pixel[2] = 90;
      }
    }
    fresh.add_pixel_layer("Background", std::move(pixels));
    const QString path = QStringLiteral("test-artifacts/dds_fresh_automatic.dds");
    QFile::remove(path);
    patchy::ui::write_flat_image_file(fresh, path, QStringLiteral("dds"), patchy::ui::ImageSaveOptions{});
    QFile file(path);
    CHECK(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    CHECK(std::memcmp(bytes.constData() + 84, "DXT1", 4) == 0);
    CHECK(dds_u32_at(bytes, 28) == patchy::dds::mip_count_for(20, 12));
  }

  // Opened from a mipmapped BC1 file: Automatic keeps BC1 and the chain, and the Save As
  // dialog's Preview Mipmaps button opens the chain encoded at those choices.
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(
      window, QString::fromStdWString(
                  patchy::test::committed_format_fixture_path("dds", "synth-dxt1-mipmapped-16x16.dds").wstring()));
  QApplication::processEvents();
  const auto defaults = patchy::ui::MainWindowTestAccess::image_save_defaults(window);
  const QString saved = QStringLiteral("test-artifacts/dds_mipmapped_automatic.dds");
  QFile::remove(saved);
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, saved, defaults));
  {
    QFile file(saved);
    CHECK(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    CHECK(std::memcmp(bytes.constData() + 84, "DXT1", 4) == 0);
    CHECK(dds_u32_at(bytes, 28) == 5);
  }
  // An explicit No mipmaps still wins over the opened file.
  auto no_chain = defaults;
  no_chain.dds_mipmaps = patchy::dds::MipmapChoice::None;
  no_chain.dds_compression = patchy::dds::Compression::Bc3;
  const QString flat = QStringLiteral("test-artifacts/dds_mipmapped_explicit_off.dds");
  QFile::remove(flat);
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, flat, no_chain));
  {
    QFile file(flat);
    CHECK(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    CHECK(std::memcmp(bytes.constData() + 84, "DXT5", 4) == 0);
    CHECK(dds_u32_at(bytes, 28) == 0);
  }

  bool saw_preview = false;
  bool saw_options = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("ddsSaveOptionsDialog"));
    CHECK(dialog != nullptr);
    auto* preview = dialog->findChild<QPushButton*>(QStringLiteral("ddsMipmapPreviewButton"));
    CHECK(preview != nullptr);
    CHECK(preview->isVisible());
    saw_options = true;
    QTimer::singleShot(0, [&] {
      auto* window_dialog = find_top_level_dialog(QStringLiteral("ddsMipmapPreviewDialog"));
      CHECK(window_dialog != nullptr);
      if (window_dialog == nullptr) {
        return;
      }
      // 16x16 is five levels, 16 down to 1, every one labelled and drawn.
      for (int level = 0; level < 5; ++level) {
        auto* image = window_dialog->findChild<QLabel*>(QStringLiteral("ddsMipmapPreviewLevel%1").arg(level));
        auto* caption = window_dialog->findChild<QLabel*>(QStringLiteral("ddsMipmapPreviewCaption%1").arg(level));
        CHECK(image != nullptr);
        CHECK(caption != nullptr);
        if (image != nullptr && caption != nullptr) {
          const int side = 16 >> level;
          CHECK(image->pixmap().width() == side);
          CHECK(caption->text().contains(QStringLiteral("Level %1: %2 x %2").arg(level).arg(side)));
        }
      }
      CHECK(window_dialog->findChild<QLabel*>(QStringLiteral("ddsMipmapPreviewLevel5")) == nullptr);
      auto* summary = window_dialog->findChild<QLabel*>(QStringLiteral("ddsMipmapPreviewSummary"));
      CHECK(summary != nullptr);
      if (summary != nullptr) {
        CHECK(summary->text().contains(QStringLiteral("BC1 / DXT1: 5 mip levels, 16 x 16 down to 1 x 1")));
        CHECK(summary->text().contains(QStringLiteral("Every level is written")));
      }
      // 400% draws level 0 at 64 px, nearest neighbour.
      auto* zoom = window_dialog->findChild<QComboBox*>(QStringLiteral("ddsMipmapPreviewZoomCombo"));
      CHECK(zoom != nullptr);
      if (zoom != nullptr) {
        zoom->setCurrentIndex(zoom->findData(4));
        auto* image = window_dialog->findChild<QLabel*>(QStringLiteral("ddsMipmapPreviewLevel0"));
        CHECK(image != nullptr && image->pixmap().width() == 64);
      }
      QApplication::processEvents();
      CHECK(window_dialog->grab().save(QStringLiteral("test-artifacts/dds_mipmap_preview_dialog.png")));
      CHECK(dialog->grab().save(QStringLiteral("test-artifacts/dds_save_options_dialog.png")));
      saw_preview = true;
      window_dialog->accept();
      QTimer::singleShot(0, [dialog] { dialog->reject(); });
    });
    preview->click();
  });
  const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  CHECK(!patchy::ui::prompt_image_save_options(nullptr, QStringLiteral("dds"), defaults, /*for_export*/ false,
                                               QSize(), &document)
             .has_value());
  CHECK(saw_options);
  CHECK(saw_preview);
  settings.remove(QStringLiteral("saveOptions"));
  settings.sync();
}

void ui_dds_cubemap_import_is_layered_and_save_routes_to_save_as() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto fixture = QString::fromStdWString(
      patchy::test::committed_format_fixture_path("dds", "synth-cubemap-a8r8g8b8-4x4.dds").wstring());
  patchy::ui::MainWindowTestAccess::open_document_path(window, fixture);
  QApplication::processEvents();
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(std::as_const(document).layers().size() == 6);
  CHECK(std::as_const(document).layers().front().name() == "+X");
  CHECK(std::as_const(document).layers().front().visible());
  CHECK(!std::as_const(document).layers()[1].visible());
  CHECK(patchy::ui::MainWindowTestAccess::active_session_path(window) == fixture);

  // Six layers cannot survive a flat .dds save, so Save routes to Save As with a .psd default
  // instead of silently flattening over the cubemap (the Photoshop-style guard).
  bool saw_dialog = false;
  QString default_name;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("saveAsFileDialog")));
    CHECK(dialog != nullptr);
    const auto selected = dialog->selectedFiles();
    if (!selected.isEmpty()) {
      default_name = QFileInfo(selected.first()).fileName();
    }
    saw_dialog = true;
    dialog->reject();
  });
  CHECK(!patchy::ui::MainWindowTestAccess::save_document(window));
  CHECK(saw_dialog);
  CHECK(default_name == QStringLiteral("synth-cubemap-a8r8g8b8-4x4.psd"));
}

void ui_export_trim_keeps_document_alpha_mask_colors() {
  ensure_artifact_dir();
  // An opaque RGB layer whose document-alpha mask reveals a 3x2 block with one hidden pixel
  // inside it: trim must follow the mask (the pixels themselves are opaque), keep the colors
  // under the hidden pixel, and resize the mask together with the pixels.
  patchy::Document document(6, 4, patchy::PixelFormat::rgb8());
  patchy::PixelBuffer pixels(6, 4, patchy::PixelFormat::rgb8());
  for (std::int32_t y = 0; y < 4; ++y) {
    for (std::int32_t x = 0; x < 6; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = 200;
      px[1] = 100;
      px[2] = 50;
    }
  }
  patchy::PixelBuffer mask(6, 4, patchy::PixelFormat::gray8());
  mask.clear(0);
  for (std::int32_t y = 1; y < 3; ++y) {
    for (std::int32_t x = 1; x < 4; ++x) {
      mask.pixel(x, y)[0] = 255;
    }
  }
  mask.pixel(1, 1)[0] = 0;
  document.add_pixel_layer("Photo", std::move(pixels));
  auto& layer = document.layers().back();
  layer.set_mask(patchy::LayerMask{patchy::Rect::from_size(6, 4), std::move(mask), 255, false});
  patchy::set_layer_mask_is_document_alpha(layer, true);

  patchy::ui::ImageSaveOptions options;
  options.export_trim_transparent = true;
  const auto trimmed_path = QStringLiteral("test-artifacts/ui_export_mask_trimmed.png");
  patchy::ui::write_flat_image_file(document, trimmed_path, QStringLiteral("png"), options);
  const auto trimmed = QImage(trimmed_path).convertToFormat(QImage::Format_RGBA8888);
  CHECK(trimmed.width() == 3);
  CHECK(trimmed.height() == 2);
  CHECK(trimmed.pixelColor(0, 0).alpha() == 0);
  CHECK(trimmed.pixelColor(0, 0).red() == 200);  // the colors survive under the mask
  CHECK(trimmed.pixelColor(1, 0) == QColor(200, 100, 50, 255));

  // Trim plus resize: the mask plane is resampled with the pixels.
  options.export_width = 12;
  options.export_height = 8;
  const auto resized_path = QStringLiteral("test-artifacts/ui_export_mask_resized.png");
  patchy::ui::write_flat_image_file(document, resized_path, QStringLiteral("png"), options);
  const auto resized = QImage(resized_path).convertToFormat(QImage::Format_RGBA8888);
  CHECK(resized.width() == 6);
  CHECK(resized.height() == 4);
  CHECK(resized.pixelColor(5, 3).alpha() == 255);
  CHECK(resized.pixelColor(0, 0).alpha() < 255);
  CHECK(resized.pixelColor(5, 3).red() == 200);

  // A background fill drops the mask structure: everything opaque, hidden pixels white.
  options.export_width = 0;
  options.export_height = 0;
  options.export_fill_transparent = true;
  options.export_background_color = QColor(Qt::white);
  const auto filled_path = QStringLiteral("test-artifacts/ui_export_mask_filled.png");
  patchy::ui::write_flat_image_file(document, filled_path, QStringLiteral("png"), options);
  const auto filled = QImage(filled_path).convertToFormat(QImage::Format_RGBA8888);
  CHECK(filled.width() == 3);
  CHECK(filled.height() == 2);
  CHECK(filled.pixelColor(0, 0) == QColor(255, 255, 255, 255));
  CHECK(filled.pixelColor(1, 0) == QColor(200, 100, 50, 255));
}

void check_animated_export_trims_frames_to_union_bounds(const QString& extension) {
  std::filesystem::create_directories("test-artifacts");
  patchy::Document document(16, 8, patchy::PixelFormat::rgba8());
  const auto add_sprite = [&document](const std::string& name, QColor color, QRect rect) {
    patchy::PixelBuffer pixels(16, 8, patchy::PixelFormat::rgba8());
    pixels.clear(0);
    for (int y = rect.top(); y <= rect.bottom(); ++y) {
      for (int x = rect.left(); x <= rect.right(); ++x) {
        auto* px = pixels.pixel(x, y);
        px[0] = static_cast<std::uint8_t>(color.red());
        px[1] = static_cast<std::uint8_t>(color.green());
        px[2] = static_cast<std::uint8_t>(color.blue());
        px[3] = 255;
      }
    }
    document.add_pixel_layer(name, std::move(pixels));
  };
  add_sprite("A", QColor(255, 0, 0), QRect(2, 1, 3, 2));
  add_sprite("B", QColor(0, 0, 255), QRect(10, 4, 4, 3));

  patchy::ui::ImageSaveOptions options;
  options.gif_animate = true;
  options.webp_animate = true;
  options.webp_lossless = true;
  options.export_trim_transparent = true;
  const auto path = QStringLiteral("test-artifacts/ui_animated_trimmed.") + extension;
  patchy::ui::write_flat_image_file(document, path, extension, options);
  QImageReader reader(path);
  CHECK(reader.imageCount() == 2);
  // The union of both sprites is x 2..13, y 1..6: every frame is 12x6.
  const auto first = reader.read().convertToFormat(QImage::Format_RGBA8888);  // top layer = B
  CHECK(first.size() == QSize(12, 6));
  CHECK(first.pixelColor(8, 3) == QColor(0, 0, 255, 255));
  const auto second = reader.read().convertToFormat(QImage::Format_RGBA8888);
  CHECK(second.size() == QSize(12, 6));
  CHECK(second.pixelColor(0, 0) == QColor(255, 0, 0, 255));

  // Fill plus scale apply per frame after the shared trim.
  options.export_fill_transparent = true;
  options.export_background_color = QColor(Qt::white);
  options.export_scale = 2;
  const auto filled_path = QStringLiteral("test-artifacts/ui_animated_trimmed_filled.") + extension;
  patchy::ui::write_flat_image_file(document, filled_path, extension, options);
  QImageReader filled_reader(filled_path);
  CHECK(filled_reader.imageCount() == 2);
  const auto filled = filled_reader.read().convertToFormat(QImage::Format_RGBA8888);
  CHECK(filled.size() == QSize(24, 12));
  CHECK(filled.pixelColor(0, 0) == QColor(255, 255, 255, 255));
  CHECK(filled.pixelColor(16, 6) == QColor(0, 0, 255, 255));
}

void ui_animated_gif_export_trims_frames_to_union_bounds() {
  check_animated_export_trims_frames_to_union_bounds(QStringLiteral("gif"));
}

void ui_webp_animation_export_trims_frames_to_union_bounds() {
  check_animated_export_trims_frames_to_union_bounds(QStringLiteral("webp"));
}

void ui_webp_lossless_round_trips_and_quality_orders_size() {
  ensure_artifact_dir();
  // Noisy RGBA content: lossless must come back exact, lossy must not, and lower quality
  // must be smaller. Alpha stays above 0 so the encoder never gets to discard hidden RGB.
  constexpr std::int32_t kSize = 32;
  patchy::PixelBuffer pixels(kSize, kSize, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < kSize; ++y) {
    for (std::int32_t x = 0; x < kSize; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>((x * 31 + y * 7) % 256);
      px[1] = static_cast<std::uint8_t>((x * 13 + y * 29) % 256);
      px[2] = static_cast<std::uint8_t>((x * 3 + y * 61) % 256);
      px[3] = static_cast<std::uint8_t>(64 + (x * 5 + y * 3) % 192);
    }
  }
  patchy::Document document(kSize, kSize, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Noise", pixels);

  const auto matches_source = [&pixels](const QString& path) {
    const auto image = QImage(path).convertToFormat(QImage::Format_RGBA8888);
    if (image.size() != QSize(kSize, kSize)) {
      return false;
    }
    for (std::int32_t y = 0; y < kSize; ++y) {
      for (std::int32_t x = 0; x < kSize; ++x) {
        const auto* px = pixels.pixel(x, y);
        if (image.pixelColor(x, y) != QColor(px[0], px[1], px[2], px[3])) {
          return false;
        }
      }
    }
    return true;
  };
  const auto write = [&document](const QString& path, int quality, bool lossless) {
    patchy::ui::ImageSaveOptions options;
    options.webp_quality = quality;
    options.webp_lossless = lossless;
    patchy::ui::write_flat_image_file(document, path, QStringLiteral("webp"), options);
    return QFileInfo(path).size();
  };
  const auto lossless_path = QStringLiteral("test-artifacts/ui_webp_lossless.webp");
  write(lossless_path, 10, true);
  CHECK(matches_source(lossless_path));
  const auto low_path = QStringLiteral("test-artifacts/ui_webp_q10.webp");
  const auto high_path = QStringLiteral("test-artifacts/ui_webp_q95.webp");
  const auto low_size = write(low_path, 10, false);
  const auto high_size = write(high_path, 95, false);
  CHECK(!matches_source(low_path));
  CHECK(low_size < high_size);
  // Quality 100 without the checkbox is the lossless mode of Qt's WebP plugin, which is
  // what the writer relies on for the Lossless option.
  const auto hundred_path = QStringLiteral("test-artifacts/ui_webp_q100.webp");
  write(hundred_path, 100, false);
  CHECK(matches_source(hundred_path));
}

struct ExportSectionProbe {
  QCheckBox* resize{nullptr};
  QSpinBox* width{nullptr};
  QSpinBox* height{nullptr};
  QDoubleSpinBox* percent{nullptr};
  QComboBox* scale{nullptr};
  QRadioButton* keep{nullptr};
  QRadioButton* fill{nullptr};
  QPushButton* swatch{nullptr};
  QCheckBox* trim{nullptr};
  QCheckBox* reveal{nullptr};

  [[nodiscard]] bool complete() const {
    return resize != nullptr && width != nullptr && height != nullptr && percent != nullptr && scale != nullptr &&
           keep != nullptr && fill != nullptr && swatch != nullptr && trim != nullptr && reveal != nullptr;
  }
};

ExportSectionProbe probe_export_section(QDialog& dialog) {
  ExportSectionProbe probe;
  probe.resize = dialog.findChild<QCheckBox*>(QStringLiteral("exportResizeCheck"));
  probe.width = dialog.findChild<QSpinBox*>(QStringLiteral("exportResizeWidthSpin"));
  probe.height = dialog.findChild<QSpinBox*>(QStringLiteral("exportResizeHeightSpin"));
  probe.percent = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("exportResizePercentSpin"));
  probe.scale = dialog.findChild<QComboBox*>(QStringLiteral("exportScaleCombo"));
  probe.keep = dialog.findChild<QRadioButton*>(QStringLiteral("exportKeepTransparencyRadio"));
  probe.fill = dialog.findChild<QRadioButton*>(QStringLiteral("exportFillTransparencyRadio"));
  probe.swatch = dialog.findChild<QPushButton*>(QStringLiteral("exportBackgroundColorSwatch"));
  probe.trim = dialog.findChild<QCheckBox*>(QStringLiteral("exportTrimCheck"));
  probe.reveal = dialog.findChild<QCheckBox*>(QStringLiteral("exportRevealCheck"));
  return probe;
}

// Prints the clipped labels so a platform font difference is diagnosable from the log.
bool no_clipped_labels(QDialog& dialog) {
  const auto clipped = clipped_labels(dialog);
  for (const auto& entry : clipped) {
    std::cerr << "clipped label in " << dialog.objectName().toStdString() << ": " << entry.toStdString() << "\n";
  }
  return clipped.isEmpty();
}

void ui_export_options_dialog_shared_section() {
  auto settings = patchy::ui::app_settings();
  settings.remove(QStringLiteral("saveOptions"));
  settings.sync();

  const patchy::ui::ImageSaveOptions defaults;
  bool saw_dialog = false;
  QTimer::singleShot(0, [&saw_dialog] {
    auto* dialog = find_top_level_dialog(QStringLiteral("exportScaleOptionsDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    const auto probe = probe_export_section(*dialog);
    CHECK(probe.complete());
    if (!probe.complete()) {
      dialog->reject();
      return;
    }
    // Defaults: nothing transforms, the pixel-art scale reads as off, and every label fits.
    CHECK(!probe.resize->isChecked());
    CHECK(!probe.width->isEnabled());
    CHECK(probe.width->value() == 640);
    CHECK(probe.height->value() == 480);
    CHECK(probe.percent->value() == 100.0);
    CHECK(probe.scale->currentData().toInt() == 1);
    CHECK(probe.scale->itemText(0).contains(QStringLiteral("off")));
    CHECK(probe.keep->isChecked());
    CHECK(!probe.swatch->isEnabled());
    CHECK(!probe.trim->isChecked());
    CHECK(!probe.reveal->isChecked());
    CHECK(no_clipped_labels(*dialog));
    ensure_artifact_dir();
    render_widget_image(*dialog).save(QStringLiteral("test-artifacts/export_options_dialog.png"));
    // Width, height, and percent mirror each other with the aspect locked.
    probe.resize->click();
    CHECK(probe.width->isEnabled() && probe.height->isEnabled() && probe.percent->isEnabled());
    probe.width->setValue(320);
    CHECK(probe.height->value() == 240);
    CHECK(probe.percent->value() == 50.0);
    probe.height->setValue(120);
    CHECK(probe.width->value() == 160);
    CHECK(probe.percent->value() == 25.0);
    probe.percent->setValue(200.0);
    CHECK(probe.width->value() == 1280);
    CHECK(probe.height->value() == 960);
    probe.scale->setCurrentIndex(std::max(0, probe.scale->findData(4)));
    probe.fill->click();
    CHECK(probe.swatch->isEnabled());
    // The swatch opens Patchy's own picker (never Qt's stock dialog): pick through it, then
    // check that a cancel keeps the color even after a live preview.
    QTimer::singleShot(0, [] {
      auto* picker_dialog = find_top_level_dialog(QStringLiteral("patchyColorDialog"));
      CHECK(picker_dialog != nullptr);
      if (picker_dialog == nullptr) {
        return;
      }
      CHECK(patchy::ui::apply_color_to_open_color_picker(QColor(10, 20, 30)));
      picker_dialog->accept();
    });
    probe.swatch->click();
    CHECK(probe.swatch->property("patchy.exportBackgroundColor").value<QColor>() == QColor(10, 20, 30));
    QTimer::singleShot(0, [] {
      auto* picker_dialog = find_top_level_dialog(QStringLiteral("patchyColorDialog"));
      CHECK(picker_dialog != nullptr);
      if (picker_dialog == nullptr) {
        return;
      }
      CHECK(patchy::ui::apply_color_to_open_color_picker(QColor(99, 99, 99)));
      picker_dialog->reject();
    });
    probe.swatch->click();
    CHECK(probe.swatch->property("patchy.exportBackgroundColor").value<QColor>() == QColor(10, 20, 30));
    probe.trim->click();
    probe.reveal->click();
    saw_dialog = true;
    dialog->accept();
  });
  auto chosen = patchy::ui::prompt_image_save_options(nullptr, QStringLiteral("png"), defaults, /*for_export*/ true,
                                                      QSize(640, 480));
  CHECK(saw_dialog);
  CHECK(chosen.has_value());
  if (chosen.has_value()) {
    CHECK(chosen->export_width == 1280);
    CHECK(chosen->export_height == 960);
    CHECK(chosen->export_scale == 4);
    CHECK(chosen->export_fill_transparent);
    CHECK(chosen->export_background_color == QColor(10, 20, 30));
    CHECK(chosen->export_trim_transparent);
    CHECK(chosen->export_reveal_in_file_explorer);
  }
  CHECK(settings.value(QStringLiteral("saveOptions/exportScale")).toInt() == 4);
  CHECK(settings.value(QStringLiteral("saveOptions/exportResize")).toBool());
  CHECK(settings.value(QStringLiteral("saveOptions/exportResizePercent")).toDouble() == 200.0);
  CHECK(settings.value(QStringLiteral("saveOptions/exportFillTransparent")).toBool());
  CHECK(settings.value(QStringLiteral("saveOptions/exportBackgroundColor")).toString() == QStringLiteral("#0a141e"));
  CHECK(settings.value(QStringLiteral("saveOptions/exportTrim")).toBool());
  CHECK(settings.value(QStringLiteral("saveOptions/exportRevealInFileExplorer")).toBool());

  // A second document prefills from the remembered choices; the percent scales its size.
  saw_dialog = false;
  QTimer::singleShot(0, [&saw_dialog] {
    auto* dialog = find_top_level_dialog(QStringLiteral("exportScaleOptionsDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    const auto probe = probe_export_section(*dialog);
    CHECK(probe.complete());
    if (!probe.complete()) {
      dialog->reject();
      return;
    }
    CHECK(probe.resize->isChecked());
    CHECK(probe.width->value() == 200);
    CHECK(probe.height->value() == 100);
    CHECK(probe.scale->currentData().toInt() == 4);
    CHECK(probe.fill->isChecked());
    CHECK(probe.swatch->isEnabled());
    CHECK(probe.trim->isChecked());
    CHECK(probe.reveal->isChecked());
    probe.keep->click();
    CHECK(!probe.swatch->isEnabled());
    saw_dialog = true;
    dialog->accept();
  });
  chosen = patchy::ui::prompt_image_save_options(nullptr, QStringLiteral("png"), defaults, /*for_export*/ true,
                                                 QSize(100, 50));
  CHECK(saw_dialog);
  CHECK(chosen.has_value());
  if (chosen.has_value()) {
    CHECK(chosen->export_width == 200);
    CHECK(chosen->export_height == 100);
    CHECK(!chosen->export_fill_transparent);
    CHECK(chosen->export_background_color == QColor(10, 20, 30));
  }
  CHECK(!settings.value(QStringLiteral("saveOptions/exportFillTransparent")).toBool());

  // Without a document size the Resize row hides and nothing resize-related changes.
  saw_dialog = false;
  QTimer::singleShot(0, [&saw_dialog] {
    auto* dialog = find_top_level_dialog(QStringLiteral("exportScaleOptionsDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* row = dialog->findChild<QWidget*>(QStringLiteral("exportResizeRow"));
    CHECK(row != nullptr && row->isHidden());
    saw_dialog = true;
    dialog->accept();
  });
  chosen = patchy::ui::prompt_image_save_options(nullptr, QStringLiteral("png"), defaults, /*for_export*/ true);
  CHECK(saw_dialog);
  CHECK(chosen.has_value());
  if (chosen.has_value()) {
    CHECK(chosen->export_width == 0);
    CHECK(chosen->export_height == 0);
  }
  CHECK(settings.value(QStringLiteral("saveOptions/exportResize")).toBool());  // untouched

  settings.remove(QStringLiteral("saveOptions"));
  settings.sync();
}

void ui_export_options_jpeg_forces_background_fill() {
  auto settings = patchy::ui::app_settings();
  settings.remove(QStringLiteral("saveOptions"));
  settings.sync();

  const patchy::ui::ImageSaveOptions defaults;
  bool saw_dialog = false;
  QTimer::singleShot(0, [&saw_dialog] {
    auto* dialog = find_top_level_dialog(QStringLiteral("jpegSaveOptionsDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    const auto probe = probe_export_section(*dialog);
    CHECK(probe.complete());
    if (!probe.complete()) {
      dialog->reject();
      return;
    }
    // JPEG cannot keep transparency: the radios hide, the fill is forced, and the swatch
    // reads as the background color.
    CHECK(probe.keep->isHidden());
    CHECK(probe.fill->isHidden());
    CHECK(probe.fill->isChecked());
    auto* background_label = dialog->findChild<QLabel*>(QStringLiteral("exportBackgroundLabel"));
    CHECK(background_label != nullptr && !background_label->isHidden());
    CHECK(probe.swatch->isEnabled());
    CHECK(dialog->findChild<QSpinBox*>(QStringLiteral("jpegQualitySpin")) != nullptr);
    CHECK(no_clipped_labels(*dialog));
    saw_dialog = true;
    dialog->accept();
  });
  auto chosen = patchy::ui::prompt_image_save_options(nullptr, QStringLiteral("jpg"), defaults, /*for_export*/ true,
                                                      QSize(64, 64));
  CHECK(saw_dialog);
  CHECK(chosen.has_value());
  if (chosen.has_value()) {
    CHECK(chosen->export_fill_transparent);
    CHECK(chosen->export_background_color == QColor(Qt::white));
  }
  // The forced fill is not a preference: the next PNG export must not inherit it.
  CHECK(!settings.contains(QStringLiteral("saveOptions/exportFillTransparent")));
  CHECK(settings.value(QStringLiteral("saveOptions/exportBackgroundColor")).toString() == QStringLiteral("#ffffff"));

  // Save As keeps the plain JPEG form.
  saw_dialog = false;
  QTimer::singleShot(0, [&saw_dialog] {
    auto* dialog = find_top_level_dialog(QStringLiteral("jpegSaveOptionsDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    CHECK(dialog->findChild<QComboBox*>(QStringLiteral("exportScaleCombo")) == nullptr);
    CHECK(dialog->findChild<QCheckBox*>(QStringLiteral("exportTrimCheck")) == nullptr);
    saw_dialog = true;
    dialog->accept();
  });
  chosen = patchy::ui::prompt_image_save_options(nullptr, QStringLiteral("jpg"), defaults);
  CHECK(saw_dialog);
  CHECK(chosen.has_value());
  if (chosen.has_value()) {
    CHECK(!chosen->export_fill_transparent);
    CHECK(chosen->export_scale == 1);
  }

  settings.remove(QStringLiteral("saveOptions"));
  settings.sync();
}

void ui_webp_save_options_dialog_persists_quality_and_lossless() {
  auto settings = patchy::ui::app_settings();
  settings.remove(QStringLiteral("saveOptions"));
  settings.sync();

  // Defaults match Qt's own implicit quality, so an unset save writes today's bytes.
  auto defaults = patchy::ui::load_image_save_option_defaults();
  CHECK(defaults.webp_quality == 75);
  CHECK(!defaults.webp_lossless);
  CHECK(patchy::ui::image_save_options_apply_to_extension(QStringLiteral("webp")));
  CHECK(patchy::ui::image_save_options_apply_to_extension(QStringLiteral(".WEBP")));

  defaults.webp_quality = 42;
  defaults.webp_lossless = true;
  patchy::ui::save_image_save_option_defaults(defaults);
  const auto reloaded = patchy::ui::load_image_save_option_defaults();
  CHECK(reloaded.webp_quality == 42);
  CHECK(reloaded.webp_lossless);

  // Out-of-range stored values clamp instead of reaching the encoder.
  settings.setValue(QStringLiteral("saveOptions/webpQuality"), -5);
  settings.sync();
  CHECK(patchy::ui::load_image_save_option_defaults().webp_quality == 0);
  settings.setValue(QStringLiteral("saveOptions/webpQuality"), 1000);
  settings.sync();
  CHECK(patchy::ui::load_image_save_option_defaults().webp_quality == 100);
  settings.remove(QStringLiteral("saveOptions"));
  settings.sync();

  // Save As form: slider and spin mirror, Lossless greys the quality row, no export section.
  patchy::ui::ImageSaveOptions seed;
  seed.webp_quality = 42;
  bool saw_dialog = false;
  QTimer::singleShot(0, [&saw_dialog] {
    auto* dialog = find_top_level_dialog(QStringLiteral("webpSaveOptionsDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* quality = dialog->findChild<QSpinBox*>(QStringLiteral("webpQualitySpin"));
    auto* slider = dialog->findChild<QSlider*>(QStringLiteral("webpQualitySlider"));
    auto* lossless = dialog->findChild<QCheckBox*>(QStringLiteral("webpLosslessCheck"));
    CHECK(quality != nullptr && slider != nullptr && lossless != nullptr);
    if (quality == nullptr || slider == nullptr || lossless == nullptr) {
      dialog->reject();
      return;
    }
    CHECK(quality->value() == 42);
    slider->setValue(64);
    CHECK(quality->value() == 64);
    CHECK(quality->isEnabled());
    lossless->click();
    CHECK(!quality->isEnabled());
    CHECK(dialog->findChild<QComboBox*>(QStringLiteral("exportScaleCombo")) == nullptr);
    saw_dialog = true;
    dialog->accept();
  });
  auto chosen = patchy::ui::prompt_image_save_options(nullptr, QStringLiteral("webp"), seed);
  CHECK(saw_dialog);
  CHECK(chosen.has_value());
  if (chosen.has_value()) {
    CHECK(chosen->webp_quality == 64);
    CHECK(chosen->webp_lossless);
  }

  // The export form carries the shared section and still fits its labels.
  saw_dialog = false;
  QTimer::singleShot(0, [&saw_dialog] {
    auto* dialog = find_top_level_dialog(QStringLiteral("webpSaveOptionsDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    CHECK(dialog->findChild<QComboBox*>(QStringLiteral("exportScaleCombo")) != nullptr);
    CHECK(dialog->findChild<QCheckBox*>(QStringLiteral("exportTrimCheck")) != nullptr);
    CHECK(no_clipped_labels(*dialog));
    saw_dialog = true;
    dialog->reject();
  });
  chosen = patchy::ui::prompt_image_save_options(nullptr, QStringLiteral("webp"), seed, /*for_export*/ true,
                                                 QSize(32, 32));
  CHECK(saw_dialog);
  CHECK(!chosen.has_value());

  settings.remove(QStringLiteral("saveOptions"));
  settings.sync();
}

void ui_export_options_sections_do_not_clip_labels() {
  auto settings = patchy::ui::app_settings();
  settings.remove(QStringLiteral("saveOptions"));
  settings.sync();

  const patchy::ui::ImageSaveOptions defaults;
  const std::vector<std::pair<QString, QString>> forms = {
      {QStringLiteral("bmp"), QStringLiteral("bmpSaveOptionsDialog")},
      {QStringLiteral("jxr"), QStringLiteral("jxrSaveOptionsDialog")},
      {QStringLiteral("rttex"), QStringLiteral("rttexSaveOptionsDialog")},
      {QStringLiteral("dds"), QStringLiteral("ddsSaveOptionsDialog")},
      {QStringLiteral("pdf"), QStringLiteral("pdfSaveOptionsDialog")},
      {QStringLiteral("tga"), QStringLiteral("exportScaleOptionsDialog")},
  };
  for (const auto& [extension, object_name] : forms) {
    bool saw_dialog = false;
    QTimer::singleShot(0, [&saw_dialog, object_name] {
      auto* dialog = find_top_level_dialog(object_name);
      CHECK(dialog != nullptr);
      if (dialog == nullptr) {
        return;
      }
      CHECK(dialog->findChild<QComboBox*>(QStringLiteral("exportScaleCombo")) != nullptr);
      CHECK(no_clipped_labels(*dialog));
      saw_dialog = true;
      dialog->reject();
    });
    const auto chosen =
        patchy::ui::prompt_image_save_options(nullptr, extension, defaults, /*for_export*/ true, QSize(300, 200));
    CHECK(saw_dialog);
    CHECK(!chosen.has_value());
  }

  bool saw_gif_dialog = false;
  QTimer::singleShot(0, [&saw_gif_dialog] {
    auto* dialog = find_top_level_dialog(QStringLiteral("gifSaveOptionsDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    CHECK(dialog->findChild<QComboBox*>(QStringLiteral("exportScaleCombo")) != nullptr);
    CHECK(no_clipped_labels(*dialog));
    saw_gif_dialog = true;
    dialog->reject();
  });
  const auto gif = patchy::ui::prompt_gif_save_options(nullptr, defaults, /*offer_flatten_choice*/ true,
                                                       /*for_export*/ true, /*has_visible_frames*/ true,
                                                       QSize(300, 200));
  CHECK(saw_gif_dialog);
  CHECK(!gif.has_value());

  settings.remove(QStringLiteral("saveOptions"));
  settings.sync();
}

void check_gif_webp_gif_round_trip(const char* fixture, const QString& artifact) {
  const auto input = patchy::ui::to_qstring(
      patchy::test::committed_format_fixture_path("gif", fixture));
  QImageReader source(input);
  CHECK(source.imageCount() == 3);
  const auto original_loops = source.loopCount();
  std::vector<QImage> expected;
  std::vector<int> delays;
  for (int frame = 0; frame < 3; ++frame) {
    expected.push_back(source.read().convertToFormat(QImage::Format_RGBA8888));
    CHECK(!expected.back().isNull());
    delays.push_back(source.nextImageDelay());
  }
  SettingsValueRestorer notes_setting(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().setValue(QStringLiteral("imports/showPsdWarningsAndInfo"), false);
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, input);
  const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  CHECK(document.layers().size() == 3);
  patchy::test::ui::ensure_artifact_dir();
  for (const bool lossless : {true, false}) {
    patchy::ui::ImageSaveOptions options;
    options.webp_animate = true;
    options.webp_lossless = lossless;
    const auto output = lossless ? artifact + QStringLiteral("-lossless.webp")
                                 : artifact + QStringLiteral("-default.webp");
    patchy::ui::write_flat_image_file(document, output, QStringLiteral("webp"), options);
    QImageReader reader(output);
    CHECK(reader.imageCount() == 3);
    CHECK(reader.loopCount() == original_loops);
    for (std::size_t frame = 0; frame < expected.size(); ++frame) {
      const auto actual = reader.read().convertToFormat(QImage::Format_RGBA8888);
      CHECK(actual.size() == expected[frame].size());
      CHECK(reader.nextImageDelay() == delays[frame]);
      std::uint64_t color_error = 0;
      std::uint64_t color_samples = 0;
      for (int y = 0; y < actual.height(); ++y) for (int x = 0; x < actual.width(); ++x) {
        const auto* a = actual.constScanLine(y) + x * 4;
        const auto* e = expected[frame].constScanLine(y) + x * 4;
        CHECK(a[3] == e[3]);
        if (e[3] != 0) for (int channel = 0; channel < 3; ++channel) {
          const auto difference = std::abs(static_cast<int>(a[channel]) - e[channel]);
          if (lossless) CHECK(difference == 0);
          color_error += static_cast<unsigned>(difference);
          ++color_samples;
        }
      }
      // Default lossy quality may alter colors, but must preserve the pictured frame.
      CHECK(color_samples > 0);
      CHECK(color_error <= color_samples * 10);
    }
  }
  patchy::ui::MainWindowTestAccess::open_document_path(window,
      artifact + QStringLiteral("-lossless.webp"));
  const auto& reopened = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  CHECK(reopened.layers().size() == 3);
  for (std::size_t frame = 0; frame < delays.size(); ++frame) {
    CHECK(patchy::animation::parse_layer_name_delay_ms(reopened.layers()[2 - frame].name()) ==
          static_cast<std::uint32_t>(delays[frame]));
  }
  patchy::ui::ImageSaveOptions gif_options;
  gif_options.gif_animate = true;
  const auto round_trip = artifact + QStringLiteral("-round-trip.gif");
  patchy::ui::write_flat_image_file(reopened, round_trip, QStringLiteral("gif"), gif_options);
  QImageReader final_gif(round_trip);
  CHECK(final_gif.imageCount() == 3);
  CHECK(final_gif.loopCount() == original_loops);
  for (std::size_t frame = 0; frame < expected.size(); ++frame) {
    const auto actual = final_gif.read().convertToFormat(QImage::Format_RGBA8888);
    CHECK(actual.size() == expected[frame].size());
    CHECK(final_gif.nextImageDelay() == delays[frame]);
    for (int y = 0; y < actual.height(); ++y) for (int x = 0; x < actual.width(); ++x) {
      const auto* a = actual.constScanLine(y) + x * 4;
      const auto* e = expected[frame].constScanLine(y) + x * 4;
      CHECK(a[3] == e[3]);
      if (e[3] != 0) CHECK(std::equal(a, a + 3, e));
    }
  }
}

void ui_animated_gif_webp_gif_round_trip_preserves_animation() {
  check_gif_webp_gif_round_trip("pillow-animated.gif", QStringLiteral("test-artifacts/gif-webp-gif"));
  check_gif_webp_gif_round_trip("pillow-animated-transparent.gif", QStringLiteral("test-artifacts/transparent-gif-webp-gif"));
}

void ui_webp_animation_import_export_unicode_round_trip() {
  const auto fixture = patchy::test::committed_format_fixture_path("webp", "pillow-animation.webp");
  SettingsValueRestorer notes_setting(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().setValue(QStringLiteral("imports/showPsdWarningsAndInfo"), false);
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, patchy::ui::to_qstring(fixture));
  const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  CHECK(document.layers().size() == 3);
  CHECK(document.metadata().values.at(patchy::webp::kLoopCountMetadata) == "3");
  const std::array<unsigned, 3> delays{33, 67, 101};
  for (std::size_t frame = 0; frame < 3; ++frame) {
    const auto& layer = document.layers()[2 - frame];
    CHECK(layer.visible());
    CHECK(patchy::animation::parse_layer_name_delay_ms(layer.name()) == delays[frame]);
    QImage expected(patchy::ui::to_qstring(fixture.parent_path()) + QStringLiteral("/animation-frame-%1.png").arg(frame + 1));
    expected = expected.convertToFormat(QImage::Format_RGBA8888);
    for (int y = 0; y < 24; ++y) for (int x = 0; x < 32; ++x) {
      const auto* pixel = layer.pixels().pixel(x, y);
      const auto* original = expected.constScanLine(y) + x * 4;
      CHECK(pixel[3] == original[3]);
      if (pixel[3] != 0) CHECK(std::equal(pixel, pixel + 3, original));
    }
  }
  QTemporaryDir directory;
  CHECK(directory.isValid());
  const auto stem = patchy::test::kUnicodePathStems[0];
  const auto path = directory.path() + QLatin1Char('/') + QString::fromUtf8(reinterpret_cast<const char*>(stem.data()), static_cast<qsizetype>(stem.size())) + QStringLiteral(".webp");
  patchy::ui::ImageSaveOptions options;
  options.webp_animate = true; options.webp_lossless = true; options.webp_loop_count = 3;
  patchy::ui::write_flat_image_file(document, path, QStringLiteral("webp"), options);
  QImageReader reader(path);
  CHECK(reader.imageCount() == 3); CHECK(reader.loopCount() == 2);
  for (const auto delay : delays) {
    CHECK(!reader.read().isNull());
    CHECK(reader.nextImageDelay() == static_cast<int>(delay));
  }
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  const auto& reopened = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  CHECK(reopened.layers().size() == 3);
  CHECK(reopened.metadata().values.at(patchy::webp::kLoopCountMetadata) == "3");
  // Failure leaves a previous file untouched.
  QFile old_file(path); CHECK(old_file.open(QIODevice::ReadOnly)); const auto old_bytes = old_file.readAll(); old_file.close();
  options.webp_loop_count = 65536;
  bool threw = false;
  try { patchy::ui::write_flat_image_file(reopened, path, QStringLiteral("webp"), options); }
  catch (const std::exception&) { threw = true; }
  CHECK(threw); CHECK(old_file.open(QIODevice::ReadOnly)); CHECK(old_file.readAll() == old_bytes);
}

void ui_webp_animation_options_and_empty_layers() {
  auto settings = patchy::ui::app_settings(); settings.remove(QStringLiteral("saveOptions"));
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::ImageSaveOptions seed;
  seed.offer_animation = true; seed.webp_loop_count = 3; seed.animation_frame_delay_ms = 33;
  std::exception_ptr failure;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("webpSaveOptionsDialog"));
    try {
      CHECK(dialog != nullptr);
      auto* mode = dialog->findChild<QRadioButton*>(QStringLiteral("webpAnimationRadio"));
      auto* delay = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("webpFrameDelaySpin"));
      auto* forever_check = dialog->findChild<QCheckBox*>(QStringLiteral("webpForeverCheck"));
      auto* count = dialog->findChild<QSpinBox*>(QStringLiteral("webpPlayCountSpin"));
      CHECK(mode && delay && forever_check && count);
      CHECK(mode->isChecked()); CHECK(delay->value() == 0.033); CHECK(count->value() == 3);
      CHECK(no_clipped_labels(*dialog));
      patchy::test::ui::save_widget_artifact("webp-animation-options", *dialog);
      forever_check->setChecked(true); CHECK(!count->isEnabled());
      forever_check->setChecked(false); count->setValue(5); delay->setValue(0.067);
      dialog->accept();
    } catch (...) { failure = std::current_exception(); if (dialog) dialog->reject(); }
  });
  const auto chosen = patchy::ui::prompt_image_save_options(&window, QStringLiteral("webp"), seed);
  if (failure) std::rethrow_exception(failure);
  CHECK(chosen.has_value()); CHECK(chosen->webp_animate);
  CHECK(chosen->animation_frame_delay_ms == 67); CHECK(chosen->webp_loop_count == 5);
  patchy::ui::save_image_save_option_defaults(*chosen);
  CHECK(patchy::ui::load_image_save_option_defaults().animation_frame_delay_ms == 67);
  CHECK(patchy::ui::load_image_save_option_defaults().webp_loop_count == 0);
  seed.has_visible_frames = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("webpSaveOptionsDialog"));
    try {
      CHECK(dialog != nullptr);
      auto* mode = dialog->findChild<QRadioButton*>(QStringLiteral("webpAnimationRadio"));
      CHECK(mode && !mode->isEnabled() && !mode->isChecked());
      dialog->accept();
    } catch (...) { failure = std::current_exception(); if (dialog) dialog->reject(); }
  });
  const auto flat = patchy::ui::prompt_image_save_options(&window, QStringLiteral("webp"), seed);
  if (failure) std::rethrow_exception(failure);
  CHECK(flat.has_value() && !flat->webp_animate);
  seed.offer_animation = false;
  seed.webp_animate = true;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("webpSaveOptionsDialog"));
    try {
      CHECK(dialog != nullptr);
      CHECK(dialog->findChild<QRadioButton*>(QStringLiteral("webpAnimationRadio")) == nullptr);
      CHECK(dialog->findChild<QComboBox*>(QStringLiteral("exportScaleCombo")) != nullptr);
      CHECK(no_clipped_labels(*dialog));
      patchy::test::ui::save_widget_artifact("webp-animation-export-options", *dialog);
      dialog->accept();
    } catch (...) { failure = std::current_exception(); if (dialog) dialog->reject(); }
  });
  const auto animated = patchy::ui::prompt_image_save_options(&window, QStringLiteral("webp"), seed, true, QSize(32, 24));
  if (failure) std::rethrow_exception(failure);
  CHECK(animated.has_value() && animated->webp_animate);
  settings.remove(QStringLiteral("saveOptions"));
}

// A translucent RGBA gradient with a distinct corner, for the JPEG XL round trips.
patchy::PixelBuffer jxl_test_gradient(std::int32_t width, std::int32_t height) {
  patchy::PixelBuffer pixels(width, height, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      auto* px = pixels.row(y).data() + static_cast<std::size_t>(x) * 4U;
      px[0] = static_cast<std::uint8_t>(x * 20);
      px[1] = static_cast<std::uint8_t>(y * 25);
      px[2] = static_cast<std::uint8_t>(90 + x + y);
      px[3] = static_cast<std::uint8_t>(x == 0 ? 0 : 60 + x * 15);
    }
  }
  return pixels;
}

void ui_jxl_opens_and_saves_as_a_read_write_format() {
  const ScopedFlatAlphaAsLayerMask mask_import(true);
  ensure_artifact_dir();
  CHECK(patchy::ui::image_format_preserves_alpha("jxl"));
  const QString source_path = QStringLiteral("test-artifacts/jxl_source.jxl");
  const auto source = jxl_test_gradient(12, 9);
  {
    patchy::Document document(12, 9, patchy::PixelFormat::rgba8());
    document.add_pixel_layer("Background", source);
    patchy::jxl::WriteOptions options;
    options.lossless = true;
    patchy::jxl::write_jxl_file(document, patchy::ui::to_filesystem_path(source_path), options);
  }

  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, source_path);
  QApplication::processEvents();
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.width() == 12);
  CHECK(document.height() == 9);
  CHECK(std::as_const(document).file_info().source_format == "JXL");
  // Flat-image alpha becomes an editable document-alpha mask, as for PNG and WebP.
  const auto& layer = std::as_const(document).layers().front();
  CHECK(layer.name() == "Background");
  CHECK(layer.mask().has_value());
  CHECK(patchy::layer_mask_is_document_alpha(layer));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_path(window) == source_path);
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));

  // The registry row has a writer, so Save writes .jxl in place; lossless gives the exact
  // pixels back, alpha included.
  const QString saved = QStringLiteral("test-artifacts/jxl_round_trip.jxl");
  QFile::remove(saved);
  patchy::ui::ImageSaveOptions options;
  options.jxl_lossless = true;
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, saved, options));
  const auto reread = patchy::jxl::read_jxl_file(patchy::ui::to_filesystem_path(saved));
  const auto& pixels = std::as_const(reread.document.layers().front()).pixels();
  CHECK(pixels.format() == patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < 9; ++y) {
    for (std::int32_t x = 1; x < 12; ++x) {  // column 0 is fully transparent
      CHECK(std::equal(pixels.pixel(x, y), pixels.pixel(x, y) + 4, source.pixel(x, y)));
    }
    CHECK(pixels.pixel(0, y)[3] == 0);
  }

  // A 16-bit file opens at 16 bits and saves back at 16 bits.
  patchy::set_deep_editing_override(true);
  const QString deep_path = QStringLiteral("test-artifacts/jxl_source16.jxl");
  {
    patchy::PixelBuffer deep(4, 3, patchy::with_bit_depth(patchy::PixelFormat::rgb8(), patchy::BitDepth::UInt16));
    auto* samples = reinterpret_cast<std::uint16_t*>(deep.data().data());
    for (std::size_t i = 0; i < 36; ++i) {
      samples[i] = static_cast<std::uint16_t>(i * 1801U + 7U);
    }
    patchy::Document deep_document(4, 3, deep.format());
    deep_document.color_state().bit_depth = patchy::BitDepth::UInt16;
    deep_document.add_pixel_layer("Background", std::move(deep));
    patchy::jxl::WriteOptions deep_options;
    deep_options.lossless = true;
    patchy::jxl::write_jxl_file(deep_document, patchy::ui::to_filesystem_path(deep_path), deep_options);
  }
  patchy::ui::MainWindow deep_window;
  show_window(deep_window);
  patchy::ui::MainWindowTestAccess::open_document_path(deep_window, deep_path);
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::document(deep_window).color_state().bit_depth ==
        patchy::BitDepth::UInt16);
  const QString deep_saved = QStringLiteral("test-artifacts/jxl_round_trip16.jxl");
  QFile::remove(deep_saved);
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(deep_window, deep_saved, options));
  CHECK(patchy::jxl::read_jxl_file(patchy::ui::to_filesystem_path(deep_saved)).document.color_state().bit_depth ==
        patchy::BitDepth::UInt16);
  patchy::set_deep_editing_override(std::nullopt);
}

void ui_avif_opens_and_saves_as_a_read_write_format() {
  const ScopedFlatAlphaAsLayerMask mask_import(true);
  if (!patchy::avif::is_available()) {
    patchy::test::skip("this build has no AVIF codec");
  }
  ensure_artifact_dir();
  CHECK(patchy::ui::image_format_preserves_alpha("avif"));
  CHECK(patchy::ui::image_save_options_apply_to_extension(QStringLiteral("avif")));
  const QString source_path = QStringLiteral("test-artifacts/avif_source.avif");
  const auto source = jxl_test_gradient(12, 9);
  {
    patchy::Document document(12, 9, patchy::PixelFormat::rgba8());
    document.add_pixel_layer("Background", source);
    patchy::avif::WriteOptions options;
    options.lossless = true;
    options.speed = 10;
    patchy::formats::write_file_bytes(patchy::ui::to_filesystem_path(source_path),
                                      patchy::avif::write_avif(document, options), "AVIF");
  }

  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, source_path);
  QApplication::processEvents();
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.width() == 12);
  CHECK(document.height() == 9);
  CHECK(std::as_const(document).file_info().source_format == "AVIF");
  const auto& layer = std::as_const(document).layers().front();
  CHECK(layer.name() == "Background");
  CHECK(layer.mask().has_value());
  CHECK(patchy::layer_mask_is_document_alpha(layer));

  // Save writes .avif in place through libaom; lossless gives the exact pixels back.
  const QString saved = QStringLiteral("test-artifacts/avif_round_trip.avif");
  QFile::remove(saved);
  patchy::ui::ImageSaveOptions options;
  options.avif_lossless = true;
  options.avif_speed = 10;
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, saved, options));
  QFile saved_file(saved);
  CHECK(saved_file.open(QIODevice::ReadOnly));
  const auto saved_bytes = saved_file.readAll();
  const auto reread = patchy::avif::read_avif(std::vector<std::uint8_t>(saved_bytes.begin(), saved_bytes.end()));
  const auto& pixels = std::as_const(reread.document.layers().front()).pixels();
  CHECK(pixels.format() == patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < 9; ++y) {
    for (std::int32_t x = 1; x < 12; ++x) {  // column 0 is fully transparent
      CHECK(std::equal(pixels.pixel(x, y), pixels.pixel(x, y) + 4, source.pixel(x, y)));
    }
    CHECK(pixels.pixel(0, y)[3] == 0);
  }
}

// Netpbm portable anymaps (.pbm/.pgm/.ppm, and .pnm for any of them) open through Qt's
// built-in PPM handler, which sniffs the P1-P6 magic when the suffix says nothing; the
// Open dialog lists them as one read-only row (GitHub issue 270). Unicode folder and
// name as every file-opening path requires.
void ui_portable_anymap_files_open() {
  // A 1-bit PBM arrives as a two-color indexed image, which would otherwise raise the
  // adopt-palette question (docs/palette-mode.md).
  SettingsValueRestorer policy_restorer(QStringLiteral("imports/adoptIndexedPalette"));
  patchy::ui::app_settings().setValue(QStringLiteral("imports/adoptIndexedPalette"), QStringLiteral("never"));
  const auto dir = patchy::ui::to_qstring(std::filesystem::absolute(patchy::test::unicode_artifact_dir(u8"portable-anymap")));
  const auto write_bytes = [&](const QString& name, const QByteArray& bytes) {
    const auto path = QDir(dir).filePath(name);
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size());
    return path;
  };
  // Colors through the exporter, so the check is independent of the buffer's byte order.
  const auto pixel = [](const patchy::Document& document, int x, int y) {
    const auto color = patchy::ui::qimage_from_document(document, true).pixelColor(x, y);
    return std::array<int, 4>{color.red(), color.green(), color.blue(), color.alpha()};
  };
  // Binary pixmap (P6): three colors over two rows.
  {
    QByteArray bytes("P6\n3 2\n255\n");
    const unsigned char rgb[] = {255, 0, 0, 0, 255, 0, 0, 0, 255, 10, 20, 30, 40, 50, 60, 70, 80, 90};
    bytes.append(reinterpret_cast<const char*>(rgb), sizeof(rgb));
    const auto path = write_bytes(QStringLiteral("pixmap.ppm"), bytes);
    patchy::ui::MainWindow window;
    show_window(window);
    const auto sessions_before = patchy::ui::MainWindowTestAccess::session_count(window);
    patchy::ui::MainWindowTestAccess::open_document_path(window, path);
    QApplication::processEvents();
    CHECK(patchy::ui::MainWindowTestAccess::session_count(window) == sessions_before + 1);
    if (patchy::ui::MainWindowTestAccess::session_count(window) == sessions_before + 1) {
      auto& document = patchy::ui::MainWindowTestAccess::document(window);
      CHECK(document.width() == 3);
      CHECK(document.height() == 2);
      CHECK(pixel(document, 0, 0) == (std::array<int, 4>{255, 0, 0, 255}));
      CHECK(pixel(document, 2, 0) == (std::array<int, 4>{0, 0, 255, 255}));
      CHECK(pixel(document, 1, 1) == (std::array<int, 4>{40, 50, 60, 255}));
    }
  }
  // Binary graymap (P5).
  {
    QByteArray bytes("P5\n3 2\n255\n");
    const unsigned char gray[] = {0, 128, 255, 17, 34, 51};
    bytes.append(reinterpret_cast<const char*>(gray), sizeof(gray));
    const auto path = write_bytes(QStringLiteral("graymap.pgm"), bytes);
    patchy::ui::MainWindow window;
    show_window(window);
    const auto sessions_before = patchy::ui::MainWindowTestAccess::session_count(window);
    patchy::ui::MainWindowTestAccess::open_document_path(window, path);
    QApplication::processEvents();
    CHECK(patchy::ui::MainWindowTestAccess::session_count(window) == sessions_before + 1);
    if (patchy::ui::MainWindowTestAccess::session_count(window) == sessions_before + 1) {
      auto& document = patchy::ui::MainWindowTestAccess::document(window);
      CHECK(document.width() == 3);
      CHECK(document.height() == 2);
      CHECK(pixel(document, 1, 0) == (std::array<int, 4>{128, 128, 128, 255}));
      CHECK(pixel(document, 2, 1) == (std::array<int, 4>{51, 51, 51, 255}));
    }
  }
  // Binary bitmap (P4): a set bit is black.
  {
    QByteArray bytes("P4\n8 2\n");
    const unsigned char bits[] = {0xA0, 0x01};
    bytes.append(reinterpret_cast<const char*>(bits), sizeof(bits));
    const auto path = write_bytes(QStringLiteral("bitmap.pbm"), bytes);
    patchy::ui::MainWindow window;
    show_window(window);
    const auto sessions_before = patchy::ui::MainWindowTestAccess::session_count(window);
    patchy::ui::MainWindowTestAccess::open_document_path(window, path);
    QApplication::processEvents();
    CHECK(patchy::ui::MainWindowTestAccess::session_count(window) == sessions_before + 1);
    if (patchy::ui::MainWindowTestAccess::session_count(window) == sessions_before + 1) {
      auto& document = patchy::ui::MainWindowTestAccess::document(window);
      CHECK(document.width() == 8);
      CHECK(document.height() == 2);
      CHECK(pixel(document, 0, 0) == (std::array<int, 4>{0, 0, 0, 255}));
      CHECK(pixel(document, 1, 0) == (std::array<int, 4>{255, 255, 255, 255}));
      CHECK(pixel(document, 2, 0) == (std::array<int, 4>{0, 0, 0, 255}));
      CHECK(pixel(document, 7, 1) == (std::array<int, 4>{0, 0, 0, 255}));
      CHECK(pixel(document, 0, 1) == (std::array<int, 4>{255, 255, 255, 255}));
    }
  }
  // .pnm says nothing about the kind: the P6 magic decides, with a non-ASCII name.
  {
    QByteArray bytes("P6\n2 1\n255\n");
    const unsigned char rgb[] = {1, 2, 3, 250, 251, 252};
    bytes.append(reinterpret_cast<const char*>(rgb), sizeof(rgb));
    const auto name = QString::fromUtf8(patchy::test::utf8_string(patchy::test::kUnicodePathStems[0])) + QStringLiteral(".pnm");
    const auto path = write_bytes(name, bytes);
    patchy::ui::MainWindow window;
    show_window(window);
    const auto sessions_before = patchy::ui::MainWindowTestAccess::session_count(window);
    patchy::ui::MainWindowTestAccess::open_document_path(window, path);
    QApplication::processEvents();
    CHECK(patchy::ui::MainWindowTestAccess::session_count(window) == sessions_before + 1);
    if (patchy::ui::MainWindowTestAccess::session_count(window) == sessions_before + 1) {
      auto& document = patchy::ui::MainWindowTestAccess::document(window);
      CHECK(document.width() == 2);
      CHECK(document.height() == 1);
      CHECK(pixel(document, 1, 0) == (std::array<int, 4>{250, 251, 252, 255}));
    }
  }
}

void ui_avif_save_options_persist_and_dialog() {
  auto settings = patchy::ui::app_settings();
  settings.remove(QStringLiteral("saveOptions"));
  settings.sync();
  auto defaults = patchy::ui::load_image_save_option_defaults();
  CHECK(defaults.avif_quality == 80);
  CHECK(!defaults.avif_lossless);
  CHECK(defaults.avif_speed == 6);

  defaults.avif_quality = 55;
  defaults.avif_lossless = true;
  defaults.avif_speed = 3;
  patchy::ui::save_image_save_option_defaults(defaults);
  auto reloaded = patchy::ui::load_image_save_option_defaults();
  CHECK(reloaded.avif_quality == 55);
  CHECK(reloaded.avif_lossless);
  CHECK(reloaded.avif_speed == 3);
  // Out-of-range stored values clamp instead of reaching the encoder.
  settings.setValue(QStringLiteral("saveOptions/avifSpeed"), 40);
  settings.setValue(QStringLiteral("saveOptions/avifQuality"), 300);
  settings.sync();
  reloaded = patchy::ui::load_image_save_option_defaults();
  CHECK(reloaded.avif_speed == 10);
  CHECK(reloaded.avif_quality == 100);

  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::ImageSaveOptions seed;
  std::exception_ptr failure;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("avifSaveOptionsDialog"));
    try {
      CHECK(dialog != nullptr);
      auto* quality = dialog->findChild<QSpinBox*>(QStringLiteral("avifQualitySpin"));
      auto* speed = dialog->findChild<QSpinBox*>(QStringLiteral("avifSpeedSpin"));
      auto* lossless = dialog->findChild<QCheckBox*>(QStringLiteral("avifLosslessCheck"));
      CHECK(quality && speed && lossless);
      CHECK(dialog->findChild<QLabel*>(QStringLiteral("avifSaveNote")) != nullptr);
      CHECK(quality->value() == 80);
      CHECK(speed->value() == 6);
      CHECK(no_clipped_labels(*dialog));
      patchy::test::ui::save_widget_artifact("avif-save-options", *dialog);
      speed->setValue(8);
      lossless->setChecked(true);
      CHECK(!quality->parentWidget()->isEnabled());
      dialog->accept();
    } catch (...) {
      failure = std::current_exception();
      if (dialog) dialog->reject();
    }
  });
  const auto chosen = patchy::ui::prompt_image_save_options(&window, QStringLiteral("avif"), seed);
  if (failure) std::rethrow_exception(failure);
  CHECK(chosen.has_value());
  CHECK(chosen->avif_speed == 8);
  CHECK(chosen->avif_lossless);
  settings.remove(QStringLiteral("saveOptions"));
  settings.sync();
}

void ui_jxl_save_options_persist_and_dialog() {
  auto settings = patchy::ui::app_settings();
  settings.remove(QStringLiteral("saveOptions"));
  settings.sync();
  auto defaults = patchy::ui::load_image_save_option_defaults();
  CHECK(defaults.jxl_quality == 90);
  CHECK(!defaults.jxl_lossless);
  CHECK(defaults.jxl_effort == 7);
  CHECK(patchy::ui::image_save_options_apply_to_extension(QStringLiteral("jxl")));
  CHECK(patchy::ui::image_save_options_apply_to_extension(QStringLiteral(".JXL")));

  defaults.jxl_quality = 70;
  defaults.jxl_lossless = true;
  defaults.jxl_effort = 3;
  patchy::ui::save_image_save_option_defaults(defaults);
  auto reloaded = patchy::ui::load_image_save_option_defaults();
  CHECK(reloaded.jxl_quality == 70);
  CHECK(reloaded.jxl_lossless);
  CHECK(reloaded.jxl_effort == 3);
  // Out-of-range stored values clamp instead of reaching the encoder.
  settings.setValue(QStringLiteral("saveOptions/jxlEffort"), 40);
  settings.setValue(QStringLiteral("saveOptions/jxlQuality"), 0);
  settings.sync();
  reloaded = patchy::ui::load_image_save_option_defaults();
  CHECK(reloaded.jxl_effort == 9);
  CHECK(reloaded.jxl_quality == 1);

  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::ImageSaveOptions seed;
  seed.offer_animation = true;
  std::exception_ptr failure;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("jxlSaveOptionsDialog"));
    try {
      CHECK(dialog != nullptr);
      auto* quality = dialog->findChild<QSpinBox*>(QStringLiteral("jxlQualitySpin"));
      auto* effort = dialog->findChild<QSpinBox*>(QStringLiteral("jxlEffortSpin"));
      auto* lossless = dialog->findChild<QCheckBox*>(QStringLiteral("jxlLosslessCheck"));
      auto* mode = dialog->findChild<QRadioButton*>(QStringLiteral("jxlAnimationRadio"));
      CHECK(quality && effort && lossless && mode);
      CHECK(dialog->findChild<QLabel*>(QStringLiteral("jxlSaveNote")) != nullptr);
      CHECK(quality->value() == 90);
      CHECK(effort->value() == 7);
      CHECK(no_clipped_labels(*dialog));
      patchy::test::ui::save_widget_artifact("jxl-save-options", *dialog);
      effort->setValue(4);
      lossless->setChecked(true);
      CHECK(!quality->parentWidget()->isEnabled());
      dialog->accept();
    } catch (...) {
      failure = std::current_exception();
      if (dialog) dialog->reject();
    }
  });
  const auto chosen = patchy::ui::prompt_image_save_options(&window, QStringLiteral("jxl"), seed);
  if (failure) std::rethrow_exception(failure);
  CHECK(chosen.has_value());
  CHECK(chosen->jxl_effort == 4);
  CHECK(chosen->jxl_lossless);
  CHECK(chosen->jxl_animate);
  settings.remove(QStringLiteral("saveOptions"));
  settings.sync();
}

void ui_jxl_animated_save_round_trips() {
  ensure_artifact_dir();
  patchy::Document document(6, 4, patchy::PixelFormat::rgba8());
  const char* names[] = {"bottom 0.1s", "middle 0.2s", "top"};
  for (int i = 0; i < 3; ++i) {
    patchy::PixelBuffer pixels(6, 4, patchy::PixelFormat::rgba8());
    for (std::int32_t y = 0; y < 4; ++y) {
      for (std::int32_t x = 0; x < 6; ++x) {
        auto* px = pixels.row(y).data() + static_cast<std::size_t>(x) * 4U;
        px[0] = static_cast<std::uint8_t>(80 * i);
        px[1] = static_cast<std::uint8_t>(x * 30);
        px[2] = static_cast<std::uint8_t>(y * 50);
        px[3] = 255;
      }
    }
    document.add_pixel_layer(names[i], std::move(pixels));
  }
  const QString path = QStringLiteral("test-artifacts/jxl_animation.jxl");
  QFile::remove(path);
  patchy::ui::ImageSaveOptions options;
  options.jxl_animate = true;
  options.jxl_lossless = true;
  options.jxl_loop_count = 2;
  options.animation_frame_delay_ms = 50;
  patchy::ui::write_flat_image_file(document, path, QStringLiteral("jxl"), options);

  // Frame 1 is the top layer; a name's time token overrides the default delay.
  const auto bytes = patchy::jxl::read_jxl_detailed(
      [&] {
        QFile file(path);
        CHECK(file.open(QIODevice::ReadOnly));
        const auto data = file.readAll();
        return std::vector<std::uint8_t>(data.begin(), data.end());
      }());
  CHECK(bytes.frame_count == 3);
  const auto& layers = bytes.read.document.layers();
  CHECK(layers.size() == 3);
  CHECK(layers.back().name() == "Frame 1 0.05s");
  CHECK(layers[1].name() == "Frame 2 0.2s");
  CHECK(layers.front().name() == "Frame 3 0.1s");
  CHECK(std::as_const(layers.back()).pixels().pixel(0, 0)[0] == 160);
  CHECK(bytes.read.document.metadata().values.at(patchy::jxl::kLoopCountMetadata) == "2");

  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  const auto& reopened = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  CHECK(reopened.layers().size() == 3);
  CHECK(QString::fromStdString(reopened.layers().back().name()).startsWith(QStringLiteral("Frame 1")));
  CHECK(reopened.metadata().values.at(patchy::jxl::kLoopCountMetadata) == "2");
}


// GitHub issue 117: Qt's libtiff has no JPEG codec, so JPEG-compressed TIFFs (compression 7)
// decode strip by strip through Qt's JPEG reader, with the shared JPEGTables spliced in.
// Builds an APNG from frames (all the same color type: ARGB32 saves as RGBA 8-bit).
// `regions` gives each frame's placement; the canvas is the first frame's size.
struct ApngFrameSpec {
  QImage image;
  QPoint offset;
  unsigned delay_num;
  unsigned delay_den;
  unsigned dispose;
  unsigned blend;
};

QByteArray build_apng(const std::vector<ApngFrameSpec>& specs, bool default_in_animation,
                      const QImage& separate_default = QImage(), bool duplicate_sequence = false) {
  static const auto crc_table = [] {
    std::array<unsigned, 256> table{};
    for (unsigned n = 0; n < 256; ++n) {
      unsigned c = n;
      for (int k = 0; k < 8; ++k) c = (c & 1U) != 0 ? 0xedb88320U ^ (c >> 1) : c >> 1;
      table[n] = c;
    }
    return table;
  }();
  const auto crc32 = [](const QByteArray& data) {
    unsigned crc = 0xffffffffU;
    for (const char byte : data) crc = crc_table[(crc ^ static_cast<unsigned char>(byte)) & 0xffU] ^ (crc >> 8);
    return crc ^ 0xffffffffU;
  };
  const auto be32 = [](unsigned v) {
    QByteArray out;
    out.append(static_cast<char>(v >> 24)).append(static_cast<char>(v >> 16)).append(static_cast<char>(v >> 8))
        .append(static_cast<char>(v));
    return out;
  };
  const auto be16 = [](unsigned v) {
    QByteArray out;
    out.append(static_cast<char>(v >> 8)).append(static_cast<char>(v));
    return out;
  };
  QByteArray apng;
  const auto chunk = [&](const char* type, const QByteArray& data) {
    apng.append(be32(static_cast<unsigned>(data.size())));
    QByteArray body(type, 4);
    body.append(data);
    apng.append(body);
    apng.append(be32(crc32(body)));
  };
  // PNG chunks of one frame, split into IHDR data and the concatenated IDAT data.
  struct Parts { QByteArray ihdr; QByteArray idat; };
  const auto parts_of = [](const QImage& image) {
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.convertToFormat(QImage::Format_ARGB32).save(&buffer, "PNG");
    Parts parts;
    qsizetype at = 8;
    while (at + 12 <= png.size()) {
      const auto length = static_cast<unsigned>((static_cast<unsigned char>(png[at]) << 24) |
                                                (static_cast<unsigned char>(png[at + 1]) << 16) |
                                                (static_cast<unsigned char>(png[at + 2]) << 8) |
                                                static_cast<unsigned char>(png[at + 3]));
      const auto type = png.mid(at + 4, 4);
      const auto data = png.mid(at + 8, static_cast<qsizetype>(length));
      if (type == "IHDR") parts.ihdr = data;
      if (type == "IDAT") parts.idat.append(data);
      at += 12 + static_cast<qsizetype>(length);
    }
    return parts;
  };
  apng.append("\x89PNG\r\n\x1a\n", 8);
  const auto first = parts_of(specs.front().image);
  chunk("IHDR", first.ihdr);  // the canvas is the first frame's size
  chunk("acTL", be32(static_cast<unsigned>(specs.size())) + be32(0));
  unsigned sequence = 0;
  const auto fctl = [&](const ApngFrameSpec& spec) {
    QByteArray data;
    data.append(be32(duplicate_sequence ? 0 : sequence++));
    data.append(be32(static_cast<unsigned>(spec.image.width()))).append(be32(static_cast<unsigned>(spec.image.height())));
    data.append(be32(static_cast<unsigned>(spec.offset.x()))).append(be32(static_cast<unsigned>(spec.offset.y())));
    data.append(be16(spec.delay_num)).append(be16(spec.delay_den));
    data.append(static_cast<char>(spec.dispose)).append(static_cast<char>(spec.blend));
    chunk("fcTL", data);
  };
  if (default_in_animation) {
    fctl(specs.front());
    chunk("IDAT", first.idat);
  } else {
    chunk("IDAT", parts_of(separate_default).idat);
  }
  for (std::size_t i = default_in_animation ? 1 : 0; i < specs.size(); ++i) {
    fctl(specs[i]);
    QByteArray data = be32(duplicate_sequence ? 0 : sequence++);
    data.append(parts_of(specs[i].image).idat);
    chunk("fdAT", data);
  }
  chunk("IEND", {});
  return apng;
}

std::span<const std::uint8_t> bytes_of(const QByteArray& data) {
  return {reinterpret_cast<const std::uint8_t*>(data.constData()), static_cast<std::size_t>(data.size())};
}

QImage solid_frame(int width, int height, QColor color) {
  QImage image(width, height, QImage::Format_ARGB32);
  image.fill(color);
  return image;
}

// GitHub issue 119: an animated PNG opens as one layer per frame, like GIF and
// WebP: frame 1 on top, every layer visible, the delay as a seconds token.
void ui_animated_png_imports_frames_as_layers() {
  const auto apng = build_apng({{solid_frame(6, 4, QColor(255, 0, 0)), {0, 0}, 1, 10, 0, 0},
                                {solid_frame(6, 4, QColor(0, 255, 0)), {0, 0}, 1, 2, 0, 0},
                                {solid_frame(6, 4, QColor(0, 0, 255)), {0, 0}, 1, 1, 0, 0}},
                               true);
  CHECK(patchy::apng::is_animated_png(bytes_of(apng)));
  const auto animation = patchy::apng::read_animated_png(bytes_of(apng));
  CHECK(animation.width == 6 && animation.height == 4);
  CHECK(animation.frames.size() == 3);
  if (animation.frames.size() == 3) {
    CHECK(animation.frames[0].delay_ms == 100);
    CHECK(animation.frames[1].delay_ms == 500);
    CHECK(animation.frames[2].delay_ms == 1000);
    CHECK(animation.frames[0].image.pixelColor(2, 2) == QColor(255, 0, 0));
    CHECK(animation.frames[1].image.pixelColor(2, 2) == QColor(0, 255, 0));
    CHECK(animation.frames[2].image.pixelColor(5, 3) == QColor(0, 0, 255));
  }

  // A plain PNG is not an animation.
  QByteArray plain;
  {
    QBuffer buffer(&plain);
    buffer.open(QIODevice::WriteOnly);
    solid_frame(6, 4, QColor(1, 2, 3)).save(&buffer, "PNG");
  }
  CHECK(!patchy::apng::is_animated_png(bytes_of(plain)));

  const auto path = QStringLiteral("test-artifacts/animated.png");
  {
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(apng);
  }
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, QFileInfo(path).absoluteFilePath());
  QApplication::processEvents();
  const auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.width() == 6 && document.height() == 4);
  CHECK(document.layers().size() == 3);
  if (document.layers().size() == 3) {
    // Frame 1 is the TOP layer (the last in the vector), like the GIF import.
    CHECK(document.layers().back().name() == "Frame 1 " + patchy::animation::format_delay_seconds_token(100));
    CHECK(document.layers().front().name() == "Frame 3 " + patchy::animation::format_delay_seconds_token(1000));
    for (const auto& layer : document.layers()) {
      CHECK(layer.visible());
    }
    CHECK(document.layers().back().pixels().pixel(1, 1)[0] == 255);
    CHECK(document.layers().front().pixels().pixel(1, 1)[2] == 255);
  }
}

// Sub-canvas frames composite with their offsets, dispose and blend ops: a
// "background" disposal clears the frame's region before the next frame, and
// "over" blending composites a translucent frame instead of replacing pixels.
void ui_animated_png_composites_offsets_dispose_and_blend() {
  const auto apng = build_apng({{solid_frame(4, 4, QColor(255, 0, 0)), {0, 0}, 1, 10, 0, 0},
                                {solid_frame(2, 2, QColor(0, 0, 255)), {1, 1}, 1, 10, 1, 0},
                                {solid_frame(4, 4, QColor(0, 255, 0, 128)), {0, 0}, 1, 10, 0, 1}},
                               true);
  const auto animation = patchy::apng::read_animated_png(bytes_of(apng));
  CHECK(animation.frames.size() == 3);
  if (animation.frames.size() != 3) {
    return;
  }
  const auto& second = animation.frames[1].image;
  CHECK(second.pixelColor(0, 0) == QColor(255, 0, 0));
  CHECK(second.pixelColor(1, 1) == QColor(0, 0, 255));
  CHECK(second.pixelColor(2, 2) == QColor(0, 0, 255));
  CHECK(second.pixelColor(3, 3) == QColor(255, 0, 0));
  const auto& third = animation.frames[2].image;
  // Outside the disposed region: green over red.
  const auto blended = third.pixelColor(0, 0);
  CHECK(std::abs(blended.red() - 127) <= 2 && std::abs(blended.green() - 128) <= 2 && blended.blue() == 0 &&
        blended.alpha() == 255);
  // Inside it: the region was cleared, so the translucent green stands alone.
  const auto cleared = third.pixelColor(1, 1);
  CHECK(std::abs(cleared.alpha() - 128) <= 1 && cleared.red() == 0 && cleared.green() >= 250);
}

// A default image without a frame control is a fallback for still viewers and
// not part of the animation; out-of-order sequence numbers reject the file,
// and the open path then keeps the default image.
void ui_animated_png_skips_separate_default_image_and_rejects_bad_sequences() {
  const auto separate = build_apng({{solid_frame(4, 4, QColor(255, 0, 0)), {0, 0}, 1, 10, 0, 0},
                                    {solid_frame(4, 4, QColor(0, 0, 255)), {0, 0}, 1, 10, 0, 0}},
                                   false, solid_frame(4, 4, QColor(128, 128, 128)));
  CHECK(patchy::apng::is_animated_png(bytes_of(separate)));
  const auto animation = patchy::apng::read_animated_png(bytes_of(separate));
  CHECK(animation.frames.size() == 2);
  if (animation.frames.size() == 2) {
    CHECK(animation.frames[0].image.pixelColor(0, 0) == QColor(255, 0, 0));
    CHECK(animation.frames[1].image.pixelColor(0, 0) == QColor(0, 0, 255));
  }

  const auto broken = build_apng({{solid_frame(4, 4, QColor(255, 0, 0)), {0, 0}, 1, 10, 0, 0},
                                  {solid_frame(4, 4, QColor(0, 0, 255)), {0, 0}, 1, 10, 0, 0}},
                                 true, QImage(), true);
  bool threw = false;
  try {
    (void)patchy::apng::read_animated_png(bytes_of(broken));
  } catch (const std::exception&) {
    threw = true;
  }
  CHECK(threw);
  const auto path = QStringLiteral("test-artifacts/animated_bad_sequence.png");
  {
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(broken);
  }
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, QFileInfo(path).absoluteFilePath());
  QApplication::processEvents();
  const auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.width() == 4 && document.height() == 4);
  CHECK(document.layers().size() == 1);
}

// GitHub issue 366: File > Export > Layers as Animated PNG writes the visible top-level
// layers as APNG frames, top layer first, that Patchy's own reader decodes exactly, at a
// Unicode path. Frame 1 is the default image, so a plain PNG reader shows it.
void ui_apng_export_round_trips_through_the_apng_reader() {
  patchy::Document document(10, 6, patchy::PixelFormat::rgba8());
  document.print_settings().horizontal_ppi = 150.0;
  document.print_settings().vertical_ppi = 150.0;
  const auto layer_pixels = [](int kind) {
    patchy::PixelBuffer pixels(10, 6, patchy::PixelFormat::rgba8());
    for (std::int32_t y = 0; y < 6; ++y) for (std::int32_t x = 0; x < 10; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(kind == 0 ? 200 : x * 20);
      px[1] = static_cast<std::uint8_t>(kind == 1 ? 180 : y * 30);
      px[2] = static_cast<std::uint8_t>(kind == 2 ? 220 : 10);
      px[3] = kind == 2 && x >= 5 ? 0 : 255;  // the top layer is half transparent
    }
    return pixels;
  };
  document.add_pixel_layer("bottom 70s", layer_pixels(0));
  document.add_pixel_layer("middle", layer_pixels(1));
  document.add_pixel_layer("top 0.033s", layer_pixels(2));
  document.add_pixel_layer("hidden", layer_pixels(1));
  document.layers().back().set_visible(false);
  std::vector<patchy::PixelBuffer> expected{layer_pixels(2), layer_pixels(1), layer_pixels(0)};

  QTemporaryDir directory;
  CHECK(directory.isValid());
  const auto stem = patchy::test::kUnicodePathStems[1];
  const auto path = directory.path() + QLatin1Char('/') +
      QString::fromUtf8(reinterpret_cast<const char*>(stem.data()), static_cast<qsizetype>(stem.size())) +
      QStringLiteral(".png");
  patchy::ui::ImageSaveOptions options;
  options.apng_animate = true;
  options.apng_loop_count = 2;
  options.animation_frame_delay_ms = 50;
  patchy::ui::write_flat_image_file(document, path, QStringLiteral("png"), options);

  QFile file(path);
  CHECK(file.open(QIODevice::ReadOnly));
  const auto data = file.readAll();
  file.close();
  // Kept for checks with other decoders (browsers, Pillow).
  ensure_artifact_dir();
  QFile::remove(QStringLiteral("test-artifacts/apng-export.png"));
  CHECK(QFile::copy(path, QStringLiteral("test-artifacts/apng-export.png")));
  CHECK(patchy::apng::is_animated_png(bytes_of(data)));
  const auto animation = patchy::apng::read_animated_png(bytes_of(data));
  CHECK(animation.width == 10 && animation.height == 6);
  CHECK(animation.play_count == 2);
  CHECK(animation.frames.size() == 3);
  const std::array<std::uint32_t, 3> delays{33, 50, 70000};
  for (std::size_t frame = 0; frame < 3; ++frame) {
    CHECK(animation.frames[frame].delay_ms == delays[frame]);
    const auto image = animation.frames[frame].image.convertToFormat(QImage::Format_RGBA8888);
    for (int y = 0; y < 6; ++y) for (int x = 0; x < 10; ++x) {
      const auto* got = image.constScanLine(y) + x * 4;
      const auto* want = expected[frame].pixel(x, y);
      CHECK(got[3] == want[3]);
      if (want[3] != 0) CHECK(std::equal(got, got + 3, want));
    }
  }
  QImage still(path);
  CHECK(still.width() == 10);
  CHECK(still.convertToFormat(QImage::Format_RGBA8888).pixelColor(0, 0) == QColor(0, 0, 220));
  CHECK(still.dotsPerMeterX() == 5906);

  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  const auto& reopened = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  CHECK(reopened.layers().size() == 3);
  CHECK(reopened.layers().back().name() == "Frame 1 0.033s");
  CHECK(reopened.metadata().values.at(patchy::apng::kLoopCountMetadata) == "2");

  // A failed export leaves the existing file alone.
  options.apng_loop_count = 65536;
  bool threw = false;
  try { patchy::ui::write_flat_image_file(document, path, QStringLiteral("png"), options); }
  catch (const std::exception&) { threw = true; }
  CHECK(threw);
  CHECK(file.open(QIODevice::ReadOnly));
  CHECK(file.readAll() == data);
}

// The Animated PNG options dialog behind File > Export > Layers as Animated PNG: frame
// delay and play count, plus the shared Export section. Save As .png shows no dialog.
void ui_apng_export_dialog_options() {
  auto settings = patchy::ui::app_settings();
  settings.remove(QStringLiteral("saveOptions"));
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(window.findChild<QAction*>(QStringLiteral("fileExportAnimatedPngAction")) != nullptr);
  CHECK(window.findChild<QAction*>(QStringLiteral("fileExportAnimatedPngAction"))->menuRole() == QAction::NoRole);
  patchy::ui::ImageSaveOptions seed;
  seed.apng_animate = true;
  seed.apng_loop_count = 4;
  std::exception_ptr failure;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("apngSaveOptionsDialog"));
    try {
      CHECK(dialog != nullptr);
      auto* delay = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("apngFrameDelaySpin"));
      auto* forever_check = dialog->findChild<QCheckBox*>(QStringLiteral("apngForeverCheck"));
      auto* plays = dialog->findChild<QSpinBox*>(QStringLiteral("apngPlayCountSpin"));
      CHECK(delay && forever_check && plays);
      CHECK(dialog->findChild<QLabel*>(QStringLiteral("apngAnimationExplanationLabel")) != nullptr);
      CHECK(dialog->findChild<QWidget*>(QStringLiteral("exportMetadataCombo")) != nullptr);
      CHECK(!forever_check->isChecked());
      CHECK(plays->value() == 4);
      CHECK(no_clipped_labels(*dialog));
      patchy::test::ui::save_widget_artifact("apng-export-options", *dialog);
      delay->setValue(0.25);
      plays->setValue(7);
      forever_check->setChecked(true);
      CHECK(!plays->isEnabled());
      dialog->accept();
    } catch (...) {
      failure = std::current_exception();
      if (dialog) dialog->reject();
    }
  });
  const auto chosen = patchy::ui::prompt_image_save_options(&window, QStringLiteral("png"), seed, true, QSize(10, 6));
  if (failure) std::rethrow_exception(failure);
  CHECK(chosen.has_value());
  CHECK(chosen->apng_animate);
  CHECK(chosen->apng_loop_count == 0);
  CHECK(chosen->animation_frame_delay_ms == 250);
  // A plain PNG save has no options dialog and stays flat.
  const auto plain = patchy::ui::prompt_image_save_options(&window, QStringLiteral("png"), {}, false);
  CHECK(plain.has_value());
  CHECK(!plain->apng_animate);
  settings.remove(QStringLiteral("saveOptions"));
  settings.sync();
}

void ui_jpeg_compressed_tiff_opens_through_the_jpeg_decoder() {
  ensure_artifact_dir();
  QImage source(24, 16, QImage::Format_RGB32);
  for (int y = 0; y < source.height(); ++y) {
    for (int x = 0; x < source.width(); ++x) {
      source.setPixelColor(x, y, QColor(x * 10, y * 15, 200 - x * 4));
    }
  }
  // Two strips of 8 rows. Each strip is its own JPEG; its tables move to JPEGTables so
  // the strips are abbreviated streams, as libtiff writes them.
  const int rows_per_strip = 8;
  std::vector<QByteArray> strips;
  QByteArray tables;
  for (int strip = 0; strip < 2; ++strip) {
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    buffer.open(QIODevice::WriteOnly);
    CHECK(source.copy(0, strip * rows_per_strip, source.width(), rows_per_strip).save(&buffer, "JPEG", 100));
    // Walk the markers up to SOS: DQT (0xdb) and DHT (0xc4) go to the tables, the rest stays.
    QByteArray abbreviated;
    abbreviated.append(QByteArray::fromHex("ffd8"));
    QByteArray strip_tables;
    qsizetype at = 2;
    while (at + 4 <= jpeg.size()) {
      const auto marker = static_cast<unsigned char>(jpeg[at + 1]);
      if (marker == 0xda) {
        abbreviated.append(jpeg.mid(at));  // SOS and the entropy-coded data through EOI
        break;
      }
      const auto length = (static_cast<unsigned char>(jpeg[at + 2]) << 8) | static_cast<unsigned char>(jpeg[at + 3]);
      const auto segment = jpeg.mid(at, 2 + length);
      (marker == 0xdb || marker == 0xc4 ? strip_tables : abbreviated).append(segment);
      at += 2 + length;
    }
    if (tables.isEmpty()) {
      tables.append(QByteArray::fromHex("ffd8"));
      tables.append(strip_tables);
      tables.append(QByteArray::fromHex("ffd9"));
    }
    strips.push_back(abbreviated);
  }

  // A little-endian classic TIFF: header, one IFD of eleven entries, then the data area.
  QByteArray tiff;
  const auto put8 = [&tiff](unsigned v) { tiff.append(static_cast<char>(static_cast<unsigned char>(v & 0xffU))); };
  const auto put16 = [&put8](unsigned v) { put8(v); put8(v >> 8); };
  const auto put32 = [&put8](unsigned v) { put8(v); put8(v >> 8); put8(v >> 16); put8(v >> 24); };
  const unsigned ifd_at = 8;
  const unsigned entry_count = 11;
  unsigned data_at = ifd_at + 2 + entry_count * 12 + 4;
  const unsigned bits_at = data_at;
  data_at += 6;
  const unsigned offsets_at = data_at;
  data_at += 8;
  const unsigned counts_at = data_at;
  data_at += 8;
  const unsigned tables_at = data_at;
  data_at += static_cast<unsigned>(tables.size());
  const unsigned strip0_at = data_at;
  const unsigned strip1_at = strip0_at + static_cast<unsigned>(strips[0].size());
  tiff.append("II", 2);
  put16(42);
  put32(ifd_at);
  put16(entry_count);
  const auto entry = [&](unsigned tag, unsigned type, unsigned count, unsigned value) {
    put16(tag);
    put16(type);
    put32(count);
    if (type == 3 && count == 1) {
      put16(value);
      put16(0);
    } else {
      put32(value);
    }
  };
  entry(256, 4, 1, static_cast<unsigned>(source.width()));
  entry(257, 4, 1, static_cast<unsigned>(source.height()));
  entry(258, 3, 3, bits_at);
  entry(259, 3, 1, 7);
  entry(262, 3, 1, 6);
  entry(273, 4, 2, offsets_at);
  entry(277, 3, 1, 3);
  entry(278, 4, 1, static_cast<unsigned>(rows_per_strip));
  entry(279, 4, 2, counts_at);
  entry(284, 3, 1, 1);
  entry(347, 7, static_cast<unsigned>(tables.size()), tables_at);
  put32(0);
  put16(8);
  put16(8);
  put16(8);
  put32(strip0_at);
  put32(strip1_at);
  put32(static_cast<unsigned>(strips[0].size()));
  put32(static_cast<unsigned>(strips[1].size()));
  tiff.append(tables);
  tiff.append(strips[0]);
  tiff.append(strips[1]);

  const std::span<const std::uint8_t> bytes(reinterpret_cast<const std::uint8_t*>(tiff.constData()),
                                           static_cast<std::size_t>(tiff.size()));
  CHECK(patchy::ui::tiff_jpeg::is_jpeg_compressed_tiff(bytes));
  const auto decoded = patchy::ui::tiff_jpeg::read_jpeg_compressed_tiff(bytes);
  CHECK(decoded.error.isEmpty());
  CHECK(decoded.image.size() == source.size());
  if (decoded.image.size() == source.size()) {
    int worst = 0;
    for (int y = 0; y < source.height(); ++y) {
      for (int x = 0; x < source.width(); ++x) {
        const auto a = source.pixelColor(x, y);
        const auto b = decoded.image.pixelColor(x, y);
        worst = std::max({worst, std::abs(a.red() - b.red()), std::abs(a.green() - b.green()),
                          std::abs(a.blue() - b.blue())});
      }
    }
    CHECK(worst <= 12);  // quality-100 JPEG round trip, chroma subsampling included
  }

  // The same bytes through the open path, where Qt's own reader fails first.
  const auto path = QStringLiteral("test-artifacts/jpeg_compressed.tif");
  {
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(tiff);
  }
  // Whether Qt's own reader also opens it depends on the libtiff it was built with (the
  // Flatpak's KDE runtime one has JPEG support); the open path must succeed either way.
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, QFileInfo(path).absoluteFilePath());
  QApplication::processEvents();
  const auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.width() == source.width() && document.height() == source.height());
}

}  // namespace

std::vector<patchy::test::TestCase> flat_image_format_tests() {
  return {
      {"ui_qimage_import_export_preserves_alpha_and_formats", ui_qimage_import_export_preserves_alpha_and_formats},
      {"ui_deep_images_import_at_their_depth", ui_deep_images_import_at_their_depth},
      {"ui_qimage_import_export_writes_tiff_and_webp", ui_qimage_import_export_writes_tiff_and_webp},
      {"ui_jpeg_compressed_tiff_opens_through_the_jpeg_decoder", ui_jpeg_compressed_tiff_opens_through_the_jpeg_decoder},
      {"ui_animated_png_imports_frames_as_layers", ui_animated_png_imports_frames_as_layers},
      {"ui_animated_png_composites_offsets_dispose_and_blend", ui_animated_png_composites_offsets_dispose_and_blend},
      {"ui_animated_png_skips_separate_default_image_and_rejects_bad_sequences",
       ui_animated_png_skips_separate_default_image_and_rejects_bad_sequences},
      {"ui_apng_export_round_trips_through_the_apng_reader", ui_apng_export_round_trips_through_the_apng_reader},
      {"ui_apng_export_dialog_options", ui_apng_export_dialog_options},
      {"ui_image_save_options_write_bmp_alpha_and_jpeg_quality",
       ui_image_save_options_write_bmp_alpha_and_jpeg_quality},
      {"ui_flat_alpha_round_trips_as_editable_mask", ui_flat_alpha_round_trips_as_editable_mask},
      {"ui_flat_alpha_opens_as_plain_layer_transparency", ui_flat_alpha_opens_as_plain_layer_transparency},
      {"ui_flat_alpha_layer_mask_preference_keeps_the_mask", ui_flat_alpha_layer_mask_preference_keeps_the_mask},
      {"ui_preferences_flat_alpha_as_layer_mask_checkbox", ui_preferences_flat_alpha_as_layer_mask_checkbox},
      {"ui_flat_alpha_open_captures", ui_flat_alpha_open_captures},
      {"ui_image_save_options_defaults_and_dialogs", ui_image_save_options_defaults_and_dialogs},
      {"ui_export_dialog_offers_cmyk_for_print_formats", ui_export_dialog_offers_cmyk_for_print_formats},
      {"ui_ico_export_dialog_sizes_and_resample", ui_ico_export_dialog_sizes_and_resample},
      {"ui_ico_real_world_fixtures_decode_png_entries", ui_ico_real_world_fixtures_decode_png_entries},
      {"ui_gif_export_round_trips_through_qt_reader", ui_gif_export_round_trips_through_qt_reader},
      {"ui_animated_gif_opens_frames_as_layers", ui_animated_gif_opens_frames_as_layers},
      {"ui_import_notices_dialog_shown_when_setting_enabled",
       ui_import_notices_dialog_shown_when_setting_enabled},
      {"ui_deep_psd_import_forces_notices_popup", ui_deep_psd_import_forces_notices_popup},
      {"ui_color_mode_and_ink_adjustment_imports_force_notices_popup",
       ui_color_mode_and_ink_adjustment_imports_force_notices_popup},
      {"ui_animated_gif_export_round_trips", ui_animated_gif_export_round_trips},
      {"ui_animated_gif_open_save_round_trip", ui_animated_gif_open_save_round_trip},
      {"ui_gif_save_options_dialog_choices", ui_gif_save_options_dialog_choices},
      {"ui_twelve_bit_jpeg_opens_as_16_bit_document", ui_twelve_bit_jpeg_opens_as_16_bit_document},
      {"ui_jxr_opens_and_saves_as_a_read_write_format", ui_jxr_opens_and_saves_as_a_read_write_format},
      {"ui_jxr_save_options_persist_quality_and_lossless",
       ui_jxr_save_options_persist_quality_and_lossless},
      {"ui_jxl_opens_and_saves_as_a_read_write_format", ui_jxl_opens_and_saves_as_a_read_write_format},
      {"ui_jxl_save_options_persist_and_dialog", ui_jxl_save_options_persist_and_dialog},
      {"ui_avif_opens_and_saves_as_a_read_write_format", ui_avif_opens_and_saves_as_a_read_write_format},
      {"ui_portable_anymap_files_open", ui_portable_anymap_files_open},
      {"ui_avif_save_options_persist_and_dialog", ui_avif_save_options_persist_and_dialog},
      {"ui_jxl_animated_save_round_trips", ui_jxl_animated_save_round_trips},
      {"ui_rttex_opens_and_saves_as_a_read_write_format", ui_rttex_opens_and_saves_as_a_read_write_format},
      {"ui_rttex_jpeg_save_round_trips_through_qt_encoder", ui_rttex_jpeg_save_round_trips_through_qt_encoder},
      {"ui_rttex_save_options_persist_and_dialog_prefills_from_source",
       ui_rttex_save_options_persist_and_dialog_prefills_from_source},
      {"ui_dds_opens_and_saves_as_a_read_write_format", ui_dds_opens_and_saves_as_a_read_write_format},
      {"ui_dds_bc3_source_resaves_as_bc3_with_alpha_intact", ui_dds_bc3_source_resaves_as_bc3_with_alpha_intact},
      {"ui_dds_save_options_persist_and_dialog_prefills_from_source",
       ui_dds_save_options_persist_and_dialog_prefills_from_source},
      {"ui_dds_automatic_follows_opened_file_and_previews_the_chain",
       ui_dds_automatic_follows_opened_file_and_previews_the_chain},
      {"ui_dds_cubemap_import_is_layered_and_save_routes_to_save_as",
       ui_dds_cubemap_import_is_layered_and_save_routes_to_save_as},
      {"ui_export_trim_keeps_document_alpha_mask_colors", ui_export_trim_keeps_document_alpha_mask_colors},
      {"ui_animated_gif_export_trims_frames_to_union_bounds", ui_animated_gif_export_trims_frames_to_union_bounds},
      {"ui_webp_animation_import_export_unicode_round_trip", ui_webp_animation_import_export_unicode_round_trip},
      {"ui_animated_gif_webp_gif_round_trip_preserves_animation",
       ui_animated_gif_webp_gif_round_trip_preserves_animation},
      {"ui_webp_animation_export_trims_frames_to_union_bounds", ui_webp_animation_export_trims_frames_to_union_bounds},
      {"ui_webp_animation_options_and_empty_layers", ui_webp_animation_options_and_empty_layers},
      {"ui_webp_lossless_round_trips_and_quality_orders_size", ui_webp_lossless_round_trips_and_quality_orders_size},
      {"ui_export_options_dialog_shared_section", ui_export_options_dialog_shared_section},
      {"ui_export_options_jpeg_forces_background_fill", ui_export_options_jpeg_forces_background_fill},
      {"ui_webp_save_options_dialog_persists_quality_and_lossless",
       ui_webp_save_options_dialog_persists_quality_and_lossless},
      {"ui_export_options_sections_do_not_clip_labels", ui_export_options_sections_do_not_clip_labels},
  };
}
