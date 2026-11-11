#pragma once

// Shaping a text run with its Photoshop language (GitHub issue 683, follow-up 4).
//
// Qt 6.8 hands HarfBuzz one language for every run: hb_language_get_default(), the process
// locale (QTextEngine::shapeTextWithHarfbuzzNG), and has no per-run setting. HarfBuzz then picks
// the font's default language system, so rules a font files under a language (Adobe Arabic 3.005
// builds the Allah ligature under 'ARA ', Romanian comma-below letters under 'ROM ', Farsi digit
// forms under 'FAR ') never apply, where Photoshop applies them for a run whose Character panel
// language is that language.
//
// Patchy gets there without a shaper of its own: for such a run it registers a copy of the face
// whose GSUB and GPOS make that language system the default one (every other byte the same, the
// glyph ids too) under a private family name, and the run is shaped with that family. See
// docs/text-character-styles.md ("Run language").

#include <QByteArray>
#include <QFont>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace patchy::ui {

// The OpenType language system tags ("ARA ", "ROM ") HarfBuzz would try for a Photoshop text
// language (the Txt2 /38 numbering, docs/txt2.md), most specific first. Empty for English, an
// unset language (-1), Chinese (Photoshop does not say which one) and any number Photoshop does
// not list: those runs shape as before, with the font's default language system.
[[nodiscard]] QList<QByteArray> opentype_language_tags_for_photoshop_language(int language);

// Whether a run in the Photoshop `language` is shaped in its language copy with Contextual
// Alternates on even when the run leaves them at their default, off: Arabic and Farsi. Photoshop
// 2018 builds Adobe Arabic 3.005's Allah ligature, a calt rule of its ARA language system, in a
// run with no /ContextualLigatures (GitHub issue 683).
[[nodiscard]] bool text_language_keeps_contextual_alternates(int language);

// `table` (a GSUB or GPOS table) with every script that lists one of `tags` using that language
// system (the first of `tags` it lists) as its default one, and every script's other language
// systems dropped, so the shaper's own language guess can no longer pick one. Only the counts and
// offsets of the script list change; the table keeps its size. nullopt when the table is
// malformed or no script lists any of the tags.
[[nodiscard]] std::optional<QByteArray> layout_table_for_language(const QByteArray& table,
                                                                  const QList<QByteArray>& tags);

// Whether `family` is one of the copies text_language_font_families registers. Font lists
// (font_families_without_foundries) leave them out.
[[nodiscard]] bool is_text_language_font_family(const QString& family);

// `font`'s families with each installed one whose face lists a language system for the Photoshop
// `language` replaced by that face's language copy, registered on first use and cached for the
// process. Empty when the language has no tags or none of the families' faces lists one, so the
// caller keeps the font as it is. Variable fonts are left alone. Thread-safe.
[[nodiscard]] QStringList text_language_font_families(const QFont& font, int language);

}  // namespace patchy::ui
