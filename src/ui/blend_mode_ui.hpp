#pragma once

#include "core/layer.hpp"
#include "core/paint_mode.hpp"
#include "core/pixel_buffer.hpp"

#include <QComboBox>
#include <QString>

#include <cstdint>
#include <functional>
#include <vector>

namespace patchy::ui {

// Which menu is being built. Layer offers every selectable mode; Filter drops
// the ones a recipe / Smart Filter blend step cannot execute (currently
// Dissolve), so the combo never offers a mode that would be rejected on save.
// Folder is Layer with Pass Through first: only a folder can pass through, and
// folders are created and imported in it (GitHub issue 235). Effect is the layer-effect
// list. No menu lists Paint.NET's modes (GitHub issue 379): see
// set_paint_dot_net_blend_modes_offered.
enum class BlendModeMenu : std::uint8_t { Layer, Filter, Folder, Effect };

[[nodiscard]] QString blend_mode_name(BlendMode mode);
// Fills `combo` with the modes in Photoshop's menu order and makes Left/Right
// step it like Up/Down (Qt handles only Up/Down on a combo box), both closed
// and with the list open.
void add_blend_mode_items(QComboBox* combo, BlendModeMenu menu = BlendModeMenu::Layer);
// For a combo that serves layers and folders in turn (the Layers panel's): adds Pass
// Through as the first item when `offered` and it is missing, and removes it otherwise.
void set_blend_mode_pass_through_offered(QComboBox* combo, bool offered);
// Paint.NET's Reflect, Glow, Negation and Xor (GitHub issue 379) appear in a layer or
// folder combo only while a layer it edits uses one: shows exactly the modes in `in_use`
// that are Paint.NET-only, after Photoshop's list, and removes the rest. Stepping the
// closed combo (arrows, wheel) never lands on them.
void set_paint_dot_net_blend_modes_offered(QComboBox* combo, const std::vector<BlendMode>& in_use);
// The Brush tool's Mode menu (core/paint_mode.hpp): Normal, Dissolve, Behind, Clear, then
// the layer list's groups in the same order. Item data is paint_mode_item_data; the same
// Left/Right stepping as add_blend_mode_items. retranslate_paint_mode_items renames them.
[[nodiscard]] QString paint_mode_name(PaintMode mode);
[[nodiscard]] int paint_mode_item_data(PaintMode mode);
[[nodiscard]] PaintMode paint_mode_from_item_data(int data);
void add_paint_mode_items(QComboBox* combo);
void retranslate_paint_mode_items(QComboBox* combo);
// Disables the layer blend modes a document of `depth` cannot use (32-bit documents,
// blend_mode_supported_at_depth) and re-enables the rest; Qt's stepping skips them.
void enable_blend_mode_items_for_depth(QComboBox* combo, BitDepth depth);

// A combo box that reports its list opening and closing (the Layers panel's live blend
// preview, GitHub issue 367). popup_shown runs before the list appears, so the highlight
// Qt sets on opening already finds the handler ready. popup_hidden runs once the list is
// gone, before a chosen item's currentIndexChanged; it also runs when the list closes
// without hidePopup (the window losing focus), and may run twice for one close.
class PopupAwareComboBox final : public QComboBox {
public:
  using QComboBox::QComboBox;
  std::function<void()> popup_shown;
  std::function<void()> popup_hidden;
  void showPopup() override;
  void hidePopup() override;

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  QObject* watched_popup_{nullptr};
};

}  // namespace patchy::ui
