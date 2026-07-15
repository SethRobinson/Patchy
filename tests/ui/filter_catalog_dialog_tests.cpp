#include "ui/canvas_widget.hpp"
#include "core/adjustment_layer.hpp"
#include "core/contour_presets.hpp"
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
      {"patchy.filters.auto_contrast", Category::Adjustment, true,
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
       {{"levels", "filterLevels", 2, 16, 4, Unit::None}}},
      {"patchy.filters.box_blur", Category::Blur, false,
       {{"radius", "filterRadius", 1, 12, 1, Unit::Pixels, Scale::Pixels}}},
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
       {{"radius", "filterRadius", 1, 12, 2, Unit::Pixels, Scale::Pixels}}},
      {"patchy.filters.motion_blur",
       Category::Blur,
       false,
       {{"angle", "filterAngle", -360, 360, 0, Unit::Degrees, Scale::None,
         Kind::Integer, 1.0, Presentation::Angle},
        {"distance", "filterDistance", 1, 999, 12, Unit::Pixels,
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
       {{"angle", "filterAngle", -180, 180, 135, Unit::Degrees, Scale::None,
         Kind::Integer, 1.0, Presentation::Angle},
        {"height", "filterHeight", 1, 24, 2, Unit::Pixels, Scale::Pixels},
        {"amount", "filterDepth", 0, 300, 100, Unit::Percent}}},
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
       {{"amplitude", "filterAmplitude", 0, 64, 12, Unit::Pixels, Scale::Pixels,
         Kind::Integer, 1.0, Presentation::WaveAmplitude},
        {"wavelength", "filterWavelength", 4, 256, 48, Unit::Pixels, Scale::Pixels,
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
       {{"block_size", "filterBlockSize", 2, 32, 4, Unit::Pixels, Scale::Pixels}}},
      {"patchy.filters.color_halftone", Category::Pixelate, false,
       {{"cell_size", "filterCellSize", 4, 64, 10, Unit::Pixels, Scale::Pixels},
        {"intensity", "filterIntensity", 0, 100, 75, Unit::Percent},
        {"contrast", "filterContrast", 0, 100, 60, Unit::Percent}}},
      {"patchy.filters.film_grain", Category::Noise, false,
       {{"amount", "filterAmount", 0, 100, 50, Unit::Percent}}},
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
       {{"radius", "filterRadius", 1, 100, 1, Unit::Pixels, Scale::Pixels},
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
       {{"blur", "filterBlur", 0, 100, 15, Unit::Pixels, Scale::Pixels,
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
      CHECK(actual.minimum.has_value());
      CHECK(actual.maximum.has_value());
      CHECK(*actual.minimum == wanted.minimum);
      CHECK(*actual.maximum == wanted.maximum);
      CHECK(std::abs(actual.step.value_or(1.0) - wanted.step) < 0.000001);
      CHECK(actual.unit == wanted.unit);
      CHECK(actual.spatial_scale == wanted.spatial_scale);
      CHECK(actual.presentation == wanted.presentation);
      CHECK(actual.display_name.empty() == false);

      const auto& control = dialog_spec.controls[parameter_index];
      CHECK(control.parameter_key == wanted.key);
      CHECK(control.object_name == QString::fromLatin1(wanted.object_name));
      CHECK(control.kind == wanted.kind);
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
      CHECK(control.presentation == wanted.presentation);
      CHECK(!control.label.isEmpty());
      if (actual_filter.identifier == "patchy.filters.high_pass" &&
          actual.key == "radius") {
        CHECK(actual.practical_minimum == 0.1);
        CHECK(actual.practical_maximum == 12.0);
      } else if (actual_filter.identifier == "patchy.filters.median" &&
                 actual.key == "radius") {
        CHECK(actual.practical_minimum == 1.0);
        CHECK(actual.practical_maximum == 25.0);
      } else if (actual_filter.identifier ==
                     "patchy.filters.dust_and_scratches" &&
                 actual.key == "radius") {
        CHECK(actual.practical_minimum == 1.0);
        CHECK(actual.practical_maximum == 25.0);
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
        CHECK(actual.practical_maximum == 12.0);
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
  auto* convert_action =
      require_action(window, "filterConvertForSmartFiltersAction");
  auto* gallery_action = require_action(window, "filterGalleryAction");
  CHECK(convert_action->property("patchy.channelViewBlocked").toBool());
  CHECK(gallery_action->property("patchy.channelViewBlocked").toBool());
  CHECK(!filter_menu->actions().isEmpty());
  CHECK(filter_menu->actions().front() == convert_action);
  CHECK(filter_menu->actions().size() >= 4);
  CHECK(filter_menu->actions()[1]->isSeparator());
  CHECK(filter_menu->actions()[2] == gallery_action);
  CHECK(filter_menu->actions()[3]->isSeparator());
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
        QStringLiteral("filterAction_patchy_filters_tilt_shift_blur")}},
      {"filterSharpenMenu",
       {QStringLiteral("filterAction_patchy_filters_sharpen"),
        QStringLiteral("filterAction_patchy_filters_unsharp_mask"),
        QStringLiteral("filterAction_patchy_filters_high_pass")}},
      {"filterDistortMenu",
       {QStringLiteral("filterAction_patchy_filters_twirl"), QStringLiteral("filterAction_patchy_filters_wave"),
        QStringLiteral("filterAction_patchy_filters_pinch_bloat")}},
      {"filterNoiseMenu",
       {QStringLiteral("filterAction_patchy_filters_film_grain"),
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
  const std::array<ExpectedHotkeyAction, 6> existing_filter_hotkeys = {{
      {"patchy.filters.invert", "imageAdjustInvertAction"},
      {"patchy.filters.desaturate", "imageAdjustDesaturateAction"},
      {"patchy.filters.auto_contrast", "imageAdjustAutoContrastAction"},
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

  auto invocation = filter_invocation(registry, "patchy.filters.gaussian_blur");
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
  };
}
