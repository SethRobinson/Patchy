#include "ui/user_fonts.hpp"

#include "formats/font_zip.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QSet>
#include <QStandardPaths>

#include <cstdint>
#include <initializer_list>
#include <string>

#ifdef Q_OS_WASM
#include "ui/user_fonts_wasm.hpp"
#endif

namespace patchy::ui::user_fonts {
namespace {

bool has_any_suffix(const QString& path, std::initializer_list<const char*> suffixes) {
  const auto suffix = QFileInfo(path).suffix().toLower();
  for (const auto* candidate : suffixes) {
    if (suffix == QLatin1String(candidate)) {
      return true;
    }
  }
  return false;
}

// Content hashes of every font registered this session (drops plus the
// startup restore), so a re-dropped font counts as a duplicate instead of
// registering twice. Never pruned: application fonts are never removed.
QSet<QByteArray>& session_hashes() {
  static QSet<QByteArray> hashes;
  return hashes;
}

// "f.ttf" -> "f (2).ttf" while the target exists: an already-registered file
// must never be overwritten in place (the running font may be backed by it).
QString unique_target_path(const QString& directory, const QString& name) {
  auto path = directory + QLatin1Char('/') + name;
  if (!QFileInfo::exists(path)) {
    return path;
  }
  const QFileInfo info(name);
  const auto stem = info.completeBaseName();
  const auto suffix = info.suffix();
  for (int counter = 2;; ++counter) {
    path = QStringLiteral("%1/%2 (%3)%4%5")
               .arg(directory, stem, QString::number(counter),
                    suffix.isEmpty() ? QString() : QStringLiteral("."), suffix);
    if (!QFileInfo::exists(path)) {
      return path;
    }
  }
}

struct RegisterOutcome {
  QStringList families;
  bool duplicate = false;
  bool ok = false;
};

// Writes the registration copy and registers it. Desktop: the copy in the
// user-fonts directory IS the persistence, and registering that copy (never
// the drop source) keeps the live font's backing file from vanishing. Wasm:
// the copy is MEMFS (session-only) and persistence is the IndexedDB put,
// skipped for restored fonts. Bytes that fail to register are never
// persisted.
RegisterOutcome register_font_bytes(const QString& name, const QByteArray& bytes,
                                    bool persist_to_wasm_store) {
  RegisterOutcome outcome;
  if (bytes.isEmpty() || name.isEmpty()) {
    return outcome;
  }
  const auto hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
  if (session_hashes().contains(hash)) {
    outcome.duplicate = true;
    outcome.ok = true;
    return outcome;
  }

#ifdef Q_OS_WASM
  static int counter = 0;
  const auto directory = QStringLiteral("/userfonts/%1").arg(++counter);
#else
  const auto directory = user_fonts_directory();
#endif
  if (directory.isEmpty() || !QDir().mkpath(directory)) {
    return outcome;
  }
  const auto path = unique_target_path(directory, name);
  {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
      return outcome;
    }
  }
  const auto font_id = QFontDatabase::addApplicationFont(path);
  if (font_id < 0) {
    QFile::remove(path);
    return outcome;
  }
  outcome.families = QFontDatabase::applicationFontFamilies(font_id);
  outcome.ok = true;
  session_hashes().insert(hash);
#ifdef Q_OS_WASM
  if (persist_to_wasm_store) {
    wasm_store::put(QFileInfo(path).fileName(), bytes);
  }
#else
  Q_UNUSED(persist_to_wasm_store);
#endif
  return outcome;
}

void fold_outcome_into_result(const RegisterOutcome& outcome, const QString& name,
                              AddFontsResult& result) {
  if (!outcome.ok) {
    result.invalid_names.push_back(name);
    return;
  }
  if (outcome.duplicate) {
    ++result.duplicate_count;
    return;
  }
  for (const auto& family : outcome.families) {
    if (!result.added_families.contains(family)) {
      result.added_families.push_back(family);
    }
  }
}

}  // namespace

bool is_user_font_path(const QString& path) {
  return has_any_suffix(path, {"ttf", "otf", "ttc"});
}

bool is_zip_path(const QString& path) {
  return has_any_suffix(path, {"zip"});
}

AddFontsResult add_user_fonts(const QStringList& paths) {
  AddFontsResult result;
  for (const auto& path : paths) {
    const QFileInfo info(path);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      result.invalid_names.push_back(info.fileName());
      continue;
    }
    const auto bytes = file.readAll();
    if (is_zip_path(path)) {
      std::string error;
      const auto entries = formats::extract_font_files_from_zip(
          reinterpret_cast<const std::uint8_t*>(bytes.constData()),
          static_cast<std::size_t>(bytes.size()), {}, &error);
      if (entries.empty()) {
        result.zips_without_fonts.push_back(info.fileName());
        continue;
      }
      for (const auto& entry : entries) {
        const auto entry_name = QString::fromStdString(entry.name);
        const QByteArray entry_bytes(reinterpret_cast<const char*>(entry.bytes.data()),
                                     static_cast<qsizetype>(entry.bytes.size()));
        fold_outcome_into_result(register_font_bytes(entry_name, entry_bytes, true), entry_name,
                                 result);
      }
    } else if (is_user_font_path(path)) {
      fold_outcome_into_result(register_font_bytes(info.fileName(), bytes, true), info.fileName(),
                               result);
    }
  }
  return result;
}

QString user_fonts_directory() {
#ifdef Q_OS_WASM
  return {};
#else
  const auto base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (base.isEmpty()) {
    return {};
  }
  return base + QStringLiteral("/user-fonts");
#endif
}

void restore_user_fonts_at_startup() {
#ifdef Q_OS_WASM
  wasm_store::begin_restore();
#else
  const auto directory = user_fonts_directory();
  if (directory.isEmpty()) {
    return;
  }
  const QDir dir(directory);
  const QStringList filters = {QStringLiteral("*.ttf"), QStringLiteral("*.otf"),
                               QStringLiteral("*.ttc")};
  for (const auto& entry : dir.entryInfoList(filters, QDir::Files, QDir::Name)) {
    QFile file(entry.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
      continue;
    }
    const auto hash = QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
    if (session_hashes().contains(hash)) {
      continue;
    }
    if (QFontDatabase::addApplicationFont(entry.absoluteFilePath()) >= 0) {
      session_hashes().insert(hash);
    }
  }
#endif
}

void clear_user_font_store() {
#ifdef Q_OS_WASM
  wasm_store::clear();
#else
  const auto directory = user_fonts_directory();
  if (directory.isEmpty()) {
    return;
  }
  const QDir dir(directory);
  const QStringList filters = {QStringLiteral("*.ttf"), QStringLiteral("*.otf"),
                               QStringLiteral("*.ttc")};
  for (const auto& entry : dir.entryInfoList(filters, QDir::Files)) {
    // The registered fonts stay usable (their files may be memory-mapped);
    // deletion only empties the persistence store for the next launch.
    QFile::remove(entry.absoluteFilePath());
  }
#endif
}

#ifdef Q_OS_WASM
void register_restored_font(const QString& name, const QByteArray& bytes) {
  (void)register_font_bytes(name, bytes, false);
}
#endif

}  // namespace patchy::ui::user_fonts
