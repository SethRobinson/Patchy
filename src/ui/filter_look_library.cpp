#include "ui/filter_look_library.hpp"

#include "psd/psd_layer_effects.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace patchy::ui {

namespace {

constexpr qint64 kMaximumLookFileBytes = 1024LL * 1024LL;
constexpr qsizetype kMaximumLookNameBytes = 512;
constexpr qsizetype kMaximumFilterIdBytes = 512;
constexpr qsizetype kMaximumParameterKeyBytes = 256;
constexpr qsizetype kMaximumStringValueBytes = 4096;
constexpr qsizetype kMaximumRecipeEntries = 64;
constexpr qsizetype kMaximumParametersPerEntry = 64;
// JSON numbers are interoperably exact only through 53 bits. Filter catalog
// integers are much smaller; reject values that cannot round-trip exactly.
constexpr double kMaximumExactJsonInteger = 9007199254740991.0;

void set_error(FilterLookLibraryError* destination,
               FilterLookLibraryError value) {
  if (destination != nullptr) {
    *destination = value;
  }
}

[[nodiscard]] bool bounded_text(const QString& value, qsizetype maximum_bytes,
                                bool allow_empty = false) {
  if ((!allow_empty && value.isEmpty()) || value.contains(QChar::Null)) {
    return false;
  }
  return value.toUtf8().size() <= maximum_bytes;
}

[[nodiscard]] std::optional<QString> qstring_from_utf8_strict(
    std::string_view value, qsizetype maximum_bytes, bool allow_empty = false) {
  if (value.size() > static_cast<std::size_t>(maximum_bytes) ||
      (!allow_empty && value.empty()) ||
      value.find('\0') != std::string_view::npos) {
    return std::nullopt;
  }
  const QByteArray bytes(value.data(), static_cast<qsizetype>(value.size()));
  const auto decoded = QString::fromUtf8(bytes);
  if (decoded.toUtf8() != bytes || !bounded_text(decoded, maximum_bytes, allow_empty)) {
    return std::nullopt;
  }
  return decoded;
}

[[nodiscard]] std::optional<std::int64_t> json_safe_integer(
    const QJsonValue& value) {
  if (!value.isDouble()) {
    return std::nullopt;
  }
  const auto number = value.toDouble(
      std::numeric_limits<double>::quiet_NaN());
  if (!std::isfinite(number) || std::trunc(number) != number ||
      number < -kMaximumExactJsonInteger ||
      number > kMaximumExactJsonInteger) {
    return std::nullopt;
  }
  return static_cast<std::int64_t>(number);
}

[[nodiscard]] bool canonical_uuid(const QString& text) {
  if (text.size() != 36 || !bounded_text(text, 36)) {
    return false;
  }
  const QUuid uuid(text);
  return !uuid.isNull() &&
         uuid.toString(QUuid::WithoutBraces) == text;
}

[[nodiscard]] QString blend_mode_token(BlendMode mode) {
  const auto token = psd::blend_mode_lfx2_string(mode);
  return QString::fromLatin1(token.data(), static_cast<qsizetype>(token.size()));
}

[[nodiscard]] std::optional<BlendMode> blend_mode_from_token(
    const QString& token) {
  if (!bounded_text(token, 64)) {
    return std::nullopt;
  }
  const auto utf8 = token.toUtf8();
  const auto mode = psd::blend_mode_from_lfx2_enum(
      std::string_view(utf8.constData(), static_cast<std::size_t>(utf8.size())));
  const auto value = static_cast<int>(mode);
  if (value < static_cast<int>(BlendMode::Normal) ||
      value > static_cast<int>(BlendMode::Divide) ||
      blend_mode_token(mode) != token) {
    return std::nullopt;
  }
  return mode;
}

[[nodiscard]] QJsonObject color_to_json(RgbColor color) {
  QJsonObject object;
  object.insert(QStringLiteral("red"), static_cast<int>(color.red));
  object.insert(QStringLiteral("green"), static_cast<int>(color.green));
  object.insert(QStringLiteral("blue"), static_cast<int>(color.blue));
  return object;
}

[[nodiscard]] std::optional<RgbColor> color_from_json(
    const QJsonValue& value) {
  if (!value.isObject()) {
    return std::nullopt;
  }
  const auto object = value.toObject();
  const auto red = json_safe_integer(object.value(QStringLiteral("red")));
  const auto green = json_safe_integer(object.value(QStringLiteral("green")));
  const auto blue = json_safe_integer(object.value(QStringLiteral("blue")));
  if (!red.has_value() || !green.has_value() || !blue.has_value() ||
      *red < 0 || *red > 255 || *green < 0 || *green > 255 || *blue < 0 ||
      *blue > 255) {
    return std::nullopt;
  }
  return RgbColor{static_cast<std::uint8_t>(*red),
                  static_cast<std::uint8_t>(*green),
                  static_cast<std::uint8_t>(*blue)};
}

[[nodiscard]] std::optional<QJsonObject> parameter_value_to_json(
    const FilterParameterValue& value) {
  QJsonObject object;
  if (const auto* integer = std::get_if<std::int64_t>(&value);
      integer != nullptr) {
    if (static_cast<double>(*integer) < -kMaximumExactJsonInteger ||
        static_cast<double>(*integer) > kMaximumExactJsonInteger) {
      return std::nullopt;
    }
    object.insert(QStringLiteral("type"), QStringLiteral("integer"));
    object.insert(QStringLiteral("value"), static_cast<qint64>(*integer));
  } else if (const auto* number = std::get_if<double>(&value);
             number != nullptr) {
    if (!std::isfinite(*number)) {
      return std::nullopt;
    }
    object.insert(QStringLiteral("type"), QStringLiteral("double"));
    object.insert(QStringLiteral("value"), *number);
  } else if (const auto* boolean = std::get_if<bool>(&value);
             boolean != nullptr) {
    object.insert(QStringLiteral("type"), QStringLiteral("boolean"));
    object.insert(QStringLiteral("value"), *boolean);
  } else if (const auto* text = std::get_if<std::string>(&value);
             text != nullptr) {
    const auto decoded = qstring_from_utf8_strict(
        *text, kMaximumStringValueBytes, true);
    if (!decoded.has_value()) {
      return std::nullopt;
    }
    object.insert(QStringLiteral("type"), QStringLiteral("string"));
    object.insert(QStringLiteral("value"), *decoded);
  } else {
    return std::nullopt;
  }
  return object;
}

[[nodiscard]] std::optional<FilterParameterValue> parameter_value_from_json(
    const QJsonValue& value) {
  if (!value.isObject()) {
    return std::nullopt;
  }
  const auto object = value.toObject();
  const auto type_value = object.value(QStringLiteral("type"));
  if (!type_value.isString()) {
    return std::nullopt;
  }
  const auto type = type_value.toString();
  const auto encoded = object.value(QStringLiteral("value"));
  if (type == QStringLiteral("integer")) {
    if (const auto integer = json_safe_integer(encoded); integer.has_value()) {
      return FilterParameterValue{*integer};
    }
    return std::nullopt;
  }
  if (type == QStringLiteral("double")) {
    if (!encoded.isDouble()) {
      return std::nullopt;
    }
    const auto number = encoded.toDouble(
        std::numeric_limits<double>::quiet_NaN());
    if (!std::isfinite(number)) {
      return std::nullopt;
    }
    return FilterParameterValue{number};
  }
  if (type == QStringLiteral("boolean")) {
    if (!encoded.isBool()) {
      return std::nullopt;
    }
    return FilterParameterValue{encoded.toBool()};
  }
  if (type == QStringLiteral("string")) {
    if (!encoded.isString()) {
      return std::nullopt;
    }
    const auto text = encoded.toString();
    if (!bounded_text(text, kMaximumStringValueBytes, true)) {
      return std::nullopt;
    }
    const auto utf8 = text.toUtf8();
    return FilterParameterValue{std::string(
        utf8.constData(), static_cast<std::size_t>(utf8.size()))};
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<QJsonObject> invocation_to_json(
    const FilterInvocation& invocation) {
  const auto filter_id = qstring_from_utf8_strict(
      invocation.filter_id, kMaximumFilterIdBytes);
  if (!filter_id.has_value()) {
    return std::nullopt;
  }

  QJsonObject parameters;
  if (invocation.parameters.size() >
      static_cast<std::size_t>(kMaximumParametersPerEntry)) {
    return std::nullopt;
  }
  for (const auto& [key, value] : invocation.parameters) {
    const auto decoded_key = qstring_from_utf8_strict(
        key, kMaximumParameterKeyBytes);
    const auto encoded_value = parameter_value_to_json(value);
    if (!decoded_key.has_value() || !encoded_value.has_value()) {
      return std::nullopt;
    }
    parameters.insert(*decoded_key, *encoded_value);
  }

  QJsonObject object;
  object.insert(QStringLiteral("filterId"), *filter_id);
  object.insert(QStringLiteral("schemaVersion"),
                static_cast<qint64>(invocation.schema_version));
  object.insert(QStringLiteral("parameters"), parameters);
  object.insert(QStringLiteral("foreground"),
                color_to_json(invocation.foreground));
  object.insert(QStringLiteral("background"),
                color_to_json(invocation.background));
  return object;
}

[[nodiscard]] std::optional<FilterInvocation> invocation_from_json(
    const QJsonValue& value) {
  if (!value.isObject()) {
    return std::nullopt;
  }
  const auto object = value.toObject();
  const auto filter_id_value = object.value(QStringLiteral("filterId"));
  if (!filter_id_value.isString()) {
    return std::nullopt;
  }
  const auto filter_id_text = filter_id_value.toString();
  if (!bounded_text(filter_id_text, kMaximumFilterIdBytes)) {
    return std::nullopt;
  }
  const auto schema_version =
      json_safe_integer(object.value(QStringLiteral("schemaVersion")));
  if (!schema_version.has_value() || *schema_version < 0 ||
      *schema_version > std::numeric_limits<std::uint32_t>::max()) {
    return std::nullopt;
  }
  const auto parameters_value = object.value(QStringLiteral("parameters"));
  if (!parameters_value.isObject()) {
    return std::nullopt;
  }
  const auto parameters = parameters_value.toObject();
  if (parameters.size() > kMaximumParametersPerEntry) {
    return std::nullopt;
  }

  FilterInvocation invocation;
  const auto filter_id = filter_id_text.toUtf8();
  invocation.filter_id.assign(
      filter_id.constData(), static_cast<std::size_t>(filter_id.size()));
  invocation.schema_version = static_cast<std::uint32_t>(*schema_version);
  for (auto iterator = parameters.constBegin();
       iterator != parameters.constEnd(); ++iterator) {
    if (!bounded_text(iterator.key(), kMaximumParameterKeyBytes)) {
      return std::nullopt;
    }
    auto decoded = parameter_value_from_json(iterator.value());
    if (!decoded.has_value()) {
      return std::nullopt;
    }
    const auto key = iterator.key().toUtf8();
    invocation.parameters.emplace(
        std::string(key.constData(), static_cast<std::size_t>(key.size())),
        std::move(*decoded));
  }
  const auto foreground =
      color_from_json(object.value(QStringLiteral("foreground")));
  const auto background =
      color_from_json(object.value(QStringLiteral("background")));
  if (!foreground.has_value() || !background.has_value()) {
    return std::nullopt;
  }
  invocation.foreground = *foreground;
  invocation.background = *background;
  return invocation;
}

[[nodiscard]] std::optional<QJsonObject> recipe_entry_to_json(
    const FilterRecipeEntry& entry) {
  if (!std::isfinite(entry.opacity) || entry.opacity < 0.0 ||
      entry.opacity > 1.0) {
    return std::nullopt;
  }
  const auto mode_value = static_cast<int>(entry.blend_mode);
  if (mode_value < static_cast<int>(BlendMode::Normal) ||
      mode_value > static_cast<int>(BlendMode::Divide)) {
    return std::nullopt;
  }
  const auto mode = blend_mode_from_token(blend_mode_token(entry.blend_mode));
  const auto invocation = invocation_to_json(entry.invocation);
  if (!mode.has_value() || !invocation.has_value()) {
    return std::nullopt;
  }
  QJsonObject object;
  object.insert(QStringLiteral("enabled"), entry.enabled);
  object.insert(QStringLiteral("opacity"), entry.opacity);
  object.insert(QStringLiteral("blendMode"), blend_mode_token(*mode));
  object.insert(QStringLiteral("invocation"), *invocation);
  return object;
}

[[nodiscard]] std::optional<FilterRecipeEntry> recipe_entry_from_json(
    const QJsonValue& value) {
  if (!value.isObject()) {
    return std::nullopt;
  }
  const auto object = value.toObject();
  const auto enabled_value = object.value(QStringLiteral("enabled"));
  const auto opacity_value = object.value(QStringLiteral("opacity"));
  const auto blend_value = object.value(QStringLiteral("blendMode"));
  if (!enabled_value.isBool() || !opacity_value.isDouble() ||
      !blend_value.isString()) {
    return std::nullopt;
  }
  const auto opacity = opacity_value.toDouble(
      std::numeric_limits<double>::quiet_NaN());
  const auto blend_mode = blend_mode_from_token(blend_value.toString());
  auto invocation =
      invocation_from_json(object.value(QStringLiteral("invocation")));
  if (!std::isfinite(opacity) || opacity < 0.0 || opacity > 1.0 ||
      !blend_mode.has_value() || !invocation.has_value()) {
    return std::nullopt;
  }
  return FilterRecipeEntry{std::move(*invocation), enabled_value.toBool(),
                           opacity, *blend_mode};
}

[[nodiscard]] std::optional<QJsonObject> recipe_to_json(
    const FilterRecipe& recipe) {
  if (recipe.entries.size() >
      static_cast<std::size_t>(kMaximumRecipeEntries)) {
    return std::nullopt;
  }
  QJsonArray entries;
  for (const auto& entry : recipe.entries) {
    const auto encoded = recipe_entry_to_json(entry);
    if (!encoded.has_value()) {
      return std::nullopt;
    }
    entries.append(*encoded);
  }
  QJsonObject object;
  object.insert(QStringLiteral("entries"), entries);
  return object;
}

[[nodiscard]] std::optional<FilterRecipe> recipe_from_json(
    const QJsonValue& value) {
  if (!value.isObject()) {
    return std::nullopt;
  }
  const auto entries_value = value.toObject().value(QStringLiteral("entries"));
  if (!entries_value.isArray()) {
    return std::nullopt;
  }
  const auto entries = entries_value.toArray();
  if (entries.size() > kMaximumRecipeEntries) {
    return std::nullopt;
  }
  FilterRecipe recipe;
  recipe.entries.reserve(static_cast<std::size_t>(entries.size()));
  for (const auto& encoded_entry : entries) {
    auto entry = recipe_entry_from_json(encoded_entry);
    if (!entry.has_value()) {
      return std::nullopt;
    }
    recipe.entries.push_back(std::move(*entry));
  }
  return recipe;
}

[[nodiscard]] std::optional<QJsonObject> entry_to_json(
    const FilterLookLibraryEntry& entry) {
  const auto name = entry.name.trimmed();
  const auto recipe = recipe_to_json(entry.recipe);
  if (!canonical_uuid(entry.id) ||
      !bounded_text(name, kMaximumLookNameBytes) || !recipe.has_value()) {
    return std::nullopt;
  }
  QJsonObject object;
  object.insert(QStringLiteral("version"), kFilterLookRecordVersion);
  object.insert(QStringLiteral("id"), entry.id);
  object.insert(QStringLiteral("name"), name);
  object.insert(QStringLiteral("recipe"), *recipe);
  return object;
}

[[nodiscard]] std::optional<FilterLookLibraryEntry> entry_from_json(
    const QByteArray& bytes, const QString& expected_id) {
  QJsonParseError parse_error;
  const auto document = QJsonDocument::fromJson(bytes, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    return std::nullopt;
  }
  const auto object = document.object();
  const auto version = json_safe_integer(object.value(QStringLiteral("version")));
  const auto id_value = object.value(QStringLiteral("id"));
  const auto name_value = object.value(QStringLiteral("name"));
  if (!version.has_value() || *version != kFilterLookRecordVersion ||
      !id_value.isString() || !name_value.isString()) {
    return std::nullopt;
  }
  const auto id = id_value.toString();
  const auto name = name_value.toString().trimmed();
  auto recipe = recipe_from_json(object.value(QStringLiteral("recipe")));
  if (id != expected_id || !canonical_uuid(id) ||
      !bounded_text(name, kMaximumLookNameBytes) || !recipe.has_value()) {
    return std::nullopt;
  }
  return FilterLookLibraryEntry{id, name, std::move(*recipe)};
}

}  // namespace

FilterLookLibrary::FilterLookLibrary(QString storage_dir, QObject* parent)
    : FilterLookLibraryBase(std::move(storage_dir), parent) {
  reload();
}

void FilterLookLibrary::reload() {
  entries_.clear();
  const QDir directory(storage_dir_);
  if (!directory.exists()) {
    return;
  }
  const auto files = directory.entryInfoList(
      {QStringLiteral("*.json")}, QDir::Files, QDir::Name);
  for (const auto& file_info : files) {
    if (file_info.size() < 0 || file_info.size() > kMaximumLookFileBytes) {
      continue;
    }
    QFile file(file_info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
      continue;
    }
    const auto bytes = file.read(kMaximumLookFileBytes + 1);
    if (file.error() != QFileDevice::NoError ||
        bytes.size() > kMaximumLookFileBytes || !file.atEnd()) {
      continue;
    }
    auto entry = entry_from_json(bytes, file_info.completeBaseName());
    if (!entry.has_value() || find_entry(entry->id) != nullptr) {
      continue;
    }
    entries_.push_back(std::move(*entry));
  }
  sort_entries();
}

bool FilterLookLibrary::write_entry(
    const FilterLookLibraryEntry& entry) const {
  const auto object = entry_to_json(entry);
  if (!object.has_value()) {
    return false;
  }
  const auto bytes = QJsonDocument(*object).toJson(QJsonDocument::Indented);
  if (bytes.size() > kMaximumLookFileBytes) {
    return false;
  }
  QSaveFile file(json_path(entry.id));
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
    return false;
  }
  return file.commit();
}

QString FilterLookLibrary::add_look(const QString& name,
                                    const FilterRecipe& recipe,
                                    FilterLookLibraryError* error) {
  set_error(error, FilterLookLibraryError::None);
  const auto trimmed = name.trimmed();
  if (!bounded_text(trimmed, kMaximumLookNameBytes)) {
    set_error(error, FilterLookLibraryError::InvalidName);
    return {};
  }
  if (!recipe_to_json(recipe).has_value()) {
    set_error(error, FilterLookLibraryError::InvalidRecipe);
    return {};
  }
  if (!QDir().mkpath(storage_dir_)) {
    set_error(error, FilterLookLibraryError::StorageUnavailable);
    return {};
  }

  QString id;
  do {
    id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  } while (find_entry(id) != nullptr || QFileInfo::exists(json_path(id)));

  FilterLookLibraryEntry entry{id, trimmed, recipe};
  if (!write_entry(entry)) {
    set_error(error, FilterLookLibraryError::WriteFailed);
    return {};
  }
  entries_.push_back(std::move(entry));
  sort_entries();
  emit changed();
  return id;
}

bool FilterLookLibrary::rename_look(const QString& id, const QString& name,
                                    FilterLookLibraryError* error) {
  set_error(error, FilterLookLibraryError::None);
  const auto found = std::find_if(
      entries_.begin(), entries_.end(),
      [&id](const FilterLookLibraryEntry& entry) { return entry.id == id; });
  if (found == entries_.end()) {
    set_error(error, FilterLookLibraryError::NotFound);
    return false;
  }
  const auto trimmed = name.trimmed();
  if (!bounded_text(trimmed, kMaximumLookNameBytes)) {
    set_error(error, FilterLookLibraryError::InvalidName);
    return false;
  }
  if (found->name == trimmed) {
    return true;
  }
  auto updated = *found;
  updated.name = trimmed;
  if (!write_entry(updated)) {
    set_error(error, FilterLookLibraryError::WriteFailed);
    return false;
  }
  found->name = trimmed;
  sort_entries();
  emit changed();
  return true;
}

bool FilterLookLibrary::remove_look(const QString& id,
                                    FilterLookLibraryError* error) {
  set_error(error, FilterLookLibraryError::None);
  const auto found = std::find_if(
      entries_.begin(), entries_.end(),
      [&id](const FilterLookLibraryEntry& entry) { return entry.id == id; });
  if (found == entries_.end()) {
    set_error(error, FilterLookLibraryError::NotFound);
    return false;
  }
  const auto path = json_path(id);
  if (QFileInfo::exists(path) && !QFile::remove(path) &&
      QFileInfo::exists(path)) {
    set_error(error, FilterLookLibraryError::RemoveFailed);
    return false;
  }
  entries_.erase(found);
  emit changed();
  return true;
}

}  // namespace patchy::ui
