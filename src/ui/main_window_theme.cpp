// The application-wide QSS theme, split out of main_window.cpp:
// photoshop_style() returns the stylesheet the MainWindow constructor applies
// to the whole window (declared in main_window_shared.hpp), resolved against the
// active color scheme. Colors live in theme_palette.hpp as @role_name tokens;
// never write a hex literal into the template below.

#include "ui/main_window.hpp"
#include "ui/theme_script_host.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/document_float_window.hpp"
#include "ui/theme_apply_trace.hpp"
#include "ui/theme_palette.hpp"
#include "ui/theme_qss.hpp"
#include "ui/tool_palette_footer.hpp"

#include <QApplication>
#include <QListWidget>
#include <QRegularExpression>
#include <QString>
#include <QStyle>
#include <QWidget>

// Icon resources live in the static patchy_ui library; force registration before first use.
int qInitResources_icons();

namespace patchy::ui {

namespace {

// The stylesheet with its colors still written as @role_name tokens. Every
// color here must be a token: theme_palette.hpp owns the values, and
// ui_theme_qss_resolves_every_token fails if a token has no matching role.
QString photoshop_style_template() {
  return QStringLiteral(R"(
    QMainWindow, QMenuBar, QMenu, QWidget {
      background: @window_bg;
      color: @text_primary;
      font-size: 12px;
    }
    QMainWindow {
      background: @dock_separator_bg;
      border: 1px solid @window_border;
    }
    /* The panel column's dividers: its edge beside the document tabs
       (rightDockResizeHandle) and the strips between its sections. Styled
       through ::handle so the platform style draws no grip dots on them. */
    QSplitter#workspaceSplitter::handle,
    QSplitter#panelColumnSplitter::handle {
      background: @dock_separator_bg;
    }
    QSplitter#workspaceSplitter::handle:hover,
    QSplitter#panelColumnSplitter::handle:hover {
      background: @splitter_hover_bg;
    }
    QMenuBar {
      background: @title_bar_bg;
      color: @text_bright;
      border-bottom: 1px solid @title_bar_border;
      min-height: 34px;
      max-height: 34px;
    }
    /* Room for the app badge (configure_window_chrome). Only the frameless chrome draws
       one; under a desktop's own title bar the menus start at the edge (GitHub issue 196). */
    QMenuBar[patchyBadgeChrome="true"] {
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
    /* 20 px content, 2 px padding and the border: 26 px, the bar's field height, so a
       row of icon buttons centers on the strip like the fields (GitHub issue 409).
       QStyleSheetStyle inflated the old 16 px minimum to 27 px. */
    QToolButton[optionsBarButton="true"] {
      padding: 2px;
      min-width: 18px;
      min-height: 20px;
      max-height: 20px;
    }
    /* The tip picker reads as a field with a dropdown chevron, like the Artboard bar's
       preset picker; BrushTipPicker::paintEvent draws the thumbnail, name and chevron. */
    QToolButton#brushTipPicker {
      background: @field_bg;
      border: 1px solid @field_inset_border;
      border-top-color: @field_bevel_top;
      border-radius: 0;
      padding: 0;
      min-height: 24px;
      max-height: 24px;
    }
    QToolButton#brushTipPicker:hover {
      border-color: @button_hover_border;
    }
    QToolButton#brushTipPicker:disabled {
      background: @field_bg_disabled;
      border-color: @field_border_disabled;
    }
    QToolButton#brushDynamicsButton {
      padding: 2px 3px;
      min-width: 16px;
      min-height: 20px;
      max-height: 20px;
    }
    /* min-width: the generic QToolButton rule's 26 px content minimum made the gear
       button 37 px wide for a 16 px glyph (GitHub issue 364 needed the room). */
    QToolButton#brushSmoothingOptionsButton, QToolButton[optionsBarMenuButton="true"] {
      padding: 2px 3px;
      min-width: 16px;
      min-height: 20px;
      max-height: 20px;
    }
    QToolButton#brushSmoothingOptionsButton::menu-indicator,
    QToolButton[optionsBarMenuButton="true"]::menu-indicator {
      width: 0;
    }
    /* The Color panel's settings gear sits in its 18 px title row (GitHub issue 404). */
    QToolButton#colorPanelSettingsButton {
      padding: 0;
      min-width: 18px;
      max-width: 18px;
      min-height: 18px;
      max-height: 18px;
    }
    /* The clone source flip toggles (GitHub issue 424): 24 px squares whose 20 px
       glyph fills them, so no generic 3 px padding or 26 px content minimum. */
    QToolButton#cloneSourceFlipHorizontalButton, QToolButton#cloneSourceFlipVerticalButton {
      padding: 1px;
      min-width: 0;
      min-height: 0;
    }
    QToolButton#colorPanelSettingsButton::menu-indicator {
      width: 0;
      image: none;
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
    /* 35 px outside (31 + padding + border): exactly the collapsed column's
       content width (43 less the 4 px side margins), so the button fills the
       column. A narrower button is centered inside the 35 px item while the
       bar is collapsed but sits at the column's left edge once an expansion
       makes every column as wide as the widest button, which moved the plain
       tools 1 px on every expand (GitHub issue 121). The tp(n) length tokens
       scale with the toolbar icon size and the tf(n) ones with its footer
       (resolve_tool_palette_lengths). */
    QToolBar#toolPalette QToolButton {
      min-width: @tp(31)px;
      max-width: @tp(31)px;
      min-height: @tp(24)px;
      max-height: @tp(24)px;
      padding: 1px;
    }
    /* The Layers footer menu's checkbox rows sit on the menu's own background. */
    QMenu#layerFooterButtonsMenu QWidget#layerFooterToggleRow, QMenu#layerFooterButtonsMenu QCheckBox {
      background: transparent;
    }
    /* The two-column rows only arrange buttons: the bar shows through. */
    QToolBar#toolPalette QWidget#toolPaletteRow {
      background: transparent;
    }
    QToolBar#toolPalette QPushButton {
      min-width: @tp(26)px;
      max-width: @tp(26)px;
      min-height: @tp(24)px;
      max-height: @tp(24)px;
      padding: 0;
    }
    /* The footer's color well paints itself (tool_palette_footer.cpp); the
       toolbar rules above must not size or decorate its pieces. The extension
       chevron keeps Qt's own extent instead of the 24 px tool button height,
       which clipped it against the bottom of the bar. */
    QToolBar#toolPalette QPushButton#foregroundColorButton, QToolBar#toolPalette QPushButton#backgroundColorButton {
      min-width: @tf(22)px;
      max-width: @tf(22)px;
      min-height: @tf(22)px;
      max-height: @tf(22)px;
      background: transparent;
      border: 0;
      padding: 0;
    }
    QToolBar#toolPalette QToolButton#colorSwapButton, QToolBar#toolPalette QToolButton#colorDefaultButton {
      min-width: @tf(14)px;
      max-width: @tf(14)px;
      min-height: @tf(14)px;
      max-height: @tf(14)px;
      background: transparent;
      border: 0;
      padding: 0;
    }
    QToolBar#toolPalette QToolButton#quickMaskButton {
      min-width: @tf(32)px;
      max-width: @tf(32)px;
      min-height: @tf(22)px;
      max-height: @tf(22)px;
    }
    /* The screen mode button's row under Quick Mask (GitHub issue 212): a 36 x 21 box,
       which still fits the row when the footer factor rounds it down (Smallest). */
    QToolBar#toolPalette QToolButton#screenModeButton {
      min-width: @tf(32)px;
      max-width: @tf(32)px;
      min-height: @tf(17)px;
      max-height: @tf(17)px;
    }
    QToolBar#toolPalette QToolButton#qt_toolbar_ext_button {
      min-height: 14px;
      max-height: 14px;
      padding: 0;
    }
    QWidget#toolPaletteFooter {
      background: @tool_palette_bg;
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
      margin: 3px 4px;
    }
    QToolBar#Options {
      background: @options_bar_bg;
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
      /* Plain text, no chip: a filled, bevelled label read as a button beside
         the field it names (GitHub issue 76). The scrub cursor still marks it. */
      background: transparent;
      border: 0;
      color: @text_bright;
      min-height: 24px;
      max-height: 24px;
      padding: 0 0 0 5px;
    }
    QToolBar#Options QSpinBox, QToolBar#Options QDoubleSpinBox, QToolBar#Options QComboBox, QToolBar#Options QFontComboBox {
      min-height: 24px;
      max-height: 24px;
      padding-left: 4px;
      background: @field_bg;
      border: 1px solid @field_inset_border;
      border-top-color: @field_bevel_top;
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
    QToolBar#Options QCheckBox:disabled {
      color: @text_disabled;
    }
    QToolBar#Options QCheckBox::indicator:disabled {
      background: @field_bg_disabled;
      border-color: @field_border_disabled;
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
    /* No vertical padding: the button is then as tall as the bar's fields (26 px
       with the border) and its text sits on their text line (GitHub issue 409). */
    QToolBar#Options QPushButton {
      min-height: 24px;
      max-height: 24px;
      background: @options_button_bg;
      border: 1px solid @field_inset_border;
      border-top-color: @field_bevel_top;
      padding: 0 7px;
    }
    QToolBar#Options QPushButton[optionsSessionButton="true"] {
      padding: 0 2px; /* the 20px session icons need the width the default 7px padding eats */
    }
    QToolBar#Options QPushButton:checked {
      background: @accent_checked_bg;
      border-color: @accent_checked_border;
      color: @text_on_accent;
    }
    /* The shape Fill and Stroke swatches: framed like the bar's push buttons so they
       read as clickable (GitHub issue 311). */
    QToolBar#Options QToolButton[optionsBarSwatch="true"] {
      background: @options_button_bg;
      border: 1px solid @field_inset_border;
      border-top-color: @field_bevel_top;
      padding: 0;
      min-width: 36px;
      max-width: 36px;
      min-height: 24px;
      max-height: 24px;
    }
    QToolBar#Options QToolButton[optionsBarSwatch="true"]:hover {
      background: @button_hover_bg;
      border-color: @button_hover_border;
    }
    QToolBar#Options QToolButton[optionsBarSwatch="true"]:pressed {
      background: @accent_pressed_bg;
      border-color: @accent_border_bright;
    }
    /* Keyboard focus (GitHub issue 440): a control reached with Tab or Backtab carries the
       accent outline; a click focuses without it (optionsKeyboardFocus is set by
       MainWindow::handle_options_bar_focus_event). Border colors only, so nothing changes
       size; text fields show their caret as well. The bar's checkboxes paint themselves
       (CheckGlyphBox) and draw the same outline. */
    QToolBar#Options QSpinBox[optionsKeyboardFocus="true"]:focus,
    QToolBar#Options QDoubleSpinBox[optionsKeyboardFocus="true"]:focus,
    QToolBar#Options QComboBox[optionsKeyboardFocus="true"]:focus,
    QToolBar#Options QFontComboBox[optionsKeyboardFocus="true"]:focus,
    QToolBar#Options QPushButton[optionsKeyboardFocus="true"]:focus,
    QToolBar#Options QToolButton[optionsKeyboardFocus="true"]:focus,
    QToolBar#Options QSlider[optionsKeyboardFocus="true"]:focus {
      border-color: @accent_border_bright;
    }
    /* A checked button already wears the accent border, and Tab lands on a mode group's
       checked button: its focus outline is the bright text color instead. */
    QToolBar#Options QToolButton[optionsKeyboardFocus="true"]:focus:checked,
    QToolBar#Options QPushButton[optionsKeyboardFocus="true"]:focus:checked {
      border-color: @text_bright;
    }
    QWidget#historyDockTitle, QWidget#channelsDockTitle, QWidget#propertiesDockTitle, QWidget#infoDockTitle,
    QWidget#layersDockTitle, QWidget#pathsDockTitle, QWidget#colorDockTitle, QWidget#paletteDockTitle {
      background: @panel_title_bg;
      border-top: 1px solid @panel_title_bevel_top;
      border-bottom: 1px solid @panel_title_border_bottom;
    }
    QWidget#historyDockTitle QLabel, QWidget#channelsDockTitle QLabel, QWidget#propertiesDockTitle QLabel,
    QWidget#infoDockTitle QLabel, QWidget#layersDockTitle QLabel, QWidget#pathsDockTitle QLabel,
    QWidget#colorDockTitle QLabel, QWidget#paletteDockTitle QLabel {
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
    /* QDoubleSpinBox is not a QSpinBox subclass: left out, unit fields (UnitSpinBox)
       drew the platform frame beside the plain ones (issues 327, 329). */
    QListWidget, QTreeWidget, QComboBox, QSpinBox, QDoubleSpinBox, QSlider, QLineEdit, QTextEdit {
      background: @field_bg_large;
      color: @text_primary;
      border: 1px solid @field_border;
      min-height: 20px;
    }
    /* Both colors: without selection-color the selected row's text comes from the platform
       palette, white on GNOME/KDE, which is light on light under the Light scheme. */
    QListWidget, QTreeWidget, QComboBox {
      selection-background-color: @list_selection_bg;
      selection-color: @list_selection_text;
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
    /* Layer color labels (GitHub issue 244): a strip down the row's left edge. */
    QWidget#layerRowColorStrip[layerColor="red"] {
      background: @layer_color_red;
    }
    QWidget#layerRowColorStrip[layerColor="orange"] {
      background: @layer_color_orange;
    }
    QWidget#layerRowColorStrip[layerColor="yellow"] {
      background: @layer_color_yellow;
    }
    QWidget#layerRowColorStrip[layerColor="green"] {
      background: @layer_color_green;
    }
    QWidget#layerRowColorStrip[layerColor="blue"] {
      background: @layer_color_blue;
    }
    QWidget#layerRowColorStrip[layerColor="violet"] {
      background: @layer_color_violet;
    }
    QWidget#layerRowColorStrip[layerColor="gray"] {
      background: @layer_color_gray;
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
    /* Customize Tool Palette's rows: room for the tick and the icon with a clear gap above
       and below (the cluster headings get more through their size hint), and the checkbox look
       (the rule above hides list indicators). The shortcut column's gap before the scrollbar is
       its delegate's (tool_palette_customization.cpp): a padding here does not move
       right-aligned text. */
    QTreeWidget#toolPaletteCustomizeTree::item {
      min-height: 26px;
    }
    QListWidget#layerFooterButtonsList::item {
      min-height: 30px;
      padding: 0 4px;
    }
    QTreeWidget#toolPaletteCustomizeTree::indicator, QListWidget#layerFooterButtonsList::indicator {
      width: 12px;
      height: 12px;
      max-width: 12px;
      max-height: 12px;
      background: @checkbox_indicator_bg;
      border: 1px solid @checkbox_indicator_border;
    }
    QTreeWidget#toolPaletteCustomizeTree::indicator:checked, QListWidget#layerFooterButtonsList::indicator:checked {
      background: @accent;
      border-color: @checkbox_accent_border;
      image: url(@icon(checkmark));
    }
    QTreeWidget#toolPaletteCustomizeTree::indicator:indeterminate {
      background: @accent;
      border-color: @checkbox_accent_border;
      image: url(@icon(spin-minus));
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
    /* The inline rename editor takes the name label's slot, so it keeps the
       label's height and font instead of the generic 20 px field. */
    QLineEdit#layerRowNameEdit {
      background: @field_bg;
      color: @layer_row_name_text;
      border: 1px solid @accent_bright;
      border-radius: 0;
      padding: 0 1px;
      margin: 0;
      min-height: 0px;
      font-size: 12px;
    }
    QLabel#layerRowDetails {
      color: @layer_row_details_text;
      font-size: 10px;
    }
    QLabel#layerContentThumbnail[layerTargetActive="true"],
    QLabel#layerMaskThumbnail[layerTargetActive="true"],
    QLabel#layerVectorMaskThumbnail[layerTargetActive="true"],
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
      /* 17 px, so the 19 px zoom group leaves an even split in the 39 px status
         bar: the same gap above and below (GitHub issue 409). */
      min-height: 17px;
      max-height: 17px;
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
    QWidget#statusZoomControls {
      background: transparent;
    }
    QToolButton#statusZoomFitButton {
      background: transparent;
      color: @status_text;
      border: 1px solid transparent;
      border-radius: 3px;
      padding: 0 1px;
      min-height: 17px;
      max-height: 17px;
      min-width: 0;
      font-size: 11px;
    }
    QToolButton#statusZoomFitButton:hover {
      background: @button_hover_bg;
      border-color: @status_field_border;
      color: @text_bright;
    }
    QToolButton#statusZoomFitButton:disabled {
      color: @status_text_disabled;
    }
    QLabel#statusDocumentInfoLabel {
      background: transparent;
      color: @status_text;
      padding: 0 2px;
      font-size: 11px;
    }
    QFrame#statusInfoSeparator {
      color: @status_field_border;
    }
    QLabel#statusSelectionInfoLabel {
      background: transparent;
      color: @status_text;
      padding: 0 0 0 2px;
      font-size: 11px;
    }
    QLabel#canvasInfoLabel, QLabel#documentInfoLabel {
      color: @info_text;
      line-height: 130%;
    }
    QScrollArea#propertiesScrollArea, QScrollArea#paletteScrollArea,
    QScrollArea#rightDockOverflowScroll, QScrollArea#rightDockOverflowScroll > QWidget {
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
    QToolButton#layerFolderDisclosureButton, QToolButton#layerEffectsDisclosureButton,
    QToolButton#layerSmartFiltersDisclosureButton {
      background: transparent;
      color: @layer_glyph_text;
      border: 1px solid transparent;
      border-radius: 3px;
      padding: 0;
      min-width: 10px;
      max-width: 10px;
      min-height: 20px;
      max-height: 20px;
    }
    QToolButton#layerFolderDisclosureButton:hover, QToolButton#layerEffectsDisclosureButton:hover,
    QToolButton#layerSmartFiltersDisclosureButton:hover {
      border-color: @layer_button_hover_border;
      background: @layer_button_hover_bg;
    }
    QToolButton#layerFolderDisclosureButton[layerDragActive="true"]:hover,
    QToolButton#layerEffectsDisclosureButton[layerDragActive="true"]:hover,
    QToolButton#layerSmartFiltersDisclosureButton[layerDragActive="true"]:hover {
      border-color: transparent;
      background: transparent;
    }
    QToolButton#layerFolderDisclosureButton:disabled, QToolButton#layerEffectsDisclosureButton:disabled,
    QToolButton#layerSmartFiltersDisclosureButton:disabled {
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
    QToolButton#layerMaskLinkButton, QToolButton#layerVectorMaskLinkButton, QToolButton#layerFxBadgeButton,
    QToolButton#layerSmartObjectBadgeButton, QToolButton#layerClippingBadgeButton {
      background: transparent;
      border: 1px solid transparent;
      border-radius: 3px;
      padding: 0;
    }
    /* The clip badge takes the content thumbnail's width (Small is 22 px), so it centers on
       the base row's thumbnail (#397), and the mask link is 22 px; the generic 26 px minimum
       would widen both. */
    QToolButton#layerClippingBadgeButton, QToolButton#layerMaskLinkButton, QToolButton#layerVectorMaskLinkButton {
      min-width: 0;
    }
    QToolButton#layerMaskLinkButton:hover, QToolButton#layerVectorMaskLinkButton:hover,
    QToolButton#layerFxBadgeButton:hover,
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
    /* The button Enter presses carries the accent outline, like Photoshop's save
       prompt (Seth, October 2026). Qt hands "default" to whichever auto-default
       button has focus, so the outline follows Tab between a dialog's buttons.
       After :hover so a hovered default keeps it. */
    QPushButton:default {
      border: 1px solid @accent_border_bright;
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
    /* Normal; layer_footer_size_rules scales these for the footer's other icon sizes. */
    QPushButton[layerActionButton="true"], QToolButton[layerActionButton="true"] {
      padding: 0;
      /* The Layers footer narrows its buttons down to min-width before it wraps (issue 199). */
      min-width: 30px;
      max-width: 38px;
      min-height: 34px;
      max-height: 34px;
    }
    /* Flat like the footer's QToolButtons (adjustment, styles): the outline shows
       only on hover, so the whole row reads alike (GitHub issue 354). The border
       stays 1 px so the sizes above do not move. */
    QPushButton[layerActionButton="true"] {
      background: transparent;
      border: 1px solid transparent;
    }
    QPushButton[layerActionButton="true"]:hover {
      background: @button_hover_bg;
      border-color: @button_hover_border;
    }
    QPushButton[layerActionButton="true"]:pressed, QPushButton[layerActionButton="true"]:checked,
    QToolButton[layerActionButton="true"]:pressed {
      background: @accent_pressed_bg;
      border-color: @accent_border_bright;
    }
    QPushButton[layerActionButton="true"]:disabled {
      background: transparent;
      border-color: transparent;
    }
    QToolButton[channelActionButton="true"] {
      padding: 0;
      min-width: 34px;
      max-width: 34px;
      min-height: 30px;
      max-height: 30px;
    }
    /* :hover too, so the hover rule above cannot outrank a drop target under the cursor. */
    QPushButton[layerDropActive="true"], QToolButton[layerDropActive="true"],
    QPushButton[layerDropActive="true"]:hover, QToolButton[layerDropActive="true"]:hover {
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
    /* A disabled checkbox must read as disabled: without these rules the label
       and box paint exactly like an enabled one, so a greyed-out option looks
       like a checkbox that refuses to toggle (the Merge Layers "vector types"
       report, September 2026). The checked glyph stays so the stored value is
       still visible; only the colors drop to the disabled field tokens. */
    QCheckBox:disabled {
      color: @text_disabled;
    }
    QCheckBox::indicator:disabled {
      background: @field_bg_disabled;
      border-color: @field_border_disabled;
    }
    QTabWidget::pane {
      border-top: 1px solid @tab_pane_border;
    }
    /* The panel tab group's bar draws no base line: Fusion paints a
       palette-light 1px line along the bar beside the tabs, a stray white
       line next to Layers / Channels / Paths on Linux (GitHub issue 123).
       The ::tab rules draw everything the bar needs. */
    QTabBar#panelTabGroupTabBar {
      qproperty-drawBase: 0;
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

// Resolves the template's @tp(n) and @tf(n) tool palette lengths for a toolbar icon
// size factor (tool_palette_footer.hpp): n times the factor, or times the footer factor.
QString resolve_tool_palette_lengths(QString sheet, double factor) {
  const double footer_factor = tool_palette_footer_factor(factor);
  static const QRegularExpression length_token(QStringLiteral(R"(@t([pf])\((\d+)\))"));
  QString resolved;
  resolved.reserve(sheet.size());
  qsizetype last = 0;
  for (auto it = length_token.globalMatch(sheet); it.hasNext();) {
    const auto match = it.next();
    resolved += QStringView(sheet).mid(last, match.capturedStart() - last);
    const double scale = match.captured(1) == QLatin1String("p") ? factor : footer_factor;
    resolved += QString::number(tool_palette_length(match.captured(2).toInt(), scale));
    last = match.capturedEnd();
  }
  resolved += QStringView(sheet).mid(last);
  return resolved;
}

// The Layers footer buttons' lengths at every icon size but Normal (GitHub issue 213
// follow-up): the Normal rule's 30 to 38 px wide, 34 px tall content times the size's
// factor, for the layerFooterIconSize property MainWindow::apply_layer_footer_icon_size sets.
QString layer_footer_size_rules() {
  QString rules;
  for (const auto size : kToolbarIconSizes) {
    if (size == ToolbarIconSize::Normal) {
      continue;
    }
    const auto factor = toolbar_icon_size_factor(size);
    const auto token = toolbar_icon_size_token(size);
    rules += QStringLiteral(
                 "QPushButton[layerActionButton=\"true\"][layerFooterIconSize=\"%1\"], "
                 "QToolButton[layerActionButton=\"true\"][layerFooterIconSize=\"%1\"] {\n"
                 "  min-width: %2px;\n  max-width: %3px;\n  min-height: %4px;\n  max-height: %4px;\n}\n")
                 .arg(token)
                 .arg(tool_palette_length(30, factor))
                 .arg(tool_palette_length(38, factor))
                 .arg(tool_palette_length(34, factor));
  }
  return rules;
}

}  // namespace

QString photoshop_style(double tool_palette_factor) {
  // Keyed on theme_generation(), not on active_color_scheme(): a user-imported
  // custom palette (theme_file.hpp) is not a compile-time constant like the
  // two built-in palettes, so a 2-slot Dark/Light cache would keep serving a
  // stale sheet (or the wrong custom colors) after a custom-palette apply.
  // theme_generation() bumps on every actual palette change, built-in or
  // custom, which is the only signal available -- Qt fires no event for a
  // palette-struct change.
  // Also keyed on the toolbar icon size factor, which sizes the tool palette rules.
  // And on the theme's panel backgrounds (theme_script_host.hpp), appended last.
  static int cached_generation = -1;
  static double cached_factor = 0.0;
  static int cached_panel_revision = -1;
  static QString cached;
  const auto generation = theme_generation();
  const auto panel_revision = theme_panel_background_revision();
  if (generation != cached_generation || tool_palette_factor != cached_factor ||
      panel_revision != cached_panel_revision) {
    auto sheet = resolve_tool_palette_lengths(photoshop_style_template(), tool_palette_factor);
    sheet += layer_footer_size_rules();
    // Where the window server draws no shadow around the frameless main window
    // (X11, Wayland), widen its outline so the window still has a visible edge
    // against another window; see frameless_window_edge_width in dialog_utils.hpp.
    if (MainWindow::use_custom_window_chrome() && frameless_window_edge_width() > 1) {
      sheet += QStringLiteral("\nQMainWindow { border: %1px solid @window_border; }\n")
                   .arg(frameless_window_edge_width());
    }
    cached = apply_theme_tokens(sheet) + theme_panel_background_stylesheet();
    cached_generation = generation;
    cached_factor = tool_palette_factor;
    cached_panel_revision = panel_revision;
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
  theme_apply_phase("signal handlers before the window");
  setStyleSheet(photoshop_style(tool_palette_factor_));
  theme_apply_phase("window stylesheet");

  // 2. Top-level windows that are not in this window's child tree, plus any
  //    widget deeper in the tree that carries its own themed template (the
  //    chromed dialogs, per-instance color swatches, the start panel).
  rebuild_themed_styles_in(*this);
  theme_apply_phase("themed child stylesheets");
  for (auto* top_level : QApplication::topLevelWidgets()) {
    if (top_level == nullptr || top_level == this) {
      continue;
    }
    // Translucent click-through overlays (the screen color picker) paint a
    // single near-transparent fill and must not be restyled or repolished.
    if (top_level->testAttribute(Qt::WA_TranslucentBackground)) {
      continue;
    }
    // A window parented into this one (Preferences, the float windows) took the
    // window's sheet and had its templates rebuilt above; doing it again restyled
    // the open Preferences dialog a second time (GitHub issue 490).
    // (QWidget::isAncestorOf stops at a window boundary, so walk the parents.)
    bool parented_here = false;
    for (auto* parent = top_level->parentWidget(); parent != nullptr; parent = parent->parentWidget()) {
      if (parent == this) {
        parented_here = true;
        break;
      }
    }
    if (parented_here) {
      continue;
    }
    rebuild_themed_styles_in(*top_level);
    top_level->style()->unpolish(top_level);
    top_level->style()->polish(top_level);
    top_level->update();
  }
  theme_apply_phase("other top-level windows");

  // 3. Pixmaps built from theme colors. Qt sends no event for a palette-struct
  //    change, so anything cached has to be dropped by hand or it keeps
  //    painting the old scheme forever.
  layer_thumbnail_cache_.clear();
  channel_thumbnail_cache_.clear();
  path_thumbnail_cache_.clear();
  // The shape and text color swatches bake theme().swatch_border into their outline.
  update_vector_swatch_icons();
  refresh_text_color_button();
  refresh_layer_list();
  refresh_channel_panel();
  refresh_paths_panel();
  // Future-state rows stamp theme().history_future_text as a foreground brush.
  refresh_history_panel();
  theme_apply_phase("thumbnails and panels");

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
  theme_apply_phase("canvas repaint requests");
}

}  // namespace patchy::ui
