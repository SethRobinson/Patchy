// MainWindow's action/menu/tool-palette/options-bar construction, split out of
// main_window.cpp: create_actions() orchestrates the build phases in their
// historical order (menu bar, tool palette, Options bar, translation binding,
// final refresh). The phase bodies are pure function moves into
// main_window_actions_menus.cpp, main_window_actions_tool_palette.cpp and
// main_window_actions_options_bar.cpp, threaded together by the
// ActionBuildContext in main_window_actions_internal.hpp;
// bind_action_translations() and the retranslation machinery those menus feed
// stay here (register_retranslation, retranslate_ui,
// retranslate_bound_children, refresh_language_actions and the combo
// retranslators). Behavior must stay identical to the pre-split single
// function.

#include "ui/main_window.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/main_window_actions_internal.hpp"

#include "core/blend_math.hpp"
#include "core/layer_metadata.hpp"
#include "core/smart_object.hpp"
#include "core/text_warp.hpp"
#include "core/vector_shape.hpp"
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
#include "ui/pattern_manager_dialog.hpp"
#include "ui/photo_pattern_presets.hpp"
#include "ui/style_library.hpp"
#include "ui/print_dialog.hpp"
#include "ui/smart_object_render.hpp"
#include "ui/scanner_import.hpp"
#include "ui/image_sequence_dialog.hpp"
#include "ui/sprite_sheet_dialog.hpp"
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
#include <initializer_list>
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

void MainWindow::create_actions() {
  // Startup builds the options bar before any document exists (canvas_ is null):
  // a throwaway default-constructed canvas donates the initial control values,
  // which are identical to a fresh session canvas's. Once the first document
  // arrives, load_tool_settings() + sync_tool_option_controls_from_canvas()
  // re-read the controls from the real canvas with the stored settings applied.
  CanvasWidget startup_defaults_canvas;
  // Stack-only build context threading the cross-phase locals between the
  // builders; it dies when create_actions() returns, so builder lambdas
  // capture pointer values from it, never the context itself.
  ActionBuildContext ctx;
  ctx.canvas_defaults = canvas_ != nullptr ? canvas_ : &startup_defaults_canvas;

  // Phase order is load-bearing (widget construction, hotkey registration and
  // register_retranslation callbacks all follow it): menu bar, tool palette,
  // Options bar, translation binding, final refresh.
  build_menu_bar_actions(ctx);
  build_tool_palette(ctx);
  build_options_bar(ctx);
  bind_action_translations(ctx);

  retranslate_brush_preset_combo();
  for (auto* action : menuBar()->actions()) {
    hide_menu_action_icons(action->menu());
  }
  refresh_options_bar();
  refresh_color_buttons();

  update_undo_redo_actions();
}

// The translation-binding pass of create_actions(): binds retranslatable text
// sources to the actions and widgets the three build phases created (reaching
// them through the ActionBuildContext) and registers the toolbars'
// toggle-view actions in the Window menu.
void MainWindow::bind_action_translations(ActionBuildContext& ctx) {
  ctx.window_menu->addAction(ctx.tool_palette->toggleViewAction());
  ctx.window_menu->addAction(ctx.options_toolbar->toggleViewAction());
  // The "Options" toggle would otherwise be captured by macOS's menu-text heuristic
  // (any menubar action containing "options" gets relocated as a Preferences item).
  ctx.tool_palette->toggleViewAction()->setMenuRole(QAction::NoRole);
  ctx.options_toolbar->toggleViewAction()->setMenuRole(QAction::NoRole);
  bind_widget_text(ctx.tool_palette, "Tool Palette");
  bind_widget_text(ctx.options_toolbar, "Options");
  const std::vector<std::pair<QAction*, const char*>> translated_actions = {
      {ctx.new_action, "&New"},
      {ctx.open_action, "&Open..."},
      {recent_files_menu_->menuAction(), "Open &Recent File"},
      {recent_folders_menu_->menuAction(), "Open Recent &Folder"},
      {ctx.save_action, "&Save"},
      {ctx.save_as_action, "Save &As..."},
      {ctx.export_flat_action, "Export &Flat Image..."},
      {ctx.page_setup_action, "Page Set&up..."},
      {ctx.print_action, "&Print..."},
      {ctx.close_action, "&Close"},
      {ctx.close_all_action, "Close &All"},
      {ctx.preferences_action, "&Preferences..."},
      {ctx.quit_action, "&Quit"},
      {undo_action_, "&Undo"},
      {redo_action_, "&Redo"},
      {ctx.cut_action, "Cu&t"},
      {ctx.copy_action, "&Copy"},
      {ctx.copy_merged_action, "Copy Merged"},
      {ctx.paste_action, "&Paste"},
      {ctx.transform_action, "Free &Transform..."},
      {ctx.select_all_action, "Select &All"},
      {ctx.clear_selection_action, "&Clear Selection"},
      {ctx.reselect_action, "&Reselect"},
      {ctx.inverse_selection_action, "&Inverse"},
      {quick_mask_action_, "Edit in &Quick Mask Mode"},
      {ctx.grow_selection_action, "&Grow"},
      {ctx.similar_selection_action, "Simi&lar"},
      {ctx.expand_selection_action, "&Expand..."},
      {ctx.contract_selection_action, "Con&tract..."},
      {ctx.border_selection_action, "&Border..."},
      {ctx.layer_transparency_action, "Load Layer &Transparency"},
      {ctx.stroke_selection_action, "&Stroke Selection"},
      {ctx.define_brush_tip_action, "Define Brush Tip from Selection"},
      {ctx.add_layer_action, "&New Layer"},
      {ctx.add_folder_action, "New &Folder"},
      {ctx.new_adjustment_layer_menu->menuAction(), "New &Adjustment Layer"},
      {ctx.new_fill_layer_menu->menuAction(), "New F&ill Layer"},
      {ctx.vector_mask_menu->menuAction(), "&Vector Mask"},
      {ctx.layer_via_copy_action, "Layer Via &Copy"},
      {ctx.layer_via_cut_action, "Layer Via Cu&t"},
      {ctx.add_mask_action, "Add Layer &Mask"},
      {edit_layer_mask_action_, "&Edit Layer Mask"},
      {mask_overlay_action_, "Show Mask &Overlay"},
      {delete_layer_mask_action_, "&Delete Layer Mask"},
      {link_layer_mask_action_, "Link Layer &Mask"},
      {disable_layer_mask_action_, "&Disable Layer Mask"},
      {invert_layer_mask_action_, "&Invert Layer Mask"},
      {apply_layer_mask_action_, "&Apply Layer Mask"},
      {ctx.edit_adjustment_action, "&Edit Adjustment..."},
      {layer_blending_options_action_, "Edit Layer &Styles..."},
      {layer_copy_style_action_, "Copy Layer Style"},
      {layer_paste_style_action_, "Paste Layer Style"},
      {layer_delete_style_action_, "Delete Layer Style"},
      {layer_rasterize_action_, "Rasterize"},
      {layer_rasterize_layer_style_action_, "Rasterize (including layer style)"},
      {ctx.duplicate_layer_action, "&Duplicate Layer"},
      {ctx.merge_visible_action, "Merge &Visible to New Layer"},
      {ctx.merge_down_action, "Merge &Down"},
      {ctx.rename_layer_action, "&Rename Layer..."},
      {ctx.delete_layer_action, "&Delete Layer"},
      {ctx.fill_layer_action, "&Fill Layer / Selection"},
      {ctx.fill_background_action, "Fill With &Background Color"},
      {ctx.clear_layer_action, "&Clear Layer / Selection"},
      {ctx.flip_h_action, "Flip Layer &Horizontal"},
      {ctx.flip_v_action, "Flip Layer &Vertical"},
      {ctx.layer_up_action, "Move Layer &Up"},
      {ctx.layer_down_action, "Move Layer &Down"},
      {ctx.adjustments_menu->menuAction(), "&Adjustments"},
      {ctx.levels_action, "&Levels..."},
      {ctx.curves_action, "&Curves..."},
      {ctx.hue_saturation_action, "&Hue/Saturation..."},
      {ctx.color_balance_action, "Color &Balance..."},
      {ctx.image_size_action, "&Image Size..."},
      {ctx.canvas_size_action, "&Canvas Size..."},
      {ctx.crop_action, "&Crop to Selection"},
      {ctx.rotate_cw_action, "Rotate 90 &Clockwise"},
      {ctx.rotate_ccw_action, "Rotate 90 Counterclockwise"},
      {ctx.shift_seams_action, "Shift &Seams to Center"},
      {ctx.scan_legacy_plugins_action, "&Scan Legacy Photoshop Plug-ins..."},
      {legacy_plugins_menu_->menuAction(), "Legacy Photoshop Plug-ins"},
      {ctx.zoom_in, "Zoom &In"},
      {ctx.zoom_out, "Zoom &Out"},
      {ctx.fit_on_screen, "&Fit on Screen"},
      {ctx.zoom_reset, "&Actual Pixels"},
      {ctx.selection_edges_action, "Show Selection &Edges"},
      {ctx.target_path_action, "Show Target &Path"},
      {ctx.tile_preview_action, "Seamless &Tile Preview"},
      {tiling_mode_action_, "Seamless Tiling in &Window"},
      {view_rulers_action_, "&Rulers"},
      {view_grid_action_, "&Grid"},
      {view_guides_action_, "&Guides"},
      {view_snap_action_, "&Snap"},
      {view_lock_guides_action_, "Lock Guides"},
      {ctx.snap_to_menu->menuAction(), "Snap &To"},
      {view_snap_guides_action_, "Guides"},
      {view_snap_grid_action_, "Grid"},
      {view_snap_document_action_, "Document Bounds and Center"},
      {view_snap_layers_action_, "Layer Bounds and Centers"},
      {view_snap_selection_action_, "Selection Bounds and Center"},
      {ctx.guides_menu->menuAction(), "Guide Operations"},
      {ctx.new_guide_action, "New Guide..."},
      {ctx.new_guide_layout_action, "New Guide Layout..."},
      {ctx.clear_selected_guides_action, "Clear Selected Guides"},
      {ctx.clear_guides_action, "Clear Guides"},
      {ctx.screen_size_menu->menuAction(), "Set Screen Size"},
      {ctx.force_refresh_action, "Force Refresh"},
      {language_english_action_, "&English"},
      {ctx.scripting_guide_action, "&Scripting Guide"},
      {ctx.about_action, "&About Patchy"},
      {ctx.default_colors_action, "Default Colors"},
      {ctx.swap_colors_action, "Swap Colors"},
      {ctx.brush_smaller_action, "Brush Smaller"},
      {ctx.brush_larger_action, "Brush Larger"},
      {ctx.brush_much_smaller_action, "Brush Much Smaller"},
      {ctx.brush_much_larger_action, "Brush Much Larger"},
  };
  for (const auto& [action, source] : translated_actions) {
    bind_action_text(action, source);
    refresh_action_tooltip(action);
  }
  const std::vector<std::pair<QObject*, const char*>> translated_widgets = {
      {primary_color_button_, "FG"},
      {secondary_color_button_, "BG"},
      {move_auto_select_check_, "Auto-Select"},
      {move_show_transform_controls_check_, "Show Transform Controls"},
      {clone_aligned_check_, "Aligned"},
      {gradient_reverse_check_, "Reverse"},
      {gradient_edit_stops_button_, "Edit Stops..."},
      {wand_contiguous_check_, "Contiguous"},
      {wand_sample_all_layers_check_, "Sample All Layers"},
      {quick_select_sample_all_layers_check_, "Sample All Layers"},
      {quick_select_enhance_edge_check_, "Enhance Edge"},
      {ctx.fill_shapes, "Fill"},
      {text_bold_button_, "B"},
      {text_italic_button_, "I"},
      {text_color_button_, "T"},
      {text_align_left_button_, "L"},
      {text_align_center_button_, "C"},
      {text_align_right_button_, "R"},
  };
  for (const auto& [widget, source] : translated_widgets) {
    bind_widget_text(widget, source);
  }
}

void MainWindow::sync_tool_option_controls_from_canvas() {
  if (canvas_ == nullptr) {
    return;
  }
  // Re-reads the options-bar controls that create_actions initialized from the
  // defaults-donor canvas. Runs after the deferred startup load_tool_settings()
  // so the bar shows the stored settings the first document's canvas now holds.
  const auto set_spin_value = [this](const QString& name, int value) {
    if (auto* spin = findChild<QSpinBox*>(name); spin != nullptr) {
      const QSignalBlocker blocker(spin);
      spin->setValue(value);
    }
  };
  const auto set_slider_value = [this](const QString& name, int value) {
    if (auto* slider = findChild<QSlider*>(name); slider != nullptr) {
      const QSignalBlocker blocker(slider);
      slider->setValue(value);
    }
  };
  const auto set_checked = [](QCheckBox* check, bool value) {
    if (check != nullptr) {
      const QSignalBlocker blocker(check);
      check->setChecked(value);
    }
  };
  set_checked(move_auto_select_check_, canvas_->auto_select_layer());
  set_checked(move_show_transform_controls_check_, canvas_->show_transform_controls());
  set_checked(clone_aligned_check_, canvas_->clone_aligned());
  set_checked(wand_contiguous_check_, canvas_->wand_contiguous());
  set_checked(wand_sample_all_layers_check_, canvas_->wand_sample_all_layers());
  set_checked(quick_select_sample_all_layers_check_, canvas_->quick_select_sample_all_layers());
  set_checked(quick_select_enhance_edge_check_, canvas_->quick_select_enhance_edge());
  set_spin_value(QStringLiteral("wandToleranceSpin"), canvas_->wand_tolerance());
  set_spin_value(QStringLiteral("quickSelectSizeSpin"), canvas_->quick_select_size());
  set_slider_value(QStringLiteral("quickSelectSizeSlider"), canvas_->quick_select_size());
  set_spin_value(QStringLiteral("magneticLassoWidthSpin"), canvas_->magnetic_lasso_width());
  set_spin_value(QStringLiteral("magneticLassoContrastSpin"), canvas_->magnetic_lasso_edge_contrast());
  set_spin_value(QStringLiteral("magneticLassoFrequencySpin"), canvas_->magnetic_lasso_frequency());
  set_spin_value(QStringLiteral("shapeCornerRadiusSpin"), canvas_->shape_corner_radius());
  set_spin_value(QStringLiteral("fillOpacitySpin"), canvas_->fill_opacity());
  set_slider_value(QStringLiteral("fillOpacitySlider"), canvas_->fill_opacity());
  set_spin_value(QStringLiteral("fillSoftnessSpin"), canvas_->fill_softness());
  set_slider_value(QStringLiteral("fillSoftnessSlider"), canvas_->fill_softness());
  refresh_gradient_controls_from_canvas();
  sync_brush_controls_from_canvas();
}

void MainWindow::register_retranslation(std::function<void()> callback) {
  if (!callback) {
    return;
  }
  callback();
  retranslation_callbacks_.push_back(std::move(callback));
}

void MainWindow::retranslate_bound_children() {
  apply_bound_translation(this);
  const auto children = findChildren<QObject*>();
  for (auto* child : children) {
    apply_bound_translation(child);
  }
}

void MainWindow::retranslate_blend_combo() {
  if (blend_combo_ == nullptr) {
    return;
  }
  QSignalBlocker blocker(blend_combo_);
  for (int index = 0; index < blend_combo_->count(); ++index) {
    blend_combo_->setItemText(index, blend_mode_name(static_cast<BlendMode>(blend_combo_->itemData(index).toInt())));
  }
}

void MainWindow::retranslate_brush_preset_combo() {
  if (brush_preset_combo_ == nullptr) {
    return;
  }
  QSignalBlocker blocker(brush_preset_combo_);
  for (int index = 0; index < brush_preset_combo_->count(); ++index) {
    if (const auto* preset = find_brush_preset(brush_preset_combo_->itemData(index).toString()); preset != nullptr) {
      brush_preset_combo_->setItemText(index, brush_preset_display_name(*preset));
    }
  }
}

void MainWindow::refresh_language_actions() {
  const auto current = LocalizationManager::instance().current_language();
  if (language_english_action_ != nullptr) {
    language_english_action_->setChecked(current == QStringLiteral("en"));
  }
  if (language_japanese_action_ != nullptr) {
    language_japanese_action_->setChecked(current == QStringLiteral("ja"));
  }
}

void MainWindow::retranslate_ui() {
  retranslate_bound_children();
  if (menuBar() != nullptr) {
    for (auto* action : menuBar()->actions()) {
      apply_bound_translation(action);
    }
  }
  for (const auto& callback : retranslation_callbacks_) {
    callback();
  }
  refresh_language_actions();
  retranslate_blend_combo();
  retranslate_brush_preset_combo();
  if (text_size_spin_ != nullptr) {
    text_size_spin_->setSuffix(tr(" pt"));
  }
  const auto actions = findChildren<QAction*>();
  for (auto* action : actions) {
    refresh_action_tooltip(action);
  }
  rebuild_recent_files_menu();
  rebuild_recent_folders_menu();
  refresh_layer_list();
  refresh_layer_controls();
  refresh_channel_panel();
  refresh_document_info();
  refresh_color_buttons();
  refresh_text_color_button();
  update_undo_redo_actions();
  update_document_action_state();
  if (statusBar() != nullptr) {
    statusBar()->showMessage(tr("Ready"));
  }
}

}  // namespace patchy::ui
