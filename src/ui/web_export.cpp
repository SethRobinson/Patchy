#include "ui/web_export.hpp"

#include "color/rgb_profiles.hpp"
#include "formats/animation_timing.hpp"
#include "formats/apng_writer.hpp"
#include "formats/avif_document_io.hpp"
#include "ui/apng_reader.hpp"
#include "formats/webp_animation_io.hpp"
#include "ui/edit_conversions.hpp"
#include "formats/jxl_document_io.hpp"
#include "ui/action_icons.hpp"
#include "ui/activity_spinner.hpp"
#include "ui/app_settings.hpp"
#include "ui/background_workers.hpp"
#include "ui/color_panel.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/image_document_io.hpp"
#include "ui/image_save_option_keys.hpp"
#include "ui/image_save_options_dialog.hpp"
#include "ui/measurement_units.hpp"
#include "ui/theme_palette.hpp"
#include "ui/theme_qss.hpp"
#include "ui/unit_spin_box.hpp"
#include "ui/zoomable_image_preview.hpp"

#include <QBuffer>
#include <QButtonGroup>
#include <QImageReader>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QScopeGuard>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <deque>
#include <functional>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

// Settings keys and their tokens are persisted identifiers (docs/web-export.md): never rename.
constexpr const char* kFormatKey = "exportForWeb/format";
constexpr const char* kJpegQualityKey = "exportForWeb/jpegQuality";
constexpr const char* kJpegProgressiveKey = "exportForWeb/jpegProgressive";
constexpr const char* kJpegOptimizeKey = "exportForWeb/jpegOptimize";
constexpr const char* kJpegBackgroundKey = "exportForWeb/jpegBackground";
constexpr const char* kWebpQualityKey = "exportForWeb/webpQuality";
constexpr const char* kWebpLosslessKey = "exportForWeb/webpLossless";
constexpr const char* kWebpAnimateKey = "exportForWeb/webpAnimate";
constexpr const char* kMetadataKey = "exportForWeb/metadata";
constexpr const char* kThumbnailKey = "exportForWeb/thumbnail";
constexpr const char* kResampleKey = "exportForWeb/resample";
constexpr const char* kTrimKey = "exportForWeb/trim";
constexpr const char* kConvertToSrgbKey = "exportForWeb/convertToSrgb";
constexpr const char* kCompareKey = "exportForWeb/compare";
constexpr const char* kRevealKey = "exportForWeb/revealWhenDone";
constexpr const char* kAvifQualityKey = "exportForWeb/avifQuality";
constexpr const char* kAvifSpeedKey = "exportForWeb/avifSpeed";
constexpr const char* kAvifLosslessKey = "exportForWeb/avifLossless";
constexpr const char* kJxlQualityKey = "exportForWeb/jxlQuality";
constexpr const char* kJxlEffortKey = "exportForWeb/jxlEffort";
constexpr const char* kJxlLosslessKey = "exportForWeb/jxlLossless";
constexpr const char* kJxlAnimateKey = "exportForWeb/jxlAnimate";
constexpr const char* kJxlFrameDelayKey = "exportForWeb/jxlFrameDelayMs";
constexpr const char* kPngColorsKey = "exportForWeb/pngColors";
constexpr const char* kPngAnimateKey = "exportForWeb/pngAnimate";
constexpr const char* kFillTransparentKey = "exportForWeb/fillTransparent";
constexpr const char* kPngFrameDelayKey = "exportForWeb/pngFrameDelayMs";
constexpr const char* kPngPaletteColorsKey = "exportForWeb/pngPaletteColors";
constexpr const char* kPngDitherKey = "exportForWeb/pngDither";
constexpr const char* kPngDitherAmountKey = "exportForWeb/pngDitherAmount";
constexpr const char* kGifColorsKey = "exportForWeb/gifColors";
constexpr const char* kGifDitherKey = "exportForWeb/gifDither";
constexpr const char* kGifDitherAmountKey = "exportForWeb/gifDitherAmount";
constexpr const char* kGifAnimateKey = "exportForWeb/gifAnimate";
constexpr const char* kGifFrameDelayKey = "exportForWeb/gifFrameDelayMs";

constexpr int kPreviewDebounceMs = 200;
constexpr int kMaxOutputPixels = 100000;
constexpr int kMaxPixelScale = 16;
// Finished encodes kept by signature, so switching back to a format or setting shows its
// result at once.
constexpr std::size_t kCachedResults = 12;

[[nodiscard]] QString resample_key(WebExportResample resample) {
  return resample == WebExportResample::PixelArt ? QStringLiteral("pixelArt") : QStringLiteral("smooth");
}

[[nodiscard]] QString compare_key(WebExportCompare compare) {
  switch (compare) {
    case WebExportCompare::SideBySide:
      return QStringLiteral("sideBySide");
    case WebExportCompare::ResultOnly:
      return QStringLiteral("result");
    case WebExportCompare::Split:
      break;
  }
  return QStringLiteral("split");
}

[[nodiscard]] WebExportCompare compare_from_key(const QString& key) {
  if (key == QStringLiteral("sideBySide")) {
    return WebExportCompare::SideBySide;
  }
  if (key == QStringLiteral("result")) {
    return WebExportCompare::ResultOnly;
  }
  return WebExportCompare::Split;
}

[[nodiscard]] QString geometry_signature(const ImageSaveOptions& geometry, bool convert_to_srgb) {
  return QStringLiteral("w%1h%2s%3t%4c%5")
      .arg(geometry.export_width)
      .arg(geometry.export_height)
      .arg(geometry.export_scale)
      .arg(geometry.export_trim_transparent ? 1 : 0)
      .arg(convert_to_srgb ? 1 : 0);
}

[[nodiscard]] QImage display_image(QImage image) {
  if (image.isNull()) {
    return image;
  }
  return image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

// The distinct opaque colors of an image, sorted, at most 256 (a palette image's colors).
[[nodiscard]] std::vector<QRgb> used_colors(const QImage& image) {
  std::set<QRgb> colors;
  for (int y = 0; y < image.height() && colors.size() <= 256; ++y) {
    const auto* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
    for (int x = 0; x < image.width(); ++x) {
      if (qAlpha(row[x]) == 255) {
        colors.insert(row[x]);
        if (colors.size() > 256) {
          break;
        }
      }
    }
  }
  return {colors.begin(), colors.end()};
}

[[nodiscard]] QString data_size_text(std::size_t bytes) {
  return QLocale().formattedDataSize(static_cast<qint64>(bytes), 1, QLocale::DataSizeTraditionalFormat);
}

}  // namespace

// The encoded file read back for display: Qt's readers for JPEG, WebP, PNG and GIF, Patchy's
// own for AVIF and JPEG XL (Qt has no plug-in for them). The first frame of an animation.
QImage decode_web_export_bytes(WebExportFormat format, std::span<const std::uint8_t> bytes) {
  if (format == WebExportFormat::Avif || format == WebExportFormat::Jxl) {
    auto read = format == WebExportFormat::Avif ? avif::read_avif(bytes) : jxl::read_jxl(bytes);
    std::optional<Document> narrowed;
    const auto& eight_bit = document_for_8bit_writer(read.document, narrowed);
    // Animations open one layer per frame, frame 1 on top: show that layer alone.
    if (eight_bit.layers().size() > 1) {
      const auto& top = std::as_const(eight_bit).layers().back();
      return display_image(render_layer_isolated(eight_bit, top));
    }
    return display_image(qimage_from_document(eight_bit, true));
  }
  const auto extension = web_export_extension(format).toLatin1();
  return display_image(QImage::fromData(
      QByteArrayView(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size())),
      extension.constData()));
}

WebExportAnimation decode_web_export_animation(WebExportFormat format, std::span<const std::uint8_t> bytes,
                                               std::size_t max_frames) {
  WebExportAnimation animation;
  max_frames = std::max<std::size_t>(1, max_frames);
  if (format == WebExportFormat::Gif) {
    // Qt's GIF reader, as File > Open: composited full-canvas frames, nextImageDelay() the
    // just-read frame's delay.
    QByteArray data(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size()));
    QBuffer buffer(&data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "gif");
    animation.total = std::max(1, reader.imageCount());
    for (int index = 0; index < animation.total; ++index) {
      auto image = reader.read();
      if (image.isNull()) {
        break;
      }
      if (animation.frames.size() < max_frames) {
        animation.frames.push_back(display_image(std::move(image)));
        animation.delays_ms.push_back(std::max(0, reader.nextImageDelay()));
      }
    }
  } else if (format == WebExportFormat::Webp) {
    const bool animated = webp::decode_animation(
        bytes, std::uint64_t{4} * 1024 * 1024 * 1024,
        [&animation](const webp::AnimationInfo& info) { animation.total = static_cast<int>(info.frame_count); },
        [&animation, max_frames](PixelBuffer pixels, std::uint32_t delay_ms) {
          if (animation.frames.size() < max_frames) {
            animation.frames.push_back(display_image(display_qimage_from_pixel_buffer(pixels)));
            animation.delays_ms.push_back(static_cast<int>(delay_ms));
          }
        });
    if (!animated) {
      animation.frames = {decode_web_export_bytes(format, bytes)};
      animation.delays_ms = {0};
      animation.total = 1;
    }
  } else if (format == WebExportFormat::Png && apng::is_animated_png(bytes)) {
    // Patchy's APNG reader, as File > Open: composited full-canvas frames, millisecond delays.
    auto read = apng::read_animated_png(bytes);
    animation.total = static_cast<int>(read.frames.size());
    for (auto& frame : read.frames) {
      if (animation.frames.size() >= max_frames) {
        break;
      }
      animation.frames.push_back(display_image(std::move(frame.image)));
      animation.delays_ms.push_back(static_cast<int>(frame.delay_ms));
    }
  } else if (format == WebExportFormat::Jxl) {
    // libjxl through Patchy's reader: one full-canvas layer per frame, frame 1 on top, each
    // named with its delay ("Frame 2 0.25s").
    auto read = jxl::read_jxl(bytes);
    std::optional<Document> narrowed;
    const auto& eight_bit = document_for_8bit_writer(read.document, narrowed);
    const auto& layers = std::as_const(eight_bit).layers();
    animation.total = static_cast<int>(layers.size());
    for (auto it = layers.rbegin(); it != layers.rend() && animation.frames.size() < max_frames; ++it) {
      animation.frames.push_back(display_image(render_layer_isolated(eight_bit, *it)));
      animation.delays_ms.push_back(static_cast<int>(animation::parse_layer_name_delay_ms(it->name()).value_or(0)));
    }
  } else {
    animation.frames = {decode_web_export_bytes(format, bytes)};
    animation.delays_ms = {0};
    animation.total = 1;
  }
  return animation;
}

const std::vector<WebExportFormat>& web_export_formats() {
  static const std::vector<WebExportFormat> formats = [] {
    std::vector<WebExportFormat> list{WebExportFormat::Jpeg, WebExportFormat::Webp};
    if (avif::is_available()) {
      list.push_back(WebExportFormat::Avif);
    }
    list.push_back(WebExportFormat::Jxl);
    list.push_back(WebExportFormat::Png);
    list.push_back(WebExportFormat::Gif);
    return list;
  }();
  return formats;
}

bool web_export_format_is_slow(WebExportFormat format) {
  return format == WebExportFormat::Avif || format == WebExportFormat::Jxl;
}

QString web_export_format_key(WebExportFormat format) {
  switch (format) {
    case WebExportFormat::Webp:
      return QStringLiteral("webp");
    case WebExportFormat::Png:
      return QStringLiteral("png");
    case WebExportFormat::Gif:
      return QStringLiteral("gif");
    case WebExportFormat::Avif:
      return QStringLiteral("avif");
    case WebExportFormat::Jxl:
      return QStringLiteral("jxl");
    case WebExportFormat::Jpeg:
      break;
  }
  return QStringLiteral("jpeg");
}

std::optional<WebExportFormat> web_export_format_from_key(const QString& key) {
  for (const auto format : web_export_formats()) {
    if (web_export_format_key(format) == key) {
      return format;
    }
  }
  return std::nullopt;
}

QString web_export_extension(WebExportFormat format) {
  switch (format) {
    case WebExportFormat::Webp:
      return QStringLiteral("webp");
    case WebExportFormat::Png:
      return QStringLiteral("png");
    case WebExportFormat::Gif:
      return QStringLiteral("gif");
    case WebExportFormat::Avif:
      return QStringLiteral("avif");
    case WebExportFormat::Jxl:
      return QStringLiteral("jxl");
    case WebExportFormat::Jpeg:
      break;
  }
  return QStringLiteral("jpg");
}

QString web_export_format_name(WebExportFormat format) {
  switch (format) {
    case WebExportFormat::Webp:
      return QStringLiteral("WebP");
    case WebExportFormat::Png:
      return QStringLiteral("PNG");
    case WebExportFormat::Gif:
      return QStringLiteral("GIF");
    case WebExportFormat::Avif:
      return QStringLiteral("AVIF");
    case WebExportFormat::Jxl:
      return QStringLiteral("JPEG XL");
    case WebExportFormat::Jpeg:
      break;
  }
  return QStringLiteral("JPEG");
}

WebExportSettings load_web_export_settings() {
  WebExportSettings settings;
  auto stored = app_settings();
  settings.format = web_export_format_from_key(stored.value(QLatin1String(kFormatKey)).toString())
                        .value_or(WebExportFormat::Jpeg);
  settings.jpeg_quality = std::clamp(stored.value(QLatin1String(kJpegQualityKey), 80).toInt(), 1, 100);
  settings.jpeg_progressive = stored.value(QLatin1String(kJpegProgressiveKey), true).toBool();
  settings.jpeg_optimize = stored.value(QLatin1String(kJpegOptimizeKey), true).toBool();
  const QColor background(stored.value(QLatin1String(kJpegBackgroundKey)).toString());
  settings.jpeg_background = background.isValid() ? background : QColor(Qt::white);
  settings.webp_quality = std::clamp(stored.value(QLatin1String(kWebpQualityKey), 75).toInt(), 0, 100);
  settings.webp_lossless = stored.value(QLatin1String(kWebpLosslessKey), false).toBool();
  settings.webp_animate = stored.value(QLatin1String(kWebpAnimateKey), false).toBool();
  settings.avif_quality = std::clamp(stored.value(QLatin1String(kAvifQualityKey), 80).toInt(), 0, 100);
  settings.avif_speed = std::clamp(stored.value(QLatin1String(kAvifSpeedKey), 6).toInt(), 0, 10);
  settings.avif_lossless = stored.value(QLatin1String(kAvifLosslessKey), false).toBool();
  settings.jxl_quality = std::clamp(stored.value(QLatin1String(kJxlQualityKey), 90).toInt(), 1, 100);
  settings.jxl_effort = std::clamp(stored.value(QLatin1String(kJxlEffortKey), 7).toInt(), 1, 9);
  settings.jxl_lossless = stored.value(QLatin1String(kJxlLosslessKey), false).toBool();
  settings.jxl_animate = stored.value(QLatin1String(kJxlAnimateKey), false).toBool();
  settings.png_palette = stored.value(QLatin1String(kPngColorsKey)).toString() == QStringLiteral("palette");
  settings.png_animate = stored.value(QLatin1String(kPngAnimateKey), false).toBool();
  settings.png_colors = std::clamp(stored.value(QLatin1String(kPngPaletteColorsKey), 256).toInt(), 2, 256);
  settings.png_dither = stored.value(QLatin1String(kPngDitherKey)).toString() == QStringLiteral("diffusion");
  settings.png_dither_amount = std::clamp(stored.value(QLatin1String(kPngDitherAmountKey), 100).toInt(), 0, 100);
  settings.gif_colors = std::clamp(stored.value(QLatin1String(kGifColorsKey), 256).toInt(), 2, 256);
  settings.gif_dither = stored.value(QLatin1String(kGifDitherKey)).toString() == QStringLiteral("diffusion");
  settings.gif_dither_amount = std::clamp(stored.value(QLatin1String(kGifDitherAmountKey), 100).toInt(), 0, 100);
  settings.gif_animate = stored.value(QLatin1String(kGifAnimateKey), false).toBool();
  // The default frame delay is the animated exports' shared one; the older per-format
  // exportForWeb/*FrameDelayMs keys are read only when it was never set.
  const auto legacy_delay = stored.value(QLatin1String(kGifFrameDelayKey),
                                         stored.value(QLatin1String(kJxlFrameDelayKey),
                                                      stored.value(QLatin1String(kPngFrameDelayKey), 100)));
  settings.animation_frame_delay_ms = std::clamp(
      stored.value(QStringLiteral("saveOptions/animationFrameDelayMs"),
                   stored.contains(QStringLiteral("saveOptions/gifFrameDelayCs"))
                       ? QVariant(stored.value(QStringLiteral("saveOptions/gifFrameDelayCs")).toInt() * 10)
                       : legacy_delay)
          .toInt(),
      0, static_cast<int>(animation::kMaxFrameDelayMs));
  settings.fill_transparent = stored.value(QLatin1String(kFillTransparentKey), false).toBool();
  settings.metadata = metadata_policy_from_key(stored.value(QLatin1String(kMetadataKey)).toString())
                          .value_or(metadata::MetadataPolicy::None);
  settings.thumbnail = stored.value(QLatin1String(kThumbnailKey), false).toBool();
  settings.resample = stored.value(QLatin1String(kResampleKey)).toString() == QStringLiteral("pixelArt")
                          ? WebExportResample::PixelArt
                          : WebExportResample::Smooth;
  settings.trim = stored.value(QLatin1String(kTrimKey), false).toBool();
  settings.convert_to_srgb = stored.value(QLatin1String(kConvertToSrgbKey), true).toBool();
  settings.compare = compare_from_key(stored.value(QLatin1String(kCompareKey)).toString());
  settings.reveal_when_done = stored.value(QLatin1String(kRevealKey), false).toBool();
  return settings;
}

void save_web_export_settings(const WebExportSettings& settings) {
  auto stored = app_settings();
  stored.setValue(QLatin1String(kFormatKey), web_export_format_key(settings.format));
  stored.setValue(QLatin1String(kJpegQualityKey), settings.jpeg_quality);
  stored.setValue(QLatin1String(kJpegProgressiveKey), settings.jpeg_progressive);
  stored.setValue(QLatin1String(kJpegOptimizeKey), settings.jpeg_optimize);
  stored.setValue(QLatin1String(kJpegBackgroundKey), settings.jpeg_background.name(QColor::HexRgb));
  stored.setValue(QLatin1String(kWebpQualityKey), settings.webp_quality);
  stored.setValue(QLatin1String(kWebpLosslessKey), settings.webp_lossless);
  stored.setValue(QLatin1String(kWebpAnimateKey), settings.webp_animate);
  stored.setValue(QLatin1String(kAvifQualityKey), settings.avif_quality);
  stored.setValue(QLatin1String(kAvifSpeedKey), settings.avif_speed);
  stored.setValue(QLatin1String(kAvifLosslessKey), settings.avif_lossless);
  stored.setValue(QLatin1String(kJxlQualityKey), settings.jxl_quality);
  stored.setValue(QLatin1String(kJxlEffortKey), settings.jxl_effort);
  stored.setValue(QLatin1String(kJxlLosslessKey), settings.jxl_lossless);
  stored.setValue(QLatin1String(kJxlAnimateKey), settings.jxl_animate);
  stored.setValue(QLatin1String(kJxlFrameDelayKey), settings.animation_frame_delay_ms);
  stored.setValue(QLatin1String(kPngColorsKey), settings.png_palette ? QStringLiteral("palette") : QStringLiteral("full"));
  stored.setValue(QLatin1String(kPngAnimateKey), settings.png_animate);
  stored.setValue(QLatin1String(kPngFrameDelayKey), settings.animation_frame_delay_ms);
  stored.setValue(QLatin1String(kPngPaletteColorsKey), settings.png_colors);
  stored.setValue(QLatin1String(kPngDitherKey), settings.png_dither ? QStringLiteral("diffusion") : QStringLiteral("none"));
  stored.setValue(QLatin1String(kPngDitherAmountKey), settings.png_dither_amount);
  stored.setValue(QLatin1String(kGifColorsKey), settings.gif_colors);
  stored.setValue(QLatin1String(kGifDitherKey), settings.gif_dither ? QStringLiteral("diffusion") : QStringLiteral("none"));
  stored.setValue(QLatin1String(kGifDitherAmountKey), settings.gif_dither_amount);
  stored.setValue(QLatin1String(kGifAnimateKey), settings.gif_animate);
  stored.setValue(QLatin1String(kGifFrameDelayKey), settings.animation_frame_delay_ms);
  // The shared default of the animated exports, written the way the GIF dialog writes it.
  stored.setValue(QStringLiteral("saveOptions/animationFrameDelayMs"), settings.animation_frame_delay_ms);
  stored.setValue(QStringLiteral("saveOptions/gifFrameDelayCs"),
                  std::clamp((settings.animation_frame_delay_ms + 5) / 10, 0, 0xffff));
  stored.setValue(QLatin1String(kFillTransparentKey), settings.fill_transparent);
  stored.setValue(QLatin1String(kMetadataKey), metadata_policy_key(settings.metadata));
  stored.setValue(QLatin1String(kThumbnailKey), settings.thumbnail);
  stored.setValue(QLatin1String(kResampleKey), resample_key(settings.resample));
  stored.setValue(QLatin1String(kTrimKey), settings.trim);
  stored.setValue(QLatin1String(kConvertToSrgbKey), settings.convert_to_srgb);
  stored.setValue(QLatin1String(kCompareKey), compare_key(settings.compare));
  stored.setValue(QLatin1String(kRevealKey), settings.reveal_when_done);
}

ImageSaveOptions web_export_geometry_options(const WebExportSettings& settings, QSize document_size) {
  ImageSaveOptions options;
  options.export_trim_transparent = settings.trim;
  if (settings.resample == WebExportResample::PixelArt) {
    options.export_scale = std::clamp(settings.pixel_scale, 1, kMaxPixelScale);
  } else if (settings.width > 0 && settings.height > 0 &&
             (settings.width != document_size.width() || settings.height != document_size.height())) {
    options.export_width = settings.width;
    options.export_height = settings.height;
  }
  return options;
}

ImageSaveOptions web_export_image_options(const WebExportSettings& settings, WebExportFormat format,
                                          QSize document_size) {
  auto options = web_export_geometry_options(settings, document_size);
  options.metadata_policy = settings.metadata;
  // Keep transparent or fill with the one background color; JPEG always fills (below).
  options.export_fill_transparent = settings.fill_transparent;
  options.export_background_color = settings.jpeg_background;
  const auto delay_ms = std::clamp(settings.animation_frame_delay_ms, 0, static_cast<int>(animation::kMaxFrameDelayMs));
  options.export_reveal_in_file_explorer = settings.reveal_when_done;
  switch (format) {
    case WebExportFormat::Jpeg:
      options.jpeg_quality = std::clamp(settings.jpeg_quality, 1, 100);
      options.jpeg_progressive = settings.jpeg_progressive;
      options.jpeg_optimize = settings.jpeg_optimize;
      options.jpeg_exif_thumbnail = settings.thumbnail;
      // The thumbnail is its own choice: with Metadata None it comes in a minimal EXIF.
      options.jpeg_exif_thumbnail_without_metadata = settings.thumbnail;
      // JPEG has no alpha: transparent areas take the background color.
      options.export_fill_transparent = true;
      options.export_background_color = settings.jpeg_background;
      break;
    case WebExportFormat::Webp:
      options.webp_quality = std::clamp(settings.webp_quality, 0, 100);
      options.webp_lossless = settings.webp_lossless;
      options.webp_animate = settings.webp_animate;
      options.animation_frame_delay_ms = delay_ms;
      options.webp_loop_count = std::clamp(settings.webp_loop_count, 0, 65535);
      break;
    case WebExportFormat::Png:
      if (settings.png_animate) {
        // APNG is written in full color (8-bit RGBA).
        options.apng_animate = true;
        options.animation_frame_delay_ms = delay_ms;
        options.apng_loop_count = std::clamp(settings.apng_loop_count, 0, 65535);
        break;
      }
      options.png_palette = settings.png_palette;
      if (settings.png_palette) {
        options.palette_colors = std::clamp(settings.png_colors, 2, 256);
        options.palette_dither = settings.png_dither;
        options.palette_dither_amount = std::clamp(settings.png_dither_amount, 0, 100);
      }
      break;
    case WebExportFormat::Avif:
      options.avif_quality = std::clamp(settings.avif_quality, 0, 100);
      options.avif_speed = std::clamp(settings.avif_speed, 0, 10);
      options.avif_lossless = settings.avif_lossless;
      break;
    case WebExportFormat::Jxl:
      options.jxl_quality = std::clamp(settings.jxl_quality, 1, 100);
      options.jxl_effort = std::clamp(settings.jxl_effort, 1, 9);
      options.jxl_lossless = settings.jxl_lossless;
      options.jxl_animate = settings.jxl_animate;
      options.animation_frame_delay_ms = delay_ms;
      options.jxl_loop_count = std::clamp(settings.jxl_loop_count, 0, 65535);
      break;
    case WebExportFormat::Gif:
      options.palette_colors = std::clamp(settings.gif_colors, 2, 256);
      options.palette_dither = settings.gif_dither;
      options.palette_dither_amount = std::clamp(settings.gif_dither_amount, 0, 100);
      options.gif_animate = settings.gif_animate;
      // GIF stores centiseconds, rounded as the GIF dialog does.
      options.gif_frame_delay_cs = std::clamp((delay_ms + 5) / 10, 0, 0xffff);
      options.animation_frame_delay_ms = options.gif_frame_delay_cs * 10;
      break;
  }
  return options;
}

QString web_export_format_summary(const WebExportSettings& settings, WebExportFormat format) {
  switch (format) {
    case WebExportFormat::Jpeg:
      return settings.jpeg_progressive ? QObject::tr("Quality %1, progressive").arg(settings.jpeg_quality)
                                       : QObject::tr("Quality %1").arg(settings.jpeg_quality);
    case WebExportFormat::Webp: {
      const auto base = settings.webp_lossless ? QObject::tr("Lossless")
                                               : QObject::tr("Quality %1").arg(settings.webp_quality);
      return settings.webp_animate ? QObject::tr("%1, animated").arg(base) : base;
    }
    case WebExportFormat::Png:
      if (settings.png_animate) {
        return QObject::tr("%1, animated").arg(QObject::tr("Full color"));
      }
      if (settings.png_palette) {
        return settings.png_dither ? QObject::tr("%n colors, diffusion", nullptr, settings.png_colors)
                                   : QObject::tr("%n colors, no dither", nullptr, settings.png_colors);
      }
      break;
    case WebExportFormat::Avif:
      return settings.avif_lossless
                 ? QObject::tr("Lossless, speed %1").arg(settings.avif_speed)
                 : QObject::tr("Quality %1, speed %2").arg(settings.avif_quality).arg(settings.avif_speed);
    case WebExportFormat::Jxl: {
      const auto base = settings.jxl_lossless
                            ? QObject::tr("Lossless, effort %1").arg(settings.jxl_effort)
                            : QObject::tr("Quality %1, effort %2").arg(settings.jxl_quality).arg(settings.jxl_effort);
      return settings.jxl_animate ? QObject::tr("%1, animated").arg(base) : base;
    }
    case WebExportFormat::Gif: {
      const auto base = settings.gif_dither ? QObject::tr("%n colors, diffusion", nullptr, settings.gif_colors)
                                            : QObject::tr("%n colors, no dither", nullptr, settings.gif_colors);
      return settings.gif_animate ? QObject::tr("%1, animated").arg(base) : base;
    }
  }
  return QObject::tr("Full color");
}

WebExportJob web_export_job(const WebExportSettings& settings, WebExportFormat format, QSize document_size) {
  WebExportJob job;
  job.format = format;
  job.geometry = web_export_geometry_options(settings, document_size);
  job.options = web_export_image_options(settings, format, document_size);
  job.convert_to_srgb = settings.convert_to_srgb;
  job.animate = (format == WebExportFormat::Webp && settings.webp_animate) ||
                (format == WebExportFormat::Gif && settings.gif_animate) ||
                (format == WebExportFormat::Jxl && settings.jxl_animate) ||
                (format == WebExportFormat::Png && settings.png_animate);
  auto signature = web_export_format_key(format) + QLatin1Char('|') +
                   geometry_signature(job.geometry, job.convert_to_srgb) + QLatin1Char('|') +
                   metadata_policy_key(settings.metadata) +
                   QStringLiteral("|f%1%2").arg(job.options.export_fill_transparent ? 1 : 0)
                       .arg(job.options.export_fill_transparent ? job.options.export_background_color.name(QColor::HexRgb)
                                                                : QString());
  if (job.animate) {
    signature += QStringLiteral("|d%1l%2.%3.%4")
                     .arg(job.options.animation_frame_delay_ms)
                     .arg(job.options.webp_loop_count)
                     .arg(job.options.jxl_loop_count)
                     .arg(job.options.apng_loop_count);
  }
  switch (format) {
    case WebExportFormat::Jpeg:
      signature += QStringLiteral("|q%1p%2o%3t%4b%5")
                       .arg(job.options.jpeg_quality)
                       .arg(job.options.jpeg_progressive ? 1 : 0)
                       .arg(job.options.jpeg_optimize ? 1 : 0)
                       .arg(job.options.jpeg_exif_thumbnail ? 1 : 0)
                       .arg(settings.jpeg_background.name(QColor::HexRgb));
      break;
    case WebExportFormat::Webp:
      signature += QStringLiteral("|q%1l%2a%3")
                       .arg(job.options.webp_quality)
                       .arg(job.options.webp_lossless ? 1 : 0)
                       .arg(job.animate ? 1 : 0);
      break;
    case WebExportFormat::Png:
      if (job.animate) {
        signature += QStringLiteral("|n1f%1").arg(job.options.animation_frame_delay_ms);
      }
      if (job.options.png_palette) {
        signature += QStringLiteral("|c%1d%2a%3")
                         .arg(job.options.palette_colors)
                         .arg(job.options.palette_dither ? 1 : 0)
                         .arg(job.options.palette_dither_amount);
      }
      break;
    case WebExportFormat::Avif:
      signature += QStringLiteral("|q%1s%2l%3")
                       .arg(job.options.avif_quality)
                       .arg(job.options.avif_speed)
                       .arg(job.options.avif_lossless ? 1 : 0);
      break;
    case WebExportFormat::Jxl:
      signature += QStringLiteral("|q%1e%2l%3n%4f%5")
                       .arg(job.options.jxl_quality)
                       .arg(job.options.jxl_effort)
                       .arg(job.options.jxl_lossless ? 1 : 0)
                       .arg(job.animate ? 1 : 0)
                       .arg(job.options.animation_frame_delay_ms);
      break;
    case WebExportFormat::Gif:
      signature += QStringLiteral("|c%1d%2a%3n%4f%5")
                       .arg(job.options.palette_colors)
                       .arg(job.options.palette_dither ? 1 : 0)
                       .arg(job.options.palette_dither_amount)
                       .arg(job.animate ? 1 : 0)
                       .arg(job.options.gif_frame_delay_cs);
      break;
  }
  job.signature = signature;
  return job;
}

bool web_export_has_non_srgb_profile(const Document& document) {
  return rgb_profile_needs_conversion(document.color_state().embedded_icc_profile);
}

// ---------------------------------------------------------------------------------------
// Preview engine

struct WebExportPreviewEngine::State {
  std::shared_ptr<const Document> source;
  // Worker side. The mutex guards the narrowed source and the prepared cache; encodes run
  // outside it on immutable shared documents, so the two lanes encode at the same time.
  std::mutex worker_mutex;
  std::shared_ptr<const Document> narrowed;
  QString prepared_key;
  std::shared_ptr<const Document> prepared;
  QImage prepared_image;
  std::vector<std::string> prepared_notices;
  // UI thread. Lane 0 runs the selected format's job, lane 1 the other rows one at a time,
  // so a slow AVIF or JPEG XL size never holds back the selected format.
  struct Lane {
    std::deque<WebExportJob> queue;
    bool in_flight{false};
  };
  std::array<Lane, 2> lanes;
  std::map<QString, WebExportResult> results;
  std::deque<QString> result_order;
  bool closed{false};
  std::function<void(const WebExportJob&, const WebExportResult&)> callback;
  std::atomic<std::uint64_t> generation{0};
  std::atomic<int> encodes{0};
};

namespace {

void start_next_web_export_job(const std::shared_ptr<WebExportPreviewEngine::State>& state, std::size_t lane);

void store_web_export_result(WebExportPreviewEngine::State& state, const QString& signature,
                             const WebExportResult& result) {
  if (state.results.find(signature) == state.results.end()) {
    state.result_order.push_back(signature);
  }
  state.results[signature] = result;
  while (state.result_order.size() > kCachedResults) {
    state.results.erase(state.result_order.front());
    state.result_order.pop_front();
  }
}

// The 8-bit source and the prepared (resized, trimmed, sRGB) document for a job's geometry,
// made once and shared by both lanes.
struct PreparedSources {
  std::shared_ptr<const Document> narrowed;
  std::shared_ptr<const Document> prepared;
  QImage prepared_image;
  std::vector<std::string> notices;
};

PreparedSources prepared_sources(WebExportPreviewEngine::State& state, const WebExportJob& job) {
  const std::lock_guard lock(state.worker_mutex);
  if (state.narrowed == nullptr) {
    std::optional<Document> narrowed;
    const auto& eight_bit = document_for_8bit_writer(*state.source, narrowed);
    state.narrowed = narrowed.has_value() ? std::make_shared<const Document>(std::move(*narrowed))
                                          : std::shared_ptr<const Document>(state.source, &eight_bit);
  }
  const auto key = geometry_signature(job.geometry, job.convert_to_srgb);
  if (state.prepared == nullptr || state.prepared_key != key) {
    state.prepared_notices.clear();
    state.prepared =
        WebExportPreviewEngine::prepare(*state.narrowed, job.geometry, job.convert_to_srgb, &state.prepared_notices);
    state.prepared_image = display_image(flat_export_qimage(*state.prepared, true));
    state.prepared_key = key;
  }
  return PreparedSources{state.narrowed, state.prepared, state.prepared_image, state.prepared_notices};
}

void start_next_web_export_job(const std::shared_ptr<WebExportPreviewEngine::State>& state, std::size_t lane_index) {
  auto& lane = state->lanes[lane_index];
  if (state->closed || lane.in_flight) {
    return;
  }
  while (!lane.queue.empty() && state->results.find(lane.queue.front().signature) != state->results.end()) {
    lane.queue.pop_front();
  }
  if (lane.queue.empty()) {
    return;
  }
  auto job = std::move(lane.queue.front());
  lane.queue.pop_front();
  lane.in_flight = true;
  const auto generation = state->generation.load(std::memory_order_acquire);
  auto* app = QCoreApplication::instance();
  run_tracked_background_worker([state, job = std::move(job), generation, app, lane_index]() mutable {
    std::optional<WebExportResult> result;
    // A job whose request was replaced before it started is dropped unrun.
    if (state->generation.load(std::memory_order_acquire) == generation) {
      state->encodes.fetch_add(1, std::memory_order_acq_rel);
      WebExportResult encoded;
      PreparedSources sources;
      try {
        sources = prepared_sources(*state, job);
        encoded = WebExportPreviewEngine::encode(*sources.narrowed, job, sources.prepared);
        encoded.original = encoded.original_frames.empty() ? sources.prepared_image : encoded.original_frames.front();
        encoded.notices.insert(encoded.notices.begin(), sources.notices.begin(), sources.notices.end());
      } catch (const std::exception& error) {
        encoded.error = QString::fromUtf8(error.what());
        encoded.original = sources.prepared_image;
      }
      result = std::move(encoded);
    }
    if (app == nullptr) {
      return;
    }
    QMetaObject::invokeMethod(
        app,
        [state, job = std::move(job), result = std::move(result), lane_index]() mutable {
          state->lanes[lane_index].in_flight = false;
          if (state->closed) {
            return;
          }
          if (result.has_value()) {
            if (result->error.isEmpty()) {
              store_web_export_result(*state, job.signature, *result);
            }
            if (state->callback) {
              state->callback(job, *result);
            }
          }
          start_next_web_export_job(state, lane_index);
        },
        Qt::QueuedConnection);
  });
}

}  // namespace

WebExportPreviewEngine::WebExportPreviewEngine(std::shared_ptr<const Document> source)
    : state_(std::make_shared<State>()) {
  state_->source = std::move(source);
}

WebExportPreviewEngine::~WebExportPreviewEngine() { close(); }

void WebExportPreviewEngine::set_result_callback(
    std::function<void(const WebExportJob&, const WebExportResult&)> callback) {
  state_->callback = std::move(callback);
}

void WebExportPreviewEngine::request(std::vector<WebExportJob> jobs) {
  if (state_->closed) {
    return;
  }
  state_->generation.fetch_add(1, std::memory_order_acq_rel);
  state_->lanes[0].queue.clear();
  state_->lanes[1].queue.clear();
  for (std::size_t index = 0; index < jobs.size(); ++index) {
    state_->lanes[index == 0 ? 0 : 1].queue.push_back(std::move(jobs[index]));
  }
  start_next();
}

void WebExportPreviewEngine::start_next() {
  start_next_web_export_job(state_, 0);
  start_next_web_export_job(state_, 1);
}

std::optional<WebExportResult> WebExportPreviewEngine::cached(const QString& signature) const {
  const auto found = state_->results.find(signature);
  if (found == state_->results.end()) {
    return std::nullopt;
  }
  return found->second;
}

bool WebExportPreviewEngine::busy() const {
  return std::any_of(state_->lanes.begin(), state_->lanes.end(),
                     [](const State::Lane& lane) { return lane.in_flight || !lane.queue.empty(); });
}

std::uint64_t WebExportPreviewEngine::generation() const {
  return state_->generation.load(std::memory_order_acquire);
}

int WebExportPreviewEngine::encodes_started() const { return state_->encodes.load(std::memory_order_acquire); }

void WebExportPreviewEngine::close() {
  state_->closed = true;
  state_->generation.fetch_add(1, std::memory_order_acq_rel);
  state_->lanes[0].queue.clear();
  state_->lanes[1].queue.clear();
  state_->callback = {};
}

std::shared_ptr<const Document> WebExportPreviewEngine::prepare(const Document& narrowed_source,
                                                                const ImageSaveOptions& geometry,
                                                                bool convert_to_srgb,
                                                                std::vector<std::string>* notices) {
  auto prepared = std::make_shared<Document>(export_transformed_document(narrowed_source, geometry, notices));
  if (convert_to_srgb && rgb_profile_needs_conversion(prepared->color_state().embedded_icc_profile)) {
    // The stand-in is one pixel layer, so every pixel converts (no stored colors stay).
    (void)convert_document_rgb_profile(*prepared, {}, RenderingIntent::RelativeColorimetric, true);
  }
  return prepared;
}

WebExportResult WebExportPreviewEngine::encode(const Document& narrowed_source, const WebExportJob& job,
                                               const std::shared_ptr<const Document>& prepared) {
  WebExportResult result;
  const auto extension = web_export_extension(job.format);
  std::vector<std::uint8_t> bytes;
  if (job.animate) {
    // The animated writers read the layers: the whole document with every option.
    if (job.convert_to_srgb && rgb_profile_needs_conversion(narrowed_source.color_state().embedded_icc_profile)) {
      Document converted = narrowed_source;
      (void)convert_document_rgb_profile(converted, {}, RenderingIntent::RelativeColorimetric, true);
      bytes = encode_flat_image_bytes(converted, extension, job.options, &result.notices);
    } else {
      bytes = encode_flat_image_bytes(narrowed_source, extension, job.options, &result.notices);
    }
  } else {
    const Document& source = prepared != nullptr ? *prepared : narrowed_source;
    auto options = prepared != nullptr ? without_export_transforms(job.options) : job.options;
    // The fill is the one transform applied to the prepared image (JPEG's background).
    options.export_fill_transparent = job.options.export_fill_transparent;
    options.export_background_color = job.options.export_background_color;
    bytes = encode_flat_image_bytes(source, extension, options, &result.notices);
  }
  if (job.animate) {
    // Every frame of the file as written, and the source frames the writer encoded, both
    // capped together at kWebExportAnimationCacheBytes.
    const Document* frame_source = &narrowed_source;
    std::optional<Document> converted;
    if (job.convert_to_srgb && rgb_profile_needs_conversion(narrowed_source.color_state().embedded_icc_profile)) {
      converted.emplace(narrowed_source);
      (void)convert_document_rgb_profile(*converted, {}, RenderingIntent::RelativeColorimetric, true);
      frame_source = &*converted;
    }
    auto first = decode_web_export_bytes(job.format, bytes);
    const auto frame_bytes = std::max<std::size_t>(
        1, static_cast<std::size_t>(std::max(1, first.width())) * static_cast<std::size_t>(std::max(1, first.height())) * 4U);
    const auto max_frames = std::max<std::size_t>(1, kWebExportAnimationCacheBytes / (2U * frame_bytes));
    auto animation = decode_web_export_animation(job.format, bytes, max_frames);
    result.frames = std::move(animation.frames);
    result.frame_delays_ms = std::move(animation.delays_ms);
    result.frames_total = animation.total;
    std::vector<std::string> ignored;
    // The Original pane shows the frames before the background fill.
    auto original_options = job.options;
    original_options.export_fill_transparent = false;
    for_each_animation_export_frame(*frame_source, original_options, &ignored, [&result](PixelBuffer rgba, const Layer&) {
      if (result.original_frames.size() < result.frames.size()) {
        result.original_frames.push_back(display_image(display_qimage_from_pixel_buffer(rgba)));
      }
    });
    result.decoded = result.frames.empty() ? std::move(first) : result.frames.front();
  } else {
    result.decoded = decode_web_export_bytes(job.format, bytes);
  }
  result.bytes = std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));
  if (job.format == WebExportFormat::Gif || (job.format == WebExportFormat::Png && job.options.png_palette)) {
    result.palette = used_colors(result.decoded);
  }
  return result;
}

// ---------------------------------------------------------------------------------------
// Preview panes

namespace {

// A smooth reduction of an image for drawing below 100%, built on a worker once per size,
// so paintEvent only ever draws a cached image (or the full one, scaled by the painter,
// until the reduction lands).
class ScaledImageCache {
public:
  explicit ScaledImageCache(QWidget* owner) : owner_(owner), state_(std::make_shared<State>()) {}

  void set_source(QImage image) {
    if (image.cacheKey() == source_.cacheKey()) {
      return;
    }
    source_ = std::move(image);
  }
  [[nodiscard]] const QImage& source() const noexcept { return source_; }

  [[nodiscard]] const QImage& best_for(QSize device_size) {
    if (source_.isNull() || device_size.isEmpty() || device_size.width() >= source_.width()) {
      return source_;
    }
    if (scaled_size_ != device_size) {
      scaled_.clear();
      scaled_order_.clear();
      scaled_size_ = device_size;
    }
    if (const auto found = scaled_.find(source_.cacheKey()); found != scaled_.end()) {
      return found->second;
    }
    request(device_size);
    return source_;
  }

private:
  struct State {
    std::uint64_t generation{0};
    bool in_flight{false};
    qint64 requested_key{0};
    QSize requested_size;
    std::optional<std::pair<QImage, QSize>> pending;
  };

  void request(QSize size) {
    if (state_->requested_key == source_.cacheKey() && state_->requested_size == size) {
      return;
    }
    state_->requested_key = source_.cacheKey();
    state_->requested_size = size;
    state_->pending = std::make_pair(source_, size);
    ++state_->generation;
    if (!state_->in_flight) {
      start();
    }
  }

  void start() {
    if (!state_->pending.has_value()) {
      return;
    }
    auto [image, size] = std::move(*state_->pending);
    state_->pending.reset();
    state_->in_flight = true;
    const auto generation = state_->generation;
    auto state = state_;
    auto* app = QCoreApplication::instance();
    const QPointer<QWidget> owner(owner_);
    run_tracked_background_worker([state, image = std::move(image), size, generation, app, owner, this]() {
      auto scaled = image.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
      if (app == nullptr) {
        return;
      }
      QMetaObject::invokeMethod(
          app,
          [state, key = image.cacheKey(), scaled = std::move(scaled), generation, owner, this]() mutable {
            state->in_flight = false;
            if (owner == nullptr) {
              return;
            }
            // The owner is alive, so this cache (its member) is too. Every finished reduction
            // at the current size is kept (an animation's frames come round again), up to
            // kScaledFrames of them.
            (void)generation;
            if (scaled.size() == scaled_size_ && scaled_.find(key) == scaled_.end()) {
              scaled_.emplace(key, std::move(scaled));
              scaled_order_.push_back(key);
              while (scaled_order_.size() > kScaledFrames) {
                scaled_.erase(scaled_order_.front());
                scaled_order_.pop_front();
              }
              owner->update();
            }
            start();
          },
          Qt::QueuedConnection);
    });
  }

  static constexpr std::size_t kScaledFrames = 64;
  QWidget* owner_;
  std::shared_ptr<State> state_;
  QImage source_;
  QSize scaled_size_;
  std::map<qint64, QImage> scaled_;
  std::deque<qint64> scaled_order_;
};

// One preview viewport (ZoomableImagePreview's zoom and pan): the original, the encoded
// result, or both split by a draggable divider (original left, result right). The divider
// sits at a fraction of the widget's width, so panning moves the image under it.
class WebExportPane final : public ZoomableImagePreview {
public:
  enum class Content { Split, Original, Result };

  explicit WebExportPane(QWidget* parent) : ZoomableImagePreview(parent), original_(this), result_(this) {
    setMinimumSize(300, 260);
    setFocusPolicy(Qt::NoFocus);
  }

  void set_images(QImage original, QImage result) {
    const auto size = !original.isNull() ? original.size() : result.size();
    setProperty("webExportResultKey", result.cacheKey());
    original_.set_source(std::move(original));
    result_.set_source(std::move(result));
    if (!size.isEmpty()) {
      set_logical_size(size);
    }
    update();
  }
  void set_content(Content content) {
    content_ = content;
    update();
  }
  [[nodiscard]] Content content() const noexcept { return content_; }
  void set_labels(QString original_label, QString result_label) {
    original_label_ = std::move(original_label);
    result_label_ = std::move(result_label);
    update();
  }
  [[nodiscard]] double split() const noexcept { return split_; }
  void set_split(double split) {
    split_ = std::clamp(split, 0.0, 1.0);
    setProperty("webExportSplit", split_);
    update();
  }
  [[nodiscard]] bool has_result() const { return !result_.source().isNull(); }

protected:
  void paint_backdrop(QPainter& painter) override { painter.fillRect(rect(), theme().canvas_backdrop); }

  void paint_content(QPainter& painter, const QRectF& image_rect) override {
    const auto visible = image_rect.intersected(QRectF(rect()));
    if (visible.isEmpty()) {
      return;
    }
    // The transparency checkerboard depicts alpha (deliberately not a theme role,
    // docs/ui-conventions.md).
    painter.save();
    painter.setClipRect(visible);
    painter.fillRect(visible, QColor(200, 200, 200));
    constexpr int kCheck = 8;
    const auto x0 = static_cast<int>(std::floor(image_rect.left()));
    const auto y0 = static_cast<int>(std::floor(image_rect.top()));
    const auto first_row = std::max(0, static_cast<int>(std::floor((visible.top() - y0) / kCheck)));
    const auto first_column = std::max(0, static_cast<int>(std::floor((visible.left() - x0) / kCheck)));
    for (int row = first_row; y0 + row * kCheck < visible.bottom(); ++row) {
      for (int column = first_column + ((first_column + row) % 2 == 0 ? 0 : 1); x0 + column * kCheck < visible.right();
           column += 2) {
        painter.fillRect(QRect(x0 + column * kCheck, y0 + row * kCheck, kCheck, kCheck), QColor(240, 240, 240));
      }
    }
    painter.restore();

    const auto device = (image_rect.size() * devicePixelRatioF()).toSize();
    const auto draw = [&](ScaledImageCache& cache, const QRectF& clip) {
      if (cache.source().isNull() || clip.isEmpty()) {
        return;
      }
      const auto& image = cache.best_for(device);
      painter.save();
      painter.setClipRect(clip);
      painter.setRenderHint(QPainter::SmoothPixmapTransform, zoom() < 1.0);
      painter.drawImage(image_rect, image);
      painter.restore();
    };
    const auto divider_x = width() * split_;
    switch (content_) {
      case Content::Original:
        draw(original_, visible);
        break;
      case Content::Result:
        draw(result_.source().isNull() ? original_ : result_, visible);
        break;
      case Content::Split: {
        const QRectF left(visible.left(), visible.top(), std::max(0.0, divider_x - visible.left()), visible.height());
        const QRectF right(std::max(divider_x, visible.left()), visible.top(),
                           std::max(0.0, visible.right() - std::max(divider_x, visible.left())), visible.height());
        draw(original_, left.intersected(visible));
        draw(result_.source().isNull() ? original_ : result_, right.intersected(visible));
        break;
      }
    }
    painter.setPen(theme().canvas_document_border);
    painter.drawRect(image_rect.adjusted(-1.0, -1.0, 0.0, 0.0));
  }

  void paintEvent(QPaintEvent* event) override {
    ZoomableImagePreview::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (content_ == Content::Split && !logical_size().isEmpty()) {
      // The divider reads over any artwork: a light line with a dark edge, the text
      // resize handles' pair.
      const auto x = std::round(width() * split_) + 0.5;
      painter.setPen(QPen(theme().resize_handle_border, 3.0));
      painter.drawLine(QPointF(x, 0.0), QPointF(x, height()));
      painter.setPen(QPen(theme().resize_handle_bg, 1.0));
      painter.drawLine(QPointF(x, 0.0), QPointF(x, height()));
      const QPointF center(x, height() / 2.0);
      painter.setPen(QPen(theme().resize_handle_border, 1.0));
      painter.setBrush(theme().resize_handle_bg);
      painter.drawEllipse(center, 11.0, 11.0);
      painter.setPen(QPen(theme().resize_handle_border, 1.5));
      painter.drawLine(center + QPointF(-3.0, -4.0), center + QPointF(-6.0, 0.0));
      painter.drawLine(center + QPointF(-6.0, 0.0), center + QPointF(-3.0, 4.0));
      painter.drawLine(center + QPointF(3.0, -4.0), center + QPointF(6.0, 0.0));
      painter.drawLine(center + QPointF(6.0, 0.0), center + QPointF(3.0, 4.0));
    }
    const auto draw_tag = [&](const QString& text, bool right_side) {
      if (text.isEmpty()) {
        return;
      }
      const auto metrics = painter.fontMetrics();
      const QSize size(metrics.horizontalAdvance(text) + 16, metrics.height() + 6);
      const QPoint top_left(right_side ? width() - size.width() - 8 : 8, 8);
      const QRect box(top_left, size);
      auto fill = theme().status_field_bg;
      fill.setAlpha(225);
      painter.setPen(theme().status_field_border);
      painter.setBrush(fill);
      painter.drawRoundedRect(QRectF(box).adjusted(0.5, 0.5, -0.5, -0.5), 2.0, 2.0);
      painter.setPen(theme().status_text);
      painter.drawText(box, Qt::AlignCenter, text);
    };
    switch (content_) {
      case Content::Split:
        draw_tag(original_label_, false);
        draw_tag(result_label_, true);
        break;
      case Content::Original:
        draw_tag(original_label_, false);
        break;
      case Content::Result:
        draw_tag(result_label_, false);
        break;
    }
  }

  [[nodiscard]] QCursor interaction_cursor(bool is_panning) const override {
    return is_panning ? Qt::ClosedHandCursor : Qt::OpenHandCursor;
  }

  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton && near_divider(event->position())) {
      dragging_divider_ = true;
      event->accept();
      return;
    }
    ZoomableImagePreview::mousePressEvent(event);
  }

  void mouseMoveEvent(QMouseEvent* event) override {
    if (dragging_divider_) {
      set_split(width() > 0 ? event->position().x() / width() : 0.5);
      event->accept();
      return;
    }
    ZoomableImagePreview::mouseMoveEvent(event);
    if (!panning() && near_divider(event->position())) {
      setCursor(Qt::SplitHCursor);
    }
  }

  void mouseReleaseEvent(QMouseEvent* event) override {
    if (dragging_divider_ && event->button() == Qt::LeftButton) {
      dragging_divider_ = false;
      setCursor(interaction_cursor(false));
      event->accept();
      return;
    }
    ZoomableImagePreview::mouseReleaseEvent(event);
  }

private:
  [[nodiscard]] bool near_divider(QPointF position) const {
    return content_ == Content::Split && !logical_size().isEmpty() &&
           std::abs(position.x() - width() * split_) <= 8.0;
  }

  ScaledImageCache original_;
  ScaledImageCache result_;
  Content content_{Content::Split};
  QString original_label_;
  QString result_label_;
  double split_{0.5};
  bool dragging_divider_{false};
};

// The colors a GIF or palette PNG uses, as a grid of small cells (32 per row, so 256 colors
// take 8 rows). The cells show image colors (user data); the frame is a theme role.
class PaletteSwatches final : public QWidget {
public:
  explicit PaletteSwatches(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    set_colors({});
  }

  void set_colors(std::vector<QRgb> colors) {
    colors_ = std::move(colors);
    setProperty("webExportPaletteCount", static_cast<int>(colors_.size()));
    const int rows = std::max(1, static_cast<int>((colors_.size() + kColumns - 1) / kColumns));
    setFixedHeight(rows * kPitch + 3);
    update();
  }

  [[nodiscard]] QSize sizeHint() const override { return {kColumns * kPitch + 3, height()}; }

protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.fillRect(rect(), theme().field_bg);
    painter.setPen(theme().group_box_border);
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
    for (std::size_t index = 0; index < colors_.size(); ++index) {
      const auto column = static_cast<int>(index % kColumns);
      const auto row = static_cast<int>(index / kColumns);
      painter.fillRect(QRect(2 + column * kPitch, 2 + row * kPitch, kPitch - 1, kPitch - 1), QColor(colors_[index]));
    }
  }

private:
  static constexpr int kColumns = 32;
  static constexpr int kPitch = 9;
  std::vector<QRgb> colors_;
};

// Colors, Dither (None or Diffusion with an amount) and the swatches of one palette format.
struct PaletteRows {
  QSpinBox* colors{nullptr};
  QComboBox* dither{nullptr};
  QSpinBox* amount{nullptr};
  PaletteSwatches* swatches{nullptr};
};

PaletteRows add_palette_rows(QFormLayout* form, QWidget* parent, const QString& prefix, int colors, bool dither,
                             int amount) {
  PaletteRows rows;
  rows.colors = add_dialog_slider_spin_row(form, parent, QObject::tr("Colors:"), prefix + QStringLiteral("ColorsSlider"),
                                           prefix + QStringLiteral("ColorsSpin"), 2, 256, colors);
  rows.colors->parentWidget()->setToolTip(
      QObject::tr("The most colors the palette may hold. An image with fewer colors keeps them exactly."));
  auto* dither_row = new QWidget(parent);
  auto* dither_layout = new QHBoxLayout(dither_row);
  dither_layout->setContentsMargins(0, 0, 0, 0);
  dither_layout->setSpacing(8);
  rows.dither = new QComboBox(dither_row);
  rows.dither->setObjectName(prefix + QStringLiteral("DitherCombo"));
  rows.dither->addItem(QObject::tr("None"), QStringLiteral("none"));
  rows.dither->addItem(QObject::tr("Diffusion"), QStringLiteral("diffusion"));
  rows.dither->setCurrentIndex(dither ? 1 : 0);
  rows.dither->setToolTip(QObject::tr("Diffusion mixes neighboring palette colors so gradients band less. "
                                      "It usually makes the file larger."));
  dither_layout->addWidget(rows.dither, 1);
  auto* amount_label = new QLabel(QObject::tr("Amount:"), dither_row);
  dither_layout->addWidget(amount_label);
  auto* amount_spin = new UnitIntSpinBox(SpinUnit::Percent, dither_row);
  amount_spin->setObjectName(prefix + QStringLiteral("DitherAmountSpin"));
  amount_spin->setRange(0, 100);
  amount_spin->setValue(amount);
  configure_dialog_spinbox(amount_spin, 76);
  rows.amount = amount_spin;
  dither_layout->addWidget(amount_spin);
  form->addRow(QObject::tr("Dither:"), dither_row);
  rows.swatches = new PaletteSwatches(parent);
  rows.swatches->setObjectName(prefix + QStringLiteral("PaletteSwatches"));
  rows.swatches->setToolTip(QObject::tr("The colors the file uses"));
  form->addRow(QObject::tr("Palette:"), rows.swatches);
  return rows;
}

[[nodiscard]] QGroupBox* make_group(QWidget* parent, const QString& title, const char* object_name) {
  auto* group = new QGroupBox(title, parent);
  group->setObjectName(QLatin1String(object_name));
  return group;
}

[[nodiscard]] QFormLayout* make_form(QGroupBox* group) {
  auto* form = new QFormLayout(group);
  form->setContentsMargins(10, 10, 10, 8);
  form->setHorizontalSpacing(8);
  form->setVerticalSpacing(6);
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  return form;
}

}  // namespace

// ---------------------------------------------------------------------------------------
// The dialog

std::optional<WebExportDialogResult> request_web_export(QWidget* parent, std::shared_ptr<const Document> document) {
  if (document == nullptr || document->width() <= 0 || document->height() <= 0) {
    return std::nullopt;
  }
  const QSize document_size(document->width(), document->height());
  const auto& layers = document->layers();
  const bool offer_animation =
      layers.size() >= 2 && std::any_of(layers.begin(), layers.end(), [](const Layer& layer) { return layer.visible(); });
  const bool non_srgb = web_export_has_non_srgb_profile(*document);

  auto settings = load_web_export_settings();
  settings.width = document_size.width();
  settings.height = document_size.height();
  settings.pixel_scale = 1;
  if (!offer_animation) {
    settings.webp_animate = false;
    settings.gif_animate = false;
    settings.jxl_animate = false;
    settings.png_animate = false;
  }

  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("webExportDialog"));
  auto* content = install_dark_dialog_chrome(dialog, new QVBoxLayout(&dialog), QObject::tr("Export for Web"));
  auto* body = new QHBoxLayout();
  body->setSpacing(12);
  content->addLayout(body, 1);

  // --- left column: formats and settings ---
  auto* controls_scroll = new QScrollArea(&dialog);
  controls_scroll->setObjectName(QStringLiteral("webExportControlsScroll"));
  controls_scroll->setFrameShape(QFrame::NoFrame);
  controls_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  controls_scroll->setWidgetResizable(true);
  controls_scroll->setFixedWidth(410);
  auto* controls_widget = new QWidget(controls_scroll);
  controls_widget->setObjectName(QStringLiteral("webExportControls"));
  controls_scroll->setWidget(controls_widget);
  auto* controls = new QVBoxLayout(controls_widget);
  controls->setContentsMargins(0, 0, 0, 0);
  controls->setSpacing(8);
  body->addWidget(controls_scroll);

  controls->addWidget(new QLabel(QObject::tr("Format"), &dialog));
  auto* format_list = new QTreeWidget(&dialog);
  format_list->setObjectName(QStringLiteral("webExportFormatList"));
  format_list->setColumnCount(3);
  format_list->setHeaderHidden(true);
  format_list->setRootIsDecorated(false);
  format_list->setItemsExpandable(false);
  format_list->setUniformRowHeights(true);
  format_list->setSelectionMode(QAbstractItemView::SingleSelection);
  format_list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  format_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  format_list->header()->setStretchLastSection(false);
  format_list->header()->setSectionResizeMode(0, QHeaderView::Fixed);
  format_list->header()->setSectionResizeMode(1, QHeaderView::Stretch);
  format_list->header()->setSectionResizeMode(2, QHeaderView::Fixed);
  format_list->header()->resizeSection(0, 70);
  format_list->header()->resizeSection(2, 104);
  for (const auto format : web_export_formats()) {
    auto* item = new QTreeWidgetItem(format_list);
    item->setText(0, web_export_format_name(format));
    item->setData(0, Qt::UserRole, web_export_format_key(format));
    item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
    item->setSizeHint(0, QSize(0, 26));
    auto bold = item->font(0);
    bold.setBold(true);
    item->setFont(0, bold);
  }
  format_list->setFixedHeight(format_list->sizeHintForRow(0) * static_cast<int>(web_export_formats().size()) +
                              2 * format_list->frameWidth() + 2);
  controls->addWidget(format_list);
  auto* format_hint = new QLabel(
      QObject::tr("Sizes are the real encoded files at each format's own settings and update as you change them."),
      &dialog);
  format_hint->setObjectName(QStringLiteral("webExportFormatHint"));
  format_hint->setWordWrap(true);
  set_themed_style(*format_hint, QStringLiteral("color: @hint_text;"));
  controls->addWidget(format_hint);

  // JPEG settings.
  auto* jpeg_group = make_group(&dialog, QObject::tr("JPEG settings"), "webExportJpegGroup");
  auto* jpeg_form = make_form(jpeg_group);
  auto* jpeg_quality = add_dialog_slider_spin_row(jpeg_form, jpeg_group, QObject::tr("Quality:"),
                                                  QStringLiteral("webExportJpegQualitySlider"),
                                                  QStringLiteral("webExportJpegQualitySpin"), 1, 100,
                                                  settings.jpeg_quality);
  auto* jpeg_progressive =
      new QCheckBox(QObject::tr("Progressive (shows a rough image first while loading)"), jpeg_group);
  jpeg_progressive->setObjectName(QStringLiteral("webExportJpegProgressiveCheck"));
  jpeg_form->addRow(jpeg_progressive);
  auto* jpeg_optimize = new QCheckBox(QObject::tr("Optimize coding (smaller file, same image)"), jpeg_group);
  jpeg_optimize->setObjectName(QStringLiteral("webExportJpegOptimizeCheck"));
  jpeg_form->addRow(jpeg_optimize);
  controls->addWidget(jpeg_group);

  // WebP settings.
  auto* webp_group = make_group(&dialog, QObject::tr("WebP settings"), "webExportWebpGroup");
  auto* webp_form = make_form(webp_group);
  auto* webp_quality = add_dialog_slider_spin_row(webp_form, webp_group, QObject::tr("Quality:"),
                                                  QStringLiteral("webExportWebpQualitySlider"),
                                                  QStringLiteral("webExportWebpQualitySpin"), 0, 100,
                                                  settings.webp_quality);
  auto* webp_lossless = new QCheckBox(QObject::tr("Lossless (larger file)"), webp_group);
  webp_lossless->setObjectName(QStringLiteral("webExportWebpLosslessCheck"));
  webp_form->addRow(webp_lossless);
  controls->addWidget(webp_group);

  // AVIF and JPEG XL settings (slow encoders: a note says so).
  const auto add_slow_note = [](QFormLayout* form, QWidget* parent) {
    auto* note = new QLabel(QObject::tr("Encoding takes longer than the other formats; the preview updates when it is "
                                        "done."),
                            parent);
    note->setWordWrap(true);
    set_themed_style(*note, QStringLiteral("color: @hint_text;"));
    form->addRow(note);
  };
  auto* avif_group = make_group(&dialog, QObject::tr("AVIF settings"), "webExportAvifGroup");
  auto* avif_form = make_form(avif_group);
  auto* avif_quality = add_dialog_slider_spin_row(avif_form, avif_group, QObject::tr("Quality:"),
                                                  QStringLiteral("webExportAvifQualitySlider"),
                                                  QStringLiteral("webExportAvifQualitySpin"), 0, 100,
                                                  settings.avif_quality);
  auto* avif_speed = new QSpinBox(avif_group);
  avif_speed->setObjectName(QStringLiteral("webExportAvifSpeedSpin"));
  avif_speed->setRange(0, 10);
  avif_speed->setValue(settings.avif_speed);
  avif_speed->setToolTip(QObject::tr("Lower speed makes smaller files and takes longer to save."));
  configure_dialog_spinbox(avif_speed, 76);
  avif_speed->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  avif_form->addRow(QObject::tr("Speed:"), avif_speed);
  auto* avif_lossless = new QCheckBox(QObject::tr("Lossless (larger file)"), avif_group);
  avif_lossless->setObjectName(QStringLiteral("webExportAvifLosslessCheck"));
  avif_form->addRow(avif_lossless);
  add_slow_note(avif_form, avif_group);
  avif_group->setVisible(false);
  controls->addWidget(avif_group);

  auto* jxl_group = make_group(&dialog, QObject::tr("JPEG XL settings"), "webExportJxlGroup");
  auto* jxl_form = make_form(jxl_group);
  auto* jxl_quality = add_dialog_slider_spin_row(jxl_form, jxl_group, QObject::tr("Quality:"),
                                                 QStringLiteral("webExportJxlQualitySlider"),
                                                 QStringLiteral("webExportJxlQualitySpin"), 1, 100,
                                                 settings.jxl_quality);
  auto* jxl_effort = new QSpinBox(jxl_group);
  jxl_effort->setObjectName(QStringLiteral("webExportJxlEffortSpin"));
  jxl_effort->setRange(1, 9);
  jxl_effort->setValue(settings.jxl_effort);
  jxl_effort->setToolTip(QObject::tr("Higher effort makes smaller files and takes longer to save."));
  configure_dialog_spinbox(jxl_effort, 76);
  jxl_effort->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  jxl_form->addRow(QObject::tr("Effort:"), jxl_effort);
  auto* jxl_lossless = new QCheckBox(QObject::tr("Lossless (larger file)"), jxl_group);
  jxl_lossless->setObjectName(QStringLiteral("webExportJxlLosslessCheck"));
  jxl_form->addRow(jxl_lossless);
  add_slow_note(jxl_form, jxl_group);
  jxl_group->setVisible(false);
  controls->addWidget(jxl_group);

  // PNG settings.
  auto* png_group = make_group(&dialog, QObject::tr("PNG settings"), "webExportPngGroup");
  auto* png_form = make_form(png_group);
  auto* png_colors_combo = new QComboBox(png_group);
  png_colors_combo->setObjectName(QStringLiteral("webExportPngColorsCombo"));
  png_colors_combo->addItem(QObject::tr("Full color"), QStringLiteral("full"));
  png_colors_combo->addItem(QObject::tr("Palette"), QStringLiteral("palette"));
  png_colors_combo->setToolTip(
      QObject::tr("Palette writes up to 256 colors, much smaller for flat graphics, logos and screenshots."));
  png_form->addRow(QObject::tr("Type:"), png_colors_combo);
  auto* png_note = new QLabel(QObject::tr("Full color, lossless. Transparency is kept."), png_group);
  png_note->setObjectName(QStringLiteral("webExportPngNote"));
  png_note->setWordWrap(true);
  png_form->addRow(png_note);
  auto* png_palette_box = new QWidget(png_group);
  png_palette_box->setObjectName(QStringLiteral("webExportPngPaletteBox"));
  auto* png_palette_form = new QFormLayout(png_palette_box);
  png_palette_form->setContentsMargins(0, 0, 0, 0);
  png_palette_form->setHorizontalSpacing(8);
  png_palette_form->setVerticalSpacing(6);
  png_palette_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  const auto png_rows = add_palette_rows(png_palette_form, png_palette_box, QStringLiteral("webExportPng"),
                                         settings.png_colors, settings.png_dither, settings.png_dither_amount);
  auto* png_cut_note = new QLabel(QObject::tr("Pixels under 50% opacity become transparent."), png_palette_box);
  png_cut_note->setWordWrap(true);
  set_themed_style(*png_cut_note, QStringLiteral("color: @hint_text;"));
  png_palette_form->addRow(png_cut_note);
  png_form->addRow(png_palette_box);
  auto* png_animation_note =
      new QLabel(QObject::tr("An animated PNG is written in full color; the palette applies to still images."), png_group);
  png_animation_note->setObjectName(QStringLiteral("webExportPngAnimationNote"));
  png_animation_note->setWordWrap(true);
  set_themed_style(*png_animation_note, QStringLiteral("color: @hint_text;"));
  png_form->addRow(png_animation_note);
  controls->addWidget(png_group);

  // GIF settings.
  auto* gif_group = make_group(&dialog, QObject::tr("GIF settings"), "webExportGifGroup");
  auto* gif_form = make_form(gif_group);
  const auto gif_rows = add_palette_rows(gif_form, gif_group, QStringLiteral("webExportGif"), settings.gif_colors,
                                         settings.gif_dither, settings.gif_dither_amount);
  auto* gif_cut_note = new QLabel(QObject::tr("Pixels under 50% opacity become transparent."), gif_group);
  gif_cut_note->setWordWrap(true);
  set_themed_style(*gif_cut_note, QStringLiteral("color: @hint_text;"));
  gif_form->addRow(gif_cut_note);
  controls->addWidget(gif_group);

  // Animation, built from the same helper as the Export > Layers as Animated ... dialogs:
  // the format's own "Animation from visible layers", the shared Default frame delay (GIF
  // in centiseconds), Forever / Play count where the format stores one, and the old dialogs'
  // explanation. Shown for GIF, WebP, JPEG XL and PNG when the document has 2+ layers.
  auto* animation_group = make_group(&dialog, QObject::tr("Animation"), "webExportAnimationGroup");
  auto* animation_layout = new QVBoxLayout(animation_group);
  animation_layout->setContentsMargins(10, 10, 10, 8);
  animation_layout->setSpacing(6);
  struct AnimationRows {
    WebExportFormat format;
    QCheckBox* animate;
    AnimationTimingControls timing;
  };
  std::vector<AnimationRows> animation_rows;
  const auto add_animation_rows = [&](WebExportFormat format, const char* check_name, AnimationTimingKind kind,
                                      const char* timing_prefix, int loop_count) {
    auto* animate = new QCheckBox(QObject::tr("Animation from visible layers"), animation_group);
    animate->setObjectName(QLatin1String(check_name));
    animate->setToolTip(QObject::tr("Each visible top-level layer becomes one frame, with the top layer first."));
    animation_layout->addWidget(animate);
    auto timing = create_animation_timing_controls(animation_group, kind, QLatin1String(timing_prefix),
                                                   settings.animation_frame_delay_ms, loop_count);
    animation_layout->addWidget(timing.controls);
    animation_layout->addWidget(timing.explanation);
    animation_rows.push_back(AnimationRows{format, animate, timing});
    return animate;
  };
  // Play counts start from the document, as the old exports do.
  const auto& document_metadata = document->metadata().values;
  const auto loop_count_from = [&document_metadata](const char* key) {
    if (const auto found = document_metadata.find(key); found != document_metadata.end()) {
      bool valid = false;
      const auto count = QString::fromStdString(found->second).toInt(&valid);
      if (valid && count >= 0 && count <= 65535) {
        return count;
      }
    }
    return 0;
  };
  settings.webp_loop_count = loop_count_from(webp::kLoopCountMetadata);
  settings.jxl_loop_count = loop_count_from(jxl::kLoopCountMetadata);
  settings.apng_loop_count = loop_count_from(apng::kLoopCountMetadata);
  auto* gif_animate = add_animation_rows(WebExportFormat::Gif, "webExportGifAnimateCheck", AnimationTimingKind::Gif,
                                         "webExportGif", 0);
  auto* webp_animate = add_animation_rows(WebExportFormat::Webp, "webExportWebpAnimateCheck",
                                          AnimationTimingKind::Webp, "webExportWebp", settings.webp_loop_count);
  auto* jxl_animate = add_animation_rows(WebExportFormat::Jxl, "webExportJxlAnimateCheck", AnimationTimingKind::Jxl,
                                         "webExportJxl", settings.jxl_loop_count);
  auto* png_animate = add_animation_rows(WebExportFormat::Png, "webExportPngAnimateCheck", AnimationTimingKind::Apng,
                                         "webExportPng", settings.apng_loop_count);
  const auto timing_for = [&animation_rows](WebExportFormat format) -> const AnimationTimingControls* {
    for (const auto& row : animation_rows) {
      if (row.format == format) {
        return &row.timing;
      }
    }
    return nullptr;
  };
  controls->addWidget(animation_group);

  // Transparency, built from the same helper as the Export section's group: Keep transparent
  // or Fill with the one background color. JPEG has no alpha, so it always fills and only the
  // color shows.
  auto* transparency_group = make_group(&dialog, QObject::tr("Transparency"), "webExportTransparencyGroup");
  auto* transparency_layout = new QVBoxLayout(transparency_group);
  transparency_layout->setContentsMargins(10, 10, 10, 8);
  transparency_layout->setSpacing(4);
  std::function<void()> transparency_changed;
  const auto transparency = add_transparency_choice(transparency_layout, transparency_group, dialog,
                                                    QStringLiteral("webExport"), settings.jpeg_background,
                                                    settings.fill_transparent, /*forced_fill*/ false,
                                                    [&transparency_changed] {
                                                      if (transparency_changed) {
                                                        transparency_changed();
                                                      }
                                                    });
  auto* jpeg_background_hint =
      new QLabel(QObject::tr("Fills transparent areas (JPEG has no transparency)"), transparency_group);
  jpeg_background_hint->setObjectName(QStringLiteral("webExportJpegBackgroundHint"));
  jpeg_background_hint->setWordWrap(true);
  set_themed_style(*jpeg_background_hint, QStringLiteral("color: @hint_text;"));
  transparency_layout->addWidget(jpeg_background_hint);
  controls->addWidget(transparency_group);

  // Image size.
  auto* size_group = make_group(&dialog, QObject::tr("Image size"), "webExportSizeGroup");
  auto* size_grid = new QGridLayout(size_group);
  size_grid->setContentsMargins(10, 10, 10, 8);
  size_grid->setHorizontalSpacing(8);
  size_grid->setVerticalSpacing(6);
  const auto make_pixels_spin = [&](const char* object_name, int basis) {
    auto* spin = new UnitIntSpinBox(SpinUnit::Pixels, size_group);
    spin->set_context_provider([basis] { return UnitConversionContext{72.0, static_cast<double>(basis)}; });
    spin->setObjectName(QLatin1String(object_name));
    spin->setRange(1, kMaxOutputPixels);
    configure_dialog_spinbox(spin, 96);
    return spin;
  };
  // Each spin goes in right after its label (insertion order pairs the scrub handle).
  size_grid->addWidget(new QLabel(QObject::tr("Width:"), size_group), 0, 0);
  auto* width_spin = make_pixels_spin("webExportWidthSpin", document_size.width());
  size_grid->addWidget(width_spin, 0, 1);
  auto* link = new QToolButton(size_group);
  link->setObjectName(QStringLiteral("webExportLinkButton"));
  link->setIcon(simple_icon(QStringLiteral("link")));
  link->setIconSize(QSize(18, 18));
  link->setCheckable(true);
  link->setChecked(true);
  link->setAutoRaise(true);
  link->setToolTip(QObject::tr("Constrain proportions"));
  size_grid->addWidget(link, 0, 2, 2, 1, Qt::AlignCenter);
  size_grid->addWidget(new QLabel(QObject::tr("Height:"), size_group), 1, 0);
  auto* height_spin = make_pixels_spin("webExportHeightSpin", document_size.height());
  size_grid->addWidget(height_spin, 1, 1);
  size_grid->addWidget(new QLabel(QObject::tr("Scale:"), size_group), 2, 0);
  auto* scale_spin = new QDoubleSpinBox(size_group);
  scale_spin->setObjectName(QStringLiteral("webExportScaleSpin"));
  scale_spin->setSuffix(percent_suffix());
  configure_dialog_spinbox(scale_spin, 96);
  size_grid->addWidget(scale_spin, 2, 1);
  auto* original_size = new QLabel(
      QObject::tr("of %1 x %2").arg(document_size.width()).arg(document_size.height()), size_group);
  original_size->setObjectName(QStringLiteral("webExportOriginalSizeLabel"));
  set_themed_style(*original_size, QStringLiteral("color: @hint_text;"));
  size_grid->addWidget(original_size, 2, 2, 1, 2);
  size_grid->addWidget(new QLabel(QObject::tr("Resample:"), size_group), 3, 0);
  auto* resample_combo = new QComboBox(size_group);
  resample_combo->setObjectName(QStringLiteral("webExportResampleCombo"));
  resample_combo->addItem(QObject::tr("Smooth"), resample_key(WebExportResample::Smooth));
  resample_combo->addItem(QObject::tr("Pixel art (whole pixels)"), resample_key(WebExportResample::PixelArt));
  resample_combo->setToolTip(QObject::tr("Pixel art enlarges by whole pixels with no smoothing, so sprites stay crisp."));
  size_grid->addWidget(resample_combo, 3, 1, 1, 3);
  auto* trim_check = new QCheckBox(QObject::tr("Trim transparent edges"), size_group);
  trim_check->setObjectName(QStringLiteral("webExportTrimCheck"));
  size_grid->addWidget(trim_check, 4, 0, 1, 4);
  size_grid->setColumnStretch(3, 1);
  controls->addWidget(size_group);

  // Metadata and color.
  auto* metadata_group = make_group(&dialog, QObject::tr("Metadata and color"), "webExportMetadataGroup");
  auto* metadata_form = make_form(metadata_group);
  auto* metadata_combo = new QComboBox(metadata_group);
  metadata_combo->setObjectName(QStringLiteral("webExportMetadataCombo"));
  add_export_metadata_choices(*metadata_combo);
  metadata_combo->setToolTip(QObject::tr("File Info metadata to include. None writes the smallest file."));
  metadata_form->addRow(QObject::tr("Metadata:"), metadata_combo);
  auto* thumbnail_check = new QCheckBox(QObject::tr("Include thumbnail (about 5 to 10 KB)"), metadata_group);
  thumbnail_check->setObjectName(QStringLiteral("webExportThumbnailCheck"));
  thumbnail_check->setToolTip(QObject::tr(
      "Adds a small preview image (about 5 to 10 KB) that some viewers and file browsers show. Works with every "
      "Metadata choice: with None the file holds only the thumbnail."));
  metadata_form->addRow(thumbnail_check);
  QCheckBox* convert_check = nullptr;
  if (non_srgb) {
    const auto profile_name =
        QString::fromStdString(document_rgb_profile_name(document->color_state().embedded_icc_profile));
    convert_check = new QCheckBox(QObject::tr("Convert to sRGB (from %1)").arg(profile_name), metadata_group);
    convert_check->setObjectName(QStringLiteral("webExportConvertToSrgbCheck"));
    convert_check->setToolTip(
        QObject::tr("Browsers show untagged images as sRGB. Unchecked, the file keeps its color profile."));
    metadata_form->addRow(QObject::tr("Color:"), convert_check);
  } else {
    auto* color_label = new QLabel(QObject::tr("sRGB, no profile needed"), metadata_group);
    color_label->setObjectName(QStringLiteral("webExportColorLabel"));
    metadata_form->addRow(QObject::tr("Color:"), color_label);
  }
  controls->addWidget(metadata_group);
  controls->addStretch(1);

  // --- right column: preview ---
  auto* preview_column = new QVBoxLayout();
  preview_column->setSpacing(6);
  auto* panes_row = new QHBoxLayout();
  panes_row->setSpacing(10);
  auto* original_pane = new WebExportPane(&dialog);
  original_pane->setObjectName(QStringLiteral("webExportOriginalPreview"));
  original_pane->set_content(WebExportPane::Content::Original);
  auto* main_pane = new WebExportPane(&dialog);
  main_pane->setObjectName(QStringLiteral("webExportPreview"));
  main_pane->setMinimumSize(300, 420);
  original_pane->setMinimumSize(300, 420);
  panes_row->addWidget(original_pane, 1);
  panes_row->addWidget(main_pane, 1);
  preview_column->addLayout(panes_row, 1);

  auto* view_row = new QHBoxLayout();
  view_row->setSpacing(4);
  view_row->addWidget(new QLabel(QObject::tr("Compare:"), &dialog));
  auto* compare_buttons = new QButtonGroup(&dialog);
  compare_buttons->setExclusive(true);
  const auto make_compare_button = [&](const QString& text, const char* object_name, WebExportCompare mode) {
    auto* button = new QPushButton(text, &dialog);
    button->setObjectName(QLatin1String(object_name));
    button->setCheckable(true);
    button->setFocusPolicy(Qt::NoFocus);
    compare_buttons->addButton(button, static_cast<int>(mode));
    view_row->addWidget(button);
    return button;
  };
  (void)make_compare_button(QObject::tr("Split"), "webExportCompareSplit", WebExportCompare::Split);
  (void)make_compare_button(QObject::tr("Side by side"), "webExportCompareSideBySide", WebExportCompare::SideBySide);
  (void)make_compare_button(QObject::tr("Result only"), "webExportCompareResult", WebExportCompare::ResultOnly);
  view_row->addStretch(1);
  const auto make_zoom_button = [&dialog](const char* object_name, const QString& text, const QString& tooltip) {
    auto* button = new QToolButton(&dialog);
    button->setObjectName(QLatin1String(object_name));
    button->setText(text);
    button->setToolTip(tooltip);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
  };
  auto* zoom_fit = make_zoom_button("webExportZoomFit", QObject::tr("Fit"), QObject::tr("Fit the image in the preview"));
  auto* zoom_100 = make_zoom_button("webExportZoom100", QStringLiteral("100%"),
                                    QObject::tr("Zoom to 100% (1 image pixel = 1 screen pixel)"));
  auto* zoom_out = make_zoom_button("webExportZoomOut", QStringLiteral("-"), QObject::tr("Zoom out"));
  auto* zoom_in = make_zoom_button("webExportZoomIn", QStringLiteral("+"), QObject::tr("Zoom in"));
  auto* zoom_label = new QLabel(&dialog);
  zoom_label->setObjectName(QStringLiteral("webExportZoomLabel"));
  zoom_label->setMinimumWidth(72);
  zoom_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  view_row->addWidget(zoom_fit);
  view_row->addWidget(zoom_100);
  view_row->addWidget(zoom_out);
  view_row->addWidget(zoom_in);
  view_row->addWidget(zoom_label);
  preview_column->addLayout(view_row);

  // Playback for an animated result (shown only then): Play/Pause, previous and next frame,
  // the frame counter with the frame's delay, and Loop.
  auto* playback_row = new QWidget(&dialog);
  playback_row->setObjectName(QStringLiteral("webExportPlaybackControls"));
  auto* playback_layout = new QHBoxLayout(playback_row);
  playback_layout->setContentsMargins(0, 0, 0, 0);
  playback_layout->setSpacing(4);
  auto* play_button = new QPushButton(QObject::tr("Pause"), playback_row);
  play_button->setObjectName(QStringLiteral("webExportPlayButton"));
  play_button->setFocusPolicy(Qt::NoFocus);
  play_button->setMinimumWidth(72);
  auto* previous_frame = make_zoom_button("webExportPreviousFrameButton", QStringLiteral("<"),
                                          QObject::tr("Previous frame"));
  auto* next_frame = make_zoom_button("webExportNextFrameButton", QStringLiteral(">"), QObject::tr("Next frame"));
  auto* frame_label = new QLabel(playback_row);
  frame_label->setObjectName(QStringLiteral("webExportFrameLabel"));
  auto* loop_check = new QCheckBox(QObject::tr("Loop"), playback_row);
  loop_check->setObjectName(QStringLiteral("webExportLoopCheck"));
  loop_check->setChecked(true);
  playback_layout->addWidget(play_button);
  playback_layout->addWidget(previous_frame);
  playback_layout->addWidget(next_frame);
  playback_layout->addWidget(frame_label, 1);
  playback_layout->addWidget(loop_check);
  playback_row->hide();
  preview_column->addWidget(playback_row);
  auto* playback_timer = new QTimer(&dialog);
  playback_timer->setObjectName(QStringLiteral("webExportPlaybackTimer"));
  playback_timer->setSingleShot(true);

  auto* info_row = new QHBoxLayout();
  info_row->setSpacing(6);
  auto* busy = new ActivitySpinner(&dialog);
  busy->setObjectName(QStringLiteral("webExportBusySpinner"));
  info_row->addWidget(busy);
  auto* info_label = new QLabel(&dialog);
  info_label->setObjectName(QStringLiteral("webExportInfoLabel"));
  info_label->setWordWrap(true);
  info_row->addWidget(info_label, 1);
  preview_column->addLayout(info_row);
  body->addLayout(preview_column, 1);

  // --- footer ---
  auto* footer = new QHBoxLayout();
  footer->setSpacing(8);
  auto* reveal_check = new QCheckBox(export_reveal_check_label(), &dialog);
  reveal_check->setObjectName(QStringLiteral("webExportRevealCheck"));
  footer->addWidget(reveal_check);
  footer->addStretch(1);
  auto* reset_button = new QPushButton(QObject::tr("Reset"), &dialog);
  reset_button->setObjectName(QStringLiteral("webExportResetButton"));
  reset_button->setToolTip(QObject::tr("Return every setting to its default"));
  reset_button->setAutoDefault(false);
  auto* save_button = new QPushButton(QObject::tr("Save..."), &dialog);
  save_button->setObjectName(QStringLiteral("webExportSaveButton"));
  save_button->setDefault(true);
  auto* cancel_button = new QPushButton(QObject::tr("Cancel"), &dialog);
  cancel_button->setObjectName(QStringLiteral("webExportCancelButton"));
  cancel_button->setAutoDefault(false);
  footer->addWidget(reset_button);
  footer->addWidget(save_button);
  footer->addWidget(cancel_button);
  // Frameless chrome has no native resize border; the corner grip is the resize handle.
  footer->addWidget(new VisibleSizeGrip(&dialog), 0, Qt::AlignBottom);
  content->addLayout(footer);

  // --- state ---
  WebExportPreviewEngine engine(document);
  bool syncing = false;
  bool accepting = false;
  // Encodes that failed, by signature (their list row says so instead of "encoding...").
  std::map<QString, QString> errors;
  // Rows selected at least once (AVIF and JPEG XL encode in the background only then) and
  // each row's last size, shown while a new encode runs.
  std::set<WebExportFormat> visited;
  std::map<WebExportFormat, QString> last_sizes;
  const auto selected_format = [&] {
    const auto* item = format_list->currentItem();
    return item != nullptr ? web_export_format_from_key(item->data(0, Qt::UserRole).toString())
                                 .value_or(WebExportFormat::Jpeg)
                           : WebExportFormat::Jpeg;
  };
  const auto read_settings = [&] {
    auto read = settings;
    read.format = selected_format();
    read.jpeg_quality = jpeg_quality->value();
    read.jpeg_progressive = jpeg_progressive->isChecked();
    read.jpeg_optimize = jpeg_optimize->isChecked();
    read.jpeg_background = transparency_choice_color(transparency);
    read.fill_transparent = transparency.fill->isChecked();
    if (const auto* timing = timing_for(read.format)) {
      read.animation_frame_delay_ms = animation_timing_delay_ms(*timing);
    }
    if (const auto* timing = timing_for(WebExportFormat::Webp)) {
      read.webp_loop_count = animation_timing_loop_count(*timing);
    }
    if (const auto* timing = timing_for(WebExportFormat::Jxl)) {
      read.jxl_loop_count = animation_timing_loop_count(*timing);
    }
    if (const auto* timing = timing_for(WebExportFormat::Png)) {
      read.apng_loop_count = animation_timing_loop_count(*timing);
    }
    read.webp_quality = webp_quality->value();
    read.webp_lossless = webp_lossless->isChecked();
    read.webp_animate = offer_animation && webp_animate->isChecked();
    read.avif_quality = avif_quality->value();
    read.avif_speed = avif_speed->value();
    read.avif_lossless = avif_lossless->isChecked();
    read.jxl_quality = jxl_quality->value();
    read.jxl_effort = jxl_effort->value();
    read.jxl_lossless = jxl_lossless->isChecked();
    read.jxl_animate = offer_animation && jxl_animate->isChecked();
    read.png_palette = png_colors_combo->currentData().toString() == QStringLiteral("palette");
    read.png_animate = offer_animation && png_animate->isChecked();
    read.png_colors = png_rows.colors->value();
    read.png_dither = png_rows.dither->currentData().toString() == QStringLiteral("diffusion");
    read.png_dither_amount = png_rows.amount->value();
    read.gif_colors = gif_rows.colors->value();
    read.gif_dither = gif_rows.dither->currentData().toString() == QStringLiteral("diffusion");
    read.gif_dither_amount = gif_rows.amount->value();
    read.gif_animate = offer_animation && gif_animate->isChecked();
    read.metadata = metadata_policy_from_key(metadata_combo->currentData().toString())
                        .value_or(metadata::MetadataPolicy::None);
    read.thumbnail = thumbnail_check->isChecked();
    read.resample = resample_combo->currentData().toString() == resample_key(WebExportResample::PixelArt)
                        ? WebExportResample::PixelArt
                        : WebExportResample::Smooth;
    read.trim = trim_check->isChecked();
    read.convert_to_srgb = convert_check == nullptr || convert_check->isChecked();
    read.compare = static_cast<WebExportCompare>(std::max(0, compare_buttons->checkedId()));
    read.reveal_when_done = reveal_check->isChecked();
    read.width = width_spin->value();
    read.height = height_spin->value();
    read.pixel_scale = std::clamp(static_cast<int>(std::lround(scale_spin->value() / 100.0)), 1, kMaxPixelScale);
    return read;
  };

  const auto refresh_size_mode = [&] {
    const bool pixel_art = resample_combo->currentData().toString() == resample_key(WebExportResample::PixelArt);
    const QSignalBlocker block_scale(scale_spin);
    if (pixel_art) {
      scale_spin->setDecimals(0);
      scale_spin->setRange(100.0, 100.0 * kMaxPixelScale);
      scale_spin->setSingleStep(100.0);
    } else {
      scale_spin->setDecimals(2);
      scale_spin->setRange(1.0, 1000.0);
      scale_spin->setSingleStep(10.0);
    }
    width_spin->setEnabled(!pixel_art);
    height_spin->setEnabled(!pixel_art);
    link->setEnabled(!pixel_art);
    scale_spin->setEnabled(pixel_art || link->isChecked());
  };
  const auto set_size_fields = [&](int width, int height, double percent) {
    const QSignalBlocker block_width(width_spin);
    const QSignalBlocker block_height(height_spin);
    const QSignalBlocker block_scale(scale_spin);
    width_spin->setValue(std::clamp(width, 1, kMaxOutputPixels));
    height_spin->setValue(std::clamp(height, 1, kMaxOutputPixels));
    scale_spin->setValue(percent);
  };
  const auto pixels_for = [](int source, double percent) {
    return static_cast<int>(std::clamp<long long>(std::llround(source * percent / 100.0), 1, kMaxOutputPixels));
  };
  const auto refresh_format_rows = [&] {
    const auto current = read_settings();
    for (int row = 0; row < format_list->topLevelItemCount(); ++row) {
      auto* item = format_list->topLevelItem(row);
      const auto format =
          web_export_format_from_key(item->data(0, Qt::UserRole).toString()).value_or(WebExportFormat::Jpeg);
      item->setText(1, web_export_format_summary(current, format));
      const auto job = web_export_job(current, format, document_size);
      const auto cached = engine.cached(job.signature);
      const bool unvisited_slow = web_export_format_is_slow(format) && !visited.contains(format);
      const bool waiting = !cached.has_value() && !errors.contains(job.signature) && !unvisited_slow;
      QString text;
      if (cached.has_value() && cached->bytes != nullptr) {
        text = data_size_text(cached->bytes->size());
        last_sizes[format] = text;
      } else if (errors.contains(job.signature)) {
        text = QObject::tr("failed");
      } else if (unvisited_slow) {
        // AVIF and JPEG XL sizes start once their row has been selected (docs/web-export.md).
        text = QObject::tr("select to encode");
      } else if (const auto last = last_sizes.find(format); last != last_sizes.end()) {
        // The last size stays, marked, until the new encode lands.
        text = last->second + QStringLiteral(" ...");
      } else {
        text = QObject::tr("encoding...");
      }
      item->setText(2, text);
      auto font = item->font(2);
      font.setItalic(waiting);
      item->setFont(2, font);
      item->setToolTip(2, waiting ? QObject::tr("Encoding...") : QString());
    }
  };
  const auto refresh_visibility = [&] {
    const auto format = selected_format();
    jpeg_group->setVisible(format == WebExportFormat::Jpeg);
    webp_group->setVisible(format == WebExportFormat::Webp);
    png_group->setVisible(format == WebExportFormat::Png);
    gif_group->setVisible(format == WebExportFormat::Gif);
    avif_group->setVisible(format == WebExportFormat::Avif);
    jxl_group->setVisible(format == WebExportFormat::Jxl);
    avif_quality->parentWidget()->setEnabled(!avif_lossless->isChecked());
    jxl_quality->parentWidget()->setEnabled(!jxl_lossless->isChecked());
    // One Animation group: the selected format's checkbox and timing rows.
    bool animation_format = false;
    for (const auto& row : animation_rows) {
      const bool current = row.format == format;
      animation_format = animation_format || current;
      row.animate->setVisible(current);
      row.timing.controls->setVisible(current);
      row.timing.explanation->setVisible(current);
      row.timing.controls->setEnabled(row.animate->isChecked());
      row.timing.explanation->setEnabled(row.animate->isChecked());
    }
    animation_group->setVisible(offer_animation && animation_format);
    // Transparency: free choice for the formats with alpha, forced fill for JPEG.
    set_transparency_choice_forced_fill(transparency, format == WebExportFormat::Jpeg);
    jpeg_background_hint->setVisible(format == WebExportFormat::Jpeg);
    const bool png_palette = png_colors_combo->currentData().toString() == QStringLiteral("palette");
    png_palette_box->setVisible(png_palette);
    png_note->setVisible(!png_palette);
    // An animated PNG is full color: the Type and Palette controls rest, with a note.
    const bool png_animated = offer_animation && png_animate->isChecked();
    png_colors_combo->setEnabled(!png_animated);
    png_palette_box->setEnabled(!png_animated);
    png_animation_note->setVisible(png_animated);
    for (const auto* rows : {&png_rows, &gif_rows}) {
      rows->amount->setEnabled(rows->dither->currentData().toString() == QStringLiteral("diffusion"));
    }
    thumbnail_check->setVisible(format == WebExportFormat::Jpeg);
    webp_quality->parentWidget()->setEnabled(!webp_lossless->isChecked());
  };
  const auto refresh_compare = [&] {
    const auto mode = static_cast<WebExportCompare>(std::max(0, compare_buttons->checkedId()));
    original_pane->setVisible(mode == WebExportCompare::SideBySide);
    main_pane->set_content(mode == WebExportCompare::Split ? WebExportPane::Content::Split
                                                           : WebExportPane::Content::Result);
    if (original_pane->isVisible()) {
      original_pane->set_view_state(main_pane->view_state());
    }
  };
  // The animated result being played: its frames stay cached (display images) until another
  // animated result for the selected format replaces them, so playback keeps running while a
  // newer encode is under way. The timer only picks a cached frame and repaints.
  std::optional<WebExportResult> animation;
  QString animation_signature;
  int animation_frame = 0;
  bool animation_playing = true;
  const auto frame_delay_ms = [&](int index) {
    const auto delay = animation.has_value() && index < static_cast<int>(animation->frame_delays_ms.size())
                           ? animation->frame_delays_ms[static_cast<std::size_t>(index)]
                           : 100;
    // Browsers show 0 and 10 ms GIF delays at 100 ms; so does the preview.
    return delay < 20 ? 100 : delay;
  };
  const auto show_frame = [&](int index) {
    if (!animation.has_value() || animation->frames.empty()) {
      return;
    }
    const auto count = static_cast<int>(animation->frames.size());
    animation_frame = std::clamp(index, 0, count - 1);
    const auto at = static_cast<std::size_t>(animation_frame);
    const auto& original_frame =
        at < animation->original_frames.size() ? animation->original_frames[at] : animation->original;
    main_pane->set_images(original_frame, animation->frames[at]);
    original_pane->set_images(original_frame, {});
    main_pane->setProperty("webExportFrameIndex", animation_frame);
    main_pane->setProperty("webExportFrameCount", count);
    frame_label->setText(QObject::tr("Frame %1 of %2 (%3 s)")
                             .arg(animation_frame + 1)
                             .arg(count)
                             .arg(QLocale().toString(frame_delay_ms(animation_frame) / 1000.0, 'f', 2)));
  };
  const auto set_playing = [&](bool playing) {
    animation_playing = playing;
    play_button->setText(playing ? QObject::tr("Pause") : QObject::tr("Play"));
    main_pane->setProperty("webExportPlaying", playing && animation.has_value());
    if (playing && animation.has_value() && animation->frames.size() > 1) {
      playback_timer->start(frame_delay_ms(animation_frame));
    } else {
      playback_timer->stop();
    }
  };
  const auto stop_animation = [&] {
    playback_timer->stop();
    animation.reset();
    animation_signature.clear();
    playback_row->hide();
    main_pane->setProperty("webExportFrameCount", 0);
    main_pane->setProperty("webExportPlaying", false);
  };
  QObject::connect(playback_timer, &QTimer::timeout, &dialog, [&] {
    if (!animation.has_value() || animation->frames.size() < 2) {
      return;
    }
    auto next = animation_frame + 1;
    if (next >= static_cast<int>(animation->frames.size())) {
      if (!loop_check->isChecked()) {
        set_playing(false);
        return;
      }
      next = 0;
    }
    show_frame(next);
    playback_timer->start(frame_delay_ms(next));
  });
  QObject::connect(play_button, &QPushButton::clicked, &dialog, [&] {
    // Play at the last frame without Loop starts over.
    if (!animation_playing && animation.has_value() && !loop_check->isChecked() &&
        animation_frame + 1 >= static_cast<int>(animation->frames.size())) {
      show_frame(0);
    }
    set_playing(!animation_playing);
  });
  const auto step_frame = [&](int direction) {
    if (!animation.has_value() || animation->frames.empty()) {
      return;
    }
    set_playing(false);
    const auto count = static_cast<int>(animation->frames.size());
    show_frame((animation_frame + direction + count) % count);
  };
  QObject::connect(previous_frame, &QToolButton::clicked, &dialog, [&] { step_frame(-1); });
  QObject::connect(next_frame, &QToolButton::clicked, &dialog, [&] { step_frame(1); });
  // Closing or hiding the dialog stops playback.
  QObject::connect(&dialog, &QDialog::finished, playback_timer, &QTimer::stop);

  const auto show_result = [&] {
    const auto current = read_settings();
    const auto format = current.format;
    const auto job = web_export_job(current, format, document_size);
    const auto cached = engine.cached(job.signature);
    const auto format_label = web_export_format_name(format);
    if (!job.animate) {
      stop_animation();
    }
    if (cached.has_value()) {
      if (job.animate && cached->frames.size() > 1) {
        if (animation_signature != job.signature) {
          animation = cached;
          animation_signature = job.signature;
          playback_row->show();
          show_frame(animation_frame < static_cast<int>(cached->frames.size()) ? animation_frame : 0);
          set_playing(animation_playing);
        }
      } else {
        stop_animation();
        main_pane->set_images(cached->original, cached->decoded);
        original_pane->set_images(cached->original, {});
      }
      const auto size_text = data_size_text(cached->bytes->size());
      main_pane->set_labels(QObject::tr("Original"), QObject::tr("%1: %2").arg(format_label, size_text));
      original_pane->set_labels(QObject::tr("Original"), {});
      const auto width = cached->decoded.width();
      const auto height = cached->decoded.height();
      const auto channels = cached->decoded.hasAlphaChannel() ? 4 : 3;
      const auto raw = static_cast<std::size_t>(std::max(0, width)) * static_cast<std::size_t>(std::max(0, height)) *
                       static_cast<std::size_t>(channels);
      const auto percent = raw > 0 ? static_cast<int>(std::lround(100.0 * static_cast<double>(cached->bytes->size()) /
                                                                  static_cast<double>(raw)))
                                   : 0;
      auto text = QObject::tr("Output %1 x %2, %3 %4 (%5% of %6 uncompressed)")
                      .arg(width)
                      .arg(height)
                      .arg(format_label, size_text)
                      .arg(percent)
                      .arg(data_size_text(raw));
      if (cached->frames.size() > 1) {
        long long total_ms = 0;
        for (int index = 0; index < static_cast<int>(cached->frame_delays_ms.size()); ++index) {
          total_ms += cached->frame_delays_ms[static_cast<std::size_t>(index)] < 20
                          ? 100
                          : cached->frame_delays_ms[static_cast<std::size_t>(index)];
        }
        text += QStringLiteral(", ") + QObject::tr("%1 frames").arg(cached->frames_total) +
                QStringLiteral(", ") +
                QObject::tr("%1 s per loop").arg(QLocale().toString(static_cast<double>(total_ms) / 1000.0, 'f', 2));
        if (static_cast<int>(cached->frames.size()) < cached->frames_total) {
          text += QLatin1Char(' ') + QObject::tr("The preview plays the first %1 frames to save memory.")
                                         .arg(cached->frames.size());
        }
      }
      for (const auto& notice : cached->notices) {
        text += QLatin1Char(' ') + QString::fromStdString(notice);
      }
      info_label->setText(text);
      if (format == WebExportFormat::Gif) {
        gif_rows.swatches->set_colors(cached->palette);
      } else if (format == WebExportFormat::Png) {
        png_rows.swatches->set_colors(cached->palette);
      }
    } else {
      // The last result stays on screen until the new one lands.
      main_pane->set_labels(QObject::tr("Original"), QObject::tr("%1: encoding...").arg(format_label));
      const auto failed = errors.find(job.signature);
      info_label->setText(failed != errors.end() ? QObject::tr("Encoding failed: %1").arg(failed->second)
                                                 : QObject::tr("Encoding %1...").arg(format_label));
    }
    main_pane->setProperty("webExportShowsCurrent", cached.has_value());
    busy->set_active(engine.busy() || !cached.has_value());
  };

  auto* debounce = new QTimer(&dialog);
  debounce->setSingleShot(true);
  debounce->setInterval(kPreviewDebounceMs);
  const auto request_encodes = [&] {
    const auto current = read_settings();
    std::vector<WebExportJob> jobs;
    // The selected format first, then the others one at a time.
    visited.insert(current.format);
    jobs.push_back(web_export_job(current, current.format, document_size));
    // Then the fast formats, then AVIF and JPEG XL rows that have been selected once.
    for (const bool slow : {false, true}) {
      for (const auto format : web_export_formats()) {
        if (format != current.format && web_export_format_is_slow(format) == slow &&
            (!slow || visited.contains(format))) {
          jobs.push_back(web_export_job(current, format, document_size));
        }
      }
    }
    engine.request(std::move(jobs));
    refresh_format_rows();
    show_result();
  };
  QObject::connect(debounce, &QTimer::timeout, &dialog, [&] { request_encodes(); });

  engine.set_result_callback([&](const WebExportJob& job, const WebExportResult& result) {
    if (!result.error.isEmpty()) {
      errors[job.signature] = result.error;
    }
    refresh_format_rows();
    const auto current_job = web_export_job(read_settings(), selected_format(), document_size);
    if (job.signature == current_job.signature) {
      if (!result.error.isEmpty()) {
        info_label->setText(QObject::tr("Encoding failed: %1").arg(result.error));
        busy->set_active(false);
        if (accepting) {
          accepting = false;
          save_button->setEnabled(true);
        }
        return;
      }
      show_result();
      if (accepting) {
        dialog.accept();
        return;
      }
    }
    busy->set_active(engine.busy() || !engine.cached(current_job.signature).has_value());
  });

  const auto on_changed = [&] {
    if (syncing) {
      return;
    }
    refresh_visibility();
    refresh_format_rows();
    show_result();
    debounce->start();
  };
  const auto write_settings = [&](const WebExportSettings& values) {
    syncing = true;
    for (int row = 0; row < format_list->topLevelItemCount(); ++row) {
      if (format_list->topLevelItem(row)->data(0, Qt::UserRole).toString() == web_export_format_key(values.format)) {
        format_list->setCurrentItem(format_list->topLevelItem(row));
      }
    }
    jpeg_quality->setValue(values.jpeg_quality);
    jpeg_progressive->setChecked(values.jpeg_progressive);
    jpeg_optimize->setChecked(values.jpeg_optimize);
    set_transparency_choice_color(transparency, values.jpeg_background);
    transparency.fill->setChecked(values.fill_transparent);
    transparency.keep->setChecked(!values.fill_transparent);
    for (const auto& row : animation_rows) {
      set_animation_timing_delay_ms(row.timing, values.animation_frame_delay_ms);
    }
    if (const auto* timing = timing_for(WebExportFormat::Webp)) {
      set_animation_timing_loop_count(*timing, values.webp_loop_count);
    }
    if (const auto* timing = timing_for(WebExportFormat::Jxl)) {
      set_animation_timing_loop_count(*timing, values.jxl_loop_count);
    }
    if (const auto* timing = timing_for(WebExportFormat::Png)) {
      set_animation_timing_loop_count(*timing, values.apng_loop_count);
    }
    webp_quality->setValue(values.webp_quality);
    webp_lossless->setChecked(values.webp_lossless);
    webp_animate->setChecked(values.webp_animate);
    avif_quality->setValue(values.avif_quality);
    avif_speed->setValue(values.avif_speed);
    avif_lossless->setChecked(values.avif_lossless);
    jxl_quality->setValue(values.jxl_quality);
    jxl_effort->setValue(values.jxl_effort);
    jxl_lossless->setChecked(values.jxl_lossless);
    jxl_animate->setChecked(values.jxl_animate);
    png_colors_combo->setCurrentIndex(values.png_palette ? 1 : 0);
    png_animate->setChecked(values.png_animate);
    png_rows.colors->setValue(values.png_colors);
    png_rows.dither->setCurrentIndex(values.png_dither ? 1 : 0);
    png_rows.amount->setValue(values.png_dither_amount);
    gif_rows.colors->setValue(values.gif_colors);
    gif_rows.dither->setCurrentIndex(values.gif_dither ? 1 : 0);
    gif_rows.amount->setValue(values.gif_dither_amount);
    gif_animate->setChecked(values.gif_animate);
    metadata_combo->setCurrentIndex(std::max(0, metadata_combo->findData(metadata_policy_key(values.metadata))));
    thumbnail_check->setChecked(values.thumbnail);
    resample_combo->setCurrentIndex(std::max(0, resample_combo->findData(resample_key(values.resample))));
    trim_check->setChecked(values.trim);
    if (convert_check != nullptr) {
      convert_check->setChecked(values.convert_to_srgb);
    }
    if (auto* button = compare_buttons->button(static_cast<int>(values.compare))) {
      button->setChecked(true);
    }
    reveal_check->setChecked(values.reveal_when_done);
    refresh_size_mode();
    if (values.resample == WebExportResample::PixelArt) {
      const auto scale = std::clamp(values.pixel_scale, 1, kMaxPixelScale);
      set_size_fields(document_size.width() * scale, document_size.height() * scale, 100.0 * scale);
    } else {
      set_size_fields(values.width, values.height, 100.0 * values.width / std::max(1, document_size.width()));
    }
    refresh_compare();
    refresh_visibility();
    syncing = false;
  };

  // Size fields: linked, width, height and scale follow each other.
  QObject::connect(width_spin, &QSpinBox::valueChanged, &dialog, [&](int width) {
    if (syncing) {
      return;
    }
    const double percent = width * 100.0 / std::max(1, document_size.width());
    if (link->isChecked()) {
      set_size_fields(width, pixels_for(document_size.height(), percent), percent);
    }
    on_changed();
  });
  QObject::connect(height_spin, &QSpinBox::valueChanged, &dialog, [&](int height) {
    if (syncing) {
      return;
    }
    const double percent = height * 100.0 / std::max(1, document_size.height());
    if (link->isChecked()) {
      set_size_fields(pixels_for(document_size.width(), percent), height, percent);
    }
    on_changed();
  });
  QObject::connect(scale_spin, &QDoubleSpinBox::valueChanged, &dialog, [&](double percent) {
    if (syncing) {
      return;
    }
    set_size_fields(pixels_for(document_size.width(), percent), pixels_for(document_size.height(), percent), percent);
    on_changed();
  });
  QObject::connect(link, &QToolButton::toggled, &dialog, [&](bool linked) {
    refresh_size_mode();
    if (linked) {
      // Re-link at the width's scale.
      const double percent = width_spin->value() * 100.0 / std::max(1, document_size.width());
      set_size_fields(width_spin->value(), pixels_for(document_size.height(), percent), percent);
      on_changed();
    }
  });
  QObject::connect(resample_combo, &QComboBox::currentIndexChanged, &dialog, [&](int) {
    if (syncing) {
      return;
    }
    refresh_size_mode();
    const bool pixel_art = resample_combo->currentData().toString() == resample_key(WebExportResample::PixelArt);
    if (pixel_art) {
      const auto scale = std::clamp(static_cast<int>(std::lround(width_spin->value() /
                                                                  static_cast<double>(std::max(1, document_size.width())))),
                                    1, kMaxPixelScale);
      set_size_fields(document_size.width() * scale, document_size.height() * scale, 100.0 * scale);
    }
    on_changed();
  });

  QObject::connect(format_list, &QTreeWidget::currentItemChanged, &dialog,
                   [&](QTreeWidgetItem*, QTreeWidgetItem*) { on_changed(); });
  QObject::connect(jpeg_quality, &QSpinBox::valueChanged, &dialog, [&](int) { on_changed(); });
  QObject::connect(webp_quality, &QSpinBox::valueChanged, &dialog, [&](int) { on_changed(); });
  QObject::connect(metadata_combo, &QComboBox::currentIndexChanged, &dialog, [&](int) { on_changed(); });
  for (auto* spin : {png_rows.colors, png_rows.amount, gif_rows.colors, gif_rows.amount, avif_quality, avif_speed,
                     jxl_quality, jxl_effort}) {
    QObject::connect(spin, &QSpinBox::valueChanged, &dialog, [&](int) { on_changed(); });
  }
  for (auto* combo : {png_colors_combo, png_rows.dither, gif_rows.dither}) {
    QObject::connect(combo, &QComboBox::currentIndexChanged, &dialog, [&](int) { on_changed(); });
  }
  // The default frame delay is one value: a change in one format's row shows in the others.
  bool syncing_delay = false;
  for (const auto& row : animation_rows) {
    auto* spin = row.timing.frame_delay;
    const auto timing = row.timing;
    QObject::connect(spin, &QDoubleSpinBox::valueChanged, &dialog, [&, timing](double) {
      if (syncing_delay) {
        return;
      }
      syncing_delay = true;
      const auto delay = animation_timing_delay_ms(timing);
      for (const auto& other : animation_rows) {
        if (other.timing.frame_delay != timing.frame_delay) {
          set_animation_timing_delay_ms(other.timing, delay);
        }
      }
      syncing_delay = false;
      on_changed();
    });
    if (row.timing.forever_check != nullptr) {
      QObject::connect(row.timing.forever_check, &QCheckBox::toggled, &dialog, [&](bool) { on_changed(); });
      QObject::connect(row.timing.plays, &QSpinBox::valueChanged, &dialog, [&](int) { on_changed(); });
    }
  }
  transparency_changed = [&] { on_changed(); };
  for (auto* check : {jpeg_progressive, jpeg_optimize, webp_lossless, webp_animate, gif_animate, avif_lossless,
                      jxl_lossless, jxl_animate, png_animate, thumbnail_check,
                      trim_check}) {
    QObject::connect(check, &QCheckBox::toggled, &dialog, [&](bool) { on_changed(); });
  }
  if (convert_check != nullptr) {
    QObject::connect(convert_check, &QCheckBox::toggled, &dialog, [&](bool) { on_changed(); });
  }
  QObject::connect(compare_buttons, &QButtonGroup::idClicked, &dialog, [&](int) { refresh_compare(); });

  // Zoom: the main pane drives; side by side keeps the two panes in step.
  const auto refresh_zoom_label = [&] {
    const auto percent = main_pane->property("previewZoomPercent").toInt();
    zoom_label->setText(main_pane->fit_mode() ? QObject::tr("Fit (%1%)").arg(percent)
                                              : QStringLiteral("%1%").arg(percent));
  };
  bool syncing_view = false;
  const auto sync_view = [&](WebExportPane* from, WebExportPane* to) {
    // Only while both panes show (side by side): a hidden pane's updates never move the other.
    if (syncing_view || !to->isVisible() || !from->isVisible()) {
      return;
    }
    syncing_view = true;
    to->set_view_state(from->view_state());
    syncing_view = false;
  };
  main_pane->set_zoom_changed_callback([&] {
    refresh_zoom_label();
    sync_view(main_pane, original_pane);
  });
  original_pane->set_zoom_changed_callback([&] { sync_view(original_pane, main_pane); });
  QObject::connect(zoom_fit, &QToolButton::clicked, &dialog, [&] { main_pane->zoom_to_fit(); });
  QObject::connect(zoom_100, &QToolButton::clicked, &dialog, [&] { main_pane->zoom_to(1.0); });
  QObject::connect(zoom_out, &QToolButton::clicked, &dialog, [&] { main_pane->zoom_step(-1); });
  QObject::connect(zoom_in, &QToolButton::clicked, &dialog, [&] { main_pane->zoom_step(1); });

  QObject::connect(reset_button, &QPushButton::clicked, &dialog, [&] {
    auto defaults = WebExportSettings{};
    defaults.compare = static_cast<WebExportCompare>(std::max(0, compare_buttons->checkedId()));
    defaults.reveal_when_done = reveal_check->isChecked();
    defaults.width = document_size.width();
    defaults.height = document_size.height();
    write_settings(defaults);
    on_changed();
  });
  QObject::connect(cancel_button, &QPushButton::clicked, &dialog, &QDialog::reject);
  QObject::connect(save_button, &QPushButton::clicked, &dialog, [&] {
    const auto current_job = web_export_job(read_settings(), selected_format(), document_size);
    if (!debounce->isActive() && engine.cached(current_job.signature).has_value()) {
      dialog.accept();
      return;
    }
    // The current settings are still encoding: finish first, then close.
    accepting = true;
    save_button->setEnabled(false);
    if (debounce->isActive()) {
      debounce->stop();
      request_encodes();
      // Settings that were changed and changed back are already encoded.
      if (engine.cached(current_job.signature).has_value()) {
        dialog.accept();
      }
    }
  });

  write_settings(settings);
  append_themed_style(dialog, dialog_spinbox_button_style());
  // A wheel over an unfocused combo box, spin box or slider in the settings column scrolls
  // the column (GitHub issue 92's guard; it also covers controls shown later).
  install_wheel_scroll_guard(dialog);
  const auto engine_cleanup = qScopeGuard([&engine] { engine.close(); });
  dialog.resize(1280, 820);
  request_encodes();

  const auto code = exec_dialog(dialog);
  playback_timer->stop();
  engine.close();
  if (code != QDialog::Accepted) {
    return std::nullopt;
  }
  WebExportDialogResult result;
  result.settings = read_settings();
  result.job = web_export_job(result.settings, result.settings.format, document_size);
  if (const auto cached = engine.cached(result.job.signature); cached.has_value()) {
    result.bytes = cached->bytes;
    result.notices = cached->notices;
  }
  save_web_export_settings(result.settings);
  return result;
}

}  // namespace patchy::ui
