#include <QPoint>
#include <QString>
#include "ui/canvas_widget.hpp"
#include "core/adjustment_layer.hpp"
#include "core/document_path.hpp"
#include "core/vector_shape.hpp"
#include "core/layer_metadata.hpp"
#include "core/smart_object.hpp"
#include "core/text_warp.hpp"
#include "core/layer_tree.hpp"
#include "ui/pattern_library.hpp"
#include "ui/pattern_manager_dialog.hpp"
#include "ui/style_browser.hpp"
#include "ui/style_library.hpp"
#include "psd/psd_text_runs.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/brush_tip_picker.hpp"
#include "ui/brush_dynamics_popup.hpp"
#include "ui/brush_stroke_preview.hpp"
#include "ui/brush_tool_window.hpp"
#include "ui/color_panel.hpp"
#include "ui/gradient_stops_editor.hpp"
#include "ui/gradient_library.hpp"
#include "formats/acv_curves_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/tga_document_io.hpp"
#include "ui/image_document_io.hpp"
#include "ui/qt_paths.hpp"
#include "ui/image_trace_dialog.hpp"
#include "ui/main_window.hpp"
#include "ui/app_settings.hpp"
#include "ui/visual_filter_gallery_dialog.hpp"
#include "ui/zoomable_image_preview.hpp"
#include "filters/builtin_filters.hpp"
#include "filters/gmic/gmic_engine.hpp"
#include "ui/gmic_filter_library_dialog.hpp"
#include "psd/psd_document_io.hpp"
#include "test_fonts.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFontDatabase>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QTemporaryDir>
#include <QMetaObject>
#include <QLinearGradient>
#include <QPainter>
#include <QPushButton>
#include <QScopeGuard>
#include <QSpinBox>
#include <QSlider>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QWidget>
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
#include <initializer_list>
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
#include "readme_screenshot_test_support.hpp"

namespace {

using namespace patchy::test::ui;

void visual_contact_sheet_contains_new_feature_artifacts() {
  ensure_artifact_dir();
  std::vector<std::string> artifacts = {
      "ui_main_window.png",
      "ui_window_force_refresh.png",
      "ui_color_picker.png",
      "ui_color_picker_gradient.png",
      "ui_color_picker_result.png",
      "ui_canvas_wheel_navigation.png",
      "ui_shape_flyout_zoom_tool.png",
      "ui_tool_palette_icons_sheet.png",
      "ui_tool_palette.png",
      "ui_filled_shape_preview_cleanup.png",
      "ui_tool_options_move.png",
      "ui_tool_options_text.png",
      "ui_info_panel_layers_docks.png",
      "ui_layer_style_dialog.png",
      "ui_layer_style_result.png",
      "ui_blend_if_range_editor.png",
      "ui_gradient_stops_editor_two_track.png",
      "ui_gradient_manager_defaults.png",
      "ui_new_document_dialog.png",
      "ui_new_document_result.png",
      "ui_image_size_dialog.png",
      "ui_image_size_result.png",
      "ui_canvas_size_dialog.png",
      "ui_canvas_size_result.png",
      "ui_multiple_documents.png",
      "ui_first_tab_draw_after_second_tab.png",
      "ui_tab_session_layers.png",
      "ui_new_layer_result.png",
      "ui_multiselect_merge_down.png",
      "ui_multiselect_duplicate_delete.png",
      "ui_duplicate_text_folder_tree.png",
      "ui_copy_paste_layer_panel_tree.png",
      "ui_layer_visibility_drag_reorder.png",
      "ui_layer_folder_drag_drop.png",
      "ui_layer_panel_mixed_folder_visual_cleanup.png",
      "ui_layer_folder_expand_contract.png",
      "ui_layer_folder_saved_state.png",
      "ui_auto_select_reveals_collapsed_folder.png",
      "ui_move_preview_layer_order.png",
      "ui_move_opaque_bounds.png",
      "ui_move_hover_opaque_bounds.png",
      "ui_move_active_tab_only.png",
      "ui_move_selected_folder_tree.png",
      "ui_move_selected_masked_folder_tree.png",
      "ui_move_preview_mid_drag_partial_repaint.png",
      "ui_dirty_region_move_preview_force_refresh.png",
      "ui_selection_modifiers.png",
      "ui_selection_toolbar_modes.png",
      "ui_ctrl_a_select_all.png",
      "ui_alt_backspace_fill_selection.png",
      "ui_feathered_marquee_fill.png",
      "ui_marquee_fixed_size_ratio.png",
      "ui_elliptical_marquee.png",
      "ui_marquee_space_drag_reposition.png",
      "ui_rulers_grid_guides.png",
      "ui_guides_editing.png",
      "ui_snapped_marquee_guides.png",
      "ui_snap_shape_text_move.png",
      "ui_grid_guides_preferences_dialogs.png",
      "ui_complex_selection_outline.png",
      "ui_selection_edges_visible_no_tint.png",
      "ui_selection_edges_hidden.png",
      "ui_select_inverse.png",
      "ui_select_reselect.png",
      "ui_selection_expand_contract_transparency.png",
      "ui_selection_border.png",
      "ui_ctrl_click_layer_transparency.png",
      "ui_select_grow.png",
      "ui_select_similar.png",
      "ui_complex_stroke_selection.png",
      "ui_extended_blend_modes.png",
      "ui_layer_lock_transparency.png",
      "ui_layer_full_lock_controls.png",
      "ui_folder_lock_inheritance.png",
      "ui_move_auto_select_locked_layer.png",
      "ui_merge_down_position_locked_background.png",
      "ui_keyboard_nudge_layer.png",
      "ui_lasso_selection.png",
      "ui_copy_paste_transform.png",
      "ui_transform_opaque_bounds.png",
      "ui_move_show_transform_controls.png",
      "ui_layer_via_copy_cut.png",
      "ui_layer_mask_from_selection.png",
      "ui_layer_mask_target_editing.png",
      "ui_layer_mask_add_without_selection.png",
      "ui_layer_mask_link_button_click.png",
      "ui_layer_mask_rubylith_overlay.png",
      "ui_layer_mask_grayscale_view.png",
      "ui_layer_thumbnail_refresh.png",
      "ui_cut_selection.png",
      "ui_brush_expands_pasted_layer.png",
      "ui_brush_opacity_per_stroke.png",
      "ui_soft_brush_single_click.png",
      "ui_soft_brush_shift_click_anchor.png",
      "ui_soft_brush_event_density.png",
      "ui_layer_mask_brush_opacity_per_stroke.png",
      "ui_airbrush_stationary_build_up.png",
      "ui_airbrush_event_density.png",
      "ui_airbrush_smoothed_jitter.png",
      "ui_soft_opaque_brush_l_corner.png",
      "ui_soft_artifact_acute_zigzag.png",
      "ui_soft_artifact_crossing_star.png",
      "ui_soft_artifact_large_sparse_star.png",
      "ui_soft_artifact_large_sparse_star_default_layers.png",
      "ui_clone_tool_stamp.png",
      "ui_smudge_tool.png",
      "ui_hidden_layer_copy_ignored.png",
      "ui_copy_selected_layers.png",
      "ui_background_eraser_background_color.png",
      "ui_inline_text_editor.png",
      "ui_text_tool_layer.png",
      "ui_transformed_text_reedit.png",
      "format_alpha.png",
      "ui_image_adjustments_invert_desaturate.png",
      "ui_image_adjustments_auto_contrast.png",
      "ui_image_adjustments_auto_tone.png",
      "ui_image_adjustments_auto_color.png",
      "ui_filter_gallery_photo_looks.png",
      "ui_filter_gallery_all_filters.png",
      "ui_filter_gallery_stack.png",
      "ui_all_builtin_filters_stroke_contact_sheet.png",
      "ui_image_adjustment_selection_scope.png",
      "ui_hue_saturation_dialog.png",
      "ui_hue_saturation_selection.png",
      "ui_hue_saturation_adjustment_layer.png",
      "ui_levels_dialog.png",
      "ui_levels_selection.png",
      "ui_curves_dialog.png",
      "ui_curves_editor.png",
      "ui_curves_presets.png",
      "ui_curves_selection.png",
      "ui_color_balance_dialog.png",
      "ui_color_balance_selection.png",
      "ui_gradient_tool.png",
      "ui_radial_gradient_transparency.png",
      "ui_magic_wand_selection.png",
      "ui_magic_wand_options.png",
      "ui_magic_wand_complex_selection.png",
      "ui_legacy_plugin_greyscale.png",
      "ui_transparency_checkerboard.png",
      "ui_transparent_copy_paste.png",
      "ui_crop_to_selection.png",
      "ui_rotate_canvas.png",
      "ui_stroke_selection.png",
      "ui_merge_visible_and_filter.png",
      "tool_gradient.bmp",
      "tool_soft_brush.bmp",
      "tool_brush_expand_layer.bmp",
      "document_crop.bmp",
      "document_canvas_resize.bmp",
      "document_image_resize.bmp",
      "document_rotate_clockwise.bmp",
      "document_rotate_counterclockwise.bmp",
      "tool_stroke_selection.bmp",
      "tool_lock_transparency.bmp",
      "layer_merge_visible.bmp",
      "filter_brightness_contrast.bmp",
      "filter_grayscale.bmp",
      "filter_desaturate.bmp",
      "filter_auto_tone.bmp",
      "filter_auto_contrast.bmp",
      "filter_auto_color.bmp",
      "filter_sepia.bmp",
      "filter_threshold.bmp",
      "filter_posterize.bmp",
      "filter_box_blur.bmp",
      "filter_sharpen.bmp",
      "filter_unsharp_mask.bmp",
      "filter_gaussian_blur.bmp",
      "filter_motion_blur.bmp",
      "filter_radial_blur.bmp",
      "filter_edge_detect.bmp",
      "filter_emboss.bmp",
      "filter_glowing_edges.bmp",
      "filter_twirl.bmp",
      "filter_wave.bmp",
      "filter_pinch_bloat.bmp",
      "filter_clouds.bmp",
      "filter_pixelate.bmp",
      "filter_color_halftone.bmp",
      "filter_film_grain.bmp",
      "filter_vignette.bmp",
      "filter_soft_glow.bmp",
      "filter_punchy_color.bmp",
      "filter_noir.bmp",
      "filter_cinematic_matte.bmp",
      "filter_vintage_fade.bmp",
      "ui_filter_edge_detect.png",
      "ui_marching_ants_zoom_100.png",
  };
#ifndef Q_OS_WIN
  // Produced by the Windows-only bundled legacy 8BF shim tests, which [SKIP] on this
  // platform (see ui_bundled_legacy_plugin_action_applies_filter).
  std::erase_if(artifacts, [](const std::string& name) {
    return name == "ui_legacy_plugin_greyscale.png" || name == "ui_transparency_checkerboard.png" ||
           name == "ui_transparent_copy_paste.png";
  });
#endif

  constexpr int kColumns = 4;
  constexpr int kCellWidth = 280;
  constexpr int kCellHeight = 220;
  constexpr int kPadding = 12;
  const auto rows = static_cast<int>((artifacts.size() + kColumns - 1) / kColumns);
  QImage sheet(kColumns * kCellWidth, rows * kCellHeight, QImage::Format_RGB32);
  sheet.fill(QColor(30, 32, 36));

  QPainter painter(&sheet);
  painter.setRenderHint(QPainter::SmoothPixmapTransform);
  painter.setFont(visual_test_font());
  painter.setPen(QColor(225, 230, 238));
  for (std::size_t index = 0; index < artifacts.size(); ++index) {
    const auto path = std::filesystem::path("test-artifacts") / artifacts[index];
    CHECK(std::filesystem::exists(path));
    QImage image(QString::fromStdString(path.string()));
    CHECK(!image.isNull());

    const auto column = static_cast<int>(index % kColumns);
    const auto row = static_cast<int>(index / kColumns);
    const QRect cell(column * kCellWidth, row * kCellHeight, kCellWidth, kCellHeight);
    painter.fillRect(cell.adjusted(4, 4, -4, -4), QColor(42, 45, 51));
    painter.drawText(cell.adjusted(kPadding, 8, -kPadding, -kPadding), Qt::AlignTop | Qt::AlignLeft,
                     QString::fromStdString(artifacts[index]));

    const QRect image_rect = cell.adjusted(kPadding, 34, -kPadding, -kPadding);
    const auto scaled = image.scaled(image_rect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QPoint image_pos(image_rect.x() + (image_rect.width() - scaled.width()) / 2,
                           image_rect.y() + (image_rect.height() - scaled.height()) / 2);
    painter.drawImage(image_pos, scaled);
  }
  painter.end();

  CHECK(sheet.save(QStringLiteral("test-artifacts/visual_feature_contact_sheet.png")));
}

// ===========================================================================
// README screenshots (shot_readme_*), part 1: the pre-scripting scenes.
// ===========================================================================
// The group-wide story lives in the aggregator (readme_screenshot_tests.cpp);
// helpers shared with the other part live in readme_screenshot_test_support.

void paint_readme_polyline(patchy::ui::CanvasWidget& canvas, const std::vector<QPointF>& document_points) {
  CHECK(document_points.size() >= 2);
  const auto widget_point = [&canvas](QPointF point) {
    return canvas.widget_position_for_document_point(point.toPoint());
  };
  send_mouse(canvas, QEvent::MouseButtonPress, widget_point(document_points.front()), Qt::LeftButton, Qt::LeftButton);
  for (std::size_t i = 1; i < document_points.size(); ++i) {
    send_mouse(canvas, QEvent::MouseMove, widget_point(document_points[i]), Qt::NoButton, Qt::LeftButton);
  }
  send_mouse(canvas, QEvent::MouseButtonRelease, widget_point(document_points.back()), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
}

std::vector<QPointF> readme_wave_points(double x_start, double x_end, double y_center, double amplitude,
                                        double cycles = 1.5, int steps = 30) {
  std::vector<QPointF> points;
  points.reserve(static_cast<std::size_t>(steps) + 1);
  for (int i = 0; i <= steps; ++i) {
    const auto t = static_cast<double>(i) / steps;
    points.emplace_back(x_start + t * (x_end - x_start),
                        y_center - std::sin(t * cycles * 2.0 * 3.14159265358979323846) * amplitude);
  }
  return points;
}

QString readme_tip_id_by_name(patchy::ui::BrushTipLibrary& library, const QString& name) {
  for (const auto& entry : library.entries()) {
    if (entry.name == name) {
      return entry.id;
    }
  }
  CHECK(false);
  return {};
}

// Seeds the 36 built-in default tips into the (test-scoped) library so the
// picker and strokes show the real first-run brush set.
void seed_default_brush_tips_for_readme_shot() {
  clear_brush_tip_test_state();
  auto settings = patchy::ui::app_settings();
  settings.setValue(QStringLiteral("brushes/defaultTipsVersion"), 0);
  settings.sync();
}

// Points an open color picker at one of its three modes (0 Square, 1 Wheel,
// 2 Sliders) and optionally a palette-dropdown choice, so each README shot
// showing a picker presents a different face. Both writes persist app-wide
// (colorPanel/lastTab, palettes/lastPaletteChoice); callers hold
// SettingsValueRestorers for them.
void configure_readme_color_picker(QDialog& dialog, int tab_index, const char* palette_choice) {
  auto* picker = dialog.findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
  CHECK(picker != nullptr);
  auto* tabs = picker->findChild<QTabWidget*>(QStringLiteral("patchyColorPickerTabs"));
  CHECK(tabs != nullptr);
  CHECK(tabs->count() == 3);
  tabs->setCurrentIndex(tab_index);
  if (palette_choice != nullptr) {
    auto* combo = picker->findChild<QComboBox*>(QStringLiteral("patchyColorPaletteCombo"));
    CHECK(combo != nullptr);
    const auto row = combo->findData(QString::fromLatin1(palette_choice));
    CHECK(row >= 0);
    combo->setCurrentIndex(row);
  }
  QApplication::processEvents();
}

// Opens the persistent Foreground Color picker via the toolbox swatch, applies
// the requested mode, and parks it at a window-relative offset (the left side
// in every scene, clear of the featured dialog on the right).
QDialog* open_foreground_picker_for_readme_shot(patchy::ui::MainWindow& window, int tab_index,
                                                const char* palette_choice, QPoint offset) {
  auto* foreground = window.findChild<QPushButton*>(QStringLiteral("foregroundColorButton"));
  CHECK(foreground != nullptr);
  foreground->click();
  QApplication::processEvents();
  auto* dialog = find_top_level_dialog(QStringLiteral("patchyColorDialog"));
  CHECK(dialog != nullptr);
  CHECK(dialog->isVisible());
  configure_readme_color_picker(*dialog, tab_index, palette_choice);
  dialog->move(window.geometry().topLeft() + offset);
  QApplication::processEvents();
  return dialog;
}

// Standalone Levels/color-picker regression scene, retired from the gallery:
// the Okinawa cycling photo with rulers and grid on, the
// Levels dialog's live histogram floating over the canvas, and the color
// picker on the left in Wheel mode with its swatch grid on DOS / VGA 256. The
// palette dropdown stays closed: it would cover the swatches, and the
// palette_mode shot already shows a preset dropdown in action.
void shot_readme_levels() {
  const auto path = patchy::test::local_psd_fixture_path("akiko_cycling_okinawa.jpg");
  patchy::test::skip_unless_exists(path);
  QImage photo(QString::fromStdString(path.string()));
  CHECK(!photo.isNull());
  // The picker tab and palette-dropdown choice persist app-wide; scope them to
  // this scene so the other shots (and later suite runs) pick their own.
  SettingsValueRestorer picker_tab_restorer(QStringLiteral("colorPanel/lastTab"));
  SettingsValueRestorer palette_choice_restorer(QStringLiteral("palettes/lastPaletteChoice"));
  // The shot unchecks Transform Controls, which the checkbox writes to the settings.
  SettingsValueRestorer transform_controls_restorer(QStringLiteral("tools/showTransformControls"));
  {
    // A coarse line grid reads well over a photo; the default fine mesh at
    // fit zoom washes the image out.
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("view/gridSpacing32"), 128 * 32);
    settings.setValue(QStringLiteral("view/gridSubdivisions"), 1);
    settings.sync();
  }
  patchy::ui::MainWindow window;
  show_readme_shot_window(window);
  window.add_document_session(patchy::ui::document_from_qimage(photo, "akiko_cycling_okinawa"),
                              QStringLiteral("akiko_cycling_okinawa.jpg"));
  QApplication::processEvents();
  close_untitled_start_tab(window);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  auto* transform_controls_check =
      window.findChild<QAbstractButton*>(QStringLiteral("moveShowTransformControlsCheck"));
  CHECK(transform_controls_check != nullptr);
  transform_controls_check->setChecked(false);
  auto* rulers_action = require_action(window, "viewToggleRulersAction");
  if (!rulers_action->isChecked()) {
    rulers_action->trigger();
  }
  auto* grid_action = require_action(window, "viewToggleGridAction");
  if (!grid_action->isChecked()) {
    grid_action->trigger();
  }
  require_action(window, "viewFitOnScreenAction")->trigger();
  QApplication::processEvents();

  // Foreground color picker on the left (Wheel mode), its swatch grid already
  // on DOS / VGA 256 so the retro swatches stay fully visible.
  const QPoint picker_offset(60, 270);
  auto* picker_dialog = open_foreground_picker_for_readme_shot(window, 1, "vga256", picker_offset);
  auto* picker =
      picker_dialog->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
  CHECK(picker != nullptr);
  picker->setCurrentColor(QColor(0xE0, 0x50, 0x7A));  // the bike's bar-tape pink
  QApplication::processEvents();

  bool captured = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyLevelsDialog"));
    CHECK(dialog != nullptr);
    auto* preview = dialog->findChild<QCheckBox*>(QStringLiteral("levelsPreviewCheck"));
    CHECK(preview != nullptr);
    CHECK(preview->isChecked());
    process_events_for(400);  // let the histogram and preview settle
    const QPoint dialog_offset(790, 250);
    dialog->move(window.geometry().topLeft() + dialog_offset);
    QApplication::processEvents();
    auto* combo = picker_dialog->findChild<QComboBox*>(QStringLiteral("patchyColorPaletteCombo"));
    CHECK(combo != nullptr);
    CHECK(combo->currentData().toString() == QStringLiteral("vga256"));
    reset_readme_status_bar(window);
    auto base = window.grab().toImage();
    draw_readme_overlay(base, picker_dialog->grab().toImage(), picker_offset);
    draw_readme_overlay(base, dialog->grab().toImage(), dialog_offset);
    save_readme_shot("shot_readme_levels", base);
    captured = true;
    dialog->reject();
  });
  require_action(window, "imageAdjustLevelsAction")->trigger();
  QApplication::processEvents();
  CHECK(captured);
  picker_dialog->close();
  QApplication::processEvents();

  // Rulers/grid persist to view settings on window teardown; toggle them back
  // off so the scenes that run after this one keep their clean canvases.
  rulers_action->trigger();
  grid_action->trigger();
  QApplication::processEvents();
}

// Layer Style dialog over the same PSD, opened on a button layer that carries
// a drop shadow + stroke + gradient overlay, showing the Gradient Overlay page
// with the two-track (color + opacity) stop editor — plus the "Choose Gradient
// Stop Color" picker (Sliders mode) that clicking the selected 0% stop opens,
// floating on the left.
void shot_readme_layer_styles() {
  const auto path = patchy::test::local_psd_fixture_path("ipad_main_v04.psd");
  patchy::test::skip_unless_exists(path);
  SettingsValueRestorer picker_tab_restorer(QStringLiteral("colorPanel/lastTab"));
  SettingsValueRestorer palette_choice_restorer(QStringLiteral("palettes/lastPaletteChoice"));
  patchy::ui::MainWindow window;
  show_readme_shot_window(window);
  window.add_document_session(patchy::psd::DocumentIo::read_file(path), QStringLiteral("ipad_main_v04.psd"));
  QApplication::processEvents();
  close_untitled_start_tab(window);

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  expand_layer_folder_row(*layer_list, QStringLiteral("Buttons"));
  auto* new_item = require_layer_item(*layer_list, QStringLiteral("New"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(new_item);
  new_item->setSelected(true);
  layer_list->scrollToItem(new_item, QAbstractItemView::PositionAtCenter);
  QApplication::processEvents();

  bool captured = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog"));
    CHECK(dialog != nullptr);
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
    CHECK(categories != nullptr);
    QListWidgetItem* gradient_item = nullptr;
    for (int row = 0; row < categories->count(); ++row) {
      if (categories->item(row)->text() == QStringLiteral("Gradient Overlay")) {
        gradient_item = categories->item(row);
      }
    }
    CHECK(gradient_item != nullptr);
    categories->setCurrentItem(gradient_item);
    QApplication::processEvents();

    // Right of center so the stop picker fits on the left without covering
    // the dialog (or the layer list's fx badges).
    const QPoint dialog_offset(560, 210);
    dialog->move(window.geometry().topLeft() + dialog_offset);
    QApplication::processEvents();

    // Clicking the already-selected 0% color stop opens the stop's color
    // picker; it runs a nested event loop inside the release event, so the
    // capture is scheduled first and fires while the picker is up.
    auto* editor = dialog->findChild<patchy::ui::GradientStopsEditorWidget*>(
        QStringLiteral("layerStyleGradientStopsEditor"));
    auto* location = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleGradientStopLocationSpin"));
    CHECK(editor != nullptr);
    CHECK(location != nullptr);
    CHECK(editor->isVisible());
    CHECK(location->value() == 0);  // the 0% stop opens the page selected
    // The color tags live below the bar; the 0% tag sits at the left gutter.
    const QPoint stop_tag(10, editor->height() - 18);
    send_mouse(*editor, QEvent::MouseButtonPress, stop_tag, Qt::LeftButton, Qt::LeftButton);
    QTimer::singleShot(0, [&] {
      auto* picker_dialog = find_top_level_dialog(QStringLiteral("patchyColorDialog"));
      CHECK(picker_dialog != nullptr);
      CHECK(picker_dialog->isVisible());
      configure_readme_color_picker(*picker_dialog, 2, "basic");
      const QPoint picker_offset(12, 290);
      picker_dialog->move(window.geometry().topLeft() + picker_offset);
      process_events_for(120);
      reset_readme_status_bar(window);
      auto base = window.grab().toImage();
      draw_readme_overlay(base, dialog->grab().toImage(), dialog_offset);
      draw_readme_overlay(base, picker_dialog->grab().toImage(), picker_offset);
      save_readme_shot("shot_readme_layer_styles", base);
      captured = true;
      picker_dialog->reject();  // restores the stop's original color
    });
    send_mouse(*editor, QEvent::MouseButtonRelease, stop_tag, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    CHECK(captured);
    dialog->reject();
  });
  require_action(window, "layerBlendingOptionsAction")->trigger();
  QApplication::processEvents();
  CHECK(captured);
}

// Bitmap brush tips and dynamics: strokes from plain and dynamics tips down the left of the
// canvas, with the floating Brush Tips and Brush Dynamics and Effects windows parked on the
// right (GitHub issue 506), as someone painting with both open would keep them.
void shot_readme_brush_tips() {
  seed_default_brush_tips_for_readme_shot();
  const auto clear_windows = [] {
    for (const auto* key : {"brushes/settingsWindowOpen", "brushes/settingsWindowSize", "brushes/tipsWindowOpen",
                            "brushes/tipsWindowSize"}) {
      patchy::ui::app_settings().remove(QLatin1String(key));
    }
  };
  clear_windows();
  const auto cleanup = qScopeGuard(clear_windows);
  patchy::ui::MainWindow window;
  show_readme_shot_window(window);
  patchy::Document document(1180, 780, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(1180, 780, patchy::PixelFormat::rgb8(), QColor(255, 255, 255)));
  window.add_document_session(std::move(document), QStringLiteral("Brush Tips"));
  QApplication::processEvents();
  close_untitled_start_tab(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  canvas->set_brush_dynamics_test_seed(0xC0FFEEu);
  canvas->set_brush_softness(0);

  // The windows cover the canvas right of about x 470, so most strokes stay left of it; the
  // bottom two run the full width, below the windows.
  auto& library = window.brush_tip_library();
  struct StrokeSpec {
    const char* tip;
    QColor color;
    int size;
    double x_end;
    double y_center;
    double amplitude;
    double cycles;
  };
  const StrokeSpec strokes[] = {
      {"Chalk", QColor(0x2f, 0x6f, 0xb2), 52, 440.0, 70.0, 26.0, 1.0},
      {"Chain", QColor(0x63, 0x6d, 0x7a), 40, 440.0, 160.0, 24.0, 1.0},
      {"Snowflake", QColor(0x4a, 0xa3, 0xd8), 52, 440.0, 250.0, 26.0, 1.0},
      {"Leaf", QColor(0xc9, 0x6a, 0x1e), 48, 440.0, 340.0, 26.0, 1.0},
      {"Heart", QColor(0xe0, 0x50, 0x7a), 44, 440.0, 430.0, 26.0, 1.0},
      {"Spatter", QColor(0x8e, 0x44, 0xad), 50, 440.0, 520.0, 24.0, 1.0},
      {"Stitches", QColor(0xc0, 0x39, 0x2b), 40, 1090.0, 625.0, 22.0, 2.5},
      {"Grass", QColor(0x3f, 0x8f, 0x3f), 58, 1090.0, 722.0, 6.0, 0.5},
  };
  for (const auto& stroke : strokes) {
    window.set_active_brush_tip(readme_tip_id_by_name(library, QString::fromLatin1(stroke.tip)), false);
    canvas->set_brush_size(stroke.size);
    canvas->set_primary_color(stroke.color);
    paint_readme_polyline(*canvas, readme_wave_points(70.0, stroke.x_end, stroke.y_center, stroke.amplitude,
                                                      stroke.cycles));
  }
  // Leaf carries its own dynamics, so the window's section dots, pages and preview show them.
  window.set_active_brush_tip(readme_tip_id_by_name(library, QStringLiteral("Leaf")), false);
  canvas->set_brush_size(48);
  canvas->set_primary_color(QColor(0xc9, 0x6a, 0x1e));
  QApplication::processEvents();

  require_action(window, "windowBrushTipsAction")->trigger();
  require_action(window, "windowBrushSettingsAction")->trigger();
  auto* picker = window.findChild<patchy::ui::BrushTipPicker*>(QStringLiteral("brushTipPicker"));
  auto* button = window.findChild<patchy::ui::BrushDynamicsButton*>(QStringLiteral("brushDynamicsButton"));
  CHECK(picker != nullptr && button != nullptr);
  CHECK(process_events_until([&] {
    return picker->tool_window() != nullptr && picker->tool_window()->isVisible() &&
           button->tool_window() != nullptr && button->tool_window()->isVisible();
  }));
  if (picker->tool_window() == nullptr || button->tool_window() == nullptr) {
    return;
  }
  auto* tips_window = picker->tool_window();
  auto* dynamics_window = button->tool_window();
  tips_window->resize(466, 690);  // six columns, every tip in view
  button->window_panel()->select_section(patchy::ui::BrushDynamicsPanel::Section::ShapeDynamics);
  QApplication::processEvents();
  button->window_panel()->preview()->rebuild_now();
  QApplication::processEvents();

  reset_readme_status_bar(window);
  auto base = window.grab().toImage();
  const auto tips_image = tips_window->grab().toImage();
  const auto dynamics_image = dynamics_window->grab().toImage();
  const QPoint tips_at(base.width() - tips_image.width() - 12, 90);
  const QPoint dynamics_at(tips_at.x() - dynamics_image.width() - 14, 90);
  draw_readme_overlay(base, dynamics_image, dynamics_at);
  draw_readme_overlay(base, tips_image, tips_at);
  save_readme_shot("shot_readme_brush_tips", base);
  tips_window->close();
  dynamics_window->close();
  QApplication::processEvents();
  clear_brush_tip_test_state();
}

// Brush dynamics: scatter/jitter strokes from the stamp tips, with the
// Dynamics popup open showing the active tip's Photoshop-style controls.
void shot_readme_brush_dynamics() {
  seed_default_brush_tips_for_readme_shot();
  patchy::ui::MainWindow window;
  show_readme_shot_window(window);
  patchy::Document document(1180, 780, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(1180, 780, patchy::PixelFormat::rgb8(), QColor(255, 255, 255)));
  window.add_document_session(std::move(document), QStringLiteral("Brush Dynamics"));
  QApplication::processEvents();
  close_untitled_start_tab(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  canvas->set_brush_dynamics_test_seed(0x5EEDu);
  canvas->set_brush_softness(0);

  auto& library = window.brush_tip_library();
  struct StrokeSpec {
    const char* tip;
    QColor color;
    int size;
    double y_center;
    double amplitude;
  };
  const StrokeSpec strokes[] = {
      {"Snowflake", QColor(0x4a, 0xa3, 0xd8), 54, 140.0, 40.0},
      {"Bubbles", QColor(0x2e, 0x86, 0xc1), 58, 300.0, 44.0},
      {"Heart", QColor(0xe0, 0x50, 0x7a), 50, 460.0, 42.0},
      {"Confetti", QColor(0x27, 0xae, 0x60), 46, 600.0, 36.0},
      {"Star", QColor(0xd4, 0xa0, 0x17), 56, 700.0, 26.0},
  };
  for (const auto& stroke : strokes) {
    window.set_active_brush_tip(readme_tip_id_by_name(library, QString::fromLatin1(stroke.tip)), false);
    canvas->set_brush_size(stroke.size);
    canvas->set_primary_color(stroke.color);
    paint_readme_polyline(*canvas, readme_wave_points(90.0, 1090.0, stroke.y_center, stroke.amplitude));
  }

  auto* button = window.findChild<QToolButton*>(QStringLiteral("brushDynamicsButton"));
  CHECK(button != nullptr);
  button->click();
  QApplication::processEvents();
  QWidget* popup = nullptr;
  for (auto* widget : QApplication::topLevelWidgets()) {
    if (widget->objectName() == QStringLiteral("brushDynamicsPopup") && widget->isVisible()) {
      popup = widget;
    }
  }
  CHECK(popup != nullptr);
  // Drop the popup under the options-bar Dynamics button; the offscreen
  // platform's small virtual screen otherwise clamps it over the menu bar.
  const QPoint popup_offset(700, 96);
  popup->move(window.geometry().topLeft() + popup_offset);
  QApplication::processEvents();

  reset_readme_status_bar(window);
  auto base = window.grab().toImage();
  draw_readme_overlay(base, popup->grab().toImage(), popup_offset);
  save_readme_shot("shot_readme_brush_dynamics", base);
  popup->close();
  QApplication::processEvents();
  clear_brush_tip_test_state();
}

// Palettized (indexed color) mode: the Okinawa photo converted to the
// Commodore 64 palette (WYSIWYG canvas), the Palette panel expanded with the
// C64 swatches, the Convert to Indexed dialog re-opened with its palette
// dropdown showing the built-in retro presets, and the color picker on the
// left (Square mode) pinned to the document's Current palette.
void shot_readme_palette_mode() {
  const auto path = patchy::test::local_psd_fixture_path("akiko_cycling_okinawa.jpg");
  patchy::test::skip_unless_exists(path);
  QImage photo(QString::fromStdString(path.string()));
  CHECK(!photo.isNull());
  SettingsValueRestorer picker_tab_restorer(QStringLiteral("colorPanel/lastTab"));
  patchy::ui::MainWindow window;
  show_readme_shot_window(window);
  window.add_document_session(patchy::ui::document_from_qimage(photo, "akiko_cycling_okinawa"),
                              QStringLiteral("akiko_cycling_okinawa.jpg"));
  QApplication::processEvents();
  close_untitled_start_tab(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  auto* rulers_action = require_action(window, "viewToggleRulersAction");
  if (!rulers_action->isChecked()) {
    rulers_action->trigger();
  }
  require_action(window, "viewFitOnScreenAction")->trigger();
  QApplication::processEvents();

  // First pass: convert the photo to the built-in Commodore 64 palette through
  // the real Convert to Indexed dialog.
  bool converted = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("paletteConvertDialog"));
    CHECK(dialog != nullptr);
    auto* source = dialog->findChild<QComboBox*>(QStringLiteral("paletteConvertSourceCombo"));
    CHECK(source != nullptr);
    const auto c64_row = source->findText(QStringLiteral("Commodore 64"));
    CHECK(c64_row >= 0);
    source->setCurrentIndex(c64_row);
    auto* buttons = dialog->findChild<QDialogButtonBox*>();
    CHECK(buttons != nullptr);
    converted = true;
    buttons->button(QDialogButtonBox::Ok)->click();
  });
  require_action(window, "imageModeIndexedAction")->trigger();
  QApplication::processEvents();
  CHECK(converted);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.palette_editing().has_value());
  CHECK(document.palette_editing()->palette.colors.size() == 16);

  // The Palette dock ships collapsed; expand it so the C64 swatches show.
  auto* palette_toggle = window.findChild<QToolButton*>(QStringLiteral("paletteDockCollapseButton"));
  CHECK(palette_toggle != nullptr);
  if (!palette_toggle->isChecked()) {
    palette_toggle->click();
  }
  QApplication::processEvents();

  // Color picker on the left (Square mode): palette mode pins its dropdown to
  // the document's Current palette, so the swatch grid shows the 16 C64
  // colors. Click the light-blue swatch so a cell reads as selected and the
  // foreground color matches the constrained document.
  const QPoint picker_offset(50, 250);
  auto* picker_dialog = open_foreground_picker_for_readme_shot(window, 0, nullptr, picker_offset);
  {
    auto* combo = picker_dialog->findChild<QComboBox*>(QStringLiteral("patchyColorPaletteCombo"));
    CHECK(combo != nullptr);
    CHECK(combo->currentData().toString() == QStringLiteral("current"));
    auto* grid = picker_dialog->findChild<QWidget*>(QStringLiteral("patchyColorPaletteGrid"));
    CHECK(grid != nullptr);
    CHECK(grid->property("paletteColorCount").toInt() == 16);
    const auto cell = grid->property("paletteCellSize").toInt() + grid->property("paletteCellGap").toInt();
    const auto columns = grid->property("paletteColumns").toInt();
    CHECK(cell > 0);
    CHECK(columns > 0);
    const int light_blue_index = 14;  // C64 hardware palette order
    const QPoint swatch((light_blue_index % columns) * cell + cell / 2,
                        (light_blue_index / columns) * cell + cell / 2);
    send_mouse(*grid, QEvent::MouseButtonPress, swatch, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*grid, QEvent::MouseButtonRelease, swatch, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
  }

  // Second pass: re-open the dialog over the converted canvas and grab the
  // scene with the palette dropdown open on the preset list.
  bool captured = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("paletteConvertDialog"));
    CHECK(dialog != nullptr);
    const QPoint dialog_offset(880, 180);
    dialog->move(window.geometry().topLeft() + dialog_offset);
    // Give the portrait preview room so it still peeks out beside and below
    // the open dropdown, then nudge the dither combo there and back so the
    // debounced preview re-renders at the enlarged label size.
    dialog->resize(dialog->width(), 700);
    auto* dither = dialog->findChild<QComboBox*>(QStringLiteral("paletteConvertDitherCombo"));
    CHECK(dither != nullptr);
    dither->setCurrentIndex(1);
    dither->setCurrentIndex(0);
    process_events_for(450);  // let the debounced preview land
    auto* source = dialog->findChild<QComboBox*>(QStringLiteral("paletteConvertSourceCombo"));
    CHECK(source != nullptr);
    source->showPopup();
    QApplication::processEvents();
    auto* popup = source->view()->window();
    CHECK(popup != nullptr);
    CHECK(popup->isVisible());
    reset_readme_status_bar(window);
    auto base = window.grab().toImage();
    draw_readme_overlay(base, picker_dialog->grab().toImage(), picker_offset);
    draw_readme_overlay(base, dialog->grab().toImage(), dialog_offset);
    // The dropdown hangs off the combo's bottom edge, exactly where a click
    // would open it.
    const auto popup_offset = dialog_offset + source->mapTo(dialog, QPoint(0, source->height() + 1));
    draw_readme_overlay(base, popup->grab().toImage(), popup_offset);
    save_readme_shot("shot_readme_palette_mode", base);
    captured = true;
    source->hidePopup();
    dialog->reject();
  });
  require_action(window, "imageModeIndexedAction")->trigger();
  QApplication::processEvents();
  CHECK(captured);
  picker_dialog->close();
  QApplication::processEvents();

  // Rulers persist to view settings on window teardown; toggle them back off
  // so the scenes that run after this one keep their clean canvases.
  rulers_action->trigger();
  QApplication::processEvents();
}

// Trace Image to Shapes: a stylized sunset landscape traced with the 16 Colors
// flat-art preset, the preview marking the traced paths' own anchor points.
void shot_readme_image_trace() {
  const auto path = patchy::test::committed_format_fixture_path("readme", "stylized_sunset_cc0.png");
  QImage art(QString::fromStdString(path.string()));
  CHECK(!art.isNull());
  art = art.scaled(1240, 820, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
  art = art.copy((art.width() - 1240) / 2, (art.height() - 820) / 2, 1240, 820);
  const auto source = std::make_shared<const patchy::PixelBuffer>(patchy::ui::pixels_from_image_rgba(art));

  // Show anchors is a view preference the dialog reads once at construction;
  // seed it on so the first rendered preview already carries the marks.
  SettingsValueRestorer show_anchors_restorer(QStringLiteral("imageTrace/showAnchors"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("imageTrace/showAnchors"), true);
    settings.sync();
  }

  // Passing a built-in preset's exact options makes the combo name the preset
  // (select_matching_preset) and keeps the un-debounced initial trace the only
  // trace the scene has to wait for.
  const auto& presets = patchy::ui::image_trace_presets();
  const auto preset =
      std::find_if(presets.begin(), presets.end(), [](const patchy::ui::ImageTracePreset& entry) {
        return std::string_view(entry.english_name) == "16 Colors";
      });
  CHECK(preset != presets.end());

  patchy::ui::MainWindow theme_host;
  show_readme_shot_window(theme_host);
  bool captured = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("imageTraceDialog"));
    CHECK(dialog != nullptr);
    dialog->resize(1180, 760);
    auto* info = dialog->findChild<QLabel*>(QStringLiteral("imageTracePreviewInfo"));
    auto* spinner = dialog->findChild<QWidget*>(QStringLiteral("imageTraceBusySpinner"));
    auto* preset_combo = dialog->findChild<QComboBox*>(QStringLiteral("imageTracePresetCombo"));
    auto* anchors_check = dialog->findChild<QCheckBox*>(QStringLiteral("imageTraceShowAnchorsCheck"));
    auto* warning = dialog->findChild<QLabel*>(QStringLiteral("imageTraceSizeWarningLabel"));
    CHECK(info != nullptr && spinner != nullptr && preset_combo != nullptr && anchors_check != nullptr &&
          warning != nullptr);
    CHECK(anchors_check->isChecked());
    CHECK(preset_combo->currentText() == QStringLiteral("16 Colors"));
    CHECK(process_events_until(
        [&] { return !spinner->isVisible() && info->text().contains(QStringLiteral("shape layer")); }, 15000));
    CHECK(!warning->isVisible());
    dialog->repaint();
    process_events_for(100);
    save_readme_shot("shot_readme_image_trace", dialog->grab().toImage());
    captured = true;
    dialog->reject();
  });
  const auto result = patchy::ui::request_image_trace(&theme_host, source, preset->options);
  CHECK(captured);
  CHECK(!result.has_value());
}

// One rich-text run line per character so a single layer carries per-letter
// colors (the runs metadata format: start\tlength\tsize\tbold\titalic\t#rrggbb\tfont).
std::string readme_rainbow_runs(const std::vector<const char*>& colors, int size, const char* font) {
  std::string runs = "v1";
  for (std::size_t i = 0; i < colors.size(); ++i) {
    runs += "\n" + std::to_string(i) + "\t1\t" + std::to_string(size) + "\t0\t0\t" + colors[i] + "\t" + font;
  }
  return runs;
}

std::string readme_text_runs(std::size_t length, int size, const char* color, const char* font) {
  return "v1\n0\t" + std::to_string(length) + "\t" + std::to_string(size) + "\t0\t0\t" + color + "\t" + font;
}

// Text layer from metadata alone (pixels arrive via the identity apply_text_warp
// render below), styled with the poster look: dark stroke + soft drop shadow.
patchy::LayerId add_readme_warp_text_layer(patchy::Document& document, const char* name, int x, int y,
                                           const std::string& text, const std::string& runs, int size,
                                           const char* color, float stroke_size, float shadow_distance,
                                           float shadow_size) {
  patchy::Layer layer(document.allocate_layer_id(), name,
                      solid_pixels(1, 1, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));
  const auto id = layer.id();
  layer.set_bounds(patchy::Rect{x, y, 1, 1});
  layer.metadata()[patchy::kLayerMetadataText] = text;
  layer.metadata()[patchy::kLayerMetadataTextRuns] = runs;
  layer.metadata()[patchy::kLayerMetadataTextParagraphRuns] =
      "v1\n0\t" + std::to_string(text.size()) + "\tleft";
  layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial Black";
  layer.metadata()[patchy::kLayerMetadataTextSize] = std::to_string(size);
  layer.metadata()[patchy::kLayerMetadataTextColor] = color;
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.opacity = 0.55F;
  shadow.distance = shadow_distance;
  shadow.size = shadow_size;
  layer.layer_style().drop_shadows.push_back(shadow);
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.color = patchy::RgbColor{0x24, 0x12, 0x38};
  stroke.size = stroke_size;
  layer.layer_style().strokes.push_back(stroke);
  document.add_layer(std::move(layer));
  return id;
}

// A finished type poster makes the continuous shadow legible at README size.
// This stays offscreen because scripts cannot stage the Layer Style dialog.
void shot_readme_long_shadow() {
  patchy::test::register_test_fonts(patchy::test::TestFontRole::ArialBlack);
  QImage background(1180, 780, QImage::Format_RGB32);
  background.fill(QColor("#ef684b"));
  {
    QPainter painter(&background);
    painter.fillRect(60, 62, 48, 7, QColor("#fff1ce"));
    QFont font(QStringLiteral("Arial"));
    font.setPixelSize(20);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(QColor("#fff1ce"));
    painter.drawText(125, 75, QStringLiteral("TYPE / LIGHT / DEPTH"));
    font.setPixelSize(18);
    font.setBold(false);
    painter.setFont(font);
    painter.drawText(60, 725, QStringLiteral("One layer style. A different dimension."));
  }
  auto document = patchy::ui::document_from_qimage(background, "Coral paper");
  const auto long_id = add_readme_warp_text_layer(document, "LONG", 60, 185, "LONG",
      readme_text_runs(4, 174, "#fff1ce", "Arial Black"), 174, "#fff1ce", 0, 230, 0);
  const auto shadow_id = add_readme_warp_text_layer(document, "SHADOW", 60, 390, "SHADOW",
      readme_text_runs(6, 116, "#fff1ce", "Arial Black"), 116, "#fff1ce", 0, 230, 0);
  for (const auto id : {long_id, shadow_id}) {
    auto* layer = document.find_layer(id);
    CHECK(layer != nullptr);
    auto& style = layer->layer_style();
    style.strokes.clear();
    auto& shadow = style.drop_shadows.front();
    shadow.continuous = true;
    shadow.angle_degrees = 135.0F;
    shadow.opacity = 1.0F;
    shadow.color = patchy::RgbColor{0x36, 0x43, 0x49};
    shadow.blend_mode = patchy::BlendMode::Normal;
  }
  document.set_active_layer(long_id);
  patchy::ui::MainWindow window;
  show_readme_shot_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Long Shadow Poster"));
  close_untitled_start_tab(window);
  auto& staged = patchy::ui::MainWindowTestAccess::document(window);
  for (const auto id : {long_id, shadow_id}) {
    auto* layer = staged.find_layer(id);
    CHECK(layer != nullptr);
    CHECK(patchy::ui::MainWindowTestAccess::apply_text_warp(window, *layer, patchy::TextWarp{}));
  }
  require_canvas(window)->document_changed();
  require_action(window, "viewFitOnScreenAction")->trigger();
  QApplication::processEvents();
  bool captured = false;
  std::exception_ptr error;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog"));
    try {
      CHECK(dialog != nullptr);
      auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
      auto* continuous = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleDropShadowContinuousCheck"));
      CHECK(categories != nullptr && continuous != nullptr);
      const auto rows = categories->findItems(QStringLiteral("Drop Shadow"), Qt::MatchExactly);
      CHECK(!rows.empty());
      categories->setCurrentItem(rows.front());
      CHECK(continuous->isChecked());
      QApplication::processEvents();
      const QPoint offset(window.width() - dialog->width() - 10, 145);
      dialog->move(window.geometry().topLeft() + offset);
      reset_readme_status_bar(window);
      auto base = window.grab().toImage();
      draw_readme_overlay(base, dialog->grab().toImage(), offset);
      save_readme_shot("shot_readme_long_shadow", base);
      captured = true;
    } catch (...) {
      error = std::current_exception();
    }
    if (dialog != nullptr) dialog->reject();
  });
  require_action(window, "layerBlendingOptionsAction")->trigger();
  if (error) std::rethrow_exception(error);
  CHECK(captured);
}

// Live typography specimen: warped type, mixed-run paragraph text with the Character
// panel's styles, tracking, and real Japanese vertical columns, with the Warp Text dialog
// and the Character and Paragraph panels open.
void shot_readme_warp_text() {
  for (const auto role : {patchy::test::TestFontRole::ArialBlack, patchy::test::TestFontRole::UiDefault,
                          patchy::test::TestFontRole::Verdana, patchy::test::TestFontRole::Georgia,
                          patchy::test::TestFontRole::JapaneseGothic}) {
    patchy::test::register_test_fonts(role);
  }
  const auto japanese_families = QFontDatabase::families(QFontDatabase::Japanese);
  if (japanese_families.isEmpty()) {
    patchy::test::skip("no Japanese font for the typography showcase");
  }
  QString japanese_font = japanese_families.front();
  for (const auto& family : japanese_families) {
    if (family.contains(QStringLiteral("Gothic"), Qt::CaseInsensitive)) {
      japanese_font = family;
      break;
    }
  }
  patchy::ui::MainWindow window;
  show_readme_shot_window(window);
  QImage poster(1180, 780, QImage::Format_RGB32);
  poster.fill(QColor("#202a36"));
  {
    QPainter painter(&poster);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(44, 432, 710, 292, QColor("#f3ede0"));
    painter.fillRect(780, 432, 356, 292, QColor("#eb9976"));
    painter.fillRect(60, 398, 1060, 2, QColor("#54616b"));
    QFont font(QStringLiteral("Verdana"));
    font.setPixelSize(17);
    painter.setFont(font);
    painter.setPen(QColor("#b5c2c8"));
    painter.drawText(60, 382, QStringLiteral("WARP / LAYER EFFECTS"));
    painter.drawText(60, 756, QStringLiteral("One paragraph. Mixed faces. Still editable."));
    painter.drawText(800, 756, QStringLiteral("VERTICAL TYPE / JAPANESE"));
    font.setPixelSize(16);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(QColor("#5e6568"));
    painter.drawText(70, 465, QStringLiteral("TYPE WITH CHARACTER"));
  }
  auto built = patchy::ui::document_from_qimage(poster, "Paper and ink");
  const char* black_font = "Arial%20Black";
  const auto flag_id = add_readme_warp_text_layer(built, "Flag", 60, 284, "Flag",
      readme_text_runs(4, 42, "#f3ede0", black_font), 42, "#f3ede0", 2, 4, 6);
  const auto fisheye_id = add_readme_warp_text_layer(built, "Fisheye", 225, 284, "Fisheye",
      readme_text_runs(7, 42, "#e6bf70", black_font), 42, "#e6bf70", 2, 4, 6);
  const auto twist_id = add_readme_warp_text_layer(built, "Twist", 448, 284, "Twist",
      readme_text_runs(5, 42, "#ee9b7d", black_font), 42, "#ee9b7d", 2, 4, 6);
  const auto headline_id = add_readme_warp_text_layer(built, "PATCHY / live warp", 120, 125, "PATCHY",
      readme_rainbow_runs({"#ed997b", "#e6ba72", "#eee2b9", "#93c6ad", "#88bacd", "#b5a8cb"}, 100, black_font),
      100, "#eee2b9", 3, 7, 9);

  // Serialize real rich-text runs, so the paragraph can be edited as one object.
  std::string paragraph_text;
  std::vector<patchy::psd::PsdTextStyleRun> paragraph_runs;
  const auto append_run = [&](const char* text, bool bold = false, bool italic = false,
                               patchy::RgbColor color = {0x28, 0x36, 0x42}, const char* family = "Verdana",
                               const std::function<void(patchy::psd::PsdTextStyleRun&)>& character = {}) {
    patchy::psd::PsdTextStyleRun run;
    run.start = static_cast<int>(paragraph_text.size());
    run.length = static_cast<int>(std::string_view(text).size());
    run.family = family;
    run.size = 24;
    run.leading = 44;
    run.bold = bold;
    run.italic = italic;
    run.color = color;
    if (character) character(run);
    paragraph_runs.push_back(run);
    paragraph_text += text;
  };
  // The Character panel's styles, each on its own words: underline, strikethrough,
  // superscript, subscript, small caps and all caps.
  constexpr patchy::RgbColor kInk{0x28, 0x36, 0x42};
  append_run("Make something ");
  append_run("bold", true);
  append_run(", add a little ");
  append_run("italic", false, true);
  append_run(", give ");
  append_run("color", true, false, {0xb2, 0x48, 0x35});
  append_run(" a voice. ");
  append_run("Underline", false, false, kInk, "Verdana", [](auto& run) { run.underline = true; });
  append_run(" what matters, ");
  append_run("strike", false, false, kInk, "Verdana", [](auto& run) { run.strikethrough = true; });
  append_run(" what doesn't, add a footnote");
  append_run("1", false, false, kInk, "Verdana", [](auto& run) { run.font_baseline = 1; });
  append_run(" or H");
  append_run("2", false, false, kInk, "Verdana", [](auto& run) { run.font_baseline = 2; });
  append_run("O, set ");
  append_run("Small Caps", false, false, kInk, "Verdana", [](auto& run) { run.font_caps = 1; });
  append_run(" or ");
  append_run("all caps", false, false, kInk, "Verdana", [](auto& run) { run.font_caps = 2; });
  append_run(", and mix ");
  append_run("a serif", false, true, {0x26, 0x76, 0x79}, "Georgia");
  append_run(" with a sans. Every word stays editable.");
  const auto paragraph_id = add_readme_warp_text_layer(built, "One paragraph / mixed styles", 70, 490,
      paragraph_text, patchy::psd::serialize_patchy_text_runs(paragraph_runs), 24, "#283642", 0, 0, 0);
  auto* paragraph = built.find_layer(paragraph_id);
  CHECK(paragraph != nullptr);
  paragraph->layer_style().strokes.clear();
  paragraph->layer_style().drop_shadows.clear();
  paragraph->metadata()[patchy::kLayerMetadataTextFont] = "Verdana";
  paragraph->metadata()[patchy::kLayerMetadataTextFlow] = "box";
  paragraph->metadata()[patchy::kLayerMetadataTextBoxWidth] = "656";
  paragraph->metadata()[patchy::kLayerMetadataTextBoxHeight] = "214";
  paragraph->metadata()[patchy::kLayerMetadataTextLayoutMode] = patchy::kTextLayoutModePhotoshop;
  patchy::psd::PsdTextParagraphRun paragraph_style;
  paragraph_style.length = static_cast<int>(paragraph_text.size());
  paragraph_style.first_line_indent = 18;
  paragraph->metadata()[patchy::kLayerMetadataTextParagraphRuns] =
      patchy::psd::serialize_patchy_paragraph_runs(std::span(&paragraph_style, 1));

  const auto japanese = QStringLiteral("\u6587\u5b57\u3067\u904a\u307c\u3046\n\u8272\u3092\u697d\u3057\u3082\u3046");
  patchy::psd::PsdTextStyleRun vertical_run;
  vertical_run.length = static_cast<int>(japanese.size());
  vertical_run.family = japanese_font.toUtf8().toStdString();
  vertical_run.size = 39;
  vertical_run.color = {0x28, 0x36, 0x42};
  const auto vertical_id = add_readme_warp_text_layer(built, "Japanese / vertical columns", 961, 447,
      japanese.toUtf8().toStdString(), patchy::psd::serialize_patchy_text_runs(std::span(&vertical_run, 1)),
      39, "#283642", 0, 0, 0);
  auto* vertical = built.find_layer(vertical_id);
  CHECK(vertical != nullptr);
  vertical->layer_style().strokes.clear();
  vertical->layer_style().drop_shadows.clear();
  vertical->metadata()[patchy::kLayerMetadataTextFont] = vertical_run.family;
  vertical->metadata()[patchy::kLayerMetadataTextOrientation] = patchy::kTextOrientationVertical;
  vertical->metadata()[patchy::kLayerMetadataTextParagraphRuns] =
      "v1\n0\t7\tleft\n7\t6\tleft";

  const std::string tracking_text = "LETTERS AT PLAY";
  patchy::psd::PsdTextStyleRun tracking_run;
  tracking_run.length = static_cast<int>(tracking_text.size());
  tracking_run.family = "Verdana";
  tracking_run.size = 22;
  tracking_run.tracking = 220;
  tracking_run.color = {0xc0, 0xd3, 0xd5};
  const auto tracking_id = add_readme_warp_text_layer(built, "Tracking / +220", 60, 46, tracking_text,
      patchy::psd::serialize_patchy_text_runs(std::span(&tracking_run, 1)), 22, "#c0d3d5", 0, 0, 0);
  auto* tracked = built.find_layer(tracking_id);
  CHECK(tracked != nullptr);
  tracked->layer_style().strokes.clear();
  tracked->layer_style().drop_shadows.clear();
  tracked->metadata()[patchy::kLayerMetadataTextFont] = "Verdana";

  built.set_active_layer(headline_id);
  window.add_document_session(std::move(built), QStringLiteral("Type Playground"));
  close_untitled_start_tab(window);
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  for (const auto id : {headline_id, flag_id, fisheye_id, twist_id, paragraph_id, vertical_id, tracking_id}) {
    auto* layer = document.find_layer(id);
    CHECK(layer != nullptr);
    CHECK(patchy::ui::MainWindowTestAccess::apply_text_warp(window, *layer, patchy::TextWarp{}));
    CHECK(std::as_const(*layer).pixels().width() > 1 && std::as_const(*layer).pixels().height() > 1);
  }
  const struct { patchy::LayerId id; const char* style; double bend; } word_warps[] = {
      {flag_id, "warpFlag", 45.0}, {fisheye_id, "warpFisheye", 55.0}, {twist_id, "warpTwist", 60.0}};
  for (const auto& spec : word_warps) {
    auto* layer = document.find_layer(spec.id);
    CHECK(layer != nullptr);
    patchy::TextWarp warp;
    warp.style = spec.style;
    warp.value = spec.bend;
    CHECK(patchy::ui::MainWindowTestAccess::apply_text_warp(window, *layer, warp));
  }
  require_canvas(window)->document_changed();
  require_action(window, "viewFitOnScreenAction")->trigger();
  QApplication::processEvents();
  // The Character and Paragraph panels open first and show the headline's settings beside
  // the Warp Text dialog. Each panel runs its own non-modal loop (run_non_modal_dialog), so
  // the next window opens from a timer inside the previous one's loop, and each closes as
  // the loops unwind.
  auto* character_button = window.findChild<QPushButton*>(QStringLiteral("textCharacterButton"));
  auto* paragraph_button = window.findChild<QPushButton*>(QStringLiteral("textParagraphButton"));
  CHECK(character_button != nullptr && character_button->isVisible());
  CHECK(paragraph_button != nullptr && paragraph_button->isVisible());
  bool captured = false;
  std::exception_ptr error;
  const auto capture = [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("warpTextDialog"));
    try {
      auto* character = find_top_level_dialog(QStringLiteral("textCharacterDialog"));
      auto* paragraph_panel = find_top_level_dialog(QStringLiteral("textParagraphDialog"));
      CHECK(dialog != nullptr && character != nullptr && paragraph_panel != nullptr);
      CHECK(character->isVisible() && paragraph_panel->isVisible());
      auto* style_combo = dialog->findChild<QComboBox*>(QStringLiteral("warpTextStyleCombo"));
      auto* bend_spin = dialog->findChild<QSpinBox*>(QStringLiteral("warpTextBendSpin"));
      CHECK(style_combo != nullptr && bend_spin != nullptr);
      style_combo->setCurrentIndex(style_combo->findData(QStringLiteral("warpArc")));
      bend_spin->setValue(36);
      process_events_for(300);
      reset_readme_status_bar(window);
      const auto icon = window.windowIcon();
      const auto paragraph_image = readme_captioned_dialog(*paragraph_panel, icon);
      const auto warp_image = readme_captioned_dialog(*dialog, icon);
      const auto character_image = readme_captioned_dialog(*character, icon);
      // Paragraph and Warp Text side by side in the empty canvas right of the headline;
      // Character over the lower Layers rows, leaving the text layers above it in view.
      const QPoint paragraph_offset(690, 128);
      const QPoint warp_offset(paragraph_offset.x() + paragraph_image.width() + 12, 128);
      const QPoint character_offset(window.width() - character_image.width() - 8,
                                    window.height() - character_image.height() - 48);
      auto base = window.grab().toImage();
      draw_readme_overlay(base, paragraph_image, paragraph_offset);
      draw_readme_overlay(base, character_image, character_offset);
      draw_readme_overlay(base, warp_image, warp_offset);
      save_readme_shot("shot_readme_warp_text", base);
      captured = true;
    } catch (...) {
      error = std::current_exception();
    }
    if (dialog != nullptr) dialog->reject();
  };
  QTimer::singleShot(0, [&] {
    // Inside the Character panel's loop.
    QTimer::singleShot(0, [&] {
      // Inside the Paragraph panel's loop: Warp Text is modal and returns once captured.
      QTimer::singleShot(0, capture);
      patchy::ui::MainWindowTestAccess::request_warp_text_dialog(window);
      if (auto* panel = find_top_level_dialog(QStringLiteral("textParagraphDialog")); panel != nullptr) panel->reject();
    });
    paragraph_button->click();
    if (auto* panel = find_top_level_dialog(QStringLiteral("textCharacterDialog")); panel != nullptr) panel->reject();
  });
  character_button->click();
  QApplication::processEvents();
  if (error) std::rethrow_exception(error);
  CHECK(captured);
}

// Hand-plotted, perfectly tileable pixel art: white-noise grass shades, a dirt
// path riding sines whose periods divide the tile (so it wraps), and
// decorations plotted through a wrapping setter — seamless by construction.
QImage readme_seamless_tile() {
  constexpr int kSize = 128;
  QImage tile(kSize, kSize, QImage::Format_RGB32);
  const auto hash01 = [](int x, int y, std::uint32_t salt) {
    auto h = static_cast<std::uint32_t>(x) * 374761393u + static_cast<std::uint32_t>(y) * 668265263u +
             salt * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<double>((h ^ (h >> 16)) & 0xFFFFu) / 65535.0;
  };
  const auto path_center = [](int x) {
    const auto t = 2.0 * 3.14159265358979323846 * x / kSize;
    return 64.0 + 10.0 * std::sin(t) + 3.0 * std::sin(2.0 * t + 1.1);
  };
  const QColor grass[4] = {QColor(0x4e, 0x99, 0x3f), QColor(0x44, 0x87, 0x38), QColor(0x5b, 0xa8, 0x4a),
                           QColor(0x69, 0xb8, 0x55)};
  const QColor dirt[3] = {QColor(0xa8, 0x7c, 0x4f), QColor(0x97, 0x6d, 0x45), QColor(0xb8, 0x8c, 0x5c)};
  for (int y = 0; y < kSize; ++y) {
    for (int x = 0; x < kSize; ++x) {
      const auto v = hash01(x, y, 1u);
      QColor color = v < 0.60 ? grass[0] : v < 0.80 ? grass[1] : v < 0.93 ? grass[2] : grass[3];
      const auto dist = std::abs(y - path_center(x));
      if (dist < 10.0) {
        const auto d = hash01(x, y, 2u);
        color = d < 0.62 ? dirt[0] : d < 0.86 ? dirt[1] : dirt[2];
      } else if (dist < 11.5) {
        color = QColor(0x6f, 0x51, 0x33);
      }
      tile.setPixelColor(x, y, color);
    }
  }
  const auto put = [&tile](int x, int y, QColor color) {
    tile.setPixelColor(((x % kSize) + kSize) % kSize, ((y % kSize) + kSize) % kSize, color);
  };
  const auto block2 = [&put](int x, int y, QColor color) {
    put(x, y, color);
    put(x + 1, y, color);
    put(x, y + 1, color);
    put(x + 1, y + 1, color);
  };
  for (int i = 0; i < 8; ++i) {  // stepping stones along the path
    const auto stone_x = i * 16 + 6;
    const auto stone_y = static_cast<int>(path_center(stone_x)) + (i % 3) * 4 - 5;
    for (int dy = 0; dy < 3; ++dy) {
      for (int dx = 0; dx < 4; ++dx) {
        const bool shaded = dy == 2 || dx == 3;
        put(stone_x + dx, stone_y + dy,
            shaded ? QColor(0x8a, 0x77, 0x58) : QColor(0xd4, 0xcd, 0xb4));
      }
    }
  }
  struct Spot {
    int x;
    int y;
  };
  const auto on_grass = [&](Spot spot, double margin) {
    return std::abs(spot.y - path_center(spot.x)) > margin;
  };
  const Spot flowers[] = {{14, 16}, {52, 8},   {90, 20},  {116, 30}, {30, 110},
                          {74, 102}, {104, 116}, {8, 94},  {60, 120}, {40, 28}};
  int flower_index = 0;
  for (const auto& spot : flowers) {
    if (!on_grass(spot, 17.0)) {
      continue;
    }
    const auto petal = (flower_index++ % 2) == 0 ? QColor(0xe8, 0x8c, 0xc8) : QColor(0xf4, 0xf4, 0xf4);
    block2(spot.x - 1, spot.y - 3, petal);
    block2(spot.x - 1, spot.y + 1, petal);
    block2(spot.x - 3, spot.y - 1, petal);
    block2(spot.x + 1, spot.y - 1, petal);
    block2(spot.x - 1, spot.y - 1, QColor(0xf5, 0xd4, 0x4f));
  }
  const Spot rocks[] = {{18, 34}, {96, 100}, {118, 58}};
  for (const auto& spot : rocks) {
    if (!on_grass(spot, 17.0)) {
      continue;
    }
    for (int dy = 0; dy < 4; ++dy) {
      for (int dx = 0; dx < 5; ++dx) {
        if ((dy == 0 || dy == 3) && (dx == 0 || dx == 4)) {
          continue;  // rounded corners
        }
        QColor color(0x9a, 0x9a, 0x92);
        if (dy == 0) {
          color = QColor(0xb5, 0xb5, 0xac);
        } else if (dy == 3 || dx == 4) {
          color = QColor(0x6e, 0x6e, 0x66);
        }
        put(spot.x + dx, spot.y + dy, color);
      }
    }
  }
  const Spot tufts[] = {{24, 22}, {68, 14}, {102, 28}, {44, 104}, {88, 94}, {12, 50}, {120, 104}, {56, 30}};
  for (const auto& spot : tufts) {
    if (!on_grass(spot, 15.0)) {
      continue;
    }
    const QColor blade(0x2f, 0x63, 0x2a);
    const QColor tip(0x74, 0xc4, 0x60);
    put(spot.x, spot.y, blade);
    put(spot.x, spot.y - 1, blade);
    put(spot.x, spot.y - 2, tip);
    put(spot.x - 2, spot.y, blade);
    put(spot.x - 2, spot.y - 1, tip);
    put(spot.x + 2, spot.y, blade);
    put(spot.x + 2, spot.y - 1, tip);
  }
  return tile;
}

// Seamless tiling mode: the pixel-art tile repeating across the whole canvas as
// live ghost tiles, with a just-painted black line that wraps over the tile
// edges, proving strokes tile seamlessly as you paint.
void shot_readme_tile_preview() {
  patchy::ui::MainWindow window;
  show_readme_shot_window(window);
  window.add_document_session(patchy::ui::document_from_qimage(readme_seamless_tile(), "Tile"),
                              QStringLiteral("Seamless Tile"));
  QApplication::processEvents();
  close_untitled_start_tab(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  auto* rulers_action = require_action(window, "viewToggleRulersAction");
  if (!rulers_action->isChecked()) {
    rulers_action->trigger();
  }
  auto* canvas = require_canvas(window);
  auto* tiling_action = require_action(window, "viewTilingModeAction");
  CHECK(!tiling_action->isChecked());
  tiling_action->trigger();
  QApplication::processEvents();
  CHECK(canvas->tiling_preview_enabled());

  // 200% keeps the pixels crisp while leaving room for a full field of ghost
  // copies around the center tile.
  canvas->set_zoom(2.0);
  canvas->center_document_in_view();
  QApplication::processEvents();

  // A hard black dash covering only part of the tile, painted as two strokes:
  // one leaves the right edge and the other re-enters the left edge at the same
  // height (the manual wrap a texture artist paints). The tiled view joins them
  // across every seam, and the mid-tile gap makes each repeated copy read as
  // the single painted piece it is.
  canvas->set_brush_size(5);
  canvas->set_brush_softness(0);
  canvas->set_primary_color(Qt::black);
  paint_readme_polyline(*canvas, {QPointF(78.0, 106.0), QPointF(104.0, 92.0), QPointF(128.0, 100.0)});
  paint_readme_polyline(*canvas, {QPointF(0.0, 100.0), QPointF(24.0, 110.0), QPointF(46.0, 98.0)});
  QApplication::processEvents();

  reset_readme_status_bar(window);
  save_readme_shot("shot_readme_tile_preview", window.grab().toImage());
  tiling_action->trigger();
  QApplication::processEvents();

  // Rulers persist to view settings on window teardown; restore the clean state.
  rulers_action->trigger();
  QApplication::processEvents();
}

// Smart objects: the Teenage Lawnmower title text converted to a smart object,
// caught mid Warp Transform (the Bezier cage bending the logo live, options bar
// on Flag/40%), with its embedded contents open in a child tab and the
// smart-object badge button in the panel. One scene shows both features; smart
// objects commit warps non-destructively.
void shot_readme_smart_objects() {
  const auto path = patchy::test::local_psd_fixture_path("mow_master.psd");
  patchy::test::skip_unless_exists(path);
  patchy::ui::MainWindow window;
  show_readme_shot_window(window);
  window.add_document_session(patchy::psd::DocumentIo::read_file(path), QStringLiteral("mow_master.psd"));
  QApplication::processEvents();
  close_untitled_start_tab(window);
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);
  const auto parent_tab_index = tabs->currentIndex();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* logo_item = require_layer_item(*layer_list, QStringLiteral("Lawnmower"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(logo_item);
  logo_item->setSelected(true);
  QApplication::processEvents();
  require_action(window, "layerConvertSmartObjectAction")->trigger();
  QApplication::processEvents();

  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  patchy::Layer* smart_layer = nullptr;
  for (auto& layer : document.layers()) {
    if (patchy::layer_is_smart_object(layer) && layer.name() == "Lawnmower") {
      smart_layer = &layer;
    }
  }
  CHECK(smart_layer != nullptr);
  const auto smart_id = smart_layer->id();

  // Open the embedded contents (the child tab tells the story in its title),
  // then come back to the parent for the transform.
  document.set_active_layer(smart_id);
  patchy::ui::MainWindowTestAccess::open_smart_object_contents(window);
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_smart_object_child(window));
  tabs->setCurrentIndex(parent_tab_index);
  QApplication::processEvents();

  auto* smart_item = require_layer_item(*layer_list, QStringLiteral("Lawnmower"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(smart_item);
  smart_item->setSelected(true);
  layer_list->scrollToItem(smart_item, QAbstractItemView::PositionAtCenter);
  patchy::ui::MainWindowTestAccess::document(window).set_active_layer(smart_id);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  require_action(window, "viewFitOnScreenAction")->trigger();
  QApplication::processEvents();

  auto* canvas = require_canvas(window);
  require_action(window, "editWarpTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->warp_transform_active());
  // Drive the options-bar warp controls (the user path) so the bar reads
  // Flag / 40% while the cage bends the logo live on the canvas.
  auto* warp_combo = window.findChild<QComboBox*>(QStringLiteral("warpStyleCombo"));
  auto* warp_bend = window.findChild<QDoubleSpinBox*>(QStringLiteral("warpBendSpin"));
  CHECK(warp_combo != nullptr && warp_bend != nullptr);
  const auto flag_row = warp_combo->findData(QStringLiteral("warpFlag"));
  CHECK(flag_row >= 0);
  warp_combo->setCurrentIndex(flag_row);
  warp_bend->setValue(40.0);
  process_events_for(300);  // let the live warp re-render land
  CHECK(canvas->warp_style_preset() == QStringLiteral("warpFlag"));

  reset_readme_status_bar(window);
  auto base = window.grab().toImage();
  save_readme_shot("shot_readme_smart_objects", base);
  canvas->cancel_warp_transform();  // nothing commits
  QApplication::processEvents();
}

QListWidgetItem* require_readme_gallery_filter_item(QListWidget& list,
                                                    const QString& filter_id) {
  for (int row = 0; row < list.count(); ++row) {
    auto* item = list.item(row);
    if (item != nullptr &&
        item->data(Qt::UserRole + 1).toString() == filter_id) {
      return item;
    }
  }
  CHECK(false);
  return nullptr;
}

// Pattern Manager: the complete preset browser with a bundled CC0 photo
// texture selected and its full-resolution tile visible in the preview.
void shot_readme_pattern_manager() {
  QTemporaryDir directory;
  CHECK(directory.isValid());
  patchy::ui::PatternLibrary library(
      directory.filePath(QStringLiteral("patterns")));
  CHECK(library.restore_default_patterns() > 0);
  const auto* texture = library.find_entry_by_pattern_id(
      QStringLiteral("f0705a00-0008-4c8b-9e3d-2a5b6c77e008"));
  CHECK(texture != nullptr);
  const auto texture_pattern_id = texture->id;
  patchy::ui::MainWindow theme_host;
  show_readme_shot_window(theme_host);
  bool captured = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patternManagerDialog"));
    CHECK(dialog != nullptr);
    auto* tree = dialog->findChild<QTreeWidget*>(
        QStringLiteral("patternManagerTree"));
    auto* preview = dialog->findChild<QWidget*>(
        QStringLiteral("patternManagerPreview"));
    auto* name = dialog->findChild<QLineEdit*>(
        QStringLiteral("patternManagerNameEdit"));
    CHECK(tree != nullptr && preview != nullptr && name != nullptr);
    CHECK(name->text() == QStringLiteral("Weathered Marble"));
    auto* item = tree->currentItem();
    CHECK(item != nullptr && item->parent() != nullptr);
    CHECK(item->data(0, Qt::UserRole).toString() == texture->storage_id);
    item->parent()->setExpanded(true);
    dialog->resize(980, 640);
    tree->scrollToItem(item->parent(), QAbstractItemView::PositionAtTop);
    process_events_for(300);
    CHECK(preview->property("previewZoomPercent").toInt() == 100);
    save_readme_shot("shot_readme_pattern_manager", dialog->grab().toImage());
    captured = true;
    dialog->reject();
  });
  const auto result = patchy::ui::request_pattern_manager(
      &theme_host, library, texture_pattern_id);
  CHECK(captured);
  CHECK(result.isEmpty());
}

// Tilt-Shift Blur over a dense city scene, with its handle-only overlay (no
// boundary lines or bars) visible in the live gallery preview.
void shot_readme_tilt_shift() {
  const auto path = patchy::test::committed_format_fixture_path(
      "readme", "san_francisco_cityscape_cc0.jpg");
  QImage city(QString::fromStdString(path.string()));
  CHECK(!city.isNull());
  city = city.scaled(1100, 720, Qt::KeepAspectRatioByExpanding,
                     Qt::SmoothTransformation);
  city = city.copy((city.width() - 1100) / 2, (city.height() - 720) / 2,
                   1100, 720);
  const auto source = patchy::ui::pixels_from_image_rgba(city);
  const patchy::Rect bounds{0, 0, source.width(), source.height()};
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  patchy::ui::MainWindow theme_host;
  show_readme_shot_window(theme_host);
  bool captured = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("filterGalleryDialog"));
    CHECK(dialog != nullptr);
    auto* looks = dialog->findChild<QListWidget*>(
        QStringLiteral("filterGalleryLooksList"));
    auto* editor = dialog->findChild<QWidget*>(
        QStringLiteral("filterGalleryParameterEditor"));
    auto* preview = dynamic_cast<patchy::ui::ZoomableImagePreview*>(
        dialog->findChild<QWidget*>(QStringLiteral("filterGalleryPreview")));
    auto* status = dialog->findChild<QLabel*>(
        QStringLiteral("filterGalleryStatusLabel"));
    CHECK(looks != nullptr && editor != nullptr && preview != nullptr &&
          status != nullptr);
    looks->setCurrentItem(require_readme_gallery_filter_item(
        *looks, QStringLiteral("patchy.filters.tilt_shift_blur")));
    QApplication::processEvents();
    auto* blur = editor->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterBlurSpin"));
    auto* center_x = editor->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterCenterXSpin"));
    auto* center_y = editor->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterCenterYSpin"));
    auto* angle = editor->findChild<QSpinBox*>(
        QStringLiteral("filterAngleSpin"));
    auto* focus = editor->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterFocusHalfWidthSpin"));
    auto* transition = editor->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterTransitionWidthSpin"));
    CHECK(blur != nullptr && center_x != nullptr && center_y != nullptr &&
          angle != nullptr && focus != nullptr && transition != nullptr);
    blur->setValue(10.0);
    center_x->setValue(50.0);
    center_y->setValue(62.0);
    angle->setValue(-2);
    focus->setValue(10.0);
    transition->setValue(18.0);
    CHECK(process_events_until(
        [&] {
          return preview->property("filterTiltShiftOverlayVisible").toBool() &&
                 status->text() == QCoreApplication::translate("QObject", "Ready");
        },
        10000));
    dialog->repaint();
    process_events_for(100);
    save_readme_shot("shot_readme_tilt_shift", dialog->grab().toImage());
    captured = true;
    dialog->reject();
  });
  const auto result = patchy::ui::request_visual_filter_gallery(
      &theme_host, source, bounds, QRegion(), registry, patchy::RgbColor{},
      patchy::RgbColor{255, 255, 255});
  CHECK(captured);
  CHECK(result.outcome == patchy::ui::VisualFilterGalleryOutcome::Cancelled);
}

// The G'MIC Filter Library over the Havana street photo: Rodilius previewing live
// inside a selection of the right half, so the canvas shows the original beside the
// result, with the filter tree and the generated parameter panel in the dialog.
// Offscreen because scripts cannot open the dialog (docs/readme-showcase.md).
void shot_readme_gmic() {
  if (!patchy::gmic::engine_available()) {
    patchy::test::skip("this build has no G'MIC engine");
  }
  const auto path = patchy::test::committed_format_fixture_path(
      "readme", "havana_vintage_car_cc0.jpg");
  QImage street(QString::fromStdString(path.string()));
  CHECK(!street.isNull());
  // The street with the car centered under the seam, kept under the 1.5 megapixel
  // draft threshold so the first preview is already the full-quality one.
  street = street.copy(1000, 500, 1800, 1475)
               .scaled(1000, 820, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  auto document = patchy::ui::document_from_qimage(street, "Havana street");
  const int width = document.width();
  const int height = document.height();
  const int seam = width / 2;
  patchy::ui::MainWindow window;
  show_readme_shot_window(window);
  // Wider than the other scenes: this capture spans both README columns. The dock
  // is widened under the dialog so the whole photo stays in view beside it.
  window.resize(1920, 1080);
  QApplication::processEvents();
  window.add_document_session(std::move(document), QStringLiteral("havana_street.jpg"));
  close_untitled_start_tab(window);
  patchy::ui::MainWindowTestAccess::set_right_dock_stack_width(window, 880);
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  patchy::PixelBuffer mask(width, height, patchy::PixelFormat::gray8());
  for (int y = 0; y < height; ++y) {
    auto row = mask.row(y);
    for (int x = 0; x < width; ++x) {
      row[static_cast<std::size_t>(x)] = x >= seam ? 255 : 0;
    }
  }
  canvas->replace_selection_from_grayscale(mask, QStringLiteral("Rectangular Marquee"));
  require_action(window, "viewFitOnScreenAction")->trigger();
  QApplication::processEvents();

  const auto pixel_at = [&window](int x, int y) {
    const auto& staged = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
    const auto active = staged.active_layer_id();
    const auto* layer = active.has_value() ? staged.find_layer(*active) : nullptr;
    CHECK(layer != nullptr);
    const auto* px = layer->pixels().pixel(x - layer->bounds().x, y - layer->bounds().y);
    return QColor(px[0], px[1], px[2]);
  };
  const QPoint left_probe(seam / 2, height / 2);
  const QPoint right_probe(seam + seam / 2, height / 2);
  const auto left_before = pixel_at(left_probe.x(), left_probe.y());
  const auto right_before = pixel_at(right_probe.x(), right_probe.y());

  bool captured = false;
  std::exception_ptr error;
  QTimer::singleShot(0, &window, [&] {
    auto* dialog = window.findChild<patchy::ui::GmicFilterLibraryDialog*>();
    try {
      CHECK(dialog != nullptr);
      // Amplitude, thickness, sharpness, orientations, offset, smoothness,
      // color mode (Lighter), channels (All), value action (None): bright
      // strokes that still leave the car and the buildings readable.
      CHECK(dialog->select_filter("fx_rodilius",
                                  {"7", "8", "300", "9", "0", "0", "1", "0", "0"}));
      dialog->resize(880, 800);
      auto* tree = dialog->findChild<QTreeWidget*>(QStringLiteral("gmicFilterTree"));
      CHECK(tree != nullptr && tree->currentItem() != nullptr);
      tree->scrollToItem(tree->currentItem(), QAbstractItemView::PositionAtCenter);
      CHECK(process_events_until(
          [&] { return pixel_at(right_probe.x(), right_probe.y()) != right_before; },
          60000));
      // One full-quality run lands at once; let the canvas drop its badge.
      process_events_for(1000);
      CHECK(pixel_at(left_probe.x(), left_probe.y()) == left_before);
      const QPoint offset(window.width() - dialog->width() - 12, 150);
      dialog->move(window.geometry().topLeft() + offset);
      QApplication::processEvents();
      reset_readme_status_bar(window);
      auto base = window.grab().toImage();
      draw_readme_overlay(base, dialog->grab().toImage(), offset);
      save_readme_shot("shot_readme_gmic", base);
      captured = true;
    } catch (...) {
      error = std::current_exception();
    }
    if (dialog != nullptr) dialog->reject();
  });
  require_action(window, "filterGmicLibraryAction")->trigger();
  QApplication::processEvents();
  if (error) std::rethrow_exception(error);
  CHECK(captured);
}

QTreeWidgetItem* readme_style_item_for_storage_id(QTreeWidget& tree,
                                                  const QString& storage_id) {
  const std::function<QTreeWidgetItem*(QTreeWidgetItem*)> visit =
      [&](QTreeWidgetItem* item) -> QTreeWidgetItem* {
    if (item->data(0, Qt::UserRole).toString() == storage_id) {
      return item;
    }
    for (int child = 0; child < item->childCount(); ++child) {
      if (auto* found = visit(item->child(child)); found != nullptr) {
        return found;
      }
    }
    return nullptr;
  };
  for (int row = 0; row < tree.topLevelItemCount(); ++row) {
    if (auto* found = visit(tree.topLevelItem(row)); found != nullptr) {
      return found;
    }
  }
  return nullptr;
}

// Material styles: live application of the Riveted Steel preset to large
// transparent type, with the expanded Materials browser beside the canvas.
void shot_readme_material_styles() {
  patchy::test::register_test_fonts(patchy::test::TestFontRole::ArialBlack);
  QImage background(1180, 780, QImage::Format_RGB32);
  {
    QPainter painter(&background);
    QLinearGradient gradient(0, 0, 1180, 780);
    gradient.setColorAt(0.0, QColor(0x18, 0x24, 0x2b));
    gradient.setColorAt(0.52, QColor(0x2c, 0x46, 0x52));
    gradient.setColorAt(1.0, QColor(0x88, 0x5a, 0x3e));
    painter.fillRect(background.rect(), gradient);
    painter.setPen(QPen(QColor(255, 255, 255, 20), 2));
    for (int x = -600; x < 1500; x += 70) {
      painter.drawLine(x, 0, x + 780, 780);
    }
  }
  QImage lettering(1180, 780, QImage::Format_RGBA8888);
  lettering.fill(Qt::transparent);
  {
    QPainter painter(&lettering);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QFont font(QStringLiteral("Arial Black"));
    font.setPixelSize(155);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(QColor(235, 235, 228));
    painter.drawText(QRect(80, 165, 1020, 230), Qt::AlignCenter,
                     QStringLiteral("PATCHY"));
    font.setPixelSize(76);
    painter.setFont(font);
    painter.drawText(QRect(80, 405, 1020, 130), Qt::AlignCenter,
                     QStringLiteral("MATERIALS"));
  }

  auto document = patchy::ui::document_from_qimage(background, "Backdrop");
  patchy::Layer type_layer(document.allocate_layer_id(), "Material Type",
                           patchy::ui::pixels_from_image_rgba(lettering));
  const auto type_id = type_layer.id();
  document.add_layer(std::move(type_layer));
  document.set_active_layer(type_id);

  patchy::ui::MainWindow window;
  show_readme_shot_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Material Styles"));
  QApplication::processEvents();
  close_untitled_start_tab(window);
  window.style_library().restore_default_styles();
  const auto* preset = window.style_library().find_entry_by_style_id(
      QStringLiteral("57a1e500-0024-4c6d-8f2a-9b3d4e55c024"));
  CHECK(preset != nullptr);
  const auto storage_id = preset->storage_id;
  require_action(window, "viewFitOnScreenAction")->trigger();
  QApplication::processEvents();

  bool captured = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog"));
    CHECK(dialog != nullptr);
    auto* categories = dialog->findChild<QListWidget*>(
        QStringLiteral("layerStyleCategoryList"));
    auto* browser = dialog->findChild<patchy::ui::StyleBrowserWidget*>(
        QStringLiteral("layerStyleStylesBrowser"));
    CHECK(categories != nullptr && browser != nullptr);
    const auto style_rows = categories->findItems(
        QStringLiteral("Style Presets"), Qt::MatchExactly);
    CHECK(!style_rows.empty());
    categories->setCurrentItem(style_rows.front());
    QApplication::processEvents();
    auto* item = readme_style_item_for_storage_id(*browser, storage_id);
    CHECK(item != nullptr && item->parent() != nullptr);
    item->parent()->setExpanded(true);
    browser->scrollToItem(item, QAbstractItemView::PositionAtCenter);
    QApplication::processEvents();
    const auto center = browser->visualItemRect(item).center();
    send_mouse(*browser->viewport(), QEvent::MouseButtonPress, center,
               Qt::LeftButton, Qt::LeftButton);
    send_mouse(*browser->viewport(), QEvent::MouseButtonRelease, center,
               Qt::LeftButton, Qt::NoButton);
    process_events_for(700);
    const QPoint dialog_offset(690, 130);
    dialog->move(window.geometry().topLeft() + dialog_offset);
    QApplication::processEvents();
    reset_readme_status_bar(window);
    auto base = window.grab().toImage();
    draw_readme_overlay(base, dialog->grab().toImage(), dialog_offset);
    save_readme_shot("shot_readme_material_styles", base);
    captured = true;
    dialog->reject();
  });
  require_action(window, "layerBlendingOptionsAction")->trigger();
  QApplication::processEvents();
  CHECK(captured);
}

// README hero: a filtered photo with a grouped caption design. Its color wash
// is a real clipped layer; the photo retains its shared mask and native stack.
// The color picker and a live Levels adjustment editor complete the workspace.
void shot_readme_smart_filters() {
  SettingsValueRestorer picker_tab_restorer(QStringLiteral("colorPanel/lastTab"));
  SettingsValueRestorer palette_choice_restorer(QStringLiteral("palettes/lastPaletteChoice"));
  SettingsValueRestorer rulers_restorer(QStringLiteral("view/rulersVisible"));
  patchy::test::register_test_fonts(patchy::test::TestFontRole::ArialBlack);
  patchy::test::register_test_fonts(patchy::test::TestFontRole::Verdana);
  const auto path =
      patchy::test::local_psd_fixture_path("akiko_cycling_okinawa.jpg");
  patchy::test::skip_unless_exists(path);
  QImage photo(patchy::ui::to_qstring(path));
  CHECK(!photo.isNull());
  photo = photo.copy(0, 170, photo.width(), std::min(1180, photo.height() - 170))
              .scaled(900, 760, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  patchy::ui::MainWindow window;
  show_readme_shot_window(window);
  window.add_document_session(patchy::ui::document_from_qimage(photo, "Cyclist"),
                              QStringLiteral("Coastal Ride"));
  QApplication::processEvents();
  close_untitled_start_tab(window);
  require_action(window, "filterConvertForSmartFiltersAction")->trigger();
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  const auto& document =
      std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  patchy::PixelBuffer selection(document.width(), document.height(),
                                patchy::PixelFormat::gray8());
  selection.clear(0);
  const auto cx = document.width() / 2;
  const auto cy = document.height() / 2;
  const auto radius_x = document.width() * 42 / 100;
  const auto radius_y = document.height() * 42 / 100;
  for (int y = 0; y < document.height(); ++y) {
    auto row = selection.row(y);
    for (int x = 0; x < document.width(); ++x) {
      const auto nx = static_cast<double>(x - cx) / radius_x;
      const auto ny = static_cast<double>(y - cy) / radius_y;
      const auto distance = std::sqrt(nx * nx + ny * ny);
      // Protect the cyclist, including her face. Only the surroundings receive
      // the blur stack; the soft transition avoids a hard cutout edge.
      row[static_cast<std::size_t>(x)] = static_cast<std::uint8_t>(
          255.0 - std::clamp((1.08 - distance) * 900.0, 0.0, 255.0));
    }
  }
  canvas->replace_selection_from_grayscale(
      selection, QStringLiteral("Protect cyclist / soften background"));
  bool applied = false;
  std::exception_ptr gallery_error;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("filterGalleryDialog"));
    try {
      CHECK(dialog != nullptr);
      auto* looks = dialog->findChild<QListWidget*>(
          QStringLiteral("filterGalleryLooksList"));
      auto* duplicate = dialog->findChild<QPushButton*>(
          QStringLiteral("filterGalleryDuplicateEffectButton"));
      auto* buttons = dialog->findChild<QDialogButtonBox*>(
          QStringLiteral("filterGalleryButtonBox"));
      auto* status = dialog->findChild<QLabel*>(
          QStringLiteral("filterGalleryStatusLabel"));
      CHECK(looks != nullptr && duplicate != nullptr && buttons != nullptr &&
            status != nullptr);
      looks->setCurrentItem(require_readme_gallery_filter_item(
          *looks, QStringLiteral("patchy.filters.gaussian_blur")));
      QApplication::processEvents();
      if (auto* radius = dialog->findChild<QDoubleSpinBox*>(
              QStringLiteral("filterRadiusSpin"));
        radius != nullptr) {
        radius->setValue(1.1);
      }
      duplicate->click();
      QApplication::processEvents();
      looks->setCurrentItem(require_readme_gallery_filter_item(
          *looks, QStringLiteral("patchy.filters.dust_and_scratches")));
      QApplication::processEvents();
      if (auto* radius = dialog->findChild<QSpinBox*>(
              QStringLiteral("filterRadiusSpin"));
        radius != nullptr) {
        radius->setValue(1);
      }
      if (auto* threshold = dialog->findChild<QSpinBox*>(
              QStringLiteral("filterThresholdSpin"));
          threshold != nullptr) {
        threshold->setValue(12);
      }
      duplicate->click();
      QApplication::processEvents();
      looks->setCurrentItem(require_readme_gallery_filter_item(
          *looks, QStringLiteral("patchy.filters.surface_blur")));
      QApplication::processEvents();
      if (auto* radius = dialog->findChild<QDoubleSpinBox*>(
              QStringLiteral("filterRadiusSpin"));
        radius != nullptr) {
        radius->setValue(2.2);
      }
      if (auto* threshold = dialog->findChild<QSpinBox*>(
              QStringLiteral("filterThresholdSpin"));
        threshold != nullptr) {
        threshold->setValue(20);
      }
      CHECK(process_events_until(
          [&] {
            return status->text() == QCoreApplication::translate("QObject", "Ready");
          },
          15000));
      applied = true;
      buttons->button(QDialogButtonBox::Ok)->click();
    } catch (...) {
      gallery_error = std::current_exception();
      if (dialog != nullptr) dialog->reject();
    }
  });
  require_action(window, "filterGalleryAction")->trigger();
  if (gallery_error) std::rethrow_exception(gallery_error);
  CHECK(applied);
  CHECK(process_events_until(
      [&] {
        return window.isEnabled() && !canvas->processing_operation_active() &&
               !canvas->processing_overlay_visible();
      },
      15000));
  canvas->clear_selection();
  auto& composed = patchy::ui::MainWindowTestAccess::document(window);
  const QRect face_region(composed.width() * 47 / 100, composed.height() * 21 / 100,
                          composed.width() * 13 / 100, composed.height() * 14 / 100);
  const auto filtered_photo = patchy::ui::qimage_from_document(std::as_const(composed), false);
  CHECK(filtered_photo.copy(face_region).convertToFormat(QImage::Format_RGBA8888) ==
        photo.copy(face_region).convertToFormat(QImage::Format_RGBA8888));
  const auto cyclist_id = *composed.active_layer_id();
  const int caption_y = composed.height() - 178;
  QImage card(composed.width(), composed.height(), QImage::Format_ARGB32);
  card.fill(Qt::transparent);
  {
    QPainter painter(&card);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 230));
    painter.drawRoundedRect(QRectF(32, caption_y, composed.width() - 64, 146), 18, 18);
  }
  const auto card_id = composed.add_pixel_layer("Caption card", patchy::ui::pixels_from_image_rgba(card)).id();
  QImage wash(card.size(), QImage::Format_ARGB32);
  {
    QPainter painter(&wash);
    QLinearGradient gradient(0, 0, wash.width(), 0);
    gradient.setColorAt(0, QColor("#123443"));
    gradient.setColorAt(1, QColor("#277d8b"));
    painter.fillRect(wash.rect(), gradient);
  }
  const auto wash_id = composed.add_pixel_layer("Ocean color / clipped", patchy::ui::pixels_from_image_rgba(wash)).id();
  composed.find_layer(wash_id)->set_clipped(true);
  const std::string title = "COASTAL RIDE";
  const auto title_id = add_readme_warp_text_layer(composed, "COASTAL RIDE", 56, caption_y + 24,
      title, readme_text_runs(title.size(), 48, "#fff7df", "Arial%20Black"), 48, "#fff7df", 0, 0, 0);
  const std::string subtitle = "OKINAWA  /  A DAY ON TWO WHEELS";
  const auto subtitle_id = add_readme_warp_text_layer(composed, "Ride details", 60, caption_y + 98,
      subtitle, readme_text_runs(subtitle.size(), 18, "#d2ebed", "Verdana"), 18, "#d2ebed", 0, 0, 0);
  composed.find_layer(subtitle_id)->metadata()[patchy::kLayerMetadataTextFont] = "Verdana";
  for (const auto id : {title_id, subtitle_id}) {
    auto* layer = composed.find_layer(id);
    CHECK(layer != nullptr);
    layer->layer_style().strokes.clear();
    layer->layer_style().drop_shadows.clear();
    CHECK(patchy::ui::MainWindowTestAccess::apply_text_warp(window, *layer, patchy::TextWarp{}));
  }
  patchy::Layer notes(composed.allocate_layer_id(), "Ride notes", patchy::LayerKind::Group);
  notes.metadata()[patchy::kLayerMetadataGroupExpanded] = "true";
  const auto notes_id = notes.id();
  for (const auto id : {card_id, wash_id, title_id, subtitle_id}) {
    auto layer = patchy::take_layer_from_tree(composed.layers(), id);
    CHECK(layer.has_value());
    notes.children().push_back(std::move(*layer));
  }
  composed.add_layer(std::move(notes));
  composed.set_active_layer(cyclist_id);
  const auto* group = std::as_const(composed).find_layer(notes_id);
  CHECK(group != nullptr && group->children().size() == 4);
  const auto* clip_base = patchy::effective_clip_base(group->children(), 1);
  CHECK(clip_base != nullptr && clip_base->id() == card_id);
  patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
  canvas->document_changed();
  patchy::ui::MainWindowTestAccess::set_right_dock_stack_width(window, 380);
  auto* rulers_action = require_action(window, "viewToggleRulersAction");
  if (!rulers_action->isChecked()) {
    rulers_action->trigger();
  }
  require_action(window, "viewFitOnScreenAction")->trigger();
  process_events_for(900);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* item = require_layer_item(*layer_list, QStringLiteral("Cyclist"));
  auto* row = layer_list->itemWidget(item);
  CHECK(row != nullptr);
  const auto labels = row->findChildren<QLabel*>(
      QStringLiteral("layerSmartFilterEntryLabel"));
  CHECK(labels.size() == 3);
  reset_readme_status_bar(window);
  window.repaint();
  process_events_for(600);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Ride notes")) != nullptr);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Ocean color / clipped")) != nullptr);

  // Create an actual adjustment layer at neutral settings, preserving the
  // photograph. Reopen its editor so the histogram and editable layer coexist.
  bool levels_created = false;
  std::exception_ptr levels_error;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyLevelsDialog"));
    try {
      CHECK(dialog != nullptr);
      levels_created = true;
      dialog->accept();
    } catch (...) {
      levels_error = std::current_exception();
      if (dialog != nullptr) dialog->reject();
    }
  });
  require_action(window, "layerNewLevelsAdjustmentAction")->trigger();
  if (levels_error) std::rethrow_exception(levels_error);
  CHECK(levels_created);
  const auto levels_id = composed.active_layer_id();
  CHECK(levels_id.has_value());
  const auto* levels_layer = std::as_const(composed).find_layer(*levels_id);
  CHECK(levels_layer != nullptr && patchy::layer_is_adjustment(*levels_layer));
  const auto levels_settings = patchy::adjustment_settings_from_layer(*levels_layer);
  CHECK(levels_settings.has_value() && levels_settings->kind == patchy::AdjustmentKind::Levels);

  const QPoint picker_offset(18, 220);
  auto* picker_dialog = open_foreground_picker_for_readme_shot(window, 1, "vga256", picker_offset);
  auto* picker = picker_dialog->findChild<patchy::ui::PatchyColorPicker*>(
      QStringLiteral("patchyAdvancedColorPicker"));
  CHECK(picker != nullptr);
  picker->setCurrentColor(QColor(0xE0, 0x50, 0x7A));
  bool captured = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyLevelsDialog"));
    try {
      CHECK(dialog != nullptr);
      auto* preview = dialog->findChild<QCheckBox*>(QStringLiteral("levelsPreviewCheck"));
      CHECK(preview != nullptr && preview->isChecked());
      const QPoint levels_offset(window.width() - 380 - dialog->width() - 18, 220);
      dialog->move(window.geometry().topLeft() + levels_offset);
      process_events_for(400);
      const auto preview_image = patchy::ui::qimage_from_document(std::as_const(composed), false);
      CHECK(canvas->rulers_visible());
      CHECK(preview_image.copy(face_region).convertToFormat(QImage::Format_RGBA8888) ==
            photo.copy(face_region).convertToFormat(QImage::Format_RGBA8888));
      reset_readme_status_bar(window);
      auto base = window.grab().toImage();
      draw_readme_overlay(base, picker_dialog->grab().toImage(), picker_offset);
      draw_readme_overlay(base, dialog->grab().toImage(), levels_offset);
      save_readme_shot("shot_readme_smart_filters", base);
      captured = true;
    } catch (...) {
      levels_error = std::current_exception();
    }
    if (dialog != nullptr) dialog->reject();
  });
  require_action(window, "layerEditAdjustmentAction")->trigger();
  picker_dialog->close();
  QApplication::processEvents();
  if (levels_error) std::rethrow_exception(levels_error);
  CHECK(captured);
}

// Camera Raw: a real CC0 raw.pixls.us sample in the full develop dialog.
void shot_readme_camera_raw() {
  const auto path = patchy::test::local_format_fixture_path(
      "raw", "fujifilm_xt1.raf");
  patchy::test::skip_unless_exists(path);
  SettingsValueRestorer dialog_restorer(
      QStringLiteral("imports/showRawDevelopDialog"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("imports/showRawDevelopDialog"), true);
    settings.sync();
  }
  patchy::ui::MainWindow window;
  show_readme_shot_window(window);
  auto finished = std::make_shared<bool>(false);
  auto attempts = std::make_shared<int>(2400);
  auto step = std::make_shared<std::function<void()>>();
  *step = [=] {
    auto* dialog = find_top_level_dialog(QStringLiteral("rawDevelopDialog"));
    if (dialog != nullptr) {
      auto* preview = static_cast<patchy::ui::ZoomableImagePreview*>(
          dialog->findChild<QWidget*>(QStringLiteral("rawDevelopPreview")));
      auto* status = dialog->findChild<QLabel*>(
          QStringLiteral("rawDevelopStatus"));
      auto* exposure = dialog->findChild<QSlider*>(
          QStringLiteral("rawExposureSlider"));
      auto* shadows = dialog->findChild<QSlider*>(
          QStringLiteral("rawShadowsSlider"));
      auto* vibrance = dialog->findChild<QSlider*>(
          QStringLiteral("rawVibranceSlider"));
      if (preview != nullptr && status != nullptr && exposure != nullptr &&
          shadows != nullptr && vibrance != nullptr &&
          !preview->image().isNull() && status->text().isEmpty()) {
        exposure->setValue(30);
        shadows->setValue(24);
        vibrance->setValue(18);
        process_events_for(900);
        if (status->text().isEmpty() && !preview->image().isNull()) {
          save_readme_shot("shot_readme_camera_raw", dialog->grab().toImage());
          *finished = true;
          dialog->reject();
          return;
        }
      }
    }
    if ((*attempts)-- > 0) {
      QTimer::singleShot(25, *step);
    }
  };
  QTimer::singleShot(25, *step);
  patchy::ui::MainWindowTestAccess::open_document_path(
      window, QString::fromStdString(path.string()));
  CHECK(*finished);
}

// Quick Mask: load a clean, feathered portrait frame from a hidden layer's
// transparency, then convert it to the live red editing overlay. The Channels
// dock shows the temporary Quick Mask channel.
void shot_readme_quick_mask() {
  const auto path =
      patchy::test::local_psd_fixture_path("akiko_cycling_okinawa.jpg");
  patchy::test::skip_unless_exists(path);
  QImage photo(QString::fromStdString(path.string()));
  CHECK(!photo.isNull());
  photo = photo.scaled(800, 1200, Qt::KeepAspectRatio,
                       Qt::SmoothTransformation);
  QImage selection_source(photo.size(), QImage::Format_RGBA8888);
  selection_source.fill(Qt::transparent);
  {
    QPainter painter(&selection_source);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    const QRectF outer(photo.width() * 0.245, photo.height() * 0.075,
                       photo.width() * 0.49, photo.height() * 0.82);
    constexpr std::array<int, 7> alpha_steps = {18, 38, 72, 116, 168, 218, 255};
    for (std::size_t step = 0; step < alpha_steps.size(); ++step) {
      const auto inset = static_cast<qreal>(step) * 3.2;
      const auto frame = outer.adjusted(inset, inset, -inset, -inset);
      painter.setBrush(QColor(255, 255, 255, alpha_steps[step]));
      painter.setPen(Qt::NoPen);
      painter.drawRoundedRect(frame, frame.width() * 0.47,
                              frame.width() * 0.47);
    }
  }
  auto source_document = patchy::ui::document_from_qimage(photo, "Cyclist");
  const auto photo_layer_id = *source_document.active_layer_id();
  patchy::Layer selection_layer(
      source_document.allocate_layer_id(), "Portrait Selection Source",
      patchy::ui::pixels_from_image_rgba(selection_source));
  selection_layer.set_visible(false);
  source_document.add_layer(std::move(selection_layer));
  source_document.set_active_layer(photo_layer_id);
  patchy::ui::MainWindow window;
  show_readme_shot_window(window);
  window.add_document_session(std::move(source_document),
                              QStringLiteral("Quick Mask Portrait"));
  QApplication::processEvents();
  close_untitled_start_tab(window);
  auto* canvas = require_canvas(window);
  const auto& document =
      std::as_const(patchy::ui::MainWindowTestAccess::document(window));

  patchy::PixelBuffer selection(document.width(), document.height(),
                                patchy::PixelFormat::gray8());
  for (int y = 0; y < selection.height(); ++y) {
    auto row = selection.row(y);
    for (int x = 0; x < selection.width(); ++x) {
      row[static_cast<std::size_t>(x)] =
          static_cast<std::uint8_t>(qAlpha(selection_source.pixel(x, y)));
    }
  }
  canvas->replace_selection_from_grayscale(
      selection, QStringLiteral("Load portrait layer transparency"));
  require_hotkey_action(window, QStringLiteral("select.quick_mask"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->quick_mask_active());
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_brush_size(56);
  canvas->set_brush_softness(55);
  canvas->set_primary_color(Qt::white);
  require_action(window, "viewFitOnScreenAction")->trigger();
  auto* channels_dock = window.findChild<QWidget*>(QStringLiteral("channelsDock"));
  auto* channels = window.findChild<QListWidget*>(QStringLiteral("channelList"));
  CHECK(channels_dock != nullptr && channels != nullptr);
  patchy::ui::MainWindowTestAccess::show_panel(window, QStringLiteral("channelsDock"));
  process_events_for(400);
  CHECK(channels->findItems(QStringLiteral("Quick Mask"), Qt::MatchExactly).size() == 1);
  reset_readme_status_bar(window);
  save_readme_shot("shot_readme_quick_mask", window.grab().toImage());
}

// ===========================================================================
// Vector tools, shape appearance, and SVG import scenes (0.80)
// ===========================================================================

void readme_shape_drag(patchy::ui::CanvasWidget& canvas, QPoint document_from, QPoint document_to) {
  drag(canvas, canvas.widget_position_for_document_point(document_from),
       canvas.widget_position_for_document_point(document_to));
  QApplication::processEvents();
}

void readme_pen_click(patchy::ui::CanvasWidget& canvas, QPoint document_point) {
  const auto widget_point = canvas.widget_position_for_document_point(document_point);
  drag(canvas, widget_point, widget_point);
  QApplication::processEvents();
}

// A smooth pen anchor: press at the anchor and drag out to the handle end.
void readme_pen_smooth(patchy::ui::CanvasWidget& canvas, QPoint anchor, QPoint handle_end) {
  drag(canvas, canvas.widget_position_for_document_point(anchor),
       canvas.widget_position_for_document_point(handle_end));
  QApplication::processEvents();
}

patchy::RgbColor readme_rgb(QColor color) {
  return patchy::RgbColor{static_cast<std::uint8_t>(color.red()),
                          static_cast<std::uint8_t>(color.green()),
                          static_cast<std::uint8_t>(color.blue())};
}

patchy::LayerStyleGradient readme_vector_gradient(
    std::initializer_list<std::pair<double, QColor>> stops, patchy::LayerStyleGradientType type,
    float angle_degrees) {
  patchy::LayerStyleGradient gradient;
  gradient.type = type;
  gradient.angle_degrees = angle_degrees;
  for (const auto& [location, color] : stops) {
    patchy::GradientColorStop stop;
    stop.location = static_cast<float>(location);
    stop.color = readme_rgb(color);
    gradient.color_stops.push_back(stop);
  }
  gradient.alpha_stops.push_back({0.0F, 1.0F, 0.5F});
  gradient.alpha_stops.push_back({1.0F, 1.0F, 0.5F});
  return gradient;
}

// Vector tools: a flat sunset poster built layer by layer with the real shape
// tools - gradient-filled sky and sun, two pen-drawn mountain ridges,
// custom shapes, and a rounded gradient badge with a dashed pattern stroke.
// Direct Select and the Paths panel expose the front ridge's editable anchors.
void shot_readme_vector_tools() {
  VectorSettingsGuard vector_guard;
  SettingsValueRestorer custom_shape_restorer(QStringLiteral("tools/customShapeId"));
  SettingsValueRestorer pattern_version_restorer(QStringLiteral("patterns/defaultPatternsVersion"));
  patchy::test::register_test_fonts(patchy::test::TestFontRole::ArialBlack);
  clear_pattern_test_state();
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("patterns/defaultPatternsVersion"), 0);
    settings.sync();
  }
  patchy::ui::MainWindow window;
  show_readme_shot_window(window);
  patchy::Document poster(1180, 780, patchy::PixelFormat::rgb8());
  poster.add_pixel_layer("Background",
                         solid_pixels(1180, 780, patchy::PixelFormat::rgb8(), QColor(255, 255, 255)));
  window.add_document_session(std::move(poster), QStringLiteral("Sunset Poster"));
  QApplication::processEvents();
  close_untitled_start_tab(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* rulers_action = require_action(window, "viewToggleRulersAction");
  if (!rulers_action->isChecked()) {
    rulers_action->trigger();
  }
  require_action(window, "viewFitOnScreenAction")->trigger();
  QApplication::processEvents();

  auto& fill = patchy::ui::MainWindowTestAccess::current_vector_fill(window);
  const auto set_gradient_fill = [&](patchy::LayerStyleGradient gradient) {
    fill = patchy::VectorFill{};
    fill.kind = patchy::VectorFillKind::Gradient;
    fill.gradient = std::move(gradient);
    patchy::ui::MainWindowTestAccess::update_vector_swatch_icons(window);
  };
  const auto set_solid_fill = [&](QColor color) {
    fill = patchy::VectorFill{};
    fill.kind = patchy::VectorFillKind::Solid;
    fill.color = readme_rgb(color);
    patchy::ui::MainWindowTestAccess::update_vector_swatch_icons(window);
  };
  // Every renamed layer in this scene is a freshly committed shape layer; the
  // vector CHECK catches a failed drag (which would leave the raster
  // Background active and silently rename it instead).
  const auto rename_active = [&](const char* name) {
    const auto active = document.active_layer_id();
    CHECK(active.has_value());
    auto* layer = document.find_layer(*active);
    CHECK(layer != nullptr);
    CHECK(patchy::layer_is_vector_shape(*layer));
    layer->set_name(name);
  };

  // Sky: a near-full-canvas rectangle with a vertical sunset gradient. The
  // drag stays one pixel inside the document: a press exactly on the canvas
  // corner lands in the gutter and the shape tools ignore the gesture.
  require_action_by_text(window, QStringLiteral("Rect"))->trigger();
  QApplication::processEvents();
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  CHECK(radius_spin != nullptr);
  radius_spin->setValue(0);
  set_gradient_fill(readme_vector_gradient({{0.0, QColor(0xff, 0x8b, 0x5e)},
                                            {0.55, QColor(0x8a, 0x4a, 0x8f)},
                                            {1.0, QColor(0x23, 0x2a, 0x63)}},
                                           patchy::LayerStyleGradientType::Linear, 90.0F));
  readme_shape_drag(*canvas, QPoint(1, 1), QPoint(1179, 779));
  CHECK(document.layers().size() == 2);
  rename_active("Sky");

  // Sun: a radial-gradient ellipse low over the horizon.
  require_action_by_text(window, QStringLiteral("Ellipse"))->trigger();
  QApplication::processEvents();
  set_gradient_fill(readme_vector_gradient({{0.0, QColor(0xff, 0xf6, 0xcf)},
                                            {0.55, QColor(0xff, 0xd4, 0x65)},
                                            {1.0, QColor(0xff, 0x9a, 0x4d)}},
                                           patchy::LayerStyleGradientType::Radial, 90.0F));
  readme_shape_drag(*canvas, QPoint(430, 320), QPoint(750, 640));
  rename_active("Sun");

  // Two mountain ridges drawn with the Pen. Mid-session clicks always extend
  // the path, so only each session's FIRST click must stay clear of the
  // previously active layer's path (the pen doubles as the point editor).
  require_action_by_text(window, QStringLiteral("Pen"))->trigger();
  QApplication::processEvents();
  set_solid_fill(QColor(0x4a, 0x2b, 0x68));
  readme_pen_click(*canvas, QPoint(80, 470));
  for (const auto point : {QPoint(230, 390), QPoint(450, 560), QPoint(700, 430), QPoint(960, 560),
                           QPoint(1180, 500), QPoint(1180, 780), QPoint(0, 780), QPoint(0, 540)}) {
    readme_pen_click(*canvas, point);
  }
  readme_pen_click(*canvas, QPoint(80, 470));  // closing click commits the shape
  CHECK(!canvas->pen_session_active());
  rename_active("Back Ridge");

  set_solid_fill(QColor(0x2c, 0x17, 0x46));
  readme_pen_click(*canvas, QPoint(300, 690));
  for (const auto point : {QPoint(520, 555), QPoint(760, 660), QPoint(1000, 570), QPoint(1180, 660),
                           QPoint(1180, 780), QPoint(0, 780), QPoint(0, 655)}) {
    readme_pen_click(*canvas, point);
  }
  readme_pen_click(*canvas, QPoint(300, 690));
  CHECK(!canvas->pen_session_active());
  rename_active("Front Ridge");
  const auto front_ridge_id = *document.active_layer_id();

  // Pines, stars, and a ring moon from the built-in custom shape library; the
  // Add combine op keeps each cluster on one shape layer.
  require_action_by_text(window, QStringLiteral("Custom Shape"))->trigger();
  QApplication::processEvents();
  auto* shape_combo = window.findChild<QComboBox*>(QStringLiteral("customShapeCombo"));
  auto* combine_combo = window.findChild<QComboBox*>(QStringLiteral("vectorCombineCombo"));
  CHECK(shape_combo != nullptr && combine_combo != nullptr);
  const auto stamp_cluster = [&](const char* shape_id, QColor color, const char* name,
                                 std::initializer_list<std::pair<QPoint, QPoint>> drags) {
    const auto row = shape_combo->findData(QLatin1String(shape_id));
    CHECK(row >= 0);
    shape_combo->setCurrentIndex(row);
    QApplication::processEvents();
    set_solid_fill(color);
    bool first = true;
    for (const auto& [from, to] : drags) {
      combine_combo->setCurrentIndex(first ? 0 : 1);  // New Layer, then Add
      readme_shape_drag(*canvas, from, to);
      first = false;
    }
    combine_combo->setCurrentIndex(0);
    rename_active(name);
  };
  stamp_cluster("shape.builtin.triangle", QColor(0x1b, 0x0e, 0x33), "Pine Trees",
                {{QPoint(150, 545), QPoint(240, 700)},
                 {QPoint(255, 585), QPoint(330, 700)},
                 {QPoint(965, 575), QPoint(1050, 700)}});
  stamp_cluster("shape.builtin.star", QColor(0xff, 0xe9, 0xa8), "Stars",
                {{QPoint(390, 60), QPoint(455, 125)},
                 {QPoint(540, 140), QPoint(580, 180)},
                 {QPoint(1010, 205), QPoint(1045, 240)}});
  stamp_cluster("shape.builtin.ring", QColor(0xff, 0xe9, 0xa8), "Moon",
                {{QPoint(880, 90), QPoint(985, 195)}});

  // Add the appearance examples to the same artwork. Editing options on a
  // selected shape would restyle it, so select the raster background first.
  patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
  auto* shape_layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(shape_layers != nullptr);
  shape_layers->setCurrentItem(require_layer_item(*shape_layers, QStringLiteral("Background")));
  require_action_by_text(window, QStringLiteral("Rect"))->trigger();
  QApplication::processEvents();
  radius_spin->setValue(32);
  auto* stroke_width = window.findChild<QDoubleSpinBox*>(QStringLiteral("vectorStrokeWidthSpin"));
  CHECK(stroke_width != nullptr);
  stroke_width->setValue(8.0);
  set_gradient_fill(readme_vector_gradient({{0.0, QColor("#efaa53")}, {1.0, QColor("#fff0bd")}},
                                           patchy::LayerStyleGradientType::Linear, 90.0F));
  auto& stroke_paint = patchy::ui::MainWindowTestAccess::current_vector_stroke_paint(window);
  stroke_paint = patchy::VectorFill{};
  stroke_paint.kind = patchy::VectorFillKind::Pattern;
  stroke_paint.pattern_id = "f0705a00-000c-4c8b-9e3d-2a5b6c77e00c";
  stroke_paint.pattern_name = "Coarse Rust";
  CHECK(window.pattern_library().find_entry_by_pattern_id(
      QString::fromStdString(stroke_paint.pattern_id)) != nullptr);
  patchy::ui::MainWindowTestAccess::update_vector_swatch_icons(window);
  readme_shape_drag(*canvas, QPoint(64, 68), QPoint(344, 245));
  rename_active("Rounded badge / patterned dashes");
  const auto badge_id = *document.active_layer_id();
  auto badge_layer = patchy::take_layer_from_tree(document.layers(), badge_id);
  CHECK(badge_layer.has_value());
  document.add_layer(std::move(*badge_layer));
  patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
  std::exception_ptr appearance_error;
  bool badge_styled = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("shapeAppearanceDialog"));
    try {
      CHECK(dialog != nullptr);
      auto* dashes = dialog->findChild<QComboBox*>(QStringLiteral("shapeStrokeDashCombo"));
      CHECK(dashes != nullptr);
      dashes->setCurrentIndex(1);
      badge_styled = true;
      dialog->accept();
    } catch (...) {
      appearance_error = std::current_exception();
      if (dialog != nullptr) dialog->reject();
    }
  });
  patchy::ui::MainWindowTestAccess::edit_active_shape_appearance(window);
  if (appearance_error) std::rethrow_exception(appearance_error);
  CHECK(badge_styled);
  const auto* badge = std::as_const(document).find_layer(badge_id);
  CHECK(badge != nullptr && badge->vector_shape() != nullptr);
  CHECK(!badge->vector_shape()->stroke.dashes.empty());
  const auto label_id = add_readme_warp_text_layer(document, "GOLDEN HOUR", 99, 103, "GOLDEN\nHOUR",
      readme_text_runs(11, 38, "#4a2b68", "Arial%20Black"), 38, "#4a2b68", 0, 0, 0);
  auto* label = document.find_layer(label_id);
  CHECK(label != nullptr);
  label->layer_style().strokes.clear();
  label->layer_style().drop_shadows.clear();
  CHECK(patchy::ui::MainWindowTestAccess::apply_text_warp(window, *label, patchy::TextWarp{}));
  // Return to a shape before the Path-mode gestures below.
  document.set_active_layer(front_ridge_id);
  patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
  canvas->document_changed();

  // A cloud path drawn in Path mode and saved (double-click promotes the work
  // path), then a fresh work-path halo around the sun, so the panel lists a
  // saved path, the work path, and the active layer's shape path together.
  require_action_by_text(window, QStringLiteral("Pen"))->trigger();
  QApplication::processEvents();
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  CHECK(mode_combo != nullptr);
  mode_combo->setCurrentIndex(1);  // Path
  readme_pen_click(*canvas, QPoint(170, 200));
  readme_pen_smooth(*canvas, QPoint(350, 165), QPoint(430, 185));
  readme_pen_smooth(*canvas, QPoint(560, 205), QPoint(640, 222));
  readme_pen_click(*canvas, QPoint(790, 185));
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  auto* paths_list = window.findChild<QListWidget*>(QStringLiteral("pathsList"));
  CHECK(paths_list != nullptr);
  QListWidgetItem* work_row = nullptr;
  for (int row = 0; row < paths_list->count(); ++row) {
    if (paths_list->item(row)->text() == QStringLiteral("Work Path")) {
      work_row = paths_list->item(row);
    }
  }
  CHECK(work_row != nullptr);
  CHECK(QMetaObject::invokeMethod(paths_list, "itemDoubleClicked", Qt::DirectConnection,
                                  Q_ARG(QListWidgetItem*, work_row)));
  QApplication::processEvents();
  // The save drops the row into inline rename; commit it with an empty-space
  // click, then give the saved path its scene name through the document.
  send_mouse(*paths_list->viewport(), QEvent::MouseButtonPress,
             QPoint(paths_list->viewport()->width() / 2, paths_list->viewport()->height() - 4),
             Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(document.paths().size() == 1);
  document.find_path(document.paths().front().id())->set_name("Cloud Path");
  patchy::ui::MainWindowTestAccess::refresh_paths_panel(window);
  require_action_by_text(window, QStringLiteral("Ellipse"))->trigger();
  QApplication::processEvents();
  readme_shape_drag(*canvas, QPoint(400, 290), QPoint(780, 670));  // sun-halo work path
  mode_combo->setCurrentIndex(0);  // back to Shape mode
  QApplication::processEvents();

  // Select the front ridge and switch to Direct Select: the ridge outline and
  // anchors draw on canvas. Click the ridge's peaks directly: starting a
  // marquee over the sky now picks that shape before it starts the drag.
  patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* ridge_item = require_layer_item(*layer_list, QStringLiteral("Front Ridge"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(ridge_item);
  ridge_item->setSelected(true);
  QApplication::processEvents();
  CHECK(document.active_layer_id() == front_ridge_id);
  // The halo drawing left the Work Path row explicitly selected, which
  // outranks the transient layer row; retarget the ridge's shape path so the
  // point selection (and the canvas overlay) work on the ridge.
  paths_list->setCurrentRow(0);
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Direct Select"))->trigger();
  QApplication::processEvents();
  click_widget_like_a_user(*canvas, canvas->widget_position_for_document_point(QPoint(520, 555)));
  click_widget_like_a_user(*canvas, canvas->widget_position_for_document_point(QPoint(1000, 570)),
                          Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(document.active_layer_id() == front_ridge_id);
  CHECK(canvas->path_edit_selected_anchor_count() == 2);

  // Panels no longer float, so the Paths panel is grabbed from the column and laid
  // over the canvas, keeping the layer list visible in the same shot (the two share
  // the column's tab group).
  auto* paths_dock = window.findChild<QWidget*>(QStringLiteral("pathsDock"));
  CHECK(paths_dock != nullptr);
  patchy::ui::MainWindowTestAccess::show_panel(window, QStringLiteral("pathsDock"));
  const QPoint paths_offset(64, 615);
  process_events_for(250);
  CHECK(paths_list->count() == 3);  // Front Ridge Shape Path, Cloud Path, Work Path
  const auto paths_image = paths_dock->grab(QRect(0, 0, std::min(paths_dock->width(), 300),
                                                  std::min(paths_dock->height(), 255))).toImage();
  patchy::ui::MainWindowTestAccess::show_panel(window, QStringLiteral("layersDock"));
  process_events_for(100);

  reset_readme_status_bar(window);
  auto base = window.grab().toImage();
  draw_readme_overlay(base, paths_image, paths_offset);
  save_readme_shot("shot_readme_vector_tools", base);
  QApplication::processEvents();

  // Rulers persist to view settings on window teardown; restore the clean state.
  rulers_action->trigger();
  QApplication::processEvents();
  clear_pattern_test_state();
}

// Shape appearance: a badge composition (ring accent, gradient rounded-rect
// card, star with a Coarse Rust pattern stroke) with the Shape Appearance
// dialog open on the card - the fill switched live to the Golden Hour library
// gradient and the stroke dashed - showing the paint, stroke, and per-corner
// Geometry controls.
void shot_readme_shape_appearance() {
  VectorSettingsGuard vector_guard;
  SettingsValueRestorer custom_shape_restorer(QStringLiteral("tools/customShapeId"));
  SettingsValueRestorer polygon_sides_restorer(QStringLiteral("tools/polygonSides"));
  SettingsValueRestorer polygon_inset_restorer(QStringLiteral("tools/polygonStarInset"));
  SettingsValueRestorer pattern_version_restorer(QStringLiteral("patterns/defaultPatternsVersion"));
  // A fresh pattern library seeded with the bundled CC0 photo textures, so the
  // pattern stroke resolves the Coarse Rust preset deterministically.
  clear_pattern_test_state();
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("patterns/defaultPatternsVersion"), 0);
    settings.sync();
  }
  patchy::ui::MainWindow window;
  show_readme_shot_window(window);
  CHECK(window.pattern_library().find_entry_by_pattern_id(
            QStringLiteral("f0705a00-000c-4c8b-9e3d-2a5b6c77e00c")) != nullptr);
  window.gradient_library().restore_default_gradients();

  QImage backdrop(1180, 780, QImage::Format_RGB32);
  {
    QPainter painter(&backdrop);
    QLinearGradient shade(0, 0, 0, 780);
    shade.setColorAt(0.0, QColor(0x1a, 0x20, 0x38));
    shade.setColorAt(1.0, QColor(0x10, 0x14, 0x24));
    painter.fillRect(backdrop.rect(), shade);
    painter.setPen(QPen(QColor(255, 255, 255, 10), 1));
    for (int x = 0; x < 1180; x += 48) {
      painter.drawLine(x, 0, x, 780);
    }
    for (int y = 0; y < 780; y += 48) {
      painter.drawLine(0, y, 1180, y);
    }
  }
  window.add_document_session(patchy::ui::document_from_qimage(backdrop, "Backdrop"),
                              QStringLiteral("Badge Design"));
  QApplication::processEvents();
  close_untitled_start_tab(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  require_action(window, "viewFitOnScreenAction")->trigger();
  QApplication::processEvents();
  const auto backdrop_id = *document.active_layer_id();

  auto& fill = patchy::ui::MainWindowTestAccess::current_vector_fill(window);
  auto& stroke_paint = patchy::ui::MainWindowTestAccess::current_vector_stroke_paint(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  const auto rename_active = [&](const char* name) {
    const auto active = document.active_layer_id();
    CHECK(active.has_value());
    auto* layer = document.find_layer(*active);
    CHECK(layer != nullptr);
    CHECK(patchy::layer_is_vector_shape(*layer));
    layer->set_name(name);
    patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
  };
  // The options-bar appearance controls live-edit the selected shape layer, so
  // every stroke checkbox/width change below happens with the raster Backdrop
  // active. New shapes insert above the active layer, so drawing star, card,
  // then ring while re-selecting Backdrop stacks them ring < card < star.
  const auto select_backdrop = [&] {
    auto* item = require_layer_item(*layer_list, QStringLiteral("Backdrop"));
    layer_list->clearSelection();
    layer_list->setCurrentItem(item);
    item->setSelected(true);
    QApplication::processEvents();
    CHECK(document.active_layer_id() == backdrop_id);
  };

  // Star with a chunky Coarse Rust pattern stroke; it ends up topmost.
  require_action_by_text(window, QStringLiteral("Polygon"))->trigger();
  QApplication::processEvents();
  auto* sides_spin = window.findChild<QSpinBox*>(QStringLiteral("polygonSidesSpin"));
  auto* inset_spin = window.findChild<QSpinBox*>(QStringLiteral("polygonStarInsetSpin"));
  auto* stroke_width = window.findChild<QDoubleSpinBox*>(QStringLiteral("vectorStrokeWidthSpin"));
  CHECK(sides_spin != nullptr && inset_spin != nullptr && stroke_width != nullptr);
  sides_spin->setValue(5);
  inset_spin->setValue(45);
  stroke_width->setValue(12.0);
  fill = patchy::VectorFill{};
  fill.kind = patchy::VectorFillKind::Solid;
  fill.color = readme_rgb(QColor(0xff, 0xd2, 0x57));
  stroke_paint = patchy::VectorFill{};
  stroke_paint.kind = patchy::VectorFillKind::Pattern;
  stroke_paint.pattern_id = "f0705a00-000c-4c8b-9e3d-2a5b6c77e00c";  // Coarse Rust
  stroke_paint.pattern_name = "Coarse Rust";
  patchy::ui::MainWindowTestAccess::update_vector_swatch_icons(window);
  readme_shape_drag(*canvas, QPoint(630, 560), QPoint(630, 430));  // center-out drag
  rename_active("Star Burst");

  // The badge card: a rounded rectangle (live shape, so the dialog grows its
  // Geometry section) with a warm fill and a deep plum stroke, under the star.
  select_backdrop();
  require_action_by_text(window, QStringLiteral("Rect"))->trigger();
  QApplication::processEvents();
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  CHECK(radius_spin != nullptr);
  radius_spin->setValue(36);
  stroke_width->setValue(8.0);
  fill = patchy::VectorFill{};
  fill.kind = patchy::VectorFillKind::Solid;
  fill.color = readme_rgb(QColor(0xc9, 0x6a, 0x3f));
  stroke_paint = patchy::VectorFill{};
  stroke_paint.kind = patchy::VectorFillKind::Solid;
  stroke_paint.color = readme_rgb(QColor(0x4a, 0x1f, 0x3c));
  patchy::ui::MainWindowTestAccess::update_vector_swatch_icons(window);
  readme_shape_drag(*canvas, QPoint(120, 140), QPoint(680, 600));
  rename_active("Badge Card");
  const auto card_id = *document.active_layer_id();

  // Ring accent tucked behind the card's top-left corner.
  select_backdrop();
  require_action_by_text(window, QStringLiteral("Custom Shape"))->trigger();
  QApplication::processEvents();
  auto* shape_combo = window.findChild<QComboBox*>(QStringLiteral("customShapeCombo"));
  CHECK(shape_combo != nullptr);
  const auto ring_row = shape_combo->findData(QStringLiteral("shape.builtin.ring"));
  CHECK(ring_row >= 0);
  shape_combo->setCurrentIndex(ring_row);
  QApplication::processEvents();
  stroke_width->setValue(0.0);  // no stroke (issue 311: the width is the switch)
  fill = patchy::VectorFill{};
  fill.kind = patchy::VectorFillKind::Solid;
  fill.color = readme_rgb(QColor(0x3d, 0x4a, 0x7d));
  patchy::ui::MainWindowTestAccess::update_vector_swatch_icons(window);
  readme_shape_drag(*canvas, QPoint(80, 70), QPoint(230, 220));
  rename_active("Ring");

  // Open the Shape Appearance dialog on the card and restyle it live: Golden
  // Hour gradient fill, dashed stroke. The preview rasterizes on a background
  // worker, so the capture waits for the card's pixels to actually change.
  auto* card_item = require_layer_item(*layer_list, QStringLiteral("Badge Card"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(card_item);
  card_item->setSelected(true);
  QApplication::processEvents();
  CHECK(document.active_layer_id() == card_id);
  const QPoint card_sample(350, 350);  // clear of the star and the ring
  const auto before_color = canvas_pixel(*canvas, card_sample);

  bool captured = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("shapeAppearanceDialog"));
    CHECK(dialog != nullptr);
    auto* fill_kind_combo = dialog->findChild<QComboBox*>(QStringLiteral("shapeFillKindCombo"));
    auto* gradient_combo = dialog->findChild<QComboBox*>(QStringLiteral("shapeFillGradientCombo"));
    auto* type_combo = dialog->findChild<QComboBox*>(QStringLiteral("shapeGradientTypeCombo"));
    auto* angle_spin = dialog->findChild<QSpinBox*>(QStringLiteral("shapeGradientAngleSpin"));
    auto* dash_combo = dialog->findChild<QComboBox*>(QStringLiteral("shapeStrokeDashCombo"));
    CHECK(fill_kind_combo != nullptr && gradient_combo != nullptr && type_combo != nullptr &&
          angle_spin != nullptr && dash_combo != nullptr);
    const auto gradient_kind_row = fill_kind_combo->findText(QObject::tr("Gradient"));
    CHECK(gradient_kind_row >= 0);
    fill_kind_combo->setCurrentIndex(gradient_kind_row);
    const auto golden_row = gradient_combo->findText(QStringLiteral("Golden Hour"));
    CHECK(golden_row >= 0);
    gradient_combo->setCurrentIndex(golden_row);
    type_combo->setCurrentIndex(
        type_combo->findData(static_cast<int>(patchy::LayerStyleGradientType::Linear)));
    angle_spin->setValue(90);
    dash_combo->setCurrentIndex(1);  // Dashed
    QApplication::processEvents();
    const QPoint dialog_offset(440, 60);  // the two-column dialog is ~800 px wide
    dialog->move(window.geometry().topLeft() + dialog_offset);
    QApplication::processEvents();
    CHECK(process_events_until(
        [&] { return !color_close(canvas_pixel(*canvas, card_sample), before_color, 8); }, 20000));
    process_events_for(400);  // let the coalesced dash re-render land too
    reset_readme_status_bar(window);
    auto base = window.grab().toImage();
    draw_readme_overlay(base, dialog->grab().toImage(), dialog_offset);
    save_readme_shot("shot_readme_shape_appearance", base);
    captured = true;
    dialog->accept();
  });
  patchy::ui::MainWindowTestAccess::edit_active_shape_appearance(window);
  QApplication::processEvents();
  CHECK(captured);
  clear_pattern_test_state();
}

// Depth-first walk sharing one best-candidate state, so a nested group's
// local runner-up can never overwrite the global winner's folder chain.
void readme_collect_largest_editable_shape(const std::vector<patchy::Layer>& layers,
                                           double maximum_width, std::vector<QString>& chain,
                                           const patchy::Layer*& best, double& best_area,
                                           std::vector<QString>& best_chain) {
  for (const auto& layer : layers) {
    if (layer.kind() == patchy::LayerKind::Group) {
      chain.push_back(QString::fromStdString(layer.name()));
      readme_collect_largest_editable_shape(layer.children(), maximum_width, chain, best, best_area,
                                            best_chain);
      chain.pop_back();
      continue;
    }
    if (!patchy::layer_is_vector_shape(layer) || layer.vector_shape() == nullptr) {
      continue;
    }
    std::size_t anchors = 0;
    for (const auto& subpath : layer.vector_shape()->path.subpaths) {
      anchors += subpath.anchors.size();
    }
    const auto area = static_cast<double>(layer.bounds().width) * layer.bounds().height;
    if (anchors >= 8 && layer.bounds().width < maximum_width && area > best_area) {
      best = &layer;
      best_area = area;
      best_chain = chain;
      best_chain.push_back(QString::fromStdString(layer.name()));
    }
  }
}

// SVG import: the CC0 hot-air-balloon clip art opens as editable shape layers -
// groups become folders, rows carry the vector badge - and Direct Select shows
// the largest balloon envelope's bezier anchors right on the canvas.
void shot_readme_svg_import() {
  VectorSettingsGuard vector_guard;
  const auto path =
      patchy::test::committed_format_fixture_path("svg", "hot_air_balloons_cc0.svg");
  CHECK(std::filesystem::exists(path));
  patchy::ui::MainWindow window;
  show_readme_shot_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(
      window, QString::fromStdString(path.string()));
  QApplication::processEvents();
  close_untitled_start_tab(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.width() == 1920);
  patchy::ui::MainWindowTestAccess::set_right_dock_stack_width(window, 380);
  require_action(window, "viewFitOnScreenAction")->trigger();
  QApplication::processEvents();

  // The largest editable shape narrower than 40% of the canvas: the sky,
  // ground, and lake span the full width, so the winner is the big central
  // balloon's outline path. The walk also records its folder chain.
  std::vector<QString> folder_chain;
  std::vector<QString> best_chain;
  const patchy::Layer* envelope = nullptr;
  double envelope_area = 0.0;
  readme_collect_largest_editable_shape(std::as_const(document).layers(), document.width() * 0.4,
                                        folder_chain, envelope, envelope_area, best_chain);
  CHECK(envelope != nullptr);
  CHECK(!best_chain.empty());

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  for (std::size_t depth = 0; depth + 1 < best_chain.size(); ++depth) {
    expand_layer_folder_row(*layer_list, best_chain[depth]);
  }
  auto* envelope_item = require_layer_item(*layer_list, best_chain.back());
  layer_list->clearSelection();
  layer_list->setCurrentItem(envelope_item);
  envelope_item->setSelected(true);
  QApplication::processEvents();

  // Direct Select shows the envelope's anchors. Alt-click an actual anchor
  // to select its subpath without picking another shape behind the balloon.
  require_action_by_text(window, QStringLiteral("Direct Select"))->trigger();
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  const auto envelope_id = envelope->id();
  const auto* target_path = canvas->path_edit_target_path();
  CHECK(target_path != nullptr && !target_path->subpaths.empty());
  CHECK(!target_path->subpaths.front().anchors.empty());
  const auto anchor = target_path->subpaths.front().anchors.front();
  click_widget_like_a_user(*canvas, canvas->widget_position_for_document_point(
      QPoint(qRound(anchor.anchor_x), qRound(anchor.anchor_y))), Qt::AltModifier);
  process_events_for(250);
  CHECK(document.active_layer_id() == envelope_id);
  CHECK(canvas->path_edit_selected_anchor_count() >= 8);

  // Scroll the list to its top so the group-to-folder story is visible: the
  // outer folder rows lead the panel while the balloon's anchors carry the
  // editability story on canvas.
  auto* outer_folder_item = require_layer_item(*layer_list, best_chain.front());
  layer_list->scrollToItem(outer_folder_item, QAbstractItemView::PositionAtTop);
  QApplication::processEvents();

  reset_readme_status_bar(window);
  save_readme_shot("shot_readme_svg_import", window.grab().toImage());
}

}  // namespace

std::vector<patchy::test::TestCase> readme_screenshot_tests_part1() {
  return {
      {"visual_contact_sheet_contains_new_feature_artifacts", visual_contact_sheet_contains_new_feature_artifacts},
      {"shot_readme_levels", shot_readme_levels},
      {"shot_readme_layer_styles", shot_readme_layer_styles},
      {"shot_readme_brush_tips", shot_readme_brush_tips},
      {"shot_readme_brush_dynamics", shot_readme_brush_dynamics},
      {"shot_readme_palette_mode", shot_readme_palette_mode},
      {"shot_readme_image_trace", shot_readme_image_trace},
      {"shot_readme_warp_text", shot_readme_warp_text},
      {"shot_readme_tile_preview", shot_readme_tile_preview},
      {"shot_readme_smart_objects", shot_readme_smart_objects},
      {"shot_readme_pattern_manager", shot_readme_pattern_manager},
      {"shot_readme_smart_filters", shot_readme_smart_filters},
      {"shot_readme_tilt_shift", shot_readme_tilt_shift},
      {"shot_readme_gmic", shot_readme_gmic},
      {"shot_readme_material_styles", shot_readme_material_styles},
      {"shot_readme_camera_raw", shot_readme_camera_raw},
      {"shot_readme_quick_mask", shot_readme_quick_mask},
      {"shot_readme_vector_tools", shot_readme_vector_tools},
      {"shot_readme_shape_appearance", shot_readme_shape_appearance},
      {"shot_readme_svg_import", shot_readme_svg_import},
      {"shot_readme_long_shadow", shot_readme_long_shadow},
  };
}
