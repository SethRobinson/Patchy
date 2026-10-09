#include "ui/blend_mode_ui.hpp"

#include "core/document_depth.hpp"
#include "filters/filter_registry.hpp"

#include <QAbstractItemView>
#include <QComboBox>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QStandardItemModel>
#include <QObject>

#include <array>

namespace patchy::ui {

namespace {

constexpr char kBlendModeArrowKeysProperty[] = "patchy.blendModeArrowKeys";

// Qt steps a closed combo box with Up/Down only (Left/Right are keypad-navigation
// only), while the Opacity and Fill fields beside the layer blend combo take all
// four arrows. Left/Right are replayed as Up/Down to the same widget, so Qt's own
// stepping (end clamping, disabled items) and the open list's highlight movement
// apply unchanged.
class BlendModeArrowKeys final : public QObject {
public:
  explicit BlendModeArrowKeys(QComboBox* combo) : QObject(combo) {
    combo->installEventFilter(this);
    combo->view()->installEventFilter(this);
  }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() != QEvent::KeyPress) {
      return false;
    }
    const auto* key_event = static_cast<QKeyEvent*>(event);
    if ((key_event->key() != Qt::Key_Left && key_event->key() != Qt::Key_Right) ||
        (key_event->modifiers() & ~Qt::KeypadModifier) != Qt::NoModifier) {
      return false;
    }
    if (const auto* combo = qobject_cast<QComboBox*>(watched); combo != nullptr && combo->isEditable()) {
      return false;  // the line edit owns Left/Right for its caret
    }
    QKeyEvent replacement(QEvent::KeyPress, key_event->key() == Qt::Key_Left ? Qt::Key_Up : Qt::Key_Down,
                          key_event->modifiers(), QString(), key_event->isAutoRepeat(),
                          static_cast<quint16>(key_event->count()));
    QCoreApplication::sendEvent(watched, &replacement);
    return true;
  }
};

}  // namespace

QString blend_mode_name(BlendMode mode) {
  switch (mode) {
    case BlendMode::PassThrough:
      return QObject::tr("Pass Through");
    case BlendMode::Normal:
      return QObject::tr("Normal");
    case BlendMode::Dissolve:
      return QObject::tr("Dissolve");
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


void add_blend_mode_items(QComboBox* combo, BlendModeMenu menu) {
  // Photoshop's menu grouping order; item data carries the enum value, so display order is
  // free to differ from enum order.
  constexpr std::array kBlendModes = {
      BlendMode::Normal,     BlendMode::Dissolve,
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
    if (menu == BlendModeMenu::Filter && !recipe_blend_mode_supported(mode)) {
      continue;
    }
    combo->addItem(blend_mode_name(mode), static_cast<int>(mode));
  }
  if (!combo->property(kBlendModeArrowKeysProperty).toBool()) {
    combo->setProperty(kBlendModeArrowKeysProperty, true);
    new BlendModeArrowKeys(combo);
  }
}


void enable_blend_mode_items_for_depth(QComboBox* combo, BitDepth depth) {
  auto* model = combo != nullptr ? qobject_cast<QStandardItemModel*>(combo->model()) : nullptr;
  if (model == nullptr) {
    return;
  }
  for (int index = 0; index < combo->count(); ++index) {
    if (auto* item = model->item(index); item != nullptr) {
      item->setEnabled(blend_mode_supported_at_depth(static_cast<BlendMode>(combo->itemData(index).toInt()), depth));
    }
  }
}

}  // namespace patchy::ui
