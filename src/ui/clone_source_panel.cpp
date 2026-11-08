#include "ui/clone_source_panel.hpp"

#include "ui/action_icons.hpp"
#include "ui/canvas_types.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/measurement_units.hpp"
#include "ui/modifier_names.hpp"
#include "ui/tool_modifier_style.hpp"

#include <QActionEvent>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QPainter>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <algorithm>

namespace patchy::ui {

namespace {

constexpr int kOffsetLimit = 1000000;
// The flip toggles match the 24 px dialog spin boxes (configure_dialog_spinbox).
constexpr int kFlipButtonSize = 24;
constexpr int kInfoButtonSize = 18;

}  // namespace

CloneSourcePanel::CloneSourcePanel(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("cloneSourcePanel"));
  auto* layout = new QGridLayout(this);
  layout->setContentsMargins(10, 8, 10, 8);
  layout->setHorizontalSpacing(8);
  layout->setVerticalSpacing(6);
  // Columns (GitHub issue 515): 0 the row captions, 1 the Offset's "X:", 2 the first field
  // column (where every value starts), 3 the second column's captions, 4 the second field
  // column; 5 takes any extra width. Captions in columns 1 and 3 sit right beside their field.
  constexpr auto kFieldCaption = Qt::AlignRight | Qt::AlignVCenter;

  // Row 0: the slots.
  slots_label_ = new QLabel(this);
  layout->addWidget(slots_label_, 0, 0);
  auto* slot_row = new QHBoxLayout();
  slot_row->setSpacing(4);
  // A little more air under the slot buttons than between the other rows, so the Point
  // readout never crowds the checked slot's box (GitHub issue 515).
  slot_row->setContentsMargins(0, 0, 0, 4);
  slot_group_ = new QButtonGroup(this);
  slot_group_->setExclusive(true);
  for (int slot = 0; slot < kSlotCount; ++slot) {
    auto* button = new QToolButton(this);
    button->setObjectName(QStringLiteral("cloneSourceSlotButton%1").arg(slot + 1));
    // Drawn as bordered buttons with the accent on the checked one (GitHub issue 574,
    // main_window_theme.cpp).
    button->setProperty("cloneSourceSlot", true);
    button->setText(QString::number(slot + 1));
    button->setCheckable(true);
    button->setAutoRaise(false);
    // No fixed size: the theme rule sizes them, and a fixed 22 px height let the grid plan
    // a 22 px row the buttons then overhung, onto the Point row (GitHub issue 515).
    button->setFocusPolicy(Qt::NoFocus);
    slot_group_->addButton(button, slot);
    slot_row->addWidget(button);
    slot_buttons_[static_cast<std::size_t>(slot)] = button;
  }
  slot_row->addStretch(1);
  layout->addLayout(slot_row, 0, 2, 1, 3);
  connect(slot_group_, &QButtonGroup::idClicked, this, [this](int slot) { emit slot_picked(slot); });

  // Row 1: where the active source is, and Clear, which empties it (GitHub issue 574) in
  // the column of Reset below, which only resets the transform.
  source_caption_ = new QLabel(this);
  layout->addWidget(source_caption_, 1, 0);
  source_label_ = new QLabel(this);
  source_label_->setObjectName(QStringLiteral("cloneSourcePointLabel"));
  layout->addWidget(source_label_, 1, 2, 1, 2);
  clear_source_button_ = new QPushButton(this);
  clear_source_button_->setObjectName(QStringLiteral("cloneSourceClearButton"));
  clear_source_button_->setAutoDefault(false);
  layout->addWidget(clear_source_button_, 1, 4);
  connect(clear_source_button_, &QPushButton::clicked, this,
          [this] { emit clear_source_requested(state_.slot); });

  // Lines between the sections (GitHub issue 574): under Point (Clear's row), under Offset,
  // and under scale, rotation and flips (Reset's rows).
  const auto add_divider = [this, layout](int row, const QString& name) {
    auto* divider = new QFrame(this);
    divider->setObjectName(name);
    divider->setFrameShape(QFrame::HLine);
    divider->setFrameShadow(QFrame::Plain);
    layout->addWidget(divider, row, 0, 1, 5);
    return divider;
  };
  add_divider(2, QStringLiteral("cloneSourcePointDivider"));

  // Row 3: the offset, with an info mark right of X while the fields are off or the next
  // stroke still sets it (its tooltip says which; GitHub issue 574).
  offset_label_ = new QLabel(this);
  layout->addWidget(offset_label_, 3, 0);
  offset_x_label_ = new QLabel(this);
  offset_x_label_->setAlignment(kFieldCaption);
  layout->addWidget(offset_x_label_, 3, 1, kFieldCaption);
  offset_x_spin_ = new QSpinBox(this);
  offset_x_spin_->setObjectName(QStringLiteral("cloneSourceOffsetXSpin"));
  offset_y_label_ = new QLabel(this);
  offset_y_label_->setAlignment(kFieldCaption);
  offset_y_spin_ = new QSpinBox(this);
  offset_y_spin_->setObjectName(QStringLiteral("cloneSourceOffsetYSpin"));
  for (auto* spin : {offset_x_spin_, offset_y_spin_}) {
    spin->setRange(-kOffsetLimit, kOffsetLimit);
    configure_dialog_spinbox(spin, 84);
  }
  layout->addWidget(offset_x_spin_, 3, 2);
  // The mark shares the second caption column with "Y:", on the opposite side; one row
  // layout, so the column is never narrower than both.
  auto* y_caption_row = new QHBoxLayout();
  y_caption_row->setContentsMargins(0, 0, 0, 0);
  y_caption_row->setSpacing(4);
  offset_info_button_ = new QToolButton(this);
  offset_info_button_->setObjectName(QStringLiteral("cloneSourceOffsetInfoButton"));
  offset_info_button_->setIcon(info_mark_icon());
  offset_info_button_->setFixedSize(kInfoButtonSize, kInfoButtonSize);
  offset_info_button_->setIconSize(QSize(16, 16));
  offset_info_button_->setAutoRaise(true);
  offset_info_button_->setFocusPolicy(Qt::NoFocus);
  y_caption_row->addWidget(offset_info_button_, 0, Qt::AlignVCenter);
  y_caption_row->addStretch(1);
  // A click shows the same text at once, for pens and touch screens that never hover.
  connect(offset_info_button_, &QToolButton::clicked, this, [this] {
    QToolTip::showText(offset_info_button_->mapToGlobal(QPoint(0, offset_info_button_->height())),
                       offset_info_button_->toolTip(), offset_info_button_);
  });
  y_caption_row->addWidget(offset_y_label_, 0, kFieldCaption);
  // Its field is outside the row layout, so the scrub label finds it as the buddy.
  offset_y_label_->setBuddy(offset_y_spin_);
  layout->addLayout(y_caption_row, 3, 3);
  layout->addWidget(offset_y_spin_, 3, 4);
  const auto offset_changed = [this] {
    emit offset_edited(QPoint(offset_x_spin_->value(), offset_y_spin_->value()));
  };
  connect(offset_x_spin_, &QSpinBox::valueChanged, this, offset_changed);
  connect(offset_y_spin_, &QSpinBox::valueChanged, this, offset_changed);
  add_divider(4, QStringLiteral("cloneSourceOffsetDivider"));

  // Rows 5 and 6: the Clone Stamp's scale, rotation and flips, and Reset. The Healing Brush
  // never transforms its source, so they and their line hide there (GitHub issue 596).
  scale_label_ = new QLabel(this);
  layout->addWidget(scale_label_, 5, 0);
  scale_spin_ = new QDoubleSpinBox(this);
  scale_spin_->setObjectName(QStringLiteral("cloneSourceScaleSpin"));
  scale_spin_->setRange(patchy::CloneSourceTransform::kMinScalePercent,
                        patchy::CloneSourceTransform::kMaxScalePercent);
  scale_spin_->setDecimals(1);
  scale_spin_->setSingleStep(1.0);
  scale_spin_->setValue(100.0);
  configure_dialog_spinbox(scale_spin_, 84);
  layout->addWidget(scale_spin_, 5, 2);
  rotation_label_ = new QLabel(this);
  rotation_label_->setAlignment(kFieldCaption);
  layout->addWidget(rotation_label_, 5, 3, kFieldCaption);
  rotation_spin_ = new QDoubleSpinBox(this);
  rotation_spin_->setObjectName(QStringLiteral("cloneSourceRotationSpin"));
  rotation_spin_->setRange(-patchy::CloneSourceTransform::kMaxRotationDegrees,
                           patchy::CloneSourceTransform::kMaxRotationDegrees);
  rotation_spin_->setDecimals(1);
  rotation_spin_->setSingleStep(1.0);
  rotation_spin_->setWrapping(true);
  configure_dialog_spinbox(rotation_spin_, 84);
  layout->addWidget(rotation_spin_, 5, 4);
  connect(scale_spin_, &QDoubleSpinBox::valueChanged, this, [this] { emit_transform(); });
  connect(rotation_spin_, &QDoubleSpinBox::valueChanged, this, [this] { emit_transform(); });
  // The flips mirror the source about the source point (one eye onto the other).
  flip_label_ = new QLabel(this);
  layout->addWidget(flip_label_, 6, 0, Qt::AlignVCenter);
  auto* flip_row = new QHBoxLayout();
  flip_row->setSpacing(4);
  flip_row->setContentsMargins(0, 0, 0, 0);
  flip_horizontal_button_ = new QToolButton(this);
  flip_horizontal_button_->setObjectName(QStringLiteral("cloneSourceFlipHorizontalButton"));
  flip_horizontal_button_->setIcon(simple_icon(QStringLiteral("FH")));
  flip_vertical_button_ = new QToolButton(this);
  flip_vertical_button_->setObjectName(QStringLiteral("cloneSourceFlipVerticalButton"));
  flip_vertical_button_->setIcon(simple_icon(QStringLiteral("FV")));
  for (auto* button : {flip_horizontal_button_, flip_vertical_button_}) {
    button->setCheckable(true);
    button->setAutoRaise(false);
    // As tall as the spin boxes above; the theme's 1 px padding for these two buttons
    // leaves the 20 px glyph filling them (main_window_theme.cpp).
    button->setFixedSize(kFlipButtonSize, kFlipButtonSize);
    button->setIconSize(QSize(kFlipButtonSize - 4, kFlipButtonSize - 4));
    button->setFocusPolicy(Qt::NoFocus);
    // Centered on the row, so the buttons line up with "Flip:" and Reset.
    flip_row->addWidget(button, 0, Qt::AlignVCenter);
    connect(button, &QToolButton::toggled, this, [this] { emit_transform(); });
  }
  flip_row->addStretch(1);
  layout->addLayout(flip_row, 6, 2);
  reset_button_ = new QPushButton(this);
  reset_button_->setObjectName(QStringLiteral("cloneSourceResetButton"));
  reset_button_->setAutoDefault(false);
  layout->addWidget(reset_button_, 6, 4);
  connect(reset_button_, &QPushButton::clicked, this,
          [this] { emit transform_edited(patchy::CloneSourceTransform{}); });

  transform_divider_ = add_divider(7, QStringLiteral("cloneSourceTransformDivider"));

  // Row 8: the overlay.
  overlay_check_ = new QCheckBox(this);
  overlay_check_->setObjectName(QStringLiteral("cloneSourceOverlayCheck"));
  layout->addWidget(overlay_check_, 8, 0, 1, 3);
  overlay_opacity_label_ = new QLabel(this);
  overlay_opacity_label_->setAlignment(kFieldCaption);
  layout->addWidget(overlay_opacity_label_, 8, 3, kFieldCaption);
  overlay_opacity_spin_ = new QSpinBox(this);
  overlay_opacity_spin_->setObjectName(QStringLiteral("cloneSourceOverlayOpacitySpin"));
  overlay_opacity_spin_->setRange(1, 100);
  configure_dialog_spinbox(overlay_opacity_spin_, 84);
  layout->addWidget(overlay_opacity_spin_, 8, 4);
  const auto overlay_changed = [this] { emit overlay_edited(overlay_check_->isChecked(), overlay_opacity_spin_->value()); };
  connect(overlay_check_, &QCheckBox::toggled, this, overlay_changed);
  connect(overlay_opacity_spin_, &QSpinBox::valueChanged, this, overlay_changed);
  layout->setColumnStretch(5, 1);

  retranslate();
  refresh();
  install_scrub_labels_in(this);
}

QSize CloneSourcePanel::sizeHint() const {
  // QMenu sizes a QWidgetAction from its size hint alone, never height-for-width: ask the
  // grid how tall any wrapping row makes it at the hinted width (GitHub issue 515).
  auto hint = QWidget::sizeHint();
  if (layout() != nullptr && layout()->hasHeightForWidth()) {
    hint.setHeight(std::max(hint.height(), layout()->totalHeightForWidth(hint.width())));
  }
  return hint;
}

void CloneSourcePanel::relayout_popup() {
  updateGeometry();
  // QMenu measures its items once and keeps those rectangles, also across closing and
  // reopening: a popup first opened before the note existed came back with the old,
  // shorter panel and every row squeezed (GitHub issue 515). An ActionChanged event makes it
  // measure again at the next show, and resizes it now when it is open.
  auto* menu = qobject_cast<QMenu*>(parentWidget());
  if (menu == nullptr) {
    return;
  }
  for (auto* action : menu->actions()) {
    const auto* widget_action = qobject_cast<QWidgetAction*>(action);
    if (widget_action != nullptr && widget_action->defaultWidget() == this) {
      QActionEvent event(QEvent::ActionChanged, action);
      QCoreApplication::sendEvent(menu, &event);
    }
  }
  if (menu->isVisible()) {
    menu->resize(menu->sizeHint());
  }
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
  patchy::CloneSourceTransform transform{scale_spin_->value(), rotation_spin_->value()};
  transform.flip_horizontal = flip_horizontal_button_->isChecked();
  transform.flip_vertical = flip_vertical_button_->isChecked();
  emit transform_edited(transform);
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
  flip_label_->setText(tr("Flip:"));
  flip_horizontal_button_->setToolTip(tr("Flip Horizontal: mirror the cloned content left to right"));
  flip_horizontal_button_->setAccessibleName(tr("Flip Horizontal"));
  flip_vertical_button_->setToolTip(tr("Flip Vertical: mirror the cloned content top to bottom"));
  flip_vertical_button_->setAccessibleName(tr("Flip Vertical"));
  clear_source_button_->setText(tr("Clear"));
  clear_source_button_->setAccessibleName(tr("Clear Source"));
  reset_button_->setText(tr("Reset"));
  reset_button_->setToolTip(tr("Back to 100%, 0 degrees and no flip; the source point stays"));
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
                                    : resolve_tool_modifier_names(tr("Not set: %SOURCE_KEY%-click the canvas"),
                                                                  CanvasTool::Clone));
  // Here rather than in retranslate(): the source key follows the shortcut set's tool
  // modifiers (GitHub issue 302), which can change without a language change.
  clear_source_button_->setToolTip(resolve_tool_modifier_names(
      tr("Clear this source point; %SOURCE_KEY%-click the canvas to set a new one"), CanvasTool::Clone));
  clear_source_button_->setEnabled(source_set);

  const bool offset_editable = source_set && state_.aligned;
  for (auto* spin : {offset_x_spin_, offset_y_spin_}) {
    const QSignalBlocker blocker(spin);
    spin->setEnabled(offset_editable);
  }
  {
    const QSignalBlocker x_blocker(offset_x_spin_);
    const QSignalBlocker y_blocker(offset_y_spin_);
    // An empty slot has no offset to show (GitHub issue 632).
    const auto offset = source_set ? state_.offset : QPoint();
    offset_x_spin_->setValue(offset.x());
    offset_y_spin_->setValue(offset.y());
  }
  QString offset_note;
  if (source_set && !state_.aligned) {
    offset_note = tr("Aligned is off: every stroke starts at the source point.");
  } else if (source_set && !state_.offset_set) {
    offset_note = tr("Your next stroke sets the offset.");
  }
  offset_info_button_->setToolTip(offset_note);
  offset_info_button_->setAccessibleName(offset_note);
  offset_info_button_->setVisible(!offset_note.isEmpty());

  {
    const QSignalBlocker scale_blocker(scale_spin_);
    const QSignalBlocker rotation_blocker(rotation_spin_);
    // Typing 50.5 or 4.5 keeps its text (ui-conventions.md#number-fields).
    set_spin_value_unless_typing(scale_spin_, state_.transform.scale_percent);
    set_spin_value_unless_typing(rotation_spin_, state_.transform.rotation_degrees);
  }
  for (auto* button : {flip_horizontal_button_, flip_vertical_button_}) {
    const QSignalBlocker blocker(button);
    button->setChecked(button == flip_horizontal_button_ ? state_.transform.flip_horizontal
                                                         : state_.transform.flip_vertical);
  }
  const bool transform_editable = state_.has_document && state_.transform_applies;
  scale_spin_->setEnabled(transform_editable);
  rotation_spin_->setEnabled(transform_editable);
  flip_horizontal_button_->setEnabled(transform_editable);
  flip_vertical_button_->setEnabled(transform_editable);
  reset_button_->setEnabled(transform_editable && !state_.transform.is_identity());
  // Not applicable on the Healing Brush: hidden, not shown off (GitHub issue 596). Their rows
  // and line collapse, and the hint check below resizes the popup either way.
  for (QWidget* widget : std::initializer_list<QWidget*>{
           scale_label_, scale_spin_, rotation_label_, rotation_spin_, flip_label_, flip_horizontal_button_,
           flip_vertical_button_, reset_button_, transform_divider_}) {
    widget->setVisible(state_.transform_applies);
  }

  {
    const QSignalBlocker check_blocker(overlay_check_);
    const QSignalBlocker opacity_blocker(overlay_opacity_spin_);
    overlay_check_->setChecked(state_.overlay_shown);
    overlay_opacity_spin_->setValue(state_.overlay_opacity);
  }
  overlay_opacity_spin_->setEnabled(state_.overlay_shown);
  // Any change of the hint (rows shown or hidden, a longer Point text, a language) goes to
  // the menu, open or closed. The layout is redone first, also while the popup is closed:
  // the hint must count the rows just shown or hidden, and QMenu sizes the panel to at
  // least its minimum size, which only an activation updates (a Healing Brush popup opened
  // after a switch made while closed kept the Clone Stamp's taller minimum, issue 596).
  if (layout() != nullptr) {
    layout()->invalidate();
    layout()->activate();
  }
  if (const auto hint = sizeHint(); hint != reported_hint_) {
    reported_hint_ = hint;
    relayout_popup();
  }
}

}  // namespace patchy::ui
