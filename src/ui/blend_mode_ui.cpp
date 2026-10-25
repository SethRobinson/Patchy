#include "ui/blend_mode_ui.hpp"

#include "core/blend_math.hpp"
#include "core/document_depth.hpp"
#include "ui/dialog_utils.hpp"
#include "filters/filter_registry.hpp"

#include <QAbstractItemView>
#include <QComboBox>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QSignalBlocker>
#include <QWheelEvent>
#include <QStandardItemModel>
#include <QObject>

#include <algorithm>
#include <array>

namespace patchy::ui {

namespace {

constexpr char kBlendModeArrowKeysProperty[] = "patchy.blendModeArrowKeys";

// A Paint.NET or GIMP mode's row (GitHub issue 379, docs/xcf.md): offered only while a
// layer uses it, and never a stop when the closed combo is stepped.
bool is_non_photoshop_row(const QComboBox* combo, int index) {
  const auto data = combo->itemData(index);
  return data.isValid() && blend_mode_is_non_photoshop(static_cast<BlendMode>(data.toInt()));
}

bool is_step_stop(const QComboBox* combo, int index) {
  const auto* model = qobject_cast<const QStandardItemModel*>(combo->model());
  const auto* item = model != nullptr ? model->item(index) : nullptr;
  return (item == nullptr || item->isEnabled()) && !is_non_photoshop_row(combo, index);
}

// The next row a step of `direction` (+1/-1) lands on from `from`, or `from` itself.
int next_step_stop(const QComboBox* combo, int from, int direction) {
  for (int index = from + direction; index >= 0 && index < combo->count(); index += direction) {
    if (is_step_stop(combo, index)) {
      return index;
    }
  }
  return from;
}

// Qt steps a closed combo box with Up/Down only (Left/Right are keypad-navigation
// only), while the Opacity and Fill fields beside the layer blend combo take all
// four arrows. The closed combo is stepped here (arrows, Home/End, Page Up/Down and
// the wheel) with Qt's rules (end clamping, disabled rows skipped) plus one more:
// Paint.NET's modes are never a stop, so stepping can leave one but never land on one.
// In the open list Left/Right are replayed as Up/Down for the highlight.
class BlendModeArrowKeys final : public QObject {
public:
  explicit BlendModeArrowKeys(QComboBox* combo) : QObject(combo), combo_(combo) {
    combo->installEventFilter(this);
    combo->view()->installEventFilter(this);
  }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (watched == combo_ && !combo_->isEditable()) {
      if (event->type() == QEvent::Wheel) {
        const auto* wheel = static_cast<QWheelEvent*>(event);
        const auto delta = wheel->angleDelta().y();
        if (delta != 0) {
          step_to(next_step_stop(combo_, combo_->currentIndex(), delta > 0 ? -1 : 1));
        }
        return true;
      }
      if (event->type() == QEvent::KeyPress) {
        const auto* key_event = static_cast<QKeyEvent*>(event);
        // Left/Right only bare (a modified arrow keeps its own meaning); the keys Qt
        // itself steps with also with Shift, as Qt does. Alt/Ctrl+Down opens the list.
        const auto modifiers = key_event->modifiers() & ~Qt::KeypadModifier;
        const bool horizontal = key_event->key() == Qt::Key_Left || key_event->key() == Qt::Key_Right;
        if (horizontal ? modifiers == Qt::NoModifier
                       : (modifiers & (Qt::AltModifier | Qt::ControlModifier | Qt::MetaModifier)) == 0) {
          const auto current = combo_->currentIndex();
          switch (key_event->key()) {
            case Qt::Key_Up:
            case Qt::Key_Left:
            case Qt::Key_PageUp:
              step_to(next_step_stop(combo_, current, -1));
              return true;
            case Qt::Key_Down:
            case Qt::Key_Right:
            case Qt::Key_PageDown:
              step_to(next_step_stop(combo_, current, 1));
              return true;
            case Qt::Key_Home:
              step_to(next_step_stop(combo_, -1, 1));
              return true;
            case Qt::Key_End:
              step_to(next_step_stop(combo_, combo_->count(), -1));
              return true;
            default:
              break;
          }
        }
      }
      return false;
    }
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

private:
  // What Qt's own stepping emits: the index change, then activated.
  void step_to(int index) {
    if (index < 0 || index >= combo_->count() || index == combo_->currentIndex()) {
      return;
    }
    combo_->setCurrentIndex(index);
    emit combo_->activated(index);
  }

  QComboBox* combo_;
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
    case BlendMode::Reflect:
      return QObject::tr("Reflect");
    case BlendMode::Glow:
      return QObject::tr("Glow");
    case BlendMode::Negation:
      return QObject::tr("Negation");
    case BlendMode::Xor:
      return QObject::tr("Xor");
    case BlendMode::GrainExtract:
      return QObject::tr("Grain Extract");
    case BlendMode::GrainMerge:
      return QObject::tr("Grain Merge");
    case BlendMode::HsvHue:
      return QObject::tr("HSV Hue");
    case BlendMode::HsvSaturation:
      return QObject::tr("HSV Saturation");
    case BlendMode::HslColor:
      return QObject::tr("HSL Color");
    case BlendMode::HsvValue:
      return QObject::tr("HSV Value");
    case BlendMode::LchHue:
      return QObject::tr("LCh Hue");
    case BlendMode::LchChroma:
      return QObject::tr("LCh Chroma");
    case BlendMode::LchColor:
      return QObject::tr("LCh Color");
    case BlendMode::LchLightness:
      return QObject::tr("LCh Lightness");
    case BlendMode::Luminance:
      return QObject::tr("Luminance");
    case BlendMode::LumaDarkenOnly:
      return QObject::tr("Luma Darken Only");
    case BlendMode::LumaLightenOnly:
      return QObject::tr("Luma Lighten Only");
    case BlendMode::LinearMultiply:
      return QObject::tr("Multiply (Linear)");
    case BlendMode::LinearAddition:
      return QObject::tr("Addition (Linear)");
    case BlendMode::LinearSubtract:
      return QObject::tr("Subtract (Linear)");
    case BlendMode::LinearDivide:
      return QObject::tr("Divide (Linear)");
    case BlendMode::SoftLightSvg:
      return QObject::tr("Soft Light (SVG)");
    case BlendMode::AdditiveSubtractive:
      return QObject::tr("Additive-Subtractive");
    case BlendMode::ArcTangent:
      return QObject::tr("Arc Tangent");
    case BlendMode::GeometricMean:
      return QObject::tr("Geometric Mean");
    case BlendMode::Allanon:
      return QObject::tr("Allanon");
    case BlendMode::Parallel:
      return QObject::tr("Parallel");
    case BlendMode::HardOverlay:
      return QObject::tr("Hard Overlay");
    case BlendMode::Interpolation:
      return QObject::tr("Interpolation");
    case BlendMode::EasyDodge:
      return QObject::tr("Easy Dodge");
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

// Paint.NET's modes (GitHub issue 379) and GIMP's grain modes (docs/xcf.md): no menu
// lists them. A layer or folder combo shows one, after Photoshop's list, only while the
// layer it edits uses it (set_non_photoshop_blend_modes_offered), so nobody picks a mode a
// PSD cannot keep.
constexpr std::array kNonPhotoshopBlendModes = {
    BlendMode::Negation, BlendMode::Reflect,      BlendMode::Glow,
    BlendMode::Xor,      BlendMode::GrainExtract, BlendMode::GrainMerge,
    BlendMode::HsvHue,
    BlendMode::HsvSaturation,
    BlendMode::HslColor,
    BlendMode::HsvValue,
    BlendMode::LchHue,
    BlendMode::LchChroma,
    BlendMode::LchColor,
    BlendMode::LchLightness,
    BlendMode::Luminance,
    BlendMode::LumaDarkenOnly,
    BlendMode::LumaLightenOnly,
    BlendMode::LinearMultiply,
    BlendMode::LinearAddition,
    BlendMode::LinearSubtract,
    BlendMode::LinearDivide,
    BlendMode::SoftLightSvg,
    BlendMode::AdditiveSubtractive,
    BlendMode::ArcTangent,
    BlendMode::GeometricMean,
    BlendMode::Allanon,
    BlendMode::Parallel,
    BlendMode::HardOverlay,
    BlendMode::Interpolation,
    BlendMode::EasyDodge,
};

// Behind and Clear are not BlendMode values; their item data sits past the enum.
constexpr int kPaintModeBehindData = 1000;
constexpr int kPaintModeClearData = 1001;

void install_blend_mode_arrow_keys(QComboBox* combo) {
  if (!combo->property(kBlendModeArrowKeysProperty).toBool()) {
    combo->setProperty(kBlendModeArrowKeysProperty, true);
    new BlendModeArrowKeys(combo);
  }
  // The menu form placed the current row over the box and ran off the top of a small
  // screen (GitHub issue 414); the list form opens below the box and stays on the screen.
  use_scrolling_list_popup(*combo, kBlendModeListPopupRows);
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

void set_non_photoshop_blend_modes_offered(QComboBox* combo, const std::vector<BlendMode>& in_use) {
  if (combo == nullptr) {
    return;
  }
  // The caller sets the current row afterwards; the removals here must not reach a
  // blend-change handler as a user choice.
  const QSignalBlocker blocker(combo);
  for (const auto mode : kNonPhotoshopBlendModes) {
    const bool wanted = std::find(in_use.begin(), in_use.end(), mode) != in_use.end();
    const auto index = combo->findData(static_cast<int>(mode));
    if (!wanted && index >= 0) {
      combo->removeItem(index);
    }
  }
  for (const auto mode : kNonPhotoshopBlendModes) {
    const bool wanted = std::find(in_use.begin(), in_use.end(), mode) != in_use.end();
    if (wanted && combo->findData(static_cast<int>(mode)) < 0) {
      combo->addItem(blend_mode_name(mode), static_cast<int>(mode));
    }
  }
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
