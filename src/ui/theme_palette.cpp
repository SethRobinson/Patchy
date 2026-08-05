// The Dark and Light color-scheme tables.
//
// Dark is the authored palette: every value here is the one Patchy has always
// shipped, listed in ThemePalette member order.
//
// Light is derived from it and then hand-corrected. See flip_for_light() below
// for why the long tail is derived, and light_palette() for the list of roles
// where a mechanical flip is wrong. Adding a role therefore costs one line here
// and nothing in Light unless it needs a real decision.
//
// The canvas backdrop, rulers, and other painted chrome live in these tables too;
// the transparency checkerboard, marching ants, and tool cursors deliberately do
// not (see theme_palette.hpp).

#include "ui/theme_palette.hpp"

#include <algorithm>
#include <array>

namespace patchy::ui {

namespace {

[[nodiscard]] QColor rgb(quint32 value) { return QColor::fromRgb(value); }

ColorScheme g_active_scheme = ColorScheme::Dark;
int g_generation = 0;

}  // namespace

const ThemePalette& dark_palette() {
  static const ThemePalette palette{
      // Base surfaces, text, and window frame.
      .window_bg = rgb(0x262626),
      .window_border = rgb(0x1f1f1f),
      .text_primary = rgb(0xe6e6e6),
      .text_secondary = rgb(0xe1e1e1),
      .text_bright = rgb(0xf0f0f0),
      .text_disabled = rgb(0x737373),
      .text_on_accent = rgb(0xffffff),
      .text_on_raised = rgb(0xffffff),
      .splitter_bg = rgb(0x1e2022),
      .splitter_hover_bg = rgb(0x4e6f95),
      .dock_separator_bg = rgb(0x36383b),

      // The 34px chrome bar.
      .title_bar_bg = rgb(0x4f4f4f),
      .title_bar_border = rgb(0x343434),

      // Menu bar and menus.
      .menu_bar_item_hover_bg = rgb(0x3a3a3a),
      .menu_bar_text_disabled = rgb(0x9a9a9a),
      .menu_bg = rgb(0x3a3a3a),
      .menu_border = rgb(0x1f1f1f),
      .menu_item_selected_bg = rgb(0x4e6f95),
      .menu_separator = rgb(0x555555),

      // Generic toolbars and tool buttons.
      .toolbar_bg = rgb(0x3b3b3b),
      .toolbar_border = rgb(0x292929),
      .button_hover_bg = rgb(0x4a4a4a),
      .button_hover_border = rgb(0x696969),
      .accent_pressed_bg = rgb(0x2f75bd),
      .accent_border_bright = rgb(0x6bb3ff),

      // Custom window chrome.
      .window_chrome_hover_bg = rgb(0x626262),
      .window_chrome_pressed_bg = rgb(0x3c3c3c),
      .window_close_hover_bg = rgb(0xc42b1c),
      .window_close_pressed_bg = rgb(0x9f2117),

      // Tool palette.
      .tool_palette_bg = rgb(0x535353),
      .tool_palette_border = rgb(0x202020),
      .tool_palette_separator = rgb(0x616161),

      // Options bar.
      .options_bar_bg = rgb(0x3d3d3d),
      .options_bar_top_edge = rgb(0x5a5a5a),
      .option_separator = rgb(0x565656),
      .option_chip_bg = rgb(0x262626),
      .field_bg = rgb(0x292929),
      .field_inset_border = rgb(0x171717),
      .field_bevel_top = rgb(0x5d5d5d),
      .options_button_bg = rgb(0x303030),
      .accent_checked_bg = rgb(0x1667b7),
      .accent_checked_border = rgb(0x63adff),

      // Check boxes and sliders.
      .checkbox_compact_bg = rgb(0x1f1f1f),
      .checkbox_compact_border = rgb(0x777777),
      .checkbox_indicator_bg = rgb(0x4a4a4a),
      .checkbox_indicator_border = rgb(0x8a8a8a),
      .checkbox_accent_border = rgb(0x9ccfff),
      .accent = rgb(0x1473e6),
      .slider_groove_bg = rgb(0x1c1c1c),
      .slider_groove_border = rgb(0x555555),
      .slider_fill_border = rgb(0x5aa9ff),
      .slider_handle_bg = rgb(0xc9d0d8),
      .slider_handle_border = rgb(0x101010),

      // Dialog spin boxes.
      .spinbox_disabled_bg = rgb(0x2c2c2c),
      .spinbox_disabled_text = rgb(0x767676),
      .spin_button_disabled_bg = rgb(0x2e2e2e),
      .spin_button_disabled_bevel = rgb(0x444444),

      // Docks and panels.
      .dock_title_bg = rgb(0x323232),
      .panel_border_strong = rgb(0x202020),
      .panel_title_bg = rgb(0x2f3032),
      .panel_title_bevel_top = rgb(0x45474b),
      .panel_title_border_bottom = rgb(0x1b1c1e),
      .dock_collapse_text = rgb(0xcfd3d8),
      .dock_collapse_hover_bg = rgb(0x3b3d40),
      .dock_collapse_hover_border = rgb(0x5b5e63),
      .panel_bg = rgb(0x28292b),
      .panel_inset_bg = rgb(0x24272b),
      .panel_inset_border = rgb(0x3e454d),
      .info_text = rgb(0xd7dde6),

      // Text entry and list widgets.
      .field_bg_large = rgb(0x2b2b2b),
      .field_border = rgb(0x5a5a5a),
      .field_bg_disabled = rgb(0x242527),
      .field_text_disabled = rgb(0x6d7075),
      .field_border_disabled = rgb(0x3d3f42),
      .list_selection_bg = rgb(0x3a414a),
      .list_item_border = rgb(0x202225),
      .list_selection_text = rgb(0xf4f6f8),
      .list_selection_border = rgb(0x67717d),
      .category_list_item_border = rgb(0x3b3b3b),
      .category_selected_bg = rgb(0x2d4c6d),
      .category_selected_border = rgb(0x4f91ca),
      .history_future_text = rgb(0x737373),

      // Layer rows and the layers panel.
      .layer_row_bg = rgb(0x242628),
      .layer_row_border = rgb(0x303338),
      .layer_row_group_bg = rgb(0x292d31),
      .layer_row_selected_bg = rgb(0x2d4c6d),
      .layer_row_selected_border = rgb(0x4f91ca),
      .layer_row_name_text = rgb(0xf0f3f8),
      .layer_row_details_text = rgb(0xaeb6c2),
      .layer_glyph_text = rgb(0xd9e0ea),
      .layer_visibility_text = rgb(0xf2f6fb),
      .layer_button_hover_bg = rgb(0x30343a),
      .layer_button_hover_border = rgb(0x59636f),
      .layer_lock_border = rgb(0x46505b),
      .layer_lock_hover_border = rgb(0x687481),
      .layer_lock_checked_bg = rgb(0x3b3420),
      .layer_lock_checked_border = rgb(0xc9a944),
      .layer_lock_mixed_bg = rgb(0x2f3136),
      .layer_lock_mixed_border = rgb(0x7b8490),
      .layer_drop_bg = rgb(0x2e3f50),
      .layer_depth_guide = rgb(0x444a52),
      .layer_eye_on = rgb(0xe4ecf6),
      .layer_eye_off = rgb(0x767e88),
      .layer_lock_badge_active = rgb(0xf2d77d),
      .layer_lock_badge_inherited = rgb(0x7e848c),
      .layer_thumbnail_border = rgb(0x969ea8),
      .layer_mask_disabled_cross = rgb(0xe84646),
      .layer_clip_badge = rgb(0x96cdff),

      // Bright accent.
      .accent_bright = rgb(0x31a8ff),
      .accent_bright_hover = rgb(0x5cbcff),
      .accent_bright_border = rgb(0x6cc4ff),
      .text_on_accent_bright = rgb(0x0d1420),

      // Push buttons.
      .button_bg = rgb(0x3a3a3a),
      .button_border = rgb(0x666666),
      .button_hover_border_strong = rgb(0x8a8a8a),

      // Status bar.
      .status_bar_bg = rgb(0x252525),
      .status_text = rgb(0xcfcfcf),
      .status_text_disabled = rgb(0x6f6f6f),
      .status_field_bg = rgb(0x1e1e1e),
      .status_field_border = rgb(0x4a4a4a),
      .status_field_border_disabled = rgb(0x3a3a3a),
      .status_error_text = rgb(0xff6b68),
      .status_error_wash = rgb(0xa83232),
      .status_warning_fill = rgb(0xe05c5c),
      .status_error_mark = rgb(0x252525),

      // Drop indicators and text resize handles.
      .drop_indicator_bg = rgb(0x31a8ff),
      .drop_indicator_border = rgb(0x07121e),
      .resize_handle_bg = rgb(0xf5f8fc),
      .resize_handle_border = rgb(0x23262c),
      .hint_text = rgb(0x999999),

      // Script console.
      .script_detail_text = rgb(0x8a939f),
      .console_warning_text = rgb(0xe0a030),
      .console_error_text = rgb(0xe05050),

      // Hand-painted rows, preview panes, and the painted-control accent.
      .list_row_hover_bg = rgb(0x33373d),
      .preview_pane_bg = rgb(0x252525),
      .preview_pane_muted_text = rgb(0x9a9a9a),
      .accent_control = rgb(0x4c9aff),

      // Tabs.
      .tab_bg = rgb(0x2b2b2b),
      .tab_hover_bg = rgb(0x353535),
      .tab_selected_bg = rgb(0x3f3f3f),
      .tab_pane_border = rgb(0x5c5c5c),

      // The color picker's tab bar.
      .picker_tab_bg = rgb(0x343434),
      .picker_tab_border = rgb(0x2a2a2a),
      .picker_tab_hover_bg = rgb(0x404040),
      .picker_tab_selected_bg = rgb(0x5a5a5a),

      // Canvas chrome.
      .canvas_backdrop = rgb(0x242629),
      .canvas_empty_text = rgb(0xaab0b8),
      .canvas_document_border = rgb(0x5f656e),
      .ruler_bar_bg = rgb(0x2a2d31),
      .ruler_corner_bg = rgb(0x23262a),
      .ruler_edge = rgb(0x4e525a),
      .ruler_tick = rgb(0xb9bec6),
      .canvas_hud_bg = rgb(0x1f2329),
      .canvas_hud_border = rgb(0x4e5660),
      .canvas_hud_text = rgb(0xeef2f7),
      .canvas_hud_spinner = rgb(0xeef4fa),

      // Scroll bars.
      .canvas_scrollbar_track = rgb(0x262626),
      .panel_scrollbar_track = rgb(0x262626),
      .scrollbar_handle_bg = rgb(0x565656),
      .scrollbar_handle_border = rgb(0x6e6e6e),
      .scrollbar_handle_hover_bg = rgb(0x646464),

      // macOS-only group boxes.
      .group_box_border = rgb(0x4f4f4f),

      // Preferences dialog.
      .dialog_tab_bg = rgb(0x2b2b2b),
      .dialog_tab_border = rgb(0x444444),
      .dialog_tab_text = rgb(0xdcdcdc),
      .dialog_tab_hover_bg = rgb(0x343434),
      .dialog_tab_selected_bg = rgb(0x383838),
      .panel_card_bg = rgb(0x303030),
      .panel_card_border = rgb(0x464646),
      .color_button_border = rgb(0x626262),
      .color_button_hover_bg = rgb(0x404040),
      .color_button_hover_border = rgb(0x80bfff),
      .grid_preview_bg = rgb(0x202020),
      .grid_preview_border = rgb(0x575757),

      .link_text = rgb(0x7fa8cf),

      // Help > About.
      .splash_bg = rgb(0x171d26),
      .splash_border = rgb(0x3b4655),
      .splash_title_text = rgb(0xf5f8fb),
      .splash_subtitle_text = rgb(0xc9d5e2),
      .splash_body_text = rgb(0xedf3f8),
      .splash_caption_text = rgb(0xb7c3d2),
      .splash_status_text = rgb(0x93a2b3),
      .splash_link_text = rgb(0x9ed0ff),
      .splash_button_bg = rgb(0x263242),
      .splash_button_border = rgb(0x536277),
      .splash_button_hover_bg = rgb(0x304057),
      .splash_primary_bg = rgb(0x2f7fc1),
      .splash_primary_border = rgb(0x63a9df),
      .splash_primary_hover_bg = rgb(0x358fd9),

      // Hotkey editor.
      .hotkey_hint_text = rgb(0xa8b4be),
      .hotkey_muted_text = rgb(0x9a9a9a),
      .hotkey_header_border = rgb(0x383838),
      .hotkey_conflict_text = rgb(0xe8b04a),
      .hotkey_search_bg = rgb(0x343434),
      .hotkey_search_border = rgb(0x4a4a4a),
      .hotkey_search_text = rgb(0xb5b5b5),
      .hotkey_shortcut_text = rgb(0x9ab8d6),
      .hotkey_shortcut_active_text = rgb(0xcfe5ff),
      .hotkey_recording_bg = rgb(0x252525),
      .hotkey_recording_text = rgb(0x9fc1e4),
      .hotkey_warning_bg = rgb(0x322d20),
      .hotkey_warning_border = rgb(0x6a5a2c),
      .hotkey_warning_text = rgb(0xe8c87a),

      // Font picker popup.
      .popup_list_bg = rgb(0x232323),

      // New Document.
      .nd_section_text = rgb(0xa8a8a8),
      .nd_chip_border = rgb(0x474747),
      .nd_chip_text = rgb(0xd9d9d9),
      .nd_field_border = rgb(0x4a4a4a),
      .nd_field_disabled_bg = rgb(0x2a2a2a),
      .nd_field_disabled_border = rgb(0x383838),
      .nd_field_disabled_text = rgb(0x6f6f6f),
      .nd_card_disabled_bg = rgb(0x2b2b2b),
      .nd_card_disabled_border = rgb(0x393939),
      .nd_card_selected_bg = rgb(0x334152),
      .nd_card_hover_border = rgb(0x5f5f5f),
      .nd_card_title_text = rgb(0xe8e8e8),
      .nd_card_title_disabled_text = rgb(0x6f6f6f),
      .nd_card_detail_text = rgb(0xa5a5a5),
      .nd_card_detail_disabled_text = rgb(0x5c5c5c),
      .nd_card_meta_text = rgb(0x838383),
      .nd_card_meta_disabled_text = rgb(0x505050),
      .nd_thumb_fill = rgb(0x474747),
      .nd_thumb_fill_disabled = rgb(0x353535),
      .nd_thumb_border = rgb(0xa2a2a2),
      .nd_thumb_border_disabled = rgb(0x585858),
      .nd_thumb_outline = rgb(0x9a9a9a),

      // Image Size and Canvas Size.
      .dlg_raised_bg = rgb(0x555555),
      .dlg_raised_text = rgb(0xf2f2f2),
      .dlg_raised_border = rgb(0x8b8b8b),
      .dlg_raised_hover_bg = rgb(0x606060),
      .dlg_raised_hover_border = rgb(0xb5b5b5),
      .dlg_field_bg = rgb(0x1e1e1e),
      .dlg_field_border = rgb(0x242424),
      .dlg_field_text = rgb(0xd8d8d8),
      .dlg_button_bg = rgb(0x4a4a4a),
      .dlg_button_border = rgb(0x686868),
      .dlg_button_focus_bg = rgb(0x3f3f3f),
      .dlg_focus_border = rgb(0x9abbe7),
      .dlg_focus_ring = rgb(0x2d8cff),
      .dlg_anchor_active_bg = rgb(0x424f5f),
      .dlg_neutral_border = rgb(0x9a9a9a),
      .dlg_neutral_border_bright = rgb(0xc8c8c8),
      .dlg_divider = rgb(0x727272),
      .dlg_tab_bg = rgb(0x4f4f4f),
      .dlg_tab_border = rgb(0x767676),
      .dlg_grid_bg = rgb(0x5a5a5a),
      .dlg_grid_border = rgb(0x8a8a8a),
      .dlg_cell_bg = rgb(0x5c5c5c),
      .dlg_cell_border = rgb(0x777777),
      .dlg_cell_hover_bg = rgb(0x656565),
      .dlg_action_border = rgb(0x8c8c8c),
      .dlg_action_hover_border = rgb(0xb7b7b7),

      // Layer Styles warnings and swatches.
      .warning_banner_bg = rgb(0x4a3a1f),
      .warning_banner_border = rgb(0x9a7430),
      .warning_banner_text = rgb(0xffe0a3),
      .swatch_border = rgb(0x9aa4b2),

      // Shared button and surface families.
      .primary_bg = rgb(0x354960),
      .primary_border = rgb(0x6f9bd1),
      .primary_hover_bg = rgb(0x3f5773),
      .neutral_button_hover_bg = rgb(0x454545),
      .neutral_button_hover_border = rgb(0x7d7d7d),
      .list_surface_bg = rgb(0x222222),
      .list_surface_border = rgb(0x1b1b1b),
      .selection_soft_bg = rgb(0x33414f),

      // Start panel.
      .start_panel_title_text = rgb(0xe9e9e9),
      .start_panel_tagline_text = rgb(0x9fb0c0),
      .start_panel_muted_text = rgb(0x8b8b8b),
      .start_panel_status_text = rgb(0x7a8a9a),
      .start_panel_section_text = rgb(0x9a9a9a),
      .start_panel_hint_text = rgb(0x7a7a7a),
      .start_panel_row_hover_bg = rgb(0x323232),

      // Icon ink.
      .icon_ink = rgb(0xdce2eb),
      .icon_accent = rgb(0x74c0ff),
      .icon_accent_soft = rgb(0xb8dcff),
      .icon_accent_tint = rgb(0xacd8ff),
      .icon_danger = rgb(0xff9696),
      .icon_warning = rgb(0xffc078),
      .icon_folder = rgb(0xf5cd69),
      .icon_folder_fill = rgb(0x3a3320),
      .icon_success = rgb(0x9be9a8),
      .icon_surface = rgb(0x242628),
  };
  return palette;
}

namespace {

// Raises a mirrored neutral into Light's surface band.
//
// A mirror alone is not enough. Dark's surfaces are packed into the bottom third
// of the value range (0x1e to 0x5d covers the canvas, every panel, every toolbar
// and the tool palette), so mirroring parks all of them between 0xa2 and 0xe1 and
// the result reads as a medium-gray app rather than a light one. Light schemes
// put their surfaces in the top fifth instead, which is what makes them look lit.
//
// Ink mirrors low and must not move: body text, muted labels and disabled text
// all land under the first stop and pass through unchanged. Above it the curve
// climbs steeply through the chrome band and then flattens as it approaches
// white, because that end has no room left. It is a table rather than a formula
// because the stops were placed against the rendered app: the first has to clear
// disabled text, and the knee has to lift the tool palette (Dark's lightest
// chrome, and therefore Light's darkest) without flattening it into the panels.
//
// Monotonic, so no two roles can swap order; only the spacing changes, and near
// white it compresses. Hairlines that carry real structure are corrected by hand
// in light_palette() below.
[[nodiscard]] qreal lift_to_surface_band(qreal mirrored) {
  static constexpr std::array<std::pair<qreal, qreal>, 6> kRamp{{
      {0.00, 0.00},
      {0.58, 0.58},
      {0.66, 0.82},
      {0.75, 0.885},
      {0.85, 0.935},
      {1.00, 1.00},
  }};
  for (std::size_t index = 1; index < kRamp.size(); ++index) {
    const auto [from_x, from_y] = kRamp[index - 1];
    const auto [to_x, to_y] = kRamp[index];
    if (mirrored <= to_x) {
      const qreal span = to_x - from_x;
      const qreal along = span <= 0.0 ? 1.0 : (mirrored - from_x) / span;
      return from_y + along * (to_y - from_y);
    }
  }
  return kRamp.back().second;
}

// Flips one dark role to its light counterpart.
//
// The palette has a long tail of one-off neutrals (a divider here, a disabled
// button fill there) whose only job is to sit a defined distance from their
// neighbours. Hand-picking a light value for each of those is busywork that
// invites inconsistency, and forgetting one leaves a dark smear in an otherwise
// light dialog. So the default is derived and the exceptions are explicit.
//
// Neutrals mirror their lightness and are then lifted into Light's surface band,
// which preserves every "this is two steps darker than that" relationship within
// a family. Chromatic colors keep their hue and saturation but are pulled toward
// the middle instead: a bright accent designed to glow on near-black becomes
// unreadable if simply inverted, while a mid-tone of the same hue reads on both.
QColor flip_for_light(const QColor& color) {
  const auto hsl = color.toHsl();
  const qreal hue = hsl.hslHueF();
  const qreal saturation = hsl.hslSaturationF();
  const qreal lightness = hsl.lightnessF();
  constexpr qreal kNeutralSaturation = 0.15;
  const qreal flipped = 1.0 - lightness;
  const qreal target = saturation < kNeutralSaturation ? lift_to_surface_band(flipped)
                                                       : std::clamp(flipped, 0.28, 0.66);
  auto result = QColor::fromHslF(hue < 0.0 ? 0.0 : hue, saturation, std::clamp(target, 0.0, 1.0));
  result.setAlpha(color.alpha());
  return result.toRgb();
}

ThemePalette derive_light_palette() {
  ThemePalette light;
  for (const auto& [name, member] : theme_palette_roles()) {
    light.*member = flip_for_light(dark_palette().*member);
  }
  return light;
}

}  // namespace

const ThemePalette& light_palette() {
  static const ThemePalette palette = [] {
    ThemePalette light = derive_light_palette();

    // Everything below is a place where a mechanical flip is the wrong answer.

    // The pasteboard around the document never goes white: a white surround
    // destroys value judgement while editing, which is why Photoshop and every
    // other editor keep a mid gray in light mode. Its scroll-bar track is the
    // same surface and has to match exactly or the gutter reads as a seam. The
    // canvas bars also lay the scroll-dither texture over that track, so the
    // Light variant of the asset (icons/light/scroll-dither.svg) is authored
    // around this value; move one and move the other.
    //
    // It also has to stay clear of title_bar_bg below. A flat flip put both at
    // the same gray, so a dialog floating over the canvas lost its title bar
    // into the pasteboard behind it.
    light.canvas_backdrop = rgb(0xb0b0b0);
    light.canvas_scrollbar_track = rgb(0xb0b0b0);
    // The document edge has to stay visible against that mid gray rather than
    // becoming the near-white a flip would produce.
    light.canvas_document_border = rgb(0x6e747c);

    // The chrome bar sits between the pasteboard and the window surface, and has
    // to read as distinct from both: darker than the dialog body it heads, and
    // clearly lighter than the canvas it may float over. A slight cool cast
    // separates it from the neutral grays around it.
    light.title_bar_bg = rgb(0xdcdfe4);
    light.title_bar_border = rgb(0xc0c4ca);

    // Brand and state colors carry meaning, so they hold their hue rather than
    // being pushed to a mid tone. The blue accent already reads on both.
    light.accent = rgb(0x1473e6);
    light.window_close_hover_bg = rgb(0xc42b1c);
    light.window_close_pressed_bg = rgb(0x9f2117);

    // An inset control's top edge is a raised highlight against a dark surface
    // and has to become a cast shadow against a light one. Flipping the value
    // would keep it a highlight and invert the bevel.
    light.field_bevel_top = rgb(0xa8a8a8);

    // A tab bar says "current document" with lightness, and a mirror keeps the
    // value while inverting the claim: Dark lifts the selected tab above its
    // neighbours (0x3f over 0x2b), so Light handed it back as the darkest of the
    // three and the background document looked like the active one. Light keeps
    // the meaning instead. The selected tab rises to near white, above the
    // window surface the strip itself is painted in, and the unselected tabs
    // drop below that surface so they read as recessed into it. Hover still sits
    // between the two, moving toward the selected end.
    light.tab_bg = rgb(0xdedede);
    light.tab_hover_bg = rgb(0xe6e6e6);
    light.tab_selected_bg = rgb(0xf8f8f8);

    // The Preferences tab strip carries the same inversion, one step subtler.
    // Only these two move: dialog_tab_bg is also the pane behind the tabs, so
    // darkening it to separate the unselected tabs would darken the settings
    // sheet with them.
    light.dialog_tab_hover_bg = rgb(0xf2f2f2);
    light.dialog_tab_selected_bg = rgb(0xf9f9f9);

    // And again in the color picker, which keeps its wider step: 0x26 of
    // lightness between the unselected and selected mode against the document
    // tabs' 0x1a, the same spread it has in Dark. Its outline is the other half
    // of the correction. A mirrored 1px line lands lighter than the tabs it is
    // supposed to bound, which on a near-white strip draws nothing, so the tabs
    // ran together into one pale block.
    light.picker_tab_bg = rgb(0xd4d4d4);
    light.picker_tab_border = rgb(0xc4c4c4);
    light.picker_tab_hover_bg = rgb(0xe4e4e4);
    light.picker_tab_selected_bg = rgb(0xfafafa);

    // The About dialog's surfaces are surfaces, and the cool cast they wear in
    // Dark is a tint on a near-black, not a brand color. That cast still carries
    // enough saturation to fail the neutral test in flip_for_light(), so instead
    // of being lifted into Light's surface band all of them clamped into the
    // chromatic mid band: the dialog body, its secondary button, and that
    // button's hover all landed within a few points of #93a4be and the About
    // screen rendered as one flat periwinkle slab. Keep the tint, drop the mid
    // tone. The button family sits a step below the surface and deepens on
    // hover, the direction a light scheme gains contrast, matching the neutral
    // push buttons elsewhere. The other two roles in the family are corrected
    // with their own kind below: splash_primary_bg with the selection fills,
    // splash_border with the outlines.
    light.splash_bg = rgb(0xf3f6fa);
    light.splash_button_bg = rgb(0xe4e9f1);
    light.splash_button_border = rgb(0xa9b4c3);
    light.splash_button_hover_bg = rgb(0xd7dfea);

    // Selection backgrounds keep their saturation rather than washing out to a
    // pale tint. That is the convention in light themes, and it is what keeps the
    // white label on a highlighted menu item or a checked button readable: a
    // lightened selection with white text on it is illegible.
    light.menu_item_selected_bg = rgb(0x2f6fb5);
    light.category_selected_bg = rgb(0x2f6fb5);
    light.accent_checked_bg = rgb(0x1667b7);
    light.splash_primary_bg = rgb(0x2f7fc1);

    // White-on-accent text stays white, because of the four roles above. Text on
    // a raised neutral surface has to invert instead.
    light.text_on_accent = rgb(0xffffff);
    light.text_on_raised = rgb(0x1a1a1a);

    // Edges that separate a surface from what is behind it. Flipping a near-black
    // outline gives a near-white one, which against a light surface is no edge at
    // all: the frameless main window would lose its silhouette against a light
    // desktop, and an open menu would blend into the panel underneath it.
    // Dark enough to outline a dialog against the canvas pasteboard, not just
    // against the window surface.
    light.window_border = rgb(0x7d7d7d);
    light.menu_border = rgb(0xa8a8a8);
    light.splash_border = rgb(0xa8adb5);

    // The same inversion, one level in: outlines and recesses drawn INSIDE the
    // window. Each of these is darker than the surface it sits on in Dark, so the
    // mirror hands back a line lighter than that surface, and the surface band is
    // near white with nowhere for such a line to show. Text fields would lose
    // their well, the tool column and docks their outline, and the splitter its
    // groove. Values are roughly twice their Dark separation because the same
    // step reads weaker at the light end of the range.
    // A panel scroll bar's groove is the same recess, one step subtler. The
    // mirror lands it on exactly the same near-white as window_bg, which in Dark
    // is harmless (the dither carried the texture and the surface was near-black)
    // but in Light erases the gutter: the bar becomes a floating handle with no
    // channel to run in. Drop it just far enough to read as a recess while
    // staying lighter than the handle that slides over it.
    light.panel_scrollbar_track = rgb(0xe6e6e6);

    // The handle is the control and the groove is the surface it runs in, so the
    // handle is the light element on a dark scheme and the dark one on a light
    // scheme. The mirror preserved its value instead of its role and left a
    // near-white handle sliding over the mid-gray canvas gutter, which read as a
    // gap in the bar rather than a grip. One fill serves both families, so it has
    // to clear the darker of the two tracks: it sits a Dark-sized step below the
    // pasteboard gray the canvas gutter is painted in, which leaves it well below
    // the near-white panel groove as well. The border deepens rather than
    // brightens it, and hover deepens it further, the direction a light scheme
    // gains contrast.
    light.scrollbar_handle_bg = rgb(0x8a8a8a);
    light.scrollbar_handle_border = rgb(0x6e6e6e);
    light.scrollbar_handle_hover_bg = rgb(0x757575);

    light.field_inset_border = rgb(0xc6c6c6);
    light.toolbar_border = rgb(0xcbcbcb);
    light.tool_palette_border = rgb(0xa9a9a9);
    light.panel_border_strong = rgb(0xcdcdcd);
    light.panel_title_border_bottom = rgb(0xd5d6d8);
    light.list_surface_border = rgb(0xd9d9d9);
    light.list_item_border = rgb(0xe6e6e7);
    light.splitter_bg = rgb(0xdedede);

    // The clipping badge is a pale blue chosen to glow on a dark row. A flip
    // clamps it to a heavy navy; match the icon accent instead so it reads as
    // the same family of marker and holds up on both an unselected light row and
    // the blue of a selected one.
    light.layer_clip_badge = rgb(0x1668c4);

    // Icon ink. The flip lands close, but these are the most-looked-at pixels in
    // the app and deserve exact values.
    light.icon_ink = rgb(0x333a42);
    light.icon_accent = rgb(0x1668c4);
    light.icon_accent_soft = rgb(0x5f9fe0);
    light.icon_accent_tint = rgb(0x5f9fe0);
    light.icon_danger = rgb(0xc0392b);
    light.icon_warning = rgb(0xb96a12);
    light.icon_folder = rgb(0xb98600);
    light.icon_folder_fill = rgb(0xfdf0c8);
    light.icon_success = rgb(0x1f8a3d);
    light.icon_surface = rgb(0xf0f0f0);

    return light;
  }();
  return palette;
}

const ThemePalette& theme(ColorScheme scheme) {
  return scheme == ColorScheme::Light ? light_palette() : dark_palette();
}

const ThemePalette& theme() { return theme(g_active_scheme); }

ColorScheme active_color_scheme() { return g_active_scheme; }

void set_active_color_scheme(ColorScheme scheme) {
  if (scheme == g_active_scheme) {
    return;
  }
  g_active_scheme = scheme;
  ++g_generation;
}

int theme_generation() { return g_generation; }

std::span<const ThemePaletteRole> theme_palette_roles() {
// The macro spells the role name from the member itself, so the token used in
// QSS and the struct field can never drift apart.
#define PATCHY_THEME_ROLE(member) ThemePaletteRole{QLatin1StringView(#member), &ThemePalette::member}
  static const auto roles = std::to_array<ThemePaletteRole>({
      PATCHY_THEME_ROLE(window_bg),
      PATCHY_THEME_ROLE(window_border),
      PATCHY_THEME_ROLE(text_primary),
      PATCHY_THEME_ROLE(text_secondary),
      PATCHY_THEME_ROLE(text_bright),
      PATCHY_THEME_ROLE(text_disabled),
      PATCHY_THEME_ROLE(text_on_accent),
      PATCHY_THEME_ROLE(text_on_raised),
      PATCHY_THEME_ROLE(splitter_bg),
      PATCHY_THEME_ROLE(splitter_hover_bg),
      PATCHY_THEME_ROLE(dock_separator_bg),

      PATCHY_THEME_ROLE(title_bar_bg),
      PATCHY_THEME_ROLE(title_bar_border),
      PATCHY_THEME_ROLE(menu_bar_item_hover_bg),
      PATCHY_THEME_ROLE(menu_bar_text_disabled),
      PATCHY_THEME_ROLE(menu_bg),
      PATCHY_THEME_ROLE(menu_border),
      PATCHY_THEME_ROLE(menu_item_selected_bg),
      PATCHY_THEME_ROLE(menu_separator),

      PATCHY_THEME_ROLE(toolbar_bg),
      PATCHY_THEME_ROLE(toolbar_border),
      PATCHY_THEME_ROLE(button_hover_bg),
      PATCHY_THEME_ROLE(button_hover_border),
      PATCHY_THEME_ROLE(accent_pressed_bg),
      PATCHY_THEME_ROLE(accent_border_bright),

      PATCHY_THEME_ROLE(window_chrome_hover_bg),
      PATCHY_THEME_ROLE(window_chrome_pressed_bg),
      PATCHY_THEME_ROLE(window_close_hover_bg),
      PATCHY_THEME_ROLE(window_close_pressed_bg),

      PATCHY_THEME_ROLE(tool_palette_bg),
      PATCHY_THEME_ROLE(tool_palette_border),
      PATCHY_THEME_ROLE(tool_palette_separator),

      PATCHY_THEME_ROLE(options_bar_bg),
      PATCHY_THEME_ROLE(options_bar_top_edge),
      PATCHY_THEME_ROLE(option_separator),
      PATCHY_THEME_ROLE(option_chip_bg),
      PATCHY_THEME_ROLE(field_bg),
      PATCHY_THEME_ROLE(field_inset_border),
      PATCHY_THEME_ROLE(field_bevel_top),
      PATCHY_THEME_ROLE(options_button_bg),
      PATCHY_THEME_ROLE(accent_checked_bg),
      PATCHY_THEME_ROLE(accent_checked_border),

      PATCHY_THEME_ROLE(checkbox_compact_bg),
      PATCHY_THEME_ROLE(checkbox_compact_border),
      PATCHY_THEME_ROLE(checkbox_indicator_bg),
      PATCHY_THEME_ROLE(checkbox_indicator_border),
      PATCHY_THEME_ROLE(checkbox_accent_border),
      PATCHY_THEME_ROLE(accent),
      PATCHY_THEME_ROLE(slider_groove_bg),
      PATCHY_THEME_ROLE(slider_groove_border),
      PATCHY_THEME_ROLE(slider_fill_border),
      PATCHY_THEME_ROLE(slider_handle_bg),
      PATCHY_THEME_ROLE(slider_handle_border),
      PATCHY_THEME_ROLE(spinbox_disabled_bg),
      PATCHY_THEME_ROLE(spinbox_disabled_text),
      PATCHY_THEME_ROLE(spin_button_disabled_bg),
      PATCHY_THEME_ROLE(spin_button_disabled_bevel),

      PATCHY_THEME_ROLE(dock_title_bg),
      PATCHY_THEME_ROLE(panel_border_strong),
      PATCHY_THEME_ROLE(panel_title_bg),
      PATCHY_THEME_ROLE(panel_title_bevel_top),
      PATCHY_THEME_ROLE(panel_title_border_bottom),
      PATCHY_THEME_ROLE(dock_collapse_text),
      PATCHY_THEME_ROLE(dock_collapse_hover_bg),
      PATCHY_THEME_ROLE(dock_collapse_hover_border),
      PATCHY_THEME_ROLE(panel_bg),
      PATCHY_THEME_ROLE(panel_inset_bg),
      PATCHY_THEME_ROLE(panel_inset_border),
      PATCHY_THEME_ROLE(info_text),

      PATCHY_THEME_ROLE(field_bg_large),
      PATCHY_THEME_ROLE(field_border),
      PATCHY_THEME_ROLE(field_bg_disabled),
      PATCHY_THEME_ROLE(field_text_disabled),
      PATCHY_THEME_ROLE(field_border_disabled),
      PATCHY_THEME_ROLE(list_selection_bg),
      PATCHY_THEME_ROLE(list_item_border),
      PATCHY_THEME_ROLE(list_selection_text),
      PATCHY_THEME_ROLE(list_selection_border),
      PATCHY_THEME_ROLE(category_list_item_border),
      PATCHY_THEME_ROLE(category_selected_bg),
      PATCHY_THEME_ROLE(category_selected_border),
      PATCHY_THEME_ROLE(history_future_text),

      PATCHY_THEME_ROLE(layer_row_bg),
      PATCHY_THEME_ROLE(layer_row_border),
      PATCHY_THEME_ROLE(layer_row_group_bg),
      PATCHY_THEME_ROLE(layer_row_selected_bg),
      PATCHY_THEME_ROLE(layer_row_selected_border),
      PATCHY_THEME_ROLE(layer_row_name_text),
      PATCHY_THEME_ROLE(layer_row_details_text),
      PATCHY_THEME_ROLE(layer_glyph_text),
      PATCHY_THEME_ROLE(layer_visibility_text),
      PATCHY_THEME_ROLE(layer_button_hover_bg),
      PATCHY_THEME_ROLE(layer_button_hover_border),
      PATCHY_THEME_ROLE(layer_lock_border),
      PATCHY_THEME_ROLE(layer_lock_hover_border),
      PATCHY_THEME_ROLE(layer_lock_checked_bg),
      PATCHY_THEME_ROLE(layer_lock_checked_border),
      PATCHY_THEME_ROLE(layer_lock_mixed_bg),
      PATCHY_THEME_ROLE(layer_lock_mixed_border),
      PATCHY_THEME_ROLE(layer_drop_bg),
      PATCHY_THEME_ROLE(layer_depth_guide),
      PATCHY_THEME_ROLE(layer_eye_on),
      PATCHY_THEME_ROLE(layer_eye_off),
      PATCHY_THEME_ROLE(layer_lock_badge_active),
      PATCHY_THEME_ROLE(layer_lock_badge_inherited),
      PATCHY_THEME_ROLE(layer_thumbnail_border),
      PATCHY_THEME_ROLE(layer_mask_disabled_cross),
      PATCHY_THEME_ROLE(layer_clip_badge),

      PATCHY_THEME_ROLE(accent_bright),
      PATCHY_THEME_ROLE(accent_bright_hover),
      PATCHY_THEME_ROLE(accent_bright_border),
      PATCHY_THEME_ROLE(text_on_accent_bright),

      PATCHY_THEME_ROLE(button_bg),
      PATCHY_THEME_ROLE(button_border),
      PATCHY_THEME_ROLE(button_hover_border_strong),

      PATCHY_THEME_ROLE(status_bar_bg),
      PATCHY_THEME_ROLE(status_text),
      PATCHY_THEME_ROLE(status_text_disabled),
      PATCHY_THEME_ROLE(status_field_bg),
      PATCHY_THEME_ROLE(status_field_border),
      PATCHY_THEME_ROLE(status_field_border_disabled),
      PATCHY_THEME_ROLE(status_error_text),
      PATCHY_THEME_ROLE(status_error_wash),
      PATCHY_THEME_ROLE(status_warning_fill),
      PATCHY_THEME_ROLE(status_error_mark),

      PATCHY_THEME_ROLE(drop_indicator_bg),
      PATCHY_THEME_ROLE(drop_indicator_border),
      PATCHY_THEME_ROLE(resize_handle_bg),
      PATCHY_THEME_ROLE(resize_handle_border),
      PATCHY_THEME_ROLE(hint_text),
      PATCHY_THEME_ROLE(script_detail_text),
      PATCHY_THEME_ROLE(console_warning_text),
      PATCHY_THEME_ROLE(console_error_text),
      PATCHY_THEME_ROLE(list_row_hover_bg),
      PATCHY_THEME_ROLE(preview_pane_bg),
      PATCHY_THEME_ROLE(preview_pane_muted_text),
      PATCHY_THEME_ROLE(accent_control),

      PATCHY_THEME_ROLE(tab_bg),
      PATCHY_THEME_ROLE(tab_hover_bg),
      PATCHY_THEME_ROLE(tab_selected_bg),
      PATCHY_THEME_ROLE(tab_pane_border),

      PATCHY_THEME_ROLE(picker_tab_bg),
      PATCHY_THEME_ROLE(picker_tab_border),
      PATCHY_THEME_ROLE(picker_tab_hover_bg),
      PATCHY_THEME_ROLE(picker_tab_selected_bg),

      PATCHY_THEME_ROLE(canvas_backdrop),
      PATCHY_THEME_ROLE(canvas_empty_text),
      PATCHY_THEME_ROLE(canvas_document_border),
      PATCHY_THEME_ROLE(ruler_bar_bg),
      PATCHY_THEME_ROLE(ruler_corner_bg),
      PATCHY_THEME_ROLE(ruler_edge),
      PATCHY_THEME_ROLE(ruler_tick),
      PATCHY_THEME_ROLE(canvas_hud_bg),
      PATCHY_THEME_ROLE(canvas_hud_border),
      PATCHY_THEME_ROLE(canvas_hud_text),
      PATCHY_THEME_ROLE(canvas_hud_spinner),

      PATCHY_THEME_ROLE(canvas_scrollbar_track),
      PATCHY_THEME_ROLE(panel_scrollbar_track),
      PATCHY_THEME_ROLE(scrollbar_handle_bg),
      PATCHY_THEME_ROLE(scrollbar_handle_border),
      PATCHY_THEME_ROLE(scrollbar_handle_hover_bg),

      PATCHY_THEME_ROLE(group_box_border),

      PATCHY_THEME_ROLE(dialog_tab_bg),
      PATCHY_THEME_ROLE(dialog_tab_border),
      PATCHY_THEME_ROLE(dialog_tab_text),
      PATCHY_THEME_ROLE(dialog_tab_hover_bg),
      PATCHY_THEME_ROLE(dialog_tab_selected_bg),
      PATCHY_THEME_ROLE(panel_card_bg),
      PATCHY_THEME_ROLE(panel_card_border),
      PATCHY_THEME_ROLE(color_button_border),
      PATCHY_THEME_ROLE(color_button_hover_bg),
      PATCHY_THEME_ROLE(color_button_hover_border),
      PATCHY_THEME_ROLE(grid_preview_bg),
      PATCHY_THEME_ROLE(grid_preview_border),

      PATCHY_THEME_ROLE(link_text),

      PATCHY_THEME_ROLE(splash_bg),
      PATCHY_THEME_ROLE(splash_border),
      PATCHY_THEME_ROLE(splash_title_text),
      PATCHY_THEME_ROLE(splash_subtitle_text),
      PATCHY_THEME_ROLE(splash_body_text),
      PATCHY_THEME_ROLE(splash_caption_text),
      PATCHY_THEME_ROLE(splash_status_text),
      PATCHY_THEME_ROLE(splash_link_text),
      PATCHY_THEME_ROLE(splash_button_bg),
      PATCHY_THEME_ROLE(splash_button_border),
      PATCHY_THEME_ROLE(splash_button_hover_bg),
      PATCHY_THEME_ROLE(splash_primary_bg),
      PATCHY_THEME_ROLE(splash_primary_border),
      PATCHY_THEME_ROLE(splash_primary_hover_bg),

      PATCHY_THEME_ROLE(hotkey_hint_text),
      PATCHY_THEME_ROLE(hotkey_muted_text),
      PATCHY_THEME_ROLE(hotkey_header_border),
      PATCHY_THEME_ROLE(hotkey_conflict_text),
      PATCHY_THEME_ROLE(hotkey_search_bg),
      PATCHY_THEME_ROLE(hotkey_search_border),
      PATCHY_THEME_ROLE(hotkey_search_text),
      PATCHY_THEME_ROLE(hotkey_shortcut_text),
      PATCHY_THEME_ROLE(hotkey_shortcut_active_text),
      PATCHY_THEME_ROLE(hotkey_recording_bg),
      PATCHY_THEME_ROLE(hotkey_recording_text),
      PATCHY_THEME_ROLE(hotkey_warning_bg),
      PATCHY_THEME_ROLE(hotkey_warning_border),
      PATCHY_THEME_ROLE(hotkey_warning_text),

      PATCHY_THEME_ROLE(popup_list_bg),

      PATCHY_THEME_ROLE(nd_section_text),
      PATCHY_THEME_ROLE(nd_chip_border),
      PATCHY_THEME_ROLE(nd_chip_text),
      PATCHY_THEME_ROLE(nd_field_border),
      PATCHY_THEME_ROLE(nd_field_disabled_bg),
      PATCHY_THEME_ROLE(nd_field_disabled_border),
      PATCHY_THEME_ROLE(nd_field_disabled_text),
      PATCHY_THEME_ROLE(nd_card_disabled_bg),
      PATCHY_THEME_ROLE(nd_card_disabled_border),
      PATCHY_THEME_ROLE(nd_card_selected_bg),
      PATCHY_THEME_ROLE(nd_card_hover_border),
      PATCHY_THEME_ROLE(nd_card_title_text),
      PATCHY_THEME_ROLE(nd_card_title_disabled_text),
      PATCHY_THEME_ROLE(nd_card_detail_text),
      PATCHY_THEME_ROLE(nd_card_detail_disabled_text),
      PATCHY_THEME_ROLE(nd_card_meta_text),
      PATCHY_THEME_ROLE(nd_card_meta_disabled_text),
      PATCHY_THEME_ROLE(nd_thumb_fill),
      PATCHY_THEME_ROLE(nd_thumb_fill_disabled),
      PATCHY_THEME_ROLE(nd_thumb_border),
      PATCHY_THEME_ROLE(nd_thumb_border_disabled),
      PATCHY_THEME_ROLE(nd_thumb_outline),

      PATCHY_THEME_ROLE(dlg_raised_bg),
      PATCHY_THEME_ROLE(dlg_raised_text),
      PATCHY_THEME_ROLE(dlg_raised_border),
      PATCHY_THEME_ROLE(dlg_raised_hover_bg),
      PATCHY_THEME_ROLE(dlg_raised_hover_border),
      PATCHY_THEME_ROLE(dlg_field_bg),
      PATCHY_THEME_ROLE(dlg_field_border),
      PATCHY_THEME_ROLE(dlg_field_text),
      PATCHY_THEME_ROLE(dlg_button_bg),
      PATCHY_THEME_ROLE(dlg_button_border),
      PATCHY_THEME_ROLE(dlg_button_focus_bg),
      PATCHY_THEME_ROLE(dlg_focus_border),
      PATCHY_THEME_ROLE(dlg_focus_ring),
      PATCHY_THEME_ROLE(dlg_anchor_active_bg),
      PATCHY_THEME_ROLE(dlg_neutral_border),
      PATCHY_THEME_ROLE(dlg_neutral_border_bright),
      PATCHY_THEME_ROLE(dlg_divider),
      PATCHY_THEME_ROLE(dlg_tab_bg),
      PATCHY_THEME_ROLE(dlg_tab_border),
      PATCHY_THEME_ROLE(dlg_grid_bg),
      PATCHY_THEME_ROLE(dlg_grid_border),
      PATCHY_THEME_ROLE(dlg_cell_bg),
      PATCHY_THEME_ROLE(dlg_cell_border),
      PATCHY_THEME_ROLE(dlg_cell_hover_bg),
      PATCHY_THEME_ROLE(dlg_action_border),
      PATCHY_THEME_ROLE(dlg_action_hover_border),

      PATCHY_THEME_ROLE(warning_banner_bg),
      PATCHY_THEME_ROLE(warning_banner_border),
      PATCHY_THEME_ROLE(warning_banner_text),
      PATCHY_THEME_ROLE(swatch_border),

      PATCHY_THEME_ROLE(primary_bg),
      PATCHY_THEME_ROLE(primary_border),
      PATCHY_THEME_ROLE(primary_hover_bg),
      PATCHY_THEME_ROLE(neutral_button_hover_bg),
      PATCHY_THEME_ROLE(neutral_button_hover_border),
      PATCHY_THEME_ROLE(list_surface_bg),
      PATCHY_THEME_ROLE(list_surface_border),
      PATCHY_THEME_ROLE(selection_soft_bg),

      PATCHY_THEME_ROLE(start_panel_title_text),
      PATCHY_THEME_ROLE(start_panel_tagline_text),
      PATCHY_THEME_ROLE(start_panel_muted_text),
      PATCHY_THEME_ROLE(start_panel_status_text),
      PATCHY_THEME_ROLE(start_panel_section_text),
      PATCHY_THEME_ROLE(start_panel_hint_text),
      PATCHY_THEME_ROLE(start_panel_row_hover_bg),

      PATCHY_THEME_ROLE(icon_ink),
      PATCHY_THEME_ROLE(icon_accent),
      PATCHY_THEME_ROLE(icon_accent_soft),
      PATCHY_THEME_ROLE(icon_accent_tint),
      PATCHY_THEME_ROLE(icon_danger),
      PATCHY_THEME_ROLE(icon_warning),
      PATCHY_THEME_ROLE(icon_folder),
      PATCHY_THEME_ROLE(icon_folder_fill),
      PATCHY_THEME_ROLE(icon_success),
      PATCHY_THEME_ROLE(icon_surface),
  });
#undef PATCHY_THEME_ROLE
  return roles;
}

}  // namespace patchy::ui
