#include "test_harness.hpp"
#include "ui/action_icons.hpp"
#include "ui/splash_artwork.hpp"
#include "ui_test_support.hpp"

#include <QAbstractAnimation>
#include <QPainter>
#include <iterator>
#include <vector>

std::vector<patchy::test::TestCase> app_shell_tests_part1();
std::vector<patchy::test::TestCase> app_shell_tests_part2();
std::vector<patchy::test::TestCase> app_shell_tests_part3();
std::vector<patchy::test::TestCase> app_shell_tests_part4();

namespace {

void ui_splash_artwork_intro_stages_and_resting_icon() {
  using patchy::test::ui::capture_widget_artifact;
  using patchy::ui::SplashArtwork;
  auto parent = patchy::test::ui::make_themed_dialog_parent();
  parent->resize(360, 160);
  SplashArtwork automatic(parent.get());
  SplashArtwork animated(parent.get(), SplashArtwork::Playback::Animate);
  automatic.setGeometry(0, 0, 180, 136);
  animated.setGeometry(180, 0, 180, 136);
  parent->show();
  CHECK(patchy::test::ui::process_events_until([&] {
    return animated.findChild<QAbstractAnimation*>() != nullptr;
  }));
  auto* clock = animated.findChild<QAbstractAnimation*>();
  CHECK(clock != nullptr);
  clock->pause();
  CHECK(automatic.findChild<QAbstractAnimation*>() == nullptr);
  const QImage reference = capture_widget_artifact("ui_splash_artwork_static", automatic);

  CHECK(!reference.isNull());

  // Pin the former QIcon paint, including the padding in a non-square widget.
  QImage expected = reference;
  expected.fill(reference.pixelColor(0, 0));
  {
    QPainter painter(&expected);
    const int side = 136 * 824 / 1024;
    patchy::ui::patchy_app_icon().paint(&painter, QRect((180 - side) / 2, (136 - side) / 2, side, side));
  }
  CHECK(reference == expected);

  auto at = [&](int ms, const char* name) {
    clock->setCurrentTime(ms);
    return capture_widget_artifact(std::string("ui_splash_artwork_") + name, animated);
  };
  const QImage tile = at(80, "tile");
  const QImage letter = at(350, "letter");
  const QImage turning = at(680, "flip");
  const QImage landed = at(860, "landed");
  const QImage shine = at(1070, "shine");
  // Interior samples from the stem and the folded paper distinguish the
  // authored parts even when the test platform uses a higher pixel density.
  const auto pixel = [](const QImage& image, QPoint point) {
    return image.pixel(point * image.devicePixelRatio());
  };
  const QPoint stem(60, 72);
  const QPoint paper(114, 38);
  CHECK(pixel(tile, stem) == pixel(tile, paper));
  CHECK(pixel(letter, stem) == pixel(reference, stem));
  CHECK(pixel(letter, paper) == pixel(letter, stem));
  CHECK(pixel(landed, paper) == qRgb(255, 255, 255));
  CHECK(tile != letter);
  CHECK(letter != turning);
  CHECK(turning != landed);
  CHECK(landed == reference);
  CHECK(shine != landed);
  // Nothing outside the tile, including the clipped top right corner, shines.
  for (int y = 0; y < reference.height(); ++y) {
    for (int x = 0; x < reference.width(); ++x) {
      if (reference.pixel(x, y) == reference.pixel(0, 0)) {
        CHECK(shine.pixel(x, y) == reference.pixel(x, y));
      }
    }
  }

  clock->resume();
  clock->setCurrentTime(clock->duration());
  CHECK(clock->state() == QAbstractAnimation::Stopped);
  CHECK(capture_widget_artifact("ui_splash_artwork_rest", animated) == reference);
  animated.hide();
  animated.show();
  QApplication::processEvents();
  CHECK(capture_widget_artifact("ui_splash_artwork_reshown", animated) == reference);
}

}  // namespace

// Keep the original registration order: later tests consume settings and artifacts.
std::vector<patchy::test::TestCase> app_shell_tests() {
  std::vector<patchy::test::TestCase> tests;
  {
    auto part = app_shell_tests_part1();
    tests.insert(tests.end(), std::make_move_iterator(part.begin()), std::make_move_iterator(part.end()));
  }
  {
    auto part = app_shell_tests_part2();
    tests.insert(tests.end(), std::make_move_iterator(part.begin()), std::make_move_iterator(part.end()));
  }
  {
    auto part = app_shell_tests_part3();
    tests.insert(tests.end(), std::make_move_iterator(part.begin()), std::make_move_iterator(part.end()));
  }
  {
    auto part = app_shell_tests_part4();
    tests.insert(tests.end(), std::make_move_iterator(part.begin()), std::make_move_iterator(part.end()));
  }
  tests.push_back({"ui_splash_artwork_intro_stages_and_resting_icon", ui_splash_artwork_intro_stages_and_resting_icon});
  return tests;
}
