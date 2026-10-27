#include "ui/script_save_options.hpp"

#include "formats/animation_timing.hpp"
#include "ui/image_save_option_keys.hpp"
#include "ui/pdf_export.hpp"
#include "ui/pdf_import.hpp"

#include <QCoreApplication>
#include <QJSValueIterator>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>

namespace patchy::ui {

namespace {

// A finite integer within [low, high].
std::optional<int> integer_in_range(const QJSValue& value, int low, int high) {
  if (!value.isNumber()) {
    return std::nullopt;
  }
  const auto number = value.toNumber();
  if (!std::isfinite(number) || number != std::floor(number) || number < low || number > high) {
    return std::nullopt;
  }
  return static_cast<int>(number);
}

QString quoted_tokens(const QStringList& tokens) {
  QStringList quoted;
  for (const auto& token : tokens) {
    quoted << QLatin1Char('"') + token + QLatin1Char('"');
  }
  return quoted.join(QStringLiteral(", "));
}

struct KeyReader {
  const QString& method;
  QString error;

  void fail(const QString& text) {
    if (error.isEmpty()) {
      error = text;
    }
  }

  void read_int(const QString& key, const QJSValue& value, int low, int high, int& target) {
    const auto parsed = integer_in_range(value, low, high);
    if (!parsed.has_value()) {
      fail(QCoreApplication::translate("patchy::ui::ScriptSaveOptions", "%1: option \"%2\" must be an integer from %3 to %4.")
               .arg(method, key, QString::number(low), QString::number(high)));
      return;
    }
    target = *parsed;
  }

  void read_bool(const QString& key, const QJSValue& value, bool& target) {
    if (!value.isBool()) {
      fail(QCoreApplication::translate("patchy::ui::ScriptSaveOptions", "%1: option \"%2\" must be true or false.").arg(method, key));
      return;
    }
    target = value.toBool();
  }

  void read_string(const QString& key, const QJSValue& value, QString& target) {
    if (!value.isString()) {
      fail(QCoreApplication::translate("patchy::ui::ScriptSaveOptions", "%1: option \"%2\" must be a string.").arg(method, key));
      return;
    }
    target = value.toString();
  }

  // A string that must be one of `allowed`; `apply` receives the matched token.
  void read_token(const QString& key, const QJSValue& value, const QStringList& allowed,
                  const std::function<void(const QString&)>& apply) {
    if (value.isString() && allowed.contains(value.toString())) {
      apply(value.toString());
      return;
    }
    fail(QCoreApplication::translate("patchy::ui::ScriptSaveOptions", "%1: option \"%2\" must be one of %3.").arg(method, key, quoted_tokens(allowed)));
  }

  void unknown(const QString& key, const QString& extension) {
    fail(QCoreApplication::translate("patchy::ui::ScriptSaveOptions", "%1: option \"%2\" does not apply to a .%3 file.").arg(method, key, extension));
  }
};

QStringList dds_compression_tokens() {
  QStringList tokens;
  for (const auto compression : {dds::Compression::Automatic, dds::Compression::Uncompressed, dds::Compression::Bc1,
                                 dds::Compression::Bc3, dds::Compression::Bc4, dds::Compression::Bc5,
                                 dds::Compression::Bc7}) {
    tokens << dds_compression_key(compression);
  }
  return tokens;
}

QStringList dds_mipmap_tokens() {
  QStringList tokens;
  for (const auto choice : {dds::MipmapChoice::Automatic, dds::MipmapChoice::Generate, dds::MipmapChoice::None}) {
    tokens << dds_mipmap_choice_key(choice);
  }
  return tokens;
}

QStringList rttex_encoding_tokens() {
  QStringList tokens;
  for (const auto encoding : {rttex::Encoding::Rgba8, rttex::Encoding::Rgba4444, rttex::Encoding::Jpeg}) {
    tokens << rttex_encoding_key(encoding);
  }
  return tokens;
}

QStringList rttex_power_of_two_tokens() {
  QStringList tokens;
  for (const auto mode : {rttex::PowerOfTwo::Pad, rttex::PowerOfTwo::Stretch, rttex::PowerOfTwo::None}) {
    tokens << rttex_power_of_two_key(mode);
  }
  return tokens;
}

QStringList pdf_image_quality_tokens() {
  QStringList tokens;
  for (const auto& preset : pdf_image_quality_presets()) {
    tokens << QString::fromLatin1(preset.id);
  }
  return tokens;
}

QStringList ico_size_tokens() {
  QStringList tokens;
  for (const auto size : kIcoSizeChoices) {
    tokens << QString::number(size);
  }
  return tokens;
}

// `sizes`: a non-empty array of distinct icon sizes from kIcoSizeChoices, kept in the
// script's order.
void read_ico_sizes(KeyReader& reader, const QString& key, const QJSValue& value, std::vector<int>& target) {
  const auto fail = [&] {
    reader.fail(QCoreApplication::translate("patchy::ui::ScriptSaveOptions", "%1: option \"%2\" must be a non-empty array of sizes from %3.")
                    .arg(reader.method, key, ico_size_tokens().join(QStringLiteral(", "))));
  };
  if (!value.isArray()) {
    fail();
    return;
  }
  const auto count = value.property(QStringLiteral("length")).toUInt();
  if (count == 0) {
    fail();
    return;
  }
  std::vector<int> sizes;
  for (quint32 index = 0; index < count; ++index) {
    const auto size = integer_in_range(value.property(index), 1, 256);
    if (!size.has_value() || std::find(kIcoSizeChoices.begin(), kIcoSizeChoices.end(), *size) == kIcoSizeChoices.end()) {
      fail();
    return;
    }
    if (std::find(sizes.begin(), sizes.end(), *size) == sizes.end()) {
      sizes.push_back(*size);
    }
  }
  target = std::move(sizes);
}

// `hotspot`: {x, y} integers inside a 256 px cursor.
void read_hotspot(KeyReader& reader, const QString& key, const QJSValue& value, ImageSaveOptions& options) {
  const auto fail = [&] {
    reader.fail(QCoreApplication::translate("patchy::ui::ScriptSaveOptions", "%1: option \"%2\" must be an object {x, y} of integers from 0 to 255.")
                    .arg(reader.method, key));
  };
  if (!value.isObject() || value.isArray() || value.isNull()) {
    fail();
    return;
  }
  const auto x = integer_in_range(value.property(QStringLiteral("x")), 0, 255);
  const auto y = integer_in_range(value.property(QStringLiteral("y")), 0, 255);
  if (!x.has_value() || !y.has_value()) {
    fail();
    return;
  }
  options.cur_hotspot_x = *x;
  options.cur_hotspot_y = *y;
}

// Applies one key for the extension family. Returns false when the key is unknown for it;
// a type or range problem is recorded on the reader (which still returns true, handled).
bool apply_key(KeyReader& reader, const QString& extension, const QString& key, const QJSValue& value,
               ImageSaveOptions& options) {
  const bool ico = is_ico_extension(extension);
  const bool cur = is_cur_extension(extension);
  // File Info metadata (docs/file-info.md), for every format that carries it.
  if (key == QStringLiteral("metadata") &&
      (is_jpeg_extension(extension) || is_webp_extension(extension) || extension == QStringLiteral("png") ||
       extension == QStringLiteral("tif") || extension == QStringLiteral("tiff") ||
       extension == QStringLiteral("gif") || is_jxl_extension(extension) || is_avif_extension(extension))) {
    reader.read_token(key, value,
                      {QStringLiteral("all"), QStringLiteral("copyright"), QStringLiteral("basic"),
                       QStringLiteral("none")},
                      [&](const QString& token) {
                        options.metadata_policy =
                            metadata_policy_from_key(token).value_or(metadata::MetadataPolicy::All);
                      });
    return true;
  }
  // CMYK output (docs/cmyk.md), for the formats that can carry it.
  if (key == QStringLiteral("colorMode") &&
      (is_jpeg_extension(extension) || extension == QStringLiteral("tif") || extension == QStringLiteral("tiff") ||
       extension == QStringLiteral("pdf"))) {
    reader.read_token(key, value, {QStringLiteral("rgb"), QStringLiteral("cmyk")},
                      [&](const QString& token) {
                        options.export_cmyk = token == QStringLiteral("cmyk");
                        options.export_color_explicit = true;
                      });
    return true;
  }
  if (key == QStringLiteral("blackInkOnly") &&
      (is_jpeg_extension(extension) || extension == QStringLiteral("tif") || extension == QStringLiteral("tiff") ||
       extension == QStringLiteral("pdf"))) {
    reader.read_bool(key, value, options.export_black_ink_only);
    return true;
  }
  // Layered TIFF (docs/tiff.md): Photoshop's layer data beside the composite.
  if (key == QStringLiteral("layers") && (extension == QStringLiteral("tif") || extension == QStringLiteral("tiff"))) {
    bool keep = true;
    reader.read_bool(key, value, keep);
    options.tiff_layers = keep;
    return true;
  }
  // Layered saves (docs/blend-modes.md, "Layered saves"): layers in blend modes the format
  // cannot store are saved as Normal ("normal", the default) or keep the look ("bake").
  if (key == QStringLiteral("unsupportedBlendModes") &&
      (extension == QStringLiteral("psd") || extension == QStringLiteral("psb") ||
       extension == QStringLiteral("tif") || extension == QStringLiteral("tiff") ||
       extension == QStringLiteral("aseprite") || extension == QStringLiteral("ase"))) {
    reader.read_token(key, value, {QStringLiteral("normal"), QStringLiteral("bake")},
                      [&](const QString& token) { options.bake_unsupported_blend_modes = token == QStringLiteral("bake"); });
    return true;
  }
  if (is_jpeg_extension(extension)) {
    if (key == QStringLiteral("quality")) {
      reader.read_int(key, value, 0, 100, options.jpeg_quality);
      return true;
    }
    // Export for Web's JPEG coding (docs/web-export.md).
    if (key == QStringLiteral("progressive")) {
      reader.read_bool(key, value, options.jpeg_progressive);
      return true;
    }
    if (key == QStringLiteral("optimizeCoding")) {
      reader.read_bool(key, value, options.jpeg_optimize);
      return true;
    }
    if (key == QStringLiteral("thumbnail")) {
      // Asked for explicitly, the thumbnail is written with every metadata choice, none
      // included, as Export for Web's "Include thumbnail" is.
      reader.read_bool(key, value, options.jpeg_exif_thumbnail);
      options.jpeg_exif_thumbnail_without_metadata = options.jpeg_exif_thumbnail;
      return true;
    }
    return false;
  }
  if (is_webp_extension(extension)) {
    if (key == QStringLiteral("quality")) {
      reader.read_int(key, value, 0, 100, options.webp_quality);
      return true;
    }
    if (key == QStringLiteral("lossless")) {
      reader.read_bool(key, value, options.webp_lossless);
      return true;
    }
    return false;
  }
  if (is_jxr_extension(extension)) {
    if (key == QStringLiteral("quality")) {
      reader.read_int(key, value, 1, 100, options.jxr_quality);
      return true;
    }
    if (key == QStringLiteral("lossless")) {
      reader.read_bool(key, value, options.jxr_lossless);
      return true;
    }
    return false;
  }
  if (is_avif_extension(extension)) {
    if (key == QStringLiteral("quality")) {
      reader.read_int(key, value, 0, 100, options.avif_quality);
      return true;
    }
    if (key == QStringLiteral("lossless")) {
      reader.read_bool(key, value, options.avif_lossless);
      return true;
    }
    if (key == QStringLiteral("speed")) {
      reader.read_int(key, value, 0, 10, options.avif_speed);
      return true;
    }
    return false;
  }
  if (is_jxl_extension(extension)) {
    if (key == QStringLiteral("quality")) {
      reader.read_int(key, value, 1, 100, options.jxl_quality);
      return true;
    }
    if (key == QStringLiteral("lossless")) {
      reader.read_bool(key, value, options.jxl_lossless);
      return true;
    }
    if (key == QStringLiteral("effort")) {
      reader.read_int(key, value, 1, 9, options.jxl_effort);
      return true;
    }
    if (key == QStringLiteral("animate")) {
      reader.read_bool(key, value, options.jxl_animate);
      return true;
    }
    if (key == QStringLiteral("frameDelayMs")) {
      reader.read_int(key, value, 0, static_cast<int>(animation::kMaxFrameDelayMs), options.animation_frame_delay_ms);
      return true;
    }
    if (key == QStringLiteral("loopCount")) {
      reader.read_int(key, value, 0, 65535, options.jxl_loop_count);
      return true;
    }
    return false;
  }
  if (is_dds_extension(extension)) {
    if (key == QStringLiteral("compression")) {
      reader.read_token(key, value, dds_compression_tokens(), [&](const QString& token) {
        options.dds_compression = dds_compression_from_key(token, options.dds_compression);
      });
      return true;
    }
    if (key == QStringLiteral("mipmaps")) {
      reader.read_token(key, value, dds_mipmap_tokens(), [&](const QString& token) {
        options.dds_mipmaps = dds_mipmap_choice_from_key(token, options.dds_mipmaps);
      });
      return true;
    }
    return false;
  }
  if (ico || cur) {
    if (key == QStringLiteral("sizes")) {
      read_ico_sizes(reader, key, value, options.ico_sizes);
      return true;
    }
    if (key == QStringLiteral("resample")) {
      reader.read_token(key, value, {QStringLiteral("auto"), QStringLiteral("nearest"), QStringLiteral("smooth")},
                               [&](const QString& token) {
                                 options.ico_resample = ico_resample_from_key(token, options.ico_resample);
                               });
      return true;
    }
    if (cur && key == QStringLiteral("hotspot")) {
      read_hotspot(reader, key, value, options);
      return true;
    }
    return false;
  }
  if (is_bmp_extension(extension)) {
    if (key == QStringLiteral("encoding")) {
      reader.read_token(key, value,
                               {QStringLiteral("rgba32"), QStringLiteral("rgb24"), QStringLiteral("indexed8"),
                                QStringLiteral("indexed4"), QStringLiteral("indexed2")},
                               [&](const QString& token) {
                                 options.bmp_encoding = bmp_encoding_from_key(token, options.bmp_encoding);
                               });
      return true;
    }
    if (key == QStringLiteral("paletteMode")) {
      reader.read_token(key, value, {QStringLiteral("exact"), QStringLiteral("quantize"), QStringLiteral("paletteFile")},
                               [&](const QString& token) {
                                 options.bmp_palette_mode = bmp_palette_mode_from_key(token, options.bmp_palette_mode);
                               });
      return true;
    }
    if (key == QStringLiteral("palettePath")) {
      reader.read_string(key, value, options.bmp_palette_path);
      return true;
    }
    return false;
  }
  if (is_rttex_extension(extension)) {
    if (key == QStringLiteral("encoding")) {
      reader.read_token(key, value, rttex_encoding_tokens(), [&](const QString& token) {
        options.rttex_encoding = rttex_encoding_from_key(token, options.rttex_encoding);
      });
      return true;
    }
    if (key == QStringLiteral("quality")) {
      reader.read_int(key, value, 1, 100, options.rttex_jpeg_quality);
      return true;
    }
    if (key == QStringLiteral("powerOfTwo")) {
      reader.read_token(key, value, rttex_power_of_two_tokens(), [&](const QString& token) {
        options.rttex_power_of_two = rttex_power_of_two_from_key(token, options.rttex_power_of_two);
      });
      return true;
    }
    if (key == QStringLiteral("forceSquare")) {
      reader.read_bool(key, value, options.rttex_force_square);
      return true;
    }
    if (key == QStringLiteral("forceAlpha")) {
      reader.read_bool(key, value, options.rttex_force_alpha);
      return true;
    }
    if (key == QStringLiteral("compress")) {
      reader.read_bool(key, value, options.rttex_compress);
      return true;
    }
    return false;
  }
  if (is_pdf_extension(extension)) {
    // The same names as app.exportPdf. A preset names both halves of the image choice, so
    // it is applied after `lossless` regardless of key order (see the caller).
    if (key == QStringLiteral("imageQuality")) {
      reader.read_token(key, value, pdf_image_quality_tokens(), [&](const QString& token) {
        for (const auto& preset : pdf_image_quality_presets()) {
          if (token == QLatin1String(preset.id)) {
            options.pdf_lossless = preset.lossless;
            options.pdf_jpeg_quality = preset.jpeg_quality;
          }
        }
      });
      return true;
    }
    if (key == QStringLiteral("lossless")) {
      reader.read_bool(key, value, options.pdf_lossless);
      return true;
    }
    if (key == QStringLiteral("editableLayers")) {
      reader.read_bool(key, value, options.pdf_editable_layers);
      return true;
    }
    if (key == QStringLiteral("keepOriginalImageData")) {
      reader.read_bool(key, value, options.pdf_keep_original_images);
      return true;
    }
    if (key == QStringLiteral("missingFontsAsImages")) {
      reader.read_bool(key, value, options.pdf_missing_fonts_as_images);
      return true;
    }
    if (key == QStringLiteral("artboardPages")) {
      bool artboard_pages = options.pdf_pages == QStringLiteral("artboards");
      reader.read_bool(key, value, artboard_pages);
      options.pdf_pages = artboard_pages ? QStringLiteral("artboards") : QStringLiteral("canvas");
      return true;
    }
    if (key == QStringLiteral("pages")) {
      reader.read_token(key, value, {QStringLiteral("artboards"), QStringLiteral("groups"), QStringLiteral("canvas")},
                        [&options](const QString& token) { options.pdf_pages = token; });
      return true;
    }
    return false;
  }
  // GIF and palette PNG quantization (docs/web-export.md); PNG takes them with pngColors "palette".
  if ((extension == QStringLiteral("gif") || extension == QStringLiteral("png"))) {
    if (key == QStringLiteral("colors")) {
      reader.read_int(key, value, 2, 256, options.palette_colors);
      return true;
    }
    if (key == QStringLiteral("dither")) {
      reader.read_token(key, value, {QStringLiteral("none"), QStringLiteral("diffusion")},
                        [&options](const QString& token) { options.palette_dither = token == QStringLiteral("diffusion"); });
      return true;
    }
    if (key == QStringLiteral("ditherAmount")) {
      reader.read_int(key, value, 0, 100, options.palette_dither_amount);
      return true;
    }
  }
  // Animated PNG (APNG) from visible layers, the keys JPEG XL takes (docs/web-export.md).
  if (extension == QStringLiteral("png")) {
    if (key == QStringLiteral("animate")) {
      reader.read_bool(key, value, options.apng_animate);
      return true;
    }
    if (key == QStringLiteral("frameDelayMs")) {
      reader.read_int(key, value, 0, static_cast<int>(animation::kMaxFrameDelayMs), options.animation_frame_delay_ms);
      return true;
    }
    if (key == QStringLiteral("loopCount")) {
      reader.read_int(key, value, 0, 65535, options.apng_loop_count);
      return true;
    }
  }
  if (extension == QStringLiteral("png") && key == QStringLiteral("pngColors")) {
    reader.read_token(key, value, {QStringLiteral("full"), QStringLiteral("palette")},
                      [&options](const QString& token) { options.png_palette = token == QStringLiteral("palette"); });
    return true;
  }
  if (extension == QStringLiteral("gif")) {
    if (key == QStringLiteral("animate")) {
      reader.read_bool(key, value, options.gif_animate);
      return true;
    }
    if (key == QStringLiteral("frameDelayMs")) {
      int delay_ms = -1;
      reader.read_int(key, value, 0, static_cast<int>(animation::kMaxFrameDelayMs), delay_ms);
      if (delay_ms >= 0) {
        options.animation_frame_delay_ms = delay_ms;
        options.gif_frame_delay_cs = std::clamp(delay_ms / 10, 0, 0xffff);
      }
      return true;
    }
    return false;
  }
  return false;
}

}  // namespace

QString apply_script_image_save_options(const QString& method, const QString& extension, const QJSValue& object,
                                        ImageSaveOptions& options) {
  if (object.isUndefined()) {
    return {};
  }
  if (!object.isObject() || object.isArray() || object.isNull()) {
    return QCoreApplication::translate("patchy::ui::ScriptSaveOptions", "%1: options must be an object such as {quality: 85}.").arg(method);
  }
  const auto normalized = normalized_save_extension(extension);
  KeyReader reader{method, {}};
  // PDF: `lossless` first so an `imageQuality` preset wins regardless of key order (the
  // exportPdf rule).
  QStringList keys;
  QJSValueIterator it(object);
  while (it.hasNext()) {
    it.next();
    keys << it.name();
  }
  std::stable_sort(keys.begin(), keys.end(), [](const QString& a, const QString& b) {
    return (a == QStringLiteral("lossless")) > (b == QStringLiteral("lossless"));
  });
  bool palette_keys = false;
  for (const auto& key : keys) {
    palette_keys = palette_keys || key == QStringLiteral("colors") || key == QStringLiteral("dither") ||
                   key == QStringLiteral("ditherAmount");
    if (!apply_key(reader, normalized, key, object.property(key), options)) {
      reader.unknown(key, normalized);
    }
    if (!reader.error.isEmpty()) {
      return reader.error;
    }
  }
  if (options.tiff_layers.value_or(false) && options.export_cmyk) {
    return QCoreApplication::translate("patchy::ui::ScriptSaveOptions",
                                       "%1: layers needs colorMode: \"rgb\"; a TIFF with layers is saved in RGB.")
        .arg(method);
  }
  if (normalized == QStringLiteral("png") && options.apng_animate && options.png_palette) {
    return QCoreApplication::translate("patchy::ui::ScriptSaveOptions",
                                       "%1: an animated PNG is full color, so animate cannot be combined with "
                                       "pngColors: \"palette\".")
        .arg(method);
  }
  if (normalized == QStringLiteral("png") && palette_keys && !options.png_palette) {
    return QCoreApplication::translate("patchy::ui::ScriptSaveOptions",
                                       "%1: colors, dither and ditherAmount need pngColors: \"palette\" for PNG.")
        .arg(method);
  }
  if (options.export_black_ink_only && !options.export_cmyk) {
    return QCoreApplication::translate("patchy::ui::ScriptSaveOptions",
                                       "%1: blackInkOnly needs colorMode: \"cmyk\".")
        .arg(method);
  }
  return {};
}

}  // namespace patchy::ui
