#pragma once

// The names a font face carries in its own OpenType `name` table, the Qt family + style that
// really renders the face carrying a given name, and application-font registration that keeps
// a face under its Windows names on macOS.
//
// Qt lists a face under whatever family and style its platform engine derives, and the
// engines disagree for older fonts: CoreText reads the Macintosh name records, so Bitstream's
// FUTURABC.TTF is family "Futura", style "Bold" on macOS, while DirectWrite, GDI and FreeType
// read the Windows records and list "Futura BdCn BT" + "Bold". Photoshop stores the Windows
// (GDI) family and the PostScript name, so a PSD written on Windows names a family no macOS
// database lists even when the font is registered. Two remedies, see docs/font-resolution.md:
//
// - font_face_for_name_table_name looks inside the registered faces instead of guessing from
//   their listed names: every face's PostScript name (id 6), full name (id 4), Windows family
//   (id 1) and typographic family (id 16) map to the Qt family/style pair whose QFont loads it.
// - add_application_font_by_windows_names registers a font Patchy itself loads (user fonts,
//   fixtures) from a copy without its Macintosh name records when those name a different
//   family, so CoreText derives the Windows names and the face never shares a Qt style slot
//   with a same-named system face (Qt keeps ONE style per family + style name; the last face
//   registered under "Futura" + "Bold" silently takes the slot, and CoreText's lazy alias
//   population re-registers every system face).

#include <QByteArray>
#include <QList>
#include <QStringList>
#include <QString>

#include <optional>

namespace patchy::ui {

// Letters and digits only, case-folded: the key two spellings of one font name share
// ("FuturaBT-BoldCondensed" and "Futura BT Bold Condensed" both fold to the same key).
[[nodiscard]] QString compact_text_family_key(const QString& value);

// `family` without the " [Foundry]" suffix QFontDatabase::families() appends when several
// foundries install one family ("Arial [TMC ]", "C059 [urw]" -> "Arial", "C059"; Linux, GitHub
// issue 646). Only a trailing bracketed part after a space goes; "Foo [Bar] Baz" and "[Foo]" are
// returned as they are. A real family name ending in brackets loses them too, which costs
// nothing: Qt's own font matching reads that part as a foundry as well.
[[nodiscard]] QString font_family_without_foundry(const QString& family);
// A family list as the UI shows it: every name without its foundry suffix, and a family several
// foundries install listed once, where its first copy was. Qt resolves the plain name to one of
// those copies.
[[nodiscard]] QStringList font_families_without_foundries(const QStringList& families);

struct OpenTypeNameRecord {
  quint16 platform{0};
  quint16 encoding{0};
  quint16 language{0};
  quint16 name_id{0};
  QString text;  // decoded; UTF-16BE for the Windows and Unicode platforms, Latin-1 for Macintosh
};

// Every decodable record of an OpenType `name` table, in table order. Empty for a malformed table.
[[nodiscard]] QList<OpenTypeNameRecord> opentype_name_records(const QByteArray& name_table);

struct OpenTypeFaceNames {
  QString family;                 // name id 1 (the Windows/GDI family when the table has one)
  QString subfamily;              // name id 2
  QString full_name;              // name id 4
  QString postscript_name;        // name id 6
  QString typographic_family;     // name id 16
  QString typographic_subfamily;  // name id 17
};

// Decodes an OpenType `name` table (the bytes QRawFont::fontTable("name") returns). Windows
// English records win, then any Windows record, then Unicode, then Macintosh; a table with no
// family, full or PostScript name yields nullopt, as does a malformed one.
[[nodiscard]] std::optional<OpenTypeFaceNames> parse_opentype_face_names(const QByteArray& name_table);

// The PostScript name of a variable font's named instance whose subfamily is `subfamily`
// ("Bold"), from its `fvar` and `name` tables: the instance's own PostScript name record when it
// has one, else Adobe's rule (Technical Note 5902): the variations prefix (name id 25, else the
// typographic or plain family) and the subfamily, each without spaces or punctuation, joined by
// '-' ("RobotoMono-Bold"). A variable font's name id 6 names only its default instance, so a
// bold run written under it would open in Photoshop as the default weight. Nullopt when the
// font has no fvar table or no instance with that subfamily.
[[nodiscard]] std::optional<QString> variable_instance_postscript_name(const QByteArray& fvar_table,
                                                                       const QByteArray& name_table,
                                                                       const QString& subfamily);

// One table of a single sfnt font (TrueType or CFF; not a `ttcf` collection). Empty when absent.
[[nodiscard]] QByteArray opentype_table(const QByteArray& font, const char* tag);

// A copy of a single sfnt font with its Macintosh-platform `name` records removed, when the
// Macintosh family (id 1) differs from the Windows family; nullopt when the font is a
// collection, has no such disagreement, or cannot be parsed. Glyphs, metrics and every other
// table are untouched; the table directory entry and the `head` checksum adjustment are
// refreshed. CoreText then derives the family, style and full name from the Windows records.
[[nodiscard]] std::optional<QByteArray> windows_named_font_data(const QByteArray& font);

// QFontDatabase::addApplicationFont for a font file Patchy loads itself. On macOS a font whose
// Macintosh family differs from its Windows family is registered from windows_named_font_data
// instead of the file, so it is listed under the Windows names every other platform and
// Photoshop use; everywhere else, and for every other font, this is the plain registration.
// Returns the application font id, or -1.
int add_application_font_by_windows_names(const QString& path);

struct FontFaceNameMatch {
  QString family;  // a family QFontDatabase::families() lists
  QString style;   // one of QFontDatabase::styles(family), or empty for the family's default face
};

// The database family + style whose font carries `name` in its name table: an exact face for a
// PostScript or full name; for a Windows or typographic family name, the face with the plainest
// subfamily (Regular before Bold before Bold Italic), which the caller's bold/italic flags cannot
// then swap for a sibling face (a known gap). Consult it only after QFontDatabase::families()
// and the platform lookup have missed: it opens fonts. Families whose listed name shares a
// prefix with the request are scanned first; off Windows a miss then scans every family once.
// Results and scanned families are cached until fontDatabaseChanged.
[[nodiscard]] std::optional<FontFaceNameMatch> font_face_for_name_table_name(const QString& name);

}  // namespace patchy::ui
