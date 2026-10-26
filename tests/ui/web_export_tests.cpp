// File > Export for Web (docs/web-export.md): the settings model and its defaults, the
// JPEG coding options read back from the bytes, the preview engine's stale-request
// handling, and the dialog end to end (previews, sizes, compare modes, Save).
#include "core/document.hpp"
#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"
#include "ui/app_settings.hpp"
#include "ui/image_document_io.hpp"
#include "ui/main_window.hpp"
#include "ui/qt_paths.hpp"
#include "ui/web_export.hpp"
#include "ui/zoomable_image_preview.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QTreeWidget>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace patchy::test::ui;
using patchy::ui::MainWindowTestAccess;
using patchy::ui::WebExportFormat;
using patchy::ui::WebExportPreviewEngine;
using patchy::ui::WebExportSettings;

// Every exportForWeb/* key, restored after the test.
struct WebExportSettingsGuard {
  WebExportSettingsGuard() {
    for (const auto* key : {"format", "jpegQuality", "jpegProgressive", "jpegOptimize", "jpegBackground",
                            "webpQuality", "webpLossless", "webpAnimate", "metadata", "thumbnail", "resample",
                            "trim", "convertToSrgb", "compare", "revealWhenDone", "pngColors", "pngPaletteColors",
                            "pngDither", "pngDitherAmount", "gifColors", "gifDither", "gifDitherAmount", "gifAnimate",
                            "gifFrameDelayMs", "avifQuality", "avifSpeed", "avifLossless", "jxlQuality",
                            "jxlEffort", "jxlLossless", "jxlAnimate", "jxlFrameDelayMs"}) {
      const auto full = QStringLiteral("exportForWeb/") + QLatin1String(key);
      restorers.push_back(std::make_unique<SettingsValueRestorer>(full));
      auto settings = patchy::ui::app_settings();
      settings.remove(full);
    }
  }
  std::vector<std::unique_ptr<SettingsValueRestorer>> restorers;
};

// A photo-like test image: smooth gradients plus deterministic noise, so lossy coding
// choices change the byte count.
patchy::Document textured_document(int width, int height) {
  QImage image(width, height, QImage::Format_RGBA8888);
  std::uint32_t seed = 12345U;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      seed = seed * 1664525U + 1013904223U;
      const int noise = static_cast<int>((seed >> 24) % 40U) - 20;
      image.setPixelColor(x, y,
                          QColor(std::clamp(x * 255 / std::max(1, width - 1) + noise, 0, 255),
                                 std::clamp(y * 255 / std::max(1, height - 1) - noise, 0, 255),
                                 std::clamp(128 + noise * 2, 0, 255), 255));
    }
  }
  return patchy::ui::document_from_qimage(image, "Texture");
}

std::vector<std::uint8_t> encode_jpeg(const patchy::Document& document, const WebExportSettings& settings) {
  const QSize size(document.width(), document.height());
  const auto job = patchy::ui::web_export_job(settings, WebExportFormat::Jpeg, size);
  const auto prepared = WebExportPreviewEngine::prepare(document, job.geometry, job.convert_to_srgb, nullptr);
  const auto result = WebExportPreviewEngine::encode(document, job, prepared);
  CHECK(result.error.isEmpty());
  CHECK(!result.decoded.isNull());
  return result.bytes != nullptr ? *result.bytes : std::vector<std::uint8_t>{};
}

// The first start-of-frame marker: 0xC0 baseline, 0xC1 extended, 0xC2 progressive.
int jpeg_frame_marker(const std::vector<std::uint8_t>& bytes) {
  std::size_t at = 2;
  while (at + 4 <= bytes.size()) {
    if (bytes[at] != 0xFF) {
      return -1;
    }
    const auto marker = bytes[at + 1];
    if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
      return marker;
    }
    const auto length = static_cast<std::size_t>((bytes[at + 2] << 8) | bytes[at + 3]);
    at += 2 + length;
  }
  return -1;
}

QTreeWidgetItem* row_named(QTreeWidget* list, const QString& name) {
  for (int row = 0; row < list->topLevelItemCount(); ++row) {
    if (list->topLevelItem(row)->text(0) == name) {
      return list->topLevelItem(row);
    }
  }
  return nullptr;
}

bool wait_until(const std::function<bool()>& condition, int timeout_ms = 60000) {
  QElapsedTimer timer;
  timer.start();
  while (!condition() && timer.elapsed() < timeout_ms) {
    QApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  return condition();
}

QString artifact_dir() {
  const auto dir = QFileInfo(QStringLiteral("test-artifacts/web-export")).absoluteFilePath();
  QDir().mkpath(dir);
  return dir;
}

void ui_web_export_defaults_and_options() {
  WebExportSettingsGuard guard;
  const auto loaded = patchy::ui::load_web_export_settings();
  // First use: JPEG at quality 80, progressive and optimized, no metadata, no thumbnail.
  CHECK(loaded.format == WebExportFormat::Jpeg);
  CHECK(loaded.jpeg_quality == 80);
  CHECK(loaded.jpeg_progressive);
  CHECK(loaded.jpeg_optimize);
  CHECK(loaded.metadata == patchy::metadata::MetadataPolicy::None);
  CHECK(!loaded.thumbnail);
  CHECK(loaded.compare == patchy::ui::WebExportCompare::Split);

  const QSize size(200, 100);
  auto settings = loaded;
  settings.width = 200;
  settings.height = 100;
  const auto jpeg = patchy::ui::web_export_image_options(settings, WebExportFormat::Jpeg, size);
  CHECK(jpeg.jpeg_quality == 80);
  CHECK(jpeg.jpeg_progressive && jpeg.jpeg_optimize);
  CHECK(!jpeg.jpeg_exif_thumbnail);
  CHECK(jpeg.metadata_policy == patchy::metadata::MetadataPolicy::None);
  CHECK(jpeg.export_fill_transparent);
  // The document's own size is no resize.
  CHECK(jpeg.export_width == 0 && jpeg.export_height == 0 && jpeg.export_scale == 1);
  settings.width = 100;
  settings.height = 50;
  const auto halved = patchy::ui::web_export_image_options(settings, WebExportFormat::Png, size);
  CHECK(halved.export_width == 100 && halved.export_height == 50);
  CHECK(!halved.export_fill_transparent);
  settings.resample = patchy::ui::WebExportResample::PixelArt;
  settings.pixel_scale = 3;
  const auto pixel_art = patchy::ui::web_export_image_options(settings, WebExportFormat::Webp, size);
  CHECK(pixel_art.export_scale == 3 && pixel_art.export_width == 0);

  // Settings round trip through the persisted keys; each format keeps its own.
  settings.format = WebExportFormat::Webp;
  settings.jpeg_quality = 55;
  settings.webp_quality = 61;
  settings.webp_lossless = true;
  settings.metadata = patchy::metadata::MetadataPolicy::CopyrightAndContact;
  settings.compare = patchy::ui::WebExportCompare::SideBySide;
  patchy::ui::save_web_export_settings(settings);
  const auto reloaded = patchy::ui::load_web_export_settings();
  CHECK(reloaded.format == WebExportFormat::Webp);
  CHECK(reloaded.jpeg_quality == 55);
  CHECK(reloaded.webp_quality == 61 && reloaded.webp_lossless);
  CHECK(reloaded.metadata == patchy::metadata::MetadataPolicy::CopyrightAndContact);
  CHECK(reloaded.resample == patchy::ui::WebExportResample::PixelArt);
  CHECK(reloaded.compare == patchy::ui::WebExportCompare::SideBySide);
  CHECK(patchy::ui::web_export_format_key(WebExportFormat::Jpeg) == QStringLiteral("jpeg"));
  CHECK(patchy::ui::web_export_format_from_key(QStringLiteral("png")) == WebExportFormat::Png);
  CHECK(patchy::ui::web_export_format_from_key(QStringLiteral("gif")) == WebExportFormat::Gif);
  CHECK(patchy::ui::web_export_format_from_key(QStringLiteral("jxl")) == WebExportFormat::Jxl);
  CHECK(patchy::ui::web_export_format_from_key(QStringLiteral("avif")) == WebExportFormat::Avif);
  CHECK(!patchy::ui::web_export_format_from_key(QStringLiteral("bmp")).has_value());
  // AVIF and JPEG XL: Flat Image's defaults, their own options only for their own format.
  CHECK(loaded.avif_quality == 80 && loaded.avif_speed == 6 && !loaded.avif_lossless);
  CHECK(loaded.jxl_quality == 90 && loaded.jxl_effort == 7 && !loaded.jxl_lossless && !loaded.jxl_animate);
  auto slow_settings = loaded;
  slow_settings.avif_quality = 55;
  slow_settings.avif_speed = 9;
  slow_settings.jxl_lossless = true;
  slow_settings.jxl_effort = 3;
  const auto avif = patchy::ui::web_export_image_options(slow_settings, WebExportFormat::Avif, size);
  CHECK(avif.avif_quality == 55 && avif.avif_speed == 9 && !avif.avif_lossless);
  const auto jxl = patchy::ui::web_export_image_options(slow_settings, WebExportFormat::Jxl, size);
  CHECK(jxl.jxl_lossless && jxl.jxl_effort == 3);
  CHECK(patchy::ui::web_export_format_is_slow(WebExportFormat::Avif));
  CHECK(!patchy::ui::web_export_format_is_slow(WebExportFormat::Png));
  // GIF and palette PNG: 256 colors, no dither, full-color PNG by default; the options
  // reach the writers only for their own format.
  CHECK(!loaded.png_palette && loaded.png_colors == 256 && loaded.gif_colors == 256);
  CHECK(!loaded.gif_dither && !loaded.png_dither && !loaded.gif_animate);
  auto palette_settings = loaded;
  palette_settings.png_palette = true;
  palette_settings.png_colors = 32;
  palette_settings.png_dither = true;
  palette_settings.png_dither_amount = 60;
  palette_settings.gif_colors = 64;
  palette_settings.gif_dither = true;
  const auto png = patchy::ui::web_export_image_options(palette_settings, WebExportFormat::Png, size);
  CHECK(png.png_palette && png.palette_colors == 32 && png.palette_dither && png.palette_dither_amount == 60);
  const auto gif = patchy::ui::web_export_image_options(palette_settings, WebExportFormat::Gif, size);
  CHECK(!gif.png_palette && gif.palette_colors == 64 && gif.palette_dither && gif.palette_dither_amount == 100);
  const auto jpeg_again = patchy::ui::web_export_image_options(palette_settings, WebExportFormat::Jpeg, size);
  CHECK(jpeg_again.palette_colors == 256 && !jpeg_again.palette_dither);
  CHECK(patchy::ui::web_export_job(palette_settings, WebExportFormat::Gif, size).signature !=
        patchy::ui::web_export_job(loaded, WebExportFormat::Gif, size).signature);
}

void ui_web_export_jpeg_progressive_and_optimize_read_back() {
  const auto document = textured_document(160, 120);
  WebExportSettings settings;
  settings.jpeg_progressive = false;
  settings.jpeg_optimize = false;
  const auto baseline = encode_jpeg(document, settings);
  settings.jpeg_optimize = true;
  const auto optimized = encode_jpeg(document, settings);
  settings.jpeg_progressive = true;
  const auto progressive = encode_jpeg(document, settings);
  CHECK(jpeg_frame_marker(baseline) == 0xC0);
  CHECK(jpeg_frame_marker(optimized) == 0xC0);
  CHECK(jpeg_frame_marker(progressive) == 0xC2);
  // Optimized Huffman tables code the same image in fewer bytes.
  CHECK(optimized.size() < baseline.size());
  for (const auto* bytes : {&baseline, &optimized, &progressive}) {
    const auto image = QImage::fromData(QByteArrayView(reinterpret_cast<const char*>(bytes->data()),
                                                       static_cast<qsizetype>(bytes->size())),
                                        "jpg");
    CHECK(image.width() == 160 && image.height() == 120);
  }
  // Save, Save As and Export Flat Image keep baseline coding (their bytes do not change).
  const patchy::ui::ImageSaveOptions plain;
  CHECK(!plain.jpeg_progressive && !plain.jpeg_optimize && plain.jpeg_exif_thumbnail);
  const auto flat = patchy::ui::encode_flat_image_bytes(document, QStringLiteral("jpg"), plain);
  CHECK(jpeg_frame_marker(flat) == 0xC0);
}

// Palette PNG reads back as an indexed image with the asked-for colors and a transparent
// entry; GIF keeps its color count; the result lists the colors the file uses.
void ui_web_export_palette_png_and_gif_read_back() {
  auto document = textured_document(96, 64);
  {
    // A transparent strip, so the palette needs its tRNS entry.
    auto& pixels = document.layers().front().pixels();
    for (std::int32_t y = 0; y < 64; ++y) {
      for (std::int32_t x = 0; x < 8; ++x) {
        pixels.pixel(x, y)[3] = 0;
      }
    }
  }
  const QSize size(96, 64);
  WebExportSettings settings;
  settings.png_palette = true;
  settings.png_colors = 16;
  settings.png_dither = true;
  settings.gif_colors = 32;
  for (const auto format : {WebExportFormat::Png, WebExportFormat::Gif}) {
    const auto job = patchy::ui::web_export_job(settings, format, size);
    const auto prepared = WebExportPreviewEngine::prepare(document, job.geometry, job.convert_to_srgb, nullptr);
    const auto result = WebExportPreviewEngine::encode(document, job, prepared);
    CHECK(result.error.isEmpty() && result.bytes != nullptr);
    if (result.bytes == nullptr) {
      continue;
    }
    const auto limit = format == WebExportFormat::Png ? 16 : 32;
    const auto image = QImage::fromData(QByteArrayView(reinterpret_cast<const char*>(result.bytes->data()),
                                                       static_cast<qsizetype>(result.bytes->size())),
                                        format == WebExportFormat::Png ? "png" : "gif");
    CHECK(image.size() == size);
    if (format == WebExportFormat::Png) {
      // An indexed PNG with a transparent table entry (tRNS).
      CHECK(image.format() == QImage::Format_Indexed8);
      CHECK(image.colorCount() >= 2 && image.colorCount() <= limit);
      const auto table = image.colorTable();
      CHECK(std::any_of(table.begin(), table.end(), [](QRgb color) { return qAlpha(color) == 0; }));
    }
    // Qt reads a GIF as 32-bit, so count the distinct colors either way.
    const auto argb = image.convertToFormat(QImage::Format_ARGB32);
    std::set<QRgb> distinct;
    for (int y = 0; y < argb.height(); ++y) {
      for (int x = 0; x < argb.width(); ++x) {
        distinct.insert(argb.pixel(x, y));
      }
    }
    CHECK(distinct.size() >= 2 && static_cast<int>(distinct.size()) <= limit);
    CHECK(qAlpha(argb.pixel(2, 10)) == 0);
    CHECK(!result.palette.empty() && static_cast<int>(result.palette.size()) <= limit);
  }
  // Full-color PNG has no palette list and stays a 32-bit image.
  settings.png_palette = false;
  const auto full_job = patchy::ui::web_export_job(settings, WebExportFormat::Png, size);
  const auto prepared = WebExportPreviewEngine::prepare(document, full_job.geometry, full_job.convert_to_srgb, nullptr);
  const auto full = WebExportPreviewEngine::encode(document, full_job, prepared);
  CHECK(full.palette.empty());
  CHECK(full.bytes != nullptr && QImage::fromData(QByteArrayView(reinterpret_cast<const char*>(full.bytes->data()),
                                                                 static_cast<qsizetype>(full.bytes->size())),
                                                  "png")
                                         .format() != QImage::Format_Indexed8);
}

// AVIF and JPEG XL: the bytes Save writes read back at the right size; lossless keeps every
// pixel and lossy does not.
void ui_web_export_avif_and_jxl_read_back() {
  const auto document = textured_document(96, 64);
  const auto source = patchy::ui::qimage_from_document(document, true).convertToFormat(QImage::Format_ARGB32);
  const QSize size(96, 64);
  for (const auto format : {WebExportFormat::Avif, WebExportFormat::Jxl}) {
    for (const bool lossless : {true, false}) {
      WebExportSettings settings;
      settings.avif_lossless = lossless;
      settings.jxl_lossless = lossless;
      settings.avif_quality = 40;
      settings.jxl_quality = 40;
      const auto job = patchy::ui::web_export_job(settings, format, size);
      const auto prepared = WebExportPreviewEngine::prepare(document, job.geometry, job.convert_to_srgb, nullptr);
      const auto result = WebExportPreviewEngine::encode(document, job, prepared);
      CHECK(result.error.isEmpty() && result.bytes != nullptr && !result.bytes->empty());
      if (result.bytes == nullptr) {
        continue;
      }
      const auto path = artifact_dir() + QStringLiteral("/read-back-%1-%2.%3")
                                             .arg(patchy::ui::web_export_format_key(format))
                                             .arg(lossless ? 1 : 0)
                                             .arg(patchy::ui::web_export_extension(format));
      patchy::ui::write_encoded_image_file(path, *result.bytes);
      QFile file(path);
      CHECK(file.open(QIODevice::ReadOnly));
      const auto data = file.readAll();
      const std::vector<std::uint8_t> bytes(reinterpret_cast<const std::uint8_t*>(data.constData()),
                                            reinterpret_cast<const std::uint8_t*>(data.constData()) + data.size());
      CHECK(bytes == *result.bytes);
      const auto decoded = patchy::ui::decode_web_export_bytes(format, bytes).convertToFormat(QImage::Format_ARGB32);
      CHECK(decoded.size() == size);
      CHECK(result.decoded.size() == size);
      bool identical = decoded.size() == source.size();
      for (int y = 0; identical && y < decoded.height(); ++y) {
        for (int x = 0; x < decoded.width(); ++x) {
          if (decoded.pixel(x, y) != source.pixel(x, y)) {
            identical = false;
            break;
          }
        }
      }
      CHECK(identical == lossless);
    }
  }
}

// A slow AVIF encode in the background lane never holds back the selected format, and a
// newer request wins over an older one for the same row.
void ui_web_export_slow_encode_never_blocks_the_selected_format() {
  auto document = std::make_shared<const patchy::Document>(textured_document(1600, 1200));
  WebExportPreviewEngine engine(document);
  std::vector<QString> reported;
  engine.set_result_callback([&reported](const patchy::ui::WebExportJob& job, const patchy::ui::WebExportResult&) {
    reported.push_back(job.signature);
  });
  const QSize size(1600, 1200);
  WebExportSettings slow;
  slow.avif_speed = 3;
  slow.avif_quality = 60;
  WebExportSettings first;
  first.jpeg_quality = 20;
  const auto avif = patchy::ui::web_export_job(slow, WebExportFormat::Avif, size);
  engine.request({patchy::ui::web_export_job(first, WebExportFormat::Jpeg, size), avif});
  WebExportSettings second;
  second.jpeg_quality = 70;
  const auto jpeg = patchy::ui::web_export_job(second, WebExportFormat::Jpeg, size);
  engine.request({jpeg, avif});
  CHECK(wait_until([&] { return engine.cached(jpeg.signature).has_value(); }));
  // The JPEG landed while the AVIF still ran.
  CHECK(!engine.cached(avif.signature).has_value());
  CHECK(wait_until([&engine] { return !engine.busy(); }, 300000));
  CHECK(engine.cached(avif.signature).has_value());
  const auto jpeg_at = std::find(reported.begin(), reported.end(), jpeg.signature);
  const auto avif_at = std::find(reported.begin(), reported.end(), avif.signature);
  CHECK(jpeg_at != reported.end() && avif_at != reported.end() && jpeg_at < avif_at);
  engine.close();
}

void ui_web_export_engine_drops_stale_requests() {
  auto document = std::make_shared<const patchy::Document>(textured_document(320, 240));
  WebExportPreviewEngine engine(document);
  std::vector<QString> reported;
  engine.set_result_callback(
      [&reported](const patchy::ui::WebExportJob& job, const patchy::ui::WebExportResult& result) {
        CHECK(result.error.isEmpty());
        reported.push_back(job.signature);
      });
  const QSize size(320, 240);
  WebExportSettings first;
  first.jpeg_quality = 20;
  std::vector<patchy::ui::WebExportJob> stale;
  for (const auto format : patchy::ui::web_export_formats()) {
    stale.push_back(patchy::ui::web_export_job(first, format, size));
  }
  const auto stale_png = stale.back().signature;
  engine.request(stale);
  // A new request before the worker gets far: the old queue is dropped.
  WebExportSettings second;
  second.jpeg_quality = 90;
  const auto current = patchy::ui::web_export_job(second, WebExportFormat::Jpeg, size);
  engine.request({current});
  CHECK(wait_until([&engine] { return !engine.busy(); }));
  // At most the job already handed to a worker ran besides the current one.
  CHECK(engine.encodes_started() <= 2);
  CHECK(engine.cached(current.signature).has_value());
  CHECK(!engine.cached(stale_png).has_value());
  CHECK(!reported.empty() && reported.back() == current.signature);
  const auto result = engine.cached(current.signature);
  CHECK(result.has_value() && result->bytes != nullptr && !result->bytes->empty());
  CHECK(result.has_value() && result->decoded.size() == QSize(320, 240));
  CHECK(result.has_value() && result->original.size() == QSize(320, 240));
  // An identical request is served from the cache without encoding again.
  const auto before = engine.encodes_started();
  engine.request({current});
  CHECK(wait_until([&engine] { return !engine.busy(); }));
  CHECK(engine.encodes_started() == before);
  engine.close();
}

void ui_web_export_dialog_previews_and_saves() {
  WebExportSettingsGuard guard;
  SettingsValueRestorer last_save_directory_restorer(QStringLiteral("lastSaveDirectory"));
  patchy::ui::MainWindow window;
  show_window(window);
  // The README's CC0 photo, so the captured dialog shows a real picture.
  const auto photo = patchy::test::committed_format_fixture_path("readme", "havana_vintage_car_cc0.jpg");
  MainWindowTestAccess::open_document_path(window, patchy::ui::to_qstring(photo));
  QApplication::processEvents();
  const auto& opened = MainWindowTestAccess::document(window);
  const QSize photo_size(opened.width(), opened.height());
  const auto target = artifact_dir() + QStringLiteral("/web-export-dialog.jpg");
  QFile::remove(target);

  bool inspected = false;
  bool saved_dialog_seen = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("webExportDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    dialog->resize(1280, 820);
    auto* list = dialog->findChild<QTreeWidget*>(QStringLiteral("webExportFormatList"));
    auto* preview = dynamic_cast<patchy::ui::ZoomableImagePreview*>(dialog->findChild<QWidget*>(QStringLiteral("webExportPreview")));
    auto* original = dynamic_cast<patchy::ui::ZoomableImagePreview*>(dialog->findChild<QWidget*>(QStringLiteral("webExportOriginalPreview")));
    auto* quality = dialog->findChild<QSpinBox*>(QStringLiteral("webExportJpegQualitySpin"));
    auto* thumbnail = dialog->findChild<QCheckBox*>(QStringLiteral("webExportThumbnailCheck"));
    auto* info = dialog->findChild<QLabel*>(QStringLiteral("webExportInfoLabel"));
    auto* side_by_side = dialog->findChild<QPushButton*>(QStringLiteral("webExportCompareSideBySide"));
    auto* split = dialog->findChild<QPushButton*>(QStringLiteral("webExportCompareSplit"));
    auto* save = dialog->findChild<QPushButton*>(QStringLiteral("webExportSaveButton"));
    CHECK(list != nullptr && preview != nullptr && original != nullptr && quality != nullptr &&
          thumbnail != nullptr && info != nullptr && side_by_side != nullptr && split != nullptr &&
          save != nullptr);
    if (list == nullptr || preview == nullptr || original == nullptr || quality == nullptr || thumbnail == nullptr ||
        info == nullptr || side_by_side == nullptr || split == nullptr || save == nullptr) {
      dialog->reject();
      return;
    }
    // Defaults: JPEG selected, quality 80, the thumbnail off and disabled (metadata None).
    CHECK(list->topLevelItemCount() == static_cast<int>(patchy::ui::web_export_formats().size()));
    CHECK(list->topLevelItemCount() == 6);
    auto* jpeg_row = row_named(list, QStringLiteral("JPEG"));
    auto* avif_row = row_named(list, QStringLiteral("AVIF"));
    auto* jxl_row = row_named(list, QStringLiteral("JPEG XL"));
    auto* png_row = row_named(list, QStringLiteral("PNG"));
    auto* gif_row = row_named(list, QStringLiteral("GIF"));
    CHECK(jpeg_row != nullptr && avif_row != nullptr && jxl_row != nullptr && png_row != nullptr && gif_row != nullptr);
    if (jpeg_row == nullptr || avif_row == nullptr || jxl_row == nullptr || png_row == nullptr || gif_row == nullptr) {
      dialog->reject();
      return;
    }
    CHECK(list->currentItem() != nullptr && list->currentItem()->text(0) == QStringLiteral("JPEG"));
    CHECK(quality->value() == 80);
    CHECK(!thumbnail->isChecked() && !thumbnail->isEnabled());
    CHECK(!original->isVisible());
    // Every fast row gets its real size; AVIF and JPEG XL wait until their row is selected.
    CHECK(avif_row->text(2) == QStringLiteral("select to encode"));
    const auto all_sized = [list] {
      for (int row = 0; row < list->topLevelItemCount(); ++row) {
        const auto text = list->topLevelItem(row)->text(2);
        if (text != QStringLiteral("select to encode") &&
            (!text.contains(QLatin1Char('B')) || text.endsWith(QStringLiteral("...")))) {
          return false;
        }
      }
      return true;
    };
    CHECK(wait_until(all_sized));
    CHECK(preview->property("webExportShowsCurrent").toBool());
    CHECK(info->text().contains(QStringLiteral("%1 x %2").arg(photo_size.width()).arg(photo_size.height())));
    const auto size_at_80 = jpeg_row->text(2);

    // A lower quality re-encodes after the debounce and changes the size.
    quality->setValue(40);
    CHECK(wait_until([&] { return preview->property("webExportShowsCurrent").toBool() &&
                                  jpeg_row->text(2) != size_at_80; }));
    // The split view at 100% over the car, for the card.
    preview->zoom_to(1.0, QPointF(preview->width() * 0.55, preview->height() * 0.8));
    QApplication::processEvents();
    CHECK(!preview->fit_mode() && preview->property("previewZoomPercent").toInt() == 100);
    capture_widget_artifact("web_export_jpeg_split", *dialog);
    // Results landing later (the other formats) never move the view.
    CHECK(wait_until(all_sized));
    CHECK(!preview->fit_mode());

    // Side by side: the original pane appears and follows the zoom.
    side_by_side->click();
    QApplication::processEvents();
    CHECK(original->isVisible());
    preview->zoom_to(2.0);
    QApplication::processEvents();
    CHECK(original->property("previewZoomPercent").toInt() == 200);
    CHECK(original->property("previewPanOffset") == preview->property("previewPanOffset"));
    CHECK(original->width() == preview->width());
    list->setCurrentItem(png_row);
    CHECK(wait_until([&] { return preview->property("webExportShowsCurrent").toBool(); }));
    CHECK(dialog->findChild<QWidget*>(QStringLiteral("webExportPngGroup"))->isVisible());
    CHECK(!thumbnail->isVisible());
    // Palette PNG: 32 colors with diffusion; the swatches show the colors the file uses.
    auto* png_colors = dialog->findChild<QComboBox*>(QStringLiteral("webExportPngColorsCombo"));
    auto* png_count = dialog->findChild<QSpinBox*>(QStringLiteral("webExportPngColorsSpin"));
    auto* png_dither = dialog->findChild<QComboBox*>(QStringLiteral("webExportPngDitherCombo"));
    auto* png_swatches = dialog->findChild<QWidget*>(QStringLiteral("webExportPngPaletteSwatches"));
    CHECK(png_colors != nullptr && png_count != nullptr && png_dither != nullptr && png_swatches != nullptr);
    if (png_colors != nullptr && png_count != nullptr && png_dither != nullptr && png_swatches != nullptr) {
      CHECK(!png_count->isVisible());
      png_colors->setCurrentIndex(1);
      png_count->setValue(32);
      png_dither->setCurrentIndex(1);
      QApplication::processEvents();
      CHECK(png_count->isVisible());
      CHECK(wait_until([&] { return preview->property("webExportShowsCurrent").toBool(); }));
      const auto used = png_swatches->property("webExportPaletteCount").toInt();
      CHECK(used > 1 && used <= 32);
      CHECK(png_row->text(1).contains(QStringLiteral("32")));
      capture_widget_artifact("web_export_png_palette", *dialog);
    }

    // GIF at 64 colors with diffusion, side by side.
    list->setCurrentItem(gif_row);
    auto* gif_count = dialog->findChild<QSpinBox*>(QStringLiteral("webExportGifColorsSpin"));
    auto* gif_dither = dialog->findChild<QComboBox*>(QStringLiteral("webExportGifDitherCombo"));
    auto* gif_swatches = dialog->findChild<QWidget*>(QStringLiteral("webExportGifPaletteSwatches"));
    CHECK(gif_count != nullptr && gif_dither != nullptr && gif_swatches != nullptr);
    if (gif_count != nullptr && gif_dither != nullptr && gif_swatches != nullptr) {
      CHECK(dialog->findChild<QWidget*>(QStringLiteral("webExportGifGroup"))->isVisible());
      CHECK(gif_count->value() == 256 && gif_dither->currentIndex() == 0);
      gif_count->setValue(64);
      gif_dither->setCurrentIndex(1);
      CHECK(wait_until([&] { return preview->property("webExportShowsCurrent").toBool(); }));
      const auto used = gif_swatches->property("webExportPaletteCount").toInt();
      CHECK(used > 1 && used <= 64);
      CHECK(gif_row->text(1).contains(QStringLiteral("64")));
      CHECK(gif_row->text(2).contains(QLatin1Char('B')));
      capture_widget_artifact("web_export_gif_side_by_side", *dialog);
    }

    // AVIF and JPEG XL encode once selected; while a newer setting encodes, the last
    // result stays on screen and the row keeps its last size.
    for (auto* slow_row : {avif_row, jxl_row}) {
      list->setCurrentItem(slow_row);
      CHECK(wait_until([&] { return preview->property("webExportShowsCurrent").toBool(); }, 120000));
      CHECK(slow_row->text(2).contains(QLatin1Char('B')) && !slow_row->text(2).endsWith(QStringLiteral("...")));
      const auto shown = preview->property("webExportResultKey").toLongLong();
      CHECK(shown != 0);
      auto* slow_quality = dialog->findChild<QSpinBox*>(slow_row == avif_row
                                                           ? QStringLiteral("webExportAvifQualitySpin")
                                                           : QStringLiteral("webExportJxlQualitySpin"));
      CHECK(slow_quality != nullptr);
      if (slow_quality == nullptr) {
        continue;
      }
      slow_quality->setValue(slow_quality->value() - 30);
      QApplication::processEvents();
      CHECK(!preview->property("webExportShowsCurrent").toBool());
      CHECK(preview->property("webExportResultKey").toLongLong() == shown);
      CHECK(slow_row->text(2).endsWith(QStringLiteral(" ...")));
      CHECK(wait_until([&] { return preview->property("webExportShowsCurrent").toBool(); }, 120000));
      CHECK(preview->property("webExportResultKey").toLongLong() != shown);
      capture_widget_artifact(slow_row == avif_row ? "web_export_avif" : "web_export_jxl", *dialog);
    }

    // Back to JPEG in split view, then Save: the file dialog takes the name.
    list->setCurrentItem(jpeg_row);
    split->click();
    preview->zoom_to_fit();
    CHECK(wait_until([&] { return preview->property("webExportShowsCurrent").toBool(); }));
    inspected = true;
    auto* file_dialog_timer = new QTimer(&window);
    file_dialog_timer->setInterval(20);
    QObject::connect(file_dialog_timer, &QTimer::timeout, &window, [&, file_dialog_timer] {
      auto* file_dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("exportForWebFileDialog")));
      if (file_dialog == nullptr) {
        return;
      }
      file_dialog_timer->stop();
      file_dialog_timer->deleteLater();
      saved_dialog_seen = true;
      file_dialog->setDirectory(QFileInfo(target).absolutePath());
      file_dialog->selectFile(target);
      static_cast<QDialog*>(file_dialog)->accept();
    });
    file_dialog_timer->start();
    save->click();
  });
  patchy::test::ui::require_action(window, "fileExportForWebAction")->trigger();
  QApplication::processEvents();
  CHECK(inspected);
  CHECK(saved_dialog_seen);
  CHECK(QFileInfo(target).isFile());
  QFile file(target);
  CHECK(file.open(QIODevice::ReadOnly));
  const auto bytes = file.readAll();
  const std::vector<std::uint8_t> data(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                       reinterpret_cast<const std::uint8_t*>(bytes.constData()) + bytes.size());
  // Progressive by default and no metadata at all (no APP1 segment).
  CHECK(jpeg_frame_marker(data) == 0xC2);
  CHECK(!bytes.contains(QByteArray("Exif\0\0", 6)));
  CHECK(QImage(target).size() == photo_size);
  // The dialog remembered the format and quality for next time.
  const auto remembered = patchy::ui::load_web_export_settings();
  CHECK(remembered.format == WebExportFormat::Jpeg && remembered.jpeg_quality == 40);
}

}  // namespace

std::vector<patchy::test::TestCase> web_export_tests() {
  return {
      {"ui_web_export_defaults_and_options", ui_web_export_defaults_and_options},
      {"ui_web_export_jpeg_progressive_and_optimize_read_back", ui_web_export_jpeg_progressive_and_optimize_read_back},
      {"ui_web_export_engine_drops_stale_requests", ui_web_export_engine_drops_stale_requests},
      {"ui_web_export_avif_and_jxl_read_back", ui_web_export_avif_and_jxl_read_back},
      {"ui_web_export_slow_encode_never_blocks_the_selected_format",
       ui_web_export_slow_encode_never_blocks_the_selected_format},
      {"ui_web_export_palette_png_and_gif_read_back", ui_web_export_palette_png_and_gif_read_back},
      {"ui_web_export_dialog_previews_and_saves", ui_web_export_dialog_previews_and_saves},
  };
}
