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
    case BlendMode::Exclusion:
      return QObject::tr("Exclusion");
    case BlendMode::Hue:
      return QObject::tr("Hue");
    case BlendMode::Color:
      return QObject::tr("Color");
    case BlendMode::LinearDodge:
      return QObject::tr("Linear Dodge (Add)");
    case BlendMode::Subtract:
      return QObject::tr("Subtract");
    case BlendMode::Divide:
      return QObject::tr("Divide");
    case BlendMode::VividLight:
      return QObject::tr("Vivid Light");
    case BlendMode::LinearLight:
      return QObject::tr("Linear Light");
    case BlendMode::HardMix:
      return QObject::tr("Hard Mix");
    case BlendMode::DarkerColor:
      return QObject::tr("Darker Color");
    case BlendMode::LighterColor:
      return QObject::tr("Lighter Color");
  }
  return QObject::tr("Normal");
}


void add_blend_mode_items(QComboBox* combo) {
  // Photoshop's menu grouping order; item data carries the enum value, so display order is
  // free to differ from enum order.
  constexpr std::array kBlendModes = {
      BlendMode::Normal,
      BlendMode::Darken,     BlendMode::Multiply,    BlendMode::ColorBurn,
      BlendMode::LinearBurn, BlendMode::DarkerColor,
      BlendMode::Lighten,    BlendMode::Screen,      BlendMode::ColorDodge,
      BlendMode::LinearDodge, BlendMode::LighterColor,
      BlendMode::Overlay,    BlendMode::SoftLight,   BlendMode::HardLight,
      BlendMode::VividLight, BlendMode::LinearLight, BlendMode::PinLight,   BlendMode::HardMix,
      BlendMode::Difference, BlendMode::Exclusion,   BlendMode::Subtract,   BlendMode::Divide,
      BlendMode::Hue,        BlendMode::Saturation,  BlendMode::Color,      BlendMode::Luminosity,
  };
  for (const auto mode : kBlendModes) {
    combo->addItem(blend_mode_name(mode), static_cast<int>(mode));
  }
}


}  // namespace patchy::ui
