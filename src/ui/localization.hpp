#pragma once

#include <QString>

#include <string>
#include <vector>

class QTranslator;
class QLocale;

namespace patchy::ui {

// One shipped UI language. `code` is the catalog code (`translations/patchy_<code>.ts`,
// `patchy_<code>.qm`, `qtbase_<code>.qm`) and the value persisted under
// `preferences/language`; codes never change. English is the source language: it has
// no catalog, and every other language falls back to the English source text for any
// string its catalog lacks.
struct LanguageInfo {
  QString code;
  QString english_name;
  QString native_name;
};

// Translation context for strings defined outside Qt-aware code: preset names and
// diagnostics from the Qt-free libraries (marked with PATCHY_TRANSLATE_NOOP), filter
// catalog labels, file-format display names. See docs/localization.md.
constexpr const char* kDataTranslationContext = "QObject";

// Translates a user-facing string that reaches the UI as plain text (see
// kDataTranslationContext). A catalog miss returns the English text unchanged.
[[nodiscard]] QString translate_data_text(const char* english);
[[nodiscard]] QString translate_data_text(const std::string& english);

class LocalizationManager final {
public:
  static LocalizationManager& instance();

  // The source language code.
  [[nodiscard]] static QString source_language();

  // Every language Patchy ships, in the order the Preferences combo shows them
  // (English first). This table is the single list of supported languages; CMake's
  // PATCHY_TRANSLATED_LANGUAGES and the translations/ directory mirror it.
  [[nodiscard]] const std::vector<LanguageInfo>& languages() const noexcept;
  // The subset whose catalog is installed next to this executable (English always).
  [[nodiscard]] std::vector<LanguageInfo> available_languages() const;
  [[nodiscard]] const LanguageInfo* find_language(const QString& code) const noexcept;

  [[nodiscard]] QString current_language() const;

  // Maps a language code, locale name ("fr_CA") or BCP 47 tag ("zh-Hant-TW") to a
  // shipped code, choosing the Chinese variant by script. Empty when nothing ships
  // for that language.
  [[nodiscard]] QString match_language(const QString& code) const;
  // The best shipped language for a system locale, walking its UI-language preference
  // list; English when nothing matches.
  [[nodiscard]] QString language_for_locale(const QLocale& locale) const;

  void load_saved_language();
  void load_saved_language(const QLocale& system_locale);
  // Installs the catalogs for `code` (any form match_language accepts). Returns false,
  // with English active, when the language is not shipped or its catalog is missing.
  bool set_language(QString code, bool persist = true);

private:
  LocalizationManager();

  [[nodiscard]] bool load_translators(const QString& code);
  void remove_translators();
  void persist_language(const QString& code) const;

  QTranslator* brand_translator_{nullptr};
  std::vector<LanguageInfo> languages_;
  QString current_language_;
  QTranslator* patchy_translator_{nullptr};
  QTranslator* qtbase_translator_{nullptr};
};

}  // namespace patchy::ui
