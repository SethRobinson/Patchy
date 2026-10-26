#pragma once
// Text document construction and run formatting shared by editing and rendering.
// No window or canvas ownership crosses this interface.
#include "core/vector_shape.hpp"
#include "ui/text_font_service.hpp"
#include <QColor>
#include <QFont>
#include <QString>
#include <QTextDocument>
#include <QTextCharFormat>
#include <cstdint>
#include <memory>
#include <optional>
namespace patchy::ui {
inline constexpr int kMinimumTextBoxDocumentSize = 16;
struct TextToolSettings {
  QString text;
  QString html;
  QString family;
  int size{36};
  bool bold{false};
  bool italic{false};
  int anti_alias{3};
  bool boxed{false};
  int box_width{0};
  int box_height{0};
  // Photoshop leading-model layout (kLayerMetadataTextLayoutMode == "photoshop"): baselines
  // advance by each entered line's max leading instead of Qt's natural spacing.
  bool photoshop_layout{false};
  // Vertical type (kLayerMetadataTextOrientation == "vertical"): upright glyphs stacked in
  // columns that advance right to left. See ui/text_layout.hpp.
  bool vertical{false};
  std::optional<VectorPath> area{std::nullopt};
};


// The document a text layer is rasterized from, plus the font/width the layout was built against.
// Built by build_text_render_document -- the single construction shared by the rasterizer and the
// caret/selection layout so glyphs and caret geometry can never diverge.
struct TextRenderDocument {
  std::unique_ptr<QTextDocument> document;
  QFont font;
  int text_width{0};
};


QString paragraph_alignment_name(Qt::Alignment alignment);
Qt::Alignment paragraph_alignment_from_name(const QString& name);
void set_text_display_family(QTextCharFormat& format, const QString& family);
QString text_display_family_from_format(const QTextCharFormat& format, const QString& fallback);
QTextCharFormat text_format_with_smoothing(QTextCharFormat format, int anti_alias);
void apply_text_smoothing_to_document(QTextDocument& document, int anti_alias);
void set_stretch_for_advance_ratio(QFont& font, double ratio, int naive_value);
void scale_document_font_sizes(QTextDocument& document, double scale);
std::unique_ptr<QTextDocument> copy_text_document_formats(const QTextDocument& source);
QString rich_text_runs_from_document(const QTextDocument& document, const TextToolSettings& fallback,
                                     QColor fallback_color);
// A runs-list version header ("v1" .. "v9", or a later one).
bool is_text_runs_version_line(const QString& line);
// The character-panel styles of one format (runs v9 column 16, GitHub issue 346): caps 1 Small
// Caps, 2 All Caps; baseline 1 superscript, 2 subscript.
struct TextCharacterStyles {
  int caps{0};
  int baseline{0};
  bool underline{false};
  bool strikethrough{false};
  bool ordinals{false};
  bool fractions{false};
  [[nodiscard]] bool any() const noexcept {
    return caps != 0 || baseline != 0 || underline || strikethrough || ordinals || fractions;
  }
  friend bool operator==(const TextCharacterStyles&, const TextCharacterStyles&) = default;
};
TextCharacterStyles text_character_styles_from_format(const QTextCharFormat& format);
void set_text_character_styles_on_format(QTextCharFormat& format, const TextCharacterStyles& styles);
QString serialize_text_character_styles(const TextCharacterStyles& styles);
TextCharacterStyles parse_text_character_styles(const QString& field);
// Render documents (build_text_render_document): turns the character styles into fonts, baseline
// shifts and OpenType feature fragments. Underline and strikethrough stay flags; the render plan
// draws them. Vertical text takes the caps only.
void apply_text_character_styles_to_document(QTextDocument& document, bool vertical);
// A render-document fragment drawn from its own featured shaping, and the font that shaping uses.
bool text_format_has_render_features(const QTextCharFormat& format);
QFont text_render_featured_font(const QTextCharFormat& format);
QString paragraph_runs_from_document(const QTextDocument& document);
void apply_paragraph_runs_to_document(QTextDocument& document, const QString& paragraph_runs, double scale = 1.0);
void apply_patchy_text_runs_to_document(QTextDocument& document, const QString& runs_text, const QFont& fallback_font,
                                        QColor fallback_color, double scale, int anti_alias);
QString document_html_from_text_runs(const QString& text, const QString& rich_text_runs,
                                     const TextToolSettings& fallback, QColor fallback_color);
// `replacements` (Resolve Missing Fonts) swaps only the families it names and leaves every other
// missing family as it is; without it each missing family moves to the face drawing it.
void substitute_missing_document_font_families(QTextDocument& document,
                                               const TextFontReplacements* replacements = nullptr);
TextRenderDocument build_text_render_document(const TextToolSettings& settings, QColor color,
                                              std::int32_t max_width, const QString& paragraph_runs,
                                              const QString& rich_text_runs, double metric_scale,
                                              double layout_scale = 1.0, double box_scale = 1.0);
QString first_text_run_style_name(const QString& rich_text_runs);

}  // namespace patchy::ui
