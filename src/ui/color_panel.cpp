#include "ui/color_panel.hpp"

#include "ui/app_settings.hpp"

#include "ui/dialog_utils.hpp"

#include <QApplication>
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
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QTabletEvent>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <vector>

namespace patchy::ui {

namespace {

constexpr int kColorPlaneSize = 220;
constexpr int kHueSliderWidth = 18;
constexpr int kSwatchSize = 24;
constexpr int kSwatchSpacing = 5;
constexpr int kCustomColorCount = 16;
constexpr auto kCustomColorsKey = "colorPanel/customColors";
constexpr auto kNextCustomColorSlotKey = "colorPanel/nextCustomColorSlot";

enum class ColorChangeNotification {
  No,
  Yes,
};

class ColorPlaneWidget;
class HueSliderWidget;
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

std::vector<QColor> basic_colors() {
  return {
      QColor(0, 0, 0),       QColor(128, 0, 0),     QColor(0, 128, 0),     QColor(128, 64, 0),
      QColor(0, 128, 255),   QColor(128, 128, 0),   QColor(0, 255, 0),     QColor(128, 255, 0),
      QColor(0, 0, 192),     QColor(192, 0, 192),   QColor(0, 96, 128),    QColor(160, 80, 80),
      QColor(0, 192, 96),    QColor(160, 160, 128), QColor(0, 255, 128),   QColor(128, 255, 64),
      QColor(0, 0, 255),     QColor(192, 0, 255),   QColor(0, 128, 255),   QColor(192, 160, 255),
      QColor(0, 192, 192),   QColor(192, 192, 255), QColor(0, 255, 192),   QColor(160, 255, 224),
      QColor(128, 0, 0),     QColor(255, 0, 0),     QColor(128, 128, 0),   QColor(255, 96, 0),
      QColor(128, 255, 0),   QColor(255, 176, 0),   QColor(64, 255, 0),    QColor(255, 255, 0),
      QColor(96, 0, 160),    QColor(255, 0, 128),   QColor(96, 96, 160),   QColor(255, 96, 128),
      QColor(96, 192, 160),  QColor(255, 176, 160), QColor(64, 255, 128),  QColor(255, 255, 96),
      QColor(96, 0, 255),    QColor(255, 0, 255),   QColor(96, 96, 255),   QColor(255, 160, 255),
      QColor(96, 255, 255),  QColor(160, 255, 255), QColor(96, 255, 224),  QColor(255, 255, 255),
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
  void add_current_to_custom_colors();
  void select_custom_color(int index);
  void update_selected_custom_color();
  void start_screen_pick();
  void finish_screen_pick(QPoint global_position, bool sample);

  [[nodiscard]] QColor current_color() const { return color_; }
  [[nodiscard]] int hue() const { return hue_; }
  [[nodiscard]] int saturation() const { return saturation_; }
  [[nodiscard]] int value() const { return value_; }

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
  ColorPlaneWidget* color_plane_{nullptr};
  HueSliderWidget* hue_slider_{nullptr};
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

    QImage image(paint_size, QImage::Format_RGB32);
    const auto max_x = std::max(1, paint_size.width() - 1);
    const auto max_y = std::max(1, paint_size.height() - 1);
    for (int y = 0; y < paint_size.height(); ++y) {
      auto* scanline = reinterpret_cast<QRgb*>(image.scanLine(y));
      const int value = 255 - rounded_scaled_channel(y, max_y, 255);
      for (int x = 0; x < paint_size.width(); ++x) {
        const int saturation = rounded_scaled_channel(x, max_x, 255);
        scanline[x] = QColor::fromHsv(picker_.hue(), saturation, value).rgb();
      }
    }

    QPainter painter(this);
    painter.drawImage(QPoint(0, 0), image);
    painter.setPen(QPen(QColor(26, 26, 26), 1));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));

    const double cursor_x = static_cast<double>(picker_.saturation()) / 255.0 * max_x;
    const double cursor_y = static_cast<double>(255 - picker_.value()) / 255.0 * max_y;
    const QPointF cursor(cursor_x, cursor_y);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(10, 10, 10), 3.0));
    painter.drawLine(cursor + QPointF(-8.0, 0.0), cursor + QPointF(8.0, 0.0));
    painter.drawLine(cursor + QPointF(0.0, -8.0), cursor + QPointF(0.0, 8.0));
    painter.setPen(QPen(QColor(245, 245, 245), 1.0));
    painter.drawLine(cursor + QPointF(-8.0, 0.0), cursor + QPointF(8.0, 0.0));
    painter.drawLine(cursor + QPointF(0.0, -8.0), cursor + QPointF(0.0, 8.0));
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
  owner_.setMinimumSize(520, 360);
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

  auto* selector_row = new QHBoxLayout();
  selector_row->setContentsMargins(0, 0, 0, 0);
  selector_row->setSpacing(10);
  color_plane_ = new ColorPlaneWidget(*this, picker_column);
  hue_slider_ = new HueSliderWidget(*this, picker_column);
  selector_row->addWidget(color_plane_);
  selector_row->addWidget(hue_slider_);
  selector_row->addStretch(1);
  picker_layout->addLayout(selector_row);

  auto* controls_row = new QHBoxLayout();
  controls_row->setContentsMargins(0, 0, 0, 0);
  controls_row->setSpacing(12);
  preview_ = new QFrame(picker_column);
  preview_->setObjectName(QStringLiteral("patchyColorPreview"));
  preview_->setFixedSize(58, 116);
  controls_row->addWidget(preview_);

  hue_spin_ = create_spin(picker_column, QStringLiteral("patchyColorHueSpin"), 359);
  saturation_spin_ = create_spin(picker_column, QStringLiteral("patchyColorSaturationSpin"), 255);
  value_spin_ = create_spin(picker_column, QStringLiteral("patchyColorValueSpin"), 255);
  red_spin_ = create_spin(picker_column, QStringLiteral("patchyColorRedSpin"), 255);
  green_spin_ = create_spin(picker_column, QStringLiteral("patchyColorGreenSpin"), 255);
  blue_spin_ = create_spin(picker_column, QStringLiteral("patchyColorBlueSpin"), 255);
  html_edit_ = new QLineEdit(picker_column);
  html_edit_->setObjectName(QStringLiteral("patchyColorHtmlEdit"));
  html_edit_->setFixedWidth(90);

  auto* hsv_grid = new QGridLayout();
  hsv_grid->setContentsMargins(0, 0, 0, 0);
  hsv_grid->setHorizontalSpacing(6);
  hsv_grid->setVerticalSpacing(6);
  hsv_grid->addWidget(new QLabel(PatchyColorPicker::tr("Hue:"), picker_column), 0, 0);
  hsv_grid->addWidget(hue_spin_, 0, 1);
  hsv_grid->addWidget(new QLabel(PatchyColorPicker::tr("Sat:"), picker_column), 1, 0);
  hsv_grid->addWidget(saturation_spin_, 1, 1);
  hsv_grid->addWidget(new QLabel(PatchyColorPicker::tr("Val:"), picker_column), 2, 0);
  hsv_grid->addWidget(value_spin_, 2, 1);
  hsv_grid->addWidget(new QLabel(PatchyColorPicker::tr("HTML:"), picker_column), 3, 0);
  hsv_grid->addWidget(html_edit_, 3, 1);
  controls_row->addLayout(hsv_grid);

  auto* rgb_grid = new QGridLayout();
  rgb_grid->setContentsMargins(0, 0, 0, 0);
  rgb_grid->setHorizontalSpacing(6);
  rgb_grid->setVerticalSpacing(6);
  rgb_grid->addWidget(new QLabel(PatchyColorPicker::tr("Red:"), picker_column), 0, 0);
  rgb_grid->addWidget(red_spin_, 0, 1);
  rgb_grid->addWidget(new QLabel(PatchyColorPicker::tr("Green:"), picker_column), 1, 0);
  rgb_grid->addWidget(green_spin_, 1, 1);
  rgb_grid->addWidget(new QLabel(PatchyColorPicker::tr("Blue:"), picker_column), 2, 0);
  rgb_grid->addWidget(blue_spin_, 2, 1);
  controls_row->addLayout(rgb_grid);
  controls_row->addStretch(1);
  picker_layout->addLayout(controls_row);

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
  color_plane_->update();
  hue_slider_->update();

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
  dialog->resize(560, 520);

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

std::optional<QColor> request_patchy_color(QWidget* parent, QColor initial, const QString& title) {
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
  dialog.resize(560, 520);

  auto* layout = install_dark_dialog_chrome(dialog, new QVBoxLayout(&dialog), title);
  auto* picker = new PatchyColorPicker(normalized_rgb_color(initial), &dialog);
  layout->addWidget(picker, 1);

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
