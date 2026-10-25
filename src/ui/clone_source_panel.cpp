#include "ui/clone_source_panel.hpp"

#include "ui/dialog_utils.hpp"
#include "ui/measurement_units.hpp"
#include "ui/modifier_names.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace patchy::ui {

namespace {

constexpr int kOffsetLimit = 1000000;

}  // namespace

CloneSourcePanel::CloneSourcePanel(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("cloneSourcePanel"));
  auto* layout = new QGridLayout(this);
  layout->setContentsMargins(10, 8, 10, 8);
  layout->setHorizontalSpacing(8);
  layout->setVerticalSpacing(6);

  // Row 0: the slots.
  slots_label_ = new QLabel(this);
  layout->addWidget(slots_label_, 0, 0);
  auto* slot_row = new QHBoxLayout();
  slot_row->setSpacing(4);
  slot_group_ = new QButtonGroup(this);
  slot_group_->setExclusive(true);
  for (int slot = 0; slot < kSlotCount; ++slot) {
    auto* button = new QToolButton(this);
    button->setObjectName(QStringLiteral("cloneSourceSlotButton%1").arg(slot + 1));
    button->setText(QString::number(slot + 1));
    button->setCheckable(true);
    button->setAutoRaise(false);
    button->setFixedSize(28, 22);
    button->setFocusPolicy(Qt::NoFocus);
    slot_group_->addButton(button, slot);
    slot_row->addWidget(button);
    slot_buttons_[static_cast<std::size_t>(slot)] = button;
  }
  slot_row->addStretch(1);
  layout->addLayout(slot_row, 0, 1, 1, 4);
  connect(slot_group_, &QButtonGroup::idClicked, this, [this](int slot) { emit slot_picked(slot); });

  // Row 1: where the active source is.
  source_caption_ = new QLabel(this);
  layout->addWidget(source_caption_, 1, 0);
  source_label_ = new QLabel(this);
  source_label_->setObjectName(QStringLiteral("cloneSourcePointLabel"));
  layout->addWidget(source_label_, 1, 1, 1, 4);

  // Row 2: the offset, with a note under it while the next stroke still sets it.
  offset_label_ = new QLabel(this);
  layout->addWidget(offset_label_, 2, 0);
  offset_x_label_ = new QLabel(this);
  layout->addWidget(offset_x_label_, 2, 1);
  offset_x_spin_ = new QSpinBox(this);
  offset_x_spin_->setObjectName(QStringLiteral("cloneSourceOffsetXSpin"));
  offset_y_label_ = new QLabel(this);
  offset_y_spin_ = new QSpinBox(this);
  offset_y_spin_->setObjectName(QStringLiteral("cloneSourceOffsetYSpin"));
  for (auto* spin : {offset_x_spin_, offset_y_spin_}) {
    spin->setRange(-kOffsetLimit, kOffsetLimit);
    configure_dialog_spinbox(spin, 84);
  }
  layout->addWidget(offset_x_spin_, 2, 2);
  layout->addWidget(offset_y_label_, 2, 3);
  layout->addWidget(offset_y_spin_, 2, 4);
  const auto offset_changed = [this] {
    emit offset_edited(QPoint(offset_x_spin_->value(), offset_y_spin_->value()));
  };
  connect(offset_x_spin_, &QSpinBox::valueChanged, this, offset_changed);
  connect(offset_y_spin_, &QSpinBox::valueChanged, this, offset_changed);
  offset_note_ = new QLabel(this);
  offset_note_->setObjectName(QStringLiteral("cloneSourceOffsetNote"));
  offset_note_->setWordWrap(true);
  layout->addWidget(offset_note_, 3, 1, 1, 4);

  // Rows 4 and 5: the Clone Stamp's scale and rotation, and Reset.
  scale_label_ = new QLabel(this);
  layout->addWidget(scale_label_, 4, 0);
  scale_spin_ = new QDoubleSpinBox(this);
  scale_spin_->setObjectName(QStringLiteral("cloneSourceScaleSpin"));
  scale_spin_->setRange(patchy::CloneSourceTransform::kMinScalePercent,
                        patchy::CloneSourceTransform::kMaxScalePercent);
  scale_spin_->setDecimals(1);
  scale_spin_->setSingleStep(1.0);
  scale_spin_->setValue(100.0);
  configure_dialog_spinbox(scale_spin_, 84);
  layout->addWidget(scale_spin_, 4, 1, 1, 2);
  rotation_label_ = new QLabel(this);
  layout->addWidget(rotation_label_, 4, 3);
  rotation_spin_ = new QDoubleSpinBox(this);
  rotation_spin_->setObjectName(QStringLiteral("cloneSourceRotationSpin"));
  rotation_spin_->setRange(-patchy::CloneSourceTransform::kMaxRotationDegrees,
                           patchy::CloneSourceTransform::kMaxRotationDegrees);
  rotation_spin_->setDecimals(1);
  rotation_spin_->setSingleStep(1.0);
  rotation_spin_->setWrapping(true);
  configure_dialog_spinbox(rotation_spin_, 84);
  layout->addWidget(rotation_spin_, 4, 4);
  connect(scale_spin_, &QDoubleSpinBox::valueChanged, this, [this] { emit_transform(); });
  connect(rotation_spin_, &QDoubleSpinBox::valueChanged, this, [this] { emit_transform(); });
  reset_button_ = new QPushButton(this);
  reset_button_->setObjectName(QStringLiteral("cloneSourceResetButton"));
  reset_button_->setAutoDefault(false);
  layout->addWidget(reset_button_, 5, 4);
  connect(reset_button_, &QPushButton::clicked, this,
          [this] { emit transform_edited(patchy::CloneSourceTransform{}); });
  transform_note_ = new QLabel(this);
  transform_note_->setObjectName(QStringLiteral("cloneSourceTransformNote"));
  transform_note_->setWordWrap(true);
  layout->addWidget(transform_note_, 5, 0, 1, 4);

  // Row 6: the overlay.
  overlay_check_ = new QCheckBox(this);
  overlay_check_->setObjectName(QStringLiteral("cloneSourceOverlayCheck"));
  layout->addWidget(overlay_check_, 6, 0, 1, 3);
  overlay_opacity_label_ = new QLabel(this);
  layout->addWidget(overlay_opacity_label_, 6, 3);
  overlay_opacity_spin_ = new QSpinBox(this);
  overlay_opacity_spin_->setObjectName(QStringLiteral("cloneSourceOverlayOpacitySpin"));
  overlay_opacity_spin_->setRange(1, 100);
  configure_dialog_spinbox(overlay_opacity_spin_, 84);
  layout->addWidget(overlay_opacity_spin_, 6, 4);
  const auto overlay_changed = [this] { emit overlay_edited(overlay_check_->isChecked(), overlay_opacity_spin_->value()); };
  connect(overlay_check_, &QCheckBox::toggled, this, overlay_changed);
  connect(overlay_opacity_spin_, &QSpinBox::valueChanged, this, overlay_changed);
  layout->setColumnStretch(5, 1);

  retranslate();
  refresh();
  install_scrub_labels_in(this);
}

QToolButton* CloneSourcePanel::slot_button(int slot) const {
  if (slot < 0 || slot >= kSlotCount) {
    return nullptr;
  }
  return slot_buttons_[static_cast<std::size_t>(slot)];
}

void CloneSourcePanel::set_state(const State& state) {
  state_ = state;
  refresh();
}

void CloneSourcePanel::emit_transform() {
  emit transform_edited(patchy::CloneSourceTransform{scale_spin_->value(), rotation_spin_->value()});
}

void CloneSourcePanel::changeEvent(QEvent* event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::LanguageChange) {
    retranslate();
    refresh();
  }
}

void CloneSourcePanel::retranslate() {
  slots_label_->setText(tr("Sources:"));
  source_caption_->setText(tr("Point:"));
  offset_label_->setText(tr("Offset:"));
  offset_x_label_->setText(tr("X:"));
  offset_y_label_->setText(tr("Y:"));
  offset_x_spin_->setToolTip(tr("How far right of the brush the source is sampled"));
  offset_y_spin_->setToolTip(tr("How far below the brush the source is sampled"));
  scale_label_->setText(tr("Scale:"));
  scale_spin_->setToolTip(tr("Size of the cloned content"));
  rotation_label_->setText(tr("Rotation:"));
  rotation_spin_->setToolTip(tr("Turns the cloned content clockwise"));
  reset_button_->setText(tr("Reset"));
  reset_button_->setToolTip(tr("Back to 100% and 0 degrees"));
  overlay_check_->setText(tr("Show overlay"));
  overlay_check_->setToolTip(tr("Preview the source under the brush before you paint"));
  overlay_opacity_label_->setText(tr("Opacity:"));
  overlay_opacity_spin_->setToolTip(tr("Opacity of the source overlay"));
  for (auto* spin : {offset_x_spin_, offset_y_spin_}) {
    spin->setSuffix(pixel_suffix());
  }
  scale_spin_->setSuffix(percent_suffix());
  rotation_spin_->setSuffix(degree_suffix());
  overlay_opacity_spin_->setSuffix(percent_suffix());
}

void CloneSourcePanel::refresh() {
  const QSignalBlocker slot_blocker(slot_group_);
  for (int slot = 0; slot < kSlotCount; ++slot) {
    auto* button = slot_buttons_[static_cast<std::size_t>(slot)];
    const QSignalBlocker blocker(button);
    button->setChecked(slot == state_.slot);
    button->setEnabled(state_.has_document);
    const auto index = static_cast<std::size_t>(slot);
    button->setToolTip(state_.slot_set[index]
                           ? tr("Source %1: %2, %3")
                                 .arg(slot + 1)
                                 .arg(state_.slot_point[index].x())
                                 .arg(state_.slot_point[index].y())
                           : tr("Source %1: not set").arg(slot + 1));
  }
  const auto active = static_cast<std::size_t>(std::clamp(state_.slot, 0, kSlotCount - 1));
  const bool source_set = state_.has_document && state_.slot_set[active];
  source_label_->setText(source_set ? QStringLiteral("%1, %2").arg(state_.slot_point[active].x()).arg(state_.slot_point[active].y())
                                    : resolve_modifier_names(tr("Not set: %ALT%-click the canvas")));

  const bool offset_editable = source_set && state_.aligned;
  for (auto* spin : {offset_x_spin_, offset_y_spin_}) {
    const QSignalBlocker blocker(spin);
    spin->setEnabled(offset_editable);
  }
  {
    const QSignalBlocker x_blocker(offset_x_spin_);
    const QSignalBlocker y_blocker(offset_y_spin_);
    offset_x_spin_->setValue(state_.offset.x());
    offset_y_spin_->setValue(state_.offset.y());
  }
  QString offset_note;
  if (source_set && !state_.aligned) {
    offset_note = tr("Aligned is off: every stroke starts at the source point.");
  } else if (source_set && !state_.offset_set) {
    offset_note = tr("Your next stroke sets the offset.");
  }
  offset_note_->setText(offset_note);
  offset_note_->setVisible(!offset_note.isEmpty());

  {
    const QSignalBlocker scale_blocker(scale_spin_);
    const QSignalBlocker rotation_blocker(rotation_spin_);
    scale_spin_->setValue(state_.transform.scale_percent);
    rotation_spin_->setValue(state_.transform.rotation_degrees);
  }
  const bool transform_editable = state_.has_document && state_.transform_applies;
  scale_spin_->setEnabled(transform_editable);
  rotation_spin_->setEnabled(transform_editable);
  reset_button_->setEnabled(transform_editable && !state_.transform.is_identity());
  transform_note_->setText(state_.transform_applies ? QString() : tr("Scale and rotation apply to the Clone Stamp."));
  transform_note_->setVisible(!state_.transform_applies);

  {
    const QSignalBlocker check_blocker(overlay_check_);
    const QSignalBlocker opacity_blocker(overlay_opacity_spin_);
    overlay_check_->setChecked(state_.overlay_shown);
    overlay_opacity_spin_->setValue(state_.overlay_opacity);
  }
  overlay_opacity_spin_->setEnabled(state_.overlay_shown);
}

}  // namespace patchy::ui
