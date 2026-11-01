#pragma once

// Patchy's own settings dialog for a GIMP 3 plug-in (docs/gimp-plugins.md, "Settings
// dialog and canvas preview"): one row per argument the procedure registered, built
// from its registration with Patchy's widgets (sliders and spin boxes, check boxes,
// lists, color buttons). The host runs the plug-in for the canvas preview whenever a
// value changes and reports the preview's state back here.

#include "ui/gimp_plugin_arguments.hpp"

#include <QDialog>

#include <functional>

class QCheckBox;
class QLabel;
class QProgressBar;
class QPushButton;

namespace patchy::ui {

class FilterParameterPanel;

class GimpPluginSettingsDialog : public QDialog {
  Q_OBJECT

public:
  enum class PreviewState { Off, Updating, Quick, Ready, Failed };

  GimpPluginSettingsDialog(const QString& plugin_name, const QString& description, GimpPluginArgumentSet arguments,
                           GimpPluginValues values, bool preview_on, QWidget* parent = nullptr);

  [[nodiscard]] const GimpPluginValues& values() const noexcept { return values_; }
  [[nodiscard]] bool preview_enabled() const;

  // A value changed (every slider step: the host waits for the drag to settle).
  void set_values_changed_callback(std::function<void()> callback) { values_changed_ = std::move(callback); }
  void set_preview_toggled_callback(std::function<void(bool)> callback) { preview_toggled_ = std::move(callback); }
  // "Use the Plug-in's Own Dialog": the host closes this dialog and opens the plug-in's.
  void set_own_dialog_callback(std::function<void()> callback) { own_dialog_ = std::move(callback); }
  // OK: the host returns true to close now, or false while it finishes the result and
  // calls finish_accept() once it has.
  void set_accept_callback(std::function<bool()> callback) { accept_ = std::move(callback); }
  void finish_accept();

  void set_preview_state(PreviewState state, const QString& detail = {});
  // The full-quality result is being made after OK: the settings lock until it lands.
  void set_applying(bool applying);

  void done(int result) override;

private:
  void build_rows();
  void value_changed();
  void reset_values();

  GimpPluginArgumentSet arguments_;
  GimpPluginValues values_;
  GimpPluginValues defaults_;
  FilterParameterPanel* panel_{nullptr};
  QWidget* extra_rows_{nullptr};
  QCheckBox* preview_check_{nullptr};
  QLabel* preview_status_{nullptr};
  QProgressBar* preview_busy_{nullptr};
  QLabel* error_label_{nullptr};
  QPushButton* own_dialog_button_{nullptr};
  QPushButton* reset_button_{nullptr};
  QPushButton* ok_button_{nullptr};
  std::vector<std::function<void()>> sync_extra_rows_;
  std::function<void()> values_changed_;
  std::function<void(bool)> preview_toggled_;
  std::function<void()> own_dialog_;
  std::function<bool()> accept_;
  bool applying_{false};
  bool closing_{false};
};

}  // namespace patchy::ui
