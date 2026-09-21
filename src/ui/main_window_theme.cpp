// The application-wide QSS theme, split out of main_window.cpp:
// photoshop_style() returns the stylesheet the MainWindow constructor applies
// to the whole window (declared in main_window_shared.hpp), resolved against the
// active color scheme. Colors live in theme_palette.hpp as @role_name tokens;
// never write a hex literal into the template below.

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
#include "ui/image_sequence_dialog.hpp"
#include "ui/sprite_sheet_dialog.hpp"
#include "ui/start_panel.hpp"
#include "ui/theme_qss.hpp"
#include "ui/theme_palette.hpp"
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

namespace {

// The stylesheet with its colors still written as @role_name tokens. Every
// color here must be a token: theme_palette.hpp owns the values, and
// ui_theme_qss_resolves_every_token fails if a token has no matching role.
QString photoshop_style_template() {
  return QStringLiteral(R"(
    QMainWindow, QMenuBar, QMenu, QDockWidget, QWidget {
      background: @window_bg;
      color: @text_primary;
      font-size: 12px;
    }
    QMainWindow {
      /* The main window's only exposed pixels are the dock-area separator
         gaps (Qt paints ::separator itself only mid-drag), so this is what
         makes the resting dividers between panels visible. */
      background: @dock_separator_bg;
      border: 1px solid @window_border;
    }
    QMainWindow::separator {
      background: @dock_separator_bg;
      width: 7px;
      height: 7px;
    }
    QMainWindow::separator:hover {
      background: @splitter_hover_bg;
    }
    QWidget#rightDockResizeHandle {
      background: @dock_separator_bg;
    }
    /* Floating dock windows, single and tab-grouped: the widened resize
       frame paints as divider chrome, and the border keeps the window's
       boundary readable over a same-value canvas backdrop. */
    QDockWidgetGroupWindow,
    QDockWidget[floatingChrome="true"] {
      background: @dock_separator_bg;
      border: 1px solid @panel_border_strong;
    }
    QWidget#rightDockResizeHandle:hover {
      background: @splitter_hover_bg;
    }
    QMenuBar {
      background: @title_bar_bg;
      color: @text_bright;
      border-bottom: 1px solid @title_bar_border;
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
      background: @menu_bar_item_hover_bg;
    }
    QLabel#patchyBadge {
      background: transparent;
      border: 0;
    }
    QMenu {
      background: @menu_bg;
      border: 1px solid @menu_border;
    }
    QMenu::item {
      padding: 7px 34px 7px 24px;
    }
    QMenu::item:selected {
      background: @menu_item_selected_bg;
      color: @text_on_accent;
    }
    QMenu::item:disabled {
      color: @text_disabled;
    }
    QMenuBar::item:disabled {
      color: @menu_bar_text_disabled;
    }
    QMenu::separator {
      height: 1px;
      background: @menu_separator;
      margin: 4px 6px;
    }
    QToolBar {
      background: @toolbar_bg;
      border: 0;
      border-bottom: 1px solid @toolbar_border;
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
    QToolButton#brushSmoothingOptionsButton {
      padding: 2px 1px;
      min-height: 20px;
      max-height: 20px;
    }
    QToolButton#brushSmoothingOptionsButton::menu-indicator {
      width: 0;
    }
    QToolButton#brushDynamicsButton[dynamicsActive="true"] {
      border-color: @accent_border_bright;
    }
    QToolButton:hover {
      background: @button_hover_bg;
      border-color: @button_hover_border;
    }
    QToolButton:checked {
      background: @accent_pressed_bg;
      border-color: @accent_border_bright;
    }
    /* Line-edit side widgets (the setClearButtonEnabled x, any addAction icon) are
       QToolButtons, so the rule above applied a 26 px minimum to them:
       QStyleSheetStyle turns min-width/min-height into a real minimum size, which
       QLineEdit's own 18 px placement is then clamped up to (34x34 with the box
       margins), hanging the x below the bottom of a filter field. The zeroes hand
       the placement back to QLineEdit, and the flat chrome keeps the hover rule
       from boxing the glyph. */
    QLineEdit QToolButton {
      background: transparent;
      border: none;
      min-width: 0;
      min-height: 0;
      padding: 0;
    }
    QWidget#windowChromeControls {
      background: @title_bar_bg;
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
      background: @window_chrome_hover_bg;
      border: 0;
    }
    QToolButton[windowChromeButton="true"]:pressed {
      background: @window_chrome_pressed_bg;
    }
    QToolButton#windowCloseButton:hover {
      background: @window_close_hover_bg;
    }
    QToolButton#windowCloseButton:pressed {
      background: @window_close_pressed_bg;
    }
    QToolBar#toolPalette {
      background: @tool_palette_bg;
      border-right: 1px solid @tool_palette_border;
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
    QToolBar#toolPalette QPushButton {
      min-width: 26px;
      max-width: 26px;
      min-height: 24px;
      max-height: 24px;
      padding: 0;
    }
    QToolButton[toolFlyout="true"]::menu-indicator {
      image: url(@icon(tool-flyout-corner));
      width: 7px;
      height: 7px;
      subcontrol-origin: padding;
      subcontrol-position: bottom right;
      bottom: 1px;
      right: 1px;
    }
    QToolBar#toolPalette::separator {
      background: @tool_palette_separator;
      height: 1px;
      margin: 3px 7px;
    }
    QWidget#toolPaletteSpacer {
      background: @tool_palette_bg;
    }
    QToolBar#Options {
      background: @options_bar_bg;
      min-height: 38px;
      border-top: 1px solid @options_bar_top_edge;
      border-bottom: 1px solid @toolbar_border;
      spacing: 5px;
      padding: 4px 7px;
    }
    QToolBar#Options QFrame#optionSeparator {
      color: @option_separator;
      max-width: 2px;
    }
    QToolBar#Options QLabel {
      color: @text_secondary;
      padding-left: 5px;
      padding-right: 2px;
    }
    QToolBar#Options QLabel[optionLabel="true"] {
      background: @option_chip_bg;
      border: 1px solid @field_inset_border;
      border-right: 0;
      border-top-color: @field_bevel_top;
      color: @text_bright;
      min-height: 24px;
      max-height: 24px;
      padding: 0 7px;
    }
    QToolBar#Options QSpinBox, QToolBar#Options QDoubleSpinBox, QToolBar#Options QComboBox, QToolBar#Options QFontComboBox {
      min-height: 24px;
      max-height: 24px;
      padding-left: 4px;
      background: @field_bg;
      border: 1px solid @field_inset_border;
      border-top-color: @field_bevel_top;
    }
    QWidget#selectionFeatherGroup {
      background: @field_bg;
      border: 1px solid @field_inset_border;
      border-top-color: @field_bevel_top;
      min-height: 24px;
      max-height: 24px;
    }
    QWidget#selectionFeatherGroup QLabel {
      background: @option_chip_bg;
      border: 0;
      border-right: 1px solid @field_inset_border;
      color: @text_bright;
      min-height: 24px;
      max-height: 24px;
      padding: 0 8px;
    }
    QWidget#selectionFeatherGroup QSpinBox {
      background: @field_bg;
      border: 0;
      min-height: 24px;
      max-height: 24px;
      padding-left: 6px;
    }
    QToolBar#Options QCheckBox {
      color: @text_bright;
      min-height: 24px;
      max-height: 24px;
      padding-left: 6px;
      padding-right: 8px;
      spacing: 6px;
    }
    QToolBar#Options QCheckBox#selectionAntiAliasCheck {
      background: @field_bg;
      border: 1px solid @field_inset_border;
      border-top-color: @field_bevel_top;
      padding-left: 7px;
      padding-right: 10px;
    }
    QToolBar#Options QCheckBox::indicator {
      width: 14px;
      height: 14px;
      background: @checkbox_compact_bg;
      border: 1px solid @checkbox_compact_border;
    }
    QToolBar#Options QCheckBox::indicator:hover {
      border-color: @checkbox_accent_border;
    }
    QToolBar#Options QCheckBox::indicator:checked {
      background: @accent;
      border-color: @checkbox_accent_border;
      image: url(@icon(checkmark));
    }
    QToolBar#Options QSlider::groove:horizontal {
      height: 4px;
      background: @slider_groove_bg;
      border: 1px solid @slider_groove_border;
    }
    QToolBar#Options QSlider::sub-page:horizontal {
      background: @accent;
      border: 1px solid @slider_fill_border;
    }
    QToolBar#Options QSlider::handle:horizontal {
      background: @slider_handle_bg;
      border: 1px solid @slider_handle_border;
      width: 10px;
      margin: -5px 0;
    }
    QToolBar#Options QPushButton {
      min-height: 24px;
      max-height: 24px;
      background: @options_button_bg;
      border: 1px solid @field_inset_border;
      border-top-color: @field_bevel_top;
      padding: 1px 7px;
    }
    QToolBar#Options QPushButton[optionsSessionButton="true"] {
      padding: 1px 2px; /* the 20px session icons need the width the default 7px padding eats */
    }
    QToolBar#Options QPushButton:checked {
      background: @accent_checked_bg;
      border-color: @accent_checked_border;
      color: @text_on_accent;
    }
    QDockWidget::title {
      background: @dock_title_bg;
      padding: 5px;
      border-bottom: 1px solid @panel_border_strong;
    }
    QWidget#historyDockTitle, QWidget#channelsDockTitle, QWidget#propertiesDockTitle, QWidget#infoDockTitle,
    QWidget#layersDockTitle, QWidget#pathsDockTitle, QWidget#paletteDockTitle {
      background: @panel_title_bg;
      border-top: 1px solid @panel_title_bevel_top;
      border-bottom: 1px solid @panel_title_border_bottom;
    }
    QWidget#historyDockTitle QLabel, QWidget#channelsDockTitle QLabel, QWidget#propertiesDockTitle QLabel,
    QWidget#infoDockTitle QLabel, QWidget#layersDockTitle QLabel, QWidget#pathsDockTitle QLabel,
    QWidget#paletteDockTitle QLabel {
      color: @text_bright;
      font-weight: 600;
    }
    QToolButton[dockCollapseButton="true"] {
      background: transparent;
      color: @dock_collapse_text;
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
      background: @dock_collapse_hover_bg;
      border-color: @dock_collapse_hover_border;
    }
    QToolButton[dockCollapseButton="true"]:checked {
      background: transparent;
      color: @dock_collapse_text;
      border-color: transparent;
    }
    QListWidget, QTreeWidget, QComboBox, QSpinBox, QSlider, QLineEdit, QTextEdit {
      background: @field_bg_large;
      color: @text_primary;
      border: 1px solid @field_border;
      min-height: 20px;
    }
    QListWidget, QTreeWidget, QComboBox {
      selection-background-color: @list_selection_bg;
    }
    /* Text selection inside entry fields uses the accent, not the muted list-row
       highlight: the gray list_selection_bg is nearly invisible behind selected
       text in a spin box. QDoubleSpinBox is listed explicitly because it is not
       a QSpinBox subclass and would otherwise fall back to the Qt default. */
    QSpinBox, QDoubleSpinBox, QLineEdit, QTextEdit {
      selection-background-color: @accent;
      selection-color: @text_on_accent;
    }
    QComboBox:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QLineEdit:disabled,
    QTextEdit:disabled {
      background: @field_bg_disabled;
      color: @field_text_disabled;
      border: 1px solid @field_border_disabled;
    }
    QListWidget::item {
      min-height: 48px;
      padding: 0;
      border-bottom: 1px solid @list_item_border;
    }
    QListWidget::item:selected {
      background: @list_selection_bg;
      color: @list_selection_text;
      border: 1px solid @list_selection_border;
    }
    QListWidget#layerList::item {
      color: transparent;
      background: transparent;
      border: 0;
    }
    QListWidget#layerList::item:selected {
      color: transparent;
      background: transparent;
      border: 0;
    }
    /* Layer rows paint through these app-level rules keyed on the
       layerRowSelected/layerRowGroup dynamic properties (restyle_layer_rows
       flips them). The transparent rule is load-bearing: these plain-QWidget
       containers inside each row otherwise match the global QWidget rule, get
       WA_StyledBackground auto-applied by the stylesheet engine, and paint
       opaque window_bg over the row background - which is what silently hid the
       selection highlight for months. */
    QWidget#layerMainRow, QWidget#layerSmartFiltersRow, QWidget#layerSmartFilterEntryRow {
      background: transparent;
    }
    QWidget#layerRowWidget {
      background: @layer_row_bg;
      border-bottom: 1px solid @layer_row_border;
    }
    QWidget#layerRowWidget[layerRowGroup="true"] {
      background: @layer_row_group_bg;
    }
    QWidget#layerRowWidget[layerRowSelected="true"] {
      background: @layer_row_selected_bg;
      border-bottom: 1px solid @layer_row_selected_border;
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
      border-bottom: 1px solid @category_list_item_border;
    }
    QListWidget#layerStyleCategoryList::item:selected {
      background: @category_selected_bg;
      color: @text_on_accent;
      border: 1px solid @category_selected_border;
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
      color: @layer_row_name_text;
      font-size: 12px;
    }
    QLabel#layerRowDetails {
      color: @layer_row_details_text;
      font-size: 10px;
    }
    QLabel#layerContentThumbnail[layerTargetActive="true"],
    QLabel#layerMaskThumbnail[layerTargetActive="true"],
    QLabel#layerSmartFilterMaskThumbnail[layerTargetActive="true"] {
      border: 2px solid @accent_bright;
      padding: 0;
    }
    QToolButton#maskEditModeChip {
      background: @accent_bright;
      color: @text_on_accent_bright;
      border: 1px solid @accent_bright_border;
      border-radius: 4px;
      padding: 2px 10px;
      font-weight: 600;
    }
    QToolButton#maskEditModeChip:hover {
      background: @accent_bright_hover;
    }
    QLineEdit#statusZoomEdit {
      background: @status_field_bg;
      color: @status_text;
      border: 1px solid @status_field_border;
      border-radius: 3px;
      padding: 0 5px;
      min-height: 16px;
      font-size: 11px;
    }
    QLineEdit#statusZoomEdit:focus {
      border-color: @accent_bright;
      color: @text_bright;
    }
    QLineEdit#statusZoomEdit:disabled {
      color: @status_text_disabled;
      border-color: @status_field_border_disabled;
    }
    QLabel#canvasInfoLabel, QLabel#documentInfoLabel {
      color: @info_text;
      line-height: 130%;
    }
    QScrollArea#propertiesScrollArea, QScrollArea#paletteScrollArea {
      background: @panel_bg;
      border: 0;
    }
    QScrollArea#paletteScrollArea > QWidget > QWidget {
      background: @panel_bg;
    }
    QWidget#propertiesPanel {
      background: @panel_bg;
    }
    QLabel#documentInfoLabel, QLabel#activeLayerInfoLabel, QLabel#activeLayerGeometryLabel,
    QLabel#activeLayerMaskLabel, QLabel#activeLayerAdjustmentLabel, QLabel#activeLayerTextLabel,
    QLabel#activeToolInfoLabel {
      background: @panel_inset_bg;
      border: 1px solid @panel_inset_border;
      padding: 4px;
      color: @info_text;
      font-size: 11px;
    }
    QWidget#layersPanel, QWidget#channelPanel, QWidget#pathsPanel, QWidget#infoPanel,
    QWidget#palettePanel {
      background: @panel_bg;
    }
    QListWidget#layerList {
      min-height: 120px;
    }
    QToolButton#layerFolderDisclosureButton {
      background: transparent;
      color: @layer_glyph_text;
      border: 1px solid transparent;
      border-radius: 3px;
      padding: 0;
      min-width: 18px;
      max-width: 18px;
      min-height: 20px;
      max-height: 20px;
    }
    QToolButton#layerFolderDisclosureButton:hover {
      border-color: @layer_button_hover_border;
      background: @layer_button_hover_bg;
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
      color: @layer_visibility_text;
      border: 1px solid transparent;
      border-radius: 3px;
      padding: 0;
      min-width: 22px;
      max-width: 22px;
      min-height: 22px;
      max-height: 22px;
    }
    QToolButton#layerVisibilityCheck:hover {
      background: @layer_button_hover_bg;
      border-color: @layer_button_hover_border;
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
      background: @panel_inset_bg;
      border: 1px solid @layer_lock_border;
      border-radius: 3px;
      padding: 0;
      min-width: 24px;
      max-width: 24px;
      min-height: 24px;
      max-height: 24px;
    }
    QToolButton[layerLockControl="true"]:hover {
      background: @layer_button_hover_bg;
      border-color: @layer_lock_hover_border;
    }
    QToolButton[layerLockControl="true"]:checked {
      background: @layer_lock_checked_bg;
      border-color: @layer_lock_checked_border;
    }
    QToolButton[layerLockControl="true"][mixed="true"] {
      background: @layer_lock_mixed_bg;
      border-color: @layer_lock_mixed_border;
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
      background: @layer_button_hover_bg;
      border-color: @layer_button_hover_border;
    }
    QPushButton {
      background: @button_bg;
      color: @text_primary;
      border: 1px solid @button_border;
      border-radius: 0;
      padding: 4px 8px;
    }
    QPushButton:hover {
      background: @button_hover_bg;
      border-color: @button_hover_border_strong;
    }
    QPushButton:checked {
      background: @accent_checked_bg;
      border-color: @accent_checked_border;
      color: @text_on_accent;
    }
    QPushButton:disabled {
      background: @field_bg_disabled;
      color: @field_text_disabled;
      border-color: @field_border_disabled;
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
      min-width: 38px;
      max-width: 38px;
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
      background: @layer_drop_bg;
      border: 2px solid @accent_bright;
      padding: 0;
    }
    QStatusBar {
      background: @status_bar_bg;
      color: @status_text;
    }
    QLabel {
      color: @text_secondary;
      /* Transparent, not the global QWidget window_bg: labels sit on panels of other
         shades (e.g. the Preferences panels) and an opaque fill shows as a
         mismatched strip behind the text. */
      background: transparent;
    }
    QCheckBox {
      color: @text_secondary;
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
      background: @checkbox_indicator_bg;
      border: 1px solid @checkbox_indicator_border;
    }
    QCheckBox::indicator:hover {
      border-color: @checkbox_accent_border;
    }
    QCheckBox::indicator:checked {
      background: @accent;
      border-color: @checkbox_accent_border;
      image: url(@icon(checkmark));
    }
    QTabWidget::pane {
      border-top: 1px solid @tab_pane_border;
    }
    QTabBar::tab {
      background: @tab_bg;
      color: @text_secondary;
      border: 1px solid @tab_bg;
      padding: 5px 12px;
      min-height: 20px;
    }
    QTabBar::tab:hover:!selected {
      background: @tab_hover_bg;
    }
    QTabBar::tab:selected {
      background: @tab_selected_bg;
      color: @text_on_raised;
      border-bottom-color: @tab_selected_bg;
    }
    /* A QTabWidget always has a current tab, but while a float window holds the
       active document no tab IS the active document: the document tab bar's
       documentTabsInactive property makes the current tab paint like an
       unselected one (Photoshop dims it the same way). */
    QTabBar#documentTabBar[documentTabsInactive="true"]::tab:selected {
      background: @tab_bg;
      color: @text_secondary;
      border-bottom-color: @tab_bg;
    }
    /* The tab-overflow scroll arrows are QToolButtons whose geometry comes from
       the style's scroll-button metric, not from a layout, so the global
       QToolButton minimums shove the arrow glyph off-center and the right arrow
       clips at the bar's edge. Give the box back to the metric, pinned to the
       same width on every platform (macOS otherwise defaults narrower). The
       scroller width is the whole two-button area, 16px per arrow. Padding must
       stay 0 here: the Windows base style shrinks its themed arrow toward a dot
       when the content rect tightens. macOS pads below instead, because its
       base style scales the glyph edge-to-edge. */
    QTabBar::scroller {
      width: 32px;
    }
    QTabBar QToolButton {
      min-width: 0;
      min-height: 0;
      padding: 0;
    }
  )")
         // The canvas scroll bars are document-window chrome, so their track slaves
         // to the canvas backdrop (Photoshop's document window) rather than to the
         // window surface every other bar sits on. That one role is the only thing
         // separating them from the panel bars below, whose rules also match them;
         // these ID selectors are more specific and win.
         + QStringLiteral(R"(
    QScrollBar#canvasHorizontalScrollBar, QScrollBar#canvasVerticalScrollBar {
      background: @canvas_scrollbar_track;
      background-image: url(@icon(scroll-dither));
    }
    QScrollBar#canvasVerticalScrollBar:vertical { width: 16px; }
    QScrollBar#canvasHorizontalScrollBar:horizontal { height: 16px; }
    QScrollBar#canvasHorizontalScrollBar::handle, QScrollBar#canvasVerticalScrollBar::handle {
      background: @scrollbar_handle_bg;
      border: 1px solid @scrollbar_handle_border;
    }
    QScrollBar#canvasVerticalScrollBar::handle:vertical { min-height: 8px; }
    QScrollBar#canvasHorizontalScrollBar::handle:horizontal { min-width: 8px; }
    QScrollBar#canvasHorizontalScrollBar::handle:hover, QScrollBar#canvasVerticalScrollBar::handle:hover {
      background: @scrollbar_handle_hover_bg;
    }
    QScrollBar#canvasHorizontalScrollBar::sub-line, QScrollBar#canvasHorizontalScrollBar::add-line,
    QScrollBar#canvasVerticalScrollBar::sub-line, QScrollBar#canvasVerticalScrollBar::add-line {
      width: 0;
      height: 0;
      background: none;
      border: none;
    }
    QScrollBar#canvasHorizontalScrollBar::add-page, QScrollBar#canvasHorizontalScrollBar::sub-page,
    QScrollBar#canvasVerticalScrollBar::add-page, QScrollBar#canvasVerticalScrollBar::sub-page {
      background: transparent;
    }
  )")
#ifdef Q_OS_MACOS
         // macOS-only: QMacStyle group boxes carry Aqua-sized native chrome (big
         // title gap and content margins, plus Aqua layout-item overlaps since
         // their rule border stays native), which blows dense panels like the brush
         // Dynamics popup past the screen height.
         + QStringLiteral(R"(
    QGroupBox {
      border: 1px solid @group_box_border;
      border-radius: 3px;
      margin-top: 8px;
      padding: 2px 2px 2px 2px;
    }
    QGroupBox::title {
      subcontrol-origin: margin;
      subcontrol-position: top left;
      left: 8px;
      padding: 0 3px;
      background: @window_bg;
    }
    /* QMacStyle scales the tab-scroller arrow to fill the button's content rect
       edge-to-edge; this padding is what gives the glyph its margins (see the
       QTabBar QToolButton comment above; Windows/Linux draw fixed-size arrows
       and need padding 0 there). */
    QTabBar QToolButton {
      padding: 2px;
    }
  )")
#endif
         // Panel, dialog and list scroll bars, on every platform.
         //
         // These cannot be left to the native style, however much it looks like
         // they could be. The QWidget rule at the top of this sheet sets a
         // background on every widget in the application, scroll bars included, and
         // once QSS touches a scroll bar QStyleSheetStyle owns the entire complex
         // control. With no subcontrol rules it fills the groove with the window
         // background and leaves the base style to draw the rest on top. Against
         // Dark's near-black surface that passed for native rendering. In Light the
         // groove, the handle and the panel behind them all resolve to the same
         // near-white and the bar disappears, leaving only the arrow glyphs as two
         // faint marks. So every subcontrol gets a rule.
         + QStringLiteral(R"(
    /* Flat, unlike the canvas bars above. The dither is a single asset shared by
       both, and it can only suit one of them: the two tracks are the same value in
       Dark but Light pins the canvas track to the mid-gray pasteboard and derives
       the panel track to near-white, and a mid-gray checkerboard laid over that
       turns a dialog's gutter into a dark stripe. The texture belongs to the
       pasteboard gutter; a panel bar reads from its handle. */
    /* margin: 0 is load-bearing, not a no-op. QStyleSheetStyle only takes
       ownership of the groove rect when the scroll bar's widget rule has box
       properties; without one it asks the native style, which reserves room
       for the arrow buttons this sheet removes. On a short bar (a squeezed
       panel) that phantom reservation leaves the groove smaller than the
       styled handle, the drag span goes negative, and every handle drag
       snaps the value to the minimum. */
    QScrollBar:vertical {
      background: @panel_scrollbar_track;
      width: 16px;
      margin: 0;
    }
    QScrollBar:horizontal {
      background: @panel_scrollbar_track;
      height: 16px;
      margin: 0;
    }
    QScrollBar::handle:vertical, QScrollBar::handle:horizontal {
      background: @scrollbar_handle_bg;
      border: 1px solid @scrollbar_handle_border;
    }
    QScrollBar::handle:vertical { min-height: 8px; }
    QScrollBar::handle:horizontal { min-width: 8px; }
    QScrollBar::handle:vertical:hover, QScrollBar::handle:horizontal:hover {
      background: @scrollbar_handle_hover_bg;
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
      ;
}

}  // namespace

static QString compositor_style_template() {
  /* Native-macOS / Compositor chrome delta: flat unified surfaces, hairline
     borders instead of bevels, pill controls, macOS sliders and checkboxes,
     overlay scroll bars, SF Pro font stack. Token-only; adapts to both schemes. */
  QString sheet = QStringLiteral(R"(
        /* ---------- Typography ---------- */
        QMainWindow, QMenuBar, QMenu, QDockWidget, QWidget, QDialog {
          font-family: "__PATCHY_UI_FONT__";
          font-size: 13px;
        }
        QWidget#layersDockTitle QLabel, QWidget#channelsDockTitle QLabel,
        QWidget#historyDockTitle QLabel, QWidget#pathsDockTitle QLabel,
        QWidget#propertiesDockTitle QLabel, QWidget#infoDockTitle QLabel,
        QWidget#paletteDockTitle QLabel {
          font-weight: 600;
          letter-spacing: 0.2px;
        }

        /* ---------- Flat window + unified top chrome ---------- */
        QMainWindow, QDockWidget, QDialog { border: 0; }
        QMenuBar {
          background: @window_bg;
          color: @text_bright;
          border-bottom: 1px solid @window_border;
          min-height: 36px;
          max-height: 36px;
          padding-left: 12px;
        }
        QMenuBar::item {
          background: transparent;
          border-radius: 5px;
          padding: 4px 10px;
          margin: 4px 1px;
        }
        QMenuBar::item:selected {
          background: @button_hover_bg;
          color: @text_bright;
        }
        QMenu {
          background: @menu_bg;
          border: 1px solid @menu_border;
          border-radius: 8px;
          padding: 5px 0;
        }
        QMenu::item { padding: 5px 30px 5px 24px; }
        QMenu::item:selected {
          background: @menu_item_selected_bg;
          color: @text_on_accent;
        }
        QMenu::separator { margin: 5px 12px; }

        /* ---------- Tool rail: flat, breathing room, pill buttons ---------- */
        QToolBar#toolPalette {
          background: @window_bg;
          border: 0;
          border-right: 1px solid @window_border;
          padding: 6px 5px;
          spacing: 2px;
        }
        QToolBar#toolPalette QToolButton {
          background: transparent;
          border: 0;
          border-radius: 7px;
          min-width: 30px;
          max-width: 30px;
          min-height: 28px;
          max-height: 28px;
          padding: 0;
        }
        QToolBar#toolPalette QToolButton:hover {
          background: @button_hover_bg;
          border: 0;
        }
        QToolBar#toolPalette QToolButton:checked {
          background: @compositor_selected_chrome_bg;
          border: 1px solid @compositor_selected_chrome_border;
          border-radius: 7px;
        }
        QToolBar#toolPalette::separator {
          background: @window_border;
          height: 1px;
          margin: 5px 9px;
        }
        QWidget#toolPaletteSpacer { background: @window_bg; }

        /* ---------- Options bar: one flat strip ---------- */
        QToolBar#Options {
          background: @window_bg;
          border: 0;
          border-bottom: 1px solid @window_border;
          min-height: 42px;
          spacing: 7px;
          padding: 5px 12px;
        }
        QToolBar#Options::separator {
          background: @window_border;
          width: 1px;
          margin: 7px 4px;
        }
        QToolBar#Options QLabel {
          color: @text_secondary;
          background: transparent;
          border: 0;
          padding: 0 2px;
        }
        QToolBar#Options QToolButton {
          background: transparent;
          border: 0;
          border-radius: 6px;
          padding: 4px;
        }
        QToolBar#Options QToolButton:hover { background: @button_hover_bg; }
        QToolBar#Options QToolButton:checked {
          background: @compositor_accent;
          border: 0;
        }
        QToolBar#Options QToolButton[optionsBarButton="true"] {
          background: @field_bg;
          border: 1px solid @window_border;
          border-radius: 6px;
          min-height: 22px;
          padding: 2px 8px;
        }
        QToolBar#Options QToolButton[optionsBarButton="true"]:hover {
          background: @button_hover_bg;
          border-color: @field_border;
        }
        QToolBar#Options QToolButton[optionsBarButton="true"]:checked {
          background: @compositor_accent;
          border-color: @compositor_accent;
        }


        /* ---------- Fields: rounded, hairline, no bevel ---------- */
        QSpinBox, QDoubleSpinBox, QComboBox, QFontComboBox, QLineEdit, QTextEdit {
          background: @field_bg_large;
          border: 1px solid @field_border;
          border-radius: 6px;
          padding: 2px 7px;
          min-height: 20px;
        }
        QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus, QLineEdit:focus,
        QTextEdit:focus { border-color: @compositor_accent; }
        QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled,
        QLineEdit:disabled, QTextEdit:disabled {
          background: @field_bg_disabled;
          color: @field_text_disabled;
          border-color: @field_border_disabled;
        }
        QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button,
        QDoubleSpinBox::down-button {
          background: transparent;
          border: 0;
          width: 14px;
        }
        QSpinBox::up-button:hover, QSpinBox::down-button:hover,
        QDoubleSpinBox::up-button:hover, QDoubleSpinBox::down-button:hover {
          background: @button_hover_bg;
          border-radius: 4px;
        }
        QComboBox::drop-down { border: 0; width: 20px; }
        QComboBox QAbstractItemView {
          background: @popup_list_bg;
          border: 1px solid @menu_border;
          border-radius: 8px;
          selection-background-color: @list_selection_bg;
          selection-color: @list_selection_text;
          outline: 0;
          padding: 4px;
        }
        QListWidget, QTreeWidget {
          background: @field_bg_large;
          border: 1px solid @field_border;
          border-radius: 8px;
          padding: 3px;
          outline: 0;
        }
        QListWidget::item {
          border: 0;
          border-radius: 6px;
          min-height: 22px;
          padding: 2px 6px;
        }
        QListWidget::item:selected {
          background: @list_selection_bg;
          color: @list_selection_text;
          border: 0;
        }
        QListWidget::item:hover { background: @button_hover_bg; }

        /* ---------- Buttons: filled pill, flat hover ---------- */
        QPushButton {
          background: @button_bg;
          border: 1px solid @button_border;
          border-radius: 7px;
          padding: 4px 14px;
          min-height: 20px;
        }
        QPushButton:hover {
          background: @neutral_button_hover_bg;
          border-color: @neutral_button_hover_border;
        }
        QPushButton:checked {
          background: @compositor_accent;
          border-color: @compositor_accent;
          color: @text_on_accent;
        }
        QPushButton:default {
          background: @compositor_accent;
          border-color: @compositor_accent;
          color: @text_on_accent;
          font-weight: 600;
        }
        QToolBar#Options QPushButton {
          background: @field_bg;
          border: 1px solid @window_border;
          border-radius: 6px;
          padding: 3px 10px;
          min-height: 22px;
        }
        QToolBar#Options QPushButton:checked {
          background: @compositor_accent;
          border-color: @compositor_accent;
          color: @text_on_accent;
        }

        /* ---------- Checkbox: macOS rounded indicator ---------- */
        QCheckBox { color: @text_bright; spacing: 7px; }
        QCheckBox::indicator {
          width: 16px;
          height: 16px;
          border-radius: 5px;
          background: @checkbox_compact_bg;
          border: 1px solid @checkbox_compact_border;
        }
        QCheckBox::indicator:hover { border-color: @checkbox_accent_border; }
        QCheckBox::indicator:checked {
          background: @compositor_accent;
          border-color: @compositor_accent;
          image: url(@icon(checkmark));
        }

        /* ---------- Slider: macOS track + round handle ---------- */
        QSlider { min-height: 22px; }
        QSlider::groove:horizontal {
          height: 4px;
          border: 0;
          border-radius: 2px;
          background: @slider_groove_bg;
        }
        QSlider::sub-page:horizontal {
          background: @compositor_accent;
          border: 0;
          border-radius: 2px;
        }
        QSlider::handle:horizontal {
          background: @slider_handle_bg;
          border: 0;
          width: 18px;
          height: 18px;
          margin: -7px 0;
          border-radius: 9px;
        }

    /* ---------- Docks: flat title bars ---------- */
        QDockWidget::title {
          background: @window_bg;
          padding: 8px 10px;
          border: 0;
          border-bottom: 1px solid @window_border;
        }
        QWidget#layersDockTitle, QWidget#channelsDockTitle, QWidget#historyDockTitle,
        QWidget#pathsDockTitle, QWidget#propertiesDockTitle, QWidget#infoDockTitle,
        QWidget#paletteDockTitle {
          background: @window_bg;
          border: 0;
          border-bottom: 1px solid @window_border;
        }
        QMainWindow::separator {
          background: @window_bg;
          width: 4px;
          height: 4px;
        }
        QMainWindow::separator:hover { background: @splitter_hover_bg; }

        /* ---------- Tabs: soft rounded ---------- */
        QTabBar { background: transparent; }
        QTabBar::tab {
          background: transparent;
          border: 0;
          border-radius: 6px;
          padding: 5px 14px;
          margin: 3px 2px 0 2px;
          color: @text_secondary;
        }
        QTabBar::tab:hover {
          background: @button_hover_bg;
          color: @text_bright;
        }
        QTabBar::tab:selected {
          background: @tab_selected_bg;
          color: @text_bright;
        }

        /* ---------- Scroll bars: overlay style ---------- */
        QScrollBar:vertical {
          background: transparent;
          width: 11px;
          margin: 2px;
        }
        QScrollBar:horizontal {
          background: transparent;
          height: 11px;
          margin: 2px;
        }
        QScrollBar::handle:vertical, QScrollBar::handle:horizontal {
          background: @scrollbar_handle_bg;
          border: 0;
          border-radius: 4px;
          min-height: 24px;
          min-width: 24px;
        }
        QScrollBar::handle:vertical:hover, QScrollBar::handle:horizontal:hover {
          background: @scrollbar_handle_hover_bg;
        }
        QScrollBar::add-line, QScrollBar::sub-line {
          width: 0;
          height: 0;
          background: none;
          border: none;
        }
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

        /* ---------- Status bar: flat strip ---------- */
        QStatusBar {
          background: @window_bg;
          color: @text_secondary;
          border-top: 1px solid @window_border;
        }
        QLineEdit#statusZoomEdit {
          background: @field_bg;
          border: 1px solid @window_border;
          border-radius: 5px;
          padding: 1px 7px;
        }

        /* ---------- Assorted ---------- */
        QProgressBar {
          border: 0;
          background: @field_bg;
          border-radius: 4px;
          text-align: center;
          color: @text_bright;
        }
        QProgressBar::chunk {
          background: @compositor_accent;
          border-radius: 4px;
        }
        QToolTip {
          background: @menu_bg;
          color: @text_bright;
          border: 1px solid @menu_border;
          border-radius: 6px;
          padding: 4px 8px;
        }

        /* ---------- De-bevel the remaining options-bar chips ---------- */
        QToolBar#Options QLabel[optionLabel="true"] {
          border: 1px solid @field_border;
          border-right: 0;
          border-top-left-radius: 6px;
          border-bottom-left-radius: 6px;
        }
        QWidget#selectionFeatherGroup {
          border: 1px solid @field_border;
          border-radius: 6px;
        }
        QWidget#selectionFeatherGroup QLabel {
          border: 0;
          border-top-left-radius: 6px;
          border-bottom-left-radius: 6px;
        }
        QToolBar#Options QCheckBox#selectionAntiAliasCheck {
          border: 1px solid @field_border;
          border-radius: 6px;
        }
        QToolBar#Options QComboBox, QToolBar#Options QFontComboBox,
        QToolBar#Options QSpinBox, QToolBar#Options QDoubleSpinBox {
          border: 1px solid @field_border;
          border-radius: 6px;
        }

        /* ---------- Right panels read as one continuous dark surface ---------- */
        QWidget#layersPanel, QWidget#channelPanel, QWidget#pathsPanel,
        QWidget#infoPanel, QWidget#palettePanel, QWidget#propertiesPanel {
          background: @window_bg;
        }
        QListWidget#layerList, QListWidget#channelList, QListWidget#pathsList,
        QListWidget#historyList {
          background: @window_bg;
          border: 0;
        }

        /* ---------- Tab strips sit on the window surface ---------- */
        QTabBar {
          background: @window_bg;
          border: 0;
        }
        QTabBar::tab {
          background: transparent;
          border: 1px solid transparent;
          border-radius: 6px;
          padding: 5px 14px;
          margin: 4px 2px 0 2px;
          color: @text_secondary;
        }
        QTabBar::tab:hover:!selected {
          background: @button_hover_bg;
          color: @text_bright;
        }
        QTabBar::tab:selected {
          background: @tab_selected_bg;
          color: @text_bright;
          border: 1px solid @window_border;
          border-bottom-color: @tab_selected_bg;
        }
        QTabWidget::pane {
          border-top: 1px solid @window_border;
        }

        /* ---------- Compositor parity: flat labels, wells, dividers ---------- */
        /* Non-button toolbar objects carry no chrome at all. */
        QToolBar#Options QSlider {
          background: transparent;
          border: 0;
        }
        QToolBar#Options QLabel {
          background: transparent;
          border: 0;
        }

        /* Dropdowns and text fields: borderless dark wells. */
        QSpinBox, QDoubleSpinBox, QComboBox, QFontComboBox, QLineEdit, QTextEdit {
          background: @compositor_field_bg;
          border: 0;
        }
        QToolBar#Options QSpinBox, QToolBar#Options QDoubleSpinBox,
        QToolBar#Options QComboBox, QToolBar#Options QFontComboBox {
          background: @compositor_field_bg;
          border: 0;
        }
        QToolBar#Options QLabel[optionLabel="true"] {
          background: @compositor_field_bg;
          border: 0;
        }
        QWidget#selectionFeatherGroup {
          background: @compositor_field_bg;
          border: 0;
        }
        QWidget#selectionFeatherGroup QLabel {
          background: @compositor_field_bg;
          border: 0;
        }
        QToolBar#Options QCheckBox#selectionAntiAliasCheck {
          background: @compositor_field_bg;
          border: 0;
        }
        QComboBox QAbstractItemView {
          background: @compositor_field_bg;
          border: 0;
        }

        /* The dock divider and its resize handle stay grabbable but paint
           nothing: canvas and panels touch directly. */
        QMainWindow {
          background: @window_bg;
        }
        QMainWindow::separator {
          background: @window_bg;
          border: 0;
        }
        QMainWindow::separator:hover {
          background: @splitter_hover_bg;
          border: 0;
        }
        QWidget#rightDockResizeHandle:hover {
          background: transparent;
        }

        /* Layer rows: unselected are fully transparent; the selection is the
           Compositor blue. */
        QWidget#layerRowWidget {
          background: transparent;
          border-bottom: 0;
        }
        QWidget#layerRowWidget[layerRowGroup="true"] {
          background: transparent;
        }
        QWidget#layerRowWidget[layerRowSelected="true"] {
          background: @compositor_selected_bg;
          border-bottom: 0;
        }

        /* Bottom utility icons: bare glyphs, no button chrome ever. */
        QPushButton[layerActionButton="true"],
        QToolButton#layerNewAdjustmentButton {
          background: transparent;
          border: 0;
        }
        QPushButton[layerActionButton="true"]:hover,
        QToolButton#layerNewAdjustmentButton:hover {
          background: transparent;
          border: 0;
        }

        /* Buttons: borderless; standard is a dark gray, primary/active is the
           exact system blue. */
        QPushButton {
          background: @compositor_button_bg;
          border: 0;
        }
        QPushButton:hover, QPushButton:pressed {
          background: @neutral_button_hover_bg;
          border: 0;
        }
        QPushButton:checked, QPushButton:default {
          background: @compositor_primary_bg;
          border: 0;
        }
        QToolBar#Options QPushButton {
          background: @compositor_button_bg;
          border: 0;
        }
        QToolBar#Options QPushButton:checked {
          background: @compositor_primary_bg;
          border: 0;
        }
        QToolBar#Options QToolButton[optionsBarButton="true"] {
          background: @compositor_field_bg;
          border: 0;
        }
        QToolBar#Options QToolButton[optionsBarButton="true"]:hover {
          background: @button_hover_bg;
          border: 0;
        }
        QToolBar#Options QToolButton[optionsBarButton="true"]:checked {
          background: @compositor_primary_bg;
          border: 0;
        }

        /* ---------- Corrections pass: chrome that must not paint at all ---------- */
        /* Options-bar text carries NO background: the chips only looked flat
           because the field next to them was dark. */
        QToolBar#Options QLabel,
        QToolBar#Options QLabel[optionLabel="true"] {
          background: transparent;
          border: 0;
        }
        QWidget#selectionFeatherGroup QLabel {
          background: transparent;
          border: 0;
        }
        /* The window frame itself carries no border in this skin. */
        QMainWindow {
          background: @window_bg;
          border: 0;
        }
        /* The dock-width handle overlays each dock's left edge. Painting it the
           pasteboard color makes the divider read as canvas instead of a band,
           while the widget keeps its width and cursor for resizing. */
        QWidget#rightDockResizeHandle {
          background: transparent;
        }
        QWidget#rightDockResizeHandle:hover {
          background: @splitter_hover_bg;
        }

        /* ---------- Layer lock controls ---------- */
        QToolButton[layerLockControl="true"] {
          background: transparent;
          border: 0;
        }
        QToolButton[layerLockControl="true"]:hover {
          background: transparent;
          border: 0;
        }
        QToolButton[layerLockControl="true"]:checked {
          background: @compositor_selected_chrome_bg;
          border: 1px solid @compositor_selected_chrome_border;
          border-radius: 4px;
        }
        QToolButton[layerLockControl="true"][mixed="true"] {
          background: transparent;
          border: 0;
        }

        /* ---------- Tool rail sizing ---------- */
        /* Only a floor, never a ceiling: a forced max-width fights Qt's
           overflow extension, and the rail's buttons then land off-grid after
           an expand/collapse cycle. */
        QToolBar#toolPalette QToolButton {
          min-width: 30px;
          min-height: 30px;
          max-width: 40px;
          max-height: 40px;
          padding: 0 2px;
        }
        /* Qt's toolbar overflow button sizes itself; leave it alone. */
        QToolBar#toolPalette QToolButton#qt_toolbar_ext_button {
          min-width: 0;
          min-height: 0;
          max-width: 40px;
          max-height: 40px;
          padding: 0;
        }

        /* ---------- Welcome screen buttons ---------- */
        /* The start panel installs its own sheet, so these carry an extra
           ancestor in the selector to out-rank it. They exist only inside this
           delta, which means the Photoshop skin never sees them. */
        QMainWindow QWidget#startPanel QPushButton {
          background: @compositor_button_bg;
          border: 0;
          border-radius: 14px;
        }
        QMainWindow QWidget#startPanel QPushButton:hover {
          background: @neutral_button_hover_bg;
          border: 0;
        }
        QMainWindow QWidget#startPanel QPushButton#startPanelNewButton {
          background: @compositor_primary_bg;
          border: 0;
        }
        QMainWindow QWidget#startPanel QPushButton#startPanelNewButton:hover {
          background: @compositor_primary_bg;
          border: 0;
        }

        /* ---------- Combo chevron + slider chrome ---------- */
        QComboBox::drop-down {
          background: transparent;
          border: 0;
          width: 20px;
        }
        QComboBox::down-arrow {
          background: transparent;
          border: 0;
        }
        QSlider,
        QSlider::groove:horizontal, QSlider::groove:vertical,
        QSlider::sub-page:horizontal, QSlider::sub-page:vertical,
        QSlider::add-page:horizontal, QSlider::add-page:vertical,
        QSlider::handle:horizontal, QSlider::handle:vertical {
          border: 0;
        }
  )");
#ifdef Q_OS_MACOS
  sheet.replace(QStringLiteral("__PATCHY_UI_FONT__"),
                QStringLiteral("-apple-system, 'SF Pro Text', 'SF Pro Display',"
                               " 'Helvetica Neue', 'Segoe UI', 'Roboto', Arial, sans-serif"));
#else
  sheet.replace(QStringLiteral("__PATCHY_UI_FONT__"),
                QStringLiteral("'Segoe UI', 'Roboto', 'Helvetica Neue', Arial, sans-serif"));
#endif
  return sheet;
}

QString window_style() {
  return active_window_appearance() == WindowAppearance::Compositor
             ? apply_theme_tokens(photoshop_style_template()
                                  + compositor_style_template())
             : photoshop_style();
}

QString photoshop_style() {
  // Both palettes are compile-time constants, so a scheme's resolved sheet never
  // changes once built and can be cached for the process lifetime.
  static std::array<QString, 2> resolved;
  auto& cached = resolved[active_color_scheme() == ColorScheme::Light ? 1 : 0];
  if (cached.isEmpty()) {
    cached = apply_theme_tokens(photoshop_style_template());
  }
  return cached;
}

void MainWindow::apply_color_scheme() {
  // set_active_color_scheme() has already run inside ThemeManager, so every
  // theme() read below sees the new palette.

  // 1. The window's own sheet. Qt repolishes the entire child tree from here,
  //    which covers every dock, panel, and parented dialog, plus
  //    DocumentFloatWindow (stylesheet propagation follows the QObject parent
  //    chain, and a float window is a child of this window).
  setStyleSheet(window_style());

  // 2. Top-level windows that are not in this window's child tree, plus any
  //    widget deeper in the tree that carries its own themed template (the
  //    chromed dialogs, per-instance color swatches, the start panel).
  rebuild_themed_styles_in(*this);
  for (auto* top_level : QApplication::topLevelWidgets()) {
    if (top_level == nullptr || top_level == this) {
      continue;
    }
    // Translucent click-through overlays (the screen color picker) paint a
    // single near-transparent fill and must not be restyled or repolished.
    if (top_level->testAttribute(Qt::WA_TranslucentBackground)) {
      continue;
    }
    rebuild_themed_styles_in(*top_level);
    top_level->style()->unpolish(top_level);
    top_level->style()->polish(top_level);
    top_level->update();
  }

  // 3. Pixmaps built from theme colors. Qt sends no event for a palette-struct
  //    change, so anything cached has to be dropped by hand or it keeps
  //    painting the old scheme forever.
  layer_thumbnail_cache_.clear();
  channel_thumbnail_cache_.clear();
  path_thumbnail_cache_.clear();
  refresh_layer_list();
  refresh_channel_panel();
  refresh_paths_panel();
  // Future-state rows stamp theme().history_future_text as a foreground brush.
  refresh_history_panel();

  // 4. Canvas chrome is painted, not styled, so it needs an explicit repaint.
  for (const auto& session : sessions_) {
    if (session == nullptr) {
      continue;
    }
    if (session->canvas != nullptr) {
      session->canvas->update();
    }
    if (session->float_window != nullptr) {
      session->float_window->update();
    }
  }
  update();
}

}  // namespace patchy::ui
