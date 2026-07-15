#include "ui/brush_dynamics_popup.hpp"

#include "ui/brush_tip_library.hpp"
#include "ui/dialog_utils.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <tuple>
#include <utility>

namespace patchy::ui {

namespace {

// Percent fraction <-> spin value helpers (BrushDynamics stores 0..1 fractions).
[[nodiscard]] int percent_from_fraction(double fraction) {
  return std::clamp(static_cast<int>(std::lround(fraction * 100.0)), 0, 1000);
}

[[nodiscard]] double fraction_from_percent(int percent) {
  return static_cast<double>(percent) / 100.0;
}

[[nodiscard]] int normalized_angle_value(double degrees) {
  auto normalized = std::fmod(degrees, 360.0);
  if (normalized > 180.0) {
    normalized -= 360.0;
  } else if (normalized < -180.0) {
    normalized += 360.0;
  }
  return static_cast<int>(std::lround(normalized));
}

// True for the control sources that actually read the pen / fade (mirrors the core's gating;
// Off and GlobalDefault only decide global-preference precedence).
[[nodiscard]] bool control_has_source(patchy::BrushDynamicControl control) {
  switch (control) {
    case patchy::BrushDynamicControl::Fade:
    case patchy::BrushDynamicControl::PenPressure:
    case patchy::BrushDynamicControl::PenTilt:
    case patchy::BrushDynamicControl::PenRotation:
    case patchy::BrushDynamicControl::StylusWheel:
      return true;
    default:
      return false;
  }
}

[[nodiscard]] patchy::BrushDynamicControl combo_control(const QComboBox& combo) {
  return static_cast<patchy::BrushDynamicControl>(
      combo.currentData().toInt());
}

void select_combo_control(QComboBox& combo, patchy::BrushDynamicControl control) {
  auto index = combo.findData(static_cast<int>(control));
  if (index < 0) {
    index = 0;  // sanitized upstream; first item is the slot's default
  }
  combo.setCurrentIndex(index);
}

// macOS: QMacStyle's Aqua layout spacings/margins are far roomier than the
// Windows metrics this dense panel was designed around, which pushed the popup
// past the screen height. Pin Windows-like metrics there; other platforms keep
// their style defaults (Windows rendering must not move).
void compact_group_grid(QGridLayout* grid) {
#ifdef Q_OS_MACOS
  grid->setHorizontalSpacing(8);
  grid->setVerticalSpacing(6);
  grid->setContentsMargins(9, 4, 9, 9);
#else
  Q_UNUSED(grid);
#endif
}

}  // namespace

BrushDynamicsPanel::BrushDynamicsPanel(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("brushDynamicsPanel"));
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(8, 8, 8, 8);
  layout->setSpacing(6);

  const auto add_percent_row = [this](QGridLayout* grid, int row, const QString& label,
                                      const QString& object_name, int maximum) -> QSpinBox* {
    auto* text = new QLabel(label, this);
    auto* slider = new QSlider(Qt::Horizontal, this);
    slider->setObjectName(object_name + QStringLiteral("Slider"));
    slider->setRange(0, maximum);
    slider->setMinimumWidth(120);
    auto* spin = new QSpinBox(this);
    spin->setObjectName(object_name);
    spin->setRange(0, maximum);
    spin->setSuffix(QStringLiteral("%"));
    QObject::connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
    QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), slider, &QSlider::setValue);
    grid->addWidget(text, row, 0);
    grid->addWidget(slider, row, 1);
    grid->addWidget(spin, row, 2);
    return spin;
  };

  // A "Control:" combo plus its fade-steps spin (shown only while the combo says Fade). The
  // items carry the enum in their data so display order stays decoupled from the enum values;
  // with_global lists "Use Global Pen Setting" first (size/roundness/opacity only).
  const auto add_control_row = [this](QGridLayout* grid, int row, const QString& label,
                                      const QString& combo_name, const QString& fade_name,
                                      bool with_global) -> std::pair<QComboBox*, QSpinBox*> {
    grid->addWidget(new QLabel(label, this), row, 0);
    auto* combo = new QComboBox(this);
    combo->setObjectName(combo_name);
    if (with_global) {
      combo->addItem(tr("Use Global Pen Setting"),
                     static_cast<int>(patchy::BrushDynamicControl::GlobalDefault));
    }
    combo->addItem(tr("Off"), static_cast<int>(patchy::BrushDynamicControl::Off));
    combo->addItem(tr("Fade"), static_cast<int>(patchy::BrushDynamicControl::Fade));
    combo->addItem(tr("Pen Pressure"), static_cast<int>(patchy::BrushDynamicControl::PenPressure));
    combo->addItem(tr("Pen Tilt"), static_cast<int>(patchy::BrushDynamicControl::PenTilt));
    combo->addItem(tr("Stylus Wheel"), static_cast<int>(patchy::BrushDynamicControl::StylusWheel));
    combo->addItem(tr("Pen Rotation"), static_cast<int>(patchy::BrushDynamicControl::PenRotation));
    auto* fade_spin = new QSpinBox(this);
    fade_spin->setObjectName(fade_name);
    fade_spin->setRange(1, 9999);
    fade_spin->setValue(25);
    fade_spin->setToolTip(tr("Spacing steps to fade over"));
    fade_spin->setVisible(false);
    auto* row_layout = new QHBoxLayout();
    row_layout->addWidget(combo);
    row_layout->addWidget(fade_spin);
    row_layout->addStretch(1);
    grid->addLayout(row_layout, row, 1, 1, 2);
    return {combo, fade_spin};
  };

  // Tip Shape: the static Photoshop "Brush Tip Shape" angle/roundness.
  auto* shape_group = new QGroupBox(tr("Tip Shape"), this);
  auto* shape_grid = new QGridLayout(shape_group);
  compact_group_grid(shape_grid);
  shape_grid->addWidget(new QLabel(tr("Angle:"), this), 0, 0);
  base_angle_spin_ = new QSpinBox(this);
  base_angle_spin_->setObjectName(QStringLiteral("dynamicsBaseAngleSpin"));
  base_angle_spin_->setRange(-180, 180);
  base_angle_spin_->setSuffix(QStringLiteral("°"));
  shape_grid->addWidget(base_angle_spin_, 0, 1);
  shape_grid->addWidget(new QLabel(tr("Roundness:"), this), 0, 2);
  base_roundness_spin_ = new QSpinBox(this);
  base_roundness_spin_->setObjectName(QStringLiteral("dynamicsBaseRoundnessSpin"));
  base_roundness_spin_->setRange(1, 100);
  base_roundness_spin_->setSuffix(QStringLiteral("%"));
  base_roundness_spin_->setValue(100);
  shape_grid->addWidget(base_roundness_spin_, 0, 3);
  shape_grid->setColumnStretch(4, 1);
  layout->addWidget(shape_group);

  // Shape Dynamics.
  auto* dynamics_group = new QGroupBox(tr("Shape Dynamics"), this);
  auto* dynamics_grid = new QGridLayout(dynamics_group);
  compact_group_grid(dynamics_grid);
  size_jitter_spin_ =
      add_percent_row(dynamics_grid, 0, tr("Size Jitter:"), QStringLiteral("dynamicsSizeJitterSpin"), 100);
  minimum_diameter_spin_ = add_percent_row(dynamics_grid, 1, tr("Minimum Diameter:"),
                                           QStringLiteral("dynamicsMinimumDiameterSpin"), 100);
  std::tie(size_control_combo_, size_fade_steps_spin_) =
      add_control_row(dynamics_grid, 2, tr("Size Control:"), QStringLiteral("dynamicsSizeControlCombo"),
                      QStringLiteral("dynamicsSizeFadeStepsSpin"), true);
  angle_jitter_spin_ =
      add_percent_row(dynamics_grid, 3, tr("Angle Jitter:"), QStringLiteral("dynamicsAngleJitterSpin"), 100);
  dynamics_grid->addWidget(new QLabel(tr("Angle Control:"), this), 4, 0);
  // The angle combo predates the data-mapped ones: its item indices equal the enum values
  // (tests and set_values rely on that), so new sources append in enum order.
  angle_control_combo_ = new QComboBox(this);
  angle_control_combo_->setObjectName(QStringLiteral("dynamicsAngleControlCombo"));
  angle_control_combo_->addItem(tr("Off"));
  angle_control_combo_->addItem(tr("Fade"));
  angle_control_combo_->addItem(tr("Pen Pressure"));
  angle_control_combo_->addItem(tr("Pen Tilt"));
  angle_control_combo_->addItem(tr("Pen Rotation"));
  angle_control_combo_->addItem(tr("Initial Direction"));
  angle_control_combo_->addItem(tr("Direction"));
  angle_control_combo_->addItem(tr("Stylus Wheel"));
  fade_steps_spin_ = new QSpinBox(this);
  fade_steps_spin_->setObjectName(QStringLiteral("dynamicsFadeStepsSpin"));
  fade_steps_spin_->setRange(1, 9999);
  fade_steps_spin_->setValue(25);
  fade_steps_spin_->setToolTip(tr("Spacing steps to fade over"));
  fade_steps_spin_->setVisible(false);
  auto* control_row = new QHBoxLayout();
  control_row->addWidget(angle_control_combo_);
  control_row->addWidget(fade_steps_spin_);
  control_row->addStretch(1);
  dynamics_grid->addLayout(control_row, 4, 1, 1, 2);
  roundness_jitter_spin_ = add_percent_row(dynamics_grid, 5, tr("Roundness Jitter:"),
                                           QStringLiteral("dynamicsRoundnessJitterSpin"), 100);
  minimum_roundness_spin_ = add_percent_row(dynamics_grid, 6, tr("Minimum Roundness:"),
                                            QStringLiteral("dynamicsMinimumRoundnessSpin"), 100);
  minimum_roundness_spin_->setValue(25);
  std::tie(roundness_control_combo_, roundness_fade_steps_spin_) = add_control_row(
      dynamics_grid, 7, tr("Roundness Control:"), QStringLiteral("dynamicsRoundnessControlCombo"),
      QStringLiteral("dynamicsRoundnessFadeStepsSpin"), true);
  auto* flips_row = new QHBoxLayout();
  flip_x_check_ = new QCheckBox(tr("Flip X Jitter"), this);
  flip_x_check_->setObjectName(QStringLiteral("dynamicsFlipXCheck"));
  flip_y_check_ = new QCheckBox(tr("Flip Y Jitter"), this);
  flip_y_check_->setObjectName(QStringLiteral("dynamicsFlipYCheck"));
  flips_row->addWidget(flip_x_check_);
  flips_row->addWidget(flip_y_check_);
  flips_row->addStretch(1);
  dynamics_grid->addLayout(flips_row, 8, 0, 1, 3);
  layout->addWidget(dynamics_group);

  // Scattering.
  auto* scatter_group = new QGroupBox(tr("Scattering"), this);
  auto* scatter_grid = new QGridLayout(scatter_group);
  compact_group_grid(scatter_grid);
  scatter_spin_ = add_percent_row(scatter_grid, 0, tr("Scatter:"), QStringLiteral("dynamicsScatterSpin"), 1000);
  std::tie(scatter_control_combo_, scatter_fade_steps_spin_) =
      add_control_row(scatter_grid, 1, tr("Scatter Control:"), QStringLiteral("dynamicsScatterControlCombo"),
                      QStringLiteral("dynamicsScatterFadeStepsSpin"), false);
  both_axes_check_ = new QCheckBox(tr("Both Axes"), this);
  both_axes_check_->setObjectName(QStringLiteral("dynamicsBothAxesCheck"));
  scatter_grid->addWidget(both_axes_check_, 2, 0, 1, 2);
  scatter_grid->addWidget(new QLabel(tr("Count:"), this), 3, 0);
  count_spin_ = new QSpinBox(this);
  count_spin_->setObjectName(QStringLiteral("dynamicsCountSpin"));
  count_spin_->setRange(1, 16);
  scatter_grid->addWidget(count_spin_, 3, 1, Qt::AlignLeft);
  count_jitter_spin_ =
      add_percent_row(scatter_grid, 4, tr("Count Jitter:"), QStringLiteral("dynamicsCountJitterSpin"), 100);
  std::tie(count_control_combo_, count_fade_steps_spin_) =
      add_control_row(scatter_grid, 5, tr("Count Control:"), QStringLiteral("dynamicsCountControlCombo"),
                      QStringLiteral("dynamicsCountFadeStepsSpin"), false);
  layout->addWidget(scatter_group);

  // Transfer (opacity and flow).
  auto* transfer_group = new QGroupBox(tr("Transfer"), this);
  auto* transfer_grid = new QGridLayout(transfer_group);
  compact_group_grid(transfer_grid);
  opacity_jitter_spin_ = add_percent_row(transfer_grid, 0, tr("Opacity Jitter:"),
                                         QStringLiteral("dynamicsOpacityJitterSpin"), 100);
  minimum_opacity_spin_ = add_percent_row(transfer_grid, 1, tr("Minimum Opacity:"),
                                          QStringLiteral("dynamicsMinimumOpacitySpin"), 100);
  std::tie(opacity_control_combo_, opacity_fade_steps_spin_) = add_control_row(
      transfer_grid, 2, tr("Opacity Control:"), QStringLiteral("dynamicsOpacityControlCombo"),
      QStringLiteral("dynamicsOpacityFadeStepsSpin"), true);
  flow_jitter_spin_ = add_percent_row(transfer_grid, 3, tr("Flow Jitter:"),
                                      QStringLiteral("dynamicsFlowJitterSpin"), 100);
  minimum_flow_spin_ = add_percent_row(transfer_grid, 4, tr("Minimum Flow:"),
                                       QStringLiteral("dynamicsMinimumFlowSpin"), 100);
  std::tie(flow_control_combo_, flow_fade_steps_spin_) = add_control_row(
      transfer_grid, 5, tr("Flow Control:"), QStringLiteral("dynamicsFlowControlCombo"),
      QStringLiteral("dynamicsFlowFadeStepsSpin"), false);
  layout->addWidget(transfer_group);

  auto* footer = new QHBoxLayout();
  footer->addStretch(1);
  auto* reset_button = new QPushButton(tr("Reset"), this);
  reset_button->setObjectName(QStringLiteral("dynamicsResetButton"));
  reset_button->setToolTip(tr("Reset the tip shape and all dynamics to defaults"));
  footer->addWidget(reset_button);
  layout->addLayout(footer);

  const auto emit_edited = [this] {
    if (!loading_) {
      refresh_control_dependent_widgets();
      emit edited();
    }
  };
  for (auto* spin :
       {base_angle_spin_, base_roundness_spin_, size_jitter_spin_, minimum_diameter_spin_,
        size_fade_steps_spin_, angle_jitter_spin_, fade_steps_spin_, roundness_jitter_spin_,
        minimum_roundness_spin_, roundness_fade_steps_spin_, scatter_spin_, scatter_fade_steps_spin_,
        count_spin_, count_jitter_spin_, count_fade_steps_spin_, opacity_jitter_spin_,
        minimum_opacity_spin_, opacity_fade_steps_spin_, flow_jitter_spin_, minimum_flow_spin_,
        flow_fade_steps_spin_}) {
    connect(spin, qOverload<int>(&QSpinBox::valueChanged), this, emit_edited);
  }
  for (auto* combo : {angle_control_combo_, size_control_combo_, roundness_control_combo_,
                      scatter_control_combo_, count_control_combo_, opacity_control_combo_,
                      flow_control_combo_}) {
    connect(combo, &QComboBox::currentIndexChanged, this, emit_edited);
  }
  for (auto* check : {flip_x_check_, flip_y_check_, both_axes_check_}) {
    connect(check, &QCheckBox::toggled, this, emit_edited);
  }
  refresh_control_dependent_widgets();
  connect(reset_button, &QPushButton::clicked, this, &BrushDynamicsPanel::reset_to_defaults);

  // Keep - / + buttons on the panel's spin boxes (see the sub-control gotcha in dialog_utils);
  // applied after all children exist.
  setStyleSheet(dialog_spinbox_button_style());
}

void BrushDynamicsPanel::set_values(const patchy::BrushDynamics& dynamics, double base_angle_degrees,
                                    double base_roundness) {
  loading_ = true;
  base_angle_spin_->setValue(normalized_angle_value(base_angle_degrees));
  base_roundness_spin_->setValue(
      std::clamp(static_cast<int>(std::lround(base_roundness)), 1, 100));
  size_jitter_spin_->setValue(percent_from_fraction(dynamics.size_jitter));
  minimum_diameter_spin_->setValue(percent_from_fraction(dynamics.minimum_diameter));
  select_combo_control(*size_control_combo_, dynamics.size_control);
  size_fade_steps_spin_->setValue(std::clamp(dynamics.size_fade_steps, 1, 9999));
  angle_jitter_spin_->setValue(percent_from_fraction(dynamics.angle_jitter));
  angle_control_combo_->setCurrentIndex(static_cast<int>(dynamics.angle_control));
  fade_steps_spin_->setValue(std::clamp(dynamics.angle_fade_steps, 1, 9999));
  roundness_jitter_spin_->setValue(percent_from_fraction(dynamics.roundness_jitter));
  minimum_roundness_spin_->setValue(percent_from_fraction(dynamics.minimum_roundness));
  select_combo_control(*roundness_control_combo_, dynamics.roundness_control);
  roundness_fade_steps_spin_->setValue(std::clamp(dynamics.roundness_fade_steps, 1, 9999));
  flip_x_check_->setChecked(dynamics.flip_x_jitter);
  flip_y_check_->setChecked(dynamics.flip_y_jitter);
  scatter_spin_->setValue(percent_from_fraction(dynamics.scatter));
  select_combo_control(*scatter_control_combo_, dynamics.scatter_control);
  scatter_fade_steps_spin_->setValue(std::clamp(dynamics.scatter_fade_steps, 1, 9999));
  both_axes_check_->setChecked(dynamics.scatter_both_axes);
  count_spin_->setValue(std::clamp(dynamics.count, 1, 16));
  count_jitter_spin_->setValue(percent_from_fraction(dynamics.count_jitter));
  select_combo_control(*count_control_combo_, dynamics.count_control);
  count_fade_steps_spin_->setValue(std::clamp(dynamics.count_fade_steps, 1, 9999));
  opacity_jitter_spin_->setValue(percent_from_fraction(dynamics.opacity_jitter));
  minimum_opacity_spin_->setValue(percent_from_fraction(dynamics.minimum_opacity));
  select_combo_control(*opacity_control_combo_, dynamics.opacity_control);
  opacity_fade_steps_spin_->setValue(std::clamp(dynamics.opacity_fade_steps, 1, 9999));
  flow_jitter_spin_->setValue(percent_from_fraction(dynamics.flow_jitter));
  minimum_flow_spin_->setValue(percent_from_fraction(dynamics.minimum_flow));
  select_combo_control(*flow_control_combo_, dynamics.flow_control);
  flow_fade_steps_spin_->setValue(std::clamp(dynamics.flow_fade_steps, 1, 9999));
  refresh_control_dependent_widgets();
  loading_ = false;
}

patchy::BrushDynamics BrushDynamicsPanel::dynamics() const {
  patchy::BrushDynamics dynamics;
  dynamics.size_jitter = fraction_from_percent(size_jitter_spin_->value());
  dynamics.minimum_diameter = fraction_from_percent(minimum_diameter_spin_->value());
  dynamics.size_control = combo_control(*size_control_combo_);
  dynamics.size_fade_steps = size_fade_steps_spin_->value();
  dynamics.angle_jitter = fraction_from_percent(angle_jitter_spin_->value());
  dynamics.angle_control = static_cast<patchy::BrushDynamicControl>(
      std::clamp(angle_control_combo_->currentIndex(), 0,
                 static_cast<int>(patchy::BrushDynamicControl::StylusWheel)));
  dynamics.angle_fade_steps = fade_steps_spin_->value();
  dynamics.roundness_jitter = fraction_from_percent(roundness_jitter_spin_->value());
  dynamics.minimum_roundness = fraction_from_percent(minimum_roundness_spin_->value());
  dynamics.roundness_control = combo_control(*roundness_control_combo_);
  dynamics.roundness_fade_steps = roundness_fade_steps_spin_->value();
  dynamics.flip_x_jitter = flip_x_check_->isChecked();
  dynamics.flip_y_jitter = flip_y_check_->isChecked();
  dynamics.scatter = fraction_from_percent(scatter_spin_->value());
  dynamics.scatter_both_axes = both_axes_check_->isChecked();
  dynamics.scatter_control = combo_control(*scatter_control_combo_);
  dynamics.scatter_fade_steps = scatter_fade_steps_spin_->value();
  dynamics.count = count_spin_->value();
  dynamics.count_jitter = fraction_from_percent(count_jitter_spin_->value());
  dynamics.count_control = combo_control(*count_control_combo_);
  dynamics.count_fade_steps = count_fade_steps_spin_->value();
  dynamics.opacity_jitter = fraction_from_percent(opacity_jitter_spin_->value());
  dynamics.minimum_opacity = fraction_from_percent(minimum_opacity_spin_->value());
  dynamics.opacity_control = combo_control(*opacity_control_combo_);
  dynamics.opacity_fade_steps = opacity_fade_steps_spin_->value();
  dynamics.flow_jitter = fraction_from_percent(flow_jitter_spin_->value());
  dynamics.minimum_flow = fraction_from_percent(minimum_flow_spin_->value());
  dynamics.flow_control = combo_control(*flow_control_combo_);
  dynamics.flow_fade_steps = flow_fade_steps_spin_->value();
  return dynamics;
}

void BrushDynamicsPanel::refresh_control_dependent_widgets() {
  fade_steps_spin_->setVisible(angle_control_combo_->currentIndex() ==
                               static_cast<int>(patchy::BrushDynamicControl::Fade));
  const std::pair<QComboBox*, QSpinBox*> control_rows[] = {
      {size_control_combo_, size_fade_steps_spin_},
      {roundness_control_combo_, roundness_fade_steps_spin_},
      {scatter_control_combo_, scatter_fade_steps_spin_},
      {count_control_combo_, count_fade_steps_spin_},
      {opacity_control_combo_, opacity_fade_steps_spin_},
      {flow_control_combo_, flow_fade_steps_spin_},
  };
  for (const auto& [combo, fade_spin] : control_rows) {
    fade_spin->setVisible(combo_control(*combo) == patchy::BrushDynamicControl::Fade);
  }
  // The Minimum Opacity floor only participates while the opacity control has a real source.
  const auto minimum_opacity_live = control_has_source(combo_control(*opacity_control_combo_));
  minimum_opacity_spin_->setEnabled(minimum_opacity_live);
  if (auto* slider = findChild<QSlider*>(QStringLiteral("dynamicsMinimumOpacitySpinSlider"))) {
    slider->setEnabled(minimum_opacity_live);
  }
  const auto minimum_flow_live = control_has_source(combo_control(*flow_control_combo_));
  minimum_flow_spin_->setEnabled(minimum_flow_live);
  if (auto* slider = findChild<QSlider*>(QStringLiteral("dynamicsMinimumFlowSpinSlider"))) {
    slider->setEnabled(minimum_flow_live);
  }
}

double BrushDynamicsPanel::base_angle_degrees() const {
  return base_angle_spin_->value();
}

double BrushDynamicsPanel::base_roundness() const {
  return base_roundness_spin_->value();
}

void BrushDynamicsPanel::reset_to_defaults() {
  set_values({}, 0.0, 100.0);
  emit edited();
}

BrushDynamicsButton::BrushDynamicsButton(QWidget* parent) : QToolButton(parent) {
  setObjectName(QStringLiteral("brushDynamicsButton"));
  setToolButtonStyle(Qt::ToolButtonTextOnly);
  popup_clock_.start();
  connect(this, &QToolButton::clicked, this, &BrushDynamicsButton::show_popup);
  retranslate();
  set_active_entry(nullptr);
}

void BrushDynamicsButton::retranslate() {
  setText(tr("Dynamics"));
  setToolTip(round_session_
                 ? tr("Shape dynamics, scattering, and Transfer for the Round brush "
                      "(this session only; resets on the next launch)")
                 : tr("Shape dynamics, scattering, and Transfer for the active brush tip"));
}

void BrushDynamicsButton::set_active_entry(const BrushTipEntry* entry) {
  if (entry == nullptr) {
    if (popup_ != nullptr) {
      popup_->close();
    }
    tip_id_.clear();
    round_session_ = false;
    dynamics_ = {};
    base_angle_degrees_ = 0.0;
    base_roundness_ = 100.0;
    setEnabled(false);
    retranslate();
    refresh_active_indicator();
    return;
  }
  if (popup_ != nullptr && entry->id == tip_id_) {
    // Our own edit echoing back through the library's changed(); the open popup owns the values.
    return;
  }
  tip_id_ = entry->id;
  round_session_ = false;
  dynamics_ = entry->dynamics;
  base_angle_degrees_ = entry->base_angle_degrees;
  base_roundness_ = entry->base_roundness;
  setEnabled(true);
  retranslate();
  refresh_active_indicator();
}

void BrushDynamicsButton::set_round_session(const QString& round_tip_id,
                                            const patchy::BrushDynamics& dynamics,
                                            double base_angle_degrees, double base_roundness) {
  if (popup_ != nullptr && tip_id_ == round_tip_id) {
    // Our own edit echoing back through MainWindow's re-apply; the open popup owns the values.
    return;
  }
  tip_id_ = round_tip_id;
  round_session_ = true;
  dynamics_ = dynamics;
  base_angle_degrees_ = base_angle_degrees;
  base_roundness_ = base_roundness;
  setEnabled(true);
  retranslate();
  refresh_active_indicator();
}

void BrushDynamicsButton::refresh_active_indicator() {
  // Keyed on non-default (not active()): a brush whose only customization is a control of Off
  // (ignore the pen) never runs the per-dab path but is still a deliberate setup worth showing.
  const auto active = !tip_id_.isEmpty() &&
                      (!brush_dynamics_is_default(dynamics_) || base_angle_degrees_ != 0.0 ||
                       base_roundness_ != 100.0);
  if (property("dynamicsActive").toBool() == active) {
    return;
  }
  setProperty("dynamicsActive", active);
  style()->unpolish(this);
  style()->polish(this);
}

void BrushDynamicsButton::schedule_emit() {
  refresh_active_indicator();
  static constexpr int kDebounceMs = 200;
  static const char kTimerProperty[] = "patchyEmitTimer";
  auto* timer = property(kTimerProperty).value<QTimer*>();
  if (timer == nullptr) {
    timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(kDebounceMs);
    connect(timer, &QTimer::timeout, this, [this] {
      if (!tip_id_.isEmpty()) {
        emit dynamics_edited(tip_id_, dynamics_, base_angle_degrees_, base_roundness_);
      }
    });
    setProperty(kTimerProperty, QVariant::fromValue(timer));
  }
  timer->start();
}

void BrushDynamicsButton::show_popup() {
  if (tip_id_.isEmpty()) {
    return;
  }
  // Clicking the button while its popup is open must DISMISS it, not reopen it. The press that
  // closes the Qt::Popup is replayed onto the button, so by the time clicked() fires the popup
  // is either still closing (pointer alive) or was just destroyed (timestamp) — both mean this
  // click was the dismissal.
  if (popup_ != nullptr) {
    popup_->close();
    return;
  }
  if (popup_dismissed_ms_ >= 0 && popup_clock_.elapsed() - popup_dismissed_ms_ < 300) {
    popup_dismissed_ms_ = -1;
    return;
  }
  auto* popup = new QFrame(this, Qt::Popup);
  popup->setAttribute(Qt::WA_DeleteOnClose);
  popup->setObjectName(QStringLiteral("brushDynamicsPopup"));
  popup->setFrameShape(QFrame::StyledPanel);
  popup_ = popup;
  connect(popup, &QObject::destroyed, this, [this] {
    // Arm the swallow window only when the popup died under a click on the button itself;
    // closing it by choosing elsewhere must not eat a quick legitimate reopen.
    if (rect().contains(mapFromGlobal(QCursor::pos()))) {
      popup_dismissed_ms_ = popup_clock_.elapsed();
    }
  });

  auto* layout = new QVBoxLayout(popup);
  layout->setContentsMargins(0, 0, 0, 0);
  // The control rows outgrew short screens, so the panel lives in a scroll area and the popup
  // clamps to the available screen height (a vertical scrollbar appears only when clamped).
  auto* scroll = new QScrollArea(popup);
  scroll->setObjectName(QStringLiteral("brushDynamicsPopupScroll"));
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto* panel = new BrushDynamicsPanel(scroll);
  panel->set_values(dynamics_, base_angle_degrees_, base_roundness_);
  scroll->setWidget(panel);
  layout->addWidget(scroll);
  connect(panel, &BrushDynamicsPanel::edited, popup, [this, panel] {
    dynamics_ = panel->dynamics();
    base_angle_degrees_ = panel->base_angle_degrees();
    base_roundness_ = panel->base_roundness();
    schedule_emit();
  });

  // Size from the panel's own hint: QScrollArea::sizeHint is capped at ~24 font-heights, so
  // adjustSize() through it would show a needless scrollbar on any normal screen. The scroll
  // area only earns its keep when the screen truly cannot fit the full panel.
  const auto panel_hint = panel->sizeHint();
  const auto frame = 2 * popup->frameWidth();
  auto popup_width = panel_hint.width() + frame;
  auto popup_height = panel_hint.height() + frame;
  const auto* screen = this->screen();
  if (screen != nullptr) {
    const auto available = screen->availableGeometry();
    const auto max_height = std::max(200, available.height() - 40);
    if (popup_height > max_height) {
      popup_height = max_height;
      // Reserve room for the scrollbar so clamping never squeezes the rows horizontally.
      popup_width += scroll->verticalScrollBar()->sizeHint().width();
    }
  }
  popup->resize(popup_width, popup_height);
  auto position = mapToGlobal(QPoint(0, height()));
  if (screen != nullptr) {
    const auto available = screen->availableGeometry();
    if (position.y() + popup->height() > available.bottom()) {
      position.setY(std::max(available.top(), mapToGlobal(QPoint(0, 0)).y() - popup->height()));
    }
    position.setX(std::min(position.x(), available.right() - popup->width()));
  }
  popup->move(position);
  popup->show();
}

}  // namespace patchy::ui
