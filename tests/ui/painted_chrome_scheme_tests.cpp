// Painted chrome follows the color scheme (docs/ui-conventions.md, "Color scheme"):
// the Curves graph, the Levels histogram and output ramp, the Blend If control and
// the gradient stops editor paint their chrome from theme() roles, so each one is
// rendered in Dark and in Light and read back against that scheme's palette. Dark
// stays pixel-identical to the literals these widgets used before their roles
// existed (image_adjustments_curves_tests.cpp pins the Levels bars and well).

#include "test_harness.hpp"
#include "ui/blend_if_range_editor.hpp"
#include "ui/curves_editor.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/gradient_stops_editor.hpp"
#include "ui/theme_manager.hpp"
#include "ui/theme_palette.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QApplication>
#include <QDialog>
#include <QImage>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

using namespace patchy::test::ui;

struct SchemeCase {
  patchy::ui::ColorSchemePreference preference;
  patchy::ui::ColorScheme scheme;
  const char* label;
};

constexpr SchemeCase kSchemes[] = {
    {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorScheme::Dark, "dark"},
    {patchy::ui::ColorSchemePreference::Light, patchy::ui::ColorScheme::Light, "light"},
};

// Exact for a flat fill; a small tolerance absorbs nothing but rounding.
bool pixel_is(const QImage& image, QPoint at, const QColor& expected, const char* what, const char* label,
              int tolerance = 2) {
  const auto actual = image.pixelColor(at);
  const bool ok = color_close(actual, expected, tolerance);
  if (!ok) {
    std::fprintf(stderr, "  %s: %s at (%d, %d) is %s, want %s\n", label, what, at.x(), at.y(),
                 qPrintable(actual.name(QColor::HexRgb)), qPrintable(expected.name(QColor::HexRgb)));
  }
  return ok;
}

int channel_delta(const QColor& first, const QColor& second) {
  return std::max({std::abs(first.red() - second.red()), std::abs(first.green() - second.green()),
                   std::abs(first.blue() - second.blue())});
}

double relative_luminance(const QColor& color) {
  const auto channel = [](double value) {
    return value <= 0.03928 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
  };
  return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) + 0.0722 * channel(color.blueF());
}

double contrast_ratio(const QColor& first, const QColor& second) {
  const double a = relative_luminance(first);
  const double b = relative_luminance(second);
  return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

int count_close(const QImage& image, const QRect& area, const QColor& color, int tolerance) {
  int count = 0;
  const auto bounded = area.intersected(image.rect());
  for (int y = bounded.top(); y <= bounded.bottom(); ++y) {
    for (int x = bounded.left(); x <= bounded.right(); ++x) {
      count += color_close(image.pixelColor(x, y), color, tolerance) ? 1 : 0;
    }
  }
  return count;
}

void ui_painted_graph_and_stop_chrome_follows_the_color_scheme() {
  ColorSchemeRestorer restore_active;
  // The two palettes must actually differ for these roles, or the test proves nothing.
  CHECK(patchy::ui::dark_palette().curves_graph_well_bg != patchy::ui::light_palette().curves_graph_well_bg);
  CHECK(patchy::ui::dark_palette().levels_graph_well_bg != patchy::ui::light_palette().levels_graph_well_bg);
  CHECK(patchy::ui::dark_palette().gradient_stop_outline != patchy::ui::light_palette().gradient_stop_outline);
  // Dark keeps the values the widgets were authored with.
  CHECK(patchy::ui::dark_palette().curves_graph_well_bg == QColor(23, 26, 32));
  CHECK(patchy::ui::dark_palette().levels_graph_well_bg == QColor(55, 55, 55));
  CHECK(patchy::ui::dark_palette().levels_graph_ink == QColor(218, 218, 218));
  CHECK(patchy::ui::dark_palette().gradient_stop_outline == QColor(255, 255, 255));

  for (const auto& scheme_case : kSchemes) {
    ColorSchemeRestorer::apply(scheme_case.preference);
    const auto& palette = patchy::ui::theme(scheme_case.scheme);
    const char* label = scheme_case.label;
    CHECK(patchy::ui::active_color_scheme() == scheme_case.scheme);

    // The pairs that sit against each other read in this scheme, not just the one
    // they were authored in.
    const auto check_contrast = [label](const char* what, const QColor& ink, const QColor& ground, double minimum) {
      const double measured = contrast_ratio(ink, ground);
      if (measured < minimum) {
        std::fprintf(stderr, "  %s: %s contrast %.2f, want >= %.2f\n", label, what, measured, minimum);
      }
      CHECK(measured >= minimum);
    };
    check_contrast("curves composite ink on the well", palette.curves_graph_ink, palette.curves_graph_well_bg, 4.5);
    check_contrast("curves axis labels", palette.curves_graph_label_text, palette.curves_graph_bg, 4.5);
    check_contrast("levels composite bars on the well", palette.levels_graph_ink, palette.levels_graph_well_bg, 4.5);
    check_contrast("gradient stop outline on the dialog", palette.gradient_stop_outline, palette.window_bg, 4.5);
    CHECK(channel_delta(palette.levels_graph_border, palette.levels_graph_well_bg) >= 8);

    // Curves: widget surface, the well, and the RGB composite curve's ink.
    {
      patchy::ui::CurvesEditorWidget editor;
      editor.resize(560, 420);
      editor.show();
      QApplication::processEvents();
      auto* graph = editor.findChild<QWidget*>(QStringLiteral("curvesGraph"));
      CHECK(graph != nullptr);
      if (graph != nullptr) {
        const auto image = graph->grab().toImage();
        CHECK(pixel_is(image, QPoint(2, 2), palette.curves_graph_bg, "curves surface", label));
        // Up and left of the center: clear of the diagonal, the quarter grid and the
        // (empty) histogram.
        const int step = std::min(graph->width(), graph->height()) / 10;
        const QPoint well(graph->width() / 2 - step, graph->height() / 2 - step);
        CHECK(pixel_is(image, well, palette.curves_graph_well_bg, "curves well", label));
        // The identity composite curve crosses the center; the points sit at the corners.
        const QRect center(graph->width() / 2 - step / 2, graph->height() / 2 - step / 2, step, step);
        const int ink = count_close(image, center, palette.curves_graph_ink, 3);
        if (ink <= 8) {
          std::fprintf(stderr, "  %s: curves composite ink pixels %d near the center, want > 8\n", label, ink);
        }
        CHECK(ink > 8);
        if (scheme_case.scheme == patchy::ui::ColorScheme::Light) {
          save_widget_artifact("ui_painted_chrome_curves_light", editor);
        }
      }
    }

    // Levels: the dialog owns both graphs, so drive it the way the Levels tests do.
    bool inspected = false;
    QTimer::singleShot(0, guarded([&] {
      auto* dialog = find_top_level_dialog(QStringLiteral("patchyLevelsDialog"));
      CHECK(dialog != nullptr);
      if (dialog == nullptr) {
        return;
      }
      auto* input = dialog->findChild<QWidget*>(QStringLiteral("levelsInputGraph"));
      auto* output = dialog->findChild<QWidget*>(QStringLiteral("levelsOutputRange"));
      CHECK(input != nullptr);
      CHECK(output != nullptr);
      if (input != nullptr && output != nullptr) {
        const auto image = input->grab().toImage();
        // LevelsInputGraph::graph_rect().
        const auto graph_rect = QRect(0, 0, input->width(), input->height()).adjusted(8, 8, -8, -28);
        CHECK(pixel_is(image, QPoint(2, 2), palette.levels_graph_bg, "levels surface", label));
        // The frame's top edge, between the black and gamma markers. The graph paints
        // antialiased, so the 1 px line straddles the edge and covers half of this row.
        const auto half = [](const QColor& line, const QColor& under) {
          return QColor((line.red() + under.red()) / 2, (line.green() + under.green()) / 2,
                        (line.blue() + under.blue()) / 2);
        };
        CHECK(pixel_is(image, QPoint(graph_rect.left() + graph_rect.width() / 4, graph_rect.top()),
                       half(palette.levels_graph_border, palette.levels_graph_well_bg), "levels frame", label, 3));
        // The bars never reach the top 4 px of the well.
        CHECK(pixel_is(image, QPoint(graph_rect.left() + graph_rect.width() / 4, graph_rect.top() + 2),
                       palette.levels_graph_well_bg, "levels well", label));
        // The placeholder histogram's midtone hump, clear of the gamma marker.
        const int column = graph_rect.left() + (100 * graph_rect.width()) / 256;
        CHECK(pixel_is(image, QPoint(column, graph_rect.bottom() - 2), palette.levels_graph_ink, "levels bars",
                       label));
        const auto ramp = output->grab().toImage();
        CHECK(pixel_is(ramp, QPoint(2, 2), palette.levels_graph_bg, "levels output surface", label));
        if (scheme_case.scheme == patchy::ui::ColorScheme::Light) {
          save_widget_artifact("ui_painted_chrome_levels_light", *dialog);
        }
      }
      inspected = true;
      dialog->reject();
    }));
    const auto result = patchy::ui::request_levels_settings(nullptr);
    CHECK(inspected);
    CHECK(!result.has_value());

    // Blend If: the unfocused frame.
    {
      patchy::ui::BlendIfRangeEditorWidget blend_if;
      // A shown top-level takes focus, and the focused frame is accent_control.
      blend_if.setFocusPolicy(Qt::NoFocus);
      blend_if.resize(360, 56);
      blend_if.show();
      QApplication::processEvents();
      const auto image = blend_if.grab().toImage();
      CHECK(pixel_is(image, QPoint(1, image.height() / 2), palette.blend_if_frame_border, "blend if frame", label));
    }

    // Gradient stops: bar outline, unselected stop outlines and midpoint diamonds.
    {
      patchy::ui::GradientStopsEditorWidget stops;
      stops.set_opacity_track_enabled(true);
      // Stop colors and opacities chosen so no content pixel can pass for chrome.
      stops.set_stops({patchy::GradientStop{0.0F, patchy::EditColor{200, 40, 40, 255}, 0.0F},
                       patchy::GradientStop{1.0F, patchy::EditColor{40, 160, 40, 255}, 0.0F}});
      stops.set_color_midpoints({0.5F, 0.5F});
      stops.set_opacity_stops({patchy::GradientAlphaStop{0.0F, 0.9F, 0.5F}, patchy::GradientAlphaStop{1.0F, 0.1F, 0.5F}});
      stops.set_current_row(-1);
      stops.set_current_opacity_row(-1);
      stops.resize(420, 96);
      stops.show();
      QApplication::processEvents();
      const auto image = stops.grab().toImage();
      // GradientStopsEditorWidget::bar_rect() with the opacity track: left 10, top 30.
      CHECK(pixel_is(image, QPoint(10, 46), palette.swatch_border, "gradient bar outline", label));
      const QRect color_handles(0, 62, image.width(), image.height() - 62);
      const int outline = count_close(image, color_handles, palette.gradient_stop_outline, 4);
      const int midpoint = count_close(image, image.rect(), palette.gradient_midpoint_fill, 4);
      if (outline <= 10 || midpoint <= 2) {
        std::fprintf(stderr, "  %s: stop outline pixels %d (want > 10), midpoint fill pixels %d (want > 2)\n", label,
                     outline, midpoint);
      }
      CHECK(outline > 10);
      CHECK(midpoint > 2);
      if (scheme_case.scheme == patchy::ui::ColorScheme::Light) {
        save_widget_artifact("ui_painted_chrome_gradient_stops_light", stops);
      }
    }
  }
}

}  // namespace

std::vector<patchy::test::TestCase> painted_chrome_scheme_tests() {
  return {
      {"ui_painted_graph_and_stop_chrome_follows_the_color_scheme",
       ui_painted_graph_and_stop_chrome_follows_the_color_scheme},
  };
}
