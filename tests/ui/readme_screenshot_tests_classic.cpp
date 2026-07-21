#include "ui/canvas_widget.hpp"
#include "core/adjustment_layer.hpp"
#include "core/contour_presets.hpp"
#include "core/document_path.hpp"
#include "core/vector_shape.hpp"
#include "core/gradient_presets.hpp"
#include "core/layer_metadata.hpp"
#include "core/pattern_presets.hpp"
#include "core/smart_filter.hpp"
#include "core/smart_filter_effects.hpp"
#include "core/smart_object.hpp"
#include "core/text_warp.hpp"
#include "ui/smart_object_render.hpp"
#include "core/layer_tree.hpp"
#include "core/palette.hpp"
#include "core/palette_presets.hpp"
#include "ui/palette_panel.hpp"
#include "ui/pattern_library.hpp"
#include "ui/pattern_manager_dialog.hpp"
#include "ui/photo_pattern_presets.hpp"
#include "ui/style_browser.hpp"
#include "ui/style_library.hpp"
#include "ui/style_manager_dialog.hpp"
#include "psd/asl_io.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_layer_effects.hpp"
#include "core/style_presets.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/brush_tip_manager_dialog.hpp"
#include "ui/brush_tip_picker.hpp"
#include "ui/blend_if_range_editor.hpp"
#include "ui/color_panel.hpp"
#include "ui/default_brush_tips.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/document_float_window.hpp"
#include "ui/compatibility_report.hpp"
#include "ui/curves_editor.hpp"
#include "ui/curves_presets.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/filter_look_library.hpp"
#include "ui/font_picker.hpp"
#include "ui/gradient_stops_editor.hpp"
#include "ui/gradient_library.hpp"
#include "ui/gradient_manager_dialog.hpp"
#include "formats/acv_curves_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/tga_document_io.hpp"
#include "ui/image_document_io.hpp"
#include "ui/image_save_options_dialog.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/localization.hpp"
#include "ui/main_window.hpp"
#include "ui/print_dialog.hpp"
#include "ui/selection_outline.hpp"
#include "ui/sprite_sheet_dialog.hpp"
#include "ui/splash_dialog.hpp"
#include "ui/app_settings.hpp"
#include "ui/update_checker.hpp"
#include "ui/visual_filter_gallery_dialog.hpp"
#include "ui/zoomable_image_preview.hpp"
#include "ui/zoom_status_bar.hpp"
#include "filters/builtin_filters.hpp"
#include "psd/psd_document_io.hpp"
#include "psd/psd_filter_effects.hpp"
#include "render/compositor.hpp"
#include "synthetic_dng.hpp"
#include "test_fonts.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"

#include <QAbstractItemModel>
#include <QAbstractSpinBox>
#include <QAbstractItemView>
#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QByteArray>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDataStream>
#include <QDockWidget>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFrame>
#include <QGroupBox>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QInputDevice>
#include <QInputDialog>
#include <QKeyEvent>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListView>
#include <QLayout>
#include <QListWidget>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QLocale>
#include <QSizeGrip>
#include <QMetaObject>
#include <QMouseEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QMessageBox>
#include <QIODevice>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPolygonF>
#include <QThread>
#include <QPaintEvent>
#include <QPixmap>
#include <QPointingDevice>
#include <QProgressDialog>
#include <QPushButton>
#include <QStackedWidget>
#include <QRadioButton>
#include <QSpinBox>
#include <QStringList>
#include <QScrollBar>
#include <QScreen>
#include <QSettings>
#include <QSlider>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QStyleOptionSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTabletEvent>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextLayout>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVariant>
#include <QWheelEvent>
#include <QWindow>
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
      "ui_background_eraser_transparency.png",
      "ui_inline_text_editor.png",
      "ui_text_tool_layer.png",
      "ui_transformed_text_reedit.png",
      "format_alpha.png",
      "ui_image_adjustments_invert_desaturate.png",
      "ui_image_adjustments_auto_contrast.png",
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
      "filter_auto_contrast.bmp",
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

// Photo-editing hero: the Okinawa cycling photo with rulers and grid on, the
// Levels dialog's live histogram floating over the canvas, and the color
// picker on the left in Wheel mode with its swatch grid on DOS / VGA 256. The
// palette dropdown stays closed: it would cover the swatches, and the
// palette_mode shot already shows a preset dropdown in action.
void shot_readme_levels() {
  const auto path = patchy::test::local_psd_fixture_path("akiko_cycling_okinawa.jpg");
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] akiko_cycling_okinawa fixture missing: " << path.string() << '\n';
    return;
  }
  QImage photo(QString::fromStdString(path.string()));
  CHECK(!photo.isNull());
  // The picker tab and palette-dropdown choice persist app-wide; scope them to
  // this scene so the other shots (and later suite runs) pick their own.
  SettingsValueRestorer picker_tab_restorer(QStringLiteral("colorPanel/lastTab"));
  SettingsValueRestorer palette_choice_restorer(QStringLiteral("palettes/lastPaletteChoice"));
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
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] ipad_main_v04 fixture missing: " << path.string() << '\n';
    return;
  }
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

// Bitmap brush tips: strokes painted with several of the built-in tips, with
// the tip picker popup open over the canvas.
void shot_readme_brush_tips() {
  seed_default_brush_tips_for_readme_shot();
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

  auto& library = window.brush_tip_library();
  struct StrokeSpec {
    const char* tip;
    QColor color;
    int size;
    double y_center;
    double amplitude;
    double cycles;
  };
  const StrokeSpec strokes[] = {
      {"Chalk", QColor(0x2f, 0x6f, 0xb2), 56, 120.0, 44.0, 1.5},
      {"Chain", QColor(0x63, 0x6d, 0x7a), 44, 250.0, 40.0, 1.5},
      {"Leaf", QColor(0xc9, 0x6a, 0x1e), 52, 380.0, 42.0, 1.5},
      {"Spatter", QColor(0x8e, 0x44, 0xad), 56, 500.0, 38.0, 1.5},
      {"Stitches", QColor(0xc0, 0x39, 0x2b), 42, 610.0, 34.0, 1.5},
      {"Grass", QColor(0x3f, 0x8f, 0x3f), 60, 715.0, 8.0, 0.5},
  };
  for (const auto& stroke : strokes) {
    window.set_active_brush_tip(readme_tip_id_by_name(library, QString::fromLatin1(stroke.tip)), false);
    canvas->set_brush_size(stroke.size);
    canvas->set_primary_color(stroke.color);
    paint_readme_polyline(*canvas,
                          readme_wave_points(90.0, 1090.0, stroke.y_center, stroke.amplitude, stroke.cycles));
  }

  auto* picker = window.findChild<patchy::ui::BrushTipPicker*>(QStringLiteral("brushTipPicker"));
  CHECK(picker != nullptr);
  picker->click();
  QApplication::processEvents();
  QWidget* popup = nullptr;
  for (auto* widget : QApplication::topLevelWidgets()) {
    if (widget->objectName() == QStringLiteral("brushTipPickerPopup") && widget->isVisible()) {
      popup = widget;
    }
  }
  CHECK(popup != nullptr);
  popup->resize(600, 470);
  // Place the popup where it would drop from the options-bar Tip button; the
  // offscreen platform's small virtual screen otherwise clamps it elsewhere.
  const QPoint popup_offset(620, 86);
  popup->move(window.geometry().topLeft() + popup_offset);
  QApplication::processEvents();

  reset_readme_status_bar(window);
  auto base = window.grab().toImage();
  draw_readme_overlay(base, popup->grab().toImage(), popup_offset);
  save_readme_shot("shot_readme_brush_tips", base);
  popup->close();
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
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] akiko_cycling_okinawa fixture missing: " << path.string() << '\n';
    return;
  }
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

// Hue/Saturation with live preview: the fire layer of the game art shifted to
// electric blue while the dialog floats over the canvas.
void shot_readme_hue_saturation() {
  const auto path = patchy::test::local_psd_fixture_path("ipad_main_v04.psd");
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] ipad_main_v04 fixture missing: " << path.string() << '\n';
    return;
  }
  patchy::ui::MainWindow window;
  show_readme_shot_window(window);
  window.add_document_session(patchy::psd::DocumentIo::read_file(path), QStringLiteral("ipad_main_v04.psd"));
  QApplication::processEvents();
  close_untitled_start_tab(window);

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  expand_layer_folder_row(*layer_list, QStringLiteral("Fire"));
  auto* fire_item = require_layer_item(*layer_list, QStringLiteral("fire_04"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(fire_item);
  fire_item->setSelected(true);
  QApplication::processEvents();

  bool captured = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyHueSaturationDialog"));
    CHECK(dialog != nullptr);
    auto* hue = dialog->findChild<QSpinBox*>(QStringLiteral("hueSaturationHueSpin"));
    auto* preview = dialog->findChild<QCheckBox*>(QStringLiteral("hueSaturationPreviewCheck"));
    CHECK(hue != nullptr);
    CHECK(preview != nullptr);
    CHECK(preview->isChecked());
    hue->setValue(150);
    process_events_for(400);  // let the coalesced live preview land on the canvas

    const QPoint dialog_offset(170, 320);
    dialog->move(window.geometry().topLeft() + dialog_offset);
    QApplication::processEvents();
    reset_readme_status_bar(window);
    auto base = window.grab().toImage();
    draw_readme_overlay(base, dialog->grab().toImage(), dialog_offset);
    save_readme_shot("shot_readme_hue_saturation", base);
    captured = true;
    dialog->reject();
  });
  require_action(window, "imageAdjustHueSaturationAction")->trigger();
  QApplication::processEvents();
  CHECK(captured);
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

// Warp Text: a synthwave poster whose headline arcs through the Warp Text
// dialog's live preview (style dropdown open on all 15 styles), over three
// smaller words each pre-warped with the style they name — every text layer
// rich-colored and styled, showing text + warps + layer styles composing.
void shot_readme_warp_text() {
  patchy::test::register_test_fonts(patchy::test::TestFontRole::ArialBlack);
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  patchy::ui::MainWindow window;
  show_readme_shot_window(window);

  QImage poster(1180, 780, QImage::Format_RGB32);
  {
    QPainter painter(&poster);
    QLinearGradient sky(0, 0, 0, poster.height());
    sky.setColorAt(0.00, QColor(0x1b, 0x14, 0x3c));
    sky.setColorAt(0.45, QColor(0x51, 0x2b, 0x62));
    sky.setColorAt(0.72, QColor(0xb4, 0x4a, 0x6b));
    sky.setColorAt(1.00, QColor(0xf2, 0x9a, 0x5e));
    painter.fillRect(poster.rect(), sky);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    for (int i = 0; i < 60; ++i) {  // fixed-hash stars in the dark upper sky
      const auto hash = static_cast<std::uint32_t>(i) * 2654435761u;
      const auto star_x = static_cast<int>(hash % 1180u);
      const auto star_y = static_cast<int>((hash / 1180u) % 320u);
      painter.setBrush(QColor(255, 255, 255, 90 + static_cast<int>((hash >> 16) % 130u)));
      const auto radius = 0.8 + ((hash >> 8) % 3u) * 0.5;
      painter.drawEllipse(QPointF(star_x, star_y), radius, radius);
    }
    QPainterPath sun;
    sun.addEllipse(QPointF(700, 650), 190, 190);
    QPainterPath slits;  // the retro banded-sun look, above the near ridge line
    int slit_y = 575;
    int slit_thickness = 5;
    for (int i = 0; i < 4; ++i) {
      slits.addRect(500, slit_y, 400, slit_thickness);
      slit_y += 28 + i * 6;
      slit_thickness += 4;
    }
    QLinearGradient sun_fill(0, 460, 0, 840);
    sun_fill.setColorAt(0.0, QColor(0xff, 0xe9, 0xb0));
    sun_fill.setColorAt(1.0, QColor(0xff, 0x7d, 0x52));
    painter.fillPath(sun.subtracted(slits), sun_fill);
    // The far range dips behind the sun so the slit bands stay visible.
    painter.setBrush(QColor(0x2a, 0x17, 0x46));
    painter.drawPolygon(QPolygonF({{0, 640}, {150, 520}, {300, 655}, {430, 560}, {565, 675}, {700, 690},
                                   {860, 675}, {985, 540}, {1180, 650}, {1180, 780}, {0, 780}}));
    painter.setBrush(QColor(0x1c, 0x0f, 0x33));
    painter.drawPolygon(QPolygonF(
        {{0, 700}, {185, 605}, {365, 722}, {545, 645}, {760, 730}, {945, 660}, {1180, 718}, {1180, 780}, {0, 780}}));
  }
  auto built = patchy::ui::document_from_qimage(poster, "Background");
  const char* black_font = "Arial%20Black";
  const auto flag_id = add_readme_warp_text_layer(built, "Flag", 180, 320, "Flag",
                                                  readme_text_runs(4, 56, "#f2f2f2", black_font), 56,
                                                  "#f2f2f2", 3.0F, 6.0F, 8.0F);
  const auto fisheye_id = add_readme_warp_text_layer(built, "Fisheye", 390, 320, "Fisheye",
                                                     readme_text_runs(7, 56, "#ffd54f", black_font), 56,
                                                     "#ffd54f", 3.0F, 6.0F, 8.0F);
  const auto twist_id = add_readme_warp_text_layer(built, "Twist", 655, 320, "Twist",
                                                   readme_text_runs(5, 56, "#ff8a65", black_font), 56,
                                                   "#ff8a65", 3.0F, 6.0F, 8.0F);
  const auto headline_id = add_readme_warp_text_layer(
      built, "PATCHY", 220, 140, "PATCHY",
      readme_rainbow_runs({"#ff6f61", "#ffb74d", "#ffe66d", "#7ee081", "#64b5f6", "#b388ff"}, 110, black_font),
      110, "#ffe66d", 5.0F, 10.0F, 14.0F);
  built.set_active_layer(headline_id);
  window.add_document_session(std::move(built), QStringLiteral("Warp Text"));
  QApplication::processEvents();
  close_untitled_start_tab(window);
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  // The Type options bar starts on the new-text defaults; point it at the
  // headline's face so the shot reads coherently. Neither control persists
  // (only an open inline editor consumes them).
  auto* font_combo = window.findChild<QFontComboBox*>(QStringLiteral("textFontCombo"));
  auto* size_spin = window.findChild<QDoubleSpinBox*>(QStringLiteral("textSizeSpin"));
  CHECK(font_combo != nullptr && size_spin != nullptr);
  // setCurrentFont resolves through QFontInfo and can land on the base family;
  // select the model row by name instead.
  const auto black_row = font_combo->findText(QStringLiteral("Arial Black"));
  if (black_row >= 0) {
    font_combo->setCurrentIndex(black_row);
  }
  size_spin->setValue(110.0);
  QApplication::processEvents();

  // Identity warp = the committed unwarped render (glyphs through the real text
  // pipeline); then bake each small word's namesake style. The headline stays
  // unwarped so the dialog's live preview is what arcs it in the shot.
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  for (const auto id : {headline_id, flag_id, fisheye_id, twist_id}) {
    auto* layer = document.find_layer(id);
    CHECK(layer != nullptr);
    CHECK(patchy::ui::MainWindowTestAccess::apply_text_warp(window, *layer, patchy::TextWarp{}));
  }
  const struct {
    patchy::LayerId id;
    const char* style;
    double bend;
  } word_warps[] = {{flag_id, "warpFlag", 45.0}, {fisheye_id, "warpFisheye", 55.0}, {twist_id, "warpTwist", 60.0}};
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

  bool captured = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("warpTextDialog"));
    CHECK(dialog != nullptr);
    auto* style_combo = dialog->findChild<QComboBox*>(QStringLiteral("warpTextStyleCombo"));
    auto* bend_spin = dialog->findChild<QSpinBox*>(QStringLiteral("warpTextBendSpin"));
    CHECK(style_combo != nullptr && bend_spin != nullptr);
    style_combo->setCurrentIndex(style_combo->findData(QStringLiteral("warpArc")));
    bend_spin->setValue(42);
    process_events_for(300);  // let the live preview re-render the headline
    // Over the empty starfield right of the headline: the style dropdown hangs
    // over sky the words deliberately stop short of, keeping the dialog, the
    // canvas text, and the Layers panel all visible at once.
    const QPoint dialog_offset(920, 130);
    dialog->move(window.geometry().topLeft() + dialog_offset);
    QApplication::processEvents();
    style_combo->showPopup();
    QApplication::processEvents();
    auto* popup = style_combo->view()->window();
    CHECK(popup != nullptr);
    CHECK(popup->isVisible());
    reset_readme_status_bar(window);
    auto base = window.grab().toImage();
    draw_readme_overlay(base, dialog->grab().toImage(), dialog_offset);
    const auto popup_offset =
        dialog_offset + style_combo->mapTo(dialog, QPoint(0, style_combo->height() + 1));
    draw_readme_overlay(base, popup->grab().toImage(), popup_offset);
    save_readme_shot("shot_readme_warp_text", base);
    captured = true;
    style_combo->hidePopup();
    dialog->reject();
  });
  patchy::ui::MainWindowTestAccess::request_warp_text_dialog(window);
  QApplication::processEvents();
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
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] mow_master fixture missing: " << path.string() << '\n';
    return;
  }
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

// A deterministic, code-authored city scene for the spatial filter shots. It
// gives Tilt-Shift Blur strong near/middle/far detail without adding a licensed
// photo fixture to the repository.
[[maybe_unused]] QImage readme_isometric_city() {
  QImage city(1100, 720, QImage::Format_RGB32);
  QPainter painter(&city);
  painter.setRenderHint(QPainter::Antialiasing, true);
  QLinearGradient sky(0, 0, 0, city.height());
  sky.setColorAt(0.0, QColor(0x71, 0xb8, 0xd8));
  sky.setColorAt(0.42, QColor(0xd7, 0xe9, 0xe8));
  sky.setColorAt(1.0, QColor(0xe8, 0xc9, 0x9e));
  painter.fillRect(city.rect(), sky);
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(0x5b, 0x8f, 0x98));
  painter.drawPolygon(QPolygonF({{0, 250}, {180, 180}, {330, 245}, {500, 155},
                                 {690, 250}, {880, 165}, {1100, 260},
                                 {1100, 390}, {0, 390}}));
  painter.setBrush(QColor(0x8e, 0xb5, 0xa4));
  painter.drawPolygon(QPolygonF({{0, 310}, {205, 230}, {390, 325}, {590, 220},
                                 {805, 320}, {970, 250}, {1100, 315},
                                 {1100, 450}, {0, 450}}));
  painter.setBrush(QColor(0x6f, 0x92, 0x87));
  painter.drawRect(0, 370, 1100, 350);

  // Road grid, kept broad so it remains legible after the gallery proxy scales.
  painter.setBrush(QColor(0x4b, 0x58, 0x5c));
  for (int y : {430, 555, 675}) {
    painter.drawPolygon(QPolygonF({QPointF(-40, y - 18), QPointF(1100, y - 58),
                                   QPointF(1140, y - 18), QPointF(0, y + 22)}));
  }
  for (int x : {155, 390, 650, 900}) {
    painter.drawPolygon(QPolygonF({QPointF(x - 23, 360), QPointF(x + 32, 360),
                                   QPointF(x + 105, 720), QPointF(x + 30, 720)}));
  }
  painter.setPen(QPen(QColor(0xf1, 0xd4, 0x72), 3, Qt::DashLine));
  painter.drawLine(0, 664, 1100, 624);
  painter.setPen(Qt::NoPen);

  const std::array<QColor, 6> fronts = {
      QColor(0xd5, 0x75, 0x5f), QColor(0xd8, 0xb1, 0x66),
      QColor(0x78, 0xa7, 0xae), QColor(0x9c, 0x83, 0xaa),
      QColor(0x72, 0x9c, 0x78), QColor(0xcf, 0x8e, 0x68)};
  int building = 0;
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 7; ++column) {
      const int x = 40 + column * 150 + row * 26;
      const int base_y = 495 + row * 95 - column * 5;
      const int width = 76 + ((column + row) % 3) * 12;
      const int height = 62 + ((column * 37 + row * 53) % 120);
      const int depth = 22;
      const auto front = fronts[static_cast<std::size_t>(building++) % fronts.size()];
      const auto side = front.darker(132);
      const auto roof = front.lighter(132);
      painter.setBrush(front);
      painter.drawRect(x, base_y - height, width, height);
      painter.setBrush(side);
      painter.drawPolygon(QPolygonF({QPointF(x + width, base_y - height),
                                     QPointF(x + width + depth, base_y - height - depth / 2),
                                     QPointF(x + width + depth, base_y - depth / 2),
                                     QPointF(x + width, base_y)}));
      painter.setBrush(roof);
      painter.drawPolygon(QPolygonF({QPointF(x, base_y - height),
                                     QPointF(x + depth, base_y - height - depth / 2),
                                     QPointF(x + width + depth, base_y - height - depth / 2),
                                     QPointF(x + width, base_y - height)}));
      painter.setBrush(QColor(0xf4, 0xdf, 0x9a, 210));
      const int window_rows = std::max(1, (height - 22) / 24);
      for (int window_row = 0; window_row < window_rows; ++window_row) {
        for (int window_column = 0; window_column < 3; ++window_column) {
          painter.drawRect(x + 10 + window_column * 21,
                           base_y - height + 14 + window_row * 23, 10, 9);
        }
      }
    }
  }
  // Foreground cars and trees create small details for the blur transition.
  for (int index = 0; index < 10; ++index) {
    const int x = 32 + index * 108;
    const int y = 650 - (index % 3) * 13;
    painter.setBrush(index % 2 == 0 ? QColor(0xe8, 0x58, 0x4f)
                                    : QColor(0xf3, 0xc7, 0x49));
    painter.drawRoundedRect(x, y, 34, 14, 4, 4);
    painter.setBrush(QColor(0x26, 0x2b, 0x2d));
    painter.drawEllipse(QRect(x + 4, y + 10, 8, 8));
    painter.drawEllipse(QRect(x + 23, y + 10, 8, 8));
  }
  painter.end();
  return city;
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

// Tilt-Shift Blur over a dense city scene, with its safe short-grip overlay
// visible in the live gallery preview.
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

// Smart Filters: convert a photo layer, build a native three-filter recipe,
// and expose the shared mask plus editable stack in the Layers panel.
void shot_readme_smart_filters() {
  const auto path =
      patchy::test::local_psd_fixture_path("akiko_cycling_okinawa.jpg");
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] akiko_cycling_okinawa fixture missing: "
              << path.string() << '\n';
    return;
  }
  QImage photo(QString::fromStdString(path.string()));
  CHECK(!photo.isNull());
  photo = photo.copy(0, 170, photo.width(), std::min(1180, photo.height() - 170))
              .scaled(900, 760, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  patchy::ui::MainWindow window;
  show_readme_shot_window(window);
  window.add_document_session(patchy::ui::document_from_qimage(photo, "Cyclist"),
                              QStringLiteral("Editable Smart Filters"));
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
      row[static_cast<std::size_t>(x)] = static_cast<std::uint8_t>(
          std::clamp((1.08 - distance) * 900.0, 0.0, 255.0));
    }
  }
  canvas->replace_selection_from_grayscale(
      selection, QStringLiteral("Smart Filter portrait mask"));
  window.repaint();
  process_events_for(500);
  const auto header = window.grab().toImage().copy(0, 0, window.width(), 82);

  bool applied = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("filterGalleryDialog"));
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
  });
  require_action(window, "filterGalleryAction")->trigger();
  CHECK(applied);
  CHECK(process_events_until(
      [&] {
        return window.isEnabled() && !canvas->processing_operation_active() &&
               !canvas->processing_overlay_visible();
      },
      15000));
  canvas->clear_selection();
  patchy::ui::MainWindowTestAccess::set_right_dock_stack_width(window, 380);
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
  auto image = window.grab().toImage();
  {
    QPainter painter(&image);
    painter.drawImage(0, 0, header);
  }
  save_readme_shot("shot_readme_smart_filters", image);
}

// Camera Raw: a real CC0 raw.pixls.us sample in the full develop dialog.
void shot_readme_camera_raw() {
  const auto path = patchy::test::local_format_fixture_path(
      "raw", "fujifilm_xt1.raf");
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] CC0 Fujifilm raw fixture missing: " << path.string()
              << '\n';
    return;
  }
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
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] akiko_cycling_okinawa fixture missing: "
              << path.string() << '\n';
    return;
  }
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
  auto* channels_dock =
      window.findChild<QDockWidget*>(QStringLiteral("channelsDock"));
  auto* channels = window.findChild<QListWidget*>(QStringLiteral("channelList"));
  CHECK(channels_dock != nullptr && channels != nullptr);
  channels_dock->show();
  channels_dock->raise();
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
// custom-shape pines, stars, and a ring moon - captured with Direct Select
// showing the front ridge's anchors and the Paths panel floating beside the
// canvas with the saved cloud path, the work path, and the ridge's shape path.
void shot_readme_vector_tools() {
  VectorSettingsGuard vector_guard;
  SettingsValueRestorer custom_shape_restorer(QStringLiteral("tools/customShapeId"));
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
  // anchors draw on canvas, and a marquee leaves the mid anchors selected.
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
  // marquee (and the canvas overlay) work on the ridge.
  paths_list->setCurrentRow(0);
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Direct Select"))->trigger();
  QApplication::processEvents();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(360, 470)),
       canvas->widget_position_for_document_point(QPoint(1060, 760)));
  QApplication::processEvents();

  // Float the Paths panel over the canvas so the layer list stays visible in
  // the same shot (the two docks share the right-side tab stack).
  auto* paths_dock = window.findChild<QDockWidget*>(QStringLiteral("pathsDock"));
  CHECK(paths_dock != nullptr);
  paths_dock->setFloating(true);
  paths_dock->resize(300, 320);
  const QPoint paths_offset(64, 246);
  paths_dock->move(window.geometry().topLeft() + paths_offset);
  paths_dock->show();
  paths_dock->raise();
  process_events_for(250);
  CHECK(paths_list->count() == 3);  // Front Ridge Shape Path, Cloud Path, Work Path

  reset_readme_status_bar(window);
  auto base = window.grab().toImage();
  draw_readme_overlay(base, paths_dock->grab().toImage(), paths_offset);
  save_readme_shot("shot_readme_vector_tools", base);
  paths_dock->setFloating(false);
  QApplication::processEvents();

  // Rulers persist to view settings on window teardown; restore the clean state.
  rulers_action->trigger();
  QApplication::processEvents();
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
  auto* stroke_check = window.findChild<QCheckBox*>(QStringLiteral("vectorStrokeCheck"));
  auto* stroke_width = window.findChild<QDoubleSpinBox*>(QStringLiteral("vectorStrokeWidthSpin"));
  CHECK(sides_spin != nullptr && inset_spin != nullptr && stroke_check != nullptr &&
        stroke_width != nullptr);
  sides_spin->setValue(5);
  inset_spin->setValue(45);
  stroke_check->setChecked(true);
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
  stroke_check->setChecked(false);
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
    const QPoint dialog_offset(800, 88);
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

  // Direct Select shows the envelope's anchors; a marquee over its bounds
  // selects them so they read as solid squares.
  require_action_by_text(window, QStringLiteral("Direct Select"))->trigger();
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  const auto bounds = envelope->bounds();
  drag(*canvas,
       canvas->widget_position_for_document_point(QPoint(bounds.x - 14, bounds.y - 14)),
       canvas->widget_position_for_document_point(
           QPoint(bounds.x + bounds.width + 14, bounds.y + bounds.height + 14)));
  process_events_for(250);

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
      {"shot_readme_hue_saturation", shot_readme_hue_saturation},
      {"shot_readme_warp_text", shot_readme_warp_text},
      {"shot_readme_tile_preview", shot_readme_tile_preview},
      {"shot_readme_smart_objects", shot_readme_smart_objects},
      {"shot_readme_pattern_manager", shot_readme_pattern_manager},
      {"shot_readme_smart_filters", shot_readme_smart_filters},
      {"shot_readme_tilt_shift", shot_readme_tilt_shift},
      {"shot_readme_material_styles", shot_readme_material_styles},
      {"shot_readme_camera_raw", shot_readme_camera_raw},
      {"shot_readme_quick_mask", shot_readme_quick_mask},
      {"shot_readme_vector_tools", shot_readme_vector_tools},
      {"shot_readme_shape_appearance", shot_readme_shape_appearance},
      {"shot_readme_svg_import", shot_readme_svg_import},
  };
}
