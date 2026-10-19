#pragma once
#include "ui/layer_style_dialog.hpp"
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>
class QCheckBox;
class QComboBox;
class QDialog;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
namespace patchy::ui {
class GradientStopsEditorWidget;
namespace layer_style_detail {
LayerStyleGradient default_layer_style_gradient();
// Shared host state for the callback-driven gradient-stop editor used by both
// Gradient Overlay and gradient Strokes. The editor widget intentionally never
// owns or mutates its stop vectors; these working vectors remain stable during
// a drag, and value() sorts only the copies written back to LayerStyle.
struct LayerStyleGradientControls {
  GradientStopsEditorWidget* editor{nullptr};
  QSpinBox* stop_location{nullptr};
  QLabel* stop_color_label{nullptr};
  QLineEdit* stop_hex{nullptr};
  QPushButton* stop_swatch{nullptr};
  QLabel* stop_opacity_label{nullptr};
  QSpinBox* stop_opacity{nullptr};
  QLabel* stop_midpoint_label{nullptr};
  QSpinBox* stop_midpoint{nullptr};
  QPushButton* add_stop{nullptr};
  QPushButton* remove_stop{nullptr};
  QComboBox *form_combo{nullptr};
  QSpinBox *smoothness{nullptr};
  QWidget *solid_controls{nullptr};
  QWidget *selected_stop_controls{nullptr};
  QWidget *midpoint_controls{nullptr};
  QWidget *stop_button_controls{nullptr};
  QWidget *noise_controls{nullptr};
  QSpinBox *noise_seed{nullptr};
  QSpinBox *noise_roughness{nullptr};
  QCheckBox*noise_transparency{nullptr};
  QCheckBox *noise_restrict{nullptr};
  QComboBox *noise_color_model{nullptr};
  std::array<QSpinBox *, 4> noise_minimum{};
  std::array<QSpinBox *, 4> noise_maximum{};
  QCheckBox * reverse{nullptr};
  QCheckBox *dither{nullptr};
  QCheckBox *align{nullptr};
  QComboBox*interpolation{nullptr};
  QComboBox * style_combo{nullptr};
  QSpinBox* angle{nullptr};
  QSpinBox* scale{nullptr};
  QSpinBox *offset_x{nullptr};
  QSpinBox *offset_y{nullptr};
  QPushButton *reset_alignment{nullptr};
  LayerStyleGradient template_gradient;
  std::vector<GradientColorStop> color_stops;
  std::vector<GradientAlphaStop> alpha_stops;
  int selected_color_stop{0};
  int selected_alpha_stop{-1};
  // The color stop the open stop-color picker edits, or -1. The picker is
  // non-modal, so the editor stays clickable: selecting another stop closes it
  // (keeping its color), and a request for that stop is queued in
  // pending_picker_row until the open picker's loop unwinds.
  int color_picker_row{-1};
  int pending_picker_row{-1};
  bool loading{false};
  std::function<void()> update_previews;
  std::function<void(bool)> changed;
  std::function<void(const LayerStyleGradient&)> load;

  [[nodiscard]] LayerStyleGradient value() const;
  // Closes the open stop-color picker, keeping its color. detach also stops it
  // writing to color_picker_row (the stop indices are about to change).
  void close_stop_color_picker(bool detach);
};

// Kept in request_layer_style_settings for the entire dialog execution. The
// editor callbacks capture references to these members, never factory locals.
struct LayerStyleGradientContext {
  QDialog& dialog;
  GradientLibrary* gradient_library;
  RgbColor foreground;
  RgbColor background;
  bool batch;
  std::shared_ptr<AppearanceEdits<LayerStyleSettings>> batch_edits;
  std::vector<std::string>& explicit_fields;
  std::function<void()>& reset_batch_capture;
  std::function<std::string(bool)> ramp_property_key;
};
std::unique_ptr<LayerStyleGradientControls> make_gradient_controls(
    LayerStyleGradientContext& context, QFormLayout* form, QWidget* parent_widget,
    const QString& object_prefix, const LayerStyleGradient& initial_gradient,
    const QString& color_picker_title, bool offer_shape_burst = false);
}  // namespace layer_style_detail
}  // namespace patchy::ui
