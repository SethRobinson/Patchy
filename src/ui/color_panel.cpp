#include "ui/color_panel.hpp"

#include "ui/app_settings.hpp"

#include "ui/dialog_utils.hpp"

#include <QApplication>
#include <QConicalGradient>
#include <QCursor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QTabWidget>
#include <QTabletEvent>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

constexpr int kColorPlaneSize = 188;
constexpr int kHueSliderWidth = 16;
constexpr int kColorWheelSize = 188;
constexpr int kColorWheelMinSize = 150;
constexpr int kColorWheelRing = 20;
constexpr int kChannelSliderHeight = 20;
constexpr int kChannelSliderWidth = 184;
constexpr int kSwatchSize = 24;
constexpr int kSwatchSpacing = 5;
constexpr int kCustomColorCount = 16;
constexpr double kPi = 3.14159265358979323846;
constexpr auto kCustomColorsKey = "colorPanel/customColors";
constexpr auto kNextCustomColorSlotKey = "colorPanel/nextCustomColorSlot";
constexpr auto kLastTabKey = "colorPanel/lastTab";

enum class ColorChangeNotification {
  No,
  Yes,
};

// One scalar component of the current colour. Drives the gradient sliders and the
// generic getter/setter on the picker so a single slider class covers all six.
enum class ColorChannel { Red, Green, Blue, Hue, Saturation, Value };

class ColorPlaneWidget;
class HueSliderWidget;
class ColorWheelWidget;
class ColorChannelSlider;
class ScreenColorOverlay;

QColor normalized_rgb_color(QColor color) {
  if (!color.isValid()) {
    return color;
  }
  color = color.toRgb();
  color.setAlpha(255);
  return color;
}

int bounded_channel(int value) {
  return std::clamp(value, 0, 255);
}

int rounded_scaled_channel(int position, int maximum_position, int maximum_value) {
  if (maximum_position <= 0) {
    return 0;
  }
  return std::clamp(static_cast<int>(std::lround(static_cast<double>(position) * maximum_value / maximum_position)), 0,
                    maximum_value);
}

// Renders the saturation (x) / value (y) gradient for a fixed hue into an image,
// shared by the square-mode plane and the colour wheel's inner square.
QImage render_saturation_value_image(int hue, QSize size) {
  if (size.isEmpty()) {
    return {};
  }
  QImage image(size, QImage::Format_RGB32);
  const auto max_x = std::max(1, size.width() - 1);
  const auto max_y = std::max(1, size.height() - 1);
  for (int y = 0; y < size.height(); ++y) {
    auto* scanline = reinterpret_cast<QRgb*>(image.scanLine(y));
    const int value = 255 - rounded_scaled_channel(y, max_y, 255);
    for (int x = 0; x < size.width(); ++x) {
      const int saturation = rounded_scaled_channel(x, max_x, 255);
      scanline[x] = QColor::fromHsv(hue, saturation, value).rgb();
    }
  }
  return image;
}

// A two-tone crosshair (dark halo + light core) so the marker stays visible over
// any colour underneath. Used by the SV plane and the wheel's inner square.
void draw_crosshair_marker(QPainter& painter, QPointF center) {
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QPen(QColor(10, 10, 10), 3.0));
  painter.drawLine(center + QPointF(-8.0, 0.0), center + QPointF(8.0, 0.0));
  painter.drawLine(center + QPointF(0.0, -8.0), center + QPointF(0.0, 8.0));
  painter.setPen(QPen(QColor(245, 245, 245), 1.0));
  painter.drawLine(center + QPointF(-8.0, 0.0), center + QPointF(8.0, 0.0));
  painter.drawLine(center + QPointF(0.0, -8.0), center + QPointF(0.0, 8.0));
}

std::vector<QColor> basic_colors() {
  // 8 columns x 4 rows: a grayscale ramp, then vivid / dark / light hue rows.
  // Curated to avoid the near-duplicates the old 48-swatch grid contained.
  return {
      QColor(0, 0, 0),       QColor(48, 48, 48),    QColor(96, 96, 96),    QColor(128, 128, 128),
      QColor(160, 160, 160), QColor(192, 192, 192), QColor(224, 224, 224), QColor(255, 255, 255),
      QColor(255, 0, 0),     QColor(255, 128, 0),   QColor(255, 255, 0),   QColor(0, 200, 0),
      QColor(0, 200, 200),   QColor(0, 96, 255),    QColor(128, 0, 255),   QColor(255, 0, 255),
      QColor(128, 0, 0),     QColor(128, 64, 0),    QColor(128, 128, 0),   QColor(0, 100, 0),
      QColor(0, 100, 100),   QColor(0, 0, 160),     QColor(64, 0, 128),    QColor(128, 0, 96),
      QColor(255, 160, 160), QColor(255, 200, 128), QColor(255, 255, 160), QColor(160, 230, 160),
      QColor(160, 224, 224), QColor(160, 200, 255), QColor(200, 160, 255), QColor(255, 180, 224),
  };
}

QString color_tool_tip(QColor color) {
  return color.name(QColor::HexRgb).toUpper();
}

QString custom_swatch_style(QColor color, bool selected) {
  const auto border = selected ? QStringLiteral("2px solid #63a6ff") : QStringLiteral("1px solid #747b86");
  const auto background = QStringLiteral("rgb(%1, %2, %3)").arg(color.red()).arg(color.green()).arg(color.blue());
  const auto border_radius = selected ? 1 : 2;
  return QStringLiteral(
             "QPushButton { background: %1; border: %2; border-radius: %3px; min-width: %4px; min-height: %4px; "
             "max-width: %4px; max-height: %4px; padding: 0; }"
             "QPushButton:hover { border: 2px solid #63a6ff; }")
      .arg(background)
      .arg(border)
      .arg(border_radius)
      .arg(kSwatchSize);
}

QString color_frame_style(QColor color) {
  return QStringLiteral("QFrame { background: rgb(%1, %2, %3); border: 1px solid #747b86; }")
      .arg(color.red())
      .arg(color.green())
      .arg(color.blue());
}

QPushButton* make_swatch_button(QWidget* parent, QColor color, const QString& object_name) {
  auto* button = new QPushButton(parent);
  button->setObjectName(object_name);
  button->setFocusPolicy(Qt::NoFocus);
  button->setFixedSize(kSwatchSize, kSwatchSize);
  button->setStyleSheet(swatch_button_style(color));
  button->setToolTip(color_tool_tip(color));
  return button;
}

}  // namespace

class PatchyColorPickerPrivate {
public:
  explicit PatchyColorPickerPrivate(PatchyColorPicker& owner) : owner_(owner) {}
  ~PatchyColorPickerPrivate();

  void build_ui();
  void set_color(QColor color, ColorChangeNotification notification);
  void apply_hsv_color(ColorChangeNotification notification);
  void set_saturation_value_from_point(QPoint point, QSize size);
  void set_hue_from_point(QPoint point, QSize size);
  void set_hue(int hue);
  void set_channel(ColorChannel channel, int value);
  void add_current_to_custom_colors();
  void select_custom_color(int index);
  void update_selected_custom_color();
  void start_screen_pick();
  void finish_screen_pick(QPoint global_position, bool sample);

  [[nodiscard]] QColor current_color() const { return color_; }
  [[nodiscard]] int hue() const { return hue_; }
  [[nodiscard]] int saturation() const { return saturation_; }
  [[nodiscard]] int value() const { return value_; }
  [[nodiscard]] int channel_value(ColorChannel channel) const;
  [[nodiscard]] int channel_maximum(ColorChannel channel) const { return channel == ColorChannel::Hue ? 359 : 255; }

private:
  QSpinBox* create_spin(QWidget* parent, const QString& object_name, int maximum);
  void connect_controls();
  void sync_controls();
  void load_custom_colors();
  void save_custom_colors() const;
  void refresh_custom_swatch(int index);
  void refresh_custom_controls();
  void sample_screen_color(QPoint global_position);
  void set_html_color();

  PatchyColorPicker& owner_;
  QColor color_{Qt::black};
  int hue_{0};
  int saturation_{0};
  int value_{0};
  bool syncing_{false};
  int next_custom_slot_{0};
  int selected_custom_slot_{-1};
  QTabWidget* tabs_{nullptr};
  ColorPlaneWidget* color_plane_{nullptr};
  HueSliderWidget* hue_slider_{nullptr};
  ColorWheelWidget* wheel_{nullptr};
  std::array<ColorChannelSlider*, 6> channel_sliders_{};
  std::vector<QWidget*> live_views_;
  QFrame* preview_{nullptr};
  QSpinBox* hue_spin_{nullptr};
  QSpinBox* saturation_spin_{nullptr};
  QSpinBox* value_spin_{nullptr};
  QSpinBox* red_spin_{nullptr};
  QSpinBox* green_spin_{nullptr};
  QSpinBox* blue_spin_{nullptr};
  QLineEdit* html_edit_{nullptr};
  QPushButton* update_custom_button_{nullptr};
  std::array<QPushButton*, kCustomColorCount> custom_buttons_{};
  std::array<QColor, kCustomColorCount> custom_colors_{};
  QPointer<ScreenColorOverlay> overlay_;
};

namespace {

class ColorPlaneWidget final : public QWidget {
public:
  explicit ColorPlaneWidget(PatchyColorPickerPrivate& picker, QWidget* parent) : QWidget(parent), picker_(picker) {
    setObjectName(QStringLiteral("patchyColorPlane"));
    setCursor(Qt::CrossCursor);
    setMinimumSize(kColorPlaneSize, kColorPlaneSize);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  }

  [[nodiscard]] QSize sizeHint() const override { return QSize(kColorPlaneSize, kColorPlaneSize); }

protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    const auto paint_size = size();
    if (paint_size.isEmpty()) {
      return;
    }

    const auto max_x = std::max(1, paint_size.width() - 1);
    const auto max_y = std::max(1, paint_size.height() - 1);

    QPainter painter(this);
    painter.drawImage(QPoint(0, 0), render_saturation_value_image(picker_.hue(), paint_size));
    painter.setPen(QPen(QColor(26, 26, 26), 1));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));

    const double cursor_x = static_cast<double>(picker_.saturation()) / 255.0 * max_x;
    const double cursor_y = static_cast<double>(255 - picker_.value()) / 255.0 * max_y;
    draw_crosshair_marker(painter, QPointF(cursor_x, cursor_y));
  }

  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton) {
      picker_.set_saturation_value_from_point(event->position().toPoint(), size());
      event->accept();
      return;
    }
    QWidget::mousePressEvent(event);
  }

  void mouseMoveEvent(QMouseEvent* event) override {
    if ((event->buttons() & Qt::LeftButton) != 0) {
      picker_.set_saturation_value_from_point(event->position().toPoint(), size());
      event->accept();
      return;
    }
    QWidget::mouseMoveEvent(event);
  }

private:
  PatchyColorPickerPrivate& picker_;
};

class HueSliderWidget final : public QWidget {
public:
  explicit HueSliderWidget(PatchyColorPickerPrivate& picker, QWidget* parent) : QWidget(parent), picker_(picker) {
    setObjectName(QStringLiteral("patchyHueSlider"));
    setCursor(Qt::PointingHandCursor);
    setMinimumSize(kHueSliderWidth, kColorPlaneSize);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  }

  [[nodiscard]] QSize sizeHint() const override { return QSize(kHueSliderWidth, kColorPlaneSize); }

protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    QPainter painter(this);
    const auto max_y = std::max(1, height() - 1);
    for (int y = 0; y < height(); ++y) {
      const int hue = rounded_scaled_channel(y, max_y, 359);
      painter.setPen(QColor::fromHsv(hue, 255, 255));
      painter.drawLine(0, y, width() - 1, y);
    }

    painter.setPen(QPen(QColor(26, 26, 26), 1));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));

    const int marker_y = rounded_scaled_channel(picker_.hue(), 359, max_y);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(0, 0, 0), 3.0));
    painter.drawLine(QPointF(0.0, marker_y), QPointF(width() - 1.0, marker_y));
    painter.setPen(QPen(QColor(245, 245, 245), 1.0));
    painter.drawLine(QPointF(0.0, marker_y), QPointF(width() - 1.0, marker_y));
  }

  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton) {
      picker_.set_hue_from_point(event->position().toPoint(), size());
      event->accept();
      return;
    }
    QWidget::mousePressEvent(event);
  }

  void mouseMoveEvent(QMouseEvent* event) override {
    if ((event->buttons() & Qt::LeftButton) != 0) {
      picker_.set_hue_from_point(event->position().toPoint(), size());
      event->accept();
      return;
    }
    QWidget::mouseMoveEvent(event);
  }

private:
  PatchyColorPickerPrivate& picker_;
};

// "Wheel" tab: an outer hue ring (drag = hue) wrapping an inner saturation/value
// square (drag = sat/val) for the current hue. Shares the picker's HSV state with
// every other view, so dragging here updates the square tab, the sliders, and the
// numeric fields in lock-step.
class ColorWheelWidget final : public QWidget {
public:
  explicit ColorWheelWidget(PatchyColorPickerPrivate& picker, QWidget* parent) : QWidget(parent), picker_(picker) {
    setObjectName(QStringLiteral("patchyColorWheel"));
    setCursor(Qt::CrossCursor);
    setMinimumSize(kColorWheelMinSize, kColorWheelMinSize);
    // Expand to fill the tab page so there is no wasted space around the ring.
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  }

  [[nodiscard]] QSize sizeHint() const override { return QSize(kColorWheelSize, kColorWheelSize); }

protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    const auto geometry = wheel_geometry();
    if (geometry.outer <= 0.0) {
      return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Hue ring: a conical sweep clipped to the annulus. Qt's conical gradient runs
    // counter-clockwise from 3 o'clock, which is the same convention the click
    // hit-test uses (atan2 of the inverted y), so marker and gradient stay aligned.
    QConicalGradient ring(geometry.center, 0.0);
    for (int stop = 0; stop <= 6; ++stop) {
      ring.setColorAt(stop / 6.0, QColor::fromHsv((stop * 60) % 360, 255, 255));
    }
    QPainterPath ring_path;
    ring_path.addEllipse(geometry.center, geometry.outer, geometry.outer);
    QPainterPath hole;
    hole.addEllipse(geometry.center, geometry.inner, geometry.inner);
    painter.fillPath(ring_path.subtracted(hole), QBrush(ring));
    painter.setPen(QPen(QColor(26, 26, 26), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(geometry.center, geometry.outer, geometry.outer);
    painter.drawEllipse(geometry.center, geometry.inner, geometry.inner);

    // Inner saturation/value square for the current hue.
    const QRect square = geometry.square.toRect();
    painter.drawImage(square.topLeft(), render_saturation_value_image(picker_.hue(), square.size()));
    painter.setPen(QPen(QColor(26, 26, 26), 1));
    painter.drawRect(square.adjusted(0, 0, -1, -1));

    // Hue marker on the ring.
    const double angle = picker_.hue() * kPi / 180.0;
    const double mid_radius = (geometry.outer + geometry.inner) / 2.0;
    const QPointF hue_marker(geometry.center.x() + mid_radius * std::cos(angle),
                             geometry.center.y() - mid_radius * std::sin(angle));
    painter.setPen(QPen(QColor(20, 20, 20), 3.0));
    painter.drawEllipse(hue_marker, 6.0, 6.0);
    painter.setPen(QPen(QColor(245, 245, 245), 1.5));
    painter.drawEllipse(hue_marker, 6.0, 6.0);

    // Saturation/value crosshair inside the square.
    const double cursor_x = square.left() + static_cast<double>(picker_.saturation()) / 255.0 * std::max(1, square.width() - 1);
    const double cursor_y = square.top() + static_cast<double>(255 - picker_.value()) / 255.0 * std::max(1, square.height() - 1);
    draw_crosshair_marker(painter, QPointF(cursor_x, cursor_y));
  }

  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton) {
      active_ = hit_test(event->position().toPoint());
      apply(event->position().toPoint());
      event->accept();
      return;
    }
    QWidget::mousePressEvent(event);
  }

  void mouseMoveEvent(QMouseEvent* event) override {
    if ((event->buttons() & Qt::LeftButton) != 0 && active_ != Region::None) {
      apply(event->position().toPoint());
      event->accept();
      return;
    }
    QWidget::mouseMoveEvent(event);
  }

  void mouseReleaseEvent(QMouseEvent* event) override {
    active_ = Region::None;
    QWidget::mouseReleaseEvent(event);
  }

private:
  enum class Region { None, Ring, Square };

  struct WheelGeometry {
    QPointF center;
    double outer{0.0};
    double inner{0.0};
    QRectF square;
  };

  [[nodiscard]] WheelGeometry wheel_geometry() const {
    const double side = std::min(width(), height());
    const QPointF center(width() / 2.0, height() / 2.0);
    const double outer = side / 2.0 - 2.0;
    const double inner = std::max(0.0, outer - kColorWheelRing);
    const double square_side = std::max(8.0, inner * std::sqrt(2.0) - 2.0);
    const QRectF square(center.x() - square_side / 2.0, center.y() - square_side / 2.0, square_side, square_side);
    return {center, outer, inner, square};
  }

  [[nodiscard]] Region hit_test(QPoint pos) const {
    const auto geometry = wheel_geometry();
    const double distance = std::hypot(pos.x() - geometry.center.x(), pos.y() - geometry.center.y());
    if (distance >= geometry.inner - 2.0 && distance <= geometry.outer + 6.0) {
      return Region::Ring;
    }
    if (distance < geometry.inner) {
      return Region::Square;
    }
    return Region::None;
  }

  void apply(QPoint pos) {
    const auto geometry = wheel_geometry();
    if (active_ == Region::Ring) {
      double degrees = std::atan2(geometry.center.y() - pos.y(), pos.x() - geometry.center.x()) * 180.0 / kPi;
      if (degrees < 0.0) {
        degrees += 360.0;
      }
      picker_.set_hue(static_cast<int>(std::lround(degrees)) % 360);
      return;
    }
    if (active_ == Region::Square) {
      const QRect square = geometry.square.toRect();
      const QPoint local(std::clamp(pos.x() - square.left(), 0, std::max(1, square.width() - 1)),
                         std::clamp(pos.y() - square.top(), 0, std::max(1, square.height() - 1)));
      picker_.set_saturation_value_from_point(local, square.size());
    }
  }

  PatchyColorPickerPrivate& picker_;
  Region active_{Region::None};
};

// "Sliders" tab: one gradient track per colour channel. The track previews the
// colour across that channel's range with the other channels held at their current
// values, so each slider reads as "what happens if I move only this". Edits route
// through the picker's generic channel setter, keeping every view in sync.
class ColorChannelSlider final : public QWidget {
public:
  ColorChannelSlider(PatchyColorPickerPrivate& picker, ColorChannel channel, const QString& object_name,
                     QWidget* parent)
      : QWidget(parent), picker_(picker), channel_(channel) {
    setObjectName(object_name);
    setCursor(Qt::PointingHandCursor);
    setMinimumSize(120, kChannelSliderHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  }

  [[nodiscard]] QSize sizeHint() const override { return QSize(kChannelSliderWidth, kChannelSliderHeight); }

protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    const int track_width = std::max(1, width());
    const int max_value = picker_.channel_maximum(channel_);

    QImage track(track_width, 1, QImage::Format_RGB32);
    auto* scanline = reinterpret_cast<QRgb*>(track.scanLine(0));
    for (int x = 0; x < track_width; ++x) {
      scanline[x] = channel_color(rounded_scaled_channel(x, track_width - 1, max_value)).rgb();
    }

    QPainter painter(this);
    painter.drawImage(rect(), track);
    painter.setPen(QPen(QColor(26, 26, 26), 1));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));

    const double thumb_x =
        static_cast<double>(picker_.channel_value(channel_)) / std::max(1, max_value) * (track_width - 1);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF handle(thumb_x - 4.0, 0.5, 8.0, height() - 1.0);
    painter.setPen(QPen(QColor(20, 20, 20), 2.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(handle, 2.0, 2.0);
    painter.setPen(QPen(QColor(245, 245, 245), 1.0));
    painter.drawRoundedRect(handle.adjusted(1.0, 1.0, -1.0, -1.0), 1.5, 1.5);
  }

  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton) {
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

private:
  [[nodiscard]] QColor channel_color(int channel_value) const {
    const QColor color = picker_.current_color();
    switch (channel_) {
      case ColorChannel::Red:
        return QColor(channel_value, color.green(), color.blue());
      case ColorChannel::Green:
        return QColor(color.red(), channel_value, color.blue());
      case ColorChannel::Blue:
        return QColor(color.red(), color.green(), channel_value);
      case ColorChannel::Hue:
        return QColor::fromHsv(std::clamp(channel_value, 0, 359), 255, 255);
      case ColorChannel::Saturation:
        return QColor::fromHsv(picker_.hue(), channel_value, picker_.value());
      case ColorChannel::Value:
        return QColor::fromHsv(picker_.hue(), picker_.saturation(), channel_value);
    }
    return color;
  }

  void set_from_x(int x) {
    const int track_width = std::max(1, width());
    const int clamped = std::clamp(x, 0, track_width - 1);
    picker_.set_channel(channel_, rounded_scaled_channel(clamped, track_width - 1, picker_.channel_maximum(channel_)));
  }

  PatchyColorPickerPrivate& picker_;
  ColorChannel channel_;
};

QRect screen_virtual_geometry() {
  QRect geometry;
  const auto screens = QGuiApplication::screens();
  for (auto* screen : screens) {
    if (screen != nullptr) {
      geometry = geometry.united(screen->geometry());
    }
  }
  if (geometry.isNull()) {
    if (auto* primary = QGuiApplication::primaryScreen()) {
      geometry = primary->geometry();
    }
  }
  return geometry;
}

// Transparent, always-on-top window covering the whole virtual desktop. It is
// what makes "Pick Screen Color" able to sample any pixel on screen, including
// other applications: because the overlay sits above everything, every mouse
// click and pen tap lands on it instead of the window underneath. That keeps
// the crosshair in effect, prevents the click from leaking through to the app
// below (e.g. starting a video), and lets a Wacom pen pick outside Patchy.
class ScreenColorOverlay final : public QWidget {
public:
  explicit ScreenColorOverlay(PatchyColorPickerPrivate& picker)
      : QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool),
        picker_(picker) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setCursor(Qt::CrossCursor);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setGeometry(screen_virtual_geometry());
  }

protected:
  void paintEvent(QPaintEvent* /*event*/) override {
    // A single unit of alpha keeps the overlay effectively invisible while still
    // catching input: on Windows fully transparent (alpha 0) regions of a
    // translucent window are click-through. The grab is deferred until after the
    // overlay hides, so this does not tint the sampled pixel.
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0, 1));
  }

  void mousePressEvent(QMouseEvent* event) override {
    picker_.finish_screen_pick(event->globalPosition().toPoint(), event->button() == Qt::LeftButton);
    event->accept();
  }

  void tabletEvent(QTabletEvent* event) override {
    if (event->type() == QEvent::TabletPress) {
      // A pen tap arrives as a tablet press; the tip and an unidentified button
      // both mean "sample this pixel".
      const bool sample = event->button() == Qt::LeftButton || event->button() == Qt::NoButton;
      picker_.finish_screen_pick(event->globalPosition().toPoint(), sample);
    }
    event->accept();
  }

  void keyPressEvent(QKeyEvent* event) override {
    if (event->key() == Qt::Key_Escape) {
      picker_.finish_screen_pick(QPoint(), false);
      return;
    }
    QWidget::keyPressEvent(event);
  }

private:
  PatchyColorPickerPrivate& picker_;
};

}  // namespace

PatchyColorPickerPrivate::~PatchyColorPickerPrivate() {
  auto* overlay = overlay_.data();
  if (overlay == nullptr) {
    return;
  }

  overlay_.clear();
  overlay->releaseKeyboard();
  overlay->hide();
  overlay->deleteLater();
}

void PatchyColorPickerPrivate::build_ui() {
  owner_.setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  owner_.setMinimumSize(470, 360);
  custom_colors_.fill(Qt::white);
  load_custom_colors();

  auto* root = new QHBoxLayout(&owner_);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(14);

  auto* swatch_column = new QWidget(&owner_);
  auto* swatch_layout = new QVBoxLayout(swatch_column);
  swatch_layout->setContentsMargins(0, 0, 0, 0);
  swatch_layout->setSpacing(7);

  auto* basic_label = new QLabel(PatchyColorPicker::tr("Basic colors"), swatch_column);
  swatch_layout->addWidget(basic_label);

  auto* basic_grid = new QGridLayout();
  basic_grid->setContentsMargins(0, 0, 0, 0);
  basic_grid->setHorizontalSpacing(kSwatchSpacing);
  basic_grid->setVerticalSpacing(kSwatchSpacing);
  const auto colors = basic_colors();
  for (int index = 0; index < static_cast<int>(colors.size()); ++index) {
    const auto color = colors[static_cast<size_t>(index)];
    auto* button = make_swatch_button(swatch_column, color, QStringLiteral("patchyBasicColorSwatch"));
    QObject::connect(button, &QPushButton::clicked, &owner_, [this, color] {
      set_color(color, ColorChangeNotification::Yes);
    });
    basic_grid->addWidget(button, index / 8, index % 8);
  }
  swatch_layout->addLayout(basic_grid);

  auto* pick_screen = new QPushButton(PatchyColorPicker::tr("Pick Screen Color"), swatch_column);
  pick_screen->setObjectName(QStringLiteral("patchyPickScreenColorButton"));
  QObject::connect(pick_screen, &QPushButton::clicked, &owner_, [this] { start_screen_pick(); });
  swatch_layout->addWidget(pick_screen);
  swatch_layout->addStretch(1);

  auto* custom_label = new QLabel(PatchyColorPicker::tr("Custom colors"), swatch_column);
  swatch_layout->addWidget(custom_label);

  auto* custom_grid = new QGridLayout();
  custom_grid->setContentsMargins(0, 0, 0, 0);
  custom_grid->setHorizontalSpacing(kSwatchSpacing);
  custom_grid->setVerticalSpacing(kSwatchSpacing);
  for (int index = 0; index < kCustomColorCount; ++index) {
    auto* button = new QPushButton(swatch_column);
    button->setObjectName(QStringLiteral("patchyCustomColorSwatch"));
    button->setFocusPolicy(Qt::NoFocus);
    button->setFixedSize(kSwatchSize, kSwatchSize);
    custom_buttons_[static_cast<size_t>(index)] = button;
    QObject::connect(button, &QPushButton::clicked, &owner_, [this, index] {
      select_custom_color(index);
    });
    refresh_custom_swatch(index);
    custom_grid->addWidget(button, index / 8, index % 8);
  }
  swatch_layout->addLayout(custom_grid);

  auto* add_custom = new QPushButton(PatchyColorPicker::tr("Add to Custom Colors"), swatch_column);
  add_custom->setObjectName(QStringLiteral("patchyAddCustomColorButton"));
  QObject::connect(add_custom, &QPushButton::clicked, &owner_, [this] { add_current_to_custom_colors(); });
  swatch_layout->addWidget(add_custom);

  update_custom_button_ = new QPushButton(PatchyColorPicker::tr("Update Custom Color"), swatch_column);
  update_custom_button_->setObjectName(QStringLiteral("patchyUpdateCustomColorButton"));
  QObject::connect(update_custom_button_, &QPushButton::clicked, &owner_,
                   [this] { update_selected_custom_color(); });
  swatch_layout->addWidget(update_custom_button_);
  refresh_custom_controls();
  root->addWidget(swatch_column, 0);

  auto* picker_column = new QWidget(&owner_);
  auto* picker_layout = new QVBoxLayout(picker_column);
  picker_layout->setContentsMargins(0, 0, 0, 0);
  picker_layout->setSpacing(12);

  // Three interchangeable picker modes, all sharing the same HSV state. The
  // numeric footer below the tabs is shared and stays visible in every mode.
  tabs_ = new QTabWidget(picker_column);
  tabs_->setObjectName(QStringLiteral("patchyColorPickerTabs"));

  // Square mode: saturation/value plane + vertical hue bar (the original layout).
  auto* square_page = new QWidget(tabs_);
  auto* square_layout = new QHBoxLayout(square_page);
  square_layout->setContentsMargins(2, 6, 2, 2);
  square_layout->setSpacing(10);
  color_plane_ = new ColorPlaneWidget(*this, square_page);
  hue_slider_ = new HueSliderWidget(*this, square_page);
  square_layout->addWidget(color_plane_);
  square_layout->addWidget(hue_slider_);
  square_layout->addStretch(1);
  tabs_->addTab(square_page, PatchyColorPicker::tr("Square"));

  // Wheel mode: hue ring around an inner saturation/value square. The wheel widget
  // expands to fill this page (it self-centers the circle), so there is no wasted
  // margin around the ring.
  auto* wheel_page = new QWidget(tabs_);
  auto* wheel_layout = new QHBoxLayout(wheel_page);
  wheel_layout->setContentsMargins(4, 6, 4, 4);
  wheel_layout->setSpacing(0);
  wheel_ = new ColorWheelWidget(*this, wheel_page);
  wheel_layout->addWidget(wheel_);
  tabs_->addTab(wheel_page, PatchyColorPicker::tr("Wheel"));

  // Sliders mode: a gradient track per channel (H, S, V then R, G, B).
  auto* sliders_page = new QWidget(tabs_);
  auto* sliders_layout = new QGridLayout(sliders_page);
  sliders_layout->setContentsMargins(8, 10, 8, 8);
  sliders_layout->setHorizontalSpacing(10);
  sliders_layout->setVerticalSpacing(8);
  sliders_layout->setColumnStretch(1, 1);
  struct ChannelRow {
    ColorChannel channel;
    QString label;
    QString object_name;
  };
  const std::array<ChannelRow, 6> rows{{
      {ColorChannel::Hue, PatchyColorPicker::tr("Hue"), QStringLiteral("patchyChannelSliderHue")},
      {ColorChannel::Saturation, PatchyColorPicker::tr("Sat"), QStringLiteral("patchyChannelSliderSat")},
      {ColorChannel::Value, PatchyColorPicker::tr("Val"), QStringLiteral("patchyChannelSliderVal")},
      {ColorChannel::Red, PatchyColorPicker::tr("Red"), QStringLiteral("patchyChannelSliderRed")},
      {ColorChannel::Green, PatchyColorPicker::tr("Green"), QStringLiteral("patchyChannelSliderGreen")},
      {ColorChannel::Blue, PatchyColorPicker::tr("Blue"), QStringLiteral("patchyChannelSliderBlue")},
  }};
  for (int index = 0; index < static_cast<int>(rows.size()); ++index) {
    const auto& row = rows[static_cast<size_t>(index)];
    auto* label = new QLabel(row.label, sliders_page);
    label->setMinimumWidth(40);
    auto* slider = new ColorChannelSlider(*this, row.channel, row.object_name, sliders_page);
    channel_sliders_[static_cast<size_t>(index)] = slider;
    sliders_layout->addWidget(label, index, 0);
    sliders_layout->addWidget(slider, index, 1);
  }
  sliders_layout->setRowStretch(static_cast<int>(rows.size()), 1);
  tabs_->addTab(sliders_page, PatchyColorPicker::tr("Sliders"));

  // Local override of the global tab style: make the *selected* tab the light one
  // (the global theme darkens it, which reads as "not selected" on this dialog).
  tabs_->setStyleSheet(QStringLiteral(R"(
    QTabBar::tab {
      background: #343434;
      color: #c8c8c8;
      border: 1px solid #2a2a2a;
      padding: 5px 14px;
      min-height: 20px;
    }
    QTabBar::tab:hover:!selected { background: #404040; }
    QTabBar::tab:selected {
      background: #5a5a5a;
      color: #ffffff;
      border-bottom-color: #5a5a5a;
    }
  )"));

  // Re-open on whichever mode was used last.
  {
    auto settings = app_settings();
    tabs_->setCurrentIndex(std::clamp(settings.value(QLatin1String(kLastTabKey), 0).toInt(), 0, tabs_->count() - 1));
  }
  QObject::connect(tabs_, &QTabWidget::currentChanged, &owner_, [](int index) {
    auto settings = app_settings();
    settings.setValue(QLatin1String(kLastTabKey), index);
  });

  picker_layout->addWidget(tabs_, 1);

  // Shared numeric footer. HSV spins sit in columns 0-1, RGB spins in columns 3-4
  // (column 2 is a gap), and the HTML field gets its own full-width row so its
  // width can never distort a spin column or collide with the RGB block.
  auto* footer_row = new QHBoxLayout();
  footer_row->setContentsMargins(0, 0, 0, 0);
  footer_row->setSpacing(10);
  preview_ = new QFrame(picker_column);
  preview_->setObjectName(QStringLiteral("patchyColorPreview"));
  preview_->setFixedSize(52, 116);
  footer_row->addWidget(preview_);

  hue_spin_ = create_spin(picker_column, QStringLiteral("patchyColorHueSpin"), 359);
  saturation_spin_ = create_spin(picker_column, QStringLiteral("patchyColorSaturationSpin"), 255);
  value_spin_ = create_spin(picker_column, QStringLiteral("patchyColorValueSpin"), 255);
  red_spin_ = create_spin(picker_column, QStringLiteral("patchyColorRedSpin"), 255);
  green_spin_ = create_spin(picker_column, QStringLiteral("patchyColorGreenSpin"), 255);
  blue_spin_ = create_spin(picker_column, QStringLiteral("patchyColorBlueSpin"), 255);
  html_edit_ = new QLineEdit(picker_column);
  html_edit_->setObjectName(QStringLiteral("patchyColorHtmlEdit"));
  html_edit_->setMinimumWidth(90);

  auto* fields_grid = new QGridLayout();
  fields_grid->setContentsMargins(0, 0, 0, 0);
  fields_grid->setHorizontalSpacing(6);
  fields_grid->setVerticalSpacing(6);
  fields_grid->setColumnMinimumWidth(2, 12);
  fields_grid->addWidget(new QLabel(PatchyColorPicker::tr("Hue:"), picker_column), 0, 0);
  fields_grid->addWidget(hue_spin_, 0, 1);
  fields_grid->addWidget(new QLabel(PatchyColorPicker::tr("Sat:"), picker_column), 1, 0);
  fields_grid->addWidget(saturation_spin_, 1, 1);
  fields_grid->addWidget(new QLabel(PatchyColorPicker::tr("Val:"), picker_column), 2, 0);
  fields_grid->addWidget(value_spin_, 2, 1);
  fields_grid->addWidget(new QLabel(PatchyColorPicker::tr("Red:"), picker_column), 0, 3);
  fields_grid->addWidget(red_spin_, 0, 4);
  fields_grid->addWidget(new QLabel(PatchyColorPicker::tr("Green:"), picker_column), 1, 3);
  fields_grid->addWidget(green_spin_, 1, 4);
  fields_grid->addWidget(new QLabel(PatchyColorPicker::tr("Blue:"), picker_column), 2, 3);
  fields_grid->addWidget(blue_spin_, 2, 4);
  fields_grid->addWidget(new QLabel(PatchyColorPicker::tr("HTML:"), picker_column), 3, 0);
  fields_grid->addWidget(html_edit_, 3, 1, 1, 4);
  footer_row->addLayout(fields_grid);
  footer_row->addStretch(1);
  picker_layout->addLayout(footer_row);

  // Every mode-specific view repaints from one place in sync_controls().
  live_views_ = {color_plane_, hue_slider_, wheel_};
  for (auto* slider : channel_sliders_) {
    live_views_.push_back(slider);
  }

  root->addWidget(picker_column, 1);
  connect_controls();
}

QSpinBox* PatchyColorPickerPrivate::create_spin(QWidget* parent, const QString& object_name, int maximum) {
  auto* spin = new QSpinBox(parent);
  spin->setObjectName(object_name);
  spin->setRange(0, maximum);
  configure_dialog_spinbox(spin, 44);
  return spin;
}

void PatchyColorPickerPrivate::connect_controls() {
  QObject::connect(hue_spin_, &QSpinBox::valueChanged, &owner_, [this](int hue) {
    if (syncing_) {
      return;
    }
    hue_ = std::clamp(hue, 0, 359);
    apply_hsv_color(ColorChangeNotification::Yes);
  });
  QObject::connect(saturation_spin_, &QSpinBox::valueChanged, &owner_, [this](int saturation) {
    if (syncing_) {
      return;
    }
    saturation_ = bounded_channel(saturation);
    apply_hsv_color(ColorChangeNotification::Yes);
  });
  QObject::connect(value_spin_, &QSpinBox::valueChanged, &owner_, [this](int value) {
    if (syncing_) {
      return;
    }
    value_ = bounded_channel(value);
    apply_hsv_color(ColorChangeNotification::Yes);
  });
  QObject::connect(red_spin_, &QSpinBox::valueChanged, &owner_, [this](int red) {
    if (!syncing_) {
      set_color(QColor(red, color_.green(), color_.blue()), ColorChangeNotification::Yes);
    }
  });
  QObject::connect(green_spin_, &QSpinBox::valueChanged, &owner_, [this](int green) {
    if (!syncing_) {
      set_color(QColor(color_.red(), green, color_.blue()), ColorChangeNotification::Yes);
    }
  });
  QObject::connect(blue_spin_, &QSpinBox::valueChanged, &owner_, [this](int blue) {
    if (!syncing_) {
      set_color(QColor(color_.red(), color_.green(), blue), ColorChangeNotification::Yes);
    }
  });
  QObject::connect(html_edit_, &QLineEdit::editingFinished, &owner_, [this] {
    if (!syncing_) {
      set_html_color();
    }
  });
}

void PatchyColorPickerPrivate::set_color(QColor color, ColorChangeNotification notification) {
  if (!color.isValid()) {
    return;
  }

  color = normalized_rgb_color(color);
  const auto previous = color_;

  int color_hue = 0;
  int color_saturation = 0;
  int color_value = 0;
  color.getHsv(&color_hue, &color_saturation, &color_value);
  if (color_hue >= 0) {
    hue_ = std::clamp(color_hue, 0, 359);
  }
  saturation_ = bounded_channel(color_saturation);
  value_ = bounded_channel(color_value);
  color_ = color;
  sync_controls();

  if (notification == ColorChangeNotification::Yes && color_ != previous) {
    emit owner_.currentColorChanged(color_);
  }
}

void PatchyColorPickerPrivate::apply_hsv_color(ColorChangeNotification notification) {
  const auto previous = color_;
  color_ = normalized_rgb_color(QColor::fromHsv(std::clamp(hue_, 0, 359), bounded_channel(saturation_),
                                                bounded_channel(value_)));
  sync_controls();

  if (notification == ColorChangeNotification::Yes && color_ != previous) {
    emit owner_.currentColorChanged(color_);
  }
}

void PatchyColorPickerPrivate::set_saturation_value_from_point(QPoint point, QSize size) {
  const auto max_x = std::max(1, size.width() - 1);
  const auto max_y = std::max(1, size.height() - 1);
  const int x = std::clamp(point.x(), 0, max_x);
  const int y = std::clamp(point.y(), 0, max_y);
  saturation_ = x <= 2 ? 0 : (x >= max_x - 2 ? 255 : rounded_scaled_channel(x, max_x, 255));
  value_ = y <= 2 ? 255 : (y >= max_y - 2 ? 0 : 255 - rounded_scaled_channel(y, max_y, 255));
  apply_hsv_color(ColorChangeNotification::Yes);
}

void PatchyColorPickerPrivate::set_hue_from_point(QPoint point, QSize size) {
  const auto max_y = std::max(1, size.height() - 1);
  const int y = std::clamp(point.y(), 0, max_y);
  hue_ = rounded_scaled_channel(y, max_y, 359);
  apply_hsv_color(ColorChangeNotification::Yes);
}

void PatchyColorPickerPrivate::set_hue(int hue) {
  hue_ = std::clamp(hue, 0, 359);
  apply_hsv_color(ColorChangeNotification::Yes);
}

void PatchyColorPickerPrivate::set_channel(ColorChannel channel, int value) {
  switch (channel) {
    case ColorChannel::Red:
      set_color(QColor(bounded_channel(value), color_.green(), color_.blue()), ColorChangeNotification::Yes);
      return;
    case ColorChannel::Green:
      set_color(QColor(color_.red(), bounded_channel(value), color_.blue()), ColorChangeNotification::Yes);
      return;
    case ColorChannel::Blue:
      set_color(QColor(color_.red(), color_.green(), bounded_channel(value)), ColorChangeNotification::Yes);
      return;
    case ColorChannel::Hue:
      hue_ = std::clamp(value, 0, 359);
      apply_hsv_color(ColorChangeNotification::Yes);
      return;
    case ColorChannel::Saturation:
      saturation_ = bounded_channel(value);
      apply_hsv_color(ColorChangeNotification::Yes);
      return;
    case ColorChannel::Value:
      value_ = bounded_channel(value);
      apply_hsv_color(ColorChangeNotification::Yes);
      return;
  }
}

int PatchyColorPickerPrivate::channel_value(ColorChannel channel) const {
  switch (channel) {
    case ColorChannel::Red:
      return color_.red();
    case ColorChannel::Green:
      return color_.green();
    case ColorChannel::Blue:
      return color_.blue();
    case ColorChannel::Hue:
      return hue_;
    case ColorChannel::Saturation:
      return saturation_;
    case ColorChannel::Value:
      return value_;
  }
  return 0;
}

void PatchyColorPickerPrivate::sync_controls() {
  syncing_ = true;

  const QSignalBlocker hue_blocker(hue_spin_);
  const QSignalBlocker saturation_blocker(saturation_spin_);
  const QSignalBlocker value_blocker(value_spin_);
  const QSignalBlocker red_blocker(red_spin_);
  const QSignalBlocker green_blocker(green_spin_);
  const QSignalBlocker blue_blocker(blue_spin_);
  const QSignalBlocker html_blocker(html_edit_);

  hue_spin_->setValue(hue_);
  saturation_spin_->setValue(saturation_);
  value_spin_->setValue(value_);
  red_spin_->setValue(color_.red());
  green_spin_->setValue(color_.green());
  blue_spin_->setValue(color_.blue());
  html_edit_->setText(color_.name(QColor::HexRgb).toUpper());
  preview_->setStyleSheet(color_frame_style(color_));
  for (auto* view : live_views_) {
    if (view != nullptr) {
      view->update();
    }
  }

  syncing_ = false;
}

void PatchyColorPickerPrivate::set_html_color() {
  auto html = html_edit_->text().trimmed();
  if (!html.startsWith(QLatin1Char('#'))) {
    html.prepend(QLatin1Char('#'));
  }
  const QColor parsed(html);
  if (parsed.isValid()) {
    set_color(parsed, ColorChangeNotification::Yes);
  } else {
    sync_controls();
  }
}

void PatchyColorPickerPrivate::load_custom_colors() {
  auto settings = app_settings();
  const auto values = settings.value(QLatin1String(kCustomColorsKey)).toStringList();
  for (int index = 0; index < kCustomColorCount && index < values.size(); ++index) {
    if (values.at(index).isEmpty()) {
      continue;
    }
    const QColor color(values.at(index));
    if (color.isValid()) {
      custom_colors_[static_cast<size_t>(index)] = normalized_rgb_color(color);
    }
  }
  next_custom_slot_ = std::clamp(settings.value(QLatin1String(kNextCustomColorSlotKey), 0).toInt(), 0,
                                 kCustomColorCount - 1);
}

void PatchyColorPickerPrivate::save_custom_colors() const {
  QStringList values;
  values.reserve(kCustomColorCount);
  for (const auto& color : custom_colors_) {
    values.push_back(color.name(QColor::HexRgb).toUpper());
  }

  auto settings = app_settings();
  settings.setValue(QLatin1String(kCustomColorsKey), values);
  settings.setValue(QLatin1String(kNextCustomColorSlotKey), next_custom_slot_);
}

void PatchyColorPickerPrivate::refresh_custom_swatch(int index) {
  auto* button = custom_buttons_[static_cast<size_t>(index)];
  const auto color = custom_colors_[static_cast<size_t>(index)];
  if (button == nullptr) {
    return;
  }
  button->setStyleSheet(custom_swatch_style(color, index == selected_custom_slot_));
  button->setToolTip(color_tool_tip(color));
}

void PatchyColorPickerPrivate::refresh_custom_controls() {
  for (int index = 0; index < kCustomColorCount; ++index) {
    refresh_custom_swatch(index);
  }

  const bool has_selection = selected_custom_slot_ >= 0 && selected_custom_slot_ < kCustomColorCount;
  if (update_custom_button_ != nullptr) {
    update_custom_button_->setEnabled(has_selection);
  }
}

void PatchyColorPickerPrivate::add_current_to_custom_colors() {
  const int target_slot = next_custom_slot_;
  custom_colors_[static_cast<size_t>(target_slot)] = color_;
  selected_custom_slot_ = target_slot;
  next_custom_slot_ = (target_slot + 1) % kCustomColorCount;
  refresh_custom_controls();
  save_custom_colors();
}

void PatchyColorPickerPrivate::select_custom_color(int index) {
  selected_custom_slot_ = std::clamp(index, 0, kCustomColorCount - 1);
  const auto color = custom_colors_[static_cast<size_t>(selected_custom_slot_)];
  set_color(color, ColorChangeNotification::Yes);
  refresh_custom_controls();
}

void PatchyColorPickerPrivate::update_selected_custom_color() {
  if (selected_custom_slot_ < 0 || selected_custom_slot_ >= kCustomColorCount) {
    return;
  }

  custom_colors_[static_cast<size_t>(selected_custom_slot_)] = color_;
  next_custom_slot_ = (selected_custom_slot_ + 1) % kCustomColorCount;
  refresh_custom_controls();
  save_custom_colors();
}

void PatchyColorPickerPrivate::start_screen_pick() {
  if (overlay_ != nullptr) {
    return;
  }
  auto* overlay = new ScreenColorOverlay(*this);
  overlay_ = overlay;
  overlay->show();
  overlay->raise();
  overlay->activateWindow();
  overlay->setFocus();
  // Grab the keyboard so Escape cancels even if the frameless tool window does
  // not take activation focus on every platform.
  overlay->grabKeyboard();
}

void PatchyColorPickerPrivate::finish_screen_pick(QPoint global_position, bool sample) {
  auto* overlay = overlay_.data();
  if (overlay == nullptr) {
    return;
  }

  overlay_.clear();
  overlay->releaseKeyboard();
  overlay->hide();
  overlay->deleteLater();

  if (sample) {
    // Defer the grab one event-loop turn so the just-hidden overlay is gone from
    // the composited screen, yielding an untinted sample. owner_ is the context
    // object, so the callback is dropped if the picker is destroyed first.
    QTimer::singleShot(0, &owner_, [this, global_position] { sample_screen_color(global_position); });
  }
}

void PatchyColorPickerPrivate::sample_screen_color(QPoint global_position) {
  QScreen* screen = QGuiApplication::screenAt(global_position);
  if (screen == nullptr) {
    screen = QGuiApplication::primaryScreen();
  }
  if (screen == nullptr) {
    return;
  }

  const QPoint screen_position = global_position - screen->geometry().topLeft();
  const QPixmap sample = screen->grabWindow(0, screen_position.x(), screen_position.y(), 1, 1);
  if (sample.isNull()) {
    return;
  }

  const auto image = sample.toImage();
  if (!image.rect().contains(0, 0)) {
    return;
  }
  set_color(image.pixelColor(0, 0), ColorChangeNotification::Yes);
}

PatchyColorPicker::PatchyColorPicker(QColor initial, QWidget* parent)
    : QWidget(parent), impl_(std::make_unique<PatchyColorPickerPrivate>(*this)) {
  setObjectName(QStringLiteral("patchyAdvancedColorPicker"));
  impl_->build_ui();
  impl_->set_color(normalized_rgb_color(initial), ColorChangeNotification::No);
}

PatchyColorPicker::~PatchyColorPicker() = default;

QColor PatchyColorPicker::currentColor() const {
  return impl_->current_color();
}

void PatchyColorPicker::setCurrentColor(QColor color) {
  impl_->set_color(normalized_rgb_color(color), ColorChangeNotification::Yes);
}

QString color_button_style(QColor color) {
  const auto text = color.lightness() < 128 ? QStringLiteral("white") : QStringLiteral("black");
  return QStringLiteral(R"(
    QPushButton {
      background: rgb(%1, %2, %3);
      color: %4;
      border: 1px solid #f0f0f0;
      border-radius: 0;
      min-width: 26px;
      max-width: 26px;
      min-height: 24px;
      max-height: 24px;
      font-weight: 700;
      padding: 0;
    }
    QPushButton:hover {
      border-color: #4aa3ff;
    }
  )")
      .arg(color.red())
      .arg(color.green())
      .arg(color.blue())
      .arg(text);
}

QString swatch_button_style(QColor color, bool large) {
  return QStringLiteral(
             "QPushButton { background: rgb(%1, %2, %3); border: 1px solid #747b86; border-radius: 2px; min-width: %4px; "
             "min-height: %4px; max-width: %4px; max-height: %4px; }"
             "QPushButton:hover { border: 2px solid #63a6ff; }")
      .arg(color.red())
      .arg(color.green())
      .arg(color.blue())
      .arg(large ? 30 : 24);
}

QString inline_text_editor_style(QColor color, int pixel_size) {
  Q_UNUSED(pixel_size);
  return QStringLiteral(
             "QTextEdit { background: transparent; border: 1px dashed #63a8ff; padding: 0; color: rgb(%1, %2, %3); "
             "selection-background-color: rgba(49, 116, 190, 130); selection-color: rgb(%1, %2, %3); } "
             "QTextEdit QWidget { background: transparent; } "
             "QTextEdit::selection { background: rgba(49, 116, 190, 130); color: rgb(%1, %2, %3); }")
      .arg(color.red())
      .arg(color.green())
      .arg(color.blue());
}

QDialog* create_patchy_color_panel(QWidget* parent, QColor initial, const QString& title,
                                      std::function<void(QColor)> color_changed) {
  auto* dialog = new QDialog(parent);
  dialog->setObjectName(QStringLiteral("patchyColorDialog"));
  dialog->setModal(false);
  dialog->setWindowModality(Qt::NonModal);
  dialog->setAttribute(Qt::WA_DeleteOnClose, true);
  dialog->resize(540, 450);

  auto* layout = install_dark_dialog_chrome(*dialog, new QVBoxLayout(dialog), title);
  auto* picker = new PatchyColorPicker(normalized_rgb_color(initial), dialog);
  layout->addWidget(picker, 1);
  auto changed_callback = std::make_shared<std::function<void(QColor)>>(std::move(color_changed));
  QObject::connect(picker, &PatchyColorPicker::currentColorChanged, dialog, [changed_callback](QColor color) {
    color = normalized_rgb_color(color);
    if (*changed_callback) {
      (*changed_callback)(color);
    }
  });

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
  QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::close);
  remember_dialog_position(*dialog);
  return dialog;
}

std::optional<QColor> request_patchy_color(QWidget* parent, QColor initial, const QString& title,
                                           std::function<void(QColor)> color_changed) {
  // The picker runs a nested non-modal event loop, which keeps the widget that
  // launched it clickable. Guard against re-entrancy so spamming a colour swatch
  // cannot stack multiple identical pickers on top of each other.
  static bool request_in_progress = false;
  if (request_in_progress) {
    return std::nullopt;
  }
  request_in_progress = true;
  struct RequestGuard {
    ~RequestGuard() { request_in_progress = false; }
  } request_guard;

  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("patchyColorDialog"));
  dialog.resize(540, 450);

  auto* layout = install_dark_dialog_chrome(dialog, new QVBoxLayout(&dialog), title);
  auto* picker = new PatchyColorPicker(normalized_rgb_color(initial), &dialog);
  layout->addWidget(picker, 1);
  QObject::connect(picker, &PatchyColorPicker::currentColorChanged, &dialog, [color_changed = std::move(color_changed)](
                                                                               QColor color) {
    if (color_changed) {
      color_changed(normalized_rgb_color(color));
    }
  });

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (run_non_modal_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  return normalized_rgb_color(picker->currentColor());
}

}  // namespace patchy::ui
