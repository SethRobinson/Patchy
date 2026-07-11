#include "ui/curves_clipping_preview.hpp"

#include <QColor>
#include <QImage>

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

#define CHECK(condition)                                                                    \
  do {                                                                                      \
    if (!(condition)) {                                                                     \
      throw std::runtime_error(std::string("CHECK failed: ") + #condition + " at " +      \
                               __FILE__ + ":" + std::to_string(__LINE__));                  \
    }                                                                                       \
  } while (false)

void check_color(const QImage& image, int x, int y, int red, int green, int blue,
                 int alpha = 255) {
  const auto color = image.pixelColor(x, y);
  CHECK(color.red() == red);
  CHECK(color.green() == green);
  CHECK(color.blue() == blue);
  CHECK(color.alpha() == alpha);
}

void highlights_show_clipped_components_on_black() {
  QImage source(4, 1, QImage::Format_RGBA8888);
  source.setPixelColor(0, 0, QColor(255, 0, 128, 17));
  source.setPixelColor(1, 0, QColor(255, 255, 255, 128));
  source.setPixelColor(2, 0, QColor(254, 255, 0, 255));
  source.setPixelColor(3, 0, QColor(1, 2, 3, 0));

  const auto preview = patchy::ui::render_curves_clipping_preview(
      source, patchy::ui::CurvesClippingMode::Highlights);
  CHECK(preview.format() == QImage::Format_RGBA8888);
  check_color(preview, 0, 0, 255, 0, 0, 17);
  check_color(preview, 1, 0, 255, 255, 255, 128);
  check_color(preview, 2, 0, 0, 255, 0, 255);
  check_color(preview, 3, 0, 0, 0, 0, 0);

  check_color(source, 0, 0, 255, 0, 128, 17);
}

void shadows_show_subtractive_clipped_components_on_white() {
  QImage source(4, 1, QImage::Format_RGB888);
  source.setPixelColor(0, 0, QColor(0, 255, 128));
  source.setPixelColor(1, 0, QColor(0, 0, 0));
  source.setPixelColor(2, 0, QColor(1, 0, 255));
  source.setPixelColor(3, 0, QColor(1, 2, 3));

  const auto preview = patchy::ui::render_curves_clipping_preview(
      source, patchy::ui::CurvesClippingMode::Shadows);
  CHECK(preview.format() == QImage::Format_RGB888);
  check_color(preview, 0, 0, 0, 255, 255);
  check_color(preview, 1, 0, 0, 0, 0);
  check_color(preview, 2, 0, 255, 0, 255);
  check_color(preview, 3, 0, 255, 255, 255);
}

void combined_view_distinguishes_shadows_and_highlights() {
  QImage source(4, 1, QImage::Format_RGB888);
  source.setPixelColor(0, 0, QColor(255, 0, 128));
  source.setPixelColor(1, 0, QColor(0, 255, 255));
  source.setPixelColor(2, 0, QColor(100, 100, 100));
  source.setPixelColor(3, 0, QColor(255, 64, 0));

  const auto preview = patchy::ui::render_curves_clipping_preview(
      source, patchy::ui::CurvesClippingMode::Both);
  CHECK(preview == patchy::ui::render_curves_clipping_preview(
                       source, patchy::ui::CurvesClippingMode::Both));
  check_color(preview, 0, 0, 255, 0, 128);
  check_color(preview, 1, 0, 0, 255, 255);
  check_color(preview, 2, 0, 128, 128, 128);
  check_color(preview, 3, 0, 255, 128, 0);
}

void active_component_restricts_the_indication() {
  QImage white(1, 1, QImage::Format_RGB888);
  white.fill(QColor(255, 255, 255));
  const auto red_highlight = patchy::ui::render_curves_clipping_preview(
      white, patchy::ui::CurvesClippingMode::Highlights, patchy::CurvesChannel::Red);
  const auto green_highlight = patchy::ui::render_curves_clipping_preview(
      white, patchy::ui::CurvesClippingMode::Highlights, patchy::CurvesChannel::Green);
  const auto all_highlights = patchy::ui::render_curves_clipping_preview(
      white, patchy::ui::CurvesClippingMode::Highlights, patchy::CurvesChannel::Rgb);
  check_color(red_highlight, 0, 0, 255, 0, 0);
  check_color(green_highlight, 0, 0, 0, 255, 0);
  check_color(all_highlights, 0, 0, 255, 255, 255);

  QImage black(1, 1, QImage::Format_RGB888);
  black.fill(QColor(0, 0, 0));
  const auto red_shadow = patchy::ui::render_curves_clipping_preview(
      black, patchy::ui::CurvesClippingMode::Shadows, patchy::CurvesChannel::Red);
  const auto green_shadow = patchy::ui::render_curves_clipping_preview(
      black, patchy::ui::CurvesClippingMode::Shadows, patchy::CurvesChannel::Green);
  const auto blue_shadow = patchy::ui::render_curves_clipping_preview(
      black, patchy::ui::CurvesClippingMode::Shadows, patchy::CurvesChannel::Blue);
  check_color(red_shadow, 0, 0, 0, 255, 255);
  check_color(green_shadow, 0, 0, 255, 0, 255);
  check_color(blue_shadow, 0, 0, 255, 255, 0);
}

void null_and_premultiplied_inputs_are_safe() {
  CHECK(patchy::ui::render_curves_clipping_preview(
            {}, patchy::ui::CurvesClippingMode::Both)
            .isNull());

  QImage source(1, 1, QImage::Format_ARGB32_Premultiplied);
  source.setPixelColor(0, 0, QColor(255, 0, 128, 73));
  const auto preview = patchy::ui::render_curves_clipping_preview(
      source, patchy::ui::CurvesClippingMode::Highlights);
  CHECK(preview.format() == QImage::Format_RGBA8888);
  check_color(preview, 0, 0, 255, 0, 0, 73);
}

}  // namespace

int main() {
  try {
    highlights_show_clipped_components_on_black();
    shadows_show_subtractive_clipped_components_on_white();
    combined_view_distinguishes_shadows_and_highlights();
    active_component_restricts_the_indication();
    null_and_premultiplied_inputs_are_safe();
    std::cout << "Curves clipping preview tests passed.\n";
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
