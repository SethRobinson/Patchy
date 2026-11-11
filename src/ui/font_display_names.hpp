#pragma once

// "Show font names in English" (Preferences > Application, GitHub issue 691): the names the font
// picker shows for families the font database lists in another script. Display only. The family
// string the database lists stays the key everywhere else: the text engine, the PSD reader and
// writer, Recent and Favorites all keep using it. See docs/font-picker.md, "English names".

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

namespace patchy::ui {

// Persisted identifier (bool, default false): never rename.
inline constexpr char kShowFontNamesInEnglishKey[] = "ui/showFontNamesInEnglish";

[[nodiscard]] bool stored_show_font_names_in_english();
// Stores the choice; a change drops the cached table and emits FontDisplayNamesNotifier::changed.
void set_stored_show_font_names_in_english(bool enabled);

// Whether every character of `name` is Latin script, a digit, punctuation or a space. Such a
// family needs no English name. True for an empty name.
[[nodiscard]] bool font_name_is_latin(const QString& name);

// The Latin-script family name a face's OpenType `name` table offers for a family the database
// lists as `listed_family`, or an empty string when the table has none (or only `listed_family`
// itself). The record kind follows the listed name: a family listed by its GDI family (name id
// 1) answers with that record's English twin before the typographic family (id 16) and the
// other way round, so "Yu Gothic Medium" never collapses into "Yu Gothic". Within a kind:
// Windows en-US, any Windows English, Macintosh English, then any record in Latin script. The
// PostScript name (id 6) is the last resort.
[[nodiscard]] QString english_family_name_from_name_table(const QByteArray& name_table, const QString& listed_family);

// Listed family <-> shown name, for one family list.
class FontDisplayNames {
public:
  // The English name to show for a listed family; empty or the family itself for "none".
  using Lookup = std::function<QString(const QString& family)>;
  // Whether two listed families are one face under two names. Qt's fontconfig database lists a
  // face's second family name as a family of its own when that name's style differs (GitHub
  // issue 691 follow-up 2: a Latin family with a Chinese style beside the Chinese family).
  using SameFace = std::function<bool(const QString& family, const QString& other)>;

  FontDisplayNames() = default;
  // `families` as the picker lists them (plain names, each once). A shown name another family
  // already goes by (listed or shown) is dropped, so every shown name leads back to one family.
  // The one exception: when the English name is a listed family that `same_face` says is the
  // same face, `family` is folded into that twin (shown as its name, left out of the rows).
  [[nodiscard]] static FontDisplayNames build(const QStringList& families, const Lookup& english_name,
                                              const SameFace& same_face = {});

  [[nodiscard]] bool empty() const noexcept { return shown_.isEmpty(); }
  // The name to show for `family`: its English name when it has one, else `family`.
  [[nodiscard]] QString display_name(const QString& family) const;
  // The listed family a shown name stands for (case-insensitive); `name` itself when it is not a
  // shown name.
  [[nodiscard]] QString family_for_display_name(const QString& name) const;
  // Whether the search text is part of the family's listed or shown name, or the listed name of
  // a family folded into it.
  [[nodiscard]] bool matches_search(const QString& family, const QString& text) const;
  // `families` ordered by shown name. Unchanged when no family has one.
  [[nodiscard]] QStringList sorted(const QStringList& families) const;
  // The listed family whose row stands for `family`: its twin when it is folded into one, else
  // `family` itself.
  [[nodiscard]] QString row_family(const QString& family) const;
  // `families` without the ones folded into a twin.
  [[nodiscard]] QStringList without_twins(const QStringList& families) const;

private:
  QHash<QString, QString> shown_;    // listed family -> shown name
  QHash<QString, QString> family_;   // case-folded shown name -> listed family
  QHash<QString, QString> twin_;     // folded family -> the listed family it is shown as
  QHash<QString, QStringList> folded_;  // twin -> the families folded into it
};

// The table for the font database's families: empty while the option is off. Built on first use
// (one font engine load per family whose listed name is not Latin) and kept until the font
// database or the option changes. GUI thread only.
[[nodiscard]] const FontDisplayNames& font_display_names();

// Emits changed() when the table above may answer differently: the option was toggled, or a test
// replaced the lookup. Open font controls refresh from it.
class FontDisplayNamesNotifier : public QObject {
  Q_OBJECT

public:
  [[nodiscard]] static FontDisplayNamesNotifier& instance();

signals:
  void changed();

private:
  FontDisplayNamesNotifier() = default;
};

// Tests: replaces the per-family lookup (the families' own name tables) until destroyed, so a
// test can give any listed family a shown name, and the same-face check (none by default, so a
// shown name a listed family has is dropped). Rebuilds the table and notifies both ways.
class ScopedFontDisplayNameLookup {
public:
  explicit ScopedFontDisplayNameLookup(FontDisplayNames::Lookup lookup, FontDisplayNames::SameFace same_face = {});
  ~ScopedFontDisplayNameLookup();
  ScopedFontDisplayNameLookup(const ScopedFontDisplayNameLookup&) = delete;
  ScopedFontDisplayNameLookup& operator=(const ScopedFontDisplayNameLookup&) = delete;
};

}  // namespace patchy::ui
