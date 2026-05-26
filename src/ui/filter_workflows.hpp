#pragma once

#include "core/layer.hpp"
#include "filters/filter_registry.hpp"

#include <QColor>
#include <QRegion>
#include <QString>

#include <functional>
#include <optional>
#include <vector>

class QWidget;

namespace photoslop::ui {

struct HueSaturationSettings {
  int hue_shift{0};
  int saturation_delta{0};
  int lightness_delta{0};
};

struct LevelsSettings {
  int black_input{0};
  int white_input{255};
  int gamma_percent{100};
};

struct CurvesSettings {
  int shadow_output{0};
  int midtone_output{128};
  int highlight_output{255};
};

struct ColorBalanceSettings {
  int cyan_red{0};
  int magenta_green{0};
  int yellow_blue{0};
};

struct FilterControlSpec {
  QString label;
  QString object_name;
  int minimum{0};
  int maximum{100};
  int value{100};
  QString suffix;
};

struct FilterDialogSpec {
  QString identifier;
  QString display_name;
  std::vector<FilterControlSpec> controls;
};

struct FilterPreviewSettings {
  bool preview_enabled{true};
  std::vector<int> values;
};

[[nodiscard]] QString filter_action_object_name(const QString& identifier);
[[nodiscard]] bool is_adjustment_only_filter(const QString& identifier);
[[nodiscard]] FilterDialogSpec filter_dialog_spec_for(const FilterDefinition& filter);
[[nodiscard]] std::optional<std::vector<int>> request_filter_settings(
    QWidget* parent, const FilterDialogSpec& spec, const std::function<void(FilterPreviewSettings)>& preview_changed);
[[nodiscard]] std::optional<LevelsSettings> request_levels_settings(
    QWidget* parent, std::function<void(bool, const LevelsSettings&)> preview_changed = {});
[[nodiscard]] std::optional<CurvesSettings> request_curves_settings(
    QWidget* parent, std::function<void(bool, const CurvesSettings&)> preview_changed = {});
[[nodiscard]] std::optional<HueSaturationSettings> request_hue_saturation_settings(
    QWidget* parent, std::function<void(bool, const HueSaturationSettings&)> preview_changed = {});
[[nodiscard]] std::optional<ColorBalanceSettings> request_color_balance_settings(
    QWidget* parent, std::function<void(bool, const ColorBalanceSettings&)> preview_changed = {});
void apply_filter_with_settings(const QString& identifier, const FilterRegistry& registry, PixelBuffer& pixels,
                                const std::vector<int>& values, QColor foreground = QColor(Qt::black),
                                QColor background = QColor(Qt::white));
[[nodiscard]] PixelBuffer build_filter_preview_pixels(
    const PixelBuffer& original, const QRegion& selection, Rect bounds, const QString& identifier,
    const FilterRegistry& registry, const FilterPreviewSettings& settings, QColor foreground = QColor(Qt::black),
    QColor background = QColor(Qt::white));
[[nodiscard]] bool pixel_buffers_equal(const PixelBuffer& lhs, const PixelBuffer& rhs);
[[nodiscard]] bool editable_rgb8_layer(const Layer* layer);
void apply_levels_to_pixels(PixelBuffer& pixels, Rect bounds, const QRegion& selection, LevelsSettings settings);
void apply_curves_to_pixels(PixelBuffer& pixels, Rect bounds, const QRegion& selection, CurvesSettings settings);
void apply_hue_saturation_to_pixels(PixelBuffer& pixels, Rect bounds, const QRegion& selection,
                                    HueSaturationSettings settings);
void apply_color_balance_to_pixels(PixelBuffer& pixels, Rect bounds, const QRegion& selection,
                                   ColorBalanceSettings settings);

}  // namespace photoslop::ui
