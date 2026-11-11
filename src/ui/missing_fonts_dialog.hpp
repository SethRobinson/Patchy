#pragma once

#include "core/font_substitutes.hpp"

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

class QWidget;

namespace patchy::ui {

// Type > Resolve Missing Fonts (GitHub issue 169), Photoshop's Missing Fonts dialog: one row per
// family the document's text layers name but cannot draw, each with an installed replacement.
// MainWindow collects the rows and applies the answer (resolve_missing_fonts); this file owns
// only the dialog.

// Where `suggested` comes from (issue 463): a pair from the built-in substitute table or an
// installed default of the same font type (each with a "Suggested:" line under the name), or the
// face Qt falls back to (no line). no_symbol_match: a symbol font (Wingdings) with no installed
// font of the same codes; the row starts unticked with a line saying so, since a text font would
// turn its pictures into letters.
enum class MissingFontSuggestion { fallback, metric, lookalike, same_type, no_symbol_match };

// One installed stand-in the built-in rules offer for a missing font (a table pair or the
// same-type default), listed with its reason in the "Suggested" section of the row's picker
// (GitHub issue 692).
struct MissingFontCandidate {
  QString family;
  MissingFontSuggestion kind{MissingFontSuggestion::lookalike};  // metric, lookalike or same_type
  FontClass font_type{FontClass::unknown};                       // the missing font's type, for same_type
};

struct MissingFontEntry {
  QString family;          // as the text layers name it
  bool no_glyphs{false};   // installed, but draws none of the text it is asked for
  int layer_count{0};      // text layers that use it
  QString suggested;       // the installed family the picker starts on
  // Where the built-in recommendation comes from (`recommended`), whatever the picker starts on.
  MissingFontSuggestion suggestion{MissingFontSuggestion::fallback};
  FontClass font_type{FontClass::unknown};  // the missing font's type, for same_type
  // Remembered replacements (GitHub issue 692, ui/font_replacement_memory.hpp). chosen_before:
  // `suggested` is the font chosen for this missing font the last time, so the row says "Chosen
  // before" in place of its "Suggested" line and starts ticked even for a symbol font.
  bool chosen_before{false};
  QString recommended{};                 // the built-in recommendation: `suggested` with nothing remembered
  QStringList chosen_before_families{};  // every remembered family still installed, most often chosen first
  std::vector<MissingFontCandidate> candidates{};  // every installed built-in candidate, best first
};

struct MissingFontsDialogResult {
  // text_font_replacement_key(family) -> the installed family chosen for it. Unticked rows are
  // absent.
  QHash<QString, QString> replacements;
  // The keys of the ticked rows whose font was picked by hand (in the picker's list, or a name
  // typed into it), even when the pick is the font the row started on (issue 692 follow-up).
  QSet<QString> picked_by_hand;
  // The "Don't show when opening documents" box, inverted (imports/showMissingFontsDialog).
  bool show_on_open{true};
};

// Each row's name label (missingFontName<N>) shows a name with a style as the family and the style
// in brackets ("Museo Sans Rounded (500)"); this property holds the whole name.
inline constexpr const char* kMissingFontFullNameProperty = "patchyMissingFontFullName";

// nullopt when cancelled. `show_on_open` seeds the "Don't show" box; a cancel still reports it
// through `show_on_open_out`, since that box is about the dialog itself, not the fonts.
[[nodiscard]] std::optional<MissingFontsDialogResult> run_missing_fonts_dialog(
    QWidget* parent, const std::vector<MissingFontEntry>& fonts, bool show_on_open, bool* show_on_open_out = nullptr);

}  // namespace patchy::ui
