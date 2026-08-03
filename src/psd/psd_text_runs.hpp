#pragma once

// Public face of the PSD text-run model: the per-run style/paragraph structs
// and the serializers that produce the patchy.text.runs / patchy.text.paragraph.runs
// metadata (persistence contracts, v1-v3) plus the Qt rich-text body for
// patchy.text.html. Split out of psd_io_internal.hpp so non-PSD importers
// (the Affinity .af reader) can emit the same editable-text metadata;
// definitions live in psd_text_read.cpp.

#include "core/layer.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>

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
};

struct PsdTextParagraphRun {
  int start{0};
  int length{0};
  int justification{0};
  double first_line_indent{0.0};
  double start_indent{0.0};
  double end_indent{0.0};
  double space_before{0.0};
  double space_after{0.0};
  // Auto-leading fraction (Photoshop default 1.2): auto leading = fraction x font size.
  double auto_leading_fraction{1.2};
};

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

}  // namespace patchy::psd
