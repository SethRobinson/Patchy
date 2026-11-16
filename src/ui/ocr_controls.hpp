#pragma once

// The Engine and Language pickers every text recognition dialog shares (docs/ocr.md,
// "Dialogs"): Type > Recognize Text, the Import PDF option, Scan a Book, Preferences. One
// widget, so the engine list, the "nothing available" note and the stored picks (ocr/engine,
// ocr/language) behave the same everywhere.
//
// Binding rule (the "Text recognition" entry of docs/legal-constraints-scanning.md): the
// engine and the language are what the user picks here. Nothing fills them in from an image.

#include "ui/ocr_recognition.hpp"

#include <QString>
#include <QWidget>

#include <functional>

class QComboBox;
class QFormLayout;
class QLabel;
class QPushButton;

namespace patchy::ui {

class OcrEnginePicker final : public QWidget {
public:
  // Object names are `<prefix>EngineCombo`, `<prefix>LanguageCombo`, `<prefix>UnavailableNote`
  // and `<prefix>LanguageSettingsButton`. Starts on the stored picks while they still exist.
  OcrEnginePicker(const QString& object_name_prefix, QWidget* parent);

  // True when the chosen engine can run and has the chosen language.
  [[nodiscard]] bool usable() const;
  [[nodiscard]] QString engine_id() const;
  [[nodiscard]] QString language_tag() const;
  // "Windows, English (United States)": the choice in a line, for a summary label. Empty
  // when nothing usable is chosen.
  [[nodiscard]] QString summary() const;
  // Writes the picks to ocr/engine and ocr/language (a dialog calls it when accepted).
  void store() const;
  // Called whenever usable() or the picks may have changed (a dialog enables its OK there).
  void on_changed(std::function<void()> callback);
  // Asks the engines again: languages installed while the dialog was open show up.
  void refresh();

private:
  void fill_engines();
  void fill_languages();
  void update_note();
  void notify();

  QComboBox* engine_{nullptr};
  QComboBox* language_{nullptr};
  QLabel* note_{nullptr};
  QPushButton* language_settings_{nullptr};
  std::function<void()> changed_;
};

// The name an engine shows under: Patchy's own translated name for the ids it knows, the
// engine's name otherwise.
[[nodiscard]] QString ocr_engine_display_name(const ocr::Engine& engine);

// "Windows (built in), English (United States)" for a resolved choice (resolve_ocr_choice):
// the line a dialog shows beside an option that will use it. Empty without an engine.
[[nodiscard]] QString ocr_choice_summary(const OcrChoice& choice);

// Tests replace what "Open Language Settings" does (it opens a Windows Settings page).
void set_ocr_language_settings_opener_for_testing(std::function<void()> opener);

}  // namespace patchy::ui
