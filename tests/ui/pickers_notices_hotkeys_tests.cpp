#include <QClipboard>
#include <QStatusBar>

#include "ui/canvas_widget.hpp"
#include "ui/qt_paths.hpp"
#include "core/adjustment_layer.hpp"
#include "core/document_depth.hpp"
#include "core/pixel_depth.hpp"
#include "core/layer_metadata.hpp"
#include "core/smart_object.hpp"
#include "ui/color_panel.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/eyedropper_sample_overlay.hpp"
#include "ui/theme_qss.hpp"
#include "ui/theme_palette.hpp"
#include "ui/compatibility_report.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/tga_document_io.hpp"
#include "ui/image_document_io.hpp"
#include "ui/measurement_units.hpp"
#include "ui/main_window.hpp"
#include "ui/tool_cursors.hpp"
#include "ui/script_folders.hpp"
#include "ui/app_settings.hpp"
#include "psd/psd_document_io.hpp"
#include "test_harness.hpp"
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QAction>
#include <chrono>
#include <tuple>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFrame>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QTemporaryDir>
#include <QMetaObject>
#include <QPainter>
#include <QMessageBox>
#include <QMenu>
#include <QMenuBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QSlider>
#include <QTabWidget>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVariant>
#include <QWheelEvent>
#include <QWidget>
#include <QWindow>
#include <algorithm>
#include <atomic>
#include <array>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
namespace {
using namespace patchy::test::ui;
void ui_color_picker_changes_foreground_color() {
  ensure_artifact_dir();
  auto&& [window, canvas] = ShownWindowFixture{};
  auto* foreground = window.findChild<QPushButton*>(QStringLiteral("foregroundColorButton"));
  CHECK(foreground != nullptr);
  foreground->click();
  QApplication::processEvents();
  QDialog* dialog = nullptr;
  for (auto* widget : QApplication::topLevelWidgets()) {
    if (widget->objectName() == QStringLiteral("patchyColorDialog")) {
      dialog = qobject_cast<QDialog*>(widget);
      break;
    }
  }
  CHECK(dialog != nullptr);
  CHECK(dialog->isVisible());
  CHECK(!dialog->isModal());
  CHECK(dialog->windowModality() == Qt::NonModal);
  CHECK(dialog->windowFlags().testFlag(Qt::FramelessWindowHint));
  CHECK(dialog->findChild<QWidget*>(QStringLiteral("dialogChromeTitleBar")) != nullptr);
  CHECK(dialog->findChild<QToolButton*>(QStringLiteral("dialogChromeCloseButton")) != nullptr);
  auto* picker = dialog->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
  CHECK(picker != nullptr);
  auto* color_plane = picker->findChild<QWidget*>(QStringLiteral("patchyColorPlane"));
  CHECK(color_plane != nullptr);
  const auto color_plane_image = color_plane->grab().toImage();
  CHECK(color_close(color_plane_image.pixelColor(1, 1), QColor(255, 255, 255), 4));
  CHECK(color_close(color_plane_image.pixelColor(color_plane_image.width() - 2, 1), QColor(255, 0, 0), 4));
  CHECK(color_close(color_plane_image.pixelColor(color_plane_image.width() / 2, color_plane_image.height() - 2),
                    QColor(0, 0, 0), 4));
  send_mouse(*color_plane, QEvent::MouseButtonPress, QPoint(0, 0), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*color_plane, QEvent::MouseButtonRelease, QPoint(0, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->primary_color() == QColor(255, 255, 255));
  auto custom_swatches = picker->findChildren<QPushButton*>(QStringLiteral("patchyCustomColorSwatch"));
  auto* set_custom = picker->findChild<QPushButton*>(QStringLiteral("patchySetCustomColorButton"));
  CHECK(custom_swatches.size() == 16);
  CHECK(set_custom != nullptr);
  // The single Set button replaced the old Add/Update/Delete trio.
  CHECK(picker->findChild<QPushButton*>(QStringLiteral("patchyAddCustomColorButton")) == nullptr);
  CHECK(picker->findChild<QPushButton*>(QStringLiteral("patchyUpdateCustomColorButton")) == nullptr);
  CHECK(picker->findChild<QPushButton*>(QStringLiteral("patchyDeleteCustomColorButton")) == nullptr);
  // Always usable (GitHub issue 140): with no box selected it adds the current
  // color (white here) at the front and the others shift right.
  CHECK(set_custom->isEnabled());
  const auto previous_first = custom_swatches[0]->toolTip();
  set_custom->click();
  QApplication::processEvents();
  CHECK(custom_swatches[0]->toolTip() == QStringLiteral("#FFFFFF"));
  CHECK(custom_swatches[1]->toolTip() == previous_first);
  // Selecting a swatch must not resize it or shift the rows (a state-dependent
  // QSS border once grew the content box by 2px and made the grid jump).
  const auto unselected_geometry = custom_swatches[1]->geometry();
  custom_swatches[1]->click();
  QApplication::processEvents();
  CHECK(custom_swatches[1]->geometry() == unselected_geometry);
  custom_swatches.front()->click();
  QApplication::processEvents();
  CHECK(custom_swatches[1]->geometry() == unselected_geometry);
  CHECK(set_custom->isEnabled());
  picker->setCurrentColor(QColor(10, 20, 30));
  set_custom->click();
  QApplication::processEvents();
  picker->setCurrentColor(QColor(200, 210, 220));
  custom_swatches.front()->click();
  QApplication::processEvents();
  CHECK(canvas->primary_color() == QColor(10, 20, 30));
  picker->setCurrentColor(QColor(30, 40, 50));
  set_custom->click();
  picker->setCurrentColor(QColor(90, 80, 70));
  custom_swatches.front()->click();
  QApplication::processEvents();
  CHECK(canvas->primary_color() == QColor(30, 40, 50));
  picker->setCurrentColor(QColor(80, 120, 200));
  QApplication::processEvents();
  CHECK(canvas->primary_color() == QColor(80, 120, 200));
  dialog->grab().save(QStringLiteral("test-artifacts/ui_color_picker_gradient.png"));
  picker->setCurrentColor(QColor(12, 180, 240));
  QApplication::processEvents();
  dialog->grab().save(QStringLiteral("test-artifacts/ui_color_picker.png"));
  CHECK(canvas->primary_color() == QColor(12, 180, 240));
  CHECK(std::filesystem::exists(std::filesystem::path("test-artifacts") / "ui_color_picker.png"));
  save_widget_artifact("ui_color_picker_result", window);
  dialog->close();
  QApplication::processEvents();
}
void ui_color_picker_ignores_reentrant_requests() {
  ensure_artifact_dir();
  const auto count_visible_pickers = [] {
    int count = 0;
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() == QStringLiteral("patchyColorDialog") && widget->isVisible()) {
        ++count;
      }
    }
    return count;
  };
  bool nested_request_returned = false;
  std::optional<QColor> nested_result;
  int pickers_while_first_open = 0;
  QTimer::singleShot(0, [&] {
    QDialog* first = nullptr;
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() == QStringLiteral("patchyColorDialog") && widget->isVisible()) {
        first = qobject_cast<QDialog*>(widget);
        break;
      }
    }
    CHECK(first != nullptr);
    // Re-enter while the first picker is still open; the guard must reject this
    // immediately instead of stacking a second identical picker on top.
    nested_result = patchy::ui::request_patchy_color(nullptr, QColor(1, 2, 3), QStringLiteral("Nested"));
    nested_request_returned = true;
    pickers_while_first_open = count_visible_pickers();
    if (first != nullptr) {
      first->accept();
    }
  });
  const auto result = patchy::ui::request_patchy_color(nullptr, QColor(10, 20, 30), QStringLiteral("Re-entrancy"));
  CHECK(nested_request_returned);
  CHECK(!nested_result.has_value());
  CHECK(pickers_while_first_open == 1);
  CHECK(result.has_value());
  QApplication::processEvents();
  CHECK(count_visible_pickers() == 0);
}
void ui_color_picker_closes_with_parent_dialog() {
  // Pickers launched from a dialog (layer style swatches, gradient stops) are
  // non-modal, so the user can close the parent dialog while the picker is still
  // open. The orphaned picker used to survive its parent, drop behind the main
  // window on the next click, and its stuck request guard then silently swallowed
  // every later color-pick request in the app.
  QDialog parent;
  parent.setObjectName(QStringLiteral("testPickerParentDialog"));
  parent.show();
  QApplication::processEvents();
  bool closed_parent_while_picker_open = false;
  QTimer::singleShot(0, &parent, [&] {
    closed_parent_while_picker_open = true;
    parent.reject();
  });
  const auto orphaned =
      patchy::ui::request_patchy_color(&parent, QColor(10, 20, 30), QStringLiteral("Orphaned"));
  CHECK(closed_parent_while_picker_open);
  CHECK(!orphaned.has_value());
  for (auto* widget : QApplication::topLevelWidgets()) {
    CHECK(!(widget->objectName() == QStringLiteral("patchyColorDialog") && widget->isVisible()));
  }
  // The single-picker guard must have been released with it: a later request
  // opens a fresh picker instead of silently doing nothing.
  bool second_picker_opened = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() == QStringLiteral("patchyColorDialog") && widget->isVisible()) {
        second_picker_opened = true;
        qobject_cast<QDialog*>(widget)->accept();
        return;
      }
    }
    CHECK(false);
  });
  const auto second = patchy::ui::request_patchy_color(nullptr, QColor(3, 2, 1), QStringLiteral("Fresh"));
  CHECK(second_picker_opened);
  CHECK(second.has_value());
}
// GitHub issue 216: OK on the dialog that launched a picker confirms what the
// picker's live preview showed, so the picked color stands instead of reverting.
void ui_color_picker_keeps_its_color_when_parent_is_accepted() {
  QDialog parent;
  parent.setObjectName(QStringLiteral("testPickerAcceptedParentDialog"));
  parent.show();
  QApplication::processEvents();
  std::optional<QColor> live;
  QTimer::singleShot(0, &parent, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyColorDialog") || !widget->isVisible()) {
        continue;
      }
      auto* picker = widget->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
      CHECK(picker != nullptr);
      if (picker != nullptr) {
        picker->setCurrentColor(QColor(40, 160, 210));
      }
      break;
    }
    parent.accept();
  });
  const auto kept = patchy::ui::request_patchy_color(&parent, QColor(10, 20, 30), QStringLiteral("Kept"),
                                                     [&](QColor color) { live = color; });
  CHECK(live.has_value() && *live == QColor(40, 160, 210));
  CHECK(kept.has_value() && *kept == QColor(40, 160, 210));
  for (auto* widget : QApplication::topLevelWidgets()) {
    CHECK(!(widget->objectName() == QStringLiteral("patchyColorDialog") && widget->isVisible()));
  }
}
// A picker opened from a dialog styled with dialog_spinbox_button_style() (the G'MIC
// Filter Library's color swatches) keeps room for its digits: that style reserves 54 px
// on the right for -/+ buttons, and the picker's 44 px fields have none.
void ui_color_picker_fields_keep_their_digits_in_a_styled_dialog() {
  QDialog parent;
  parent.setObjectName(QStringLiteral("testStyledPickerParent"));
  patchy::ui::append_themed_style(parent, patchy::ui::dialog_spinbox_button_style());
  parent.show();
  QApplication::processEvents();
  bool checked = false;
  QTimer::singleShot(0, &parent, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyColorDialog") || !widget->isVisible()) {
        continue;
      }
      QApplication::processEvents();
      for (const char* name : {"patchyColorHueSpin", "patchyColorSaturationSpin", "patchyColorValueSpin",
                               "patchyColorRedSpin", "patchyColorGreenSpin", "patchyColorBlueSpin"}) {
        auto* spin = widget->findChild<QSpinBox*>(QString::fromLatin1(name));
        CHECK(spin != nullptr);
        auto* edit = spin != nullptr ? spin->findChild<QLineEdit*>() : nullptr;
        CHECK(edit != nullptr);
        if (edit != nullptr) {
          CHECK(edit->width() >= edit->fontMetrics().horizontalAdvance(QStringLiteral("359")));
        }
      }
      checked = true;
      qobject_cast<QDialog*>(widget)->reject();
      return;
    }
  });
  (void)patchy::ui::request_patchy_color(&parent, QColor(0x5a, 0x2d, 0x25), QStringLiteral("Styled parent"));
  CHECK(checked);
}
void ui_color_picker_wheel_and_sliders_modes() {
  ensure_artifact_dir();
  auto&& [window, canvas] = ShownWindowFixture{};
  auto* foreground = window.findChild<QPushButton*>(QStringLiteral("foregroundColorButton"));
  CHECK(foreground != nullptr);
  // Clear any persisted tab so the picker opens on Square (the unset default).
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("colorPanel/lastTab"));
    settings.sync();
  }
  foreground->click();
  QApplication::processEvents();
  QDialog* dialog = nullptr;
  for (auto* widget : QApplication::topLevelWidgets()) {
    if (widget->objectName() == QStringLiteral("patchyColorDialog") && widget->isVisible()) {
      dialog = qobject_cast<QDialog*>(widget);
      break;
    }
  }
  CHECK(dialog != nullptr);
  auto* picker = dialog->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
  CHECK(picker != nullptr);
  auto* tabs = picker->findChild<QTabWidget*>(QStringLiteral("patchyColorPickerTabs"));
  CHECK(tabs != nullptr);
  CHECK(tabs->count() == 3);
  CHECK(tabs->currentIndex() == 0);  // Square mode is the unset default.
  // Start from a fully saturated colour so hue edits on the wheel are visible.
  picker->setCurrentColor(QColor(0, 128, 255));
  QApplication::processEvents();
  // --- Wheel mode: hue ring + inner saturation/value square. ---
  tabs->setCurrentIndex(1);
  QApplication::processEvents();
  auto* wheel = picker->findChild<QWidget*>(QStringLiteral("patchyColorWheel"));
  CHECK(wheel != nullptr);
  CHECK(wheel->width() >= 150);
  CHECK(wheel->height() >= 150);
  // Mirror the widget's own geometry so the clicks land on the ring / inner square
  // regardless of how large the wheel expanded to.
  const int cx = wheel->width() / 2;
  const int cy = wheel->height() / 2;
  const double side = std::min(wheel->width(), wheel->height());
  const double outer = side / 2.0 - 2.0;
  const double inner = outer - 20.0;  // kColorWheelRing
  const int ring_radius = static_cast<int>(std::lround((outer + inner) / 2.0));
  // The ring is a static hue wheel; its 3 o'clock band is red (hue 0).
  const auto wheel_image = wheel->grab().toImage();
  CHECK(color_close(wheel_image.pixelColor(cx + ring_radius, cy), QColor(255, 0, 0), 80));
  // Click the top of the ring (12 o'clock) -> hue ~90.
  auto* hue_spin = picker->findChild<QSpinBox*>(QStringLiteral("patchyColorHueSpin"));
  CHECK(hue_spin != nullptr);
  send_mouse(*wheel, QEvent::MouseButtonPress, QPoint(cx, cy - ring_radius), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*wheel, QEvent::MouseButtonRelease, QPoint(cx, cy - ring_radius), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(std::abs(hue_spin->value() - 90) <= 6);
  dialog->grab().save(QStringLiteral("test-artifacts/ui_color_picker_wheel.png"));
  // Click the centre of the inner square -> mid saturation/value (well inside the
  // ring), confirming the square drives sat/val while the hue stays put.
  auto* sat_spin = picker->findChild<QSpinBox*>(QStringLiteral("patchyColorSaturationSpin"));
  auto* val_spin = picker->findChild<QSpinBox*>(QStringLiteral("patchyColorValueSpin"));
  CHECK(sat_spin != nullptr);
  CHECK(val_spin != nullptr);
  send_mouse(*wheel, QEvent::MouseButtonPress, QPoint(cx, cy), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*wheel, QEvent::MouseButtonRelease, QPoint(cx, cy), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  // The fields are percentages (GitHub issue 149): mid square reads about 50%.
  CHECK(sat_spin->maximum() == 100 && val_spin->maximum() == 100);
  CHECK(std::abs(sat_spin->value() - 50) <= 10);
  CHECK(std::abs(val_spin->value() - 50) <= 10);
  // Typing a percentage drives the model: 100% saturation at full value is the pure hue.
  sat_spin->setValue(100);
  val_spin->setValue(100);
  QApplication::processEvents();
  CHECK(picker->currentColor().saturation() == 255 && picker->currentColor().value() == 255);
  CHECK(std::abs(hue_spin->value() - 90) <= 6);  // hue unchanged by the square
  // --- Sliders mode: one gradient track per channel. ---
  picker->setCurrentColor(QColor(40, 60, 80));
  tabs->setCurrentIndex(2);
  QApplication::processEvents();
  // Switching tabs is remembered for next time.
  {
    auto settings = patchy::ui::app_settings();
    CHECK(settings.value(QStringLiteral("colorPanel/lastTab"), -1).toInt() == 2);
  }
  auto* red_slider = picker->findChild<QWidget*>(QStringLiteral("patchyChannelSliderRed"));
  CHECK(red_slider != nullptr);
  CHECK(red_slider->width() > 40);
  const int slider_y = red_slider->height() / 2;
  // Drag the red track to the far right -> red 255, other channels untouched.
  send_mouse(*red_slider, QEvent::MouseButtonPress, QPoint(red_slider->width() - 1, slider_y), Qt::LeftButton,
             Qt::LeftButton);
  send_mouse(*red_slider, QEvent::MouseButtonRelease, QPoint(red_slider->width() - 1, slider_y), Qt::LeftButton,
             Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->primary_color() == QColor(255, 60, 80));
  dialog->grab().save(QStringLiteral("test-artifacts/ui_color_picker_sliders.png"));
  // Far left -> red 0.
  send_mouse(*red_slider, QEvent::MouseButtonPress, QPoint(0, slider_y), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*red_slider, QEvent::MouseButtonRelease, QPoint(0, slider_y), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->primary_color().red() == 0);
  dialog->close();
  QApplication::processEvents();
}
void ui_dialog_position_memory_restores_last_position() {
  const auto settings_group = QStringLiteral("dialogPositions/patchyDialogPositionMemoryTest");
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(settings_group);
    settings.sync();
  }
  const auto screen_rect = QApplication::primaryScreen() != nullptr
                               ? QApplication::primaryScreen()->availableGeometry()
                               : QRect(0, 0, 640, 480);
  const auto target_position = screen_rect.topLeft() + QPoint(37, 43);
  QPoint saved_position;
  {
    QDialog dialog;
    dialog.setObjectName(QStringLiteral("patchyDialogPositionMemoryTest"));
    dialog.resize(140, 90);
    patchy::ui::remember_dialog_position(dialog);
    dialog.show();
    QApplication::processEvents();
    dialog.move(target_position);
    QApplication::processEvents();
    saved_position = dialog.pos();
    dialog.close();
    QApplication::processEvents();
    // Reusing the same dialog must retain the position saved by its first
    // opening, even when the second opening needs no user move.
    dialog.show();
    QApplication::processEvents();
    CHECK((dialog.pos() - saved_position).manhattanLength() <= 2);
    dialog.close();
    QApplication::processEvents();
  }
  {
    QDialog dialog;
    dialog.setObjectName(QStringLiteral("patchyDialogPositionMemoryTest"));
    dialog.resize(140, 90);
    patchy::ui::remember_dialog_position(dialog);
    dialog.show();
    QApplication::processEvents();
    CHECK((dialog.pos() - saved_position).manhattanLength() <= 2);
    dialog.close();
    QApplication::processEvents();
  }
  auto settings = patchy::ui::app_settings();
  settings.remove(settings_group);
  settings.sync();
}
void ui_dialog_position_memory_centers_unmoved_dialogs_on_parent() {
  const auto settings_group = QStringLiteral("dialogPositions/patchyDialogCenterTest");
  const auto screen_rect = QApplication::primaryScreen() != nullptr
                               ? QApplication::primaryScreen()->availableGeometry()
                               : QRect(0, 0, 640, 480);
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(settings_group);
    settings.setValue(settings_group + QStringLiteral("/pos"), screen_rect.topLeft() + QPoint(4, 5));
    settings.sync();
  }
  QWidget parent;
  parent.resize(420, 260);
  parent.move(screen_rect.topLeft() + QPoint(80, 70));
  parent.show();
  QApplication::processEvents();
  QDialog dialog(&parent);
  dialog.setObjectName(QStringLiteral("patchyDialogCenterTest"));
  dialog.resize(140, 90);
  patchy::ui::remember_dialog_position(dialog);
  dialog.show();
  QApplication::processEvents();
  const auto expected_position =
      parent.frameGeometry().center() - QPoint(dialog.size().width() / 2, dialog.size().height() / 2);
  CHECK((dialog.pos() - expected_position).manhattanLength() <= 10);
  dialog.close();
  QApplication::processEvents();
  auto settings = patchy::ui::app_settings();
  CHECK(!settings.value(settings_group + QStringLiteral("/pos")).isValid());
  settings.remove(settings_group);
  settings.sync();
}
// Progress dialogs are transient status windows: a remembered position (here a
// seeded one from an earlier layout) is ignored, the dialog is centered on its
// owner, and moving it records nothing.
void ui_progress_dialogs_ignore_position_memory_and_center_on_parent() {
  const auto settings_group = QStringLiteral("dialogPositions/patchyProgressPositionTest");
  const auto screen_rect = QApplication::primaryScreen() != nullptr
                               ? QApplication::primaryScreen()->availableGeometry()
                               : QRect(0, 0, 640, 480);
  const auto far_position = screen_rect.topLeft() + QPoint(4, 5);
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(settings_group);
    settings.setValue(settings_group + QStringLiteral("/pos"), far_position);
    settings.setValue(settings_group + QStringLiteral("/moved"), true);
    settings.sync();
  }
  QWidget parent;
  parent.resize(420, 260);
  parent.move(screen_rect.topLeft() + QPoint(160, 120));
  parent.show();
  QApplication::processEvents();
  {
    QProgressDialog dialog(QStringLiteral("Opening..."), QString(), 0, 0, &parent);
    dialog.setObjectName(QStringLiteral("patchyProgressPositionTest"));
    dialog.setMinimumDuration(0);
    dialog.resize(220, 90);
    patchy::ui::remember_dialog_position(dialog);
    dialog.show();
    QApplication::processEvents();
    const auto expected_position =
        parent.frameGeometry().center() - QPoint(dialog.size().width() / 2, dialog.size().height() / 2);
    CHECK((dialog.pos() - expected_position).manhattanLength() <= 10);
    CHECK((dialog.pos() - far_position).manhattanLength() > 10);
    dialog.move(far_position);
    QApplication::processEvents();
    dialog.close();
    QApplication::processEvents();
  }
  auto settings = patchy::ui::app_settings();
  CHECK(!settings.value(settings_group + QStringLiteral("/pos")).isValid());
  CHECK(!settings.value(settings_group + QStringLiteral("/moved"), false).toBool());
  settings.remove(settings_group);
  settings.sync();
}
// Message boxes (the save prompt, every question) and dialogs marked with
// mark_dialog_always_centered (About) ignore a remembered position: they center
// on their owner every time and drop any saved spot (Seth, October 2026).
void ui_message_boxes_and_marked_dialogs_ignore_position_memory() {
  const auto screen_rect = QApplication::primaryScreen() != nullptr
                               ? QApplication::primaryScreen()->availableGeometry()
                               : QRect(0, 0, 640, 480);
  const auto far_position = screen_rect.topLeft() + QPoint(4, 5);
  QWidget parent;
  parent.resize(420, 260);
  parent.move(screen_rect.topLeft() + QPoint(160, 120));
  parent.show();
  QApplication::processEvents();
  const auto seed = [far_position](const QString& group) {
    auto settings = patchy::ui::app_settings();
    settings.remove(group);
    settings.setValue(group + QStringLiteral("/pos"), far_position);
    settings.setValue(group + QStringLiteral("/moved"), true);
    settings.sync();
  };
  const auto expect_centered_and_forgotten = [&](QDialog& dialog, const QString& group) {
    patchy::ui::remember_dialog_position(dialog);
    dialog.show();
    QApplication::processEvents();
    const auto expected =
        parent.frameGeometry().center() - QPoint(dialog.size().width() / 2, dialog.size().height() / 2);
    CHECK((dialog.pos() - expected).manhattanLength() <= 10);
    CHECK((dialog.pos() - far_position).manhattanLength() > 10);
    dialog.move(far_position);
    QApplication::processEvents();
    dialog.close();
    QApplication::processEvents();
    auto settings = patchy::ui::app_settings();
    CHECK(!settings.value(group + QStringLiteral("/pos")).isValid());
    CHECK(!settings.value(group + QStringLiteral("/moved"), false).toBool());
    settings.remove(group);
    settings.sync();
  };
  {
    const auto group = QStringLiteral("dialogPositions/patchyMessageBoxPositionTest");
    seed(group);
    QMessageBox box(QMessageBox::Warning, QStringLiteral("Save changes?"), QStringLiteral("Save?"),
                    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, &parent);
    box.setObjectName(QStringLiteral("patchyMessageBoxPositionTest"));
    expect_centered_and_forgotten(box, group);
  }
  {
    const auto group = QStringLiteral("dialogPositions/patchyMarkedPositionTest");
    seed(group);
    QDialog dialog(&parent);
    dialog.setObjectName(QStringLiteral("patchyMarkedPositionTest"));
    dialog.resize(240, 120);
    patchy::ui::mark_dialog_always_centered(dialog);
    expect_centered_and_forgotten(dialog, group);
  }
  // An unmarked dialog still honors its remembered position.
  {
    const auto group = QStringLiteral("dialogPositions/patchyPlainPositionTest");
    seed(group);
    QDialog dialog(&parent);
    dialog.setObjectName(QStringLiteral("patchyPlainPositionTest"));
    dialog.resize(240, 120);
    patchy::ui::remember_dialog_position(dialog);
    dialog.show();
    QApplication::processEvents();
    CHECK((dialog.pos() - far_position).manhattanLength() <= 10);
    dialog.close();
    QApplication::processEvents();
    auto settings = patchy::ui::app_settings();
    settings.remove(group);
    settings.sync();
  }
}
void ui_message_boxes_stay_inside_screen_with_offscreen_parent() {
  const auto* screen = QApplication::primaryScreen();
  CHECK(screen != nullptr);
  const auto available = screen->availableGeometry();
  QWidget parent;
  parent.resize(420, 260);
  parent.show();
  QApplication::processEvents();
  // Every edge, a corner, and an owner wholly outside the screen. The final
  // message-box size and native title bar must fit, not just its size hint.
  const std::vector<QPoint> positions{
      QPoint(available.left() - parent.width() + 20, available.center().y()),
      QPoint(available.right() - 20, available.center().y()),
      QPoint(available.center().x(), available.top() - parent.height() + 20),
      QPoint(available.center().x(), available.bottom() - 20),
      available.bottomRight() - QPoint(20, 20),
      available.bottomRight() + QPoint(100, 100)};
  for (const auto position : positions) {
    parent.move(position);
    QApplication::processEvents();
    for (const auto icon : {QMessageBox::Warning, QMessageBox::Information, QMessageBox::Critical}) {
      QMessageBox box(icon, QStringLiteral("Save changes?"),
                      QStringLiteral("Save changes to Untitled-1 before closing?"),
                      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, &parent);
      patchy::ui::remember_dialog_position(box);
      box.show();
      QApplication::processEvents();
      const auto frame = box.frameGeometry();
      const auto dialog_available = box.screen()->availableGeometry();
      box.reject();
      CHECK(dialog_available.contains(frame));
    }
  }
}
void ui_dialog_position_memory_clamps_native_frame_without_remembering_placement() {
  const auto* screen = QApplication::primaryScreen();
  CHECK(screen != nullptr);
  const auto available = screen->availableGeometry();
  const auto group = QStringLiteral("dialogPositions/patchyDialogFrameClampTest");
  auto settings = patchy::ui::app_settings();
  settings.remove(group);
  QWidget parent;
  parent.resize(420, 260);
  parent.move(available.bottomRight() - QPoint(20, 20));
  parent.show();
  QApplication::processEvents();
  for (const bool remembered : {false, true}) {
    if (remembered) {
      settings.setValue(group + QStringLiteral("/pos"), available.bottomRight() - QPoint(20, 20));
      settings.setValue(group + QStringLiteral("/moved"), true);
      settings.sync();
    }
    QDialog dialog(&parent);
    dialog.setObjectName(QStringLiteral("patchyDialogFrameClampTest"));
    dialog.resize(240, 120);
    patchy::ui::remember_dialog_position(dialog);
    dialog.show();
    QApplication::processEvents();
    const auto frame = dialog.frameGeometry();
    const auto dialog_available = dialog.screen()->availableGeometry();
    dialog.reject();
    settings.sync();
    CHECK(dialog_available.contains(frame));
    CHECK(settings.value(group + QStringLiteral("/moved"), false).toBool() == remembered);
  }
  settings.remove(group);
}
void ui_message_box_expanded_details_stay_inside_screen() {
  QMessageBox box(QMessageBox::Warning, QStringLiteral("Warning"), QStringLiteral("Details are available."),
                  QMessageBox::Ok);
  box.setDetailedText(QStringLiteral("Additional information about this warning."));
  patchy::ui::remember_dialog_position(box);
  box.show();
  QApplication::processEvents();
  const auto available = box.screen()->availableGeometry();
  box.move(QPoint(available.left() + 10, available.bottom() - box.frameGeometry().height() + 1));
  QApplication::processEvents();
  const int collapsed_height = box.frameGeometry().height();
  bool clicked_details = false;
  for (auto* button : box.buttons()) {
    if (box.buttonRole(button) == QMessageBox::ActionRole) {
      button->click();
      clicked_details = true;
      break;
    }
  }
  QApplication::processEvents();
  const auto expanded_frame = box.frameGeometry();
  box.reject();
  CHECK(clicked_details);
  CHECK(expanded_frame.height() > collapsed_height);
  CHECK(available.contains(expanded_frame));
}
void ui_dirty_state_marks_tabs_and_undo_restores_saved_revision() {
  auto&& [window, canvas] = ShownWindowFixture{};
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);
  CHECK(!tabs->tabText(tabs->currentIndex()).endsWith(QStringLiteral("*")));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(tabs->tabText(tabs->currentIndex()).endsWith(QStringLiteral("*")));
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(!tabs->tabText(tabs->currentIndex()).endsWith(QStringLiteral("*")));
}
void ui_compatibility_report_flags_psd_text_placeholders() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgba8());
  auto& layer = document.add_pixel_layer("PSD Text", solid_pixels(120, 90, patchy::PixelFormat::rgba8(),
                                                                  QColor(0, 0, 0, 0)));
  layer.metadata()[patchy::kLayerMetadataText] = "Title";
  layer.metadata()[patchy::kLayerMetadataTextFont] = "PSD Text";
  layer.metadata()[patchy::kLayerMetadataTextSourceBlock] = "TySh";
  layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "placeholder";
  layer.unknown_psd_blocks().push_back(patchy::UnknownPsdBlock{"TySh", {1, 2, 3}});
  auto warnings = patchy::ui::compatibility_warnings_for_document(document);
  CHECK(!warnings.isEmpty());
  CHECK(warnings.join(QLatin1Char('\n')).contains(QStringLiteral("placeholder")));
  CHECK(warnings.join(QLatin1Char('\n')).contains(QStringLiteral("TySh")));
  CHECK(warnings.join(QLatin1Char('\n')).contains(QStringLiteral("unknown PSD layer block")));
}
void ui_compatibility_report_ignores_patchy_written_psd_blocks() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(120, 90, patchy::PixelFormat::rgb8(), QColor(Qt::white)));
  auto& text_layer = document.add_pixel_layer(
      "Text: Hey man", solid_pixels(120, 90, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.distance = 4.0F;
  shadow.size = 6.0F;
  text_layer.layer_style().drop_shadows.push_back(shadow);
  const auto round_tripped = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  const auto warnings = patchy::ui::compatibility_warnings_for_document(round_tripped);
  CHECK(warnings.isEmpty());
}
void ui_compatibility_report_treats_levels_as_native_psd_adjustment() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(120, 90, patchy::PixelFormat::rgb8(), QColor(Qt::white)));
  patchy::AdjustmentSettings settings;
  settings.kind = patchy::AdjustmentKind::Levels;
  settings.levels.red.black_output = 128;
  patchy::Layer levels(document.allocate_layer_id(), "Levels", patchy::LayerKind::Adjustment);
  levels.set_bounds(patchy::Rect::from_size(document.width(), document.height()));
  patchy::configure_adjustment_layer(levels, settings);
  document.add_layer(std::move(levels));
  const auto warnings = patchy::ui::compatibility_warnings_for_document(document);
  CHECK(warnings.isEmpty());
}
// Native Photoshop adjustment blocks read back from a PSD (Curves, Invert, Color Balance,
// Vibrance here: the reader keeps their raw blocks for the round trip) are understood
// data, not "unknown PSD layer block(s)"; a key nothing models still is (the healing
// test file's Curves layer drew the false warning).
void ui_compatibility_report_treats_native_adjustment_blocks_as_known() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(120, 90, patchy::PixelFormat::rgb8(), QColor(90, 140, 200)));
  const auto add_adjustment = [&document](const char* name, const patchy::AdjustmentSettings& settings) {
    patchy::Layer layer(document.allocate_layer_id(), name, patchy::LayerKind::Adjustment);
    layer.set_bounds(patchy::Rect::from_size(document.width(), document.height()));
    patchy::configure_adjustment_layer(layer, settings);
    document.add_layer(std::move(layer));
  };
  patchy::AdjustmentSettings curves;
  curves.kind = patchy::AdjustmentKind::Curves;
  curves.curves.rgb = {{0, 0}, {64, 40}, {192, 215}, {255, 255}};
  add_adjustment("Curves", curves);
  patchy::AdjustmentSettings invert;
  invert.kind = patchy::AdjustmentKind::Invert;
  add_adjustment("Invert", invert);
  patchy::AdjustmentSettings balance;
  balance.kind = patchy::AdjustmentKind::ColorBalance;
  balance.color_balance = patchy::ColorBalanceAdjustment{20, -10, 15};
  add_adjustment("Color Balance", balance);
  patchy::AdjustmentSettings vibrance;
  vibrance.kind = patchy::AdjustmentKind::Vibrance;
  add_adjustment("Vibrance", vibrance);
  auto round_tripped = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  const auto has_block = [&round_tripped](std::string_view key) {
    for (const auto& layer : round_tripped.layers()) {
      for (const auto& block : layer.unknown_psd_blocks()) {
        if (block.key == key) {
          return true;
        }
      }
    }
    return false;
  };
  CHECK(has_block("curv") && has_block("nvrt") && has_block("blnc"));
  CHECK(patchy::psd::is_native_adjustment_block_key("curv"));
  CHECK(patchy::psd::is_native_adjustment_block_key("vibA"));
  CHECK(!patchy::psd::is_native_adjustment_block_key("zzzz"));
  auto warnings = patchy::ui::compatibility_warnings_for_document(round_tripped);
  if (!warnings.isEmpty()) {
    std::fprintf(stderr, "  %s\n", qPrintable(warnings.join(QLatin1Char('\n'))));
  }
  CHECK(warnings.isEmpty());
  round_tripped.layers().front().unknown_psd_blocks().push_back(patchy::UnknownPsdBlock{"zzzz", {1, 2, 3, 4}});
  warnings = patchy::ui::compatibility_warnings_for_document(round_tripped);
  CHECK(warnings.join(QLatin1Char('\n')).contains(QStringLiteral("unknown PSD layer block")));
}
// An adjustment layer read from a CMYK document runs on the inks; saving writes RGB, where
// the same numbers mean something else, and the report says so.
void ui_compatibility_report_warns_about_cmyk_ink_adjustments() {
  std::shared_ptr<patchy::InkSpace> space = std::make_shared<patchy::InkSpace>();
  space->id = "test-ink-space";
  space->rgb_grid = 2;
  space->ink_grid = 2;
  space->rgb_to_ink.assign(2U * 2U * 2U * 4U, std::uint16_t{32768});
  space->ink_to_rgb.assign(2U * 2U * 2U * 2U * 3U, std::uint16_t{32768});
  patchy::register_ink_space(space);
  const auto warnings_for = [&](patchy::AdjustmentKind kind, bool in_ink_space, bool native_color = false) {
    patchy::Document document(60, 40, patchy::PixelFormat::rgb8());
    if (native_color) {
      document.metadata().psd_native_color_space = std::make_shared<patchy::PsdNativeColorSpace>();
    }
    document.add_pixel_layer("Background", solid_pixels(60, 40, patchy::PixelFormat::rgb8(), QColor(Qt::white)));
    patchy::AdjustmentSettings settings;
    settings.kind = kind;
    settings.levels.gamma_percent = 150;
    if (in_ink_space) {
      settings.ink_space = space;
    }
    patchy::Layer adjustment(document.allocate_layer_id(), "Ink Levels", patchy::LayerKind::Adjustment);
    adjustment.set_bounds(patchy::Rect::from_size(document.width(), document.height()));
    patchy::configure_adjustment_layer(adjustment, settings);
    document.add_layer(std::move(adjustment));
    return patchy::ui::compatibility_warnings_for_document(document);
  };
  const auto ink = warnings_for(patchy::AdjustmentKind::Levels, true);
  CHECK(ink.size() == 1);
  CHECK(!ink.isEmpty() && ink.front().contains(QStringLiteral("Ink Levels")));
  CHECK(!ink.isEmpty() && ink.front().contains(QStringLiteral("CMYK")));
  // The same layer in an RGB document, and a kind that stays on RGB math, say nothing.
  CHECK(warnings_for(patchy::AdjustmentKind::Levels, false).isEmpty());
  CHECK(warnings_for(patchy::AdjustmentKind::HueSaturation, true).isEmpty());
  const auto native = warnings_for(patchy::AdjustmentKind::Levels, true, true);
  CHECK(native.size() == 1);
  CHECK(native.front().contains(QStringLiteral("original color mode")));
  CHECK(native.front().contains(QStringLiteral("Content edits or unsupported layers require RGB saving")));
  CHECK(!native.front().contains(QStringLiteral("Patchy saves RGB files")));
  // A grayscale document's space is the one-channel form, and the wording follows it.
  auto gray = std::make_shared<patchy::InkSpace>();
  gray->id = "test-gray-space";
  gray->gray_to_rgb.assign(768U, std::uint8_t{128});
  gray->rgb_to_gray.assign(256U, std::uint8_t{128});
  patchy::register_ink_space(gray);
  space = gray;
  const auto gray_warning = warnings_for(patchy::AdjustmentKind::Threshold, true);
  CHECK(gray_warning.size() == 1);
  CHECK(!gray_warning.isEmpty() && gray_warning.front().contains(QStringLiteral("grayscale")));
  CHECK(!gray_warning.isEmpty() && !gray_warning.front().contains(QStringLiteral("CMYK")));
}
void ui_compatibility_report_pins_native_vs_private_adjustment_kinds() {
  const auto adjustment_warnings = [](patchy::AdjustmentKind kind) {
    patchy::Document document(120, 90, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Background",
                             solid_pixels(120, 90, patchy::PixelFormat::rgb8(), QColor(Qt::white)));
    patchy::AdjustmentSettings settings;
    settings.kind = kind;
    patchy::Layer adjustment(document.allocate_layer_id(), "Adjustment", patchy::LayerKind::Adjustment);
    adjustment.set_bounds(patchy::Rect::from_size(document.width(), document.height()));
    patchy::configure_adjustment_layer(adjustment, settings);
    document.add_layer(std::move(adjustment));
    return patchy::ui::compatibility_warnings_for_document(document);
  };
  // Every modeled kind writes a native Photoshop block and stays warning-free
  // (Color Balance included since the July 2026 blnc migration).
  for (const auto kind :
       {patchy::AdjustmentKind::Levels, patchy::AdjustmentKind::Curves, patchy::AdjustmentKind::HueSaturation,
        patchy::AdjustmentKind::ColorBalance, patchy::AdjustmentKind::Invert, patchy::AdjustmentKind::Posterize,
        patchy::AdjustmentKind::Threshold, patchy::AdjustmentKind::BrightnessContrast}) {
    CHECK(adjustment_warnings(kind).isEmpty());
  }
  // An adjustment layer whose settings cannot be parsed still warns.
  patchy::Document document(120, 90, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(120, 90, patchy::PixelFormat::rgb8(), QColor(Qt::white)));
  patchy::Layer opaque(document.allocate_layer_id(), "Mystery", patchy::LayerKind::Adjustment);
  opaque.set_bounds(patchy::Rect::from_size(document.width(), document.height()));
  document.add_layer(std::move(opaque));
  const auto opaque_text = patchy::ui::compatibility_warnings_for_document(document).join(QLatin1Char('\n'));
  CHECK(opaque_text.contains(QStringLiteral("Patchy-native adjustment layer")));
}
void ui_compatibility_report_flags_cmyk_rgb_conversion() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgb8());
  document.metadata().values["psd.color_mode"] = "CMYK";
  document.add_pixel_layer("Background", solid_pixels(120, 90, patchy::PixelFormat::rgb8(), QColor(Qt::white)));
  const auto warnings = patchy::ui::compatibility_warnings_for_document(document);
  CHECK(!warnings.isEmpty());
  const auto text = warnings.join(QLatin1Char('\n'));
  CHECK(text.contains(QStringLiteral("CMYK")));
  CHECK(text.contains(QStringLiteral("converted")));
  CHECK(text.contains(QStringLiteral("RGB/RGBA")));
}
void ui_compatibility_report_flags_grayscale_rgb_conversion() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgb8());
  document.metadata().values["psd.color_mode"] = "Grayscale";
  document.add_pixel_layer("Background", solid_pixels(120, 90, patchy::PixelFormat::rgb8(), QColor(Qt::white)));
  const auto warnings = patchy::ui::compatibility_warnings_for_document(document);
  CHECK(!warnings.isEmpty());
  const auto text = warnings.join(QLatin1Char('\n'));
  CHECK(text.contains(QStringLiteral("Grayscale")));
  CHECK(text.contains(QStringLiteral("converted")));
  CHECK(text.contains(QStringLiteral("RGB/RGBA")));
}
void ui_compatibility_report_flags_unrendered_styles_on_groups() {
  // Group layer effects RENDER since July 2026, so a styled group must NOT
  // raise a compatibility warning anymore (the Satin-contour warning below
  // stays: that limitation is per-effect, not per-kind).
  patchy::Document document(120, 90, patchy::PixelFormat::rgba8());
  patchy::Layer group(document.allocate_layer_id(), "Styled Group", patchy::LayerKind::Group);
  patchy::LayerSatin satin;
  satin.enabled = true;
  group.layer_style().satins.push_back(satin);
  patchy::LayerPatternOverlay pattern;
  pattern.enabled = true;
  group.layer_style().pattern_overlays.push_back(pattern);
  group.add_child(patchy::Layer(document.allocate_layer_id(), "Child",
                               solid_pixels(20, 20, patchy::PixelFormat::rgba8(), QColor(Qt::white))));
  document.add_layer(std::move(group));
  const auto warnings = patchy::ui::compatibility_warnings_for_document(document);
  const auto text = warnings.join(QLatin1Char('\n'));
  CHECK(!text.contains(QStringLiteral("group with layer effects")));
  CHECK(!text.contains(QStringLiteral("does not render group layer effects")));
  // Pattern Overlay is a supported effect now; the report must not warn about it.
  CHECK(!text.contains(QStringLiteral("Pattern Overlay")));
  auto& imported_satin = document.layers().front().layer_style().satins.front();
  imported_satin.enabled = false;
  imported_satin.unsupported_contour_options = true;
  const auto contour_text =
      patchy::ui::compatibility_warnings_for_document(document).join(QLatin1Char('\n'));
  CHECK(contour_text.contains(QStringLiteral("Satin contour settings")));
  CHECK(contour_text.contains(QStringLiteral("custom curve or anti-aliasing")));
  CHECK(contour_text.contains(QStringLiteral("non-anti-aliased Linear contour")));
}
void ui_compatibility_report_handles_supported_unsupported_and_boundary_blend_if() {
  constexpr std::array<std::uint8_t, 8> identity_range{0, 0, 255, 255, 0, 0, 255, 255};
  std::vector<std::uint8_t> identity_payload;
  identity_payload.reserve(identity_range.size() * 5U);
  for (int channel = 0; channel < 5; ++channel) {
    for (const auto byte : identity_range) {
      identity_payload.push_back(byte);
    }
  }
  patchy::Document document(120, 90, patchy::PixelFormat::rgba8());
  patchy::Layer group(document.allocate_layer_id(), "Blend If Group", patchy::LayerKind::Group);
  group.raw_psd_blending_ranges() = identity_payload;
  group.raw_psd_group_boundary_blending_ranges() = identity_payload;
  patchy::Layer child(document.allocate_layer_id(), "Blend If Layer",
                      solid_pixels(20, 20, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  child.raw_psd_blending_ranges() = identity_payload;
  group.add_child(std::move(child));
  document.add_layer(std::move(group));
  CHECK(patchy::ui::compatibility_warnings_for_document(document).isEmpty());
  const auto supported_nondefault = patchy::encode_layer_blend_if(blend_if_ui_test_settings());
  auto& child_layer = document.layers().front().children().front();
  child_layer.set_blend_if_payload(supported_nondefault, true);
  CHECK(patchy::ui::compatibility_warnings_for_document(document).isEmpty());
  child_layer.set_blend_if_payload(std::vector<std::uint8_t>(identity_range.begin(), identity_range.end()), true);
  const auto unsupported_text =
      patchy::ui::compatibility_warnings_for_document(document).join(QLatin1Char('\n'));
  CHECK(unsupported_text.contains(QStringLiteral("Blend If Layer")));
  CHECK(unsupported_text.contains(QStringLiteral("unsupported color mode or payload shape")));
  CHECK(unsupported_text.contains(QStringLiteral("preserves it for PSD round-trip")));
  CHECK(unsupported_text.contains(QStringLiteral("does not render or edit")));
  child_layer.set_blend_if_payload(supported_nondefault, true);
  document.layers().front().raw_psd_group_boundary_blending_ranges() = supported_nondefault;
  const auto boundary_text =
      patchy::ui::compatibility_warnings_for_document(document).join(QLatin1Char('\n'));
  CHECK(boundary_text.contains(QStringLiteral("Blend If Group")));
  CHECK(boundary_text.contains(QStringLiteral("group-boundary record")));
  CHECK(boundary_text.contains(QStringLiteral("preserves that boundary data")));
  CHECK(boundary_text.contains(QStringLiteral("does not render or edit")));
}
void ui_psd_import_notice_reports_only_unsupported_blend_if() {
  SettingsValueRestorer restore_psd_warning_check(QStringLiteral("imports/showPsdWarningsAndInfo"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("imports/showPsdWarningsAndInfo"));
    settings.sync();
  }
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto write_blend_if_fixture = [&](const QString& name, const std::vector<std::uint8_t>& payload) {
    patchy::Document document(32, 24, patchy::PixelFormat::rgba8());
    patchy::Layer layer(document.allocate_layer_id(), name.toStdString(),
                        solid_pixels(16, 12, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
    layer.set_blend_if_payload(payload, true);
    document.add_layer(std::move(layer));
    const auto path = temp.filePath(name + QStringLiteral(".psd"));
    patchy::psd::DocumentIo::write_layered_rgb8_file(document, patchy::ui::to_filesystem_path(path));
    return path;
  };
  const auto supported_path =
      write_blend_if_fixture(QStringLiteral("supported-blend-if"),
                             patchy::encode_layer_blend_if(blend_if_ui_test_settings()));
  constexpr std::array<std::uint8_t, 8> unsupported_payload{0, 0, 255, 255, 0, 0, 255, 255};
  const auto unsupported_path = write_blend_if_fixture(
      QStringLiteral("unsupported-blend-if"),
      std::vector<std::uint8_t>(unsupported_payload.begin(), unsupported_payload.end()));
  auto&& [window, canvas] = ShownWindowFixture{};
  patchy::ui::MainWindowTestAccess::open_document_path(window, supported_path);
  QApplication::processEvents();
  CHECK(!window.statusBar()->currentMessage().contains(QStringLiteral("Blend If")));
  patchy::ui::MainWindowTestAccess::open_document_path(window, unsupported_path);
  QApplication::processEvents();
  const auto notice = window.statusBar()->currentMessage();
  CHECK(notice.contains(QStringLiteral("unsupported Photoshop Blend If payloads")));
  CHECK(notice.contains(QStringLiteral("does not render or edit")));
  CHECK(notice.contains(QStringLiteral("1 layer(s)")));
}
void ui_compatibility_report_describes_linked_smart_object_updates() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgba8());
  auto& layer = document.add_pixel_layer(
      "Linked Object", solid_pixels(120, 90, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  layer.metadata()[patchy::kLayerMetadataSmartObject] = "linked-source-uuid";
  layer.metadata()[patchy::kLayerMetadataSmartObjectLock] = "external";
  const auto text = patchy::ui::compatibility_warnings_for_document(document).join(QLatin1Char('\n'));
  CHECK(text.contains(QStringLiteral("linked to an external file")));
  CHECK(text.contains(QStringLiteral("can update it from disk when the source file is available")));
  CHECK(!text.contains(QStringLiteral("cannot update it from disk")));
}
void ui_psd_import_notice_reports_unrendered_layer_effects() {
  SettingsValueRestorer restore_psd_warning_check(QStringLiteral("imports/showPsdWarningsAndInfo"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("imports/showPsdWarningsAndInfo"));
    settings.sync();
  }
  patchy::Document document(32, 24, patchy::PixelFormat::rgba8());
  patchy::Layer root(document.allocate_layer_id(), "Root Satin",
                     solid_pixels(16, 12, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::LayerSatin root_satin;
  root_satin.enabled = true;
  root.layer_style().satins.push_back(root_satin);
  document.add_layer(std::move(root));
  patchy::Layer group(document.allocate_layer_id(), "Effects Group", patchy::LayerKind::Group);
  patchy::LayerSatin group_satin;
  group_satin.enabled = true;
  group.layer_style().satins.push_back(group_satin);
  patchy::Layer child(document.allocate_layer_id(), "Nested Effects",
                      solid_pixels(16, 12, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::LayerSatin nested_satin;
  nested_satin.enabled = true;
  child.layer_style().satins.push_back(nested_satin);
  patchy::LayerPatternOverlay pattern;
  pattern.enabled = true;
  pattern.pattern_name = "Test Pattern";
  pattern.pattern_id = "test-pattern";
  child.layer_style().pattern_overlays.push_back(pattern);
  pattern.enabled = false;
  child.layer_style().pattern_overlays.push_back(pattern);
  group.add_child(std::move(child));
  document.add_layer(std::move(group));
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto path = temp.filePath(QStringLiteral("unrendered-effects.psd"));
  patchy::psd::DocumentIo::write_layered_rgb8_file(document, patchy::ui::to_filesystem_path(path));
  auto&& [window, canvas] = ShownWindowFixture{};
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  // Group layer effects RENDER since July 2026: no unrendered-effects notice.
  const auto notice = window.statusBar()->currentMessage();
  CHECK(!notice.contains(QStringLiteral("does not render them yet")));
  CHECK(!notice.contains(QStringLiteral("Pattern Overlay")));
}
void ui_psd_import_warning_dialog_is_hidden_by_default() {
  SettingsValueRestorer restore_psd_warning_check(QStringLiteral("imports/showPsdWarningsAndInfo"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("imports/showPsdWarningsAndInfo"));
    settings.sync();
  }
  const auto psd_path = write_psd_import_warning_fixture(QStringLiteral("psd-import-warning-default.psd"));
  auto&& [window, canvas] = ShownWindowFixture{};
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);
  const auto original_tab_count = tabs->count();
  const auto compatibility_report_done = std::make_shared<bool>(false);
  accept_compatibility_report_when_present(compatibility_report_done);
  patchy::ui::MainWindowTestAccess::open_document_path(window, psd_path);
  const bool saw_compatibility_report = *compatibility_report_done;
  *compatibility_report_done = true;
  QApplication::processEvents();
  CHECK(!saw_compatibility_report);
  CHECK(tabs->count() == original_tab_count + 1);
  CHECK(tabs->tabText(tabs->currentIndex()) == QFileInfo(psd_path).fileName());
}
void ui_psd_import_warning_dialog_shows_when_enabled() {
  SettingsValueRestorer restore_psd_warning_check(QStringLiteral("imports/showPsdWarningsAndInfo"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("imports/showPsdWarningsAndInfo"), true);
    settings.sync();
  }
  const auto psd_path = write_psd_import_warning_fixture(QStringLiteral("psd-import-warning-enabled.psd"));
  auto&& [window, canvas] = ShownWindowFixture{};
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);
  const auto original_tab_count = tabs->count();
  const auto compatibility_report_done = std::make_shared<bool>(false);
  accept_compatibility_report_when_present(compatibility_report_done);
  patchy::ui::MainWindowTestAccess::open_document_path(window, psd_path);
  const bool saw_compatibility_report = *compatibility_report_done;
  *compatibility_report_done = true;
  QApplication::processEvents();
  CHECK(saw_compatibility_report);
  CHECK(tabs->count() == original_tab_count + 1);
  CHECK(tabs->tabText(tabs->currentIndex()) == QFileInfo(psd_path).fileName());
}
void ui_alt_left_click_samples_foreground_color() {
  auto&& [window, canvas] = ShownWindowFixture{};
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(12, 180, 240));
  use_solid_fill_settings(canvas);
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  const QPoint sample_document_point(80, 80);
  const auto sampled_before_click = canvas_pixel(*canvas, sample_document_point);
  CHECK(color_close(sampled_before_click, QColor(12, 180, 240), 4));
  canvas->set_primary_color(QColor(230, 20, 20));
  const auto sample_widget_point = canvas->widget_position_for_document_point(sample_document_point);
  send_mouse(*canvas, QEvent::MouseButtonPress, sample_widget_point, Qt::LeftButton, Qt::LeftButton,
             Qt::AltModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, sample_widget_point, Qt::LeftButton, Qt::NoButton,
             Qt::AltModifier);
  QApplication::processEvents();
  CHECK(color_close(canvas->primary_color(), QColor(12, 180, 240), 4));
  CHECK(color_close(canvas_pixel(*canvas, sample_document_point), sampled_before_click, 0));
}
void ui_alt_color_pick_shows_rgb_status_and_updates_open_color_panel() {
  auto&& [window, canvas] = ShownWindowFixture{};
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(40, 160, 220));
  use_solid_fill_settings(canvas);
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  canvas->set_primary_color(QColor(Qt::black));
  auto* foreground_button = window.findChild<QPushButton*>(QStringLiteral("foregroundColorButton"));
  CHECK(foreground_button != nullptr);
  foreground_button->click();
  QApplication::processEvents();
  auto* color_dialog = find_top_level_dialog(QStringLiteral("patchyColorDialog"));
  CHECK(color_dialog != nullptr);
  CHECK(color_dialog->isVisible());
  auto* picker =
      color_dialog->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
  CHECK(picker != nullptr);
  CHECK(picker->currentColor() == QColor(Qt::black));
  const QPoint sample_document_point(80, 80);
  const auto sample_widget_point = canvas->widget_position_for_document_point(sample_document_point);
  send_mouse(*canvas, QEvent::MouseButtonPress, sample_widget_point, Qt::LeftButton, Qt::LeftButton,
             Qt::AltModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, sample_widget_point, Qt::LeftButton, Qt::NoButton,
             Qt::AltModifier);
  QApplication::processEvents();
  const auto picked = canvas->primary_color();
  CHECK(color_close(picked, QColor(40, 160, 220), 4));
  const auto message = window.statusBar()->currentMessage();
  CHECK(message.contains(
      QStringLiteral("%1, %2, %3").arg(picked.red()).arg(picked.green()).arg(picked.blue())));
  CHECK(message.contains(picked.name(QColor::HexRgb).toUpper()));
  CHECK(!message.contains(QStringLiteral("Foreground color changed")));
  CHECK(picker->currentColor() == picked);
  color_dialog->close();
  QApplication::processEvents();
}
void ui_image_mode_converts_bit_depth_with_undo() {
  // Image > Mode > 8/16/32 Bits/Channel: shown while deep editing is on, converts every
  // buffer, keeps Indexed (8-bit only) out of reach, and undoes.
  patchy::set_deep_editing_override(true);
  {
    auto&& [window, canvas] = ShownWindowFixture{};
    auto* to8 = require_action(window, "imageMode8BitAction");
    auto* to16 = require_action(window, "imageMode16BitAction");
    auto* to32 = require_action(window, "imageMode32BitAction");
    CHECK(to8->isVisible() && to16->isVisible() && to32->isVisible());
    CHECK(to8->isChecked());
    to16->trigger();
    QApplication::processEvents();
    CHECK(patchy::ui::MainWindowTestAccess::document(window).color_state().bit_depth == patchy::BitDepth::UInt16);
    CHECK(patchy::document_depth_problems(patchy::ui::MainWindowTestAccess::document(window)).empty());
    CHECK(to16->isChecked() && !to8->isChecked());
    CHECK(!require_action(window, "imageModeIndexedAction")->isEnabled());
    to32->trigger();
    QApplication::processEvents();
    CHECK(patchy::ui::MainWindowTestAccess::document(window).color_state().bit_depth == patchy::BitDepth::Float32);
    CHECK(patchy::document_depth_problems(patchy::ui::MainWindowTestAccess::document(window)).empty());
    require_action_by_text(window, QStringLiteral("Undo"))->trigger();
    QApplication::processEvents();
    CHECK(patchy::ui::MainWindowTestAccess::document(window).color_state().bit_depth == patchy::BitDepth::UInt16);
    CHECK(to16->isChecked());
    require_action_by_text(window, QStringLiteral("Undo"))->trigger();
    QApplication::processEvents();
    CHECK(patchy::ui::MainWindowTestAccess::document(window).color_state().bit_depth == patchy::BitDepth::UInt8);
    CHECK(to8->isChecked());
    CHECK(require_action(window, "imageModeIndexedAction")->isEnabled());
  }
  patchy::set_deep_editing_override(false);
  {
    auto&& [window, canvas] = ShownWindowFixture{};
    CHECK(!require_action(window, "imageMode16BitAction")->isVisible());
  }
  patchy::set_deep_editing_override(std::nullopt);
}
// File > New offers Bit Depth while deep editing is on (the default in the app): the
// chosen depth makes the document, the Last Used card the next dialog opens on brings
// that depth back, and the row is hidden when deep editing is off.
void ui_new_document_dialog_creates_the_chosen_bit_depth() {
  const auto answer_new_document = [](int bits, bool expect_row, int* remembered) {
    QTimer::singleShot(0, [bits, expect_row, remembered] {
      auto* dialog = find_top_level_dialog(QStringLiteral("patchyNewDocumentDialog"));
      CHECK(dialog != nullptr);
      if (dialog == nullptr) {
        return;
      }
      auto* combo = dialog->findChild<QComboBox*>(QStringLiteral("newDocumentBitDepthCombo"));
      CHECK(combo != nullptr);
      if (combo == nullptr) {
        dialog->reject();
        return;
      }
      CHECK(combo->isVisible() == expect_row);
      if (remembered != nullptr) {
        *remembered = combo->currentData().toInt();
      }
      if (expect_row) {
        CHECK(combo->findData(16) >= 0 && combo->findData(32) >= 0);
        combo->setCurrentIndex(combo->findData(bits));
        dialog->grab().save(QStringLiteral("test-artifacts/ui_new_document_bit_depth.png"));
        // The memory estimate follows the depth: 64 x 48 RGBA is 12K at 8 bits.
        auto* width = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("newDocumentWidthSpin"));
        auto* height = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("newDocumentHeightSpin"));
        auto* unit = dialog->findChild<QComboBox*>(QStringLiteral("newDocumentUnitCombo"));
        unit->setCurrentIndex(unit->findData(static_cast<int>(patchy::ui::MeasurementUnit::Pixels)));
        width->setValue(64);
        height->setValue(48);
        QApplication::processEvents();
        const auto expected = bits == 32 ? QStringLiteral("48.0K") : bits == 16 ? QStringLiteral("24.0K")
                                                                                : QStringLiteral("12.0K");
        bool found = false;
        for (const auto* label : dialog->findChildren<QLabel*>()) {
          found = found || label->text().contains(expected);
        }
        CHECK(found);
      }
      auto* width = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("newDocumentWidthSpin"));
      auto* height = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("newDocumentHeightSpin"));
      auto* unit = dialog->findChild<QComboBox*>(QStringLiteral("newDocumentUnitCombo"));
      if (unit != nullptr) {
        unit->setCurrentIndex(unit->findData(static_cast<int>(patchy::ui::MeasurementUnit::Pixels)));
      }
      if (width != nullptr && height != nullptr) {
        width->setValue(64);
        height->setValue(48);
      }
      dialog->accept();
    });
  };
  patchy::set_deep_editing_override(true);
  {
    auto&& [window, canvas] = ShownWindowFixture{};
    answer_new_document(16, true, nullptr);
    require_action(window, "fileNewAction")->trigger();
    QApplication::processEvents();
    const auto& sixteen = patchy::ui::MainWindowTestAccess::document(window);
    CHECK(sixteen.color_state().bit_depth == patchy::BitDepth::UInt16);
    CHECK(sixteen.width() == 64 && sixteen.height() == 48);
    CHECK(patchy::document_depth_problems(sixteen).empty());
    CHECK(require_action(window, "imageMode16BitAction")->isChecked());
    int remembered = 0;
    answer_new_document(32, true, &remembered);
    require_action(window, "fileNewAction")->trigger();
    QApplication::processEvents();
    CHECK(remembered == 16);
    CHECK(patchy::ui::MainWindowTestAccess::document(window).color_state().bit_depth == patchy::BitDepth::Float32);
    CHECK(require_action(window, "imageMode32BitAction")->isChecked());
    CHECK(window.findChild<QDoubleSpinBox*>(QStringLiteral("hdrPreviewExposureSpin"))->isVisible());
  }
  patchy::set_deep_editing_override(false);
  {
    auto&& [window, canvas] = ShownWindowFixture{};
    answer_new_document(8, false, nullptr);
    require_action(window, "fileNewAction")->trigger();
    QApplication::processEvents();
    CHECK(patchy::ui::MainWindowTestAccess::document(window).color_state().bit_depth == patchy::BitDepth::UInt8);
  }
  patchy::set_deep_editing_override(std::nullopt);
}
// Leaving 32 bits asks for HDR Toning (Exposure and Gamma): Cancel keeps the document
// at 32 bits, OK tones every pixel layer and converts.
void ui_image_mode_from_32_bits_asks_for_hdr_toning() {
  patchy::set_deep_editing_override(true);
  {
    auto&& [window, canvas] = ShownWindowFixture{};
    auto* to16 = require_action(window, "imageMode16BitAction");
    require_action(window, "imageMode32BitAction")->trigger();
    QApplication::processEvents();
    const auto& document = patchy::ui::MainWindowTestAccess::document(window);
    CHECK(document.color_state().bit_depth == patchy::BitDepth::Float32);
    CHECK(!document.layers().empty());
    const auto& base = document.layers().front();
    const auto before = patchy::load_pixel(base.pixels().format(), base.pixels().pixel(0, 0));
    // The 32-bit preview exposure darkens the display, not the pixels, and survives undo.
    auto* exposure = window.findChild<QDoubleSpinBox*>(QStringLiteral("hdrPreviewExposureSpin"));
    CHECK(exposure != nullptr && exposure->isVisible());
    const auto display_red = [&window] {
      return patchy::ui::qimage_from_document_rect(patchy::ui::MainWindowTestAccess::document(window),
                                                   QRect(0, 0, 1, 1), true)
          .pixelColor(0, 0)
          .red();
    };
    const auto plain_red = display_red();
    if (exposure != nullptr) {
      exposure->setValue(-2.0);
    }
    QApplication::processEvents();
    const auto expected_red = static_cast<int>(std::lround(
        patchy::srgb_encode(std::min(1.0, static_cast<double>(before[0]) / 255.0 * 0.25)) * 255.0));
    CHECK(std::abs(display_red() - expected_red) <= 1);
    CHECK(display_red() < plain_red || plain_red == 0);
    const auto& still = patchy::ui::MainWindowTestAccess::document(window).layers().front();
    CHECK(patchy::load_pixel(still.pixels().format(), still.pixels().pixel(0, 0)) == before);
    if (exposure != nullptr) {
      exposure->setValue(0.0);
    }
    const auto answer_toning = [](bool accept, double exposure) {
      QTimer::singleShot(0, [accept, exposure] {
        auto* dialog = find_top_level_dialog(QStringLiteral("hdrToningDialog"));
        CHECK(dialog != nullptr);
        if (dialog == nullptr) {
          return;
        }
        auto* spin = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("hdrToningExposureSpin"));
        CHECK(spin != nullptr);
        if (spin != nullptr) {
          spin->setValue(exposure);
        }
        if (accept) {
          dialog->accept();
        } else {
          dialog->reject();
        }
      });
    };
    answer_toning(false, 0.0);
    to16->trigger();
    QApplication::processEvents();
    CHECK(patchy::ui::MainWindowTestAccess::document(window).color_state().bit_depth ==
          patchy::BitDepth::Float32);
    CHECK(!to16->isChecked());
    answer_toning(true, -1.0);
    to16->trigger();
    QApplication::processEvents();
    const auto& converted = patchy::ui::MainWindowTestAccess::document(window);
    CHECK(converted.color_state().bit_depth == patchy::BitDepth::UInt16);
    CHECK(exposure != nullptr && !exposure->isVisible());
    CHECK(patchy::document_depth_problems(converted).empty());
    const auto& toned = converted.layers().front();
    const auto after = patchy::load_pixel(toned.pixels().format(), toned.pixels().pixel(0, 0));
    for (std::size_t c = 0; c < 3U; ++c) {
      const auto expected =
          patchy::srgb_encode(std::min(1.0, static_cast<double>(before[c]) / 255.0 * 0.5)) * 255.0;
      CHECK(std::abs(static_cast<double>(after[c]) - expected) < 0.01);
    }
  }
  patchy::set_deep_editing_override(std::nullopt);
}
void ui_deep_document_render_strips_match_the_sequential_render() {
  // The display render of a large 16-bit document splits into strips; the assembled
  // image must be the sequential render.
  patchy::Document document(2400, 2000, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer base(2400, 2000, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < 2000; ++y) {
    for (std::int32_t x = 0; x < 2400; ++x) {
      auto* px = base.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(x % 256);
      px[1] = static_cast<std::uint8_t>(y % 256);
      px[2] = static_cast<std::uint8_t>((x * 7 + y) % 256);
      px[3] = static_cast<std::uint8_t>(128 + (x + y) % 128);
    }
  }
  document.add_pixel_layer("Base", base);
  auto& top = document.add_pixel_layer("Top", solid_pixels(2400, 2000, patchy::PixelFormat::rgba8(), QColor(30, 200, 90)));
  top.set_blend_mode(patchy::BlendMode::Screen);
  top.set_opacity(0.4F);
  patchy::convert_document_depth(document, patchy::BitDepth::UInt16);
  const QRect all(0, 0, 2400, 2000);
  const auto parallel = patchy::ui::qimage_from_document_rect(document, all, true);
  const auto sequential = [&] {
    const EnvironmentVariableRestorer restore_single_threaded("PATCHY_RENDER_SINGLE_THREADED");
    qputenv("PATCHY_RENDER_SINGLE_THREADED", "1");
    return patchy::ui::qimage_from_document_rect(document, all, true);
  }();
  CHECK(!parallel.isNull());
  CHECK(parallel == sequential);
}
// Runs New Layer, Merge Down, Copy Merged + Paste and Clear in a document at `depth` and
// returns the resulting layers narrowed to 8 bits, checking the depth invariant on the way.
std::vector<patchy::PixelBuffer> run_layer_operations_at_depth(patchy::BitDepth depth) {
  using patchy::ui::MainWindowTestAccess;
  auto&& [window, canvas] = ShownWindowFixture{};
  auto& doc = MainWindowTestAccess::document(window);
  const auto width = doc.width();
  const auto height = doc.height();
  patchy::PixelBuffer base(width, height, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer top(width, height, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      auto* b = base.pixel(x, y);
      b[0] = static_cast<std::uint8_t>(x % 256);
      b[1] = static_cast<std::uint8_t>(y % 256);
      b[2] = 120;
      b[3] = 255;
      auto* t = top.pixel(x, y);
      t[0] = 230;
      t[1] = static_cast<std::uint8_t>((x + y) % 256);
      t[2] = 40;
      t[3] = static_cast<std::uint8_t>(80 + (x % 160));
    }
  }
  doc.add_pixel_layer("Base", base);
  auto& top_layer = doc.add_pixel_layer("Top", top);
  top_layer.set_blend_mode(patchy::BlendMode::Multiply);
  top_layer.set_opacity(0.6F);
  const auto top_id = top_layer.id();
  patchy::convert_document_depth(doc, depth);
  doc.set_active_layer(top_id);
  MainWindowTestAccess::refresh_layer_ui(window);
  canvas->document_changed();
  QApplication::processEvents();
  // New Layer takes the document's depth.
  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  CHECK(std::as_const(doc).find_layer(doc.active_layer_id().value_or(0))->pixels().format().bit_depth == depth);
  CHECK(patchy::document_depth_problems(doc).empty());
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  doc.set_active_layer(top_id);
  MainWindowTestAccess::refresh_layer_ui(window);
  // Merge Down composites at depth into the layer below.
  require_action(window, "layerMergeDownAction")->trigger();
  CHECK(process_events_until([&] { return std::as_const(doc).find_layer(top_id) == nullptr; }, 15000));
  CHECK(patchy::document_depth_problems(doc).empty());
  // Copy Merged of a selection, pasted back as a layer at the document's depth.
  patchy::PixelBuffer selection(width, height, patchy::PixelFormat::gray8());
  selection.clear(0);
  for (std::int32_t y = 20; y < 50; ++y) {
    for (std::int32_t x = 30; x < 90; ++x) {
      selection.pixel(x, y)[0] = 255;
    }
  }
  canvas->replace_selection_from_grayscale(selection, QStringLiteral("Select"));
  require_action(window, "editCopyMergedAction")->trigger();
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();
  const auto* pasted = std::as_const(doc).find_layer(doc.active_layer_id().value_or(0));
  CHECK(pasted != nullptr && pasted->pixels().format().bit_depth == depth);
  CHECK(pasted->pixels().width() == 60 && pasted->pixels().height() == 30);
  CHECK(patchy::document_depth_problems(doc).empty());
  // Clear a selected band of the merged layer.
  const auto merged_id = std::as_const(doc).layers()[std::as_const(doc).layers().size() - 2].id();
  doc.set_active_layer(merged_id);
  MainWindowTestAccess::refresh_layer_ui(window);
  canvas->replace_selection_from_grayscale(selection, QStringLiteral("Select"));
  require_action(window, "layerClearAction")->trigger();
  QApplication::processEvents();
  const auto* cleared = std::as_const(doc).find_layer(merged_id);
  CHECK(cleared != nullptr);
  CHECK(patchy::pixel_alpha_at(cleared->pixels(), 40 - cleared->bounds().x, 30 - cleared->bounds().y) == 0.0F);
  CHECK(patchy::pixel_alpha_at(cleared->pixels(), 10 - cleared->bounds().x, 10 - cleared->bounds().y) > 0.0F);
  CHECK(patchy::document_depth_problems(doc).empty());
  std::vector<patchy::PixelBuffer> layers;
  for (const auto& layer : std::as_const(doc).layers()) {
    layers.push_back(patchy::convert_pixel_buffer_depth(layer.pixels(), patchy::BitDepth::UInt8,
                                                        patchy::SampleKind::Color));
  }
  return layers;
}
void ui_layer_operations_keep_deep_documents_at_depth() {
  // The same operations on an 8-bit document and on its 16-bit conversion end in the
  // same layers, within a level (the deep composite rounds once, at the end).
  const auto eight = run_layer_operations_at_depth(patchy::BitDepth::UInt8);
  const auto sixteen = run_layer_operations_at_depth(patchy::BitDepth::UInt16);
  CHECK(eight.size() == sixteen.size());
  for (std::size_t i = 0; i < std::min(eight.size(), sixteen.size()); ++i) {
    CHECK(eight[i].width() == sixteen[i].width() && eight[i].height() == sixteen[i].height());
    CHECK(eight[i].format() == sixteen[i].format());
    int worst = 0;
    for (std::size_t b = 0; b < std::min(eight[i].byte_size(), sixteen[i].byte_size()); ++b) {
      worst = std::max(worst, std::abs(static_cast<int>(eight[i].data()[b]) - static_cast<int>(sixteen[i].data()[b])));
    }
    if (worst > 1) {
      std::cerr << "layer " << i << ": worst " << worst << "\n";
    }
    CHECK(worst <= 1);
  }
  // 32 bits runs the same operations; its values differ by design (linear compositing).
  const auto thirty_two = run_layer_operations_at_depth(patchy::BitDepth::Float32);
  CHECK(thirty_two.size() == eight.size());
}
// Free Transform (scale and rotate, committed) of a masked layer in a document at
// `depth`; returns the layer and its mask narrowed to 8 bits.
std::pair<patchy::PixelBuffer, patchy::PixelBuffer> free_transform_at_depth(patchy::BitDepth depth) {
  patchy::Document document(96, 64, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(96, 64, patchy::PixelFormat::rgba8(), QColor(255, 255, 255)));
  patchy::PixelBuffer content(96, 64, patchy::PixelFormat::rgba8());
  content.clear(0);
  for (std::int32_t y = 16; y < 48; ++y) {
    for (std::int32_t x = 24; x < 72; ++x) {
      auto* px = content.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(x * 3);
      px[1] = static_cast<std::uint8_t>(y * 4);
      px[2] = 150;
      px[3] = static_cast<std::uint8_t>(120 + x);
    }
  }
  auto& layer = document.add_pixel_layer("Layer", content);
  patchy::LayerMask mask;
  mask.bounds = patchy::Rect::from_size(96, 64);
  mask.pixels = patchy::PixelBuffer(96, 64, patchy::PixelFormat::gray8());
  for (std::int32_t y = 0; y < 64; ++y) {
    for (std::int32_t x = 0; x < 96; ++x) {
      mask.pixels.pixel(x, y)[0] = static_cast<std::uint8_t>(x * 2 + y);
    }
  }
  layer.set_mask(mask);
  const auto id = layer.id();
  patchy::convert_document_depth(document, depth);
  document.set_active_layer(id);
  patchy::ui::CanvasWidget canvas;
  canvas.resize(320, 240);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  canvas.set_transform_interpolation(patchy::ui::CanvasWidget::TransformInterpolation::Bilinear);
  canvas.show();
  QApplication::processEvents();
  CHECK(canvas.begin_free_transform());
  const auto state = canvas.transform_controls_state();
  CHECK(state.has_value());
  CHECK(canvas.set_transform_controls_state(state->reference_position, 140.0, 80.0, 21.0));
  canvas.finish_free_transform();
  QApplication::processEvents();
  CHECK(!canvas.free_transform_active());
  const auto& transformed = *std::as_const(document).find_layer(id);
  CHECK(transformed.pixels().format().bit_depth == depth);
  CHECK(transformed.mask().has_value() && transformed.mask()->pixels.format().bit_depth == depth);
  CHECK(patchy::document_depth_problems(document).empty());
  return {patchy::convert_pixel_buffer_depth(transformed.pixels(), patchy::BitDepth::UInt8, patchy::SampleKind::Color),
          patchy::convert_pixel_buffer_depth(transformed.mask()->pixels, patchy::BitDepth::UInt8,
                                             patchy::SampleKind::Coverage)};
}
void ui_free_transform_keeps_deep_layers_at_depth() {
  // The 16-bit transform resamples in float and the 8-bit one in bytes: they agree
  // within a level (alpha-weighted colors of nearly transparent edge pixels aside).
  const auto eight = free_transform_at_depth(patchy::BitDepth::UInt8);
  const auto sixteen = free_transform_at_depth(patchy::BitDepth::UInt16);
  CHECK(eight.first.width() == sixteen.first.width() && eight.first.height() == sixteen.first.height());
  int color_worst = 0;
  int alpha_worst = 0;
  for (std::int32_t y = 0; y < std::min(eight.first.height(), sixteen.first.height()); ++y) {
    for (std::int32_t x = 0; x < std::min(eight.first.width(), sixteen.first.width()); ++x) {
      const auto* a = eight.first.pixel(x, y);
      const auto* b = sixteen.first.pixel(x, y);
      alpha_worst = std::max(alpha_worst, std::abs(static_cast<int>(a[3]) - static_cast<int>(b[3])));
      if (a[3] >= 32 && b[3] >= 32) {
        for (int c = 0; c < 3; ++c) {
          color_worst = std::max(color_worst, std::abs(static_cast<int>(a[c]) - static_cast<int>(b[c])));
        }
      }
    }
  }
  CHECK(alpha_worst <= 1);
  CHECK(color_worst <= 2);
  CHECK(eight.second.width() == sixteen.second.width() && eight.second.height() == sixteen.second.height());
  int mask_worst = 0;
  for (std::size_t i = 0; i < std::min(eight.second.byte_size(), sixteen.second.byte_size()); ++i) {
    mask_worst = std::max(mask_worst, std::abs(static_cast<int>(eight.second.data()[i]) -
                                               static_cast<int>(sixteen.second.data()[i])));
  }
  CHECK(mask_worst <= 1);
  // 32 bits resamples linear light, so mixed edge pixels may differ a little from the
  // 8-bit result; flat colors must not move (Seth saw a 32-bit transform darken).
  const auto thirty_two = free_transform_at_depth(patchy::BitDepth::Float32);
  CHECK(eight.first.width() == thirty_two.first.width() && eight.first.height() == thirty_two.first.height());
  int interior_worst = 0;
  for (std::int32_t y = 2; y + 2 < std::min(eight.first.height(), thirty_two.first.height()); ++y) {
    for (std::int32_t x = 2; x + 2 < std::min(eight.first.width(), thirty_two.first.width()); ++x) {
      const auto* a = eight.first.pixel(x, y);
      const auto* b = thirty_two.first.pixel(x, y);
      if (a[3] == 255 && b[3] == 255) {
        for (int c = 0; c < 3; ++c) {
          interior_worst = std::max(interior_worst, std::abs(static_cast<int>(a[c]) - static_cast<int>(b[c])));
        }
      }
    }
  }
  CHECK(interior_worst <= 4);
}

// Free Transform on 16 and 32-bit documents shows and commits the layer's colors: the
// preview blit drew a 32-bit layer's linear floats as display values (mid-gray came out
// near-black, gradients darker). Checked during the transform and after Enter, at 100%
// and 50% zoom, against the 8-bit document.
void ui_free_transform_preview_and_commit_keep_colors_at_every_depth() {
  for (const double zoom : {1.0, 0.5}) {
    for (const auto depth : {patchy::BitDepth::UInt8, patchy::BitDepth::UInt16, patchy::BitDepth::Float32}) {
      patchy::Document document(200, 150, patchy::PixelFormat::rgba8());
      document.add_pixel_layer("Background", solid_pixels(200, 150, patchy::PixelFormat::rgba8(), QColor(80, 160, 220)));
      patchy::PixelBuffer content(80, 60, patchy::PixelFormat::rgba8());
      for (std::int32_t y = 0; y < 60; ++y) {
        for (std::int32_t x = 0; x < 80; ++x) {
          auto* px = content.pixel(x, y);
          const bool left = x < 40;
          px[0] = left ? 128 : 200;
          px[1] = left ? 128 : 60;
          px[2] = left ? 128 : 40;
          px[3] = 255;
        }
      }
      patchy::Layer layer(document.allocate_layer_id(), "Layer", std::move(content));
      layer.set_bounds(patchy::Rect{40, 30, 80, 60});
      const auto id = layer.id();
      document.add_layer(std::move(layer));
      patchy::convert_document_depth(document, depth);
      document.set_active_layer(id);
      patchy::ui::CanvasWidget canvas;
      canvas.resize(420, 320);
      canvas.set_document(&document);
      canvas.set_zoom(zoom);
      canvas.set_tool(patchy::ui::CanvasTool::Move);
      canvas.show();
      QApplication::processEvents();

      // 150% around the center (80, 60): the layer spans 20..140 x 15..105.
      CHECK(canvas.begin_free_transform());
      const auto state = canvas.transform_controls_state();
      CHECK(state.has_value());
      CHECK(canvas.set_transform_controls_state(state->reference_position, 150.0, 150.0, 0.0));
      QApplication::processEvents();
      const auto check_colors = [&](const char* phase) {
        const auto gray = canvas_pixel(canvas, QPoint(55, 60));
        const auto red = canvas_pixel(canvas, QPoint(110, 60));
        const auto backdrop = canvas_pixel(canvas, QPoint(10, 10));
        const bool ok = color_close(gray, QColor(128, 128, 128), 6) && color_close(red, QColor(200, 60, 40), 6) &&
                        color_close(backdrop, QColor(80, 160, 220), 6);
        if (!ok) {
          std::cout << "  zoom " << zoom << " depth " << static_cast<int>(depth) << " " << phase << ": gray "
                    << gray.name().toStdString() << " red " << red.name().toStdString() << " backdrop "
                    << backdrop.name().toStdString() << '\n';
        }
        CHECK(ok);
      };
      check_colors("preview");
      if (depth == patchy::BitDepth::Float32 && zoom == 1.0) {
        save_widget_artifact("ui_free_transform_preview_32_bit", canvas);
      }
      canvas.finish_free_transform();
      QApplication::processEvents();
      const auto settle_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
      while (!canvas.render_settled() && std::chrono::steady_clock::now() < settle_deadline) {
        QApplication::processEvents();
      }
      CHECK(!canvas.free_transform_active());
      check_colors("committed");
      const auto& transformed = *std::as_const(document).find_layer(id);
      CHECK(transformed.pixels().format().bit_depth == depth);
    }
  }
}
void ui_warp_transform_keeps_deep_layers_at_depth() {
  // A committed warp of a 16/32-bit layer writes the layer back at the document's
  // depth instead of narrowing it to 8 bits.
  for (const auto depth : {patchy::BitDepth::UInt16, patchy::BitDepth::Float32}) {
    patchy::Document document(96, 64, patchy::PixelFormat::rgba8());
    document.add_pixel_layer("Background", solid_pixels(96, 64, patchy::PixelFormat::rgba8(), QColor(255, 255, 255)));
    patchy::PixelBuffer content(96, 64, patchy::PixelFormat::rgba8());
    content.clear(0);
    for (std::int32_t y = 16; y < 48; ++y) {
      for (std::int32_t x = 24; x < 72; ++x) {
        auto* px = content.pixel(x, y);
        px[0] = static_cast<std::uint8_t>(x * 3);
        px[1] = static_cast<std::uint8_t>(y * 4);
        px[2] = 150;
        px[3] = 255;
      }
    }
    const auto id = document.add_pixel_layer("Layer", content).id();
    patchy::convert_document_depth(document, depth);
    document.set_active_layer(id);
    patchy::ui::CanvasWidget canvas;
    canvas.resize(320, 240);
    canvas.set_document(&document);
    canvas.set_tool(patchy::ui::CanvasTool::Move);
    canvas.show();
    QApplication::processEvents();
    CHECK(canvas.begin_warp_transform());
    CHECK(canvas.warp_transform_active());
    const auto corner = canvas.warp_handle_document_position(0);
    canvas.set_warp_handle_document_position(0, corner + QPointF(-8.0, -6.0));
    canvas.finish_warp_transform();
    QApplication::processEvents();
    CHECK(!canvas.warp_transform_active());
    const auto& warped = *std::as_const(document).find_layer(id);
    CHECK(warped.bounds().width > 96);
    CHECK(warped.pixels().format().bit_depth == depth);
    CHECK(patchy::document_depth_problems(document).empty());
  }
}
// A soft brush stroke on a layer, then one on its mask, in a document at `depth`;
// returns the layer and mask narrowed to 8 bits.
std::pair<patchy::PixelBuffer, patchy::PixelBuffer> brush_strokes_at_depth(patchy::BitDepth depth) {
  patchy::Document document(80, 48, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(80, 48, patchy::PixelFormat::rgba8(), QColor(255, 255, 255)));
  patchy::PixelBuffer content(80, 48, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < 48; ++y) {
    for (std::int32_t x = 0; x < 80; ++x) {
      auto* px = content.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(x * 3);
      px[1] = 90;
      px[2] = static_cast<std::uint8_t>(y * 5);
      px[3] = static_cast<std::uint8_t>(x < 40 ? 255 : 120);
    }
  }
  auto& layer = document.add_pixel_layer("Layer", content);
  patchy::LayerMask mask;
  mask.bounds = patchy::Rect::from_size(80, 48);
  mask.pixels = patchy::PixelBuffer(80, 48, patchy::PixelFormat::gray8());
  mask.pixels.clear(200);
  layer.set_mask(mask);
  const auto id = layer.id();
  patchy::convert_document_depth(document, depth);
  document.set_active_layer(id);
  patchy::ui::CanvasWidget canvas;
  canvas.resize(320, 200);
  canvas.set_document(&document);
  canvas.set_zoom(1.0);
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_primary_color(QColor(30, 160, 220));
  canvas.set_brush_size(11);
  canvas.set_brush_softness(80);
  canvas.set_brush_opacity(70);
  canvas.show();
  QApplication::processEvents();
  const auto stroke = [&canvas](QPoint from, QPoint to) {
    const auto a = canvas.widget_position_for_document_point(from);
    const auto b = canvas.widget_position_for_document_point(to);
    send_mouse(canvas, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
    send_mouse(canvas, QEvent::MouseMove, (a + b) / 2, Qt::NoButton, Qt::LeftButton);
    send_mouse(canvas, QEvent::MouseMove, b, Qt::NoButton, Qt::LeftButton);
    send_mouse(canvas, QEvent::MouseButtonRelease, b, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
  };
  stroke(QPoint(8, 10), QPoint(70, 36));
  canvas.set_layer_edit_target(patchy::ui::CanvasWidget::LayerEditTarget::Mask);
  canvas.set_primary_color(QColor(0, 0, 0));
  stroke(QPoint(10, 40), QPoint(72, 6));
  const auto& painted = *std::as_const(document).find_layer(id);
  CHECK(painted.pixels().format().bit_depth == depth);
  CHECK(painted.mask().has_value() && painted.mask()->pixels.format().bit_depth == depth);
  CHECK(patchy::document_depth_problems(document).empty());
  return {patchy::convert_pixel_buffer_depth(painted.pixels(), patchy::BitDepth::UInt8, patchy::SampleKind::Color),
          patchy::convert_pixel_buffer_depth(painted.mask()->pixels, patchy::BitDepth::UInt8,
                                             patchy::SampleKind::Coverage)};
}
void ui_brush_paints_deep_layers_and_masks_at_depth() {
  const auto eight = brush_strokes_at_depth(patchy::BitDepth::UInt8);
  const auto sixteen = brush_strokes_at_depth(patchy::BitDepth::UInt16);
  const auto worst = [](const patchy::PixelBuffer& a, const patchy::PixelBuffer& b) {
    if (a.byte_size() != b.byte_size()) {
      return 999;
    }
    int result = 0;
    for (std::size_t i = 0; i < a.byte_size(); ++i) {
      result = std::max(result, std::abs(static_cast<int>(a.data()[i]) - static_cast<int>(b.data()[i])));
    }
    return result;
  };
  const auto layer_worst = worst(eight.first, sixteen.first);
  const auto mask_worst = worst(eight.second, sixteen.second);
  if (layer_worst > 1 || mask_worst > 1) {
    std::cerr << "brush at 16 bits: layer worst " << layer_worst << ", mask worst " << mask_worst << "\n";
  }
  CHECK(layer_worst <= 1);
  CHECK(mask_worst <= 1);
  // Both strokes landed: brush-colored layer pixels, and mask values below the 200 fill.
  const auto landed = [](const std::pair<patchy::PixelBuffer, patchy::PixelBuffer>& result) {
    int brushed = 0;
    for (std::int32_t y = 0; y < result.first.height(); ++y) {
      for (std::int32_t x = 0; x < result.first.width(); ++x) {
        const auto* px = result.first.pixel(x, y);
        brushed += std::abs(px[0] - 30) < 20 && std::abs(px[1] - 160) < 20 && std::abs(px[2] - 220) < 20 ? 1 : 0;
      }
    }
    int masked = 0;
    for (const auto value : result.second.data()) {
      masked += value < 150 ? 1 : 0;
    }
    return brushed > 40 && masked > 40;
  };
  CHECK(landed(eight));
  CHECK(landed(sixteen));
  (void)brush_strokes_at_depth(patchy::BitDepth::Float32);
}
// One stroke of `tool` on a textured layer in a document at `depth`; returns the layer
// narrowed to 8 bits. Clone and Healing take an Alt-click source first.
patchy::PixelBuffer retouch_stroke_at_depth(patchy::ui::CanvasTool tool, patchy::BitDepth depth) {
  patchy::Document document(72, 48, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer content(72, 48, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < 48; ++y) {
    for (std::int32_t x = 0; x < 72; ++x) {
      auto* px = content.pixel(x, y);
      px[0] = static_cast<std::uint8_t>((x * 7 + y * 3) % 256);
      px[1] = static_cast<std::uint8_t>((x * 2 + y * 9) % 256);
      px[2] = static_cast<std::uint8_t>(80 + (x + y) % 120);
      px[3] = 255;
    }
  }
  auto& layer = document.add_pixel_layer("Layer", content);
  const auto id = layer.id();
  patchy::convert_document_depth(document, depth);
  document.set_active_layer(id);
  patchy::ui::CanvasWidget canvas;
  canvas.resize(300, 200);
  canvas.set_document(&document);
  canvas.set_zoom(1.0);
  canvas.set_tool(tool);
  canvas.set_brush_size(9);
  canvas.set_brush_softness(50);
  canvas.set_brush_opacity(80);
  canvas.show();
  QApplication::processEvents();
  if (tool == patchy::ui::CanvasTool::Clone || tool == patchy::ui::CanvasTool::Healing) {
    const auto source = canvas.widget_position_for_document_point(QPoint(12, 12));
    send_mouse(canvas, QEvent::MouseButtonPress, source, Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
    send_mouse(canvas, QEvent::MouseButtonRelease, source, Qt::LeftButton, Qt::NoButton, Qt::AltModifier);
  }
  const auto a = canvas.widget_position_for_document_point(QPoint(30, 20));
  const auto b = canvas.widget_position_for_document_point(QPoint(60, 34));
  send_mouse(canvas, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, (a + b) / 2, Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, b, Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, b, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  const auto& edited = *std::as_const(document).find_layer(id);
  CHECK(edited.pixels().format().bit_depth == depth);
  CHECK(patchy::document_depth_problems(document).empty());
  return patchy::convert_pixel_buffer_depth(edited.pixels(), patchy::BitDepth::UInt8, patchy::SampleKind::Color);
}
void ui_retouch_tools_edit_deep_layers_at_depth() {
  // Each retouch tool at 16 bits matches its 8-bit stroke within a few levels (the deep
  // path rounds once; the smear and blur tools carry values between dabs) and changes
  // the layer; 32 bits runs too.
  using patchy::ui::CanvasTool;
  for (const auto tool : {CanvasTool::Smudge, CanvasTool::Dodge, CanvasTool::Burn, CanvasTool::Sponge,
                          CanvasTool::BlurBrush, CanvasTool::SharpenBrush, CanvasTool::Clone, CanvasTool::Healing,
                          CanvasTool::SpotHealing}) {
    const auto eight = retouch_stroke_at_depth(tool, patchy::BitDepth::UInt8);
    const auto sixteen = retouch_stroke_at_depth(tool, patchy::BitDepth::UInt16);
    const auto original = retouch_stroke_at_depth(CanvasTool::Eyedropper, patchy::BitDepth::UInt8);
    int worst = 0;
    int changed = 0;
    for (std::size_t i = 0; i < std::min(eight.byte_size(), sixteen.byte_size()); ++i) {
      worst = std::max(worst, std::abs(static_cast<int>(eight.data()[i]) - static_cast<int>(sixteen.data()[i])));
    }
    for (std::size_t i = 0; i < std::min(sixteen.byte_size(), original.byte_size()); ++i) {
      changed += sixteen.data()[i] != original.data()[i] ? 1 : 0;
    }
    // Smudge's 8-bit path re-rounds its carried sample on every dab, so its error grows
    // along the stroke; the deep one carries floats.
    const int allowed = tool == CanvasTool::Smudge ? 6 : 3;
    if (worst > allowed || changed == 0) {
      std::cerr << "tool " << static_cast<int>(tool) << ": worst " << worst << ", changed " << changed << "\n";
    }
    CHECK(eight.byte_size() == sixteen.byte_size());
    CHECK(worst <= allowed);
    CHECK(changed > 0);
    (void)retouch_stroke_at_depth(tool, patchy::BitDepth::Float32);
  }
}
void ui_eyedropper_picks_the_composite_of_a_16_bit_document() {
  // A 16-bit document's layers are not 8-bit, so the eyedropper picks from the deep
  // render; it must see the same composite as the 8-bit document it came from.
  patchy::Document document(48, 16, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Base", solid_pixels(48, 16, patchy::PixelFormat::rgba8(), QColor(200, 40, 10)));
  auto& top = document.add_pixel_layer("Top", solid_pixels(48, 16, patchy::PixelFormat::rgba8(), QColor(20, 200, 90)));
  top.set_blend_mode(patchy::BlendMode::Multiply);
  top.set_opacity(0.5F);
  const QPoint sample(30, 8);
  const auto expected = patchy::ui::qimage_from_document_rect(document, QRect(sample, QSize(1, 1)), true).pixelColor(0, 0);
  patchy::convert_document_depth(document, patchy::BitDepth::UInt16);
  CHECK(document.color_state().bit_depth == patchy::BitDepth::UInt16);
  patchy::ui::CanvasWidget canvas;
  canvas.resize(192, 64);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Eyedropper);
  canvas.set_primary_color(QColor(1, 2, 3));
  canvas.show();
  QApplication::processEvents();
  const auto point = canvas.widget_position_for_document_point(sample);
  send_mouse(canvas, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
  CHECK(color_close(canvas.primary_color(), expected, 1));
  CHECK(!color_close(canvas.primary_color(), QColor(1, 2, 3), 8));
}
// GitHub issue 150: with the Eyedropper tool, Alt+click picks into the BACKGROUND
// color (Photoshop's convention); the paint tools' temporary Alt eyedropper keeps
// picking the foreground.
void ui_eyedropper_alt_click_picks_the_background_color() {
  patchy::Document document(48, 16, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Teal", solid_pixels(48, 16, patchy::PixelFormat::rgba8(), QColor(20, 140, 160)));
  patchy::ui::CanvasWidget canvas;
  canvas.resize(192, 64);
  canvas.set_document(&document);
  canvas.set_primary_color(QColor(1, 2, 3));
  canvas.set_secondary_color(QColor(250, 250, 250));
  canvas.show();
  QApplication::processEvents();
  const auto point = canvas.widget_position_for_document_point(QPoint(24, 8));
  canvas.set_tool(patchy::ui::CanvasTool::Eyedropper);
  send_mouse(canvas, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
  send_mouse(canvas, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton, Qt::AltModifier);
  QApplication::processEvents();
  CHECK(color_close(canvas.secondary_color(), QColor(20, 140, 160), 1));
  CHECK(canvas.primary_color() == QColor(1, 2, 3));
  // A plain click still sets the foreground.
  send_mouse(canvas, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(color_close(canvas.primary_color(), QColor(20, 140, 160), 1));
  // The Brush tool's Alt eyedropper: foreground, never background.
  canvas.set_primary_color(QColor(1, 2, 3));
  canvas.set_secondary_color(QColor(250, 250, 250));
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  send_mouse(canvas, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
  send_mouse(canvas, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton, Qt::AltModifier);
  QApplication::processEvents();
  CHECK(color_close(canvas.primary_color(), QColor(20, 140, 160), 1));
  CHECK(canvas.secondary_color() == QColor(250, 250, 250));
}
// Eyedropper Sample Size and Sample (GitHub issue 151): a square average around
// the click over the merged image or over the active layer alone, where a
// transparent spot leaves the color unchanged.
void ui_eyedropper_sample_size_and_current_layer() {
  patchy::Document document(48, 16, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("White", solid_pixels(48, 16, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  auto dot = solid_pixels(48, 16, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(dot, QRect(10, 8, 1, 1), QColor(255, 0, 0));
  const auto dot_id = document.add_pixel_layer("Dot", std::move(dot)).id();
  document.set_active_layer(dot_id);
  patchy::ui::CanvasWidget canvas;
  canvas.resize(192, 64);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Eyedropper);
  canvas.show();
  QApplication::processEvents();
  const auto click = [&canvas](QPoint document_point) {
    const auto point = canvas.widget_position_for_document_point(document_point);
    send_mouse(canvas, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
    send_mouse(canvas, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
  };

  // Defaults: Point Sample over all layers, the pick unchanged from before.
  CHECK(canvas.eyedropper_sample_size() == 1);
  CHECK(canvas.eyedropper_sample_layers() == patchy::SampleLayers::AllLayers);
  click(QPoint(10, 8));
  CHECK(color_close(canvas.primary_color(), QColor(255, 0, 0), 1));

  // 3 by 3 over all layers: one red pixel among eight white ones.
  canvas.set_eyedropper_sample_size(3);
  click(QPoint(10, 8));
  CHECK(color_close(canvas.primary_color(), QColor(255, 227, 227), 1));
  // Clipped at the document edge: the corner square holds four white pixels.
  click(QPoint(0, 0));
  CHECK(color_close(canvas.primary_color(), QColor(255, 255, 255), 1));

  // Current Layer: only the dot has alpha, so the average is pure red.
  canvas.set_eyedropper_sample_layers(patchy::SampleLayers::CurrentLayer);
  click(QPoint(11, 9));
  CHECK(color_close(canvas.primary_color(), QColor(255, 0, 0), 1));
  // A transparent spot on the current layer keeps the color.
  canvas.set_primary_color(QColor(1, 2, 3));
  click(QPoint(30, 4));
  CHECK(canvas.primary_color() == QColor(1, 2, 3));

  // Sides are odd and capped at Photoshop's 101.
  canvas.set_eyedropper_sample_size(4);
  CHECK(canvas.eyedropper_sample_size() == 5);
  canvas.set_eyedropper_sample_size(500);
  CHECK(canvas.eyedropper_sample_size() == 101);
}

// Eyedropper Sets (GitHub issue 151): a click sets the color the option names,
// Alt inverts it for that click (with Foreground, Alt+click is Photoshop's
// background pick), and the cursor badge shows the color a click would set.
void ui_eyedropper_sets_option_and_alt_inverts() {
  patchy::Document document(48, 16, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Teal", solid_pixels(48, 16, patchy::PixelFormat::rgba8(), QColor(20, 140, 160)));
  patchy::ui::CanvasWidget canvas;
  canvas.resize(192, 64);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Eyedropper);
  canvas.show();
  QApplication::processEvents();
  const auto point = canvas.widget_position_for_document_point(QPoint(24, 8));
  const auto reset = [&canvas] {
    canvas.set_primary_color(QColor(1, 2, 3));
    canvas.set_secondary_color(QColor(250, 250, 250));
  };
  const auto click = [&](Qt::KeyboardModifiers modifiers) {
    send_mouse(canvas, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton, modifiers);
    send_mouse(canvas, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton, modifiers);
    QApplication::processEvents();
  };
  const QColor teal(20, 140, 160);
  const auto cursor_image = [&canvas] { return canvas.cursor().pixmap().toImage(); };

  CHECK(!canvas.eyedropper_picks_background());
  CHECK(cursor_image() == patchy::ui::eyedropper_target_cursor(false).pixmap().toImage());
  CHECK(cursor_image() != patchy::ui::eyedropper_target_cursor(true).pixmap().toImage());
  reset();
  click(Qt::NoModifier);
  CHECK(color_close(canvas.primary_color(), teal, 1) && canvas.secondary_color() == QColor(250, 250, 250));
  reset();
  click(Qt::AltModifier);
  CHECK(color_close(canvas.secondary_color(), teal, 1) && canvas.primary_color() == QColor(1, 2, 3));

  canvas.set_eyedropper_picks_background(true);
  CHECK(cursor_image() == patchy::ui::eyedropper_target_cursor(true).pixmap().toImage());
  reset();
  click(Qt::NoModifier);
  CHECK(color_close(canvas.secondary_color(), teal, 1) && canvas.primary_color() == QColor(1, 2, 3));
  reset();
  click(Qt::AltModifier);
  CHECK(color_close(canvas.primary_color(), teal, 1) && canvas.secondary_color() == QColor(250, 250, 250));
  // Holding Alt flips the badge without moving the pointer.
  send_key_press(canvas, Qt::Key_Alt, Qt::AltModifier);
  QApplication::processEvents();
  CHECK(cursor_image() == patchy::ui::eyedropper_target_cursor(false).pixmap().toImage());
  send_key_release(canvas, Qt::Key_Alt);
  QApplication::processEvents();
  CHECK(cursor_image() == patchy::ui::eyedropper_target_cursor(true).pixmap().toImage());
  save_widget_artifact("ui_eyedropper_background_cursor", canvas);

  // The Brush tool's Alt eyedropper still sets the foreground whatever Sets says.
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  reset();
  click(Qt::AltModifier);
  CHECK(color_close(canvas.primary_color(), teal, 1) && canvas.secondary_color() == QColor(250, 250, 250));
}

// A small colorful document for the Pick tool's overlay shots: a two-way gradient
// with a dark block, so outlines and the loupe read over light and dark pixels.
patchy::Document eyedropper_overlay_document() {
  patchy::Document document(64, 48, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(64, 48, patchy::PixelFormat::rgba8(), QColor(Qt::white));
  for (int y = 0; y < 48; ++y) {
    for (int x = 0; x < 64; ++x) {
      fill_pixel_rect(pixels, QRect(x, y, 1, 1), QColor(60 + x * 3, 90 + y * 3, 230 - x * 2));
    }
  }
  fill_pixel_rect(pixels, QRect(36, 8, 20, 14), QColor(30, 26, 40));
  fill_pixel_rect(pixels, QRect(8, 28, 14, 12), QColor(250, 236, 120));
  document.add_pixel_layer("Gradient", std::move(pixels));
  return document;
}

// GitHub issue 319: with an average Sample Size the Pick tool outlines the square a
// click averages, snapped to the document pixels and moving with the pointer. Point
// Sample, a square under 4 screen pixels, the pointer off the document and other
// tools show nothing.
void ui_eyedropper_sample_outline_follows_the_pointer() {
  CHECK(!patchy::ui::eyedropper_sample_outline_visible(1, 32.0));
  CHECK(!patchy::ui::eyedropper_sample_outline_visible(3, 1.0));
  CHECK(patchy::ui::eyedropper_sample_outline_visible(3, 4.0 / 3.0));
  CHECK(patchy::ui::eyedropper_sample_outline_visible(5, 1.0));
  CHECK(!patchy::ui::eyedropper_sample_outline_visible(101, 0.03));
  CHECK(patchy::ui::eyedropper_sample_rect(QPoint(10, 10), 5, QSize(64, 48)) == QRect(8, 8, 5, 5));
  CHECK(patchy::ui::eyedropper_sample_rect(QPoint(0, 47), 5, QSize(64, 48)) == QRect(0, 45, 3, 3));

  auto document = eyedropper_overlay_document();
  patchy::ui::CanvasWidget canvas;
  canvas.resize(640, 480);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Eyedropper);
  canvas.show();
  canvas.set_zoom(1.0);
  QApplication::processEvents();
  const auto hover = [&canvas](QPoint document_point) {
    send_mouse(canvas, QEvent::MouseMove, canvas.widget_position_for_document_point(document_point), Qt::NoButton,
               Qt::NoButton);
    QApplication::processEvents();
  };
  const auto expected = [&canvas](QRect area) {
    const auto top_left = canvas.widget_position_f(QPointF(area.topLeft()));
    const auto bottom_right = canvas.widget_position_f(QPointF(area.x() + area.width(), area.y() + area.height()));
    return QRect(top_left.toPoint(), bottom_right.toPoint() - QPoint(1, 1));
  };

  hover(QPoint(20, 20));
  CHECK(!canvas.eyedropper_sample_outline_widget_rect().has_value());  // Point Sample
  canvas.set_eyedropper_sample_size(3);
  CHECK(!canvas.eyedropper_sample_outline_widget_rect().has_value());  // 3 screen pixels
  canvas.set_eyedropper_sample_size(5);
  auto outline = canvas.eyedropper_sample_outline_widget_rect();
  CHECK(outline.has_value() && outline->size() == QSize(5, 5));

  canvas.set_zoom(6.0);
  QApplication::processEvents();
  hover(QPoint(20, 20));
  outline = canvas.eyedropper_sample_outline_widget_rect();
  CHECK(outline.has_value() && *outline == expected(QRect(18, 18, 5, 5)));
  CHECK(outline.has_value() && outline->size() == QSize(30, 30));
  // It moves with the pointer, one document pixel at a time.
  hover(QPoint(21, 20));
  const auto moved = canvas.eyedropper_sample_outline_widget_rect();
  CHECK(moved.has_value() && outline.has_value() && moved->topLeft() == outline->topLeft() + QPoint(6, 0));
  // Clipped to the document at its edge, like the average.
  hover(QPoint(0, 0));
  outline = canvas.eyedropper_sample_outline_widget_rect();
  CHECK(outline.has_value() && *outline == expected(QRect(0, 0, 3, 3)));
  hover(QPoint(24, 24));
  save_widget_artifact("ui_eyedropper_sample_outline", canvas);
  // Off the document: nothing.
  send_mouse(canvas, QEvent::MouseMove, canvas.widget_position_for_document_point(QPoint(0, 0)) - QPoint(30, 30),
             Qt::NoButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(!canvas.eyedropper_sample_outline_widget_rect().has_value());
  hover(QPoint(24, 24));
  CHECK(canvas.eyedropper_sample_outline_widget_rect().has_value());
  // Another tool hides it.
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  CHECK(!canvas.eyedropper_sample_outline_widget_rect().has_value());
  canvas.set_tool(patchy::ui::CanvasTool::Eyedropper);
  hover(QPoint(24, 24));
  CHECK(canvas.eyedropper_sample_outline_widget_rect().has_value());
  canvas.set_eyedropper_sample_size(1);
  CHECK(!canvas.eyedropper_sample_outline_widget_rect().has_value());
}

// GitHub issue 320: the sample loupe's geometry. It centers on the pointer, slides
// inward near the viewport edge instead of clipping, and its lens shows the sample
// square with three pixels of context (nine pixels across at least).
void ui_sample_loupe_geometry_stays_inside_the_viewport() {
  const QRectF viewport(0.0, 0.0, 400.0, 300.0);
  const double reach = patchy::ui::kSampleLoupeBandOuterRadius + patchy::ui::kSampleLoupeMargin;
  auto geometry = patchy::ui::sample_loupe_geometry(QPointF(200.0, 150.0), viewport);
  CHECK(geometry.center == QPointF(200.0, 150.0) && !geometry.shifted);
  CHECK(geometry.bounds() == QRectF(200.0 - reach, 150.0 - reach, 2.0 * reach, 2.0 * reach));
  for (const auto pointer : {QPointF(3.0, 4.0), QPointF(398.0, 296.0), QPointF(200.0, 2.0), QPointF(1.0, 150.0)}) {
    geometry = patchy::ui::sample_loupe_geometry(pointer, viewport);
    CHECK(geometry.shifted);
    CHECK(viewport.contains(geometry.bounds()));
  }
  geometry = patchy::ui::sample_loupe_geometry(QPointF(3.0, 4.0), viewport);
  CHECK(geometry.center == QPointF(reach, reach));
  // Off one edge only: the other axis stays on the pointer.
  geometry = patchy::ui::sample_loupe_geometry(QPointF(200.0, 2.0), viewport);
  CHECK(geometry.center == QPointF(200.0, reach));
  // A viewport smaller than the loupe centers it.
  geometry = patchy::ui::sample_loupe_geometry(QPointF(10.0, 10.0), QRectF(20.0, 30.0, 100.0, 80.0));
  CHECK(geometry.center == QPointF(70.0, 70.0));

  CHECK(patchy::ui::sample_loupe_view_pixels(1) == 9);
  CHECK(patchy::ui::sample_loupe_view_pixels(3) == 9);
  CHECK(patchy::ui::sample_loupe_view_pixels(5) == 11);
  CHECK(patchy::ui::sample_loupe_view_pixels(11) == 17);
  CHECK(patchy::ui::sample_loupe_view_pixels(101) == 107);
  CHECK(patchy::ui::sample_loupe_view_rect(QPoint(20, 10), 1) == QRect(16, 6, 9, 9));
  CHECK(patchy::ui::sample_loupe_view_rect(QPoint(20, 10), 5) == QRect(15, 5, 11, 11));
}

// GitHub issue 320: holding a pick shows the loupe (on by default), its top arc the
// live sample and its bottom arc the color the pick replaces (foreground, or the
// background when Sets says so), for the Pick tool and the Brush's Alt eyedropper.
// The pointer is hidden while it shows; the loupe slides inside the canvas at its
// edge and goes away on release or when Show Sample Ring is off.
void ui_sample_loupe_shows_while_a_pick_is_held() {
  auto document = eyedropper_overlay_document();
  patchy::ui::CanvasWidget canvas;
  canvas.resize(520, 380);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Eyedropper);
  canvas.show();
  canvas.set_zoom(6.0);
  QApplication::processEvents();
  CHECK(canvas.eyedropper_show_sample_ring());
  const QColor foreground(1, 2, 3);
  const QColor background(250, 250, 250);
  canvas.set_primary_color(foreground);
  canvas.set_secondary_color(background);
  const QColor dark_block(30, 26, 40);
  const auto at = [&canvas](QPoint document_point) {
    return canvas.widget_position_for_document_point(document_point) + QPoint(3, 3);
  };

  // Press on the dark block, 5 by 5 average, by the block's edge.
  canvas.set_eyedropper_sample_size(5);
  send_mouse(canvas, QEvent::MouseButtonPress, at(QPoint(37, 12)), Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  auto loupe = canvas.sample_loupe_widget_rect();
  auto colors = canvas.sample_loupe_colors();
  CHECK(loupe.has_value() && colors.has_value());
  CHECK(colors.has_value() && colors->second == foreground);
  CHECK(colors.has_value() && colors->first == canvas.primary_color());
  CHECK(canvas.cursor().shape() == Qt::BlankCursor);
  CHECK(loupe.has_value() && loupe->contains(at(QPoint(37, 12))));
  save_widget_artifact("ui_sample_loupe_dark", canvas);
  // Dragging into the block: the top arc follows the live pick, the bottom keeps the old color.
  send_mouse(canvas, QEvent::MouseMove, at(QPoint(44, 14)), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  colors = canvas.sample_loupe_colors();
  CHECK(colors.has_value() && color_close(colors->first, dark_block, 1) && colors->second == foreground);
  send_mouse(canvas, QEvent::MouseButtonRelease, at(QPoint(44, 14)), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(!canvas.sample_loupe_widget_rect().has_value());
  CHECK(canvas.cursor().shape() != Qt::BlankCursor);
  CHECK(color_close(canvas.primary_color(), dark_block, 1));

  // Point Sample on the light (yellow) block, picking into the background.
  canvas.set_eyedropper_sample_size(1);
  canvas.set_eyedropper_picks_background(true);
  canvas.set_primary_color(foreground);
  send_mouse(canvas, QEvent::MouseButtonPress, at(QPoint(9, 29)), Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  colors = canvas.sample_loupe_colors();
  CHECK(colors.has_value() && colors->second == background);
  CHECK(colors.has_value() && color_close(colors->first, QColor(250, 236, 120), 1));
  save_widget_artifact("ui_sample_loupe_light", canvas);
  send_mouse(canvas, QEvent::MouseButtonRelease, at(QPoint(9, 29)), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  canvas.set_eyedropper_picks_background(false);

  // At the canvas corner the loupe slides inward, whole.
  const QPoint corner = canvas.widget_position_for_document_point(QPoint(0, 0)) + QPoint(1, 1);
  send_mouse(canvas, QEvent::MouseButtonPress, corner, Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  loupe = canvas.sample_loupe_widget_rect();
  CHECK(loupe.has_value());
  if (loupe.has_value()) {
    const auto center = loupe->center();
    CHECK(canvas.rect().contains(*loupe));
    CHECK(center != corner);
  }
  save_widget_artifact("ui_sample_loupe_edge", canvas);
  send_mouse(canvas, QEvent::MouseButtonRelease, corner, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  // The Brush's Alt eyedropper shows it too, against the foreground.
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_primary_color(foreground);
  send_mouse(canvas, QEvent::MouseButtonPress, at(QPoint(20, 20)), Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
  QApplication::processEvents();
  colors = canvas.sample_loupe_colors();
  CHECK(colors.has_value() && colors->second == foreground);
  send_mouse(canvas, QEvent::MouseButtonRelease, at(QPoint(20, 20)), Qt::LeftButton, Qt::NoButton, Qt::AltModifier);
  QApplication::processEvents();
  CHECK(!canvas.sample_loupe_widget_rect().has_value());

  // Show Sample Ring off: no loupe, the cursor stays.
  canvas.set_tool(patchy::ui::CanvasTool::Eyedropper);
  canvas.set_eyedropper_show_sample_ring(false);
  send_mouse(canvas, QEvent::MouseButtonPress, at(QPoint(20, 20)), Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(!canvas.sample_loupe_widget_rect().has_value());
  CHECK(canvas.cursor().shape() != Qt::BlankCursor);
  send_mouse(canvas, QEvent::MouseButtonRelease, at(QPoint(20, 20)), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
}

// GitHub #478: a Pick drag sampled, refreshed the loupe and reported the color to the
// panels for every pointer move, so on Wayland (every motion delivered) a fast pointer
// queued moves and the loupe trailed behind it. Moves queued in one event-loop pass now
// apply once, at the latest position; the release still picks where the pointer is, and
// moves over flat color do not report the same color again.
void ui_pick_drag_coalesces_queued_moves() {
  auto document = eyedropper_overlay_document();
  patchy::ui::CanvasWidget canvas;
  canvas.resize(520, 380);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Eyedropper);
  canvas.show();
  canvas.set_zoom(6.0);
  QApplication::processEvents();
  canvas.set_eyedropper_sample_size(1);
  canvas.set_drag_move_coalescing_for_testing(patchy::ui::CanvasWidget::DragMoveCoalescing::AllMoves);
  int reports = 0;
  canvas.set_color_picked_callback([&reports, &canvas](QColor color) {
    ++reports;
    canvas.set_primary_color(color);
  });
  const auto at = [&canvas](QPoint document_point) {
    return canvas.widget_position_for_document_point(document_point) + QPoint(3, 3);
  };
  // The gradient under the blocks (eyedropper_overlay_document).
  const auto gradient = [](QPoint point) { return QColor(60 + point.x() * 3, 90 + point.y() * 3, 230 - point.x() * 2); };
  const auto coalesced = [&canvas] { return canvas.render_cache_diagnostics().drag_moves_coalesced; };
  const auto deliver_move = [&canvas](QPoint position) {
    // Delivered without running the event loop, which would end the pass between moves.
    QMouseEvent event(QEvent::MouseMove, QPointF(position), QPointF(canvas.mapToGlobal(position)), Qt::NoButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &event);
  };

  send_mouse(canvas, QEvent::MouseButtonPress, at(QPoint(2, 4)), Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(reports == 1);
  CHECK(color_close(canvas.primary_color(), gradient(QPoint(2, 4)), 1));
  const auto before = coalesced();
  // A burst of 30 moves along the gradient, queued as a window system queues them.
  for (int move = 1; move <= 30; ++move) {
    const auto position = at(QPoint(2 + move, 4));
    QApplication::postEvent(&canvas, new QMouseEvent(QEvent::MouseMove, QPointF(position),
                                                     QPointF(canvas.mapToGlobal(position)), Qt::NoButton,
                                                     Qt::LeftButton, Qt::NoModifier));
  }
  QApplication::processEvents();
  QApplication::processEvents();
  QApplication::processEvents();
  // The first queued move applies at once, the other 29 wait for the pass end, which
  // samples the latest: three reports for the whole gesture so far, not 31.
  CHECK(coalesced() - before == 29);
  CHECK(reports == 3);
  CHECK(color_close(canvas.primary_color(), gradient(QPoint(32, 4)), 1));
  auto colors = canvas.sample_loupe_colors();
  CHECK(colors.has_value() && colors->first == canvas.primary_color());
  const auto loupe = canvas.sample_loupe_widget_rect();
  CHECK(loupe.has_value() && loupe->contains(at(QPoint(32, 4))));
  // A move recorded right before the release still lands: the release applies it and
  // picks at the release position.
  deliver_move(at(QPoint(30, 30)));  // applied, opens a pass
  deliver_move(at(QPoint(31, 31)));  // recorded
  CHECK(coalesced() - before == 30);
  send_mouse(canvas, QEvent::MouseButtonRelease, at(QPoint(31, 31)), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(coalesced() - before == 30);
  CHECK(color_close(canvas.primary_color(), gradient(QPoint(31, 31)), 1));
  CHECK(!canvas.sample_loupe_widget_rect().has_value());

  // Moves over flat color (the dark block) report the color once, at the press.
  reports = 0;
  send_mouse(canvas, QEvent::MouseButtonPress, at(QPoint(38, 10)), Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  for (int move = 1; move <= 5; ++move) {
    send_mouse(canvas, QEvent::MouseMove, at(QPoint(38 + move, 10 + move)), Qt::NoButton, Qt::LeftButton);
    QApplication::processEvents();
  }
  send_mouse(canvas, QEvent::MouseButtonRelease, at(QPoint(43, 15)), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(reports == 1);
  CHECK(color_close(canvas.primary_color(), QColor(30, 26, 40), 1));
  // Leaving the block reports the new color.
  send_mouse(canvas, QEvent::MouseButtonPress, at(QPoint(38, 10)), Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, at(QPoint(30, 30)), Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, at(QPoint(30, 30)), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(reports == 3);
  CHECK(color_close(canvas.primary_color(), gradient(QPoint(30, 30)), 1));
}

// GitHub #478: each Pick drag move saved the colors to the settings file (a disk flush
// per move, the largest part of a move's cost). The drag's moves still show the color in
// the panels; the pair is written once, when the pick ends.
void ui_pick_drag_saves_colors_on_release() {
  SettingsValueRestorer foreground_setting(QStringLiteral("tools/foregroundColor"));
  SettingsValueRestorer background_setting(QStringLiteral("tools/backgroundColor"));
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(eyedropper_overlay_document(), QStringLiteral("Pick drag"));
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Pick"))->trigger();
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  canvas->set_zoom(6.0);
  canvas->set_eyedropper_sample_size(1);
  QApplication::processEvents();
  const auto at = [canvas](QPoint document_point) {
    return canvas->widget_position_for_document_point(document_point) + QPoint(3, 3);
  };
  const auto saved_foreground = [] {
    return patchy::ui::app_settings().value(QStringLiteral("tools/foregroundColor")).toString();
  };
  const auto before = saved_foreground();
  const QColor last(60 + 30 * 3, 90 + 30 * 3, 230 - 30 * 2);  // the gradient at (30, 30)
  CHECK(before != last.name(QColor::HexRgb));
  send_mouse(*canvas, QEvent::MouseButtonPress, at(QPoint(2, 4)), Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  send_mouse(*canvas, QEvent::MouseMove, at(QPoint(10, 20)), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  send_mouse(*canvas, QEvent::MouseMove, at(QPoint(30, 30)), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  // The panels follow the drag; the file does not.
  CHECK(color_close(canvas->primary_color(), last, 1));
  CHECK(saved_foreground() == before);
  send_mouse(*canvas, QEvent::MouseButtonRelease, at(QPoint(30, 30)), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(QColor(saved_foreground()) == canvas->primary_color());
  CHECK(color_close(QColor(saved_foreground()), last, 1));
}

// GitHub issue 320: deep documents reach the lens through the display conversion. The
// lens center shows the sampled pixel as the canvas shows it at 8, 16 and 32 bits (a
// 32-bit composite drawn as raw linear floats would come out too dark).
void ui_sample_loupe_lens_matches_the_canvas_at_every_depth() {
  for (const auto depth : {patchy::BitDepth::UInt8, patchy::BitDepth::UInt16, patchy::BitDepth::Float32}) {
    patchy::Document document(48, 32, patchy::PixelFormat::rgba8());
    document.add_pixel_layer("Base", solid_pixels(48, 32, patchy::PixelFormat::rgba8(), QColor(200, 40, 10)));
    auto& top =
        document.add_pixel_layer("Top", solid_pixels(48, 32, patchy::PixelFormat::rgba8(), QColor(20, 200, 90)));
    top.set_blend_mode(patchy::BlendMode::Multiply);
    top.set_opacity(0.5F);
    if (depth != patchy::BitDepth::UInt8) {
      patchy::convert_document_depth(document, depth);
    }
    patchy::ui::CanvasWidget canvas;
    canvas.resize(360, 260);
    canvas.set_document(&document);
    canvas.set_tool(patchy::ui::CanvasTool::Eyedropper);
    canvas.show();
    canvas.set_zoom(4.0);
    QApplication::processEvents();
    const auto point = canvas.widget_position_for_document_point(QPoint(24, 16)) + QPoint(2, 2);
    const auto shown = canvas.grab().toImage().pixelColor(point);
    send_mouse(canvas, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
    QApplication::processEvents();
    CHECK(canvas.sample_loupe_widget_rect().has_value());
    // A point sample's cell is outlined; its middle is the pixel itself.
    const auto lens = canvas.grab().toImage().pixelColor(point);
    if (!color_close(lens, shown, 3)) {
      std::fprintf(stderr, "  depth %d: lens %s, canvas %s\n", static_cast<int>(depth), qPrintable(lens.name()),
                   qPrintable(shown.name()));
    }
    CHECK(color_close(lens, shown, 3));
    send_mouse(canvas, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
  }
}

// The Eyedropper options row (GitHub issue 151): Sample Size and Sample combos
// for the Eyedropper only, reaching the canvas, every new document and the
// saved tool settings.
void ui_eyedropper_options_bar_sample_controls() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* size_combo = window.findChild<QComboBox*>(QStringLiteral("eyedropperSampleSizeCombo"));
  auto* layers_combo = window.findChild<QComboBox*>(QStringLiteral("eyedropperSampleLayersCombo"));
  CHECK(size_combo != nullptr && layers_combo != nullptr);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  CHECK(!size_combo->isVisible() && !layers_combo->isVisible());
  require_action_by_text(window, QStringLiteral("Pick"))->trigger();
  QApplication::processEvents();
  CHECK(size_combo->isVisible() && layers_combo->isVisible());
  CHECK(size_combo->count() == 7);
  CHECK(size_combo->itemText(0) == QStringLiteral("Point Sample"));
  CHECK(size_combo->itemText(3) == QStringLiteral("11 by 11 Average"));
  CHECK(layers_combo->count() == 5);
  CHECK(layers_combo->itemText(0) == QStringLiteral("Current Layer"));
  CHECK(layers_combo->itemText(1) == QStringLiteral("Current & Below"));
  CHECK(layers_combo->itemText(2) == QStringLiteral("All Layers"));
  CHECK(layers_combo->itemText(3) == QStringLiteral("All Layers No Adjustments"));
  CHECK(layers_combo->itemText(4) == QStringLiteral("Current & Below No Adjustments"));
  CHECK(layers_combo->currentIndex() == 2);
  auto* sets_foreground = window.findChild<QPushButton*>(QStringLiteral("eyedropperSetsForegroundButton"));
  auto* sets_background = window.findChild<QPushButton*>(QStringLiteral("eyedropperSetsBackgroundButton"));
  CHECK(sets_foreground != nullptr && sets_background != nullptr);
  CHECK(sets_foreground->isVisible() && sets_background->isVisible());
  CHECK(sets_foreground->isChecked() && !sets_background->isChecked());
  CHECK(sets_foreground->toolTip().contains(QStringLiteral("+click")));
  CHECK(!sets_foreground->toolTip().contains(QStringLiteral("%ALT%")));
  auto* show_ring = window.findChild<QCheckBox*>(QStringLiteral("eyedropperShowSampleRingCheck"));
  CHECK(show_ring != nullptr && show_ring->isVisible() && show_ring->isChecked());
  CHECK(show_ring != nullptr && show_ring->text() == QStringLiteral("Show Sample Ring"));
  save_widget_artifact("ui_eyedropper_options_bar", window);

  // The open Sample menu (GitHub issue 321). The window is grabbed before the popup
  // opens and the list drawn over it: Qt's private popup frame around the list keeps
  // an unthemed palette, so the shared popup capture would reject the shot.
  CHECK(capture_theme_mismatch(window).isEmpty());
  auto shot = window.grab().toImage();
  bool popup_captured = false;
  QTimer::singleShot(0, [&] {
    auto* list = layers_combo->view();
    CHECK(list != nullptr && list->isVisible());
    if (list != nullptr && list->isVisible()) {
      CHECK(capture_theme_mismatch(*list).isEmpty());
      const auto origin = window.mapToGlobal(QPoint(0, 0));
      const auto list_position = list->mapToGlobal(QPoint(0, 0)) - origin;
      {
        QPainter painter(&shot);
        painter.drawPixmap(list_position, list->grab());
      }
      const auto crop = QRect(layers_combo->mapToGlobal(QPoint(0, 0)) - origin, layers_combo->size())
                            .united(QRect(list_position, list->size()))
                            .adjusted(-170, -14, 48, 14)
                            .intersected(shot.rect());
      ensure_artifact_dir();
      CHECK(shot.copy(crop).save(QStringLiteral("test-artifacts/issue_321_sample_menu.png")));
      popup_captured = true;
    }
    layers_combo->hidePopup();
  });
  layers_combo->showPopup();
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(popup_captured);

  auto* canvas = require_canvas(window);
  layers_combo->setCurrentIndex(1);
  QApplication::processEvents();
  CHECK(canvas->eyedropper_sample_layers() == patchy::SampleLayers::CurrentAndBelow);
  layers_combo->setCurrentIndex(4);
  QApplication::processEvents();
  CHECK(canvas->eyedropper_sample_layers() == patchy::SampleLayers::CurrentAndBelowNoAdjustments);
  {
    auto settings = patchy::ui::app_settings();
    CHECK(settings.value(QStringLiteral("tools/eyedropperSampleLayers")).toString() ==
          QStringLiteral("currentAndBelowNoAdjustments"));
    CHECK(settings.value(QStringLiteral("tools/eyedropperSampleAllLayers")).toBool());
  }
  size_combo->setCurrentIndex(size_combo->findData(5));
  layers_combo->setCurrentIndex(0);
  QApplication::processEvents();
  CHECK(canvas->eyedropper_sample_size() == 5);
  CHECK(canvas->eyedropper_sample_layers() == patchy::SampleLayers::CurrentLayer);
  sets_background->click();
  QApplication::processEvents();
  CHECK(sets_background->isChecked() && !sets_foreground->isChecked());
  CHECK(canvas->eyedropper_picks_background());
  CHECK(canvas->eyedropper_show_sample_ring());
  if (show_ring != nullptr) {
    show_ring->click();
  }
  QApplication::processEvents();
  CHECK(!canvas->eyedropper_show_sample_ring());
  {
    auto settings = patchy::ui::app_settings();
    CHECK(!settings.value(QStringLiteral("tools/eyedropperShowSampleRing"), true).toBool());
    CHECK(settings.value(QStringLiteral("tools/eyedropperSampleSize")).toInt() == 5);
    CHECK(!settings.value(QStringLiteral("tools/eyedropperSampleAllLayers")).toBool());
    CHECK(settings.value(QStringLiteral("tools/eyedropperSampleLayers")).toString() == QStringLiteral("current"));
    CHECK(settings.value(QStringLiteral("tools/eyedropperPicksBackground")).toBool());
  }
  patchy::ui::MainWindowTestAccess::create_default_document(window);
  QApplication::processEvents();
  auto* second = require_canvas(window);
  CHECK(second != canvas);
  CHECK(second->eyedropper_sample_size() == 5);
  CHECK(second->eyedropper_sample_layers() == patchy::SampleLayers::CurrentLayer);
  CHECK(second->eyedropper_picks_background());
  CHECK(!second->eyedropper_show_sample_ring());

  // Back to the defaults so later tests see Photoshop's starting state.
  size_combo->setCurrentIndex(0);
  layers_combo->setCurrentIndex(2);
  sets_foreground->click();
  if (show_ring != nullptr) {
    show_ring->click();
  }
  QApplication::processEvents();
  CHECK(second->eyedropper_show_sample_ring());
  auto settings = patchy::ui::app_settings();
  CHECK(settings.value(QStringLiteral("tools/eyedropperShowSampleRing")).toBool());
  CHECK(!settings.value(QStringLiteral("tools/eyedropperPicksBackground")).toBool());
  CHECK(settings.value(QStringLiteral("tools/eyedropperSampleSize")).toInt() == 1);
  CHECK(settings.value(QStringLiteral("tools/eyedropperSampleAllLayers")).toBool());
  CHECK(settings.value(QStringLiteral("tools/eyedropperSampleLayers")).toString() == QStringLiteral("all"));
}

namespace {

// Seth's test stack for the Sample choices (GitHub issue 321), bottom to top: a blue
// Background, an Invert, a half-opaque green square (the active layer), another
// Invert, and a half-opaque red layer over everything. Inside the square each
// Sample choice reads a different color.
patchy::Document sample_layers_test_document() {
  patchy::Document document(240, 160, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(240, 160, patchy::PixelFormat::rgba8(), QColor(60, 120, 200)));
  patchy::AdjustmentSettings invert;
  invert.kind = patchy::AdjustmentKind::Invert;
  patchy::Layer invert_below(document.allocate_layer_id(), "Invert below", patchy::LayerKind::Adjustment);
  patchy::configure_adjustment_layer(invert_below, invert);
  document.add_layer(std::move(invert_below));
  auto square = solid_pixels(240, 160, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(square, QRect(60, 40, 120, 80), QColor(40, 160, 60));
  auto& active = document.add_pixel_layer("Green square (select this one)", std::move(square));
  active.set_opacity(0.5F);
  const auto active_id = active.id();
  patchy::Layer invert_above(document.allocate_layer_id(), "Invert above", patchy::LayerKind::Adjustment);
  patchy::configure_adjustment_layer(invert_above, invert);
  document.add_layer(std::move(invert_above));
  document.add_pixel_layer("Red veil", solid_pixels(240, 160, patchy::PixelFormat::rgba8(), QColor(220, 40, 40)))
      .set_opacity(0.5F);
  document.set_active_layer(active_id);
  return document;
}

}  // namespace

// Pick tool Sample choices (GitHub issue 321): each choice reads its own composite
// through the canvas, Point Sample and averaged, and the paint tools' Alt
// eyedropper shares the choice.
void ui_eyedropper_sample_layers_choices_pick_their_composites() {
  auto document = sample_layers_test_document();
  {
    // The stack Seth tests with, as a PSD artifact (test-files/issue-321-sample-layers.psd).
    ensure_artifact_dir();
    const auto path = QStringLiteral("test-artifacts/issue-321-sample-layers.psd");
    patchy::psd::DocumentIo::write_layered_rgb8_file(document, patchy::ui::to_filesystem_path(path));
    const auto reread = patchy::psd::DocumentIo::read_file(patchy::ui::to_filesystem_path(path));
    CHECK(reread.layers().size() == 5);
    CHECK(patchy::ui::qimage_from_document(reread, true).pixelColor(120, 80) ==
          patchy::ui::qimage_from_document(document, true).pixelColor(120, 80));
  }
  patchy::ui::CanvasWidget canvas;
  canvas.resize(480, 320);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Eyedropper);
  canvas.show();
  QApplication::processEvents();
  const auto click = [&canvas](QPoint document_point, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    const auto point = canvas.widget_position_for_document_point(document_point);
    send_mouse(canvas, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton, modifiers);
    send_mouse(canvas, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton, modifiers);
    QApplication::processEvents();
  };
  using patchy::SampleLayers;
  const std::array<std::pair<SampleLayers, QColor>, 5> expected{{
      {SampleLayers::CurrentLayer, QColor(40, 160, 60)},
      {SampleLayers::CurrentAndBelow, QColor(117, 147, 57)},
      {SampleLayers::AllLayers, QColor(179, 74, 119)},
      {SampleLayers::AllLayersNoAdjustments, QColor(135, 90, 85)},
      {SampleLayers::CurrentAndBelowNoAdjustments, QColor(50, 140, 130)},
  }};
  for (const auto size : {1, 5}) {
    canvas.set_eyedropper_sample_size(size);
    for (const auto& [mode, color] : expected) {
      canvas.set_eyedropper_sample_layers(mode);
      canvas.set_primary_color(QColor(1, 2, 3));
      click(QPoint(120, 80));
      CHECK(color_close(canvas.primary_color(), color, 3));
    }
  }
  // Outside the square, Current & Below reads the stack under the active layer's level.
  canvas.set_eyedropper_sample_size(1);
  canvas.set_eyedropper_sample_layers(SampleLayers::CurrentAndBelow);
  click(QPoint(10, 10));
  CHECK(color_close(canvas.primary_color(), QColor(195, 135, 55), 3));

  // The Brush tool's Alt eyedropper uses the same choice.
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_eyedropper_sample_layers(SampleLayers::CurrentAndBelowNoAdjustments);
  canvas.set_primary_color(QColor(1, 2, 3));
  click(QPoint(120, 80), Qt::AltModifier);
  CHECK(color_close(canvas.primary_color(), QColor(50, 140, 130), 3));

  // Current & Below with no active layer warns and keeps the color.
  canvas.set_tool(patchy::ui::CanvasTool::Eyedropper);
  canvas.set_eyedropper_sample_layers(SampleLayers::CurrentAndBelow);
  document.clear_active_layer();
  canvas.set_primary_color(QColor(1, 2, 3));
  click(QPoint(120, 80));
  CHECK(canvas.primary_color() == QColor(1, 2, 3));
  canvas.set_eyedropper_sample_layers(SampleLayers::AllLayers);
}

// The pre-321 bool still decides the first launch after the update (GitHub issue
// 321): false opens on Current Layer, and the next save writes the new key.
void ui_eyedropper_sample_layers_migrates_legacy_setting() {
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("tools/eyedropperSampleLayers"));
    settings.setValue(QStringLiteral("tools/eyedropperSampleAllLayers"), false);
  }
  {
    auto&& [window, canvas] = ShownWindowFixture{};
    auto* layers_combo = window.findChild<QComboBox*>(QStringLiteral("eyedropperSampleLayersCombo"));
    CHECK(layers_combo != nullptr);
    CHECK(require_canvas(window)->eyedropper_sample_layers() == patchy::SampleLayers::CurrentLayer);
    if (layers_combo != nullptr) {
      CHECK(layers_combo->currentText() == QStringLiteral("Current Layer"));
      layers_combo->setCurrentIndex(3);
      QApplication::processEvents();
    }
    auto settings = patchy::ui::app_settings();
    CHECK(settings.value(QStringLiteral("tools/eyedropperSampleLayers")).toString() ==
          QStringLiteral("allNoAdjustments"));
    CHECK(settings.value(QStringLiteral("tools/eyedropperSampleAllLayers")).toBool());
  }
  {
    // The new key wins over the bool on the next launch.
    auto&& [window, canvas] = ShownWindowFixture{};
    CHECK(require_canvas(window)->eyedropper_sample_layers() == patchy::SampleLayers::AllLayersNoAdjustments);
  }
  auto settings = patchy::ui::app_settings();
  settings.remove(QStringLiteral("tools/eyedropperSampleLayers"));
  settings.remove(QStringLiteral("tools/eyedropperSampleAllLayers"));
}

// The 8-bit eyedropper reads the real composite, not a per-layer approximation: an
// opaque red child inside a 50% group over white picks pink (the old sampler returned
// pure red), a clipped layer shows only over its base, and every pick equals the
// compositor's own pixel, whether the canvas has a current render cache or not.
void ui_eyedropper_picks_group_opacity_and_clipping_composite() {
  patchy::Document document(48, 16, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("White", solid_pixels(48, 16, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer group(document.allocate_layer_id(), "Half", patchy::LayerKind::Group);
  group.set_opacity(0.5F);
  group.add_child(patchy::Layer(document.allocate_layer_id(), "Red",
                               solid_pixels(48, 16, patchy::PixelFormat::rgba8(), QColor(255, 0, 0))));
  document.add_layer(std::move(group));
  auto base = solid_pixels(48, 16, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(base, QRect(32, 0, 16, 16), QColor(0, 200, 0));
  document.add_pixel_layer("Green right", std::move(base));
  auto& blue = document.add_pixel_layer("Blue clipped",
                                        solid_pixels(48, 16, patchy::PixelFormat::rgba8(), QColor(0, 0, 255)));
  blue.set_clipped(true);
  const QPoint pink_point(8, 8);
  const QPoint blue_point(40, 8);
  const auto expected_pink =
      patchy::ui::qimage_from_document_rect(document, QRect(pink_point, QSize(1, 1)), true).pixelColor(0, 0);
  const auto expected_blue =
      patchy::ui::qimage_from_document_rect(document, QRect(blue_point, QSize(1, 1)), true).pixelColor(0, 0);
  CHECK(color_close(expected_pink, QColor(255, 128, 128), 2));
  CHECK(color_close(expected_blue, QColor(0, 0, 255), 1));
  patchy::ui::CanvasWidget canvas;
  canvas.resize(192, 64);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Eyedropper);
  canvas.set_primary_color(QColor(1, 2, 3));
  const auto pick = [&](QPoint document_point) {
    const auto point = canvas.widget_position_for_document_point(document_point);
    send_mouse(canvas, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
    send_mouse(canvas, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
    return canvas.primary_color();
  };
  // Before the first paint there is no render cache: the one-pixel render path.
  CHECK(color_close(pick(pink_point), expected_pink, 1));
  CHECK(!color_close(canvas.primary_color(), QColor(255, 0, 0), 8));
  CHECK(color_close(pick(blue_point), expected_blue, 1));
  // Shown and painted: the cached composite path answers the same.
  canvas.show();
  QApplication::processEvents();
  CHECK(color_close(pick(pink_point), expected_pink, 1));
  CHECK(color_close(pick(blue_point), expected_blue, 1));
}
void ui_eyedropper_starts_in_gray_area_and_drags_to_document_color() {
  auto&& [window, canvas] = ShownWindowFixture{};
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(34, 201, 145));
  use_solid_fill_settings(canvas);
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  const auto grey_start = canvas->widget_position_for_document_point(QPoint(-24, -24));
  const QPoint sample_document_point(80, 80);
  const auto sample_widget_point = canvas->widget_position_for_document_point(sample_document_point);
  CHECK(canvas->rect().contains(grey_start));
  CHECK(color_close(canvas_pixel(*canvas, sample_document_point), QColor(34, 201, 145), 4));
  canvas->set_primary_color(QColor(230, 20, 20));
  require_action_by_text(window, QStringLiteral("Pick"))->trigger();
  send_mouse(*canvas, QEvent::MouseButtonPress, grey_start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, sample_widget_point, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, sample_widget_point, Qt::LeftButton, Qt::NoButton);
  CHECK(color_close(canvas->primary_color(), QColor(34, 201, 145), 4));
}
void ui_photoshop_shortcuts_are_registered() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(require_action_by_text(window, QStringLiteral("New"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_N));
  CHECK(require_action_by_text(window, QStringLiteral("Open..."))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_O));
  CHECK(require_action_by_text(window, QStringLiteral("Save"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_S));
  CHECK(require_action_by_text(window, QStringLiteral("Save As..."))->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
  // The conventional web export key: File > Export for Web... (docs/web-export.md).
  CHECK(require_action(window, "fileExportForWebAction")->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_S));
  CHECK(require_action_by_text(window, QStringLiteral("Close"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_W));
  CHECK(require_action_by_text(window, QStringLiteral("Close All"))->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_W));
  CHECK(require_action(window, "filePrintAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_P));
  CHECK(require_action(window, "filePageSetupAction") != nullptr);
  CHECK(require_action_by_text(window, QStringLiteral("Undo"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_Z));
  CHECK(require_action_by_text(window, QStringLiteral("Redo"))->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z));
  CHECK(require_action_by_text(window, QStringLiteral("Redo"))->shortcuts().contains(QKeySequence(Qt::CTRL | Qt::Key_Y)));
  CHECK(require_action_by_text(window, QStringLiteral("Cut"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_X));
  CHECK(require_action_by_text(window, QStringLiteral("Copy"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_C));
  CHECK(require_action_by_text(window, QStringLiteral("Paste"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_V));
  CHECK(require_action_by_text(window, QStringLiteral("Free Transform..."))->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::Key_T));
  CHECK(require_action_by_text(window, QStringLiteral("Select All"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_A));
  CHECK(require_action_by_text(window, QStringLiteral("Clear Selection"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_D));
  CHECK(require_action_by_text(window, QStringLiteral("Reselect"))->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D));
  CHECK(require_action_by_text(window, QStringLiteral("Inverse"))->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I));
  CHECK(require_action(window, "selectExpandAction") != nullptr);
  CHECK(require_action(window, "selectContractAction") != nullptr);
  CHECK(require_action(window, "selectBorderAction") != nullptr);
  CHECK(require_action(window, "selectLayerTransparencyAction") != nullptr);
  CHECK(require_action(window, "selectGrowAction") != nullptr);
  CHECK(require_action(window, "selectSimilarAction") != nullptr);
  CHECK(require_action(window, "layerEditAdjustmentAction") != nullptr);
  // GitHub issues 275, 294: the dialog on Ctrl+Shift+N; New Layer without it has no default key.
  CHECK(require_action_by_text(window, QStringLiteral("New Layer..."))->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
  CHECK(require_action_by_text(window, QStringLiteral("New Layer"))->shortcut().isEmpty());
  CHECK(require_action_by_text(window, QStringLiteral("Layer Via Copy"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_J));
  CHECK(require_action_by_text(window, QStringLiteral("Layer Via Cut"))->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_J));
  CHECK(require_action_by_text(window, QStringLiteral("Duplicate Layer"))->shortcut().isEmpty());
  CHECK(require_action(window, "layerFillForegroundAction")->shortcut() == QKeySequence(Qt::ALT | Qt::Key_Backspace));
  CHECK(require_action(window, "layerFillBackgroundAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_Backspace));
  CHECK(require_action_by_text(window, QStringLiteral("Merge Down"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_E));
  CHECK(require_action_by_text(window, QStringLiteral("Merge Visible"))->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
  CHECK(require_action_by_text(window, QStringLiteral("Merge Visible to New Layer (Copy)"))->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_E));
  CHECK(require_action(window, "imageSizeAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_I));
  CHECK(require_action_by_text(window, QStringLiteral("Canvas Size..."))->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_C));
  CHECK(require_action(window, "imageAdjustInvertAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_I));
  CHECK(require_action(window, "imageAdjustLevelsAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_L));
  CHECK(require_action(window, "imageAdjustCurvesAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_M));
  CHECK(require_action(window, "imageAdjustHueSaturationAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_U));
  CHECK(require_action(window, "imageAdjustColorBalanceAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_B));
  CHECK(require_action(window, "imageAdjustDesaturateAction")->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U));
  CHECK(require_action(window, "imageAdjustAutoToneAction")->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L));
  CHECK(require_action(window, "imageAdjustAutoContrastAction")->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_L));
  CHECK(require_action(window, "imageAdjustAutoColorAction")->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B));
  CHECK(require_action(window, "imageAdjustAutoAllAction")->shortcut().isEmpty());
  CHECK(require_action(window, "imageAdjustBrightnessContrastAction")->shortcut().isEmpty());
  CHECK(window.findChild<QAction*>(QStringLiteral("imageAdjustBrightnessAction")) == nullptr);
  CHECK(window.findChild<QAction*>(QStringLiteral("imageAdjustContrastAction")) == nullptr);
  CHECK(require_action(window, "viewExtrasAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_H));
  CHECK(require_action(window, "viewToggleRulersAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_R));
  CHECK(require_action(window, "viewToggleGridAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_Apostrophe));
  CHECK(require_action(window, "viewToggleGuidesAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_Semicolon));
  CHECK(require_action(window, "viewToggleSnapAction")->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Semicolon));
  CHECK(require_action(window, "viewLockGuidesAction")->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Semicolon));
  CHECK(require_action(window, "viewSnapToGuidesAction")->isCheckable());
  CHECK(require_action(window, "viewSnapToGridAction")->isCheckable());
  CHECK(require_action(window, "viewSnapToDocumentAction")->isCheckable());
  CHECK(require_action(window, "viewSnapToLayersAction")->isCheckable());
  CHECK(require_action(window, "viewSnapToSelectionAction")->isCheckable());
  CHECK(require_action(window, "viewNewGuideAction") != nullptr);
  CHECK(require_action(window, "viewNewGuideLayoutAction") != nullptr);
  CHECK(require_action_by_text(window, QStringLiteral("Default Colors"))->shortcut() == QKeySequence(Qt::Key_D));
  CHECK(require_action_by_text(window, QStringLiteral("Swap Colors"))->shortcut() == QKeySequence(Qt::Key_X));
  CHECK(require_action_by_text(window, QStringLiteral("Move"))->shortcut() == QKeySequence(Qt::Key_V));
  CHECK(require_action_by_text(window, QStringLiteral("Marquee"))->shortcut() == QKeySequence(Qt::Key_M));
  // Shift+<letter> walks a flyout (GitHub issue 45); the members past the first ship unbound.
  CHECK(require_action_by_text(window, QStringLiteral("Elliptical Marquee"))->shortcut().isEmpty());
  CHECK(require_action(window, "toolCycleMarqueeAction")->shortcut() == QKeySequence(Qt::SHIFT | Qt::Key_M));
  CHECK(require_action(window, "toolCycleLassoAction")->shortcut() == QKeySequence(Qt::SHIFT | Qt::Key_L));
  CHECK(require_action(window, "toolCycleWandAction")->shortcut() == QKeySequence(Qt::SHIFT | Qt::Key_W));
  CHECK(require_action(window, "toolCyclePenAction")->shortcut() == QKeySequence(Qt::SHIFT | Qt::Key_P));
  CHECK(require_action(window, "toolCyclePathAction")->shortcut() == QKeySequence(Qt::SHIFT | Qt::Key_A));
  CHECK(require_action_by_text(window, QStringLiteral("Lasso"))->shortcut() == QKeySequence(Qt::Key_L));
  CHECK(require_action_by_text(window, QStringLiteral("Magic Wand"))->shortcut() == QKeySequence(Qt::Key_W));
  CHECK(require_action_by_text(window, QStringLiteral("Brush"))->shortcut() == QKeySequence(Qt::Key_B));
  CHECK(require_action_by_text(window, QStringLiteral("Clone"))->shortcut() == QKeySequence(Qt::Key_S));
  CHECK(require_action_by_text(window, QStringLiteral("Pattern Stamp"))->shortcut().isEmpty());
  CHECK(require_action(window, "toolCycleStampAction")->shortcut() == QKeySequence(Qt::SHIFT | Qt::Key_S));
  CHECK(require_action_by_text(window, QStringLiteral("Healing Brush"))->shortcut() ==
        QKeySequence(Qt::Key_J));
  CHECK(require_action(window, "toolSpotHealingAction")->shortcut().isEmpty());
  CHECK(require_action(window, "toolCycleHealingAction")->shortcut() == QKeySequence(Qt::SHIFT | Qt::Key_J));
  CHECK(require_action(window, "toolPatchAction")->shortcut().isEmpty());
  CHECK(require_action_by_text(window, QStringLiteral("Smudge"))->shortcut() == QKeySequence(Qt::Key_R));
  CHECK(require_action(window, "toolBlurAction")->shortcut().isEmpty());
  CHECK(require_action(window, "toolCycleDetailAction")->shortcut() == QKeySequence(Qt::SHIFT | Qt::Key_R));
  CHECK(require_action(window, "toolSharpenAction")->shortcut().isEmpty());
  CHECK(require_action(window, "toolDodgeAction")->shortcut() == QKeySequence(Qt::Key_O));
  CHECK(require_action(window, "toolBurnAction")->shortcut().isEmpty());
  CHECK(require_action(window, "toolCycleToningAction")->shortcut() == QKeySequence(Qt::SHIFT | Qt::Key_O));
  CHECK(require_action(window, "toolSpongeAction")->shortcut().isEmpty());
  CHECK(require_action_by_text(window, QStringLiteral("Eraser"))->shortcut() == QKeySequence(Qt::Key_E));
  CHECK(require_action_by_text(window, QStringLiteral("Gradient"))->shortcut() == QKeySequence(Qt::Key_G));
  CHECK(require_action_by_text(window, QStringLiteral("Fill"))->shortcut().isEmpty());
  CHECK(require_action(window, "toolCycleFillAction")->shortcut() == QKeySequence(Qt::SHIFT | Qt::Key_G));
  CHECK(require_action_by_text(window, QStringLiteral("Rect"))->shortcut() == QKeySequence(Qt::Key_U));
  // Line ships unbound: its old Ctrl+Shift+U default collided with Desaturate, so neither fired.
  CHECK(require_action_by_text(window, QStringLiteral("Line"))->shortcut().isEmpty());
  CHECK(require_action_by_text(window, QStringLiteral("Ellipse"))->shortcut().isEmpty());
  CHECK(require_action(window, "toolCycleShapeAction")->shortcut() == QKeySequence(Qt::SHIFT | Qt::Key_U));
  CHECK(require_action_by_text(window, QStringLiteral("Pick"))->shortcut() == QKeySequence(Qt::Key_I));
  CHECK(require_action_by_text(window, QStringLiteral("Type"))->shortcut() == QKeySequence(Qt::Key_T));
  CHECK(require_action_by_text(window, QStringLiteral("Hand"))->shortcut() == QKeySequence(Qt::Key_H));
  CHECK(require_action_by_text(window, QStringLiteral("Zoom"))->shortcut() == QKeySequence(Qt::Key_Z));
  CHECK(require_action(window, "brushSmallerAction")->shortcut() == QKeySequence(Qt::Key_BracketLeft));
  CHECK(require_action(window, "brushLargerAction")->shortcut() == QKeySequence(Qt::Key_BracketRight));
  CHECK(require_action(window, "brushMuchSmallerAction")->shortcut() ==
        QKeySequence(Qt::SHIFT | Qt::Key_BracketLeft));
  CHECK(require_action(window, "brushMuchLargerAction")->shortcut() ==
        QKeySequence(Qt::SHIFT | Qt::Key_BracketRight));
  const auto tooltip_matches_shortcut = [](QAction* action) {
    const auto shortcut = action->shortcut().toString(QKeySequence::NativeText);
    CHECK(!shortcut.isEmpty());
    CHECK(action->toolTip().contains(action->text().remove('&')));
    CHECK(action->toolTip().contains(shortcut));
  };
  tooltip_matches_shortcut(require_action_by_text(window, QStringLiteral("Move")));
  tooltip_matches_shortcut(require_action_by_text(window, QStringLiteral("Brush")));
  tooltip_matches_shortcut(require_action_by_text(window, QStringLiteral("Smudge")));
  tooltip_matches_shortcut(require_action(window, "toolCycleDetailAction"));
  tooltip_matches_shortcut(require_action(window, "toolDodgeAction"));
  tooltip_matches_shortcut(require_action(window, "toolCycleToningAction"));
  tooltip_matches_shortcut(require_action_by_text(window, QStringLiteral("Clone")));
  tooltip_matches_shortcut(require_action(window, "toolCycleStampAction"));
  tooltip_matches_shortcut(require_action_by_text(window, QStringLiteral("Healing Brush")));
  tooltip_matches_shortcut(require_action(window, "toolCycleHealingAction"));
  tooltip_matches_shortcut(require_action_by_text(window, QStringLiteral("Type")));
  tooltip_matches_shortcut(require_action_by_text(window, QStringLiteral("Cut")));
  tooltip_matches_shortcut(require_action_by_text(window, QStringLiteral("Default Colors")));
  tooltip_matches_shortcut(require_action_by_text(window, QStringLiteral("Swap Colors")));
  tooltip_matches_shortcut(require_action(window, "brushLargerAction"));
  auto* canvas = require_canvas(window);
  auto* blend_combo = window.findChild<QComboBox*>(QStringLiteral("layerBlendModeCombo"));
  CHECK(blend_combo != nullptr);
  CHECK(blend_combo->findText(QStringLiteral("Difference")) >= 0);
  CHECK(blend_combo->findText(QStringLiteral("Color Dodge")) >= 0);
  CHECK(blend_combo->findText(QStringLiteral("Pin Light")) >= 0);
  CHECK(blend_combo->findText(QStringLiteral("Luminosity")) >= 0);
  CHECK(window.findChild<QToolButton*>(QStringLiteral("layerLockTransparentButton")) != nullptr);
  CHECK(window.findChild<QToolButton*>(QStringLiteral("layerLockPixelsButton")) != nullptr);
  CHECK(window.findChild<QToolButton*>(QStringLiteral("layerLockPositionButton")) != nullptr);
  CHECK(window.findChild<QToolButton*>(QStringLiteral("layerLockAllButton")) != nullptr);
  CHECK(require_action(window, "selectionNewModeAction")->isCheckable());
  CHECK(require_action(window, "selectionAddModeAction")->isCheckable());
  CHECK(require_action(window, "selectionSubtractModeAction")->isCheckable());
  CHECK(require_action(window, "selectionIntersectModeAction")->isCheckable());
  auto* brush_size = window.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin"));
  auto* brush_size_slider = window.findChild<QSlider*>(QStringLiteral("brushSizeSlider"));
  auto* brush_opacity = window.findChild<QSpinBox*>(QStringLiteral("brushOpacitySpin"));
  auto* brush_flow = window.findChild<QSpinBox*>(QStringLiteral("brushFlowSpin"));
  auto* brush_airbrush = window.findChild<QCheckBox*>(QStringLiteral("brushAirbrushCheck"));
  auto* brush_softness = window.findChild<QSpinBox*>(QStringLiteral("brushSoftnessSpin"));
  auto* brush_preset = window.findChild<QComboBox*>(QStringLiteral("brushPresetCombo"));
  CHECK(brush_size != nullptr);
  CHECK(brush_size_slider != nullptr);
  CHECK(brush_opacity != nullptr);
  CHECK(window.findChild<QSlider*>(QStringLiteral("brushOpacitySlider")) == nullptr);
  CHECK(brush_flow != nullptr);
  CHECK(brush_airbrush != nullptr);
  CHECK(brush_softness != nullptr);
  CHECK(window.findChild<QSlider*>(QStringLiteral("brushSoftnessSlider")) == nullptr);
  CHECK(brush_preset != nullptr);
  CHECK(brush_size->maximum() == patchy::ui::kMaxBrushSize);
  CHECK(brush_size_slider->maximum() == patchy::ui::kCurvedSliderPositions);
  CHECK(brush_size->buttonSymbols() == QAbstractSpinBox::NoButtons);
  CHECK(brush_opacity->buttonSymbols() == QAbstractSpinBox::NoButtons);
  CHECK(brush_flow->buttonSymbols() == QAbstractSpinBox::NoButtons);
  CHECK(brush_softness->buttonSymbols() == QAbstractSpinBox::NoButtons);
  CHECK(brush_preset->currentData().toString() == QStringLiteral("hard_round"));
  CHECK(brush_size->value() == 25);
  CHECK(brush_opacity->value() == 100);
  CHECK(brush_flow->value() == 100);
  CHECK(!brush_airbrush->isChecked());
  CHECK(brush_softness->value() == 0);
  CHECK(canvas->brush_size() == 25);
  CHECK(canvas->brush_opacity() == 100);
  CHECK(canvas->brush_flow() == 100);
  CHECK(canvas->brush_softness() == 0);
  const auto airbrush_index = brush_preset->findData(QStringLiteral("airbrush"));
  CHECK(airbrush_index >= 0);
  brush_preset->setCurrentIndex(airbrush_index);
  CHECK(brush_size->value() == 56);
  CHECK(brush_opacity->value() == 100);
  CHECK(brush_flow->value() == 12);
  CHECK(brush_softness->value() == 100);
  CHECK(canvas->brush_build_up());
  CHECK(brush_airbrush->isChecked());
  const auto soft_round_index = brush_preset->findData(QStringLiteral("soft_round"));
  CHECK(soft_round_index >= 0);
  brush_preset->setCurrentIndex(soft_round_index);
  CHECK(!canvas->brush_build_up());
  CHECK(brush_flow->value() == 100);
  CHECK(!brush_airbrush->isChecked());
  // Bracket keys resize proportionally (Photoshop-like): the step scales with
  // the current size, so at 20 px the plain step is +2 (10%) and Shift is +6
  // (30%), and grow-then-shrink returns to the same size.
  brush_size->setValue(20);
  CHECK(patchy::ui::slider_value(*brush_size_slider) == 20);
  require_action(window, "brushLargerAction")->trigger();
  CHECK(brush_size->value() == 22);
  CHECK(patchy::ui::slider_value(*brush_size_slider) == 22);
  CHECK(canvas->brush_size() == 22);
  require_action(window, "brushSmallerAction")->trigger();
  CHECK(brush_size->value() == 20);
  require_action(window, "brushMuchLargerAction")->trigger();
  CHECK(brush_size->value() == 26);
  CHECK(canvas->brush_size() == 26);
  require_action(window, "brushMuchSmallerAction")->trigger();
  CHECK(brush_size->value() == 20);
  // Small brushes keep 1-px precision; large brushes jump proportionally.
  brush_size->setValue(5);
  require_action(window, "brushLargerAction")->trigger();
  CHECK(brush_size->value() == 6);
  brush_size->setValue(100);
  require_action(window, "brushLargerAction")->trigger();
  CHECK(brush_size->value() == 110);
  require_action(window, "brushSmallerAction")->trigger();
  CHECK(brush_size->value() == 100);
  require_action(window, "brushMuchLargerAction")->trigger();
  CHECK(brush_size->value() == 130);
  require_action(window, "brushMuchSmallerAction")->trigger();
  CHECK(brush_size->value() == 100);
  brush_size->setValue(patchy::ui::kMaxBrushSize);
  CHECK(patchy::ui::slider_value(*brush_size_slider) == patchy::ui::kMaxBrushSize);
  CHECK(canvas->brush_size() == patchy::ui::kMaxBrushSize);
  require_action(window, "brushLargerAction")->trigger();
  CHECK(brush_size->value() == patchy::ui::kMaxBrushSize);
  brush_size->setValue(patchy::ui::kMaxBrushSize - 5);
  require_action(window, "brushMuchLargerAction")->trigger();
  CHECK(brush_size->value() == patchy::ui::kMaxBrushSize);
  CHECK(canvas->brush_size() == patchy::ui::kMaxBrushSize);
  brush_size->setValue(20);
  brush_opacity->setValue(45);
  CHECK(brush_opacity->value() == 45);
  CHECK(canvas->brush_opacity() == 45);
  brush_softness->setValue(65);
  CHECK(brush_softness->value() == 65);
  CHECK(canvas->brush_softness() == 65);
  auto settings = patchy::ui::app_settings();
  settings.remove(QStringLiteral("tools"));
  settings.sync();
}

// Select All, Clear Selection, Reselect and Inverse sit only in the Select menu,
// keep their hotkey ids and keys, and the keys still fire from the canvas (GitHub issue 493).
void ui_selection_commands_live_only_in_select_menu() {
  patchy::ui::MainWindow window;
  show_window(window);
  QMenu* edit_menu = nullptr;
  QMenu* select_menu = nullptr;
  for (auto* top : window.menuBar()->actions()) {
    const auto title = top->text().remove(QLatin1Char('&'));
    if (title == QStringLiteral("Edit")) {
      edit_menu = top->menu();
    } else if (title == QStringLiteral("Select")) {
      select_menu = top->menu();
    }
  }
  CHECK(edit_menu != nullptr);
  CHECK(select_menu != nullptr);
  if (edit_menu == nullptr || select_menu == nullptr) {
    return;
  }
  for (const auto& [object_name, hotkey_id, key] :
       std::vector<std::tuple<const char*, const char*, QKeySequence>>{
           {"editSelectAllAction", "select.all", QKeySequence(Qt::CTRL | Qt::Key_A)},
           {"editDeselectAction", "select.deselect", QKeySequence(Qt::CTRL | Qt::Key_D)},
           {"selectReselectAction", "select.reselect", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D)},
           {"selectInverseAction", "select.inverse", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I)}}) {
    auto* action = require_action(window, object_name);
    CHECK(require_hotkey_action(window, QLatin1String(hotkey_id)) == action);
    CHECK(action->shortcut() == key);
    CHECK(!edit_menu->actions().contains(action));
    CHECK(select_menu->actions().contains(action));
  }

  auto* canvas = require_canvas(window);
  canvas->setFocus();
  QApplication::processEvents();
  CHECK(!canvas->has_selection());
  QTest::keyClick(canvas, Qt::Key_A, Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(canvas->has_selection());
  QTest::keyClick(canvas, Qt::Key_D, Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(!canvas->has_selection());
  QTest::keyClick(canvas, Qt::Key_D, Qt::ControlModifier | Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(canvas->has_selection());

  // The hotkey editor groups rows by the menu they sit in: these now list under Select.
  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    wait_for_preferences_start_page(*dialog);  // opened on Hotkeys: built once it shows
    for (const auto* id : {"select.all", "select.deselect", "select.reselect", "select.inverse"}) {
      auto* row = dialog->findChild<QWidget*>(QStringLiteral("hotkeyRow.") + QLatin1String(id));
      CHECK(row != nullptr);
      QString category;
      if (auto* panel = row != nullptr ? row->parentWidget() : nullptr; panel != nullptr) {
        for (auto* label : panel->findChildren<QLabel*>(Qt::FindDirectChildrenOnly)) {
          if (label->property("hotkeyCategoryHeader").toBool()) {
            category = label->text();
          }
        }
      }
      CHECK(category == QStringLiteral("Select"));
    }
    saw_dialog = true;
    dialog->reject();
  });
  window.show_hotkey_preferences(QString());
  QApplication::processEvents();
  CHECK(saw_dialog);
}
void ui_brush_flow_popup_slider_updates_canvas() {
  patchy::ui::MainWindow window;
  show_window(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  auto* canvas = require_canvas(window);
  auto* flow = window.findChild<QSpinBox*>(QStringLiteral("brushFlowSpin"));
  auto* popup_action = window.findChild<QAction*>(QStringLiteral("brushFlowPopupAction"));
  CHECK(flow != nullptr);
  CHECK(popup_action != nullptr);
  CHECK(popup_action->icon().isNull());
  flow->setValue(100);
  QApplication::processEvents();
  auto* flow_editor = flow->findChild<QLineEdit*>();
  CHECK(flow_editor != nullptr);
  const auto text_margins = flow_editor->textMargins();
  const auto text_width = flow_editor->fontMetrics().horizontalAdvance(QStringLiteral("100%"));
  CHECK(flow_editor->contentsRect().width() - text_margins.left() -
            text_margins.right() >=
        text_width);
  save_widget_artifact("ui_brush_flow_control", *flow);
  flow->setValue(37);
  QTest::mouseClick(flow_editor, Qt::LeftButton, Qt::NoModifier,
                    QPoint(flow_editor->width() - 2, flow_editor->height() / 2));
  QApplication::processEvents();
  auto* popup = window.findChild<QFrame*>(QStringLiteral("brushFlowPopup"));
  auto* slider = window.findChild<QSlider*>(QStringLiteral("brushFlowPopupSlider"));
  CHECK(popup != nullptr);
  CHECK(popup->isVisible());
  CHECK(slider != nullptr);
  CHECK(slider->value() == 37);
  slider->setValue(22);
  CHECK(flow->value() == 22);
  CHECK(canvas->brush_flow() == 22);
  auto* quick = window.findChild<QPushButton*>(QStringLiteral("brushFlowQuick75"));
  CHECK(quick != nullptr);
  quick->click();
  CHECK(flow->value() == 75);
  CHECK(slider->value() == 75);
  CHECK(canvas->brush_flow() == 75);
  save_widget_artifact("ui_brush_flow_popup", *popup);
  popup->close();
  QApplication::processEvents();
}
// The press that closes an options-bar slider popup also reaches what is under it, so the
// first press on the canvas after picking a value paints, for every slider popup. The
// presses go through Qt's popup handling (QTest on the window handle, not sendEvent to the
// canvas), as a real click outside the popup does. A press on the popup's own chevron
// still only closes it.
void ui_slider_popup_dismissing_press_paints() {
  patchy::ui::MainWindow window;
  show_window(window);
  {
    patchy::Document seed(200, 150, patchy::PixelFormat::rgba8());
    seed.add_layer(patchy::Layer(seed.allocate_layer_id(), "Paper",
                                 solid_pixels(200, 150, patchy::PixelFormat::rgba8(), QColor(255, 255, 255))));
    window.add_document_session(std::move(seed), QStringLiteral("Popup paint"));
  }
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  canvas->set_primary_color(QColor(0, 0, 0));
  canvas->set_brush_size(9);
  QApplication::processEvents();
  auto* handle = window.windowHandle();
  CHECK(handle != nullptr);
  if (handle == nullptr) {
    return;
  }
  const auto at = [&](QPoint document_point) {
    return canvas->mapTo(&window, canvas->widget_position_for_document_point(document_point));
  };
  const auto open_popup = [&window](const QString& base_name) {
    auto* action = window.findChild<QAction*>(base_name + QStringLiteral("PopupAction"));
    CHECK(action != nullptr);
    if (action != nullptr) {
      action->trigger();
      QApplication::processEvents();
    }
    auto* popup = window.findChild<QFrame*>(base_name + QStringLiteral("Popup"));
    CHECK(popup != nullptr && popup->isVisible());
  };
  const auto pixel_at = [&window](QPoint document_point) {
    const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
    const auto* layer =
        document.active_layer_id().has_value() ? document.find_layer(*document.active_layer_id()) : nullptr;
    if (layer == nullptr) {
      return 0;
    }
    const auto& bounds = layer->bounds();
    return static_cast<int>(layer->pixels().pixel(document_point.x() - bounds.x, document_point.y() - bounds.y)[0]);
  };

  int row = 40;
  for (const auto* base_name : {"brushSize", "brushOpacity"}) {
    open_popup(QLatin1String(base_name));
    QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, at({40, row}));
    QApplication::processEvents();
    for (int x = 50; x <= 160; x += 10) {
      QTest::mouseMove(handle, at({x, row}));
      QApplication::processEvents();
    }
    QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, at({160, row}));
    QApplication::processEvents();
    CHECK(window.findChild<QFrame*>(QLatin1String(base_name) + QStringLiteral("Popup")) == nullptr);
    CHECK(pixel_at({100, row}) < 128);
    row += 40;
  }

  // The chevron closes its own popup without reopening it.
  open_popup(QStringLiteral("brushSize"));
  auto* chevron = window.findChild<QWidget*>(QStringLiteral("brushSizePopupButton"));
  CHECK(chevron != nullptr);
  if (chevron != nullptr) {
    const auto chevron_point = chevron->mapTo(&window, chevron->rect().center());
    QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, chevron_point);
    QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, chevron_point);
    QApplication::processEvents();
    const auto* reopened = window.findChild<QFrame*>(QStringLiteral("brushSizePopup"));
    CHECK(reopened == nullptr || !reopened->isVisible());
  }
}
// GitHub issue 293: a quick value picked in a slider popup redraws the canvas cursor while
// the pointer is in the popup, and Qt hands that cursor to the whole main window (Qt's
// qt_qpa_set_cursor applies a widget's cursor to its window when the last widget the pointer
// entered is in another window). Closing the popup gives the window back the cursor of the
// widget under the pointer. On Windows nothing else does: the pointer already crossed back
// over the bar while the popup held the grab, so no enter event follows the closing click.
// The offscreen platform does send the main window an Enter when a popup hides, which would
// reset the cursor by itself, so the test drops that one platform event to behave like Windows.
void ui_slider_popup_close_restores_cursor_under_pointer() {
  patchy::ui::MainWindow window;
  show_window(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  auto* canvas = require_canvas(window);
  canvas->set_brush_size(20);  // a drawn brush cursor, not the large-brush crosshair
  QApplication::processEvents();
  auto* handle = window.windowHandle();
  CHECK(handle != nullptr);
  if (handle == nullptr) {
    return;
  }
  struct DropWindowEnter final : QObject {
    QWindow* window{nullptr};
    bool eventFilter(QObject* watched, QEvent* event) override {
      return watched == window && event->type() == QEvent::Enter;
    }
  };
  for (const auto* base_name : {"brushSoftness", "brushSize"}) {
    auto* spin = window.findChild<QSpinBox*>(QLatin1String(base_name) + QStringLiteral("Spin"));
    CHECK(spin != nullptr);
    if (spin == nullptr) {
      continue;
    }
    // The pointer rests on the field, left of its chevron.
    const auto on_field = spin->mapToGlobal(QPoint(6, spin->height() / 2));
    QCursor::setPos(on_field);
    QApplication::processEvents();
    auto* action = window.findChild<QAction*>(QLatin1String(base_name) + QStringLiteral("PopupAction"));
    CHECK(action != nullptr);
    action->trigger();
    QApplication::processEvents();
    auto* popup = window.findChild<QFrame*>(QLatin1String(base_name) + QStringLiteral("Popup"));
    CHECK(popup != nullptr && popup->isVisible());
    QPushButton* quick = nullptr;
    for (auto* button : popup->findChildren<QPushButton*>()) {
      if (button->objectName().startsWith(QLatin1String(base_name) + QStringLiteral("Quick")) &&
          button->text() != spin->text()) {
        quick = button;
        break;
      }
    }
    CHECK(quick != nullptr && popup->windowHandle() != nullptr);
    // Through the popup's window, as a real pointer: Qt sees it enter the button.
    const auto quick_point = quick->mapTo(popup, quick->rect().center());
    QTest::mouseMove(popup->windowHandle(), quick_point);
    QTest::mouseClick(popup->windowHandle(), Qt::LeftButton, Qt::NoModifier, quick_point);
    QApplication::processEvents();
    CHECK(canvas->cursor().shape() == Qt::BitmapCursor);
    CHECK(handle->cursor().shape() == Qt::BitmapCursor);  // the leak itself
    CHECK(QApplication::widgetAt(QCursor::pos()) != nullptr &&
          QApplication::widgetAt(QCursor::pos())->window() == &window);

    DropWindowEnter drop_enter;
    drop_enter.window = handle;
    qApp->installEventFilter(&drop_enter);
    popup->close();
    QApplication::processEvents();
    qApp->removeEventFilter(&drop_enter);
    CHECK(window.findChild<QFrame*>(QLatin1String(base_name) + QStringLiteral("Popup")) == nullptr);
    CHECK(handle->cursor().shape() != Qt::BitmapCursor);
  }
}
// Esc while typing in an options-bar field puts back the value from before the edit, and
// the tool follows it back (GitHub issue 186); Enter keeps the typed value, after which Esc
// has nothing to undo.
void ui_options_bar_field_escape_reverts_the_edit() {
  patchy::ui::MainWindow window;
  show_window(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  auto* canvas = require_canvas(window);
  auto* size = window.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin"));
  CHECK(size != nullptr);
  if (size == nullptr) {
    return;
  }
  size->setValue(25);
  QApplication::processEvents();
  size->setFocus(Qt::OtherFocusReason);
  QApplication::processEvents();
  size->selectAll();
  QTest::keyClicks(size, QStringLiteral("80"));
  QApplication::processEvents();
  CHECK(size->value() == 80);
  QTest::keyClick(size, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(size->value() == 25);
  CHECK(canvas->brush_size() == 25);
  CHECK(size->text().startsWith(QStringLiteral("25")));

  size->selectAll();
  QTest::keyClicks(size, QStringLiteral("60"));
  QTest::keyClick(size, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(size->value() == 60);
  QTest::keyClick(size, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(size->value() == 60);
  CHECK(canvas->brush_size() == 60);
}
// Counts the Esc presses a widget receives.
class EscapePressCounter final : public QObject {
public:
  int count = 0;

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
      ++count;
    }
    return QObject::eventFilter(watched, event);
  }
};

// Enter in an options-bar field commits the value and Esc puts back the one from before the
// edit; either way the keyboard goes back to the canvas, so tool keys work again, and the Esc
// never reaches the canvas (it would deselect the layer). Tab still walks the bar, and the
// font box drops typed text on Esc (GitHub issue 439).
void ui_options_bar_enter_and_esc_return_the_keyboard_to_the_canvas() {
  patchy::ui::MainWindow window;
  show_window(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  auto* canvas = require_canvas(window);
  auto* size = window.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin"));
  CHECK(size != nullptr);
  if (size == nullptr) {
    return;
  }
  EscapePressCounter canvas_escapes;
  canvas->installEventFilter(&canvas_escapes);
  size->setValue(25);
  const auto focus_field = [](QWidget* field) {
    field->setFocus(Qt::MouseFocusReason);
    QApplication::processEvents();
  };

  focus_field(size);
  CHECK(size->hasFocus());
  size->selectAll();
  QTest::keyClicks(size, QStringLiteral("80"));
  QTest::keyClick(size, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(size->value() == 80);
  CHECK(canvas->brush_size() == 80);
  CHECK(canvas->hasFocus());

  focus_field(size);
  size->selectAll();
  QTest::keyClicks(size, QStringLiteral("40"));
  CHECK(size->value() == 40);
  QTest::keyClick(size, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(size->value() == 80);
  CHECK(canvas->brush_size() == 80);
  CHECK(canvas->hasFocus());

  // Esc with nothing typed leaves the field too, and the keypad Enter commits like Return.
  focus_field(size);
  QTest::keyClick(size, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(canvas->hasFocus());
  focus_field(size);
  size->selectAll();
  QTest::keyClicks(size, QStringLiteral("30"));
  QTest::keyClick(size, Qt::Key_Enter, Qt::KeypadModifier);
  QApplication::processEvents();
  CHECK(size->value() == 30);
  CHECK(canvas->hasFocus());
  CHECK(canvas_escapes.count == 0);

  // Tab moves on through the bar instead of handing the keyboard back.
  focus_field(size);
  QTest::keyClick(size, Qt::Key_Tab);
  QApplication::processEvents();
  auto* options = window.findChild<QWidget*>(QStringLiteral("OptionsContent"));
  CHECK(options != nullptr && QApplication::focusWidget() != nullptr &&
        options->isAncestorOf(QApplication::focusWidget()) && !size->hasFocus());

  // The Text bar: Enter in the size field, Esc in the font box (typed text dropped).
  require_action(window, "toolTypeAction")->trigger();
  QApplication::processEvents();
  auto* text_size = window.findChild<QDoubleSpinBox*>(QStringLiteral("textSizeSpin"));
  auto* font = window.findChild<QComboBox*>(QStringLiteral("textFontCombo"));
  CHECK(text_size != nullptr && font != nullptr);
  if (text_size == nullptr || font == nullptr) {
    canvas->removeEventFilter(&canvas_escapes);
    return;
  }
  focus_field(text_size);
  text_size->selectAll();
  QTest::keyClicks(text_size, QStringLiteral("20"));
  QTest::keyClick(text_size, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(std::abs(text_size->value() - 20.0) < 0.01);
  CHECK(canvas->hasFocus());
  const auto family = font->currentText();
  focus_field(font);
  CHECK(font->lineEdit() != nullptr);
  if (font->lineEdit() != nullptr) {
    font->lineEdit()->selectAll();
    font->lineEdit()->insert(QStringLiteral("qqqz"));
  }
  QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(font->currentText() == family);
  CHECK(canvas->hasFocus());
  CHECK(canvas_escapes.count == 0);
  canvas->removeEventFilter(&canvas_escapes);
}

// Tab onto a slider, a checkbox or a dropdown in the options bar shows the accent outline in
// both schemes; a click gives focus without it, and the outline never changes a control's
// size (GitHub issue 440). The shots are the issue's screenshots.
void ui_options_bar_tab_focus_shows_a_themed_outline() {
  for (const auto scheme : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer restore_scheme(scheme);
    const QString scheme_name = scheme == patchy::ui::ColorSchemePreference::Light ? QStringLiteral("light")
                                                                                   : QStringLiteral("dark");
    patchy::ui::MainWindow window;
    show_window(window);
    window.resize(1700, 738);
    QApplication::processEvents();
    auto* options_bar = window.findChild<QToolBar*>(QStringLiteral("Options"));
    CHECK(options_bar != nullptr);
    if (options_bar == nullptr) {
      return;
    }
    const QColor outline = patchy::ui::theme().accent_border_bright;
    const auto outline_pixels = [&outline](QWidget* widget) {
      const auto image = widget->grab().toImage();
      int count = 0;
      for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
          const QColor pixel = image.pixelColor(x, y);
          count += std::abs(pixel.red() - outline.red()) <= 8 && std::abs(pixel.green() - outline.green()) <= 8 &&
                           std::abs(pixel.blue() - outline.blue()) <= 8
                       ? 1
                       : 0;
        }
      }
      return count;
    };
    struct Case {
      const char* tool;
      const char* name;
      const char* kind;
    };
    for (const Case& item : {Case{"Brush", "brushSizeSlider", "slider"}, Case{"Brush", "brushModeCombo", "dropdown"},
                             Case{"Magic Wand", "wandContiguousCheck", "checkbox"}}) {
      require_action_by_text(window, QString::fromLatin1(item.tool))->trigger();
      QApplication::processEvents();
      auto* control = window.findChild<QWidget*>(QString::fromLatin1(item.name));
      CHECK(control != nullptr && control->isVisible());
      if (control == nullptr || !control->isVisible()) {
        continue;
      }
      const QSize size = control->size();
      const int plain = outline_pixels(control);
      control->setFocus(Qt::TabFocusReason);
      QApplication::processEvents();
      CHECK(control->hasFocus());
      CHECK(control->property("optionsKeyboardFocus").toBool());
      const int focused = outline_pixels(control);
      std::printf("  %s %s: %d outline pixels unfocused, %d with Tab focus\n", qPrintable(scheme_name), item.name,
                  plain, focused);
      CHECK(focused >= plain + 8);
      CHECK(control->size() == size);
      CHECK(options_bar->grab().save(
          QStringLiteral("test-artifacts/issue_440_%1_%2.png").arg(scheme_name, QString::fromLatin1(item.kind))));
      // A click focuses without the mark.
      control->clearFocus();
      control->setFocus(Qt::MouseFocusReason);
      QApplication::processEvents();
      CHECK(control->hasFocus());
      CHECK(!control->property("optionsKeyboardFocus").toBool());
      CHECK(outline_pixels(control) == plain);
      CHECK(control->size() == size);
      control->clearFocus();
      QApplication::processEvents();
    }
  }
}
// Tab and Shift+Tab stay inside the options bar: from the last control Tab wraps to the first,
// from the first Shift+Tab wraps to the last, every stop on the way is a bar control in reading
// order with the keyboard outline, and the keyboard never walks on into the Layers panel or the
// document tabs (GitHub issue 439 follow-up).
void ui_options_bar_tab_wraps_within_the_bar() {
  auto&& [window, canvas] = ShownWindowFixture{};
  window.resize(1366, 738);
  QApplication::processEvents();
  auto* options = window.findChild<QWidget*>(QStringLiteral("OptionsContent"));
  CHECK(options != nullptr);
  if (options == nullptr) {
    return;
  }
  const auto in_bar = [options](QWidget* widget) { return widget != nullptr && options->isAncestorOf(widget); };
  for (const char* tool : {"Brush", "Magic Wand"}) {
    require_action_by_text(window, QString::fromLatin1(tool))->trigger();
    QApplication::processEvents();
    // Start on the canvas side of the bar: the first field, focused by a click.
    QWidget* start = nullptr;
    for (auto* spin : options->findChildren<QAbstractSpinBox*>()) {
      if (spin->isVisible() && spin->isEnabled() &&
          (start == nullptr || spin->mapTo(options, QPoint()).x() < start->mapTo(options, QPoint()).x())) {
        start = spin;
      }
    }
    CHECK(start != nullptr);
    if (start == nullptr) {
      continue;
    }
    start->setFocus(Qt::MouseFocusReason);
    QApplication::processEvents();
    // Tab round the bar until it comes back to the start; the stops seen are the cycle.
    std::vector<QWidget*> cycle{start};
    bool left_bar = false;
    for (int press = 0; press < 80; ++press) {
      QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab);
      QApplication::processEvents();
      auto* focus = QApplication::focusWidget();
      if (!in_bar(focus)) {
        left_bar = true;
        break;
      }
      if (auto* parent = focus->parentWidget();
          qobject_cast<QLineEdit*>(focus) != nullptr &&
          (qobject_cast<QAbstractSpinBox*>(parent) != nullptr || qobject_cast<QComboBox*>(parent) != nullptr)) {
        focus = parent;
      }
      CHECK(focus->property("optionsKeyboardFocus").toBool());
      if (focus == start) {
        break;
      }
      cycle.push_back(focus);
    }
    CHECK(!left_bar);
    CHECK(QApplication::focusWidget() != nullptr && in_bar(QApplication::focusWidget()));
    CHECK(cycle.size() >= 4);
    std::printf("  %s: %d Tab stops\n", tool, static_cast<int>(cycle.size()));
    // Reading order: the first stop is the leftmost on the top row, the last the rightmost.
    std::vector<QWidget*> ordered = cycle;
    const auto first_it = std::min_element(ordered.begin(), ordered.end(), [options](QWidget* a, QWidget* b) {
      const auto pa = a->mapTo(options, a->rect().center());
      const auto pb = b->mapTo(options, b->rect().center());
      return std::abs(pa.y() - pb.y()) > 8 ? pa.y() < pb.y() : pa.x() < pb.x();
    });
    std::rotate(ordered.begin(), first_it, ordered.end());
    for (std::size_t i = 1; i < ordered.size(); ++i) {
      const auto previous = ordered[i - 1]->mapTo(options, ordered[i - 1]->rect().center());
      const auto current = ordered[i]->mapTo(options, ordered[i]->rect().center());
      CHECK(current.y() - previous.y() > 8 || (std::abs(current.y() - previous.y()) <= 8 && current.x() > previous.x()));
    }
    QWidget* first = ordered.front();
    QWidget* last = ordered.back();
    // Tab on the last control lands on the first.
    last->setFocus(Qt::TabFocusReason);
    QApplication::processEvents();
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab);
    QApplication::processEvents();
    CHECK(first->hasFocus());
    CHECK(first->property("optionsKeyboardFocus").toBool());
    // Shift+Tab on the first lands on the last.
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Backtab, Qt::ShiftModifier);
    QApplication::processEvents();
    CHECK(last->hasFocus());
    CHECK(last->property("optionsKeyboardFocus").toBool());
    // Esc still hands the keyboard back to the canvas.
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
    QApplication::processEvents();
    CHECK(canvas->hasFocus());
  }
}
// An exclusive button group in the options bar (the selection modes, Zoom In/Out, the Pick
// tool's Sets, text alignment) is one Tab stop, its checked button, as a radio group is. The
// arrows move to the neighbour and select it (wrapping), Space and Enter select the focused
// button, a click focuses a button so Tab moves on from there, and Space after a click still
// pans the canvas instead of pressing the button (GitHub issue 440 follow-up).
void ui_options_bar_button_groups_are_one_tab_stop() {
  auto&& [window, canvas] = ShownWindowFixture{};
  window.resize(1700, 738);
  QApplication::processEvents();
  auto* options = window.findChild<QWidget*>(QStringLiteral("OptionsContent"));
  CHECK(options != nullptr);
  if (options == nullptr) {
    return;
  }
  const auto button_for = [options](const char* action_name) -> QToolButton* {
    for (auto* button : options->findChildren<QToolButton*>()) {
      if (button->defaultAction() != nullptr && button->defaultAction()->objectName() == QLatin1String(action_name)) {
        return button;
      }
    }
    return nullptr;
  };
  const auto settle = [] { QApplication::processEvents(); };
  const auto press = [&settle](Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QTest::keyClick(QApplication::focusWidget(), key, modifiers);
    settle();
  };

  // Every group: click a button, then Tab once round the bar. The group comes up once, on its
  // checked button, and the click left no outline while Tab shows it.
  struct GroupCase {
    const char* tool;
    std::vector<QString> members;
  };
  const std::vector<GroupCase> groups = {
      {"Marquee",
       {QStringLiteral("selectionNewModeAction"), QStringLiteral("selectionAddModeAction"),
        QStringLiteral("selectionSubtractModeAction"), QStringLiteral("selectionIntersectModeAction")}},
      {"Zoom", {QStringLiteral("zoomInModeAction"), QStringLiteral("zoomOutModeAction")}},
      {"Pick", {QStringLiteral("eyedropperSetsForegroundButton"), QStringLiteral("eyedropperSetsBackgroundButton")}},
      {"Horizontal Type",
       {QStringLiteral("textAlignLeftButton"), QStringLiteral("textAlignCenterButton"),
        QStringLiteral("textAlignRightButton")}},
  };
  for (const auto& item : groups) {
    if (QString::fromLatin1(item.tool) == QStringLiteral("Horizontal Type")) {
      require_action(window, "toolTypeAction")->trigger();
    } else {
      require_action_by_text(window, QString::fromLatin1(item.tool))->trigger();
    }
    settle();
    std::vector<QAbstractButton*> members;
    for (const auto& name : item.members) {
      QAbstractButton* button = button_for(qPrintable(name));
      if (button == nullptr) {
        button = options->findChild<QAbstractButton*>(name);
      }
      CHECK(button != nullptr && button->isVisible());
      if (button == nullptr || !button->isVisible()) {
        return;
      }
      members.push_back(button);
    }
    auto* clicked = members.back();
    QTest::mouseClick(clicked, Qt::LeftButton);
    settle();
    CHECK(clicked->hasFocus());
    CHECK(!clicked->property("optionsKeyboardFocus").toBool());
    // The group's stop is its checked button: the clicked one, except text alignment with no
    // text layer to align, which stays on Left.
    QAbstractButton* checked = nullptr;
    for (auto* member : members) {
      checked = member->isChecked() ? member : checked;
    }
    CHECK(checked != nullptr);
    int group_stops = 0;
    int presses = 0;
    for (; presses < 80; ++presses) {
      press(Qt::Key_Tab);
      auto* focus = QApplication::focusWidget();
      CHECK(focus != nullptr && options->isAncestorOf(focus));
      if (focus == nullptr || !options->isAncestorOf(focus)) {
        break;
      }
      if (std::find(members.begin(), members.end(), focus) != members.end()) {
        ++group_stops;
        CHECK(focus == checked);
        CHECK(focus->property("optionsKeyboardFocus").toBool());
        break;
      }
    }
    std::printf("  %s: back on the group after %d Tab presses\n", item.tool, presses + 1);
    CHECK(group_stops == 1);
    CHECK(presses >= 1);
    press(Qt::Key_Escape);
    CHECK(canvas->hasFocus());
  }

  // The Marquee modes from the keyboard.
  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  settle();
  auto* new_button = button_for("selectionNewModeAction");
  auto* add_button = button_for("selectionAddModeAction");
  auto* subtract_button = button_for("selectionSubtractModeAction");
  auto* intersect_button = button_for("selectionIntersectModeAction");
  CHECK(new_button != nullptr && add_button != nullptr && subtract_button != nullptr && intersect_button != nullptr);
  if (new_button == nullptr || add_button == nullptr || subtract_button == nullptr || intersect_button == nullptr) {
    return;
  }
  new_button->defaultAction()->trigger();
  settle();
  new_button->setFocus(Qt::TabFocusReason);
  settle();
  press(Qt::Key_Right);
  CHECK(add_button->hasFocus() && add_button->isChecked());
  CHECK(add_button->property("optionsKeyboardFocus").toBool());
  CHECK(canvas->selection_mode() == patchy::ui::CanvasWidget::SelectionMode::Add);
  if (auto* options_bar = window.findChild<QToolBar*>(QStringLiteral("Options")); options_bar != nullptr) {
    CHECK(options_bar->grab().save(QStringLiteral("test-artifacts/issue_440_mode_group_focus.png")));
  }
  press(Qt::Key_Left);
  press(Qt::Key_Left);
  CHECK(intersect_button->hasFocus() && intersect_button->isChecked());
  press(Qt::Key_Right);
  CHECK(new_button->hasFocus() && new_button->isChecked());
  // Space and Enter select the focused button.
  subtract_button->setFocus(Qt::TabFocusReason);
  settle();
  press(Qt::Key_Space);
  CHECK(subtract_button->isChecked());
  CHECK(canvas->selection_mode() == patchy::ui::CanvasWidget::SelectionMode::Subtract);
  add_button->setFocus(Qt::TabFocusReason);
  settle();
  press(Qt::Key_Return);
  CHECK(add_button->isChecked());
  // Tab and Shift+Tab leave the group in one step each.
  press(Qt::Key_Tab);
  auto* after_group = QApplication::focusWidget();
  CHECK(after_group != nullptr && after_group != new_button && after_group != subtract_button &&
        after_group != intersect_button && after_group != add_button);
  press(Qt::Key_Backtab, Qt::ShiftModifier);
  CHECK(add_button->hasFocus());
  // After a click, Space pans the canvas instead of pressing the focused button.
  QTest::mouseClick(intersect_button, Qt::LeftButton);
  settle();
  CHECK(intersect_button->hasFocus() && intersect_button->isChecked());
  new_button->defaultAction()->trigger();
  settle();
  CHECK(intersect_button->hasFocus());
  press(Qt::Key_Space);
  CHECK(!intersect_button->isChecked());
  CHECK(new_button->isChecked());
  // Click then Tab moves on from the clicked group with the outline.
  QTest::mouseClick(subtract_button, Qt::LeftButton);
  settle();
  press(Qt::Key_Tab);
  CHECK(QApplication::focusWidget() == after_group);
  CHECK(after_group->property("optionsKeyboardFocus").toBool());
  press(Qt::Key_Escape);
  CHECK(canvas->hasFocus());
}
// Size-like sliders use SliderCurve::FineLowEnd: the value grows with the
// square of the handle position, so small sizes get most of the track.
void ui_size_sliders_give_the_low_end_most_of_the_track() {
  using patchy::ui::kCurvedSliderPositions;
  CHECK(patchy::ui::curved_slider_value(0, 1.0, 1024.0) == 1.0);
  CHECK(patchy::ui::curved_slider_value(kCurvedSliderPositions, 1.0, 1024.0) == 1024.0);
  CHECK(patchy::ui::curved_slider_value(kCurvedSliderPositions / 2, 0.0, 1000.0) == 250.0);
  CHECK(patchy::ui::curved_slider_position(250.0, 0.0, 1000.0) == kCurvedSliderPositions / 2);
  CHECK(patchy::ui::curved_slider_position(-5.0, 0.0, 1000.0) == 0);
  CHECK(patchy::ui::curved_slider_position(5000.0, 0.0, 1000.0) == kCurvedSliderPositions);
  patchy::ui::MainWindow window;
  show_window(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  auto* canvas = require_canvas(window);
  auto* brush_size = window.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin"));
  auto* slider = window.findChild<QSlider*>(QStringLiteral("brushSizeSlider"));
  CHECK(brush_size != nullptr && slider != nullptr);
  CHECK(slider->minimum() == 0 && slider->maximum() == kCurvedSliderPositions);
  // Half the track is a quarter of 1..1024, and the first quarter of the track
  // covers 1..65.
  slider->setValue(kCurvedSliderPositions / 2);
  CHECK(brush_size->value() == 257);
  CHECK(canvas->brush_size() == 257);
  slider->setValue(kCurvedSliderPositions / 4);
  CHECK(brush_size->value() == 65);
  // Typed values move the handle to the matching position.
  brush_size->setValue(20);
  CHECK(patchy::ui::slider_value(*slider) == 20);
  brush_size->setValue(patchy::ui::kMaxBrushSize);
  CHECK(slider->value() == kCurvedSliderPositions);
  // A drag keeps the handle under the mouse instead of snapping it to the
  // value's canonical position (301 and 302 both show 94).
  slider->setSliderDown(true);
  slider->setSliderPosition(301);
  CHECK(brush_size->value() == 94);
  CHECK(slider->value() == 301);
  slider->setSliderDown(false);
  // Keyboard and wheel steps change the value by one unit even where one
  // position is far less than one pixel of brush.
  brush_size->setValue(1);
  CHECK(slider->value() == 0);
  slider->triggerAction(QAbstractSlider::SliderSingleStepAdd);
  CHECK(brush_size->value() == 2);
  slider->triggerAction(QAbstractSlider::SliderSingleStepAdd);
  CHECK(brush_size->value() == 3);
  slider->triggerAction(QAbstractSlider::SliderSingleStepSub);
  CHECK(brush_size->value() == 2);
  const QPointF center(slider->width() / 2.0, slider->height() / 2.0);
  QWheelEvent wheel(center, slider->mapToGlobal(center), QPoint(), QPoint(0, 120), Qt::NoButton,
                    Qt::NoModifier, Qt::NoScrollPhase, false);
  QApplication::sendEvent(slider, &wheel);
  CHECK(std::abs(brush_size->value() - 2) == 1);
  const auto open_popup_slider = [&window](const QString& base_name) -> QSlider* {
    auto* action = window.findChild<QAction*>(base_name + QStringLiteral("PopupAction"));
    CHECK(action != nullptr);
    action->trigger();
    QApplication::processEvents();
    auto* popup_slider = window.findChild<QSlider*>(base_name + QStringLiteral("PopupSlider"));
    CHECK(popup_slider != nullptr);
    return popup_slider;
  };
  const auto close_popup = [&window](const QString& base_name) {
    auto* popup = window.findChild<QFrame*>(base_name + QStringLiteral("Popup"));
    CHECK(popup != nullptr);
    popup->close();
    QApplication::processEvents();
  };
  // The brush size popup shares the curve and keeps the options-bar slider in step.
  auto* size_popup_slider = open_popup_slider(QStringLiteral("brushSize"));
  CHECK(size_popup_slider->maximum() == kCurvedSliderPositions);
  size_popup_slider->setValue(kCurvedSliderPositions / 2);
  CHECK(brush_size->value() == 257);
  CHECK(slider->value() == kCurvedSliderPositions / 2);
  close_popup(QStringLiteral("brushSize"));
  auto* feather = window.findChild<QSpinBox*>(QStringLiteral("selectionFeatherSpin"));
  CHECK(feather != nullptr);
  auto* feather_slider = open_popup_slider(QStringLiteral("selectionFeather"));
  CHECK(feather_slider->maximum() == kCurvedSliderPositions);
  feather_slider->setValue(kCurvedSliderPositions / 2);
  CHECK(feather->value() == 250);
  patchy::ui::set_slider_to_value(*feather_slider, 12);
  CHECK(feather->value() == 12);
  close_popup(QStringLiteral("selectionFeather"));
  auto* line_weight = window.findChild<QDoubleSpinBox*>(QStringLiteral("vectorLineWeightSpin"));
  CHECK(line_weight != nullptr);
  auto* line_weight_slider = open_popup_slider(QStringLiteral("vectorLineWeight"));
  CHECK(line_weight_slider->maximum() == kCurvedSliderPositions);
  line_weight_slider->setValue(kCurvedSliderPositions / 2);
  CHECK(line_weight->value() == 251);
  close_popup(QStringLiteral("vectorLineWeight"));
  // Percent popups stay linear.
  auto* opacity_slider = open_popup_slider(QStringLiteral("brushOpacity"));
  CHECK(opacity_slider->maximum() == 100);
  close_popup(QStringLiteral("brushOpacity"));
  auto* quick_select_size = window.findChild<QSpinBox*>(QStringLiteral("quickSelectSizeSpin"));
  auto* quick_select_slider = window.findChild<QSlider*>(QStringLiteral("quickSelectSizeSlider"));
  CHECK(quick_select_size != nullptr && quick_select_slider != nullptr);
  CHECK(quick_select_slider->maximum() == kCurvedSliderPositions);
  quick_select_slider->setValue(kCurvedSliderPositions / 2);
  CHECK(quick_select_size->value() == 129);
}
// Snapshots and restores the whole "hotkeys" settings group so hotkey tests
// cannot leak overrides into each other or into a developer's settings.
class HotkeySettingsGroupRestorer {
public:
  HotkeySettingsGroupRestorer() {
    auto settings = patchy::ui::app_settings();
    settings.beginGroup(QStringLiteral("hotkeys"));
    const auto keys = settings.childKeys();
    for (const auto& key : keys) {
      saved_.insert(key, settings.value(key));
    }
    settings.endGroup();
  }
  ~HotkeySettingsGroupRestorer() {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("hotkeys"));
    settings.beginGroup(QStringLiteral("hotkeys"));
    for (auto it = saved_.constBegin(); it != saved_.constEnd(); ++it) {
      settings.setValue(it.key(), it.value());
    }
    settings.endGroup();
    settings.sync();
  }
private:
  QHash<QString, QVariant> saved_;
};
void clear_hotkey_overrides() {
  auto settings = patchy::ui::app_settings();
  settings.remove(QStringLiteral("hotkeys"));
  settings.sync();
}
void ui_hotkey_resolution_rules() {
  using patchy::ui::HotkeyCommand;
  using patchy::ui::HotkeyOverrideMap;
  using patchy::ui::resolve_hotkey_assignments;
  std::vector<HotkeyCommand> commands;
  commands.push_back({QStringLiteral("first"), QString(), nullptr, {QKeySequence(QStringLiteral("Ctrl+K"))}});
  commands.push_back({QStringLiteral("second"), QString(), nullptr, {QKeySequence(QStringLiteral("Ctrl+L"))}});
  commands.push_back({QStringLiteral("late_duplicate"), QString(), nullptr, {QKeySequence(QStringLiteral("Ctrl+K"))}});
  {
    const auto resolved = resolve_hotkey_assignments(commands, {});
    CHECK(resolved.effective.value(QStringLiteral("first")) ==
          QList<QKeySequence>{QKeySequence(QStringLiteral("Ctrl+K"))});
    CHECK(resolved.effective.value(QStringLiteral("second")) ==
          QList<QKeySequence>{QKeySequence(QStringLiteral("Ctrl+L"))});
    // Duplicate defaults resolve deterministically: first registered wins.
    CHECK(resolved.effective.value(QStringLiteral("late_duplicate")).isEmpty());
    CHECK(resolved.suppressions.size() == 1);
    CHECK(resolved.suppressions[0].id == QStringLiteral("late_duplicate"));
    CHECK(resolved.suppressions[0].winner_id == QStringLiteral("first"));
  }
  {
    // A user override beats another command's default, and the loser is only
    // suppressed at resolution time, never rewritten.
    HotkeyOverrideMap overrides;
    overrides.insert(QStringLiteral("second"), {QKeySequence(QStringLiteral("Ctrl+K"))});
    const auto resolved = resolve_hotkey_assignments(commands, overrides);
    CHECK(resolved.effective.value(QStringLiteral("second")) ==
          QList<QKeySequence>{QKeySequence(QStringLiteral("Ctrl+K"))});
    CHECK(resolved.effective.value(QStringLiteral("first")).isEmpty());
    bool first_suppressed_by_second = false;
    for (const auto& suppression : resolved.suppressions) {
      if (suppression.id == QStringLiteral("first") && suppression.winner_id == QStringLiteral("second")) {
        first_suppressed_by_second = true;
      }
    }
    CHECK(first_suppressed_by_second);
  }
  {
    // An empty override means explicitly unbound.
    HotkeyOverrideMap overrides;
    overrides.insert(QStringLiteral("first"), {});
    const auto resolved = resolve_hotkey_assignments(commands, overrides);
    CHECK(resolved.effective.value(QStringLiteral("first")).isEmpty());
    // With Ctrl+K free again, the late duplicate default gets it.
    CHECK(resolved.effective.value(QStringLiteral("late_duplicate")) ==
          QList<QKeySequence>{QKeySequence(QStringLiteral("Ctrl+K"))});
  }
  {
    // Storage round-trips sequences whose text contains separators (Ctrl+;).
    const QList<QKeySequence> shortcuts{QKeySequence(QStringLiteral("Ctrl+;")),
                                        QKeySequence(QStringLiteral("Ctrl+Shift+Z"))};
    const auto stored = patchy::ui::hotkey_shortcuts_to_storage(shortcuts);
    CHECK(patchy::ui::hotkey_shortcuts_from_storage(stored) == shortcuts);
    CHECK(patchy::ui::hotkey_shortcuts_from_storage(QString()).isEmpty());
    CHECK(patchy::ui::hotkey_shortcuts_to_storage({}).isEmpty());
  }
}
void ui_hotkey_defaults_have_no_conflicts() {
  HotkeySettingsGroupRestorer restore_hotkeys;
  clear_hotkey_overrides();
  patchy::ui::MainWindow window;
  const auto& registry = window.hotkey_registry();
  CHECK(registry.commands().size() > 80);
  QSet<QString> ids;
  for (const auto& command : registry.commands()) {
    CHECK(!command.id.isEmpty());
    CHECK(!ids.contains(command.id));
    ids.insert(command.id);
  }
  // No two default shortcuts may collide: Qt treats ambiguous application
  // shortcuts as dead keys (this regressed once via Desaturate vs Line).
  const auto resolved = patchy::ui::resolve_hotkey_assignments(registry.commands(), {});
  for (const auto& suppression : resolved.suppressions) {
    std::fprintf(stderr, "duplicate default shortcut: %s on %s and %s\n",
                 suppression.sequence.toString(QKeySequence::PortableText).toUtf8().constData(),
                 suppression.id.toUtf8().constData(), suppression.winner_id.toUtf8().constData());
  }
  CHECK(resolved.suppressions.empty());
}
// A saved override that puts Shift+<letter> back on a flyout member keeps the
// old direct binding: overrides win the sequence and the cycle default is
// suppressed rather than left ambiguous.
void ui_tool_cycle_yields_to_member_override() {
  HotkeySettingsGroupRestorer restore_hotkeys;
  clear_hotkey_overrides();
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("hotkeys/tools.fill"), QStringLiteral("Shift+G"));
    settings.sync();
  }
  patchy::ui::MainWindow window;
  CHECK(require_action(window, "toolFillAction")->shortcut() == QKeySequence(Qt::SHIFT | Qt::Key_G));
  CHECK(require_action(window, "toolCycleFillAction")->shortcut().isEmpty());
  const auto resolved = window.hotkey_registry().resolution();
  bool suppressed = false;
  for (const auto& suppression : resolved.suppressions) {
    suppressed = suppressed || (suppression.id == QStringLiteral("tools.cycle.gradient") &&
                                suppression.winner_id == QStringLiteral("tools.fill"));
  }
  CHECK(suppressed);
}
void ui_hotkey_override_applies_at_startup() {
  HotkeySettingsGroupRestorer restore_hotkeys;
  clear_hotkey_overrides();
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("hotkeys/file.new"), QStringLiteral("Ctrl+F9"));
    settings.sync();
  }
  {
    patchy::ui::MainWindow window;
    auto* new_action = require_action(window, "fileNewAction");
    CHECK(new_action->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_F9));
    CHECK(new_action->toolTip().contains(
        QKeySequence(Qt::CTRL | Qt::Key_F9).toString(QKeySequence::NativeText)));
  }
  clear_hotkey_overrides();
  {
    // Unmodified commands track the shipped defaults again.
    patchy::ui::MainWindow window;
    CHECK(require_action(window, "fileNewAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_N));
    const auto* undo_command = window.hotkey_registry().find_command(QStringLiteral("edit.undo"));
    CHECK(undo_command != nullptr);
    CHECK(undo_command->action != nullptr);
    CHECK(undo_command->action->shortcuts().size() == 2);
  }
}
// GitHub issues 275 and 294 moved defaults, never ids: New Layer (layer.new) lost Ctrl+Shift+N
// and has no default key, Merge Visible to New Layer (layer.merge_visible) took Ctrl+Alt+Shift+E,
// and their old keys went to the new layer.new_with_dialog and layer.merge_visible_in_place. A user who saved the old key back
// on an old command keeps it; the new command's default yields instead of colliding.
void ui_new_layer_and_merge_visible_keys_moved_defaults_only() {
  HotkeySettingsGroupRestorer restore_hotkeys;
  clear_hotkey_overrides();
  {
    patchy::ui::MainWindow window;
    const auto& registry = window.hotkey_registry();
    const auto default_of = [&registry](const char* id) {
      const auto* command = registry.find_command(QLatin1String(id));
      return command != nullptr && !command->default_shortcuts.isEmpty() ? command->default_shortcuts.front()
                                                                         : QKeySequence();
    };
    CHECK(require_hotkey_action(window, QStringLiteral("layer.new")) == require_action(window, "layerNewAction"));
    CHECK(require_hotkey_action(window, QStringLiteral("layer.merge_visible")) ==
          require_action(window, "layerMergeVisibleAction"));
    CHECK(default_of("layer.new").isEmpty());
    CHECK(default_of("layer.new_with_dialog") == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
    CHECK(default_of("layer.merge_visible") == QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_E));
    CHECK(default_of("layer.merge_visible_in_place") == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
    CHECK(default_of("file.open_clipboard") == QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_N));
  }
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("hotkeys/layer.new"), QStringLiteral("Ctrl+Shift+N"));
    settings.setValue(QStringLiteral("hotkeys/layer.merge_visible"), QStringLiteral("Ctrl+Shift+E"));
    settings.sync();
  }
  patchy::ui::MainWindow window;
  CHECK(require_action(window, "layerNewAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
  CHECK(require_action(window, "layerNewWithDialogAction")->shortcut().isEmpty());
  CHECK(require_action(window, "layerMergeVisibleAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
  CHECK(require_action(window, "layerMergeVisibleLayersAction")->shortcut().isEmpty());
  int yielded = 0;
  for (const auto& suppression : window.hotkey_registry().resolution().suppressions) {
    yielded += (suppression.id == QStringLiteral("layer.new_with_dialog") &&
                suppression.winner_id == QStringLiteral("layer.new")) ||
               (suppression.id == QStringLiteral("layer.merge_visible_in_place") &&
                suppression.winner_id == QStringLiteral("layer.merge_visible"));
  }
  CHECK(yielded == 2);
}
// The Hotkeys page is not the last Preferences page everywhere (Windows appends
// Plug-ins after it), and its editor panel is built on the first visit, so
// select the page by its title and let the panel appear.
void select_hotkeys_tab(QDialog& dialog) {
  CHECK(select_preferences_page(dialog, QStringLiteral("Hotkeys")));
  CHECK(dialog.findChild<QWidget*>(QStringLiteral("hotkeyEditorPanel")) != nullptr);
}
// Opening Preferences must not build the hotkey rows (the most expensive part
// of the dialog, and most opens never visit that tab); the first visit builds
// them once, and accepting the dialog without a visit still succeeds.
void ui_preferences_builds_hotkey_editor_on_first_visit() {
  HotkeySettingsGroupRestorer restore_hotkeys;
  clear_hotkey_overrides();
  auto&& [window, canvas] = ShownWindowFixture{};
  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    CHECK(preferences_page_list(*dialog) != nullptr);
    CHECK(dialog->findChild<QWidget*>(QStringLiteral("hotkeyEditorPanel")) == nullptr);
    select_hotkeys_tab(*dialog);
    auto* panel = dialog->findChild<QWidget*>(QStringLiteral("hotkeyEditorPanel"));
    CHECK(panel != nullptr);
    CHECK(dialog->findChild<QPushButton*>(QStringLiteral("hotkeyChip.file.new.0")) != nullptr);
    // Leaving and returning reuses the same panel.
    CHECK(select_preferences_page(*dialog, QStringLiteral("Application")));
    select_hotkeys_tab(*dialog);
    CHECK(dialog->findChild<QWidget*>(QStringLiteral("hotkeyEditorPanel")) == panel);
    saw_dialog = true;
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
  // A second open that never visits the page accepts cleanly with no panel. The first
  // closed on Hotkeys, which the dialog would reopen on (GitHub issue 451).
  CHECK(patchy::ui::stored_preferences_last_page() == QStringLiteral("hotkeys"));
  patchy::ui::set_stored_preferences_last_page(QStringLiteral("application"));
  saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    CHECK(dialog->findChild<QWidget*>(QStringLiteral("hotkeyEditorPanel")) == nullptr);
    saw_dialog = true;
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
}
void ui_hotkey_editor_assigns_and_persists_custom_shortcut() {
  HotkeySettingsGroupRestorer restore_hotkeys;
  clear_hotkey_overrides();
  auto&& [window, canvas] = ShownWindowFixture{};
  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    select_hotkeys_tab(*dialog);
    QApplication::processEvents();
    CHECK(dialog->findChild<QWidget*>(QStringLiteral("hotkeyEditorPanel")) != nullptr);
    save_widget_artifact("hotkey_editor_tab", *dialog);
    auto* chip = dialog->findChild<QPushButton*>(QStringLiteral("hotkeyChip.file.new.0"));
    CHECK(chip != nullptr);
    CHECK(chip->text() == QKeySequence(Qt::CTRL | Qt::Key_N).toString(QKeySequence::NativeText));
    chip->click();
    QApplication::processEvents();
    auto* capture = dialog->findChild<QLineEdit*>(QStringLiteral("hotkeyCaptureEdit"));
    CHECK(capture != nullptr);
    QKeyEvent press(QEvent::KeyPress, Qt::Key_F9, Qt::ControlModifier);
    QApplication::sendEvent(capture, &press);
    QApplication::processEvents();
    QApplication::processEvents();
    // The row is rebuilt with the staged shortcut, a modified marker, and a
    // visible per-row reset button.
    auto* updated_chip = dialog->findChild<QPushButton*>(QStringLiteral("hotkeyChip.file.new.0"));
    CHECK(updated_chip != nullptr);
    CHECK(updated_chip->text() == QKeySequence(Qt::CTRL | Qt::Key_F9).toString(QKeySequence::NativeText));
    auto* reset_button = dialog->findChild<QToolButton*>(QStringLiteral("hotkeyRowReset.file.new"));
    CHECK(reset_button != nullptr);
    CHECK(reset_button->isVisible());
    // Filtering by shortcut text keeps the matching row visible.
    auto* search = dialog->findChild<QLineEdit*>(QStringLiteral("hotkeySearchEdit"));
    CHECK(search != nullptr);
    search->setText(QStringLiteral("ctrl+f9"));
    QApplication::processEvents();
    auto* row = dialog->findChild<QWidget*>(QStringLiteral("hotkeyRow.file.new"));
    CHECK(row != nullptr);
    CHECK(row->isVisible());
    auto* other_row = dialog->findChild<QWidget*>(QStringLiteral("hotkeyRow.file.open"));
    CHECK(other_row != nullptr);
    CHECK(!other_row->isVisible());
    search->clear();
    QApplication::processEvents();
    saw_dialog = true;
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
  auto settings = patchy::ui::app_settings();
  settings.beginGroup(QStringLiteral("hotkeys"));
  const auto keys = settings.childKeys();
  const auto stored = settings.value(QStringLiteral("file.new")).toString();
  settings.endGroup();
  CHECK(keys == QStringList{QStringLiteral("file.new")});
  CHECK(stored == QKeySequence(Qt::CTRL | Qt::Key_F9).toString(QKeySequence::PortableText));
  CHECK(require_action(window, "fileNewAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_F9));
}
// Scripts get their own category in the editor whether or not File > Scripts was ever
// opened, and the Script Manager's "Assign Hotkey..." entry point lands on the row.
void ui_hotkey_editor_lists_scripts_category() {
  HotkeySettingsGroupRestorer restore_hotkeys;
  clear_hotkey_overrides();
  auto&& [window, canvas] = ShownWindowFixture{};
  const auto pong_id = patchy::ui::script_hotkey_command_id(QStringLiteral("Games/pong.js"));
  CHECK(window.hotkey_registry().find_command(pong_id) != nullptr);
  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* list = preferences_page_list(*dialog);
    CHECK(list != nullptr && list->currentItem() != nullptr);
    CHECK(list->currentItem()->text() == QStringLiteral("Hotkeys"));
    wait_for_preferences_start_page(*dialog);  // opened on Hotkeys: built once it shows
    CHECK(dialog->findChild<QWidget*>(QStringLiteral("hotkeyEditorPanel")) != nullptr);
    auto* search = dialog->findChild<QLineEdit*>(QStringLiteral("hotkeySearchEdit"));
    CHECK(search != nullptr);
    CHECK(search->text() == QStringLiteral("Pong"));
    auto* row = dialog->findChild<QWidget*>(QStringLiteral("hotkeyRow.") + pong_id);
    CHECK(row != nullptr);
    CHECK(row->isVisible());
    auto* other_row = dialog->findChild<QWidget*>(QStringLiteral("hotkeyRow.file.new"));
    CHECK(other_row != nullptr);
    CHECK(!other_row->isVisible());
    // The row sits in the Scripts category panel, not under File > Scripts.
    bool in_scripts_panel = false;
    for (auto* label : dialog->findChildren<QLabel*>()) {
      if (label->text() == QStringLiteral("Scripts") && label->isVisible()) {
        in_scripts_panel = true;
      }
    }
    CHECK(in_scripts_panel);
    save_widget_artifact("hotkey_editor_scripts_category", *dialog);
    saw_dialog = true;
    dialog->reject();
  });
  window.show_hotkey_preferences(QStringLiteral("Pong"));
  QApplication::processEvents();
  CHECK(saw_dialog);
}
void ui_hotkey_editor_steals_conflicting_shortcut() {
  HotkeySettingsGroupRestorer restore_hotkeys;
  clear_hotkey_overrides();
  auto&& [window, canvas] = ShownWindowFixture{};
  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    select_hotkeys_tab(*dialog);
    QApplication::processEvents();
    // The Line tool ships unbound, so it renders an assign chip.
    auto* assign_chip = dialog->findChild<QPushButton*>(QStringLiteral("hotkeyAssignChip.tools.line"));
    CHECK(assign_chip != nullptr);
    assign_chip->click();
    QApplication::processEvents();
    auto* capture = dialog->findChild<QLineEdit*>(QStringLiteral("hotkeyCaptureEdit"));
    CHECK(capture != nullptr);
    QKeyEvent press(QEvent::KeyPress, Qt::Key_L, Qt::ControlModifier);
    QApplication::sendEvent(capture, &press);
    QApplication::processEvents();
    QApplication::processEvents();
    // Ctrl+L belongs to Levels, so a confirmation banner appears instead of a
    // silent reassignment.
    auto* banner = dialog->findChild<QFrame*>(QStringLiteral("hotkeyConflictBanner"));
    CHECK(banner != nullptr);
    save_widget_artifact("hotkey_editor_conflict_banner", *dialog);
    auto* take_button = dialog->findChild<QPushButton*>(QStringLiteral("hotkeyConflictAssignButton"));
    CHECK(take_button != nullptr);
    take_button->click();
    QApplication::processEvents();
    QApplication::processEvents();
    auto* line_chip = dialog->findChild<QPushButton*>(QStringLiteral("hotkeyChip.tools.line.0"));
    CHECK(line_chip != nullptr);
    CHECK(line_chip->text() == QKeySequence(Qt::CTRL | Qt::Key_L).toString(QKeySequence::NativeText));
    // Levels falls back to unbound at runtime, with a note explaining why; its
    // stored settings stay untouched so a future default change still applies.
    CHECK(dialog->findChild<QPushButton*>(QStringLiteral("hotkeyAssignChip.image.levels")) != nullptr);
    auto* note = dialog->findChild<QLabel*>(QStringLiteral("hotkeyRowNote.image.levels"));
    CHECK(note != nullptr);
    CHECK(note->isVisible());
    saw_dialog = true;
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
  auto settings = patchy::ui::app_settings();
  settings.beginGroup(QStringLiteral("hotkeys"));
  const auto keys = settings.childKeys();
  settings.endGroup();
  CHECK(keys == QStringList{QStringLiteral("tools.line")});
  CHECK(require_action(window, "toolLineAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_L));
  CHECK(require_action(window, "imageAdjustLevelsAction")->shortcut().isEmpty());
}
void ui_hotkey_editor_reset_all_clears_overrides() {
  HotkeySettingsGroupRestorer restore_hotkeys;
  clear_hotkey_overrides();
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("hotkeys/file.new"), QStringLiteral("Ctrl+F9"));
    settings.setValue(QStringLiteral("hotkeys/edit.undo"), QString());
    settings.sync();
  }
  auto&& [window, canvas] = ShownWindowFixture{};
  CHECK(require_action(window, "fileNewAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_F9));
  const auto* undo_command = window.hotkey_registry().find_command(QStringLiteral("edit.undo"));
  CHECK(undo_command != nullptr && undo_command->action != nullptr);
  CHECK(undo_command->action->shortcuts().isEmpty());
  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    select_hotkeys_tab(*dialog);
    QApplication::processEvents();
    auto* reset_all = dialog->findChild<QPushButton*>(QStringLiteral("hotkeyResetAllButton"));
    CHECK(reset_all != nullptr);
    reset_all->click();
    QApplication::processEvents();
    QApplication::processEvents();
    saw_dialog = true;
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
  auto settings = patchy::ui::app_settings();
  settings.beginGroup(QStringLiteral("hotkeys"));
  CHECK(settings.childKeys().isEmpty());
  settings.endGroup();
  CHECK(require_action(window, "fileNewAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_N));
  CHECK(undo_command->action->shortcuts().size() == 2);
}
}  // namespace
// Stores a working CMYK choice for one test, so every MainWindow it builds starts from it,
// and puts the default (Automatic) back afterwards for the tests that follow.
class ScopedCmykWorkingSettings {
public:
  explicit ScopedCmykWorkingSettings(const patchy::ui::CmykWorkingSettings& settings) { apply(settings); }
  ~ScopedCmykWorkingSettings() { apply(patchy::ui::CmykWorkingSettings{}); }
  ScopedCmykWorkingSettings(const ScopedCmykWorkingSettings&) = delete;
  ScopedCmykWorkingSettings& operator=(const ScopedCmykWorkingSettings&) = delete;
private:
  static void apply(const patchy::ui::CmykWorkingSettings& settings) {
    patchy::ui::set_stored_cmyk_working_settings(settings);
    patchy::set_working_cmyk_space(patchy::ui::cmyk_working_space_for(settings));
    patchy::ui::refresh_color_picker_cmyk_fields();
  }
};
// Issue 90: the picker shows and accepts ink percentages in the working CMYK space, and
// the Info panel reads the pixel under the pointer the same way.
void ui_color_picker_cmyk_fields_follow_working_space() {
  ensure_artifact_dir();
  const ScopedCmykWorkingSettings formula(patchy::ui::CmykWorkingSettings{QLatin1String(patchy::ui::kCmykProfileFormula)});
  auto&& [window, canvas] = ShownWindowFixture{};
  CHECK(!patchy::working_cmyk_space()->has_profile());
  window.findChild<QPushButton*>(QStringLiteral("foregroundColorButton"))->click();
  QApplication::processEvents();
  auto* dialog = find_top_level_dialog(QStringLiteral("patchyColorDialog"));
  CHECK(dialog != nullptr);
  auto* picker = dialog->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
  CHECK(picker != nullptr);
  std::array<QSpinBox*, 4> ink{};
  const std::array<const char*, 4> names{"patchyColorCyanSpin", "patchyColorMagentaSpin", "patchyColorYellowSpin",
                                         "patchyColorBlackSpin"};
  for (std::size_t index = 0; index < ink.size(); ++index) {
    ink[index] = picker->findChild<QSpinBox*>(QLatin1String(names[index]));
    CHECK(ink[index] != nullptr);
    CHECK(ink[index]->minimum() == 0 && ink[index]->maximum() == 100);
    CHECK(ink[index]->toolTip().contains(QStringLiteral("no profile")));
    CHECK(!ink[index]->accessibleName().isEmpty());
  }
  const auto values = [&ink] {
    return std::array<int, 4>{ink[0]->value(), ink[1]->value(), ink[2]->value(), ink[3]->value()};
  };
  // Any color change fills the fields from the working space.
  picker->setCurrentColor(QColor(255, 0, 0));
  QApplication::processEvents();
  CHECK((values() == std::array<int, 4>{0, 100, 100, 0}));
  // A typed ink value sets the color and stays as typed, even though the same RGB reads
  // back as a different mix (0, 100, 100, 50 here).
  ink[0]->setValue(50);
  QApplication::processEvents();
  CHECK(canvas->primary_color() == QColor(128, 0, 0));
  CHECK((values() == std::array<int, 4>{50, 100, 100, 0}));
  ink[3]->setValue(20);
  QApplication::processEvents();
  CHECK(canvas->primary_color() == QColor(102, 0, 0));
  CHECK((values() == std::array<int, 4>{50, 100, 100, 20}));
  // Editing another model recomputes them.
  picker->findChild<QSpinBox*>(QStringLiteral("patchyColorRedSpin"))->setValue(200);
  QApplication::processEvents();
  CHECK(canvas->primary_color() == QColor(200, 0, 0));
  CHECK((values() == std::array<int, 4>{0, 100, 100, 22}));
  dialog->grab().save(QStringLiteral("test-artifacts/ui_color_picker_cmyk.png"));
  // The four fields sit inside the picker, clear of the RGB column.
  const auto* blue = picker->findChild<QSpinBox*>(QStringLiteral("patchyColorBlueSpin"));
  for (const auto* spin : ink) {
    const auto top_left = spin->mapTo(picker, QPoint(0, 0));
    CHECK(spin->isVisible());
    CHECK(top_left.x() > blue->mapTo(picker, QPoint(0, 0)).x() + blue->width());
    CHECK(top_left.x() + spin->width() <= picker->width());
  }
  // Automatic always has a profile (the bundled one when Adobe's is not installed): the
  // same color reads differently and the tooltip names the profile.
  {
    patchy::set_working_cmyk_space(patchy::ui::cmyk_working_space_for(patchy::ui::CmykWorkingSettings{}));
    patchy::ui::refresh_color_picker_cmyk_fields();
    CHECK(patchy::working_cmyk_space()->has_profile());
    CHECK(!ink[0]->toolTip().contains(QStringLiteral("no profile")));
    CHECK((values() != std::array<int, 4>{0, 100, 100, 22}));
    CHECK(canvas->primary_color() == QColor(200, 0, 0));
    patchy::set_working_cmyk_space(patchy::CmykWorkingSpace::formula());
    patchy::ui::refresh_color_picker_cmyk_fields();
  }
  CHECK((values() == std::array<int, 4>{0, 100, 100, 22}));
  auto* info = window.findChild<QLabel*>(QStringLiteral("canvasInfoLabel"));
  CHECK(info != nullptr);
  send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(QPoint(64, 48)), Qt::NoButton,
             Qt::NoButton);
  CHECK(info->text().contains(QStringLiteral("RGB:")));
  CHECK(info->text().contains(QStringLiteral("CMYK: ")));
  dialog->close();
  QApplication::processEvents();
}
// The picker warns when a color cannot print in the working CMYK space and offers the
// color it would print as (issue 90).
void ui_color_picker_warns_when_a_color_cannot_print() {
  ensure_artifact_dir();
  const auto previous = patchy::working_cmyk_space();
  const auto restore = qScopeGuard([&previous] {
    patchy::set_working_cmyk_space(previous);
    patchy::ui::refresh_color_picker_cmyk_fields();
  });
  patchy::set_working_cmyk_space(patchy::CmykWorkingSpace::from_icc_profile(
      patchy::ui::bundled_cmyk_profile(), patchy::RenderingIntent::RelativeColorimetric, true));
  patchy::ui::PatchyColorPicker picker(QColor(150, 110, 90));
  picker.resize(640, 400);
  picker.show();
  QApplication::processEvents();
  auto* warning = picker.findChild<QPushButton*>(QStringLiteral("patchyColorGamutWarningButton"));
  CHECK(warning != nullptr);
  CHECK(!warning->isVisible());
  // Pure green is far outside an offset press.
  picker.setCurrentColor(QColor(0, 255, 0));
  QApplication::processEvents();
  CHECK(warning->isVisible());
  CHECK(warning->toolTip().contains(QStringLiteral("ISO Coated v2")));
  CHECK(!warning->accessibleName().isEmpty());
  picker.grab().save(QStringLiteral("test-artifacts/ui_color_picker_gamut_warning.png"));
  const auto check = patchy::ui::cmyk_gamut_check();
  CHECK(check != nullptr);
  const auto printed = check->printed({0, 255, 0});
  CHECK(!(printed == patchy::RgbColor{0, 255, 0}));
  warning->click();
  QApplication::processEvents();
  CHECK(picker.currentColor() == QColor(printed.red, printed.green, printed.blue));
  // An in-gamut color has no warning, and neither does anything under the formula.
  picker.setCurrentColor(QColor(150, 110, 90));
  QApplication::processEvents();
  CHECK(!warning->isVisible());
  patchy::set_working_cmyk_space(patchy::CmykWorkingSpace::formula());
  patchy::ui::refresh_color_picker_cmyk_fields();
  picker.setCurrentColor(QColor(0, 255, 0));
  QApplication::processEvents();
  CHECK(!warning->isVisible());
  CHECK(patchy::ui::cmyk_gamut_check() == nullptr);
}
// Preferences > Application > CMYK stores the profile, intent and black point choice and
// applies them on OK.
void ui_preferences_cmyk_settings_persist_and_apply() {
  const ScopedCmykWorkingSettings defaults(patchy::ui::CmykWorkingSettings{});
  auto&& [window, canvas] = ShownWindowFixture{};
  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    auto* profile = dialog->findChild<QComboBox*>(QStringLiteral("preferencesCmykProfileCombo"));
    auto* intent = dialog->findChild<QComboBox*>(QStringLiteral("preferencesCmykIntentCombo"));
    auto* black_point = dialog->findChild<QCheckBox*>(QStringLiteral("preferencesCmykBlackPointCheck"));
    CHECK(profile != nullptr && intent != nullptr && black_point != nullptr);
    // Automatic first, relative colorimetric with black point compensation by default.
    CHECK(profile->currentIndex() == 0);
    CHECK(profile->currentData().toString().isEmpty());
    CHECK(profile->itemText(0).startsWith(QStringLiteral("Automatic (")));
    // The Automatic entry says where its profile comes from, and the note says only one ships.
    CHECK(profile->itemText(0).endsWith(QStringLiteral(", included with Patchy)")) ||
          profile->itemText(0).endsWith(QStringLiteral(", found on this computer)")));
    auto* source_info = dialog->findChild<QLabel*>(QStringLiteral("preferencesCmykSourceInfo"));
    CHECK(source_info != nullptr && source_info->text().contains(QStringLiteral("includes one CMYK profile")));
    CHECK(intent->currentData().toInt() == static_cast<int>(patchy::RenderingIntent::RelativeColorimetric));
    CHECK(intent->count() == 4);
    CHECK(black_point->isChecked());
    const auto formula_index = profile->findData(QLatin1String(patchy::ui::kCmykProfileFormula));
    CHECK(formula_index > 0);
    profile->setCurrentIndex(formula_index);
    intent->setCurrentIndex(intent->findData(static_cast<int>(patchy::RenderingIntent::Perceptual)));
    black_point->setChecked(false);
    saw_dialog = true;
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
  const auto stored = patchy::ui::stored_cmyk_working_settings();
  CHECK(stored.profile == QLatin1String(patchy::ui::kCmykProfileFormula));
  CHECK(stored.intent == patchy::RenderingIntent::Perceptual);
  CHECK(!stored.black_point_compensation);
  CHECK(!patchy::working_cmyk_space()->has_profile());
  // The keys are a compatibility contract.
  const auto settings = patchy::ui::app_settings();
  CHECK(settings.value(QStringLiteral("color/cmykProfile")).toString() == QStringLiteral("formula"));
  CHECK(settings.value(QStringLiteral("color/cmykIntent")).toString() == QStringLiteral("perceptual"));
  CHECK(!settings.value(QStringLiteral("color/cmykBlackPointCompensation")).toBool());
  // A stored file that no longer exists falls back instead of failing.
  const patchy::ui::CmykWorkingSettings missing{QStringLiteral("test-artifacts/no-such-profile.icc")};
  CHECK(patchy::ui::cmyk_working_space_for(missing) != nullptr);
  CHECK(patchy::ui::cmyk_working_space_for(missing)->has_profile());
  // The bundled profile is compiled in, unmodified.
  CHECK(patchy::ui::bundled_cmyk_profile().size() == 1052608U);
  const auto bundled = patchy::CmykWorkingSpace::from_icc_profile(
      patchy::ui::bundled_cmyk_profile(), patchy::RenderingIntent::RelativeColorimetric, true);
  CHECK(bundled != nullptr);
  CHECK(bundled->profile_description() == "ISO Coated v2 (basICColor)");
}
void ui_color_picker_accepts_css_rgba_and_names() {
  patchy::ui::PatchyColorPicker picker(Qt::black);
  auto* edit = picker.findChild<QLineEdit*>(QStringLiteral("patchyColorHtmlEdit"));
  CHECK(edit != nullptr);
  edit->setText(QStringLiteral("#11223380"));
  CHECK(QMetaObject::invokeMethod(edit, "editingFinished", Qt::DirectConnection));
  CHECK(picker.currentColor().red() == 0x11);
  CHECK(picker.currentColor().green() == 0x22);
  CHECK(picker.currentColor().blue() == 0x33);
  // Qt's SVG color names include navy on every supported Qt version.
  edit->setText(QStringLiteral("navy"));
  CHECK(QMetaObject::invokeMethod(edit, "editingFinished", Qt::DirectConnection));
  CHECK(picker.currentColor() == QColor(0, 0, 128));
}
// GitHub issue 68: the picker opens with keyboard focus in the HTML (hex) field
// and its value selected, so Ctrl+V then Return applies a copied hex and closes
// the dialog, and Ctrl+C copies the current hex. The field also selects all
// whenever it regains focus.
void ui_color_picker_opens_with_hex_field_selected_for_paste() {
  QGuiApplication::clipboard()->setText(QStringLiteral("#336699"));
  bool dialog_seen = false;
  bool hex_had_focus_with_all_selected = false;
  bool paste_replaced_hex = false;
  int ticks = 0;
  QTimer poll;
  QObject::connect(&poll, &QTimer::timeout, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyColorDialog"));
    if (dialog == nullptr || !dialog->isVisible()) {
      return;
    }
    dialog_seen = true;
    auto* edit = dialog->findChild<QLineEdit*>(QStringLiteral("patchyColorHtmlEdit"));
    CHECK(edit != nullptr);
    // The focus lands queued after show; give it a few ticks before judging.
    if (!(edit->hasFocus() && edit->hasSelectedText()) && ++ticks < 50) {
      return;
    }
    poll.stop();
    hex_had_focus_with_all_selected =
        edit->hasFocus() && edit->selectedText() == edit->text() && edit->text() == QStringLiteral("#0A141E");
    send_key(*edit, Qt::Key_V, Qt::ControlModifier);
    paste_replaced_hex = edit->text() == QStringLiteral("#336699");
    // Return commits the field and reaches the dialog's default (OK) button.
    send_key(*edit, Qt::Key_Return);
  });
  poll.start(10);
  const auto result = patchy::ui::request_patchy_color(nullptr, QColor(10, 20, 30), QStringLiteral("Hex paste"));
  poll.stop();
  CHECK(dialog_seen);
  CHECK(hex_had_focus_with_all_selected);
  CHECK(paste_replaced_hex);
  CHECK(result.has_value());
  CHECK(result.has_value() && *result == QColor(0x33, 0x66, 0x99));
  QApplication::processEvents();
  // Re-focusing the field selects its value again (a click, Tab, or setFocus).
  patchy::ui::PatchyColorPicker picker(QColor(1, 2, 3));
  picker.show();
  picker.activateWindow();
  QApplication::processEvents();
  auto* edit = picker.findChild<QLineEdit*>(QStringLiteral("patchyColorHtmlEdit"));
  CHECK(edit != nullptr);
  auto* red_spin = picker.findChild<QSpinBox*>(QStringLiteral("patchyColorRedSpin"));
  CHECK(red_spin != nullptr);
  red_spin->setFocus(Qt::MouseFocusReason);
  QApplication::processEvents();
  CHECK(!edit->hasFocus());
  edit->setFocus(Qt::MouseFocusReason);
  QApplication::processEvents();
  CHECK(edit->hasFocus());
  CHECK(edit->selectedText() == QStringLiteral("#010203"));
}
void ui_hotkey_duplicate_ids_fail_without_replacing_the_command() {
  patchy::ui::HotkeyRegistry registry;
  QAction first(nullptr), second(nullptr);
  registry.register_command(&first, QStringLiteral("test.duplicate"), {});
  bool rejected = false;
  try { registry.register_command(&second, QStringLiteral("test.duplicate"), {}); }
  catch (const std::logic_error&) { rejected = true; }
  CHECK(rejected);
  CHECK(registry.commands().size() == 1);
  CHECK(registry.find_command(QStringLiteral("test.duplicate"))->action == &first);
}
std::vector<patchy::test::TestCase> pickers_notices_hotkeys_tests() {
  return {
      {"ui_color_picker_changes_foreground_color", ui_color_picker_changes_foreground_color},
      {"ui_color_picker_ignores_reentrant_requests", ui_color_picker_ignores_reentrant_requests},
      {"ui_color_picker_closes_with_parent_dialog", ui_color_picker_closes_with_parent_dialog},
      {"ui_color_picker_keeps_its_color_when_parent_is_accepted",
       ui_color_picker_keeps_its_color_when_parent_is_accepted},
      {"ui_color_picker_fields_keep_their_digits_in_a_styled_dialog",
       ui_color_picker_fields_keep_their_digits_in_a_styled_dialog},
      {"ui_color_picker_wheel_and_sliders_modes", ui_color_picker_wheel_and_sliders_modes},
      {"ui_dialog_position_memory_restores_last_position", ui_dialog_position_memory_restores_last_position},
      {"ui_dialog_position_memory_centers_unmoved_dialogs_on_parent",
       ui_dialog_position_memory_centers_unmoved_dialogs_on_parent},
      {"ui_progress_dialogs_ignore_position_memory_and_center_on_parent",
       ui_progress_dialogs_ignore_position_memory_and_center_on_parent},
      {"ui_message_boxes_and_marked_dialogs_ignore_position_memory",
       ui_message_boxes_and_marked_dialogs_ignore_position_memory},
      {"ui_message_boxes_stay_inside_screen_with_offscreen_parent",
       ui_message_boxes_stay_inside_screen_with_offscreen_parent},
      {"ui_message_box_expanded_details_stay_inside_screen", ui_message_box_expanded_details_stay_inside_screen},
      {"ui_dialog_position_memory_clamps_native_frame_without_remembering_placement",
       ui_dialog_position_memory_clamps_native_frame_without_remembering_placement},
      {"ui_dirty_state_marks_tabs_and_undo_restores_saved_revision",
       ui_dirty_state_marks_tabs_and_undo_restores_saved_revision},
      {"ui_compatibility_report_flags_psd_text_placeholders",
       ui_compatibility_report_flags_psd_text_placeholders},
      {"ui_compatibility_report_ignores_patchy_written_psd_blocks",
       ui_compatibility_report_ignores_patchy_written_psd_blocks},
      {"ui_compatibility_report_treats_native_adjustment_blocks_as_known",
       ui_compatibility_report_treats_native_adjustment_blocks_as_known},
      {"ui_compatibility_report_treats_levels_as_native_psd_adjustment",
       ui_compatibility_report_treats_levels_as_native_psd_adjustment},
      {"ui_compatibility_report_pins_native_vs_private_adjustment_kinds",
       ui_compatibility_report_pins_native_vs_private_adjustment_kinds},
      {"ui_compatibility_report_flags_cmyk_rgb_conversion",
       ui_compatibility_report_flags_cmyk_rgb_conversion},
      {"ui_compatibility_report_flags_grayscale_rgb_conversion",
       ui_compatibility_report_flags_grayscale_rgb_conversion},
      {"ui_compatibility_report_flags_unrendered_styles_on_groups",
       ui_compatibility_report_flags_unrendered_styles_on_groups},
      {"ui_compatibility_report_handles_supported_unsupported_and_boundary_blend_if",
       ui_compatibility_report_handles_supported_unsupported_and_boundary_blend_if},
      {"ui_compatibility_report_describes_linked_smart_object_updates",
       ui_compatibility_report_describes_linked_smart_object_updates},
      {"ui_compatibility_report_warns_about_cmyk_ink_adjustments",
       ui_compatibility_report_warns_about_cmyk_ink_adjustments},
      {"ui_psd_import_notice_reports_unrendered_layer_effects",
       ui_psd_import_notice_reports_unrendered_layer_effects},
      {"ui_psd_import_notice_reports_only_unsupported_blend_if",
       ui_psd_import_notice_reports_only_unsupported_blend_if},
      {"ui_psd_import_warning_dialog_is_hidden_by_default",
       ui_psd_import_warning_dialog_is_hidden_by_default},
      {"ui_psd_import_warning_dialog_shows_when_enabled",
       ui_psd_import_warning_dialog_shows_when_enabled},
      {"ui_alt_left_click_samples_foreground_color", ui_alt_left_click_samples_foreground_color},
      {"ui_alt_color_pick_shows_rgb_status_and_updates_open_color_panel",
       ui_alt_color_pick_shows_rgb_status_and_updates_open_color_panel},
      {"ui_image_mode_converts_bit_depth_with_undo", ui_image_mode_converts_bit_depth_with_undo},
      {"ui_image_mode_from_32_bits_asks_for_hdr_toning", ui_image_mode_from_32_bits_asks_for_hdr_toning},
      {"ui_new_document_dialog_creates_the_chosen_bit_depth", ui_new_document_dialog_creates_the_chosen_bit_depth},
      {"ui_deep_document_render_strips_match_the_sequential_render",
       ui_deep_document_render_strips_match_the_sequential_render},
      {"ui_layer_operations_keep_deep_documents_at_depth", ui_layer_operations_keep_deep_documents_at_depth},
      {"ui_free_transform_keeps_deep_layers_at_depth", ui_free_transform_keeps_deep_layers_at_depth},
      {"ui_free_transform_preview_and_commit_keep_colors_at_every_depth",
       ui_free_transform_preview_and_commit_keep_colors_at_every_depth},
      {"ui_warp_transform_keeps_deep_layers_at_depth", ui_warp_transform_keeps_deep_layers_at_depth},
      {"ui_brush_paints_deep_layers_and_masks_at_depth", ui_brush_paints_deep_layers_and_masks_at_depth},
      {"ui_retouch_tools_edit_deep_layers_at_depth", ui_retouch_tools_edit_deep_layers_at_depth},
      {"ui_eyedropper_picks_the_composite_of_a_16_bit_document", ui_eyedropper_picks_the_composite_of_a_16_bit_document},
      {"ui_eyedropper_picks_group_opacity_and_clipping_composite",
       ui_eyedropper_picks_group_opacity_and_clipping_composite},
      {"ui_eyedropper_alt_click_picks_the_background_color", ui_eyedropper_alt_click_picks_the_background_color},
      {"ui_eyedropper_sample_size_and_current_layer", ui_eyedropper_sample_size_and_current_layer},
      {"ui_eyedropper_sets_option_and_alt_inverts", ui_eyedropper_sets_option_and_alt_inverts},
      {"ui_eyedropper_sample_outline_follows_the_pointer", ui_eyedropper_sample_outline_follows_the_pointer},
      {"ui_sample_loupe_geometry_stays_inside_the_viewport", ui_sample_loupe_geometry_stays_inside_the_viewport},
      {"ui_sample_loupe_shows_while_a_pick_is_held", ui_sample_loupe_shows_while_a_pick_is_held},
      {"ui_pick_drag_coalesces_queued_moves", ui_pick_drag_coalesces_queued_moves},
      {"ui_pick_drag_saves_colors_on_release", ui_pick_drag_saves_colors_on_release},
      {"ui_sample_loupe_lens_matches_the_canvas_at_every_depth",
       ui_sample_loupe_lens_matches_the_canvas_at_every_depth},
      {"ui_eyedropper_options_bar_sample_controls", ui_eyedropper_options_bar_sample_controls},
      {"ui_eyedropper_sample_layers_choices_pick_their_composites",
       ui_eyedropper_sample_layers_choices_pick_their_composites},
      {"ui_eyedropper_sample_layers_migrates_legacy_setting", ui_eyedropper_sample_layers_migrates_legacy_setting},
      {"ui_eyedropper_starts_in_gray_area_and_drags_to_document_color",
       ui_eyedropper_starts_in_gray_area_and_drags_to_document_color},
      {"ui_photoshop_shortcuts_are_registered", ui_photoshop_shortcuts_are_registered},
      {"ui_selection_commands_live_only_in_select_menu", ui_selection_commands_live_only_in_select_menu},
      {"ui_brush_flow_popup_slider_updates_canvas",
       ui_brush_flow_popup_slider_updates_canvas},
      {"ui_slider_popup_dismissing_press_paints", ui_slider_popup_dismissing_press_paints},
      {"ui_slider_popup_close_restores_cursor_under_pointer", ui_slider_popup_close_restores_cursor_under_pointer},
      {"ui_options_bar_field_escape_reverts_the_edit", ui_options_bar_field_escape_reverts_the_edit},
      {"ui_options_bar_enter_and_esc_return_the_keyboard_to_the_canvas",
       ui_options_bar_enter_and_esc_return_the_keyboard_to_the_canvas},
      {"ui_options_bar_tab_focus_shows_a_themed_outline", ui_options_bar_tab_focus_shows_a_themed_outline},
      {"ui_options_bar_tab_wraps_within_the_bar", ui_options_bar_tab_wraps_within_the_bar},
      {"ui_options_bar_button_groups_are_one_tab_stop", ui_options_bar_button_groups_are_one_tab_stop},
      {"ui_size_sliders_give_the_low_end_most_of_the_track",
       ui_size_sliders_give_the_low_end_most_of_the_track},
      {"ui_hotkey_resolution_rules", ui_hotkey_resolution_rules},
      {"ui_hotkey_defaults_have_no_conflicts", ui_hotkey_defaults_have_no_conflicts},
      {"ui_new_layer_and_merge_visible_keys_moved_defaults_only", ui_new_layer_and_merge_visible_keys_moved_defaults_only},
      {"ui_tool_cycle_yields_to_member_override", ui_tool_cycle_yields_to_member_override},
      {"ui_hotkey_override_applies_at_startup", ui_hotkey_override_applies_at_startup},
      {"ui_preferences_builds_hotkey_editor_on_first_visit", ui_preferences_builds_hotkey_editor_on_first_visit},
      {"ui_hotkey_editor_assigns_and_persists_custom_shortcut",
       ui_hotkey_editor_assigns_and_persists_custom_shortcut},
      {"ui_hotkey_editor_steals_conflicting_shortcut", ui_hotkey_editor_steals_conflicting_shortcut},
      {"ui_hotkey_editor_reset_all_clears_overrides", ui_hotkey_editor_reset_all_clears_overrides},
      {"ui_hotkey_editor_lists_scripts_category", ui_hotkey_editor_lists_scripts_category},
      {"ui_color_picker_accepts_css_rgba_and_names", ui_color_picker_accepts_css_rgba_and_names},
      {"ui_color_picker_opens_with_hex_field_selected_for_paste", ui_color_picker_opens_with_hex_field_selected_for_paste},
      {"ui_color_picker_cmyk_fields_follow_working_space", ui_color_picker_cmyk_fields_follow_working_space},
      {"ui_preferences_cmyk_settings_persist_and_apply", ui_preferences_cmyk_settings_persist_and_apply},
      {"ui_color_picker_warns_when_a_color_cannot_print", ui_color_picker_warns_when_a_color_cannot_print},
      {"ui_hotkey_duplicate_ids_fail_without_replacing_the_command", ui_hotkey_duplicate_ids_fail_without_replacing_the_command},
  };
}
#include "core/psd_source_colors.hpp"
