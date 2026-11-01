#include "ui/font_family_filters.hpp"

#include "ui/app_settings.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QMetaEnum>
#include <QRawFont>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>

#include <array>

namespace patchy::ui {

namespace {

constexpr std::array kStyleFilters{
    FontStyleFilter::all,        FontStyleFilter::serif,  FontStyleFilter::sans_serif,
    FontStyleFilter::slab_serif, FontStyleFilter::script, FontStyleFilter::decorative,
    FontStyleFilter::symbol,     FontStyleFilter::monospace, FontStyleFilter::variable,
};

// Cache file format (one family per line after the header): category token, 0/1 variable,
// the style signature, the family name; tab separated. A new header version discards old files.
const QString kCacheHeader = QStringLiteral("patchy-font-style-cache 1");

[[nodiscard]] QString category_token(FontStyleCategory category) {
  switch (category) {
    case FontStyleCategory::serif:
      return QStringLiteral("serif");
    case FontStyleCategory::sans_serif:
      return QStringLiteral("sans-serif");
    case FontStyleCategory::slab_serif:
      return QStringLiteral("slab-serif");
    case FontStyleCategory::script:
      return QStringLiteral("script");
    case FontStyleCategory::decorative:
      return QStringLiteral("decorative");
    case FontStyleCategory::symbol:
      return QStringLiteral("symbol");
    case FontStyleCategory::unknown:
      break;
  }
  return QStringLiteral("unknown");
}

[[nodiscard]] FontStyleCategory category_from_token(const QString& token) {
  for (const auto category : {FontStyleCategory::serif, FontStyleCategory::sans_serif, FontStyleCategory::slab_serif,
                              FontStyleCategory::script, FontStyleCategory::decorative, FontStyleCategory::symbol}) {
    if (token == category_token(category)) {
      return category;
    }
  }
  return FontStyleCategory::unknown;
}

[[nodiscard]] QString style_signature(const QString& family) {
  return QFontDatabase::styles(family).join(QLatin1Char('|'));
}

[[nodiscard]] std::span<const std::uint8_t> table_bytes(const QByteArray& table) {
  return {reinterpret_cast<const std::uint8_t*>(table.constData()), static_cast<std::size_t>(table.size())};
}

}  // namespace

std::span<const FontStyleFilter> font_style_filters() noexcept { return kStyleFilters; }

QString font_style_filter_token(FontStyleFilter filter) {
  switch (filter) {
    case FontStyleFilter::all:
      return QStringLiteral("all");
    case FontStyleFilter::serif:
      return QStringLiteral("serif");
    case FontStyleFilter::sans_serif:
      return QStringLiteral("sans-serif");
    case FontStyleFilter::slab_serif:
      return QStringLiteral("slab-serif");
    case FontStyleFilter::script:
      return QStringLiteral("script");
    case FontStyleFilter::decorative:
      return QStringLiteral("decorative");
    case FontStyleFilter::symbol:
      return QStringLiteral("symbol");
    case FontStyleFilter::monospace:
      return QStringLiteral("monospace");
    case FontStyleFilter::variable:
      return QStringLiteral("variable");
  }
  return QStringLiteral("all");
}

FontStyleFilter font_style_filter_from_token(const QString& token) {
  for (const auto filter : kStyleFilters) {
    if (token == font_style_filter_token(filter)) {
      return filter;
    }
  }
  return FontStyleFilter::all;
}

bool font_style_filter_reads_tables(FontStyleFilter filter) noexcept {
  return filter != FontStyleFilter::all && filter != FontStyleFilter::monospace;
}

QString writing_system_token(QFontDatabase::WritingSystem system) {
  if (system == QFontDatabase::Any) {
    return QStringLiteral("all");
  }
  const auto meta = QMetaEnum::fromType<QFontDatabase::WritingSystem>();
  const char* key = meta.valueToKey(static_cast<int>(system));
  return key != nullptr ? QString::fromLatin1(key) : QStringLiteral("all");
}

std::optional<QFontDatabase::WritingSystem> writing_system_from_token(const QString& token) {
  if (token.isEmpty() || token == QStringLiteral("all")) {
    return std::nullopt;
  }
  const auto meta = QMetaEnum::fromType<QFontDatabase::WritingSystem>();
  bool ok = false;
  const auto value = meta.keyToValue(token.toLatin1().constData(), &ok);
  if (!ok || value == QFontDatabase::Any) {
    return std::nullopt;
  }
  return static_cast<QFontDatabase::WritingSystem>(value);
}

QStringList stored_recent_font_families() {
  auto families = app_settings().value(QString::fromLatin1(kFontPickerRecentFamiliesKey)).toStringList();
  families.removeAll(QString());
  return families.mid(0, kMaxRecentFontFamilies);
}

void record_recent_font_family(const QString& family) {
  if (family.trimmed().isEmpty()) {
    return;
  }
  auto families = stored_recent_font_families();
  families.removeAll(family);
  families.prepend(family);
  auto settings = app_settings();
  settings.setValue(QString::fromLatin1(kFontPickerRecentFamiliesKey), families.mid(0, kMaxRecentFontFamilies));
}

FontStyleIndex& FontStyleIndex::instance() {
  static FontStyleIndex index;
  return index;
}

QString FontStyleIndex::cache_path() {
  return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
      .filePath(QStringLiteral("font-style-cache.txt"));
}

void FontStyleIndex::load() {
  if (loaded_) {
    return;
  }
  loaded_ = true;
  QFile file(cache_path());
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return;
  }
  QTextStream in(&file);
  in.setEncoding(QStringConverter::Utf8);
  if (in.readLine() != kCacheHeader) {
    return;
  }
  while (!in.atEnd()) {
    const auto fields = in.readLine().split(QLatin1Char('\t'));
    if (fields.size() != 4 || fields[3].isEmpty()) {
      continue;
    }
    Entry entry;
    entry.traits.category = category_from_token(fields[0]);
    entry.traits.variable = fields[1] == QStringLiteral("1");
    entry.signature = fields[2];
    entries_.insert(fields[3], entry);
  }
}

std::optional<FontFamilyTraits> FontStyleIndex::cached(const QString& family) {
  load();
  const auto found = entries_.find(family);
  if (found == entries_.end()) {
    return std::nullopt;
  }
  if (!found->checked) {
    // A family reinstalled with other faces is read again.
    if (found->signature != style_signature(family)) {
      entries_.erase(found);
      return std::nullopt;
    }
    found->checked = true;
  }
  return found->traits;
}

FontFamilyTraits FontStyleIndex::classify(const QString& family) {
  load();
  QFont font(family);
  font.setStyleStrategy(QFont::NoFontMerging);
  const auto raw = QRawFont::fromFont(font);
  FontFamilyTraits traits;
  FontStyleCategory os2_category = FontStyleCategory::unknown;
  if (raw.isValid()) {
    os2_category = font_style_category_from_os2(table_bytes(raw.fontTable("OS/2")));
    traits.variable = !raw.fontTable("fvar").isEmpty();
  }
  const auto systems = QFontDatabase::writingSystems(family);
  const bool symbol_only = systems.contains(QFontDatabase::Symbol) && !systems.contains(QFontDatabase::Latin);
  const auto utf8 = family.toUtf8();
  const auto name_class = font_class_for_name(std::string_view(utf8.constData(), static_cast<std::size_t>(utf8.size())));
  traits.category = resolve_font_style_category(os2_category, symbol_only, name_class);
  Entry entry;
  entry.traits = traits;
  entry.signature = style_signature(family);
  entry.checked = true;
  entries_.insert(family, entry);
  dirty_ = true;
  return traits;
}

bool FontStyleIndex::fixed_pitch(const QString& family) {
  const auto found = fixed_pitch_.constFind(family);
  if (found != fixed_pitch_.constEnd()) {
    return found.value();
  }
  return *fixed_pitch_.insert(family, QFontDatabase::isFixedPitch(family));
}

bool FontStyleIndex::matches(FontStyleFilter filter, const QString& family, const FontFamilyTraits& traits) {
  switch (filter) {
    case FontStyleFilter::all:
      return true;
    case FontStyleFilter::serif:
      return traits.category == FontStyleCategory::serif;
    case FontStyleFilter::sans_serif:
      return traits.category == FontStyleCategory::sans_serif;
    case FontStyleFilter::slab_serif:
      return traits.category == FontStyleCategory::slab_serif;
    case FontStyleFilter::script:
      return traits.category == FontStyleCategory::script;
    case FontStyleFilter::decorative:
      return traits.category == FontStyleCategory::decorative;
    case FontStyleFilter::symbol:
      return traits.category == FontStyleCategory::symbol;
    case FontStyleFilter::monospace:
      return fixed_pitch(family);
    case FontStyleFilter::variable:
      return traits.variable;
  }
  return true;
}

void FontStyleIndex::save() {
  if (!dirty_) {
    return;
  }
  const auto path = cache_path();
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    return;
  }
  QTextStream out(&file);
  out.setEncoding(QStringConverter::Utf8);
  out << kCacheHeader << '\n';
  for (auto it = entries_.cbegin(); it != entries_.cend(); ++it) {
    if (it.key().contains(QLatin1Char('\t')) || it.key().contains(QLatin1Char('\n'))) {
      continue;
    }
    out << category_token(it->traits.category) << '\t' << (it->traits.variable ? '1' : '0') << '\t'
        << it->signature << '\t' << it.key() << '\n';
  }
  out.flush();
  if (file.commit()) {
    dirty_ = false;
  }
}

void FontStyleIndex::clear() {
  entries_.clear();
  fixed_pitch_.clear();
  loaded_ = true;
  dirty_ = false;
  QFile::remove(cache_path());
}

}  // namespace patchy::ui
