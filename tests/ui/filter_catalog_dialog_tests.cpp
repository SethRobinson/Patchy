#include "ui/curved_slider.hpp"
#include <QStatusBar>

#include "ui/canvas_widget.hpp"
#include "core/smart_object.hpp"
#include "ui/filter_parameter_panel.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/image_document_io.hpp"
#include "ui/liquify_dialog.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/theme_palette.hpp"
#include "ui/transparency_checkerboard.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/main_window.hpp"
#include "ui/zoomable_image_preview.hpp"
#include "filters/builtin_filters.hpp"
#include "test_harness.hpp"
#include <QAction>
#include <QApplication>
#include <QBrush>
#include <QPixmap>
#include <QTransform>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QImage>
#include <QKeySequence>
#include <QList>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QSlider>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QWidget>
#include <set>
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

struct ExpectedFilterParameter {
  const char* key;
  const char* object_name;
  double minimum;
  double maximum;
  double default_value;
  patchy::FilterParameterUnit unit;
  patchy::FilterSpatialScale spatial_scale{patchy::FilterSpatialScale::None};
  patchy::FilterParameterKind kind{patchy::FilterParameterKind::Integer};
  double step{1.0};
  patchy::FilterParameterPresentation presentation{
      patchy::FilterParameterPresentation::Standard};
};

struct ExpectedFilterCatalogEntry {
  const char* id;
  patchy::FilterCategory category;
  bool adjustment_only;
  std::vector<ExpectedFilterParameter> parameters;
};

const std::vector<ExpectedFilterCatalogEntry>& expected_filter_catalog() {
  using Category = patchy::FilterCategory;
  using Kind = patchy::FilterParameterKind;
  using Presentation = patchy::FilterParameterPresentation;
  using Scale = patchy::FilterSpatialScale;
  using Unit = patchy::FilterParameterUnit;
  static const std::vector<ExpectedFilterCatalogEntry> expected = {
      {"patchy.filters.invert", Category::Adjustment, true,
       {{"amount", "filterAmount", 0, 100, 100, Unit::Percent}}},
      {"patchy.filters.brightness_contrast", Category::Adjustment, true,
       {{"brightness", "filterBrightness", -100, 100, 0, Unit::None},
        {"contrast", "filterContrast", -100, 100, 0, Unit::Percent}}},
      {"patchy.filters.grayscale", Category::Adjustment, true,
       {{"amount", "filterAmount", 0, 100, 100, Unit::Percent}}},
      {"patchy.filters.desaturate", Category::Adjustment, true,
       {{"amount", "filterAmount", 0, 100, 100, Unit::Percent}}},
      {"patchy.filters.auto_tone", Category::Adjustment, true,
       {{"amount", "filterAmount", 0, 100, 100, Unit::Percent}}},
      {"patchy.filters.auto_contrast", Category::Adjustment, true,
       {{"amount", "filterAmount", 0, 100, 100, Unit::Percent}}},
      {"patchy.filters.auto_color", Category::Adjustment, true,
       {{"amount", "filterAmount", 0, 100, 100, Unit::Percent}}},
      {"patchy.filters.soft_glow", Category::PhotoLooks, false,
       {{"amount", "filterAmount", 0, 100, 100, Unit::Percent}}},
      {"patchy.filters.punchy_color", Category::PhotoLooks, false,
       {{"amount", "filterAmount", 0, 100, 100, Unit::Percent}}},
      {"patchy.filters.noir", Category::PhotoLooks, false,
       {{"amount", "filterAmount", 0, 100, 100, Unit::Percent}}},
      {"patchy.filters.cinematic_matte", Category::PhotoLooks, false,
       {{"amount", "filterAmount", 0, 100, 100, Unit::Percent}}},
      {"patchy.filters.vintage_fade", Category::PhotoLooks, false,
       {{"amount", "filterAmount", 0, 100, 100, Unit::Percent}}},
      {"patchy.filters.sepia", Category::PhotoLooks, false,
       {{"amount", "filterAmount", 0, 100, 100, Unit::Percent}}},
      {"patchy.filters.threshold", Category::Adjustment, true,
       {{"threshold", "filterThreshold", 0, 255, 128, Unit::None}}},
      {"patchy.filters.posterize", Category::Adjustment, true,
       {{"levels", "filterLevels", 2, 255, 4, Unit::None}}},
      {"patchy.filters.box_blur", Category::Blur, false,
       {{"radius", "filterRadius", 1, 2000, 1, Unit::Pixels, Scale::Pixels}}},
      {"patchy.filters.sharpen", Category::Sharpen, false,
       {{"amount", "filterAmount", 0, 300, 100, Unit::Percent}}},
      {"patchy.filters.unsharp_mask",
       Category::Sharpen,
       false,
       {{"amount", "filterAmount", 1, 500, 150, Unit::Percent},
        {"radius", "filterRadius", 0.1, 1000, 2, Unit::Pixels, Scale::Pixels,
         Kind::Double, 0.1},
        {"threshold", "filterThreshold", 0, 255, 8, Unit::None}}},
      {"patchy.filters.gaussian_blur", Category::Blur, false,
       {{"radius", "filterRadius", 0.1, 1000, 2, Unit::Pixels, Scale::Pixels,
         Kind::Double, 0.1}}},
      {"patchy.filters.motion_blur",
       Category::Blur,
       false,
       {{"angle", "filterAngle", -360, 360, 0, Unit::Degrees, Scale::None,
         Kind::Integer, 1.0, Presentation::Angle},
        {"distance", "filterDistance", 1, 2000, 12, Unit::Pixels,
         Scale::Pixels}}},
      {"patchy.filters.radial_blur",
       Category::Blur,
       false,
       {{"amount", "filterAmount", 0, 100, 35, Unit::Percent},
        {"samples", "filterSamples", 4, 32, 16, Unit::None},
        {"center_x", "filterCenterX", 0, 100, 50, Unit::Percent, Scale::None,
         Kind::Double, 0.1, Presentation::CenterXPercent},
        {"center_y", "filterCenterY", 0, 100, 50, Unit::Percent, Scale::None,
         Kind::Double, 0.1, Presentation::CenterYPercent}}},
      {"patchy.filters.edge_detect", Category::Stylize, false,
       {{"strength", "filterStrength", 0, 300, 100, Unit::Percent}}},
      {"patchy.filters.emboss", Category::Stylize, false,
       {{"angle", "filterAngle", -360, 360, 135, Unit::Degrees, Scale::None,
         Kind::Integer, 1.0, Presentation::Angle},
        {"height", "filterHeight", 1, 100, 2, Unit::Pixels, Scale::Pixels},
        {"amount", "filterDepth", 0, 500, 100, Unit::Percent}}},
      {"patchy.filters.glowing_edges", Category::Stylize, false,
       {{"edge_width", "filterEdgeWidth", 1, 12, 2, Unit::Pixels, Scale::Pixels},
        {"brightness", "filterBrightness", 0, 300, 140, Unit::Percent},
        {"smoothness", "filterSmoothness", 0, 12, 2, Unit::Pixels, Scale::Pixels}}},
      {"patchy.filters.twirl", Category::Distort, false,
       {{"angle", "filterAngle", -720, 720, 180, Unit::Degrees, Scale::None,
         Kind::Integer, 1.0, Presentation::Angle},
        {"radius", "filterRadius", 1, 100, 100, Unit::Percent, Scale::None,
         Kind::Integer, 1.0, Presentation::EffectRadiusPercent},
        {"center_x", "filterCenterX", 0, 100, 50, Unit::Percent, Scale::None,
         Kind::Double, 0.1, Presentation::CenterXPercent},
        {"center_y", "filterCenterY", 0, 100, 50, Unit::Percent, Scale::None,
         Kind::Double, 0.1, Presentation::CenterYPercent}}},
      {"patchy.filters.wave", Category::Distort, false,
       {{"amplitude", "filterAmplitude", 0, 999, 12, Unit::Pixels, Scale::Pixels,
         Kind::Integer, 1.0, Presentation::WaveAmplitude},
        {"wavelength", "filterWavelength", 4, 999, 48, Unit::Pixels, Scale::Pixels,
         Kind::Integer, 1.0, Presentation::WaveWavelength},
        {"phase", "filterPhase", 0, 360, 0, Unit::Degrees, Scale::None,
         Kind::Integer, 1.0, Presentation::WavePhase}}},
      {"patchy.filters.pinch_bloat", Category::Distort, false,
       {{"amount", "filterAmount", -100, 100, 35, Unit::Percent},
        {"radius", "filterRadius", 1, 100, 100, Unit::Percent, Scale::None,
         Kind::Integer, 1.0, Presentation::EffectRadiusPercent},
        {"center_x", "filterCenterX", 0, 100, 50, Unit::Percent, Scale::None,
         Kind::Double, 0.1, Presentation::CenterXPercent},
        {"center_y", "filterCenterY", 0, 100, 50, Unit::Percent, Scale::None,
         Kind::Double, 0.1, Presentation::CenterYPercent}}},
      {"patchy.filters.clouds", Category::Render, false,
       {{"scale", "filterScale", 12, 512, 96, Unit::Pixels, Scale::Pixels},
        {"detail", "filterDetail", 1, 8, 6, Unit::None},
        {"contrast", "filterContrast", 0, 100, 40, Unit::Percent},
        {"seed", "filterSeed", 1, 9999, 1, Unit::None}}},
      {"patchy.filters.pixelate", Category::Pixelate, false,
       {{"block_size", "filterBlockSize", 2, 200, 4, Unit::Pixels, Scale::Pixels}}},
      {"patchy.filters.color_halftone", Category::Pixelate, false,
       {{"cell_size", "filterCellSize", 4, 127, 10, Unit::Pixels, Scale::Pixels},
        {"intensity", "filterIntensity", 0, 100, 75, Unit::Percent},
        {"contrast", "filterContrast", 0, 100, 60, Unit::Percent}}},
      {"patchy.filters.film_grain", Category::Noise, false,
       {{"amount", "filterAmount", 0, 100, 50, Unit::Percent}}},
      {"patchy.filters.add_noise", Category::Noise, false,
       {{"amount", "filterAmount", 0.1, 400, 12.5, Unit::Percent,
         Scale::None, Kind::Double, 0.1},
        {"distribution", "filterDistribution", 0, 0, 0, Unit::None,
         Scale::None, Kind::Option},
        {"monochromatic", "filterMonochromatic", 0, 0, 0, Unit::None,
         Scale::None, Kind::Boolean},
        {"seed", "filterSeed", 0, 999999999, 1, Unit::None}}},
      {"patchy.filters.vignette", Category::PhotoLooks, false,
       {{"strength", "filterStrength", 0, 100, 55, Unit::Percent},
        {"center_x", "filterCenterX", 0, 100, 50, Unit::Percent, Scale::None,
         Kind::Double, 0.1, Presentation::CenterXPercent},
        {"center_y", "filterCenterY", 0, 100, 50, Unit::Percent, Scale::None,
         Kind::Double, 0.1, Presentation::CenterYPercent}}},
      {"patchy.filters.high_pass", Category::Sharpen, false,
       {{"radius", "filterRadius", 0.1, 1000, 10, Unit::Pixels, Scale::Pixels,
         Kind::Double, 0.1}}},
      {"patchy.filters.median", Category::Noise, false,
       {{"radius", "filterRadius", 1, 500, 1, Unit::Pixels, Scale::Pixels,
         Kind::Double, 0.01}}},
      {"patchy.filters.dust_and_scratches", Category::Noise, false,
       {{"radius", "filterRadius", 1, 500, 1, Unit::Pixels, Scale::Pixels},
        {"threshold", "filterThreshold", 0, 255, 0, Unit::None}}},
      {"patchy.filters.surface_blur", Category::Blur, false,
       {{"radius", "filterRadius", 1, 100, 5, Unit::Pixels, Scale::Pixels,
         Kind::Double, 0.01},
        {"threshold", "filterThreshold", 2, 255, 15, Unit::None}}},
      {"patchy.filters.lens_blur", Category::Blur, false,
       {{"radius", "filterRadius", 0, 100, 15, Unit::Pixels, Scale::Pixels,
         Kind::Double, 0.1},
        {"blades", "filterBlades", 3, 8, 6, Unit::None},
        {"blade_curvature", "filterBladeCurvature", 0, 100, 50,
         Unit::Percent},
        {"rotation", "filterRotation", -180, 180, 0, Unit::Degrees,
         Scale::None, Kind::Integer, 1.0, Presentation::Angle}}},
      {"patchy.filters.iris_blur", Category::Blur, false,
       {{"blur", "filterBlur", 0, 500, 15, Unit::Pixels, Scale::Pixels,
         Kind::Double, 0.1},
        {"center_x", "filterCenterX", 0, 100, 50, Unit::Percent,
         Scale::None, Kind::Double, 0.1, Presentation::CenterXPercent},
        {"center_y", "filterCenterY", 0, 100, 50, Unit::Percent,
         Scale::None, Kind::Double, 0.1, Presentation::CenterYPercent},
        {"angle", "filterAngle", -180, 180, 0, Unit::Degrees, Scale::None,
         Kind::Integer, 1.0, Presentation::Angle},
        {"iris_width", "filterIrisWidth", 1, 200, 50, Unit::Percent,
         Scale::None, Kind::Double, 0.1, Presentation::IrisWidthPercent},
        {"iris_height", "filterIrisHeight", 1, 200, 40, Unit::Percent,
         Scale::None, Kind::Double, 0.1, Presentation::IrisHeightPercent},
        {"focus", "filterFocus", 0, 100, 50, Unit::Percent, Scale::None,
         Kind::Double, 0.1}}},
      {"patchy.filters.tilt_shift_blur", Category::Blur, false,
       {{"blur", "filterBlur", 0, 500, 15, Unit::Pixels, Scale::Pixels,
         Kind::Double, 0.1},
        {"center_x", "filterCenterX", 0, 100, 50, Unit::Percent,
         Scale::None, Kind::Double, 0.1, Presentation::CenterXPercent},
        {"center_y", "filterCenterY", 0, 100, 50, Unit::Percent,
         Scale::None, Kind::Double, 0.1, Presentation::CenterYPercent},
        {"angle", "filterAngle", -180, 180, 0, Unit::Degrees, Scale::None,
         Kind::Integer, 1.0, Presentation::Angle},
        {"focus_half_width", "filterFocusHalfWidth", 0, 100, 10,
         Unit::Percent, Scale::None, Kind::Double, 0.1,
         Presentation::TiltFocusHalfWidthPercent},
        {"transition_width", "filterTransitionWidth", 0, 100, 20,
         Unit::Percent, Scale::None, Kind::Double, 0.1,
         Presentation::TiltTransitionWidthPercent}}},
      {"patchy.filters.plastic_wrap", Category::Artistic, false,
       {{"highlight_strength", "filterHighlightStrength", 0, 20, 9,
         Unit::None},
        {"detail", "filterDetail", 1, 15, 7, Unit::None},
        {"smoothness", "filterSmoothness", 1, 15, 5, Unit::None}}},
      {"patchy.filters.blur", Category::Blur, false, {}},
      {"patchy.filters.blur_more", Category::Blur, false, {}},
      {"patchy.filters.average", Category::Blur, false, {}},
      {"patchy.filters.sharpen_more", Category::Sharpen, false, {}},
      {"patchy.filters.sharpen_edges", Category::Sharpen, false, {}},
  };
  return expected;
}

QStringList filter_action_object_names(const QMenu& menu) {
  QStringList result;
  for (const auto* action : menu.actions()) {
    if (!action->isSeparator() && action->menu() == nullptr &&
        action->objectName().startsWith(QStringLiteral("filterAction_"))) {
      result.append(action->objectName());
    }
  }
  return result;
}

void ui_filter_catalog_and_menu_contracts_are_stable() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  const auto& expected = expected_filter_catalog();
  CHECK(registry.filters().size() == expected.size());
  for (std::size_t filter_index = 0; filter_index < expected.size(); ++filter_index) {
    const auto& actual_filter = registry.filters()[filter_index];
    const auto& expected_filter = expected[filter_index];
    CHECK(actual_filter.identifier == expected_filter.id);
    CHECK(actual_filter.catalog.category == expected_filter.category);
    CHECK(actual_filter.catalog.adjustment_only == expected_filter.adjustment_only);
    // Clouds is the only canvas-filling (generative) filter in the catalog.
    CHECK(actual_filter.catalog.fills_entire_canvas ==
          (actual_filter.identifier == "patchy.filters.clouds"));
    CHECK(actual_filter.catalog.schema_version == 1U);
    CHECK(actual_filter.catalog.parameters.size() == expected_filter.parameters.size());
    CHECK(static_cast<bool>(actual_filter.catalog.execute));
    const auto dialog_spec = patchy::ui::filter_dialog_spec_for(actual_filter);
    CHECK(dialog_spec.identifier == QString::fromLatin1(expected_filter.id));
    CHECK(dialog_spec.schema_version == 1U);
    CHECK(!dialog_spec.display_name.isEmpty());
    CHECK(dialog_spec.controls.size() == expected_filter.parameters.size());
    for (std::size_t parameter_index = 0; parameter_index < expected_filter.parameters.size(); ++parameter_index) {
      const auto& actual = actual_filter.catalog.parameters[parameter_index];
      const auto& wanted = expected_filter.parameters[parameter_index];
      CHECK(actual.key == wanted.key);
      CHECK(actual.control_object_name == wanted.object_name);
      CHECK(actual.kind == wanted.kind);
      if (wanted.kind == patchy::FilterParameterKind::Integer) {
        const auto* default_value = std::get_if<std::int64_t>(&actual.default_value);
        CHECK(default_value != nullptr);
        CHECK(static_cast<double>(*default_value) == wanted.default_value);
      } else if (wanted.kind == patchy::FilterParameterKind::Double) {
        const auto* default_value = std::get_if<double>(&actual.default_value);
        CHECK(default_value != nullptr);
        CHECK(std::abs(*default_value - wanted.default_value) < 0.000001);
      }
      const auto& control = dialog_spec.controls[parameter_index];
      CHECK(control.parameter_key == wanted.key);
      CHECK(control.object_name == QString::fromLatin1(wanted.object_name));
      CHECK(control.kind == wanted.kind);
      CHECK(control.presentation == wanted.presentation);
      CHECK(!control.label.isEmpty());
      // Option and Boolean parameters carry no numeric range; their controls
      // are a combo box and a check box.
      if (wanted.kind == patchy::FilterParameterKind::Option ||
          wanted.kind == patchy::FilterParameterKind::Boolean) {
        CHECK(!actual.minimum.has_value());
        CHECK(!actual.maximum.has_value());
        if (wanted.kind == patchy::FilterParameterKind::Option) {
          CHECK(!actual.options.empty());
          CHECK(control.options.size() == actual.options.size());
          CHECK(std::get_if<std::string>(&actual.default_value) != nullptr);
        } else {
          CHECK(std::get_if<bool>(&actual.default_value) != nullptr);
        }
        CHECK(actual.unit == wanted.unit);
        CHECK(actual.spatial_scale == wanted.spatial_scale);
        CHECK(actual.display_name.empty() == false);
        continue;
      }
      CHECK(actual.minimum.has_value());
      CHECK(actual.maximum.has_value());
      CHECK(*actual.minimum == wanted.minimum);
      CHECK(*actual.maximum == wanted.maximum);
      CHECK(std::abs(actual.step.value_or(1.0) - wanted.step) < 0.000001);
      CHECK(actual.unit == wanted.unit);
      CHECK(actual.spatial_scale == wanted.spatial_scale);
      CHECK(actual.presentation == wanted.presentation);
      CHECK(actual.display_name.empty() == false);

      const auto practical_minimum =
          actual.practical_minimum.value_or(wanted.minimum);
      const auto practical_maximum =
          actual.practical_maximum.value_or(wanted.maximum);
      CHECK(control.minimum ==
            static_cast<int>(std::lround(practical_minimum)));
      CHECK(control.maximum ==
            static_cast<int>(std::lround(practical_maximum)));
      CHECK(control.value == static_cast<int>(std::lround(wanted.default_value)));
      CHECK(control.typed_minimum.has_value());
      CHECK(control.typed_maximum.has_value());
      CHECK(*control.typed_minimum == wanted.minimum);
      CHECK(*control.typed_maximum == wanted.maximum);
      CHECK(std::abs(control.step.value_or(1.0) - wanted.step) < 0.000001);
      if (actual_filter.identifier == "patchy.filters.high_pass" &&
          actual.key == "radius") {
        CHECK(actual.practical_minimum == 0.1);
        CHECK(actual.practical_maximum == 100.0);
      } else if (actual_filter.identifier ==
                     "patchy.filters.surface_blur" &&
                 actual.key == "radius") {
        CHECK(actual.practical_minimum == 1.0);
        CHECK(actual.practical_maximum == 25.0);
      } else if ((actual_filter.identifier == "patchy.filters.lens_blur" &&
                  actual.key == "radius") ||
                 (actual_filter.identifier == "patchy.filters.iris_blur" &&
                  actual.key == "blur")) {
        CHECK(actual.practical_minimum == 0.0);
        CHECK(actual.practical_maximum == 50.0);
      } else if (actual_filter.identifier == "patchy.filters.unsharp_mask" &&
                 actual.key == "radius") {
        CHECK(actual.practical_minimum == 0.1);
        CHECK(actual.practical_maximum == 100.0);
      } else if (actual_filter.identifier == "patchy.filters.motion_blur" &&
                 actual.key == "angle") {
        CHECK(actual.practical_minimum == -180.0);
        CHECK(actual.practical_maximum == 180.0);
      } else if (actual_filter.identifier == "patchy.filters.motion_blur" &&
                 actual.key == "distance") {
        CHECK(actual.practical_minimum == 1.0);
        CHECK(actual.practical_maximum == 64.0);
      } else if (actual_filter.identifier == "patchy.filters.tilt_shift_blur" &&
                 actual.key == "blur") {
        CHECK(actual.practical_minimum == 0.0);
        CHECK(actual.practical_maximum == 50.0);
      } else if (actual_filter.identifier == "patchy.filters.emboss" &&
                 actual.key == "angle") {
        CHECK(actual.practical_minimum == -180.0);
        CHECK(actual.practical_maximum == 180.0);
      } else if (actual_filter.identifier == "patchy.filters.emboss" &&
                 actual.key == "height") {
        CHECK(actual.practical_minimum == 1.0);
        CHECK(actual.practical_maximum == 24.0);
      } else if (actual_filter.identifier == "patchy.filters.emboss" &&
                 actual.key == "amount") {
        CHECK(actual.practical_minimum == 0.0);
        CHECK(actual.practical_maximum == 300.0);
      } else if (actual_filter.identifier == "patchy.filters.box_blur" &&
                 actual.key == "radius") {
        CHECK(actual.practical_minimum == 1.0);
        CHECK(actual.practical_maximum == 100.0);
      } else if (actual_filter.identifier == "patchy.filters.gaussian_blur" &&
                 actual.key == "radius") {
        CHECK(actual.practical_minimum == 0.1);
        CHECK(actual.practical_maximum == 100.0);
      } else if (actual_filter.identifier == "patchy.filters.add_noise" &&
                 actual.key == "amount") {
        CHECK(actual.practical_minimum == 0.1);
        CHECK(actual.practical_maximum == 100.0);
      } else {
        CHECK(!actual.practical_minimum.has_value());
        CHECK(!actual.practical_maximum.has_value());
      }
    }
  }

  patchy::ui::MainWindow window;
  show_window(window);
  auto* filter_menu = window.findChild<QMenu*>(QStringLiteral("filterMenu"));
  CHECK(filter_menu != nullptr);
  auto* last_filter_action = require_action(window, "filterLastFilterAction");
  auto* convert_action =
      require_action(window, "filterConvertForSmartFiltersAction");
  auto* gallery_action = require_action(window, "filterGalleryAction");
  auto* camera_raw_filter_action = require_action(window, "filterCameraRawFilterAction");
  auto* liquify_action = require_action(window, "filterLiquifyAction");
  CHECK(convert_action->property("patchy.channelViewBlocked").toBool());
  CHECK(gallery_action->property("patchy.channelViewBlocked").toBool());
  CHECK(liquify_action->property("patchy.channelViewBlocked").toBool());
  CHECK(!filter_menu->actions().isEmpty());
  // Photoshop's order: Last Filter, then Convert for Smart Filters, each in its own group.
  CHECK(filter_menu->actions().size() >= 9);
  CHECK(filter_menu->actions().front() == last_filter_action);
  CHECK(filter_menu->actions()[1]->isSeparator());
  CHECK(filter_menu->actions()[2] == convert_action);
  CHECK(filter_menu->actions()[3]->isSeparator());
  CHECK(filter_menu->actions()[4] == gallery_action);
  CHECK(filter_menu->actions()[5] == camera_raw_filter_action);
  CHECK(filter_menu->actions()[6] == liquify_action);
  CHECK(filter_menu->actions()[7] == require_action(window, "filterPerspectivePlanesAction"));
  CHECK(filter_menu->actions()[8]->isSeparator());
  const auto* convert_command = window.hotkey_registry().find_command(
      QStringLiteral("filter.convert_for_smart_filters"));
  CHECK(convert_command != nullptr);
  CHECK(convert_command->action == convert_action);
  CHECK(convert_command->default_shortcuts.isEmpty());
  CHECK(convert_action->shortcuts().isEmpty());
  const auto* gallery_command = window.hotkey_registry().find_command(QStringLiteral("filter.gallery"));
  CHECK(gallery_command != nullptr);
  CHECK(gallery_command->action == gallery_action);
  CHECK(gallery_command->default_shortcuts.isEmpty());
  CHECK(gallery_action->shortcuts().isEmpty());
  const auto* liquify_command = window.hotkey_registry().find_command(
      QStringLiteral("filter.liquify"));
  CHECK(liquify_command != nullptr);
  CHECK(liquify_command->action == liquify_action);
  CHECK(liquify_command->default_shortcuts ==
        QList<QKeySequence>{QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_X)});
  struct ExpectedMenu {
    const char* object_name;
    QStringList action_object_names;
  };
  const std::array<ExpectedMenu, 9> expected_menus = {{
      {"filterPhotoLooksMenu",
       {QStringLiteral("filterAction_patchy_filters_soft_glow"),
        QStringLiteral("filterAction_patchy_filters_punchy_color"),
        QStringLiteral("filterAction_patchy_filters_noir"),
        QStringLiteral("filterAction_patchy_filters_cinematic_matte"),
        QStringLiteral("filterAction_patchy_filters_vintage_fade"),
        QStringLiteral("filterAction_patchy_filters_sepia"),
        QStringLiteral("filterAction_patchy_filters_vignette")}},
      {"filterBlurMenu",
       {QStringLiteral("filterAction_patchy_filters_box_blur"),
        QStringLiteral("filterAction_patchy_filters_gaussian_blur"),
        QStringLiteral("filterAction_patchy_filters_motion_blur"),
        QStringLiteral("filterAction_patchy_filters_radial_blur"),
        QStringLiteral("filterAction_patchy_filters_surface_blur"),
        QStringLiteral("filterAction_patchy_filters_lens_blur"),
        QStringLiteral("filterAction_patchy_filters_iris_blur"),
        QStringLiteral("filterAction_patchy_filters_tilt_shift_blur"),
        QStringLiteral("filterAction_patchy_filters_blur"),
        QStringLiteral("filterAction_patchy_filters_blur_more"),
        QStringLiteral("filterAction_patchy_filters_average")}},
      {"filterSharpenMenu",
       {QStringLiteral("filterAction_patchy_filters_sharpen"),
        QStringLiteral("filterAction_patchy_filters_unsharp_mask"),
        QStringLiteral("filterAction_patchy_filters_high_pass"),
        QStringLiteral("filterAction_patchy_filters_sharpen_more"),
        QStringLiteral("filterAction_patchy_filters_sharpen_edges")}},
      {"filterDistortMenu",
       {QStringLiteral("filterAction_patchy_filters_twirl"), QStringLiteral("filterAction_patchy_filters_wave"),
        QStringLiteral("filterAction_patchy_filters_pinch_bloat")}},
      {"filterNoiseMenu",
       {QStringLiteral("filterAction_patchy_filters_film_grain"),
        QStringLiteral("filterAction_patchy_filters_add_noise"),
        QStringLiteral("filterAction_patchy_filters_median"),
        QStringLiteral("filterAction_patchy_filters_dust_and_scratches")}},
      {"filterPixelateMenu",
       {QStringLiteral("filterAction_patchy_filters_pixelate"),
        QStringLiteral("filterAction_patchy_filters_color_halftone")}},
      {"filterArtisticMenu",
       {QStringLiteral("filterAction_patchy_filters_plastic_wrap")}},
      {"filterStylizeMenu",
       {QStringLiteral("filterAction_patchy_filters_edge_detect"),
        QStringLiteral("filterAction_patchy_filters_emboss"),
        QStringLiteral("filterAction_patchy_filters_glowing_edges")}},
      {"filterRenderMenu", {QStringLiteral("filterAction_patchy_filters_clouds")}},
  }};
  QStringList actual_submenus;
  for (const auto* action : filter_menu->actions()) {
    if (action->menu() != nullptr && action->menu()->objectName().startsWith(QStringLiteral("filter"))) {
      actual_submenus.append(action->menu()->objectName());
    }
  }
  QStringList expected_submenus;
  for (const auto& expected_menu : expected_menus) {
    expected_submenus.append(QString::fromLatin1(expected_menu.object_name));
    auto* menu = window.findChild<QMenu*>(QString::fromLatin1(expected_menu.object_name));
    CHECK(menu != nullptr);
    CHECK(filter_action_object_names(*menu) == expected_menu.action_object_names);
    CHECK(!menu->actions().contains(liquify_action));
  }
  CHECK(actual_submenus == expected_submenus);
  auto* dust_action = require_action(
      window, "filterAction_patchy_filters_dust_and_scratches");
  CHECK(dust_action->text() == QStringLiteral("Dust && Scratches"));
  CHECK(dust_action->toolTip() == QStringLiteral("Dust & Scratches"));
  auto* surface_action = require_action(
      window, "filterAction_patchy_filters_surface_blur");
  CHECK(surface_action->text() == QStringLiteral("Surface Blur"));
  CHECK(surface_action->toolTip() == QStringLiteral("Surface Blur"));
  auto* lens_action = require_action(
      window, "filterAction_patchy_filters_lens_blur");
  CHECK(lens_action->text() == QStringLiteral("Lens Blur"));
  CHECK(lens_action->toolTip() == QStringLiteral("Lens Blur"));
  auto* iris_action = require_action(
      window, "filterAction_patchy_filters_iris_blur");
  CHECK(iris_action->text() == QStringLiteral("Iris Blur"));
  CHECK(iris_action->toolTip() == QStringLiteral("Iris Blur"));
  auto* plastic_action = require_action(
      window, "filterAction_patchy_filters_plastic_wrap");
  CHECK(plastic_action->text() == QStringLiteral("Plastic Wrap"));
  CHECK(plastic_action->toolTip() == QStringLiteral("Plastic Wrap"));
  auto* tilt_action = require_action(
      window, "filterAction_patchy_filters_tilt_shift_blur");
  CHECK(tilt_action->text() == QStringLiteral("Tilt-Shift Blur"));
  CHECK(tilt_action->toolTip() == QStringLiteral("Tilt-Shift Blur"));

  struct ExpectedHotkeyAction {
    const char* id;
    const char* object_name;
  };
  const std::array<ExpectedHotkeyAction, 8> existing_filter_hotkeys = {{
      {"patchy.filters.invert", "imageAdjustInvertAction"},
      {"patchy.filters.desaturate", "imageAdjustDesaturateAction"},
      {"patchy.filters.auto_tone", "imageAdjustAutoToneAction"},
      {"patchy.filters.auto_contrast", "imageAdjustAutoContrastAction"},
      {"patchy.filters.auto_color", "imageAdjustAutoColorAction"},
      {"patchy.filters.brightness_contrast", "imageAdjustBrightnessContrastAction"},
      {"patchy.filters.threshold", "imageAdjustThresholdAction"},
      {"patchy.filters.posterize", "imageAdjustPosterizeAction"},
  }};
  for (const auto& expected_hotkey : existing_filter_hotkeys) {
    const auto* command = window.hotkey_registry().find_command(QString::fromLatin1(expected_hotkey.id));
    CHECK(command != nullptr);
    CHECK(command->action == require_action(window, expected_hotkey.object_name));
  }
  CHECK(window.hotkey_registry().find_command(QStringLiteral("patchy.filters.grayscale")) == nullptr);
  for (const auto& expected_filter : expected) {
    if (expected_filter.adjustment_only) {
      continue;
    }
    CHECK(window.hotkey_registry().find_command(QString::fromLatin1(expected_filter.id)) == nullptr);
  }
  CHECK(window.findChild<QAction*>(QStringLiteral("filterAction_patchy_filters_threshold")) == nullptr);
  CHECK(window.findChild<QAction*>(QStringLiteral("filterAction_patchy_filters_posterize")) == nullptr);
  CHECK(window.findChild<QAction*>(QStringLiteral("filterAction_patchy_filters_auto_tone")) == nullptr);
  CHECK(window.findChild<QAction*>(QStringLiteral("filterAction_patchy_filters_auto_color")) == nullptr);
}

void ui_liquify_dialog_exposes_manual_tools_and_brush_controls() {
  patchy::PixelBuffer source(80, 60, patchy::PixelFormat::rgba8());
  for (int y = 0; y < source.height(); ++y) {
    for (int x = 0; x < source.width(); ++x) {
      auto* pixel = source.pixel(x, y);
      pixel[0] = static_cast<std::uint8_t>(x * 3);
      pixel[1] = static_cast<std::uint8_t>(y * 4);
      pixel[2] = 90;
      pixel[3] = 255;
    }
  }
  bool inspected = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("liquifyDialog"));
    CHECK(dialog != nullptr);
    for (const auto* name : {"liquifyWarpTool", "liquifyReconstructTool",
                             "liquifySmoothTool", "liquifyTwirlTool",
                             "liquifyPuckerTool", "liquifyBloatTool",
                             "liquifyFreezeTool", "liquifyThawTool"}) {
      auto* tool = dialog->findChild<QToolButton*>(QLatin1String(name));
      CHECK(tool != nullptr);
      CHECK(tool->width() >= tool->fontMetrics().horizontalAdvance(tool->text()) + 16);
    }
    auto* size =
        dialog->findChild<QSpinBox*>(QStringLiteral("liquifySizeSpin"));
    auto* pressure = dialog->findChild<QSpinBox*>(
        QStringLiteral("liquifyPressureSpin"));
    auto* density = dialog->findChild<QSpinBox*>(
        QStringLiteral("liquifyDensitySpin"));
    auto* preview =
        dialog->findChild<QWidget*>(QStringLiteral("liquifyPreview"));
    auto* show_mask = dialog->findChild<QCheckBox*>(
        QStringLiteral("liquifyShowMaskCheck"));
    auto* restore = dialog->findChild<QPushButton*>(
        QStringLiteral("liquifyRestoreAllButton"));
    CHECK(size != nullptr && pressure != nullptr && density != nullptr);
    CHECK(preview != nullptr && show_mask != nullptr && restore != nullptr);
    CHECK(size->minimum() == 5 && size->maximum() == 2000);
    // Size uses the fine-low-end curve: the middle of the track is a quarter
    // of the range, not half.
    auto* size_slider = dialog->findChild<QSlider*>(QStringLiteral("liquifySizeSlider"));
    CHECK(size_slider != nullptr);
    CHECK(size_slider->maximum() == patchy::ui::kCurvedSliderPositions);
    const int original_size = size->value();
    CHECK(patchy::ui::slider_value(*size_slider) == original_size);
    size_slider->setValue(patchy::ui::kCurvedSliderPositions / 2);
    CHECK(size->value() == 504);
    size->setValue(original_size);
    CHECK(patchy::ui::slider_value(*size_slider) == original_size);
    CHECK(pressure->minimum() == 1 && pressure->maximum() == 100);
    CHECK(density->minimum() == 1 && density->maximum() == 100);
    CHECK(show_mask->isChecked());
    auto* bloat = dialog->findChild<QToolButton*>(
        QStringLiteral("liquifyBloatTool"));
    CHECK(bloat != nullptr);
    QTest::mouseClick(bloat, Qt::LeftButton);
    QTest::mouseClick(preview, Qt::LeftButton, Qt::NoModifier,
                      preview->rect().center());
    inspected = true;
    dialog->accept();
  });
  const auto result = patchy::ui::request_liquify(
      nullptr, source, patchy::Rect{0, 0, source.width(), source.height()},
      QRegion());
  CHECK(inspected);
  CHECK(result.has_value());
  CHECK(!result->is_identity());
}

// GitHub #296 follow-ups (in-dialog Undo/Redo, keys, Rate, pen pressure, Show Mesh, the
// Preview toggle and Reconstruct by an amount) drive the workspace on a 160x120 layer,
// below the proxy cap, with the preview fixed at 1:1: proxy point p is widget point
// p + (12, 12). The dialog's default brush there is 50 px with Pressure and Density 50.
patchy::PixelBuffer liquify_test_source(int width = 160, int height = 120) {
  patchy::PixelBuffer source(width, height, patchy::PixelFormat::rgba8());
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      auto* pixel = source.pixel(x, y);
      const bool check = ((x / 16) + (y / 16)) % 2 == 0;
      pixel[0] = static_cast<std::uint8_t>(check ? 230 : 40 + (x * 150) / std::max(1, width - 1));
      pixel[1] = static_cast<std::uint8_t>(check ? 200 : 60 + (y * 150) / std::max(1, height - 1));
      pixel[2] = static_cast<std::uint8_t>(check ? 90 : 210);
      pixel[3] = 255;
    }
  }
  return source;
}

using LiquifyDriver = std::function<void(QDialog&, QWidget&)>;

std::optional<patchy::LiquifyMesh> drive_liquify(const LiquifyDriver& driver, bool fixed_preview = true,
                                                 const patchy::PixelBuffer& source = liquify_test_source()) {
  std::exception_ptr driver_error;
  bool ran = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("liquifyDialog"));
    try {
      CHECK(dialog != nullptr);
      auto* preview = dialog->findChild<QWidget*>(QStringLiteral("liquifyPreview"));
      CHECK(preview != nullptr);
      if (fixed_preview) {
        preview->setFixedSize(source.width() + 24, source.height() + 24);
      }
      dialog->activateWindow();
      preview->setFocus();
      QApplication::processEvents();
      driver(*dialog, *preview);
      ran = true;
      if (dialog->isVisible()) {
        dialog->accept();
      }
    } catch (...) {
      driver_error = std::current_exception();
      if (dialog != nullptr) {
        dialog->reject();
      }
    }
  });
  auto mesh = patchy::ui::request_liquify(nullptr, source, patchy::Rect{0, 0, source.width(), source.height()},
                                          QRegion());
  if (driver_error) {
    std::rethrow_exception(driver_error);
  }
  CHECK(ran);
  return mesh;
}

QPoint liquify_widget_point(QPointF proxy_point) {
  return (proxy_point + QPointF(12.0, 12.0)).toPoint();
}

void liquify_drag(QWidget& preview, QPointF from, QPointF to) {
  drag(preview, liquify_widget_point(from), liquify_widget_point(to));
}

// The mesh the same drag makes: the press dab, then the move segment.
void expect_drag(patchy::LiquifyMesh& mesh, QPointF from, QPointF to,
                 patchy::LiquifyTool tool = patchy::LiquifyTool::ForwardWarp, double pressure = 50.0) {
  mesh.apply_stroke(tool, from.x(), from.y(), from.x(), from.y(), 50.0, pressure, 50.0);
  mesh.apply_stroke(tool, from.x(), from.y(), to.x(), to.y(), 50.0, pressure, 50.0);
}

std::vector<double> liquify_mesh_samples(const patchy::LiquifyMesh& mesh) {
  std::vector<double> samples;
  for (int y = 0; y < mesh.height(); y += 4) {
    for (int x = 0; x < mesh.width(); x += 4) {
      const auto [dx, dy] = mesh.displacement_at(x, y);
      samples.push_back(dx);
      samples.push_back(dy);
      samples.push_back(mesh.freeze_strength_at(x, y));
    }
  }
  return samples;
}

bool liquify_meshes_equal(const std::optional<patchy::LiquifyMesh>& actual, const patchy::LiquifyMesh& expected) {
  return actual.has_value() && liquify_mesh_samples(*actual) == liquify_mesh_samples(expected);
}

QToolButton* liquify_tool_button(QDialog& dialog, const char* name) {
  auto* button = dialog.findChild<QToolButton*>(QLatin1String(name));
  CHECK(button != nullptr);
  return button;
}

QPushButton* liquify_push_button(QDialog& dialog, const char* name) {
  auto* button = dialog.findChild<QPushButton*>(QLatin1String(name));
  CHECK(button != nullptr);
  return button;
}

void liquify_key(QWidget& preview, Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QTest::keyClick(&preview, key, modifiers);
  QApplication::processEvents();
}

// Every stroke, Restore All and Freeze/Thaw stroke is one step of the dialog's own
// history: Ctrl+Z (Command on macOS), Ctrl+Shift+Z and Ctrl+Y, and the Undo and Redo
// buttons, which enable only while a step is there. The history keeps 100 steps.
void ui_liquify_undo_redo_steps_strokes_and_restore() {
  const QPointF a_from(40.0, 60.0), a_to(70.0, 60.0);
  const QPointF b_from(110.0, 40.0), b_to(115.0, 80.0);
  patchy::LiquifyMesh only_a(160, 120);
  expect_drag(only_a, a_from, a_to);
  auto a_and_b = only_a;
  expect_drag(a_and_b, b_from, b_to);

  // Ctrl+Z undoes the last stroke; Ctrl+Shift+Z and Ctrl+Y redo it.
  CHECK(liquify_meshes_equal(drive_liquify([&](QDialog&, QWidget& preview) {
    liquify_drag(preview, a_from, a_to);
    liquify_drag(preview, b_from, b_to);
    liquify_key(preview, Qt::Key_Z, Qt::ControlModifier);
  }), only_a));
  for (const auto redo : {Qt::Key_Z, Qt::Key_Y}) {
    CHECK(liquify_meshes_equal(drive_liquify([&](QDialog&, QWidget& preview) {
      liquify_drag(preview, a_from, a_to);
      liquify_drag(preview, b_from, b_to);
      liquify_key(preview, Qt::Key_Z, Qt::ControlModifier);
      liquify_key(preview, redo, redo == Qt::Key_Z ? Qt::ControlModifier | Qt::ShiftModifier : Qt::ControlModifier);
    }), a_and_b));
  }

  // Restore All is a step; the buttons follow the history.
  CHECK(liquify_meshes_equal(drive_liquify([&](QDialog& dialog, QWidget& preview) {
    auto* undo = liquify_push_button(dialog, "liquifyUndoButton");
    auto* redo = liquify_push_button(dialog, "liquifyRedoButton");
    CHECK(!undo->isEnabled() && !redo->isEnabled());
    liquify_drag(preview, a_from, a_to);
    CHECK(undo->isEnabled() && !redo->isEnabled());
    QTest::mouseClick(liquify_push_button(dialog, "liquifyRestoreAllButton"), Qt::LeftButton);
    QTest::mouseClick(undo, Qt::LeftButton);
    CHECK(redo->isEnabled());
    QTest::mouseClick(redo, Qt::LeftButton);
    QTest::mouseClick(undo, Qt::LeftButton);
    // A new stroke after an undo drops the redo step.
    liquify_drag(preview, b_from, b_to);
    CHECK(!redo->isEnabled());
    QTest::mouseClick(undo, Qt::LeftButton);
  }), only_a));

  // A Freeze stroke is a step of its own.
  CHECK(liquify_meshes_equal(drive_liquify([&](QDialog& dialog, QWidget& preview) {
    liquify_drag(preview, a_from, a_to);
    QTest::mouseClick(liquify_tool_button(dialog, "liquifyFreezeTool"), Qt::LeftButton);
    liquify_drag(preview, QPointF(60.0, 60.0), QPointF(62.0, 60.0));
    QTest::mouseClick(liquify_tool_button(dialog, "liquifyWarpTool"), Qt::LeftButton);
    liquify_drag(preview, b_from, b_to);
    liquify_key(preview, Qt::Key_Z, Qt::ControlModifier);
    liquify_key(preview, Qt::Key_Z, Qt::ControlModifier);
  }), only_a));

  // 105 strokes: the oldest five fall out of the 100-step history.
  patchy::LiquifyMesh first_five(160, 120);
  const auto click_point = [](int index) { return QPointF(20.0 + (index % 12) * 10.0, 20.0 + (index / 12) * 9.0); };
  for (int index = 0; index < 5; ++index) {
    expect_drag(first_five, click_point(index), click_point(index) + QPointF(3.0, 1.0));
  }
  CHECK(liquify_meshes_equal(drive_liquify([&](QDialog& dialog, QWidget& preview) {
    for (int index = 0; index < 105; ++index) {
      liquify_drag(preview, click_point(index), click_point(index) + QPointF(3.0, 1.0));
    }
    for (int undo = 0; undo < 110; ++undo) {
      liquify_key(preview, Qt::Key_Z, Qt::ControlModifier);
    }
    CHECK(!liquify_push_button(dialog, "liquifyUndoButton")->isEnabled());
  }), first_five));
}

// Single letters choose the tools (shown in each tooltip) and [ ] change the brush size,
// Shift for a larger step; none of these keys, nor Ctrl+Z, reaches the main window while
// the dialog is open.
void ui_liquify_keys_choose_tools_and_size_without_reaching_the_main_window() {
  patchy::Document built(120, 90, patchy::PixelFormat::rgba8());
  patchy::Layer layer(built.allocate_layer_id(), "Liquify Keys", liquify_test_source(120, 90));
  const auto layer_id = layer.id();
  built.add_layer(std::move(layer));
  built.set_active_layer(layer_id);
  patchy::ui::MainWindow window;
  window.add_document_session(std::move(built), QStringLiteral("Liquify Keys"));
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* brush_size = window.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin"));
  CHECK(brush_size != nullptr);
  const auto tool_before = canvas->tool();
  const auto brush_size_before = brush_size->value();
  const auto undo_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  bool ran = false;
  std::exception_ptr driver_error;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("liquifyDialog"));
    try {
      CHECK(dialog != nullptr);
      auto* preview = dialog->findChild<QWidget*>(QStringLiteral("liquifyPreview"));
      auto* size = dialog->findChild<QSpinBox*>(QStringLiteral("liquifySizeSpin"));
      CHECK(preview != nullptr && size != nullptr);
      dialog->activateWindow();
      preview->setFocus();
      QApplication::processEvents();
      const std::array<std::pair<Qt::Key, const char*>, 8> letters{{
          {Qt::Key_R, "liquifyReconstructTool"},
          {Qt::Key_E, "liquifySmoothTool"},
          {Qt::Key_C, "liquifyTwirlTool"},
          {Qt::Key_S, "liquifyPuckerTool"},
          {Qt::Key_B, "liquifyBloatTool"},
          {Qt::Key_F, "liquifyFreezeTool"},
          {Qt::Key_D, "liquifyThawTool"},
          {Qt::Key_W, "liquifyWarpTool"},
      }};
      for (const auto& [key, name] : letters) {
        auto* button = liquify_tool_button(*dialog, name);
        const auto letter = QKeySequence(key).toString(QKeySequence::NativeText);
        CHECK(button->toolTip().contains(QStringLiteral("(%1)").arg(letter)));
        liquify_key(*preview, key);
        CHECK(button->isChecked());
        CHECK(canvas->tool() == tool_before);
      }
      const int start = size->value();
      liquify_key(*preview, Qt::Key_BracketRight);
      CHECK(size->value() == start + patchy::ui::proportional_brush_step(start, 1, false));
      const int grown = size->value();
      liquify_key(*preview, Qt::Key_BracketLeft);
      CHECK(size->value() == grown - patchy::ui::proportional_brush_step(grown, -1, false));
      const int base = size->value();
      liquify_key(*preview, Qt::Key_BracketRight, Qt::ShiftModifier);
      CHECK(size->value() == base + patchy::ui::proportional_brush_step(base, 1, true));
      size->setValue(2000);
      liquify_key(*preview, Qt::Key_BracketRight, Qt::ShiftModifier);
      CHECK(size->value() == 2000);
      size->setValue(5);
      liquify_key(*preview, Qt::Key_BracketLeft);
      CHECK(size->value() == 5);
      CHECK(brush_size->value() == brush_size_before);
      liquify_drag(*preview, QPointF(30.0, 30.0), QPointF(50.0, 40.0));
      liquify_key(*preview, Qt::Key_Z, Qt::ControlModifier);
      CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before);
      ran = true;
      dialog->reject();
    } catch (...) {
      driver_error = std::current_exception();
      if (dialog != nullptr) {
        dialog->reject();
      }
    }
  });
  require_action(window, "filterLiquifyAction")->trigger();
  QApplication::processEvents();
  if (driver_error) {
    std::rethrow_exception(driver_error);
  }
  CHECK(ran);
  CHECK(canvas->tool() == tool_before);
  CHECK(brush_size->value() == brush_size_before);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before);
}

// Twirl, Pucker, Bloat and Reconstruct keep working while the button is held still, at
// the Rate; Warp does nothing without motion. With Stylus Pressure on, a pen's pressure
// scales the Pressure setting.
void ui_liquify_rate_and_pen_pressure() {
  const QPointF center(80.0, 60.0);
  patchy::LiquifyMesh one_dab(160, 120);
  one_dab.apply_stroke(patchy::LiquifyTool::TwirlClockwise, center.x(), center.y(), center.x(), center.y(), 50.0,
                       50.0, 50.0);
  const auto magnitude = [](const patchy::LiquifyMesh& mesh) {
    const auto [dx, dy] = mesh.displacement_at(95.0, 60.0);
    return std::hypot(dx, dy);
  };
  const auto held = drive_liquify([&](QDialog& dialog, QWidget& preview) {
    auto* rate = dialog.findChild<QSpinBox*>(QStringLiteral("liquifyRateSpin"));
    CHECK(rate != nullptr && rate->minimum() == 1 && rate->maximum() == 100 && rate->value() == 80);
    QTest::mouseClick(liquify_tool_button(dialog, "liquifyTwirlTool"), Qt::LeftButton);
    send_mouse(preview, QEvent::MouseButtonPress, liquify_widget_point(center), Qt::LeftButton, Qt::LeftButton);
    QTest::qWait(300);
    send_mouse(preview, QEvent::MouseButtonRelease, liquify_widget_point(center), Qt::LeftButton, Qt::NoButton);
  });
  CHECK(held.has_value() && magnitude(*held) > 3.0 * magnitude(one_dab));
  const auto still_warp = drive_liquify([&](QDialog&, QWidget& preview) {
    send_mouse(preview, QEvent::MouseButtonPress, liquify_widget_point(center), Qt::LeftButton, Qt::LeftButton);
    QTest::qWait(150);
    send_mouse(preview, QEvent::MouseButtonRelease, liquify_widget_point(center), Qt::LeftButton, Qt::NoButton);
  });
  CHECK(still_warp.has_value() && still_warp->is_identity());

  const QPointF from(40.0, 60.0), to(70.0, 62.0);
  const auto pen_drag = [&](QWidget& preview) {
    send_tablet(preview, QEvent::TabletPress, liquify_widget_point(from), 0.25);
    send_mouse(preview, QEvent::MouseButtonPress, liquify_widget_point(from), Qt::LeftButton, Qt::LeftButton);
    send_tablet(preview, QEvent::TabletMove, liquify_widget_point(to), 0.25);
    send_mouse(preview, QEvent::MouseMove, liquify_widget_point(to), Qt::NoButton, Qt::LeftButton);
    send_tablet(preview, QEvent::TabletRelease, liquify_widget_point(to), 0.0, Qt::LeftButton, Qt::NoButton);
    send_mouse(preview, QEvent::MouseButtonRelease, liquify_widget_point(to), Qt::LeftButton, Qt::NoButton);
  };
  patchy::LiquifyMesh light(160, 120);
  expect_drag(light, from, to, patchy::LiquifyTool::ForwardWarp, 12.5);
  CHECK(liquify_meshes_equal(drive_liquify([&](QDialog& dialog, QWidget& preview) {
    auto* stylus = dialog.findChild<QCheckBox*>(QStringLiteral("liquifyStylusPressureCheck"));
    CHECK(stylus != nullptr && stylus->isChecked());
    pen_drag(preview);
  }), light));
  patchy::LiquifyMesh full(160, 120);
  expect_drag(full, from, to);
  CHECK(liquify_meshes_equal(drive_liquify([&](QDialog& dialog, QWidget& preview) {
    dialog.findChild<QCheckBox*>(QStringLiteral("liquifyStylusPressureCheck"))->setChecked(false);
    pen_drag(preview);
  }), full));
}

QImage liquify_preview_grab(QWidget& preview) {
  QEvent leave(QEvent::Leave);
  QApplication::sendEvent(&preview, &leave);
  QApplication::processEvents();
  return preview.grab().toImage();
}

// Show Mesh draws the deformed grid over the preview; unchecking Preview (or pressing P)
// shows the original; Reconstruct... moves the whole field back by an amount, as one
// history step, and its Cancel changes nothing.
void ui_liquify_mesh_preview_toggle_and_reconstruct_amount() {
  const QPointF from(40.0, 60.0), to(80.0, 60.0);
  patchy::LiquifyMesh expected(160, 120);
  expect_drag(expected, from, to);
  const auto run_reconstruct = [](QDialog& dialog, int amount, bool accept) {
    QTimer::singleShot(0, [amount, accept] {
      auto* reconstruct = find_top_level_dialog(QStringLiteral("liquifyReconstructDialog"));
      if (reconstruct == nullptr) {
        patchy::test::report_failure("no Reconstruct dialog");
        return;
      }
      auto* spin = reconstruct->findChild<QSpinBox*>(QStringLiteral("liquifyReconstructAmountSpin"));
      if (spin == nullptr || spin->minimum() != 0 || spin->maximum() != 100) {
        patchy::test::report_failure("no Reconstruct amount field");
      } else {
        spin->setValue(amount);
      }
      if (accept) {
        reconstruct->accept();
      } else {
        reconstruct->reject();
      }
    });
    QTest::mouseClick(liquify_push_button(dialog, "liquifyReconstructButton"), Qt::LeftButton);
    QApplication::processEvents();
  };

  auto halfway = expected;
  halfway.reconstruct(50.0);
  CHECK(liquify_meshes_equal(drive_liquify([&](QDialog& dialog, QWidget& preview) {
    auto* show_mesh = dialog.findChild<QCheckBox*>(QStringLiteral("liquifyShowMeshCheck"));
    auto* show_preview = dialog.findChild<QCheckBox*>(QStringLiteral("liquifyPreviewCheck"));
    CHECK(show_mesh != nullptr && show_preview != nullptr);
    CHECK(!show_mesh->isChecked() && show_preview->isChecked());
    const auto original = liquify_preview_grab(preview);
    liquify_drag(preview, from, to);
    const auto warped = liquify_preview_grab(preview);
    CHECK(warped != original);
    show_preview->setChecked(false);
    CHECK(liquify_preview_grab(preview) == original);
    liquify_key(preview, Qt::Key_P);
    CHECK(show_preview->isChecked());
    CHECK(liquify_preview_grab(preview) == warped);
    show_mesh->setChecked(true);
    const auto meshed = liquify_preview_grab(preview);
    CHECK(meshed != warped);
    show_mesh->setChecked(false);
    CHECK(liquify_preview_grab(preview) == warped);
    run_reconstruct(dialog, 30, false);
    CHECK(liquify_preview_grab(preview) == warped);
    run_reconstruct(dialog, 50, true);
  }), halfway));

  // Reconstruct is one step: Ctrl+Z brings the stroke back.
  CHECK(liquify_meshes_equal(drive_liquify([&](QDialog& dialog, QWidget& preview) {
    liquify_drag(preview, from, to);
    run_reconstruct(dialog, 100, true);
    liquify_key(preview, Qt::Key_Z, Qt::ControlModifier);
  }), expected));
}

// A smooth sunset gradient with a pale disc, so the shot's mesh and warp read clearly.
patchy::PixelBuffer liquify_shot_source(int width, int height) {
  patchy::PixelBuffer source(width, height, patchy::PixelFormat::rgba8());
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      auto* pixel = source.pixel(x, y);
      const double u = static_cast<double>(x) / std::max(1, width - 1);
      const double v = static_cast<double>(y) / std::max(1, height - 1);
      const double disc = std::hypot(u - 0.45, (v - 0.5) * 0.75) < 0.16 ? 1.0 : 0.0;
      pixel[0] = static_cast<std::uint8_t>(std::lround(60 + 170 * v + 25 * disc));
      pixel[1] = static_cast<std::uint8_t>(std::lround(50 + 80 * u * (1.0 - v) + 110 * disc));
      pixel[2] = static_cast<std::uint8_t>(std::lround(170 - 110 * v + 60 * disc));
      pixel[3] = 255;
    }
  }
  return source;
}

// Issue 296 shots: the workspace with its new controls (Undo/Redo, Rate, Stylus Pressure,
// Show Mesh, Preview, Reconstruct...) after a warp and a frozen area, in Dark and Light.
void ui_liquify_dialog_captures_in_both_schemes() {
  for (const auto scheme : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer restorer(scheme);
    const auto name = std::string(scheme == patchy::ui::ColorSchemePreference::Dark ? "liquify/after-dialog-dark"
                                                                                        : "liquify/after-dialog-light");
    (void)drive_liquify(
        [&](QDialog& dialog, QWidget& preview) {
          const auto proxy = [&preview](double x, double y) {
            return QPoint(static_cast<int>(preview.width() * x), static_cast<int>(preview.height() * y));
          };
          QTest::mouseClick(liquify_tool_button(dialog, "liquifyFreezeTool"), Qt::LeftButton);
          drag(preview, proxy(0.62, 0.30), proxy(0.66, 0.36));
          QTest::mouseClick(liquify_tool_button(dialog, "liquifyWarpTool"), Qt::LeftButton);
          drag(preview, proxy(0.35, 0.55), proxy(0.55, 0.45));
          dialog.findChild<QCheckBox*>(QStringLiteral("liquifyShowMeshCheck"))->setChecked(true);
          send_mouse(preview, QEvent::MouseMove, proxy(0.45, 0.62), Qt::NoButton, Qt::NoButton);
          QApplication::processEvents();
          std::filesystem::create_directories("test-artifacts/liquify");
          save_widget_artifact(name, dialog);
          dialog.reject();
        },
        /*fixed_preview=*/false, liquify_shot_source(640, 480));
  }
}

// A layer's transparent pixels show the transparency checkerboard in the preview, not the
// workspace's dark backdrop, which stays only around the image (Seth, October 28, 2026).
void ui_liquify_preview_shows_transparency_checkerboard() {
  patchy::PixelBuffer source(64, 48, patchy::PixelFormat::rgba8());
  for (int y = 10; y < 30; ++y) {
    for (int x = 20; x < 40; ++x) {
      auto* pixel = source.pixel(x, y);
      pixel[0] = 200;
      pixel[1] = 30;
      pixel[2] = 30;
      pixel[3] = 255;
    }
  }
  QImage shot;
  (void)drive_liquify(
      [&](QDialog& dialog, QWidget& preview) {
        QApplication::processEvents();
        shot = preview.grab().toImage();
        dialog.reject();
      },
      /*fixed_preview=*/true, source);
  CHECK(!shot.isNull());
  if (shot.isNull()) {
    return;
  }
  std::filesystem::create_directories("test-artifacts/liquify");
  save_image_artifact("liquify/transparency-preview", shot);
  // At 1:1 the image starts 12 px in (liquify_widget_point); squares are 8 px.
  const auto at = [&shot](int x, int y) { return shot.pixelColor(QPoint(x, y) * shot.devicePixelRatio()); };
  const auto is_checker_gray = [](const QColor& color) {
    return color.red() == color.green() && color.green() == color.blue() &&
           (color.red() == 188 || color.red() == 236);
  };
  const auto first = at(12 + 3, 12 + 3);
  const auto next = at(12 + 8 + 3, 12 + 3);
  CHECK(is_checker_gray(first));
  CHECK(is_checker_gray(next));
  CHECK(first != next);
  CHECK(is_checker_gray(at(12 + 60, 12 + 44)));
  CHECK(at(12 + 30, 12 + 20) == QColor(200, 30, 30));
  CHECK(at(4, 4) == patchy::ui::theme().canvas_backdrop);
}

// Dialog previews (the Filter Gallery, filter dialogs, Develop Raw) show transparency with
// the canvas's light checkerboard in both schemes; Dark once drew darkened squares.
void ui_dialog_preview_checkerboard_matches_the_canvas_in_both_schemes() {
  for (const auto scheme : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer restorer(scheme);
    patchy::ui::ZoomableImagePreview preview;
    preview.resize(200, 160);
    QImage clear(96, 64, QImage::Format_ARGB32_Premultiplied);
    clear.fill(Qt::transparent);
    {
      QPainter ink(&clear);
      ink.fillRect(QRect(4, 8, 20, 48), Qt::black);  // a stroke, clear of the sampled center
    }
    preview.set_image(clear);
    preview.show();
    QApplication::processEvents();
    const auto shot = preview.grab().toImage();
    std::filesystem::create_directories("test-artifacts/liquify");
    save_image_artifact(scheme == patchy::ui::ColorSchemePreference::Dark ? "liquify/dialog-preview-dark"
                                                                          : "liquify/dialog-preview-light",
                        shot);
    const auto center = QPoint(shot.width() / 2, shot.height() / 2);
    std::set<QRgb> squares;
    for (int dx = -12; dx <= 12; dx += 12) {
      const auto color = shot.pixelColor(center + QPoint(dx, 0));
      CHECK(color.red() == color.green() && color.green() == color.blue());
      CHECK(color.red() == 188 || color.red() == 236);
      squares.insert(color.rgb());
    }
    CHECK(squares.size() == 2U);
    CHECK(shot.pixelColor(2, 2) == patchy::ui::theme().canvas_backdrop);
  }
}

void ui_liquify_preview_selection_preserves_sample_mapping() {
  for (const auto size : {QSize(83, 61), QSize(1001, 773), QSize(1024, 1), QSize(1, 1024)}) {
    auto proxy_size = size;
    if (std::max(size.width(), size.height()) > 720) {
      proxy_size.scale(QSize(720, 720), Qt::KeepAspectRatio);
      proxy_size = proxy_size.expandedTo(QSize(1, 1));
    }
    patchy::PixelBuffer source(size.width(), size.height(), patchy::PixelFormat::rgba8());
    for (int y = 0; y < source.height(); ++y) {
      for (int x = 0; x < source.width(); ++x) {
        auto* pixel = source.pixel(x, y);
        pixel[0] = static_cast<std::uint8_t>((x * 17 + y * 3) % 256);
        pixel[1] = static_cast<std::uint8_t>((x * 5 + y * 19) % 256);
        pixel[2] = 90;
        pixel[3] = 255;
      }
    }
    const patchy::Rect bounds{-13, 7, size.width(), size.height()};
    const QRegion selection = QRegion(QRect(bounds.x - 5, bounds.y - 3,
                                            5 + std::max(1, size.width() / 2),
                                            3 + std::max(1, size.height() / 2)))
                                  .united(QRect(bounds.x + size.width() - 1,
                                                bounds.y + size.height() - 1, 3, 3));
    QImage actual;
    QSize widget_size;
    std::exception_ptr driver_error;
    QTimer::singleShot(0, [&] {
      auto* dialog = find_top_level_dialog(QStringLiteral("liquifyDialog"));
      try {
        CHECK(dialog != nullptr);
        auto* preview = dialog->findChild<QWidget*>(QStringLiteral("liquifyPreview"));
        auto* bloat = dialog->findChild<QToolButton*>(QStringLiteral("liquifyBloatTool"));
        CHECK(preview != nullptr && bloat != nullptr);
        // Inspect proxy pixels at 1:1. QWidget's backing store and an ARGB32
        // QImage take different rounding paths when resampling for display.
        preview->setFixedSize(proxy_size + QSize(24, 24));
        QTest::mouseClick(bloat, Qt::LeftButton);
        QTest::mouseClick(preview, Qt::LeftButton, Qt::NoModifier,
                          QPoint(12 + proxy_size.width() / 3, 12 + proxy_size.height() / 3));
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(preview, &leave);
        widget_size = preview->size();
        actual = preview->grab().toImage().convertToFormat(QImage::Format_ARGB32);
        dialog->accept();
      } catch (...) {
        driver_error = std::current_exception();
        if (dialog != nullptr) {
          dialog->reject();
        }
      }
    });
    const auto mesh = patchy::ui::request_liquify(nullptr, source, bounds, selection);
    if (driver_error) {
      std::rethrow_exception(driver_error);
    }
    CHECK(mesh.has_value());
    CHECK(!actual.isNull());
    const auto original = patchy::ui::qimage_from_pixel_buffer(source).scaled(
        proxy_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const auto rendered = mesh->render(patchy::ui::pixels_from_image_rgba(original));
    CHECK(rendered.has_value());
    auto expected_preview = patchy::ui::qimage_from_pixel_buffer(*rendered);
    // Independent scalar oracle for the historical document-coordinate samples.
    for (int y = 0; y < proxy_size.height(); ++y) {
      for (int x = 0; x < proxy_size.width(); ++x) {
        const QPoint point(bounds.x + x * (size.width() - 1) / std::max(1, proxy_size.width() - 1),
                           bounds.y + y * (size.height() - 1) / std::max(1, proxy_size.height() - 1));
        if (!selection.contains(point)) {
          expected_preview.setPixelColor(x, y, original.pixelColor(x, y));
        }
      }
    }
    QImage expected(actual.size(), actual.format());
    expected.setDevicePixelRatio(actual.devicePixelRatio());
    expected.fill(patchy::ui::theme().canvas_backdrop);
    const double scale = std::min((widget_size.width() - 24.0) / proxy_size.width(),
                                  (widget_size.height() - 24.0) / proxy_size.height());
    const QSizeF drawn_size(proxy_size.width() * scale, proxy_size.height() * scale);
    const QRectF target(QPointF((widget_size.width() - drawn_size.width()) * 0.5,
                                (widget_size.height() - drawn_size.height()) * 0.5), drawn_size);
    QPainter painter(&expected);
    QPixmap tile(16, 16);
    tile.fill(patchy::ui::checkerboard_light_square());
    {
      QPainter tile_painter(&tile);
      tile_painter.fillRect(0, 0, 8, 8, patchy::ui::checkerboard_dark_square());
      tile_painter.fillRect(8, 8, 8, 8, patchy::ui::checkerboard_dark_square());
    }
    QBrush checkerboard(tile);
    checkerboard.setTransform(QTransform::fromTranslate(target.x(), target.y()));
    painter.fillRect(target, checkerboard);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, target.width() < proxy_size.width());
    painter.drawImage(target, expected_preview);
    painter.setPen(QPen(patchy::ui::theme().canvas_document_border, 1));
    painter.drawRect(target.adjusted(0, 0, -1, -1));
    painter.end();
    if (actual != expected) {
      save_image_artifact("ui_liquify_selection_actual", actual);
      save_image_artifact("ui_liquify_selection_expected", expected);
      int differences = 0;
      for (int y = 0; y < actual.height(); ++y) {
        for (int x = 0; x < actual.width(); ++x) {
          if (actual.pixel(x, y) != expected.pixel(x, y)) {
            if (differences == 0) {
              std::cout << "  Liquify " << size.width() << 'x' << size.height()
                        << " first mismatch at " << x << ',' << y << ": "
                        << actual.pixelColor(x, y).name().toStdString() << " vs "
                        << expected.pixelColor(x, y).name().toStdString() << '\n';
            }
            ++differences;
          }
        }
      }
      std::cout << "  Liquify preview pixel differences: " << differences << '\n';
      CHECK(differences == 0);
    }
  }
}

void check_liquify_action_applies_selection_as_one_undo_step(bool fragmented) {
  const int width = fragmented ? 960 : 80;
  const int height = fragmented ? 640 : 60;
  patchy::Document built(width + 20, height + 20, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer pixels(width, height, patchy::PixelFormat::rgba8());
  for (int y = 0; y < pixels.height(); ++y) {
    for (int x = 0; x < pixels.width(); ++x) {
      auto* pixel = pixels.pixel(x, y);
      pixel[0] = static_cast<std::uint8_t>((x * 17 + y * 3) % 256);
      pixel[1] = static_cast<std::uint8_t>((x * 5 + y * 19) % 256);
      pixel[2] = static_cast<std::uint8_t>((x * 11 + y * 7) % 256);
      pixel[3] = 255;
    }
  }
  const auto original_pixels = pixels;
  patchy::Layer layer(built.allocate_layer_id(), "Liquify Subject",
                      std::move(pixels));
  const auto layer_id = layer.id();
  const patchy::Rect bounds{10, 8, width, height};
  layer.set_bounds(bounds);
  built.add_layer(std::move(layer));
  built.set_active_layer(layer_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(built), QStringLiteral("Liquify Apply"));
  show_window(window);
  auto* canvas = require_canvas(window);
  patchy::PixelBuffer selection(width + 20, height + 20, patchy::PixelFormat::gray8());
  selection.clear(0U);
  const QRect selected_rect = fragmented ? QRect(11, 9, width - 2, height - 2) : QRect(30, 23, 40, 30);
  for (int y = selected_rect.top(); y <= selected_rect.bottom(); ++y) {
    for (int x = selected_rect.left(); x <= selected_rect.right(); ++x) {
      if (!fragmented || (x % 2 == 0 && y % 2 == 0)) {
        selection.pixel(x, y)[0] = 255U;
      }
    }
  }
  canvas->replace_selection_from_grayscale(
      selection, QStringLiteral("Liquify selection"));
  QApplication::processEvents();
  const auto undo_before =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  bool accepted = false;
  std::exception_ptr driver_error;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("liquifyDialog"));
    try {
      CHECK(dialog != nullptr);
      auto* bloat = dialog->findChild<QToolButton*>(
          QStringLiteral("liquifyBloatTool"));
      auto* preview =
          dialog->findChild<QWidget*>(QStringLiteral("liquifyPreview"));
      CHECK(bloat != nullptr && preview != nullptr);
      QTest::mouseClick(bloat, Qt::LeftButton);
      QTest::mouseClick(preview, Qt::LeftButton, Qt::NoModifier,
                        preview->rect().center());
      accepted = true;
      dialog->accept();
    } catch (...) {
      driver_error = std::current_exception();
      if (dialog != nullptr) {
        dialog->reject();
      }
    }
  });
  QElapsedTimer timer;
  timer.start();
  require_action(window, "filterLiquifyAction")->trigger();
  QApplication::processEvents();
  if (driver_error) {
    std::rethrow_exception(driver_error);
  }
  CHECK(accepted);
  if (fragmented) {
    std::cout << "  fragmented selection Liquify open, preview and apply " << timer.elapsed() << " ms\n";
    CHECK(timer.elapsed() < 120000);
  }

  const auto* applied = std::as_const(
      patchy::ui::MainWindowTestAccess::document(window)).find_layer(layer_id);
  CHECK(applied != nullptr);
  CHECK(applied->bounds().x == bounds.x && applied->bounds().y == bounds.y &&
        applied->bounds().width == bounds.width &&
        applied->bounds().height == bounds.height);
  bool changed_inside = false;
  for (int y = 0; y < original_pixels.height(); ++y) {
    for (int x = 0; x < original_pixels.width(); ++x) {
      const bool equal = std::equal(original_pixels.pixel(x, y),
                                    original_pixels.pixel(x, y) + 4,
                                    applied->pixels().pixel(x, y));
      if (*std::as_const(selection).pixel(bounds.x + x, bounds.y + y) != 0U) {
        changed_inside = changed_inside || !equal;
      } else {
        CHECK(equal);
      }
    }
  }
  CHECK(changed_inside);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 1U);
  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  const auto* restored = std::as_const(
      patchy::ui::MainWindowTestAccess::document(window)).find_layer(layer_id);
  CHECK(restored != nullptr);
  CHECK(std::equal(restored->pixels().data().begin(),
                   restored->pixels().data().end(),
                   original_pixels.data().begin()));

  require_action(window, "filterConvertForSmartFiltersAction")->trigger();
  QApplication::processEvents();
  const auto* smart_layer = std::as_const(
      patchy::ui::MainWindowTestAccess::document(window)).find_layer(layer_id);
  CHECK(smart_layer != nullptr && patchy::layer_is_smart_object(*smart_layer));
  const auto smart_undo_depth =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  require_action(window, "filterLiquifyAction")->trigger();
  QApplication::processEvents();
  CHECK(window.statusBar()->currentMessage() ==
        QStringLiteral("Rasterize the Smart Object before using Liquify"));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        smart_undo_depth);
}

void ui_liquify_action_applies_selection_as_one_undo_step() {
  check_liquify_action_applies_selection_as_one_undo_step(false);
}

void ui_liquify_fragmented_selection_preview_and_apply() {
  check_liquify_action_applies_selection_as_one_undo_step(true);
}

void ui_filter_progress_callback_can_cancel_heavy_filter() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  const auto pixels =
      solid_pixels(96, 96, patchy::PixelFormat::rgb8(), QColor(120, 70, 210));
  bool saw_progress = false;
  bool saw_blurring = false;
  patchy::ui::FilterProgress progress{[&](int completed, int total, patchy::FilterProgressStage stage) {
    saw_progress = true;
    saw_blurring = saw_blurring || stage == patchy::FilterProgressStage::Blurring;
    CHECK(total == 96);
    return completed < 4;
  }};

  auto invocation = filter_invocation(registry, "patchy.filters.box_blur");
  set_filter_integer(invocation, "radius", 12);

  bool cancelled = false;
  try {
    (void)patchy::ui::build_filter_preview_pixels(
        pixels, QRegion(), patchy::Rect{0, 0, 96, 96}, registry,
        patchy::ui::FilterPreviewSettings{true, std::move(invocation)}, &progress);
  } catch (const patchy::ui::FilterCancelled&) {
    cancelled = true;
  }

  CHECK(saw_progress);
  CHECK(saw_blurring);
  CHECK(cancelled);
}

void ui_filter_progress_callback_can_cancel_simple_filter() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  const auto pixels = solid_pixels(32, 32, patchy::PixelFormat::rgb8(), QColor(120, 70, 210));
  bool saw_progress = false;
  bool saw_filtering_detail = false;
  patchy::ui::FilterProgress progress{[&](int completed, int total, patchy::FilterProgressStage stage) {
    saw_progress = true;
    saw_filtering_detail = saw_filtering_detail || stage == patchy::FilterProgressStage::Filtering;
    CHECK(total > 0);
    return completed < 2;
  }};

  auto invocation = filter_invocation(registry, "patchy.filters.sepia");

  bool cancelled = false;
  try {
    (void)patchy::ui::build_filter_preview_pixels(
        pixels, QRegion(), patchy::Rect{0, 0, 32, 32}, registry,
        patchy::ui::FilterPreviewSettings{true, std::move(invocation)}, &progress);
  } catch (const patchy::ui::FilterCancelled&) {
    cancelled = true;
  }

  CHECK(saw_progress);
  CHECK(saw_filtering_detail);
  CHECK(cancelled);
}

void ui_all_builtin_filters_report_progress() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);

  for (const auto& filter : registry.filters()) {
    auto invocation = registry.default_invocation(filter.identifier);

    const auto pixels = solid_pixels(24, 24, patchy::PixelFormat::rgb8(), QColor(120, 70, 210));
    bool saw_progress = false;
    patchy::ui::FilterProgress progress{[&](int completed, int total, patchy::FilterProgressStage) {
      saw_progress = true;
      CHECK(total > 0);
      CHECK(completed >= 0);
      return false;
    }};

    bool cancelled = false;
    try {
      (void)patchy::ui::build_filter_preview_pixels(
          pixels, QRegion(), patchy::Rect{0, 0, 24, 24}, registry,
          patchy::ui::FilterPreviewSettings{true, std::move(invocation)}, &progress);
    } catch (const patchy::ui::FilterCancelled&) {
      cancelled = true;
    }

    CHECK(saw_progress);
    CHECK(cancelled);
  }
}

void ui_all_builtin_filters_complete_progress_monotonically() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  const std::array stages = {patchy::FilterProgressStage::Filtering,       patchy::FilterProgressStage::Blurring,
                             patchy::FilterProgressStage::Sharpening,      patchy::FilterProgressStage::DetectingEdges,
                             patchy::FilterProgressStage::Distorting,      patchy::FilterProgressStage::Twisting,
                             patchy::FilterProgressStage::Embossing,       patchy::FilterProgressStage::GeneratingClouds,
                             patchy::FilterProgressStage::Pixelating,      patchy::FilterProgressStage::RenderingHalftone,
                             patchy::FilterProgressStage::AddingGrain,     patchy::FilterProgressStage::ApplyingVignette};
  for (const auto stage : stages) {
    CHECK(!patchy::ui::filter_progress_stage_text(stage).isEmpty());
  }

  for (const auto& filter : registry.filters()) {
    const auto pixels = solid_pixels(24, 24, patchy::PixelFormat::rgb8(), QColor(120, 70, 210));
    auto invocation = registry.default_invocation(filter.identifier);
    int last_completed = -1;
    int expected_total = -1;
    int callback_count = 0;
    patchy::ui::FilterProgress progress{[&](int completed, int total, patchy::FilterProgressStage stage) {
      CHECK(total > 0);
      CHECK(completed >= 0);
      CHECK(completed <= total);
      CHECK(completed >= last_completed);
      CHECK(expected_total < 0 || total == expected_total);
      CHECK(!patchy::ui::filter_progress_stage_text(stage).isEmpty());
      last_completed = completed;
      expected_total = total;
      ++callback_count;
      return true;
    }};
    const auto result = registry.render(invocation, pixels, patchy::Rect{0, 0, 24, 24}, false, &progress);
    CHECK(!result.pixels.empty());
    CHECK(callback_count > 0);
    CHECK(last_completed == expected_total);
  }
}

void ui_adjustment_pixel_progress_callback_can_cancel() {
  auto pixels = solid_pixels(32, 32, patchy::PixelFormat::rgb8(), QColor(120, 70, 210));
  bool saw_progress = false;
  patchy::ui::FilterProgress progress{[&](int completed, int total, patchy::FilterProgressStage stage) {
    saw_progress = true;
    CHECK(total > 0);
    CHECK(completed >= 0);
    CHECK(stage == patchy::FilterProgressStage::Filtering);
    return false;
  }};

  bool cancelled = false;
  try {
    patchy::ui::apply_hue_saturation_to_pixels(pixels, patchy::Rect{0, 0, 32, 32}, QRegion(),
                                               patchy::ui::HueSaturationSettings{120, 0, 0}, &progress);
  } catch (const patchy::ui::FilterCancelled&) {
    cancelled = true;
  }

  CHECK(saw_progress);
  CHECK(cancelled);
}

void ui_filter_settings_dialog_shows_before_initial_preview() {
  bool preview_ran = false;
  bool preview_saw_visible_dialog = false;
  const patchy::ui::FilterDialogSpec spec{QStringLiteral("patchy.filters.sepia"), QStringLiteral("Deferred Preview"),
                                          {{QStringLiteral("Amount"), QStringLiteral("filterAmount"), 0, 100, 100,
                                            QStringLiteral("%"), "amount", patchy::FilterParameterKind::Integer,
                                            std::int64_t{100}}}};

  const auto settings = patchy::ui::request_filter_settings(nullptr, spec, [&](patchy::ui::FilterPreviewSettings) {
    preview_ran = true;
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyFilterDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      CHECK(dialog != nullptr);
      preview_saw_visible_dialog = dialog->isVisible();
      // Without a preview source the dialog stays lightweight: no in-dialog
      // proxy preview surface is created.
      CHECK(dialog->findChild<QWidget*>(QStringLiteral("filterDialogPreview")) ==
            nullptr);
      dialog->accept();
      return;
    }
  });

  CHECK(preview_ran);
  CHECK(preview_saw_visible_dialog);
  CHECK(settings.has_value());
  CHECK(settings->filter_id == "patchy.filters.sepia");
  CHECK(filter_integer(*settings, "amount") == 100);
}

void ui_filter_settings_dialog_coalesces_rapid_slider_preview_callbacks() {
  int preview_calls = 0;
  int latest_preview_value = -1;
  const patchy::ui::FilterDialogSpec spec{QStringLiteral("patchy.filters.sepia"), QStringLiteral("Coalesced Preview"),
                                          {{QStringLiteral("Amount"), QStringLiteral("filterAmount"), 0, 100, 0,
                                            QStringLiteral("%"), "amount", patchy::FilterParameterKind::Integer,
                                            std::int64_t{0}}}};

  QTimer::singleShot(0, [] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* amount = dialog->findChild<QSpinBox*>(QStringLiteral("filterAmountSpin"));
    CHECK(amount != nullptr);
    for (int value = 1; value <= 24; ++value) {
      amount->setValue(value);
    }
    QTimer::singleShot(120, dialog, [dialog] { dialog->accept(); });
  });

  const auto settings = patchy::ui::request_filter_settings(
      nullptr, spec, [&](patchy::ui::FilterPreviewSettings preview) {
        ++preview_calls;
        latest_preview_value = filter_integer(preview.invocation, "amount");
      });

  CHECK(settings.has_value());
  CHECK(filter_integer(*settings, "amount") == 24);
  CHECK(latest_preview_value == 24);
  CHECK(preview_calls > 0);
  CHECK(preview_calls < 8);
}

void ui_filter_settings_dialog_delivers_latest_after_slow_preview_callback() {
  int preview_calls = 0;
  bool queued_changes = false;
  std::vector<int> preview_values;
  const patchy::ui::FilterDialogSpec spec{QStringLiteral("patchy.filters.sepia"), QStringLiteral("Latest Preview"),
                                          {{QStringLiteral("Amount"), QStringLiteral("filterAmount"), 0, 100, 0,
                                            QStringLiteral("%"), "amount", patchy::FilterParameterKind::Integer,
                                            std::int64_t{0}}}};

  const auto settings = patchy::ui::request_filter_settings(
      nullptr, spec, [&](patchy::ui::FilterPreviewSettings preview) {
        ++preview_calls;
        preview_values.push_back(filter_integer(preview.invocation, "amount"));
        if (queued_changes) {
          return;
        }
        queued_changes = true;
        QTimer::singleShot(0, [] {
          auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
          CHECK(dialog != nullptr);
          auto* amount = dialog->findChild<QSpinBox*>(QStringLiteral("filterAmountSpin"));
          CHECK(amount != nullptr);
          for (int value = 1; value <= 18; ++value) {
            amount->setValue(value);
          }
          QTimer::singleShot(120, dialog, [dialog] { dialog->accept(); });
        });
        QElapsedTimer slow_preview;
        slow_preview.start();
        while (slow_preview.elapsed() < 45) {
        }
      });

  CHECK(settings.has_value());
  CHECK(filter_integer(*settings, "amount") == 18);
  CHECK(queued_changes);
  CHECK(preview_calls >= 2);
  CHECK(preview_calls < 8);
  CHECK(!preview_values.empty());
  CHECK(preview_values.back() == 18);
}

void ui_filter_settings_dialog_reset_restores_named_defaults_and_colors() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  const auto* clouds = registry.find("patchy.filters.clouds");
  CHECK(clouds != nullptr);
  const auto spec = patchy::ui::filter_dialog_spec_for(*clouds);
  auto initial = registry.default_invocation(clouds->identifier, patchy::RgbColor{240, 30, 20},
                                             patchy::RgbColor{10, 40, 230});
  set_filter_integer(initial, "scale", 48);
  set_filter_integer(initial, "detail", 3);
  set_filter_integer(initial, "contrast", 65);
  set_filter_integer(initial, "seed", 77);

  int preview_calls = 0;
  patchy::ui::FilterPreviewSettings last_preview;
  QTimer::singleShot(0, [] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* scale = dialog->findChild<QSpinBox*>(QStringLiteral("filterScaleSpin"));
    auto* detail = dialog->findChild<QSpinBox*>(QStringLiteral("filterDetailSpin"));
    auto* contrast = dialog->findChild<QSpinBox*>(QStringLiteral("filterContrastSpin"));
    auto* seed = dialog->findChild<QSpinBox*>(QStringLiteral("filterSeedSpin"));
    auto* buttons = dialog->findChild<QDialogButtonBox*>();
    CHECK(scale != nullptr);
    CHECK(detail != nullptr);
    CHECK(contrast != nullptr);
    CHECK(seed != nullptr);
    CHECK(buttons != nullptr);
    CHECK(scale->value() == 48);
    CHECK(detail->value() == 3);
    CHECK(contrast->value() == 65);
    CHECK(seed->value() == 77);
    scale->setValue(220);
    detail->setValue(8);
    buttons->button(QDialogButtonBox::Reset)->click();
    QApplication::processEvents();
    CHECK(scale->value() == 96);
    CHECK(detail->value() == 6);
    CHECK(contrast->value() == 40);
    CHECK(seed->value() == 1);
    dialog->accept();
  });

  const auto settings = patchy::ui::request_filter_settings(
      nullptr, spec,
      [&](patchy::ui::FilterPreviewSettings preview) {
        ++preview_calls;
        last_preview = std::move(preview);
      },
      initial);
  CHECK(settings.has_value());
  CHECK(preview_calls > 0);
  CHECK(filter_integer(*settings, "scale") == 96);
  CHECK(filter_integer(*settings, "detail") == 6);
  CHECK(filter_integer(*settings, "contrast") == 40);
  CHECK(filter_integer(*settings, "seed") == 1);
  const patchy::RgbColor expected_foreground{240, 30, 20};
  const patchy::RgbColor expected_background{10, 40, 230};
  CHECK(filter_rgb_equal(settings->foreground, expected_foreground));
  CHECK(filter_rgb_equal(settings->background, expected_background));
  CHECK(filter_rgb_equal(last_preview.invocation.foreground, settings->foreground));
  CHECK(filter_rgb_equal(last_preview.invocation.background, settings->background));
}

void ui_filter_parameter_panel_round_trips_catalog_and_legacy_specs() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);

  // Catalog spec with companions: Wave carries the three wave presentation
  // roles, so the waveform companion appears and spins stay authoritative.
  const auto* wave = registry.find("patchy.filters.wave");
  CHECK(wave != nullptr);
  const auto wave_spec = patchy::ui::filter_dialog_spec_for(*wave);
  auto initial = registry.default_invocation(wave->identifier);
  set_filter_integer(initial, "amplitude", 20);

  patchy::ui::FilterParameterPanel panel;
  patchy::ui::FilterParameterPanelOptions options;
  options.build_companions = true;
  panel.rebuild(wave_spec, initial, options);

  std::vector<patchy::ui::FilterParameterPanel::ValueChanges> changes;
  panel.set_values_changed_callback(
      [&](const patchy::ui::FilterParameterPanel::ValueChanges& batch) {
        changes.push_back(batch);
      });

  auto* amplitude =
      panel.findChild<QSpinBox*>(QStringLiteral("filterAmplitudeSpin"));
  CHECK(amplitude != nullptr);
  CHECK(amplitude->value() == 20);
  CHECK(panel.findChild<QWidget*>(QStringLiteral("filterWaveformControl")) !=
        nullptr);
  CHECK(panel.findChild<QWidget*>(QStringLiteral("filterAngleDial")) ==
        nullptr);

  amplitude->setValue(24);
  CHECK(changes.size() == 1);
  CHECK(changes.back().size() == 1);
  CHECK(changes.back().front().first == "amplitude");

  auto read = initial;
  panel.read_into(read);
  CHECK(filter_integer(read, "amplitude") == 24);
  CHECK(filter_integer(read, "wavelength") == 48);
  CHECK(filter_integer(read, "phase") == 0);

  panel.reset_to_defaults();
  auto defaults_read = initial;
  panel.read_into(defaults_read);
  CHECK(filter_integer(defaults_read, "amplitude") == 12);
  CHECK(filter_integer(defaults_read, "wavelength") == 48);
  CHECK(filter_integer(defaults_read, "phase") == 0);

  // The angle dial companion appears for filters carrying the Angle role.
  const auto* motion = registry.find("patchy.filters.motion_blur");
  CHECK(motion != nullptr);
  panel.rebuild(patchy::ui::filter_dialog_spec_for(*motion),
                registry.default_invocation(motion->identifier), options);
  CHECK(panel.findChild<QWidget*>(QStringLiteral("filterAngleDial")) !=
        nullptr);
  CHECK(panel.findChild<QWidget*>(QStringLiteral("filterWaveformControl")) ==
        nullptr);

  // Legacy hand-built spec: the stable key derives from the control object
  // name and the reset value comes from the aggregate control.value.
  const patchy::ui::FilterDialogSpec legacy_spec{
      QStringLiteral("patchy.filters.sepia"),
      QStringLiteral("Legacy"),
      {{QStringLiteral("Amount"), QStringLiteral("filterAmount"), 0, 100, 80,
        QStringLiteral("%")},
       {QStringLiteral("Block Size"), QStringLiteral("filterBlockSize"), 1, 64,
        4, QString()}}};
  patchy::ui::FilterParameterPanel legacy_panel;
  legacy_panel.rebuild(legacy_spec, patchy::FilterInvocation{},
                       patchy::ui::FilterParameterPanelOptions{});
  auto* legacy_amount =
      legacy_panel.findChild<QSpinBox*>(QStringLiteral("filterAmountSpin"));
  CHECK(legacy_amount != nullptr);
  CHECK(legacy_amount->value() == 80);
  patchy::FilterInvocation legacy_read;
  legacy_panel.read_into(legacy_read);
  CHECK(filter_integer(legacy_read, "amount") == 80);
  CHECK(filter_integer(legacy_read, "block_size") == 4);
}

void ui_filter_settings_dialog_angle_dial_and_waveform_sync_numeric_controls() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);

  // Motion Blur: the direct dialog now carries the gallery's angle dial, and
  // dial gestures drive the numeric angle control (and the accepted result).
  {
    const auto* motion = registry.find("patchy.filters.motion_blur");
    CHECK(motion != nullptr);
    const auto spec = patchy::ui::filter_dialog_spec_for(*motion);
    bool drove_dialog = false;
    QTimer::singleShot(0, [&] {
      auto* dialog = qobject_cast<QDialog*>(
          find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
      CHECK(dialog != nullptr);
      auto* dial =
          dialog->findChild<QWidget*>(QStringLiteral("filterAngleDial"));
      auto* angle_spin =
          dialog->findChild<QSpinBox*>(QStringLiteral("filterAngleSpin"));
      CHECK(dial != nullptr && angle_spin != nullptr);
      CHECK(dial->property("filterAngleDegrees").toInt() == 0);
      angle_spin->setValue(-180);
      QApplication::processEvents();
      CHECK(dial->property("filterAngleDegrees").toInt() == -180);
      const QPoint dial_top(dial->width() / 2, 10);
      send_mouse(*dial, QEvent::MouseButtonPress, dial_top, Qt::LeftButton,
                 Qt::LeftButton);
      send_mouse(*dial, QEvent::MouseButtonRelease, dial_top, Qt::LeftButton,
                 Qt::NoButton);
      QApplication::processEvents();
      CHECK(angle_spin->value() >= 88 && angle_spin->value() <= 92);
      CHECK(dial->property("filterAngleDegrees").toInt() ==
            angle_spin->value());
      drove_dialog = true;
      dialog->accept();
    });
    const auto settings = patchy::ui::request_filter_settings(nullptr, spec, {});
    CHECK(drove_dialog);
    CHECK(settings.has_value());
    const auto angle = filter_integer(*settings, "angle");
    CHECK(angle >= 88 && angle <= 92);
  }

  // Wave: the waveform graph follows the three numeric controls, and a graph
  // drag/wheel drives them back (same gesture semantics as the gallery).
  {
    const auto* wave = registry.find("patchy.filters.wave");
    CHECK(wave != nullptr);
    const auto spec = patchy::ui::filter_dialog_spec_for(*wave);
    bool drove_dialog = false;
    QTimer::singleShot(0, [&] {
      auto* dialog = qobject_cast<QDialog*>(
          find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
      CHECK(dialog != nullptr);
      auto* waveform =
          dialog->findChild<QWidget*>(QStringLiteral("filterWaveformControl"));
      auto* amplitude =
          dialog->findChild<QSpinBox*>(QStringLiteral("filterAmplitudeSpin"));
      auto* wavelength =
          dialog->findChild<QSpinBox*>(QStringLiteral("filterWavelengthSpin"));
      auto* phase =
          dialog->findChild<QSpinBox*>(QStringLiteral("filterPhaseSpin"));
      CHECK(waveform != nullptr && amplitude != nullptr &&
            wavelength != nullptr && phase != nullptr);
      CHECK(waveform->property("filterWaveAmplitude").toInt() == 12);
      CHECK(waveform->property("filterWaveWavelength").toInt() == 48);
      CHECK(waveform->property("filterWavePhase").toInt() == 0);
      amplitude->setValue(20);
      QApplication::processEvents();
      CHECK(waveform->property("filterWaveAmplitude").toInt() == 20);
      const auto wave_center = waveform->rect().center();
      drag(*waveform, wave_center,
           wave_center + QPoint(waveform->width() / 4, -waveform->height() / 4));
      QApplication::processEvents();
      CHECK(amplitude->value() > 20);
      CHECK(phase->value() > 0);
      CHECK(waveform->property("filterWaveAmplitude").toInt() ==
            amplitude->value());
      CHECK(waveform->property("filterWavePhase").toInt() == phase->value());
      const auto wavelength_before = wavelength->value();
      send_wheel(*waveform, waveform->rect().center(), 120);
      QApplication::processEvents();
      CHECK(wavelength->value() == wavelength_before + 1);
      drove_dialog = true;
      dialog->accept();
    });
    const auto settings = patchy::ui::request_filter_settings(nullptr, spec, {});
    CHECK(drove_dialog);
    CHECK(settings.has_value());
    CHECK(filter_integer(*settings, "amplitude") > 20);
    CHECK(filter_integer(*settings, "phase") > 0);
  }
}

void ui_filter_settings_dialog_proxy_preview_renders_and_center_overlay_drag_syncs_spins() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  const auto* twirl = registry.find("patchy.filters.twirl");
  CHECK(twirl != nullptr);
  const auto spec = patchy::ui::filter_dialog_spec_for(*twirl);
  const auto pixels =
      solid_pixels(96, 64, patchy::PixelFormat::rgba8(), QColor(90, 140, 200));
  const patchy::Rect bounds{0, 0, 96, 64};
  const patchy::ui::FilterDialogPreviewSource source{&pixels, bounds, QRegion(),
                                                     &registry};

  bool drove_dialog = false;
  double dragged_center_x = 0.0;
  double dragged_center_y = 0.0;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* preview_widget =
        dialog->findChild<QWidget*>(QStringLiteral("filterDialogPreview"));
    auto* preview =
        dynamic_cast<patchy::ui::ZoomableImagePreview*>(preview_widget);
    auto* center_x =
        dialog->findChild<QDoubleSpinBox*>(QStringLiteral("filterCenterXSpin"));
    auto* center_y =
        dialog->findChild<QDoubleSpinBox*>(QStringLiteral("filterCenterYSpin"));
    CHECK(preview != nullptr && center_x != nullptr && center_y != nullptr);
    CHECK(process_events_until(
        [&] {
          return preview->property("filterDialogRenderedFilterId").toString() ==
                 QStringLiteral("patchy.filters.twirl");
        },
        5000));
    CHECK(process_events_until(
        [&] {
          return preview->property("filterSpatialOverlayVisible").toBool();
        },
        3000));
    CHECK(preview->property("filterSpatialRadiusVisible").toBool());
    CHECK(center_x->value() == 50.0 && center_y->value() == 50.0);

    const auto displayed_size =
        QSizeF(preview->image().width() * preview->zoom(),
               preview->image().height() * preview->zoom());
    const QRectF displayed(
        QPointF((preview->width() - displayed_size.width()) / 2.0,
                (preview->height() - displayed_size.height()) / 2.0),
        displayed_size);
    const QPointF overlay_center(
        displayed.left() +
            preview->property("filterCenterXNormalized").toDouble() *
                displayed.width(),
        displayed.top() +
            preview->property("filterCenterYNormalized").toDouble() *
                displayed.height());
    const auto moved_center =
        QPointF(displayed.left() + displayed.width() * 0.70,
                displayed.top() + displayed.height() * 0.30)
            .toPoint();
    send_mouse(*preview, QEvent::MouseButtonPress, overlay_center.toPoint(),
               Qt::LeftButton, Qt::LeftButton);
    send_mouse(*preview, QEvent::MouseMove, moved_center, Qt::NoButton,
               Qt::LeftButton);
    send_mouse(*preview, QEvent::MouseButtonRelease, moved_center,
               Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    CHECK(center_x->value() > 50.0 && center_x->value() <= 100.0);
    CHECK(center_y->value() < 50.0 && center_y->value() >= 0.0);
    CHECK(std::abs(preview->property("filterCenterXNormalized").toDouble() -
                   0.70) <= 0.002);
    CHECK(std::abs(preview->property("filterCenterYNormalized").toDouble() -
                   0.30) <= 0.002);
    dragged_center_x = center_x->value();
    dragged_center_y = center_y->value();
    save_widget_artifact("ui_filter_settings_dialog_proxy_preview", *dialog);
    drove_dialog = true;
    dialog->accept();
  });
  const auto settings =
      patchy::ui::request_filter_settings(nullptr, spec, {}, {}, &source);
  CHECK(drove_dialog);
  CHECK(settings.has_value());
  CHECK(std::abs(std::get<double>(settings->parameters.at("center_x")) -
                 dragged_center_x) < 0.000001);
  CHECK(std::abs(std::get<double>(settings->parameters.at("center_y")) -
                 dragged_center_y) < 0.000001);
}

void ui_filter_settings_dialog_tilt_shift_overlay_defers_proxy_adoption_until_release() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  const auto* tilt = registry.find("patchy.filters.tilt_shift_blur");
  CHECK(tilt != nullptr);
  const auto spec = patchy::ui::filter_dialog_spec_for(*tilt);
  const auto pixels =
      solid_pixels(96, 64, patchy::PixelFormat::rgba8(), QColor(200, 120, 60));
  const patchy::Rect bounds{0, 0, 96, 64};
  const patchy::ui::FilterDialogPreviewSource source{&pixels, bounds, QRegion(),
                                                     &registry};

  bool drove_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* preview_widget =
        dialog->findChild<QWidget*>(QStringLiteral("filterDialogPreview"));
    auto* preview =
        dynamic_cast<patchy::ui::ZoomableImagePreview*>(preview_widget);
    auto* center_x =
        dialog->findChild<QDoubleSpinBox*>(QStringLiteral("filterCenterXSpin"));
    CHECK(preview != nullptr && center_x != nullptr);
    CHECK(process_events_until(
        [&] {
          return preview->property("filterDialogRenderedFilterId").toString() ==
                 QStringLiteral("patchy.filters.tilt_shift_blur");
        },
        5000));
    CHECK(process_events_until(
        [&] {
          return preview->property("filterTiltShiftOverlayVisible").toBool();
        },
        3000));

    const auto pending_size = preview->image().size();
    const auto handle =
        preview->property("filterTiltShiftCenterPoint").toPoint();
    send_mouse(*preview, QEvent::MouseButtonPress, handle, Qt::LeftButton,
               Qt::LeftButton);
    send_mouse(*preview, QEvent::MouseMove, handle + QPoint(14, 8),
               Qt::NoButton, Qt::LeftButton);
    process_events_for(120);
    // Mid-gesture the size-changing proxy must not be adopted, so the
    // overlay's coordinate system cannot jump under the pointer.
    CHECK(preview->image().size() == pending_size);
    CHECK(center_x->value() > 50.0);
    send_mouse(*preview, QEvent::MouseButtonRelease, handle + QPoint(14, 8),
               Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    drove_dialog = true;
    dialog->accept();
  });
  const auto settings =
      patchy::ui::request_filter_settings(nullptr, spec, {}, {}, &source);
  CHECK(drove_dialog);
  CHECK(settings.has_value());
  CHECK(std::get<double>(settings->parameters.at("center_x")) > 50.0);
}

}  // namespace

std::vector<patchy::test::TestCase> filter_catalog_dialog_tests() {
  return {
      {"ui_filter_catalog_and_menu_contracts_are_stable", ui_filter_catalog_and_menu_contracts_are_stable},
      {"ui_filter_progress_callback_can_cancel_heavy_filter",
       ui_filter_progress_callback_can_cancel_heavy_filter},
      {"ui_filter_progress_callback_can_cancel_simple_filter",
       ui_filter_progress_callback_can_cancel_simple_filter},
      {"ui_all_builtin_filters_report_progress",
       ui_all_builtin_filters_report_progress},
      {"ui_all_builtin_filters_complete_progress_monotonically",
       ui_all_builtin_filters_complete_progress_monotonically},
      {"ui_adjustment_pixel_progress_callback_can_cancel",
       ui_adjustment_pixel_progress_callback_can_cancel},
      {"ui_filter_settings_dialog_shows_before_initial_preview",
       ui_filter_settings_dialog_shows_before_initial_preview},
      {"ui_filter_settings_dialog_coalesces_rapid_slider_preview_callbacks",
       ui_filter_settings_dialog_coalesces_rapid_slider_preview_callbacks},
      {"ui_filter_settings_dialog_delivers_latest_after_slow_preview_callback",
       ui_filter_settings_dialog_delivers_latest_after_slow_preview_callback},
      {"ui_filter_settings_dialog_reset_restores_named_defaults_and_colors",
       ui_filter_settings_dialog_reset_restores_named_defaults_and_colors},
      {"ui_filter_parameter_panel_round_trips_catalog_and_legacy_specs",
       ui_filter_parameter_panel_round_trips_catalog_and_legacy_specs},
      {"ui_filter_settings_dialog_angle_dial_and_waveform_sync_numeric_controls",
       ui_filter_settings_dialog_angle_dial_and_waveform_sync_numeric_controls},
      {"ui_filter_settings_dialog_proxy_preview_renders_and_center_overlay_drag_syncs_spins",
       ui_filter_settings_dialog_proxy_preview_renders_and_center_overlay_drag_syncs_spins},
      {"ui_filter_settings_dialog_tilt_shift_overlay_defers_proxy_adoption_until_release",
       ui_filter_settings_dialog_tilt_shift_overlay_defers_proxy_adoption_until_release},
      {"ui_liquify_dialog_exposes_manual_tools_and_brush_controls",
       ui_liquify_dialog_exposes_manual_tools_and_brush_controls},
      {"ui_liquify_action_applies_selection_as_one_undo_step",
       ui_liquify_action_applies_selection_as_one_undo_step},
      {"ui_liquify_fragmented_selection_preview_and_apply", ui_liquify_fragmented_selection_preview_and_apply},
      {"ui_liquify_preview_selection_preserves_sample_mapping", ui_liquify_preview_selection_preserves_sample_mapping},
      {"ui_liquify_undo_redo_steps_strokes_and_restore", ui_liquify_undo_redo_steps_strokes_and_restore},
      {"ui_liquify_keys_choose_tools_and_size_without_reaching_the_main_window",
       ui_liquify_keys_choose_tools_and_size_without_reaching_the_main_window},
      {"ui_liquify_rate_and_pen_pressure", ui_liquify_rate_and_pen_pressure},
      {"ui_liquify_mesh_preview_toggle_and_reconstruct_amount", ui_liquify_mesh_preview_toggle_and_reconstruct_amount},
      {"ui_liquify_dialog_captures_in_both_schemes", ui_liquify_dialog_captures_in_both_schemes},
      {"ui_liquify_preview_shows_transparency_checkerboard", ui_liquify_preview_shows_transparency_checkerboard},
      {"ui_dialog_preview_checkerboard_matches_the_canvas_in_both_schemes",
       ui_dialog_preview_checkerboard_matches_the_canvas_in_both_schemes},
  };
}
