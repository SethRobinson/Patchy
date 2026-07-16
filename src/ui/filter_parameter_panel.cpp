#include "ui/filter_parameter_panel.hpp"

#include "ui/dialog_utils.hpp"
#include "ui/filter_gallery_controls.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>

#include <algorithm>
#include <cmath>

namespace patchy::ui {
namespace {

QString translate_filter_catalog_text(const std::string& source) {
  return QCoreApplication::translate("QObject", source.c_str());
}

double numeric_filter_value(const FilterParameterValue& value,
                            double fallback = 0.0) {
  if (const auto* integer = std::get_if<std::int64_t>(&value);
      integer != nullptr) {
    return static_cast<double>(*integer);
  }
  if (const auto* number = std::get_if<double>(&value); number != nullptr) {
    return *number;
  }
  return fallback;
}

// Legacy hand-built specs predate parameter keys; their stable key is derived
// from the control object name (historical behavior pinned by the direct
// dialog's legacy-spec tests).
std::string stable_key_for(const FilterControlSpec& control) {
  if (!control.parameter_key.empty()) {
    return control.parameter_key;
  }
  auto legacy = control.object_name;
  if (legacy.startsWith(QStringLiteral("filter"))) {
    legacy.remove(0, 6);
  }
  QString key;
  for (const auto character : legacy) {
    if (character.isUpper() && !key.isEmpty()) {
      key += QLatin1Char('_');
    }
    key += character.toLower();
  }
  return key.toStdString();
}

}  // namespace

FilterParameterPanel::FilterParameterPanel(QWidget* parent)
    : QWidget(parent) {}

void FilterParameterPanel::delete_generated_children() {
  bindings_.clear();
  angle_dial_ = nullptr;
  delete layout();
  const auto children =
      findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
  for (auto* child : children) {
    delete child;
  }
}

QFormLayout* FilterParameterPanel::make_form(
    const FilterParameterPanelOptions& options) {
  auto* form = new QFormLayout(this);
  if (options.form_margins.left() >= 0) {
    form->setContentsMargins(options.form_margins);
  }
  if (options.form_horizontal_spacing >= 0) {
    form->setHorizontalSpacing(options.form_horizontal_spacing);
  }
  if (options.form_vertical_spacing >= 0) {
    form->setVerticalSpacing(options.form_vertical_spacing);
  }
  return form;
}

void FilterParameterPanel::clear(const QString& hint_text,
                                 const FilterParameterPanelOptions& options) {
  delete_generated_children();
  auto* form = make_form(options);
  auto* hint = new QLabel(hint_text, this);
  hint->setWordWrap(true);
  form->addRow(hint);
}

void FilterParameterPanel::set_values_changed_callback(
    ValuesChangedCallback callback) {
  values_changed_ = std::move(callback);
}

void FilterParameterPanel::fire(const ValueChanges& changes) {
  if (values_changed_) {
    values_changed_(changes);
  }
}

void FilterParameterPanel::rebuild(const FilterDialogSpec& spec,
                                   const FilterInvocation& values,
                                   const FilterParameterPanelOptions& options) {
  delete_generated_children();
  auto* form = make_form(options);
  bindings_.reserve(spec.controls.size());

  const auto initial_value_for =
      [&](const FilterControlSpec& control,
          const std::string& key) -> FilterParameterValue {
    if (const auto found = values.parameters.find(key);
        found != values.parameters.end()) {
      return found->second;
    }
    // Legacy hand-built specs predate typed defaults. Catalog specs always
    // have a stable key, so this branch is only for old helpers and tests.
    return control.parameter_key.empty()
               ? FilterParameterValue{std::int64_t{control.value}}
               : control.default_value;
  };

  for (const auto& control : spec.controls) {
    ControlBinding binding;
    binding.spec = control;
    binding.key = stable_key_for(control);
    const auto key = binding.key;
    const auto initial_value = initial_value_for(control, key);
    const auto reset_value = control.parameter_key.empty()
                                 ? FilterParameterValue{std::int64_t{control.value}}
                                 : control.default_value;

    if (control.kind == FilterParameterKind::Boolean) {
      auto* check = new QCheckBox(this);
      check->setObjectName(control.object_name + QStringLiteral("Check"));
      const auto* checked = std::get_if<bool>(&initial_value);
      check->setChecked(checked != nullptr && *checked);
      form->addRow(control.label, check);
      QObject::connect(check, &QCheckBox::toggled, this,
                       [this, key](bool value) {
                         fire({{key, FilterParameterValue{value}}});
                       });
      binding.read = [check] { return FilterParameterValue{check->isChecked()}; };
      binding.reset = [check, reset_value] {
        const auto* value = std::get_if<bool>(&reset_value);
        check->setChecked(value != nullptr && *value);
      };
      bindings_.push_back(std::move(binding));
      continue;
    }

    if (control.kind == FilterParameterKind::Option) {
      auto* combo = new QComboBox(this);
      combo->setObjectName(control.object_name + QStringLiteral("Combo"));
      for (const auto& option : control.options) {
        combo->addItem(translate_filter_catalog_text(option.display_name),
                       QString::fromStdString(option.value));
      }
      const auto* selected = std::get_if<std::string>(&initial_value);
      const auto index = selected == nullptr
                             ? -1
                             : combo->findData(QString::fromStdString(*selected));
      combo->setCurrentIndex(index >= 0 ? index
                                        : (combo->count() > 0 ? 0 : -1));
      form->addRow(control.label, combo);
      QObject::connect(combo, qOverload<int>(&QComboBox::currentIndexChanged),
                       this, [this, key, combo](int) {
                         fire({{key, FilterParameterValue{combo->currentData()
                                                              .toString()
                                                              .toUtf8()
                                                              .toStdString()}}});
                       });
      binding.read = [combo] {
        return FilterParameterValue{
            combo->currentData().toString().toUtf8().toStdString()};
      };
      binding.reset = [combo, reset_value] {
        const auto* value = std::get_if<std::string>(&reset_value);
        const auto index =
            value == nullptr ? -1
                             : combo->findData(QString::fromStdString(*value));
        combo->setCurrentIndex(index >= 0 ? index
                                          : (combo->count() > 0 ? 0 : -1));
      };
      bindings_.push_back(std::move(binding));
      continue;
    }

    auto* container = new QWidget(this);
    auto* row = new QHBoxLayout(container);
    row->setContentsMargins(0, 0, 0, 0);
    if (options.slider_row_spacing >= 0) {
      row->setSpacing(options.slider_row_spacing);
    }
    auto* slider = new QSlider(Qt::Horizontal, container);
    slider->setObjectName(control.object_name + QStringLiteral("Slider"));
    row->addWidget(slider, 1);
    binding.slider = slider;

    if (control.kind == FilterParameterKind::Double) {
      const auto minimum =
          control.typed_minimum.value_or(static_cast<double>(control.minimum));
      const auto maximum =
          control.typed_maximum.value_or(static_cast<double>(control.maximum));
      const auto slider_minimum =
          std::clamp(static_cast<double>(control.minimum), minimum, maximum);
      const auto slider_maximum = std::clamp(
          static_cast<double>(control.maximum), slider_minimum, maximum);
      const auto step = std::max(0.000001, control.step.value_or(0.01));
      const auto ticks = std::clamp(
          static_cast<int>(std::lround((slider_maximum - slider_minimum) / step)),
          1, 1'000'000);
      auto* spin = new QDoubleSpinBox(container);
      spin->setObjectName(control.object_name + QStringLiteral("Spin"));
      spin->setRange(minimum, maximum);
      spin->setSingleStep(step);
      int decimals = 0;
      for (auto probe = step;
           decimals < 6 && std::abs(probe - std::round(probe)) > 0.0000001;
           probe *= 10.0) {
        ++decimals;
      }
      spin->setDecimals(decimals);
      if (!control.suffix.isEmpty()) {
        spin->setSuffix(control.suffix);
      }
      configure_dialog_spinbox(spin, options.double_spin_width);
      if (options.plus_minus_spin_buttons) {
        spin->setButtonSymbols(QAbstractSpinBox::PlusMinus);
      }
      slider->setRange(0, ticks);
      const auto number = std::clamp(
          numeric_filter_value(initial_value,
                               numeric_filter_value(control.default_value)),
          minimum, maximum);
      spin->setValue(number);
      slider->setValue(std::clamp(
          static_cast<int>(std::lround((number - slider_minimum) / step)), 0,
          ticks));
      QObject::connect(slider, &QSlider::valueChanged, spin,
                       [spin, slider_minimum, slider_maximum, step](int tick) {
                         spin->setValue(std::clamp(
                             slider_minimum + static_cast<double>(tick) * step,
                             slider_minimum, slider_maximum));
                       });
      QObject::connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged),
                       slider, [slider, slider_minimum, step, ticks](double value) {
                         const QSignalBlocker blocker(slider);
                         slider->setValue(std::clamp(
                             static_cast<int>(std::lround(
                                 (value - slider_minimum) / step)),
                             0, ticks));
                       });
      QObject::connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged),
                       this, [this, key](double value) {
                         fire({{key, FilterParameterValue{value}}});
                       });
      row->addWidget(spin);
      binding.double_spin = spin;
      binding.slider_minimum = slider_minimum;
      binding.slider_step = step;
      binding.slider_ticks = ticks;
      binding.read = [spin] { return FilterParameterValue{spin->value()}; };
      binding.reset = [spin, reset_value] {
        spin->setValue(numeric_filter_value(reset_value, spin->minimum()));
      };
    } else {
      auto* spin = new QSpinBox(container);
      spin->setObjectName(control.object_name + QStringLiteral("Spin"));
      slider->setRange(control.minimum, control.maximum);
      if (options.integer_spin_uses_typed_range) {
        const auto typed_minimum = static_cast<int>(
            std::lround(control.typed_minimum.value_or(control.minimum)));
        const auto typed_maximum = static_cast<int>(
            std::lround(control.typed_maximum.value_or(control.maximum)));
        spin->setRange(std::min(typed_minimum, typed_maximum),
                       std::max(typed_minimum, typed_maximum));
      } else {
        spin->setRange(control.minimum, control.maximum);
      }
      const auto number = std::clamp(
          static_cast<int>(std::lround(
              numeric_filter_value(initial_value, control.value))),
          spin->minimum(), spin->maximum());
      slider->setValue(number);
      spin->setValue(number);
      if (!control.suffix.isEmpty()) {
        spin->setSuffix(control.suffix);
      }
      configure_dialog_spinbox(spin, options.integer_spin_width);
      if (options.plus_minus_spin_buttons) {
        spin->setButtonSymbols(QAbstractSpinBox::PlusMinus);
      }
      QObject::connect(slider, &QSlider::valueChanged, spin,
                       &QSpinBox::setValue);
      QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), slider,
                       [slider](int value) {
                         const QSignalBlocker blocker(slider);
                         slider->setValue(value);
                       });
      QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), this,
                       [this, key](int value) {
                         fire({{key, FilterParameterValue{
                                         static_cast<std::int64_t>(value)}}});
                       });
      row->addWidget(spin);
      binding.int_spin = spin;
      binding.slider_minimum = static_cast<double>(control.minimum);
      binding.slider_step = 1.0;
      binding.slider_ticks = control.maximum - control.minimum;
      binding.read = [spin] {
        return FilterParameterValue{static_cast<std::int64_t>(spin->value())};
      };
      binding.reset = [spin, reset_value, legacy_default = control.value] {
        spin->setValue(static_cast<int>(
            std::lround(numeric_filter_value(reset_value, legacy_default))));
      };
    }
    form->addRow(control.label, container);
    bindings_.push_back(std::move(binding));
  }

  if (options.build_companions) {
    build_companion_rows(spec, form);
  }
}

void FilterParameterPanel::build_companion_rows(const FilterDialogSpec& spec,
                                                QFormLayout* form) {
  const auto angle_control = std::find_if(
      spec.controls.begin(), spec.controls.end(),
      [](const FilterControlSpec& control) {
        return control.presentation == FilterParameterPresentation::Angle;
      });
  if (angle_control != spec.controls.end()) {
    auto* dial = new FilterAngleDial(this);
    dial->setObjectName(QStringLiteral("filterAngleDial"));
    dial->set_range(angle_control->minimum, angle_control->maximum);
    dial->set_default_angle(angle_control->value);
    auto* spin = findChild<QSpinBox*>(angle_control->object_name +
                                      QStringLiteral("Spin"));
    dial->set_angle(spin != nullptr ? spin->value() : angle_control->value);
    dial->set_angle_changed_callback([dial, spin](int degrees, bool) {
      if (spin != nullptr) {
        spin->setValue(degrees);
        dial->set_angle(spin->value());
      }
    });
    if (spin != nullptr) {
      QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), dial,
                       [dial](int degrees) { dial->set_angle(degrees); });
    }
    form->addRow(dial);
    angle_dial_ = dial;
  }

  const auto control_with_presentation =
      [&](FilterParameterPresentation presentation)
      -> std::optional<FilterControlSpec> {
    const auto found = std::find_if(
        spec.controls.begin(), spec.controls.end(),
        [presentation](const FilterControlSpec& control) {
          return control.presentation == presentation;
        });
    return found == spec.controls.end()
               ? std::optional<FilterControlSpec>{}
               : std::optional<FilterControlSpec>{*found};
  };
  const auto wave_amplitude =
      control_with_presentation(FilterParameterPresentation::WaveAmplitude);
  const auto wave_wavelength =
      control_with_presentation(FilterParameterPresentation::WaveWavelength);
  const auto wave_phase =
      control_with_presentation(FilterParameterPresentation::WavePhase);
  if (!wave_amplitude.has_value() || !wave_wavelength.has_value() ||
      !wave_phase.has_value()) {
    return;
  }
  auto* waveform = new FilterWaveformControl(this);
  waveform->setObjectName(QStringLiteral("filterWaveformControl"));
  waveform->set_ranges(
      FilterWaveformValues{wave_amplitude->minimum, wave_wavelength->minimum,
                           wave_phase->minimum},
      FilterWaveformValues{wave_amplitude->maximum, wave_wavelength->maximum,
                           wave_phase->maximum});
  waveform->set_default_values(FilterWaveformValues{
      wave_amplitude->value, wave_wavelength->value, wave_phase->value});
  auto* amplitude_spin = findChild<QSpinBox*>(wave_amplitude->object_name +
                                              QStringLiteral("Spin"));
  auto* wavelength_spin = findChild<QSpinBox*>(wave_wavelength->object_name +
                                               QStringLiteral("Spin"));
  auto* phase_spin =
      findChild<QSpinBox*>(wave_phase->object_name + QStringLiteral("Spin"));
  const auto read_wave_values =
      [amplitude_spin, wavelength_spin, phase_spin,
       defaults = FilterWaveformValues{wave_amplitude->value,
                                       wave_wavelength->value,
                                       wave_phase->value}] {
        return FilterWaveformValues{
            amplitude_spin != nullptr ? amplitude_spin->value()
                                      : defaults.amplitude,
            wavelength_spin != nullptr ? wavelength_spin->value()
                                       : defaults.wavelength,
            phase_spin != nullptr ? phase_spin->value() : defaults.phase};
      };
  waveform->set_values(read_wave_values());
  waveform->set_values_changed_callback(
      [this, waveform, amplitude_spin, wavelength_spin, phase_spin,
       amplitude_key = stable_key_for(*wave_amplitude),
       wavelength_key = stable_key_for(*wave_wavelength),
       phase_key = stable_key_for(*wave_phase),
       amplitude = *wave_amplitude, wavelength = *wave_wavelength,
       phase = *wave_phase](FilterWaveformValues values, bool) {
        const auto sync = [this](QSpinBox* spin,
                                 const FilterControlSpec& control, int value) {
          if (spin != nullptr) {
            const QSignalBlocker spin_blocker(spin);
            spin->setValue(value);
          }
          if (auto* slider = findChild<QSlider*>(
                  control.object_name + QStringLiteral("Slider"));
              slider != nullptr) {
            const QSignalBlocker slider_blocker(slider);
            slider->setValue(value);
          }
        };
        sync(amplitude_spin, amplitude, values.amplitude);
        sync(wavelength_spin, wavelength, values.wavelength);
        sync(phase_spin, phase, values.phase);
        waveform->set_values(values);
        fire({{amplitude_key,
               FilterParameterValue{static_cast<std::int64_t>(values.amplitude)}},
              {wavelength_key,
               FilterParameterValue{static_cast<std::int64_t>(values.wavelength)}},
              {phase_key,
               FilterParameterValue{static_cast<std::int64_t>(values.phase)}}});
      });
  const auto refresh_waveform = [waveform, read_wave_values](int) {
    waveform->set_values(read_wave_values());
  };
  if (amplitude_spin != nullptr) {
    QObject::connect(amplitude_spin, qOverload<int>(&QSpinBox::valueChanged),
                     waveform, refresh_waveform);
  }
  if (wavelength_spin != nullptr) {
    QObject::connect(wavelength_spin, qOverload<int>(&QSpinBox::valueChanged),
                     waveform, refresh_waveform);
  }
  if (phase_spin != nullptr) {
    QObject::connect(phase_spin, qOverload<int>(&QSpinBox::valueChanged),
                     waveform, refresh_waveform);
  }
  form->addRow(waveform);
}

void FilterParameterPanel::read_into(FilterInvocation& invocation) const {
  for (const auto& binding : bindings_) {
    invocation.parameters[binding.key] = binding.read();
  }
}

void FilterParameterPanel::reset_to_defaults() {
  for (const auto& binding : bindings_) {
    binding.reset();
  }
}

const FilterControlSpec* FilterParameterPanel::control_for(
    FilterParameterPresentation role) const {
  for (const auto& binding : bindings_) {
    if (binding.spec.presentation == role) {
      return &binding.spec;
    }
  }
  return nullptr;
}

const FilterParameterPanel::ControlBinding* FilterParameterPanel::binding_for(
    const FilterControlSpec& control) const {
  for (const auto& binding : bindings_) {
    if (binding.spec.object_name == control.object_name) {
      return &binding;
    }
  }
  return nullptr;
}

double FilterParameterPanel::control_numeric_value(
    const FilterControlSpec& control) const {
  const auto* binding = binding_for(control);
  if (binding == nullptr) {
    return numeric_filter_value(control.default_value);
  }
  if (binding->double_spin != nullptr) {
    return binding->double_spin->value();
  }
  if (binding->int_spin != nullptr) {
    return static_cast<double>(binding->int_spin->value());
  }
  return numeric_filter_value(binding->read());
}

FilterParameterValue FilterParameterPanel::sync_control(
    const FilterControlSpec& control, double requested) {
  const auto* binding = binding_for(control);
  if (binding == nullptr) {
    return control.default_value;
  }
  if (binding->double_spin != nullptr) {
    auto* spin = binding->double_spin;
    const auto step = std::max(0.000001, control.step.value_or(0.01));
    const auto clamped = std::clamp(requested, spin->minimum(), spin->maximum());
    const auto quantized = std::clamp(
        spin->minimum() + std::round((clamped - spin->minimum()) / step) * step,
        spin->minimum(), spin->maximum());
    {
      const QSignalBlocker spin_blocker(spin);
      spin->setValue(quantized);
    }
    if (binding->slider != nullptr) {
      const QSignalBlocker slider_blocker(binding->slider);
      binding->slider->setValue(std::clamp(
          static_cast<int>(std::lround(
              (spin->value() - binding->slider_minimum) / binding->slider_step)),
          0, binding->slider_ticks));
    }
    return FilterParameterValue{spin->value()};
  }
  if (binding->int_spin != nullptr) {
    auto* spin = binding->int_spin;
    const auto value = std::clamp(static_cast<int>(std::lround(requested)),
                                  spin->minimum(), spin->maximum());
    {
      const QSignalBlocker spin_blocker(spin);
      spin->setValue(value);
    }
    if (binding->slider != nullptr) {
      const QSignalBlocker slider_blocker(binding->slider);
      binding->slider->setValue(value);
    }
    // The spin is set under a blocker, so the spin->dial connection cannot
    // follow it; keep the companion in sync directly.
    if (binding->spec.presentation == FilterParameterPresentation::Angle &&
        angle_dial_ != nullptr) {
      angle_dial_->set_angle(value);
    }
    return FilterParameterValue{static_cast<std::int64_t>(value)};
  }
  return binding->read();
}

}  // namespace patchy::ui
