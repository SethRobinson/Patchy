#pragma once

#include "core/document.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// GIMP's native .xcf documents, read-only (docs/xcf.md). Written from the published XCF
// specification (developer.gimp.org/core/standards/xcf/) and from files GIMP writes,
// inspected as data; never from GIMP's or another XCF reader's source. Qt-free: tiles
// inflate through the vendored miniz, and gzip, bzip2 and xz wrapped files
// (.xcf.gz/.xcfgz, .xcf.bz2/.xcfbz2, .xcf.xz/.xcfxz) unwrap by their signature.
namespace patchy::xcf {

// True when the bytes start like an XCF file ("gimp xcf "), or like a gzip, bzip2 or xz
// stream whose first decompressed bytes do.
[[nodiscard]] bool sniff(std::span<const std::uint8_t> bytes) noexcept;
// The extensions the registry routes here: ".xcf" and GIMP's single-word compressed
// forms (".xcfgz", ".xcfbz2", ".xcfxz").
[[nodiscard]] const std::vector<std::string>& xcf_extensions();
// True for a file name GIMP gives a compressed XCF: "*.xcf.gz", "*.xcf.bz2", "*.xcf.xz"
// (any case). Their last extension alone says nothing, so the open path asks this.
[[nodiscard]] bool is_compressed_xcf_file_name(std::string_view file_name) noexcept;

// The file's bytes with any gzip, bzip2 or xz wrapper removed (unchanged when there is
// none). Throws std::runtime_error when a wrapper is damaged or expands past the limits.
[[nodiscard]] std::vector<std::uint8_t> unwrap(std::span<const std::uint8_t> bytes);

// The untranslated notice a read gives when text layers were painted on in GIMP; the open
// path replaces it when it turns them into text (docs/xcf.md, "Text layers").
[[nodiscard]] const char* painted_text_layers_notice() noexcept;

// GIMP 3 layer effects (GEGL filters stored on a layer) that the reader has no code of its
// own for run through an effect runner when one is set (docs/xcf.md, "Layer effects (GIMP 3
// filters)"): the app sets one that runs Patchy's own GEGL. The reader picks the effects from
// its table (effect_runner_operations) and passes each stored argument by name with the
// helper's setting kind: 'i' int, 'u' unsigned (seeds), 'd' number, 'b' boolean (0 or 1),
// 'e' enum value, 'c' color ("r g b a", display-encoded sRGB, 0..1).
struct EffectSetting {
  std::string name;
  char kind{'d'};
  std::string value;
};
struct EffectRun {
  std::string operation;  // "gegl:bloom"
  std::vector<EffectSetting> settings;
};
// Runs the operation on `pixels` (RGBA at the document's depth: display-encoded 8 and 16
// bits, linear 32-bit floats) in place. Returns false, leaving the pixels alone, when it
// cannot. Called from the thread that reads the file.
using EffectRunner = std::function<bool(const EffectRun&, PixelBuffer& pixels)>;
// Sets (or, with an empty function, clears) the process-wide runner.
void set_effect_runner(EffectRunner runner);
[[nodiscard]] bool has_effect_runner();
// The GIMP effect operations the reader hands to the runner.
[[nodiscard]] std::vector<std::string> effect_runner_operations();
// The untranslated notice naming the GIMP layer effects a read left out: the
// prefix, then the names joined by ", ", then the suffix (the UI translates it whole).
[[nodiscard]] const char* skipped_effects_notice_prefix() noexcept;
[[nodiscard]] const char* skipped_effects_notice_suffix() noexcept;

class DocumentIo {
public:
  // The layer tree (groups, pass-through, masks, offsets, opacity, visibility, locks,
  // color tags, blend modes), every precision and color mode, text layers as editable
  // Patchy text over GIMP's own raster, channels, the selection, paths, guides, the
  // resolution and unit, the ICC profile, comment and Exif/XMP metadata. Notices name
  // what was approximated or dropped. Throws std::runtime_error with a plain message
  // for files that are not XCF or are damaged past reading.
  [[nodiscard]] static Document read(std::span<const std::uint8_t> bytes,
                                     std::vector<std::string>* notices = nullptr);
  [[nodiscard]] static Document read_file(const std::filesystem::path& path,
                                          std::vector<std::string>* notices = nullptr);
};

namespace detail {

// One GIMP text-layer parasite ("gimp-text-layer") parsed from its serialized form:
// a list of (key value...) forms. Exposed for tests.
struct TextParasite {
  std::string text;
  std::string markup;
  std::string font;            // the font description or GIMP 3's full name
  std::string font_family;     // GIMP 3's family, when present
  std::string font_style;      // GIMP 3's style ("Bold Italic"), when present
  std::string font_psname;     // GIMP 3's PostScript name, when present
  double font_size{0.0};
  std::string font_size_unit{"pixels"};
  bool antialias{true};
  std::string justify{"left"};  // left, right, center, fill
  double indent{0.0};
  double line_spacing{0.0};
  double letter_spacing{0.0};
  std::string box_mode{"dynamic"};  // dynamic or fixed
  double box_width{0.0};
  double box_height{0.0};
  std::string base_direction{"ltr"};
  bool have_color{false};
  double color[4]{0.0, 0.0, 0.0, 1.0};  // encoded sRGB, 0..1
  // GIMP 3's outline: "none", "stroke-only" or "stroke-fill", drawn outer, inner or centered
  // on the glyph edges with `outline_width` in `outline_unit`.
  std::string outline{"none"};
  std::string outline_direction{"outer"};
  double outline_width{0.0};
  std::string outline_unit{"pixels"};
  std::string outline_custom_style{"solid-color"};  // or "pattern"
  bool outline_dashed{false};
  bool have_outline_color{false};
  double outline_color[4]{0.0, 0.0, 0.0, 1.0};  // encoded sRGB, 0..1
  // GIMP 3's (markupfont "fontNNN" (font ...)) forms: markup spans name a font by that id.
  struct MarkupFont {
    std::string id;
    std::string full_name;
    std::string family;
    std::string style;
  };
  std::vector<MarkupFont> markup_fonts;
};
[[nodiscard]] TextParasite parse_text_parasite(std::string_view text);

// Tests: 1 prints the reader's trace (what PATCHY_XCF_TRACE turns on) to stderr, 0 silences
// it, -1 goes back to the environment variable.
void set_trace_override(int enabled) noexcept;

}  // namespace detail

}  // namespace patchy::xcf
