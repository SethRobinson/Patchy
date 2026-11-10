#pragma once

// Public face of the PSD text-run model: the per-run style/paragraph structs
// and the serializers that produce the patchy.text.runs / patchy.text.paragraph.runs
// metadata (persistence contracts, v1-v3) plus the Qt rich-text body for
// patchy.text.html. Split out of psd_io_internal.hpp so non-PSD importers
// (the Affinity .af reader) can emit the same editable-text metadata;
// definitions live in psd_text_read.cpp.

#include "core/layer.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <cmath>
#include <string_view>
#include <vector>

namespace patchy::psd {

struct PsdTextStyleRun {
  int start{0};
  int length{0};
  std::string family{"Arial"};
  double size{36.0};
  RgbColor color{0, 0, 0};
  // The face (OpenType subfamily) the run names: "Italic", "Demi", "Black", "Condensed Bold".
  // Empty means Regular, or that the source only knew the bold/italic flags. Authoritative when
  // set: a family's styles are an arbitrary list, and bold+italic can only name four of them,
  // which is why weights like Demi used to have to be smuggled into `family`.
  std::string style;
  // The REAL face the run names (Georgia-Italic is italic, CenturyGothic-Bold is bold). Kept
  // alongside `style` because every persisted run format carries them and because they are the
  // only thing a family with no matching style name can fall back on.
  bool bold{false};
  bool italic{false};
  // Photoshop's /FauxBold: a synthetic emboldening of whatever face `bold`/`italic` select, NOT
  // a request for the family's real bold face. Kept separate because the two render differently
  // (Georgia Bold Italic is ~14% wider than faux-bolded Georgia Italic) and because Photoshop's
  // own Character panel shows them as separate controls.
  bool faux_bold{false};
  // Photoshop's /FauxItalic: a synthetic slant of the named face, NOT a request for the family's
  // real italic face. Separate for the same reason as faux_bold, and because Qt cannot express it
  // through QFont at all -- setStyle(StyleOblique) resolves to the real Italic face when one
  // exists, so it is rendered as a shear.
  bool faux_italic{false};
  // Photoshop's /BaselineDirection for VERTICAL type: 1 = Roman glyphs stand upright (the
  // default Photoshop writes), 2 = they lie rotated 90 degrees ("Standard Vertical Roman
  // Alignment"). 0 = unspecified (upright). Runs v7 column 14 carries it only when rotated.
  int baseline_direction{0};
  // Positive values raise glyphs, in engine pixels. Runs v8 column 15.
  double baseline_shift{0.0};
  // Fixed leading in engine units (document pixels through the TySh transform). Unset when the
  // run uses Photoshop auto leading (auto_leading), which is paragraph AutoLeading fraction x size.
  std::optional<double> leading;
  bool auto_leading{false};
  // Photoshop tracking: 1/1000 em added after every inter-glyph gap.
  double tracking{0.0};
  // Character-panel glyph scales: width x horizontal_scale, height x vertical_scale. Leading
  // stays FontSize-based (COM-calibrated: VerticalScale does not change auto leading).
  double horizontal_scale{1.0};
  double vertical_scale{1.0};
  // Character-panel styles (runs v9 column 16, GitHub issue 346). Photoshop /FontCaps: 0 normal,
  // 1 Small Caps, 2 All Caps; /FontBaseline: 0 normal, 1 superscript, 2 subscript. The text
  // itself keeps its case and size: these only change how it is drawn.
  int font_caps{0};
  int font_baseline{0};
  bool underline{false};
  bool strikethrough{false};
  // The OpenType Ordinals (ordn) and Fractions (frac) features. Photoshop keeps these in its
  // Txt2 block only (style keys /24 and /23); the TySh EngineData has no key for them.
  bool ordinals{false};
  bool fractions{false};
  // The other OpenType toggles of Photoshop's Character panel (GitHub issue 346). Standard
  // Ligatures (liga) is on by default and Contextual Alternates (calt) off, as in Photoshop.
  // Photoshop's TySh has /Ligatures and /DLigatures; the rest are Txt2-only style keys (/20
  // Contextual Alternates, /25 Swash, /26 Titling, /28 Stylistic Alternates).
  bool ligatures{true};
  bool contextual_alternates{false};
  bool discretionary_ligatures{false};
  bool swash{false};
  bool stylistic_alternates{false};
  bool titling_alternates{false};
  // Photoshop's hyphenation and spelling language, as its Txt2 style key /38 numbers it (0 English
  // USA, 14 English UK, 42 German, 39 Arabic: docs/txt2.md, "Languages"). Patchy only keeps it so
  // a Photoshop file's text keeps its tag through an edit (GitHub issue 682). Unset writes
  // kPhotoshopTextDefaultLanguage, as every Patchy save did before; a file's 14 reads back unset.
  // Runs v9 column 16 token `language:<n>`.
  std::optional<int> language;
  // Photoshop's Middle Eastern character settings (GitHub issue 683), kept so a Photoshop file's
  // text keeps them through an edit and a save; Patchy draws nothing from them. Txt2 style keys
  // (docs/txt2.md): /70 the digit set (0 default, 1 Arabic, 2 Hindi, 3 Farsi), /72 and /73 the
  // diacritic x and y offsets, /87 the mark distance from the baseline (100 by default) and /75
  // Justification Alternates. Runs v9 column 16 tokens `digits:<n>`, `diacx:<v>`, `diacy:<v>`,
  // `marky:<v>` and `justalt`, each written only off its default.
  int digit_set{0};
  double diacritic_x_offset{0.0};
  double diacritic_y_offset{0.0};
  double mark_y_distance{100.0};
  bool justification_alternates{false};
};

// True when a run carries a Middle Eastern setting off its default (the fields above).
[[nodiscard]] bool text_run_has_middle_eastern_settings(const PsdTextStyleRun& run) noexcept;

// The Txt2 language Patchy writes for a run without one (English UK, the authoring template's).
inline constexpr int kPhotoshopTextDefaultLanguage = 14;
// Txt2 /38 languages are 0 to 63 in Photoshop 2026; anything else is not a language index.
[[nodiscard]] constexpr bool valid_photoshop_text_language(int language) noexcept {
  return language >= 0 && language < 256;
}
// The TySh /Language for a Txt2 language: an older, shorter table (German 42 is TySh 4, Arabic
// 39 is 37); languages it lacks (Chinese, Japanese, Korean, Thai, Hindi, ...) are 0, as Photoshop
// 2026 writes them. Pinned by COM captures (docs/txt2.md).
[[nodiscard]] int photoshop_tysh_language_from_txt2(int language) noexcept;
// The reverse, for a TySh read without a Txt2 object. TySh 37 (Arabic and Farsi) reads as Arabic.
[[nodiscard]] int photoshop_txt2_language_from_tysh(int language) noexcept;

// Photoshop's document-level ratios (TySh /SuperscriptSize .583, /SuperscriptPosition .333,
// /SmallCapSize .7, the same values in every capture): a superscript or subscript run draws at
// 58.3% of its size with its baseline moved 33.3% of the size, and Small Caps without the font's
// own small capitals draws the lowercase as capitals at 70%.
inline constexpr double kTextScriptSizeFraction = 0.583;
inline constexpr double kTextScriptPositionFraction = 0.333;
inline constexpr double kTextSmallCapSizeFraction = 0.7;

// True when a run carries any of the character-panel styles above (runs v9).
[[nodiscard]] bool text_run_has_character_styles(const PsdTextStyleRun& run) noexcept;
// Runs v9 column 16: the run's character styles as a comma-separated token list (`allcaps`,
// `smallcaps`, `superscript`, `subscript`, `underline`, `strikethrough`, `ordinals`,
// `fractions`, `noligatures`, `contextualalternates`, `discretionaryligatures`, `swash`,
// `stylisticalternates`, `titlingalternates`), or "-" for none. The tokens are a file-format
// contract: never rename one.
[[nodiscard]] std::string serialize_text_run_character_styles(const PsdTextStyleRun& run);
// Reads a column-16 token list into `run` (unknown tokens are ignored, so a newer writer's
// additions survive in nothing worse than being dropped).
void parse_text_run_character_styles(std::string_view field, PsdTextStyleRun& run);

struct PsdTextParagraphRun {
  int start{0};
  int length{0};
  // EngineData /Justification: 0 left, 1 right, 2 center, then justified with the last line
  // 3 left, 4 right, 5 centered, 6 justified too (justify all). Paragraph-runs names: left,
  // right, center, justify, justifyRight, justifyCenter, justifyAll (persisted; never rename).
  int justification{0};
  double first_line_indent{0.0};
  double start_indent{0.0};
  double end_indent{0.0};
  double space_before{0.0};
  double space_after{0.0};
  // Auto-leading fraction (Photoshop default 1.2): auto leading = fraction x font size.
  double auto_leading_fraction{1.2};
  // Paragraph base direction: "ltr", "rtl", or empty for auto (the first strong character
  // decides, the Unicode bidi default). Photoshop has no auto: every paragraph is left to right
  // unless its Txt2 paragraph sheet says /33 1, and /Justification is relative to that
  // direction (GitHub issue 683). The reader keeps auto only where auto lays the paragraph out
  // the same way; the writer resolves auto through first_strong_text_direction.
  std::string direction;
  // Photoshop's paragraph composer, Txt2 paragraph key /35 (never in the TySh): 0 Latin and East
  // Asian, 1 World-Ready (Photoshop 2026's default, and what it uses for a file without a Txt2).
  // World-Ready aligns a paragraph's last line as if its last glyph kerned with a following
  // space (docs/text-render-calibration.md, "Tracking and alignment"). Paragraph runs v5 column 10.
  int composer{1};
  // Photoshop's Hyphenate checkbox and its settings (TySh /AutoHyphenate, /HyphenatedWordSize,
  // /PreHyphen, /PostHyphen, /ConsecutiveHyphens, /Zone; Txt2 paragraph keys /9 to /15), and the
  // Kashida width of the Middle Eastern composer (Txt2 /40: 0 none, 1 short, 2 medium, 3 long,
  // 4 stylistic). Patchy neither hyphenates nor inserts kashidas: these are kept so a Photoshop
  // file's paragraphs keep them through an edit and a save (GitHub issue 683). The defaults are
  // what Photoshop 2026 writes for a new layer (Hyphenate off). Paragraph runs v6 column 11
  // (serialize_paragraph_run_settings).
  bool auto_hyphenate{false};
  int hyphenated_word_size{6};
  int pre_hyphen{2};
  int post_hyphen{2};
  int consecutive_hyphens{8};
  double hyphenation_zone{36.0};
  bool hyphenate_capitalized{true};
  int kashida_width{2};
  // Photoshop's line composer (Paragraph panel menu): true is Adobe Every-line Composer, false
  // Adobe Single-line Composer. TySh /EveryLineComposer, Txt2 paragraph key /29. Patchy breaks
  // lines one at a time and draws nothing from it: it is kept so a Photoshop paragraph that has
  // the Every-line composer keeps it through an edit and a save. A new layer has it off, as
  // Photoshop 2026 writes one. Paragraph runs v6 column 11 token `everyline`.
  bool every_line_composer{false};
};

// Paragraph runs v6 column 11: the paragraph's kept Photoshop settings as a comma-separated token
// list, or "-" when all are at their defaults: `hyphenate` (Hyphenate on),
// `hyphenation:<word>:<before>:<after>:<limit>:<zone>:<capitalized 0|1>` (the hyphenation
// settings, when any differs from 6:2:2:8:36:1), `kashida:<n>` (Kashida width, when not 2) and
// `everyline` (Adobe Every-line Composer). The tokens are a file-format contract: never rename one.
[[nodiscard]] std::string serialize_paragraph_run_settings(const PsdTextParagraphRun& run);
// Reads a column-11 token list into `run` (unknown tokens are ignored).
void parse_paragraph_run_settings(std::string_view field, PsdTextParagraphRun& run);
// Copies the settings column 11 carries from one paragraph run to another.
void copy_paragraph_run_settings(const PsdTextParagraphRun& from, PsdTextParagraphRun& to) noexcept;

// Vertical (tategaki) text rasters are the cell union plus this bleed on every side, in
// document pixels, derived from the layer's base size only so the PSD writer (Qt-free) and the
// UI renderer agree on where the Photoshop anchor sits inside the raster. Kept as a fraction of
// the em so accents and the ascent overhang of fonts whose ascent + descent exceeds the em
// (Arial: 1.117 em) stay inside the raster.
[[nodiscard]] inline double vertical_text_bleed_for_size(double size) {
  return std::ceil(0.25 * std::max(1.0, size));
}

// The Unicode bidi default of a stretch of UTF-16 text (rule P2): the direction of its first
// strong character, "rtl" for Hebrew, Arabic, Syriac, Thaana, N'Ko and the other right-to-left
// scripts (and RLM/ALM), "ltr" for any other letter, empty when it has none. A table of strong
// ranges, not the full bidi database: digits, punctuation, symbols and marks are skipped.
std::string first_strong_text_direction(std::span<const std::uint16_t> utf16);

// A text layer's style runs and paragraph runs as its metadata describes them (patchy.text.runs
// and patchy.text.paragraph_runs, with the layer's single-value font keys as the fallback run),
// in text order: what the PSD writer itself reads. Empty for a layer that is not text; the style
// runs are also empty for text with no characters.
std::vector<PsdTextStyleRun> text_style_runs_from_layer(const Layer& layer);
std::vector<PsdTextParagraphRun> text_paragraph_runs_from_layer(const Layer& layer);

std::string serialize_patchy_text_runs(std::span<const PsdTextStyleRun> runs);
std::string serialize_patchy_paragraph_runs(std::span<const PsdTextParagraphRun> runs);
std::string html_from_text_runs(std::string_view text, std::span<const PsdTextStyleRun> runs,
                                std::span<const PsdTextParagraphRun> paragraph_runs = {});

// Display family + style flags derived from a PostScript font name by suffix
// stripping and camel-case humanizing ("ArialNarrow" -> "Arial Narrow",
// "TimesNewRomanPS-BoldMT" -> "Times New Roman" bold). The PSD reader's
// fallback resolver; the .af importer uses it for non-normal width classes
// whose wire family alone loses the face.
struct ResolvedPhotoshopFont {
  std::string family{"Arial"};
  // The face name the platform reported ("Italic", "Demi", "Black"), empty for Regular or when
  // only the flags are known. See PsdTextStyleRun::style.
  std::string style;
  bool bold{false};
  bool italic{false};
};

ResolvedPhotoshopFont heuristic_resolved_photoshop_font(std::string_view font_name);

// The whole PostScript name humanized with NO suffix stripping ("Arial-Black" ->
// "Arial Black", "LubalinGraphITCbyBT-Demi" -> "Lubalin Graph ITC by BT Demi"),
// for matching against a font database that knows the face under a display name.
std::string humanized_postscript_font_name(std::string_view font_name);

// Off Windows the PSD reader consults this hook BEFORE falling back to
// heuristic_resolved_photoshop_font, so a PostScript name resolves to the real
// installed face ("Arial-Black" -> Arial Black) instead of flattening to family
// + bold, ~20% narrower even on a machine that has the face. The Qt app installs
// a font-database resolver at startup (install_font_database_psd_font_resolver,
// src/ui/psd_font_resolver.hpp); the hook returns std::nullopt for names the
// database cannot vouch for. On Windows the reader keeps its DirectWrite ->
// registry -> heuristic chain and never consults the hook: those answers are
// pinned by the imported-text tests. The resolver may be called from whatever
// thread reads the document, so it must be thread-safe.
using PhotoshopFontResolver = std::optional<ResolvedPhotoshopFont> (*)(std::string_view font_name);
void set_photoshop_font_resolver(PhotoshopFontResolver resolver);

// The installed face the platform knows under `name` as a full name ("Futura Extra Black
// BT"), a PostScript name ("FuturaBT-ExtraBlack"), or DirectWrite's family or family + face,
// described the way the reader stores it: the GDI family and subfamily Qt's font database
// lists, plus the flags the face implies. The text engine's last resort for a display name
// the database cannot find as a family or as family + face. Windows only (DirectWrite);
// std::nullopt elsewhere and for names no installed font carries. Each call scans the system
// font collection, so callers cache per name.
std::optional<ResolvedPhotoshopFont> installed_font_for_name(std::string_view name);

// Every family name the face installed_font_for_name finds goes by, in every language its name
// table carries (DirectWrite's family, the GDI family and the typographic family). Qt's Windows
// database lists a face under its name for the user's locale (DingMao.ttf is a Chinese family
// on a Chinese Windows, "DinkieBitmap" elsewhere), so the text engine tries each of these
// against the database when the reader's en-us name is not listed (GitHub issue 413). Windows
// only; empty elsewhere and for names no installed font carries.
std::vector<std::string> installed_font_family_names(std::string_view name);

// The reader's font-name comparison: ASCII case and punctuation are ignored, every non-ASCII
// character counts, and a name with nothing left to compare matches nothing. Two all-CJK
// family names once both compacted to the empty key, so any one of them stood in for the
// others (GitHub issue 413).
bool photoshop_font_names_match(std::string_view lhs, std::string_view rhs);

}  // namespace patchy::psd
