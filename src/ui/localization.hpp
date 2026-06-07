#pragma once

#include <QString>

#include <vector>

class QTranslator;
class QLocale;

namespace patchy::ui {

struct LanguageInfo {
  QString code;
  QString english_name;
  QString native_name;
};

class LocalizationManager final {
public:
  static LocalizationManager& instance();

  [[nodiscard]] const std::vector<LanguageInfo>& languages() const noexcept;
  [[nodiscard]] QString current_language() const;

  void load_saved_language();
  void load_saved_language(const QLocale& system_locale);
  bool set_language(QString code, bool persist = true);

private:
  LocalizationManager();

  [[nodiscard]] QString normalized_language(QString code) const;
  [[nodiscard]] bool load_translators(const QString& code);
  void remove_translators();
  void persist_language(const QString& code) const;

  std::vector<LanguageInfo> languages_;
  QString current_language_;
  QTranslator* patchy_translator_{nullptr};
  QTranslator* qtbase_translator_{nullptr};
};

}  // namespace patchy::ui
