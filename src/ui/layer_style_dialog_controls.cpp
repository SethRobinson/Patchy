#include "ui/layer_style_dialog_controls.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/theme_qss.hpp"
#include <QAbstractButton>
#include <QButtonGroup>
#include <QFrame>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

namespace patchy::ui::layer_style_detail {
void update_color_preview_label(QWidget* widget, int red, int green, int blue) {
  set_themed_style(*widget, QStringLiteral("%1 { background: rgb(%2, %3, %4); border: 1px solid @swatch_border; }")
                           .arg(widget->metaObject()->className())
                           .arg(red)
                           .arg(green)
                           .arg(blue));
}

QString page_title(QString text) {
  return text.replace(QLatin1Char('&'), QStringLiteral("&&"));
}

RgbColor RgbColorRowWidgets::value() const {
    return RgbColor{static_cast<std::uint8_t>(red->value()), static_cast<std::uint8_t>(green->value()),
                    static_cast<std::uint8_t>(blue->value())};
}

void RgbColorRowWidgets::set_value(RgbColor color) const {
    red->setValue(color.red);
    green->setValue(color.green);
    blue->setValue(color.blue);
}

void RgbColorRowWidgets::update_preview() const {
    update_color_preview_label(preview, red->value(), green->value(), blue->value());
}

QString slider_object_name(QString spin_object_name) {
    if (spin_object_name.endsWith(QStringLiteral("Spin"))) {
      spin_object_name.chop(4);
    }
    return spin_object_name + QStringLiteral("Slider");
}
QSpinBox* add_slider_spin_row(QFormLayout* form, QWidget* parent, const QString& label,
                                                   const QString& spin_object_name, int minimum, int maximum,
                                                   int value, const QString& suffix, int spin_width,
                                                   int slider_maximum) {
    return add_dialog_slider_spin_row(form, parent, label, slider_object_name(spin_object_name),
                                      spin_object_name, minimum, maximum, value, suffix, spin_width,
                                      /*row_spacing=*/8, /*step_buttons=*/true, slider_maximum);
}
  // Effect sizes and distances: most picks are small, so the slider gives the
  // low end most of its track (SliderCurve::FineLowEnd).
QSpinBox* add_size_slider_spin_row(QFormLayout* form, QWidget* parent, const QString& label,
                                                        const QString& spin_object_name, int minimum,
                                                        int maximum, int value,
                                                        int slider_maximum) {
    return add_dialog_slider_spin_row(form, parent, label, slider_object_name(spin_object_name),
                                      spin_object_name, minimum, maximum, value, {}, 72,
                                      /*row_spacing=*/8, /*step_buttons=*/true, slider_maximum,
                                      SliderCurve::FineLowEnd);
}
// A two-choice setting shown as a pair of radio buttons (Bevel Direction, Inner
// Glow Source). The buttons carry the object names; the row is the form field.
TwoChoiceRadios make_two_choice_radios(QWidget* parent, const QString& first_text,
                                       const QString& first_object_name, const QString& second_text,
                                       const QString& second_object_name, bool first_checked) {
    TwoChoiceRadios radios;
    radios.row = new QWidget(parent);
    auto* layout = new QHBoxLayout(radios.row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);
    radios.first = new QRadioButton(first_text, radios.row);
    radios.first->setObjectName(first_object_name);
    radios.second = new QRadioButton(second_text, radios.row);
    radios.second->setObjectName(second_object_name);
    auto* group = new QButtonGroup(radios.row);
    group->addButton(radios.first);
    group->addButton(radios.second);
    (first_checked ? radios.first : radios.second)->setChecked(true);
    layout->addWidget(radios.first);
    layout->addWidget(radios.second);
    layout->addStretch(1);
    return radios;
}

void TwoChoiceRadios::set_first(bool first_checked) const {
    (first_checked ? first : second)->setChecked(true);
}

// A titled section inside a page's form: a line, then the bold title, both
// spanning the row, so every field stays in the page's one label column.
QLabel* add_form_section(QFormLayout* form, QWidget* parent, const QString& title,
                         const QString& object_name) {
    auto* separator = new QFrame(parent);
    separator->setObjectName(object_name + QStringLiteral("Separator"));
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Plain);
    form->addRow(separator);
    if (title.isEmpty()) {
      return nullptr;
    }
    auto* label = new QLabel(title, parent);
    label->setObjectName(object_name);
    auto font = label->font();
    font.setBold(true);
    label->setFont(font);
    form->addRow(label);
    return label;
}

  // The "Color:" form row: a swatch button that opens the color picker, then
  // R/G/B number fields. Widgets follow the shared naming scheme
  // (<prefix>RedSpin/<prefix>GreenSpin/<prefix>BlueSpin/<prefix>ColorPreview).
RgbColorRowWidgets make_color_rows(QFormLayout* form, QWidget* group,
                                                 const QString& object_prefix, RgbColor initial) {
    RgbColorRowWidgets rows;
    auto* color_row = new QWidget(group);
    auto* color_layout = new QHBoxLayout(color_row);
    color_layout->setContentsMargins(0, 0, 0, 0);
    color_layout->setSpacing(6);
    rows.row_container = color_row;
    auto* preview = new QPushButton(color_row);
    preview->setObjectName(object_prefix + QStringLiteral("ColorPreview"));
    preview->setFixedSize(34, 24);
    preview->setToolTip(QObject::tr("Choose Color..."));
    preview->setAccessibleName(QObject::tr("Choose Color..."));
    color_layout->addWidget(preview);
    color_layout->addSpacing(6);
    const auto add_channel = [&](const QString& label, const QString& accessible, const QString& name,
                                 std::uint8_t value) {
      auto* channel_label = new QLabel(label, color_row);
      auto* spin = new QSpinBox(color_row);
      spin->setObjectName(object_prefix + name);
      spin->setRange(0, 255);
      spin->setValue(value);
      spin->setAccessibleName(accessible);
      configure_dialog_spinbox(spin, 54);
      channel_label->setBuddy(spin);
      color_layout->addWidget(channel_label);
      color_layout->addWidget(spin);
      return spin;
    };
    rows.red = add_channel(QObject::tr("R:"), QObject::tr("Red"), QStringLiteral("RedSpin"), initial.red);
    rows.green =
        add_channel(QObject::tr("G:"), QObject::tr("Green"), QStringLiteral("GreenSpin"), initial.green);
    rows.blue = add_channel(QObject::tr("B:"), QObject::tr("Blue"), QStringLiteral("BlueSpin"), initial.blue);
    color_layout->addStretch(1);
    rows.preview = preview;
    rows.picker = preview;
    rows.update_preview();
    form->addRow(QObject::tr("Color:"), color_row);
    return rows;
}

}
