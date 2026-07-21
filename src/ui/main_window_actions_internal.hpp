#pragma once

// Build-time context shared by the create_actions() phase builders
// (main_window_actions.cpp, main_window_actions_menus.cpp,
// main_window_actions_tool_palette.cpp, main_window_actions_options_bar.cpp).
// Never include this header outside the main_window_actions*.cpp TUs.
//
// ActionBuildContext carries the cross-phase locals the historical single
// create_actions() body threaded from one construction phase to the next:
// each field is written by exactly one build phase and read by a later one
// (mostly the translation-binding pass in main_window_actions.cpp). The
// context lives on create_actions()'s stack and dies when it returns, so no
// lambda may capture the context or a reference into it - capture the pointer
// VALUES instead, exactly like the original function-scope locals.

class QAction;
class QCheckBox;
class QMenu;
class QToolBar;

namespace patchy::ui {

class CanvasWidget;

struct ActionBuildContext {
  // Resolved by create_actions(): the live canvas, or the throwaway
  // startup-defaults donor canvas on its stack when no document exists yet.
  CanvasWidget* canvas_defaults{nullptr};

  // Written by build_menu_bar_actions(). type_menu is read by
  // build_tool_palette() (the Type tool action joins the Type menu); the rest
  // are read by bind_action_translations().
  QMenu* type_menu{nullptr};
  QMenu* window_menu{nullptr};
  QAction* new_action{nullptr};
  QAction* open_action{nullptr};
  QAction* save_action{nullptr};
  QAction* save_as_action{nullptr};
  QAction* export_flat_action{nullptr};
  QAction* page_setup_action{nullptr};
  QAction* print_action{nullptr};
  QAction* close_action{nullptr};
  QAction* close_all_action{nullptr};
  QAction* preferences_action{nullptr};
  QAction* quit_action{nullptr};
  QAction* cut_action{nullptr};
  QAction* copy_action{nullptr};
  QAction* copy_merged_action{nullptr};
  QAction* paste_action{nullptr};
  QAction* transform_action{nullptr};
  QAction* select_all_action{nullptr};
  QAction* clear_selection_action{nullptr};
  QAction* reselect_action{nullptr};
  QAction* inverse_selection_action{nullptr};
  QAction* grow_selection_action{nullptr};
  QAction* similar_selection_action{nullptr};
  QAction* expand_selection_action{nullptr};
  QAction* contract_selection_action{nullptr};
  QAction* border_selection_action{nullptr};
  QAction* layer_transparency_action{nullptr};
  QAction* stroke_selection_action{nullptr};
  QAction* define_brush_tip_action{nullptr};
  QAction* add_layer_action{nullptr};
  QAction* add_folder_action{nullptr};
  QMenu* new_adjustment_layer_menu{nullptr};
  QMenu* new_fill_layer_menu{nullptr};
  QMenu* vector_mask_menu{nullptr};
  QAction* layer_via_copy_action{nullptr};
  QAction* layer_via_cut_action{nullptr};
  QAction* add_mask_action{nullptr};
  QAction* edit_adjustment_action{nullptr};
  QAction* duplicate_layer_action{nullptr};
  QAction* merge_visible_action{nullptr};
  QAction* merge_down_action{nullptr};
  QAction* rename_layer_action{nullptr};
  QAction* delete_layer_action{nullptr};
  QAction* fill_layer_action{nullptr};
  QAction* fill_background_action{nullptr};
  QAction* clear_layer_action{nullptr};
  QAction* flip_h_action{nullptr};
  QAction* flip_v_action{nullptr};
  QAction* layer_up_action{nullptr};
  QAction* layer_down_action{nullptr};
  QMenu* adjustments_menu{nullptr};
  QAction* levels_action{nullptr};
  QAction* curves_action{nullptr};
  QAction* hue_saturation_action{nullptr};
  QAction* color_balance_action{nullptr};
  QAction* image_size_action{nullptr};
  QAction* canvas_size_action{nullptr};
  QAction* crop_action{nullptr};
  QAction* rotate_cw_action{nullptr};
  QAction* rotate_ccw_action{nullptr};
  QAction* shift_seams_action{nullptr};
  QAction* scan_legacy_plugins_action{nullptr};
  QAction* zoom_in{nullptr};
  QAction* zoom_out{nullptr};
  QAction* fit_on_screen{nullptr};
  QAction* zoom_reset{nullptr};
  QAction* selection_edges_action{nullptr};
  QAction* target_path_action{nullptr};
  QAction* tile_preview_action{nullptr};
  QMenu* snap_to_menu{nullptr};
  QMenu* guides_menu{nullptr};
  QAction* new_guide_action{nullptr};
  QAction* new_guide_layout_action{nullptr};
  QAction* clear_selected_guides_action{nullptr};
  QAction* clear_guides_action{nullptr};
  QMenu* screen_size_menu{nullptr};
  QAction* force_refresh_action{nullptr};
  QAction* scripting_guide_action{nullptr};
  QAction* about_action{nullptr};

  // Written by build_tool_palette(), read by bind_action_translations().
  QToolBar* tool_palette{nullptr};
  QAction* default_colors_action{nullptr};
  QAction* swap_colors_action{nullptr};

  // Written by build_options_bar(), read by bind_action_translations().
  QToolBar* options_toolbar{nullptr};
  QAction* brush_smaller_action{nullptr};
  QAction* brush_larger_action{nullptr};
  QAction* brush_much_smaller_action{nullptr};
  QAction* brush_much_larger_action{nullptr};
  QCheckBox* fill_shapes{nullptr};
};

}  // namespace patchy::ui
