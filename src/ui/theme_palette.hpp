#pragma once

// The named chrome colors behind Patchy's Dark and Light color schemes.
//
// Nothing in src/ui should hardcode a chrome color. QSS builders reference these
// roles by name through @role_name tokens (see theme_qss.hpp); painted widgets and
// delegates read them through theme(). Document pixels, transparency
// checkerboards, marching ants, tool cursors, and user-configured grid/guide
// colors are deliberately NOT roles: they are content or over-the-artwork
// contrast devices and must not follow the UI scheme.
//
// Adding a role means three edits: the member below, a value in BOTH palettes in
// theme_palette.cpp, and a row in the role table. Miss any one and
// ui_theme_palettes_define_every_role fails.
//
// This header stays free of MainWindow's world so canvas, dialog, and icon code
// can include it.

#include <QColor>
#include <QLatin1StringView>

#include <span>
#include <utility>

namespace patchy::ui {

enum class ColorScheme { Dark, Light };

struct ThemePalette {
  // Base surfaces, text, and window frame.
  QColor window_bg;
  QColor window_border;
  QColor text_primary;
  QColor text_secondary;
  QColor text_bright;
  QColor text_disabled;
  // Text over a saturated selection (a highlighted menu item, a checked accent
  // button). Those backgrounds stay saturated in Light, so this stays white.
  QColor text_on_accent;
  // Text over a raised NEUTRAL surface (a selected tab, the Image Size panels).
  // Same white in Dark, but it has to invert in Light: those backgrounds lighten,
  // and white on light gray is the one combination that disappears completely.
  QColor text_on_raised;
  QColor splitter_bg;
  QColor splitter_hover_bg;

  // The 34px chrome bar: the frameless window's menu bar, the window-button
  // strip beside it, and each dialog's own title bar all share this surface.
  QColor title_bar_bg;
  QColor title_bar_border;

  // Menu bar and menus.
  QColor menu_bar_item_hover_bg;
  QColor menu_bar_text_disabled;
  QColor menu_bg;
  QColor menu_border;
  QColor menu_item_selected_bg;
  QColor menu_separator;

  // Generic toolbars and tool buttons.
  QColor toolbar_bg;
  QColor toolbar_border;
  QColor button_hover_bg;
  QColor button_hover_border;
  QColor accent_pressed_bg;
  QColor accent_border_bright;

  // Custom window chrome (Windows frameless title bar).
  QColor window_chrome_hover_bg;
  QColor window_chrome_pressed_bg;
  QColor window_close_hover_bg;
  QColor window_close_pressed_bg;

  // Tool palette.
  QColor tool_palette_bg;
  QColor tool_palette_border;
  QColor tool_palette_separator;

  // Options bar.
  QColor options_bar_bg;
  QColor options_bar_top_edge;
  QColor option_separator;
  QColor option_chip_bg;
  QColor field_bg;
  QColor field_inset_border;
  QColor field_bevel_top;
  QColor options_button_bg;
  QColor accent_checked_bg;
  QColor accent_checked_border;

  // Check boxes and sliders.
  QColor checkbox_compact_bg;
  QColor checkbox_compact_border;
  QColor checkbox_indicator_bg;
  QColor checkbox_indicator_border;
  QColor checkbox_accent_border;
  QColor accent;
  QColor slider_groove_bg;
  QColor slider_groove_border;
  QColor slider_fill_border;
  QColor slider_handle_bg;
  QColor slider_handle_border;

  // Dialog spin boxes (the large -/+ button treatment).
  QColor spinbox_disabled_bg;
  QColor spinbox_disabled_text;
  QColor spin_button_disabled_bg;
  QColor spin_button_disabled_bevel;

  // Docks and panels.
  QColor dock_title_bg;
  QColor panel_border_strong;
  QColor panel_title_bg;
  QColor panel_title_bevel_top;
  QColor panel_title_border_bottom;
  QColor dock_collapse_text;
  QColor dock_collapse_hover_bg;
  QColor dock_collapse_hover_border;
  QColor panel_bg;
  QColor panel_inset_bg;
  QColor panel_inset_border;
  QColor info_text;

  // Text entry and list widgets.
  QColor field_bg_large;
  QColor field_border;
  QColor field_bg_disabled;
  QColor field_text_disabled;
  QColor field_border_disabled;
  QColor list_selection_bg;
  QColor list_item_border;
  QColor list_selection_text;
  QColor list_selection_border;
  QColor category_list_item_border;
  QColor category_selected_bg;
  QColor category_selected_border;

  // Layer rows and the layers panel.
  QColor layer_row_bg;
  QColor layer_row_border;
  QColor layer_row_group_bg;
  QColor layer_row_selected_bg;
  QColor layer_row_selected_border;
  QColor layer_row_name_text;
  QColor layer_row_details_text;
  QColor layer_glyph_text;
  QColor layer_visibility_text;
  QColor layer_button_hover_bg;
  QColor layer_button_hover_border;
  QColor layer_lock_border;
  QColor layer_lock_hover_border;
  QColor layer_lock_checked_bg;
  QColor layer_lock_checked_border;
  QColor layer_lock_mixed_bg;
  QColor layer_lock_mixed_border;
  QColor layer_drop_bg;
  QColor layer_depth_guide;
  QColor layer_eye_on;
  QColor layer_eye_off;
  QColor layer_lock_badge_active;
  QColor layer_lock_badge_inherited;
  QColor layer_thumbnail_border;
  QColor layer_mask_disabled_cross;
  QColor layer_clip_badge;

  // The bright accent used for active targets and the mask-edit chip.
  QColor accent_bright;
  QColor accent_bright_hover;
  QColor accent_bright_border;
  QColor text_on_accent_bright;

  // Push buttons.
  QColor button_bg;
  QColor button_border;
  QColor button_hover_border_strong;

  // Status bar. status_error_mark is the status bar's own surface punched out of
  // the warning triangle, so it must track status_bar_bg exactly or the glyph
  // fills in; that is why it is a role rather than a constant.
  QColor status_bar_bg;
  QColor status_text;
  QColor status_text_disabled;
  QColor status_field_bg;
  QColor status_field_border;
  QColor status_field_border_disabled;
  QColor status_error_text;
  QColor status_error_wash;
  QColor status_warning_fill;
  QColor status_error_mark;

  // Layer drag-and-drop indicators, and the text editor's resize handles.
  QColor drop_indicator_bg;
  QColor drop_indicator_border;
  QColor resize_handle_bg;
  QColor resize_handle_border;
  QColor hint_text;

  // Script console and stop panel.
  QColor script_detail_text;
  QColor console_warning_text;
  QColor console_error_text;

  // Hand-painted list rows (font picker, script tree) and preview panes.
  QColor list_row_hover_bg;
  QColor preview_pane_bg;
  QColor preview_pane_muted_text;

  // The blue used by hand-painted controls: filter dials and waveforms, the
  // Blend If ramp handles, the zoom overlay. One role because all three were
  // authored to the same value and should stay in step.
  QColor accent_control;

  // Tabs.
  QColor tab_bg;
  QColor tab_hover_bg;
  QColor tab_selected_bg;
  QColor tab_pane_border;

  // The color picker's own tab bar, which steps further between unselected and
  // selected than the document tabs do because the popup is compact and its
  // three modes have to be told apart at a glance. Its own family rather than
  // borrowed roles: it was assembled out of a dialog hover fill and a field
  // border, and those carried no promise about which way round the two sit, so
  // Light derived the selected mode darker than the ones beside it.
  QColor picker_tab_bg;
  QColor picker_tab_border;
  QColor picker_tab_hover_bg;
  QColor picker_tab_selected_bg;

  // Canvas chrome: the pasteboard the document floats on, its rulers, and the
  // processing overlay. Painted, not styled, so a scheme change has to repaint
  // the canvas explicitly.
  //
  // Not here, deliberately: the transparency checkerboard (it depicts alpha, and
  // every editor keeps it light in both themes so "grid means transparent" stays
  // a learned signal), the marching ants and clone marker (a black-plus-white
  // pair engineered to read over arbitrary artwork, which a UI-derived color
  // would break), and the user's saved grid and guide colors (user data).
  QColor canvas_backdrop;
  QColor canvas_empty_text;
  QColor canvas_document_border;
  QColor ruler_bar_bg;
  QColor ruler_corner_bg;
  QColor ruler_edge;
  QColor ruler_tick;
  QColor canvas_hud_bg;
  QColor canvas_hud_border;
  QColor canvas_hud_text;
  QColor canvas_hud_spinner;

  // Scroll bars. The canvas track slaves to the canvas backdrop rather than the
  // window surface: it is document-window chrome sitting against the pasteboard,
  // not a panel. Every other bar in the application is styled from these roles
  // too, on every platform; none of them can be left native (see the scroll-bar
  // block in main_window_theme.cpp).
  QColor canvas_scrollbar_track;
  QColor panel_scrollbar_track;
  QColor scrollbar_handle_bg;
  QColor scrollbar_handle_border;
  QColor scrollbar_handle_hover_bg;

  // macOS-only group boxes (see the APPLE-gated QSS block).
  QColor group_box_border;

  // The Preferences dialog's own tab bar and panels. Deliberately separate from
  // the document tab roles: these are a settings-sheet treatment, and the two
  // need to be free to diverge in Light.
  QColor dialog_tab_bg;
  QColor dialog_tab_border;
  QColor dialog_tab_text;
  QColor dialog_tab_hover_bg;
  QColor dialog_tab_selected_bg;
  QColor panel_card_bg;
  QColor panel_card_border;
  QColor color_button_border;
  QColor color_button_hover_bg;
  QColor color_button_hover_border;
  QColor grid_preview_bg;
  QColor grid_preview_border;

  // Hyperlink text in rich-text labels (start panel footer, About).
  QColor link_text;

  // Help > About. Its own branded surface, darker than the app chrome.
  QColor splash_bg;
  QColor splash_border;
  QColor splash_title_text;
  QColor splash_subtitle_text;
  QColor splash_body_text;
  QColor splash_caption_text;
  QColor splash_status_text;
  QColor splash_link_text;
  QColor splash_button_bg;
  QColor splash_button_border;
  QColor splash_button_hover_bg;
  QColor splash_primary_bg;
  QColor splash_primary_border;
  QColor splash_primary_hover_bg;

  // Hotkey editor (a Preferences tab, so it restyles live with the dialog open).
  QColor hotkey_hint_text;
  QColor hotkey_muted_text;
  QColor hotkey_header_border;
  QColor hotkey_conflict_text;
  QColor hotkey_search_bg;
  QColor hotkey_search_border;
  QColor hotkey_search_text;
  QColor hotkey_shortcut_text;
  QColor hotkey_shortcut_active_text;
  QColor hotkey_recording_bg;
  QColor hotkey_recording_text;
  QColor hotkey_warning_bg;
  QColor hotkey_warning_border;
  QColor hotkey_warning_text;

  // Font picker popup.
  QColor popup_list_bg;

  // New Document: the category chips and the preset card grid.
  QColor nd_section_text;
  QColor nd_chip_border;
  QColor nd_chip_text;
  QColor nd_field_border;
  QColor nd_field_disabled_bg;
  QColor nd_field_disabled_border;
  QColor nd_field_disabled_text;
  QColor nd_card_disabled_bg;
  QColor nd_card_disabled_border;
  QColor nd_card_selected_bg;
  QColor nd_card_hover_border;
  QColor nd_card_title_text;
  QColor nd_card_title_disabled_text;
  QColor nd_card_detail_text;
  QColor nd_card_detail_disabled_text;
  QColor nd_card_meta_text;
  QColor nd_card_meta_disabled_text;
  QColor nd_thumb_fill;
  QColor nd_thumb_fill_disabled;
  QColor nd_thumb_border;
  QColor nd_thumb_border_disabled;
  QColor nd_thumb_outline;

  // Image Size and Canvas Size. These two dialogs were authored with their own,
  // lighter neutral family than the rest of the app; keeping it as its own set of
  // roles preserves that relationship instead of flattening them into the chrome.
  QColor dlg_raised_bg;
  QColor dlg_raised_text;
  QColor dlg_raised_border;
  QColor dlg_raised_hover_bg;
  QColor dlg_raised_hover_border;
  QColor dlg_field_bg;
  QColor dlg_field_border;
  QColor dlg_field_text;
  QColor dlg_button_bg;
  QColor dlg_button_border;
  QColor dlg_button_focus_bg;
  QColor dlg_focus_border;
  QColor dlg_focus_ring;
  QColor dlg_anchor_active_bg;
  QColor dlg_neutral_border;
  QColor dlg_neutral_border_bright;
  QColor dlg_divider;
  QColor dlg_tab_bg;
  QColor dlg_tab_border;
  QColor dlg_grid_bg;
  QColor dlg_grid_border;
  QColor dlg_cell_bg;
  QColor dlg_cell_border;
  QColor dlg_cell_hover_bg;
  QColor dlg_action_border;
  QColor dlg_action_hover_border;

  // Layer Styles: the inline warning banners and the category list rows.
  QColor warning_banner_bg;
  QColor warning_banner_border;
  QColor warning_banner_text;
  QColor swatch_border;

  // Shared button and surface families used by more than one dialog. These were
  // duplicated per dialog with identical values before the theme layer existed;
  // sharing keeps a call-to-action button looking the same everywhere.
  QColor primary_bg;
  QColor primary_border;
  QColor primary_hover_bg;
  QColor neutral_button_hover_bg;
  QColor neutral_button_hover_border;
  QColor list_surface_bg;
  QColor list_surface_border;
  QColor selection_soft_bg;

  // The empty-workspace start panel and its recent-files list.
  QColor start_panel_title_text;
  QColor start_panel_tagline_text;
  QColor start_panel_muted_text;
  QColor start_panel_status_text;
  QColor start_panel_section_text;
  QColor start_panel_hint_text;
  QColor start_panel_row_hover_bg;

  // Icon ink. The hand-authored SVGs in src/ui/icons are written in these exact
  // dark values and recolored at paint time (see icon_theme.hpp), so each source
  // color needs its own role even where two look alike. The black and white paint
  // swatches in default-colors.svg and swap-colors.svg are deliberately absent:
  // they depict colors rather than draw chrome.
  QColor icon_ink;
  QColor icon_accent;
  QColor icon_accent_soft;
  QColor icon_accent_tint;
  QColor icon_danger;
  QColor icon_warning;
  QColor icon_folder;
  QColor icon_folder_fill;
  QColor icon_success;
  QColor icon_surface;
};

[[nodiscard]] const ThemePalette& dark_palette();
[[nodiscard]] const ThemePalette& light_palette();
[[nodiscard]] const ThemePalette& theme(ColorScheme scheme);

// The palette for the scheme currently applied to the UI. Valid from static
// initialization onward, so constructor code may read it before any scheme has
// been resolved; it defaults to Dark.
[[nodiscard]] const ThemePalette& theme();
[[nodiscard]] ColorScheme active_color_scheme();
void set_active_color_scheme(ColorScheme scheme);

// Bumped on every actual scheme change. Caches derived from theme() key on this;
// Qt sends no event for a palette-struct change, so a cache that ignores it goes
// stale silently.
[[nodiscard]] int theme_generation();

using ThemePaletteRole = std::pair<QLatin1StringView, QColor ThemePalette::*>;

// Name-to-member table, used by the QSS token replacer and by the completeness
// test. Names match the member spelling exactly so the two cannot drift.
[[nodiscard]] std::span<const ThemePaletteRole> theme_palette_roles();

}  // namespace patchy::ui
