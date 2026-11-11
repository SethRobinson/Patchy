#pragma once

#include <QString>

#include <vector>

namespace patchy::ui {

// The replacements chosen in Type > Resolve Missing Fonts, remembered across documents and
// sessions (GitHub issue 692). docs/font-resolution-missing-fonts.md, "Remembered replacements".
//
// Persisted settings key (never rename) holding compact JSON, whose member names are part of the
// contract too:
//   [{"font": "<text_font_replacement_key of the missing font>",
//     "picks": [{"family": "<family chosen>", "count": <times chosen>}, ...]}, ...]
// Both arrays are ordered most recently chosen first, so a font's first pick is the one chosen
// last, and the caps below drop the entries that were chosen longest ago.
inline constexpr char kMissingFontReplacementsKey[] = "text/missingFontReplacements";
inline constexpr int kMaxRememberedReplacementFonts = 100;
inline constexpr int kMaxRememberedReplacementsPerFont = 6;

struct RememberedFontReplacement {
  QString family;  // as it was chosen; it may no longer be installed
  int count{0};    // how many times it was chosen for this missing font
};

// The families chosen for `missing_family` so far, most recently chosen first. Empty when the
// font was never resolved by hand.
[[nodiscard]] std::vector<RememberedFontReplacement> remembered_font_replacements(const QString& missing_family);
// The same families, most often chosen first; equal counts keep the most recent first.
[[nodiscard]] std::vector<RememberedFontReplacement> remembered_font_replacements_by_count(
    const QString& missing_family);
// Counts one more choice of `chosen_family` for `missing_family` and makes it the last pick.
void record_font_replacement(const QString& missing_family, const QString& chosen_family);
// Forgets `chosen_family` (ASCII case ignored) for `missing_family`: the "Chosen before" row's
// remove button. A font left with no pick leaves the list.
void forget_font_replacement(const QString& missing_family, const QString& chosen_family);
// Forgets everything (tests).
void clear_remembered_font_replacements();

}  // namespace patchy::ui
