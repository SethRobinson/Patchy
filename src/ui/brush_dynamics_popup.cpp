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
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

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

  // Tip Shape: the static Photoshop "Brush Tip Shape" angle/roundness.
  auto* shape_group = new QGroupBox(tr("Tip Shape"), this);
  auto* shape_grid = new QGridLayout(shape_group);
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
  size_jitter_spin_ =
      add_percent_row(dynamics_grid, 0, tr("Size Jitter:"), QStringLiteral("dynamicsSizeJitterSpin"), 100);
  minimum_diameter_spin_ = add_percent_row(dynamics_grid, 1, tr("Minimum Diameter:"),
                                           QStringLiteral("dynamicsMinimumDiameterSpin"), 100);
  angle_jitter_spin_ =
      add_percent_row(dynamics_grid, 2, tr("Angle Jitter:"), QStringLiteral("dynamicsAngleJitterSpin"), 100);
  dynamics_grid->addWidget(new QLabel(tr("Angle Control:"), this), 3, 0);
  angle_control_combo_ = new QComboBox(this);
  angle_control_combo_->setObjectName(QStringLiteral("dynamicsAngleControlCombo"));
  angle_control_combo_->addItem(tr("Off"));
  angle_control_combo_->addItem(tr("Fade"));
  angle_control_combo_->addItem(tr("Pen Pressure"));
  angle_control_combo_->addItem(tr("Pen Tilt"));
  angle_control_combo_->addItem(tr("Pen Rotation"));
  angle_control_combo_->addItem(tr("Initial Direction"));
  angle_control_combo_->addItem(tr("Direction"));
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
  dynamics_grid->addLayout(control_row, 3, 1, 1, 2);
  roundness_jitter_spin_ = add_percent_row(dynamics_grid, 4, tr("Roundness Jitter:"),
                                           QStringLiteral("dynamicsRoundnessJitterSpin"), 100);
  minimum_roundness_spin_ = add_percent_row(dynamics_grid, 5, tr("Minimum Roundness:"),
                                            QStringLiteral("dynamicsMinimumRoundnessSpin"), 100);
  minimum_roundness_spin_->setValue(25);
  auto* flips_row = new QHBoxLayout();
  flip_x_check_ = new QCheckBox(tr("Flip X Jitter"), this);
  flip_x_check_->setObjectName(QStringLiteral("dynamicsFlipXCheck"));
  flip_y_check_ = new QCheckBox(tr("Flip Y Jitter"), this);
  flip_y_check_->setObjectName(QStringLiteral("dynamicsFlipYCheck"));
  flips_row->addWidget(flip_x_check_);
  flips_row->addWidget(flip_y_check_);
  flips_row->addStretch(1);
  dynamics_grid->addLayout(flips_row, 6, 0, 1, 3);
  layout->addWidget(dynamics_group);

  // Scattering.
  auto* scatter_group = new QGroupBox(tr("Scattering"), this);
  auto* scatter_grid = new QGridLayout(scatter_group);
  scatter_spin_ = add_percent_row(scatter_grid, 0, tr("Scatter:"), QStringLiteral("dynamicsScatterSpin"), 1000);
  both_axes_check_ = new QCheckBox(tr("Both Axes"), this);
  both_axes_check_->setObjectName(QStringLiteral("dynamicsBothAxesCheck"));
  scatter_grid->addWidget(both_axes_check_, 1, 0, 1, 2);
  scatter_grid->addWidget(new QLabel(tr("Count:"), this), 2, 0);
  count_spin_ = new QSpinBox(this);
  count_spin_->setObjectName(QStringLiteral("dynamicsCountSpin"));
  count_spin_->setRange(1, 16);
  scatter_grid->addWidget(count_spin_, 2, 1, Qt::AlignLeft);
  count_jitter_spin_ =
      add_percent_row(scatter_grid, 3, tr("Count Jitter:"), QStringLiteral("dynamicsCountJitterSpin"), 100);
  layout->addWidget(scatter_group);

  // Transfer (opacity jitter).
  auto* transfer_group = new QGroupBox(tr("Transfer"), this);
  auto* transfer_grid = new QGridLayout(transfer_group);
  opacity_jitter_spin_ = add_percent_row(transfer_grid, 0, tr("Opacity Jitter:"),
                                         QStringLiteral("dynamicsOpacityJitterSpin"), 100);
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
      fade_steps_spin_->setVisible(angle_control_combo_->currentIndex() ==
                                   static_cast<int>(patchy::BrushDynamicControl::Fade));
      emit edited();
    }
  };
  for (auto* spin : {base_angle_spin_, base_roundness_spin_, size_jitter_spin_, minimum_diameter_spin_,
                     angle_jitter_spin_, fade_steps_spin_, roundness_jitter_spin_, minimum_roundness_spin_,
                     scatter_spin_, count_spin_, count_jitter_spin_, opacity_jitter_spin_}) {
    connect(spin, qOverload<int>(&QSpinBox::valueChanged), this, emit_edited);
  }
  connect(angle_control_combo_, &QComboBox::currentIndexChanged, this, emit_edited);
  for (auto* check : {flip_x_check_, flip_y_check_, both_axes_check_}) {
    connect(check, &QCheckBox::toggled, this, emit_edited);
  }
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
  angle_jitter_spin_->setValue(percent_from_fraction(dynamics.angle_jitter));
  angle_control_combo_->setCurrentIndex(static_cast<int>(dynamics.angle_control));
  fade_steps_spin_->setValue(std::clamp(dynamics.angle_fade_steps, 1, 9999));
  fade_steps_spin_->setVisible(dynamics.angle_control == patchy::BrushDynamicControl::Fade);
  roundness_jitter_spin_->setValue(percent_from_fraction(dynamics.roundness_jitter));
  minimum_roundness_spin_->setValue(percent_from_fraction(dynamics.minimum_roundness));
  flip_x_check_->setChecked(dynamics.flip_x_jitter);
  flip_y_check_->setChecked(dynamics.flip_y_jitter);
  scatter_spin_->setValue(percent_from_fraction(dynamics.scatter));
  both_axes_check_->setChecked(dynamics.scatter_both_axes);
  count_spin_->setValue(std::clamp(dynamics.count, 1, 16));
  count_jitter_spin_->setValue(percent_from_fraction(dynamics.count_jitter));
  opacity_jitter_spin_->setValue(percent_from_fraction(dynamics.opacity_jitter));
  loading_ = false;
}

patchy::BrushDynamics BrushDynamicsPanel::dynamics() const {
  patchy::BrushDynamics dynamics;
  dynamics.size_jitter = fraction_from_percent(size_jitter_spin_->value());
  dynamics.minimum_diameter = fraction_from_percent(minimum_diameter_spin_->value());
  dynamics.angle_jitter = fraction_from_percent(angle_jitter_spin_->value());
  dynamics.angle_control = static_cast<patchy::BrushDynamicControl>(
      std::clamp(angle_control_combo_->currentIndex(), 0,
                 static_cast<int>(patchy::BrushDynamicControl::Direction)));
  dynamics.angle_fade_steps = fade_steps_spin_->value();
  dynamics.roundness_jitter = fraction_from_percent(roundness_jitter_spin_->value());
  dynamics.minimum_roundness = fraction_from_percent(minimum_roundness_spin_->value());
  dynamics.flip_x_jitter = flip_x_check_->isChecked();
  dynamics.flip_y_jitter = flip_y_check_->isChecked();
  dynamics.scatter = fraction_from_percent(scatter_spin_->value());
  dynamics.scatter_both_axes = both_axes_check_->isChecked();
  dynamics.count = count_spin_->value();
  dynamics.count_jitter = fraction_from_percent(count_jitter_spin_->value());
  dynamics.opacity_jitter = fraction_from_percent(opacity_jitter_spin_->value());
  return dynamics;
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
                 ? tr("Shape dynamics, scattering, and opacity jitter for the Round brush "
                      "(this session only; resets on the next launch)")
                 : tr("Shape dynamics, scattering, and opacity jitter for the active brush tip"));
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
  const auto active = !tip_id_.isEmpty() &&
                      (dynamics_.active() || base_angle_degrees_ != 0.0 || base_roundness_ != 100.0);
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
  auto* panel = new BrushDynamicsPanel(popup);
  panel->set_values(dynamics_, base_angle_degrees_, base_roundness_);
  layout->addWidget(panel);
  connect(panel, &BrushDynamicsPanel::edited, popup, [this, panel] {
    dynamics_ = panel->dynamics();
    base_angle_degrees_ = panel->base_angle_degrees();
    base_roundness_ = panel->base_roundness();
    schedule_emit();
  });

  popup->adjustSize();
  auto position = mapToGlobal(QPoint(0, height()));
  const auto* screen = this->screen();
  if (screen != nullptr) {
    const auto available = screen->availableGeometry();
    if (position.y() + popup->height() > available.bottom()) {
      position.setY(mapToGlobal(QPoint(0, 0)).y() - popup->height());
    }
    position.setX(std::min(position.x(), available.right() - popup->width()));
  }
  popup->move(position);
  popup->show();
}

}  // namespace patchy::ui
