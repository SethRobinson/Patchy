#pragma once

// Resolves @role_name color tokens in a QSS template against a ThemePalette.
//
// Every stylesheet builder in src/ui writes its colors as @role_name tokens and
// passes the template through apply_theme_tokens(). The token spelling is the
// ThemePalette member spelling; see theme_palette.hpp.
//
// An unresolved token is a silent-corruption hazard, because Qt's QSS parser
// drops the whole declaration containing it without a warning: a missing
// background rule looks like nothing at all in review and only shows up as a
// wrong-colored strip in one state of one widget. apply_theme_tokens therefore
// leaves the token text in place and complains, and
// ui_theme_qss_resolves_every_token fails the build's test run if any survives.

#include "ui/theme_palette.hpp"

#include <QString>

class QLabel;
class QWidget;

namespace patchy::ui {

// A QSS fragment that still holds @role tokens.
//
// Deliberately not a QString. Handing token text to QWidget::setStyleSheet is
// silently destructive (Qt drops every declaration containing the token, so a
// rule just stops existing), and the shared builders below are exactly the values
// most likely to travel to a call site that forgets. A distinct type turns that
// mistake into a compile error. Reach for text() only when resolving.
class ThemedQss {
 public:
  ThemedQss() = default;
  explicit ThemedQss(QString text) : text_(std::move(text)) {}

  [[nodiscard]] const QString& text() const { return text_; }

  ThemedQss& operator+=(const ThemedQss& other) {
    text_ += other.text_;
    return *this;
  }
  [[nodiscard]] friend ThemedQss operator+(ThemedQss lhs, const ThemedQss& rhs) {
    lhs += rhs;
    return lhs;
  }

 private:
  QString text_;
};

[[nodiscard]] QString apply_theme_tokens(const QString& qss, const ThemePalette& palette);
[[nodiscard]] QString apply_theme_tokens(const QString& qss);

// Widget stylesheets that must survive a live scheme change.
//
// A widget's sheet is an accumulated concatenation: install_dark_dialog_chrome
// appends the chrome rules, the dialog appends its own block, and
// dialog_spinbox_button_style() has to come last (see docs/ui-conventions.md).
// Nothing remembered that order, so a scheme change had no way to rebuild it.
// These helpers keep the unresolved @token text in a widget property and
// re-resolve it in place, which preserves append order for free.
//
// Store templates, never resolved text: appending resolved text would freeze the
// colors of everything appended before the flip.
inline constexpr char kThemedStyleTemplateProperty[] = "patchy.styleTemplate";

// Rich text can carry colors too (a QLabel with an <a style="color:..."> link),
// and QSS cannot reach inside it. Labels set through set_themed_label_text keep
// their token-bearing template alongside the style template and re-resolve with
// it, so they follow a scheme change like everything else.
inline constexpr char kThemedTextTemplateProperty[] = "patchy.textTemplate";

// Appends a token-bearing fragment and re-resolves the widget's sheet. The first
// call seeds the stored template from any stylesheet already set directly, so
// existing literal rules are preserved untouched.
void append_themed_style(QWidget& widget, const QString& qss_template);
inline void append_themed_style(QWidget& widget, const ThemedQss& qss_template) {
  append_themed_style(widget, qss_template.text());
}

// Reads back what has been appended so far, for the rare caller that has to
// rebuild its whole sheet from the accumulated template rather than add to it.
[[nodiscard]] ThemedQss themed_style_template(const QWidget& widget);

// Replaces the stored template outright. For widgets that rebuild their whole
// sheet whenever their state changes (color swatches, inline editors), where
// appending would pile up dead rules.
void set_themed_style(QWidget& widget, const QString& qss_template);
inline void set_themed_style(QWidget& widget, const ThemedQss& qss_template) {
  set_themed_style(widget, qss_template.text());
}

// Sets a QLabel's text from a token-bearing template.
void set_themed_label_text(QLabel& label, const QString& text_template);

// Re-resolves one widget's stored style and text templates against the active
// palette.
void rebuild_themed_style(QWidget& widget);

// Re-resolves `root` and every descendant carrying a stored template.
void rebuild_themed_styles_in(QWidget& root);

}  // namespace patchy::ui
