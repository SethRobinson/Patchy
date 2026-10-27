#pragma once

// Built-in font substitute table (GitHub issue 463): for a font a document names but this computer
// lacks, the fonts that stand in for it, best first. Names and project links only, never font
// data; each pair is justified in font_substitutes.cpp from the substitute's own documentation.
// Qt-free; the text font service checks which candidates are installed. See
// docs/font-resolution.md, "Substitute table".

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace patchy {

// The requested font's design class (its "font type"): the table's column, or
// classify_font_name_words for a font the table does not list.
enum class FontClass : std::uint8_t { unknown, serif, sans, mono, script, display };

// metric: the substitute's own project states the same character widths, so lines break in the
// same places. lookalike: a similar design whose widths differ, so text can reflow.
enum class FontSubstituteMatch : std::uint8_t { metric, lookalike };

struct FontSubstituteCandidate {
  std::string_view family;
  FontSubstituteMatch match;
  std::string_view url;  // official project page of a free font; empty for a commercial one
};

struct FontSubstituteEntry {
  std::string_view family;                        // the requested font, as its maker names it
  std::span<const std::string_view> aliases;      // other names for it: PostScript stems, short names
  FontClass font_class;
  std::span<const FontSubstituteCandidate> candidates;  // preference order within each match kind
};

struct FontSubstituteLookup {
  const FontSubstituteEntry* entry{nullptr};
  std::string style;  // trailing face words the lookup dropped ("Semibold"), as written; empty when none
};

struct FontSubstituteSuggestion {
  std::string family;
  FontSubstituteMatch match{FontSubstituteMatch::metric};
};

[[nodiscard]] std::span<const FontSubstituteEntry> builtin_font_substitutes() noexcept;

// The comparison key: ASCII letters folded to lower case, ASCII digits kept, every other ASCII
// character dropped, non-ASCII bytes kept (photoshop_font_names_match's rule, issue 413).
[[nodiscard]] std::string font_substitute_key(std::string_view name);

// The entry for a UTF-8 family name, by family or alias. A face-baked name ("Myriad Pro Semibold",
// "Arial-Bold") that misses is retried with its trailing face words dropped; they come back in
// `style`. nullopt when nothing matches.
[[nodiscard]] std::optional<FontSubstituteLookup> find_font_substitutes(
    std::string_view family, std::span<const FontSubstituteEntry> table = builtin_font_substitutes());

// The entry's candidates that `usable` accepts, metric matches before look-alikes and table order
// within each, without repeats and never the requested font itself.
[[nodiscard]] std::vector<FontSubstituteSuggestion> rank_font_substitutes(
    const FontSubstituteEntry& entry, const std::function<bool(std::string_view)>& usable);

// The font type a family name's own words state, for a font the table does not list: "mono",
// "code", "console", "typewriter", "courier" and "terminal" make it mono; otherwise "sans",
// "gothic", "grotesk", "grotesque" and "hei" make it sans; otherwise "serif", "mincho", "ming",
// "song", "antiqua" and "slab" make it serif. Words split at spaces, hyphens, underscores and
// lower-to-upper case changes ("NotoSerifJP"), compared ASCII case-insensitively, so "Microsoft
// Sans Serif" is sans and "Noto Sans Mono" is mono. unknown when no word says.
[[nodiscard]] FontClass classify_font_name_words(std::string_view family);

// The table's font type for `family` (by family, alias or face-baked name), else its name words.
[[nodiscard]] FontClass font_class_for_name(std::string_view family);

// Widely installed families of one font type, best first, mixing the Windows, macOS and Linux
// defaults; the caller keeps the installed ones. Empty for unknown, script and display.
[[nodiscard]] std::span<const std::string_view> same_class_default_families(FontClass font_class) noexcept;

}  // namespace patchy
