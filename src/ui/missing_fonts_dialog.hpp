#pragma once

#include "core/font_substitutes.hpp"

#include <QHash>
#include <QString>

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
// face Qt falls back to (no line).
enum class MissingFontSuggestion { fallback, metric, lookalike, same_type };

struct MissingFontEntry {
  QString family;          // as the text layers name it
  bool no_glyphs{false};   // installed, but draws none of the text it is asked for
  int layer_count{0};      // text layers that use it
  QString suggested;       // the installed family the picker starts on
  MissingFontSuggestion suggestion{MissingFontSuggestion::fallback};
  FontClass font_type{FontClass::unknown};  // the missing font's type, for same_type
};

struct MissingFontsDialogResult {
  // text_font_replacement_key(family) -> the installed family chosen for it. Rows left on
  // "Don't Resolve" are absent.
  QHash<QString, QString> replacements;
  // The "Don't show when opening documents" box, inverted (imports/showMissingFontsDialog).
  bool show_on_open{true};
};

// nullopt when cancelled. `show_on_open` seeds the "Don't show" box; a cancel still reports it
// through `show_on_open_out`, since that box is about the dialog itself, not the fonts.
[[nodiscard]] std::optional<MissingFontsDialogResult> run_missing_fonts_dialog(
    QWidget* parent, const std::vector<MissingFontEntry>& fonts, bool show_on_open, bool* show_on_open_out = nullptr);

}  // namespace patchy::ui
