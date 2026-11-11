#include "ui/font_display_names.hpp"

#include "ui/app_settings.hpp"
#include "ui/font_face_name_index.hpp"

#include <QChar>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QPointer>
#include <QRawFont>
#include <QSet>

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

constexpr quint16 kPlatformUnicode = 0;
constexpr quint16 kPlatformMacintosh = 1;
constexpr quint16 kPlatformWindows = 3;
constexpr quint16 kWindowsEnglishUs = 0x0409;
constexpr quint16 kWindowsPrimaryLanguageMask = 0x03ff;
constexpr quint16 kWindowsPrimaryEnglish = 0x09;

constexpr quint16 kNameFamily = 1;
constexpr quint16 kNamePostScript = 6;
constexpr quint16 kNameTypographicFamily = 16;

constexpr int kNoRank = 99;

[[nodiscard]] bool is_ascii(const QString& text) {
  return std::all_of(text.cbegin(), text.cend(), [](QChar ch) { return ch.unicode() < 0x80U; });
}

// The ASCII text of a Windows record in a legacy code page (Shift JIS, PRC, Big5, Wansung, Johab;
// GitHub issue 691 follow-up), empty when it holds anything else. ASCII reads the same in all of
// them, and fonts store it two ways: one character per 16-bit unit (0x00XX, what
// decode_name_string's UTF-16 reading already gives) or two bytes packed into each unit.
[[nodiscard]] QString legacy_windows_ascii(const QString& utf16_text) {
  if (is_ascii(utf16_text)) {
    return utf16_text;
  }
  QString packed;
  packed.reserve(utf16_text.size() * 2);
  for (const auto ch : utf16_text) {
    for (const auto byte : {static_cast<char16_t>(ch.unicode() >> 8), static_cast<char16_t>(ch.unicode() & 0xffU)}) {
      if (byte == 0) {
        continue;  // padding of an odd-length string
      }
      if (byte < 0x20U || byte >= 0x7fU) {
        return {};
      }
      packed.append(QChar(byte));
    }
  }
  return packed;
}

struct NameCandidate {
  int rank{kNoRank};  // how well it serves as the English name; lower is better
  QString text;
};

// A record as an English name candidate. Every candidate must be Latin script whatever its
// language says: some fonts file their native name under en-US.
NameCandidate english_name_candidate(const OpenTypeNameRecord& record) {
  NameCandidate candidate;
  candidate.text = record.text;
  switch (record.platform) {
    case kPlatformWindows:
      // Symbol (0) and Unicode BMP (1) strings are UTF-16; the legacy code pages are read for
      // ASCII only.
      if (record.encoding > 1) {
        candidate.text = legacy_windows_ascii(record.text);
      }
      if (record.language == kWindowsEnglishUs) {
        candidate.rank = 0;
      } else {
        candidate.rank = (record.language & kWindowsPrimaryLanguageMask) == kWindowsPrimaryEnglish ? 1 : 4;
      }
      break;
    case kPlatformMacintosh:
      // decode_name_string reads Latin-1; only the ASCII range means the same in Mac Roman and
      // in the other Macintosh encodings (Chinese, Japanese, Korean and the rest).
      if (!is_ascii(record.text)) {
        return {};
      }
      candidate.rank = record.encoding == 0 && record.language == 0 ? 2 : 4;
      break;
    case kPlatformUnicode:
      candidate.rank = 3;
      break;
    default:
      return {};
  }
  candidate.text = candidate.text.simplified();
  if (candidate.text.isEmpty() || !font_name_is_latin(candidate.text)) {
    return {};
  }
  return candidate;
}

struct NameTableAnswer {
  bool lists_family{false};  // some family record (id 1 or 16, any language) is the listed name
  QString english;
};

NameTableAnswer english_name_answer(const QByteArray& name_table, const QString& listed_family) {
  NameTableAnswer answer;
  const auto listed_key = listed_family.simplified().toCaseFolded();
  using Best = NameCandidate;
  Best family;
  Best typographic;
  Best postscript;
  bool listed_is_family = false;
  bool listed_is_typographic = false;
  for (const auto& record : opentype_name_records(name_table)) {
    Best* slot = nullptr;
    switch (record.name_id) {
      case kNameFamily:
        slot = &family;
        listed_is_family = listed_is_family || record.text.simplified().toCaseFolded() == listed_key;
        break;
      case kNameTypographicFamily:
        slot = &typographic;
        listed_is_typographic =
            listed_is_typographic || record.text.simplified().toCaseFolded() == listed_key;
        break;
      case kNamePostScript:
        slot = &postscript;
        break;
      default:
        continue;
    }
    if (auto candidate = english_name_candidate(record); candidate.rank < slot->rank) {
      *slot = std::move(candidate);
    }
  }
  answer.lists_family = listed_is_family || listed_is_typographic;
  // The record kind the database listed the face under answers first.
  const bool gdi_first = listed_is_family && !listed_is_typographic;
  const std::array<const Best*, 3> order{gdi_first ? &family : &typographic, gdi_first ? &typographic : &family,
                                         &postscript};
  for (const auto* candidate : order) {
    if (candidate->rank < kNoRank && candidate->text.toCaseFolded() != listed_key) {
      answer.english = candidate->text;
      break;
    }
  }
  return answer;
}

// The default lookup: the family's own name table, through whichever font engine the platform
// uses. Families already named in Latin script are never opened.
QString english_name_from_font(const QString& family) {
  if (font_name_is_latin(family)) {
    return {};
  }
  const auto raw = QRawFont::fromFont(QFontDatabase::font(family, QFontDatabase::styles(family).value(0), 12));
  if (!raw.isValid()) {
    return {};
  }
  const auto answer = english_name_answer(raw.fontTable("name"), family);
  // A family without the default script is answered with a fallback font, whose table would
  // name the fallback: the face must be the family asked for, or carry its name.
  if (!answer.lists_family && raw.familyName().compare(family, Qt::CaseInsensitive) != 0) {
    return {};
  }
  return answer.english;
}

struct DisplayNameState {
  FontDisplayNames table;
  bool valid{false};
  bool connected{false};
  FontDisplayNames::Lookup lookup_override;
  // Lookup answers by family and style list, kept across font database changes so a font added
  // later does not reopen every family. Not used with a test lookup.
  QHash<QString, QString> english_by_family;
};

DisplayNameState& display_name_state() {
  static DisplayNameState state;
  return state;
}

void invalidate_display_names(bool notify) {
  display_name_state().valid = false;
  if (notify) {
    emit FontDisplayNamesNotifier::instance().changed();
  }
}

}  // namespace

bool stored_show_font_names_in_english() {
  return app_settings().value(QLatin1String(kShowFontNamesInEnglishKey), false).toBool();
}

void set_stored_show_font_names_in_english(bool enabled) {
  if (stored_show_font_names_in_english() == enabled) {
    return;
  }
  {
    auto settings = app_settings();
    settings.setValue(QLatin1String(kShowFontNamesInEnglishKey), enabled);
  }
  invalidate_display_names(true);
}

bool font_name_is_latin(const QString& name) {
  const auto code_points = name.toUcs4();
  return std::all_of(code_points.cbegin(), code_points.cend(), [](char32_t code_point) {
    switch (QChar::script(code_point)) {
      case QChar::Script_Latin:
      case QChar::Script_Common:
      case QChar::Script_Inherited:
        return true;
      default:
        return false;
    }
  });
}

QString english_family_name_from_name_table(const QByteArray& name_table, const QString& listed_family) {
  return english_name_answer(name_table, listed_family).english;
}

FontDisplayNames FontDisplayNames::build(const QStringList& families, const Lookup& english_name) {
  FontDisplayNames names;
  if (!english_name) {
    return names;
  }
  QSet<QString> taken;
  taken.reserve(families.size());
  for (const auto& family : families) {
    taken.insert(family.toCaseFolded());
  }
  for (const auto& family : families) {
    const auto shown = english_name(family).simplified();
    if (shown.isEmpty() || shown == family) {
      continue;
    }
    const auto key = shown.toCaseFolded();
    if (taken.contains(key)) {
      continue;  // another family is listed or already shown under that name
    }
    taken.insert(key);
    names.shown_.insert(family, shown);
    names.family_.insert(key, family);
  }
  return names;
}

QString FontDisplayNames::display_name(const QString& family) const {
  const auto found = shown_.constFind(family);
  return found == shown_.constEnd() ? family : *found;
}

QString FontDisplayNames::family_for_display_name(const QString& name) const {
  const auto found = family_.constFind(name.simplified().toCaseFolded());
  return found == family_.constEnd() ? name : *found;
}

bool FontDisplayNames::matches_search(const QString& family, const QString& text) const {
  if (family.contains(text, Qt::CaseInsensitive)) {
    return true;
  }
  const auto found = shown_.constFind(family);
  return found != shown_.constEnd() && found->contains(text, Qt::CaseInsensitive);
}

QStringList FontDisplayNames::sorted(const QStringList& families) const {
  if (shown_.isEmpty()) {
    return families;
  }
  std::vector<std::pair<QString, QString>> rows;  // shown name, family
  rows.reserve(static_cast<std::size_t>(families.size()));
  for (const auto& family : families) {
    rows.emplace_back(display_name(family), family);
  }
  std::stable_sort(rows.begin(), rows.end(), [](const auto& lhs, const auto& rhs) {
    const int order = lhs.first.compare(rhs.first, Qt::CaseInsensitive);
    return order != 0 ? order < 0 : lhs.first < rhs.first;
  });
  QStringList ordered;
  ordered.reserve(families.size());
  for (auto& row : rows) {
    ordered.append(std::move(row.second));
  }
  return ordered;
}

const FontDisplayNames& font_display_names() {
  auto& state = display_name_state();
  if (!state.connected && qGuiApp != nullptr) {
    state.connected = true;
    QObject::connect(qGuiApp, &QGuiApplication::fontDatabaseChanged, qGuiApp,
                     [] { invalidate_display_names(false); });
  }
  if (state.valid) {
    return state.table;
  }
  state.valid = true;  // before the lookups: a font load must not start a second build
  state.table = FontDisplayNames();
  if (!stored_show_font_names_in_english()) {
    return state.table;
  }
  const auto families = font_families_without_foundries(QFontDatabase::families());
  if (state.lookup_override) {
    state.table = FontDisplayNames::build(families, state.lookup_override);
    return state.table;
  }
  state.table = FontDisplayNames::build(families, [&state](const QString& family) {
    if (font_name_is_latin(family)) {
      return QString();
    }
    const auto cache_key = family + QLatin1Char('\n') + QFontDatabase::styles(family).join(QLatin1Char('\n'));
    if (const auto cached = state.english_by_family.constFind(cache_key);
        cached != state.english_by_family.constEnd()) {
      return *cached;
    }
    const auto english = english_name_from_font(family);
    state.english_by_family.insert(cache_key, english);
    return english;
  });
  return state.table;
}

FontDisplayNamesNotifier& FontDisplayNamesNotifier::instance() {
  static QPointer<FontDisplayNamesNotifier> notifier;
  if (notifier == nullptr) {
    notifier = new FontDisplayNamesNotifier;
    if (qGuiApp != nullptr) {
      notifier->setParent(qGuiApp);
    }
  }
  return *notifier;
}

ScopedFontDisplayNameLookup::ScopedFontDisplayNameLookup(FontDisplayNames::Lookup lookup) {
  display_name_state().lookup_override = std::move(lookup);
  invalidate_display_names(true);
}

ScopedFontDisplayNameLookup::~ScopedFontDisplayNameLookup() {
  display_name_state().lookup_override = nullptr;
  invalidate_display_names(true);
}

}  // namespace patchy::ui
