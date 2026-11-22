#include <QLayout>
#include <QToolButton>
#include <QStatusBar>

#include "core/contour_presets.hpp"
#include "core/gradient_presets.hpp"
#include "core/pattern_presets.hpp"
#include "ui/hdr_color_controls.hpp"
#include "ui/web_export.hpp"
#include "ui/tool_palette_customization.hpp"
#include "ui/fill_dialog.hpp"
#include "ui/selection_modify_dialogs.hpp"
#include "ui/color_range_dialog.hpp"
#include "ui/layer_lock_dialog.hpp"
#include "ui/mask_properties_dialog.hpp"
#include "ui/perspective_planes_dialog.hpp"
#include "ui/camera_raw_filter_dialog.hpp"
#include "ui/contour_editor_dialog.hpp"
#include "ui/contour_library.hpp"
#include "ui/contour_manager_dialog.hpp"
#include "ui/custom_shape_library.hpp"
#include "ui/custom_shape_manager_dialog.hpp"
#include "ui/pattern_library.hpp"
#include "ui/pattern_manager_dialog.hpp"
#include "ui/photo_pattern_presets.hpp"
#include "ui/ocr_controls.hpp"
#include "ui/recognize_text_dialog.hpp"
#include "ui/style_browser.hpp"
#include "ui/preset_grid_widget.hpp"
#include "ui/preset_manager_scaffold.hpp"
#include "ui/app_settings.hpp"
#include "ui/style_library.hpp"
#include "ui/style_manager_dialog.hpp"
#include "ui/stroke_preset_manager_dialog.hpp"
#include "psd/asl_io.hpp"
#include "psd/psd_layer_effects.hpp"
#include "core/style_presets.hpp"
#include "ui/animation_preview_window.hpp"
#include "ui/gradient_stops_editor.hpp"
#include "ui/gradient_library.hpp"
#include "ui/gradient_manager_dialog.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/main_window.hpp"
#include "ui/theme_manager.hpp"
#include "ui/color_panel.hpp"
#include "ui/splash_dialog.hpp"
#include "ui/view_extras.hpp"
#include "ui/book_scan_dialog.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QTemporaryDir>
#include <QMetaObject>
#include <QIODevice>
#include <QPainter>
#include <QProgressDialog>
#include <QPushButton>
#include <QStackedWidget>
#include <QSpinBox>
#include <QStringList>
#include <QTabWidget>
#include <QTest>
#include <QTimer>
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

void ui_layer_style_bevel_contour_and_texture_rows_round_trip() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Bevelled",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  patchy::LayerBevelEmboss bevel;
  bevel.enabled = true;
  layer.layer_style().bevels.push_back(bevel);

  const auto ring_id = QStringLiteral("contour.ring");
  const auto bumps_id = QString::fromLatin1(patchy::builtin_pattern_presets()[8].id);
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
    auto* contour_check = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleBevelContourCategoryCheck"));
    auto* texture_check = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleBevelTextureCategoryCheck"));
    auto* stack = dialog->findChild<QStackedWidget*>(QStringLiteral("layerStyleOptionsStack"));
    CHECK(categories != nullptr);
    CHECK(contour_check != nullptr);
    CHECK(texture_check != nullptr);
    CHECK(stack != nullptr);

    // The sub rows sit directly under Bevel & Emboss and are indented.
    const auto bevel_items = categories->findItems(QStringLiteral("Bevel & Emboss"), Qt::MatchExactly);
    const auto contour_items = categories->findItems(QStringLiteral("Contour"), Qt::MatchExactly);
    const auto texture_items = categories->findItems(QStringLiteral("Texture"), Qt::MatchExactly);
    CHECK(!bevel_items.empty());
    CHECK(!contour_items.empty());
    CHECK(!texture_items.empty());
    CHECK(categories->row(contour_items.front()) == categories->row(bevel_items.front()) + 1);
    CHECK(categories->row(texture_items.front()) == categories->row(bevel_items.front()) + 2);
    auto* contour_row_widget = categories->itemWidget(contour_items.front());
    CHECK(contour_row_widget != nullptr);
    CHECK(contour_row_widget->layout()->contentsMargins().left() > 10);

    // Contour page: pick Ring, anti-aliased, range 73.
    categories->setCurrentItem(contour_items.front());
    QApplication::processEvents();
    CHECK(stack->currentWidget()->objectName() == QStringLiteral("layerStyleBevelContourPage"));
    contour_check->setChecked(true);
    auto* contour_combo = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleBevelContourCombo"));
    auto* contour_aa = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleBevelContourAntiAliasedCheck"));
    auto* contour_range = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleBevelContourRangeSpin"));
    CHECK(contour_combo != nullptr);
    CHECK(contour_aa != nullptr);
    CHECK(contour_range != nullptr);
    contour_combo->setCurrentIndex(std::max(0, contour_combo->findData(ring_id)));
    contour_aa->setChecked(true);
    contour_range->setValue(73);

    // Texture page: pick Bumps, scale 152, depth -300, invert, unlink.
    categories->setCurrentItem(texture_items.front());
    QApplication::processEvents();
    CHECK(stack->currentWidget()->objectName() == QStringLiteral("layerStyleBevelTexturePage"));
    texture_check->setChecked(true);
    auto* texture_pattern = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleBevelTexturePatternCombo"));
    auto* texture_scale = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleBevelTextureScaleSpin"));
    auto* texture_depth = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleBevelTextureDepthSpin"));
    auto* texture_invert = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleBevelTextureInvertCheck"));
    auto* texture_link = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleBevelTextureLinkCheck"));
    CHECK(texture_pattern != nullptr);
    CHECK(texture_scale != nullptr);
    CHECK(texture_depth != nullptr);
    CHECK(texture_invert != nullptr);
    CHECK(texture_link != nullptr);
    texture_pattern->setCurrentIndex(std::max(0, texture_pattern->findData(bumps_id)));
    texture_scale->setValue(152);
    texture_depth->setValue(-300);
    texture_invert->setChecked(true);
    texture_link->setChecked(false);
    QTimer::singleShot(80, dialog, [dialog] { dialog->accept(); });
  }));

  const auto settings = patchy::ui::request_layer_style_settings(nullptr, layer, {});
  CHECK(settings.has_value());
  CHECK(settings->style.bevels.size() == 1);
  const auto& result = settings->style.bevels.front();
  CHECK(result.enabled);
  CHECK(result.contour.enabled);
  const auto* ring = patchy::find_builtin_contour_preset("contour.ring");
  CHECK(ring != nullptr);
  CHECK(result.contour.contour.points.size() == ring->contour.points.size());
  CHECK(result.contour.anti_aliased);
  CHECK(std::abs(result.contour.range - 0.73F) < 0.001F);
  CHECK(result.texture.enabled);
  CHECK(result.texture.pattern_id == patchy::builtin_pattern_presets()[8].id);
  CHECK(std::abs(result.texture.scale - 1.52F) < 0.001F);
  CHECK(std::abs(result.texture.depth + 3.0F) < 0.001F);
  CHECK(result.texture.invert);
  CHECK(!result.texture.link_with_layer);
}

void ui_layer_style_dialog_has_no_group_effects_warning() {
  // Group layer effects RENDER since July 2026 (the calibrated group-fx
  // pipeline), so the old "not rendered yet" banner must be gone.
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer group(document.allocate_layer_id(), "Styled Group", patchy::LayerKind::Group);
  patchy::LayerSatin satin;
  satin.enabled = true;
  group.layer_style().satins.push_back(satin);
  group.add_child(patchy::Layer(document.allocate_layer_id(), "Child",
                               solid_pixels(32, 24, patchy::PixelFormat::rgba8(), QColor(Qt::white))));

  QTimer::singleShot(0, guarded([] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* warning = dialog->findChild<QLabel*>(QStringLiteral("layerStyleGroupEffectsWarning"));
    CHECK(warning == nullptr);
    dialog->reject();
  }));

  const auto settings = patchy::ui::request_layer_style_settings(nullptr, group, {});
  CHECK(!settings.has_value());
}

void ui_gradient_stops_editor_two_track_renders_artifact() {
  patchy::ui::GradientStopsEditorWidget editor;
  editor.set_opacity_track_enabled(true);
  editor.resize(420, 96);
  editor.set_stops({patchy::GradientStop{0.0F, patchy::EditColor{255, 255, 255, 255}},
                    patchy::GradientStop{1.0F, patchy::EditColor{255, 0, 0, 255}}});
  editor.set_color_midpoints({0.5F, 0.25F});
  editor.set_opacity_stops(
      {patchy::GradientAlphaStop{0.0F, 0.0F}, patchy::GradientAlphaStop{1.0F, 1.0F, 0.75F}});
  const auto image = editor.grab().toImage();

  // Transparent left end: the gradient contributes nothing, so two vertically
  // adjacent 8px checker cells inside the bar (y 30..60) stay distinct.
  const auto left_top = image.pixelColor(14, 34);
  const auto left_bottom = image.pixelColor(14, 42);
  CHECK(!color_close(left_top, left_bottom, 20));
  // Opaque right end: solid red covers the checkerboard.
  const auto right_top = image.pixelColor(editor.width() - 14, 34);
  const auto right_bottom = image.pixelColor(editor.width() - 14, 42);
  CHECK(color_close(right_top, right_bottom, 8));
  CHECK(right_top.red() > 200);
  CHECK(right_top.green() < 60);
  CHECK(right_top.blue() < 60);
  // Photoshop tag convention above the bar: 0% opacity paints a white tag,
  // 100% a black one.
  CHECK(color_close(image.pixelColor(10, 10), QColor(255, 255, 255), 12));
  CHECK(color_close(image.pixelColor(editor.width() - 12, 10), QColor(0, 0, 0), 12));
  // Color tags below the bar keep their stop colors.
  CHECK(color_close(image.pixelColor(10, editor.height() - 14), QColor(255, 255, 255), 12));
  CHECK(color_close(image.pixelColor(editor.width() - 12, editor.height() - 14), QColor(255, 0, 0), 12));
  save_widget_artifact("ui_gradient_stops_editor_two_track", editor);
}

void ui_gradient_stops_editor_drags_destination_midpoints() {
  patchy::ui::GradientStopsEditorWidget editor;
  editor.set_opacity_track_enabled(true);
  editor.resize(420, 96);
  editor.show();
  QApplication::processEvents();
  editor.set_stops({patchy::GradientStop{0.0F, patchy::EditColor{255, 255, 255, 255}},
                    patchy::GradientStop{1.0F, patchy::EditColor{0, 0, 0, 255}}});
  std::vector<float> color_midpoints{0.5F, 0.25F};
  std::vector<patchy::GradientAlphaStop> alpha_stops{{0.0F, 0.0F}, {1.0F, 1.0F, 0.75F}};
  editor.set_color_midpoints(color_midpoints);
  editor.set_opacity_stops(alpha_stops);

  patchy::EditColor sampled_insert_color;
  bool sampled_insert = false;
  editor.stop_add_requested = [&](patchy::GradientStop stop) {
    sampled_insert_color = stop.color;
    sampled_insert = true;
    return -1;
  };
  const int span = editor.width() - 22;
  const QPoint insert_at_half(10 + span / 2, editor.height() - 10);
  send_mouse(editor, QEvent::MouseButtonPress, insert_at_half, Qt::LeftButton, Qt::LeftButton);
  send_mouse(editor, QEvent::MouseButtonRelease, insert_at_half, Qt::LeftButton, Qt::NoButton);
  CHECK(sampled_insert);
  // White to black with the destination midpoint at 25% is two-thirds black
  // at the segment's halfway location. A generic linear sample would be 128.
  CHECK(std::abs(static_cast<int>(sampled_insert_color.r) - 85) <= 1);
  CHECK(sampled_insert_color.g == sampled_insert_color.r);
  CHECK(sampled_insert_color.b == sampled_insert_color.r);

  int color_row = -1;
  int color_percent = -1;
  editor.color_midpoint_changed = [&](int row, int percent) {
    color_row = row;
    color_percent = percent;
    color_midpoints[static_cast<std::size_t>(row)] = static_cast<float>(percent) / 100.0F;
    editor.set_color_midpoints(color_midpoints);
  };
  int opacity_row = -1;
  int opacity_percent = -1;
  editor.opacity_midpoint_changed = [&](int row, int percent) {
    opacity_row = row;
    opacity_percent = percent;
    alpha_stops[static_cast<std::size_t>(row)].midpoint = static_cast<float>(percent) / 100.0F;
    editor.set_opacity_stops(alpha_stops);
  };

  drag(editor, QPoint(10 + span / 4, 54), QPoint(10 + (span * 7) / 10, 54));
  CHECK(color_row == 1);
  CHECK(std::abs(color_percent - 70) <= 1);
  drag(editor, QPoint(10 + (span * 3) / 4, 36), QPoint(10 + (span * 3) / 10, 36));
  CHECK(opacity_row == 1);
  CHECK(std::abs(opacity_percent - 30) <= 1);
}

void ui_version_two_defaults_seed_only_new_entries() {
  // A version-1 install (generated patterns, Text/Basics styles already
  // seeded, photo-era entries absent) upgrades by adding ONLY the newly
  // introduced defaults; deliberate deletions of old defaults stay deleted.
  QTemporaryDir directory;
  CHECK(directory.isValid());
  patchy::ui::PatternLibrary patterns(directory.filePath(QStringLiteral("patterns")));
  for (const auto& preset : patchy::builtin_pattern_presets()) {
    const auto resource = patchy::builtin_pattern_resource(preset.id);
    CHECK(!patterns
               .add_pattern(QString::fromLatin1(preset.english_name), resource.tile,
                            patchy::ui::default_patterns_folder_name(),
                            QString::fromLatin1(preset.id))
               .isEmpty());
  }
  // Simulate a deliberate deletion from the version-1 era.
  const auto deleted = patterns.entries().front().storage_id;
  CHECK(patterns.remove_pattern(deleted));
  CHECK(patterns.has_all_default_patterns_introduced_after(1) == false);
  const auto added = patterns.restore_default_patterns(1);
  CHECK(added == static_cast<int>(patchy::photo_pattern_presets().size()));
  CHECK(patterns.has_all_default_patterns_introduced_after(1));
  CHECK(!patterns.has_all_default_patterns_introduced_after(0));  // the deletion stays

  patchy::ui::StyleLibrary styles(directory.filePath(QStringLiteral("styles")));
  int version_one_styles = 0;
  for (const auto& preset : patchy::builtin_style_presets()) {
    version_one_styles += preset.introduced_version <= 1 ? 1 : 0;
  }
  CHECK(styles.restore_default_styles(2) == 0);
  CHECK(styles.restore_default_styles(1) ==
        static_cast<int>(patchy::builtin_style_presets().size()) - version_one_styles);
  CHECK(styles.has_all_default_styles_introduced_after(1));
  CHECK(!styles.has_all_default_styles_introduced_after(0));
  // The Materials styles carry their photo-pattern tiles into the entry files.
  const auto* carved = styles.find_entry_by_style_id(
      QStringLiteral("57a1e500-001b-4c6d-8f2a-9b3d4e55c01b"));
  CHECK(carved != nullptr);
  const auto carved_patterns = styles.patterns_for_entry(carved->storage_id);
  CHECK(carved_patterns.size() == 2U);  // Oak Veneer overlay + Tree Bark texture
}

void ui_photo_pattern_presets_load_stable_tiles() {
  // Bundled photo-texture tiles are embedded into user PSDs, so the decoded
  // bytes may never drift. Re-pin only when a texture is deliberately
  // replaced (the failure output prints the hashes).
  const auto presets = patchy::photo_pattern_presets();
  CHECK(presets.size() == 20U);
  const auto tile_hash = [](const patchy::PixelBuffer& tile) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto byte : tile.data()) {
      hash ^= byte;
      hash *= 1099511628211ULL;
    }
    return hash;
  };
  struct PinnedTile {
    const char* id;
    std::uint64_t hash;
  };
  static constexpr PinnedTile kPins[] = {
      {"f0705a00-0001-4c8b-9e3d-2a5b6c77e001", 0x7a674bbae4b82028ULL},  // Fine Wood Grain
      {"f0705a00-0002-4c8b-9e3d-2a5b6c77e002", 0xbeda164fc7cf4e53ULL},  // Dark Walnut
      {"f0705a00-0003-4c8b-9e3d-2a5b6c77e003", 0x1f627ddc7ab7b307ULL},  // Oak Veneer
      {"f0705a00-0004-4c8b-9e3d-2a5b6c77e004", 0xc70bd4b8150f9690ULL},  // Weathered Wood
      {"f0705a00-0005-4c8b-9e3d-2a5b6c77e005", 0xbc5a05917d6d8e1aULL},  // Old Planks
      {"f0705a00-0006-4c8b-9e3d-2a5b6c77e006", 0xdbad1bf7ae21b605ULL},  // Medieval Wood
      {"f0705a00-0007-4c8b-9e3d-2a5b6c77e007", 0x77b373ff3e214290ULL},  // Tree Bark
      {"f0705a00-0008-4c8b-9e3d-2a5b6c77e008", 0x19126e68c80e79fbULL},  // Weathered Marble
      {"f0705a00-0009-4c8b-9e3d-2a5b6c77e009", 0x825c03fdfe334785ULL},  // Slate Slabs
      {"f0705a00-000a-4c8b-9e3d-2a5b6c77e00a", 0xd37d8e486b479927ULL},  // Granite Blocks
      {"f0705a00-000b-4c8b-9e3d-2a5b6c77e00b", 0x12b48689ac3e5e05ULL},  // Rock Face
      {"f0705a00-000c-4c8b-9e3d-2a5b6c77e00c", 0xcd75e7b0f7764102ULL},  // Coarse Rust
      {"f0705a00-000d-4c8b-9e3d-2a5b6c77e00d", 0xfc4ca1567e7786ebULL},  // Steel Plate
      {"f0705a00-000e-4c8b-9e3d-2a5b6c77e00e", 0x4f00027ab93049aaULL},  // Brown Leather
      {"f0705a00-000f-4c8b-9e3d-2a5b6c77e00f", 0xdbdc506732348678ULL},  // Denim Weave
      {"f0705a00-0010-4c8b-9e3d-2a5b6c77e010", 0x1428e978e8df0d1fULL},  // Burlap
      {"f0705a00-0011-4c8b-9e3d-2a5b6c77e011", 0xf9719e308b09f9edULL},  // Rippled Sand
      {"f0705a00-0012-4c8b-9e3d-2a5b6c77e012", 0x89214778bd4f9c54ULL},  // Snow
      {"f0705a00-0013-4c8b-9e3d-2a5b6c77e013", 0x7e47a903e5677d97ULL},  // Cracked Earth
      {"f0705a00-0014-4c8b-9e3d-2a5b6c77e014", 0xc1761be42e05f33eULL},  // Mossy Forest Floor
  };
  QImage sheet(5 * 96, 4 * 96, QImage::Format_RGB32);
  sheet.fill(QColor(0x2B, 0x2B, 0x2B));
  QPainter painter(&sheet);
  int index = 0;
  auto all_pinned = true;
  for (const auto& pin : kPins) {
    CHECK(patchy::find_photo_pattern_preset(pin.id) != nullptr);
    const auto resource = patchy::ui::photo_pattern_resource(pin.id);
    CHECK(resource.has_value());
    CHECK(resource->tile.width() == 512 && resource->tile.height() == 512);
    CHECK(resource->tile.format() == patchy::PixelFormat::rgba8());
    CHECK(resource->provenance == patchy::PatternProvenance::Authored);
    const auto hash = tile_hash(resource->tile);
    if (hash != pin.hash) {
      std::cout << "photo tile hash " << pin.id << ": 0x" << std::hex << hash << std::dec << "\n";
      all_pinned = false;
    }
    painter.drawPixmap((index % 5) * 96, (index / 5) * 96,
                       patchy::ui::pattern_thumbnail(resource->tile, 96));
    ++index;
  }
  painter.end();
  ensure_artifact_dir();
  CHECK(sheet.save(QStringLiteral("test-artifacts/photo_pattern_tiles.png")));
  CHECK(all_pinned);
  CHECK(!patchy::ui::photo_pattern_resource("not-a-real-id").has_value());
  CHECK(patchy::ui::is_bundled_pattern_id("c4a11e00-0001-4b1d-9c3e-7a7c9e55b001"));
  CHECK(patchy::ui::is_bundled_pattern_id(presets.front().id));
  CHECK(!patchy::ui::is_bundled_pattern_id("not-a-real-id"));
  const auto generated = patchy::ui::bundled_pattern_resource("c4a11e00-0001-4b1d-9c3e-7a7c9e55b001");
  CHECK(generated.has_value());
  CHECK(!generated->tile.empty());
}

void ui_pattern_thumbnail_fits_large_images_without_tiling() {
  // A tile larger than the thumbnail renders as ONE fitted copy; a photo-sized
  // non-square tile used to tile into ~6 unrecognizable copies. Small tiles
  // must keep the repeated preview.
  const auto fill_marker_quarter = [](patchy::PixelBuffer& tile) {
    for (std::int32_t y = 0; y < tile.height() / 2; ++y) {
      for (std::int32_t x = 0; x < tile.width() / 2; ++x) {
        auto* px = tile.pixel(x, y);
        px[0] = 230;
        px[1] = 30;
        px[2] = 30;
      }
    }
  };
  const auto is_marker = [](const QColor& color) {
    return color.red() > 150 && color.blue() < 120 && color.green() < 120;
  };
  // Count disjoint runs of columns/rows containing marker pixels: one copy of
  // the tile yields exactly one run per axis, a repeat yields several.
  const auto marker_runs = [&is_marker](const QImage& image, bool columns) {
    const auto outer = columns ? image.width() : image.height();
    const auto inner = columns ? image.height() : image.width();
    int runs = 0;
    bool in_run = false;
    for (int a = 0; a < outer; ++a) {
      bool hit = false;
      for (int b = 0; b < inner && !hit; ++b) {
        hit = is_marker(image.pixelColor(columns ? a : b, columns ? b : a));
      }
      runs += (hit && !in_run) ? 1 : 0;
      in_run = hit;
    }
    return runs;
  };

  auto photo = solid_pixels(600, 900, patchy::PixelFormat::rgba8(), QColor(40, 90, 200));
  fill_marker_quarter(photo);
  const auto photo_thumb = patchy::ui::pattern_thumbnail(photo, 48).toImage();
  CHECK(marker_runs(photo_thumb, true) == 1);
  CHECK(marker_runs(photo_thumb, false) == 1);

  auto small_tile = solid_pixels(16, 16, patchy::PixelFormat::rgba8(), QColor(40, 90, 200));
  fill_marker_quarter(small_tile);
  const auto small_thumb = patchy::ui::pattern_thumbnail(small_tile, 48).toImage();
  CHECK(marker_runs(small_thumb, true) >= 2);
  CHECK(marker_runs(small_thumb, false) >= 2);

  QImage sheet(2 * 48, 48, QImage::Format_RGB32);
  sheet.fill(QColor(0x2B, 0x2B, 0x2B));
  QPainter painter(&sheet);
  painter.drawImage(0, 0, photo_thumb);
  painter.drawImage(48, 0, small_thumb);
  painter.end();
  ensure_artifact_dir();
  CHECK(sheet.save(QStringLiteral("test-artifacts/pattern_thumbnail_fit_vs_tile.png")));
}

void ui_pattern_manager_and_layer_style_buttons_use_library_pattern() {
  clear_pattern_test_state();
  patchy::ui::PatternLibrary library(pattern_test_storage_dir());
  QString error;
  QStringList warnings;
  const auto fixture = QString::fromStdString(
      patchy::test::committed_format_fixture_path("pat", "hue.pat").string());
  const auto storage_id = library.import_pat(fixture, error, warnings);
  CHECK(!storage_id.isEmpty());
  const auto* imported = library.find_entry(storage_id);
  CHECK(imported != nullptr);
  const auto imported_pattern_id = imported->id;

  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Pattern Manager",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(),
                                   QColor(80, 140, 220, 255)));
  bool checked_dialog = false;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog =
        qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* overlay_manage =
        dialog->findChild<QPushButton*>(QStringLiteral("layerStylePatternOverlayManageButton"));
    auto* texture_manage =
        dialog->findChild<QPushButton*>(QStringLiteral("layerStyleBevelTextureManageButton"));
    auto* overlay_combo =
        dialog->findChild<QComboBox*>(QStringLiteral("layerStylePatternOverlayPatternCombo"));
    auto* texture_combo =
        dialog->findChild<QComboBox*>(QStringLiteral("layerStyleBevelTexturePatternCombo"));
    auto* overlay_check =
        dialog->findChild<QCheckBox*>(QStringLiteral("layerStylePatternOverlayCategoryCheck"));
    auto* bevel_check =
        dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleBevelEmbossCategoryCheck"));
    auto* texture_check =
        dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleBevelTextureCategoryCheck"));
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
    CHECK(overlay_manage != nullptr && overlay_manage->isEnabled());
    CHECK(texture_manage != nullptr && texture_manage->isEnabled());
    CHECK(overlay_combo != nullptr && texture_combo != nullptr);
    CHECK(overlay_check != nullptr && bevel_check != nullptr && texture_check != nullptr);
    CHECK(categories != nullptr);
    CHECK(overlay_combo->findData(imported_pattern_id) >= 0);
    const auto overlay_items = categories->findItems(QStringLiteral("Pattern Overlay"), Qt::MatchExactly);
    CHECK(!overlay_items.empty());
    categories->setCurrentItem(overlay_items.front());
    QApplication::processEvents();
    save_widget_artifact("ui_layer_style_pattern_manager_buttons", *dialog);

    bool checked_manager = false;
    QTimer::singleShot(0, guarded([&] {
      auto* manager =
          qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patternManagerDialog")));
      CHECK(manager != nullptr);
      auto* tree = manager->findChild<QTreeWidget*>(QStringLiteral("patternManagerTree"));
      auto* use = manager->findChild<QPushButton*>(QStringLiteral("patternManagerUseButton"));
      CHECK(tree != nullptr);
      CHECK(use != nullptr && use->isEnabled());
      QApplication::processEvents();
      int pattern_rows = 0;
      for (int top = 0; top < tree->topLevelItemCount(); ++top) {
        auto* item = tree->topLevelItem(top);
        if (item->childCount() == 0) {
          ++pattern_rows;
          CHECK(tree->visualItemRect(item).height() >= 44);
        } else {
          for (int child = 0; child < item->childCount(); ++child) {
            ++pattern_rows;
            CHECK(tree->visualItemRect(item->child(child)).height() >= 44);
          }
        }
      }
      CHECK(pattern_rows == 1);
      save_widget_artifact("ui_pattern_manager_dialog", *manager);
      checked_manager = true;
      use->click();
    }));
    overlay_manage->click();
    CHECK(checked_manager);
    CHECK(overlay_combo->currentData().toString() == imported_pattern_id);
    overlay_check->setChecked(true);

    const auto texture_items = categories->findItems(QStringLiteral("Texture"), Qt::MatchExactly);
    CHECK(!texture_items.empty());
    categories->setCurrentItem(texture_items.front());
    QApplication::processEvents();
    QTimer::singleShot(0, guarded([&] {
      auto* manager =
          qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patternManagerDialog")));
      CHECK(manager != nullptr);
      auto* use = manager->findChild<QPushButton*>(QStringLiteral("patternManagerUseButton"));
      CHECK(use != nullptr && use->isEnabled());
      use->click();
    }));
    texture_manage->click();
    CHECK(texture_combo->currentData().toString() == imported_pattern_id);
    bevel_check->setChecked(true);
    texture_check->setChecked(true);
    checked_dialog = true;
    QTimer::singleShot(80, dialog, [dialog] { dialog->accept(); });
  }));

  int preview_count = 0;
  const auto settings = patchy::ui::request_layer_style_settings(
      nullptr, layer, [&](const patchy::ui::LayerStyleSettings&) { ++preview_count; },
      &document.metadata().patterns, &library);
  CHECK(checked_dialog);
  CHECK(settings.has_value());
  CHECK(settings->style.pattern_overlays.size() == 1);
  CHECK(settings->style.pattern_overlays.front().enabled);
  CHECK(QString::fromStdString(settings->style.pattern_overlays.front().pattern_id) ==
        imported_pattern_id);
  CHECK(settings->style.pattern_overlays.front().pattern_name == "hue");
  CHECK(settings->style.bevels.size() == 1);
  CHECK(settings->style.bevels.front().enabled);
  CHECK(settings->style.bevels.front().texture.enabled);
  CHECK(QString::fromStdString(settings->style.bevels.front().texture.pattern_id) ==
        imported_pattern_id);
  CHECK(preview_count >= 1);
  clear_pattern_test_state();
}

// GitHub issue 748: the "+" after Layer Style's two pattern pickers saves a pattern the
// document carries into the pattern library. Cancel adds nothing, a pattern whose pixels the
// library already has cannot be saved again, and the effect keeps the pattern it had.
void ui_layer_style_saves_document_pattern_as_preset() {
  clear_pattern_test_state();
  const auto cleanup = qScopeGuard([] { clear_pattern_test_state(); });
  const auto rgba8 = patchy::PixelFormat::rgba8();
  const auto same_tile = [](const patchy::PixelBuffer& a, const patchy::PixelBuffer& b) {
    return a.width() == b.width() && a.height() == b.height() && a.data().size() == b.data().size() &&
           std::equal(a.data().begin(), a.data().end(), b.data().begin());
  };
  const auto files_on_disk = [] { return QDir(pattern_test_storage_dir()).entryList(QDir::Files).size(); };

  // The library starts with two patterns: one whose id a document pattern also uses, and one
  // the size of the document's bricks with other pixels.
  QString moss_pattern_id;
  QString fern_pattern_id;
  {
    patchy::ui::PatternLibrary seed(pattern_test_storage_dir());
    const auto moss = seed.add_pattern(QStringLiteral("Moss"), solid_pixels(20, 14, rgba8, QColor(30, 200, 90)));
    const auto fern = seed.add_pattern(QStringLiteral("Fern"), solid_pixels(16, 12, rgba8, QColor(20, 120, 60)));
    CHECK(!moss.isEmpty() && !fern.isEmpty());
    moss_pattern_id = seed.find_entry(moss)->id;
    fern_pattern_id = seed.find_entry(fern)->id;
  }

  // The document's own patterns, as a PSD brings them: bricks the library does not have, dots
  // under the id the library gives to Moss, and a reference whose tile never came.
  patchy::PatternResource bricks;
  bricks.id = "74800000-aaaa-4bbb-8ccc-000000000001";
  bricks.name = "Doc Bricks";
  bricks.tile = solid_pixels(16, 12, rgba8, QColor(170, 70, 40));
  fill_pixel_rect(bricks.tile, QRect(0, 5, 16, 1), QColor(225, 215, 200));
  fill_pixel_rect(bricks.tile, QRect(7, 0, 1, 5), QColor(225, 215, 200));
  fill_pixel_rect(bricks.tile, QRect(12, 8, 4, 4), QColor(Qt::transparent));
  bricks.provenance = patchy::PatternProvenance::ImportedRaw;
  patchy::PatternResource dots;
  dots.id = moss_pattern_id.toStdString();
  dots.name = "Doc Dots";
  dots.tile = solid_pixels(8, 8, rgba8, QColor(40, 60, 150));
  fill_pixel_rect(dots.tile, QRect(2, 2, 3, 3), QColor(250, 230, 90));
  dots.provenance = patchy::PatternProvenance::ImportedRaw;
  patchy::PatternResource ghost;
  ghost.id = "74800000-aaaa-4bbb-8ccc-000000000003";
  ghost.name = "Ghost";
  ghost.tile = solid_pixels(1, 1, rgba8, QColor(Qt::transparent));
  ghost.provenance = patchy::PatternProvenance::ImportedRaw;
  CHECK(patchy::pattern_tile_is_unrenderable(ghost.tile));

  patchy::Document document(96, 72, rgba8);
  document.metadata().patterns.patterns = {bricks, dots, ghost};
  patchy::Layer layer(document.allocate_layer_id(), "Styled", solid_pixels(48, 36, rgba8, QColor(80, 140, 220)));
  patchy::LayerPatternOverlay overlay;
  overlay.enabled = true;
  overlay.pattern_id = bricks.id;
  overlay.pattern_name = bricks.name;
  layer.layer_style().pattern_overlays = {overlay};
  patchy::LayerBevelEmboss bevel;
  bevel.enabled = true;
  bevel.texture.enabled = true;
  bevel.texture.pattern_id = bricks.id;
  bevel.texture.pattern_name = bricks.name;
  layer.layer_style().bevels = {bevel};

  const auto save_tip = QStringLiteral("Save this pattern as a preset");
  const auto have_tip = QStringLiteral("This pattern is already in the pattern library");
  // Answers the name prompt: a null name cancels it, an empty one empties the field.
  const auto answer_prompt = [](QPushButton* button, const QString& prefill, const QString& name,
                                const char* artifact = nullptr) {
    when_dialog_visible(QStringLiteral("layerStyleSavePatternDialog"), [prefill, name, artifact](QDialog& prompt) {
      auto* name_edit = prompt.findChild<QLineEdit*>(QStringLiteral("layerStyleSavePatternNameEdit"));
      CHECK(name_edit != nullptr);
      if (name_edit == nullptr) {
        prompt.reject();
        return;
      }
      CHECK(name_edit->text() == prefill);
      CHECK(name_edit->selectedText() == prefill);
      if (name.isNull()) {
        prompt.reject();
        return;
      }
      name_edit->setText(name);
      QApplication::processEvents();
      if (artifact != nullptr) {
        save_widget_artifact(artifact, prompt);
      }
      prompt.accept();
    });
    button->click();
    QApplication::processEvents();
  };
  struct StyleControls {
    QDialog* dialog{};
    QPushButton* overlay_save{};
    QPushButton* texture_save{};
    QComboBox* overlay_combo{};
    QComboBox* texture_combo{};
    QListWidget* categories{};
  };
  const auto style_controls = [] {
    StyleControls found;
    found.dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(found.dialog != nullptr);
    found.overlay_save =
        found.dialog->findChild<QPushButton*>(QStringLiteral("layerStylePatternOverlaySavePatternButton"));
    found.texture_save =
        found.dialog->findChild<QPushButton*>(QStringLiteral("layerStyleBevelTextureSavePatternButton"));
    found.overlay_combo = found.dialog->findChild<QComboBox*>(QStringLiteral("layerStylePatternOverlayPatternCombo"));
    found.texture_combo = found.dialog->findChild<QComboBox*>(QStringLiteral("layerStyleBevelTexturePatternCombo"));
    found.categories = found.dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
    CHECK(found.overlay_save != nullptr && found.texture_save != nullptr);
    CHECK(found.overlay_combo != nullptr && found.texture_combo != nullptr && found.categories != nullptr);
    return found;
  };
  const auto show_page = [](const StyleControls& controls, const QString& title) {
    const auto items = controls.categories->findItems(title, Qt::MatchExactly);
    CHECK(!items.empty());
    controls.categories->setCurrentItem(items.front());
    QApplication::processEvents();
  };
  const auto pick = [](QComboBox* combo, const QString& pattern_id) {
    const auto row = combo->findData(pattern_id);
    CHECK(row >= 0);
    combo->setCurrentIndex(row);
    QApplication::processEvents();
  };
  const auto bricks_id = QString::fromStdString(bricks.id);
  const auto ghost_id = QString::fromStdString(ghost.id);

  patchy::ui::PatternLibrary library(pattern_test_storage_dir());
  CHECK(library.entries().size() == 2U);
  const auto entry_named = [](const patchy::ui::PatternLibrary& from,
                              const QString& name) -> const patchy::ui::PatternLibraryEntry* {
    for (const auto& entry : from.entries()) {
      if (entry.name == name) {
        return &entry;
      }
    }
    return nullptr;
  };
  const auto files_before = files_on_disk();
  int preview_count = 0;
  bool checked_dialog = false;
  QTimer::singleShot(0, guarded([&] {
    const auto controls = style_controls();
    // A pattern only the document has can be saved, from either picker.
    CHECK(controls.overlay_save->isEnabled() && controls.overlay_save->toolTip() == save_tip);
    CHECK(controls.texture_save->isEnabled() && controls.texture_save->toolTip() == save_tip);
    CHECK(controls.overlay_combo->currentData().toString() == bricks_id);
    show_page(controls, QStringLiteral("Pattern Overlay"));
    save_widget_artifact("issue_748_pattern_overlay_save_button", *controls.dialog);

    // Cancel: the library and its folder are as they were.
    answer_prompt(controls.overlay_save, QStringLiteral("Doc Bricks"), QString());
    CHECK(library.entries().size() == 2U);
    CHECK(files_on_disk() == files_before);
    CHECK(controls.overlay_save->isEnabled());

    // Saved under a new name, with the document's id (no library pattern had it) and pixels.
    const auto previews_before_save = preview_count;
    answer_prompt(controls.overlay_save, QStringLiteral("Doc Bricks"), QStringLiteral("Saved Bricks"),
                  "issue_748_save_pattern_name_prompt");
    CHECK(library.entries().size() == 3U);
    CHECK(files_on_disk() == files_before + 2);
    const auto* saved = entry_named(library, QStringLiteral("Saved Bricks"));
    CHECK(saved != nullptr);
    if (saved != nullptr) {
      CHECK(saved->id == bricks_id);
      CHECK(saved->folder.isEmpty());
      CHECK(saved->size == QSize(16, 12));
      const auto stored = library.resource_for_entry(saved->storage_id);
      CHECK(stored.has_value() && same_tile(stored->tile, bricks.tile));
    }
    // Nothing about the effect changed, and the pattern cannot be saved a second time from
    // either picker.
    CHECK(preview_count == previews_before_save);
    CHECK(controls.overlay_combo->currentData().toString() == bricks_id);
    CHECK(controls.texture_combo->currentData().toString() == bricks_id);
    CHECK(!controls.overlay_save->isEnabled() && controls.overlay_save->toolTip() == have_tip);
    CHECK(!controls.texture_save->isEnabled() && controls.texture_save->toolTip() == have_tip);
    save_widget_artifact("issue_748_pattern_overlay_after_saving", *controls.dialog);

    // A library pattern and a reference with no tile have nothing to save.
    pick(controls.overlay_combo, fern_pattern_id);
    CHECK(!controls.overlay_save->isEnabled() && controls.overlay_save->toolTip() == have_tip);
    pick(controls.overlay_combo, ghost_id);
    CHECK(!controls.overlay_save->isEnabled() && controls.overlay_save->toolTip() == save_tip);
    pick(controls.overlay_combo, bricks_id);
    CHECK(!controls.overlay_save->isEnabled());

    // Bevel Texture: the document's dots share an id with the library's Moss, whose pixels
    // differ, so they can be saved and get an id of their own. An emptied name field keeps
    // the pattern's name.
    show_page(controls, QStringLiteral("Texture"));
    pick(controls.texture_combo, moss_pattern_id);
    CHECK(controls.texture_save->isEnabled() && controls.texture_save->toolTip() == save_tip);
    save_widget_artifact("issue_748_bevel_texture_save_button", *controls.dialog);
    answer_prompt(controls.texture_save, QStringLiteral("Doc Dots"), QStringLiteral(""));
    CHECK(library.entries().size() == 4U);
    const auto* saved_dots = entry_named(library, QStringLiteral("Doc Dots"));
    CHECK(saved_dots != nullptr);
    if (saved_dots != nullptr) {
      CHECK(!saved_dots->id.isEmpty() && saved_dots->id != moss_pattern_id);
      const auto stored = library.resource_for_entry(saved_dots->storage_id);
      CHECK(stored.has_value() && same_tile(stored->tile, dots.tile));
      // The new library pattern is listed, and the picker still shows the document's.
      CHECK(controls.texture_combo->findData(saved_dots->id) >= 0);
    }
    CHECK(controls.texture_combo->currentData().toString() == moss_pattern_id);
    CHECK(!controls.texture_save->isEnabled() && controls.texture_save->toolTip() == have_tip);
    checked_dialog = true;
    QTimer::singleShot(80, controls.dialog, [dialog = controls.dialog] { dialog->accept(); });
  }));
  const auto settings = patchy::ui::request_layer_style_settings(
      nullptr, layer, [&](const patchy::ui::LayerStyleSettings&) { ++preview_count; },
      &document.metadata().patterns, &library);
  CHECK(checked_dialog);
  CHECK(settings.has_value());
  if (settings.has_value()) {
    CHECK(settings->style.pattern_overlays.size() == 1 &&
          settings->style.pattern_overlays.front().pattern_id == bricks.id);
    CHECK(settings->style.bevels.size() == 1 &&
          QString::fromStdString(settings->style.bevels.front().texture.pattern_id) == moss_pattern_id);
  }

  // Both patterns are on disk, and a library read from it still knows their pixels.
  patchy::ui::PatternLibrary reloaded(pattern_test_storage_dir());
  CHECK(reloaded.entries().size() == 4U);
  const auto* reloaded_bricks = reloaded.find_entry_by_tile(bricks.tile);
  CHECK(reloaded_bricks != nullptr && reloaded_bricks->name == QStringLiteral("Saved Bricks"));
  const auto* reloaded_dots = reloaded.find_entry_by_tile(dots.tile);
  CHECK(reloaded_dots != nullptr && reloaded_dots->name == QStringLiteral("Doc Dots"));
  auto other_bricks = bricks.tile;
  fill_pixel_rect(other_bricks, QRect(3, 9, 1, 1), QColor(10, 10, 10));
  CHECK(reloaded.find_entry_by_tile(other_bricks) == nullptr);
  CHECK(reloaded.find_entry_by_tile(ghost.tile) == nullptr);

  // A later session starts with the buttons off for both patterns.
  bool checked_reopened = false;
  QTimer::singleShot(0, guarded([&] {
    const auto controls = style_controls();
    CHECK(!controls.overlay_save->isEnabled() && controls.overlay_save->toolTip() == have_tip);
    CHECK(!controls.texture_save->isEnabled());
    pick(controls.texture_combo, moss_pattern_id);
    CHECK(!controls.texture_save->isEnabled() && controls.texture_save->toolTip() == have_tip);
    checked_reopened = true;
    controls.dialog->reject();
  }));
  (void)patchy::ui::request_layer_style_settings(nullptr, layer, {}, &document.metadata().patterns, &reloaded);
  CHECK(checked_reopened);

  // A tile past the image-pattern limits is refused with the reason, as add_image refuses it.
  patchy::PatternResource ribbon;
  ribbon.id = "74800000-aaaa-4bbb-8ccc-000000000004";
  ribbon.name = "Ribbon";
  ribbon.tile = patchy::PixelBuffer(30001, 1, rgba8);
  QString error;
  CHECK(reloaded.add_resource(QStringLiteral("Ribbon"), ribbon, error).isEmpty());
  CHECK(error.contains(QStringLiteral("30,000")) && error.contains(QStringLiteral("Ribbon")));
  CHECK(reloaded.entries().size() == 4U);
}

void ui_pattern_manager_preview_zooms_and_opens_image() {
  QTemporaryDir directory;
  CHECK(directory.isValid());
  patchy::ui::PatternLibrary library(directory.filePath(QStringLiteral("library")));
  auto tile = solid_pixels(16, 12, patchy::PixelFormat::rgba8(), QColor(200, 80, 40, 255));
  {
    auto* px = tile.pixel(3, 2);
    px[0] = 10;
    px[1] = 250;
    px[2] = 120;
  }
  const auto storage_id = library.add_pattern(QStringLiteral("Lava"), tile);
  CHECK(!storage_id.isEmpty());

  QString opened_name;
  std::optional<patchy::PixelBuffer> opened_tile;
  bool drove_dialog = false;
  QTimer::singleShot(0, guarded([&] {
    auto* manager =
        qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patternManagerDialog")));
    CHECK(manager != nullptr);
    auto* preview = manager->findChild<QWidget*>(QStringLiteral("patternManagerPreview"));
    auto* open_button =
        manager->findChild<QPushButton*>(QStringLiteral("patternManagerOpenImageButton"));
    CHECK(preview != nullptr);
    CHECK(open_button != nullptr);
    CHECK(open_button->isVisible());
    CHECK(open_button->isEnabled());

    // Wheel zoom steps x1.25 from 100%; double-click resets.
    CHECK(preview->property("previewZoomPercent").toInt() == 100);
    const auto center = QPoint(preview->width() / 2, preview->height() / 2);
    send_wheel(*preview, center, 120);
    CHECK(preview->property("previewZoomPercent").toInt() == 125);
    send_wheel(*preview, center, 120);
    CHECK(preview->property("previewZoomPercent").toInt() == 156);
    save_widget_artifact("ui_pattern_manager_preview_zoom", *manager);
    send_wheel(*preview, center, -120);
    CHECK(preview->property("previewZoomPercent").toInt() == 125);
    send_double_click(*preview, center);
    CHECK(preview->property("previewZoomPercent").toInt() == 100);

    // Any mouse button drags to pan; wheel zoom anchors at the cursor (an off-center
    // zoom adjusts the pan); double-click resets the whole view.
    CHECK(preview->property("previewPanOffset").toPoint() == QPoint(0, 0));
    drag(*preview, center, center + QPoint(9, 6));
    CHECK(preview->property("previewPanOffset").toPoint() == QPoint(9, 6));
    drag(*preview, center, center + QPoint(-3, 2), Qt::NoModifier, Qt::MiddleButton);
    CHECK(preview->property("previewPanOffset").toPoint() == QPoint(6, 8));
    drag(*preview, center, center + QPoint(1, -4), Qt::NoModifier, Qt::RightButton);
    CHECK(preview->property("previewPanOffset").toPoint() == QPoint(7, 4));
    drag(*preview, center, center + QPoint(2, 1), Qt::NoModifier, Qt::BackButton);
    CHECK(preview->property("previewPanOffset").toPoint() == QPoint(9, 5));
    const auto pan_before_zoom = preview->property("previewPanOffset").toPoint();
    send_wheel(*preview, center + QPoint(40, 25), 120);
    CHECK(preview->property("previewZoomPercent").toInt() == 125);
    CHECK(preview->property("previewPanOffset").toPoint() != pan_before_zoom);
    save_widget_artifact("ui_pattern_manager_preview_pan_zoom", *manager);
    send_double_click(*preview, center);
    CHECK(preview->property("previewZoomPercent").toInt() == 100);
    CHECK(preview->property("previewPanOffset").toPoint() == QPoint(0, 0));

    // The centered main tile carries the faint white outline (same as the tile preview),
    // so its corner pixel reads lighter than the flat tile interior.
    const auto grab = preview->grab().toImage();
    const QPoint origin((preview->width() - 16) / 2, (preview->height() - 12) / 2);
    const auto border_color = grab.pixelColor(origin);
    const auto inside_color = grab.pixelColor(origin + QPoint(1, 1));
    CHECK(border_color.green() > inside_color.green());
    CHECK(border_color.blue() > inside_color.blue());

    open_button->click();
    CHECK(opened_name == QStringLiteral("Lava"));
    CHECK(!open_button->isEnabled());  // one queued document per pattern per run
    drove_dialog = true;
    manager->reject();
  }));
  const auto chosen = patchy::ui::request_pattern_manager(
      nullptr, library, {}, [&](const QString& name, const patchy::PixelBuffer& out) {
        opened_name = name;
        opened_tile = out;
      });
  CHECK(drove_dialog);
  CHECK(chosen.isEmpty());
  CHECK(opened_tile.has_value());
  CHECK(opened_tile->width() == 16);
  CHECK(opened_tile->height() == 12);
  CHECK(opened_tile->data().size() == tile.data().size());
  CHECK(std::equal(tile.data().begin(), tile.data().end(), opened_tile->data().begin()));
}

void ui_gradient_manager_shows_defaults_and_supports_crud() {
  QTemporaryDir directory;
  CHECK(directory.isValid());
  patchy::ui::GradientLibrary library(directory.filePath(QStringLiteral("gradients")));
  CHECK(library.restore_default_gradients() == 20);
  CHECK(library.entries().size() == 20U);
  const auto initial_id = library.entries().front().storage_id;
  bool drove_dialog = false;
  QTimer::singleShot(0, guarded([&] {
    auto* manager = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("gradientManagerDialog")));
    CHECK(manager != nullptr);
    auto* tree = manager->findChild<QTreeWidget*>(QStringLiteral("gradientManagerTree"));
    auto* duplicate = manager->findChild<QPushButton*>(QStringLiteral("gradientManagerDuplicateButton"));
    auto* remove = manager->findChild<QPushButton*>(QStringLiteral("gradientManagerDeleteButton"));
    auto* restore = manager->findChild<QPushButton*>(QStringLiteral("gradientManagerRestoreButton"));
    auto* use = manager->findChild<QPushButton*>(QStringLiteral("gradientManagerUseButton"));
    CHECK(tree != nullptr);
    CHECK(duplicate != nullptr && remove != nullptr && restore != nullptr && use != nullptr);
    CHECK(tree->topLevelItemCount() == 4);
    int leaves = 0;
    std::function<void(QTreeWidgetItem*)> count_leaves = [&](QTreeWidgetItem* item) {
      if (item->childCount() == 0) {
        ++leaves;
        return;
      }
      item->setExpanded(true);
      for (int child = 0; child < item->childCount(); ++child) count_leaves(item->child(child));
    };
    for (int top = 0; top < tree->topLevelItemCount(); ++top) count_leaves(tree->topLevelItem(top));
    CHECK(leaves == 20);
    save_widget_artifact("ui_gradient_manager_defaults", *manager);

    CHECK(duplicate->isEnabled());
    duplicate->click();
    CHECK(library.entries().size() == 21U);
    CHECK(remove->isEnabled());
    remove->click();
    CHECK(library.entries().size() == 20U);
    restore->click();
    CHECK(library.default_gradients_match_factory());
    auto* first_folder = tree->topLevelItem(0);
    CHECK(first_folder != nullptr && first_folder->childCount() > 0);
    tree->setCurrentItem(first_folder->child(0));
    QApplication::processEvents();
    CHECK(use->isEnabled());
    drove_dialog = true;
    use->click();
  }));
  const auto selected = patchy::ui::request_gradient_manager(
      nullptr, library, initial_id, patchy::builtin_gradient_presets().front().definition);
  CHECK(drove_dialog);
  CHECK(!selected.isEmpty());
  CHECK(library.entries().size() == 20U);
}

// "Open as Image" must never create a document while the Layer Style dialog holds the
// preview-dialog edit lock (add_document_session's activation tail assumes no lock);
// the request is queued and flushed after the dialog closes — on the cancel path too.
void ui_layer_style_open_pattern_as_image_defers_until_dialog_closes() {
  clear_pattern_test_state();
  {
    patchy::ui::PatternLibrary seed(pattern_test_storage_dir());
    const auto tile = solid_pixels(20, 14, patchy::PixelFormat::rgba8(), QColor(30, 200, 90, 255));
    CHECK(!seed.add_pattern(QStringLiteral("Moss"), tile).isEmpty());
  }

  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);
  const auto initial_tabs = tabs->count();
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* row_widget = layer_list->itemWidget(layer_list->item(0));
  CHECK(row_widget != nullptr);
  auto* row_name = row_widget->findChild<QLabel*>(QStringLiteral("layerRowDetails"));
  CHECK(row_name != nullptr);

  bool drove_style_dialog = false;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog =
        qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* overlay_manage =
        dialog->findChild<QPushButton*>(QStringLiteral("layerStylePatternOverlayManageButton"));
    CHECK(overlay_manage != nullptr && overlay_manage->isEnabled());

    bool drove_manager = false;
    QTimer::singleShot(0, guarded([&] {
      auto* manager =
          qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patternManagerDialog")));
      CHECK(manager != nullptr);
      auto* open_button =
          manager->findChild<QPushButton*>(QStringLiteral("patternManagerOpenImageButton"));
      CHECK(open_button != nullptr);
      CHECK(open_button->isVisible());
      CHECK(open_button->isEnabled());
      open_button->click();
      QApplication::processEvents();
      // Deferred: nothing may open while the edit lock is held.
      CHECK(tabs->count() == initial_tabs);
      CHECK(!open_button->isEnabled());
      CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("Moss")));
      drove_manager = true;
      manager->reject();
    }));
    overlay_manage->click();
    CHECK(drove_manager);
    CHECK(tabs->count() == initial_tabs);
    drove_style_dialog = true;
    dialog->reject();  // the cancel path must flush the queued document too
  }));
  send_double_click(*row_name, row_name->rect().center());
  QApplication::processEvents();
  CHECK(drove_style_dialog);

  CHECK(tabs->count() == initial_tabs + 1);
  const auto& opened_document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  CHECK(opened_document.width() == 20);
  CHECK(opened_document.height() == 14);
  CHECK(opened_document.layers().size() == 1);
  const auto* px = opened_document.layers().front().pixels().pixel(0, 0);
  CHECK(px[0] == 30 && px[1] == 200 && px[2] == 90 && px[3] == 255);
  CHECK(tabs->tabText(tabs->currentIndex()).startsWith(QStringLiteral("Moss")));
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("Moss")));
  clear_pattern_test_state();
}

void ui_layer_style_pattern_picker_groups_and_collapses_folders() {
  QTemporaryDir directory;
  CHECK(directory.isValid());
  patchy::ui::PatternLibrary library(
      directory.filePath(QStringLiteral("library")));
  CHECK(library.restore_default_patterns() ==
        static_cast<int>(patchy::builtin_pattern_presets().size() +
                         patchy::photo_pattern_presets().size()));
  QString error;
  QStringList warnings;
  const auto fixture = QString::fromStdString(
      patchy::test::committed_format_fixture_path("pat", "hue.pat").string());
  const auto imported_storage_id = library.import_pat(fixture, error, warnings);
  CHECK(!imported_storage_id.isEmpty());
  const auto *imported = library.find_entry(imported_storage_id);
  CHECK(imported != nullptr);
  const auto imported_pattern_id = imported->id;

  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Pattern Folders",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(),
                                   QColor(80, 140, 220, 255)));

  bool checked_popup = false;
  QTimer::singleShot(0, guarded([&] {
    auto *dialog = qobject_cast<QDialog *>(
        find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto *categories = dialog->findChild<QListWidget *>(
        QStringLiteral("layerStyleCategoryList"));
    auto *combo = dialog->findChild<QComboBox *>(
        QStringLiteral("layerStyleBevelTexturePatternCombo"));
    CHECK(categories != nullptr && combo != nullptr);
    const auto texture_items =
        categories->findItems(QStringLiteral("Texture"), Qt::MatchExactly);
    CHECK(!texture_items.empty());
    categories->setCurrentItem(texture_items.front());
    QApplication::processEvents();
    const auto default_pattern_id =
        QString::fromLatin1(patchy::builtin_pattern_presets().front().id);
    const auto default_pattern_index = combo->findData(default_pattern_id);
    CHECK(default_pattern_index >= 0);
    combo->setCurrentIndex(default_pattern_index);

    combo->showPopup();
    QApplication::processEvents();
    auto *popup = combo->findChild<QFrame *>(
        QStringLiteral("layerStyleBevelTexturePatternComboPopup"),
        Qt::FindDirectChildrenOnly);
    CHECK(popup != nullptr && popup->isVisible());
    CHECK(popup->height() >= 280);
    auto *tree = popup->findChild<QTreeWidget *>(
        QStringLiteral("layerStyleBevelTexturePatternComboTree"));
    CHECK(tree != nullptr);

    const auto find_folder = [tree](const QString &name) -> QTreeWidgetItem * {
      for (int index = 0; index < tree->topLevelItemCount(); ++index) {
        auto *item = tree->topLevelItem(index);
        if (item->text(0) == name) {
          return item;
        }
      }
      return nullptr;
    };
    auto *defaults = find_folder(patchy::ui::default_patterns_folder_name());
    auto *hue = find_folder(QStringLiteral("hue"));
    CHECK(defaults != nullptr);
    CHECK(hue != nullptr);
    CHECK(defaults->childCount() ==
          static_cast<int>(patchy::builtin_pattern_presets().size()));
    CHECK(hue->childCount() == 1);
    CHECK(defaults->isExpanded());
    CHECK(!hue->isExpanded());

    defaults->setExpanded(false);
    hue->setExpanded(true);
    QApplication::processEvents();
    CHECK(!defaults->isExpanded());
    CHECK(hue->isExpanded());
    save_widget_artifact("ui_layer_style_pattern_picker_folders", *popup);

    auto *hue_pattern = hue->child(0);
    CHECK(hue_pattern != nullptr);
    tree->scrollToItem(hue_pattern);
    QApplication::processEvents();
    const auto row = tree->visualItemRect(hue_pattern);
    CHECK(row.isValid());
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                      row.center());
    QApplication::processEvents();
    CHECK(combo->currentData().toString() == imported_pattern_id);
    checked_popup = true;
    dialog->reject();
  }));

  const auto settings = patchy::ui::request_layer_style_settings(
      nullptr, layer, {}, &document.metadata().patterns, &library);
  CHECK(!settings.has_value());
  CHECK(checked_popup);
}

void ui_pattern_manager_remaps_document_id_collision() {
  QTemporaryDir directory;
  CHECK(directory.isValid());
  patchy::ui::PatternLibrary library(directory.filePath(QStringLiteral("library")));
  constexpr const char* kCollisionId = "shared-pattern-id";
  const auto library_tile = solid_pixels(4, 3, patchy::PixelFormat::rgba8(),
                                         QColor(20, 210, 80, 255));
  const auto storage_id =
      library.add_pattern(QStringLiteral("Library Green"), library_tile,
                          QStringLiteral("Collisions"), QString::fromLatin1(kCollisionId));
  CHECK(!storage_id.isEmpty());

  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::PatternResource embedded;
  embedded.id = kCollisionId;
  embedded.name = "Embedded Red";
  embedded.tile = solid_pixels(4, 3, patchy::PixelFormat::rgba8(),
                               QColor(220, 30, 40, 255));
  embedded.provenance = patchy::PatternProvenance::ImportedRaw;
  document.metadata().patterns.adopt(embedded);
  patchy::Layer layer(document.allocate_layer_id(), "Pattern Collision",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(),
                                   QColor(80, 140, 220, 255)));

  QString remapped_id;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog =
        qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* manage =
        dialog->findChild<QPushButton*>(QStringLiteral("layerStylePatternOverlayManageButton"));
    auto* combo =
        dialog->findChild<QComboBox*>(QStringLiteral("layerStylePatternOverlayPatternCombo"));
    auto* overlay_check =
        dialog->findChild<QCheckBox*>(QStringLiteral("layerStylePatternOverlayCategoryCheck"));
    auto* categories =
        dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
    CHECK(manage != nullptr && combo != nullptr && overlay_check != nullptr &&
          categories != nullptr);
    CHECK(combo->currentData().toString() == QString::fromLatin1(kCollisionId));
    const auto overlay_items =
        categories->findItems(QStringLiteral("Pattern Overlay"), Qt::MatchExactly);
    CHECK(!overlay_items.empty());
    categories->setCurrentItem(overlay_items.front());
    QApplication::processEvents();

    QTimer::singleShot(0, guarded([&] {
      auto* manager =
          qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patternManagerDialog")));
      CHECK(manager != nullptr);
      auto* use = manager->findChild<QPushButton*>(QStringLiteral("patternManagerUseButton"));
      CHECK(use != nullptr && use->isEnabled());
      use->click();
    }));
    manage->click();

    remapped_id = combo->currentData().toString();
    CHECK(!remapped_id.isEmpty());
    CHECK(remapped_id != QString::fromLatin1(kCollisionId));
    const auto* remapped = document.metadata().patterns.find(remapped_id.toStdString());
    CHECK(remapped != nullptr);
    CHECK(remapped->name == "Library Green");
    CHECK(remapped->tile.pixel(0, 0)[1] == 210);
    CHECK(document.metadata().patterns.find(kCollisionId)->tile.pixel(0, 0)[0] == 220);
    overlay_check->setChecked(true);
    dialog->accept();
  }));

  const auto settings = patchy::ui::request_layer_style_settings(
      nullptr, layer, {}, &document.metadata().patterns, &library);
  CHECK(settings.has_value());
  CHECK(settings->style.pattern_overlays.size() == 1);
  CHECK(QString::fromStdString(settings->style.pattern_overlays.front().pattern_id) ==
        remapped_id);
  CHECK(settings->style.pattern_overlays.front().pattern_name == "Library Green");
  CHECK(document.metadata().patterns.patterns.size() == 2);
}

void ui_layer_style_library_pattern_cancel_and_undo_restore_store() {
  clear_pattern_test_state();
  clear_brush_tip_test_state();
  {
    patchy::ui::MainWindow window;
    auto& library = window.pattern_library();
    QString error;
    QStringList warnings;
    const auto fixture = QString::fromStdString(
        patchy::test::committed_format_fixture_path("pat", "hue.pat").string());
    const auto storage_id = library.import_pat(fixture, error, warnings);
    CHECK(!storage_id.isEmpty());
    CHECK(error.isEmpty() && warnings.isEmpty());
    const auto* imported = library.find_entry(storage_id);
    CHECK(imported != nullptr);
    const auto imported_id = imported->id.toStdString();

    patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
    patchy::PatternResource embedded;
    embedded.id = imported_id;
    embedded.name = "Embedded Collision";
    embedded.tile = solid_pixels(3, 3, patchy::PixelFormat::rgba8(),
                                 QColor(180, 20, 30, 255));
    embedded.provenance = patchy::PatternProvenance::ImportedRaw;
    document.metadata().patterns.adopt(embedded);
    patchy::Layer layer(document.allocate_layer_id(), "Transient Pattern",
                        solid_pixels(48, 36, patchy::PixelFormat::rgba8(),
                                     QColor(80, 140, 220, 255)));
    const auto layer_id = layer.id();
    document.add_layer(std::move(layer));
    document.set_active_layer(layer_id);
    window.add_document_session(std::move(document), QStringLiteral("Pattern Undo"));
    show_window(window);

    const auto drive_dialog = [&](bool accept, bool& preview_materialized,
                                  std::string& selected_alias) {
      QTimer::singleShot(0, &window, guarded([&] {
        auto* dialog = qobject_cast<QDialog*>(
            find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
        CHECK(dialog != nullptr);
        auto* overlay_check = dialog->findChild<QCheckBox*>(
            QStringLiteral("layerStylePatternOverlayCategoryCheck"));
        auto* preview_check =
            dialog->findChild<QCheckBox*>(QStringLiteral("layerStylePreviewCheck"));
        auto* manage = dialog->findChild<QPushButton*>(
            QStringLiteral("layerStylePatternOverlayManageButton"));
        auto* combo = dialog->findChild<QComboBox*>(
            QStringLiteral("layerStylePatternOverlayPatternCombo"));
        auto* categories =
            dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
        CHECK(overlay_check != nullptr && preview_check != nullptr && manage != nullptr &&
              combo != nullptr && categories != nullptr);
        const auto overlay_items =
            categories->findItems(QStringLiteral("Pattern Overlay"), Qt::MatchExactly);
        CHECK(!overlay_items.empty());
        categories->setCurrentItem(overlay_items.front());
        QApplication::processEvents();
        QTimer::singleShot(0, guarded([&] {
          auto* manager = qobject_cast<QDialog*>(
              find_top_level_dialog(QStringLiteral("patternManagerDialog")));
          CHECK(manager != nullptr);
          auto* use =
              manager->findChild<QPushButton*>(QStringLiteral("patternManagerUseButton"));
          CHECK(use != nullptr && use->isEnabled());
          use->click();
        }));
        manage->click();
        selected_alias = combo->currentData().toString().toStdString();
        CHECK(!selected_alias.empty() && selected_alias != imported_id);
        overlay_check->setChecked(true);
        QApplication::processEvents();
        preview_materialized =
            patchy::ui::MainWindowTestAccess::document(window).metadata().patterns.find(
                selected_alias) != nullptr;
        if (!accept) {
          preview_check->setChecked(false);
          QApplication::processEvents();
          const auto& preview_off_patterns =
              patchy::ui::MainWindowTestAccess::document(window).metadata().patterns;
          CHECK(preview_off_patterns.patterns.size() == 1);
          CHECK(preview_off_patterns.find(imported_id) != nullptr);
          CHECK(preview_off_patterns.find(imported_id)->tile.pixel(0, 0)[0] == 180);
          preview_check->setChecked(true);
          QApplication::processEvents();
          CHECK(patchy::ui::MainWindowTestAccess::document(window)
                    .metadata()
                    .patterns.find(selected_alias) != nullptr);
        }
        if (accept) {
          dialog->accept();
        } else {
          dialog->reject();
        }
      }));
      require_action(window, "layerBlendingOptionsAction")->trigger();
      QApplication::processEvents();
    };

    bool cancel_preview_materialized = false;
    std::string canceled_alias;
    drive_dialog(false, cancel_preview_materialized, canceled_alias);
    CHECK(cancel_preview_materialized);
    auto& after_cancel = patchy::ui::MainWindowTestAccess::document(window);
    CHECK(after_cancel.metadata().patterns.patterns.size() == 1);
    CHECK(after_cancel.metadata().patterns.find(imported_id) != nullptr);
    CHECK(after_cancel.metadata().patterns.find(canceled_alias) == nullptr);
    const auto* canceled_layer = after_cancel.find_layer(layer_id);
    CHECK(canceled_layer != nullptr);
    CHECK(canceled_layer->layer_style().pattern_overlays.empty());

    bool commit_preview_materialized = false;
    std::string committed_alias;
    drive_dialog(true, commit_preview_materialized, committed_alias);
    CHECK(commit_preview_materialized);
    auto& after_commit = patchy::ui::MainWindowTestAccess::document(window);
    CHECK(after_commit.metadata().patterns.find(imported_id) != nullptr);
    const auto* committed_pattern = after_commit.metadata().patterns.find(committed_alias);
    CHECK(committed_pattern != nullptr);
    CHECK(committed_pattern->name == "hue");
    CHECK(committed_pattern->tile.pixel(0, 0)[0] == 255);
    const auto* committed_layer = after_commit.find_layer(layer_id);
    CHECK(committed_layer != nullptr);
    CHECK(committed_layer->layer_style().pattern_overlays.size() == 1);
    CHECK(committed_layer->layer_style().pattern_overlays.front().pattern_id == committed_alias);

    const auto* undo_command = window.hotkey_registry().find_command(QStringLiteral("edit.undo"));
    CHECK(undo_command != nullptr && undo_command->action != nullptr);
    undo_command->action->trigger();
    QApplication::processEvents();
    auto& after_undo = patchy::ui::MainWindowTestAccess::document(window);
    CHECK(after_undo.metadata().patterns.patterns.size() == 1);
    CHECK(after_undo.metadata().patterns.find(imported_id) != nullptr);
    CHECK(after_undo.metadata().patterns.find(committed_alias) == nullptr);
    const auto* undone_layer = after_undo.find_layer(layer_id);
    CHECK(undone_layer != nullptr);
    CHECK(undone_layer->layer_style().pattern_overlays.empty());
  }
  clear_pattern_test_state();
  clear_brush_tip_test_state();
}

[[nodiscard]] patchy::LayerStyle style_test_shadow_recipe() {
  patchy::LayerStyle style;
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.color = {200, 16, 32};
  shadow.opacity = 0.7F;
  shadow.angle_degrees = 120.0F;
  shadow.distance = 9.0F;
  shadow.size = 3.0F;
  style.drop_shadows.push_back(shadow);
  return style;
}

// Depth-first search for the browser row carrying a storage id.
[[nodiscard]] QTreeWidgetItem* style_tree_item_for_storage_id(QTreeWidget& tree,
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
  for (int index = 0; index < tree.topLevelItemCount(); ++index) {
    if (auto* found = visit(tree.topLevelItem(index)); found != nullptr) {
      return found;
    }
  }
  return nullptr;
}

void click_style_tree_item(QTreeWidget& tree, QTreeWidgetItem* item) {
  CHECK(item != nullptr);
  if (item->parent() != nullptr) {
    item->parent()->setExpanded(true);
  }
  tree.scrollToItem(item);
  QApplication::processEvents();
  const auto center = tree.visualItemRect(item).center();
  send_mouse(*tree.viewport(), QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*tree.viewport(), QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
}

void ui_style_library_defaults_restore_export_import_round_trip() {
  QTemporaryDir directory;
  CHECK(directory.isValid());
  patchy::ui::StyleLibrary library(directory.filePath(QStringLiteral("library")));
  CHECK(library.entries().empty());
  CHECK(library.restore_default_styles() == 39);
  CHECK(library.entries().size() == 39U);
  CHECK(library.has_all_default_styles_introduced_after(0));
  CHECK(library.default_styles_match_factory());
  CHECK(library.folders().size() == 3);
  for (const auto& entry : library.entries()) {
    CHECK(!entry.thumbnail.isNull());
    CHECK(!entry.blend_settings.has_value());
  }

  // Contact sheet of every built-in preset thumbnail for visual review.
  {
    constexpr int kColumns = 7;
    const auto cell = patchy::ui::kStyleThumbnailExtent;
    const auto count = static_cast<int>(library.entries().size());
    const auto rows = (count + kColumns - 1) / kColumns;
    QImage sheet(kColumns * cell, rows * cell, QImage::Format_RGB32);
    sheet.fill(QColor(0x2B, 0x2B, 0x2B));
    QPainter painter(&sheet);
    int index = 0;
    for (const auto& entry : library.entries()) {
      painter.drawPixmap((index % kColumns) * cell, (index / kColumns) * cell, entry.thumbnail);
      ++index;
    }
    painter.end();
    ensure_artifact_dir();
    CHECK(sheet.save(QStringLiteral("test-artifacts/style_preset_thumbnails.png")));
  }

  // Rename drifts from factory; the explicit reset repairs it.
  const auto* adventure =
      library.find_entry_by_style_id(QStringLiteral("57a1e500-0001-4c6d-8f2a-9b3d4e55c001"));
  CHECK(adventure != nullptr);
  CHECK(patchy::ui::style_library_entry_display_name(*adventure) == QStringLiteral("Adventure"));
  const auto adventure_storage = adventure->storage_id;
  CHECK(library.rename_style(adventure_storage, QStringLiteral("Renamed")));
  CHECK(!library.default_styles_match_factory());
  CHECK(library.reset_default_styles_to_factory() == 1);
  CHECK(library.default_styles_match_factory());

  // Export the Text folder; Stamped Steel carries its two pattern tiles.
  QStringList text_ids;
  const auto text_folder = patchy::ui::style_preset_folder_display_name("Text");
  for (const auto& entry : library.entries()) {
    if (entry.folder == text_folder) {
      text_ids.append(entry.storage_id);
    }
  }
  CHECK(text_ids.size() == 20);
  const auto exported_path = directory.filePath(QStringLiteral("Text.asl"));
  QString error;
  CHECK(library.export_asl(text_ids, exported_path, error));
  CHECK(error.isEmpty());
  {
    QFile exported(exported_path);
    CHECK(exported.open(QIODevice::ReadOnly));
    const auto bytes = exported.readAll();
    std::string parse_error;
    const auto parsed = patchy::psd::read_asl(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                      static_cast<std::size_t>(bytes.size())),
        parse_error);
    CHECK(parsed.has_value());
    CHECK(parsed->styles.size() == 20U);
    CHECK(parsed->patterns.size() == 2U);  // Brushed Metal + Bumps (Stamped Steel)
  }

  // Import into a second library: grouped under the file name, dedupe on
  // re-import, duplicates get fresh ids, deletes persist across reloads.
  patchy::ui::StyleLibrary second(directory.filePath(QStringLiteral("second")));
  QStringList warnings;
  const auto first_imported = second.import_asl(exported_path, error, warnings);
  CHECK(!first_imported.isEmpty());
  CHECK(error.isEmpty());
  CHECK(warnings.isEmpty());
  CHECK(second.entries().size() == 20U);
  CHECK(second.folders() == QStringList{QStringLiteral("Text")});
  CHECK(second.import_asl(exported_path, error, warnings) == first_imported);
  CHECK(second.entries().size() == 20U);
  const auto duplicate_storage = second.duplicate_style(first_imported);
  CHECK(!duplicate_storage.isEmpty());
  const auto* duplicate = second.find_entry(duplicate_storage);
  CHECK(duplicate != nullptr);
  CHECK(duplicate->id != second.find_entry(first_imported)->id);
  patchy::ui::StyleLibrary reloaded(directory.filePath(QStringLiteral("second")));
  CHECK(reloaded.entries().size() == 21U);
  int changed_signals = 0;
  QObject::connect(&second, &patchy::ui::StyleLibrary::changed, &second,
                   [&changed_signals] { ++changed_signals; });
  CHECK(second.remove_styles({first_imported, duplicate_storage}) == 2);
  CHECK(changed_signals == 1);
}

void ui_style_library_rejects_oversized_asl_before_read() {
  QTemporaryDir directory;
  CHECK(directory.isValid());
  // 512 MiB since GitHub issue 741 (32 MiB before), shared with the core reader.
  const auto limit = static_cast<qint64>(patchy::psd::kMaxAslFileBytes);
  CHECK(limit == 512LL * 1024LL * 1024LL);
  const auto oversized_path = directory.filePath(QStringLiteral("oversized.asl"));
  QFile oversized(oversized_path);
  CHECK(oversized.open(QIODevice::WriteOnly));
  CHECK(oversized.resize(limit + 1LL));
  oversized.close();

  patchy::ui::StyleLibrary library(directory.filePath(QStringLiteral("library")));
  QString error;
  QStringList warnings;
  int steps = 0;
  CHECK(library
            .import_asl(oversized_path, error, warnings,
                        [&steps](int, int) {
                          ++steps;
                          return true;
                        })
            .isEmpty());
  CHECK(error == QStringLiteral("\"oversized.asl\" is too large to import safely."));
  CHECK(steps == 0);  // refused from its size alone, before the read
  CHECK(warnings.isEmpty());
  CHECK(library.entries().empty());

  // The library rereads each stored style under the same limit: a stored file past it is
  // passed over without being read.
  const auto stored_dir = directory.filePath(QStringLiteral("stored"));
  CHECK(QDir().mkpath(stored_dir));
  CHECK(QFile::copy(oversized_path, QDir(stored_dir).filePath(QStringLiteral("too-big.asl"))));
  CHECK(patchy::ui::StyleLibrary(stored_dir).entries().empty());

  // The limit is inclusive: a file of exactly that size passes the size check and is read
  // (then turned down as not an ASL file, since it is all zeros).
  CHECK(QFile::resize(oversized_path, limit));
  CHECK(library.import_asl(oversized_path, error, warnings).isEmpty());
  CHECK(error.contains(QStringLiteral("is not a supported Photoshop ASL")));
  CHECK(library.entries().empty());
}

// `count` styles for the import tests, each a Pattern Overlay on its own `extent` pixel
// tile of one flat color, so a rendered thumbnail shows which pattern a style kept.
struct Issue741Styles {
  std::vector<patchy::psd::AslStyle> styles;
  std::vector<patchy::PatternResource> patterns;
};

[[nodiscard]] Issue741Styles issue_741_styles(int count, int extent) {
  Issue741Styles result;
  for (int index = 0; index < count; ++index) {
    patchy::PatternResource pattern;
    pattern.id = "74100000-0000-4000-8000-00000000030" + std::to_string(index + 1);
    pattern.name = "Texture " + std::to_string(index + 1);
    pattern.tile = solid_pixels(extent, extent, patchy::PixelFormat::rgba8(),
                                QColor(230 - 90 * index, 40 + 90 * index, 60));
    patchy::psd::AslStyle style;
    style.id = "74100000-0000-4000-8000-00000000040" + std::to_string(index + 1);
    style.name = "Textured " + std::to_string(index + 1);
    patchy::LayerPatternOverlay overlay;
    overlay.enabled = true;
    overlay.pattern_id = pattern.id;
    overlay.pattern_name = pattern.name;
    style.style.pattern_overlays.push_back(std::move(overlay));
    result.styles.push_back(std::move(style));
    result.patterns.push_back(std::move(pattern));
  }
  return result;
}

[[nodiscard]] QString issue_741_write_asl(const QTemporaryDir& directory, const QString& name,
                                          const Issue741Styles& content) {
  const auto bytes = patchy::psd::write_asl(content.styles, content.patterns);
  const auto path = directory.filePath(name);
  QFile file(path);
  CHECK(file.open(QIODevice::WriteOnly));
  CHECK(file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size())) ==
        static_cast<qint64>(bytes.size()));
  file.close();
  return path;
}

// GitHub issue 741: a style library past the old 32 MiB limit imports, and every style
// reopens with its embedded pattern and renders with it.
void ui_style_library_imports_asl_past_the_old_limit() {
  QTemporaryDir directory;
  CHECK(directory.isValid());
  constexpr int kExtent = 2048;
  const auto content = issue_741_styles(3, kExtent);
  const auto path = issue_741_write_asl(directory, QStringLiteral("Textures.asl"), content);
  CHECK(QFileInfo(path).size() > 32LL * 1024LL * 1024LL);

  const auto library_dir = directory.filePath(QStringLiteral("library"));
  std::vector<std::pair<int, int>> steps;
  QString first_storage_id;
  {
    patchy::ui::StyleLibrary library(library_dir);
    QString error;
    QStringList warnings;
    first_storage_id = library.import_asl(path, error, warnings, [&steps](int done, int total) {
      steps.emplace_back(done, total);
      return true;
    });
    CHECK(!first_storage_id.isEmpty());
    CHECK(error.isEmpty());
    CHECK(warnings.isEmpty());
    CHECK(library.entries().size() == 3U);
    CHECK(library.folders() == QStringList{QStringLiteral("Textures")});
  }
  // Once before the read, then once before each style is stored.
  CHECK(steps == (std::vector<std::pair<int, int>>{{0, 0}, {0, 3}, {1, 3}, {2, 3}}));

  patchy::ui::StyleLibrary reopened(library_dir);
  CHECK(reopened.entries().size() == 3U);
  std::vector<QImage> thumbnails;
  for (std::size_t index = 0; index < content.styles.size(); ++index) {
    const auto* entry = reopened.find_entry_by_style_id(QString::fromStdString(content.styles[index].id));
    CHECK(entry != nullptr);
    CHECK(entry->style.pattern_overlays.size() == 1U);
    const auto embedded = reopened.patterns_for_entry(entry->storage_id);
    CHECK(embedded.size() == 1U);  // only the tile this style uses, not the file's other two
    CHECK(embedded.front().id == content.patterns[index].id);
    CHECK(embedded.front().tile.width() == kExtent && embedded.front().tile.height() == kExtent);
    CHECK(std::equal(embedded.front().tile.data().begin(), embedded.front().tile.data().end(),
                     content.patterns[index].tile.data().begin()));
    CHECK(!entry->thumbnail.isNull());
    thumbnails.push_back(entry->thumbnail.toImage());
    // The stored style paints its own tile: without the tile the preview differs.
    const auto with_pattern =
        patchy::ui::render_style_preview(entry->style, entry->blend_settings, embedded, 96).toImage();
    const auto without_pattern =
        patchy::ui::render_style_preview(entry->style, entry->blend_settings, {}, 96).toImage();
    CHECK(with_pattern == thumbnails.back());
    CHECK(with_pattern != without_pattern);
  }
  CHECK(thumbnails[0] != thumbnails[1]);
  CHECK(thumbnails[1] != thumbnails[2]);

  // Opening the library reads the styles and their cached thumbnails, not the tiles. A
  // thumbnail that went missing is drawn again from the stored tile.
  const auto* first_entry = reopened.find_entry(first_storage_id);
  CHECK(first_entry != nullptr);
  const auto first_thumbnail = first_entry->thumbnail.toImage();
  const auto thumbnail_file = QDir(library_dir).filePath(first_storage_id + QStringLiteral(".png"));
  CHECK(QFile::remove(thumbnail_file));
  {
    patchy::ui::StyleLibrary redrawn(library_dir);
    const auto* entry = redrawn.find_entry(first_storage_id);
    CHECK(entry != nullptr);
    CHECK(entry->thumbnail.toImage() == first_thumbnail);
    CHECK(QFileInfo::exists(thumbnail_file));
  }

  // Importing the same file again adds nothing.
  QString error;
  QStringList warnings;
  CHECK(reopened.import_asl(path, error, warnings) == first_storage_id);
  CHECK(reopened.entries().size() == 3U);
}

// The step callback can stop an import between two styles, or before the read.
void ui_style_library_import_stops_from_its_step() {
  QTemporaryDir directory;
  CHECK(directory.isValid());
  const auto content = issue_741_styles(3, 4);
  const auto path = issue_741_write_asl(directory, QStringLiteral("Small.asl"), content);

  // Stopped when two of the three are stored: those two stay, with no error.
  patchy::ui::StyleLibrary library(directory.filePath(QStringLiteral("library")));
  int changed = 0;
  QObject::connect(&library, &patchy::ui::StyleLibrary::changed, &library, [&changed] { ++changed; });
  QString error;
  QStringList warnings;
  const auto first = library.import_asl(path, error, warnings, [](int done, int) { return done < 2; });
  CHECK(!first.isEmpty());
  CHECK(error.isEmpty());
  CHECK(library.entries().size() == 2U);
  CHECK(library.find_entry_by_style_id(QString::fromStdString(content.styles[1].id)) != nullptr);
  CHECK(library.find_entry_by_style_id(QString::fromStdString(content.styles[2].id)) == nullptr);
  CHECK(changed == 1);

  // Stopped before the file is read: nothing is imported, and that is not an error.
  patchy::ui::StyleLibrary untouched(directory.filePath(QStringLiteral("untouched")));
  CHECK(untouched.import_asl(path, error, warnings, [](int, int) { return false; }).isEmpty());
  CHECK(error.isEmpty());
  CHECK(untouched.entries().empty());
  // Stopped after the read, before the first style: the same.
  CHECK(untouched.import_asl(path, error, warnings, [](int, int total) { return total == 0; }).isEmpty());
  CHECK(error.isEmpty());
  CHECK(untouched.entries().empty());
}

// The managers' import progress dialog (GitHub issue 741): at once for a large file, not
// at all for a quick small one, and Cancel stops the import at its next step.
void ui_preset_import_progress_dialog_shows_for_large_files_only() {
  QWidget parent;
  const auto label = [](int item, int total) { return QStringLiteral("Item %1 of %2").arg(item).arg(total); };
  {
    patchy::ui::PresetImportProgressDialog progress(
        &parent, QStringLiteral("testImportProgressSmall"), QStringLiteral("Import"), QStringLiteral("Reading"),
        QStringLiteral("Cancel"), label, patchy::ui::PresetImportProgressDialog::kShowAtOnceBytes - 1);
    const auto step = progress.step();
    auto* dialog = parent.findChild<QProgressDialog*>(QStringLiteral("testImportProgressSmall"));
    CHECK(dialog != nullptr);
    CHECK(step(0, 0));
    CHECK(step(0, 3));
    CHECK(step(1, 3));
    CHECK(!dialog->isVisible());
    CHECK(!progress.cancelled());
  }
  CHECK(parent.findChild<QProgressDialog*>() == nullptr);
  {
    patchy::ui::PresetImportProgressDialog progress(
        &parent, QStringLiteral("testImportProgressLarge"), QStringLiteral("Import"), QStringLiteral("Reading"),
        QStringLiteral("Cancel"), label, patchy::ui::PresetImportProgressDialog::kShowAtOnceBytes);
    const auto step = progress.step();
    auto* dialog = parent.findChild<QProgressDialog*>(QStringLiteral("testImportProgressLarge"));
    CHECK(dialog != nullptr);
    CHECK(!dialog->isVisible());
    CHECK(step(0, 0));  // before the file is read
    CHECK(dialog->isVisible());
    CHECK(dialog->windowModality() == Qt::WindowModal);
    CHECK(dialog->windowTitle() == QStringLiteral("Import"));
    CHECK(dialog->labelText() == QStringLiteral("Reading"));
    CHECK(dialog->maximum() == 0);  // busy, no count yet
    CHECK(step(1, 3));  // one stored, the second of three next
    CHECK(dialog->labelText() == QStringLiteral("Item 2 of 3"));
    CHECK(dialog->maximum() == 3);
    CHECK(dialog->value() == 1);
    CHECK(step(2, 3));
    CHECK(dialog->labelText() == QStringLiteral("Item 3 of 3"));
    dialog->cancel();
    CHECK(!step(3, 3));
    CHECK(progress.cancelled());
    CHECK(!dialog->isVisible());  // Cancel hid it and the last step did not bring it back
  }
  CHECK(parent.findChild<QProgressDialog*>() == nullptr);
}

// GitHub issue 741 through the Style Manager's Import button: a 37 MB library (refused
// as too large before) shows the progress dialog while it imports and lists its styles
// after.
void ui_style_manager_imports_large_asl_with_progress() {
  QTemporaryDir directory;
  CHECK(directory.isValid());
  const auto content = issue_741_styles(3, 2048);
  const auto path = issue_741_write_asl(directory, QStringLiteral("Scrapbook Textures.asl"), content);
  CHECK(QFileInfo(path).size() > 32LL * 1024LL * 1024LL);
  patchy::ui::StyleLibrary library(directory.filePath(QStringLiteral("library")));

  auto saw_progress = false;
  QStringList progress_labels;
  when_dialog_visible(QStringLiteral("styleManagerDialog"), [&](QDialog& manager) {
    auto* import_button = manager.findChild<QPushButton*>(QStringLiteral("styleManagerImportButton"));
    auto* tree = manager.findChild<patchy::ui::StyleBrowserWidget*>(QStringLiteral("styleManagerTree"));
    CHECK(import_button != nullptr && tree != nullptr);
    when_dialog_visible(QStringLiteral("styleManagerImportFileDialog"), [&path](QDialog& dialog) {
      auto* files = qobject_cast<QFileDialog*>(&dialog);
      CHECK(files != nullptr);
      // selectFile leaves a focused name field alone, so the name is typed as well.
      files->selectFile(path);
      if (auto* name_edit = files->findChild<QLineEdit*>(QStringLiteral("fileNameEdit")); name_edit != nullptr) {
        name_edit->setText(path);
      }
      QApplication::processEvents();
      dialog.accept();
    });
    // The import runs inside the click; its progress dialog pumps events, so a timer
    // sees the dialog while the file is read and the styles are stored.
    QTimer watcher;
    watcher.setInterval(0);
    QObject::connect(&watcher, &QTimer::timeout, &manager, guarded([&] {
      auto* progress =
          qobject_cast<QProgressDialog*>(find_top_level_dialog(QStringLiteral("styleManagerImportProgress")));
      if (progress == nullptr || !progress->isVisible()) {
        return;
      }
      if (!saw_progress) {
        saw_progress = true;
        CHECK(progress->windowTitle() == QStringLiteral("Import Styles"));
        save_widget_artifact("ui_style_manager_import_progress", *progress);
      }
      if (progress_labels.isEmpty() || progress_labels.back() != progress->labelText()) {
        progress_labels.append(progress->labelText());
      }
    }));
    watcher.start();
    import_button->click();
    watcher.stop();
    QApplication::processEvents();
    CHECK(saw_progress);
    CHECK(progress_labels.front() == QStringLiteral("Reading \"Scrapbook Textures.asl\"..."));
    CHECK(progress_labels.contains(QStringLiteral("Importing style 3 of 3...")));
    CHECK(find_top_level_dialog(QStringLiteral("styleManagerImportProgress")) == nullptr);
    CHECK(library.entries().size() == 3U);
    CHECK(!tree->current_storage_id().isEmpty());  // the first imported style is selected
    save_widget_artifact("ui_style_manager_after_large_import", manager);
    manager.reject();
  });
  (void)patchy::ui::request_style_manager(nullptr, library, {});
  CHECK(library.entries().size() == 3U);
  CHECK(library.folders() == QStringList{QStringLiteral("Scrapbook Textures")});
}

void ui_layer_style_styles_page_applies_preset_and_previews() {
  QTemporaryDir directory;
  CHECK(directory.isValid());
  patchy::ui::StyleLibrary library(directory.filePath(QStringLiteral("library")));
  patchy::psd::AslBlendSettings blend_settings;
  blend_settings.opacity = 63;
  blend_settings.blend_mode = patchy::BlendMode::Multiply;
  blend_settings.blend_if.channels[0].this_layer = {11, 37, 201, 239};
  const auto storage_id =
      library.add_style(QStringLiteral("Crimson Shadow"), style_test_shadow_recipe(),
                        blend_settings, {}, QStringLiteral("Test Folder"));
  CHECK(!storage_id.isEmpty());

  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Styled",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  // The grid shows by default (GitHub issue 606); a click on a cell applies.
  patchy::ui::set_stored_layer_style_styles_grid_view(true);

  QTimer::singleShot(0, guarded([&] {
    auto* dialog =
        qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
    // PresetGridWidget has no Q_OBJECT: find it as its QListWidget base.
    auto* browser = dynamic_cast<patchy::ui::PresetGridWidget*>(
        dialog->findChild<QListWidget*>(QStringLiteral("layerStyleStylesGrid")));
    auto* opacity = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleOpacitySpin"));
    auto* blend = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleBlendModeCombo"));
    CHECK(categories != nullptr && browser != nullptr && opacity != nullptr && blend != nullptr);
    // The Styles row is first; the dialog still opens on Blending Options.
    const auto styles_items = categories->findItems(QStringLiteral("Style Presets"), Qt::MatchExactly);
    CHECK(!styles_items.empty());
    CHECK(categories->row(styles_items.front()) == 0);
    CHECK(categories->currentRow() == 1);
    categories->setCurrentItem(styles_items.front());
    QApplication::processEvents();
    CHECK(!browser->visibleRegion().isEmpty());

    auto* cell = browser->entry_item(storage_id);
    CHECK(cell != nullptr);
    if (cell != nullptr) {
      browser->scrollToItem(cell);
      QApplication::processEvents();
      const auto center = browser->visualItemRect(cell).center();
      send_mouse(*browser->viewport(), QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
      send_mouse(*browser->viewport(), QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
    }
    QApplication::processEvents();
    // The preset's effects land in the category list and its blending options
    // land in the master controls.
    const auto shadow_items = categories->findItems(QStringLiteral("Drop Shadow"), Qt::MatchExactly);
    CHECK(!shadow_items.empty());
    CHECK(shadow_items.front()->checkState() == Qt::Checked);
    CHECK(opacity->value() == 63);
    CHECK(blend->currentData().toInt() == static_cast<int>(patchy::BlendMode::Multiply));
    // The Styles row stays selected for continued browsing.
    CHECK(categories->currentRow() == 0);
    save_widget_artifact("ui_layer_style_styles_page", *dialog);
    QTimer::singleShot(50, dialog, [dialog] { dialog->accept(); });
  }));

  int previews = 0;
  const auto settings = patchy::ui::request_layer_style_settings(
      nullptr, layer, [&](const patchy::ui::LayerStyleSettings&) { ++previews; }, nullptr, nullptr,
      &library);
  CHECK(settings.has_value());
  CHECK(previews >= 1);
  CHECK(settings->opacity == 63);
  CHECK(settings->blend_mode == patchy::BlendMode::Multiply);
  CHECK(settings->blend_if.channels[0].this_layer == (patchy::BlendIfThresholds{11, 37, 201, 239}));
  CHECK(settings->style.drop_shadows.size() == 1U);
  const auto& shadow = settings->style.drop_shadows.front();
  CHECK(shadow.enabled);
  CHECK(shadow.color.red == 200 && shadow.color.green == 16 && shadow.color.blue == 32);
  CHECK(shadow.distance == 9.0F);
}

void ui_layer_style_no_style_entry_clears_effects() {
  QTemporaryDir directory;
  CHECK(directory.isValid());
  patchy::ui::StyleLibrary library(directory.filePath(QStringLiteral("library")));

  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Styled",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  layer.layer_style() = style_test_shadow_recipe();
  layer.set_opacity(0.4F);
  patchy::ui::set_stored_layer_style_styles_grid_view(true);

  QTimer::singleShot(0, guarded([&] {
    auto* dialog =
        qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
    auto* browser =
        dialog->findChild<patchy::ui::StyleBrowserWidget*>(QStringLiteral("layerStyleStylesBrowser"));
    auto* list_button = dialog->findChild<QToolButton*>(QStringLiteral("layerStyleStylesListViewButton"));
    CHECK(categories != nullptr && browser != nullptr && list_button != nullptr);
    const auto styles_items = categories->findItems(QStringLiteral("Style Presets"), Qt::MatchExactly);
    CHECK(!styles_items.empty());
    categories->setCurrentItem(styles_items.front());
    QApplication::processEvents();
    // The list view keeps the No Style row (the grid has a No Style cell).
    list_button->click();
    QApplication::processEvents();
    CHECK(!browser->visibleRegion().isEmpty());
    CHECK(browser->topLevelItemCount() >= 1);
    auto* no_style = browser->topLevelItem(0);
    CHECK(no_style->text(0).contains(QStringLiteral("No Style")));
    click_style_tree_item(*browser, no_style);
    QApplication::processEvents();
    const auto shadow_items = categories->findItems(QStringLiteral("Drop Shadow"), Qt::MatchExactly);
    CHECK(!shadow_items.empty());
    CHECK(shadow_items.front()->checkState() == Qt::Unchecked);
    QTimer::singleShot(50, dialog, [dialog] { dialog->accept(); });
  }));

  const auto settings = patchy::ui::request_layer_style_settings(nullptr, layer, {}, nullptr,
                                                                 nullptr, &library);
  CHECK(settings.has_value());
  CHECK(settings->style.drop_shadows.empty());
  CHECK(settings->style.strokes.empty());
  // Blending options survive No Style.
  CHECK(settings->opacity == 40);
  patchy::ui::set_stored_layer_style_styles_grid_view(true);
}

void ui_layer_style_new_style_saves_current_settings() {
  QTemporaryDir directory;
  CHECK(directory.isValid());
  patchy::ui::StyleLibrary library(directory.filePath(QStringLiteral("library")));

  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Styled",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  layer.layer_style() = style_test_shadow_recipe();
  layer.set_opacity(0.35F);
  layer.set_blend_mode(patchy::BlendMode::Screen);

  QTimer::singleShot(0, guarded([&] {
    auto* dialog =
        qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
    auto* new_style = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleNewStyleButton"));
    CHECK(categories != nullptr && new_style != nullptr);
    const auto styles_items = categories->findItems(QStringLiteral("Style Presets"), Qt::MatchExactly);
    CHECK(!styles_items.empty());
    categories->setCurrentItem(styles_items.front());
    QApplication::processEvents();
    QTimer::singleShot(0, guarded([&] {
      auto* prompt =
          qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("layerStyleNewStyleDialog")));
      CHECK(prompt != nullptr);
      auto* name = prompt->findChild<QLineEdit*>(QStringLiteral("layerStyleNewStyleNameEdit"));
      auto* folder = prompt->findChild<QComboBox*>(QStringLiteral("layerStyleNewStyleFolderCombo"));
      auto* include_blend =
          prompt->findChild<QCheckBox*>(QStringLiteral("layerStyleNewStyleIncludeBlendCheck"));
      CHECK(name != nullptr && folder != nullptr && include_blend != nullptr);
      name->setText(QStringLiteral("Saved From Dialog"));
      folder->setCurrentText(QStringLiteral("My Styles"));
      include_blend->setChecked(true);
      prompt->accept();
    }));
    new_style->click();
    QApplication::processEvents();
    QTimer::singleShot(50, dialog, [dialog] { dialog->reject(); });
  }));

  const auto settings = patchy::ui::request_layer_style_settings(nullptr, layer, {}, nullptr,
                                                                 nullptr, &library);
  CHECK(!settings.has_value());  // dialog canceled; the preset was saved anyway
  CHECK(library.entries().size() == 1U);
  const auto& entry = library.entries().front();
  CHECK(entry.name == QStringLiteral("Saved From Dialog"));
  CHECK(entry.folder == QStringLiteral("My Styles"));
  CHECK(patchy::psd::photoshop_lfx2_layer_style_payload(entry.style) ==
        patchy::psd::photoshop_lfx2_layer_style_payload(style_test_shadow_recipe()));
  CHECK(entry.blend_settings.has_value());
  CHECK(entry.blend_settings->opacity == 35);
  CHECK(entry.blend_settings->blend_mode == patchy::BlendMode::Screen);
}

void ui_style_browser_folder_selection_exports_asl() {
  QTemporaryDir directory;
  CHECK(directory.isValid());
  patchy::ui::StyleLibrary library(directory.filePath(QStringLiteral("library")));
  const auto in_folder_a =
      library.add_style(QStringLiteral("A One"), style_test_shadow_recipe(), std::nullopt, {},
                        QStringLiteral("Folder A"));
  const auto in_folder_a2 =
      library.add_style(QStringLiteral("A Two"), style_test_shadow_recipe(), std::nullopt, {},
                        QStringLiteral("Folder A"));
  const auto in_folder_b =
      library.add_style(QStringLiteral("B One"), style_test_shadow_recipe(), std::nullopt, {},
                        QStringLiteral("Folder B"));
  CHECK(!in_folder_a.isEmpty() && !in_folder_a2.isEmpty() && !in_folder_b.isEmpty());

  patchy::ui::StyleBrowserWidget browser(&library);
  browser.reload();
  browser.show();
  QApplication::processEvents();
  // Selecting the Folder A row expands to its two children.
  QTreeWidgetItem* folder_a = nullptr;
  for (int index = 0; index < browser.topLevelItemCount(); ++index) {
    auto* item = browser.topLevelItem(index);
    if (item->data(0, Qt::UserRole).toString() == QStringLiteral("Folder A")) {
      folder_a = item;
    }
  }
  CHECK(folder_a != nullptr);
  browser.setCurrentItem(folder_a);
  const auto ids = browser.selected_storage_ids();
  CHECK(ids.size() == 2);
  CHECK(ids.contains(in_folder_a) && ids.contains(in_folder_a2));

  const auto exported_path = directory.filePath(QStringLiteral("Folder A.asl"));
  CHECK(browser.export_selection_to(exported_path));
  QFile exported(exported_path);
  CHECK(exported.open(QIODevice::ReadOnly));
  const auto bytes = exported.readAll();
  std::string parse_error;
  const auto parsed = patchy::psd::read_asl(
      std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                    static_cast<std::size_t>(bytes.size())),
      parse_error);
  CHECK(parsed.has_value());
  CHECK(parsed->styles.size() == 2U);
  CHECK(parsed->styles[0].name == "A One");
  CHECK(parsed->styles[1].name == "A Two");
}

// Every top-level dialog the capture tooling opens without a MainWindow, grabbed
// offscreen in Dark and Light through capture_widget_artifact, whose theme guard
// (capture_theme_mismatch) rejects an unthemed or wrong-scheme widget instead of
// saving the picture. A new dialog joins this list; a theme regression fails here,
// not on a shot someone has to look at.
void ui_offscreen_dialog_sweep_is_themed_in_both_schemes() {
  ColorSchemeRestorer restore_active;
  QTemporaryDir directory;
  patchy::ui::GradientLibrary gradients(directory.filePath(QStringLiteral("gradients")));
  CHECK(gradients.restore_default_gradients() == 20);
  patchy::ui::PatternLibrary patterns(directory.filePath(QStringLiteral("patterns")));
  patchy::ui::StyleLibrary styles(directory.filePath(QStringLiteral("styles")));
  const auto style_storage_id =
      styles.add_style(QStringLiteral("Sweep Style"), style_test_shadow_recipe(), std::nullopt, {}, QStringLiteral("Folder"));
  CHECK(styles.find_entry(style_storage_id) != nullptr);
  patchy::ui::CustomShapeLibrary shapes(directory.filePath(QStringLiteral("shapes")));
  CHECK(shapes.restore_default_shapes() > 0);
  patchy::ui::ContourLibrary contours(directory.filePath(QStringLiteral("contours")));
  const auto* sweep_ring = patchy::find_builtin_contour_preset("contour.ring");
  CHECK(sweep_ring != nullptr);
  CHECK(!contours.add_contour(QStringLiteral("Sweep Ring"), sweep_ring->contour).isEmpty());
  SettingsValueRestorer stroke_presets{QString::fromLatin1(patchy::ui::kStrokeStylePresetsKey)};
  patchy::ui::save_stroke_style_presets(
      {patchy::ui::StrokeStylePreset{QStringLiteral("Sweep dots"), patchy::VectorStrokeAlignment::Center,
                                     patchy::VectorStrokeCap::Round, patchy::VectorStrokeJoin::Round,
                                     std::vector<double>{0.0, 2.0}, 0.0}});
  int captured = 0;
  for (const auto preference :
       {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer::apply(preference);
    const std::string scheme = preference == patchy::ui::ColorSchemePreference::Light ? "light" : "dark";
    const auto grab_dialog = [&](const QString& object_name, const std::string& label) {
      QTimer::singleShot(0, guarded([&, object_name, label] {
        auto* dialog = find_top_level_dialog(object_name);
        CHECK(dialog != nullptr);
        if (dialog == nullptr) {
          return;
        }
        (void)capture_widget_artifact("ui_dialog_sweep_" + label + "_" + scheme, *dialog);
        ++captured;
        QTimer::singleShot(0, dialog, [dialog] { dialog->reject(); });
      }));
    };
    patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
    patchy::Layer layer(document.allocate_layer_id(), "Styled",
                        solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
    layer.layer_style() = style_test_shadow_recipe();
    grab_dialog(QStringLiteral("patchyLayerStyleDialog"), "layer_style");
    (void)patchy::ui::request_layer_style_settings(nullptr, layer, {});
    grab_dialog(QStringLiteral("gradientManagerDialog"), "gradient_manager");
    (void)patchy::ui::request_gradient_manager(nullptr, gradients, gradients.entries().front().storage_id,
                                               patchy::builtin_gradient_presets().front().definition);
    grab_dialog(QStringLiteral("patternManagerDialog"), "pattern_manager");
    (void)patchy::ui::request_pattern_manager(nullptr, patterns, {},
                                              [](const QString&, const patchy::PixelBuffer&) {});
    grab_dialog(QStringLiteral("styleManagerDialog"), "style_manager");
    (void)patchy::ui::request_style_manager(nullptr, styles, styles.find_entry(style_storage_id)->id);
    grab_dialog(QStringLiteral("customShapeManagerDialog"), "custom_shape_manager");
    (void)patchy::ui::request_custom_shape_manager(nullptr, shapes, QStringLiteral("shape.builtin.heart"));
    grab_dialog(QStringLiteral("strokePresetManagerDialog"), "stroke_preset_manager");
    (void)patchy::ui::request_stroke_preset_manager(nullptr);
    grab_dialog(QStringLiteral("contourEditorDialog"), "contour_editor");
    (void)patchy::ui::request_contour_edit(nullptr, sweep_ring->contour);
    grab_dialog(QStringLiteral("saveContoursDialog"), "save_contours");
    (void)patchy::ui::request_contours_to_save(nullptr, contours);
    grab_dialog(QStringLiteral("contourManagerDialog"), "contour_manager");
    (void)patchy::ui::request_contour_manager(nullptr, contours, contours.entries().front().storage_id);
    grab_dialog(QStringLiteral("layerStyleContourNameDialog"), "contour_name");
    (void)patchy::ui::request_contour_name(nullptr, QStringLiteral("New Contour"), QStringLiteral("Contour 1"));
    grab_dialog(QStringLiteral("cameraRawFilterDialog"), "camera_raw_filter");
    patchy::ui::CameraRawFilterRequest camera_raw_request;
    camera_raw_request.source = std::make_shared<const patchy::PixelBuffer>(
        solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(150, 110, 70, 255)));
    camera_raw_request.bounds = patchy::Rect::from_size(48, 36);
    (void)patchy::ui::run_camera_raw_filter_dialog(nullptr, camera_raw_request);
    grab_dialog(QStringLiteral("webExportDialog"), "web_export");
    (void)patchy::ui::request_web_export(nullptr, std::make_shared<const patchy::Document>(document));
    grab_dialog(QStringLiteral("viewExtrasOptionsDialog"), "view_extras_options");
    (void)patchy::ui::request_view_extras_options(nullptr, patchy::ui::kAllViewExtras);
    grab_dialog(QStringLiteral("toolPaletteCustomizeDialog"), "tool_palette_customize");
    {
      patchy::ui::ToolPaletteCatalog catalog;
      catalog.clusters.push_back({QStringLiteral("Select"),
                                  {{QStringLiteral("Move Tools"), QStringLiteral("V"), true,
                                    {{QStringLiteral("tools.move"), QStringLiteral("Move"), QIcon(), QStringLiteral("V")},
                                     {QStringLiteral("tools.artboard"), QStringLiteral("Artboard"), QIcon(), QString()}}}}});
      catalog.layer_buttons.push_back({QStringLiteral("new_layer"), QStringLiteral("New Layer"), QIcon()});
      (void)patchy::ui::request_tool_palette_customization(nullptr, catalog, {});
    }
    grab_dialog(QStringLiteral("patchyFillDialog"), "edit_fill");
    (void)patchy::ui::request_fill_settings(nullptr, QColor(20, 140, 230), Qt::white);
    grab_dialog(QStringLiteral("patchySmoothSelectionDialog"), "smooth_selection");
    (void)patchy::ui::request_selection_smooth_radius(nullptr, 1);
    grab_dialog(QStringLiteral("patchyFeatherSelectionDialog"), "feather_selection");
    (void)patchy::ui::request_selection_feather_radius(nullptr, 5.0);
    grab_dialog(QStringLiteral("patchyColorRangeDialog"), "color_range");
    {
      patchy::ui::ColorRangeSettings color_range;
      color_range.range.samples = {patchy::ColorRangeSample{80, 140, 220, false},
                                   patchy::ColorRangeSample{200, 60, 40, true}};
      patchy::ui::ColorRangeDialogHooks color_range_hooks;
      color_range_hooks.source_image = [](bool) {
        QImage picture(96, 72, QImage::Format_RGBA8888);
        picture.fill(QColor(80, 140, 220));
        for (int y = 20; y < 52; ++y) {
          for (int x = 30; x < 66; ++x) {
            picture.setPixelColor(x, y, QColor(200, 60, 40));
          }
        }
        return picture;
      };
      (void)patchy::ui::request_color_range_settings(nullptr, color_range, color_range_hooks);
    }
    grab_dialog(QStringLiteral("layerLocksDialog"), "layer_locks");
    (void)patchy::ui::request_layer_locks(
        nullptr, {patchy::ui::LayerLockChoice::On, patchy::ui::LayerLockChoice::Off, patchy::ui::LayerLockChoice::Mixed,
                  patchy::ui::LayerLockChoice::Off},
        2);
    grab_dialog(QStringLiteral("maskPropertiesDialog"), "mask_properties");
    (void)patchy::ui::request_mask_properties(nullptr, true, QStringLiteral("Shape"), {100, 0}, {});
    grab_dialog(QStringLiteral("perspectivePlanesDialog"), "perspective_planes");
    {
      patchy::ui::PerspectiveWorkspaceInput perspective;
      perspective.layer = solid_pixels(96, 72, patchy::PixelFormat::rgba8(), QColor(120, 90, 60, 255));
      perspective.layer_bounds = patchy::Rect{0, 0, 96, 72};
      perspective.document_width = 96;
      perspective.document_height = 72;
      perspective.planes.push_back({1, {{{10.0, 10.0}, {80.0, 16.0}, {84.0, 60.0}, {8.0, 64.0}}}, 0.0, std::nullopt});
      (void)patchy::ui::run_perspective_planes_workspace(nullptr, std::move(perspective));
    }
    grab_dialog(QStringLiteral("patchySplashScreen"), "about");
    patchy::ui::show_about_splash();
    // Scan a Book to PDF on the test scanner, so the sweep never opens real hardware.
    grab_dialog(QStringLiteral("bookScanDialog"), "book_scan");
    qputenv("PATCHY_FAKE_BOOK_SCANNER", "sweep-no-scan.png");
    patchy::ui::run_book_scan_dialog(nullptr);
    qunsetenv("PATCHY_FAKE_BOOK_SCANNER");
    // Recognize Text with no engine (the suite's default): its "nothing available" note.
    grab_dialog(QStringLiteral("recognizeTextDialog"), "recognize_text");
    (void)patchy::ui::request_recognize_text(nullptr, {});
    // Its Import PDF form: no Read row, the three results.
    grab_dialog(QStringLiteral("pdfImportOcrOptionsDialog"), "pdf_import_ocr_options");
    {
      patchy::ui::RecognizeTextDialogContext import_options;
      import_options.purpose = patchy::ui::RecognizeTextPurpose::PdfImport;
      (void)patchy::ui::request_recognize_text(nullptr, import_options);
    }
    // The PDF viewer's question before it reads scanned pages: engine and language only.
    grab_dialog(QStringLiteral("pdfViewerRecognizeDialog"), "pdf_viewer_recognize");
    {
      patchy::ui::OcrChoiceRequest viewer_ocr;
      viewer_ocr.name = QStringLiteral("pdfViewerRecognize");
      viewer_ocr.title = QStringLiteral("Recognize Text");
      viewer_ocr.note = QStringLiteral("Reads the text on the pages that have none of their own (scanned pages), so "
                                       "Find and text selection work on them. Nothing is saved to the PDF.");
      viewer_ocr.picker_prefix = QStringLiteral("pdfViewerOcr");
      viewer_ocr.accept_text = QStringLiteral("Recognize");
      (void)patchy::ui::request_ocr_choice(nullptr, viewer_ocr);
    }
    // The color picker is a top-level widget, not a dialog.
    patchy::ui::PatchyColorPicker picker(QColor(120, 60, 30));
    picker.show();
    QApplication::processEvents();
    (void)capture_widget_artifact("ui_dialog_sweep_color_picker_" + scheme, picker);
    ++captured;
    picker.close();
    QApplication::processEvents();
    // The 32-bit controls the Foreground and Background Color windows add under the picker.
    patchy::ui::HdrColorControls hdr_controls(patchy::ui::HdrColorControls::Form::Full);
    hdr_controls.set_color(QColor(149, 33, 33), 1.7);
    hdr_controls.show();
    QApplication::processEvents();
    (void)capture_widget_artifact("ui_dialog_sweep_hdr_color_controls_" + scheme, hdr_controls);
    ++captured;
    hdr_controls.close();
    QApplication::processEvents();
    // The animation preview is a non-modal tool window; built here without a document.
    patchy::ui::AnimationPreviewWindow animation_preview([]() -> patchy::Document* { return nullptr; }, {}, {}, {},
                                                         nullptr);
    animation_preview.show();
    QApplication::processEvents();
    (void)capture_widget_artifact("ui_dialog_sweep_animation_preview_" + scheme, animation_preview);
    ++captured;
    animation_preview.close();
    QApplication::processEvents();
  }
  CHECK(captured == 58);
}

void ui_style_manager_lists_renames_and_uses_styles() {
  QTemporaryDir directory;
  CHECK(directory.isValid());
  patchy::ui::StyleLibrary library(directory.filePath(QStringLiteral("library")));
  const auto storage_id =
      library.add_style(QStringLiteral("Manager Style"), style_test_shadow_recipe(), std::nullopt,
                        {}, QStringLiteral("Folder"));
  CHECK(!storage_id.isEmpty());
  const auto style_id = library.find_entry(storage_id)->id;

  QTimer::singleShot(0, guarded([&] {
    auto* manager =
        qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("styleManagerDialog")));
    CHECK(manager != nullptr);
    auto* tree = manager->findChild<patchy::ui::StyleBrowserWidget*>(
        QStringLiteral("styleManagerTree"));
    auto* name_edit = manager->findChild<QLineEdit*>(QStringLiteral("styleManagerNameEdit"));
    auto* effects_label = manager->findChild<QLabel*>(QStringLiteral("styleManagerEffectsLabel"));
    auto* use_button = manager->findChild<QPushButton*>(QStringLiteral("styleManagerUseButton"));
    auto* preview = manager->findChild<QLabel*>(QStringLiteral("styleManagerPreview"));
    CHECK(tree != nullptr && name_edit != nullptr && effects_label != nullptr &&
          use_button != nullptr && preview != nullptr);
    // The initial style id preselects the entry.
    CHECK(tree->current_storage_id() == storage_id);
    CHECK(name_edit->text() == QStringLiteral("Manager Style"));
    CHECK(effects_label->text().contains(QStringLiteral("Drop Shadow")));
    CHECK(preview->pixmap().width() > 0);
    // Rename through the manager.
    name_edit->setText(QStringLiteral("Renamed Style"));
    QMetaObject::invokeMethod(name_edit, "editingFinished");
    QApplication::processEvents();
    save_widget_artifact("ui_style_manager_dialog", *manager);
    CHECK(use_button->isEnabled());
    use_button->click();
  }));

  const auto chosen = patchy::ui::request_style_manager(nullptr, library, style_id);
  CHECK(chosen == storage_id);
  CHECK(library.find_entry(storage_id)->name == QStringLiteral("Renamed Style"));
}

void ui_style_preset_pattern_apply_cancel_restores_document_store() {
  QTemporaryDir directory;
  CHECK(directory.isValid());
  patchy::ui::StyleLibrary library(directory.filePath(QStringLiteral("library")));
  // A preset whose pattern id will collide with different document pixels.
  const auto pattern_id = std::string("99999999-1111-2222-3333-444444444444");
  patchy::PatternResource preset_tile;
  preset_tile.id = pattern_id;
  preset_tile.name = "Preset Tile";
  preset_tile.tile = solid_pixels(4, 4, patchy::PixelFormat::rgba8(), QColor(40, 200, 90, 255));
  preset_tile.provenance = patchy::PatternProvenance::Authored;
  patchy::LayerStyle style;
  patchy::LayerPatternOverlay overlay;
  overlay.enabled = true;
  overlay.pattern_id = pattern_id;
  overlay.pattern_name = "Preset Tile";
  style.pattern_overlays.push_back(overlay);
  const auto storage_id =
      library.add_style(QStringLiteral("Pattern Preset"), style, std::nullopt,
                        std::span<const patchy::PatternResource>(&preset_tile, 1));
  CHECK(!storage_id.isEmpty());

  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::PatternResource embedded;
  embedded.id = pattern_id;
  embedded.name = "Embedded Collision";
  embedded.tile = solid_pixels(3, 3, patchy::PixelFormat::rgba8(), QColor(180, 20, 30, 255));
  embedded.provenance = patchy::PatternProvenance::ImportedRaw;
  document.metadata().patterns.adopt(embedded);
  patchy::Layer layer(document.allocate_layer_id(), "Styled",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));

  std::string applied_alias;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog =
        qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
    auto* browser =
        dialog->findChild<patchy::ui::StyleBrowserWidget*>(QStringLiteral("layerStyleStylesBrowser"));
    auto* list_button = dialog->findChild<QToolButton*>(QStringLiteral("layerStyleStylesListViewButton"));
    CHECK(categories != nullptr && browser != nullptr && list_button != nullptr);
    const auto styles_items = categories->findItems(QStringLiteral("Style Presets"), Qt::MatchExactly);
    CHECK(!styles_items.empty());
    categories->setCurrentItem(styles_items.front());
    QApplication::processEvents();
    // The grid shows by default (GitHub issue 606); this test drives the list view.
    list_button->click();
    QApplication::processEvents();
    CHECK(!browser->visibleRegion().isEmpty());
    click_style_tree_item(*browser, style_tree_item_for_storage_id(*browser, storage_id));
    QApplication::processEvents();
    // The collision produced a document-local alias with the preset pixels.
    CHECK(document.metadata().patterns.patterns.size() == 2U);
    for (const auto& resource : document.metadata().patterns.patterns) {
      if (resource.id != pattern_id) {
        applied_alias = resource.id;
        CHECK(resource.tile.pixel(0, 0)[1] == 200);
      }
    }
    CHECK(!applied_alias.empty());
    QTimer::singleShot(50, dialog, [dialog] { dialog->reject(); });
  }));

  const auto settings = patchy::ui::request_layer_style_settings(
      nullptr, layer, {}, &document.metadata().patterns, nullptr, &library);
  CHECK(!settings.has_value());
  // MainWindow restores the snapshot on cancel; here the dialog must not have
  // touched the original embedded resource and the alias must reference the
  // preset pixels it adopted.
  CHECK(document.metadata().patterns.find(pattern_id) != nullptr);
  CHECK(document.metadata().patterns.find(pattern_id)->tile.pixel(0, 0)[0] == 180);
  CHECK(!applied_alias.empty());
  CHECK(document.metadata().patterns.find(applied_alias) != nullptr);
}

}  // namespace

std::vector<patchy::test::TestCase> layer_style_gradient_tests_part2() {
  return {
      {"ui_pattern_manager_and_layer_style_buttons_use_library_pattern",
       ui_pattern_manager_and_layer_style_buttons_use_library_pattern},
      {"ui_layer_style_saves_document_pattern_as_preset", ui_layer_style_saves_document_pattern_as_preset},
      {"ui_pattern_manager_preview_zooms_and_opens_image",
       ui_pattern_manager_preview_zooms_and_opens_image},
      {"ui_gradient_manager_shows_defaults_and_supports_crud",
       ui_gradient_manager_shows_defaults_and_supports_crud},
      {"ui_layer_style_open_pattern_as_image_defers_until_dialog_closes",
       ui_layer_style_open_pattern_as_image_defers_until_dialog_closes},
      {"ui_layer_style_pattern_picker_groups_and_collapses_folders",
       ui_layer_style_pattern_picker_groups_and_collapses_folders},
      {"ui_pattern_manager_remaps_document_id_collision",
       ui_pattern_manager_remaps_document_id_collision},
      {"ui_layer_style_library_pattern_cancel_and_undo_restore_store",
       ui_layer_style_library_pattern_cancel_and_undo_restore_store},
      {"ui_style_library_defaults_restore_export_import_round_trip",
       ui_style_library_defaults_restore_export_import_round_trip},
      {"ui_style_library_rejects_oversized_asl_before_read", ui_style_library_rejects_oversized_asl_before_read},
      {"ui_style_library_imports_asl_past_the_old_limit", ui_style_library_imports_asl_past_the_old_limit},
      {"ui_style_library_import_stops_from_its_step", ui_style_library_import_stops_from_its_step},
      {"ui_preset_import_progress_dialog_shows_for_large_files_only",
       ui_preset_import_progress_dialog_shows_for_large_files_only},
      {"ui_style_manager_imports_large_asl_with_progress", ui_style_manager_imports_large_asl_with_progress},
      {"ui_photo_pattern_presets_load_stable_tiles", ui_photo_pattern_presets_load_stable_tiles},
      {"ui_pattern_thumbnail_fits_large_images_without_tiling",
       ui_pattern_thumbnail_fits_large_images_without_tiling},
      {"ui_version_two_defaults_seed_only_new_entries",
       ui_version_two_defaults_seed_only_new_entries},
      {"ui_layer_style_styles_page_applies_preset_and_previews",
       ui_layer_style_styles_page_applies_preset_and_previews},
      {"ui_layer_style_no_style_entry_clears_effects",
       ui_layer_style_no_style_entry_clears_effects},
      {"ui_layer_style_new_style_saves_current_settings",
       ui_layer_style_new_style_saves_current_settings},
      {"ui_style_browser_folder_selection_exports_asl",
       ui_style_browser_folder_selection_exports_asl},
      {"ui_offscreen_dialog_sweep_is_themed_in_both_schemes",
       ui_offscreen_dialog_sweep_is_themed_in_both_schemes},
      {"ui_style_manager_lists_renames_and_uses_styles",
       ui_style_manager_lists_renames_and_uses_styles},
      {"ui_style_preset_pattern_apply_cancel_restores_document_store",
       ui_style_preset_pattern_apply_cancel_restores_document_store},
      {"ui_layer_style_bevel_contour_and_texture_rows_round_trip",
       ui_layer_style_bevel_contour_and_texture_rows_round_trip},
      {"ui_layer_style_dialog_has_no_group_effects_warning",
       ui_layer_style_dialog_has_no_group_effects_warning},
      {"ui_gradient_stops_editor_two_track_renders_artifact",
       ui_gradient_stops_editor_two_track_renders_artifact},
      {"ui_gradient_stops_editor_drags_destination_midpoints",
       ui_gradient_stops_editor_drags_destination_midpoints},
  };
}
