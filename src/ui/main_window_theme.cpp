// The application-wide dark QSS theme, split out of main_window.cpp:
// photoshop_style() returns the stylesheet the MainWindow constructor applies
// to the whole window (declared in main_window_shared.hpp).
// Pure function move from main_window.cpp; behavior must stay identical.

#include "ui/main_window.hpp"
#include "ui/main_window_shared.hpp"

#include "core/blend_math.hpp"
#include "core/layer_metadata.hpp"
#include "core/smart_object.hpp"
#include "core/text_warp.hpp"
#include "core/warp_mesh.hpp"
#include "core/layer_render_utils.hpp"
#include "core/layer_tree.hpp"
#include "core/palette_presets.hpp"
#include "core/pattern_presets.hpp"
#include "core/pixel_tools.hpp"
#include "formats/palette_io.hpp"
#include "filters/builtin_filters.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/heif_document_io.hpp"
#include "formats/raw_document_io.hpp"
#include "plugins/legacy_photoshop_adapter.hpp"
#include "psd/psd_document_io.hpp"
#include "psd/psd_filter_effects.hpp"
#include "psd/psd_smart_objects.hpp"
#include "ui/action_icons.hpp"
#include "ui/app_settings.hpp"
#include "render/compositor.hpp"
#include "ui/blend_mode_ui.hpp"
#include "ui/brush_dynamics_popup.hpp"
#include "ui/brush_presets.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/brush_tip_manager_dialog.hpp"
#include "ui/brush_tip_picker.hpp"
#include "ui/default_brush_tips.hpp"
#include "ui/compatibility_report.hpp"
#include "ui/image_document_io.hpp"
#include "ui/image_save_options_dialog.hpp"
#include "ui/raw_develop_dialog.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/gradient_stops_editor.hpp"
#include "ui/gradient_library.hpp"
#include "ui/gradient_manager_dialog.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/document_float_window.hpp"
#include "ui/font_picker.hpp"
#include "ui/hotkey_editor.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/color_panel.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/localization.hpp"
#include "ui/measurement_units.hpp"
#include "ui/palette_convert_dialog.hpp"
#include "ui/palette_panel.hpp"
#include "ui/pattern_library.hpp"
#include "ui/photo_pattern_presets.hpp"
#include "ui/style_library.hpp"
#include "ui/print_dialog.hpp"
#include "ui/smart_object_render.hpp"
#include "ui/scanner_import.hpp"
#include "ui/sprite_sheet_dialog.hpp"
#include "ui/start_panel.hpp"
#include "ui/tile_preview_window.hpp"
#include "ui/warp_text_dialog.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/splash_dialog.hpp"
#include "ui/update_checker.hpp"
#include "ui/zoom_status_bar.hpp"
#include "support/string_utils.hpp"

#include <QAbstractItemView>
#include <QAbstractItemModel>
#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QBrush>
#include <QBuffer>
#include <QButtonGroup>
#include <QByteArray>
#include <QDateTime>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QCursor>
#include <QColorSpace>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLayout>
#include <QResizeEvent>
#include <QIcon>
#include <QImageReader>
#include <QInputDialog>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLabel>
#include <QKeySequence>
#include <QListWidget>
#include <QLinearGradient>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QMessageBox>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPushButton>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPolygon>
#include <QPointer>
#include <QProcess>
#include <QProgressDialog>
#include <QRegion>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QScopeGuard>
#include <QSettings>
#include <QShowEvent>
#include <QStandardPaths>
#include <QStandardItem>
#include <QStyledItemDelegate>
#include <QMutex>
#include <QRawFont>
#include <QTextCharFormat>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextLayout>
#include <QTextOption>
#include <QSignalBlocker>
#include <QSize>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QStringList>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOption>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>
#include <QTransform>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <functional>
#include <future>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <tchar.h>
#include <tpcshrd.h>
#endif

#ifndef PATCHY_VERSION
#define PATCHY_VERSION "0.0.0"
#endif

// Icon resources live in the static patchy_ui library; force registration before first use.
int qInitResources_icons();

namespace patchy::ui {

QString photoshop_style() {
  return QStringLiteral(R"(
    QMainWindow, QMenuBar, QMenu, QDockWidget, QWidget {
      background: #262626;
      color: #e6e6e6;
      font-size: 12px;
    }
    QMainWindow {
      border: 1px solid #1f1f1f;
    }
    QMainWindow::separator {
      background: #1e2022;
      width: 7px;
      height: 7px;
    }
    QMainWindow::separator:hover {
      background: #4e6f95;
    }
    QWidget#rightDockResizeHandle {
      background: #1e2022;
    }
    QWidget#rightDockResizeHandle:hover {
      background: #4e6f95;
    }
    QMenuBar {
      background: #4f4f4f;
      color: #f0f0f0;
      border-bottom: 1px solid #343434;
      min-height: 34px;
      max-height: 34px;
      padding-left: 35px;
    }
    QMenuBar::item {
      background: transparent;
      min-height: 34px;
      padding: 0 10px;
      margin: 0 1px;
    }
    QMenuBar::item:selected {
      background: #3a3a3a;
    }
    QLabel#patchyBadge {
      background: transparent;
      border: 0;
    }
    QMenu {
      background: #3a3a3a;
      border: 1px solid #1f1f1f;
    }
    QMenu::item {
      padding: 7px 34px 7px 24px;
    }
    QMenu::item:selected {
      background: #4e6f95;
      color: #ffffff;
    }
    QMenu::item:disabled {
      color: #737373;
    }
    QMenuBar::item:disabled {
      color: #9a9a9a;
    }
    QMenu::separator {
      height: 1px;
      background: #555555;
      margin: 4px 6px;
    }
    QToolBar {
      background: #3b3b3b;
      border: 0;
      border-bottom: 1px solid #292929;
      spacing: 2px;
      padding: 3px;
    }
    QToolButton {
      background: transparent;
      border: 1px solid transparent;
      border-radius: 0;
      padding: 3px;
      min-width: 26px;
      min-height: 26px;
    }
    QToolButton[optionsBarButton="true"] {
      padding: 2px;
      min-width: 18px;
      min-height: 16px;
    }
    QToolButton#brushTipPicker {
      padding: 2px;
      min-height: 20px;
      max-height: 20px;
    }
    QToolButton#brushDynamicsButton {
      padding: 2px 6px;
      min-height: 20px;
      max-height: 20px;
    }
    QToolButton#brushDynamicsButton[dynamicsActive="true"] {
      border-color: #6bb3ff;
    }
    QToolButton:hover {
      background: #4a4a4a;
      border-color: #696969;
    }
    QToolButton:checked {
      background: #2f75bd;
      border-color: #6bb3ff;
    }
    QWidget#windowChromeControls {
      background: #4f4f4f;
    }
    QToolButton[windowChromeButton="true"] {
      background: transparent;
      border: 0;
      border-radius: 0;
      padding: 0;
      min-width: 46px;
      max-width: 46px;
      min-height: 34px;
      max-height: 34px;
    }
    QToolButton[windowChromeButton="true"]:hover {
      background: #626262;
      border: 0;
    }
    QToolButton[windowChromeButton="true"]:pressed {
      background: #3c3c3c;
    }
    QToolButton#windowCloseButton:hover {
      background: #c42b1c;
    }
    QToolButton#windowCloseButton:pressed {
      background: #9f2117;
    }
    QToolBar#toolPalette {
      background: #535353;
      border-right: 1px solid #202020;
      border-bottom: 0;
      padding: 3px 4px;
      spacing: 1px;
    }
    QToolBar#toolPalette QToolButton {
      min-width: 28px;
      max-width: 28px;
      min-height: 24px;
      max-height: 24px;
      padding: 1px;
    }
    QToolButton[toolFlyout="true"]::menu-indicator {
      image: url(:/patchy/icons/tool-flyout-corner.svg);
      width: 7px;
      height: 7px;
      subcontrol-origin: padding;
      subcontrol-position: bottom right;
      bottom: 1px;
      right: 1px;
    }
    QToolBar#toolPalette::separator {
      background: #616161;
      height: 1px;
      margin: 3px 7px;
    }
    QWidget#toolPaletteSpacer {
      background: #535353;
    }
    QToolBar#Options {
      background: #3d3d3d;
      min-height: 38px;
      border-top: 1px solid #5a5a5a;
      border-bottom: 1px solid #292929;
      spacing: 5px;
      padding: 4px 7px;
    }
    QToolBar#Options QFrame#optionSeparator {
      color: #565656;
      max-width: 2px;
    }
    QToolBar#Options QLabel {
      color: #e1e1e1;
      padding-left: 5px;
      padding-right: 2px;
    }
    QToolBar#Options QLabel[optionLabel="true"] {
      background: #262626;
      border: 1px solid #171717;
      border-right: 0;
      border-top-color: #5d5d5d;
      color: #f0f0f0;
      min-height: 24px;
      max-height: 24px;
      padding: 0 7px;
    }
    QToolBar#Options QSpinBox, QToolBar#Options QDoubleSpinBox, QToolBar#Options QComboBox, QToolBar#Options QFontComboBox {
      min-height: 24px;
      max-height: 24px;
      padding-left: 4px;
      background: #292929;
      border: 1px solid #171717;
      border-top-color: #5d5d5d;
    }
    QWidget#selectionFeatherGroup {
      background: #292929;
      border: 1px solid #171717;
      border-top-color: #5d5d5d;
      min-height: 24px;
      max-height: 24px;
    }
    QWidget#selectionFeatherGroup QLabel {
      background: #262626;
      border: 0;
      border-right: 1px solid #171717;
      color: #f0f0f0;
      min-height: 24px;
      max-height: 24px;
      padding: 0 8px;
    }
    QWidget#selectionFeatherGroup QSpinBox {
      background: #292929;
      border: 0;
      min-height: 24px;
      max-height: 24px;
      padding-left: 6px;
    }
    QToolBar#Options QCheckBox {
      color: #f0f0f0;
      min-height: 24px;
      max-height: 24px;
      padding-left: 6px;
      padding-right: 8px;
      spacing: 6px;
    }
    QToolBar#Options QCheckBox#selectionAntiAliasCheck {
      background: #292929;
      border: 1px solid #171717;
      border-top-color: #5d5d5d;
      padding-left: 7px;
      padding-right: 10px;
    }
    QToolBar#Options QCheckBox::indicator {
      width: 14px;
      height: 14px;
      background: #1f1f1f;
      border: 1px solid #777777;
    }
    QToolBar#Options QCheckBox::indicator:hover {
      border-color: #9ccfff;
    }
    QToolBar#Options QCheckBox::indicator:checked {
      background: #1473e6;
      border-color: #9ccfff;
      image: url(:/patchy/icons/checkmark.svg);
    }
    QToolBar#Options QSlider::groove:horizontal {
      height: 4px;
      background: #1c1c1c;
      border: 1px solid #555555;
    }
    QToolBar#Options QSlider::sub-page:horizontal {
      background: #1473e6;
      border: 1px solid #5aa9ff;
    }
    QToolBar#Options QSlider::handle:horizontal {
      background: #c9d0d8;
      border: 1px solid #101010;
      width: 10px;
      margin: -5px 0;
    }
    QToolBar#Options QPushButton {
      min-height: 24px;
      max-height: 24px;
      background: #303030;
      border: 1px solid #171717;
      border-top-color: #5d5d5d;
      padding: 1px 7px;
    }
    QToolBar#Options QPushButton[optionsSessionButton="true"] {
      padding: 1px 2px; /* the 20px session icons need the width the default 7px padding eats */
    }
    QToolBar#Options QPushButton:checked {
      background: #1667b7;
      border-color: #63adff;
      color: #ffffff;
    }
    QDockWidget::title {
      background: #323232;
      padding: 5px;
      border-bottom: 1px solid #202020;
    }
    QWidget#historyDockTitle, QWidget#channelsDockTitle, QWidget#propertiesDockTitle, QWidget#infoDockTitle,
    QWidget#layersDockTitle {
      background: #2f3032;
      border-top: 1px solid #45474b;
      border-bottom: 1px solid #1b1c1e;
    }
    QWidget#historyDockTitle QLabel, QWidget#channelsDockTitle QLabel, QWidget#propertiesDockTitle QLabel,
    QWidget#infoDockTitle QLabel, QWidget#layersDockTitle QLabel {
      color: #f0f0f0;
      font-weight: 600;
    }
    QToolButton[dockCollapseButton="true"] {
      background: transparent;
      color: #cfd3d8;
      border: 1px solid transparent;
      border-radius: 0;
      padding: 0;
      min-width: 18px;
      max-width: 18px;
      min-height: 18px;
      max-height: 18px;
      font-weight: 700;
    }
    QToolButton[dockCollapseButton="true"]:hover {
      background: #3b3d40;
      border-color: #5b5e63;
    }
    QToolButton[dockCollapseButton="true"]:checked {
      background: transparent;
      color: #cfd3d8;
      border-color: transparent;
    }
    QListWidget, QTreeWidget, QComboBox, QSpinBox, QSlider, QLineEdit, QTextEdit {
      background: #2b2b2b;
      color: #e6e6e6;
      border: 1px solid #5a5a5a;
      selection-background-color: #3a414a;
      min-height: 20px;
    }
    QListWidget::item {
      min-height: 48px;
      padding: 0;
      border-bottom: 1px solid #202225;
    }
    QListWidget::item:selected {
      background: #3a414a;
      color: #f4f6f8;
      border: 1px solid #67717d;
    }
    QListWidget#layerList::item {
      color: transparent;
    }
    QListWidget#layerList::item:selected {
      color: transparent;
    }
    QListWidget::indicator {
      width: 0;
      height: 0;
      max-width: 0;
      max-height: 0;
      background: transparent;
      border: 0;
      margin: 0;
    }
    QListWidget::indicator:checked {
      background: transparent;
      border: 0;
    }
    QListWidget#layerStyleCategoryList::item {
      min-height: 24px;
      padding: 0;
      border-bottom: 1px solid #3b3b3b;
    }
    QListWidget#layerStyleCategoryList::item:selected {
      background: #2d4c6d;
      color: #ffffff;
      border: 1px solid #4f91ca;
    }
    QListWidget#layerStyleCategoryList::indicator {
      width: 0;
      height: 0;
      max-width: 0;
      max-height: 0;
      margin: 0;
      background: transparent;
      border: 0;
    }
    QListWidget#layerStyleCategoryList::indicator:checked {
      background: transparent;
      border: 0;
    }
    QLabel#layerRowName {
      color: #f0f3f8;
      font-size: 12px;
    }
    QLabel#layerRowDetails {
      color: #aeb6c2;
      font-size: 10px;
    }
    QLabel#layerContentThumbnail[layerTargetActive="true"],
    QLabel#layerMaskThumbnail[layerTargetActive="true"],
    QLabel#layerSmartFilterMaskThumbnail[layerTargetActive="true"] {
      border: 2px solid #31a8ff;
      padding: 0;
    }
    QToolButton#maskEditModeChip {
      background: #31a8ff;
      color: #0d1420;
      border: 1px solid #6cc4ff;
      border-radius: 4px;
      padding: 2px 10px;
      font-weight: 600;
    }
    QToolButton#maskEditModeChip:hover {
      background: #5cbcff;
    }
    QLineEdit#statusZoomEdit {
      background: #1e1e1e;
      color: #cfcfcf;
      border: 1px solid #4a4a4a;
      border-radius: 3px;
      padding: 0 5px;
      min-height: 16px;
      font-size: 11px;
    }
    QLineEdit#statusZoomEdit:focus {
      border-color: #31a8ff;
      color: #f0f0f0;
    }
    QLineEdit#statusZoomEdit:disabled {
      color: #6f6f6f;
      border-color: #3a3a3a;
    }
    QLabel#canvasInfoLabel, QLabel#documentInfoLabel {
      color: #d7dde6;
      line-height: 130%;
    }
    QScrollArea#propertiesScrollArea {
      background: #28292b;
      border: 0;
    }
    QWidget#propertiesPanel {
      background: #28292b;
    }
    QLabel#documentInfoLabel, QLabel#activeLayerInfoLabel, QLabel#activeLayerGeometryLabel,
    QLabel#activeLayerMaskLabel, QLabel#activeLayerAdjustmentLabel, QLabel#activeLayerTextLabel,
    QLabel#activeToolInfoLabel {
      background: #24272b;
      border: 1px solid #3e454d;
      padding: 4px;
      color: #d7dde6;
      font-size: 11px;
    }
    QWidget#layersPanel {
      background: #28292b;
    }
    QListWidget#layerList {
      min-height: 120px;
    }
    QToolButton#layerFolderDisclosureButton {
      background: transparent;
      color: #d9e0ea;
      border: 1px solid transparent;
      border-radius: 3px;
      padding: 0;
      min-width: 18px;
      max-width: 18px;
      min-height: 20px;
      max-height: 20px;
    }
    QToolButton#layerFolderDisclosureButton:hover {
      border-color: #59636f;
      background: #30343a;
    }
    QToolButton#layerFolderDisclosureButton[layerDragActive="true"]:hover {
      border-color: transparent;
      background: transparent;
    }
    QToolButton#layerFolderDisclosureButton:disabled {
      color: transparent;
      border-color: transparent;
      background: transparent;
    }
    QToolButton#layerVisibilityCheck {
      background: transparent;
      color: #f2f6fb;
      border: 1px solid transparent;
      border-radius: 3px;
      padding: 0;
      min-width: 22px;
      max-width: 22px;
      min-height: 22px;
      max-height: 22px;
    }
    QToolButton#layerVisibilityCheck:hover {
      background: #30343a;
      border-color: #59636f;
    }
    QToolButton#layerVisibilityCheck[layerDragActive="true"]:hover {
      border-color: transparent;
      background: transparent;
    }
    QToolButton#layerVisibilityCheck:checked {
      background: transparent;
      border-color: transparent;
    }
    QToolButton#layerVisibilityCheck[layerDragActive="true"]:checked:hover {
      background: transparent;
      border-color: transparent;
    }
    QToolButton#layerVisibilityCheck:!checked {
      background: transparent;
      border-color: transparent;
    }
    QLabel#layerLockBadge {
      background: transparent;
      border: 0;
      padding: 0;
    }
    QToolButton[layerLockControl="true"] {
      background: #24272b;
      border: 1px solid #46505b;
      border-radius: 3px;
      padding: 0;
      min-width: 24px;
      max-width: 24px;
      min-height: 24px;
      max-height: 24px;
    }
    QToolButton[layerLockControl="true"]:hover {
      background: #30343a;
      border-color: #687481;
    }
    QToolButton[layerLockControl="true"]:checked {
      background: #3b3420;
      border-color: #c9a944;
    }
    QToolButton[layerLockControl="true"][mixed="true"] {
      background: #2f3136;
      border-color: #7b8490;
    }
    QToolButton#layerMaskLinkButton, QToolButton#layerFxBadgeButton, QToolButton#layerSmartObjectBadgeButton,
    QToolButton#layerClippingBadgeButton {
      background: transparent;
      border: 1px solid transparent;
      border-radius: 3px;
      padding: 0;
    }
    QToolButton#layerMaskLinkButton:hover, QToolButton#layerFxBadgeButton:hover,
    QToolButton#layerSmartObjectBadgeButton:hover, QToolButton#layerClippingBadgeButton:hover {
      background: #30343a;
      border-color: #59636f;
    }
    QPushButton {
      background: #3a3a3a;
      color: #e6e6e6;
      border: 1px solid #666666;
      border-radius: 0;
      padding: 4px 8px;
    }
    QPushButton:hover {
      background: #4a4a4a;
      border-color: #8a8a8a;
    }
    QPushButton[compactSymbolButton="true"] {
      padding: 0;
      min-width: 22px;
      max-width: 22px;
      min-height: 22px;
      max-height: 22px;
    }
    QPushButton[layerActionButton="true"], QToolButton[layerActionButton="true"] {
      padding: 0;
      min-width: 40px;
      max-width: 40px;
      min-height: 34px;
      max-height: 34px;
    }
    QToolButton[channelActionButton="true"] {
      padding: 0;
      min-width: 34px;
      max-width: 34px;
      min-height: 30px;
      max-height: 30px;
    }
    QPushButton[layerDropActive="true"], QToolButton[layerDropActive="true"] {
      background: #2e3f50;
      border: 2px solid #31a8ff;
      padding: 0;
    }
    QStatusBar {
      background: #252525;
      color: #cfcfcf;
    }
    QLabel {
      color: #e1e1e1;
      /* Transparent, not the global QWidget #262626: labels sit on panels of other
         shades (e.g. the #303030 Preferences panels) and an opaque fill shows as a
         mismatched strip behind the text. */
      background: transparent;
    }
    QCheckBox {
      color: #e1e1e1;
      background: transparent;
      /* The explicit border matters on macOS: for rules with only a native border,
         the stylesheet layer keeps QMacStyle's Aqua layout-item margins (+2,+3,-9,-4),
         which overlap the neighboring label 9px into the checkbox. That is right for
         the inset native glyph but overlaps the flat stylesheet indicator, jamming
         label text into the box on retina Macs. A non-native border ("none" counts)
         makes QStyleSheetStyle return the plain widget rect for layout items. */
      border: none;
    }
    QCheckBox::indicator {
      width: 12px;
      height: 12px;
      background: #4a4a4a;
      border: 1px solid #8a8a8a;
    }
    QCheckBox::indicator:hover {
      border-color: #9ccfff;
    }
    QCheckBox::indicator:checked {
      background: #1473e6;
      border-color: #9ccfff;
      image: url(:/patchy/icons/checkmark.svg);
    }
    QTabWidget::pane {
      border-top: 1px solid #5c5c5c;
    }
    QTabBar::tab {
      background: #2b2b2b;
      color: #e1e1e1;
      border: 1px solid #2b2b2b;
      padding: 5px 12px;
      min-height: 20px;
    }
    QTabBar::tab:hover:!selected {
      background: #353535;
    }
    QTabBar::tab:selected {
      background: #3f3f3f;
      color: #ffffff;
      border-bottom-color: #3f3f3f;
    }
  )")
#ifdef Q_OS_MACOS
         // macOS-only styling to match the Windows look: QMacStyle group boxes carry
         // Aqua-sized native chrome (big title gap and content margins, plus Aqua
         // layout-item overlaps since their rule border stays native), which blows
         // dense panels like the brush Dynamics popup past the screen height; and
         // QMacStyle scroll bars are minimal flat overlays whose handle is hard to
         // spot on the dark theme, so they get the Windows-classic layout (dithered
         // track, flat handle, arrow buttons). Windows/Linux keep native rendering.
         + QStringLiteral(R"(
    QGroupBox {
      border: 1px solid #4f4f4f;
      border-radius: 3px;
      margin-top: 8px;
      padding: 2px 2px 2px 2px;
    }
    QGroupBox::title {
      subcontrol-origin: margin;
      subcontrol-position: top left;
      left: 8px;
      padding: 0 3px;
      background: #262626;
    }
    QScrollBar:vertical {
      background: #262626;
      background-image: url(:/patchy/icons/scroll-dither.svg);
      width: 16px;
    }
    QScrollBar:horizontal {
      background: #262626;
      background-image: url(:/patchy/icons/scroll-dither.svg);
      height: 16px;
    }
    QScrollBar::handle:vertical, QScrollBar::handle:horizontal {
      background: #565656;
      border: 1px solid #6e6e6e;
    }
    QScrollBar::handle:vertical { min-height: 8px; }
    QScrollBar::handle:horizontal { min-width: 8px; }
    QScrollBar::handle:vertical:hover, QScrollBar::handle:horizontal:hover {
      background: #646464;
    }
    /* No arrow buttons: fixed-size line buttons make the groove degenerate on
       short scrollbars (collapsed docks), where the native styles shrink theirs. */
    QScrollBar::sub-line, QScrollBar::add-line {
      width: 0;
      height: 0;
      background: none;
      border: none;
    }
    QScrollBar::add-page, QScrollBar::sub-page {
      background: transparent;
    }
  )")
#endif
      ;
}

}  // namespace patchy::ui
