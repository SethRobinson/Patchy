// Shape appearance dialog: fill (none/solid/gradient/pattern) and stroke
// (width/paint/alignment/caps/joins/dashes) for shape and fill layers, with
// live preview through the caller's callback. Gradient presets come from the
// GradientLibrary; patterns list the document store first, then the library
// (adoption into the store happens in the caller when applying).
#include "ui/shape_appearance_dialog.hpp"

#include "core/pattern_resource.hpp"
#include "ui/color_panel.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/gradient_library.hpp"
#include "ui/pattern_library.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

QIcon color_swatch_icon(RgbColor color) {
  QPixmap pixmap(28, 14);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setPen(QColor(0, 0, 0, 160));
  painter.setBrush(QColor(color.red, color.green, color.blue));
  painter.drawRect(0, 0, 27, 13);
  return QIcon(pixmap);
}

// GRD presets may defer stops to the tool colors; shape fills store concrete
// colors, so resolve at pick time (the layer-style dialog's convention).
GradientDefinition resolve_gradient_definition(GradientDefinition definition, RgbColor foreground,
                                               RgbColor background) {
  for (auto& stop : definition.color_stops) {
    if (stop.kind == GradientColorStop::Kind::Foreground) {
      stop.color = foreground;
    } else if (stop.kind == GradientColorStop::Kind::Background) {
      stop.color = background;
    }
    stop.kind = GradientColorStop::Kind::User;
  }
  return definition;
}

// Dash presets, stored in stroke-width multiples like the vstk descriptor.
const std::vector<double>& dash_preset(int index) {
  static const std::vector<double> kSolid{};
  static const std::vector<double> kDashed{2.0, 2.0};
  static const std::vector<double> kDotted{0.0, 2.0};
  switch (index) {
    case 1:
      return kDashed;
    case 2:
      return kDotted;
    default:
      return kSolid;
  }
}

struct DialogState {
  ShapeAppearanceSettings settings;
  // Set when the user picks a stroke color; otherwise a gradient/pattern
  // stroke paint read from a PSD is preserved untouched.
  bool stroke_paint_touched{false};
  // A PSD-authored dash pattern that matches no preset, restorable after
  // trying the presets.
  std::vector<double> custom_dashes;
};

}  // namespace

std::optional<ShapeAppearanceSettings> request_shape_appearance_settings(
    QWidget* parent, std::function<void(const ShapeAppearanceSettings&)> preview_changed,
    ShapeAppearanceSettings initial, GradientLibrary* gradient_library,
    PatternLibrary* pattern_library, const PatternStore* document_patterns, RgbColor foreground,
    RgbColor background) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("shapeAppearanceDialog"));
  dialog.setWindowTitle(QObject::tr("Shape Appearance"));
  auto* dialog_layout = new QVBoxLayout(&dialog);

  auto state = std::make_shared<DialogState>();
  state->settings = std::move(initial);

  const auto notify = [state, preview_changed] {
    if (preview_changed) {
      preview_changed(state->settings);
    }
  };

  // --- Geometry (single live-shape layers only) ---
  if (state->settings.geometry.has_value()) {
    const auto kind = state->settings.geometry->kind;
    auto* geometry_group = new QGroupBox(QObject::tr("Geometry"), &dialog);
    auto* geometry_layout = new QVBoxLayout(geometry_group);
    geometry_layout->setContentsMargins(10, 8, 10, 8);
    geometry_layout->setSpacing(4);
    auto* geometry_form = new QFormLayout();
    geometry_form->setHorizontalSpacing(10);
    geometry_form->setVerticalSpacing(8);
    geometry_layout->addLayout(geometry_form);
    const auto make_spin = [&](const char* name, double minimum, double maximum, double value) {
      auto* spin = new QDoubleSpinBox(geometry_group);
      spin->setObjectName(QLatin1String(name));
      spin->setRange(minimum, maximum);
      spin->setDecimals(1);
      spin->setSuffix(QStringLiteral(" px"));
      spin->setValue(value);
      configure_dialog_spinbox(spin, 72);
      return spin;
    };
    const auto& geometry = *state->settings.geometry;
    if (kind == LiveShapeKind::Line) {
      auto* start_x = make_spin("shapeGeometryLineStartXSpin", -30000, 30000, geometry.line_start_x);
      auto* start_y = make_spin("shapeGeometryLineStartYSpin", -30000, 30000, geometry.line_start_y);
      auto* end_x = make_spin("shapeGeometryLineEndXSpin", -30000, 30000, geometry.line_end_x);
      auto* end_y = make_spin("shapeGeometryLineEndYSpin", -30000, 30000, geometry.line_end_y);
      auto* weight = make_spin("shapeGeometryLineWeightSpin", 0.5, 1000, geometry.line_weight);
      geometry_form->addRow(QObject::tr("Start X:"), start_x);
      geometry_form->addRow(QObject::tr("Start Y:"), start_y);
      geometry_form->addRow(QObject::tr("End X:"), end_x);
      geometry_form->addRow(QObject::tr("End Y:"), end_y);
      geometry_form->addRow(QObject::tr("Weight:"), weight);
      const auto apply_line = [state, notify, start_x, start_y, end_x, end_y, weight] {
        auto& params = *state->settings.geometry;
        params.line_start_x = start_x->value();
        params.line_start_y = start_y->value();
        params.line_end_x = end_x->value();
        params.line_end_y = end_y->value();
        params.line_weight = weight->value();
        if (params.arrow_start || params.arrow_end) {
          // Keep the default arrowhead proportions tied to the weight.
          params.arrow_width = params.line_weight * 5.0;
          params.arrow_length = params.line_weight * 10.0;
        }
        notify();
      };
      for (auto* spin : {start_x, start_y, end_x, end_y, weight}) {
        QObject::connect(spin, &QDoubleSpinBox::valueChanged, &dialog, apply_line);
      }
    } else {
      auto* x_spin = make_spin("shapeGeometryXSpin", -30000, 30000, geometry.left);
      auto* y_spin = make_spin("shapeGeometryYSpin", -30000, 30000, geometry.top);
      auto* width_spin =
          make_spin("shapeGeometryWidthSpin", 0.5, 60000, geometry.right - geometry.left);
      auto* height_spin =
          make_spin("shapeGeometryHeightSpin", 0.5, 60000, geometry.bottom - geometry.top);
      geometry_form->addRow(QObject::tr("X:"), x_spin);
      geometry_form->addRow(QObject::tr("Y:"), y_spin);
      geometry_form->addRow(QObject::tr("Width:"), width_spin);
      geometry_form->addRow(QObject::tr("Height:"), height_spin);
      std::array<QDoubleSpinBox*, 4> radius_spins{nullptr, nullptr, nullptr, nullptr};
      if (kind == LiveShapeKind::Rectangle || kind == LiveShapeKind::RoundedRectangle) {
        // Model order TL, TR, BR, BL; a radius on a plain rect promotes it to
        // a rounded rect (the generator clamps oversized values).
        const std::array<const char*, 4> names{
            "shapeGeometryRadiusTopLeftSpin", "shapeGeometryRadiusTopRightSpin",
            "shapeGeometryRadiusBottomRightSpin", "shapeGeometryRadiusBottomLeftSpin"};
        const std::array<QString, 4> labels{
            QObject::tr("Top left radius:"), QObject::tr("Top right radius:"),
            QObject::tr("Bottom right radius:"), QObject::tr("Bottom left radius:")};
        for (std::size_t corner = 0; corner < 4; ++corner) {
          radius_spins[corner] =
              make_spin(names[corner], 0, 30000, geometry.corner_radii[corner]);
          geometry_form->addRow(labels[corner], radius_spins[corner]);
        }
      }
      const auto apply_box = [state, notify, x_spin, y_spin, width_spin, height_spin,
                              radius_spins] {
        auto& params = *state->settings.geometry;
        params.left = x_spin->value();
        params.top = y_spin->value();
        params.right = x_spin->value() + width_spin->value();
        params.bottom = y_spin->value() + height_spin->value();
        if (radius_spins[0] != nullptr) {
          for (std::size_t corner = 0; corner < 4; ++corner) {
            params.corner_radii[corner] = radius_spins[corner]->value();
          }
          const bool any_radius =
              params.corner_radii[0] > 0.0 || params.corner_radii[1] > 0.0 ||
              params.corner_radii[2] > 0.0 || params.corner_radii[3] > 0.0;
          if (params.kind == LiveShapeKind::Rectangle && any_radius) {
            params.kind = LiveShapeKind::RoundedRectangle;
          }
        }
        notify();
      };
      for (auto* spin : {x_spin, y_spin, width_spin, height_spin}) {
        QObject::connect(spin, &QDoubleSpinBox::valueChanged, &dialog, apply_box);
      }
      for (auto* spin : radius_spins) {
        if (spin != nullptr) {
          QObject::connect(spin, &QDoubleSpinBox::valueChanged, &dialog, apply_box);
        }
      }
    }
    dialog_layout->addWidget(geometry_group);
  }

  // --- Fill ---
  auto* fill_group = new QGroupBox(QObject::tr("Fill"), &dialog);
  auto* fill_layout = new QVBoxLayout(fill_group);
  fill_layout->setContentsMargins(10, 8, 10, 8);
  fill_layout->setSpacing(4);
  auto* fill_form = new QFormLayout();
  fill_form->setHorizontalSpacing(10);
  fill_form->setVerticalSpacing(8);
  fill_layout->addLayout(fill_form);

  auto* fill_kind_combo = new QComboBox(fill_group);
  fill_kind_combo->setObjectName(QStringLiteral("shapeFillKindCombo"));
  fill_kind_combo->addItem(QObject::tr("No Fill"), static_cast<int>(VectorFillKind::None));
  fill_kind_combo->addItem(QObject::tr("Solid Color"), static_cast<int>(VectorFillKind::Solid));
  fill_kind_combo->addItem(QObject::tr("Gradient"), static_cast<int>(VectorFillKind::Gradient));
  fill_kind_combo->addItem(QObject::tr("Pattern"), static_cast<int>(VectorFillKind::Pattern));
  fill_form->addRow(QObject::tr("Type:"), fill_kind_combo);

  auto* fill_color_button = new QPushButton(fill_group);
  fill_color_button->setObjectName(QStringLiteral("shapeFillColorButton"));
  fill_color_button->setIcon(color_swatch_icon(state->settings.fill.color));
  fill_color_button->setText(QObject::tr("Color..."));
  auto* fill_color_row = fill_color_button;
  fill_form->addRow(QObject::tr("Color:"), fill_color_row);

  auto* fill_gradient_combo = new QComboBox(fill_group);
  fill_gradient_combo->setObjectName(QStringLiteral("shapeFillGradientCombo"));
  fill_gradient_combo->setIconSize(QSize(64, 16));
  if (gradient_library != nullptr) {
    for (const auto& entry : gradient_library->entries()) {
      fill_gradient_combo->addItem(QIcon(entry.thumbnail), entry.name, entry.storage_id);
    }
  }
  fill_form->addRow(QObject::tr("Gradient:"), fill_gradient_combo);

  auto* gradient_type_combo = new QComboBox(fill_group);
  gradient_type_combo->setObjectName(QStringLiteral("shapeGradientTypeCombo"));
  gradient_type_combo->addItem(QObject::tr("Linear"), static_cast<int>(LayerStyleGradientType::Linear));
  gradient_type_combo->addItem(QObject::tr("Radial"), static_cast<int>(LayerStyleGradientType::Radial));
  gradient_type_combo->addItem(QObject::tr("Angle"), static_cast<int>(LayerStyleGradientType::Angle));
  gradient_type_combo->addItem(QObject::tr("Reflected"),
                               static_cast<int>(LayerStyleGradientType::Reflected));
  gradient_type_combo->addItem(QObject::tr("Diamond"),
                               static_cast<int>(LayerStyleGradientType::Diamond));
  fill_form->addRow(QObject::tr("Style:"), gradient_type_combo);

  auto* gradient_angle_spin = new QSpinBox(fill_group);
  gradient_angle_spin->setObjectName(QStringLiteral("shapeGradientAngleSpin"));
  gradient_angle_spin->setRange(-180, 180);
  gradient_angle_spin->setSuffix(QStringLiteral("°"));
  configure_dialog_spinbox(gradient_angle_spin, 72);
  fill_form->addRow(QObject::tr("Angle:"), gradient_angle_spin);

  auto* gradient_scale_spin = new QSpinBox(fill_group);
  gradient_scale_spin->setObjectName(QStringLiteral("shapeGradientScaleSpin"));
  gradient_scale_spin->setRange(10, 1000);
  gradient_scale_spin->setSuffix(QStringLiteral("%"));
  configure_dialog_spinbox(gradient_scale_spin, 72);
  fill_form->addRow(QObject::tr("Scale:"), gradient_scale_spin);

  auto* gradient_reverse_check = new QCheckBox(QObject::tr("Reverse"), fill_group);
  gradient_reverse_check->setObjectName(QStringLiteral("shapeGradientReverseCheck"));
  fill_form->addRow(QString(), gradient_reverse_check);

  auto* fill_pattern_combo = new QComboBox(fill_group);
  fill_pattern_combo->setObjectName(QStringLiteral("shapeFillPatternCombo"));
  fill_pattern_combo->setIconSize(QSize(24, 24));
  if (document_patterns != nullptr) {
    for (const auto& resource : document_patterns->patterns) {
      const auto name = resource.name.empty() ? QObject::tr("Embedded pattern")
                                              : QString::fromStdString(resource.name);
      fill_pattern_combo->addItem(name, QString::fromStdString(resource.id));
    }
  }
  if (pattern_library != nullptr) {
    for (const auto& entry : pattern_library->entries()) {
      if (fill_pattern_combo->findData(entry.id) < 0) {
        fill_pattern_combo->addItem(QIcon(entry.thumbnail), entry.name, entry.id);
      }
    }
  }
  fill_form->addRow(QObject::tr("Pattern:"), fill_pattern_combo);

  auto* pattern_scale_spin = new QSpinBox(fill_group);
  pattern_scale_spin->setObjectName(QStringLiteral("shapePatternScaleSpin"));
  pattern_scale_spin->setRange(1, 1000);
  pattern_scale_spin->setSuffix(QStringLiteral("%"));
  configure_dialog_spinbox(pattern_scale_spin, 72);
  fill_form->addRow(QObject::tr("Scale:"), pattern_scale_spin);

  dialog_layout->addWidget(fill_group);

  // --- Stroke ---
  auto* stroke_group = new QGroupBox(QObject::tr("Stroke"), &dialog);
  auto* stroke_layout = new QVBoxLayout(stroke_group);
  stroke_layout->setContentsMargins(10, 8, 10, 8);
  stroke_layout->setSpacing(4);
  auto* stroke_form = new QFormLayout();
  stroke_form->setHorizontalSpacing(10);
  stroke_form->setVerticalSpacing(8);
  stroke_layout->addLayout(stroke_form);

  auto* stroke_check = new QCheckBox(QObject::tr("Stroke the shape outline"), stroke_group);
  stroke_check->setObjectName(QStringLiteral("shapeStrokeCheck"));
  stroke_check->setChecked(state->settings.stroke.enabled);
  stroke_form->addRow(QString(), stroke_check);

  auto* stroke_width_spin = new QDoubleSpinBox(stroke_group);
  stroke_width_spin->setObjectName(QStringLiteral("shapeStrokeWidthSpin"));
  stroke_width_spin->setRange(0.1, 1000.0);
  stroke_width_spin->setDecimals(1);
  stroke_width_spin->setSuffix(QStringLiteral(" px"));
  stroke_width_spin->setValue(state->settings.stroke.width);
  configure_dialog_spinbox(stroke_width_spin, 80);
  stroke_form->addRow(QObject::tr("Width:"), stroke_width_spin);

  auto* stroke_color_button = new QPushButton(stroke_group);
  stroke_color_button->setObjectName(QStringLiteral("shapeStrokeColorButton"));
  stroke_color_button->setIcon(color_swatch_icon(state->settings.stroke.content.color));
  stroke_color_button->setText(QObject::tr("Color..."));
  stroke_form->addRow(QObject::tr("Color:"), stroke_color_button);

  auto* stroke_align_combo = new QComboBox(stroke_group);
  stroke_align_combo->setObjectName(QStringLiteral("shapeStrokeAlignCombo"));
  stroke_align_combo->addItem(QObject::tr("Inside"),
                              static_cast<int>(VectorStrokeAlignment::Inside));
  stroke_align_combo->addItem(QObject::tr("Center"),
                              static_cast<int>(VectorStrokeAlignment::Center));
  stroke_align_combo->addItem(QObject::tr("Outside"),
                              static_cast<int>(VectorStrokeAlignment::Outside));
  stroke_form->addRow(QObject::tr("Align:"), stroke_align_combo);

  auto* stroke_cap_combo = new QComboBox(stroke_group);
  stroke_cap_combo->setObjectName(QStringLiteral("shapeStrokeCapCombo"));
  stroke_cap_combo->addItem(QObject::tr("Butt"), static_cast<int>(VectorStrokeCap::Butt));
  stroke_cap_combo->addItem(QObject::tr("Round"), static_cast<int>(VectorStrokeCap::Round));
  stroke_cap_combo->addItem(QObject::tr("Square", "stroke cap"),
                            static_cast<int>(VectorStrokeCap::Square));
  stroke_form->addRow(QObject::tr("Caps:"), stroke_cap_combo);

  auto* stroke_join_combo = new QComboBox(stroke_group);
  stroke_join_combo->setObjectName(QStringLiteral("shapeStrokeJoinCombo"));
  stroke_join_combo->addItem(QObject::tr("Miter"), static_cast<int>(VectorStrokeJoin::Miter));
  stroke_join_combo->addItem(QObject::tr("Round"), static_cast<int>(VectorStrokeJoin::Round));
  stroke_join_combo->addItem(QObject::tr("Bevel"), static_cast<int>(VectorStrokeJoin::Bevel));
  stroke_form->addRow(QObject::tr("Corners:"), stroke_join_combo);

  auto* stroke_dash_combo = new QComboBox(stroke_group);
  stroke_dash_combo->setObjectName(QStringLiteral("shapeStrokeDashCombo"));
  stroke_dash_combo->addItem(QObject::tr("Solid"));
  stroke_dash_combo->addItem(QObject::tr("Dashed"));
  stroke_dash_combo->addItem(QObject::tr("Dotted"));
  stroke_form->addRow(QObject::tr("Dashes:"), stroke_dash_combo);

  dialog_layout->addWidget(stroke_group);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  dialog_layout->addWidget(buttons);

  // Per-kind row visibility.
  const auto refresh_fill_rows = [=] {
    const auto kind = static_cast<VectorFillKind>(fill_kind_combo->currentData().toInt());
    const bool solid = kind == VectorFillKind::Solid;
    const bool gradient = kind == VectorFillKind::Gradient;
    const bool pattern = kind == VectorFillKind::Pattern;
    const auto set_row = [fill_form](QWidget* field, bool visible) {
      field->setVisible(visible);
      if (auto* label = fill_form->labelForField(field); label != nullptr) {
        label->setVisible(visible);
      }
    };
    set_row(fill_color_row, solid);
    set_row(fill_gradient_combo, gradient);
    set_row(gradient_type_combo, gradient);
    set_row(gradient_angle_spin, gradient);
    set_row(gradient_scale_spin, gradient);
    set_row(gradient_reverse_check, gradient);
    set_row(fill_pattern_combo, pattern);
    set_row(pattern_scale_spin, pattern);
  };

  // The stroke rows only apply while the stroke is enabled; grey them out
  // rather than hiding so the dialog never changes height under the pointer.
  const auto refresh_stroke_rows = [=] {
    const bool enabled = stroke_check->isChecked();
    const auto set_row = [stroke_form](QWidget* field, bool row_enabled) {
      field->setEnabled(row_enabled);
      if (auto* label = stroke_form->labelForField(field); label != nullptr) {
        label->setEnabled(row_enabled);
      }
    };
    set_row(stroke_width_spin, enabled);
    set_row(stroke_color_button, enabled);
    set_row(stroke_align_combo, enabled);
    set_row(stroke_cap_combo, enabled);
    set_row(stroke_join_combo, enabled);
    set_row(stroke_dash_combo, enabled);
  };

  const auto sync_gradient_controls = [=] {
    QSignalBlocker type_blocker(gradient_type_combo);
    QSignalBlocker angle_blocker(gradient_angle_spin);
    QSignalBlocker scale_blocker(gradient_scale_spin);
    QSignalBlocker reverse_blocker(gradient_reverse_check);
    const auto& gradient = state->settings.fill.gradient;
    const auto type_index = gradient_type_combo->findData(static_cast<int>(gradient.type));
    gradient_type_combo->setCurrentIndex(std::max(0, type_index));
    gradient_angle_spin->setValue(static_cast<int>(std::lround(gradient.angle_degrees)));
    gradient_scale_spin->setValue(
        std::clamp(static_cast<int>(std::lround(gradient.scale * 100.0F)), 10, 1000));
    gradient_reverse_check->setChecked(gradient.reverse);
  };

  // Initial control state from the settings.
  {
    QSignalBlocker kind_blocker(fill_kind_combo);
    const auto kind_index = fill_kind_combo->findData(static_cast<int>(state->settings.fill.kind));
    fill_kind_combo->setCurrentIndex(std::max(0, kind_index));
    if (!state->settings.fill.pattern_id.empty()) {
      const auto pattern_index =
          fill_pattern_combo->findData(QString::fromStdString(state->settings.fill.pattern_id));
      if (pattern_index >= 0) {
        QSignalBlocker pattern_blocker(fill_pattern_combo);
        fill_pattern_combo->setCurrentIndex(pattern_index);
      }
    }
    QSignalBlocker pattern_scale_blocker(pattern_scale_spin);
    pattern_scale_spin->setValue(std::clamp(
        static_cast<int>(std::lround(state->settings.fill.pattern_scale * 100.0)), 1, 1000));
    QSignalBlocker align_blocker(stroke_align_combo);
    stroke_align_combo->setCurrentIndex(std::max(
        0, stroke_align_combo->findData(static_cast<int>(state->settings.stroke.alignment))));
    QSignalBlocker cap_blocker(stroke_cap_combo);
    stroke_cap_combo->setCurrentIndex(
        std::max(0, stroke_cap_combo->findData(static_cast<int>(state->settings.stroke.cap))));
    QSignalBlocker join_blocker(stroke_join_combo);
    stroke_join_combo->setCurrentIndex(
        std::max(0, stroke_join_combo->findData(static_cast<int>(state->settings.stroke.join))));
    QSignalBlocker dash_blocker(stroke_dash_combo);
    int dash_index = 0;
    for (int preset = 0; preset < 3; ++preset) {
      if (state->settings.stroke.dashes == dash_preset(preset)) {
        dash_index = preset;
        break;
      }
    }
    if (!state->settings.stroke.dashes.empty() &&
        state->settings.stroke.dashes != dash_preset(1) &&
        state->settings.stroke.dashes != dash_preset(2)) {
      // Preserve a custom dash pattern read from a PSD as its own entry.
      state->custom_dashes = state->settings.stroke.dashes;
      stroke_dash_combo->addItem(QObject::tr("Custom"));
      dash_index = 3;
    }
    stroke_dash_combo->setCurrentIndex(dash_index);
  }
  sync_gradient_controls();
  refresh_fill_rows();
  refresh_stroke_rows();

  // --- Wiring ---
  QObject::connect(fill_kind_combo, &QComboBox::currentIndexChanged, &dialog, [=](int) {
    state->settings.fill.kind =
        static_cast<VectorFillKind>(fill_kind_combo->currentData().toInt());
    if (state->settings.fill.kind == VectorFillKind::Gradient &&
        state->settings.fill.gradient.color_stops.empty()) {
      // First switch to Gradient: seed from the selected preset (or FG->BG).
      if (gradient_library != nullptr && fill_gradient_combo->count() > 0) {
        if (const auto* entry =
                gradient_library->find_entry(fill_gradient_combo->currentData().toString());
            entry != nullptr) {
          static_cast<GradientDefinition&>(state->settings.fill.gradient) =
              resolve_gradient_definition(entry->definition, foreground, background);
        }
      }
      if (state->settings.fill.gradient.color_stops.empty()) {
        auto& gradient = state->settings.fill.gradient;
        gradient.color_stops = {GradientColorStop{0.0F, foreground, 0.5F},
                                GradientColorStop{1.0F, background, 0.5F}};
        gradient.alpha_stops = {GradientAlphaStop{0.0F, 1.0F, 0.5F},
                                GradientAlphaStop{1.0F, 1.0F, 0.5F}};
      }
      sync_gradient_controls();
    }
    if (state->settings.fill.kind == VectorFillKind::Pattern &&
        state->settings.fill.pattern_id.empty() && fill_pattern_combo->count() > 0) {
      state->settings.fill.pattern_id = fill_pattern_combo->currentData().toString().toStdString();
      state->settings.fill.pattern_name = fill_pattern_combo->currentText().toStdString();
    }
    refresh_fill_rows();
    notify();
  });
  QObject::connect(fill_color_button, &QPushButton::clicked, &dialog, [=, &dialog] {
    const auto& current = state->settings.fill.color;
    const auto chosen = request_patchy_color(
        &dialog, QColor(current.red, current.green, current.blue), QObject::tr("Shape Fill Color"));
    if (chosen.has_value()) {
      state->settings.fill.color = RgbColor{static_cast<std::uint8_t>(chosen->red()),
                                            static_cast<std::uint8_t>(chosen->green()),
                                            static_cast<std::uint8_t>(chosen->blue())};
      fill_color_button->setIcon(color_swatch_icon(state->settings.fill.color));
      notify();
    }
  });
  QObject::connect(fill_gradient_combo, &QComboBox::currentIndexChanged, &dialog, [=](int) {
    if (gradient_library == nullptr) {
      return;
    }
    if (const auto* entry =
            gradient_library->find_entry(fill_gradient_combo->currentData().toString());
        entry != nullptr) {
      // Presets replace the definition; the geometry (type/angle/scale/
      // reverse) is the user's and stays.
      static_cast<GradientDefinition&>(state->settings.fill.gradient) =
          resolve_gradient_definition(entry->definition, foreground, background);
      notify();
    }
  });
  QObject::connect(gradient_type_combo, &QComboBox::currentIndexChanged, &dialog, [=](int) {
    state->settings.fill.gradient.type =
        static_cast<LayerStyleGradientType>(gradient_type_combo->currentData().toInt());
    notify();
  });
  QObject::connect(gradient_angle_spin, &QSpinBox::valueChanged, &dialog, [=](int value) {
    state->settings.fill.gradient.angle_degrees = static_cast<float>(value);
    notify();
  });
  QObject::connect(gradient_scale_spin, &QSpinBox::valueChanged, &dialog, [=](int value) {
    state->settings.fill.gradient.scale = static_cast<float>(value) / 100.0F;
    notify();
  });
  QObject::connect(gradient_reverse_check, &QCheckBox::toggled, &dialog, [=](bool checked) {
    state->settings.fill.gradient.reverse = checked;
    notify();
  });
  QObject::connect(fill_pattern_combo, &QComboBox::currentIndexChanged, &dialog, [=](int) {
    state->settings.fill.pattern_id = fill_pattern_combo->currentData().toString().toStdString();
    state->settings.fill.pattern_name = fill_pattern_combo->currentText().toStdString();
    notify();
  });
  QObject::connect(pattern_scale_spin, &QSpinBox::valueChanged, &dialog, [=](int value) {
    state->settings.fill.pattern_scale = static_cast<double>(value) / 100.0;
    notify();
  });
  QObject::connect(stroke_check, &QCheckBox::toggled, &dialog, [=](bool checked) {
    state->settings.stroke.enabled = checked;
    refresh_stroke_rows();
    notify();
  });
  QObject::connect(stroke_width_spin, &QDoubleSpinBox::valueChanged, &dialog, [=](double value) {
    state->settings.stroke.width = value;
    notify();
  });
  QObject::connect(stroke_color_button, &QPushButton::clicked, &dialog, [=, &dialog] {
    const auto& current = state->settings.stroke.content.color;
    const auto chosen =
        request_patchy_color(&dialog, QColor(current.red, current.green, current.blue),
                             QObject::tr("Shape Stroke Color"));
    if (chosen.has_value()) {
      state->settings.stroke.content.kind = VectorFillKind::Solid;
      state->settings.stroke.content.color =
          RgbColor{static_cast<std::uint8_t>(chosen->red()),
                   static_cast<std::uint8_t>(chosen->green()),
                   static_cast<std::uint8_t>(chosen->blue())};
      state->stroke_paint_touched = true;
      stroke_color_button->setIcon(color_swatch_icon(state->settings.stroke.content.color));
      notify();
    }
  });
  QObject::connect(stroke_align_combo, &QComboBox::currentIndexChanged, &dialog, [=](int) {
    state->settings.stroke.alignment =
        static_cast<VectorStrokeAlignment>(stroke_align_combo->currentData().toInt());
    notify();
  });
  QObject::connect(stroke_cap_combo, &QComboBox::currentIndexChanged, &dialog, [=](int) {
    state->settings.stroke.cap = static_cast<VectorStrokeCap>(stroke_cap_combo->currentData().toInt());
    notify();
  });
  QObject::connect(stroke_join_combo, &QComboBox::currentIndexChanged, &dialog, [=](int) {
    state->settings.stroke.join =
        static_cast<VectorStrokeJoin>(stroke_join_combo->currentData().toInt());
    notify();
  });
  QObject::connect(stroke_dash_combo, &QComboBox::currentIndexChanged, &dialog, [=](int index) {
    state->settings.stroke.dashes = index <= 2 ? dash_preset(index) : state->custom_dashes;
    notify();
  });

  if (run_non_modal_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  return state->settings;
}

}  // namespace patchy::ui
