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
#include "ui/apng_reader.hpp"
#include "formats/jxl_document_io.hpp"
#include "formats/webp_animation_io.hpp"
#include "ui/image_save_options_dialog.hpp"
#include "ui/web_export.hpp"
#include "ui/zoomable_image_preview.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QAbstractItemView>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QRadioButton>
#include <QScrollArea>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollBar>
#include <QSpinBox>
#include <QSpontaneKeyEvent>
#include <QWheelEvent>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>

#include <algorithm>
#include <string_view>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <utility>
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
                            "jxlEffort", "jxlLossless", "jxlAnimate", "jxlFrameDelayMs", "pngAnimate",
                            "pngFrameDelayMs", "fillTransparent", "dialogSize", "presets"}) {
      const auto full = QStringLiteral("exportForWeb/") + QLatin1String(key);
      restorers.push_back(std::make_unique<SettingsValueRestorer>(full));
      auto settings = patchy::ui::app_settings();
      settings.remove(full);
    }
    // The default frame delay shared with the Layers as Animated exports.
    for (const auto* key : {"saveOptions/animationFrameDelayMs", "saveOptions/gifFrameDelayCs"}) {
      restorers.push_back(std::make_unique<SettingsValueRestorer>(QLatin1String(key)));
      auto settings = patchy::ui::app_settings();
      settings.remove(QLatin1String(key));
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

// The Animation Preview test document (#198), made by Patchy: five frame layers, bottom to
// top "Frame 5 1s", "Frame 4 hidden" (hidden), "Frame 3", "Frame 2 0.5s", "Frame 1". Exported
// animations hold the four visible ones, top first: 0.1 s (the default), 0.5 s, 0.1 s, 1 s.
patchy::Document animation_fixture_document(patchy::ui::MainWindow& window) {
  const auto path = patchy::test::committed_format_fixture_path("psd", "web-export-animation.psd");
  MainWindowTestAccess::open_document_path(window, patchy::ui::to_qstring(path));
  QApplication::processEvents();
  return MainWindowTestAccess::document(window);
}

const std::vector<int> kAnimationFixtureDelaysMs = {100, 500, 100, 1000};

// An animated GIF, WebP and JPEG XL result decodes every frame from the encoded bytes with
// the delays as written (layer-name times included) and carries the source frames beside them.
void ui_web_export_animation_frames_read_back() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto document = animation_fixture_document(window);
  CHECK(document.layers().size() == 5);
  const QSize size(document.width(), document.height());
  WebExportSettings settings;
  settings.gif_animate = true;
  settings.webp_animate = true;
  settings.jxl_animate = true;
  settings.webp_lossless = true;
  settings.jxl_lossless = true;
  settings.png_animate = true;
  // The palette choice yields to the animation: an APNG is full color.
  settings.png_palette = true;
  for (const auto format : {WebExportFormat::Gif, WebExportFormat::Webp, WebExportFormat::Jxl, WebExportFormat::Png}) {
    const auto job = patchy::ui::web_export_job(settings, format, size);
    CHECK(job.animate);
    const auto result = WebExportPreviewEngine::encode(document, job, nullptr);
    CHECK(result.error.isEmpty() && result.bytes != nullptr);
    CHECK(result.frames.size() == 4 && result.original_frames.size() == 4 && result.frames_total == 4);
    CHECK(result.frame_delays_ms == kAnimationFixtureDelaysMs);
    if (result.frames.size() == 4 && result.original_frames.size() == 4) {
      CHECK(result.frames[0].size() == size && result.original_frames[0].size() == size);
      // The frames differ from one another, and the first one is the shown result.
      CHECK(result.frames[0] != result.frames[1]);
      CHECK(result.decoded == result.frames[0]);
    }
    // A cap keeps the first frames and still reports the file's count.
    const auto capped = patchy::ui::decode_web_export_animation(format, *result.bytes, 2);
    CHECK(capped.frames.size() == 2 && capped.total == 4);
  }
  // A still format carries no frames.
  const auto still = WebExportPreviewEngine::encode(
      document, patchy::ui::web_export_job(settings, WebExportFormat::Jpeg, size), nullptr);
  CHECK(still.frames.empty() && still.frame_delays_ms.empty());
}

// A newer animated request replaces an older one: the older job is dropped before it encodes
// or, when it already ran, its frames are never the current ones.
// A still PNG from Export for Web is the plain PNG writer's output, never an APNG.
void ui_web_export_still_png_is_unchanged_by_animation() {
  const auto document = textured_document(64, 48);
  const QSize size(64, 48);
  WebExportSettings settings;
  const auto job = patchy::ui::web_export_job(settings, WebExportFormat::Png, size);
  CHECK(!job.animate && !job.options.apng_animate);
  const auto prepared = WebExportPreviewEngine::prepare(document, job.geometry, job.convert_to_srgb, nullptr);
  const auto result = WebExportPreviewEngine::encode(document, job, prepared);
  CHECK(result.bytes != nullptr && result.frames.empty());
  if (result.bytes == nullptr) {
    return;
  }
  CHECK(!patchy::apng::is_animated_png(*result.bytes));
  patchy::ui::ImageSaveOptions plain;
  plain.metadata_policy = patchy::metadata::MetadataPolicy::None;
  CHECK(*result.bytes == patchy::ui::encode_flat_image_bytes(document, QStringLiteral("png"), plain));
  // Ticking animation on a one-layer document offers nothing: the dialog hides the box and
  // a script's animate on a single layer still writes one frame.
  settings.png_animate = true;
  CHECK(patchy::ui::web_export_job(settings, WebExportFormat::Png, size).signature != job.signature);
}

void ui_web_export_animation_stale_frames_are_discarded() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto document = std::make_shared<const patchy::Document>(animation_fixture_document(window));
  const QSize size(document->width(), document->height());
  WebExportPreviewEngine engine(document);
  std::vector<QString> reported;
  engine.set_result_callback([&reported](const patchy::ui::WebExportJob& job, const patchy::ui::WebExportResult&) {
    reported.push_back(job.signature);
  });
  WebExportSettings first;
  first.gif_animate = true;
  first.animation_frame_delay_ms = 300;
  WebExportSettings second = first;
  second.animation_frame_delay_ms = 700;
  const auto old_job = patchy::ui::web_export_job(first, WebExportFormat::Gif, size);
  const auto new_job = patchy::ui::web_export_job(second, WebExportFormat::Gif, size);
  engine.request({old_job});
  engine.request({new_job});
  CHECK(wait_until([&engine] { return !engine.busy(); }));
  CHECK(engine.encodes_started() <= 2);
  CHECK(!reported.empty() && reported.back() == new_job.signature);
  const auto current = engine.cached(new_job.signature);
  CHECK(current.has_value());
  if (current.has_value()) {
    // The frames without a time token take the newer default delay.
    CHECK(current->frame_delays_ms == (std::vector<int>{700, 500, 700, 1000}));
  }
  engine.close();
}

// A one-layer document, opaque on the left half and fully transparent on the right.
patchy::Document half_transparent_document(int width, int height) {
  QImage image(width, height, QImage::Format_RGBA8888);
  image.fill(Qt::transparent);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width / 2; ++x) {
      image.setPixelColor(x, y, QColor(20, 90, 200, 255));
    }
  }
  return patchy::ui::document_from_qimage(image, "Half");
}

bool has_transparency(const QImage& image) {
  const auto rgba = image.convertToFormat(QImage::Format_RGBA8888);
  for (int y = 0; y < rgba.height(); ++y) {
    for (int x = 0; x < rgba.width(); ++x) {
      if (rgba.pixelColor(x, y).alpha() < 255) {
        return true;
      }
    }
  }
  return false;
}

// The Animation group's play count reaches WebP, JPEG XL and APNG files; the shared default
// delay persists in the Layers as Animated exports' keys; Fill with removes alpha from a
// still PNG and WebP and from every animated GIF frame, in the fill color.
void ui_web_export_play_count_and_fill_read_back() {
  WebExportSettingsGuard guard;
  patchy::ui::MainWindow window;
  show_window(window);
  const auto document = animation_fixture_document(window);
  const QSize size(document.width(), document.height());
  WebExportSettings settings;
  settings.webp_animate = true;
  settings.jxl_animate = true;
  settings.png_animate = true;
  settings.webp_lossless = true;
  settings.jxl_lossless = true;
  settings.webp_loop_count = 3;
  settings.jxl_loop_count = 4;
  settings.apng_loop_count = 5;
  {
    const auto job = patchy::ui::web_export_job(settings, WebExportFormat::Webp, size);
    CHECK(job.options.webp_loop_count == 3);
    const auto result = WebExportPreviewEngine::encode(document, job, nullptr);
    CHECK(result.bytes != nullptr && result.frame_delays_ms == kAnimationFixtureDelaysMs);
    int loops = -1;
    if (result.bytes != nullptr) {
      (void)patchy::webp::decode_animation(
          *result.bytes, std::uint64_t{1} << 30, [&loops](const patchy::webp::AnimationInfo& info) { loops = info.loop_count; },
          [](patchy::PixelBuffer, std::uint32_t) {});
    }
    CHECK(loops == 3);
  }
  {
    const auto job = patchy::ui::web_export_job(settings, WebExportFormat::Jxl, size);
    const auto result = WebExportPreviewEngine::encode(document, job, nullptr);
    CHECK(result.bytes != nullptr && result.frame_delays_ms == kAnimationFixtureDelaysMs);
    if (result.bytes != nullptr) {
      const auto read = patchy::jxl::read_jxl(*result.bytes);
      const auto& values = read.document.metadata().values;
      const auto found = values.find(patchy::jxl::kLoopCountMetadata);
      CHECK(found != values.end() && found->second == "4");
    }
  }
  {
    const auto job = patchy::ui::web_export_job(settings, WebExportFormat::Png, size);
    const auto result = WebExportPreviewEngine::encode(document, job, nullptr);
    CHECK(result.bytes != nullptr && result.frame_delays_ms == kAnimationFixtureDelaysMs);
    if (result.bytes != nullptr) {
      CHECK(patchy::apng::read_animated_png(*result.bytes).play_count == 5);
    }
  }
  // A play count is part of the job: a change encodes again.
  auto other_count = settings;
  other_count.apng_loop_count = 0;
  CHECK(patchy::ui::web_export_job(other_count, WebExportFormat::Png, size).signature !=
        patchy::ui::web_export_job(settings, WebExportFormat::Png, size).signature);

  // The default delay is the animated exports' shared one, GIF in centiseconds.
  settings.animation_frame_delay_ms = 250;
  patchy::ui::save_web_export_settings(settings);
  {
    auto stored = patchy::ui::app_settings();
    CHECK(stored.value(QStringLiteral("saveOptions/animationFrameDelayMs")).toInt() == 250);
    CHECK(stored.value(QStringLiteral("saveOptions/gifFrameDelayCs")).toInt() == 25);
  }
  CHECK(patchy::ui::load_web_export_settings().animation_frame_delay_ms == 250);
  CHECK(patchy::ui::load_image_save_option_defaults().animation_frame_delay_ms == 250);

  // Fill with: still PNG and WebP lose their alpha and take the color.
  const auto half = half_transparent_document(64, 32);
  const QSize half_size(64, 32);
  WebExportSettings fill;
  fill.fill_transparent = true;
  fill.jpeg_background = QColor(255, 0, 0);
  fill.webp_lossless = true;
  WebExportSettings keep = fill;
  keep.fill_transparent = false;
  for (const auto format : {WebExportFormat::Png, WebExportFormat::Webp}) {
    const auto filled_job = patchy::ui::web_export_job(fill, format, half_size);
    const auto kept_job = patchy::ui::web_export_job(keep, format, half_size);
    CHECK(filled_job.options.export_fill_transparent && !kept_job.options.export_fill_transparent);
    CHECK(filled_job.signature != kept_job.signature);
    const auto filled_prepared =
        WebExportPreviewEngine::prepare(half, filled_job.geometry, filled_job.convert_to_srgb, nullptr);
    const auto filled = WebExportPreviewEngine::encode(half, filled_job, filled_prepared);
    const auto kept_prepared =
        WebExportPreviewEngine::prepare(half, kept_job.geometry, kept_job.convert_to_srgb, nullptr);
    const auto kept = WebExportPreviewEngine::encode(half, kept_job, kept_prepared);
    CHECK(!filled.decoded.isNull() && !kept.decoded.isNull());
    CHECK(has_transparency(kept.decoded));
    CHECK(!has_transparency(filled.decoded));
    CHECK(filled.decoded.pixelColor(60, 10) == QColor(255, 0, 0));
    CHECK(filled.decoded.pixelColor(4, 10) == QColor(20, 90, 200));
  }
  // An animated GIF fills every frame; the Original pane keeps the unfilled frames. Two
  // half-transparent frame layers (the fixture's frames are opaque).
  auto two_frames = half_transparent_document(64, 32);
  {
    const auto& first_layer = std::as_const(two_frames).layers().front();
    patchy::Layer second(two_frames.allocate_layer_id(), "Frame 2 0.3s", first_layer.pixels());
    two_frames.add_layer(std::move(second));
  }
  WebExportSettings gif_keep;
  gif_keep.gif_animate = true;
  auto gif_fill = gif_keep;
  gif_fill.fill_transparent = true;
  gif_fill.jpeg_background = QColor(255, 0, 0);
  const auto gif_kept = WebExportPreviewEngine::encode(
      two_frames, patchy::ui::web_export_job(gif_keep, WebExportFormat::Gif, half_size), nullptr);
  const auto gif_filled = WebExportPreviewEngine::encode(
      two_frames, patchy::ui::web_export_job(gif_fill, WebExportFormat::Gif, half_size), nullptr);
  CHECK(gif_kept.frames.size() == 2 && gif_filled.frames.size() == 2);
  CHECK(std::all_of(gif_kept.frames.begin(), gif_kept.frames.end(), has_transparency));
  CHECK(std::none_of(gif_filled.frames.begin(), gif_filled.frames.end(), has_transparency));
  CHECK(std::all_of(gif_filled.original_frames.begin(), gif_filled.original_frames.end(), has_transparency));
  CHECK(gif_filled.frames.size() == 2 && gif_filled.frames[1].pixelColor(60, 10) == QColor(255, 0, 0));
  CHECK(gif_filled.frame_delays_ms == (std::vector<int>{300, 100}));
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
  // Besides the current one, at most the job each of the two lanes had already handed to its
  // worker ran (docs/web-export.md); a fast host starts both before the new request lands.
  CHECK(engine.encodes_started() <= 3);
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

// Animated GIF, WebP and JPEG XL play in the preview with Play/Pause, frame steps, the
// frame counter and Loop; a still format shows no controls; closing stops the timer.
void ui_web_export_animation_plays_in_the_preview() {
  WebExportSettingsGuard guard;
  patchy::ui::MainWindow window;
  show_window(window);
  (void)animation_fixture_document(window);
  bool inspected = false;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("webExportDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    dialog->resize(1280, 820);
    auto* list = dialog->findChild<QTreeWidget*>(QStringLiteral("webExportFormatList"));
    auto* preview = dynamic_cast<patchy::ui::ZoomableImagePreview*>(
        dialog->findChild<QWidget*>(QStringLiteral("webExportPreview")));
    auto* controls = dialog->findChild<QWidget*>(QStringLiteral("webExportPlaybackControls"));
    auto* play = dialog->findChild<QPushButton*>(QStringLiteral("webExportPlayButton"));
    auto* next = dialog->findChild<QAbstractButton*>(QStringLiteral("webExportNextFrameButton"));
    auto* previous = dialog->findChild<QAbstractButton*>(QStringLiteral("webExportPreviousFrameButton"));
    auto* frame_label = dialog->findChild<QLabel*>(QStringLiteral("webExportFrameLabel"));
    auto* timer = dialog->findChild<QTimer*>(QStringLiteral("webExportPlaybackTimer"));
    auto* side_by_side = dialog->findChild<QPushButton*>(QStringLiteral("webExportCompareSideBySide"));
    auto* info = dialog->findChild<QLabel*>(QStringLiteral("webExportInfoLabel"));
    CHECK(list != nullptr && preview != nullptr && controls != nullptr && play != nullptr && next != nullptr &&
          previous != nullptr && frame_label != nullptr && timer != nullptr && side_by_side != nullptr &&
          info != nullptr);
    if (list == nullptr || preview == nullptr || controls == nullptr || play == nullptr || next == nullptr ||
        previous == nullptr || frame_label == nullptr || timer == nullptr || side_by_side == nullptr ||
        info == nullptr) {
      dialog->reject();
      return;
    }
    const auto frame = [preview] { return preview->property("webExportFrameIndex").toInt(); };
    const auto frames = [preview] { return preview->property("webExportFrameCount").toInt(); };
    // A still format: no controls.
    CHECK(wait_until([&] { return preview->property("webExportShowsCurrent").toBool(); }));
    CHECK(!controls->isVisible());
    CHECK(frames() == 0);

    struct Case {
      const char* row;
      const char* animate;
    };
    for (const auto& item : {Case{"GIF", "webExportGifAnimateCheck"}, Case{"WebP", "webExportWebpAnimateCheck"},
                              Case{"JPEG XL", "webExportJxlAnimateCheck"}, Case{"PNG", "webExportPngAnimateCheck"}}) {
      auto* row = row_named(list, QString::fromLatin1(item.row));
      auto* animate = dialog->findChild<QCheckBox*>(QLatin1String(item.animate));
      CHECK(row != nullptr && animate != nullptr);
      if (row == nullptr || animate == nullptr) {
        continue;
      }
      list->setCurrentItem(row);
      CHECK(animate->isVisible());
      animate->setChecked(true);
      // One Animation group with the format's own timing rows.
      auto* animation_group = dialog->findChild<QWidget*>(QStringLiteral("webExportAnimationGroup"));
      CHECK(animation_group != nullptr && animation_group->isVisible());
      CHECK(animation_group != nullptr && animation_group->isAncestorOf(animate));
      if (QLatin1String(item.row) == QLatin1String("PNG")) {
        // An animated PNG is full color: Type and the palette controls rest, with a note.
        auto* type = dialog->findChild<QComboBox*>(QStringLiteral("webExportPngColorsCombo"));
        auto* palette_box = dialog->findChild<QWidget*>(QStringLiteral("webExportPngPaletteBox"));
        auto* note = dialog->findChild<QLabel*>(QStringLiteral("webExportPngAnimationNote"));
        CHECK(type != nullptr && palette_box != nullptr && note != nullptr);
        if (type != nullptr && palette_box != nullptr && note != nullptr) {
          CHECK(!type->isEnabled() && !palette_box->isEnabled() && note->isVisible());
        }
      }
      CHECK(wait_until([&] { return preview->property("webExportShowsCurrent").toBool() && frames() == 4; }, 120000));
      CHECK(controls->isVisible());
      CHECK(timer->isActive() && preview->property("webExportPlaying").toBool());
      CHECK(info->text().contains(QStringLiteral("4 frames")));
      // It plays: the shown frame moves on by itself.
      const auto start = frame();
      CHECK(wait_until([&] { return frame() != start; }, 5000));
      // Pause, then step both ways.
      play->click();
      CHECK(!timer->isActive() && !preview->property("webExportPlaying").toBool());
      CHECK(play->text() == QStringLiteral("Play"));
      const auto paused = frame();
      next->click();
      CHECK(frame() == (paused + 1) % 4);
      previous->click();
      previous->click();
      CHECK(frame() == (paused + 3) % 4);
      CHECK(frame_label->text().contains(QStringLiteral("of 4")));
      if (QLatin1String(item.row) == QLatin1String("GIF")) {
        side_by_side->click();
        // Frame 2 shows for 0.5 s (its layer name's time).
        while (frame() != 1) {
          next->click();
        }
        CHECK(frame_label->text().contains(QStringLiteral("Frame 2 of 4")));
        CHECK(frame_label->text().contains(QStringLiteral("0.50")));
        play->click();
        CHECK(timer->isActive());
        capture_widget_artifact("web_export_gif_animation", *dialog);
      } else if (QLatin1String(item.row) == QLatin1String("PNG")) {
        // "Default frame delay:" (layer-name times win) and Forever / Play count, as in the
        // Layers as Animated PNG dialog; the delay is one value across the formats.
        auto* delay = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("webExportPngFrameDelaySpin"));
        auto* gif_delay = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("webExportGifFrameDelaySpin"));
        auto* forever_check = dialog->findChild<QCheckBox*>(QStringLiteral("webExportPngForeverCheck"));
        auto* plays = dialog->findChild<QSpinBox*>(QStringLiteral("webExportPngPlayCountSpin"));
        auto* explanation = dialog->findChild<QLabel*>(QStringLiteral("webExportPngAnimationExplanationLabel"));
        CHECK(delay != nullptr && gif_delay != nullptr && forever_check != nullptr && plays != nullptr &&
              explanation != nullptr);
        if (delay != nullptr && gif_delay != nullptr && forever_check != nullptr && plays != nullptr &&
            explanation != nullptr) {
          CHECK(delay->isVisible() && forever_check->isVisible() && explanation->isVisible());
          CHECK(!gif_delay->isVisible());
          auto* form = qobject_cast<QFormLayout*>(delay->parentWidget()->layout());
          auto* label = form != nullptr ? qobject_cast<QLabel*>(form->labelForField(delay)) : nullptr;
          CHECK(label != nullptr && label->text() == QStringLiteral("Default frame delay:"));
          CHECK(forever_check->isChecked() && !plays->isEnabled());
          delay->setValue(0.2);
          CHECK(std::abs(gif_delay->value() - 0.2) < 1e-6);
          forever_check->setChecked(false);
          plays->setValue(3);
          CHECK(wait_until([&] { return preview->property("webExportShowsCurrent").toBool() && frames() == 4; },
                           120000));
          // The named frames keep their times; the others take the new default.
          next->click();
          while (frame() != 0) {
            next->click();
          }
          CHECK(frame_label->text().contains(QStringLiteral("0.20")));
        }
        while (frame() != 1) {
          next->click();
        }
        play->click();
        CHECK(timer->isActive());
        capture_widget_artifact("web_export_png_animation", *dialog);
        // Transparency: Fill with the background color, every frame.
        auto* fill = dialog->findChild<QRadioButton*>(QStringLiteral("webExportFillTransparencyRadio"));
        auto* keep = dialog->findChild<QRadioButton*>(QStringLiteral("webExportKeepTransparencyRadio"));
        auto* swatch = dialog->findChild<QPushButton*>(QStringLiteral("webExportBackgroundColorSwatch"));
        auto* transparency = dialog->findChild<QWidget*>(QStringLiteral("webExportTransparencyGroup"));
        auto* scroll = dialog->findChild<QScrollArea*>(QStringLiteral("webExportControlsScroll"));
        CHECK(fill != nullptr && keep != nullptr && swatch != nullptr && transparency != nullptr && scroll != nullptr);
        if (fill != nullptr && keep != nullptr && swatch != nullptr && transparency != nullptr && scroll != nullptr) {
          CHECK(transparency->isVisible() && keep->isChecked() && keep->isEnabled() && !swatch->isEnabled());
          fill->setChecked(true);
          CHECK(swatch->isEnabled());
          CHECK(wait_until([&] { return preview->property("webExportShowsCurrent").toBool() && frames() == 4; },
                           120000));
          keep->setChecked(true);
          scroll->ensureWidgetVisible(animation_group);
        }
      } else {
        play->click();
        CHECK(timer->isActive());
      }
    }
    // Back to a still format: the controls hide and playback stops.
    list->setCurrentItem(row_named(list, QStringLiteral("JPEG")));
    QApplication::processEvents();
    CHECK(!controls->isVisible() && !timer->isActive());
    // JPEG has no alpha: no Animation group, and the Transparency group shows only the color.
    {
      auto* animation_group = dialog->findChild<QWidget*>(QStringLiteral("webExportAnimationGroup"));
      auto* keep = dialog->findChild<QRadioButton*>(QStringLiteral("webExportKeepTransparencyRadio"));
      auto* swatch = dialog->findChild<QPushButton*>(QStringLiteral("webExportBackgroundColorSwatch"));
      auto* hint = dialog->findChild<QToolButton*>(QStringLiteral("webExportJpegBackgroundInfo"));
      CHECK(animation_group != nullptr && keep != nullptr && swatch != nullptr && hint != nullptr);
      if (animation_group != nullptr && keep != nullptr && swatch != nullptr && hint != nullptr) {
        CHECK(!animation_group->isVisible());
        // Keep transparent stays in sight, disabled, with Fill with selected (GitHub issue 571).
        auto* fill = dialog->findChild<QRadioButton*>(QStringLiteral("webExportFillTransparencyRadio"));
        CHECK(fill != nullptr && fill->isVisible() && fill->isChecked());
        CHECK(keep->isVisible() && !keep->isEnabled() && swatch->isVisible() && swatch->isEnabled() &&
              hint->isVisible());
        // The explanation is the info mark's tooltip.
        CHECK(hint->toolTip() == QStringLiteral("Fills transparent areas (JPEG has no transparency)"));
        auto* label = dialog->findChild<QLabel*>(QStringLiteral("webExportBackgroundLabel"));
        CHECK(label != nullptr && !label->isVisible());
      }
    }
    // Closing stops the timer.
    list->setCurrentItem(row_named(list, QStringLiteral("GIF")));
    CHECK(wait_until([&] { return timer->isActive(); }));
    inspected = true;
    dialog->reject();
    CHECK(!timer->isActive());
  }));
  patchy::test::ui::require_action(window, "fileExportForWebAction")->trigger();
  QApplication::processEvents();
  CHECK(inspected);
}

// 4-up with an animation: the two lower tiles (the format at 50% and 25%) play and step with
// the main result, each showing its own cached frame at the same index (desk card local-034).
// An animated PNG has nothing to reduce, so its tiles keep the note. Captures
// web_export_quad_animation at frame 3 of 4.
void ui_web_export_quad_tiles_follow_the_animation_frame() {
  WebExportSettingsGuard guard;
  patchy::ui::MainWindow window;
  show_window(window);
  (void)animation_fixture_document(window);
  bool inspected = false;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("webExportDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    dialog->resize(1280, 820);
    auto* list = dialog->findChild<QTreeWidget*>(QStringLiteral("webExportFormatList"));
    auto* preview = dialog->findChild<QWidget*>(QStringLiteral("webExportPreview"));
    auto* half = dialog->findChild<QWidget*>(QStringLiteral("webExportHalfQualityPreview"));
    auto* quarter = dialog->findChild<QWidget*>(QStringLiteral("webExportQuarterQualityPreview"));
    auto* quad = dialog->findChild<QPushButton*>(QStringLiteral("webExportCompareQuad"));
    auto* play = dialog->findChild<QPushButton*>(QStringLiteral("webExportPlayButton"));
    auto* next = dialog->findChild<QAbstractButton*>(QStringLiteral("webExportNextFrameButton"));
    auto* previous = dialog->findChild<QAbstractButton*>(QStringLiteral("webExportPreviousFrameButton"));
    auto* timer = dialog->findChild<QTimer*>(QStringLiteral("webExportPlaybackTimer"));
    CHECK(list != nullptr && preview != nullptr && half != nullptr && quarter != nullptr && quad != nullptr &&
          play != nullptr && next != nullptr && previous != nullptr && timer != nullptr);
    if (list == nullptr || preview == nullptr || half == nullptr || quarter == nullptr || quad == nullptr ||
        play == nullptr || next == nullptr || previous == nullptr || timer == nullptr) {
      dialog->reject();
      return;
    }
    quad->click();
    QApplication::processEvents();
    CHECK(half->isVisible() && quarter->isVisible());
    const auto frame = [preview] { return preview->property("webExportFrameIndex").toInt(); };
    const auto frames = [preview] { return preview->property("webExportFrameCount").toInt(); };
    const auto tile_frame = [](QWidget* tile) {
      const auto value = tile->property("webExportFrameIndex");
      return value.isValid() ? value.toInt() : -1;
    };
    const auto tile_key = [](QWidget* tile) { return tile->property("webExportResultKey").toLongLong(); };
    const auto tiles_current = [&] {
      return preview->property("webExportShowsCurrent").toBool() && frames() == 4 &&
             half->property("webExportShowsCurrent").toBool() && quarter->property("webExportShowsCurrent").toBool();
    };
    struct Case {
      const char* row;
      const char* animate;
    };
    for (const auto& item : {Case{"GIF", "webExportGifAnimateCheck"}, Case{"WebP", "webExportWebpAnimateCheck"}}) {
      auto* row = row_named(list, QString::fromLatin1(item.row));
      auto* animate = dialog->findChild<QCheckBox*>(QLatin1String(item.animate));
      CHECK(row != nullptr && animate != nullptr);
      if (row == nullptr || animate == nullptr) {
        continue;
      }
      list->setCurrentItem(row);
      animate->setChecked(true);
      CHECK(wait_until(tiles_current, 120000));
      CHECK(wait_until([&] { return tile_frame(half) >= 0 && tile_frame(quarter) >= 0; }, 5000));
      // Playing: the tiles keep the main result's frame as it moves on.
      CHECK(timer->isActive());
      const auto start = frame();
      CHECK(wait_until([&] { return frame() != start; }, 5000));
      CHECK(tile_frame(half) == frame() && tile_frame(quarter) == frame());
      // Paused, then stepped: each step shows the tiles' own frame at that index.
      play->click();
      CHECK(!timer->isActive());
      while (frame() != 0) {
        next->click();
      }
      std::vector<qint64> half_keys;
      std::vector<qint64> quarter_keys;
      for (int index = 0; index < 4; ++index) {
        CHECK(frame() == index);
        CHECK(tile_frame(half) == index && tile_frame(quarter) == index);
        half_keys.push_back(tile_key(half));
        quarter_keys.push_back(tile_key(quarter));
        next->click();
      }
      // Four different frames per tile, and wrapping round shows frame 1 again.
      CHECK(std::set<qint64>(half_keys.begin(), half_keys.end()).size() == 4);
      CHECK(std::set<qint64>(quarter_keys.begin(), quarter_keys.end()).size() == 4);
      CHECK(frame() == 0 && tile_key(half) == half_keys[0] && tile_key(quarter) == quarter_keys[0]);
      previous->click();
      CHECK(frame() == 3 && tile_frame(half) == 3 && tile_key(half) == half_keys[3]);
      CHECK(tile_key(quarter) == quarter_keys[3]);
      if (QLatin1String(item.row) == QLatin1String("GIF")) {
        previous->click();
        CHECK(frame() == 2 && tile_frame(half) == 2 && tile_frame(quarter) == 2);
        capture_widget_artifact("web_export_quad_animation", *dialog);
      }
      play->click();
      CHECK(timer->isActive());
    }
    // An animated PNG has no quality to reduce: the tiles show the note, not a frame.
    auto* png = row_named(list, QStringLiteral("PNG"));
    auto* png_animate = dialog->findChild<QCheckBox*>(QStringLiteral("webExportPngAnimateCheck"));
    CHECK(png != nullptr && png_animate != nullptr);
    if (png != nullptr && png_animate != nullptr) {
      list->setCurrentItem(png);
      png_animate->setChecked(true);
      CHECK(wait_until([&] { return preview->property("webExportShowsCurrent").toBool() && frames() == 4; }, 120000));
      next->click();
      CHECK(tile_frame(half) == -1 && tile_frame(quarter) == -1);
    }
    inspected = true;
    dialog->reject();
  }));
  patchy::test::ui::require_action(window, "fileExportForWebAction")->trigger();
  QApplication::processEvents();
  CHECK(inspected);
}

// Transparency on a transparent one-layer image: no Animation group, Fill with enables the
// color (the pixels are checked in ui_web_export_play_count_and_fill_read_back).
void ui_web_export_transparency_fill_in_the_dialog() {
  WebExportSettingsGuard guard;
  patchy::ui::MainWindow window;
  show_window(window);
  QImage image(320, 200, QImage::Format_ARGB32);
  image.fill(Qt::transparent);
  {
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(230, 140, 20));
    painter.drawEllipse(QRectF(60, 30, 140, 140));
    painter.setBrush(QColor(40, 110, 210));
    painter.drawRoundedRect(QRectF(170, 70, 110, 90), 18, 18);
  }
  const auto path = QDir(artifact_dir()).filePath(QStringLiteral("transparent_logo_source.png"));
  CHECK(image.save(path));
  MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  bool inspected = false;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("webExportDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    dialog->resize(1280, 820);
    auto* list = dialog->findChild<QTreeWidget*>(QStringLiteral("webExportFormatList"));
    auto* preview = dialog->findChild<QWidget*>(QStringLiteral("webExportPreview"));
    auto* keep = dialog->findChild<QRadioButton*>(QStringLiteral("webExportKeepTransparencyRadio"));
    auto* fill = dialog->findChild<QRadioButton*>(QStringLiteral("webExportFillTransparencyRadio"));
    auto* label = dialog->findChild<QLabel*>(QStringLiteral("webExportBackgroundLabel"));
    auto* swatch = dialog->findChild<QPushButton*>(QStringLiteral("webExportBackgroundColorSwatch"));
    auto* group = dialog->findChild<QWidget*>(QStringLiteral("webExportTransparencyGroup"));
    auto* animation_group = dialog->findChild<QWidget*>(QStringLiteral("webExportAnimationGroup"));
    auto* scroll = dialog->findChild<QScrollArea*>(QStringLiteral("webExportControlsScroll"));
    CHECK(list != nullptr && preview != nullptr && keep != nullptr && fill != nullptr && label != nullptr &&
          swatch != nullptr && group != nullptr && animation_group != nullptr && scroll != nullptr);
    if (list == nullptr || preview == nullptr || keep == nullptr || fill == nullptr || label == nullptr ||
        swatch == nullptr || group == nullptr || animation_group == nullptr || scroll == nullptr) {
      dialog->reject();
      return;
    }
    // One layer: no Animation group.
    list->setCurrentItem(row_named(list, QStringLiteral("PNG")));
    QApplication::processEvents();
    CHECK(!animation_group->isVisible() && group->isVisible());
    CHECK(keep->isChecked() && !swatch->isEnabled());
    const patchy::ui::TransparencyChoiceWidgets widgets{keep, fill, label, swatch, nullptr};
    patchy::ui::set_transparency_choice_color(widgets, QColor(32, 160, 140));
    fill->setChecked(true);
    CHECK(swatch->isEnabled());
    CHECK(wait_until([&] { return preview->property("webExportShowsCurrent").toBool(); }));
    scroll->ensureWidgetVisible(group);
    QApplication::processEvents();
    capture_widget_artifact("web_export_transparency_fill", *dialog);
    // Keep transparent rests the color again.
    keep->setChecked(true);
    CHECK(!swatch->isEnabled());
    inspected = true;
    dialog->reject();
  }));
  patchy::test::ui::require_action(window, "fileExportForWebAction")->trigger();
  QApplication::processEvents();
  CHECK(inspected);
}

// File > Export > Layers as Animated GIF, WebP and PNG open Export for Web with that format
// selected and its Animation on (local card local-031): Fill for opaque frames, Keep
// transparent when a frame has transparent pixels. The opening is not saved on cancel.
void ui_web_export_layers_as_animated_open_export_for_web() {
  WebExportSettingsGuard guard;
  patchy::ui::MainWindow window;
  show_window(window);
  (void)animation_fixture_document(window);
  // The fixture's frames are opaque; a half-transparent two-frame document is not.
  CHECK(!patchy::ui::animation_export_frames_have_transparency(MainWindowTestAccess::document(window)));
  {
    auto two_frames = half_transparent_document(64, 32);
    const auto& first_layer = std::as_const(two_frames).layers().front();
    patchy::Layer second(two_frames.allocate_layer_id(), "Frame 2", first_layer.pixels());
    two_frames.add_layer(std::move(second));
    CHECK(patchy::ui::animation_export_frames_have_transparency(two_frames));
  }
  struct Case {
    const char* action;
    const char* row;
    const char* check;
  };
  const Case cases[] = {{"fileExportAnimatedGifAction", "GIF", "webExportGifAnimateCheck"},
                        {"fileExportAnimatedWebpAction", "WebP", "webExportWebpAnimateCheck"},
                        {"fileExportAnimatedPngAction", "PNG", "webExportPngAnimateCheck"}};
  const auto open_and_check = [&](const Case& item, bool expect_keep, const char* capture) {
    bool inspected = false;
    QTimer::singleShot(0, guarded([&] {
      auto* dialog = find_top_level_dialog(QStringLiteral("webExportDialog"));
      CHECK(dialog != nullptr);
      if (dialog == nullptr) {
        return;
      }
      auto* list = dialog->findChild<QTreeWidget*>(QStringLiteral("webExportFormatList"));
      auto* animate = dialog->findChild<QCheckBox*>(QLatin1String(item.check));
      auto* keep = dialog->findChild<QRadioButton*>(QStringLiteral("webExportKeepTransparencyRadio"));
      auto* fill = dialog->findChild<QRadioButton*>(QStringLiteral("webExportFillTransparencyRadio"));
      auto* animation_group = dialog->findChild<QWidget*>(QStringLiteral("webExportAnimationGroup"));
      CHECK(list != nullptr && animate != nullptr && keep != nullptr && fill != nullptr && animation_group != nullptr);
      if (list == nullptr || animate == nullptr || keep == nullptr || fill == nullptr || animation_group == nullptr) {
        dialog->reject();
        return;
      }
      CHECK(list->currentItem() != nullptr && list->currentItem()->text(0) == QLatin1String(item.row));
      CHECK(animation_group->isVisible() && animate->isVisible() && animate->isChecked());
      CHECK(keep->isChecked() == expect_keep);
      CHECK(fill->isChecked() == !expect_keep);
      if (capture != nullptr) {
        auto* preview = dialog->findChild<QWidget*>(QStringLiteral("webExportPreview"));
        dialog->resize(1280, 820);
        CHECK(preview != nullptr &&
              wait_until([&] { return preview->property("webExportShowsCurrent").toBool(); }));
        QApplication::processEvents();
        capture_widget_artifact(capture, *dialog);
      }
      inspected = true;
      dialog->reject();
    }));
    patchy::test::ui::require_action(window, item.action)->trigger();
    QApplication::processEvents();
    CHECK(inspected);
  };
  for (const auto& item : cases) {
    open_and_check(item, /*expect_keep*/ false, nullptr);
  }
  // Cancel saved nothing: the plain Export for Web still opens on its first-use format.
  CHECK(!patchy::ui::app_settings().contains(QStringLiteral("exportForWeb/format")));
  CHECK(!patchy::ui::app_settings().contains(QStringLiteral("exportForWeb/gifAnimate")));

  // Three frames of a shape on a transparent canvas: Keep transparent.
  {
    patchy::Document sprite(160, 120, patchy::PixelFormat::rgba8());
    const QColor colors[] = {QColor(230, 140, 20), QColor(40, 150, 90), QColor(40, 110, 210)};
    for (int frame = 0; frame < 3; ++frame) {
      QImage image(160, 120, QImage::Format_RGBA8888);
      image.fill(Qt::transparent);
      QPainter painter(&image);
      painter.setRenderHint(QPainter::Antialiasing);
      painter.setPen(Qt::NoPen);
      painter.setBrush(colors[frame]);
      painter.drawEllipse(QRectF(20 + frame * 30, 25, 70, 70));
      painter.end();
      // Bottom first: frame 1 is the top layer.
      const auto name = "Frame " + std::to_string(3 - frame);
      auto layer_document = patchy::ui::document_from_qimage(image, name);
      patchy::Layer layer(sprite.allocate_layer_id(), name, std::as_const(layer_document).layers().front().pixels());
      sprite.add_layer(std::move(layer));
    }
    CHECK(patchy::ui::animation_export_frames_have_transparency(sprite));
    window.add_document_session(std::move(sprite), QStringLiteral("Sprite"));
    QApplication::processEvents();
  }
  for (const auto& item : cases) {
    open_and_check(item, /*expect_keep*/ true,
                   std::string_view(item.row) == "GIF" ? "web_export_layers_as_animated_gif" : nullptr);
  }
}

void send_wheel_notch_down(QWidget& target) {
  const QPointF center(target.width() / 2.0, target.height() / 2.0);
  QWheelEvent wheel(center, target.mapToGlobal(center), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                    Qt::NoScrollPhase, false);
  QSpontaneKeyEvent::setSpontaneous(&wheel);
  qApp->notify(&target, &wheel);
}

// The settings column scrolls with the wheel: an unfocused combo box (Metadata) or spin box
// (JPEG Quality) passes the wheel to the column instead of changing its value; a focused one
// still takes it (install_wheel_scroll_guard, GitHub issue 92).
void ui_web_export_wheel_over_unfocused_control_scrolls_the_settings() {
  WebExportSettingsGuard guard;
  patchy::ui::MainWindow window;
  show_window(window);
  const auto photo = patchy::test::committed_format_fixture_path("readme", "havana_vintage_car_cc0.jpg");
  MainWindowTestAccess::open_document_path(window, patchy::ui::to_qstring(photo));
  QApplication::processEvents();
  bool inspected = false;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("webExportDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    dialog->resize(1280, 560);
    QApplication::processEvents();
    auto* scroll = dialog->findChild<QScrollArea*>(QStringLiteral("webExportControlsScroll"));
    auto* metadata = dialog->findChild<QComboBox*>(QStringLiteral("webExportMetadataCombo"));
    auto* quality = dialog->findChild<QSpinBox*>(QStringLiteral("webExportJpegQualitySpin"));
    auto* list = dialog->findChild<QTreeWidget*>(QStringLiteral("webExportFormatList"));
    CHECK(scroll != nullptr && metadata != nullptr && quality != nullptr && list != nullptr);
    if (scroll == nullptr || metadata == nullptr || quality == nullptr || list == nullptr) {
      dialog->reject();
      return;
    }
    auto* bar = scroll->verticalScrollBar();
    CHECK(bar->maximum() > 0);

    // A real wheel focuses a WheelFocus widget before delivery, so the policy matters too.
    for (QWidget* control : {static_cast<QWidget*>(metadata), static_cast<QWidget*>(quality)}) {
      CHECK(control->focusPolicy() != Qt::WheelFocus && (control->focusPolicy() & Qt::TabFocus) != 0);
    }
    list->setFocus();
    bar->setValue(0);
    const auto quality_before = quality->value();
    send_wheel_notch_down(*quality);
    CHECK(quality->value() == quality_before);
    CHECK(bar->value() > 0);

    scroll->ensureWidgetVisible(metadata);
    bar->setValue(std::max(0, bar->value() - 40));
    const int scrolled = bar->value();
    CHECK(metadata->currentIndex() == 0);
    send_wheel_notch_down(*metadata);
    CHECK(metadata->currentIndex() == 0);
    CHECK(bar->value() > scrolled);

    // Controls the dialog shows later (the WebP group) are covered too.
    list->setCurrentItem(row_named(list, QStringLiteral("WebP")));
    QApplication::processEvents();
    auto* webp_quality = dialog->findChild<QSpinBox*>(QStringLiteral("webExportWebpQualitySpin"));
    CHECK(webp_quality != nullptr);
    if (webp_quality != nullptr) {
      list->setFocus();
      scroll->ensureWidgetVisible(webp_quality);
      const auto before = webp_quality->value();
      send_wheel_notch_down(*webp_quality);
      CHECK(webp_quality->value() == before);
    }

    // With keyboard focus the wheel is the control's again.
    list->setCurrentItem(row_named(list, QStringLiteral("JPEG")));
    QApplication::processEvents();
    scroll->ensureWidgetVisible(quality);
    quality->setFocus();
    QApplication::processEvents();
    CHECK(quality->hasFocus());
    const auto focused_before = quality->value();
    send_wheel_notch_down(*quality);
    CHECK(quality->value() != focused_before);
    inspected = true;
    dialog->reject();
  }));
  patchy::test::ui::require_action(window, "fileExportForWebAction")->trigger();
  QApplication::processEvents();
  CHECK(inspected);
}

// Metadata offers None, Basic, Copyright and Contact Info and All, in that order, each with
// a tooltip; Include thumbnail is enabled with every choice, None included. Saves
// web_export_metadata_choices.png with the open list composited over the dialog.
void ui_web_export_metadata_choices_and_thumbnail_with_none() {
  WebExportSettingsGuard guard;
  patchy::ui::MainWindow window;
  show_window(window);
  const auto photo = patchy::test::committed_format_fixture_path("readme", "havana_vintage_car_cc0.jpg");
  MainWindowTestAccess::open_document_path(window, patchy::ui::to_qstring(photo));
  QApplication::processEvents();
  bool inspected = false;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("webExportDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    dialog->resize(1280, 820);
    QApplication::processEvents();
    auto* scroll = dialog->findChild<QScrollArea*>(QStringLiteral("webExportControlsScroll"));
    auto* metadata = dialog->findChild<QComboBox*>(QStringLiteral("webExportMetadataCombo"));
    auto* thumbnail = dialog->findChild<QCheckBox*>(QStringLiteral("webExportThumbnailCheck"));
    auto* group = dialog->findChild<QWidget*>(QStringLiteral("webExportMetadataGroup"));
    auto* preview = dialog->findChild<QWidget*>(QStringLiteral("webExportPreview"));
    CHECK(scroll != nullptr && metadata != nullptr && thumbnail != nullptr && group != nullptr && preview != nullptr);
    if (scroll == nullptr || metadata == nullptr || thumbnail == nullptr || group == nullptr || preview == nullptr) {
      dialog->reject();
      return;
    }
    const QStringList labels{QStringLiteral("None"), QStringLiteral("Basic"), QStringLiteral("Copyright and Contact Info"),
                             QStringLiteral("All")};
    const QStringList tokens{QStringLiteral("none"), QStringLiteral("basic"), QStringLiteral("copyright"),
                             QStringLiteral("all")};
    CHECK(metadata->count() == 4);
    for (int index = 0; index < std::min(4, metadata->count()); ++index) {
      CHECK(metadata->itemText(index) == labels[index]);
      CHECK(metadata->itemData(index).toString() == tokens[index]);
      CHECK(!metadata->itemData(index, Qt::ToolTipRole).toString().isEmpty());
    }
    CHECK(metadata->currentIndex() == 0);
    CHECK(thumbnail->isVisible() && thumbnail->isEnabled() && !thumbnail->isChecked());
    CHECK(thumbnail->toolTip().contains(QStringLiteral("5 to 10 KB")));
    thumbnail->setChecked(true);
    for (int index = 0; index < metadata->count(); ++index) {
      metadata->setCurrentIndex(index);
      CHECK(thumbnail->isEnabled());
    }
    metadata->setCurrentIndex(0);
    CHECK(wait_until([&] { return preview->property("webExportShowsCurrent").toBool(); }));
    scroll->ensureWidgetVisible(group);
    QApplication::processEvents();
    (void)capture_widget_artifact("web_export_metadata_group", *dialog);
    // The open list, composited over the dialog (offscreen, the popup is its own window).
    metadata->showPopup();
    QApplication::processEvents();
    auto* popup = metadata->view() != nullptr ? metadata->view()->window() : nullptr;
    CHECK(popup != nullptr && popup->isVisible());
    if (popup != nullptr && popup->isVisible()) {
      auto composite = dialog->grab().toImage();
      {
        QPainter painter(&composite);
        painter.drawPixmap(popup->mapToGlobal(QPoint(0, 0)) - dialog->mapToGlobal(QPoint(0, 0)), popup->grab());
      }
      CHECK(composite.save(QStringLiteral("test-artifacts/web_export_metadata_choices.png")));
    }
    metadata->hidePopup();
    inspected = true;
    dialog->reject();
  }));
  patchy::test::ui::require_action(window, "fileExportForWebAction")->trigger();
  QApplication::processEvents();
  CHECK(inspected);
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
  QTimer::singleShot(0, guarded([&] {
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
    // Defaults: JPEG selected, quality 80, the thumbnail off (enabled with every Metadata choice).
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
    CHECK(!thumbnail->isChecked() && thumbnail->isEnabled());
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
    // Two equal-stretch columns split an odd width with one pixel to spare.
    const auto panes_match = [&] { return std::abs(original->width() - preview->width()) <= 1; };
    if (!wait_until(panes_match)) {
      std::fprintf(stderr, "  side by side panes: original %d px, result %d px\n", original->width(),
                   preview->width());
    }
    CHECK(panes_match());
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
      // selectFile leaves the name field alone while it has the focus, which it does by now on
      // macOS: the dialog then saved its default name beside the last export. Type it instead.
      file_dialog->selectFile(target);
      if (auto* name = file_dialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit")); name != nullptr) {
        name->setText(target);
      }
      const auto chosen = file_dialog->selectedFiles();
      if (chosen.size() != 1 || QFileInfo(chosen.front()).fileName() != QFileInfo(target).fileName()) {
        // Never throw here (inside the dialog's event loop); refuse rather than save elsewhere.
        patchy::test::report_failure("Export for Web file dialog did not take the target name: " +
                                     chosen.join(QLatin1Char(',')).toStdString());
        static_cast<QDialog*>(file_dialog)->reject();
        return;
      }
      static_cast<QDialog*>(file_dialog)->accept();
    });
    file_dialog_timer->start();
    save->click();
  }));
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

// The format list puts JPEG XL under JPEG (display order only: the enum and the persisted
// tokens stay); the 2 x 2 tiles' settings are 50% and 25% of the quality (or a palette's
// colors), clamped to the format's minimum; the quality presets; and a user preset's round
// trip through exportForWeb/presets, applied to another document (GitHub issue 571).
void ui_web_export_format_order_quality_steps_and_presets() {
  WebExportSettingsGuard guard;
  const auto& formats = patchy::ui::web_export_formats();
  CHECK(formats.size() >= 5);
  if (formats.size() >= 5) {
    CHECK(formats[0] == WebExportFormat::Jpeg && formats[1] == WebExportFormat::Jxl &&
          formats[2] == WebExportFormat::Webp);
    CHECK(formats[formats.size() - 2] == WebExportFormat::Png && formats.back() == WebExportFormat::Gif);
  }
  CHECK(static_cast<int>(WebExportFormat::Jxl) == 5 && static_cast<int>(WebExportFormat::Webp) == 1);
  CHECK(patchy::ui::web_export_format_from_key(QStringLiteral("jxl")) == WebExportFormat::Jxl);
  CHECK(patchy::ui::web_export_format_from_key(QStringLiteral("webp")) == WebExportFormat::Webp);

  WebExportSettings settings;
  settings.jpeg_quality = 60;
  const auto quality = [&](WebExportFormat format, int percent) {
    const auto reduced = patchy::ui::web_export_reduced_settings(settings, format, percent);
    CHECK(reduced.has_value());
    if (!reduced.has_value()) {
      return -1;
    }
    CHECK(reduced->format == format);
    switch (format) {
      case WebExportFormat::Jpeg:
        return reduced->jpeg_quality;
      case WebExportFormat::Webp:
        return reduced->webp_quality;
      case WebExportFormat::Avif:
        return reduced->avif_quality;
      case WebExportFormat::Jxl:
        return reduced->jxl_quality;
      case WebExportFormat::Gif:
        return reduced->gif_colors;
      case WebExportFormat::Png:
        return reduced->png_colors;
    }
    return -1;
  };
  CHECK(quality(WebExportFormat::Jpeg, 50) == 30 && quality(WebExportFormat::Jpeg, 25) == 15);
  settings.webp_quality = 75;
  CHECK(quality(WebExportFormat::Webp, 50) == 38 && quality(WebExportFormat::Webp, 25) == 19);
  settings.avif_quality = 2;
  CHECK(quality(WebExportFormat::Avif, 25) == 1);
  settings.jxl_quality = 2;
  settings.jpeg_quality = 2;
  // JPEG and JPEG XL stop at 1, palettes at 2 colors.
  CHECK(quality(WebExportFormat::Jxl, 25) == 1 && quality(WebExportFormat::Jpeg, 25) == 1);
  CHECK(quality(WebExportFormat::Gif, 50) == 128 && quality(WebExportFormat::Gif, 25) == 64);
  settings.gif_colors = 4;
  CHECK(quality(WebExportFormat::Gif, 25) == 2);
  // Nothing to reduce: full-color PNG and the lossless choices.
  CHECK(!patchy::ui::web_export_reduced_settings(settings, WebExportFormat::Png, 50).has_value());
  settings.png_palette = true;
  settings.png_colors = 100;
  CHECK(quality(WebExportFormat::Png, 50) == 50 && quality(WebExportFormat::Png, 25) == 25);
  settings.webp_lossless = true;
  settings.avif_lossless = true;
  settings.jxl_lossless = true;
  for (const auto format : {WebExportFormat::Webp, WebExportFormat::Avif, WebExportFormat::Jxl}) {
    CHECK(!patchy::ui::web_export_reduced_settings(settings, format, 50).has_value());
    CHECK(patchy::ui::web_export_quality_label(settings, format).isEmpty());
  }
  CHECK(patchy::ui::web_export_quality_label(settings, WebExportFormat::Jpeg) == QStringLiteral("Quality 2"));
  CHECK(patchy::ui::web_export_quality_label(settings, WebExportFormat::Gif) == QStringLiteral("4 colors"));

  // Quality presets: Patchy's own names and values.
  const auto presets = patchy::ui::web_export_quality_presets();
  CHECK(presets.size() == 5);
  if (presets.size() == 5) {
    CHECK(QLatin1String(presets[0].name) == QLatin1String("Low") && presets[0].quality == 30);
    CHECK(QLatin1String(presets[1].name) == QLatin1String("Medium") && presets[1].quality == 50);
    CHECK(QLatin1String(presets[2].name) == QLatin1String("High") && presets[2].quality == 70);
    CHECK(QLatin1String(presets[3].name) == QLatin1String("Very High") && presets[3].quality == 85);
    CHECK(QLatin1String(presets[4].name) == QLatin1String("Maximum") && presets[4].quality == 100);
  }

  // The 2 x 2 compare mode is persisted as "quad".
  WebExportSettings compare;
  compare.compare = patchy::ui::WebExportCompare::Quad;
  patchy::ui::save_web_export_settings(compare);
  CHECK(patchy::ui::load_web_export_settings().compare == patchy::ui::WebExportCompare::Quad);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("exportForWeb/compare")).toString() == QStringLiteral("quad"));

  // A user preset: WebP at 61 on a 200 x 100 document scaled to 50%, filled, All metadata.
  CHECK(patchy::ui::load_web_export_user_presets().empty());
  const QSize small(200, 100);
  WebExportSettings saved;
  saved.format = WebExportFormat::Webp;
  saved.webp_quality = 61;
  saved.fill_transparent = true;
  saved.jpeg_background = QColor(16, 32, 48);
  saved.metadata = patchy::metadata::MetadataPolicy::All;
  saved.trim = true;
  saved.width = 100;
  saved.height = 50;
  saved.jpeg_quality = 12;  // another format's option: not part of a WebP preset
  const auto preset = patchy::ui::make_web_export_preset(QStringLiteral("  Small WebP "), saved, small);
  CHECK(preset.name == QStringLiteral("Small WebP") && std::abs(preset.scale_percent - 50.0) < 1e-9);
  CHECK(patchy::ui::web_export_preset_matches(preset, saved, small));
  patchy::ui::save_web_export_user_presets({preset});
  const auto loaded = patchy::ui::load_web_export_user_presets();
  CHECK(loaded.size() == 1);
  if (loaded.size() == 1) {
    const auto& back = loaded.front();
    CHECK(back.name == QStringLiteral("Small WebP") && !back.built_in);
    CHECK(back.settings.format == WebExportFormat::Webp && back.settings.webp_quality == 61);
    CHECK(back.settings.fill_transparent && back.settings.jpeg_background == QColor(16, 32, 48));
    CHECK(back.settings.metadata == patchy::metadata::MetadataPolicy::All && back.settings.trim);
    // On a 400 x 300 document: half size, the preset's part applied, other formats untouched.
    WebExportSettings current;
    current.jpeg_quality = 33;
    current.compare = patchy::ui::WebExportCompare::SideBySide;
    const QSize large(400, 300);
    const auto applied = patchy::ui::apply_web_export_preset(back, current, large);
    CHECK(applied.format == WebExportFormat::Webp && applied.webp_quality == 61);
    CHECK(applied.width == 200 && applied.height == 150);
    CHECK(applied.fill_transparent && applied.metadata == patchy::metadata::MetadataPolicy::All);
    CHECK(applied.jpeg_quality == 33 && applied.compare == patchy::ui::WebExportCompare::SideBySide);
    CHECK(patchy::ui::web_export_preset_matches(back, applied, large));
    auto changed = applied;
    changed.webp_quality = 62;
    CHECK(!patchy::ui::web_export_preset_matches(back, changed, large));
  }
  // Built-in presets are not stored; deleting the last user preset clears the key.
  auto all = patchy::ui::web_export_builtin_presets();
  CHECK(!all.empty() && all.front().built_in);
  all.push_back(preset);
  patchy::ui::save_web_export_user_presets(all);
  CHECK(patchy::ui::load_web_export_user_presets().size() == 1);
  patchy::ui::save_web_export_user_presets({});
  CHECK(patchy::ui::load_web_export_user_presets().empty());
  CHECK(!patchy::ui::app_settings().contains(QStringLiteral("exportForWeb/presets")));
}

// Runs `inspect` on the Export for Web dialog over `document`, then closes it.
void with_web_export_dialog(const patchy::Document& document, const std::function<void(QDialog&)>& inspect) {
  bool inspected = false;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("webExportDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    inspect(*dialog);
    inspected = true;
    if (dialog->isVisible()) {
      dialog->reject();
    }
  }));
  (void)patchy::ui::request_web_export(nullptr, std::make_shared<const patchy::Document>(document));
  CHECK(inspected);
}

// The dialog opens at its smallest size the first time, and keeps its size and compare mode
// when it is cancelled (GitHub issue 571).
void ui_web_export_dialog_keeps_size_and_compare_mode_through_cancel() {
  WebExportSettingsGuard guard;
  const auto document = textured_document(320, 200);
  QSize closed_at;
  with_web_export_dialog(document, [&](QDialog& dialog) {
    const auto smallest = dialog.minimumSizeHint().expandedTo(dialog.minimumSize());
    CHECK(dialog.width() <= smallest.width() && dialog.height() <= smallest.height());
    auto* result_only = dialog.findChild<QPushButton*>(QStringLiteral("webExportCompareResult"));
    CHECK(result_only != nullptr && !result_only->isChecked());
    if (result_only != nullptr) {
      result_only->click();
    }
    QApplication::processEvents();
    closed_at = dialog.minimumSize() + QSize(37, 23);
    dialog.resize(closed_at);
    QApplication::processEvents();
    closed_at = dialog.size();
  });
  auto stored = patchy::ui::app_settings();
  CHECK(stored.value(QStringLiteral("exportForWeb/dialogSize")).toSize() == closed_at);
  CHECK(stored.value(QStringLiteral("exportForWeb/compare")).toString() == QStringLiteral("result"));
  CHECK(patchy::ui::load_web_export_settings().compare == patchy::ui::WebExportCompare::ResultOnly);
  with_web_export_dialog(document, [&](QDialog& dialog) {
    auto* result_only = dialog.findChild<QPushButton*>(QStringLiteral("webExportCompareResult"));
    CHECK(result_only != nullptr && result_only->isChecked());
    const auto* screen = QGuiApplication::primaryScreen();
    auto expected = closed_at;
    if (screen != nullptr) {
      expected = expected.boundedTo(screen->availableGeometry().size());
    }
    expected = expected.expandedTo(dialog.minimumSize());
    CHECK(dialog.size() == expected);
  });
}

// The list order with JPEG XL under JPEG, the quality preset combo, the 2 x 2 compare mode at
// 60, 30 and 15, and a preset saved, matched, applied and deleted in the dialog. Captures
// web_export_presets, web_export_png_transparency and web_export_quad.
void ui_web_export_quad_compare_and_presets_in_the_dialog() {
  WebExportSettingsGuard guard;
  const auto photo = patchy::test::committed_format_fixture_path("readme", "havana_vintage_car_cc0.jpg");
  const auto image = QImage(patchy::ui::to_qstring(photo)).scaledToWidth(900, Qt::SmoothTransformation);
  CHECK(!image.isNull());
  const auto document = patchy::ui::document_from_qimage(image.convertToFormat(QImage::Format_RGBA8888), "Photo");
  with_web_export_dialog(document, [&](QDialog& dialog) {
    dialog.resize(1280, 820);
    QApplication::processEvents();
    auto* list = dialog.findChild<QTreeWidget*>(QStringLiteral("webExportFormatList"));
    auto* quality = dialog.findChild<QSpinBox*>(QStringLiteral("webExportJpegQualitySpin"));
    auto* quality_presets = dialog.findChild<QToolButton*>(QStringLiteral("webExportJpegQualityPresetsButton"));
    auto* presets = dialog.findChild<QComboBox*>(QStringLiteral("webExportPresetCombo"));
    auto* save = dialog.findChild<QPushButton*>(QStringLiteral("webExportPresetSaveButton"));
    auto* remove = dialog.findChild<QPushButton*>(QStringLiteral("webExportPresetDeleteButton"));
    auto* quad = dialog.findChild<QPushButton*>(QStringLiteral("webExportCompareQuad"));
    auto* split = dialog.findChild<QPushButton*>(QStringLiteral("webExportCompareSplit"));
    auto* main_pane = dialog.findChild<QWidget*>(QStringLiteral("webExportPreview"));
    auto* original = dialog.findChild<QWidget*>(QStringLiteral("webExportOriginalPreview"));
    auto* half = dialog.findChild<QWidget*>(QStringLiteral("webExportHalfQualityPreview"));
    auto* quarter = dialog.findChild<QWidget*>(QStringLiteral("webExportQuarterQualityPreview"));
    auto* zoom_fit = dialog.findChild<QPushButton*>(QStringLiteral("webExportZoomFit"));
    auto* scroll = dialog.findChild<QScrollArea*>(QStringLiteral("webExportControlsScroll"));
    CHECK(list != nullptr && quality != nullptr && quality_presets != nullptr && presets != nullptr &&
          save != nullptr && remove != nullptr && quad != nullptr && split != nullptr && main_pane != nullptr &&
          original != nullptr && half != nullptr && quarter != nullptr && zoom_fit != nullptr && scroll != nullptr);
    if (list == nullptr || quality == nullptr || quality_presets == nullptr || presets == nullptr || save == nullptr ||
        remove == nullptr || quad == nullptr || split == nullptr || main_pane == nullptr || original == nullptr ||
        half == nullptr || quarter == nullptr || zoom_fit == nullptr || scroll == nullptr) {
      return;
    }
    // JPEG XL is the second row and selects the JPEG XL settings.
    CHECK(list->topLevelItem(1) != nullptr && list->topLevelItem(1)->text(0) == QStringLiteral("JPEG XL"));
    CHECK(list->topLevelItem(1)->data(0, Qt::UserRole).toString() == QStringLiteral("jxl"));
    CHECK(list->topLevelItem(2) != nullptr && list->topLevelItem(2)->text(0) == QStringLiteral("WebP"));
    // The zoom controls are framed push buttons.
    CHECK(!zoom_fit->isFlat());

    // Quality presets: a chevron drop-down on the field. 80 ticks none; High sets 70; at 85 the
    // menu ticks Very High.
    auto* menu = quality_presets->menu();
    CHECK(menu != nullptr && menu->objectName() == QStringLiteral("webExportJpegQualityPresetsMenu"));
    if (menu != nullptr) {
      const auto ticked = [menu] {
        emit menu->aboutToShow();
        QStringList checked;
        for (auto* action : menu->actions()) {
          if (action->isChecked()) {
            checked << action->text();
          }
        }
        return checked;
      };
      QStringList names;
      for (auto* action : menu->actions()) {
        names << QStringLiteral("%1=%2").arg(action->text()).arg(action->data().toInt());
      }
      CHECK(names == QStringList({QStringLiteral("Low=30"), QStringLiteral("Medium=50"), QStringLiteral("High=70"),
                                  QStringLiteral("Very High=85"), QStringLiteral("Maximum=100")}));
      CHECK(quality->value() == 80 && ticked().isEmpty());
      menu->actions().at(2)->trigger();
      CHECK(quality->value() == 70);
      quality->setValue(85);
      CHECK(ticked() == QStringList({QStringLiteral("Very High")}));
      quality->setValue(84);
      CHECK(ticked().isEmpty());
    }
    // The quality field has the shared - and + buttons.
    CHECK(quality->buttonSymbols() == QAbstractSpinBox::UpDownArrows);

    // Presets: the built-in ones are listed; Delete is off for them.
    CHECK(presets->count() == static_cast<int>(patchy::ui::web_export_builtin_presets().size()));
    CHECK(presets->currentIndex() == -1 && !remove->isEnabled());
    quality->setValue(85);
    QApplication::processEvents();
    CHECK(presets->currentText() == QStringLiteral("JPEG Very High") && !remove->isEnabled());
    quality->setValue(60);
    QApplication::processEvents();
    CHECK(presets->currentIndex() == -1);
    // Save asks for a name and selects the new preset.
    QTimer::singleShot(0, guarded([] {
      auto* name_dialog = qobject_cast<QInputDialog*>(find_top_level_dialog(QStringLiteral("webExportPresetNameDialog")));
      CHECK(name_dialog != nullptr);
      if (name_dialog != nullptr) {
        name_dialog->setTextValue(QStringLiteral("Photo 60"));
        name_dialog->accept();
      }
    }));
    save->click();
    QApplication::processEvents();
    CHECK(presets->currentText() == QStringLiteral("Photo 60") && remove->isEnabled());
    const auto stored = patchy::ui::load_web_export_user_presets();
    CHECK(stored.size() == 1 && stored.front().settings.jpeg_quality == 60);
    CHECK(wait_until([&] { return main_pane->property("webExportShowsCurrent").toBool(); }));
    capture_widget_artifact("web_export_presets", dialog);
    // A change shows Custom; picking the preset again restores it.
    quality->setValue(61);
    QApplication::processEvents();
    CHECK(presets->currentIndex() == -1);
    const auto mine = presets->findText(QStringLiteral("Photo 60"));
    CHECK(mine >= 0);
    presets->setCurrentIndex(mine);
    emit presets->activated(mine);
    QApplication::processEvents();
    CHECK(quality->value() == 60 && presets->currentText() == QStringLiteral("Photo 60"));
    // A built-in preset switches the format.
    const auto webp = presets->findText(QStringLiteral("WebP High"));
    CHECK(webp >= 0);
    presets->setCurrentIndex(webp);
    emit presets->activated(webp);
    QApplication::processEvents();
    auto* webp_quality = dialog.findChild<QSpinBox*>(QStringLiteral("webExportWebpQualitySpin"));
    CHECK(list->currentItem() != nullptr && list->currentItem()->text(0) == QStringLiteral("WebP"));
    CHECK(webp_quality != nullptr && webp_quality->value() == 70);

    // PNG: one Transparency row, with the 50% note for a palette.
    list->setCurrentItem(row_named(list, QStringLiteral("PNG")));
    auto* png_type = dialog.findChild<QComboBox*>(QStringLiteral("webExportPngColorsCombo"));
    CHECK(png_type != nullptr);
    if (png_type != nullptr) {
      png_type->setCurrentIndex(1);
    }
    QApplication::processEvents();
    auto* keep = dialog.findChild<QRadioButton*>(QStringLiteral("webExportKeepTransparencyRadio"));
    auto* fill = dialog.findChild<QRadioButton*>(QStringLiteral("webExportFillTransparencyRadio"));
    auto* cut_note = dialog.findChild<QLabel*>(QStringLiteral("webExportTransparencyCutNote"));
    auto* transparency = dialog.findChild<QWidget*>(QStringLiteral("webExportTransparencyGroup"));
    CHECK(keep != nullptr && fill != nullptr && cut_note != nullptr && transparency != nullptr);
    if (keep != nullptr && fill != nullptr && cut_note != nullptr && transparency != nullptr) {
      CHECK(keep->isVisible() && fill->isVisible() && cut_note->isVisible());
      const auto keep_at = keep->mapTo(&dialog, QPoint(0, keep->height() / 2));
      const auto fill_at = fill->mapTo(&dialog, QPoint(0, fill->height() / 2));
      CHECK(fill_at.x() > keep_at.x() + keep->width() && std::abs(fill_at.y() - keep_at.y()) <= 2);
      scroll->ensureWidgetVisible(transparency);
      CHECK(wait_until([&] { return main_pane->property("webExportShowsCurrent").toBool(); }));
      capture_widget_artifact("web_export_png_transparency", dialog);
    }

    // 2 x 2 for JPEG at 60: the original, 60, 30 and 15.
    list->setCurrentItem(row_named(list, QStringLiteral("JPEG")));
    quality->setValue(60);
    quad->click();
    QApplication::processEvents();
    CHECK(original->isVisible() && half->isVisible() && quarter->isVisible() && main_pane->isVisible());
    CHECK(wait_until([&] {
      return main_pane->property("webExportShowsCurrent").toBool() && half->property("webExportShowsCurrent").toBool() &&
             quarter->property("webExportShowsCurrent").toBool();
    }));
    CHECK(half->property("webExportQualityLabel").toString() == QStringLiteral("Quality 30"));
    CHECK(quarter->property("webExportQualityLabel").toString() == QStringLiteral("Quality 15"));
    // The tiles sit in a 2 x 2 grid: the original top left, 30 under it.
    const auto at = [&dialog](QWidget* widget) { return widget->mapTo(&dialog, QPoint(0, 0)); };
    CHECK(at(original).y() == at(main_pane).y() && at(original).x() < at(main_pane).x());
    CHECK(at(half).x() == at(original).x() && at(half).y() > at(original).y());
    CHECK(at(quarter).x() == at(main_pane).x() && at(quarter).y() == at(half).y());
    capture_widget_artifact("web_export_quad", dialog);
    // Full-color PNG has nothing to reduce: the lower tiles say so.
    list->setCurrentItem(row_named(list, QStringLiteral("PNG")));
    if (png_type != nullptr) {
      png_type->setCurrentIndex(0);
    }
    QApplication::processEvents();
    CHECK(!half->property("webExportNote").toString().isEmpty());
    // Back to Split: one pane.
    split->click();
    QApplication::processEvents();
    CHECK(!original->isVisible() && !half->isVisible() && !quarter->isVisible());

    // Delete removes the selected user preset.
    list->setCurrentItem(row_named(list, QStringLiteral("JPEG")));
    const auto again = presets->findText(QStringLiteral("Photo 60"));
    presets->setCurrentIndex(again);
    emit presets->activated(again);
    QApplication::processEvents();
    CHECK(remove->isEnabled());
    remove->click();
    QApplication::processEvents();
    CHECK(presets->findText(QStringLiteral("Photo 60")) < 0);
    CHECK(patchy::ui::load_web_export_user_presets().empty());
  });
}

// No label, check box or radio button in the narrower settings column is cut off, for any
// format, in any shipped language (GitHub issue 571).
void ui_web_export_settings_column_fits_every_language() {
  WebExportSettingsGuard guard;
  LanguageRestorer language;
  patchy::ui::MainWindow window;
  show_window(window);
  // Two or more layers, so the Animation group shows too.
  const auto document = animation_fixture_document(window);
  QStringList clipped;
  for (const auto& info : patchy::ui::LocalizationManager::instance().languages()) {
    CHECK(patchy::ui::LocalizationManager::instance().set_language(info.code, false));
    QApplication::processEvents();
    with_web_export_dialog(document, [&](QDialog& dialog) {
      auto* list = dialog.findChild<QTreeWidget*>(QStringLiteral("webExportFormatList"));
      auto* scroll = dialog.findChild<QScrollArea*>(QStringLiteral("webExportControlsScroll"));
      auto* png_type = dialog.findChild<QComboBox*>(QStringLiteral("webExportPngColorsCombo"));
      CHECK(list != nullptr && scroll != nullptr && png_type != nullptr);
      if (list == nullptr || scroll == nullptr || png_type == nullptr) {
        return;
      }
      for (int row = 0; row < list->topLevelItemCount(); ++row) {
        for (const int png_colors : {0, 1}) {
          list->setCurrentItem(list->topLevelItem(row));
          png_type->setCurrentIndex(png_colors);
          // Let a group shown for the first time settle its layout.
          for (int pass = 0; pass < 3; ++pass) {
            QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
            QApplication::processEvents();
          }
          auto* column = scroll->widget();
          auto* viewport = scroll->viewport();
          if (column->width() > viewport->width()) {
            // Name what sets the column's minimum width.
            for (auto* widget : column->findChildren<QWidget*>()) {
              if (widget->isVisibleTo(column) && widget->minimumSizeHint().width() > viewport->width() - 30 &&
                  qobject_cast<QGroupBox*>(widget) == nullptr) {
                clipped << QStringLiteral("%1 (%2): wide %3 %4 min %5")
                               .arg(info.code, list->topLevelItem(row)->text(0),
                                    QLatin1String(widget->metaObject()->className()), widget->objectName())
                               .arg(widget->minimumSizeHint().width());
              }
            }
          }
          for (auto* widget : column->findChildren<QWidget*>()) {
            if (!widget->isVisibleTo(column)) {
              continue;
            }
            auto* label = qobject_cast<QLabel*>(widget);
            const bool text_control = label != nullptr || qobject_cast<QCheckBox*>(widget) != nullptr ||
                                      qobject_cast<QRadioButton*>(widget) != nullptr ||
                                      (qobject_cast<QPushButton*>(widget) != nullptr &&
                                       widget->minimumWidth() != widget->maximumWidth());
            if (!text_control) {
              continue;
            }
            const auto right = widget->mapTo(viewport, QPoint(widget->width(), 0)).x();
            const bool squeezed = (label == nullptr || !label->wordWrap()) &&
                                  widget->width() < widget->sizeHint().width();
            if (right > viewport->width() + 1 || squeezed) {
              const auto text = label != nullptr                                      ? label->text()
                                : qobject_cast<QAbstractButton*>(widget) != nullptr ? qobject_cast<QAbstractButton*>(widget)->text()
                                                                                      : QString();
              clipped << QStringLiteral("%1 (%2, %3): %4 %5 '%6' width %7 hint %8 right %9 of %10")
                             .arg(info.code, list->topLevelItem(row)->text(0), png_colors != 0 ? QStringLiteral("palette") : QStringLiteral("full"),
                                  QLatin1String(widget->metaObject()->className()), widget->objectName(), text)
                             .arg(widget->width())
                             .arg(widget->sizeHint().width())
                             .arg(right)
                             .arg(viewport->width());
            }
          }
        }
      }
    });
  }
  clipped.removeDuplicates();
  for (const auto& line : clipped) {
    std::fprintf(stderr, "  clipped in %s\n", qPrintable(line));
  }
  CHECK(clipped.isEmpty());
}

// At the dialog's minimum size every button and label under the preview (compare, zoom,
// playback, footer) shows its whole text, in every shipped language, for a still and an
// animated document: the zoom controls move under the compare buttons when the row is too
// narrow (GitHub issue 571 follow-up). Captures web_export_minimum_size and
// web_export_minimum_size_animated in English.
void ui_web_export_preview_buttons_fit_at_minimum_size() {
  WebExportSettingsGuard guard;
  LanguageRestorer language;
  patchy::ui::MainWindow window;
  show_window(window);
  const auto animated = animation_fixture_document(window);
  const auto still = textured_document(320, 200);
  const auto settle = [] {
    for (int pass = 0; pass < 4; ++pass) {
      QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
      QApplication::processEvents();
    }
  };
  QStringList clipped;
  for (const auto& info : patchy::ui::LocalizationManager::instance().languages()) {
    CHECK(patchy::ui::LocalizationManager::instance().set_language(info.code, false));
    QApplication::processEvents();
    for (const bool animate : {false, true}) {
      with_web_export_dialog(animate ? animated : still, [&](QDialog& dialog) {
        auto* scroll = dialog.findChild<QScrollArea*>(QStringLiteral("webExportControlsScroll"));
        auto* playback = dialog.findChild<QWidget*>(QStringLiteral("webExportPlaybackControls"));
        auto* row = dialog.findChild<QWidget*>(QStringLiteral("webExportViewRow"));
        CHECK(scroll != nullptr && playback != nullptr && row != nullptr);
        if (scroll == nullptr || playback == nullptr || row == nullptr) {
          return;
        }
        if (animate) {
          auto* list = dialog.findChild<QTreeWidget*>(QStringLiteral("webExportFormatList"));
          auto* gif_animate = dialog.findChild<QCheckBox*>(QStringLiteral("webExportGifAnimateCheck"));
          CHECK(list != nullptr && gif_animate != nullptr);
          if (list == nullptr || gif_animate == nullptr) {
            return;
          }
          list->setCurrentItem(list->topLevelItem(list->topLevelItemCount() - 1));
          gif_animate->setChecked(true);
          CHECK(wait_until([&] { return playback->isVisible(); }));
        }
        for (int attempt = 0; attempt < 3; ++attempt) {
          settle();
          dialog.resize(dialog.minimumSize());
        }
        settle();
        for (auto* widget : dialog.findChildren<QWidget*>()) {
          if (!widget->isVisible() || scroll->isAncestorOf(widget)) {
            continue;
          }
          auto* label = qobject_cast<QLabel*>(widget);
          auto* button = qobject_cast<QAbstractButton*>(widget);
          if ((label == nullptr || label->wordWrap() || label->text().isEmpty()) &&
              (button == nullptr || button->text().isEmpty())) {
            continue;
          }
          const auto right = widget->mapTo(&dialog, QPoint(widget->width(), 0)).x();
          const bool squeezed = widget->minimumWidth() != widget->maximumWidth() &&
                                widget->width() < widget->sizeHint().width();
          if (squeezed || right > dialog.width()) {
            clipped << QStringLiteral("%1 (%2): %3 %4 '%5' width %6 hint %7 right %8 of %9")
                           .arg(info.code, animate ? QStringLiteral("animated") : QStringLiteral("still"),
                                QLatin1String(widget->metaObject()->className()), widget->objectName(),
                                label != nullptr ? label->text() : button->text())
                           .arg(widget->width())
                           .arg(widget->sizeHint().width())
                           .arg(right)
                           .arg(dialog.width());
          }
        }
        if (info.code == QStringLiteral("en")) {
          capture_widget_artifact(animate ? "web_export_minimum_size_animated" : "web_export_minimum_size", dialog);
        }
      });
    }
  }
  clipped.removeDuplicates();
  for (const auto& line : clipped) {
    std::fprintf(stderr, "  clipped in %s\n", qPrintable(line));
  }
  CHECK(clipped.isEmpty());
}

// The row under the preview (GitHub issue 571): "View:" with Split, Side by side, 4-up and
// Result only in that order, then "Zoom:", the amount as a plain percent, - and + of one
// width, 100% and Fit (checked while the preview fits) at the right edge, all on one line,
// also at the minimum size.
void ui_web_export_view_row_order_and_zoom_controls() {
  WebExportSettingsGuard guard;
  with_web_export_dialog(textured_document(320, 200), [&](QDialog& dialog) {
    dialog.resize(dialog.minimumSize());
    QApplication::processEvents();
    auto* row = dialog.findChild<QWidget*>(QStringLiteral("webExportViewRow"));
    QList<QAbstractButton*> buttons;
    for (const auto* name : {"webExportCompareSplit", "webExportCompareSideBySide", "webExportCompareQuad",
                             "webExportCompareResult", "webExportZoomOut", "webExportZoomIn", "webExportZoom100",
                             "webExportZoomFit"}) {
      buttons << dialog.findChild<QAbstractButton*>(QLatin1String(name));
    }
    auto* amount = dialog.findChild<QLabel*>(QStringLiteral("webExportZoomLabel"));
    CHECK(row != nullptr && amount != nullptr && !buttons.contains(nullptr));
    if (row == nullptr || amount == nullptr || buttons.contains(nullptr)) {
      return;
    }
    QStringList labels;
    for (auto* label : row->findChildren<QLabel*>(Qt::FindDirectChildrenOnly)) {
      labels << label->text();
    }
    CHECK(labels.contains(QStringLiteral("View:")) && labels.contains(QStringLiteral("Zoom:")));
    CHECK(!labels.contains(QStringLiteral("Compare:")));
    CHECK(buttons[2]->text() == QStringLiteral("4-up"));
    // Left to right, on one line.
    for (int index = 1; index < buttons.size(); ++index) {
      CHECK(buttons[index]->geometry().left() > buttons[index - 1]->geometry().right());
      CHECK(std::abs(buttons[index]->geometry().center().y() - buttons[0]->geometry().center().y()) <= 1);
    }
    CHECK(amount->geometry().left() > buttons[3]->geometry().right() &&
          amount->geometry().right() < buttons[4]->geometry().left());
    CHECK(buttons[4]->width() == buttons[5]->width());
    CHECK(std::abs(buttons[7]->geometry().right() - (row->width() - 1)) <= 1);
    // The amount is a plain percent, Fit checked while the preview fits.
    auto* fit = buttons[7];
    CHECK(QRegularExpression(QStringLiteral("^\\d+%$")).match(amount->text()).hasMatch());
    CHECK(fit->isCheckable() && fit->isChecked());
    buttons[6]->click();
    QApplication::processEvents();
    CHECK(amount->text() == QStringLiteral("100%") && !fit->isChecked());
    fit->click();
    QApplication::processEvents();
    CHECK(fit->isChecked());
    fit->click();
    QApplication::processEvents();
    CHECK(fit->isChecked());
  });
}

// Switching views never resizes the dialog, at its minimum size or larger (GitHub issue 571:
// 2 x 2 grew the window past the screen).
void ui_web_export_view_modes_keep_the_dialog_size() {
  WebExportSettingsGuard guard;
  with_web_export_dialog(textured_document(640, 400), [&](QDialog& dialog) {
    auto* preview = dialog.findChild<QWidget*>(QStringLiteral("webExportPreview"));
    CHECK(preview != nullptr &&
          wait_until([&] { return preview->property("webExportShowsCurrent").toBool(); }));
    for (const bool smallest : {true, false}) {
      dialog.resize(smallest ? dialog.minimumSize() : dialog.minimumSize() + QSize(180, 90));
      QApplication::processEvents();
      const auto size = dialog.size();
      for (const auto* name : {"webExportCompareQuad", "webExportCompareSideBySide", "webExportCompareResult",
                               "webExportCompareQuad", "webExportCompareSplit"}) {
        auto* button = dialog.findChild<QAbstractButton*>(QLatin1String(name));
        CHECK(button != nullptr);
        if (button == nullptr) {
          continue;
        }
        button->click();
        for (int pass = 0; pass < 3; ++pass) {
          QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
          QApplication::processEvents();
        }
        if (dialog.size() != size) {
          std::fprintf(stderr, "  %s: %dx%d became %dx%d (minimum %dx%d)\n", name, size.width(), size.height(),
                       dialog.width(), dialog.height(), dialog.minimumWidth(), dialog.minimumHeight());
        }
        CHECK(dialog.size() == size);
      }
    }
  });
}

// JPEG shows Keep transparent too, disabled, with Fill with selected; the user's own choice
// comes back for the formats with alpha. Captures
// web_export_jpeg_settings with the quality presets list open.
void ui_web_export_jpeg_keeps_the_transparency_choice() {
  WebExportSettingsGuard guard;
  with_web_export_dialog(textured_document(320, 200), [&](QDialog& dialog) {
    dialog.resize(1280, 820);
    auto* list = dialog.findChild<QTreeWidget*>(QStringLiteral("webExportFormatList"));
    auto* keep = dialog.findChild<QRadioButton*>(QStringLiteral("webExportKeepTransparencyRadio"));
    auto* fill = dialog.findChild<QRadioButton*>(QStringLiteral("webExportFillTransparencyRadio"));
    auto* presets = dialog.findChild<QToolButton*>(QStringLiteral("webExportJpegQualityPresetsButton"));
    auto* preview = dialog.findChild<QWidget*>(QStringLiteral("webExportPreview"));
    CHECK(list != nullptr && keep != nullptr && fill != nullptr && presets != nullptr && preview != nullptr);
    if (list == nullptr || keep == nullptr || fill == nullptr || presets == nullptr || preview == nullptr) {
      return;
    }
    QApplication::processEvents();
    CHECK(keep->isVisible() && !keep->isEnabled() && fill->isChecked());
    list->setCurrentItem(row_named(list, QStringLiteral("PNG")));
    QApplication::processEvents();
    CHECK(keep->isEnabled() && keep->isChecked() && !fill->isChecked());
    fill->setChecked(true);
    list->setCurrentItem(row_named(list, QStringLiteral("JPEG")));
    QApplication::processEvents();
    CHECK(!keep->isEnabled() && fill->isChecked());
    list->setCurrentItem(row_named(list, QStringLiteral("GIF")));
    QApplication::processEvents();
    CHECK(keep->isEnabled() && fill->isChecked());
    keep->setChecked(true);
    list->setCurrentItem(row_named(list, QStringLiteral("JPEG")));
    QApplication::processEvents();
    CHECK(fill->isChecked() && !keep->isEnabled());
    list->setCurrentItem(row_named(list, QStringLiteral("PNG")));
    QApplication::processEvents();
    CHECK(keep->isChecked());
    list->setCurrentItem(row_named(list, QStringLiteral("JPEG")));
    CHECK(wait_until([&] { return preview->property("webExportShowsCurrent").toBool(); }));
    if (auto* menu = presets->menu(); menu != nullptr) {
      menu->popup(presets->mapToGlobal(QPoint(0, presets->height())));
      QApplication::processEvents();
      CHECK(menu->isVisible());
      save_window_popup_artifact("web_export_jpeg_settings", dialog, *presets, *menu);
      menu->hide();
    }
  });
}

// The busy spinner turns only while the shown result (and in 4-up its tiles) is encoding, and
// always stops when they arrive, even while other rows still encode (GitHub issue 571).
void ui_web_export_spinner_stops_when_the_shown_results_arrive() {
  WebExportSettingsGuard guard;
  const auto photo = patchy::test::committed_format_fixture_path("readme", "havana_vintage_car_cc0.jpg");
  const auto image = QImage(patchy::ui::to_qstring(photo)).scaledToWidth(1200, Qt::SmoothTransformation);
  const auto document = patchy::ui::document_from_qimage(image.convertToFormat(QImage::Format_RGBA8888), "Photo");
  with_web_export_dialog(document, [&](QDialog& dialog) {
    auto* list = dialog.findChild<QTreeWidget*>(QStringLiteral("webExportFormatList"));
    auto* busy = dialog.findChild<QWidget*>(QStringLiteral("webExportBusySpinner"));
    auto* preview = dialog.findChild<QWidget*>(QStringLiteral("webExportPreview"));
    auto* half = dialog.findChild<QWidget*>(QStringLiteral("webExportHalfQualityPreview"));
    auto* quarter = dialog.findChild<QWidget*>(QStringLiteral("webExportQuarterQualityPreview"));
    auto* quad = dialog.findChild<QAbstractButton*>(QStringLiteral("webExportCompareQuad"));
    auto* quality = dialog.findChild<QSpinBox*>(QStringLiteral("webExportJpegQualitySpin"));
    CHECK(list != nullptr && busy != nullptr && preview != nullptr && half != nullptr && quarter != nullptr &&
          quad != nullptr && quality != nullptr);
    if (list == nullptr || busy == nullptr || preview == nullptr || half == nullptr || quarter == nullptr ||
        quad == nullptr || quality == nullptr) {
      return;
    }
    const auto spinning = [busy] { return busy->property("webExportBusy").toBool(); };
    CHECK(wait_until([&] { return preview->property("webExportShowsCurrent").toBool(); }));
    CHECK(!spinning());
    // Visiting AVIF queues its slow background encode; back on JPEG the shown result is ready
    // and the spinner stops though AVIF may still be encoding.
    list->setCurrentItem(row_named(list, QStringLiteral("AVIF")));
    QApplication::processEvents();
    list->setCurrentItem(row_named(list, QStringLiteral("JPEG")));
    quality->setValue(61);
    QApplication::processEvents();
    CHECK(spinning());
    CHECK(wait_until([&] { return preview->property("webExportShowsCurrent").toBool(); }));
    CHECK(!spinning());
    // 4-up: spinning until both tiles have landed, then stopped.
    quad->click();
    QApplication::processEvents();
    CHECK(wait_until([&] {
      return half->property("webExportShowsCurrent").toBool() && quarter->property("webExportShowsCurrent").toBool();
    }));
    CHECK(!spinning());
    // A change made while encoding supersedes the old jobs; it still ends stopped.
    for (const int value : {40, 45, 50}) {
      quality->setValue(value);
      QApplication::processEvents();
    }
    CHECK(spinning());
    CHECK(wait_until([&] {
      return preview->property("webExportShowsCurrent").toBool() && half->property("webExportShowsCurrent").toBool() &&
             quarter->property("webExportShowsCurrent").toBool();
    }));
    CHECK(wait_until([&] { return !spinning(); }, 2000));
  });
}

}  // namespace

std::vector<patchy::test::TestCase> web_export_tests() {
  return {
      {"ui_web_export_defaults_and_options", ui_web_export_defaults_and_options},
      {"ui_web_export_jpeg_progressive_and_optimize_read_back", ui_web_export_jpeg_progressive_and_optimize_read_back},
      {"ui_web_export_engine_drops_stale_requests", ui_web_export_engine_drops_stale_requests},
      {"ui_web_export_animation_frames_read_back", ui_web_export_animation_frames_read_back},
      {"ui_web_export_play_count_and_fill_read_back", ui_web_export_play_count_and_fill_read_back},
      {"ui_web_export_animation_stale_frames_are_discarded", ui_web_export_animation_stale_frames_are_discarded},
      {"ui_web_export_still_png_is_unchanged_by_animation", ui_web_export_still_png_is_unchanged_by_animation},
      {"ui_web_export_animation_plays_in_the_preview", ui_web_export_animation_plays_in_the_preview},
      {"ui_web_export_quad_tiles_follow_the_animation_frame", ui_web_export_quad_tiles_follow_the_animation_frame},
      {"ui_web_export_avif_and_jxl_read_back", ui_web_export_avif_and_jxl_read_back},
      {"ui_web_export_slow_encode_never_blocks_the_selected_format",
       ui_web_export_slow_encode_never_blocks_the_selected_format},
      {"ui_web_export_palette_png_and_gif_read_back", ui_web_export_palette_png_and_gif_read_back},
      {"ui_web_export_transparency_fill_in_the_dialog", ui_web_export_transparency_fill_in_the_dialog},
      {"ui_web_export_layers_as_animated_open_export_for_web", ui_web_export_layers_as_animated_open_export_for_web},
      {"ui_web_export_wheel_over_unfocused_control_scrolls_the_settings",
       ui_web_export_wheel_over_unfocused_control_scrolls_the_settings},
      {"ui_web_export_metadata_choices_and_thumbnail_with_none", ui_web_export_metadata_choices_and_thumbnail_with_none},
      {"ui_web_export_dialog_previews_and_saves", ui_web_export_dialog_previews_and_saves},
      {"ui_web_export_format_order_quality_steps_and_presets", ui_web_export_format_order_quality_steps_and_presets},
      {"ui_web_export_dialog_keeps_size_and_compare_mode_through_cancel",
       ui_web_export_dialog_keeps_size_and_compare_mode_through_cancel},
      {"ui_web_export_quad_compare_and_presets_in_the_dialog", ui_web_export_quad_compare_and_presets_in_the_dialog},
      {"ui_web_export_settings_column_fits_every_language", ui_web_export_settings_column_fits_every_language},
      {"ui_web_export_preview_buttons_fit_at_minimum_size", ui_web_export_preview_buttons_fit_at_minimum_size},
      {"ui_web_export_view_row_order_and_zoom_controls", ui_web_export_view_row_order_and_zoom_controls},
      {"ui_web_export_view_modes_keep_the_dialog_size", ui_web_export_view_modes_keep_the_dialog_size},
      {"ui_web_export_jpeg_keeps_the_transparency_choice", ui_web_export_jpeg_keeps_the_transparency_choice},
      {"ui_web_export_spinner_stops_when_the_shown_results_arrive",
       ui_web_export_spinner_stops_when_the_shown_results_arrive},
  };
}
