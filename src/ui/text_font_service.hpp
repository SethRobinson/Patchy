#pragma once

// Font lookup shared by text rendering, editing, the PSD resolver and font diagnostics.
// Platform registration and caches live in the implementation; this interface owns no UI.
#include <QFont>
#include <QFontDatabase>
#include <QHash>
#include <QString>
#include <QStringList>
#include <optional>

namespace patchy { class Layer; }
namespace patchy::ui {
struct AvailableTextFamilyStyle {
  QString family;
  QString style;
};

struct TextStyleFlags {
  bool bold{false};
  bool italic{false};
};

struct TextFontProblems {
  QStringList not_installed;  // absent from the font database
  QStringList no_glyphs;      // installed, with no glyph for any character asked of it
  [[nodiscard]] bool empty() const { return not_installed.isEmpty() && no_glyphs.isEmpty(); }
};


// Resolve Missing Fonts: each missing family the user replaced, mapped to the installed family
// chosen for it, keyed by text_font_replacement_key. A missing family absent from the map is left
// unresolved.
using TextFontReplacements = QHash<QString, QString>;
QString text_font_replacement_key(const QString& family);

std::optional<QString> available_text_family_match(const QString& family);
std::optional<AvailableTextFamilyStyle> available_text_family_style_match(const QString& family,
                                                                        bool consult_name_tables = true);
QString canonical_text_display_family(const QString& family);
QString installed_text_family(const QString& family);
QString default_text_family();
QStringList available_text_family_styles(const QString& family);
QString text_style_query_family(const QString& family);
TextStyleFlags text_style_flags_for_name(const QString& style);
TextStyleFlags text_style_flags_for_style(const QString& family, const QString& style);
bool text_style_is_flag_expressible(const QString& style);
QString text_style_name_for_flags(bool bold, bool italic);
bool family_offers_face_axis(const QString& family, bool bold_axis);
QString real_face_style_name(const QString& family, bool bold, bool italic);
bool text_family_uses_photoshop_latin_fallback(const QString& family);
QString display_text_family_from_font(const QFont& font);
QStringList render_text_families_for_display_family(const QString& family);
QString substituted_text_family(const QString& family, const QString& demanded);
void append_missing_text_family(QStringList& missing, const QString& family, const QString& demanded,
                                QStringList* no_glyphs = nullptr);
QStringList missing_text_families_for_psd_raster_preview(const QString& primary_family, const QString& runs_text,
                                                       const QString& text, QStringList* no_glyphs = nullptr);
QFont render_text_font_for_display_family(const QString& family, int pixel_size, bool bold, bool italic,
                                         int anti_alias, const QString& style_name = QString());
QFontDatabase::WritingSystem fallback_writing_system_for_character(QChar character);
void configure_text_font_smoothing(QFont& font, int anti_alias);
QStringList missing_text_families_for_layer(const Layer& layer);
TextFontProblems text_font_problems_for_layer(const Layer& layer);
bool ensure_headless_system_fonts_loaded();

}  // namespace patchy::ui
