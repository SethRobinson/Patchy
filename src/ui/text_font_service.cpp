#include "ui/text_font_service.hpp"
#include "ui/font_face_name_index.hpp"
#include "ui/psd_font_resolver.hpp"
#include "ui/user_fonts.hpp"
#include "ui/localization.hpp"
#include "ui/ui_font.hpp"
#include "core/font_substitutes.hpp"
#include "core/layer.hpp"
#include "core/layer_metadata.hpp"
#include "psd/psd_text_engine_block.hpp"
#include "psd/psd_text_runs.hpp"

#include <QMutex>
#include <mutex>
#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QFontInfo>
#include <QGuiApplication>
#include <QHash>
#include <QObject>
#include <QRawFont>
#include <QSet>
#include <QSettings>
#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace patchy::ui {
QString preferred_latin_text_fallback_family() {
  const auto available = QFontDatabase::families();
  for (const auto& candidate : {QStringLiteral("Adobe Arabic"), QStringLiteral("Adobe Clean Serif"),
                                QStringLiteral("Minion Pro"), QStringLiteral("Times New Roman"),
                                QStringLiteral("Georgia"), QStringLiteral("Noto Serif"), QStringLiteral("Arial")}) {
    if (available.contains(candidate, Qt::CaseInsensitive)) {
      return candidate;
    }
  }
  return QApplication::font().family();
}

bool text_family_uses_photoshop_latin_fallback(const QString& family) {
  return family.simplified().compare(QStringLiteral("Noto Naskh Arabic"), Qt::CaseInsensitive) == 0;
}

// compact_text_family_key lives in ui/font_face_name_index.hpp (shared with the name-table index).

std::optional<QString> available_text_family_match(const QString& family) {
  const auto requested = family.trimmed();
  if (requested.isEmpty()) {
    return std::nullopt;
  }

  const auto available = QFontDatabase::families();
  for (const auto& candidate : available) {
    if (candidate.compare(requested, Qt::CaseInsensitive) == 0) {
      return candidate;
    }
  }

  const auto requested_key = compact_text_family_key(requested);
  if (requested_key.isEmpty()) {
    return std::nullopt;
  }
  for (const auto& candidate : available) {
    if (compact_text_family_key(candidate) == requested_key) {
      return candidate;
    }
  }
#ifdef Q_OS_WASM
  // The browser has no system fonts, so common system families resolve to
  // their bundled metric-compatible stand-ins instead of raising the
  // missing-font prompt. Only offer an alias whose target family actually
  // exists (a build staged without third_party/fonts-web must not invent
  // families). Committing such a text layer stores the alias family.
  for (const auto& alias : user_fonts::kWasmFamilyAliases) {
    if (compact_text_family_key(QString::fromLatin1(alias.missing)) != requested_key) {
      continue;
    }
    const auto bundled = QString::fromLatin1(alias.bundled);
    for (const auto& candidate : available) {
      if (candidate.compare(bundled, Qt::CaseInsensitive) == 0) {
        return candidate;
      }
    }
  }
#endif
  return std::nullopt;
}

QString canonical_text_display_family(const QString& family) {
  const auto requested = family.trimmed();
  if (requested.isEmpty()) {
    return default_text_family();
  }
  return available_text_family_match(requested).value_or(requested);
}

// The listed family a name actually draws in. Qt's Linux database lists fontconfig's generic
// "Sans Serif" (the application font under a plain Qt theme, as in the Flatpak) as a family of
// its own, yet it draws in DejaVu Sans or Noto Sans; Windows does not list the name and draws a
// substitute. New type stored the generic name, and the first text layer of a session showed the
// missing-font badge (GitHub issue 442). `family` (trimmed) when it resolves to nothing listed.
// For seeding new type: a family a file names stays as named.
QString installed_text_family(const QString& family) {
  const auto requested = family.trimmed();
  if (requested.isEmpty()) {
    return requested;
  }
  QFont probe;
  probe.setFamilies(QStringList{requested});
  if (const auto resolved = available_text_family_match(QFontInfo(probe).family()); resolved.has_value()) {
    return *resolved;
  }
  return available_text_family_match(requested).value_or(requested);
}

// The family new type starts in when nothing else names one: the application font, as an
// installed family (see installed_text_family).
QString default_text_family() {
  return installed_text_family(QApplication::font().family());
}


// A GDI-style name such as "Arial Black" may exist on some platforms only as family "Arial" with
// style "Black" -- the OpenType typographic family/subfamily split the FreeType database uses
// (the Windows database exposes the legacy family directly, so this is a fallback).  Find the
// longest available family that prefixes the requested name and whose remaining words name one
// of that family's styles.
std::optional<AvailableTextFamilyStyle> font_database_family_style_split(const QString& requested) {
  std::optional<AvailableTextFamilyStyle> best;
  for (const auto& candidate : QFontDatabase::families()) {
    if (candidate.isEmpty() || requested.size() <= candidate.size() ||
        !requested.startsWith(candidate, Qt::CaseInsensitive)) {
      continue;
    }
    const auto separator = requested.at(candidate.size());
    if (separator != QLatin1Char(' ') && separator != QLatin1Char('-')) {
      continue;
    }
    if (best.has_value() && best->family.size() >= candidate.size()) {
      continue;
    }
    const auto style = requested.mid(candidate.size() + 1).trimmed();
    if (style.isEmpty()) {
      continue;
    }
    for (const auto& available_style : QFontDatabase::styles(candidate)) {
      if (available_style.compare(style, Qt::CaseInsensitive) == 0) {
        best = AvailableTextFamilyStyle{candidate, available_style};
        break;
      }
    }
  }
  return best;
}

// The face the platform knows under a name the font database lists neither as a family nor as
// family + face: a full name ("Futura Extra Black BT", which is also what an older PSD reader
// stored for that face), a PostScript name ("FuturaBT-ExtraBlack"), or DirectWrite's own family
// for a legacy face. The PSD reader's DirectWrite lookup answers in the GDI family + subfamily
// the database uses ("Futura XBlk BT" + "Extra Black"); that answer is mapped back onto the
// database, and a face it does not list falls back to the flag face the name implies. Cached
// per name: the lookup scans the system collection, and the options bar asks on every caret
// move. The database can grow after a miss (the headless registry rescue, a user font drop),
// so fontDatabaseChanged clears the cache. Off Windows the platform lookup answers nothing;
// the name-table index below reads only the faces Qt's database holds, so the offscreen
// suites stay hermetic.
std::optional<AvailableTextFamilyStyle> platform_installed_family_style_match(const QString& requested,
                                                                              bool consult_name_tables) {
  // Locked: PDF saves draw text on a worker thread (docs/file-formats.md, "Save progress").
  static QHash<QString, std::optional<AvailableTextFamilyStyle>> cache;
  static QMutex cache_mutex;
  static std::once_flag invalidation_connected;
  if (qGuiApp != nullptr) {
    std::call_once(invalidation_connected, [] {
      QObject::connect(qGuiApp, &QGuiApplication::fontDatabaseChanged, qGuiApp, [] {
        const QMutexLocker lock(&cache_mutex);
        cache.clear();
      });
    });
  }
  const auto cache_key = requested.toCaseFolded() + (consult_name_tables ? QStringLiteral("|names") : QString());
  {
    const QMutexLocker lock(&cache_mutex);
    if (const auto it = cache.constFind(cache_key); it != cache.constEnd()) {
      return *it;
    }
  }
  std::optional<AvailableTextFamilyStyle> match;
  if (const auto installed = psd::installed_font_for_name(requested.toUtf8().toStdString());
      installed.has_value()) {
    const auto family = QString::fromStdString(installed->family).trimmed();
    auto style = QString::fromStdString(installed->style).trimmed();
    auto resolved_family = available_text_family_match(family);
    if (!resolved_family.has_value()) {
      // The reader keeps "family + face" for the faces bold + italic cannot name.
      if (const auto split = font_database_family_style_split(family); split.has_value()) {
        resolved_family = split->family;
        if (style.isEmpty()) {
          style = split->style;
        }
      }
    }
    if (!resolved_family.has_value()) {
      // The database lists the face under another language's name: Qt names fonts for the
      // user's locale, so a Chinese Windows lists DingMao.ttf only by its Chinese family while
      // the reader stores the en-us one (GitHub issue 413).
      for (const auto& name : psd::installed_font_family_names(requested.toUtf8().toStdString())) {
        if (const auto listed = available_text_family_match(QString::fromStdString(name)); listed.has_value()) {
          resolved_family = listed;
          break;
        }
      }
    }
    if (resolved_family.has_value()) {
      if (style.isEmpty()) {
        style = installed->bold ? (installed->italic ? QStringLiteral("Bold Italic") : QStringLiteral("Bold"))
                                : (installed->italic ? QStringLiteral("Italic") : QString());
      }
      QString available_style;
      for (const auto& candidate : QFontDatabase::styles(*resolved_family)) {
        if (candidate.compare(style, Qt::CaseInsensitive) == 0) {
          available_style = candidate;
          break;
        }
      }
      match = AvailableTextFamilyStyle{*resolved_family, available_style};
    }
  }
  if (!match.has_value() && consult_name_tables) {
    // The registered faces' own name tables: the PostScript name, full name or Windows (GDI)
    // family a PSD stores can name a face whose database family is something else entirely
    // (CoreText lists Bitstream's FUTURABC.TTF as "Futura" + "Bold"; the file and Photoshop
    // call it "Futura BdCn BT"). See ui/font_face_name_index.hpp.
    if (const auto indexed = font_face_for_name_table_name(requested); indexed.has_value()) {
      match = AvailableTextFamilyStyle{indexed->family, indexed->style};
    }
  }
  {
    const QMutexLocker lock(&cache_mutex);
    cache.insert(cache_key, match);
  }
  return match;
}

// The family + face a display name resolves to when it is not a family of its own. The
// database's family + face split first ("Arial Black" -> "Arial"/"Black"); a name the database
// cannot split is asked of the platform (full names, PostScript names, DirectWrite families).
// The returned style can be empty when the platform vouches for the family but the database
// lists no such face; callers then render the family's flag face. `consult_name_tables` adds
// the registered faces' own name tables as the last resort (font_face_for_name_table_name);
// the PSD reader's resolver turns it off so imported metadata keeps the same family string on
// every platform and only rendering learns the platform's name for the face.
std::optional<AvailableTextFamilyStyle> available_text_family_style_match(const QString& family,
                                                                          bool consult_name_tables) {
  const auto requested = family.trimmed();
  if (requested.isEmpty()) {
    return std::nullopt;
  }
  if (const auto split = font_database_family_style_split(requested); split.has_value()) {
    return split;
  }
  return platform_installed_family_style_match(requested, consult_name_tables);
}

// Bold/italic as the style NAME describes them, so the flags every downstream reader still uses
// keep agreeing with the face the picker selected.

// English-substring fallback for style names the font database cannot vouch for (a face carried
// over from another machine, or a family that is not installed here at all).
TextStyleFlags text_style_flags_for_name(const QString& style) {
  const auto key = style.trimmed().toCaseFolded();
  TextStyleFlags flags;
  flags.bold = key.contains(QStringLiteral("bold")) || key.contains(QStringLiteral("black")) ||
               key.contains(QStringLiteral("heavy"));
  flags.italic = key.contains(QStringLiteral("italic")) || key.contains(QStringLiteral("oblique"));
  return flags;
}

// The family whose faces the font database should be asked about. A display family can be a
// face-baked name: the PSD reader keeps "family + face" for faces the flags cannot express
// ("ITC Lubalin Graph Demi"), and older imports left names like "Bookman Old Style Italic"
// behind. The database only lists faces under the SPLIT base family; asked with the unsplit
// name it answers nothing at all, which is what emptied the style picker down to a lone
// Regular row and made Bold/Italic divert to faux.
QString text_style_query_family(const QString& family) {
  const auto resolved = canonical_text_display_family(family);
  if (available_text_family_match(resolved).has_value()) {
    return resolved;
  }
  if (const auto split = available_text_family_style_match(resolved); split.has_value()) {
    return split->family;
  }
  return resolved;
}

QStringList available_text_family_styles(const QString& family) {
  // Rebuilt on every caret move via the options-bar sync, so cache per family. User font drops
  // and the registry fallback registration both fire fontDatabaseChanged, which resets it.
  static QHash<QString, QStringList> cache;
  static QMutex cache_mutex;  // PDF saves draw text on a worker thread
  static std::once_flag invalidation_connected;
  if (qGuiApp != nullptr) {
    std::call_once(invalidation_connected, [] {
      QObject::connect(qGuiApp, &QGuiApplication::fontDatabaseChanged, qGuiApp, [] {
        const QMutexLocker lock(&cache_mutex);
        cache.clear();
      });
    });
  }
  const auto resolved = canonical_text_display_family(family);
  const auto cache_key = resolved.toCaseFolded();
  const QMutexLocker lock(&cache_mutex);
  if (const auto it = cache.constFind(cache_key); it != cache.constEnd()) {
    return *it;
  }
  // A family that is not installed at all has no style list to report, and the substitute Qt
  // picks honours the plain bold/italic flags perfectly well. Offer the four standard faces so
  // the picker stays usable and Bold/Italic do NOT divert to faux -- synthesizing on top of a
  // substituted face would be a second helping of the wrong thing.
  if (!available_text_family_match(resolved).has_value() &&
      !available_text_family_style_match(resolved).has_value()) {
    const QStringList fallback{QStringLiteral("Regular"), QStringLiteral("Italic"),
                               QStringLiteral("Bold"), QStringLiteral("Bold Italic")};
    cache.insert(cache_key, fallback);
    return fallback;
  }
  const auto query_family = text_style_query_family(resolved);
  auto styles = QFontDatabase::styles(query_family);
  // Qt's Windows database fills a family's faces in lazily. Push the four flag combinations
  // through the matcher to force population, folding in the REAL faces they resolve to
  // (QRawFont reports the matched face where QFontInfo may echo the requested one back); a
  // family with no italic still answers Regular here, so the list is only completed, never
  // invented.
  for (const auto& flags : {std::pair{false, false}, std::pair{true, false}, std::pair{false, true},
                            std::pair{true, true}}) {
    QFont probe(query_family);
    probe.setBold(flags.first);
    probe.setItalic(flags.second);
    const auto raw = QRawFont::fromFont(probe);
    if (raw.familyName().compare(query_family, Qt::CaseInsensitive) != 0) {
      continue;  // substituted; it says nothing about this family
    }
    const auto style = raw.styleName().trimmed();
    if (!style.isEmpty() && !styles.contains(style, Qt::CaseInsensitive)) {
      styles.append(style);
    }
  }
  // The probes forced the family's population, so ask once more for the faces only the
  // database knows about (Light, Semibold, Black and friends are beyond what four flag
  // probes can name).
  for (const auto& style : QFontDatabase::styles(query_family)) {
    if (!styles.contains(style, Qt::CaseInsensitive)) {
      styles.append(style);
    }
  }
  // Photoshop's ordering: the four standard faces first, then the rest as the database lists
  // them.
  QStringList ordered;
  for (const auto& canonical : {QStringLiteral("Regular"), QStringLiteral("Italic"),
                                QStringLiteral("Bold"), QStringLiteral("Bold Italic")}) {
    for (const auto& style : styles) {
      if (style.compare(canonical, Qt::CaseInsensitive) == 0) {
        ordered.append(style);
        break;
      }
    }
  }
  for (const auto& style : styles) {
    if (!ordered.contains(style, Qt::CaseInsensitive)) {
      ordered.append(style);
    }
  }
  cache.insert(cache_key, ordered);
  return ordered;
}

// Bold/italic as the face name AND the font database describe them, unioned. The name is
// authoritative when it names an axis: the database compares declared weights against
// QFont::Bold (700), and families like Bookman Old Style declare their whole line light
// (Regular 300, Bold 600 on the Windows database), so bold() answers FALSE for their real
// Bold faces and a DB-only reading collapsed a "Bold Italic" pick to plain italic. The
// database still ADDS axes the English substring cannot read (localized face names).
TextStyleFlags text_style_flags_for_style(const QString& family, const QString& style) {
  auto flags = text_style_flags_for_name(style);
  const auto requested = style.trimmed();
  if (!requested.isEmpty() && (!flags.bold || !flags.italic)) {
    const auto query_family = text_style_query_family(family);
    for (const auto& available : QFontDatabase::styles(query_family)) {
      if (available.compare(requested, Qt::CaseInsensitive) == 0) {
        flags.bold = flags.bold || QFontDatabase::bold(query_family, available);
        flags.italic = flags.italic || QFontDatabase::italic(query_family, available);
        break;
      }
    }
  }
  return flags;
}

// Whether bold + italic can already say everything this face name says. Mirrors the PSD
// reader's face_name_is_flag_expressible list: picks from this set live in the flags with NO
// recorded style, so ordinary Bold/Italic text stays on runs v3/v4 and round-trips byte-stable.
// "Plain" and "Roman" are older fonts' names for the upright regular face.
bool text_style_is_flag_expressible(const QString& style) {
  const auto key = style.trimmed().toCaseFolded();
  for (const auto& expressible :
       {QStringLiteral("regular"), QStringLiteral("normal"), QStringLiteral("plain"),
        QStringLiteral("roman"), QStringLiteral("bold"), QStringLiteral("italic"),
        QStringLiteral("oblique"), QStringLiteral("bold italic"), QStringLiteral("italic bold"),
        QStringLiteral("bold oblique")}) {
    if (key == expressible) {
      return true;
    }
  }
  return false;
}

#ifndef Q_OS_WASM
// DirectWrite's off-Windows stand-in for resolving a PSD PostScript font name, installed
// into the PSD reader by install_font_database_psd_font_resolver. The humanized name is
// looked up in the font database as a whole family (CoreText files heavy faces under
// their own name, "Arial Black") or as a family + style split (a FreeType database files
// the same face as family "Arial" with style "Black") -- the same two shapes the
// renderer's matchers resolve. The output rules mirror the DirectWrite resolver so a
// document imported off Windows carries the same family/style/flags a Windows import
// would; nullopt hands the name to the suffix heuristic exactly as before.
std::optional<psd::ResolvedPhotoshopFont> font_database_resolved_photoshop_font(
    std::string_view font_name) {
  const auto humanized =
      QString::fromStdString(psd::humanized_postscript_font_name(font_name)).trimmed();
  if (humanized.isEmpty()) {
    return std::nullopt;
  }
  if (const auto family = available_text_family_match(humanized); family.has_value()) {
    // The whole name is an installed family. The heuristic's flags still ride along so a
    // machine WITHOUT the face reading the saved document falls back to Bold, exactly as
    // a Windows import records (family "Arial Black" + bold). Where the face exists the
    // flag is inert: Qt only synthesizes bold onto faces lighter than the request.
    const auto heuristic = psd::heuristic_resolved_photoshop_font(font_name);
    return psd::ResolvedPhotoshopFont{family->toStdString(), std::string(), heuristic.bold,
                                      heuristic.italic};
  }
  // Database names only: a face the name-table index alone can find keeps the humanized
  // name in the metadata (identical on every platform) and resolves at render time.
  const auto split = available_text_family_style_match(humanized, /*consult_name_tables*/ false);
  if (!split.has_value()) {
    return std::nullopt;
  }
  const auto name_flags = text_style_flags_for_name(split->style);
  const bool italic = name_flags.italic || QFontDatabase::italic(split->family, split->style);
  if (text_style_is_flag_expressible(split->style)) {
    // A face the flags can express is never baked into the family (the Bookman rule in
    // the DirectWrite resolver), so ordinary imports stay on runs v3/v4.
    const bool bold = name_flags.bold || QFontDatabase::bold(split->family, split->style);
    return psd::ResolvedPhotoshopFont{split->family.toStdString(), std::string(), bold, italic};
  }
  const auto weight = QFontDatabase::weight(split->family, split->style);
  const auto compound = split->family + QLatin1Char(' ') + split->style;
  if (weight >= QFont::ExtraBold) {
    // Black/Heavy keeps its full display name plus the bold flag as the
    // uninstalled-face fallback, mirroring the DirectWrite weight >= 800 branch.
    return psd::ResolvedPhotoshopFont{compound.toStdString(), split->style.toStdString(), true,
                                      italic};
  }
  if (weight >= 0 && weight != QFont::Normal && weight != QFont::Bold) {
    // Only Regular and Bold survive flattening into family + flags; every other weight
    // keeps its real face, and the bold flag stays OFF because the name already carries
    // the weight (setting both would synthesize on top of the face).
    return psd::ResolvedPhotoshopFont{compound.toStdString(), split->style.toStdString(), false,
                                      italic};
  }
  return psd::ResolvedPhotoshopFont{split->family.toStdString(), split->style.toStdString(),
                                    name_flags.bold || weight >= QFont::DemiBold, italic};
}
#endif

// The face name the bold/italic flags describe, used when a run records no explicit style.
QString text_style_name_for_flags(bool bold, bool italic) {
  if (bold && italic) {
    return QStringLiteral("Bold Italic");
  }
  if (bold) {
    return QStringLiteral("Bold");
  }
  if (italic) {
    return QStringLiteral("Italic");
  }
  return {};
}

// Whether the family really ships a bold (or italic) face at all. Asked ONE AXIS AT A TIME on
// purpose: Century Gothic has Bold and no Italic, so Ctrl+B must still take the real Bold face
// while Ctrl+I falls back to the synthetic slant -- which is also what Photoshop does with that
// pair. When the face is missing entirely the toggle has nothing to select (Qt hands back
// Regular and it looks broken), so it applies the FAUX version instead.
bool family_offers_face_axis(const QString& family, bool bold_axis) {
  for (const auto& style : available_text_family_styles(family)) {
    const auto flags = text_style_flags_for_style(family, style);
    if (bold_axis ? flags.bold : flags.italic) {
      return true;
    }
  }
  return false;
}

// The face the flags describe once the axes the family cannot supply are dropped: bold + italic
// on Century Gothic (Bold, no Italic) really IS the "Bold" face plus a synthetic slant, so the
// picker must show Bold rather than fall back to Regular hunting for a "Bold Italic" that does
// not exist.
QString real_face_style_name(const QString& family, bool bold, bool italic) {
  return text_style_name_for_flags(bold && family_offers_face_axis(family, true),
                                   italic && family_offers_face_axis(family, false));
}

// Whether `family` can draw ANY of the characters it is being asked for. A family that resolves
// in the font database but covers none of them is missing in every way the user cares about: Qt
// falls through to some other face for every glyph, so the layer silently renders in a different
// typeface. Patchy's own bundled Noto Naskh Arabic is the case that forced this -- the family
// exists (it ships in third_party/fonts), but its cmap holds no Latin letters at all (space, ! ,
// . : and the digits are the whole ASCII coverage), so a Latin type layer set in it came up
// "font available" and then rendered entirely in the Latin fallback.
//
// Deliberately "any", not "all": a face missing one exotic glyph is a normal per-glyph fallback,
// not a missing font, and crying wolf there would put a warning on half the corpus. Probed
// without the run's bold/italic because coverage differences between styles of one family are
// vanishingly rare and a false "available" is the quiet direction.
// The writing system Qt matches a character through, so the probe below asks the font database
// the same question the renderer will. Unmapped scripts fall back to Any, which is the quiet
// answer (Han deliberately: a Japanese face claims Japanese, not SimplifiedChinese, and guessing
// between them would invent missing fonts).
QFontDatabase::WritingSystem writing_system_for_character(QChar character) {
  switch (character.script()) {
    case QChar::Script_Latin:
      return QFontDatabase::Latin;
    case QChar::Script_Greek:
      return QFontDatabase::Greek;
    case QChar::Script_Cyrillic:
      return QFontDatabase::Cyrillic;
    case QChar::Script_Arabic:
      return QFontDatabase::Arabic;
    case QChar::Script_Hebrew:
      return QFontDatabase::Hebrew;
    case QChar::Script_Thai:
      return QFontDatabase::Thai;
    case QChar::Script_Devanagari:
      return QFontDatabase::Devanagari;
    case QChar::Script_Hiragana:
    case QChar::Script_Katakana:
      return QFontDatabase::Japanese;
    case QChar::Script_Hangul:
      return QFontDatabase::Korean;
    default:
      return QFontDatabase::Any;
  }
}

// Whether the font database holds at least one family that can draw `system`. A "yes" is
// cached for the process: application fonts are only ever added, never removed, so it cannot go
// stale, while a "no" is asked again in case a later registration (the registry rescue, a user
// font) filled the gap.
bool writing_system_has_any_family(QFontDatabase::WritingSystem system) {
  static QSet<int> covered;
  const auto key = static_cast<int>(system);
  if (covered.contains(key)) {
    return true;
  }
  if (QFontDatabase::families(system).isEmpty()) {
    return false;
  }
  covered.insert(key);
  return true;
}

bool text_family_draws_any_of(const QString& family, const QString& demanded) {
  const auto requested = family.trimmed();
  if (requested.isEmpty() || demanded.isEmpty()) {
    return true;
  }
  // The face Qt would hand this family for a given writing system. Asking QRawFont WITHOUT the
  // writing system is useless here: it resolves through the default script, so a family that
  // cannot draw Latin quietly comes back as the Latin fallback and reports full coverage. The
  // family name of what comes back is the whole signal -- if Qt handed us a different family,
  // that is exactly the silent substitution the user is being warned about.
  auto expected = requested;
  if (const auto match = available_text_family_match(requested); match.has_value()) {
    expected = *match;
  } else if (const auto style_match = available_text_family_style_match(requested); style_match.has_value()) {
    expected = style_match->family;
  }
  // A listed name that stands for another face, like fontconfig's "Sans Serif" (GitHub issue
  // 442): the face it resolves to is the font asked for, not a substitute. Only for a listed name,
  // so a missing family never passes as the face Qt substitutes for it.
  if (available_text_family_match(expected).has_value()) {
    expected = installed_text_family(expected);
  }
  const auto probe_draws = [&demanded, &expected](const QFont& probe) {
    QHash<int, QRawFont> faces;
    bool tested_any = false;
    for (const auto character : demanded) {
      // Surrogates cannot be tested one half at a time, and whitespace/controls are drawn by
      // nobody -- neither proves nor disproves coverage.
      if (character.isSpace() || character.isSurrogate() || character.category() == QChar::Other_Control) {
        continue;
      }
      const auto system = writing_system_for_character(character);
      tested_any = true;
      auto found = faces.find(static_cast<int>(system));
      if (found == faces.end()) {
        found = faces.insert(static_cast<int>(system), QRawFont::fromFont(probe, system));
      }
      const auto& face = *found;
      if (!face.isValid()) {
        return true;  // nothing to interrogate; stay quiet rather than guess
      }
      // When NO registered family covers the writing system, Qt resolves the request to its
      // glyph-box engine, whose family list is empty, and QRawFont::familyName() indexes that list
      // without a check (an access violation, seen with Thai and Japanese text in --headless runs
      // and in the offscreen suite, where only the bundled and rescued faces exist). Nothing can
      // draw the character, which is exactly the fallthrough case below.
      if (!writing_system_has_any_family(system)) {
        continue;
      }
      if (face.familyName().compare(expected, Qt::CaseInsensitive) != 0) {
        continue;  // Qt already fell through to another family for this character
      }
      if (face.supportsCharacter(character)) {
        return true;
      }
    }
    return !tested_any;
  };
  QFont probe;
  probe.setFamilies(QStringList{expected});
  probe.setPixelSize(32);
  if (probe_draws(probe)) {
    return true;
  }
  // The plain probe asks for a Regular face. A family that has none (only Bold registered, a
  // display family that ships a single heavy face) can lose that request to another family that
  // does cover the script, which read as "this font cannot draw Arabic" for Noto Naskh Arabic
  // Bold registered alone. Before calling the family glyphless, ask each face it really has.
  for (const auto& style : QFontDatabase::styles(expected)) {
    auto styled = QFontDatabase::font(expected, style, 12);
    styled.setPixelSize(32);
    if (probe_draws(styled)) {
      return true;
    }
  }
  return false;
}

bool try_register_missing_system_font_family(const QString& family);

// `demanded` is the text this family actually has to draw; pass it empty to check availability
// alone. With `no_glyphs`, a family that is installed but cannot draw the text lands there
// instead of in `missing`, so a message can name the real cause.
void append_missing_text_family(QStringList& missing, const QString& family, const QString& demanded,
                                QStringList* no_glyphs) {
  const auto requested = family.trimmed();
  if (requested.isEmpty() || requested.compare(QStringLiteral("PSD Text"), Qt::CaseInsensitive) == 0) {
    return;
  }
  bool resolves = available_text_family_match(requested).has_value() ||
                  available_text_family_style_match(requested).has_value();
  if (!resolves && try_register_missing_system_font_family(requested)) {
    // A font Windows has but Qt's database skipped is not missing: the render path registers it
    // from the registry, so the warning has to ask the same question or it fires on an
    // installed font (a no-op under the offscreen platform, exactly like the render rescue).
    resolves = available_text_family_match(requested).has_value() ||
               available_text_family_style_match(requested).has_value();
  }
  if (resolves && text_family_draws_any_of(requested, demanded)) {
    return;
  }

  auto& list = resolves && no_glyphs != nullptr ? *no_glyphs : missing;
  const auto requested_key = compact_text_family_key(requested);
  const bool already_listed = std::any_of(list.begin(), list.end(), [&requested, &requested_key](const QString& item) {
    return item.compare(requested, Qt::CaseInsensitive) == 0 ||
           (!requested_key.isEmpty() && compact_text_family_key(item) == requested_key);
  });
  if (!already_listed) {
    list.push_back(requested);
  }
}

// Families the layer names that cannot draw its glyphs -- absent from the font database, or
// present with no coverage for the characters they are asked for. Each run is checked against
// its OWN slice of the text, so a run whose face genuinely covers its characters never drags a
// warning in from a sibling run.
QStringList missing_text_families_for_psd_raster_preview(const QString& primary_family, const QString& runs_text,
                                                          const QString& text, QStringList* no_glyphs) {
  QStringList missing;
  append_missing_text_family(missing, primary_family, text, no_glyphs);

  const auto lines = runs_text.split(QLatin1Char('\n'));
  for (const auto& raw_line : lines) {
    const auto line = raw_line.trimmed();
    if (line.isEmpty() || line.startsWith(QLatin1Char('v'))) {
      continue;
    }
    const auto fields = line.split(QLatin1Char('\t'));
    if (fields.size() < 7) {
      continue;
    }
    bool start_ok = false;
    bool length_ok = false;
    const auto start = std::clamp(fields[0].toInt(&start_ok), 0, static_cast<int>(text.size()));
    const auto length = std::max(0, fields[1].toInt(&length_ok));
    const auto demanded = start_ok && length_ok ? text.mid(start, length) : text;
    append_missing_text_family(missing, QString::fromUtf8(QByteArray::fromPercentEncoding(fields[6].toLatin1())),
                               demanded, no_glyphs);
  }
  return missing;
}

QString display_text_family_from_font(const QFont& font) {
  const auto families = font.families();
  if (families.size() >= 2 && text_family_uses_photoshop_latin_fallback(families.at(1))) {
    return families.at(1);
  }
  const auto family = font.family().trimmed();
  return family.isEmpty() ? default_text_family() : family;
}

QStringList render_text_families_for_display_family(const QString& family) {
  const auto display_family = canonical_text_display_family(family);
  QStringList families;
  if (text_family_uses_photoshop_latin_fallback(display_family)) {
    families = QStringList{preferred_latin_text_fallback_family(), display_family};
  } else {
    families = QStringList{display_family};
  }
#ifdef Q_OS_WASM
  // Per-glyph CJK fallback: CJK text typed into any Latin-only face renders
  // through the bundled Noto Sans JP/SC/TC instead of tofu (no system fonts
  // exist for Qt's own fallback machinery in the browser). The order follows
  // the UI language, which is the best guess available for which script's
  // glyph shapes the author wants (see wasm_cjk_fallback_families).
  for (const auto& fallback :
       wasm_cjk_fallback_families(LocalizationManager::instance().current_language())) {
    if (!families.contains(fallback, Qt::CaseInsensitive)) {
      families.append(fallback);
    }
  }
#endif
  return families;
}

// The family Qt actually draws `family` with when the family itself is missing: the face already
// on the canvas and named in the options bar (Windows resolves an unknown family through its own
// substitution chain, usually to Tahoma). An edit session has to move ONTO that face, because a
// commit that stored the missing name back would re-render in the substitute and leave the layer
// still flagged as missing its font -- adding one letter to such a layer brought the layer-panel
// warning badge straight back. Returns `family` unchanged when it can draw `demanded` itself, and
// when nothing installed can (the layer honestly still has a font problem).
// The compact key the missing-font check dedupes by, so "LiberationSans" in one run and
// "Liberation Sans" in another take the same replacement.
QString text_font_replacement_key(const QString& family) {
  const auto compact = compact_text_family_key(family);
  return compact.isEmpty() ? family.trimmed().toCaseFolded() : compact;
}

std::vector<TextFontSubstitute> installed_text_font_substitutes(const QString& family, const QString& demanded) {
  std::vector<TextFontSubstitute> substitutes;
  const auto utf8 = family.trimmed().toUtf8();
  const auto lookup = find_font_substitutes(std::string_view(utf8.constData(), static_cast<std::size_t>(utf8.size())));
  if (!lookup.has_value() || lookup->entry == nullptr) {
    return substitutes;
  }
  const auto installed_family = [](std::string_view candidate) {
    return available_text_family_match(
        QString::fromUtf8(candidate.data(), static_cast<qsizetype>(candidate.size())));
  };
  // The face a face-baked request names beyond what bold + italic express ("Semibold" in "Myriad
  // Pro Semibold"). PostScript suffixes ("BoldMT") and flag faces leave it to the run's flags.
  auto face = QString::fromStdString(lookup->style).trimmed();
  for (const auto suffix : {QStringLiteral("PSMT"), QStringLiteral("MT")}) {
    if (face.endsWith(suffix, Qt::CaseInsensitive)) {
      face.chop(suffix.size());
      break;
    }
  }
  face = face.trimmed();
  const bool face_needs_family = !face.isEmpty() && !text_style_is_flag_expressible(face) &&
                                 face.compare(QStringLiteral("It"), Qt::CaseInsensitive) != 0 &&
                                 face.compare(QStringLiteral("BoldIt"), Qt::CaseInsensitive) != 0;
  const auto ranked = rank_font_substitutes(
      *lookup->entry, [&installed_family](std::string_view candidate) { return installed_family(candidate).has_value(); });
  for (const auto& suggestion : ranked) {
    const auto installed = installed_family(suggestion.family);
    if (!installed.has_value()) {
      continue;
    }
    TextFontSubstitute substitute{*installed, *installed, suggestion.match == FontSubstituteMatch::metric};
    if (face_needs_family) {
      const auto with_face = *installed + QLatin1Char(' ') + face;
      if (available_text_family_match(with_face).has_value() ||
          available_text_family_style_match(with_face, false).has_value()) {
        substitute.face_family = with_face;
      } else {
        substitute.metric = false;  // another face of the substitute: its widths are not promised
      }
    }
    QStringList missing;
    append_missing_text_family(missing, substitute.face_family, demanded);
    if (missing.isEmpty()) {
      substitutes.push_back(substitute);
    }
  }
  // A face fallback can demote a metric match below a look-alike that kept its face.
  std::stable_sort(substitutes.begin(), substitutes.end(),
                   [](const TextFontSubstitute& a, const TextFontSubstitute& b) { return a.metric && !b.metric; });
  return substitutes;
}

QString substituted_text_family(const QString& family, const QString& demanded) {
  const auto cannot_draw = [&demanded](const QString& candidate) {
    QStringList missing;
    append_missing_text_family(missing, candidate, demanded);
    return !missing.isEmpty();
  };
  if (!cannot_draw(family)) {
    return family;
  }
  // The built-in substitute table first (GitHub issue 463): a metric match keeps the line breaks,
  // where Qt's own fallback (Tahoma on Windows) does not.
  if (const auto substitutes = installed_text_font_substitutes(family, demanded); !substitutes.empty()) {
    return substitutes.front().face_family;
  }
  QFont probe;
  probe.setFamilies(render_text_families_for_display_family(family));
  const auto resolved = QFontInfo(probe).family().trimmed();
  if (!resolved.isEmpty() && resolved.compare(family.trimmed(), Qt::CaseInsensitive) != 0 &&
      !cannot_draw(resolved)) {
    return resolved;
  }
  // Qt can echo the request back instead of naming its substitute; the UI font is always real.
  const auto ui_family = default_text_family();
  return cannot_draw(ui_family) ? family : ui_family;
}

// Windows font files the system knows about: the machine-wide CurrentVersion\Fonts
// key (relative names live in %WINDIR%\Fonts) plus the per-user key Windows fills
// when a font is installed without elevation (absolute paths).
#ifdef Q_OS_WIN
struct WindowsRegistryFont {
  QString display_name;  // the full face name Windows shows, e.g. "Futura Extra Black BT (TrueType)"
  QString file;
};

std::vector<WindowsRegistryFont> windows_registry_fonts() {
  std::vector<WindowsRegistryFont> fonts_found;
  const QDir fonts_dir(QDir(QString::fromLocal8Bit(qgetenv("WINDIR"))).filePath(QStringLiteral("Fonts")));
  for (const auto& root : {QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE"), QStringLiteral("HKEY_CURRENT_USER\\Software")}) {
    const QSettings fonts(root + QStringLiteral("\\Microsoft\\Windows NT\\CurrentVersion\\Fonts"),
                          QSettings::NativeFormat);
    for (const auto& name : fonts.allKeys()) {
      auto file = fonts.value(name).toString();
      if (file.isEmpty()) {
        continue;
      }
      if (!QFileInfo(file).isAbsolute()) {
        file = fonts_dir.filePath(file);
      }
      fonts_found.push_back(WindowsRegistryFont{name, file});
    }
  }
  return fonts_found;
}
#endif


// The offscreen platform on Windows (a --headless run) enumerates no system fonts; macOS and
// Linux offscreen go through CoreText / fontconfig and see them. Loads the installed fonts from
// the registry once per process so the database holds their real family and style names. True
// when this call loaded something new. A no-op on other platforms and for the offscreen SUITES
// (no PATCHY_HEADLESS), which stay hermetic.
bool ensure_headless_system_fonts_loaded() {
#ifdef Q_OS_WIN
  if (QGuiApplication::platformName() != QLatin1String("offscreen") ||
      !qEnvironmentVariableIsSet("PATCHY_HEADLESS")) {
    return false;
  }
  static bool loaded = false;
  if (loaded) {
    return false;
  }
  loaded = true;
  bool added = false;
  for (const auto& entry : windows_registry_fonts()) {
    if (QFileInfo::exists(entry.file) && QFontDatabase::addApplicationFont(entry.file) >= 0) {
      added = true;
    }
  }
  return added;
#else
  return false;
#endif
}


// Called when a requested family resolves to nothing. Two different gaps, both filled
// by registering the installed files as application fonts (never removed afterwards:
// removeApplicationFont can crash live font users, the testing notes' standing rule):
//
// - A --headless run on Windows sees NO system fonts (ensure_headless_system_fonts_loaded
//   above): the first miss loads every installed font once, so the ordinary matching then
//   resolves a request exactly as the desktop app would (a family, "Futura XBlk BT", or family
//   plus face, "Arial Black"). Matching registry display names against the request would be
//   guesswork: the registry stores full face names, not families. main.cpp marks a headless
//   run with PATCHY_HEADLESS=1, which the offscreen suites never set (docs/testing.md).
// - On the desktop, a font registered with Windows can still be missing from Qt's
//   database (this machine's Arial Narrow: in the registry and C:\Windows\Fonts, absent
//   from both the family list and Arial's style list). There the registry display name
//   does begin with the family, so the entries starting with the request are loaded;
//   each family is attempted once per run.
bool try_register_missing_system_font_family(const QString& family) {
#ifdef Q_OS_WIN
  const bool offscreen = QGuiApplication::platformName() == QLatin1String("offscreen");
  if (offscreen && !qEnvironmentVariableIsSet("PATCHY_HEADLESS")) {
    return false;
  }
  const auto requested = family.trimmed();
  if (requested.isEmpty()) {
    return false;
  }
  if (offscreen) {
    return ensure_headless_system_fonts_loaded();
  }
  static QSet<QString> attempted;
  const auto key = requested.toLower();
  if (attempted.contains(key)) {
    return false;
  }
  attempted.insert(key);
  bool added = false;
  for (const auto& entry : windows_registry_fonts()) {
    if (!entry.display_name.startsWith(requested, Qt::CaseInsensitive)) {
      continue;
    }
    if (QFileInfo::exists(entry.file) && QFontDatabase::addApplicationFont(entry.file) >= 0) {
      added = true;
    }
  }
  return added;
#else
  (void)family;
  return false;
#endif
}

// `style_name` is the face (OpenType subfamily) the run asks for -- "Demi", "Black", "Book" --
// for the styles bold+italic cannot name. Empty falls back to the flags.
QFont render_text_font_for_display_family(const QString& family, int pixel_size, bool bold, bool italic,
                                          int anti_alias, const QString& style_name) {
  QFont font;
  font.setFamilies(render_text_families_for_display_family(family));
  font.setPixelSize(std::max(1, pixel_size));
  font.setBold(bold);
  font.setItalic(italic);
  // When the requested name is not a family of its own, resolve it as family + style (e.g.
  // "Arial Black" -> "Arial"/"Black") so the proper face renders instead of a regular-weight
  // substitute.  The style name is set last: it takes precedence over the bold flag in matching.
  if (!available_text_family_match(family).has_value()) {
    auto match = available_text_family_style_match(family);
    if (!match.has_value() && try_register_missing_system_font_family(family)) {
      // Registration may surface the face as its own family or as a style of
      // an existing one, depending on the platform database's naming.
      if (const auto direct = available_text_family_match(family); direct.has_value()) {
        font.setFamilies(QStringList{*direct});
      } else {
        match = available_text_family_style_match(family);
      }
    }
    if (match.has_value()) {
      font.setFamilies(QStringList{match->family});
      if (!match->style.isEmpty()) {
        font.setStyleName(match->style);
      }
    }
  }
  // An explicit face name wins over everything above: a family's styles are an arbitrary list
  // ("Demi", "Book", "Condensed Black") and bold+italic can only name four of them. Only applied
  // when the family really offers it, so a style carried over from another family (or from a
  // machine where the font was installed) falls back to the flags instead of rendering nothing.
  const auto requested_style = style_name.trimmed();
  if (!requested_style.isEmpty()) {
    const auto resolved_family = font.families().isEmpty() ? family : font.families().front();
    for (const auto& available : QFontDatabase::styles(resolved_family)) {
      if (available.compare(requested_style, Qt::CaseInsensitive) == 0) {
        font.setStyleName(available);
        break;
      }
    }
  }
  configure_text_font_smoothing(font, anti_alias);
  return font;
}

// The writing system a fallback face is looked up under for one character. Han is shared by
// three systems; the first one an installed face covers wins, Japanese first (the same order the
// wasm CJK fallback uses).
QFontDatabase::WritingSystem fallback_writing_system_for_character(QChar character) {
  if (character.script() == QChar::Script_Han) {
    for (const auto system : {QFontDatabase::Japanese, QFontDatabase::SimplifiedChinese,
                              QFontDatabase::TraditionalChinese}) {
      if (!QFontDatabase::families(system).isEmpty()) {
        return system;
      }
    }
    return QFontDatabase::Any;
  }
  return writing_system_for_character(character);
}

void configure_text_font_smoothing(QFont& font, int anti_alias) {
  anti_alias = std::clamp(anti_alias, 0, 16);
  if (anti_alias <= 0) {
    font.setStyleStrategy(QFont::NoAntialias);
    font.setHintingPreference(QFont::PreferFullHinting);
    return;
  }

  // Photoshop never runs TrueType hinting: every antialiased mode (Sharp,
  // Crisp, Strong, Smooth) filters unhinted outlines with ideal metrics.
  // Mapping Sharp/Crisp to Qt's full hinting regularized stems (old
  // small-print fonts like Bitstream Futura fatten visibly at text sizes) and
  // shifted advances enough to collide adjacent glyphs, so imported text
  // re-rendered bolder and tighter than Photoshop's raster of the same layer.
  font.setStyleStrategy(QFont::PreferAntialias);
  font.setHintingPreference(QFont::PreferNoHinting);
}

// Public (ui/psd_font_resolver.hpp): points the PSD reader's PostScript font-name
// resolution at the font database. On Windows the hook is installed but never
// consulted (the reader keeps its pinned DirectWrite -> registry -> heuristic
// chain); on wasm nothing is installed, because available_text_family_match
// resolves common system families through the bundled alias table and a bare
// PostScript name like "Helvetica" would bake the stand-in family into imported
// metadata (aliases apply at render and edit time instead, see docs/fonts.md).
void install_font_database_psd_font_resolver() {
#ifndef Q_OS_WASM
  psd::set_photoshop_font_resolver([](std::string_view font_name) {
    return font_database_resolved_photoshop_font(font_name);
  });
#endif
#ifndef Q_OS_WIN
  psd::set_area_text_font_resolver([](std::string_view family, std::string_view style,
                                     bool bold, bool italic) -> std::optional<std::string> {
    const auto display_family = QString::fromUtf8(family.data(), static_cast<qsizetype>(family.size()));
    const auto display_style = QString::fromUtf8(style.data(), static_cast<qsizetype>(style.size()));
    const auto font = render_text_font_for_display_family(display_family, 24, bold, italic, 3, display_style);
    const auto systems = QFontDatabase::writingSystems(font.family());
    const auto raw = QRawFont::fromFont(font, systems.isEmpty() ? QFontDatabase::Any : systems.front());
    const auto names = parse_opentype_face_names(raw.fontTable("name"));
    if (!names || names->postscript_name.isEmpty()) return std::nullopt;
    return names->postscript_name.toStdString();
  });
#endif
}

QStringList missing_text_families_for_layer(const Layer& layer) {
  if (!layer_is_text(layer)) {
    return {};
  }
  const auto& metadata = layer.metadata();
  const auto value = [&metadata](const char* key) {
    const auto found = metadata.find(key);
    return found == metadata.end() ? QString() : QString::fromStdString(found->second);
  };
  const auto text = value(kLayerMetadataText);
  if (text.trimmed().isEmpty()) {
    return {};
  }
  return missing_text_families_for_psd_raster_preview(value(kLayerMetadataTextFont),
                                                      value(kLayerMetadataTextRuns), text);
}

TextFontProblems text_font_problems_for_layer(const Layer& layer) {
  TextFontProblems problems;
  if (!layer_is_text(layer)) {
    return problems;
  }
  const auto& metadata = layer.metadata();
  const auto value = [&metadata](const char* key) {
    const auto found = metadata.find(key);
    return found == metadata.end() ? QString() : QString::fromStdString(found->second);
  };
  const auto text = value(kLayerMetadataText);
  if (text.trimmed().isEmpty()) {
    return problems;
  }
  problems.not_installed = missing_text_families_for_psd_raster_preview(
      value(kLayerMetadataTextFont), value(kLayerMetadataTextRuns), text, &problems.no_glyphs);
  return problems;
}


}  // namespace patchy::ui
