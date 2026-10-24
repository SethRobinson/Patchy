#include "ui/color_dock_panel.hpp"

#include "ui/background_pick_cursor.hpp"
#include "ui/modifier_names.hpp"
#include "ui/tool_palette_footer.hpp"

#include <QAction>
#include <QEvent>
#include <QHBoxLayout>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <algorithm>

namespace patchy::ui {

namespace {

// The well, laid out by hand like the tool palette's: the foreground swatch over the
// background swatch, swap glyph in the free upper-right corner, default glyph in the
// free lower-left one.
constexpr int kWellWidth = 42;
constexpr int kWellHeight = 44;
constexpr int kFieldMinimum = 72;

}  // namespace

ColorDockPanel::ColorDockPanel(QAction* swap_colors, QAction* default_colors, QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("colorPanel"));
  // A click in the field or the strip (both NoFocus) gives the PANEL the focus rather than
  // whatever container above it accepts click focus, so MainWindow can tell a pick here from
  // clicking off an open text session, which commits it (GitHub issue 371).
  setFocusPolicy(Qt::ClickFocus);

  auto* well = new QWidget(this);
  well->setObjectName(QStringLiteral("colorPanelWell"));
  well->setFixedSize(kWellWidth, kWellHeight);
  background_chip_ = new ColorWellSwatch(well);
  background_chip_->setObjectName(QStringLiteral("colorPanelBackgroundChip"));
  background_chip_->setText(QStringLiteral("BG"));
  background_chip_->move(16, 18);
  foreground_chip_ = new ColorWellSwatch(well);
  foreground_chip_->setObjectName(QStringLiteral("colorPanelForegroundChip"));
  foreground_chip_->setText(QStringLiteral("FG"));
  foreground_chip_->move(2, 4);
  foreground_chip_->raise();
  swap_ = new ColorWellGlyphButton(ColorWellGlyphButton::Glyph::Swap, well);
  swap_->setObjectName(QStringLiteral("colorPanelSwapButton"));
  swap_->setDefaultAction(swap_colors);
  swap_->move(26, 2);
  default_ = new ColorWellGlyphButton(ColorWellGlyphButton::Glyph::Default, well);
  default_->setObjectName(QStringLiteral("colorPanelDefaultButton"));
  default_->setDefaultAction(default_colors);
  default_->move(0, 28);

  field_ = new ColorPlaneWidget(*this, this);
  field_->setObjectName(QStringLiteral("colorPanelField"));
  field_->setMinimumSize(kFieldMinimum, kFieldMinimum);
  field_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  field_->setFocusPolicy(Qt::NoFocus);
  hue_strip_ = new HueSliderWidget(*this, this);
  hue_strip_->setObjectName(QStringLiteral("colorPanelHueStrip"));
  hue_strip_->setMinimumSize(HueSliderWidget::kDefaultWidth, kFieldMinimum);
  hue_strip_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
  hue_strip_->setFocusPolicy(Qt::NoFocus);
  // Alt over either shows the background cursor; the picker window's field and strip,
  // which edit one color, do not get one.
  field_cursor_ = new BackgroundPickCursor(field_);
  hue_strip_cursor_ = new BackgroundPickCursor(hue_strip_);

  auto* well_column = new QVBoxLayout;
  well_column->setContentsMargins(0, 0, 0, 0);
  well_column->addWidget(well);
  well_column->addStretch(1);
  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(8, 8, 8, 8);
  layout->setSpacing(8);
  layout->addLayout(well_column);
  layout->addWidget(field_, 1);
  layout->addWidget(hue_strip_);
  retranslate();
}

void ColorDockPanel::set_colors(QColor foreground, QColor background) {
  foreground_chip_->set_color(foreground);
  background_chip_->set_color(background);
  // A foreground drag owns the field until its release; the canvas may hand back a
  // palette-snapped color, which must not move the crosshair under the pointer.
  if (picking_ && !pick_background_) {
    return;
  }
  if (foreground.rgb() != produced_foreground_.rgb() || !produced_foreground_.isValid()) {
    adopt_foreground(foreground);
  }
}

void ColorDockPanel::adopt_foreground(QColor color) {
  if (!color.isValid()) {
    return;
  }
  int color_hue = 0;
  int color_saturation = 0;
  int color_value = 0;
  color.getHsv(&color_hue, &color_saturation, &color_value);
  // A gray has no hue: keep the field on the one it showed.
  const auto new_hue = color_hue >= 0 ? std::clamp(color_hue, 0, 359) : hue_;
  const auto new_saturation = std::clamp(color_saturation, 0, 255);
  const auto new_value = std::clamp(color_value, 0, 255);
  produced_foreground_ = color;
  if (new_hue == hue_ && new_saturation == saturation_ && new_value == value_) {
    return;
  }
  const bool hue_changed = new_hue != hue_;
  hue_ = new_hue;
  saturation_ = new_saturation;
  value_ = new_value;
  field_->update();
  if (hue_changed) {
    hue_strip_->update();
  }
}

void ColorDockPanel::begin_field_pick(Qt::KeyboardModifiers modifiers) {
  // Decided once per press from the press event itself: releasing Alt mid-drag keeps
  // setting the background, as the Pick tool's Alt-click does.
  picking_ = true;
  pick_background_ = (modifiers & Qt::AltModifier) != 0;
}

void ColorDockPanel::end_field_pick() {
  picking_ = false;
  pick_background_ = false;
}

void ColorDockPanel::emit_pick(QColor color) {
  emit color_picked(color, picking_ && pick_background_);
}

void ColorDockPanel::set_saturation_value_from_point(QPoint point, QSize size) {
  // The same mapping as the picker window's field, edges snapping to 0 and 255.
  const auto max_x = std::max(1, size.width() - 1);
  const auto max_y = std::max(1, size.height() - 1);
  const int x = std::clamp(point.x(), 0, max_x);
  const int y = std::clamp(point.y(), 0, max_y);
  const int saturation = x <= 2 ? 0 : (x >= max_x - 2 ? 255 : rounded_scaled_channel(x, max_x, 255));
  const int value = y <= 2 ? 255 : (y >= max_y - 2 ? 0 : 255 - rounded_scaled_channel(y, max_y, 255));
  const auto color = QColor::fromHsv(hue_, saturation, value);
  if (picking_ && pick_background_) {
    // The background takes the color under the pointer; the field keeps showing the
    // foreground.
    emit_pick(color);
    return;
  }
  saturation_ = saturation;
  value_ = value;
  produced_foreground_ = color;
  field_->update();
  emit_pick(color);
}

void ColorDockPanel::set_hue_from_point(QPoint point, QSize size) {
  const auto max_y = std::max(1, size.height() - 1);
  const int hue = rounded_scaled_channel(std::clamp(point.y(), 0, max_y), max_y, 359);
  if (picking_ && pick_background_) {
    // The color under the pointer: the strip shows each hue at full strength.
    emit_pick(QColor::fromHsv(hue, 255, 255));
    return;
  }
  hue_ = hue;
  const auto color = QColor::fromHsv(hue_, saturation_, value_);
  produced_foreground_ = color;
  field_->update();
  hue_strip_->update();
  emit_pick(color);
}

void ColorDockPanel::changeEvent(QEvent* event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::LanguageChange) {
    retranslate();
  }
}

void ColorDockPanel::retranslate() {
  const auto hint = resolve_modifier_names(
      tr("Click or drag to set the foreground color. %ALT%-click or %ALT%-drag to set the background color."));
  field_->setToolTip(hint);
  hue_strip_->setToolTip(hint);
}

}  // namespace patchy::ui
