#pragma once

// Photoshop's document-level text engine block ('Txt2'): one text object per type layer,
// addressed by the layer's TySh TextIndex, which Photoshop trusts over the TySh itself.
// Patchy parses an existing block (or starts from a Photoshop 2026 template), authors an
// object for every regenerated type layer from the same runs the TySh gets, keeps untouched
// objects, and writes the block back with every cached layout tree emptied (Photoshop
// recomposes from the model; a stale cache is what corrupts). Format and key map: docs/txt2.md.

#include "psd/engine_data.hpp"
#include "psd/psd_text_runs.hpp"
#include "core/vector_shape.hpp"
#include "core/document.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace patchy::psd {

// On platforms without DirectWrite the GUI supplies each authored run's PostScript face name
// (from the installed face's OpenType name table) for point, box and area text alike, so
// Photoshop finds the font (GitHub issue 646: Linux wrote "Roboto Mono [GOOG]"). It returns
// std::nullopt when the family is not installed; the run then keeps
// family_name_for_photoshop_fallback(family).
using FontFaceNameResolver = std::optional<std::string> (*)(std::string_view family,
    std::string_view style, bool bold, bool italic);
void set_font_face_name_resolver(FontFaceNameResolver resolver);

// The name a run is written under when no platform lookup names its face: the family without
// the " [Foundry]" suffix Qt adds when two foundries share a family name ("Roboto Mono [GOOG]"
// -> "Roboto Mono"), "Arial" when empty.
[[nodiscard]] std::string family_name_for_photoshop_fallback(std::string_view family);

// What one type layer's text object is authored from (document pixels, the TySh engine units).
struct TextEngineInputs {
  // Photoshop engine text: '\r' paragraph separators and a trailing '\r'.
  std::string text;
  std::vector<PsdTextStyleRun> runs;
  std::vector<PsdTextParagraphRun> paragraph_runs;
  // Photoshop PostScript names per run (font_index_for_run resolution), parallel to `runs`.
  std::vector<std::string> run_font_names;
  bool boxed{false};
  double box_width{0.0};
  double box_height{0.0};
  bool vertical{false};
  std::optional<VectorPath> area;
  // The layer is laid out by Photoshop's rules (kLayerMetadataTextLayoutMode): its box frame
  // then keeps Photoshop's own first-baseline rule (the ascent) instead of the cap-height rule a
  // Qt-natural Patchy box is written with (author_frame).
  bool photoshop_layout{false};
};

// The FontSize one paragraph run of a text object hands to style runs that leave FontSize unset
// (engine pixels): the paragraph's own character style, else its parent paragraph sheet's
// ("Basic Paragraph" /32). Photoshop ranks it above the normal style sheet (GitHub issue 345).
struct TextParagraphFontSize {
  int length{0};
  std::optional<double> size;
};

// The OpenType features one style run of a text object turns on (style keys /24 Ordinals, /23
// Fractions, /20 Contextual Alternates, /25 Swash, /26 Titling, /28 Stylistic Alternates).
// Photoshop writes them only here, never into the TySh (GitHub issue 346). `language` is the
// run's /38 (its sheet, else its parent StyleSheetSet entry, else the document's default sheet),
// unset for kPhotoshopTextDefaultLanguage (GitHub issue 682). The Middle Eastern settings
// (GitHub issue 683) resolve the same way and are set only when a sheet names them: /70 the
// digit set, /72 and /73 the diacritic offsets, /75 Justification Alternates, and /87 the mark
// distance from the baseline (100, the default, when /88 says it is automatic).
struct TextStyleRunFeatures {
  int length{0};
  bool ordinals{false};
  bool fractions{false};
  bool contextual_alternates{false};
  bool swash{false};
  bool titling_alternates{false};
  bool stylistic_alternates{false};
  std::optional<int> language;
  std::optional<int> digit_set;
  std::optional<double> diacritic_x_offset;
  std::optional<double> diacritic_y_offset;
  std::optional<double> mark_y_distance;
  std::optional<bool> justification_alternates;
};

// The kerning one style run of a text object resolves to (style keys /11: 0 manual, 1 Metrics,
// 2 Optical, and /10 the manual number in 1/1000 em): the run's own sheet, else the character
// style of the paragraph its first character sits in (the paragraph run's own /32, then its
// parent ParagraphSheetSet entry's: "Basic Paragraph" says manual 0 in files Photoshop CC wrote),
// else the run's parent StyleSheetSet entry, else the document's default style sheet (/1/2).
// Photoshop reads the kerning here and not from the TySh (docs/txt2.md, "Kerning").
struct TextStyleRunKerning {
  int length{0};
  std::optional<int> mode;
  std::optional<double> kerning;
};

// The composer (paragraph key /35) and direction (/33: 0 left to right, 1 right to left) one
// paragraph run of a text object resolves to: its own sheet, else its parent ParagraphSheetSet
// entry, else the document's default paragraph sheet (/1/3). Hyphenate (/9), the Kashida
// width (/40) and the line composer (/29: true Every-line) resolve the same way; the hyphenation settings (/10 word size, /11 and /12 letters
// before and after, /13 limit, /14 zone, /15 capitalized words) come from the run's own sheet
// only, because Photoshop writes the effective ones into the TySh run and leaves its default
// sheets at other values (/13 0 beside a TySh /ConsecutiveHyphens 8).
struct TextParagraphComposer {
  int length{0};
  std::optional<int> composer;
  std::optional<int> direction;
  std::optional<bool> auto_hyphenate;
  std::optional<int> kashida_width;
  std::optional<int> hyphenated_word_size;
  std::optional<int> pre_hyphen;
  std::optional<int> post_hyphen;
  std::optional<int> consecutive_hyphens;
  std::optional<double> hyphenation_zone;
  std::optional<bool> hyphenate_capitalized;
  std::optional<bool> every_line_composer;
};

struct TextFrameGeometry {
  enum class Kind { Point, Box, Area, Unsupported };
  Kind kind{Kind::Unsupported};
  std::optional<VectorPath> area;
  // One entry per paragraph run of the object; empty when no paragraph supplies a size.
  std::vector<TextParagraphFontSize> paragraph_font_sizes{};
  // One entry per style run of the object; empty when no run turns a feature on.
  std::vector<TextStyleRunFeatures> style_features{};
  // One entry per paragraph run of the object; empty when no sheet names any of its settings.
  std::vector<TextParagraphComposer> paragraph_composers{};
  // One entry per style run of the object; empty when no sheet names a kerning mode.
  std::vector<TextStyleRunKerning> style_kerning{};
};

// Unaddressed TySh index for regenerated text beside a verbatim legacy engine,
// beyond the contiguous native object indices (the 999 probe already used TySh).
inline constexpr std::int32_t kRegeneratedTextIndexBase = 100000;

// Geometry directory plus each object's paragraph font sizes. Recognizes the older named-key
// point/box frames too, without changing the writer's verbatim preservation of that engine format.
std::vector<TextFrameGeometry> read_text_frame_geometries(std::span<const std::uint8_t> payload);

// True for the text engine block of Photoshop CS-era files: named keys (`DocumentResources`,
// `DocumentObjects/TextObjects`) instead of the numbered ones Patchy parses and authors. That
// engine has one paragraph composer, the Latin one: Photoshop 2026 lays every type layer of
// such a document out without the World-Ready paragraph-end kern, its old-text layers included
// (docs/txt2.md, "The old named-key engine").
[[nodiscard]] bool text_engine_block_is_named_key_engine(std::span<const std::uint8_t> payload);

// Called after layers and document-level blocks have both been decoded.
void import_text_frame_geometry(Document& document);

// The Photoshop 2026 resources and document settings Patchy authors into (no objects, no
// frames, FontSet = AdobeInvisFont + MyriadPro-Regular). Generated: psd_text_engine_template.cpp.
std::span<const std::uint8_t> text_engine_template_bytes();

class TextEngineBlock {
 public:
  static std::optional<TextEngineBlock> parse(std::span<const std::uint8_t> payload);
  static TextEngineBlock from_template();

  [[nodiscard]] std::vector<std::uint8_t> serialize() const;
  [[nodiscard]] std::size_t object_count() const;
  // The object's engine text (with its '\r's), for tests and diagnostics.
  [[nodiscard]] std::optional<std::string> object_text(std::size_t index) const;
  // The frame index an object's view references, or nullopt.
  [[nodiscard]] std::optional<std::size_t> object_frame_index(std::size_t index) const;
  [[nodiscard]] TextFrameGeometry object_geometry(std::size_t index) const;
  [[nodiscard]] std::vector<TextParagraphFontSize> object_paragraph_font_sizes(std::size_t index) const;
  [[nodiscard]] std::vector<TextStyleRunFeatures> object_style_features(std::size_t index) const;
  [[nodiscard]] std::vector<TextParagraphComposer> object_paragraph_composers(std::size_t index) const;
  [[nodiscard]] std::vector<TextStyleRunKerning> object_style_kerning(std::size_t index) const;

  // Authors a text object from the inputs and stores it at `index` (replacing the object and
  // its frame there, or appending when `index` is past the end); fonts join the FontSet as
  // needed. Returns the index the object landed at.
  std::size_t set_object(std::size_t index, const TextEngineInputs& inputs);
  // Appends an authored object; returns its index.
  std::size_t append_object(const TextEngineInputs& inputs);
  // Empties every object's cached layout tree (/1/2 becomes [ ]) so Photoshop recomposes from
  // the model.
  void strip_layout_caches();

  [[nodiscard]] const EngineNode& root() const noexcept { return root_; }
  [[nodiscard]] EngineNode& root() noexcept { return root_; }

 private:
  explicit TextEngineBlock(EngineNode root) : root_(std::move(root)) {}
  [[nodiscard]] EngineNode* objects();
  [[nodiscard]] const EngineNode* objects() const;
  [[nodiscard]] EngineNode* frames();
  int font_index(const std::string& postscript_name);
  [[nodiscard]] EngineNode author_object(const TextEngineInputs& inputs, std::size_t frame_index);
  [[nodiscard]] EngineNode author_frame(const TextEngineInputs& inputs) const;

  EngineNode root_;
};

}  // namespace patchy::psd
