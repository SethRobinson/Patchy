#include "ui/font_face_name_index.hpp"

#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHash>
#include <QObject>
#include <QRawFont>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

namespace patchy::ui {

QString compact_text_family_key(const QString& value) {
  QString compact;
  compact.reserve(value.size());
  for (const auto ch : value.toCaseFolded()) {
    if (ch.isLetterOrNumber()) {
      compact.append(ch);
    }
  }
  return compact;
}

QString font_family_without_foundry(const QString& family) {
  qsizetype end = family.size();
  while (end > 0 && family.at(end - 1) == QLatin1Char(' ')) {
    --end;
  }
  if (end == 0 || family.at(end - 1) != QLatin1Char(']')) {
    return family;
  }
  qsizetype open = family.lastIndexOf(QStringLiteral(" ["), end - 1);
  if (open <= 0) {
    return family;
  }
  while (open > 0 && family.at(open - 1) == QLatin1Char(' ')) {
    --open;
  }
  return open > 0 ? family.left(open) : family;
}

QStringList font_families_without_foundries(const QStringList& families) {
  QStringList plain;
  plain.reserve(families.size());
  QSet<QString> seen;
  seen.reserve(families.size());
  for (const auto& family : families) {
    auto name = font_family_without_foundry(family);
    if (name.isEmpty()) {
      continue;
    }
    const auto key = name.toCaseFolded();
    if (seen.contains(key)) {
      continue;
    }
    seen.insert(key);
    plain.append(std::move(name));
  }
  return plain;
}

namespace {

constexpr quint16 kPlatformUnicode = 0;
constexpr quint16 kPlatformMacintosh = 1;
constexpr quint16 kPlatformWindows = 3;
constexpr quint16 kWindowsEnglishUs = 0x0409;
constexpr quint16 kMacintoshEnglish = 0;

constexpr quint16 kNameFamily = 1;
constexpr quint16 kNameSubfamily = 2;
constexpr quint16 kNameFullName = 4;
constexpr quint16 kNamePostScript = 6;
constexpr quint16 kNameTypographicFamily = 16;
constexpr quint16 kNameTypographicSubfamily = 17;

constexpr int kNameHeaderBytes = 6;
constexpr int kNameRecordBytes = 12;
constexpr int kSfntHeaderBytes = 12;
constexpr int kSfntTableRecordBytes = 16;

quint16 read_u16(const QByteArray& bytes, int offset) {
  return static_cast<quint16>((static_cast<quint8>(bytes[offset]) << 8) | static_cast<quint8>(bytes[offset + 1]));
}

quint32 read_u32(const QByteArray& bytes, int offset) {
  return (static_cast<quint32>(read_u16(bytes, offset)) << 16) | read_u16(bytes, offset + 2);
}

void write_u16(QByteArray& bytes, int offset, quint16 value) {
  bytes[offset] = static_cast<char>(value >> 8);
  bytes[offset + 1] = static_cast<char>(value & 0xff);
}

void write_u32(QByteArray& bytes, int offset, quint32 value) {
  write_u16(bytes, offset, static_cast<quint16>(value >> 16));
  write_u16(bytes, offset + 2, static_cast<quint16>(value & 0xffff));
}

// The OpenType table checksum: the big-endian u32 sum over the table padded to four bytes.
quint32 sfnt_checksum(const QByteArray& bytes, int offset, int length) {
  quint32 sum = 0;
  for (int at = 0; at < length; at += 4) {
    quint32 word = 0;
    for (int byte = 0; byte < 4; ++byte) {
      word <<= 8;
      if (at + byte < length) {
        word |= static_cast<quint8>(bytes[offset + at + byte]);
      }
    }
    sum += word;
  }
  return sum;
}

// Lower ranks are preferred. Windows English is what DirectWrite, GDI and FreeType report;
// the Macintosh record is CoreText's choice and often a shorter, different family.
int name_record_rank(quint16 platform, quint16 language) {
  switch (platform) {
    case kPlatformWindows:
      return language == kWindowsEnglishUs ? 0 : 1;
    case kPlatformUnicode:
      return 2;
    case kPlatformMacintosh:
      return language == kMacintoshEnglish ? 3 : 4;
    default:
      return 5;
  }
}

QString decode_name_string(quint16 platform, const QByteArray& bytes) {
  if (platform == kPlatformWindows || platform == kPlatformUnicode) {
    QString text;
    text.reserve(bytes.size() / 2);
    for (int index = 0; index + 1 < bytes.size(); index += 2) {
      text.append(QChar(read_u16(bytes, index)));
    }
    return text;
  }
  // Macintosh Roman: the ASCII range, which every name this module compares lives in.
  return QString::fromLatin1(bytes);
}

struct TableLocation {
  int directory_entry{-1};  // offset of the 16-byte table record in the font
  int offset{0};
  int length{0};
};

// A single sfnt's table record for `tag`; a collection or a malformed directory yields none.
std::optional<TableLocation> locate_table(const QByteArray& font, const char* tag) {
  if (font.size() < kSfntHeaderBytes || std::strlen(tag) != 4) {
    return std::nullopt;
  }
  const auto version = read_u32(font, 0);
  if (version != 0x00010000U && version != 0x74727565U /* 'true' */ && version != 0x4F54544FU /* 'OTTO' */) {
    return std::nullopt;
  }
  const int count = read_u16(font, 4);
  if (kSfntHeaderBytes + count * kSfntTableRecordBytes > font.size()) {
    return std::nullopt;
  }
  for (int index = 0; index < count; ++index) {
    const int entry = kSfntHeaderBytes + index * kSfntTableRecordBytes;
    if (std::memcmp(font.constData() + entry, tag, 4) != 0) {
      continue;
    }
    const auto offset = read_u32(font, entry + 8);
    const auto length = read_u32(font, entry + 12);
    if (offset > static_cast<quint32>(font.size()) || length > static_cast<quint32>(font.size()) - offset) {
      return std::nullopt;
    }
    return TableLocation{entry, static_cast<int>(offset), static_cast<int>(length)};
  }
  return std::nullopt;
}

struct IndexedFace {
  QString family;
  QString style;
  OpenTypeFaceNames names;
};

struct FaceNameIndex {
  QSet<QString> indexed_families;
  std::vector<IndexedFace> faces;
  QHash<QString, std::size_t> by_postscript_name;
  QHash<QString, std::size_t> by_full_name;
  QHash<QString, std::vector<std::size_t>> by_family_name;
  QHash<QString, std::optional<FontFaceNameMatch>> results;
  bool complete{false};
  std::uint64_t generation{0};
};

// The editable-PDF save worker resolves text fonts too, so the index is shared between the GUI
// thread and workers: every access holds face_name_index_mutex (through LockedFaceNameIndex).
// fontDatabaseChanged only bumps a generation, so the GUI-thread slot never waits on a worker
// that is scanning fonts; the next lookup drops an index built for an older generation.
std::mutex face_name_index_mutex;
std::atomic<std::uint64_t> face_name_index_generation{0};

struct LockedFaceNameIndex {
  std::unique_lock<std::mutex> lock;
  FaceNameIndex& index;
};

LockedFaceNameIndex face_name_index() {
  static FaceNameIndex index;
  static std::once_flag invalidation_connected;
  std::call_once(invalidation_connected, [] {
    QObject::connect(qGuiApp, &QGuiApplication::fontDatabaseChanged, qGuiApp,
                     [] { face_name_index_generation.fetch_add(1, std::memory_order_acq_rel); });
  });
  std::unique_lock lock(face_name_index_mutex);
  const auto generation = face_name_index_generation.load(std::memory_order_acquire);
  if (index.generation != generation) {
    index = FaceNameIndex{};
    index.generation = generation;
  }
  return LockedFaceNameIndex{std::move(lock), index};
}

void add_family_name_key(FaceNameIndex& index, const QString& name, std::size_t face) {
  const auto key = compact_text_family_key(name);
  if (key.isEmpty()) {
    return;
  }
  auto& faces = index.by_family_name[key];
  if (std::find(faces.begin(), faces.end(), face) == faces.end()) {
    faces.push_back(face);
  }
}

// Opens every face the database lists for `family` and records its names. A face whose font
// resolves to another family (a family without the default script, which Qt answers with a
// fallback) is skipped: its name table would describe the fallback, not the listed face.
void index_family(FaceNameIndex& index, const QString& family) {
  if (family.isEmpty() || index.indexed_families.contains(family)) {
    return;
  }
  index.indexed_families.insert(family);
  auto styles = QFontDatabase::styles(family);
  if (styles.isEmpty()) {
    styles.append(QString());
  }
  for (const auto& style : styles) {
    const auto raw = QRawFont::fromFont(QFontDatabase::font(family, style, 12));
    if (!raw.isValid() || raw.familyName().compare(family, Qt::CaseInsensitive) != 0) {
      continue;
    }
    const auto names = parse_opentype_face_names(raw.fontTable("name"));
    if (!names.has_value()) {
      continue;
    }
    const auto face = index.faces.size();
    index.faces.push_back(IndexedFace{family, style, *names});
    // The first face keeps a duplicated name.
    if (const auto key = compact_text_family_key(names->postscript_name);
        !key.isEmpty() && !index.by_postscript_name.contains(key)) {
      index.by_postscript_name.insert(key, face);
    }
    if (const auto key = compact_text_family_key(names->full_name);
        !key.isEmpty() && !index.by_full_name.contains(key)) {
      index.by_full_name.insert(key, face);
    }
    add_family_name_key(index, names->family, face);
    add_family_name_key(index, names->typographic_family, face);
  }
}

// How plain a subfamily name is: the regular face first, then the fewest style words.
int subfamily_plainness(const OpenTypeFaceNames& names) {
  const auto subfamily = names.subfamily.isEmpty() ? names.typographic_subfamily : names.subfamily;
  const auto key = compact_text_family_key(subfamily);
  static constexpr std::array<const char*, 6> kRegularNames{"regular", "normal", "roman", "plain", "book", "medium"};
  for (const auto* regular : kRegularNames) {
    if (key == QLatin1String(regular)) {
      return 0;
    }
  }
  return 1 + static_cast<int>(subfamily.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts).size());
}

std::optional<FontFaceNameMatch> find_in_index(const FaceNameIndex& index, const QString& key) {
  if (const auto it = index.by_postscript_name.constFind(key); it != index.by_postscript_name.constEnd()) {
    const auto& face = index.faces[*it];
    return FontFaceNameMatch{face.family, face.style};
  }
  if (const auto it = index.by_full_name.constFind(key); it != index.by_full_name.constEnd()) {
    const auto& face = index.faces[*it];
    return FontFaceNameMatch{face.family, face.style};
  }
  const auto it = index.by_family_name.constFind(key);
  if (it == index.by_family_name.constEnd() || it->empty()) {
    return std::nullopt;
  }
  const auto best = std::min_element(it->begin(), it->end(), [&index](std::size_t lhs, std::size_t rhs) {
    return subfamily_plainness(index.faces[lhs].names) < subfamily_plainness(index.faces[rhs].names);
  });
  const auto& face = index.faces[*best];
  return FontFaceNameMatch{face.family, face.style};
}

}  // namespace

QList<OpenTypeNameRecord> opentype_name_records(const QByteArray& name_table) {
  QList<OpenTypeNameRecord> records;
  if (name_table.size() < kNameHeaderBytes) {
    return records;
  }
  const int count = read_u16(name_table, 2);
  const int string_offset = read_u16(name_table, 4);
  if (kNameHeaderBytes + count * kNameRecordBytes > name_table.size()) {
    return records;
  }
  for (int record = 0; record < count; ++record) {
    const int at = kNameHeaderBytes + record * kNameRecordBytes;
    OpenTypeNameRecord entry;
    entry.platform = read_u16(name_table, at);
    entry.encoding = read_u16(name_table, at + 2);
    entry.language = read_u16(name_table, at + 4);
    entry.name_id = read_u16(name_table, at + 6);
    const int length = read_u16(name_table, at + 8);
    const int offset = read_u16(name_table, at + 10);
    const int start = string_offset + offset;
    if (length <= 0 || start < 0 || start + length > name_table.size()) {
      continue;
    }
    entry.text = decode_name_string(entry.platform, name_table.mid(start, length)).trimmed();
    records.push_back(std::move(entry));
  }
  return records;
}

std::optional<OpenTypeFaceNames> parse_opentype_face_names(const QByteArray& name_table) {
  struct Best {
    int rank{99};
    QString text;
  };
  QHash<quint16, Best> best;
  for (const auto& record : opentype_name_records(name_table)) {
    if (record.name_id != kNameFamily && record.name_id != kNameSubfamily && record.name_id != kNameFullName &&
        record.name_id != kNamePostScript && record.name_id != kNameTypographicFamily &&
        record.name_id != kNameTypographicSubfamily) {
      continue;
    }
    const auto rank = name_record_rank(record.platform, record.language);
    if (rank >= 5 || record.text.isEmpty()) {
      continue;
    }
    auto& slot = best[record.name_id];
    if (rank < slot.rank) {
      slot.rank = rank;
      slot.text = record.text;
    }
  }
  OpenTypeFaceNames names;
  names.family = best.value(kNameFamily).text;
  names.subfamily = best.value(kNameSubfamily).text;
  names.full_name = best.value(kNameFullName).text;
  names.postscript_name = best.value(kNamePostScript).text;
  names.typographic_family = best.value(kNameTypographicFamily).text;
  names.typographic_subfamily = best.value(kNameTypographicSubfamily).text;
  if (names.family.isEmpty() && names.full_name.isEmpty() && names.postscript_name.isEmpty()) {
    return std::nullopt;
  }
  return names;
}

std::optional<QString> variable_instance_postscript_name(const QByteArray& fvar_table,
                                                       const QByteArray& name_table, const QString& subfamily) {
  constexpr int kFvarHeaderBytes = 16;
  constexpr quint16 kNameVariationsPrefix = 25;
  const auto wanted = compact_text_family_key(subfamily);
  if (fvar_table.size() < kFvarHeaderBytes || wanted.isEmpty()) {
    return std::nullopt;
  }
  const int axes_offset = read_u16(fvar_table, 4);
  const int axis_count = read_u16(fvar_table, 8);
  const int axis_size = read_u16(fvar_table, 10);
  const int instance_count = read_u16(fvar_table, 12);
  const int instance_size = read_u16(fvar_table, 14);
  const int coordinates_end = 4 + axis_count * 4;
  if (axes_offset < kFvarHeaderBytes || axis_size < 20 || instance_size < coordinates_end) {
    return std::nullopt;
  }
  QHash<quint16, std::pair<int, QString>> best;
  for (const auto& record : opentype_name_records(name_table)) {
    const auto rank = name_record_rank(record.platform, record.language);
    if (rank >= 5 || record.text.isEmpty()) {
      continue;
    }
    const auto found = best.find(record.name_id);
    if (found == best.end() || rank < found->first) {
      best.insert(record.name_id, {rank, record.text});
    }
  }
  const auto name_for = [&best](quint16 id) { return best.contains(id) ? best.value(id).second : QString(); };
  const auto postscript_chars = [](const QString& text) {
    QString out;
    for (const auto character : text) {
      if (character.unicode() < 128U && character.isLetterOrNumber()) out += character;
    }
    return out;
  };
  const int instances_at = axes_offset + axis_count * axis_size;
  for (int instance = 0; instance < instance_count; ++instance) {
    const int at = instances_at + instance * instance_size;
    if (at + instance_size > fvar_table.size()) {
      break;
    }
    const auto instance_subfamily = name_for(read_u16(fvar_table, at));
    if (compact_text_family_key(instance_subfamily) != wanted) {
      continue;
    }
    if (instance_size >= coordinates_end + 2) {
      const auto id = read_u16(fvar_table, at + coordinates_end);
      if (id != 0xFFFFU) {
        if (const auto own = name_for(id); !own.isEmpty()) return own;
      }
    }
    auto prefix = name_for(kNameVariationsPrefix);
    if (prefix.isEmpty()) prefix = name_for(kNameTypographicFamily);
    if (prefix.isEmpty()) prefix = name_for(kNameFamily);
    prefix = postscript_chars(prefix);
    const auto face = postscript_chars(instance_subfamily);
    if (prefix.isEmpty() || face.isEmpty()) {
      return std::nullopt;
    }
    return prefix + QLatin1Char('-') + face;
  }
  return std::nullopt;
}

QByteArray opentype_table(const QByteArray& font, const char* tag) {
  const auto location = locate_table(font, tag);
  return location.has_value() ? font.mid(location->offset, location->length) : QByteArray();
}

std::optional<QByteArray> windows_named_font_data(const QByteArray& font) {
  const auto location = locate_table(font, "name");
  if (!location.has_value() || location->length < kNameHeaderBytes) {
    return std::nullopt;
  }
  const auto table = font.mid(location->offset, location->length);
  const int count = read_u16(table, 2);
  const int string_offset = read_u16(table, 4);
  if (kNameHeaderBytes + count * kNameRecordBytes > table.size() || string_offset > table.size()) {
    return std::nullopt;
  }
  QString windows_family;
  QString macintosh_family;
  int windows_rank = 99;
  int macintosh_rank = 99;
  bool has_windows_records = false;
  for (const auto& record : opentype_name_records(table)) {
    if (record.platform == kPlatformWindows) {
      has_windows_records = true;
    }
    if (record.name_id != kNameFamily || record.text.isEmpty()) {
      continue;
    }
    const auto rank = name_record_rank(record.platform, record.language);
    if (record.platform == kPlatformWindows && rank < windows_rank) {
      windows_rank = rank;
      windows_family = record.text;
    } else if (record.platform == kPlatformMacintosh && rank < macintosh_rank) {
      macintosh_rank = rank;
      macintosh_family = record.text;
    }
  }
  if (!has_windows_records || windows_family.isEmpty() || macintosh_family.isEmpty() ||
      compact_text_family_key(windows_family) == compact_text_family_key(macintosh_family)) {
    return std::nullopt;
  }

  // Keep every non-Macintosh record with its string offset (the string storage is copied
  // verbatim), so the rebuilt table is the old one minus twelve bytes per dropped record.
  QByteArray rebuilt;
  int kept = 0;
  for (int record = 0; record < count; ++record) {
    const int at = kNameHeaderBytes + record * kNameRecordBytes;
    if (read_u16(table, at) == kPlatformMacintosh) {
      continue;
    }
    rebuilt.append(table.constData() + at, kNameRecordBytes);
    ++kept;
  }
  if (kept == count) {
    return std::nullopt;
  }
  QByteArray header(kNameHeaderBytes, '\0');
  write_u16(header, 0, read_u16(table, 0));
  write_u16(header, 2, static_cast<quint16>(kept));
  write_u16(header, 4, static_cast<quint16>(kNameHeaderBytes + kept * kNameRecordBytes));
  rebuilt.prepend(header);
  rebuilt.append(table.constData() + string_offset, table.size() - string_offset);
  if (rebuilt.size() > table.size()) {
    return std::nullopt;
  }

  QByteArray result = font;
  std::memcpy(result.data() + location->offset, rebuilt.constData(), static_cast<std::size_t>(rebuilt.size()));
  std::memset(result.data() + location->offset + rebuilt.size(), 0,
              static_cast<std::size_t>(table.size() - rebuilt.size()));
  write_u32(result, location->directory_entry + 4, sfnt_checksum(result, location->offset, rebuilt.size()));
  write_u32(result, location->directory_entry + 12, static_cast<quint32>(rebuilt.size()));
  // The whole-font checksum adjustment in `head` (offset 8): 0xB1B0AFBA minus the sum of the
  // file with that field zeroed.
  if (const auto head = locate_table(result, "head"); head.has_value() && head->length >= 12) {
    write_u32(result, head->offset + 8, 0);
    const auto whole = sfnt_checksum(result, 0, result.size());
    write_u32(result, head->offset + 8, 0xB1B0AFBAU - whole);
  }
  return result;
}

int add_application_font_by_windows_names(const QString& path) {
#ifdef Q_OS_MACOS
  if (QFile file(path); file.open(QIODevice::ReadOnly)) {
    if (const auto renamed = windows_named_font_data(file.readAll()); renamed.has_value()) {
      const int id = QFontDatabase::addApplicationFontFromData(*renamed);
      if (id >= 0) {
        return id;
      }
    }
  }
#endif
  return QFontDatabase::addApplicationFont(path);
}

std::optional<FontFaceNameMatch> font_face_for_name_table_name(const QString& name) {
  const auto key = compact_text_family_key(name);
  if (key.isEmpty() || qGuiApp == nullptr) {
    return std::nullopt;
  }
  // Held for the whole lookup: index_family appends faces that find_in_index reads.
  auto locked = face_name_index();
  auto& index = locked.index;
  if (const auto cached = index.results.constFind(key); cached != index.results.constEnd()) {
    return *cached;
  }
  const auto families = font_families_without_foundries(QFontDatabase::families());
  // Families whose listed name and the request are prefixes of each other: CoreText's
  // "Futura" for a "Futura BdCn BT" request, or the reverse on a machine that lists the
  // Windows name. The reverse direction needs a few characters so a short request does not
  // open half the database.
  for (const auto& family : families) {
    const auto family_key = compact_text_family_key(family);
    if (family_key.isEmpty()) {
      continue;
    }
    if (key.startsWith(family_key) || (key.size() >= 4 && family_key.startsWith(key))) {
      index_family(index, family);
    }
  }
  auto match = find_in_index(index, key);
#ifndef Q_OS_WIN
  // DirectWrite has already searched the whole system collection by every name on Windows,
  // and Qt's Windows database lists application fonts by their GDI names, so only the
  // prefix scan is worth its cost there. Elsewhere nothing else answers, so a miss opens
  // every family once per database generation.
  if (!match.has_value() && !index.complete) {
    for (const auto& family : families) {
      index_family(index, family);
    }
    index.complete = true;
    match = find_in_index(index, key);
  }
#endif
  index.results.insert(key, match);
  return match;
}

}  // namespace patchy::ui
