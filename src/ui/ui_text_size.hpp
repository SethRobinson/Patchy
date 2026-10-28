#pragma once

// The interface text size (GitHub issue 495; docs/ui-text-size.md): one process-wide
// percent, read from Preferences once at startup, that sizes the interface's text and what
// holds text, separately from the interface scale (QT_SCALE_FACTOR, which scales everything).
//
// At 100 (the default) every function here returns its input unchanged, so the default
// interface is the one Patchy always drew.

#include <QFont>
#include <QString>

class QLabel;

namespace patchy::ui {

// The text size in effect, in percent. 100 until set_ui_text_size_percent runs.
[[nodiscard]] int ui_text_size_percent();
[[nodiscard]] double ui_text_size_factor();

// Sets the text size for this process (clamped to the Preferences range). main.cpp calls
// it before the first stylesheet is built; tests call it around a sweep. Stylesheets built
// earlier keep their sizes until rebuilt (photoshop_style() is keyed on it).
void set_ui_text_size_percent(int percent);

// A font size at the current text size: a QSS or painted font pixel or point size.
// Unchanged at 100%.
[[nodiscard]] int ui_text_px(int px);
[[nodiscard]] double ui_text_pt(double points);

// A width or height that has to hold text (a field, a text row, a column of labels): it
// grows with a larger text size and keeps its default length at a smaller one, since the
// padding and arrows beside the text do not shrink. Unchanged at 100%.
[[nodiscard]] int ui_text_length(int px);

// A copy of `font` scaled by the text size, as a whole pixel size rounded like the sheets'
// font sizes (a point size is taken at its pixel size first). Needs a QGuiApplication.
[[nodiscard]] QFont ui_text_scaled_font(QFont font);

// Resolves the text-size forms in a stylesheet (apply_theme_tokens runs it on every
// themed sheet):
//  - `font-size: <n>px` becomes ui_text_px(n) px, unless the declaration is followed by
//    the comment /* scaled */ (a size already taken from a live, scaled font);
//  - `@tx(<n>)` becomes ui_text_length(n), for heights and widths of text rows written in
//    the sheet.
[[nodiscard]] QString resolve_ui_text_sizes(const QString& qss);

// At a non-default text size, keeps a word-wrapped label's minimum height at the height its
// text needs at its current width, so a panel too short for it scrolls instead of cutting the
// text off. Does nothing at 100%.
void keep_wrapped_label_height(QLabel& label);

}  // namespace patchy::ui
