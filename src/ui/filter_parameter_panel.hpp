#pragma once

#include "ui/filter_workflows.hpp"

#include <QMargins>
#include <QString>
#include <QWidget>

#include <functional>
#include <string>
#include <utility>
#include <vector>

class QDoubleSpinBox;
class QFormLayout;
class QSlider;
class QSpinBox;

namespace patchy::ui {

class FilterAngleDial;

// Layout and behavior deltas between the two historical parameter-editor
// builders (the direct filter dialog and the gallery). These are deliberate
// differences preserved through the extraction, not tunable style knobs.
struct FilterParameterPanelOptions {
  // Gallery spin boxes show +/- button symbols; the direct dialog keeps the
  // default arrows.
  bool plus_minus_spin_buttons{false};
  int integer_spin_width{78};
  int double_spin_width{78};
  // The direct dialog lets integer spin boxes accept the full typed range
  // while the slider covers the practical range; the gallery clamps both to
  // the practical range.
  bool integer_spin_uses_typed_range{true};
  // -1 keeps the Qt default.
  int slider_row_spacing{-1};
  QMargins form_margins{-1, -1, -1, -1};
  int form_horizontal_spacing{-1};
  int form_vertical_spacing{-1};
  // Append the FilterAngleDial / FilterWaveformControl companion rows for
  // controls carrying the matching presentation roles.
  bool build_companions{false};
};

// One generated parameter editor shared by the direct filter dialog, the
// gallery, and the smart-filter editing dialog. The host owns the
// authoritative values: user edits are reported through the batched change
// callback and the host pushes accepted values back through rebuild() or
// sync_control().
class FilterParameterPanel : public QWidget {
 public:
  // One user commit = one callback. A waveform gesture step reports all three
  // wave parameters in a single call so hosts render once.
  using ValueChanges =
      std::vector<std::pair<std::string, FilterParameterValue>>;
  using ValuesChangedCallback = std::function<void(const ValueChanges&)>;

  explicit FilterParameterPanel(QWidget* parent = nullptr);

  // Deletes all generated children and rebuilds from the spec, initializing
  // controls from `values` (falling back to the catalog default, then to the
  // legacy control.value for hand-built specs without parameter keys).
  void rebuild(const FilterDialogSpec& spec, const FilterInvocation& values,
               const FilterParameterPanelOptions& options);
  // Deletes all generated children and shows a single word-wrapped hint row
  // (the gallery's no-active-filter state). Only the form layout options are
  // consulted.
  void clear(const QString& hint_text,
             const FilterParameterPanelOptions& options = {});

  void set_values_changed_callback(ValuesChangedCallback callback);

  // Writes every control's current value into the invocation's parameters.
  void read_into(FilterInvocation& invocation) const;
  // Restores every control to its reset value (catalog default, or the
  // legacy control.value). Fires the change callback per control like direct
  // user edits; hosts coalesce.
  void reset_to_defaults();

  [[nodiscard]] const FilterControlSpec* control_for(
      FilterParameterPresentation role) const;
  [[nodiscard]] double control_numeric_value(
      const FilterControlSpec& control) const;
  // Clamps and step-quantizes the requested value, pushes it into the spin
  // box, slider, and companions under signal blockers, and returns the
  // accepted value. Does NOT fire the change callback: overlay hosts commit
  // the returned value and schedule renders themselves.
  FilterParameterValue sync_control(const FilterControlSpec& control,
                                    double requested);

 private:
  struct ControlBinding {
    FilterControlSpec spec;
    std::string key;
    QSpinBox* int_spin{nullptr};
    QDoubleSpinBox* double_spin{nullptr};
    QSlider* slider{nullptr};
    double slider_minimum{0.0};
    double slider_step{1.0};
    int slider_ticks{0};
    std::function<FilterParameterValue()> read;
    std::function<void()> reset;
  };

  void delete_generated_children();
  [[nodiscard]] QFormLayout* make_form(
      const FilterParameterPanelOptions& options);
  void fire(const ValueChanges& changes);
  [[nodiscard]] const ControlBinding* binding_for(
      const FilterControlSpec& control) const;
  void build_companion_rows(const FilterDialogSpec& spec, QFormLayout* form);

  std::vector<ControlBinding> bindings_;
  ValuesChangedCallback values_changed_;
  FilterAngleDial* angle_dial_{nullptr};
};

}  // namespace patchy::ui
