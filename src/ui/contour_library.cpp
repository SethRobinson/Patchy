#include "ui/contour_library.hpp"

#include "core/contour_editing.hpp"
#include "psd/shc_io.hpp"
#include "ui/localization.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

// A library file holds one small contour; anything bigger is not ours.
constexpr qint64 kMaxEntryFileBytes = 64 * 1024;
constexpr qint64 kMaxImportFileBytes = 8 * 1024 * 1024;

std::optional<psd::ShcReadResult> parse_shc(const QByteArray& raw, std::string& error) {
  return psd::read_shc(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(raw.constData()),
                                                     static_cast<std::size_t>(raw.size())),
                       error);
}

QByteArray shc_bytes(std::span<const StyleContour> contours) {
  const auto bytes = psd::write_shc(contours);
  return QByteArray(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size()));
}

// What a .shc file keeps of a contour: whole levels, sorted, named.
StyleContour stored_form(const QString& name, const StyleContour& contour) {
  auto named = contour;
  named.name = presets::utf8_from_qstring(name);
  std::string error;
  const auto parsed = parse_shc(shc_bytes(std::span<const StyleContour>(&named, 1)), error);
  if (parsed.has_value() && !parsed->contours.empty()) {
    auto stored = parsed->contours.front();
    stored.name = named.name;  // a name starting "$$$/" must not be read as a key
    return stored;
  }
  return editable_style_contour(named);
}

bool write_atomically(const QString& path, const QByteArray& bytes) {
  QSaveFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

}  // namespace

ContourLibrary::ContourLibrary(QString storage_dir, QObject* parent)
    : ContourLibraryBase(std::move(storage_dir), parent) {
  reload();
}

QString ContourLibrary::shc_path(const QString& storage_id) const { return storage_path(storage_id, ".shc"); }

void ContourLibrary::reload() {
  entries_.clear();
  const QDir dir(storage_dir_);
  for (const auto& info : dir.entryInfoList({QStringLiteral("*.shc")}, QDir::Files, QDir::Name)) {
    if (info.size() > kMaxEntryFileBytes) {
      continue;
    }
    QFile input(info.absoluteFilePath());
    if (!input.open(QIODevice::ReadOnly)) {
      continue;
    }
    std::string error;
    const auto parsed = parse_shc(input.readAll(), error);
    if (!parsed.has_value() || parsed->contours.empty()) {
      continue;
    }
    ContourLibraryEntry entry;
    entry.storage_id = info.completeBaseName();
    entry.contour = parsed->contours.front();
    entry.name = presets::qstring_from_utf8(entry.contour.name);
    entries_.push_back(std::move(entry));
  }
  sort_entries();
}

bool ContourLibrary::write_entry(const ContourLibraryEntry& entry) const {
  if (!QDir().mkpath(storage_dir_)) {
    return false;
  }
  return write_atomically(shc_path(entry.storage_id), shc_bytes(std::span<const StyleContour>(&entry.contour, 1)));
}

const ContourLibraryEntry* ContourLibrary::find_matching(const StyleContour& contour) const {
  return find_entry_if(
      [&contour](const ContourLibraryEntry& entry) { return style_contours_same_shape(entry.contour, contour); });
}

QString ContourLibrary::add_contour(const QString& name, const StyleContour& contour) {
  auto trimmed = name.trimmed();
  if (trimmed.isEmpty()) {
    trimmed = tr("Contour %1").arg(entries_.size() + 1U);
  }
  ContourLibraryEntry entry;
  entry.storage_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  entry.name = trimmed;
  entry.contour = stored_form(trimmed, contour);
  if (!write_entry(entry)) {
    return {};
  }
  const auto id = entry.storage_id;
  entries_.push_back(std::move(entry));
  sort_entries();
  emit changed();
  return id;
}

bool ContourLibrary::rename_contour(const QString& storage_id, const QString& name) {
  return rename_entry(storage_id, name, /*skip_unchanged=*/true, [this](ContourLibraryEntry& updated) {
    updated.contour.name = presets::utf8_from_qstring(updated.name);
    if (!write_entry(updated)) {
      return false;
    }
    // rename_entry commits only the name; the contour's own copy follows here.
    if (const auto found = entry_iterator(updated.storage_id); found != entries_.end()) {
      found->contour.name = updated.contour.name;
    }
    return true;
  });
}

bool ContourLibrary::set_contour_curve(const QString& storage_id, const StyleContour& contour) {
  const auto found = entry_iterator(storage_id);
  if (found == entries_.end()) {
    return false;
  }
  auto updated = *found;
  updated.contour = stored_form(updated.name, contour);
  if (!write_entry(updated)) {
    return false;
  }
  *found = std::move(updated);
  emit changed();
  return true;
}

bool ContourLibrary::remove_contour(const QString& storage_id) {
  return remove_entry(storage_id, [this](const QString& id) {
    const auto path = shc_path(id);
    return !QFile::exists(path) || QFile::remove(path);
  });
}

QStringList ContourLibrary::import_shc(const QString& path, QString& error, QStringList& warnings) {
  error.clear();
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    error = tr("Could not read the contour file.");
    return {};
  }
  if (file.size() > kMaxImportFileBytes) {
    error = tr("The contour file is too large.");
    return {};
  }
  std::string parse_error;
  const auto parsed = parse_shc(file.readAll(), parse_error);
  if (!parsed.has_value()) {
    error = translate_data_text(parse_error);
    return {};
  }
  for (const auto& warning : parsed->warnings) {
    warnings.push_back(translate_data_text(warning));
  }
  QStringList ids;
  bool added = false;
  for (const auto& contour : parsed->contours) {
    const auto name = presets::qstring_from_utf8(contour.name).trimmed();
    const auto duplicate = find_entry_if([&](const ContourLibraryEntry& existing) {
      return existing.name == name && style_contours_same_shape(existing.contour, contour);
    });
    if (duplicate != nullptr) {
      ids.push_back(duplicate->storage_id);
      continue;
    }
    ContourLibraryEntry entry;
    entry.storage_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    entry.name = name.isEmpty() ? tr("Contour %1").arg(entries_.size() + 1U) : name;
    entry.contour = stored_form(entry.name, contour);
    if (!write_entry(entry)) {
      error = tr("Could not save the contour \"%1\".").arg(entry.name);
      break;
    }
    ids.push_back(entry.storage_id);
    entries_.push_back(std::move(entry));
    added = true;
  }
  if (added) {
    sort_entries();
    emit changed();
  }
  return ids;
}

bool ContourLibrary::export_shc(const QStringList& storage_ids, const QString& path, QString& error) const {
  std::vector<StyleContour> contours;
  for (const auto& entry : entries_) {
    if (storage_ids.contains(entry.storage_id)) {
      contours.push_back(entry.contour);
    }
  }
  if (contours.empty()) {
    error = tr("Choose at least one contour to save.");
    return false;
  }
  if (!write_atomically(path, shc_bytes(contours))) {
    error = tr("Could not write the contour file.");
    return false;
  }
  error.clear();
  return true;
}

}  // namespace patchy::ui
