#include "ui/localization.hpp"

#include "ui/app_settings.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QSettings>
#include <QTranslator>

#include <memory>

namespace patchy::ui {

namespace {

QStringList translation_directories() {
  QStringList directories;
  const auto add_directory = [&directories](QString path) {
    if (path.isEmpty()) {
      return;
    }
    path = QDir::cleanPath(path);
    if (!directories.contains(path)) {
      directories.push_back(path);
    }
  };

  const auto app_dir = QCoreApplication::applicationDirPath();
  add_directory(QDir(app_dir).filePath(QStringLiteral("translations")));
  add_directory(app_dir);
  add_directory(QDir::current().filePath(QStringLiteral("translations")));
  return directories;
}

bool load_from_directories(QTranslator& translator, const QString& file_name, const QStringList& directories) {
  for (const auto& directory : directories) {
    const auto path = QDir(directory).filePath(file_name);
    if (QFileInfo::exists(path) && translator.load(path)) {
      return true;
    }
  }
  return false;
}

}  // namespace

LocalizationManager& LocalizationManager::instance() {
  static LocalizationManager manager;
  return manager;
}

LocalizationManager::LocalizationManager()
    : languages_{{QStringLiteral("en"), QStringLiteral("English"), QStringLiteral("English")},
                 {QStringLiteral("ja"), QStringLiteral("Japanese"), QStringLiteral("日本語")}},
      current_language_(QStringLiteral("en")),
      patchy_translator_(new QTranslator(qApp)),
      qtbase_translator_(new QTranslator(qApp)) {}

const std::vector<LanguageInfo>& LocalizationManager::languages() const noexcept {
  return languages_;
}

QString LocalizationManager::current_language() const {
  return current_language_;
}

void LocalizationManager::load_saved_language() {
  auto settings = app_settings();
  set_language(settings.value(QStringLiteral("preferences/language"), QStringLiteral("en")).toString(), false);
}

bool LocalizationManager::set_language(QString code, bool persist) {
  code = normalized_language(std::move(code));
  if (code == current_language_ && (code == QStringLiteral("en") || patchy_translator_->isEmpty() == false)) {
    if (persist) {
      persist_language(code);
    }
    return true;
  }

  remove_translators();
  bool loaded = true;
  if (code != QStringLiteral("en")) {
    loaded = load_translators(code);
    if (!loaded) {
      remove_translators();
      code = QStringLiteral("en");
    }
  }

  current_language_ = code;
  if (persist) {
    persist_language(code);
  }
  return loaded;
}

QString LocalizationManager::normalized_language(QString code) const {
  code = code.trimmed().toLower();
  code.replace(QLatin1Char('-'), QLatin1Char('_'));
  if (code.startsWith(QStringLiteral("ja"))) {
    return QStringLiteral("ja");
  }
  return QStringLiteral("en");
}

bool LocalizationManager::load_translators(const QString& code) {
  const auto directories = translation_directories();
  const auto patchy_file = QStringLiteral("patchy_%1.qm").arg(code);
  if (!load_from_directories(*patchy_translator_, patchy_file, directories)) {
    return false;
  }
  QCoreApplication::installTranslator(patchy_translator_);

  const auto qtbase_file = QStringLiteral("qtbase_%1.qm").arg(code);
  if (!load_from_directories(*qtbase_translator_, qtbase_file, directories)) {
    [[maybe_unused]] const bool loaded_qtbase =
        qtbase_translator_->load(qtbase_file, QLibraryInfo::path(QLibraryInfo::TranslationsPath));
  }
  if (!qtbase_translator_->isEmpty()) {
    QCoreApplication::installTranslator(qtbase_translator_);
  }
  return true;
}

void LocalizationManager::remove_translators() {
  QCoreApplication::removeTranslator(qtbase_translator_);
  QCoreApplication::removeTranslator(patchy_translator_);
}

void LocalizationManager::persist_language(const QString& code) const {
  auto settings = app_settings();
  settings.setValue(QStringLiteral("preferences/language"), code);
}

}  // namespace patchy::ui
