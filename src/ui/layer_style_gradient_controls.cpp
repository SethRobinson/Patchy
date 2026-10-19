#include "ui/layer_style_gradient_controls.hpp"
#include "ui/layer_style_dialog_controls.hpp"
#include "ui/color_panel.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/gradient_library.hpp"
#include "ui/gradient_manager_dialog.hpp"
#include "ui/gradient_preset_popup.hpp"
#include "ui/gradient_stops_editor.hpp"
#include "ui/theme_qss.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

namespace patchy::ui::layer_style_detail {
LayerStyleGradient default_layer_style_gradient() {
  LayerStyleGradient gradient;
  gradient.angle_degrees = 90.0F;
  gradient.scale = 1.0F;
  gradient.color_stops.push_back(GradientColorStop{0.0F, RgbColor{255, 255, 255}});
  gradient.color_stops.push_back(GradientColorStop{1.0F, RgbColor{32, 32, 32}});
  gradient.alpha_stops.push_back(GradientAlphaStop{0.0F, 1.0F});
  gradient.alpha_stops.push_back(GradientAlphaStop{1.0F, 1.0F});
  return gradient;
}

GradientDefinition resolved_gradient_definition(GradientDefinition definition,
                                                RgbColor foreground,
    RgbColor background) {
  for (auto &stop : definition.color_stops) {
    if (stop.kind == GradientColorStop::Kind::Foreground)
      stop.color = foreground;
    if (stop.kind == GradientColorStop::Kind::Background)
      stop.color = background;
    stop.kind = GradientColorStop::Kind::User;
  }
  return definition;
}

LayerStyleGradient LayerStyleGradientControls::value() const {
    LayerStyleGradient result = template_gradient;
    result.form =
        static_cast<GradientDefinitionForm>(form_combo->currentData().toInt());
    result.smoothness =
        static_cast<std::uint16_t>(smoothness->value() * 4096 / 100);
    result.noise.seed = static_cast<std::uint32_t>(noise_seed->value());
    result.noise.roughness =
        static_cast<std::uint16_t>(noise_roughness->value() * 4096 / 100);
    result.noise.add_transparency = noise_transparency->isChecked();
    result.noise.restrict_colors = noise_restrict->isChecked();
    result.noise.color_model = static_cast<GradientNoiseColorModel>(
        noise_color_model->currentData().toInt());
    for (std::size_t channel = 0; channel < result.noise.minimum.size();
         ++channel) {
      result.noise.minimum[channel] =
          static_cast<std::uint16_t>(noise_minimum[channel]->value());
      result.noise.maximum[channel] =
          static_cast<std::uint16_t>(noise_maximum[channel]->value());
    }
    result.type = static_cast<LayerStyleGradientType>(style_combo->currentData().toInt());
    result.reverse = reverse->isChecked();
    result.dither = dither->isChecked();
    result.align_with_layer = align->isChecked();
    result.interpolation = static_cast<GradientInterpolationMethod>(
        interpolation->currentData().toInt());
    result.angle_degrees = static_cast<float>(angle->value());
    result.scale = static_cast<float>(scale->value()) / 100.0F;
    result.offset_x_percent = static_cast<float>(offset_x->value());
    result.offset_y_percent = static_cast<float>(offset_y->value());
    result.color_stops = color_stops;
    if (result.color_stops.empty()) {
      result.color_stops = default_layer_style_gradient().color_stops;
    }
    std::stable_sort(result.color_stops.begin(), result.color_stops.end(),
                     [](const GradientColorStop& lhs, const GradientColorStop& rhs) {
                       return lhs.location < rhs.location;
                     });
    result.alpha_stops = alpha_stops;
    if (result.alpha_stops.empty()) {
      result.alpha_stops = default_layer_style_gradient().alpha_stops;
    }
    std::stable_sort(result.alpha_stops.begin(), result.alpha_stops.end(),
                     [](const GradientAlphaStop& lhs, const GradientAlphaStop& rhs) {
                       return lhs.location < rhs.location;
                     });
    return result;
  }

void LayerStyleGradientControls::close_stop_color_picker(bool detach) {
  if (color_picker_row < 0) {
    return;
  }
  if (detach) {
    color_picker_row = -1;
  }
  if (auto* picker = open_color_picker_dialog(); picker != nullptr) {
    picker->accept();
  }
}

std::unique_ptr<LayerStyleGradientControls> make_gradient_controls(
    LayerStyleGradientContext& context, QFormLayout* form, QWidget* parent_widget,
    const QString& object_prefix, const LayerStyleGradient& initial_gradient,
    const QString& color_picker_title, bool offer_shape_burst) {
  auto& dialog = context.dialog;
  auto& gradient_library = context.gradient_library;
  auto& foreground = context.foreground;
  auto& background = context.background;
  auto& batch = context.batch;
  auto& batch_edits = context.batch_edits;
  auto& explicit_fields = context.explicit_fields;
  auto& reset_batch_capture = context.reset_batch_capture;
  auto& ramp_property_key = context.ramp_property_key;
    auto controls_state = std::make_unique<LayerStyleGradientControls>();
    auto* state = controls_state.get();
    const auto mark_ramp_edit = [&, object_prefix] {
      if (!batch) return;
      explicit_fields = {ramp_property_key(object_prefix.contains(QStringLiteral("Stroke")))};
    };

    auto *preset_button = new QPushButton(QObject::tr("Preset..."),parent_widget);
    preset_button->setObjectName(object_prefix + QStringLiteral("PresetButton"));
    preset_button->setEnabled(gradient_library != nullptr);
    form->addRow(QObject::tr("Preset"), preset_button);
    if (gradient_library != nullptr) {
      const auto apply_preset = [state, foreground, background, mark_ramp_edit](
                                    const GradientLibraryEntry &entry) {
        auto value = state->value();
        static_cast<GradientDefinition &>(value) = resolved_gradient_definition(
            entry.definition, foreground, background);
        state->load(value);
        mark_ramp_edit();
        if (state->changed)
          state->changed(true);
      };QObject::connect(preset_button, &QPushButton::clicked, &dialog,
                       [&, state, preset_button, apply_preset] {
                         show_gradient_preset_popup(
                             preset_button, *gradient_library, apply_preset,
                             [&, state, apply_preset] {
                               const auto selected = request_gradient_manager(
                                   &dialog, *gradient_library, {},
                                   state->value());
                               if (const auto *entry =
                                       gradient_library->find_entry(selected);
                                   entry != nullptr) {
                                 apply_preset(*entry);
                               }
                             });
                       });
    }

    state->form_combo = new QComboBox(parent_widget);
    state->form_combo->setObjectName(object_prefix + QStringLiteral("FormCombo"));
    state->form_combo->addItem(QObject::tr("Solid"),
                               static_cast<int>(GradientDefinitionForm::Solid));
    state->form_combo->addItem(QObject::tr("Noise"),
                               static_cast<int>(GradientDefinitionForm::Noise));
    form->addRow(QObject::tr("Gradient Type"), state->form_combo);

    state->solid_controls = new QWidget(parent_widget);
    auto *solid_layout = new QVBoxLayout(state->solid_controls);
    solid_layout->setContentsMargins(0, 0, 0, 0);
    solid_layout->setSpacing(6);
    state->editor = new GradientStopsEditorWidget(state->solid_controls);
    state->editor->setObjectName(object_prefix + QStringLiteral("StopsEditor"));
    state->editor->set_opacity_track_enabled(true);
    solid_layout->addWidget(state->editor);
    form->addRow(QObject::tr("Gradient"), state->solid_controls);

    auto *selected_row = new QWidget(parent_widget);
    state->selected_stop_controls = selected_row;selected_row->setObjectName(object_prefix + QStringLiteral("SelectedStopRow"));
    auto *selected_layout = new QHBoxLayout(selected_row);
    selected_layout->setContentsMargins(0, 0, 0, 0);
    selected_layout->setSpacing(8);
    auto *location_label = new QLabel(QObject::tr("Location"), selected_row);
    state->stop_location = new QSpinBox(selected_row);
    state->stop_location->setObjectName(object_prefix + QStringLiteral("StopLocationSpin"));
    state->stop_location->setRange(0, 100);
    state->stop_location->setSuffix(percent_suffix());
    configure_dialog_spinbox(state->stop_location, 64);
    state->stop_color_label = new QLabel(QObject::tr("Color"), selected_row);
    state->stop_hex = new QLineEdit(selected_row);
    state->stop_hex->setObjectName(object_prefix + QStringLiteral("StopHexEdit"));state->stop_hex->setFixedWidth(72);
    state->stop_hex->setMaxLength(7);state->stop_swatch = new QPushButton( selected_row);
    state->stop_swatch->setObjectName(object_prefix + QStringLiteral("StopSwatchButton"));
    state->stop_swatch->setFixedSize(34, 24);
    state->stop_swatch->setToolTip(QObject::tr("Choose Color..."));
    state->stop_opacity_label = new QLabel(QObject::tr("Opacity"), selected_row);
    state->stop_opacity = new QSpinBox(selected_row);
    state->stop_opacity->setObjectName(object_prefix + QStringLiteral("StopOpacitySpin"));
    state->stop_opacity->setRange(0, 100);
    state->stop_opacity->setSuffix(percent_suffix());
    configure_dialog_spinbox(state->stop_opacity, 64);
    selected_layout->addWidget(location_label);
    selected_layout->addWidget(state->stop_location);
    selected_layout->addWidget(state->stop_color_label);
    selected_layout->addWidget(state->stop_hex);
    selected_layout->addWidget(state->stop_swatch);
    selected_layout->addWidget(state->stop_opacity_label);
    selected_layout->addWidget(state->stop_opacity);
    selected_layout->addStretch(1);
    form->addRow(QString(), selected_row);

    auto *midpoint_row = new QWidget(parent_widget);
    state->midpoint_controls = midpoint_row;
    auto *midpoint_layout = new QHBoxLayout(midpoint_row);
    midpoint_layout->setContentsMargins(0, 0, 0, 0);
    midpoint_layout->setSpacing(8);
    state->stop_midpoint_label =
        new QLabel(QObject::tr("Midpoint"), midpoint_row);
    state->stop_midpoint = new QSpinBox(midpoint_row);
    state->stop_midpoint->setObjectName(object_prefix + QStringLiteral("StopMidpointSpin"));
    state->stop_midpoint->setRange(5, 95);
    state->stop_midpoint->setSuffix(percent_suffix());
    configure_dialog_spinbox(state->stop_midpoint, 64);
    midpoint_layout->addWidget(state->stop_midpoint_label);
    midpoint_layout->addWidget(state->stop_midpoint);

    // The stop buttons share the midpoint row to keep the gradient block short;
    // they stay a separate container because Solid/Noise switching toggles it.
    auto *stop_buttons = new QWidget(midpoint_row);
    state->stop_button_controls = stop_buttons;
    auto *stop_button_layout = new QHBoxLayout(stop_buttons);
    stop_button_layout->setContentsMargins(0, 0, 0, 0);
    stop_button_layout->setSpacing(6);
    state->add_stop = new QPushButton(QObject::tr("Add Stop"), stop_buttons);
    state->add_stop->setObjectName(object_prefix + QStringLiteral("AddStopButton"));
    state->remove_stop = new QPushButton(QObject::tr("Remove Stop"), stop_buttons);
    state->remove_stop->setObjectName(object_prefix + QStringLiteral("RemoveStopButton"));
    stop_button_layout->addWidget(state->add_stop);
    stop_button_layout->addWidget(state->remove_stop);
    midpoint_layout->addWidget(stop_buttons);
    midpoint_layout->addStretch(1);
    form->addRow(QString(), midpoint_row);

    state->smoothness = add_slider_spin_row(
        form, parent_widget,QObject::tr("Smoothness"),
                                       object_prefix + QStringLiteral("SmoothnessSpin"), 0, 100,
                                       static_cast<int>(std::lround(initial_gradient.smoothness * 100.0 / 4096.0)),
        QStringLiteral("%"));

    state->noise_controls = new QWidget( parent_widget);
    auto *noise_form = new QFormLayout(state->noise_controls);
    noise_form->setContentsMargins(0, 0, 0, 0);
    state->noise_seed = new QSpinBox(state->noise_controls);
    state->noise_seed->setObjectName(
                                       object_prefix + QStringLiteral("NoiseSeedSpin"));
    state->noise_seed->setRange(0, std::numeric_limits<int>::max());
    configure_dialog_spinbox(state->noise_seed);
    noise_form->addRow(QObject::tr("Seed"), state->noise_seed);
    state->noise_roughness = new QSpinBox(state->noise_controls);
    state->noise_roughness->setObjectName(object_prefix +
                                          QStringLiteral("NoiseRoughnessSpin"));
    state->noise_roughness->setRange(0, 100);
    state->noise_roughness->setSuffix(percent_suffix());
    configure_dialog_spinbox(state->noise_roughness);
    noise_form->addRow(QObject::tr("Roughness"), state->noise_roughness);
    state->noise_color_model = new QComboBox(state->noise_controls);
    state->noise_color_model->setObjectName(
        object_prefix + QStringLiteral("NoiseColorModelCombo"));
    state->noise_color_model->addItem(
        QStringLiteral("RGB"), static_cast<int>(GradientNoiseColorModel::RGB));
    state->noise_color_model->addItem(
        QStringLiteral("HSB"), static_cast<int>(GradientNoiseColorModel::HSB));
    state->noise_color_model->addItem(
        QStringLiteral("Lab"), static_cast<int>(GradientNoiseColorModel::Lab));
    noise_form->addRow(QObject::tr("Color Model"), state->noise_color_model);
    state->noise_transparency =
        new QCheckBox(QObject::tr("Add Transparency"), state->noise_controls);
    state->noise_transparency->setObjectName(
        object_prefix + QStringLiteral("NoiseTransparencyCheck"));
    noise_form->addRow(QString(), state->noise_transparency);
    state->noise_restrict =
        new QCheckBox(QObject::tr("Restrict Colors"), state->noise_controls);
    state->noise_restrict->setObjectName(
        object_prefix + QStringLiteral("NoiseRestrictColorsCheck"));
    noise_form->addRow(QString(), state->noise_restrict);
    for (std::size_t channel = 0; channel < state->noise_minimum.size();
         ++channel) {
      auto *range_row = new QWidget(state->noise_controls);
      auto *range_layout = new QHBoxLayout(range_row);
      range_layout->setContentsMargins(0, 0, 0, 0);
      range_layout->setSpacing(6);
      state->noise_minimum[channel] = new QSpinBox(range_row);
      state->noise_minimum[channel]->setObjectName(
          object_prefix +
          QStringLiteral("NoiseChannel%1MinimumSpin").arg(channel + 1U));
      state->noise_minimum[channel]->setRange(0, 100);
      state->noise_minimum[channel]->setSuffix(percent_suffix());
      configure_dialog_spinbox(state->noise_minimum[channel], 64);
      state->noise_maximum[channel] = new QSpinBox(range_row);
      state->noise_maximum[channel]->setObjectName(
          object_prefix +
          QStringLiteral("NoiseChannel%1MaximumSpin").arg(channel + 1U));
      state->noise_maximum[channel]->setRange(0, 100);
      state->noise_maximum[channel]->setSuffix(percent_suffix());
      configure_dialog_spinbox(state->noise_maximum[channel], 64);
      range_layout->addWidget(state->noise_minimum[channel]);
      auto *range_to = new QLabel(QObject::tr("to"), range_row);
      // Not a field name: it must not become the maximum field's scrub handle.
      range_to->setProperty(kScrubLabelExemptProperty, true);
      range_layout->addWidget(range_to);
      range_layout->addWidget(state->noise_maximum[channel]);
      range_layout->addStretch(1);
      noise_form->addRow(QObject::tr("Channel %1 Range").arg(channel + 1U),
                         range_row);
    }
    form->addRow(QString(), state->noise_controls);

    // One shared row keeps the gradient block short enough that the Gradient
    // Overlay page, the tallest in the stack, does not stretch the dialog.
    auto *toggles_row = new QWidget(parent_widget);
    auto *toggles_layout = new QHBoxLayout(toggles_row);
    toggles_layout->setContentsMargins(0, 0, 0, 0);
    toggles_layout->setSpacing(12);
    state->reverse = new QCheckBox(QObject::tr("Reverse"), toggles_row);
    state->reverse->setObjectName(object_prefix +
                                  QStringLiteral("ReverseCheck"));
    toggles_layout->addWidget(state->reverse);
    state->dither = new QCheckBox(QObject::tr("Dither"), toggles_row);
    state->dither->setObjectName(object_prefix + QStringLiteral("DitherCheck"));
    toggles_layout->addWidget(state->dither);
    state->align =
        new QCheckBox(QObject::tr("Align with Layer"), toggles_row);
    state->align->setObjectName(object_prefix + QStringLiteral("AlignCheck"));
    toggles_layout->addWidget(state->align);
    toggles_layout->addStretch(1);
    form->addRow(QString(), toggles_row);
    state->interpolation = new QComboBox(parent_widget);
    state->interpolation->setObjectName(object_prefix +
                                        QStringLiteral("InterpolationCombo"));
    state->interpolation->addItem(
        QObject::tr("Classic"),
        static_cast<int>(GradientInterpolationMethod::Classic));
    state->interpolation->addItem(
        QObject::tr("Perceptual"),
        static_cast<int>(GradientInterpolationMethod::Perceptual));
    state->interpolation->addItem(
        QObject::tr("Linear", "gradient interpolation"),
        static_cast<int>(GradientInterpolationMethod::Linear));
    form->addRow(QObject::tr("Method"), state->interpolation);
    state->style_combo = new QComboBox(parent_widget);
    state->style_combo->setObjectName(object_prefix +
                                      QStringLiteral("StyleCombo"));
    state->style_combo->addItem(
        QObject::tr("Linear"),
        static_cast<int>(LayerStyleGradientType::Linear));
    state->style_combo->addItem(
        QObject::tr("Radial"),
        static_cast<int>(LayerStyleGradientType::Radial));
    state->style_combo->addItem(
        QObject::tr("Angle", "gradient style"),
        static_cast<int>(LayerStyleGradientType::Angle));
    state->style_combo->addItem(
        QObject::tr("Reflected"),
        static_cast<int>(LayerStyleGradientType::Reflected));
    state->style_combo->addItem(
        QObject::tr("Diamond"),
        static_cast<int>(LayerStyleGradientType::Diamond));
    if (offer_shape_burst) {
      // Photoshop offers Shape Burst for the Stroke effect only.
      state->style_combo->addItem(
          QObject::tr("Shape Burst"),
          static_cast<int>(LayerStyleGradientType::ShapeBurst));
    }
    form->addRow(QObject::tr("Style"), state->style_combo);
    state->angle = add_slider_spin_row(
        form, parent_widget, QObject::tr("Angle"),
        object_prefix + QStringLiteral("AngleSpin"), -180, 180,
        static_cast<int>(std::round(initial_gradient.angle_degrees)));
    state->scale = add_slider_spin_row(
        form, parent_widget, QObject::tr("Scale"),
        object_prefix + QStringLiteral("ScaleSpin"), 1, 1000,
                                       static_cast<int>(std::round(initial_gradient.scale * 100.0F)),
                                       QStringLiteral("%"));
    state->offset_x = add_slider_spin_row(
        form, parent_widget, QObject::tr("Horizontal Offset"),
        object_prefix + QStringLiteral("OffsetXSpin"), -100, 100,
        static_cast<int>(std::round(initial_gradient.offset_x_percent)),
        QStringLiteral("%"));
    state->offset_y = add_slider_spin_row(
        form, parent_widget, QObject::tr("Vertical Offset"),
        object_prefix + QStringLiteral("OffsetYSpin"), -100, 100,
        static_cast<int>(std::round(initial_gradient.offset_y_percent)),
        QStringLiteral("%"));
    state->reset_alignment =
        new QPushButton(QObject::tr("Reset Alignment"), parent_widget);
    state->reset_alignment->setObjectName(
        object_prefix + QStringLiteral("ResetAlignmentButton"));
    form->addRow(QString(), state->reset_alignment);

    state->update_previews = [state] {
      if (state->color_stops.empty()) {
        state->color_stops = default_layer_style_gradient().color_stops;
      }
      if (state->alpha_stops.empty()) {
        state->alpha_stops = default_layer_style_gradient().alpha_stops;
      }
      if (state->selected_alpha_stop >= 0) {
        state->selected_alpha_stop =
            std::clamp(state->selected_alpha_stop, 0, static_cast<int>(state->alpha_stops.size()) - 1);
        state->selected_color_stop = -1;
      } else {
        state->selected_color_stop =
            std::clamp(state->selected_color_stop, 0, static_cast<int>(state->color_stops.size()) - 1);
      }
      if (state->color_picker_row >= 0 && state->selected_color_stop != state->color_picker_row) {
        state->close_stop_color_picker(false);
      }

      std::vector<GradientStop> editor_stops;
      editor_stops.reserve(state->color_stops.size());
      for (const auto& stop : state->color_stops) {
        editor_stops.push_back(
            GradientStop{stop.location, EditColor{stop.color.red, stop.color.green, stop.color.blue, 255}});
      }
      state->editor->set_stops(std::move(editor_stops));
      std::vector<float> color_midpoints;
      color_midpoints.reserve(state->color_stops.size());
      for (const auto& stop : state->color_stops) {
        color_midpoints.push_back(stop.midpoint);
      }
      state->editor->set_color_midpoints(std::move(color_midpoints));
      state->editor->set_opacity_stops(state->alpha_stops);
      state->editor->set_current_row(state->selected_color_stop);
      state->editor->set_current_opacity_row(state->selected_alpha_stop);

      const bool color_selected = state->selected_color_stop >= 0;
      state->stop_color_label->setVisible(color_selected);
      state->stop_hex->setVisible(color_selected);
      state->stop_swatch->setVisible(color_selected);
      state->stop_opacity_label->setVisible(!color_selected);
      state->stop_opacity->setVisible(!color_selected);

      const QSignalBlocker location_blocker(state->stop_location);
      const QSignalBlocker hex_blocker(state->stop_hex);
      const QSignalBlocker opacity_blocker(state->stop_opacity);
      const QSignalBlocker midpoint_blocker(state->stop_midpoint);
      auto has_previous_stop = [](const auto& stops, int row) {
        std::vector<int> rows(stops.size());
        std::iota(rows.begin(), rows.end(), 0);
        std::stable_sort(rows.begin(), rows.end(), [&](int lhs, int rhs) {
          return stops[static_cast<std::size_t>(lhs)].location <
                 stops[static_cast<std::size_t>(rhs)].location;
        });
        const auto found = std::find(rows.begin(), rows.end(), row);
        if (found == rows.end() || found == rows.begin()) {
          return false;
        }
        const auto previous = *(found - 1);
        return stops[static_cast<std::size_t>(previous)].location <
               stops[static_cast<std::size_t>(row)].location;
      };
      bool midpoint_available = false;
      if (color_selected) {
        const auto& stop = state->color_stops[static_cast<std::size_t>(state->selected_color_stop)];
        state->stop_location->setValue(
            static_cast<int>(std::lround(std::clamp(stop.location, 0.0F, 1.0F) * 100.0F)));
        state->stop_hex->setText(
            QColor(stop.color.red, stop.color.green, stop.color.blue).name(QColor::HexRgb).toUpper());
        update_color_preview_label(state->stop_swatch, stop.color.red, stop.color.green, stop.color.blue);
        state->remove_stop->setEnabled(state->color_stops.size() > 2U);
        midpoint_available = has_previous_stop(state->color_stops, state->selected_color_stop);
        state->stop_midpoint->setValue(
            static_cast<int>(std::lround(std::clamp(stop.midpoint, 0.05F, 0.95F) * 100.0F)));
      } else {
        const auto& stop = state->alpha_stops[static_cast<std::size_t>(state->selected_alpha_stop)];
        state->stop_location->setValue(
            static_cast<int>(std::lround(std::clamp(stop.location, 0.0F, 1.0F) * 100.0F)));
        state->stop_opacity->setValue(
            static_cast<int>(std::lround(std::clamp(stop.opacity, 0.0F, 1.0F) * 100.0F)));
        state->remove_stop->setEnabled(state->alpha_stops.size() > 2U);
        midpoint_available = has_previous_stop(state->alpha_stops, state->selected_alpha_stop);
        state->stop_midpoint->setValue(
            static_cast<int>(std::lround(std::clamp(stop.midpoint, 0.05F, 0.95F) * 100.0F)));
      }
      state->stop_midpoint_label->setEnabled(midpoint_available);
      state->stop_midpoint->setEnabled(midpoint_available);
    };
    state->load = [state](const LayerStyleGradient& value) {
      state->close_stop_color_picker(true);
      state->loading = true;
      state->template_gradient = value;
      state->color_stops = value.color_stops;
      state->alpha_stops = value.alpha_stops;
      state->selected_color_stop = 0;
      state->selected_alpha_stop = -1;
      state->reverse->setChecked(value.reverse);
      state->dither->setChecked(value.dither);
      state->align->setChecked(value.align_with_layer);
      state->form_combo->setCurrentIndex(std::max(
          0, state->form_combo->findData(static_cast<int>(value.form))));
      state->smoothness->setValue(
          static_cast<int>(std::lround(value.smoothness * 100.0 / 4096.0)));
      state->noise_seed->setValue(static_cast<int>(std::min<std::uint32_t>(
          value.noise.seed, std::numeric_limits<int>::max())));
      state->noise_roughness->setValue(static_cast<int>(
          std::lround(value.noise.roughness * 100.0 / 4096.0)));
      state->noise_transparency->setChecked(value.noise.add_transparency);
      state->noise_restrict->setChecked(value.noise.restrict_colors);
      state->noise_color_model->setCurrentIndex(
          std::max(0, state->noise_color_model->findData(
                          static_cast<int>(value.noise.color_model))));
      for (std::size_t channel = 0; channel < state->noise_minimum.size();
           ++channel) {
        state->noise_minimum[channel]->setValue(value.noise.minimum[channel]);
        state->noise_maximum[channel]->setValue(value.noise.maximum[channel]);
      }
      state->interpolation->setCurrentIndex(
          std::max(0, state->interpolation->findData(
                          static_cast<int>(value.interpolation))));
      state->style_combo->setCurrentIndex(
          std::max(0, state->style_combo->findData(static_cast<int>(value.type))));
      state->angle->setValue(static_cast<int>(std::round(value.angle_degrees)));
      state->scale->setValue(static_cast<int>(std::round(value.scale * 100.0F)));
      state->offset_x->setValue(
          static_cast<int>(std::round(value.offset_x_percent)));
      state->offset_y->setValue(
          static_cast<int>(std::round(value.offset_y_percent)));
      state->solid_controls->setVisible(value.form ==
                                        GradientDefinitionForm::Solid);
      state->selected_stop_controls->setVisible(value.form ==
                                                GradientDefinitionForm::Solid);
      state->midpoint_controls->setVisible(value.form ==
                                           GradientDefinitionForm::Solid);
      state->stop_button_controls->setVisible(value.form ==
                                              GradientDefinitionForm::Solid);
      state->smoothness->parentWidget()->setVisible(
          value.form == GradientDefinitionForm::Solid);
      state->noise_controls->setVisible(value.form ==
                                        GradientDefinitionForm::Noise);
      state->update_previews();
      state->loading = false;
    };

    auto notify_changed = [state](bool immediate) {
      if (!state->loading && state->changed) {
        state->changed(immediate);
      }
    };
    state->editor->stop_selected = [state](int row) {
      state->selected_color_stop = row;
      state->selected_alpha_stop = -1;
      state->update_previews();
    };
    state->editor->opacity_stop_selected = [state](int row) {
      state->selected_alpha_stop = row;
      state->selected_color_stop = -1;
      state->update_previews();
    };
    state->editor->stop_location_changed = [state, notify_changed](int row, int location) {
      if (row < 0 || row >= static_cast<int>(state->color_stops.size())) {
        return;
      }
      state->color_stops[static_cast<std::size_t>(row)].location =
          static_cast<float>(std::clamp(location, 0, 100)) / 100.0F;
      state->update_previews();
      notify_changed(false);
    };
    state->editor->opacity_stop_location_changed = [state, notify_changed](int row, int location) {
      if (row < 0 || row >= static_cast<int>(state->alpha_stops.size())) {
        return;
      }
      state->alpha_stops[static_cast<std::size_t>(row)].location =
          static_cast<float>(std::clamp(location, 0, 100)) / 100.0F;
      state->update_previews();
      notify_changed(false);
    };
    state->editor->color_midpoint_changed = [state, notify_changed](int row, int midpoint) {
      if (row < 0 || row >= static_cast<int>(state->color_stops.size())) {
        return;
      }
      state->color_stops[static_cast<std::size_t>(row)].midpoint =
          static_cast<float>(std::clamp(midpoint, 5, 95)) / 100.0F;
      state->selected_color_stop = row;
      state->selected_alpha_stop = -1;
      state->update_previews();
      notify_changed(false);
    };
    state->editor->opacity_midpoint_changed = [state, notify_changed](int row, int midpoint) {
      if (row < 0 || row >= static_cast<int>(state->alpha_stops.size())) {
        return;
      }
      state->alpha_stops[static_cast<std::size_t>(row)].midpoint =
          static_cast<float>(std::clamp(midpoint, 5, 95)) / 100.0F;
      state->selected_alpha_stop = row;
      state->selected_color_stop = -1;
      state->update_previews();
      notify_changed(false);
    };
    state->editor->stop_color_picked = [state, notify_changed](int row, QColor color) {
      if (row < 0 || row >= static_cast<int>(state->color_stops.size()) || !color.isValid()) {
        return;
      }
      state->color_stops[static_cast<std::size_t>(row)].color =
          RgbColor{static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
                   static_cast<std::uint8_t>(color.blue())};
      state->update_previews();
      notify_changed(true);
    };
    state->editor->stop_add_requested = [state, notify_changed](GradientStop stop) {
      state->color_stops.push_back(GradientColorStop{std::clamp(stop.location, 0.0F, 1.0F),
                                                     RgbColor{stop.color.r, stop.color.g, stop.color.b}});
      state->selected_color_stop = static_cast<int>(state->color_stops.size()) - 1;
      state->selected_alpha_stop = -1;
      state->update_previews();
      notify_changed(true);
      return state->selected_color_stop;
    };
    state->editor->opacity_stop_add_requested = [state, notify_changed](GradientAlphaStop stop) {
      state->alpha_stops.push_back(
          GradientAlphaStop{std::clamp(stop.location, 0.0F, 1.0F), std::clamp(stop.opacity, 0.0F, 1.0F)});
      state->selected_alpha_stop = static_cast<int>(state->alpha_stops.size()) - 1;
      state->selected_color_stop = -1;
      state->update_previews();
      notify_changed(true);
      return state->selected_alpha_stop;
    };
    state->editor->stop_delete_requested = [state, notify_changed](int row) {
      if (state->color_stops.size() <= 2U || row < 0 || row >= static_cast<int>(state->color_stops.size())) {
        return;
      }
      state->close_stop_color_picker(true);
      state->color_stops.erase(state->color_stops.begin() + row);
      state->selected_color_stop = std::min(row, static_cast<int>(state->color_stops.size()) - 1);
      state->selected_alpha_stop = -1;
      state->update_previews();
      notify_changed(true);
    };
    state->editor->opacity_stop_delete_requested = [state, notify_changed](int row) {
      if (state->alpha_stops.size() <= 2U || row < 0 || row >= static_cast<int>(state->alpha_stops.size())) {
        return;
      }
      state->alpha_stops.erase(state->alpha_stops.begin() + row);
      state->selected_alpha_stop = std::min(row, static_cast<int>(state->alpha_stops.size()) - 1);
      state->selected_color_stop = -1;
      state->update_previews();
      notify_changed(true);
    };
    QObject::connect(state->stop_location, qOverload<int>(&QSpinBox::valueChanged), &dialog,
                     [state, notify_changed](int value) {
                       if (state->loading) {
                         return;
                       }
                       const auto location = static_cast<float>(std::clamp(value, 0, 100)) / 100.0F;
                       if (state->selected_color_stop >= 0 &&
                           state->selected_color_stop < static_cast<int>(state->color_stops.size())) {
                         state->color_stops[static_cast<std::size_t>(state->selected_color_stop)].location = location;
                       } else if (state->selected_alpha_stop >= 0 &&
                                  state->selected_alpha_stop < static_cast<int>(state->alpha_stops.size())) {
                         state->alpha_stops[static_cast<std::size_t>(state->selected_alpha_stop)].location = location;
                       } else {
                         return;
                       }
                       state->update_previews();
                       notify_changed(false);
                     });
    QObject::connect(state->stop_midpoint, qOverload<int>(&QSpinBox::valueChanged), &dialog,
                     [state, notify_changed](int value) {
                       if (state->loading || !state->stop_midpoint->isEnabled()) {
                         return;
                       }
                       const auto midpoint = static_cast<float>(std::clamp(value, 5, 95)) / 100.0F;
                       if (state->selected_color_stop >= 0 &&
                           state->selected_color_stop < static_cast<int>(state->color_stops.size())) {
                         state->color_stops[static_cast<std::size_t>(state->selected_color_stop)].midpoint = midpoint;
                       } else if (state->selected_alpha_stop >= 0 &&
                                  state->selected_alpha_stop < static_cast<int>(state->alpha_stops.size())) {
                         state->alpha_stops[static_cast<std::size_t>(state->selected_alpha_stop)].midpoint = midpoint;
                       } else {
                         return;
                       }
                       state->update_previews();
                       notify_changed(false);
                     });
    QObject::connect(state->stop_opacity, qOverload<int>(&QSpinBox::valueChanged), &dialog,
                     [state, notify_changed](int value) {
                       if (state->loading || state->selected_alpha_stop < 0 ||
                           state->selected_alpha_stop >= static_cast<int>(state->alpha_stops.size())) {
                         return;
                       }
                       state->alpha_stops[static_cast<std::size_t>(state->selected_alpha_stop)].opacity =
                           static_cast<float>(std::clamp(value, 0, 100)) / 100.0F;
                       state->update_previews();
                       notify_changed(false);
                     });
    QObject::connect(state->stop_hex, &QLineEdit::textEdited, &dialog, [state] {
      state->stop_hex->setProperty("appearanceUserTyped", true);
    });
    QObject::connect(state->stop_hex, &QLineEdit::editingFinished, &dialog, [state, notify_changed, mark_ramp_edit] {
      const bool user_typed = state->stop_hex->property("appearanceUserTyped").toBool();
      state->stop_hex->setProperty("appearanceUserTyped", false);
      if (state->loading || state->selected_color_stop < 0 ||
          state->selected_color_stop >= static_cast<int>(state->color_stops.size())) {
        return;
      }
      auto text = state->stop_hex->text().trimmed();
      if (!text.startsWith(QLatin1Char('#')) && text.size() == 6) {
        text.prepend(QLatin1Char('#'));
      }
      const QColor color(text);
      if (!color.isValid()) {
        state->update_previews();
        return;
      }
      state->color_stops[static_cast<std::size_t>(state->selected_color_stop)].color =
          RgbColor{static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
                   static_cast<std::uint8_t>(color.blue())};
      state->update_previews();
      if (user_typed) mark_ramp_edit();
      notify_changed(true);
    });
    // The picker edits the stop it opened for. It is non-modal, so selecting
    // another stop meanwhile closes it (update_previews) and keeps its color; a
    // request for that other stop opens the picker on it once the open picker's
    // loop has unwound (GitHub issue 216: the picker kept editing the first stop
    // while the dialog showed the second one selected).
    auto choose_stop_color = [&, state, notify_changed, color_picker_title, object_prefix] {
      if (state->selected_color_stop < 0 ||
          state->selected_color_stop >= static_cast<int>(state->color_stops.size())) {
        return;
      }
      if (state->color_picker_row >= 0) {
        if (state->selected_color_stop != state->color_picker_row) {
          state->pending_picker_row = state->selected_color_stop;
          state->close_stop_color_picker(false);
        } else if (auto* picker = open_color_picker_dialog(); picker != nullptr) {
          picker->raise();
          picker->activateWindow();
        }
        return;
      }
      const auto ramp_key = [&] {
        return ramp_property_key(object_prefix.contains(QStringLiteral("Stroke")));
      };
      for (int row = state->selected_color_stop; row >= 0;) {
        const auto original = state->color_stops[static_cast<std::size_t>(row)].color;
        const auto saved_edits = *batch_edits;
        bool selected = false;
        // Writes only while this picker still owns the stop: a preset load or a
        // stop deletion detaches it (color_picker_row = -1).
        auto apply_color = [state, row](QColor color) {
          if (!color.isValid() || state->color_picker_row != row ||
              row >= static_cast<int>(state->color_stops.size())) {
            return;
          }
          state->color_stops[static_cast<std::size_t>(row)].color =
              RgbColor{static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
                       static_cast<std::uint8_t>(color.blue())};
          state->update_previews();
        };
        state->color_picker_row = row;
        state->pending_picker_row = -1;
        const auto chosen = request_patchy_color(&dialog, QColor(original.red, original.green, original.blue),
                                                 color_picker_title, [&](QColor color) {
                                                   apply_color(color);
                                                   if (batch) explicit_fields = {ramp_key()};
                                                   notify_changed(false);
                                                 }, &selected);
        const bool detached = state->color_picker_row != row;
        if (!detached) {
          if (!chosen.has_value()) {
            apply_color(QColor(original.red, original.green, original.blue));
            *batch_edits = saved_edits;
            if (reset_batch_capture) reset_batch_capture();
            notify_changed(true);
          } else if (selected || !batch) {
            apply_color(*chosen);
            if (batch) explicit_fields = {ramp_key()};
            notify_changed(true);
          }
          state->color_picker_row = -1;
        }
        row = std::exchange(state->pending_picker_row, -1);
        if (row != state->selected_color_stop || row >= static_cast<int>(state->color_stops.size())) {
          row = -1;
        }
      }
    };
    QObject::connect(state->stop_swatch, &QPushButton::clicked, &dialog, choose_stop_color);
    state->editor->choose_stop_color_requested = [state, choose_stop_color](int row) {
      if (row < 0 || row >= static_cast<int>(state->color_stops.size())) {
        return;
      }
      state->selected_color_stop = row;
      state->selected_alpha_stop = -1;
      state->update_previews();
      choose_stop_color();
    };
    QObject::connect(state->add_stop, &QPushButton::clicked, &dialog, [state, notify_changed] {
      if (state->selected_alpha_stop >= 0) {
        auto stop = GradientAlphaStop{0.5F, 1.0F};
        if (!state->alpha_stops.empty()) {
          const auto source_index = static_cast<std::size_t>(
              std::clamp(state->selected_alpha_stop, 0, static_cast<int>(state->alpha_stops.size()) - 1));
          stop = state->alpha_stops[source_index];
          stop.location = std::clamp(stop.location + 0.1F, 0.0F, 1.0F);
        }
        state->alpha_stops.push_back(stop);
        state->selected_alpha_stop = static_cast<int>(state->alpha_stops.size()) - 1;
        state->selected_color_stop = -1;
      } else {
        auto stop = GradientColorStop{0.5F, RgbColor{255, 255, 255}};
        if (!state->color_stops.empty()) {
          const auto source_index = static_cast<std::size_t>(
              std::clamp(state->selected_color_stop, 0, static_cast<int>(state->color_stops.size()) - 1));
          stop = state->color_stops[source_index];
          stop.location = std::clamp(stop.location + 0.1F, 0.0F, 1.0F);
        }
        state->color_stops.push_back(stop);
        state->selected_color_stop = static_cast<int>(state->color_stops.size()) - 1;
        state->selected_alpha_stop = -1;
      }
      state->update_previews();
      notify_changed(true);
    });
    QObject::connect(state->remove_stop, &QPushButton::clicked, &dialog, [state, notify_changed] {
      if (state->selected_alpha_stop >= 0) {
        if (state->alpha_stops.size() <= 2U) {
          return;
        }
        const auto row =
            std::clamp(state->selected_alpha_stop, 0, static_cast<int>(state->alpha_stops.size()) - 1);
        state->alpha_stops.erase(state->alpha_stops.begin() + row);
        state->selected_alpha_stop = std::min(row, static_cast<int>(state->alpha_stops.size()) - 1);
      } else {
        if (state->color_stops.size() <= 2U) {
          return;
        }
        const auto row =
            std::clamp(state->selected_color_stop, 0, static_cast<int>(state->color_stops.size()) - 1);
        state->close_stop_color_picker(true);
        state->color_stops.erase(state->color_stops.begin() + row);
        state->selected_color_stop = std::min(row, static_cast<int>(state->color_stops.size()) - 1);
      }
      state->update_previews();
      notify_changed(true);
    });
    QObject::connect(state->reverse, &QCheckBox::toggled, &dialog,
                     [notify_changed](bool) { notify_changed(true); });
    QObject::connect(state->dither, &QCheckBox::toggled, &dialog,
                     [notify_changed](bool) { notify_changed(true); });
    QObject::connect(state->align, &QCheckBox::toggled, &dialog,
                     [notify_changed](bool) { notify_changed(true); });
    QObject::connect(state->form_combo, &QComboBox::currentIndexChanged,
                     &dialog, [state, notify_changed](int) {
                       const auto solid =
                           state->form_combo->currentData().toInt() ==
                           static_cast<int>(GradientDefinitionForm::Solid);
                       state->solid_controls->setVisible(solid);
                       state->selected_stop_controls->setVisible(solid);
                       state->midpoint_controls->setVisible(solid);
                       state->stop_button_controls->setVisible(solid);
                       state->smoothness->parentWidget()->setVisible(solid);
                       state->noise_controls->setVisible(!solid); notify_changed(true); });
    QObject::connect(state->interpolation, &QComboBox::currentIndexChanged, &dialog,
                     [notify_changed](int) { notify_changed(true); });
    QObject::connect(state->style_combo, &QComboBox::currentIndexChanged,
                     &dialog, [notify_changed](int) { notify_changed(true); });
    QObject::connect(state->angle, qOverload<int>(&QSpinBox::valueChanged),
                     &dialog, [notify_changed](int) { notify_changed(false); });
    QObject::connect(state->scale, qOverload<int>(&QSpinBox::valueChanged),
                     &dialog, [notify_changed](int) { notify_changed(false); });
    for (auto *spin :
         {state->smoothness, state->noise_seed, state->noise_roughness,
          state->offset_x, state->offset_y}) {
      QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), &dialog,
                       [notify_changed](int) { notify_changed(false); });
    }
    QObject::connect(state->noise_transparency, &QCheckBox::toggled, &dialog,
                     [notify_changed](bool) { notify_changed(true); });
    QObject::connect(state->noise_restrict, &QCheckBox::toggled, &dialog,
                     [notify_changed](bool) { notify_changed(true); });
    QObject::connect(state->noise_color_model, &QComboBox::currentIndexChanged,
                     &dialog, [notify_changed](int) { notify_changed(true); });
    for (std::size_t channel = 0; channel < state->noise_minimum.size();
         ++channel) {
      QObject::connect(state->noise_minimum[channel],
                       qOverload<int>(&QSpinBox::valueChanged), &dialog,
                       [state, notify_changed, channel](int value) {
                         if (value > state->noise_maximum[channel]->value())
                           state->noise_maximum[channel]->setValue(value);
                         notify_changed(false);
                       });
      QObject::connect(state->noise_maximum[channel],
                       qOverload<int>(&QSpinBox::valueChanged), &dialog,
                       [state, notify_changed, channel](int value) {
                         if (value < state->noise_minimum[channel]->value())
                           state->noise_minimum[channel]->setValue(value);
                         notify_changed(false);
                       });
    }
    QObject::connect(state->reset_alignment, &QPushButton::clicked, &dialog,
                     [state, notify_changed] {
                       state->offset_x->setValue(0);
                       state->offset_y->setValue(0);
                       notify_changed(true); });

    state->load(initial_gradient);
    return controls_state;
}

}  // namespace patchy::ui::layer_style_detail
