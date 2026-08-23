#pragma once

#include <QString>
#include <QStringList>

#include <vector>

namespace patchy::ui {

// The Windows UI-font choice, split out of main.cpp so the rule below is testable.
//
// THE RULE: never register a font FILE for a family Windows already installs. An
// application font's engine carries no file name in its QFontEngine::FaceId, and
// QPdfEngine embeds a font only when it has one; for an application font it silently
// falls back to drawing every glyph as a filled path, so an editable PDF export loses all
// its text for that family (it re-imports as shape layers). Patchy registered
// C:/Windows/Fonts/{arial,segoeui,calibri}*.ttf unconditionally until August 2026, which
// is exactly what happened to Segoe UI text. See docs/fonts.md.
struct UiFontCandidate {
  QString family;
  QStringList files;  // the files that provide the family where Windows lacks it
};

// Preference order: Arial, Segoe UI, Calibri.
[[nodiscard]] std::vector<UiFontCandidate> windows_ui_font_candidates();

// The first candidate family present in `installed_families`, or an empty string when
// none is installed.
[[nodiscard]] QString installed_ui_font_family(const QStringList& installed_families);

// The files to register as application fonts: empty whenever a candidate family is
// already installed (the normal case), and only the fallback family's files when none
// is, where an outlined PDF export beats having no UI font at all.
[[nodiscard]] QStringList ui_font_files_to_register(const QStringList& installed_families);

}  // namespace patchy::ui
