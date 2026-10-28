#pragma once

#include <QComboBox>
#include <QElapsedTimer>
#include <QPointer>

#include <functional>

class QFrame;

namespace patchy::ui {

class CustomShapeLibrary;

// Settings key of the Shape picker's collapsed folder rows (a list of folder names).
inline constexpr char kCustomShapePickerCollapsedFoldersKey[] = "tools/customShapePickerCollapsedFolders";
// Settings key of the Shape picker's view: true for the thumbnail grid (the default), false for
// the list with names.
inline constexpr char kCustomShapePickerGridViewKey[] = "tools/customShapePickerGridView";
// Settings key of the Shape picker popup's size (a QSize), set by dragging its corner grip.
inline constexpr char kCustomShapePickerSizeKey[] = "tools/customShapePickerSize";
// The popup's size before the user resizes it, and the smallest it goes.
inline constexpr int kCustomShapePickerDefaultWidth = 300;
inline constexpr int kCustomShapePickerDefaultHeight = 440;
inline constexpr int kCustomShapePickerMinimumWidth = 220;
inline constexpr int kCustomShapePickerMinimumHeight = 200;

// The size the popup opens at: `wanted` (the stored size, or the default when it is not
// valid) within the minimum and within `available` (the screen's available geometry), so a
// size stored on a larger screen never opens past the edges.
[[nodiscard]] QSize custom_shape_picker_popup_size(QSize wanted, const QSize& available);

// The options bar Shape picker of the Custom Shape tool (GitHub issue 486). It stays a
// QComboBox, filled by populate_custom_shape_combo with the shape id as item data, so every
// caller keeps currentData/findData/setCurrentIndex and the currentIndexChanged handler. Its
// dropdown is a popup instead of the flat list: a search field with List and Grid buttons, the
// library as a folder tree with thumbnails or as a thumbnail grid under folder headers
// (PresetBrowserView; the view is remembered under kCustomShapePickerGridViewKey), whose folders
// open and close (remembered under kCustomShapePickerCollapsedFoldersKey), and a Shapes
// Manager... button. Picking a shape sets the combo's index; the button closes the popup
// first, then calls `manage`. A size grip in the bottom-right corner resizes the popup (the
// grid reflows to the width); the size persists under kCustomShapePickerSizeKey, clamped to
// the screen on the next opening. Child object names: customShapeComboPopup, customShapeComboFilter,
// customShapeComboListViewButton, customShapeComboGridViewButton, customShapeComboTree,
// customShapeComboGrid, customShapeComboManageButton, customShapeComboSizeGrip.
class CustomShapePickerCombo : public QComboBox {
public:
  CustomShapePickerCombo(std::function<const CustomShapeLibrary*()> library, std::function<void()> manage,
                         QWidget* parent = nullptr);

  void showPopup() override;
  void hidePopup() override;
  [[nodiscard]] QFrame* picker_popup() const { return popup_; }

private:
  std::function<const CustomShapeLibrary*()> library_;
  std::function<void()> manage_;
  QPointer<QFrame> popup_;
  // The click that dismisses the popup lands on the combo again; ignore it briefly.
  QElapsedTimer dismissed_;
};

}  // namespace patchy::ui
