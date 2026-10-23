#pragma once

// Preferences > Watermarks (docs/watermarks.md, "Preferences"): the TrustMark text Patchy
// adds when saving, when it adds it, and whether opened images are checked for watermarks.
// The settings keys and the mode tokens are permanent.

#include "core/document.hpp"
#include "core/trustmark_payload.hpp"

#include <QString>

#include <optional>
#include <string>
#include <vector>

namespace patchy::ui {

enum class WatermarkSaveMode { Never, Ask, Always };

// Settings keys (never rename).
inline constexpr auto kWatermarkTextSettingKey = "watermarks/payloadText";
inline constexpr auto kWatermarkSaveModeSettingKey = "watermarks/addOnSave";
inline constexpr auto kWatermarkCheckOnOpenSettingKey = "watermarks/checkOnOpen";

// "never", "ask", "always" (also the scripting values).
[[nodiscard]] QString watermark_save_mode_token(WatermarkSaveMode mode);
[[nodiscard]] std::optional<WatermarkSaveMode> watermark_save_mode_from_token(const QString& token);

// The schema saves use: TrustMark's default, BCH_5 (8 characters).
inline constexpr auto kSaveWatermarkSchema = trustmark::Schema::Bch5;

// Printable ASCII, at most the schema's capacity: what the Watermark text field accepts.
[[nodiscard]] bool valid_watermark_text(const QString& text);

[[nodiscard]] QString stored_watermark_text();
void set_stored_watermark_text(const QString& text);
[[nodiscard]] WatermarkSaveMode stored_watermark_save_mode();  // Never by default
void set_stored_watermark_save_mode(WatermarkSaveMode mode);
[[nodiscard]] bool stored_watermark_check_on_open();  // off by default
void set_stored_watermark_check_on_open(bool check);

// What a save adds: the TrustMark Q packet for the stored text at the default strength.
struct SaveWatermark {
  trustmark::Packet packet{};
  std::string text;
  float strength{1.0F};
};

// True when a save of `document` to `extension` with these writer settings writes one flat
// 8-bit RGB raster the mark can live in: PNG, JPEG, WebP, BMP, flat TIFF, JPEG XL and AVIF,
// not animated, not palette or deep. Layered saves (PSD, PSB, layered TIFF), vector and
// page formats (SVG, PDF), GIF and the texture and icon formats are never marked.
[[nodiscard]] bool save_format_takes_watermark(const Document& document, const QString& extension, bool tiff_layers,
                                               bool animated);

// The images too small to mark (either side under this many pixels) are skipped.
inline constexpr int kMinimumWatermarkSide = 128;

// The document a flat writer receives instead of `document`: one 8-bit layer holding the
// flattened image with the mark added, the rest (file info, color state, print settings)
// copied. Empty, with a reason in `notices`, when the image is too small, already carries
// this text, or the models cannot run. Runs the models: call it off the UI thread.
[[nodiscard]] std::optional<Document> watermarked_save_document(const Document& document,
                                                                const SaveWatermark& watermark,
                                                                bool preserve_alpha,
                                                                std::vector<std::string>* notices);

}  // namespace patchy::ui
