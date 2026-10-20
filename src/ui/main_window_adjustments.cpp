// MainWindow adjustment-layer implementation, split out of main_window.cpp
// (and later re-split; see main_window_filters.cpp and
// main_window_destructive_adjustments.cpp): the New Adjustment Layer menu and
// flows, adjustment-layer build/preview/create/edit plumbing, and the
// adjustment-layer appliers the destructive dialogs also call. Pure function
// moves; behavior must stay identical to the pre-split code.

#include "ui/curves_editor.hpp"
#include "core/layer.hpp"
#include "core/adjustment_layer.hpp"
#include "core/pixel_buffer.hpp"
#include "ui/main_window.hpp"
#include <QStatusBar>
#include "ui/canvas_widget.hpp"
#include "ui/main_window_shared.hpp"

#include "core/palette.hpp"
#include "ui/action_icons.hpp"
#include "render/compositor.hpp"
#include "ui/image_document_io.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/qt_geometry.hpp"

#include <QAction>
#include <QColor>
#include <QMenu>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringLiteral>
#include <QtGlobal>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <optional>
#include <utility>
#include <vector>


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

  const auto add_adjustment = [this, menu, &object_name_prefix](const char* source, const QString& object_key,
                                                               const QString& icon_label, auto callback) {
    auto* action = menu->addAction(simple_icon(icon_label), tr(source));
    bind_action_text(action, source);
    if (!object_name_prefix.isEmpty()) {
      action->setObjectName(object_name_prefix + object_key + QStringLiteral("Action"));
      register_document_action(action);
    }
    connect(action, &QAction::triggered, this, callback);
    return action;
  };
  // Photoshop's New Adjustment Layer ordering puts Brightness/Contrast first.
  // No mnemonic: B and C are taken by Color Balance and Curves.
  add_adjustment(QT_TR_NOOP("Brightness/Contrast..."), QStringLiteral("BrightnessContrastAdjustment"),
                 QStringLiteral("BC"), [this] { new_brightness_contrast_adjustment_layer(); });
  add_adjustment(QT_TR_NOOP("&Levels..."), QStringLiteral("LevelsAdjustment"), QStringLiteral("LVL"),
                 [this] { new_levels_adjustment_layer(); });
  add_adjustment(QT_TR_NOOP("&Curves..."), QStringLiteral("CurvesAdjustment"), QStringLiteral("CRV"),
                 [this] { new_curves_adjustment_layer(); });
  add_adjustment(QT_TR_NOOP("&Hue/Saturation..."), QStringLiteral("HueSaturationAdjustment"), QStringLiteral("HSL"),
                 [this] { new_hue_saturation_adjustment_layer(); });
  add_adjustment(QT_TR_NOOP("Color &Balance..."), QStringLiteral("ColorBalanceAdjustment"), QStringLiteral("CB"),
                 [this] { new_color_balance_adjustment_layer(); });
  // No ellipsis: Invert has no settings, so no dialog opens.
  add_adjustment(QT_TR_NOOP("&Invert"), QStringLiteral("InvertAdjustment"), QStringLiteral("INV"),
                 [this] { new_invert_adjustment_layer(); });
  add_adjustment(QT_TR_NOOP("&Posterize..."), QStringLiteral("PosterizeAdjustment"), QStringLiteral("PST"),
                 [this] { new_posterize_adjustment_layer(); });
  add_adjustment(QT_TR_NOOP("&Threshold..."), QStringLiteral("ThresholdAdjustment"), QStringLiteral("THR"),
                 [this] { new_threshold_adjustment_layer(); });
  add_adjustment(QT_TR_NOOP("&Exposure..."), QStringLiteral("ExposureAdjustment"), QStringLiteral("EXP"),
                 [this] { new_exposure_adjustment_layer(); });
  add_adjustment(QT_TR_NOOP("Channel &Mixer..."), QStringLiteral("ChannelMixerAdjustment"), QStringLiteral("MIX"),
                 [this] { new_channel_mixer_adjustment_layer(); });
  add_adjustment(QT_TR_NOOP("Photo &Filter..."), QStringLiteral("PhotoFilterAdjustment"), QStringLiteral("PHF"),
                 [this] { new_photo_filter_adjustment_layer(); });
  add_adjustment(QT_TR_NOOP("&Gradient Map..."), QStringLiteral("GradientMapAdjustment"), QStringLiteral("GRM"),
                 [this] { new_gradient_map_adjustment_layer(); });
  add_adjustment(QT_TR_NOOP("Blac&k && White..."), QStringLiteral("BlackWhiteAdjustment"), QStringLiteral("BW"),
                 [this] { new_black_white_adjustment_layer(); });
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
    if (const auto* layer = read_only_document.find_layer(*restore_active_layer);
        editable_rgb_layer_any_depth(layer)) {
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
    settings.color_balance = color_balance;
    update_adjustment_layer_preview(tr("Color Balance"), settings, enabled, preview_id, restore_active_layer);
  };

  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto settings = request_color_balance_settings(this, preview_changed, new_color_balance_settings());
  remove_adjustment_layer_preview(preview_id, restore_active_layer);
  preview_edit_lock.release();
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled Color Balance"));
    return;
  }
  apply_color_balance_adjustment(*settings, true);
}

void MainWindow::apply_color_balance_adjustment(const ColorBalanceSettings& color_balance, bool allow_identity) {
  AdjustmentSettings settings;
  settings.kind = AdjustmentKind::ColorBalance;
  settings.color_balance = color_balance;
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

void MainWindow::new_exposure_adjustment_layer() {
  std::optional<LayerId> preview_id;
  const auto restore_active_layer = document().active_layer_id();
  const auto preview_changed = [this, &preview_id, restore_active_layer](bool enabled,
                                                                         const ExposureSettings& exposure) {
    AdjustmentSettings settings;
    settings.kind = AdjustmentKind::Exposure;
    settings.exposure = clamp_exposure(exposure);
    update_adjustment_layer_preview(tr("Exposure"), settings, enabled, preview_id, restore_active_layer);
  };

  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto settings = request_exposure_settings(this, preview_changed);
  remove_adjustment_layer_preview(preview_id, restore_active_layer);
  preview_edit_lock.release();
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled Exposure"));
    return;
  }
  apply_exposure_adjustment(*settings, true);
}

void MainWindow::apply_exposure_adjustment(const ExposureSettings& exposure, bool allow_identity) {
  AdjustmentSettings settings;
  settings.kind = AdjustmentKind::Exposure;
  settings.exposure = clamp_exposure(exposure);
  if (!allow_identity && !adjustment_has_effect(settings)) {
    return;
  }
  create_adjustment_layer(tr("Exposure"), settings);
}

void MainWindow::new_channel_mixer_adjustment_layer() {
  std::optional<LayerId> preview_id;
  const auto restore_active_layer = document().active_layer_id();
  const auto preview_changed = [this, &preview_id, restore_active_layer](bool enabled,
                                                                         const ChannelMixerSettings& mixer) {
    AdjustmentSettings settings;
    settings.kind = AdjustmentKind::ChannelMixer;
    settings.channel_mixer = mixer;
    update_adjustment_layer_preview(tr("Channel Mixer"), settings, enabled, preview_id, restore_active_layer);
  };

  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto settings = request_channel_mixer_settings(this, preview_changed);
  remove_adjustment_layer_preview(preview_id, restore_active_layer);
  preview_edit_lock.release();
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled Channel Mixer"));
    return;
  }
  AdjustmentSettings adjustment;
  adjustment.kind = AdjustmentKind::ChannelMixer;
  adjustment.channel_mixer = *settings;
  create_adjustment_layer(tr("Channel Mixer"), adjustment);
}

void MainWindow::new_photo_filter_adjustment_layer() {
  std::optional<LayerId> preview_id;
  const auto restore_active_layer = document().active_layer_id();
  const auto preview_changed = [this, &preview_id, restore_active_layer](bool enabled,
                                                                         const PhotoFilterSettings& filter) {
    AdjustmentSettings settings;
    settings.kind = AdjustmentKind::PhotoFilter;
    settings.photo_filter = filter;
    update_adjustment_layer_preview(tr("Photo Filter"), settings, enabled, preview_id, restore_active_layer);
  };

  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto settings = request_photo_filter_settings(this, preview_changed);
  remove_adjustment_layer_preview(preview_id, restore_active_layer);
  preview_edit_lock.release();
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled Photo Filter"));
    return;
  }
  AdjustmentSettings adjustment;
  adjustment.kind = AdjustmentKind::PhotoFilter;
  adjustment.photo_filter = *settings;
  create_adjustment_layer(tr("Photo Filter"), adjustment);
}

namespace {

RgbColor rgb_of(const QColor& color) {
  return RgbColor{static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
                  static_cast<std::uint8_t>(color.blue())};
}

}  // namespace

void MainWindow::new_gradient_map_adjustment_layer() {
  std::optional<LayerId> preview_id;
  const auto restore_active_layer = document().active_layer_id();
  const auto preview_changed = [this, &preview_id, restore_active_layer](bool enabled,
                                                                         const GradientMapSettings& map) {
    AdjustmentSettings settings;
    settings.kind = AdjustmentKind::GradientMap;
    settings.gradient_map = map;
    update_adjustment_layer_preview(tr("Gradient Map"), settings, enabled, preview_id, restore_active_layer);
  };

  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto settings =
      request_gradient_map_settings(this, preview_changed, {}, &gradient_library(), rgb_of(canvas_->primary_color()),
                                    rgb_of(canvas_->secondary_color()));
  remove_adjustment_layer_preview(preview_id, restore_active_layer);
  preview_edit_lock.release();
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled Gradient Map"));
    return;
  }
  AdjustmentSettings adjustment;
  adjustment.kind = AdjustmentKind::GradientMap;
  adjustment.gradient_map = *settings;
  create_adjustment_layer(tr("Gradient Map"), adjustment);
}

void MainWindow::new_black_white_adjustment_layer() {
  std::optional<LayerId> preview_id;
  const auto restore_active_layer = document().active_layer_id();
  const auto preview_changed = [this, &preview_id, restore_active_layer](bool enabled,
                                                                         const BlackWhiteSettings& bw) {
    AdjustmentSettings settings;
    settings.kind = AdjustmentKind::BlackWhite;
    settings.black_white = bw;
    update_adjustment_layer_preview(tr("Black & White"), settings, enabled, preview_id, restore_active_layer);
  };

  auto preview_edit_lock = lock_preview_dialog_edits();
  const auto settings = request_black_white_settings(this, preview_changed);
  remove_adjustment_layer_preview(preview_id, restore_active_layer);
  preview_edit_lock.release();
  if (!settings.has_value()) {
    statusBar()->showMessage(tr("Cancelled Black & White"));
    return;
  }
  AdjustmentSettings adjustment;
  adjustment.kind = AdjustmentKind::BlackWhite;
  adjustment.black_white = *settings;
  create_adjustment_layer(tr("Black & White"), adjustment);
}

void MainWindow::new_brightness_contrast_adjustment_layer() {
  std::optional<LayerId> preview_id;
  const auto restore_active_layer = document().active_layer_id();
  const auto preview_changed = [this, &preview_id, restore_active_layer](
                                   bool enabled, const BrightnessContrastSettings& brightness_contrast) {
    AdjustmentSettings settings;
    settings.kind = AdjustmentKind::BrightnessContrast;
    settings.brightness_contrast = clamp_brightness_contrast(brightness_contrast);
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
  settings.brightness_contrast = clamp_brightness_contrast(brightness_contrast);
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
    layer.set_mask(LayerMask{to_core_rect(selection_rect),
                             coverage_at_document_depth(doc, selection_mask_pixels(*canvas_, selection_rect)), 0,
                             false});
  } else {
    PixelBuffer mask_pixels(doc.width(), doc.height(), PixelFormat::gray8());
    mask_pixels.clear(255);
    layer.set_mask(LayerMask{Rect::from_size(doc.width(), doc.height()),
                             coverage_at_document_depth(doc, std::move(mask_pixels)), 255, false});
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
  const auto selected_ids = selected_or_active_layer_ids();
  const auto anchor_id = selected_ids.empty() ? std::nullopt : std::optional<LayerId>{selected_ids.front()};
  insert_layer_after_anchor(doc, std::move(preview), anchor_id);
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

  const auto layer_id = layer.id();
  const auto selected_ids = selected_or_active_layer_ids();
  const auto anchor_id = selected_ids.empty() ? std::nullopt : std::optional<LayerId>{selected_ids.front()};
  insert_layer_after_anchor(doc, std::move(layer), anchor_id);
  doc.set_active_layer(layer_id);
  refresh_layer_list();
  refresh_layer_controls();
  refresh_document_info();
  canvas_->document_changed();
  statusBar()->showMessage(tr("Added %1 adjustment layer").arg(label));
}

void MainWindow::edit_active_adjustment_layer() {
  if (canvas_ == nullptr || !has_active_document()) {
    return;  // the menu item is disabled then; scripts and stale shortcuts land here (issue 162)
  }
  // Adjustment dialogs are preview dialogs; opening one on top of another
  // preview dialog (e.g. by double-clicking a layer row while one is open)
  // stacks nested event loops and crashes.
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return;
  }
  if (refuse_layer_dialog_during_transform()) {
    return;
  }
  auto& doc = document();
  select_only_layer_if_none_active();
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
        settings.color_balance = color_balance;
        apply_settings(settings);
      };
      const auto result =
          request_color_balance_settings(this, preview_changed, original_settings->color_balance);
      if (result.has_value()) {
        accepted_settings = *original_settings;
        accepted_settings->color_balance = *result;
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
    case AdjustmentKind::ChannelMixer: {
      const auto preview_changed = [apply_settings, restore_original_layer, original_settings](
                                       bool enabled, const ChannelMixerSettings& mixer) {
        if (!enabled) {
          restore_original_layer();
          return;
        }
        auto settings = *original_settings;
        settings.channel_mixer = mixer;
        apply_settings(settings);
      };
      const auto result =
          request_channel_mixer_settings(this, preview_changed, original_settings->channel_mixer);
      if (result.has_value()) {
        accepted_settings = *original_settings;
        accepted_settings->channel_mixer = *result;
      }
      break;
    }
    case AdjustmentKind::PhotoFilter: {
      const auto preview_changed = [apply_settings, restore_original_layer, original_settings](
                                       bool enabled, const PhotoFilterSettings& filter) {
        if (!enabled) {
          restore_original_layer();
          return;
        }
        auto settings = *original_settings;
        settings.photo_filter = filter;
        apply_settings(settings);
      };
      const auto result = request_photo_filter_settings(this, preview_changed, original_settings->photo_filter);
      if (result.has_value()) {
        accepted_settings = *original_settings;
        accepted_settings->photo_filter = *result;
      }
      break;
    }
    case AdjustmentKind::ColorLookup:
      // Imported lookups render and save unchanged; choosing another table is not offered yet.
      statusBar()->showMessage(
          tr("Color Lookup \"%1\" is applied as stored in the file.")
              .arg(QString::fromStdString(original_settings->color_lookup.name)));
      break;
    case AdjustmentKind::BlackWhite: {
      const auto preview_changed = [apply_settings, restore_original_layer, original_settings](
                                       bool enabled, const BlackWhiteSettings& bw) {
        if (!enabled) {
          restore_original_layer();
          return;
        }
        auto settings = *original_settings;
        settings.black_white = bw;
        apply_settings(settings);
      };
      const auto result = request_black_white_settings(this, preview_changed, original_settings->black_white);
      if (result.has_value()) {
        accepted_settings = *original_settings;
        accepted_settings->black_white = *result;
      }
      break;
    }
    case AdjustmentKind::GradientMap: {
      const auto preview_changed = [apply_settings, restore_original_layer, original_settings](
                                       bool enabled, const GradientMapSettings& map) {
        if (!enabled) {
          restore_original_layer();
          return;
        }
        auto settings = *original_settings;
        settings.gradient_map = map;
        apply_settings(settings);
      };
      const auto result =
          request_gradient_map_settings(this, preview_changed, original_settings->gradient_map, &gradient_library(),
                                        rgb_of(canvas_->primary_color()),
                                        rgb_of(canvas_->secondary_color()));
      if (result.has_value()) {
        accepted_settings = *original_settings;
        accepted_settings->gradient_map = *result;
      }
      break;
    }
    case AdjustmentKind::Exposure: {
      const auto preview_changed = [apply_settings, restore_original_layer, original_settings](
                                       bool enabled, const ExposureSettings& exposure) {
        if (!enabled) {
          restore_original_layer();
          return;
        }
        auto settings = *original_settings;
        settings.exposure = clamp_exposure(exposure);
        apply_settings(settings);
      };
      const auto result = request_exposure_settings(this, preview_changed, original_settings->exposure);
      if (result.has_value()) {
        accepted_settings = *original_settings;
        accepted_settings->exposure = clamp_exposure(*result);
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
        settings.brightness_contrast = clamp_brightness_contrast(brightness_contrast);
        apply_settings(settings);
      };
      const auto result = request_brightness_contrast_settings(
          this, preview_changed, original_settings->brightness_contrast);
      if (result.has_value()) {
        accepted_settings = *original_settings;
        accepted_settings->brightness_contrast = clamp_brightness_contrast(*result);
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
