#include "ui/localization.hpp"

#include "ui/app_settings.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

namespace patchy::ui {

namespace {

constexpr auto kLanguageSettingsKey = "preferences/language";

class LienzoCatalogTranslator final : public QTranslator {
public:
  using QTranslator::QTranslator;

  QString translate(const char* context, const char* source_text,
                    const char* disambiguation = nullptr, int n = -1) const override {
    auto translated =
        QTranslator::translate(context, source_text, disambiguation, n);
    translated.replace(QStringLiteral("Patchy"), QStringLiteral("Lienzo"));
    return translated;
  }
};

class LienzoBrandTranslator final : public QTranslator {
public:
  using QTranslator::QTranslator;

  QString translate(const char* context, const char* source_text,
                    const char* disambiguation = nullptr, int n = -1) const override {
    (void)context;
    (void)disambiguation;
    (void)n;

    if (source_text == nullptr) {
      return {};
    }

    auto text = QString::fromUtf8(source_text);
    if (!text.contains(QStringLiteral("Patchy"))) {
      return {};
    }

    text.replace(QStringLiteral("Patchy"), QStringLiteral("Lienzo"));
    return text;
  }
};

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
  // Inside a macOS .app bundle the executable lives in Contents/MacOS; the bundled
  // translations are staged in Contents/Resources/translations.
  add_directory(QDir(app_dir).filePath(QStringLiteral("../Resources/translations")));
  // Linux installed layout (Flatpak / prefix installs): <prefix>/share/patchy.
  add_directory(QDir(app_dir).filePath(QStringLiteral("../share/patchy/translations")));
  add_directory(app_dir);
  add_directory(QDir::current().filePath(QStringLiteral("translations")));
  return directories;
}

QString find_in_directories(const QString& file_name, const QStringList& directories) {
  for (const auto& directory : directories) {
    const auto path = QDir(directory).filePath(file_name);
    if (QFileInfo::exists(path)) {
      return path;
    }
  }
  return {};
}

QString patchy_catalog_file(const QString& code) {
  return QStringLiteral("patchy_%1.qm").arg(code);
}

QString qtbase_catalog_file(const QString& code) {
  return QStringLiteral("qtbase_%1.qm").arg(code);
}

}  // namespace

QString translate_data_text(const char* english) {
  if (english == nullptr || *english == '\0') {
    return {};
  }
  return QCoreApplication::translate(kDataTranslationContext, english);
}

QString translate_data_text(const std::string& english) {
  return translate_data_text(english.c_str());
}

LocalizationManager& LocalizationManager::instance() {
  static LocalizationManager manager;
  return manager;
}

QString LocalizationManager::source_language() {
  return QStringLiteral("en");
}

LocalizationManager::LocalizationManager()
    : brand_translator_(new LienzoBrandTranslator(qApp)),
      languages_{{QStringLiteral("en"), QStringLiteral("English"), QStringLiteral("English")},
                 {QStringLiteral("de"), QStringLiteral("German"), QStringLiteral("Deutsch")},
                 {QStringLiteral("es"), QStringLiteral("Spanish"), QStringLiteral("Español")},
                 {QStringLiteral("fr"), QStringLiteral("French"), QStringLiteral("Français")},
                 {QStringLiteral("it"), QStringLiteral("Italian"), QStringLiteral("Italiano")},
                 {QStringLiteral("ja"), QStringLiteral("Japanese"), QStringLiteral("日本語")},
                 {QStringLiteral("zh_CN"), QStringLiteral("Chinese (Simplified)"), QStringLiteral("简体中文")},
                 {QStringLiteral("zh_TW"), QStringLiteral("Chinese (Traditional)"), QStringLiteral("繁體中文")}},
      current_language_(source_language()),
      patchy_translator_(new LienzoCatalogTranslator(qApp)),
      qtbase_translator_(new QTranslator(qApp)) {
  QCoreApplication::installTranslator(brand_translator_);
}

const std::vector<LanguageInfo>& LocalizationManager::languages() const noexcept {
  return languages_;
}

std::vector<LanguageInfo> LocalizationManager::available_languages() const {
  const auto directories = translation_directories();
  std::vector<LanguageInfo> available;
  for (const auto& language : languages_) {
    if (language.code == source_language() ||
        !find_in_directories(patchy_catalog_file(language.code), directories).isEmpty()) {
      available.push_back(language);
    }
  }
  return available;
}

const LanguageInfo* LocalizationManager::find_language(const QString& code) const noexcept {
  for (const auto& language : languages_) {
    if (language.code == code) {
      return &language;
    }
  }
  return nullptr;
}

QString LocalizationManager::current_language() const {
  return current_language_;
}

QString LocalizationManager::match_language(const QString& code) const {
  auto normalized = code.trimmed();
  normalized.replace(QLatin1Char('-'), QLatin1Char('_'));
  if (normalized.isEmpty()) {
    return {};
  }
  if (find_language(normalized) != nullptr) {
    return normalized;
  }
  // QLocale fills in the likely script and territory, so "zh" resolves to Simplified,
  // "zh_HK" to Traditional, and an unknown tag to the C locale.
  const QLocale locale(normalized);
  if (locale.language() == QLocale::C || locale.language() == QLocale::AnyLanguage) {
    return {};
  }
  if (locale.language() == QLocale::Chinese) {
    return locale.script() == QLocale::TraditionalHanScript ? QStringLiteral("zh_TW")
                                                             : QStringLiteral("zh_CN");
  }
  const auto language_code = QLocale::languageToCode(locale.language());
  for (const auto& language : languages_) {
    if (language.code == language_code) {
      return language.code;
    }
  }
  return {};
}

QString LocalizationManager::language_for_locale(const QLocale& locale) const {
  for (const auto& tag : locale.uiLanguages()) {
    if (const auto match = match_language(tag); !match.isEmpty()) {
      return match;
    }
  }
  if (const auto match = match_language(locale.name()); !match.isEmpty()) {
    return match;
  }
  return source_language();
}

void LocalizationManager::load_saved_language() {
  load_saved_language(QLocale::system());
}

void LocalizationManager::load_saved_language(const QLocale& system_locale) {
  auto settings = app_settings();
  const auto key = QString::fromLatin1(kLanguageSettingsKey);
  if (settings.contains(key)) {
    set_language(settings.value(key).toString(), false);
  } else {
    set_language(language_for_locale(system_locale), false);
  }
}

bool LocalizationManager::set_language(QString code, bool persist) {
  const auto matched = match_language(code);
  bool loaded = !matched.isEmpty();
  code = loaded ? matched : source_language();
  if (code == current_language_ && (code == source_language() || !patchy_translator_->isEmpty())) {
    if (persist) {
      persist_language(code);
    }
    return loaded;
  }

  remove_translators();
  if (code != source_language()) {
    loaded = load_translators(code);
    if (!loaded) {
      remove_translators();
      code = source_language();
    }
  }

  current_language_ = code;
  if (persist) {
    persist_language(code);
  }
  return loaded;
}

bool LocalizationManager::load_translators(const QString& code) {
  const auto directories = translation_directories();
  const auto patchy_path = find_in_directories(patchy_catalog_file(code), directories);
  if (patchy_path.isEmpty() || !patchy_translator_->load(patchy_path)) {
    return false;
  }
  QCoreApplication::installTranslator(patchy_translator_);

  // Qt's own dialogs and buttons. A missing qtbase catalog leaves them in English,
  // consistent with the per-string fallback everywhere else.
  const auto qtbase_file = qtbase_catalog_file(code);
  const auto qtbase_path = find_in_directories(qtbase_file, directories);
  bool qtbase_loaded = !qtbase_path.isEmpty() && qtbase_translator_->load(qtbase_path);
  if (!qtbase_loaded) {
    qtbase_loaded = qtbase_translator_->load(qtbase_file, QLibraryInfo::path(QLibraryInfo::TranslationsPath));
  }
  if (qtbase_loaded && !qtbase_translator_->isEmpty()) {
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
  settings.setValue(QString::fromLatin1(kLanguageSettingsKey), code);
}

}  // namespace patchy::ui
