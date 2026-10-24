#include "ui/blend_mode_ui.hpp"

#include "core/document_depth.hpp"
#include "filters/filter_registry.hpp"

#include <QAbstractItemView>
#include <QComboBox>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QStandardItemModel>
#include <QObject>

#include <algorithm>
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


namespace {

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

// Behind and Clear are not BlendMode values; their item data sits past the enum.
constexpr int kPaintModeBehindData = 1000;
constexpr int kPaintModeClearData = 1001;

void install_blend_mode_arrow_keys(QComboBox* combo) {
  if (!combo->property(kBlendModeArrowKeysProperty).toBool()) {
    combo->setProperty(kBlendModeArrowKeysProperty, true);
    new BlendModeArrowKeys(combo);
  }
}

}  // namespace

void add_blend_mode_items(QComboBox* combo, BlendModeMenu menu) {
  if (menu == BlendModeMenu::Folder) {
    combo->addItem(blend_mode_name(BlendMode::PassThrough), static_cast<int>(BlendMode::PassThrough));
  }
  for (const auto mode : kBlendModes) {
    if (menu == BlendModeMenu::Filter && !recipe_blend_mode_supported(mode)) {
      continue;
    }
    combo->addItem(blend_mode_name(mode), static_cast<int>(mode));
  }
  install_blend_mode_arrow_keys(combo);
}

void set_blend_mode_pass_through_offered(QComboBox* combo, bool offered) {
  if (combo == nullptr) {
    return;
  }
  const auto index = combo->findData(static_cast<int>(BlendMode::PassThrough));
  if (offered && index < 0) {
    combo->insertItem(0, blend_mode_name(BlendMode::PassThrough), static_cast<int>(BlendMode::PassThrough));
  } else if (!offered && index >= 0) {
    combo->removeItem(index);
  }
}

QString paint_mode_name(PaintMode mode) {
  switch (mode.kind) {
    case PaintModeKind::Behind:
      return QCoreApplication::translate("patchy::ui::PaintMode", "Behind");
    case PaintModeKind::Clear:
      return QCoreApplication::translate("patchy::ui::PaintMode", "Clear");
    case PaintModeKind::Blend:
      break;
  }
  return blend_mode_name(mode.blend);
}

int paint_mode_item_data(PaintMode mode) {
  switch (mode.kind) {
    case PaintModeKind::Behind:
      return kPaintModeBehindData;
    case PaintModeKind::Clear:
      return kPaintModeClearData;
    case PaintModeKind::Blend:
      break;
  }
  return static_cast<int>(mode.blend);
}

PaintMode paint_mode_from_item_data(int data) {
  if (data == kPaintModeBehindData) {
    return PaintMode::behind();
  }
  if (data == kPaintModeClearData) {
    return PaintMode::clear();
  }
  const auto found = std::find_if(kBlendModes.begin(), kBlendModes.end(),
                                  [data](BlendMode mode) { return static_cast<int>(mode) == data; });
  return found != kBlendModes.end() ? PaintMode::of(*found) : PaintMode{};
}

void add_paint_mode_items(QComboBox* combo) {
  const auto add = [combo](PaintMode mode) { combo->addItem(paint_mode_name(mode), paint_mode_item_data(mode)); };
  for (const auto mode : kBlendModes) {
    add(PaintMode::of(mode));
    if (mode == BlendMode::Dissolve) {
      add(PaintMode::behind());
      add(PaintMode::clear());
    }
  }
  install_blend_mode_arrow_keys(combo);
}

void retranslate_paint_mode_items(QComboBox* combo) {
  if (combo == nullptr) {
    return;
  }
  for (int index = 0; index < combo->count(); ++index) {
    combo->setItemText(index, paint_mode_name(paint_mode_from_item_data(combo->itemData(index).toInt())));
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

void PopupAwareComboBox::showPopup() {
  if (popup_shown) {
    popup_shown();
  }
  // The list's window exists once view() is called; watching its Hide catches closes
  // that bypass hidePopup.
  if (auto* popup = view()->window(); popup != nullptr && popup != watched_popup_ && popup != window()) {
    if (watched_popup_ != nullptr) {
      watched_popup_->removeEventFilter(this);
    }
    popup->installEventFilter(this);
    watched_popup_ = popup;
  }
  QComboBox::showPopup();
}

void PopupAwareComboBox::hidePopup() {
  QComboBox::hidePopup();
  if (popup_hidden) {
    popup_hidden();
  }
}

bool PopupAwareComboBox::eventFilter(QObject* watched, QEvent* event) {
  if (watched == watched_popup_ && event->type() == QEvent::Hide && popup_hidden) {
    popup_hidden();
  }
  return QComboBox::eventFilter(watched, event);
}

}  // namespace patchy::ui
