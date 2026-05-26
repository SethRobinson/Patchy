#include "ui/color_panel.hpp"

#include "ui/dialog_utils.hpp"

#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPushButton>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace photoslop::ui {

namespace {

class ColorGradientField final : public QWidget {
public:
  explicit ColorGradientField(QWidget* parent = nullptr) : QWidget(parent) {
    setObjectName(QStringLiteral("colorGradientField"));
    setMinimumSize(280, 150);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::CrossCursor);
  }

  void set_color(QColor color) {
    color = color.toHsv();
    if (color.hue() >= 0) {
      hue_ = color.hue();
    }
    value_ = std::clamp(color.value(), 0, 255);
    update();
  }

  std::function<void(QColor)> color_chosen;

protected:
  void paintEvent(QPaintEvent* /*event*/) override {
    QPainter painter(this);
    const auto field = rect().adjusted(1, 1, -2, -2);
    if (field.isEmpty()) {
      return;
    }

    for (int x = field.left(); x <= field.right(); ++x) {
      const auto hue = static_cast<int>(std::round(
          359.0 * static_cast<double>(x - field.left()) / static_cast<double>(std::max(1, field.width() - 1))));
      QLinearGradient gradient(QPointF(x, field.top()), QPointF(x, field.bottom()));
      gradient.setColorAt(0.0, QColor::fromHsv(hue, 255, 255));
      gradient.setColorAt(1.0, QColor::fromHsv(hue, 255, 0));
      painter.fillRect(QRect(x, field.top(), 1, field.height()), gradient);
    }

    painter.setPen(QColor(150, 158, 170));
    painter.drawRect(field);

    const auto marker_x =
        field.left() + static_cast<int>(std::round(static_cast<double>(hue_) * field.width() / 359.0));
    const auto marker_y =
        field.top() + static_cast<int>(std::round(static_cast<double>(255 - value_) * field.height() / 255.0));
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(12, 14, 18), 3));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPoint(marker_x, marker_y), 6, 6);
    painter.setPen(QPen(QColor(250, 252, 255), 1));
    painter.drawEllipse(QPoint(marker_x, marker_y), 6, 6);
  }

  void mousePressEvent(QMouseEvent* event) override {
    choose_at(event->pos());
  }

  void mouseMoveEvent(QMouseEvent* event) override {
    if ((event->buttons() & Qt::LeftButton) != 0) {
      choose_at(event->pos());
    }
  }

private:
  void choose_at(QPoint point) {
    const auto field = rect().adjusted(1, 1, -2, -2);
    if (field.isEmpty()) {
      return;
    }
    const auto x = std::clamp(point.x(), field.left(), field.right());
    const auto y = std::clamp(point.y(), field.top(), field.bottom());
    hue_ = static_cast<int>(std::round(
        359.0 * static_cast<double>(x - field.left()) / static_cast<double>(std::max(1, field.width() - 1))));
    value_ = 255 - static_cast<int>(std::round(
                       255.0 * static_cast<double>(y - field.top()) /
                       static_cast<double>(std::max(1, field.height() - 1))));
    const auto color = QColor::fromHsv(hue_, 255, std::clamp(value_, 0, 255));
    if (color_chosen) {
      color_chosen(color);
    }
    update();
  }

  int hue_{0};
  int value_{255};
};

}  // namespace

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
  return QStringLiteral(
             "QTextEdit { background: transparent; color: rgb(%1, %2, %3); "
             "border: 1px dashed #63a8ff; padding: 0; font-size: %4px; } "
             "QTextEdit QWidget { background: transparent; } "
             "QTextEdit::selection { background: rgba(49, 116, 190, 130); }")
      .arg(color.red())
      .arg(color.green())
      .arg(color.blue())
      .arg(pixel_size);
}

QDialog* create_photoslop_color_panel(QWidget* parent, QColor initial, const QString& title,
                                      std::function<void(QColor)> color_changed) {
  initial.setAlpha(255);
  auto* dialog = new QDialog(parent);
  dialog->setObjectName(QStringLiteral("photoslopColorDialog"));
  dialog->setWindowTitle(title);
  dialog->setModal(false);
  dialog->setWindowModality(Qt::NonModal);
  dialog->setAttribute(Qt::WA_DeleteOnClose, true);
  auto* layout = new QVBoxLayout(dialog);
  auto* preview = new QLabel(dialog);
  preview->setObjectName(QStringLiteral("colorPreview"));
  preview->setFixedHeight(44);
  layout->addWidget(preview);
  auto* gradient_field = new ColorGradientField(dialog);
  gradient_field->set_color(initial);
  layout->addWidget(gradient_field);

  auto* grid = new QGridLayout();
  layout->addLayout(grid);

  auto add_channel = [&](int row, const QString& label, int value, const QString& object_prefix) {
    auto* slider = new QSlider(Qt::Horizontal, dialog);
    auto* spin = new QSpinBox(dialog);
    slider->setRange(0, 255);
    spin->setRange(0, 255);
    slider->setValue(value);
    spin->setValue(value);
    slider->setObjectName(object_prefix + QStringLiteral("Slider"));
    spin->setObjectName(object_prefix + QStringLiteral("Spin"));
    configure_dialog_spinbox(spin, 58);
    grid->addWidget(new QLabel(label, dialog), row, 0);
    grid->addWidget(slider, row, 1);
    grid->addWidget(spin, row, 2);
    QObject::connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
    QObject::connect(spin, &QSpinBox::valueChanged, slider, &QSlider::setValue);
    return spin;
  };

  auto* red = add_channel(0, QObject::tr("R"), initial.red(), QStringLiteral("colorRed"));
  auto* green = add_channel(1, QObject::tr("G"), initial.green(), QStringLiteral("colorGreen"));
  auto* blue = add_channel(2, QObject::tr("B"), initial.blue(), QStringLiteral("colorBlue"));
  const auto current_color = [red, green, blue] { return QColor(red->value(), green->value(), blue->value()); };
  const auto update_preview = [preview, gradient_field, current_color] {
    const auto color = current_color();
    preview->setStyleSheet(QStringLiteral("QLabel { background: rgb(%1, %2, %3); border: 1px solid #9aa4b2; }")
                               .arg(color.red())
                               .arg(color.green())
                               .arg(color.blue()));
    gradient_field->set_color(color);
  };
  auto changed_callback = std::make_shared<std::function<void(QColor)>>(std::move(color_changed));
  const auto apply_color = [current_color, update_preview, changed_callback] {
    update_preview();
    auto color = current_color();
    color.setAlpha(255);
    if (*changed_callback) {
      (*changed_callback)(color);
    }
  };
  QObject::connect(red, qOverload<int>(&QSpinBox::valueChanged), dialog, [apply_color](int) { apply_color(); });
  QObject::connect(green, qOverload<int>(&QSpinBox::valueChanged), dialog, [apply_color](int) { apply_color(); });
  QObject::connect(blue, qOverload<int>(&QSpinBox::valueChanged), dialog, [apply_color](int) { apply_color(); });
  gradient_field->color_chosen = [red, green, blue](QColor color) {
    red->setValue(color.red());
    green->setValue(color.green());
    blue->setValue(color.blue());
  };
  update_preview();

  auto* swatches = new QGridLayout();
  layout->addLayout(swatches);
  const std::vector<QColor> colors = {Qt::black,       Qt::white,       QColor(220, 20, 40), QColor(255, 140, 0),
                                      QColor(255, 220, 0), QColor(30, 160, 80), QColor(0, 150, 220), QColor(50, 90, 220),
                                      QColor(140, 70, 220), QColor(230, 60, 170), QColor(128, 128, 128), QColor(245, 248, 252)};
  int index = 0;
  for (const auto& color : colors) {
    auto* button = new QPushButton(dialog);
    button->setObjectName(QStringLiteral("colorDialogSwatch"));
    button->setStyleSheet(swatch_button_style(color, true));
    swatches->addWidget(button, index / 6, index % 6);
    QObject::connect(button, &QPushButton::clicked, dialog, [red, green, blue, color] {
      red->setValue(color.red());
      green->setValue(color.green());
      blue->setValue(color.blue());
    });
    ++index;
  }

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
  QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::close);
  return dialog;
}

}  // namespace photoslop::ui
