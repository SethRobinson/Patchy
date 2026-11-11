#include "ui/text_language_font.hpp"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QRawFont>

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

const QString kLanguageFamilyPrefix = QStringLiteral("PatchyLang ");

quint16 read_u16(const QByteArray& bytes, qsizetype at) {
  return static_cast<quint16>((static_cast<quint8>(bytes[at]) << 8) | static_cast<quint8>(bytes[at + 1]));
}

void write_u16(QByteArray& bytes, qsizetype at, quint16 value) {
  bytes[at] = static_cast<char>(value >> 8);
  bytes[at + 1] = static_cast<char>(value & 0xFFU);
}

void write_u32(QByteArray& bytes, qsizetype at, quint32 value) {
  for (int i = 0; i < 4; ++i) {
    bytes[at + i] = static_cast<char>((value >> (24 - 8 * i)) & 0xFFU);
  }
}

void append_u16(QByteArray& bytes, quint16 value) {
  bytes.append(static_cast<char>(value >> 8));
  bytes.append(static_cast<char>(value & 0xFFU));
}

void append_u32(QByteArray& bytes, quint32 value) {
  for (int i = 0; i < 4; ++i) {
    bytes.append(static_cast<char>((value >> (24 - 8 * i)) & 0xFFU));
  }
}

// The sfnt table checksum: the big-endian uint32 sum of the bytes, zero-padded to four.
quint32 sfnt_checksum(const QByteArray& bytes, qsizetype offset, qsizetype length) {
  quint32 sum = 0;
  for (qsizetype at = 0; at < length; at += 4) {
    quint32 word = 0;
    for (qsizetype i = 0; i < 4; ++i) {
      word <<= 8;
      if (at + i < length) {
        word |= static_cast<quint8>(bytes[offset + at + i]);
      }
    }
    sum += word;
  }
  return sum;
}

// Every table a face can carry that a renderer or shaper reads (OpenType, AAT and Graphite).
// QRawFont answers one tag at a time and cannot list them, so the copy asks for each. DSIG is left
// out: the copy's signature would be wrong.
constexpr std::array<const char*, 61> kFaceTables{
    "BASE", "CBDT", "CBLC", "CFF ", "CFF2", "cmap", "COLR", "CPAL", "cvar", "cvt ", "EBDT", "EBLC", "EBSC",
    "fpgm", "gasp", "GDEF", "glyf", "GPOS", "GSUB", "hdmx", "head", "hhea", "hmtx", "HVAR", "JSTF", "kern",
    "loca", "LTSH", "MATH", "maxp", "MERG", "meta", "MVAR", "name", "OS/2", "PCLT", "post", "prep", "sbix",
    "STAT", "SVG ", "VDMX", "vhea", "vmtx", "VORG", "VVAR", "morx", "mort", "kerx", "feat", "trak", "ankr",
    "just", "opbd", "prop", "bsln", "Silf", "Glat", "Gloc", "Feat", "Sill"};

// A `name` table naming the copy's family and style, on the Macintosh (Roman) and Windows
// (Unicode BMP, English US) platforms, so every font database registers it under `family`.
QByteArray language_copy_name_table(const QString& family, const QString& style) {
  const auto postscript = [](QString text) {
    text.remove(QLatin1Char(' '));
    return text;
  };
  const std::array<std::pair<quint16, QString>, 4> names{{
      {1, family},
      {2, style},
      {4, family + QLatin1Char(' ') + style},
      {6, postscript(family) + QLatin1Char('-') + postscript(style)},
  }};
  struct Record {
    quint16 platform;
    quint16 encoding;
    quint16 language;
    quint16 name_id;
    QByteArray text;
  };
  std::vector<Record> records;
  for (const auto& [id, text] : names) {
    records.push_back(Record{1, 0, 0, id, text.toLatin1()});
  }
  for (const auto& [id, text] : names) {
    QByteArray utf16;
    for (const auto character : text) {
      append_u16(utf16, character.unicode());
    }
    records.push_back(Record{3, 1, 0x0409, id, utf16});
  }
  QByteArray table;
  append_u16(table, 0);
  append_u16(table, static_cast<quint16>(records.size()));
  append_u16(table, static_cast<quint16>(6 + 12 * records.size()));
  QByteArray strings;
  for (const auto& record : records) {
    append_u16(table, record.platform);
    append_u16(table, record.encoding);
    append_u16(table, record.language);
    append_u16(table, record.name_id);
    append_u16(table, static_cast<quint16>(record.text.size()));
    append_u16(table, static_cast<quint16>(strings.size()));
    strings += record.text;
  }
  return table + strings;
}

// An sfnt holding `tables` (tag -> bytes), sorted by tag as the format asks, with every table
// checksum and the head checksum adjustment computed.
QByteArray sfnt_from_tables(const std::map<QByteArray, QByteArray>& tables) {
  const bool cff = tables.contains(QByteArrayLiteral("CFF ")) || tables.contains(QByteArrayLiteral("CFF2"));
  const auto count = static_cast<quint16>(tables.size());
  quint16 entry_selector = 0;
  while ((2U << entry_selector) <= count) {
    ++entry_selector;
  }
  const auto search_range = static_cast<quint16>(16U << entry_selector);
  QByteArray font;
  append_u32(font, cff ? 0x4F54544FU : 0x00010000U);
  append_u16(font, count);
  append_u16(font, search_range);
  append_u16(font, entry_selector);
  append_u16(font, static_cast<quint16>(count * 16U - search_range));
  const qsizetype directory = font.size();
  font.append(QByteArray(16 * count, '\0'));
  qsizetype head_offset = -1;
  qsizetype entry = directory;
  for (const auto& [tag, data] : tables) {
    const qsizetype offset = font.size();
    font += data;
    while (font.size() % 4 != 0) {
      font.append('\0');
    }
    if (tag == QByteArrayLiteral("head")) {
      head_offset = offset;
      write_u32(font, offset + 8, 0);
    }
    std::memcpy(font.data() + entry, tag.constData(), 4);
    write_u32(font, entry + 4, sfnt_checksum(font, offset, data.size()));
    write_u32(font, entry + 8, static_cast<quint32>(offset));
    write_u32(font, entry + 12, static_cast<quint32>(data.size()));
    entry += 16;
  }
  if (head_offset >= 0) {
    write_u32(font, head_offset + 8, 0xB1B0AFBAU - sfnt_checksum(font, 0, font.size()));
  }
  return font;
}

QString ascii_style_name(const QString& style) {
  QString out;
  for (const auto character : style) {
    if (character.unicode() >= 0x20U && character.unicode() < 0x7FU) {
      out += character;
    }
  }
  out = out.simplified();
  return out.isEmpty() ? QStringLiteral("Regular") : out;
}

struct LanguageCopyCache {
  QMutex mutex;
  QHash<QString, QString> families;  // face + tags -> the copy's family, or empty for "none"
  int next_id{1};
};

LanguageCopyCache& language_copy_cache() {
  static LanguageCopyCache cache;
  return cache;
}

// The family of `raw`'s language copy for `tags`, registered on first use; empty when the face
// lists none of them, is a variable font, or will not register.
QString language_copy_family(const QRawFont& raw, const QList<QByteArray>& tags) {
  if (!raw.isValid()) {
    return {};
  }
  const auto head = raw.fontTable("head");
  if (head.size() < 54) {
    return {};
  }
  const auto key = raw.familyName() + QLatin1Char('|') + raw.styleName() + QLatin1Char('|') +
                   QString::number(raw.weight()) + QLatin1Char('|') + QString::number(static_cast<int>(raw.style())) +
                   QLatin1Char('|') + QString::fromLatin1(tags.join(',')) + QLatin1Char('|') +
                   QString::fromLatin1(head.toHex());
  auto& cache = language_copy_cache();
  int id = 0;
  {
    const QMutexLocker lock(&cache.mutex);
    if (const auto found = cache.families.constFind(key); found != cache.families.constEnd()) {
      return *found;
    }
    id = cache.next_id++;
  }
  // Built and registered unlocked: registering emits fontDatabaseChanged, whose handlers may
  // render text and come back here.
  QString family;
  if (raw.fontTable("fvar").isEmpty()) {
    std::map<QByteArray, QByteArray> tables;
    for (const auto* tag : kFaceTables) {
      if (auto data = raw.fontTable(tag); !data.isEmpty()) {
        tables.emplace(QByteArray(tag, 4), std::move(data));
      }
    }
    bool changed = false;
    for (const char* layout : {"GSUB", "GPOS"}) {
      const auto found = tables.find(QByteArray(layout, 4));
      if (found == tables.end()) {
        continue;
      }
      if (auto rewritten = layout_table_for_language(found->second, tags); rewritten.has_value()) {
        found->second = std::move(*rewritten);
        changed = true;
      }
    }
    if (changed && tables.contains(QByteArrayLiteral("cmap")) && tables.contains(QByteArrayLiteral("maxp"))) {
      const auto wanted = kLanguageFamilyPrefix + QString::fromLatin1(tags.front().trimmed()) + QLatin1Char(' ') +
                          QString::number(id);
      tables[QByteArrayLiteral("name")] = language_copy_name_table(wanted, ascii_style_name(raw.styleName()));
      const int registered = QFontDatabase::addApplicationFontFromData(sfnt_from_tables(tables));
      if (registered >= 0) {
        family = QFontDatabase::applicationFontFamilies(registered).value(0);
      }
    }
  }
  const QMutexLocker lock(&cache.mutex);
  // Another thread may have built the same copy meanwhile; both register, the first answer stays.
  return *cache.families.insert(key, cache.families.value(key, family));
}

}  // namespace

QList<QByteArray> opentype_language_tags_for_photoshop_language(int language) {
  // Txt2 /38 numbers (docs/txt2.md, "Languages") to the tags HarfBuzz tries for the language.
  switch (language) {
    case 1: return {QByteArrayLiteral("FIN ")};
    case 2:
    case 3: return {QByteArrayLiteral("FRA ")};
    case 5:
    case 42:
    case 44: return {QByteArrayLiteral("DEU ")};
    case 7: return {QByteArrayLiteral("ITA ")};
    case 8: return {QByteArrayLiteral("NOR ")};
    case 9: return {QByteArrayLiteral("NYN "), QByteArrayLiteral("NOR ")};
    case 10:
    case 11: return {QByteArrayLiteral("PTG ")};
    case 12: return {QByteArrayLiteral("ESP ")};
    case 13: return {QByteArrayLiteral("SVE ")};
    case 16: return {QByteArrayLiteral("DAN ")};
    case 17: return {QByteArrayLiteral("CAT ")};
    case 18: return {QByteArrayLiteral("RUS ")};
    case 20: return {QByteArrayLiteral("BGR ")};
    case 21: return {QByteArrayLiteral("SRB ")};
    case 22: return {QByteArrayLiteral("CSY ")};
    case 23: return {QByteArrayLiteral("PLK ")};
    case 24: return {QByteArrayLiteral("ROM "), QByteArrayLiteral("MOL ")};
    case 25: return {QByteArrayLiteral("ELL ")};
    case 26: return {QByteArrayLiteral("TRK ")};
    case 27: return {QByteArrayLiteral("ISL ")};
    case 28: return {QByteArrayLiteral("HUN ")};
    case 30: return {QByteArrayLiteral("JAN ")};
    case 31: return {QByteArrayLiteral("HRV ")};
    case 32: return {QByteArrayLiteral("IWR ")};
    case 33: return {QByteArrayLiteral("SLV ")};
    case 36: return {QByteArrayLiteral("LTH ")};
    case 37: return {QByteArrayLiteral("LVI ")};
    case 38: return {QByteArrayLiteral("SKY ")};
    case 39: return {QByteArrayLiteral("ARA ")};
    case 40: return {QByteArrayLiteral("ETI ")};
    case 41: return {QByteArrayLiteral("FAR ")};
    case 43: return {QByteArrayLiteral("NLD ")};
    case 46: return {QByteArrayLiteral("THA ")};
    case 47: return {QByteArrayLiteral("VIT ")};
    case 49: return {QByteArrayLiteral("HIN ")};
    case 51: return {QByteArrayLiteral("BEN ")};
    case 62: return {QByteArrayLiteral("IND ")};
    case 63: return {QByteArrayLiteral("KOR ")};
    default: return {};  // English (0, 14, 45), unset, Chinese (29), unknown
  }
}

std::optional<QByteArray> layout_table_for_language(const QByteArray& table, const QList<QByteArray>& tags) {
  if (table.size() < 10 || tags.isEmpty()) {
    return std::nullopt;
  }
  const qsizetype script_list = read_u16(table, 4);
  if (script_list == 0 || script_list + 2 > table.size()) {
    return std::nullopt;
  }
  const int script_count = read_u16(table, script_list);
  if (script_list + 2 + 6 * script_count > table.size()) {
    return std::nullopt;
  }
  QByteArray result = table;
  bool matched = false;
  std::vector<qsizetype> done;
  for (int index = 0; index < script_count; ++index) {
    const qsizetype script = script_list + read_u16(table, script_list + 2 + 6 * index + 4);
    if (std::find(done.begin(), done.end(), script) != done.end()) {
      continue;  // two script records may share one Script table
    }
    done.push_back(script);
    if (script + 4 > table.size()) {
      return std::nullopt;
    }
    const int lang_count = read_u16(table, script + 2);
    if (script + 4 + 6 * lang_count > table.size()) {
      return std::nullopt;
    }
    std::optional<quint16> chosen;
    for (const auto& tag : tags) {
      for (int lang = 0; lang < lang_count && !chosen.has_value(); ++lang) {
        const qsizetype record = script + 4 + 6 * lang;
        if (tag.size() == 4 && std::memcmp(table.constData() + record, tag.constData(), 4) == 0) {
          chosen = read_u16(table, record + 4);
        }
      }
      if (chosen.has_value()) {
        break;
      }
    }
    if (chosen.has_value() && *chosen != 0) {
      write_u16(result, script, *chosen);  // the LangSys offset is relative to the Script table too
      matched = true;
    }
    write_u16(result, script + 2, 0);
  }
  if (!matched) {
    return std::nullopt;
  }
  return result;
}

bool text_language_keeps_contextual_alternates(int language) {
  return language == 39 || language == 41;
}

bool is_text_language_font_family(const QString& family) {
  return family.startsWith(kLanguageFamilyPrefix);
}

QStringList text_language_font_families(const QFont& font, int language) {
  const auto tags = opentype_language_tags_for_photoshop_language(language);
  if (tags.isEmpty()) {
    return {};
  }
  // Every layout of a tagged run asks (the caret layout on each keystroke), so the answer is kept
  // per request until the font database changes.
  static QMutex answers_mutex;
  static QHash<QString, QStringList> answers;
  static std::once_flag invalidation_connected;
  if (qGuiApp != nullptr) {
    std::call_once(invalidation_connected, [] {
      QObject::connect(qGuiApp, &QGuiApplication::fontDatabaseChanged, qGuiApp, [] {
        const QMutexLocker lock(&answers_mutex);
        answers.clear();
      });
    });
  }
  const auto key = font.families().join(QLatin1Char(',')) +QLatin1Char('|') + font.family() + QLatin1Char('|') +
                   font.styleName() + QLatin1Char('|') + QString::number(font.weight()) + QLatin1Char('|') +
                   QString::number(static_cast<int>(font.style())) + QLatin1Char('|') +
                   QString::number(static_cast<int>(font.stretch())) + QLatin1Char('|') + QString::number(language);
  {
    const QMutexLocker lock(&answers_mutex);
    if (const auto found = answers.constFind(key); found != answers.constEnd()) {
      return *found;
    }
  }
  auto families = font.families();
  if (families.isEmpty()) {
    families = QStringList{font.family()};
  }
  bool replaced = false;
  for (auto& family : families) {
    if (family.isEmpty() || is_text_language_font_family(family) || !QFontDatabase::hasFamily(family)) {
      continue;
    }
    auto single = font;
    single.setFamilies(QStringList{family});
    const auto copy = language_copy_family(QRawFont::fromFont(single), tags);
    if (!copy.isEmpty()) {
      family = copy;
      replaced = true;
    }
  }
  if (!replaced) {
    families.clear();
  }
  const QMutexLocker lock(&answers_mutex);
  answers.insert(key, families);
  return families;
}

}  // namespace patchy::ui
