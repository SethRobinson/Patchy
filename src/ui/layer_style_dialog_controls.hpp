#pragma once
// Private building blocks for the Layer Style dialog, never a UI umbrella.
#include "core/layer.hpp"
#include <QString>
#include <cstdint>
#include <limits>
class QAbstractButton;
class QFormLayout;
class QLabel;
class QRadioButton;
class QSpinBox;
class QVBoxLayout;
class QWidget;
namespace patchy::ui::layer_style_detail {
struct RgbColorRowWidgets {
  QSpinBox* red{};
  QSpinBox* green{};
  QSpinBox* blue{};
  QWidget* preview{};
  QAbstractButton* picker{};
  QWidget* row_container{};
  [[nodiscard]] RgbColor value() const;
  void set_value(RgbColor color) const;
  void update_preview() const;
};

// A page title for a QGroupBox, which reads "&" as a mnemonic marker: "Bevel &
// Emboss" would otherwise underline the space (GitHub issue 606).
[[nodiscard]] QString page_title(QString text);
void update_color_preview_label(QWidget* widget, int red, int green, int blue);
QString slider_object_name(QString spin_object_name);
QSpinBox* add_slider_spin_row(QFormLayout* form, QWidget* parent, const QString& label,
                                                   const QString& spin_object_name, int minimum, int maximum,
                                                   int value, const QString& suffix = {}, int spin_width = 72,
                                                   int slider_maximum = std::numeric_limits<int>::max());
QSpinBox* add_size_slider_spin_row(QFormLayout* form, QWidget* parent, const QString& label,
                                                        const QString& spin_object_name, int minimum,
                                                        int maximum, int value,
                                                        int slider_maximum = std::numeric_limits<int>::max());
// An angle row (GitHub issue 618): the round AngleDial in the slider's place,
// then the number field and its - / + pair, kept in step both ways. The dial
// is named after the field ("...Spin" -> "...Dial").
QSpinBox* add_angle_spin_row(QFormLayout* form, QWidget* parent, const QString& label,
                             const QString& spin_object_name, int minimum, int maximum, int value);
struct AngleAltitudeSpins {
  QSpinBox* angle{};
  QSpinBox* altitude{};
};
// Bevel & Emboss shading: an Angle row with the angle-and-altitude dial and an
// Altitude row below it, both fields in the page's field column.
AngleAltitudeSpins add_angle_altitude_rows(QFormLayout* form, QWidget* parent, const QString& angle_object_name,
                                           int angle, const QString& altitude_object_name, int altitude);
struct TwoChoiceRadios {
  QWidget* row{};
  QRadioButton* first{};
  QRadioButton* second{};
  void set_first(bool first_checked) const;
};
TwoChoiceRadios make_two_choice_radios(QWidget* parent, const QString& first_text,
                                       const QString& first_object_name, const QString& second_text,
                                       const QString& second_object_name, bool first_checked);
QLabel* add_form_section(QFormLayout* form, QWidget* parent, const QString& title, const QString& object_name);
RgbColorRowWidgets make_color_rows(QFormLayout* form, QWidget* group,
                                                 const QString& object_prefix, RgbColor initial);

}  // namespace patchy::ui::layer_style_detail
