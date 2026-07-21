// MainWindow adjustment-layer implementation, split out of main_window.cpp
// (and later re-split; see main_window_filters.cpp and
// main_window_destructive_adjustments.cpp): the New Adjustment Layer menu and
// flows, adjustment-layer build/preview/create/edit plumbing, and the
// adjustment-layer appliers the destructive dialogs also call. Pure function
// moves; behavior must stay identical to the pre-split code.

#include "ui/main_window.hpp"
#include "ui/main_window_shared.hpp"

#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "core/layer_tree.hpp"
#include "core/palette.hpp"
#include "core/palette_presets.hpp"
#include "core/pixel_tools.hpp"
#include "formats/palette_io.hpp"
#include "filters/builtin_filters.hpp"
#include "filters/smart_filter_recipe_mapping.hpp"
#include "filters/smart_filter_renderer.hpp"
#include "formats/bmp_document_io.hpp"
#include "plugins/legacy_photoshop_adapter.hpp"
#include "psd/psd_document_io.hpp"
#include "psd/psd_filter_effects.hpp"
#include "psd/psd_smart_objects.hpp"
#include "ui/action_icons.hpp"
#include "ui/background_workers.hpp"
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
#include "ui/filter_workflows.hpp"
#include "ui/gradient_stops_editor.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/hotkey_editor.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/color_panel.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/liquify_dialog.hpp"
#include "ui/localization.hpp"
#include "ui/palette_convert_dialog.hpp"
#include "ui/palette_panel.hpp"
#include "ui/print_dialog.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/splash_dialog.hpp"
#include "ui/smart_object_render.hpp"
#include "ui/update_checker.hpp"
#include "ui/visual_filter_gallery_dialog.hpp"
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
#include <QButtonGroup>
#include <QByteArray>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QCursor>
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

CurvesHistograms curves_histograms_from_composite(const Document& document) {
  std::vector<std::uint8_t> merged_alpha;
  const auto rgb = Compositor{}.flatten_rgb8(document, &merged_alpha);
  return curves_histograms_from_pixels(&rgb, merged_alpha);
}

bool truncate_layer_tree_before(std::vector<Layer>& siblings, LayerId target_id) {
  for (std::size_t index = 0; index < siblings.size(); ++index) {
    if (siblings[index].id() == target_id) {
      siblings.erase(siblings.begin() + static_cast<std::ptrdiff_t>(index), siblings.end());
      return true;
    }
    if (!std::as_const(siblings[index]).children().empty()) {
      auto& children = siblings[index].children();
      if (truncate_layer_tree_before(children, target_id)) {
        siblings.erase(siblings.begin() + static_cast<std::ptrdiff_t>(index + 1U), siblings.end());
        return true;
      }
    }
  }
  return false;
}

CurvesHistograms curves_histograms_before_adjustment(const Document& document, LayerId adjustment_id) {
  auto input_document = document;
  if (!truncate_layer_tree_before(input_document.layers(), adjustment_id)) {
    return curves_histograms_from_composite(document);
  }
  return curves_histograms_from_composite(input_document);
}

bool collect_layers_at_or_above(const std::vector<Layer>& siblings, LayerId target_id,
                                std::vector<LayerId>& hidden) {
  for (std::size_t index = 0; index < siblings.size(); ++index) {
    if (siblings[index].id() == target_id) {
      for (std::size_t hidden_index = index; hidden_index < siblings.size(); ++hidden_index) {
        hidden.push_back(siblings[hidden_index].id());
      }
      return true;
    }
    if (!siblings[index].children().empty() &&
        collect_layers_at_or_above(siblings[index].children(), target_id, hidden)) {
      for (std::size_t hidden_index = index + 1U; hidden_index < siblings.size(); ++hidden_index) {
        hidden.push_back(siblings[hidden_index].id());
      }
      return true;
    }
  }
  return false;
}

QColor curves_sample_before_layer(const Document& document, LayerId layer_id, QPoint point) {
  if (point.x() < 0 || point.y() < 0 || point.x() >= document.width() || point.y() >= document.height()) {
    return {};
  }
  std::vector<LayerId> hidden;
  collect_layers_at_or_above(document.layers(), layer_id, hidden);
  const auto image = qimage_from_document_rect_with_hidden_layers(
      document, QRect(point, QSize(1, 1)), true, hidden);
  return image.isNull() ? QColor{} : image.pixelColor(0, 0);
}

}  // namespace

void MainWindow::populate_new_adjustment_layer_menu(QMenu* menu, const QString& object_name_prefix) {
  if (menu == nullptr) {
    return;
  }

  const auto add_adjustment = [this, menu, &object_name_prefix](const QString& label, const QString& object_key,
                                                               const QString& icon_label, auto callback) {
    auto* action = menu->addAction(simple_icon(icon_label), label);
    if (!object_name_prefix.isEmpty()) {
      action->setObjectName(object_name_prefix + object_key + QStringLiteral("Action"));
      register_document_action(action);
    }
    connect(action, &QAction::triggered, this, callback);
    return action;
  };
  // Photoshop's New Adjustment Layer ordering puts Brightness/Contrast first.
  // No mnemonic: B and C are taken by Color Balance and Curves.
  add_adjustment(tr("Brightness/Contrast..."), QStringLiteral("BrightnessContrastAdjustment"),
                 QStringLiteral("BC"), [this] { new_brightness_contrast_adjustment_layer(); });
  add_adjustment(tr("&Levels..."), QStringLiteral("LevelsAdjustment"), QStringLiteral("LVL"),
                 [this] { new_levels_adjustment_layer(); });
  add_adjustment(tr("&Curves..."), QStringLiteral("CurvesAdjustment"), QStringLiteral("CRV"),
                 [this] { new_curves_adjustment_layer(); });
  add_adjustment(tr("&Hue/Saturation..."), QStringLiteral("HueSaturationAdjustment"), QStringLiteral("HSL"),
                 [this] { new_hue_saturation_adjustment_layer(); });
  add_adjustment(tr("Color &Balance..."), QStringLiteral("ColorBalanceAdjustment"), QStringLiteral("CB"),
                 [this] { new_color_balance_adjustment_layer(); });
  // No ellipsis: Invert has no settings, so no dialog opens.
  add_adjustment(tr("&Invert"), QStringLiteral("InvertAdjustment"), QStringLiteral("INV"),
                 [this] { new_invert_adjustment_layer(); });
  add_adjustment(tr("&Posterize..."), QStringLiteral("PosterizeAdjustment"), QStringLiteral("PST"),
                 [this] { new_posterize_adjustment_layer(); });
  add_adjustment(tr("&Threshold..."), QStringLiteral("ThresholdAdjustment"), QStringLiteral("THR"),
                 [this] { new_threshold_adjustment_layer(); });
}

void MainWindow::new_levels_adjustment_layer() {
  std::optional<LayerId> preview_id;
  const auto restore_active_layer = document().active_layer_id();
  const auto preview_changed = [this, &preview_id, restore_active_layer](bool enabled,
                                                                         const LevelsSettings& levels) {
    AdjustmentSettings settings;
    settings.kind = AdjustmentKind::Levels;
    settings.levels = sanitized_levels_adjustment(levels);
    update_adjustment_layer_preview(tr("Levels"), settings, enabled, preview_id, restore_active_layer);
  };

  const PixelBuffer* histogram_source = nullptr;
  if (restore_active_layer.has_value()) {
    const auto& read_only_document = std::as_const(document());
    if (const auto* layer = read_only_document.find_layer(*restore_active_layer); editable_rgb8_layer(layer)) {
      histogram_source = &layer->pixels();
    }
  }
  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto settings = request_levels_settings(this, preview_changed, {}, histogram_source);
  remove_adjustment_layer_preview(preview_id, restore_active_layer);
  preview_edit_lock.release();
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled Levels"));
    return;
  }
  apply_levels_adjustment(*settings, true);
}

void MainWindow::apply_levels_adjustment(const LevelsSettings& levels, bool allow_identity) {
  AdjustmentSettings settings;
  settings.kind = AdjustmentKind::Levels;
  settings.levels = sanitized_levels_adjustment(levels);
  if (!allow_identity && !adjustment_has_effect(settings)) {
    return;
  }
  create_adjustment_layer(tr("Levels"), settings);
}

void MainWindow::new_curves_adjustment_layer() {
  std::optional<LayerId> preview_id;
  const auto restore_active_layer = document().active_layer_id();
  const auto preview_changed = [this, &preview_id, restore_active_layer](bool enabled,
                                                                         const CurvesSettings& curves) {
    AdjustmentSettings settings;
    settings.kind = AdjustmentKind::Curves;
    settings.curves = curves;
    update_adjustment_layer_preview(tr("Curves"), settings, enabled, preview_id, restore_active_layer);
  };

  const auto histograms = curves_histograms_from_composite(std::as_const(document()));
  const auto hooks = curves_canvas_hooks(canvas_, [this, &preview_id](QPoint point) {
    if (preview_id.has_value() && document().find_layer(*preview_id) != nullptr) {
      return curves_sample_before_layer(std::as_const(document()), *preview_id, point);
    }
    const auto image = qimage_from_document_rect(
        std::as_const(document()), QRect(point, QSize(1, 1)), true);
    return image.isNull() ? QColor{} : image.pixelColor(0, 0);
  });
  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto settings = request_curves_settings(this, preview_changed, {}, histograms, hooks);
  remove_adjustment_layer_preview(preview_id, restore_active_layer);
  preview_edit_lock.release();
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled Curves"));
    return;
  }
  apply_curves_adjustment(*settings, true);
}

void MainWindow::apply_curves_adjustment(const CurvesAdjustment& curves, bool allow_identity) {
  AdjustmentSettings settings;
  settings.kind = AdjustmentKind::Curves;
  settings.curves = curves;
  if (!allow_identity && !adjustment_has_effect(settings)) {
    return;
  }
  create_adjustment_layer(tr("Curves"), settings);
}

void MainWindow::new_hue_saturation_adjustment_layer() {
  std::optional<LayerId> preview_id;
  const auto restore_active_layer = document().active_layer_id();
  const auto preview_changed = [this, &preview_id, restore_active_layer](
                                   bool enabled, const HueSaturationSettings& hue_saturation) {
    AdjustmentSettings settings;
    settings.kind = AdjustmentKind::HueSaturation;
    settings.hue_saturation = to_hue_saturation_adjustment(hue_saturation);
    update_adjustment_layer_preview(tr("Hue/Saturation"), settings, enabled, preview_id, restore_active_layer);
  };

  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto settings = request_hue_saturation_settings(this, preview_changed);
  remove_adjustment_layer_preview(preview_id, restore_active_layer);
  preview_edit_lock.release();
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled Hue/Saturation"));
    return;
  }
  apply_hue_saturation_adjustment(*settings, true);
}

void MainWindow::apply_hue_saturation_adjustment(const HueSaturationSettings& hue_saturation, bool allow_identity) {
  AdjustmentSettings settings;
  settings.kind = AdjustmentKind::HueSaturation;
  settings.hue_saturation = to_hue_saturation_adjustment(hue_saturation);
  if (!allow_identity && !adjustment_has_effect(settings)) {
    return;
  }
  create_adjustment_layer(tr("Hue/Saturation"), settings);
}

void MainWindow::new_color_balance_adjustment_layer() {
  std::optional<LayerId> preview_id;
  const auto restore_active_layer = document().active_layer_id();
  const auto preview_changed = [this, &preview_id, restore_active_layer](bool enabled,
                                                                         const ColorBalanceSettings& color_balance) {
    AdjustmentSettings settings;
    settings.kind = AdjustmentKind::ColorBalance;
    settings.color_balance = ColorBalanceAdjustment{std::clamp(color_balance.cyan_red, -100, 100),
                                                    std::clamp(color_balance.magenta_green, -100, 100),
                                                    std::clamp(color_balance.yellow_blue, -100, 100)};
    update_adjustment_layer_preview(tr("Color Balance"), settings, enabled, preview_id, restore_active_layer);
  };

  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto settings = request_color_balance_settings(this, preview_changed);
  remove_adjustment_layer_preview(preview_id, restore_active_layer);
  preview_edit_lock.release();
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled Color Balance"));
    return;
  }
  apply_color_balance_adjustment(settings->cyan_red, settings->magenta_green, settings->yellow_blue, true);
}

void MainWindow::apply_color_balance_adjustment(int cyan_red, int magenta_green, int yellow_blue, bool allow_identity) {
  AdjustmentSettings settings;
  settings.kind = AdjustmentKind::ColorBalance;
  settings.color_balance = ColorBalanceAdjustment{std::clamp(cyan_red, -100, 100),
                                                  std::clamp(magenta_green, -100, 100),
                                                  std::clamp(yellow_blue, -100, 100)};
  if (!allow_identity && !adjustment_has_effect(settings)) {
    return;
  }
  create_adjustment_layer(tr("Color Balance"), settings);
}

void MainWindow::new_invert_adjustment_layer() {
  AdjustmentSettings settings;
  settings.kind = AdjustmentKind::Invert;
  create_adjustment_layer(tr("Invert"), settings);
}

void MainWindow::new_posterize_adjustment_layer() {
  std::optional<LayerId> preview_id;
  const auto restore_active_layer = document().active_layer_id();
  const auto preview_changed = [this, &preview_id, restore_active_layer](bool enabled,
                                                                         const PosterizeSettings& posterize) {
    AdjustmentSettings settings;
    settings.kind = AdjustmentKind::Posterize;
    settings.posterize.levels = std::clamp(posterize.levels, 2, 255);
    update_adjustment_layer_preview(tr("Posterize"), settings, enabled, preview_id, restore_active_layer);
  };

  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto settings = request_posterize_settings(this, preview_changed);
  remove_adjustment_layer_preview(preview_id, restore_active_layer);
  preview_edit_lock.release();
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled Posterize"));
    return;
  }
  apply_posterize_adjustment(*settings, true);
}

void MainWindow::apply_posterize_adjustment(const PosterizeSettings& posterize, bool allow_identity) {
  AdjustmentSettings settings;
  settings.kind = AdjustmentKind::Posterize;
  settings.posterize.levels = std::clamp(posterize.levels, 2, 255);
  if (!allow_identity && !adjustment_has_effect(settings)) {
    return;
  }
  create_adjustment_layer(tr("Posterize"), settings);
}

void MainWindow::new_threshold_adjustment_layer() {
  std::optional<LayerId> preview_id;
  const auto restore_active_layer = document().active_layer_id();
  const auto preview_changed = [this, &preview_id, restore_active_layer](bool enabled,
                                                                         const ThresholdSettings& threshold) {
    AdjustmentSettings settings;
    settings.kind = AdjustmentKind::Threshold;
    settings.threshold.level = std::clamp(threshold.level, 1, 255);
    update_adjustment_layer_preview(tr("Threshold"), settings, enabled, preview_id, restore_active_layer);
  };

  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto settings = request_threshold_settings(this, preview_changed);
  remove_adjustment_layer_preview(preview_id, restore_active_layer);
  preview_edit_lock.release();
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled Threshold"));
    return;
  }
  apply_threshold_adjustment(*settings, true);
}

void MainWindow::apply_threshold_adjustment(const ThresholdSettings& threshold, bool allow_identity) {
  AdjustmentSettings settings;
  settings.kind = AdjustmentKind::Threshold;
  settings.threshold.level = std::clamp(threshold.level, 1, 255);
  if (!allow_identity && !adjustment_has_effect(settings)) {
    return;
  }
  create_adjustment_layer(tr("Threshold"), settings);
}

void MainWindow::new_brightness_contrast_adjustment_layer() {
  std::optional<LayerId> preview_id;
  const auto restore_active_layer = document().active_layer_id();
  const auto preview_changed = [this, &preview_id, restore_active_layer](
                                   bool enabled, const BrightnessContrastSettings& brightness_contrast) {
    AdjustmentSettings settings;
    settings.kind = AdjustmentKind::BrightnessContrast;
    settings.brightness_contrast.brightness = std::clamp(brightness_contrast.brightness, -100, 100);
    settings.brightness_contrast.contrast = std::clamp(brightness_contrast.contrast, -100, 100);
    update_adjustment_layer_preview(tr("Brightness/Contrast"), settings, enabled, preview_id,
                                    restore_active_layer);
  };

  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto settings = request_brightness_contrast_settings(this, preview_changed);
  remove_adjustment_layer_preview(preview_id, restore_active_layer);
  preview_edit_lock.release();
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled Brightness/Contrast"));
    return;
  }
  apply_brightness_contrast_adjustment(*settings, true);
}

void MainWindow::apply_brightness_contrast_adjustment(const BrightnessContrastSettings& brightness_contrast,
                                                      bool allow_identity) {
  AdjustmentSettings settings;
  settings.kind = AdjustmentKind::BrightnessContrast;
  settings.brightness_contrast.brightness = std::clamp(brightness_contrast.brightness, -100, 100);
  settings.brightness_contrast.contrast = std::clamp(brightness_contrast.contrast, -100, 100);
  if (!allow_identity && !adjustment_has_effect(settings)) {
    return;
  }
  create_adjustment_layer(tr("Brightness/Contrast"), settings);
}

Layer MainWindow::build_adjustment_layer(QString label, const AdjustmentSettings& settings) {
  auto& doc = document();
  Layer layer(doc.allocate_layer_id(), label.toStdString(), LayerKind::Adjustment);
  layer.set_bounds(Rect::from_size(doc.width(), doc.height()));
  configure_adjustment_layer(layer, settings);

  const auto selection = canvas_->selected_document_region();
  const auto selection_rect = selection.boundingRect().intersected(QRect(0, 0, doc.width(), doc.height()));
  if (!selection.isEmpty() && !selection_rect.isEmpty()) {
    layer.set_mask(LayerMask{to_core_rect(selection_rect), selection_mask_pixels(*canvas_, selection_rect), 0, false});
  }
  return layer;
}

void MainWindow::update_adjustment_layer_preview(QString label, const AdjustmentSettings& settings, bool enabled,
                                                 std::optional<LayerId>& preview_id,
                                                 std::optional<LayerId> restore_active_layer) {
  if (canvas_ == nullptr || !enabled || !adjustment_has_effect(settings)) {
    remove_adjustment_layer_preview(preview_id, restore_active_layer);
    return;
  }

  auto& doc = document();
  if (preview_id.has_value()) {
    if (auto* layer = doc.find_layer(*preview_id); layer != nullptr) {
      layer->set_name(label.toStdString());
      layer->set_bounds(Rect::from_size(doc.width(), doc.height()));
      configure_adjustment_layer(*layer, settings);
      canvas_->document_changed();
      return;
    }
    preview_id.reset();
  }

  auto preview = build_adjustment_layer(label, settings);
  preview_id = preview.id();
  doc.add_layer(std::move(preview));
  if (restore_active_layer.has_value() && doc.find_layer(*restore_active_layer) != nullptr) {
    doc.set_active_layer(*restore_active_layer);
  }
  canvas_->document_changed();
}

void MainWindow::remove_adjustment_layer_preview(std::optional<LayerId>& preview_id,
                                                 std::optional<LayerId> restore_active_layer) {
  if (!preview_id.has_value()) {
    return;
  }

  auto& doc = document();
  const auto removed = doc.remove_layer(*preview_id);
  preview_id.reset();
  if (restore_active_layer.has_value() && doc.find_layer(*restore_active_layer) != nullptr) {
    doc.set_active_layer(*restore_active_layer);
  }
  if (removed && canvas_ != nullptr) {
    canvas_->document_changed();
  }
}

void MainWindow::create_adjustment_layer(QString label, const AdjustmentSettings& settings) {
  if (canvas_ == nullptr) {
    return;
  }

  auto& doc = document();
  push_undo_snapshot(tr("%1 adjustment layer").arg(label));
  auto layer = build_adjustment_layer(label, settings);

  doc.add_layer(std::move(layer));
  refresh_layer_list();
  refresh_layer_controls();
  refresh_document_info();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Added %1 adjustment layer").arg(label));
}

void MainWindow::edit_active_adjustment_layer() {
  // Adjustment dialogs are preview dialogs; opening one on top of another
  // preview dialog (e.g. by double-clicking a layer row while one is open)
  // stacks nested event loops and crashes.
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  auto& doc = document();
  const auto active = doc.active_layer_id();
  auto* layer = active.has_value() ? doc.find_layer(*active) : nullptr;
  if (layer == nullptr || layer->kind() != LayerKind::Adjustment) {
    show_status_error(tr("Select an adjustment layer to edit its settings"));
    return;
  }
  if (layer_id_locks_image_pixels(*active)) {
    show_status_error(tr("Layer pixels are locked."));
    return;
  }

  const auto original_settings = adjustment_settings_from_layer(*layer);
  if (!original_settings.has_value()) {
    show_status_error(tr("This adjustment layer has no editable settings"));
    return;
  }
  if (original_settings->kind == AdjustmentKind::Invert) {
    statusBar()->showMessage(tr("Invert has no settings to edit"));
    return;
  }

  const auto layer_id = layer->id();
  const auto original_layer = *layer;
  auto apply_settings = [this, &doc, layer_id](const AdjustmentSettings& settings) {
    auto* target = doc.find_layer(layer_id);
    if (target == nullptr) {
      return;
    }
    configure_adjustment_layer(*target, settings);
    if (canvas_ != nullptr) {
      canvas_->document_changed();
      refresh_layer_thumbnails();
    }
  };
  auto restore_original_layer = [this, &doc, layer_id, &original_layer] {
    auto* target = doc.find_layer(layer_id);
    if (target == nullptr) {
      return;
    }
    *target = original_layer;
    if (canvas_ != nullptr) {
      canvas_->document_changed();
      refresh_layer_thumbnails();
    }
  };

  std::optional<AdjustmentSettings> accepted_settings;
  auto preview_edit_lock = lock_preview_dialog_edits();
  switch (original_settings->kind) {
    case AdjustmentKind::Levels: {
      const auto preview_changed = [apply_settings, restore_original_layer,
                                    original_settings](bool enabled, const LevelsSettings& levels) {
        if (!enabled) {
          restore_original_layer();
          return;
        }
        auto settings = *original_settings;
        settings.levels = sanitized_levels_adjustment(levels);
        apply_settings(settings);
      };
      const auto result = request_levels_settings(this, preview_changed,
                                                  LevelsSettings{original_settings->levels.black_input,
                                                                 original_settings->levels.white_input,
                                                                 original_settings->levels.gamma_percent,
                                                                 original_settings->levels.black_output,
                                                                 original_settings->levels.white_output,
                                                                 original_settings->levels.channel,
                                                                 original_settings->levels.red,
                                                                 original_settings->levels.green,
                                                                 original_settings->levels.blue});
      if (result.has_value()) {
        accepted_settings = *original_settings;
        accepted_settings->levels = sanitized_levels_adjustment(*result);
      }
      break;
    }
    case AdjustmentKind::Curves: {
      const auto preview_changed = [apply_settings, restore_original_layer,
                                    original_settings](bool enabled, const CurvesSettings& curves) {
        if (!enabled) {
          restore_original_layer();
          return;
        }
        auto settings = *original_settings;
        settings.curves = curves;
        apply_settings(settings);
      };
      // A clipped adjustment runs inside an isolated clipping buffer. Until the
      // histogram renderer can expose that buffer directly, leave Auto disabled
      // instead of presenting a backdrop-mixed histogram as the adjustment input.
      const auto histograms = original_layer.clipped()
                                  ? CurvesHistograms{}
                                  : curves_histograms_before_adjustment(std::as_const(doc), layer_id);
      auto hooks = curves_canvas_hooks(canvas_, [this, layer_id](QPoint point) {
        return curves_sample_before_layer(std::as_const(document()), layer_id, point);
      });
      if (original_layer.clipped()) {
        hooks.set_canvas_mode = {};
      }
      const auto result =
          request_curves_settings(this, preview_changed, original_settings->curves, histograms, hooks);
      if (result.has_value()) {
        accepted_settings = *original_settings;
        accepted_settings->curves = *result;
      }
      break;
    }
    case AdjustmentKind::HueSaturation: {
      const auto preview_changed = [apply_settings, restore_original_layer, original_settings](
                                       bool enabled, const HueSaturationSettings& hue_saturation) {
        if (!enabled) {
          restore_original_layer();
          return;
        }
        auto settings = *original_settings;
        settings.hue_saturation = to_hue_saturation_adjustment(hue_saturation);
        apply_settings(settings);
      };
      const auto result = request_hue_saturation_settings(
          this, preview_changed, to_hue_saturation_settings(original_settings->hue_saturation));
      if (result.has_value()) {
        accepted_settings = *original_settings;
        accepted_settings->hue_saturation = to_hue_saturation_adjustment(*result);
      }
      break;
    }
    case AdjustmentKind::ColorBalance: {
      const auto preview_changed = [apply_settings, restore_original_layer, original_settings](
                                       bool enabled, const ColorBalanceSettings& color_balance) {
        if (!enabled) {
          restore_original_layer();
          return;
        }
        auto settings = *original_settings;
        settings.color_balance =
            ColorBalanceAdjustment{std::clamp(color_balance.cyan_red, -100, 100),
                                   std::clamp(color_balance.magenta_green, -100, 100),
                                   std::clamp(color_balance.yellow_blue, -100, 100)};
        apply_settings(settings);
      };
      const auto result = request_color_balance_settings(
          this, preview_changed,
          ColorBalanceSettings{original_settings->color_balance.cyan_red,
                               original_settings->color_balance.magenta_green,
                               original_settings->color_balance.yellow_blue});
      if (result.has_value()) {
        accepted_settings = *original_settings;
        accepted_settings->color_balance =
            ColorBalanceAdjustment{std::clamp(result->cyan_red, -100, 100),
                                   std::clamp(result->magenta_green, -100, 100),
                                   std::clamp(result->yellow_blue, -100, 100)};
      }
      break;
    }
    case AdjustmentKind::Invert:
      break;  // handled by the early return above; nothing to edit
    case AdjustmentKind::Posterize: {
      const auto preview_changed = [apply_settings, restore_original_layer, original_settings](
                                       bool enabled, const PosterizeSettings& posterize) {
        if (!enabled) {
          restore_original_layer();
          return;
        }
        auto settings = *original_settings;
        settings.posterize.levels = std::clamp(posterize.levels, 2, 255);
        apply_settings(settings);
      };
      const auto result = request_posterize_settings(
          this, preview_changed, PosterizeSettings{original_settings->posterize.levels});
      if (result.has_value()) {
        accepted_settings = *original_settings;
        accepted_settings->posterize.levels = std::clamp(result->levels, 2, 255);
      }
      break;
    }
    case AdjustmentKind::Threshold: {
      const auto preview_changed = [apply_settings, restore_original_layer, original_settings](
                                       bool enabled, const ThresholdSettings& threshold) {
        if (!enabled) {
          restore_original_layer();
          return;
        }
        auto settings = *original_settings;
        settings.threshold.level = std::clamp(threshold.level, 1, 255);
        apply_settings(settings);
      };
      const auto result = request_threshold_settings(
          this, preview_changed, ThresholdSettings{original_settings->threshold.level});
      if (result.has_value()) {
        accepted_settings = *original_settings;
        accepted_settings->threshold.level = std::clamp(result->level, 1, 255);
      }
      break;
    }
    case AdjustmentKind::BrightnessContrast: {
      const auto preview_changed = [apply_settings, restore_original_layer, original_settings](
                                       bool enabled, const BrightnessContrastSettings& brightness_contrast) {
        if (!enabled) {
          restore_original_layer();
          return;
        }
        auto settings = *original_settings;
        settings.brightness_contrast.brightness = std::clamp(brightness_contrast.brightness, -100, 100);
        settings.brightness_contrast.contrast = std::clamp(brightness_contrast.contrast, -100, 100);
        apply_settings(settings);
      };
      const auto result = request_brightness_contrast_settings(
          this, preview_changed,
          BrightnessContrastSettings{original_settings->brightness_contrast.brightness,
                                     original_settings->brightness_contrast.contrast});
      if (result.has_value()) {
        accepted_settings = *original_settings;
        accepted_settings->brightness_contrast.brightness = std::clamp(result->brightness, -100, 100);
        accepted_settings->brightness_contrast.contrast = std::clamp(result->contrast, -100, 100);
      }
      break;
    }
  }

  restore_original_layer();
  preview_edit_lock.release();
  if (!accepted_settings.has_value()) {
    refresh_layer_list();
    refresh_layer_controls();
    statusBar()->showMessage(tr("Cancelled adjustment edit"));
    return;
  }

  if (original_settings->kind == AdjustmentKind::Curves &&
      accepted_settings->curves == original_settings->curves) {
    refresh_layer_list();
    refresh_layer_controls();
    statusBar()->showMessage(tr("Updated adjustment layer"));
    return;
  }

  push_undo_snapshot(tr("Edit %1 adjustment").arg(localized_adjustment_display_name(original_settings->kind)));
  apply_settings(*accepted_settings);
  refresh_layer_list();
  refresh_layer_controls();
  refresh_document_info();
  statusBar()->showMessage(tr("Updated adjustment layer"));
}

}  // namespace patchy::ui
