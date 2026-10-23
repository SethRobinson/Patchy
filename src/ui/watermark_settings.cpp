#include "ui/watermark_settings.hpp"

#include "ai/trustmark_watermark.hpp"
#include "support/translate_noop.hpp"
#include "ui/app_settings.hpp"
#include "ui/image_document_io.hpp"

#include <QImage>
#include <QSettings>

#include <utility>

namespace patchy::ui {

QString watermark_save_mode_token(WatermarkSaveMode mode) {
  switch (mode) {
    case WatermarkSaveMode::Never:
      return QStringLiteral("never");
    case WatermarkSaveMode::Ask:
      return QStringLiteral("ask");
    case WatermarkSaveMode::Always:
      return QStringLiteral("always");
  }
  return QStringLiteral("never");
}

std::optional<WatermarkSaveMode> watermark_save_mode_from_token(const QString& token) {
  for (const auto mode : {WatermarkSaveMode::Never, WatermarkSaveMode::Ask, WatermarkSaveMode::Always}) {
    if (watermark_save_mode_token(mode) == token) {
      return mode;
    }
  }
  return std::nullopt;
}

bool valid_watermark_text(const QString& text) {
  if (text.size() > trustmark::schema_text_capacity(kSaveWatermarkSchema)) {
    return false;
  }
  for (const auto ch : text) {
    if (ch.unicode() < 32 || ch.unicode() > 126) {
      return false;
    }
  }
  return true;
}

QString stored_watermark_text() {
  const auto text = app_settings().value(QLatin1String(kWatermarkTextSettingKey)).toString();
  return valid_watermark_text(text) ? text : QString();
}

void set_stored_watermark_text(const QString& text) {
  app_settings().setValue(QLatin1String(kWatermarkTextSettingKey), valid_watermark_text(text) ? text : QString());
}

WatermarkSaveMode stored_watermark_save_mode() {
  return watermark_save_mode_from_token(app_settings().value(QLatin1String(kWatermarkSaveModeSettingKey)).toString())
      .value_or(WatermarkSaveMode::Never);
}

void set_stored_watermark_save_mode(WatermarkSaveMode mode) {
  app_settings().setValue(QLatin1String(kWatermarkSaveModeSettingKey), watermark_save_mode_token(mode));
}

bool stored_watermark_check_on_open() {
  return app_settings().value(QLatin1String(kWatermarkCheckOnOpenSettingKey), false).toBool();
}

void set_stored_watermark_check_on_open(bool check) {
  app_settings().setValue(QLatin1String(kWatermarkCheckOnOpenSettingKey), check);
}

bool save_format_takes_watermark(const Document& document, const QString& extension, bool tiff_layers,
                                 bool animated) {
  if (animated || document.color_state().bit_depth != BitDepth::UInt8 || document.palette_editing().has_value() ||
      document.indexed_palette().has_value()) {
    return false;
  }
  const auto lower = extension.toLower();
  if (lower == QStringLiteral("tif") || lower == QStringLiteral("tiff")) {
    return !tiff_layers;
  }
  static const QStringList flat{QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                QStringLiteral("jpe"), QStringLiteral("webp"), QStringLiteral("bmp"),
                                QStringLiteral("jxl"), QStringLiteral("avif")};
  return flat.contains(lower);
}

std::optional<Document> watermarked_save_document(const Document& document, const SaveWatermark& watermark,
                                                  bool preserve_alpha, std::vector<std::string>* notices) {
  const auto note = [notices](const char* text) {
    if (notices != nullptr) {
      notices->emplace_back(text);
    }
  };
  if (document.width() < kMinimumWatermarkSide || document.height() < kMinimumWatermarkSide) {
    note(PATCHY_TRANSLATE_NOOP("QObject", "No invisible watermark was added: the image is too small."));
    return std::nullopt;
  }
  if (!patchy::ai::trustmark_models_available()) {
    note(PATCHY_TRANSLATE_NOOP("QObject",
                               "No invisible watermark was added: the watermark models are not downloaded."));
    return std::nullopt;
  }
  auto pixels = pixels_from_image_rgba(flat_export_qimage(document, preserve_alpha));
  if (pixels.empty()) {
    return std::nullopt;
  }
  try {
    // Already carries the same text (an earlier save, or Add Invisible Watermark): adding it
    // again would only degrade the pixels.
    for (const auto& mark : patchy::ai::scan_trustmark(pixels).marks) {
      if (mark.variant == patchy::ai::TrustMarkVariant::Q && mark.text == watermark.text) {
        note(PATCHY_TRANSLATE_NOOP("QObject", "The image already carries your invisible watermark."));
        return std::nullopt;
      }
    }
    patchy::ai::embed_trustmark_q(pixels, watermark.packet, watermark.strength);
  } catch (const std::exception&) {
    note(PATCHY_TRANSLATE_NOOP("QObject", "No invisible watermark was added: TrustMark could not run."));
    return std::nullopt;
  }
  Document marked = document;
  marked.layers().clear();
  marked.clear_active_layer();
  auto& layer = marked.add_pixel_layer("Watermarked", std::move(pixels));
  marked.set_active_layer(layer.id());
  note(PATCHY_TRANSLATE_NOOP("QObject", "Added your invisible watermark."));
  return marked;
}

}  // namespace patchy::ui
