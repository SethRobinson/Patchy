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

// The options bar Shape picker of the Custom Shape tool (GitHub issue 486). It stays a
// QComboBox, filled by populate_custom_shape_combo with the shape id as item data, so every
// caller keeps currentData/findData/setCurrentIndex and the currentIndexChanged handler. Its
// dropdown is a popup instead of the flat list: a search field with List and Grid buttons, the
// library as a folder tree with thumbnails or as a thumbnail grid under folder headers
// (PresetBrowserView; the view is remembered under kCustomShapePickerGridViewKey), whose folders
// open and close (remembered under kCustomShapePickerCollapsedFoldersKey), and a Shapes
// Manager... button. Picking a shape sets the combo's index; the button closes the popup
// first, then calls `manage`. Child object names: customShapeComboPopup, customShapeComboFilter,
// customShapeComboListViewButton, customShapeComboGridViewButton, customShapeComboTree,
// customShapeComboGrid, customShapeComboManageButton.
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
