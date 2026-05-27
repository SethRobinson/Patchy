#include "ui/blend_mode_ui.hpp"

#include <QComboBox>
#include <QObject>

#include <array>

namespace patchy::ui {

QString blend_mode_name(BlendMode mode) {
  switch (mode) {
    case BlendMode::PassThrough:
      return QObject::tr("Pass Through");
    case BlendMode::Normal:
      return QObject::tr("Normal");
    case BlendMode::Multiply:
      return QObject::tr("Multiply");
    case BlendMode::Screen:
      return QObject::tr("Screen");
    case BlendMode::Overlay:
      return QObject::tr("Overlay");
    case BlendMode::Darken:
      return QObject::tr("Darken");
    case BlendMode::Lighten:
      return QObject::tr("Lighten");
    case BlendMode::ColorDodge:
      return QObject::tr("Color Dodge");
    case BlendMode::ColorBurn:
      return QObject::tr("Color Burn");
    case BlendMode::HardLight:
      return QObject::tr("Hard Light");
    case BlendMode::SoftLight:
      return QObject::tr("Soft Light");
    case BlendMode::Difference:
      return QObject::tr("Difference");
    case BlendMode::LinearBurn:
      return QObject::tr("Linear Burn");
    case BlendMode::PinLight:
      return QObject::tr("Pin Light");
    case BlendMode::Saturation:
      return QObject::tr("Saturation");
    case BlendMode::Luminosity:
      return QObject::tr("Luminosity");
  }
  return QObject::tr("Normal");
}


void add_blend_mode_items(QComboBox* combo) {
  constexpr std::array kBlendModes = {
      BlendMode::Normal,     BlendMode::Multiply,   BlendMode::Screen,     BlendMode::Overlay,
      BlendMode::Darken,     BlendMode::Lighten,    BlendMode::ColorDodge, BlendMode::ColorBurn,
      BlendMode::HardLight,  BlendMode::SoftLight,  BlendMode::Difference, BlendMode::LinearBurn,
      BlendMode::PinLight,   BlendMode::Saturation, BlendMode::Luminosity,
  };
  for (const auto mode : kBlendModes) {
    combo->addItem(blend_mode_name(mode), static_cast<int>(mode));
  }
}


}  // namespace patchy::ui
