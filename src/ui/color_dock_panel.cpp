#include "ui/color_dock_panel.hpp"

#include "ui/action_icons.hpp"
#include "ui/background_pick_cursor.hpp"
#include "ui/modifier_names.hpp"
#include "ui/theme_palette.hpp"
#include "ui/tool_palette_footer.hpp"
#include "ui/unit_spin_box.hpp"

#include <QAction>
#include <QActionGroup>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace patchy::ui {

namespace {

// The well, laid out by hand like the tool palette's: the foreground swatch over the
// background swatch, swap glyph in the free upper-right corner, default glyph in the
// free lower-left one.
constexpr int kWellWidth = 42;
constexpr int kWellHeight = 44;
constexpr int kFieldMinimum = 72;
constexpr int kTrackHeight = 14;

constexpr std::array<ColorDockPanel::View, 3> kViews = {ColorDockPanel::View::HueCube,
                                                        ColorDockPanel::View::RgbSliders,
                                                        ColorDockPanel::View::HsbSliders};

int view_index(ColorDockPanel::View view) { return static_cast<int>(view); }
int channel_index(ColorDockPanel::Channel channel) { return static_cast<int>(channel); }

// 0..100 percent from 0..255 and back; every percent survives the round trip.
int percent_from_byte(int value) { return static_cast<int>(std::lround(value * 100.0 / 255.0)); }
int byte_from_percent(int percent) { return static_cast<int>(std::lround(std::clamp(percent, 0, 100) * 2.55)); }

}  // namespace

// One channel's slider: the color across the channel's range with the other channels
// held, and a two-tone line at the foreground's value, like the hue strip's. A press
// or drag sets the channel.
class ColorChannelTrack final : public QWidget {
public:
  ColorChannelTrack(ColorDockPanel& panel, ColorDockPanel::Channel channel, QWidget* parent)
      : QWidget(parent), panel_(panel), channel_(channel) {
    setCursor(Qt::PointingHandCursor);
    setMinimumSize(48, kTrackHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFocusPolicy(Qt::NoFocus);
  }

  [[nodiscard]] QSize sizeHint() const override { return {120, kTrackHeight}; }

protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    const int track_width = std::max(1, width());
    const int maximum = ColorDockPanel::channel_maximum(channel_);
    QImage track(track_width, 1, QImage::Format_RGB32);
    auto* scanline = reinterpret_cast<QRgb*>(track.scanLine(0));
    for (int x = 0; x < track_width; ++x) {
      scanline[x] = panel_.channel_color(channel_, rounded_scaled_channel(x, track_width - 1, maximum)).rgb();
    }
    QPainter painter(this);
    painter.drawImage(rect(), track);
    painter.setPen(QPen(theme().field_border, 1));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));

    // The marker reads over any color, as the hue strip's does.
    const double marker_x =
        static_cast<double>(panel_.channel_value(channel_)) / std::max(1, maximum) * (track_width - 1);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(0, 0, 0), 3.0));
    painter.drawLine(QPointF(marker_x, 0.0), QPointF(marker_x, height() - 1.0));
    painter.setPen(QPen(QColor(245, 245, 245), 1.0));
    painter.drawLine(QPointF(marker_x, 0.0), QPointF(marker_x, height() - 1.0));
  }

  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton) {
      panel_.begin_channel_drag();
      set_from_x(event->position().toPoint().x());
      event->accept();
      return;
    }
    QWidget::mousePressEvent(event);
  }

  void mouseMoveEvent(QMouseEvent* event) override {
    if ((event->buttons() & Qt::LeftButton) != 0) {
      set_from_x(event->position().toPoint().x());
      event->accept();
      return;
    }
    QWidget::mouseMoveEvent(event);
  }

  void mouseReleaseEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton) {
      panel_.end_channel_drag();
      event->accept();
      return;
    }
    QWidget::mouseReleaseEvent(event);
  }

private:
  void set_from_x(int x) {
    const int track_width = std::max(1, width());
    panel_.set_channel(channel_, rounded_scaled_channel(std::clamp(x, 0, track_width - 1), track_width - 1,
                                                        ColorDockPanel::channel_maximum(channel_)));
  }

  ColorDockPanel& panel_;
  ColorDockPanel::Channel channel_;
};

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

  // The views (GitHub issue 404) are picked from the settings gear, which MainWindow puts
  // at the right end of the panel's title row (PanelSpec::header_widget). It is the only
  // place to switch them.
  settings_button_ = new QToolButton(this);
  settings_button_->setObjectName(QStringLiteral("colorPanelSettingsButton"));
  settings_button_->setIcon(simple_icon(QStringLiteral("gear")));
  settings_button_->setIconSize(QSize(14, 14));
  settings_button_->setFixedSize(18, 18);
  settings_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  settings_button_->setPopupMode(QToolButton::InstantPopup);
  settings_button_->setFocusPolicy(Qt::NoFocus);
  view_menu_ = new QMenu(settings_button_);
  view_menu_->setObjectName(QStringLiteral("colorPanelViewMenu"));
  view_group_ = new QActionGroup(this);
  view_group_->setExclusive(true);
  const std::array<const char*, 3> action_names = {"colorPanelViewHueCubeAction", "colorPanelViewRgbSlidersAction",
                                                   "colorPanelViewHsbSlidersAction"};
  for (const auto view : kViews) {
    auto* action = new QAction(this);
    action->setObjectName(QString::fromLatin1(action_names[static_cast<std::size_t>(view_index(view))]));
    action->setCheckable(true);
    action->setMenuRole(QAction::NoRole);
    view_group_->addAction(action);
    view_menu_->addAction(action);
    connect(action, &QAction::triggered, this, [this, view] { set_view(view); });
    view_actions_[static_cast<std::size_t>(view_index(view))] = action;
  }
  view_actions_[0]->setChecked(true);
  settings_button_->setMenu(view_menu_);

  // The Hue Cube.
  auto* cube = new QWidget(this);
  cube->setObjectName(QStringLiteral("colorPanelHueCubeView"));
  field_ = new ColorPlaneWidget(*this, cube);
  field_->setObjectName(QStringLiteral("colorPanelField"));
  field_->setMinimumSize(kFieldMinimum, kFieldMinimum);
  field_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  field_->setFocusPolicy(Qt::NoFocus);
  hue_strip_ = new HueSliderWidget(*this, cube);
  hue_strip_->setObjectName(QStringLiteral("colorPanelHueStrip"));
  hue_strip_->setMinimumSize(HueSliderWidget::kDefaultWidth, kFieldMinimum);
  hue_strip_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
  hue_strip_->setFocusPolicy(Qt::NoFocus);
  // Alt over either shows the background cursor; the picker window's field and strip,
  // which edit one color, do not get one.
  field_cursor_ = new BackgroundPickCursor(field_);
  hue_strip_cursor_ = new BackgroundPickCursor(hue_strip_);
  auto* cube_layout = new QHBoxLayout(cube);
  cube_layout->setContentsMargins(0, 0, 0, 0);
  cube_layout->setSpacing(8);
  cube_layout->addWidget(field_, 1);
  cube_layout->addWidget(hue_strip_);

  views_ = new QStackedWidget(this);
  views_->setObjectName(QStringLiteral("colorPanelViews"));
  views_->addWidget(cube);
  auto* rgb = build_slider_page(Channel::Red);
  rgb->setObjectName(QStringLiteral("colorPanelRgbSlidersView"));
  views_->addWidget(rgb);
  auto* hsb = build_slider_page(Channel::Hue);
  hsb->setObjectName(QStringLiteral("colorPanelHsbSlidersView"));
  views_->addWidget(hsb);
  // The stack's size hints take every page but an Ignored one; each view asks only for
  // its own (set_view moves this).
  rgb->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
  hsb->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

  well_column_ = new QVBoxLayout;
  well_column_->setContentsMargins(0, 0, 0, 0);
  well_column_->setSpacing(4);
  well_column_->addWidget(well);
  well_column_->addStretch(1);
  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(8, 8, 8, 8);
  layout->setSpacing(8);
  layout->addLayout(well_column_);
  layout->addWidget(views_, 1);
  refresh_channel_views();
  retranslate();
}

QWidget* ColorDockPanel::build_slider_page(Channel first) {
  auto* page = new QWidget(this);
  auto* grid = new QGridLayout(page);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(6);
  grid->setVerticalSpacing(4);
  grid->setColumnStretch(1, 1);
  static constexpr std::array<const char*, kChannelCount> names = {"Red",  "Green",      "Blue",
                                                                   "Hue", "Saturation", "Brightness"};
  for (int row = 0; row < 3; ++row) {
    const auto channel = static_cast<Channel>(channel_index(first) + row);
    const auto index = static_cast<std::size_t>(channel_index(channel));
    const auto name = QString::fromLatin1(names[index]);
    auto* label = new QLabel(page);
    label->setObjectName(QStringLiteral("colorPanel%1Label").arg(name));
    auto* track = new ColorChannelTrack(*this, channel, page);
    track->setObjectName(QStringLiteral("colorPanel%1Track").arg(name));
    QSpinBox* spin = nullptr;
    if (channel == Channel::Hue) {
      spin = new UnitIntSpinBox(SpinUnit::Degrees, page);
    } else if (channel == Channel::Saturation || channel == Channel::Brightness) {
      spin = new UnitIntSpinBox(SpinUnit::Percent, page);
    } else {
      spin = new QSpinBox(page);
    }
    spin->setObjectName(QStringLiteral("colorPanel%1Spin").arg(name));
    spin->setRange(0, channel_maximum(channel));
    spin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    connect(spin, qOverload<int>(&QSpinBox::valueChanged), this,
            [this, channel](int level) { set_channel(channel, level); });
    channel_labels_[index] = label;
    channel_tracks_[index] = track;
    channel_spins_[index] = spin;
    grid->addWidget(label, row, 0);
    grid->addWidget(track, row, 1);
    grid->addWidget(spin, row, 2);
  }
  // Spare height goes below the rows.
  grid->setRowStretch(3, 1);
  return page;
}

void ColorDockPanel::set_view(View view) {
  view_actions_[static_cast<std::size_t>(view_index(view))]->setChecked(true);
  if (view == view_) {
    return;
  }
  view_ = view;
  views_->setCurrentIndex(view_index(view));
  for (int index = 0; index < views_->count(); ++index) {
    views_->widget(index)->setSizePolicy(index == view_index(view) ? QSizePolicy::Preferred : QSizePolicy::Ignored,
                                         index == view_index(view) ? QSizePolicy::Preferred : QSizePolicy::Ignored);
  }
  refresh_channel_views();
  emit view_changed(view);
}

QString ColorDockPanel::view_token(View view) {
  switch (view) {
    case View::HueCube:
      return QStringLiteral("hueCube");
    case View::RgbSliders:
      return QStringLiteral("rgbSliders");
    case View::HsbSliders:
      return QStringLiteral("hsbSliders");
  }
  return QStringLiteral("hueCube");
}

std::optional<ColorDockPanel::View> ColorDockPanel::view_from_token(const QString& token) {
  for (const auto view : kViews) {
    if (token == view_token(view)) {
      return view;
    }
  }
  return std::nullopt;
}

int ColorDockPanel::fitted_height() const {
  if (view_ == View::HueCube) {
    return 0;
  }
  const auto margins = layout()->contentsMargins();
  const auto* page = views_->currentWidget();
  return margins.top() + margins.bottom() +
         std::max(well_column_->sizeHint().height(), page->layout()->sizeHint().height());
}

QAction* ColorDockPanel::view_action(View view) const {
  return view_actions_[static_cast<std::size_t>(view_index(view))];
}

QWidget* ColorDockPanel::channel_track(Channel channel) const {
  return channel_tracks_[static_cast<std::size_t>(channel_index(channel))];
}

QSpinBox* ColorDockPanel::channel_spin(Channel channel) const {
  return channel_spins_[static_cast<std::size_t>(channel_index(channel))];
}

int ColorDockPanel::channel_value(Channel channel) const {
  switch (channel) {
    case Channel::Red:
      return rgb_.red();
    case Channel::Green:
      return rgb_.green();
    case Channel::Blue:
      return rgb_.blue();
    case Channel::Hue:
      return hue_;
    case Channel::Saturation:
      return percent_from_byte(saturation_);
    case Channel::Brightness:
      return percent_from_byte(value_);
  }
  return 0;
}

int ColorDockPanel::channel_maximum(Channel channel) {
  switch (channel) {
    case Channel::Hue:
      return 359;
    case Channel::Saturation:
    case Channel::Brightness:
      return 100;
    default:
      return 255;
  }
}

QColor ColorDockPanel::channel_color(Channel channel, int level) const {
  switch (channel) {
    case Channel::Red:
      return QColor(level, rgb_.green(), rgb_.blue());
    case Channel::Green:
      return QColor(rgb_.red(), level, rgb_.blue());
    case Channel::Blue:
      return QColor(rgb_.red(), rgb_.green(), level);
    case Channel::Hue:
      return QColor::fromHsv(std::clamp(level, 0, 359), saturation_, value_);
    case Channel::Saturation:
      return QColor::fromHsv(hue_, byte_from_percent(level), value_);
    case Channel::Brightness:
      return QColor::fromHsv(hue_, saturation_, byte_from_percent(level));
  }
  return rgb_;
}

void ColorDockPanel::set_channel(Channel channel, int level) {
  level = std::clamp(level, 0, channel_maximum(channel));
  if (level == channel_value(channel)) {
    return;
  }
  QColor color;
  switch (channel) {
    case Channel::Red:
    case Channel::Green:
    case Channel::Blue: {
      color = channel_color(channel, level);
      int color_hue = 0;
      int color_saturation = 0;
      int color_value = 0;
      color.getHsv(&color_hue, &color_saturation, &color_value);
      // A gray has no hue: keep the one shown. Black has no saturation either.
      if (color_hue >= 0) {
        hue_ = std::clamp(color_hue, 0, 359);
      }
      if (color_value > 0) {
        saturation_ = std::clamp(color_saturation, 0, 255);
      }
      value_ = std::clamp(color_value, 0, 255);
      break;
    }
    case Channel::Hue:
      hue_ = level;
      color = QColor::fromHsv(hue_, saturation_, value_);
      break;
    case Channel::Saturation:
      saturation_ = byte_from_percent(level);
      color = QColor::fromHsv(hue_, saturation_, value_);
      break;
    case Channel::Brightness:
      value_ = byte_from_percent(level);
      color = QColor::fromHsv(hue_, saturation_, value_);
      break;
  }
  produce_foreground(color);
  field_->update();
  hue_strip_->update();
  emit_pick(color);
}

void ColorDockPanel::begin_channel_drag() {
  picking_ = true;
  pick_background_ = false;
}

void ColorDockPanel::end_channel_drag() {
  picking_ = false;
  pick_background_ = false;
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
  produce_foreground(color);
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
  refresh_channel_views();
}

void ColorDockPanel::produce_foreground(QColor color) {
  produced_foreground_ = color;
  rgb_ = QColor(color.rgb());
  refresh_channel_views();
}

void ColorDockPanel::refresh_channel_views() {
  // Signals blocked: showing a value is not an edit, so nothing loops back.
  for (std::size_t index = 0; index < channel_spins_.size(); ++index) {
    if (auto* spin = channel_spins_[index]; spin != nullptr) {
      const QSignalBlocker blocker(spin);
      spin->setValue(channel_value(static_cast<Channel>(index)));
    }
    if (auto* track = channel_tracks_[index]; track != nullptr && track->isVisible()) {
      track->update();
    }
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
  produce_foreground(color);
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
  produce_foreground(color);
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
  settings_button_->setToolTip(tr("Color panel settings"));
  settings_button_->setAccessibleName(tr("Color panel settings"));
  const std::array<QString, 3> view_names = {tr("Hue Cube"), tr("RGB Sliders"), tr("HSB Sliders")};
  for (std::size_t index = 0; index < view_names.size(); ++index) {
    view_actions_[index]->setText(view_names[index]);
    view_actions_[index]->setToolTip(view_names[index]);
  }
  // One-letter row labels, the channel's name in the tooltip.
  const std::array<QString, kChannelCount> letters = {
      tr("R", "Red channel"), tr("G", "Green channel"),      tr("B", "Blue channel"),
      tr("H", "Hue"),         tr("S", "Saturation"), tr("B", "Brightness")};
  const std::array<QString, kChannelCount> names = {tr("Red"), tr("Green"),      tr("Blue"),
                                                    tr("Hue"), tr("Saturation"), tr("Brightness")};
  for (std::size_t index = 0; index < channel_labels_.size(); ++index) {
    if (channel_labels_[index] == nullptr) {
      continue;
    }
    channel_labels_[index]->setText(letters[index]);
    channel_labels_[index]->setToolTip(names[index]);
    channel_tracks_[index]->setToolTip(names[index]);
    channel_spins_[index]->setToolTip(names[index]);
    channel_spins_[index]->setAccessibleName(names[index]);
  }
}

}  // namespace patchy::ui
