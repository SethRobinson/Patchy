#include "ui/hdr_color_controls.hpp"

#include "ui/theme_palette.hpp"

#include <QBoxLayout>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QSignalBlocker>
#include <QSlider>

#include <algorithm>
#include <cmath>

namespace patchy::ui {

namespace {

// The slider moves in hundredths of a stop, the spin's precision.
constexpr int kSliderScale = 100;
// Linear fields reach the brightest color the Intensity range can make.
constexpr double kMaxLinearValue = 1024.0;
constexpr int kLinearDecimals = 4;
constexpr int kFullSwatch = 30;
constexpr int kFullCurrentSwatch = 54;
constexpr int kFullSwatchHeight = 30;
constexpr int kCompactSwatch = 18;
constexpr int kCompactCurrentSwatch = 30;
constexpr int kCompactSwatchHeight = 14;

QColor qcolor(const std::array<std::uint8_t, 3>& rgb) { return QColor(rgb[0], rgb[1], rgb[2]); }

// The box layout under `layout` that holds `widget` directly, and its index there.
QBoxLayout* box_layout_holding(QLayout* layout, QWidget* widget, int& index) {
  if (layout == nullptr) {
    return nullptr;
  }
  if (auto* box = qobject_cast<QBoxLayout*>(layout); box != nullptr) {
    if (const int at = box->indexOf(widget); at >= 0) {
      index = at;
      return box;
    }
  }
  for (int item = 0; item < layout->count(); ++item) {
    if (auto* found = box_layout_holding(layout->itemAt(item)->layout(), widget, index); found != nullptr) {
      return found;
    }
  }
  return nullptr;
}

QString offset_text(int offset) {
  return offset > 0 ? QStringLiteral("+%1").arg(offset) : QString::number(offset);
}

}  // namespace

HdrColorControls::HdrColorControls(Form form, QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("hdrColorControls"));
  const bool compact = form == Form::Compact;
  auto* grid = new QGridLayout(this);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(6);
  grid->setVerticalSpacing(compact ? 4 : 6);

  int row = 0;
  if (!compact) {
    caption_ = new QLabel(this);
    caption_->setObjectName(QStringLiteral("hdrColorCaption"));
    caption_->setWordWrap(true);
    grid->addWidget(caption_, row++, 0, 1, 4);
  }
  strip_ = new HdrStopPreviewStrip(compact, this);
  strip_->setObjectName(QStringLiteral("hdrStopPreviewStrip"));
  grid->addWidget(strip_, row++, 0, 1, 4, compact ? Qt::Alignment() : Qt::AlignLeft);

  intensity_label_ = new QLabel(this);
  intensity_spin_ = new QDoubleSpinBox(this);
  intensity_spin_->setObjectName(QStringLiteral("hdrIntensitySpin"));
  intensity_spin_->setRange(kMinColorIntensityStops, kMaxColorIntensityStops);
  intensity_spin_->setDecimals(2);
  intensity_spin_->setSingleStep(0.25);
  intensity_spin_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  intensity_spin_->setKeyboardTracking(false);
  stops_label_ = new QLabel(this);
  grid->addWidget(intensity_label_, row, 0);
  if (compact) {
    auto* line = new QHBoxLayout;
    line->setContentsMargins(0, 0, 0, 0);
    line->setSpacing(6);
    line->addWidget(intensity_spin_);
    line->addWidget(stops_label_);
    line->addStretch(1);
    grid->addLayout(line, row++, 1, 1, 3);
    linear_label_ = new QLabel(this);
    linear_label_->setObjectName(QStringLiteral("hdrLinearReadout"));
    linear_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    grid->addWidget(linear_label_, row++, 0, 1, 4);
  } else {
    intensity_slider_ = new QSlider(Qt::Horizontal, this);
    intensity_slider_->setObjectName(QStringLiteral("hdrIntensitySlider"));
    intensity_slider_->setRange(static_cast<int>(kMinColorIntensityStops) * kSliderScale,
                                static_cast<int>(kMaxColorIntensityStops) * kSliderScale);
    intensity_slider_->setPageStep(kSliderScale);
    grid->addWidget(intensity_slider_, row, 1);
    grid->addWidget(intensity_spin_, row, 2);
    grid->addWidget(stops_label_, row++, 3);
    connect(intensity_slider_, &QSlider::valueChanged, this, [this](int value) {
      take_intensity(static_cast<double>(value) / kSliderScale);
    });

    linear_title_ = new QLabel(this);
    grid->addWidget(linear_title_, row, 0);
    auto* fields = new QHBoxLayout;
    fields->setContentsMargins(0, 0, 0, 0);
    fields->setSpacing(6);
    static constexpr std::array<const char*, 3> kNames{"hdrLinearRedSpin", "hdrLinearGreenSpin", "hdrLinearBlueSpin"};
    for (std::size_t c = 0; c < 3U; ++c) {
      linear_channel_labels_[c] = new QLabel(this);
      auto* spin = new QDoubleSpinBox(this);
      spin->setObjectName(QLatin1String(kNames[c]));
      spin->setRange(0.0, kMaxLinearValue);
      spin->setDecimals(kLinearDecimals);
      spin->setSingleStep(0.05);
      spin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
      spin->setKeyboardTracking(false);
      connect(spin, &QDoubleSpinBox::valueChanged, this, [this] { take_linear_fields(); });
      linear_spins_[c] = spin;
      fields->addWidget(linear_channel_labels_[c]);
      fields->addWidget(spin, 1);
    }
    grid->addLayout(fields, row++, 1, 1, 3);
    grid->setColumnStretch(1, 1);
  }
  connect(intensity_spin_, &QDoubleSpinBox::valueChanged, this, [this](double value) { take_intensity(value); });
  connect(strip_, &HdrStopPreviewStrip::offset_clicked, this,
          [this](int offset) { take_intensity(color_.stops + offset); });
  retranslate();
  refresh();
}

void HdrColorControls::set_color(QColor base, double stops) {
  base_ = QColor(base.red(), base.green(), base.blue());
  color_.base = {static_cast<std::uint8_t>(base_.red()), static_cast<std::uint8_t>(base_.green()),
                 static_cast<std::uint8_t>(base_.blue())};
  color_.stops = clamp_color_intensity_stops(stops);
  refresh();
}

LinearRgb HdrColorControls::linear() const noexcept { return hdr_linear_color(color_); }

QDoubleSpinBox* HdrColorControls::linear_spin(int channel) const {
  return channel >= 0 && channel < 3 ? linear_spins_[static_cast<std::size_t>(channel)] : nullptr;
}

void HdrColorControls::take_intensity(double stops) {
  stops = clamp_color_intensity_stops(std::round(stops * kSliderScale) / kSliderScale);
  if (stops == color_.stops) {
    refresh();  // a slider between two spin steps, or a swatch past the range: show the value
    return;
  }
  color_.stops = stops;
  refresh();
  emit intensity_changed(stops);
}

void HdrColorControls::take_linear_fields() {
  LinearRgb typed{};
  for (std::size_t c = 0; c < 3U; ++c) {
    typed[c] = linear_spins_[c]->value();
  }
  const auto split = hdr_color_from_linear(typed);
  color_ = split;
  base_ = qcolor(split.base);
  refresh();
  emit linear_color_entered(base_, color_.stops);
}

void HdrColorControls::refresh() {
  {
    const QSignalBlocker blocker(intensity_spin_);
    intensity_spin_->setValue(color_.stops);
  }
  if (intensity_slider_ != nullptr) {
    const QSignalBlocker blocker(intensity_slider_);
    intensity_slider_->setValue(static_cast<int>(std::lround(color_.stops * kSliderScale)));
  }
  const auto value = linear();
  for (std::size_t c = 0; c < 3U; ++c) {
    if (auto* spin = linear_spins_[c]; spin != nullptr) {
      const QSignalBlocker blocker(spin);
      spin->setValue(value[c]);
    }
  }
  if (linear_label_ != nullptr) {
    const QLocale locale;
    linear_label_->setText(tr("Linear  R %1  G %2  B %3")
                               .arg(locale.toString(value[0], 'f', 3), locale.toString(value[1], 'f', 3),
                                    locale.toString(value[2], 'f', 3)));
  }
  strip_->set_color(color_);
}

void HdrColorControls::retranslate() {
  if (caption_ != nullptr) {
    caption_->setText(tr("32-bit document: the color is stored as linear light, where 1.0 is display white. "
                         "Intensity brightens or darkens it in stops; the swatches show it from 3 stops darker "
                         "to 3 stops brighter."));
  }
  intensity_label_->setText(tr("Intensity:"));
  stops_label_->setText(tr("stops"));
  const auto intensity_tip = tr("Scales the color's light: +1 stop doubles it, -1 halves it. Paint on 32-bit "
                                "layers takes it; 8 and 16-bit documents use the base color.");
  intensity_spin_->setToolTip(intensity_tip);
  if (intensity_slider_ != nullptr) {
    intensity_slider_->setToolTip(intensity_tip);
  }
  strip_->setToolTip(tr("The color 3 stops darker to 3 stops brighter. Click a swatch to move the Intensity there."));
  if (linear_title_ != nullptr) {
    linear_title_->setText(tr("Linear value:"));
  }
  const std::array<QString, 3> channel_names{tr("R:"), tr("G:"), tr("B:")};
  for (std::size_t c = 0; c < 3U; ++c) {
    if (linear_channel_labels_[c] != nullptr) {
      linear_channel_labels_[c]->setText(channel_names[c]);
    }
    if (linear_spins_[c] != nullptr) {
      linear_spins_[c]->setToolTip(tr("The linear light value the paint stores, above 1.0 for overbright colors"));
    }
  }
  if (linear_label_ != nullptr) {
    linear_label_->setToolTip(tr("The linear light value the paint stores, above 1.0 for overbright colors"));
  }
  refresh();
}

void HdrColorControls::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange) {
    retranslate();
  }
  QWidget::changeEvent(event);
}

HdrStopPreviewStrip::HdrStopPreviewStrip(bool compact, QWidget* parent) : QWidget(parent), compact_(compact) {
  setCursor(Qt::PointingHandCursor);
  setSizePolicy(compact ? QSizePolicy::Expanding : QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void HdrStopPreviewStrip::set_color(const HdrColor& color) {
  color_ = color;
  update();
}

QColor HdrStopPreviewStrip::swatch_color(int offset) const { return qcolor(hdr_preview_color(color_, offset)); }

int HdrStopPreviewStrip::label_height() const { return compact_ ? 0 : fontMetrics().height(); }

QSize HdrStopPreviewStrip::sizeHint() const {
  const int swatch = compact_ ? kCompactSwatch : kFullSwatch;
  const int current = compact_ ? kCompactCurrentSwatch : kFullCurrentSwatch;
  const int count = static_cast<int>(HdrColorControls::kPreviewOffsets.size());
  return {swatch * (count - 1) + current + 2, (compact_ ? kCompactSwatchHeight : kFullSwatchHeight) + label_height() + 2};
}

QSize HdrStopPreviewStrip::minimumSizeHint() const {
  const auto hint = sizeHint();
  return compact_ ? QSize(static_cast<int>(HdrColorControls::kPreviewOffsets.size()) * 6 + 8, hint.height()) : hint;
}

QRect HdrStopPreviewStrip::swatch_rect(int offset) const {
  // Widths in proportion to the hint, so the compact strip fills the panel's width.
  const auto hint = sizeHint();
  const double scale = static_cast<double>(std::max(1, width() - 2)) / std::max(1, hint.width() - 2);
  const int swatch = compact_ ? kCompactSwatch : kFullSwatch;
  const int current = compact_ ? kCompactCurrentSwatch : kFullCurrentSwatch;
  const int height = compact_ ? kCompactSwatchHeight : kFullSwatchHeight;
  int left = 0;
  for (const int candidate : HdrColorControls::kPreviewOffsets) {
    const int nominal = candidate == 0 ? current : swatch;
    if (candidate == offset) {
      const int x0 = 1 + static_cast<int>(std::lround(left * scale));
      const int x1 = 1 + static_cast<int>(std::lround((left + nominal) * scale));
      return QRect(x0, 1, std::max(1, x1 - x0), height);
    }
    left += nominal;
  }
  return {};
}

void HdrStopPreviewStrip::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  const auto& palette = theme();
  for (const int offset : HdrColorControls::kPreviewOffsets) {
    const auto cell = swatch_rect(offset);
    painter.fillRect(cell, swatch_color(offset));
    if (!compact_) {
      painter.setPen(palette.hint_text);
      painter.drawText(QRect(cell.left(), cell.bottom() + 1, cell.width(), label_height()), Qt::AlignCenter,
                       offset_text(offset));
    }
  }
  const auto first = swatch_rect(HdrColorControls::kPreviewOffsets.front());
  const auto last = swatch_rect(HdrColorControls::kPreviewOffsets.back());
  painter.setPen(QPen(palette.field_border, 1));
  painter.drawRect(QRect(first.topLeft(), last.bottomRight()).adjusted(-1, -1, 0, 0));
  // The color itself: a two-tone frame that reads over any swatch, like the panel's markers.
  const auto current = swatch_rect(0);
  painter.setPen(QPen(QColor(0, 0, 0), 1));
  painter.drawRect(current.adjusted(0, 0, -1, -1));
  painter.setPen(QPen(QColor(245, 245, 245), 1));
  painter.drawRect(current.adjusted(1, 1, -2, -2));
}

void HdrStopPreviewStrip::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) {
    QWidget::mousePressEvent(event);
    return;
  }
  for (const int offset : HdrColorControls::kPreviewOffsets) {
    if (offset != 0 && swatch_rect(offset).contains(event->position().toPoint())) {
      emit offset_clicked(offset);
      break;
    }
  }
  event->accept();
}

HdrColorControls* add_hdr_color_controls(QDialog& dialog) {
  auto* controls = new HdrColorControls(HdrColorControls::Form::Full, &dialog);
  controls->hide();
  auto* buttons = dialog.findChild<QDialogButtonBox*>();
  int index = -1;
  auto* layout = buttons != nullptr ? box_layout_holding(buttons->parentWidget()->layout(), buttons, index) : nullptr;
  if (layout != nullptr) {
    layout->insertWidget(index, controls);
  } else if (dialog.layout() != nullptr) {
    dialog.layout()->addWidget(controls);
  }
  return controls;
}

}  // namespace patchy::ui
