#include "ui/filter_workflows.hpp"

#include "formats/acv_curves_io.hpp"
#include "ui/blend_mode_ui.hpp"
#include "ui/coalesced_preview_emitter.hpp"
#include "ui/curves_editor.hpp"
#include "ui/curves_presets.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/filter_workflows_internal.hpp"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLinearGradient>
#include <QListView>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QPolygonF>
#include <QPushButton>
#include <QRect>
#include <QSignalBlocker>
#include <QSlider>
#include <QSizePolicy>
#include <QSpinBox>
#include <QTimer>
#include <QValidator>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace patchy::ui {

// The record math (clamp_levels_record, levels_master_record,
// set_levels_master_record, levels_record_for_channel,
// set_levels_record_for_channel) comes from core/adjustment_layer.hpp.

// Shared with adjustment_dialogs.cpp via ui/filter_workflows_internal.hpp.
LevelsSettings clamp_levels_settings(LevelsSettings settings) {
  set_levels_master_record(settings, levels_master_record(settings));
  settings.red = clamp_levels_record(settings.red);
  settings.green = clamp_levels_record(settings.green);
  settings.blue = clamp_levels_record(settings.blue);
  return settings;
}

namespace {

// NOT the same function as core's levels_channel: this lrounds the DOUBLE
// while core rounds through a float (clamp_byte), and the results differ by
// 1/255 on real inputs (e.g. value 4, record {0,45,121%,0,255}: 34 here,
// 35 in core). Do not "dedupe" one into the other without accepting a
// behavior change on both the dialog path and the render path.
std::uint8_t map_levels_value(std::uint8_t value, LevelsRecord record) {
  record = clamp_levels_record(record);
  const auto input_range = static_cast<double>(record.white_input - record.black_input);
  const auto gamma = static_cast<double>(record.gamma_percent) / 100.0;
  const auto inverse_gamma = gamma <= 0.0 ? 1.0 : 1.0 / gamma;
  const auto output_range = static_cast<double>(record.white_output - record.black_output);
  const auto normalized =
      std::clamp((static_cast<double>(value) - static_cast<double>(record.black_input)) / input_range, 0.0, 1.0);
  const auto output = static_cast<double>(record.black_output) + std::pow(normalized, inverse_gamma) * output_range;
  return static_cast<std::uint8_t>(std::clamp(std::lround(output), 0L, 255L));
}

}  // namespace

QString filter_action_object_name(const QString& identifier) {
  auto object_name = QStringLiteral("filterAction_") + identifier;
  object_name.replace(QLatin1Char('.'), QLatin1Char('_'));
  return object_name;
}

namespace {

// Catalog strings are translated dynamically so the Qt-free filter library does
// not depend on Qt. Keep these markers so lupdate retains the existing QObject
// translation context and the hand-maintained Japanese entries.
[[maybe_unused]] constexpr const char* kFilterTranslationMarkers[] = {
    QT_TRANSLATE_NOOP("QObject", "Invert"),
    QT_TRANSLATE_NOOP("QObject", "Brightness/Contrast"),
    QT_TRANSLATE_NOOP("QObject", "Grayscale"),
    QT_TRANSLATE_NOOP("QObject", "Desaturate"),
    QT_TRANSLATE_NOOP("QObject", "Auto Contrast"),
    QT_TRANSLATE_NOOP("QObject", "Soft Glow"),
    QT_TRANSLATE_NOOP("QObject", "Punchy Color"),
    QT_TRANSLATE_NOOP("QObject", "Noir"),
    QT_TRANSLATE_NOOP("QObject", "Cinematic Matte"),
    QT_TRANSLATE_NOOP("QObject", "Vintage Fade"),
    QT_TRANSLATE_NOOP("QObject", "Vintage Sepia"),
    QT_TRANSLATE_NOOP("QObject", "Threshold"),
    QT_TRANSLATE_NOOP("QObject", "Posterize"),
    QT_TRANSLATE_NOOP("QObject", "Box Blur"),
    QT_TRANSLATE_NOOP("QObject", "Sharpen"),
    QT_TRANSLATE_NOOP("QObject", "Unsharp Mask"),
    QT_TRANSLATE_NOOP("QObject", "High Pass"),
    QT_TRANSLATE_NOOP("QObject", "Median"),
    QT_TRANSLATE_NOOP("QObject", "Dust & Scratches"),
    QT_TRANSLATE_NOOP("QObject", "Surface Blur"),
    QT_TRANSLATE_NOOP("QObject", "Lens Blur"),
    QT_TRANSLATE_NOOP("QObject", "Iris Blur"),
    QT_TRANSLATE_NOOP("QObject", "Tilt-Shift Blur"),
    QT_TRANSLATE_NOOP("QObject", "Plastic Wrap"),
    QT_TRANSLATE_NOOP("QObject", "Gaussian Blur"),
    QT_TRANSLATE_NOOP("QObject", "Motion Blur"),
    QT_TRANSLATE_NOOP("QObject", "Radial Blur"),
    QT_TRANSLATE_NOOP("QObject", "Edge Detect"),
    QT_TRANSLATE_NOOP("QObject", "Emboss"),
    QT_TRANSLATE_NOOP("QObject", "Glowing Edges"),
    QT_TRANSLATE_NOOP("QObject", "Twirl"),
    QT_TRANSLATE_NOOP("QObject", "Wave"),
    QT_TRANSLATE_NOOP("QObject", "Pinch/Bloat"),
    QT_TRANSLATE_NOOP("QObject", "Clouds"),
    QT_TRANSLATE_NOOP("QObject", "Pixel Mosaic"),
    QT_TRANSLATE_NOOP("QObject", "Color Halftone"),
    QT_TRANSLATE_NOOP("QObject", "Analog Grain"),
    QT_TRANSLATE_NOOP("QObject", "Lens Vignette"),
    QT_TRANSLATE_NOOP("QObject", "Other"),
    QT_TRANSLATE_NOOP("QObject", "Adjustments"),
    QT_TRANSLATE_NOOP("QObject", "Photo Looks"),
    QT_TRANSLATE_NOOP("QObject", "Blur"),
    QT_TRANSLATE_NOOP("QObject", "Sharpen"),
    QT_TRANSLATE_NOOP("QObject", "Distort"),
    QT_TRANSLATE_NOOP("QObject", "Noise"),
    QT_TRANSLATE_NOOP("QObject", "Pixelate"),
    QT_TRANSLATE_NOOP("QObject", "Stylize"),
    QT_TRANSLATE_NOOP("QObject", "Render"),
    QT_TRANSLATE_NOOP("QObject", "Artistic"),
    QT_TRANSLATE_NOOP("QObject", "Amount"),
    QT_TRANSLATE_NOOP("QObject", "Brightness"),
    QT_TRANSLATE_NOOP("QObject", "Contrast"),
    QT_TRANSLATE_NOOP("QObject", "Glow"),
    QT_TRANSLATE_NOOP("QObject", "Intensity"),
    QT_TRANSLATE_NOOP("QObject", "Fade"),
    QT_TRANSLATE_NOOP("QObject", "Levels"),
    QT_TRANSLATE_NOOP("QObject", "Radius"),
    QT_TRANSLATE_NOOP("QObject", "Blades"),
    QT_TRANSLATE_NOOP("QObject", "Blade Curvature"),
    QT_TRANSLATE_NOOP("QObject", "Rotation"),
    QT_TRANSLATE_NOOP("QObject", "Angle"),
    QT_TRANSLATE_NOOP("QObject", "Distance"),
    QT_TRANSLATE_NOOP("QObject", "Samples"),
    QT_TRANSLATE_NOOP("QObject", "Strength"),
    QT_TRANSLATE_NOOP("QObject", "Highlight Strength"),
    QT_TRANSLATE_NOOP("QObject", "Height"),
    QT_TRANSLATE_NOOP("QObject", "Edge Width"),
    QT_TRANSLATE_NOOP("QObject", "Smoothness"),
    QT_TRANSLATE_NOOP("QObject", "Amplitude"),
    QT_TRANSLATE_NOOP("QObject", "Wavelength"),
    QT_TRANSLATE_NOOP("QObject", "Phase"),
    QT_TRANSLATE_NOOP("QObject", "Scale"),
    QT_TRANSLATE_NOOP("QObject", "Detail"),
    QT_TRANSLATE_NOOP("QObject", "Seed"),
    QT_TRANSLATE_NOOP("QObject", "Block Size"),
    QT_TRANSLATE_NOOP("QObject", "Cell Size"),
    QT_TRANSLATE_NOOP("QObject", "Center X"),
    QT_TRANSLATE_NOOP("QObject", "Center Y"),
    QT_TRANSLATE_NOOP("QObject", "Iris Width"),
    QT_TRANSLATE_NOOP("QObject", "Iris Height"),
    QT_TRANSLATE_NOOP("QObject", "Focus"),
    QT_TRANSLATE_NOOP("QObject", "Focus Half-Width"),
    QT_TRANSLATE_NOOP("QObject", "Transition Width"),
};

QString translate_filter_catalog_text(const std::string& source) {
  return QCoreApplication::translate("QObject", source.c_str());
}

double numeric_filter_value(const FilterParameterValue& value, double fallback = 0.0) {
  if (const auto* integer = std::get_if<std::int64_t>(&value); integer != nullptr) {
    return static_cast<double>(*integer);
  }
  if (const auto* number = std::get_if<double>(&value); number != nullptr) {
    return *number;
  }
  return fallback;
}

QString filter_parameter_suffix(FilterParameterUnit unit) {
  switch (unit) {
    case FilterParameterUnit::Percent:
      return QStringLiteral("%");
    case FilterParameterUnit::Pixels:
      return QStringLiteral(" px");
    case FilterParameterUnit::Degrees:
      return QStringLiteral(" deg");
    case FilterParameterUnit::None:
      return {};
  }
  return {};
}

}  // namespace

QString filter_display_name(const FilterDefinition& filter) {
  return translate_filter_catalog_text(filter.display_name);
}

QString filter_category_display_name(FilterCategory category) {
  switch (category) {
    case FilterCategory::Uncategorized:
      return translate_filter_catalog_text("Other");
    case FilterCategory::Adjustment:
      return translate_filter_catalog_text("Adjustments");
    case FilterCategory::PhotoLooks:
      return translate_filter_catalog_text("Photo Looks");
    case FilterCategory::Blur:
      return translate_filter_catalog_text("Blur");
    case FilterCategory::Sharpen:
      return translate_filter_catalog_text("Sharpen");
    case FilterCategory::Distort:
      return translate_filter_catalog_text("Distort");
    case FilterCategory::Noise:
      return translate_filter_catalog_text("Noise");
    case FilterCategory::Pixelate:
      return translate_filter_catalog_text("Pixelate");
    case FilterCategory::Stylize:
      return translate_filter_catalog_text("Stylize");
    case FilterCategory::Render:
      return translate_filter_catalog_text("Render");
    case FilterCategory::Artistic:
      return translate_filter_catalog_text("Artistic");
  }
  return translate_filter_catalog_text("Other");
}

QString filter_progress_stage_text(FilterProgressStage stage) {
  switch (stage) {
    case FilterProgressStage::Filtering:
      return QObject::tr("Filtering pixels");
    case FilterProgressStage::Blurring:
      return QObject::tr("Blurring pixels");
    case FilterProgressStage::Sharpening:
      return QObject::tr("Sharpening pixels");
    case FilterProgressStage::DetectingEdges:
      return QObject::tr("Detecting edges");
    case FilterProgressStage::Distorting:
      return QObject::tr("Distorting pixels");
    case FilterProgressStage::Twisting:
      return QObject::tr("Twisting pixels");
    case FilterProgressStage::Embossing:
      return QObject::tr("Embossing pixels");
    case FilterProgressStage::GeneratingClouds:
      return QObject::tr("Generating clouds");
    case FilterProgressStage::Pixelating:
      return QObject::tr("Pixelating blocks");
    case FilterProgressStage::RenderingHalftone:
      return QObject::tr("Rendering halftone");
    case FilterProgressStage::AddingGrain:
      return QObject::tr("Adding grain");
    case FilterProgressStage::ApplyingVignette:
      return QObject::tr("Applying vignette");
  }
  return QObject::tr("Filtering pixels");
}

bool is_adjustment_only_filter(const FilterDefinition& filter) {
  return filter.catalog.adjustment_only;
}

FilterDialogSpec filter_dialog_spec_for(const FilterDefinition& filter) {
  FilterDialogSpec spec;
  spec.identifier = QString::fromStdString(filter.identifier);
  spec.display_name = filter_display_name(filter);
  spec.schema_version = filter.catalog.schema_version;
  spec.controls.reserve(filter.catalog.parameters.size());
  for (const auto& parameter : filter.catalog.parameters) {
    const auto default_number = numeric_filter_value(parameter.default_value);
    FilterControlSpec control{
        translate_filter_catalog_text(parameter.display_name),
        QString::fromStdString(parameter.control_object_name),
        static_cast<int>(std::lround(parameter.practical_minimum.value_or(
            parameter.minimum.value_or(0.0)))),
        static_cast<int>(std::lround(parameter.practical_maximum.value_or(
            parameter.maximum.value_or(100.0)))),
        static_cast<int>(std::lround(default_number)),
        filter_parameter_suffix(parameter.unit)};
    control.parameter_key = parameter.key;
    control.kind = parameter.kind;
    control.default_value = parameter.default_value;
    control.typed_minimum = parameter.minimum;
    control.typed_maximum = parameter.maximum;
    control.step = parameter.step;
    control.options = parameter.options;
    control.presentation = parameter.presentation;
    spec.controls.push_back(std::move(control));
  }
  return spec;
}

std::optional<FilterInvocation> request_filter_settings(
    QWidget* parent, const FilterDialogSpec& spec, const std::function<void(FilterPreviewSettings)>& preview_changed,
    FilterInvocation initial) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("patchyFilterDialog"));
  dialog.setProperty("patchy.filterIdentifier", spec.identifier);
  dialog.setWindowTitle(spec.display_name);
  auto* layout = new QVBoxLayout(&dialog);

  auto* preview = new QCheckBox(QObject::tr("Preview"), &dialog);
  preview->setObjectName(QStringLiteral("filterPreviewCheck"));
  preview->setChecked(true);
  layout->addWidget(preview);

  auto* form = new QFormLayout();
  layout->addLayout(form);

  if (initial.filter_id.empty()) {
    initial.filter_id = spec.identifier.toStdString();
    initial.schema_version = spec.schema_version;
  }

  struct ControlBinding {
    std::string key;
    std::function<FilterParameterValue()> read;
    std::function<void()> reset;
  };
  std::vector<ControlBinding> bindings;
  bindings.reserve(spec.controls.size());
  std::function<void()> schedule_preview_callback;

  const auto stable_key_for = [](const FilterControlSpec& control) {
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
  };
  const auto initial_value_for = [&](const FilterControlSpec& control) -> FilterParameterValue {
    const auto key = stable_key_for(control);
    if (const auto found = initial.parameters.find(key); found != initial.parameters.end()) {
      return found->second;
    }
    // Legacy hand-built specs predate typed defaults. Catalog specs always have
    // a stable key, so this branch is only for old helpers and tests.
    return control.parameter_key.empty() ? FilterParameterValue{std::int64_t{control.value}}
                                         : control.default_value;
  };
  const auto changed = [&schedule_preview_callback] {
    if (schedule_preview_callback) {
      schedule_preview_callback();
    }
  };

  for (const auto& control : spec.controls) {
    const auto key = stable_key_for(control);
    const auto initial_value = initial_value_for(control);
    const auto reset_value = control.parameter_key.empty()
                                 ? FilterParameterValue{std::int64_t{control.value}}
                                 : control.default_value;
    if (control.kind == FilterParameterKind::Boolean) {
      auto* check = new QCheckBox(&dialog);
      check->setObjectName(control.object_name + QStringLiteral("Check"));
      const auto checked = std::get_if<bool>(&initial_value);
      check->setChecked(checked != nullptr ? *checked : false);
      form->addRow(control.label, check);
      QObject::connect(check, &QCheckBox::toggled, &dialog, [changed](bool) { changed(); });
      bindings.push_back(ControlBinding{
          key,
          [check] { return FilterParameterValue{check->isChecked()}; },
          [check, reset_value] {
            const auto* value = std::get_if<bool>(&reset_value);
            check->setChecked(value != nullptr ? *value : false);
          }});
      continue;
    }

    if (control.kind == FilterParameterKind::Option) {
      auto* combo = new QComboBox(&dialog);
      combo->setObjectName(control.object_name + QStringLiteral("Combo"));
      for (const auto& option : control.options) {
        combo->addItem(translate_filter_catalog_text(option.display_name), QString::fromStdString(option.value));
      }
      const auto option_value = std::get_if<std::string>(&initial_value);
      const auto selected = option_value == nullptr ? QString() : QString::fromStdString(*option_value);
      const auto selected_index = combo->findData(selected);
      combo->setCurrentIndex(selected_index >= 0 ? selected_index : (combo->count() > 0 ? 0 : -1));
      form->addRow(control.label, combo);
      QObject::connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
                       [changed](int) { changed(); });
      bindings.push_back(ControlBinding{
          key,
          [combo] { return FilterParameterValue{combo->currentData().toString().toUtf8().toStdString()}; },
          [combo, reset_value] {
            const auto* value = std::get_if<std::string>(&reset_value);
            const auto index = value == nullptr ? -1 : combo->findData(QString::fromStdString(*value));
            combo->setCurrentIndex(index >= 0 ? index : (combo->count() > 0 ? 0 : -1));
          }});
      continue;
    }

    auto* container = new QWidget(&dialog);
    auto* row = new QHBoxLayout(container);
    row->setContentsMargins(0, 0, 0, 0);
    auto* slider = new QSlider(Qt::Horizontal, container);
    slider->setObjectName(control.object_name + QStringLiteral("Slider"));
    row->addWidget(slider, 1);

    if (control.kind == FilterParameterKind::Double) {
      const auto minimum = control.typed_minimum.value_or(static_cast<double>(control.minimum));
      const auto maximum = control.typed_maximum.value_or(static_cast<double>(control.maximum));
      const auto slider_minimum =
          std::clamp(static_cast<double>(control.minimum), minimum, maximum);
      const auto slider_maximum =
          std::clamp(static_cast<double>(control.maximum), slider_minimum,
                     maximum);
      const auto step = std::max(0.000001, control.step.value_or(0.01));
      const auto ticks = std::clamp(
          static_cast<int>(
              std::lround((slider_maximum - slider_minimum) / step)),
          1, 1'000'000);
      auto* spin = new QDoubleSpinBox(container);
      spin->setObjectName(control.object_name + QStringLiteral("Spin"));
      spin->setRange(minimum, maximum);
      spin->setSingleStep(step);
      int decimals = 0;
      for (auto probe = step; decimals < 6 && std::abs(probe - std::round(probe)) > 0.0000001;
           probe *= 10.0) {
        ++decimals;
      }
      spin->setDecimals(decimals);
      if (!control.suffix.isEmpty()) {
        spin->setSuffix(control.suffix);
      }
      configure_dialog_spinbox(spin, 78);
      slider->setRange(0, ticks);
      const auto number = std::clamp(numeric_filter_value(initial_value, numeric_filter_value(control.default_value)),
                                     minimum, maximum);
      spin->setValue(number);
      slider->setValue(std::clamp(
          static_cast<int>(
              std::lround((number - slider_minimum) / step)),
          0, ticks));
      QObject::connect(slider, &QSlider::valueChanged, spin,
                       [spin, slider_minimum, slider_maximum, step](int tick) {
                         spin->setValue(std::clamp(
                             slider_minimum + static_cast<double>(tick) * step,
                             slider_minimum, slider_maximum));
                       });
      QObject::connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), slider,
                       [slider, slider_minimum, step, ticks](double value) {
                         const QSignalBlocker blocker(slider);
                         slider->setValue(
                             std::clamp(
                                 static_cast<int>(std::lround(
                                     (value - slider_minimum) / step)),
                                 0, ticks));
                       });
      QObject::connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), &dialog,
                       [changed](double) { changed(); });
      row->addWidget(spin);
      bindings.push_back(ControlBinding{
          key,
          [spin] { return FilterParameterValue{spin->value()}; },
          [spin, reset_value] {
            spin->setValue(numeric_filter_value(reset_value, spin->minimum()));
          }});
    } else {
      auto* spin = new QSpinBox(container);
      spin->setObjectName(control.object_name + QStringLiteral("Spin"));
      slider->setRange(control.minimum, control.maximum);
      const auto typed_minimum = static_cast<int>(std::lround(
          control.typed_minimum.value_or(control.minimum)));
      const auto typed_maximum = static_cast<int>(std::lround(
          control.typed_maximum.value_or(control.maximum)));
      spin->setRange(std::min(typed_minimum, typed_maximum),
                     std::max(typed_minimum, typed_maximum));
      const auto number = std::clamp(
          static_cast<int>(std::lround(
              numeric_filter_value(initial_value, control.value))),
          spin->minimum(), spin->maximum());
      slider->setValue(number);
      spin->setValue(number);
      if (!control.suffix.isEmpty()) {
        spin->setSuffix(control.suffix);
      }
      configure_dialog_spinbox(spin, 78);
      QObject::connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
      QObject::connect(
          spin, qOverload<int>(&QSpinBox::valueChanged), slider,
          [slider](int value) {
            const QSignalBlocker blocker(slider);
            slider->setValue(value);
          });
      QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), &dialog,
                       [changed](int) { changed(); });
      row->addWidget(spin);
      bindings.push_back(ControlBinding{
          key,
          [spin] { return FilterParameterValue{static_cast<std::int64_t>(spin->value())}; },
          [spin, reset_value, legacy_default = control.value] {
            spin->setValue(static_cast<int>(std::lround(numeric_filter_value(reset_value, legacy_default))));
          }});
    }
    form->addRow(control.label, container);
  }

  auto build_settings = [&] {
    auto invocation = initial;
    invocation.filter_id = spec.identifier.toStdString();
    invocation.schema_version = spec.schema_version;
    for (const auto& binding : bindings) {
      invocation.parameters[binding.key] = binding.read();
    }
    return FilterPreviewSettings{preview->isChecked(), std::move(invocation)};
  };

  CoalescedPreviewEmitter<FilterPreviewSettings> preview_emitter(
      dialog, [&](const FilterPreviewSettings& settings) {
        if (preview_changed) {
          preview_changed(settings);
        }
      });
  auto schedule_preview = [&] { preview_emitter.schedule(build_settings()); };
  auto flush_preview = [&] { preview_emitter.flush(build_settings()); };
  schedule_preview_callback = schedule_preview;

  QObject::connect(preview, &QCheckBox::toggled, &dialog, [&flush_preview](bool) { flush_preview(); });

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Reset,
                                       &dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (auto* reset = buttons->button(QDialogButtonBox::Reset); reset != nullptr) {
    QObject::connect(reset, &QPushButton::clicked, &dialog, [&] {
      for (const auto& binding : bindings) {
        binding.reset();
      }
      preview->setChecked(true);
      flush_preview();
    });
  }

  QTimer::singleShot(0, &dialog, [&dialog, &flush_preview] {
    if (dialog.isVisible()) {
      flush_preview();
    }
  });
  if (run_non_modal_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }

  return build_settings().invocation;
}

std::optional<SmartFilterBlendingSettings> request_smart_filter_blending_settings(
    QWidget* parent, std::function<void(bool, const SmartFilterBlendingSettings&)> preview_changed,
    SmartFilterBlendingSettings initial) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("smartFilterBlendingDialog"));
  dialog.setWindowTitle(QObject::tr("Smart Filter Blending Options"));

  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  layout->addLayout(form);

  auto* blend_mode = new QComboBox(&dialog);
  blend_mode->setObjectName(QStringLiteral("smartFilterBlendModeCombo"));
  add_blend_mode_items(blend_mode);
  const auto blend_mode_index = blend_mode->findData(static_cast<int>(initial.blend_mode));
  blend_mode->setCurrentIndex(blend_mode_index >= 0 ? blend_mode_index
                                                    : blend_mode->findData(static_cast<int>(BlendMode::Normal)));
  form->addRow(QObject::tr("Mode:"), blend_mode);

  auto* opacity_row = new QWidget(&dialog);
  auto* opacity_layout = new QHBoxLayout(opacity_row);
  opacity_layout->setContentsMargins(0, 0, 0, 0);
  auto* opacity_slider = new QSlider(Qt::Horizontal, opacity_row);
  opacity_slider->setObjectName(QStringLiteral("smartFilterOpacitySlider"));
  opacity_slider->setRange(0, 100);
  auto* opacity_spin = new QDoubleSpinBox(opacity_row);
  opacity_spin->setObjectName(QStringLiteral("smartFilterOpacitySpin"));
  opacity_spin->setRange(0.0, 100.0);
  opacity_spin->setDecimals(0);
  opacity_spin->setSingleStep(1.0);
  opacity_spin->setSuffix(QObject::tr("%"));
  configure_dialog_spinbox(opacity_spin, 90);
  const auto initial_opacity = std::clamp(initial.opacity, 0.0, 1.0);
  opacity_spin->setValue(initial_opacity * 100.0);
  opacity_slider->setValue(static_cast<int>(std::lround(initial_opacity * 100.0)));
  opacity_layout->addWidget(opacity_slider, 1);
  opacity_layout->addWidget(opacity_spin);
  form->addRow(QObject::tr("Opacity:"), opacity_row);

  auto* preview = new QCheckBox(QObject::tr("Preview"), &dialog);
  preview->setObjectName(QStringLiteral("smartFilterBlendingPreviewCheck"));
  preview->setChecked(true);
  layout->addWidget(preview);

  auto build_settings = [&] {
    return SmartFilterBlendingSettings{
        static_cast<BlendMode>(blend_mode->currentData().toInt()),
        std::clamp(opacity_spin->value() / 100.0, 0.0, 1.0)};
  };
  CoalescedPreviewEmitter<AdjustmentPreviewRequest<SmartFilterBlendingSettings>> preview_emitter(
      dialog, [&](const AdjustmentPreviewRequest<SmartFilterBlendingSettings>& request) {
        if (preview_changed) {
          preview_changed(request.enabled, request.settings);
        }
      });
  auto preview_request = [&] {
    return AdjustmentPreviewRequest<SmartFilterBlendingSettings>{preview->isChecked(), build_settings()};
  };
  auto schedule_preview = [&] { preview_emitter.schedule(preview_request()); };
  auto flush_preview = [&] { preview_emitter.flush(preview_request()); };

  QObject::connect(opacity_slider, &QSlider::valueChanged, opacity_spin, [opacity_spin](int value) {
    opacity_spin->setValue(static_cast<double>(value));
  });
  QObject::connect(opacity_spin, qOverload<double>(&QDoubleSpinBox::valueChanged), opacity_slider,
                   [opacity_slider](double value) {
                     const QSignalBlocker blocker(opacity_slider);
                     opacity_slider->setValue(static_cast<int>(std::lround(value)));
                   });
  QObject::connect(opacity_spin, qOverload<double>(&QDoubleSpinBox::valueChanged), &dialog,
                   [&schedule_preview](double) { schedule_preview(); });
  QObject::connect(blend_mode, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
                   [&schedule_preview](int) { schedule_preview(); });
  QObject::connect(preview, &QCheckBox::toggled, &dialog, [&flush_preview](bool) { flush_preview(); });

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  buttons->setObjectName(QStringLiteral("smartFilterBlendingButtonBox"));
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  dialog.setStyleSheet(dialog.styleSheet() + dialog_spinbox_button_style());
  QTimer::singleShot(0, &dialog, [&dialog, &flush_preview] {
    if (dialog.isVisible()) {
      flush_preview();
    }
  });
  if (run_non_modal_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  return build_settings();
}

// These helpers are file-private. Without the anonymous namespace they had
// external linkage at patchy::ui scope with no header declaration — a silent
// ODR trap the moment any other patchy::ui TU defines a same-named helper
// (filter_engine.cpp already has anon-namespace twins of several).
namespace {

std::uint8_t filter_clamp_byte(int value) {
  return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

void report_filter_progress(const FilterProgress* progress, int completed, int total,
                            FilterProgressStage stage = FilterProgressStage::Filtering) {
  if (progress == nullptr || !progress->update) {
    return;
  }
  if (!progress->update(std::clamp(completed, 0, std::max(1, total)), std::max(1, total), stage)) {
    throw FilterCancelled();
  }
}

void report_filter_row_progress(const FilterProgress* progress, std::int32_t y, std::int32_t height) {
  report_filter_progress(progress, y, height);
}

void finish_filter_row_progress(const FilterProgress* progress, std::int32_t height) {
  report_filter_progress(progress, height, height);
}

QRegion layer_selection_region(const QRegion& selection, Rect bounds) {
  if (selection.isEmpty() || bounds.empty()) {
    return {};
  }
  return selection.intersected(QRegion(QRect(bounds.x, bounds.y, bounds.width, bounds.height)));
}

template <typename PixelFn>
void for_each_selected_pixel(PixelBuffer& pixels, Rect bounds, const QRegion& selection, const FilterProgress* progress,
                             PixelFn&& pixel_fn) {
  if (selection.isEmpty()) {
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_row_progress(progress, y, pixels.height());
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        pixel_fn(x, y);
      }
    }
    finish_filter_row_progress(progress, pixels.height());
    return;
  }

  const auto selected = layer_selection_region(selection, bounds);
  int total_rows = 0;
  for (const auto& rect : selected) {
    const auto local_left = std::max(0, rect.x() - bounds.x);
    const auto local_top = std::max(0, rect.y() - bounds.y);
    const auto local_right = std::min<std::int32_t>(pixels.width(), rect.x() + rect.width() - bounds.x);
    const auto local_bottom = std::min<std::int32_t>(pixels.height(), rect.y() + rect.height() - bounds.y);
    if (local_left < local_right && local_top < local_bottom) {
      total_rows += local_bottom - local_top;
    }
  }
  int completed_rows = 0;
  for (const auto& rect : selected) {
    const auto local_left = std::max(0, rect.x() - bounds.x);
    const auto local_top = std::max(0, rect.y() - bounds.y);
    const auto local_right = std::min<std::int32_t>(pixels.width(), rect.x() + rect.width() - bounds.x);
    const auto local_bottom = std::min<std::int32_t>(pixels.height(), rect.y() + rect.height() - bounds.y);
    for (std::int32_t y = local_top; y < local_bottom; ++y) {
      report_filter_progress(progress, completed_rows, total_rows);
      for (std::int32_t x = local_left; x < local_right; ++x) {
        pixel_fn(x, y);
      }
      ++completed_rows;
    }
  }
  report_filter_progress(progress, total_rows, total_rows);
}

void restore_pixels_outside_selection(PixelBuffer& pixels, const PixelBuffer& original, const QRegion& selection,
                                      Rect bounds) {
  if (selection.isEmpty() || pixels.format() != original.format() || pixels.width() != original.width() ||
      pixels.height() != original.height()) {
    return;
  }

  const auto selected = layer_selection_region(selection, bounds);
  if (selected.isEmpty()) {
    pixels = original;
    return;
  }

  auto filtered = std::move(pixels);
  pixels = original;
  const auto pixel_bytes = bytes_per_pixel(pixels.format());
  for (const auto& rect : selected) {
    const auto local_left = std::max(0, rect.x() - bounds.x);
    const auto local_top = std::max(0, rect.y() - bounds.y);
    const auto local_right = std::min<std::int32_t>(pixels.width(), rect.x() + rect.width() - bounds.x);
    const auto local_bottom = std::min<std::int32_t>(pixels.height(), rect.y() + rect.height() - bounds.y);
    const auto row_bytes = static_cast<std::size_t>(std::max(0, local_right - local_left)) * pixel_bytes;
    if (row_bytes == 0U) {
      continue;
    }
    for (std::int32_t y = local_top; y < local_bottom; ++y) {
      const auto* src = filtered.pixel(local_left, y);
      auto* dst = pixels.pixel(local_left, y);
      std::copy(src, src + row_bytes, dst);
    }
  }
}

PixelBuffer copy_buffer_rect(const PixelBuffer& src, Rect rect) {
  PixelBuffer out(rect.width, rect.height, src.format());
  const auto row_bytes = static_cast<std::size_t>(rect.width) * bytes_per_pixel(src.format());
  for (std::int32_t y = 0; y < rect.height; ++y) {
    const auto* source_row = src.pixel(rect.x, rect.y + y);
    auto* dest_row = out.pixel(0, y);
    std::copy(source_row, source_row + row_bytes, dest_row);
  }
  return out;
}

void blit_buffer_rect(PixelBuffer& dst, const PixelBuffer& src, std::int32_t dst_x, std::int32_t dst_y) {
  const auto row_bytes = static_cast<std::size_t>(src.width()) * bytes_per_pixel(src.format());
  for (std::int32_t y = 0; y < src.height(); ++y) {
    const auto* source_row = src.pixel(0, y);
    auto* dest_row = dst.pixel(dst_x, dst_y + y);
    std::copy(source_row, source_row + row_bytes, dest_row);
  }
}

}  // namespace

namespace {

template <typename Filter>
PixelBuffer build_filter_preview_pixels_impl(const PixelBuffer& original, const QRegion& selection, Rect bounds,
                                              const FilterRegistry& registry, const Filter& filter,
                                              const FilterProgress* progress, Rect* result_bounds) {
  if (result_bounds != nullptr) {
    *result_bounds = bounds;
  }
  if (!selection.isEmpty() && layer_selection_region(selection, bounds).isEmpty()) {
    return original;
  }

  if (selection.isEmpty()) {
    auto rendered = registry.render(filter, original, bounds, true, progress);
    if (result_bounds != nullptr) {
      *result_bounds = rendered.bounds;
    }
    return std::move(rendered.pixels);
  }

  auto pixels = original;
  if (const auto support = registry.translation_invariant_support(filter); support.has_value()) {
    const auto selected = layer_selection_region(selection, bounds);
    const QRect layer_rect(bounds.x, bounds.y, bounds.width, bounds.height);
    const auto work =
        selected.boundingRect().adjusted(-*support, -*support, *support, *support).intersected(layer_rect);
    if (!work.isEmpty() && work != layer_rect) {
      const Rect work_local{work.x() - bounds.x, work.y() - bounds.y, work.width(), work.height()};
      auto window = copy_buffer_rect(original, work_local);
      registry.apply(filter, window, progress);
      blit_buffer_rect(pixels, window, work_local.x, work_local.y);
      restore_pixels_outside_selection(pixels, original, selection, bounds);
      return pixels;
    }
  }

  registry.apply(filter, pixels, progress);
  restore_pixels_outside_selection(pixels, original, selection, bounds);
  return pixels;
}

}  // namespace

PixelBuffer build_filter_preview_pixels(const PixelBuffer& original, const QRegion& selection, Rect bounds,
                                         const FilterRegistry& registry, const FilterPreviewSettings& settings,
                                         const FilterProgress* progress, Rect* result_bounds) {
  if (!settings.preview_enabled) {
    if (result_bounds != nullptr) {
      *result_bounds = bounds;
    }
    return original;
  }
  return build_filter_preview_pixels_impl(original, selection, bounds, registry, settings.invocation, progress,
                                          result_bounds);
}

PixelBuffer build_filter_preview_pixels(const PixelBuffer& original, const QRegion& selection, Rect bounds,
                                         const FilterRegistry& registry, const FilterRecipe& recipe,
                                         const FilterProgress* progress, Rect* result_bounds) {
  return build_filter_preview_pixels_impl(original, selection, bounds, registry, recipe, progress, result_bounds);
}

bool pixel_buffers_equal(const PixelBuffer& lhs, const PixelBuffer& rhs) {
  return lhs.format() == rhs.format() && lhs.width() == rhs.width() && lhs.height() == rhs.height() &&
         lhs.data().size() == rhs.data().size() && std::equal(lhs.data().begin(), lhs.data().end(), rhs.data().begin());
}

bool editable_rgb8_layer(const Layer* layer) {
  return layer != nullptr && layer->kind() == LayerKind::Pixel && layer->pixels().format().bit_depth == BitDepth::UInt8 &&
         layer->pixels().format().channels >= 3;
}

void apply_levels_to_pixels(PixelBuffer& pixels, Rect bounds, const QRegion& selection, LevelsSettings settings,
                            const FilterProgress* progress) {
  settings = clamp_levels_settings(settings);
  const auto master = levels_master_record(settings);

  for_each_selected_pixel(pixels, bounds, selection, progress, [&](std::int32_t x, std::int32_t y) {
    auto* px = pixels.pixel(x, y);
    px[0] = map_levels_value(px[0], master);
    px[1] = map_levels_value(px[1], master);
    px[2] = map_levels_value(px[2], master);
    px[0] = map_levels_value(px[0], settings.red);
    px[1] = map_levels_value(px[1], settings.green);
    px[2] = map_levels_value(px[2], settings.blue);
  });
}

void apply_curves_to_pixels(PixelBuffer& pixels, Rect bounds, const QRegion& selection, CurvesSettings settings,
                            const FilterProgress* progress) {
  const auto lut = build_curves_lut(settings);

  for_each_selected_pixel(pixels, bounds, selection, progress, [&](std::int32_t x, std::int32_t y) {
    auto* px = pixels.pixel(x, y);
    px[0] = lut.red[px[0]];
    px[1] = lut.green[px[1]];
    px[2] = lut.blue[px[2]];
  });
}

void apply_hue_saturation_to_pixels(PixelBuffer& pixels, Rect bounds, const QRegion& selection,
                                    HueSaturationSettings settings, const FilterProgress* progress) {
  if (settings.colorize) {
    // Route through the core math so the destructive filter matches an
    // adjustment layer with identical settings exactly.
    AdjustmentSettings adjustment;
    adjustment.kind = AdjustmentKind::HueSaturation;
    adjustment.hue_saturation = to_hue_saturation_adjustment(settings);
    const auto channels = pixels.format().channels;
    for_each_selected_pixel(pixels, bounds, selection, progress, [&](std::int32_t x, std::int32_t y) {
      auto* px = pixels.pixel(x, y);
      const auto adjusted = apply_adjustment_to_color(RgbColor{px[0], px[1], px[2]}, adjustment);
      px[0] = adjusted.red;
      px[1] = adjusted.green;
      px[2] = adjusted.blue;
      static_cast<void>(channels);  // alpha (px[3]) is left untouched
    });
    return;
  }
  settings.hue_shift = std::clamp(settings.hue_shift, -180, 180);
  settings.saturation_delta = std::clamp(settings.saturation_delta, -100, 100);
  settings.lightness_delta = std::clamp(settings.lightness_delta, -100, 100);
  const auto channels = pixels.format().channels;
  const auto saturation_offset =
      static_cast<int>(std::round(static_cast<double>(settings.saturation_delta) * 255.0 / 100.0));
  const auto lightness_offset =
      static_cast<int>(std::round(static_cast<double>(settings.lightness_delta) * 255.0 / 100.0));

  for_each_selected_pixel(pixels, bounds, selection, progress, [&](std::int32_t x, std::int32_t y) {
    auto* px = pixels.pixel(x, y);
    QColor color(px[0], px[1], px[2]);
    const auto original_alpha = channels >= 4 ? px[3] : 255;
    auto hue = color.hslHue();
    if (hue < 0) {
      hue = 0;
    }
    hue = (hue + settings.hue_shift) % 360;
    if (hue < 0) {
      hue += 360;
    }
    const auto saturation = std::clamp(color.hslSaturation() + saturation_offset, 0, 255);
    const auto lightness = std::clamp(color.lightness() + lightness_offset, 0, 255);
    const auto adjusted = QColor::fromHsl(hue, saturation, lightness);
    px[0] = static_cast<std::uint8_t>(adjusted.red());
    px[1] = static_cast<std::uint8_t>(adjusted.green());
    px[2] = static_cast<std::uint8_t>(adjusted.blue());
    if (channels >= 4) {
      px[3] = original_alpha;
    }
  });
}

void apply_color_balance_to_pixels(PixelBuffer& pixels, Rect bounds, const QRegion& selection,
                                   ColorBalanceSettings settings, const FilterProgress* progress) {
  settings.cyan_red = std::clamp(settings.cyan_red, -100, 100);
  settings.magenta_green = std::clamp(settings.magenta_green, -100, 100);
  settings.yellow_blue = std::clamp(settings.yellow_blue, -100, 100);
  const auto red_delta = static_cast<int>(std::round(static_cast<double>(settings.cyan_red) * 255.0 / 100.0));
  const auto green_delta =
      static_cast<int>(std::round(static_cast<double>(settings.magenta_green) * 255.0 / 100.0));
  const auto blue_delta = static_cast<int>(std::round(static_cast<double>(settings.yellow_blue) * 255.0 / 100.0));

  for_each_selected_pixel(pixels, bounds, selection, progress, [&](std::int32_t x, std::int32_t y) {
    auto* px = pixels.pixel(x, y);
    px[0] = filter_clamp_byte(static_cast<int>(px[0]) + red_delta);
    px[1] = filter_clamp_byte(static_cast<int>(px[1]) + green_delta);
    px[2] = filter_clamp_byte(static_cast<int>(px[2]) + blue_delta);
  });
}

}  // namespace patchy::ui
