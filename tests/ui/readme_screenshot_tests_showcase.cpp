// README screenshots (shot_readme_*), part 3: gallery scenes for the dialogs a
// script cannot open (Export for Web, Perspective Planes, Flame).
// The group-wide story lives in the aggregator (readme_screenshot_tests.cpp);
// helpers shared with the other parts live in readme_screenshot_test_support.

#include "core/document_path.hpp"
#include "core/perspective_planes.hpp"
#include "core/vector_shape.hpp"
#include "local_psd_fixtures.hpp"
#include "test_fonts.hpp"
#include "test_harness.hpp"
#include "ui/app_settings.hpp"
#include "ui/image_document_io.hpp"
#include "ui/main_window.hpp"
#include "ui/perspective_planes_dialog.hpp"
#include "ui/web_export.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "readme_screenshot_test_support.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFont>
#include <QFontDatabase>
#include <QImage>
#include <QLabel>
#include <QLinearGradient>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRadialGradient>
#include <QSpinBox>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace patchy::test::ui;

// --- Export for Web: an animated GIF of a city going from day to blue hour ---

constexpr int kTimelapseWidth = 960;
constexpr int kTimelapseHeight = 600;
constexpr int kTimelapseFrames = 8;

struct FrameGrade {
  double t;
  std::array<double, 3> gain;
  double lights;  // how much the lit windows glow
};

// Keyframes the frames interpolate: bright afternoon, golden hour, dusk, blue hour.
constexpr std::array<FrameGrade, 4> kTimelapseGrades{{
    {0.0, {1.04, 1.05, 1.10}, 0.0},
    {0.45, {1.16, 0.94, 0.72}, 0.0},
    {0.75, {0.78, 0.62, 0.70}, 0.35},
    {1.0, {0.34, 0.42, 0.68}, 1.0},
}};

FrameGrade timelapse_grade(double t) {
  for (std::size_t i = 1; i < kTimelapseGrades.size(); ++i) {
    const auto& a = kTimelapseGrades[i - 1];
    const auto& b = kTimelapseGrades[i];
    if (t <= b.t) {
      const double f = (t - a.t) / (b.t - a.t);
      FrameGrade out{t, {}, a.lights + (b.lights - a.lights) * f};
      for (std::size_t c = 0; c < 3; ++c) {
        out.gain[c] = a.gain[c] + (b.gain[c] - a.gain[c]) * f;
      }
      return out;
    }
  }
  return kTimelapseGrades.back();
}

// One frame of the time-lapse: the city graded toward evening, with a slow push in, and
// the brightest facades below the skyline lighting up as it gets dark.
QImage timelapse_frame(const QImage& city, int index) {
  const double t = static_cast<double>(index) / (kTimelapseFrames - 1);
  const auto grade = timelapse_grade(t);
  const double zoom = 1.0 + 0.06 * t;
  const int crop_w = static_cast<int>(std::lround(city.width() / zoom));
  const int crop_h = static_cast<int>(std::lround(city.height() / zoom));
  QImage frame = city.copy((city.width() - crop_w) / 2, (city.height() - crop_h) * 2 / 3, crop_w, crop_h)
                     .scaled(kTimelapseWidth, kTimelapseHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                     .convertToFormat(QImage::Format_RGBA8888);
  for (int y = 0; y < frame.height(); ++y) {
    auto* row = frame.scanLine(y);
    const double below_skyline = std::clamp((static_cast<double>(y) / frame.height() - 0.38) / 0.2, 0.0, 1.0);
    for (int x = 0; x < frame.width(); ++x) {
      auto* p = row + static_cast<std::ptrdiff_t>(x) * 4;
      const double luma = 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2];
      const double glow = grade.lights * below_skyline * std::clamp((luma - 150.0) / 70.0, 0.0, 1.0);
      const std::array<double, 3> lamp{255.0, 196.0, 120.0};
      for (int c = 0; c < 3; ++c) {
        const double graded = p[c] * grade.gain[static_cast<std::size_t>(c)];
        p[c] = static_cast<std::uint8_t>(std::clamp(graded + (lamp[static_cast<std::size_t>(c)] - graded) * glow * 0.85,
                                                    0.0, 255.0));
      }
    }
  }
  return frame;
}

// Every exportForWeb/* key the scene writes, restored afterward.
std::vector<std::unique_ptr<SettingsValueRestorer>> web_export_settings_restorers() {
  std::vector<std::unique_ptr<SettingsValueRestorer>> restorers;
  for (const auto* key : {"format", "jpegQuality", "jpegProgressive", "jpegOptimize", "jpegBackground",
                          "webpQuality", "webpLossless", "webpAnimate", "metadata", "thumbnail", "resample",
                          "trim", "convertToSrgb", "compare", "revealWhenDone", "pngColors", "pngPaletteColors",
                          "pngDither", "pngDitherAmount", "gifColors", "gifDither", "gifDitherAmount", "gifAnimate",
                          "gifFrameDelayMs", "avifQuality", "avifSpeed", "avifLossless", "jxlQuality",
                          "jxlEffort", "jxlLossless", "jxlAnimate", "jxlFrameDelayMs", "pngAnimate",
                          "pngFrameDelayMs", "fillTransparent"}) {
    restorers.push_back(std::make_unique<SettingsValueRestorer>(QStringLiteral("exportForWeb/") + QLatin1String(key)));
  }
  for (const auto* key : {"saveOptions/animationFrameDelayMs", "saveOptions/gifFrameDelayCs"}) {
    restorers.push_back(std::make_unique<SettingsValueRestorer>(QLatin1String(key)));
  }
  return restorers;
}

// Export for Web making an animated GIF: eight frames of San Francisco from afternoon to
// blue hour, one per layer, playing in the preview with the original on the left of the
// split and the 16-color dithered GIF on the right. Every format's real encoded size is
// in the list.
void shot_readme_web_export() {
  const auto restorers = web_export_settings_restorers();
  auto settings = patchy::ui::load_web_export_settings();
  settings.format = patchy::ui::WebExportFormat::Gif;
  settings.gif_animate = true;
  settings.gif_colors = 16;
  settings.gif_dither = true;
  settings.gif_dither_amount = 100;
  settings.animation_frame_delay_ms = 150;
  settings.compare = patchy::ui::WebExportCompare::Split;
  patchy::ui::save_web_export_settings(settings);

  const auto path = patchy::test::committed_format_fixture_path("readme", "san_francisco_cityscape_cc0.jpg");
  QImage city(QString::fromStdString(path.string()));
  CHECK(!city.isNull());
  city = city.scaled(1400, 930, Qt::KeepAspectRatio, Qt::SmoothTransformation);

  patchy::Document document(kTimelapseWidth, kTimelapseHeight, patchy::PixelFormat::rgba8());
  // The top layer is the first frame, so the evening frames go in first. The last frame
  // holds for a second (its name's time).
  const std::array<const char*, kTimelapseFrames> names{
      "Afternoon", "Late afternoon", "Golden hour", "Sunset", "Last light", "Dusk", "Evening", "Blue hour 1s"};
  patchy::LayerId top{};
  for (int index = kTimelapseFrames - 1; index >= 0; --index) {
    patchy::Layer layer(document.allocate_layer_id(), names[static_cast<std::size_t>(index)],
                        patchy::ui::pixels_from_image_rgba(timelapse_frame(city, index)));
    layer.set_bounds(patchy::Rect{0, 0, kTimelapseWidth, kTimelapseHeight});
    top = layer.id();
    document.add_layer(std::move(layer));
  }
  document.set_active_layer(top);

  patchy::ui::MainWindow window;
  show_readme_shot_window(window);
  window.add_document_session(std::move(document), QStringLiteral("city_timelapse.psd"));
  close_untitled_start_tab(window);

  bool captured = false;
  std::exception_ptr error;
  QTimer::singleShot(0, &window, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("webExportDialog"));
    try {
      CHECK(dialog != nullptr);
      dialog->resize(1560, 960);
      QApplication::processEvents();
      auto* preview = dialog->findChild<QWidget*>(QStringLiteral("webExportPreview"));
      auto* play = dialog->findChild<QPushButton*>(QStringLiteral("webExportPlayButton"));
      auto* next = dialog->findChild<QAbstractButton*>(QStringLiteral("webExportNextFrameButton"));
      auto* list = dialog->findChild<QTreeWidget*>(QStringLiteral("webExportFormatList"));
      CHECK(preview != nullptr && play != nullptr && next != nullptr && list != nullptr);
      const auto frames = [preview] { return preview->property("webExportFrameCount").toInt(); };
      const auto frame = [preview] { return preview->property("webExportFrameIndex").toInt(); };
      CHECK(process_events_until(
          [&] { return preview->property("webExportShowsCurrent").toBool() && frames() == kTimelapseFrames; },
          120000));
      // Let the other rows' sizes land (AVIF and JPEG XL wait to be selected).
      process_events_for(4000);
      // Paused on the golden-hour frame, where the sky's gradient shows the dither.
      play->click();
      while (frame() != 3) {
        next->click();
      }
      process_events_for(500);
      dialog->repaint();
      process_events_for(100);
      save_readme_shot("shot_readme_web_export", dialog->grab().toImage());
      captured = true;
    } catch (...) {
      error = std::current_exception();
    }
    if (dialog != nullptr) dialog->reject();
  });
  require_action(window, "fileExportForWebAction")->trigger();
  QApplication::processEvents();
  if (error) std::rethrow_exception(error);
  CHECK(captured);
}

// --- Perspective Planes: posters pasted onto a brick wall ----------------------

// The brick wall photo (3072 x 2304) scaled to this document size.
constexpr int kWallWidth = 1400;
constexpr int kWallHeight = 1050;

// The wall below the windows, built from the photo's own lines: the top and bottom edges
// run to the vanishing point of the window sill and the sign's lower edge, the sides to
// that of the drainpipe and the sign's left edge, so the grid follows the brick courses.
patchy::PlaneQuad brick_wall_plane() {
  return {{{242.0, 435.0}, {1255.0, 185.0}, {1340.0, 947.0}, {36.0, 1024.0}}};
}

patchy::PixelBuffer pixels_from(const QImage& image) {
  return patchy::ui::pixels_from_image_rgba(image.convertToFormat(QImage::Format_RGBA8888));
}

QImage readme_photo(const char* name) {
  const auto path = patchy::test::committed_format_fixture_path("readme", name);
  QImage photo(QString::fromStdString(path.string()));
  CHECK(!photo.isNull());
  return photo;
}

// A street poster: a photo over a colored title band with a tagline.
QImage street_poster(const QImage& photo, const QColor& band, const QString& title, const QString& tagline) {
  constexpr int kWidth = 480;
  constexpr int kHeight = 660;
  constexpr int kPhotoHeight = 470;
  QImage poster(kWidth, kHeight, QImage::Format_RGBA8888);
  poster.fill(QColor(242, 236, 222));
  QPainter painter(&poster);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  const QRect window(16, 16, kWidth - 32, kPhotoHeight - 16);
  const auto fitted = photo.scaled(window.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
  painter.drawImage(window, fitted,
                    QRect((fitted.width() - window.width()) / 2, (fitted.height() - window.height()) / 2,
                          window.width(), window.height()));
  painter.fillRect(QRect(16, kPhotoHeight + 10, kWidth - 32, kHeight - kPhotoHeight - 26), band);
  painter.setPen(QColor(250, 244, 230));
  QFont heading(QStringLiteral("Arial Black"));
  heading.setPixelSize(48);
  heading.setBold(true);
  painter.setFont(heading);
  painter.drawText(QRect(16, kPhotoHeight + 18, kWidth - 32, 84), Qt::AlignCenter, title);
  QFont small(QStringLiteral("Arial"));
  small.setPixelSize(22);
  small.setBold(true);
  small.setLetterSpacing(QFont::AbsoluteSpacing, 6.0);
  painter.setFont(small);
  painter.drawText(QRect(16, kPhotoHeight + 100, kWidth - 32, 40), Qt::AlignCenter, tagline);
  painter.end();
  return poster;
}

QPoint wall_view_point(const QWidget& view, QSize document, patchy::PlanePoint point) {
  const double margin = 24.0;
  const double zoom = std::min((view.width() - 2.0 * margin) / document.width(),
                               (view.height() - 2.0 * margin) / document.height());
  const double ox = (view.width() - document.width() * zoom) * 0.5;
  const double oy = (view.height() - document.height() * zoom) * 0.5;
  return QPoint(static_cast<int>(std::lround(ox + point.x * zoom)), static_cast<int>(std::lround(oy + point.y * zoom)));
}

// Perspective Planes on a real brick wall seen from below: three street posters made from
// the README photos, pasted onto one plane that follows the brick courses. Two are placed;
// the third is still floating with its handles.
void shot_readme_perspective_planes() {
  QImage wall_photo = readme_photo("brick_wall_new_brunswick_cc0.jpg");
  CHECK(wall_photo.width() == 3072 && wall_photo.height() == 2304);
  wall_photo = wall_photo.scaled(kWallWidth, kWallHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  const QSize document(wall_photo.width(), wall_photo.height());

  const auto quad = brick_wall_plane();
  CHECK(patchy::check_plane_quad(quad) == patchy::PlaneQuadProblem::None);
  const patchy::PerspectivePlane plane{1, quad, 0.0, std::nullopt};
  const auto camera = patchy::perspective_camera_for({plane}, document.width(), document.height());
  const auto geometry = patchy::plane_geometry(quad, camera);
  CHECK(geometry.has_value());

  const auto city = readme_photo("san_francisco_cityscape_cc0.jpg").copy(2350, 150, 1500, 1900);
  const auto car = readme_photo("havana_vintage_car_cc0.jpg").copy(1350, 1100, 1000, 900);
  const auto sunset = readme_photo("stylized_sunset_cc0.png").copy(300, 0, 1100, 1200);
  const std::vector<QImage> posters{
      street_poster(city, QColor(178, 40, 36), QStringLiteral("SAN FRANCISCO"), QStringLiteral("CITY BY THE BAY")),
      street_poster(car, QColor(20, 118, 112), QStringLiteral("HAVANA"), QStringLiteral("CLASSIC CAR WEEK")),
      street_poster(sunset, QColor(92, 46, 112), QStringLiteral("SUNSET"), QStringLiteral("MUSIC ON THE PIER")),
  };

  patchy::ui::PerspectiveWorkspaceInput input;
  input.layer = pixels_from(wall_photo);
  input.layer_bounds = patchy::Rect{0, 0, document.width(), document.height()};
  input.document_width = document.width();
  input.document_height = document.height();
  input.layer_name = QStringLiteral("Brick wall");
  input.planes.push_back(plane);
  for (const auto& poster : posters) {
    input.paste_sources.push_back({QStringLiteral("Poster"), pixels_from(poster)});
  }

  patchy::ui::MainWindow theme_host;
  show_readme_shot_window(theme_host);
  bool captured = false;
  std::exception_ptr error;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("perspectivePlanesDialog"));
    try {
      CHECK(dialog != nullptr);
      dialog->resize(1400, 1000);
      QApplication::processEvents();
      auto* view = dialog->findChild<QWidget*>(QStringLiteral("perspectivePlanesView"));
      auto* fit = dialog->findChild<QPushButton*>(QStringLiteral("perspectiveFitButton"));
      auto* edit_tool = dialog->findChild<QToolButton*>(QStringLiteral("perspectiveEditPlaneTool"));
      auto* paste = dialog->findChild<QToolButton*>(QStringLiteral("perspectivePasteButton"));
      auto* scale = dialog->findChild<QSpinBox*>(QStringLiteral("perspectivePasteScaleSpin"));
      CHECK(view != nullptr && fit != nullptr && edit_tool != nullptr && paste != nullptr && scale != nullptr);
      fit->click();
      dialog->activateWindow();
      view->setFocus();
      edit_tool->click();
      QApplication::processEvents();
      const auto at = [&](double u, double v) {
        const auto point = geometry->to_image.map({geometry->width * u, geometry->height * v});
        CHECK(point.has_value());
        return wall_view_point(*view, document, *point);
      };
      // Pastes each poster at the same size (just under half the wall's height), side by side from the
      // left edge with a small gap, and places it; the last one stays floating with its
      // handles.
      constexpr double kPosterHeight = 0.46;
      const double width_fraction =
          kPosterHeight * geometry->height * posters.front().width() / posters.front().height() / geometry->width;
      for (std::size_t i = 0; i < posters.size(); ++i) {
        const double spot = 0.035 + width_fraction * 0.5 + static_cast<double>(i) * (width_fraction + 0.03);
        auto* action = paste->menu()->findChild<QAction*>(QStringLiteral("perspectivePasteSource%1").arg(i));
        CHECK(action != nullptr && paste->isEnabled());
        action->trigger();
        QApplication::processEvents();
        scale->setValue(static_cast<int>(std::lround(geometry->height * kPosterHeight / posters[i].height() * 100.0)));
        QApplication::processEvents();
        drag(*view, at(0.5, 0.5), at(spot, 0.46));
        if (i + 1 < posters.size()) {
          QTest::keyClick(view, Qt::Key_Return);
          QApplication::processEvents();
        }
      }
      // Park the pointer off the image so no hover outline shows.
      send_mouse(*view, QEvent::MouseMove, QPoint(6, 6), Qt::NoButton, Qt::NoButton);
      QApplication::processEvents();
      process_events_for(200);
      save_readme_shot("shot_readme_perspective_planes", dialog->grab().toImage());
      captured = true;
    } catch (...) {
      error = std::current_exception();
    }
    if (dialog != nullptr) dialog->reject();
  });
  const auto result = patchy::ui::run_perspective_planes_workspace(&theme_host, std::move(input));
  if (error) std::rethrow_exception(error);
  CHECK(captured);
  CHECK(!result.accepted);
}

// --- Flame: the word FIRE burning along its own outline ---------------------

constexpr int kFireWidth = 1400;
constexpr int kFireHeight = 800;

// A glyph outline as Paths-panel subpaths: each cubic segment's control points become
// its end anchors' handles, and the closing point folds into the first anchor.
patchy::VectorPath vector_path_from_outline(const QPainterPath& outline) {
  const auto corner = [](double x, double y) {
    patchy::PathAnchor anchor;
    anchor.anchor_x = anchor.in_x = anchor.out_x = x;
    anchor.anchor_y = anchor.in_y = anchor.out_y = y;
    return anchor;
  };
  patchy::VectorPath path;
  const auto close_last = [&path] {
    if (path.subpaths.empty()) return;
    auto& anchors = path.subpaths.back().anchors;
    if (anchors.size() > 2 && std::abs(anchors.front().anchor_x - anchors.back().anchor_x) < 1e-6 &&
        std::abs(anchors.front().anchor_y - anchors.back().anchor_y) < 1e-6) {
      anchors.front().in_x = anchors.back().in_x;
      anchors.front().in_y = anchors.back().in_y;
      anchors.pop_back();
    }
  };
  for (int i = 0; i < outline.elementCount(); ++i) {
    const auto element = outline.elementAt(i);
    if (element.isMoveTo()) {
      close_last();
      path.subpaths.emplace_back();
      path.subpaths.back().anchors.push_back(corner(element.x, element.y));
    } else if (element.isLineTo()) {
      path.subpaths.back().anchors.push_back(corner(element.x, element.y));
    } else if (element.isCurveTo() && i + 2 < outline.elementCount()) {
      const auto second = outline.elementAt(i + 1);
      const auto end = outline.elementAt(i + 2);
      auto& anchors = path.subpaths.back().anchors;
      anchors.back().out_x = element.x;
      anchors.back().out_y = element.y;
      auto next = corner(end.x, end.y);
      next.in_x = second.x;
      next.in_y = second.y;
      anchors.push_back(next);
      i += 2;
    }
  }
  close_last();
  return path;
}

bool same_rgba_pixels(const patchy::PixelBuffer& a, const patchy::PixelBuffer& b) {
  if (a.width() != b.width() || a.height() != b.height()) return false;
  for (int y = 0; y < a.height(); ++y) {
    if (std::memcmp(a.pixel(0, y), b.pixel(0, y), static_cast<std::size_t>(a.width()) * 4U) != 0) return false;
  }
  return true;
}

// Flame following a path: the word FIRE in charred Arial Black on a night backdrop, its
// outline saved in the Paths panel, and the Flame dialog previewing Campfire along that
// path, live on the canvas and in the dialog's own preview.
void shot_readme_flame() {
  patchy::test::register_test_fonts(patchy::test::TestFontRole::ArialBlack);
  // Offscreen, ariblk.ttf registers as the Black style of Arial (its typographic family).
  const bool black_style = QFontDatabase::styles(QStringLiteral("Arial")).contains(QStringLiteral("Black"));
  if (!black_style && !QFontDatabase::families().contains(QStringLiteral("Arial Black"))) {
    patchy::test::skip("Arial Black is not installed");
  }
  const auto arial_black = [black_style](int pixel_size) {
    QFont font = black_style ? QFontDatabase::font(QStringLiteral("Arial"), QStringLiteral("Black"), 12)
                             : QFont(QStringLiteral("Arial Black"));
    font.setPixelSize(pixel_size);
    return font;
  };
  const auto letters = arial_black(300);
  QPainterPath outline;
  outline.addText(QPointF(0.0, 0.0), letters, QStringLiteral("FIRE"));
  const auto glyphs = outline.boundingRect();
  outline.translate(50.0 - glyphs.left(), 560.0 - glyphs.bottom());
  const auto box = outline.boundingRect();

  QImage night(kFireWidth, kFireHeight, QImage::Format_RGBA8888);
  {
    QPainter painter(&night);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    QLinearGradient sky(0.0, 0.0, 0.0, kFireHeight);
    sky.setColorAt(0.0, QColor(5, 5, 11));
    sky.setColorAt(0.6, QColor(13, 8, 11));
    sky.setColorAt(1.0, QColor(32, 11, 6));
    painter.fillRect(night.rect(), sky);
    QRadialGradient warmth(box.center() + QPointF(0.0, 140.0), 780.0);
    warmth.setColorAt(0.0, QColor(140, 40, 8, 120));
    warmth.setColorAt(1.0, QColor(140, 40, 8, 0));
    painter.fillRect(night.rect(), warmth);
    QLinearGradient charcoal(0.0, box.top(), 0.0, box.bottom());
    charcoal.setColorAt(0.0, QColor(20, 9, 8));
    charcoal.setColorAt(1.0, QColor(66, 22, 10));
    painter.fillPath(outline, charcoal);
    auto tagline = arial_black(26);
    tagline.setLetterSpacing(QFont::AbsoluteSpacing, 9.0);
    painter.setFont(tagline);
    painter.setPen(QColor(196, 132, 88));
    painter.drawText(QPointF(box.left() + 6.0, box.bottom() + 78.0), QStringLiteral("FILTER  /  RENDER  /  FLAME"));
  }
  auto document = patchy::ui::document_from_qimage(night, "Night");
  patchy::DocumentPath fire_path(document.allocate_path_id(), "FIRE outline", patchy::DocumentPathKind::Saved,
                                 vector_path_from_outline(outline));
  fire_path.mark_dirty();
  document.add_path(std::move(fire_path));

  patchy::ui::MainWindow window;
  show_readme_shot_window(window);
  window.resize(1920, 1080);
  QApplication::processEvents();
  window.add_document_session(std::move(document), QStringLiteral("fire.psd"));
  close_untitled_start_tab(window);
  require_action(window, "viewFitOnScreenAction")->trigger();
  patchy::ui::MainWindowTestAccess::show_panel(window, QStringLiteral("pathsDock"));
  patchy::ui::MainWindowTestAccess::refresh_paths_panel(window);
  auto* paths_list = window.findChild<QListWidget*>(QStringLiteral("pathsList"));
  CHECK(paths_list != nullptr && paths_list->count() == 1);
  paths_list->setCurrentRow(0);
  QApplication::processEvents();

  const auto layer_pixels = [&window] {
    const auto& staged = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
    const auto active = staged.active_layer_id();
    const auto* layer = active.has_value() ? staged.find_layer(*active) : nullptr;
    CHECK(layer != nullptr);
    return layer->pixels();
  };
  const auto original = layer_pixels();

  bool captured = false;
  std::exception_ptr error;
  QTimer::singleShot(0, &window, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyFilterDialog"));
    try {
      CHECK(dialog != nullptr);
      auto* preset = dialog->findChild<QComboBox*>(QStringLiteral("filterFlameColorsCombo"));
      auto* mode = dialog->findChild<QComboBox*>(QStringLiteral("filterFlameModeCombo"));
      auto* rise = dialog->findChild<QComboBox*>(QStringLiteral("filterFlameRiseCombo"));
      auto* length = dialog->findChild<QSpinBox*>(QStringLiteral("filterLengthSpin"));
      auto* core = dialog->findChild<QSpinBox*>(QStringLiteral("filterCoreSpin"));
      auto* density = dialog->findChild<QSpinBox*>(QStringLiteral("filterDensitySpin"));
      auto* glow = dialog->findChild<QSpinBox*>(QStringLiteral("filterGlowSpin"));
      auto* sparks = dialog->findChild<QSpinBox*>(QStringLiteral("filterSparksSpin"));
      auto* notice = dialog->findChild<QLabel*>(QStringLiteral("filterDialogNotice"));
      CHECK(preset != nullptr && mode != nullptr && rise != nullptr && length != nullptr && core != nullptr && density != nullptr &&
            glow != nullptr && sparks != nullptr && notice != nullptr);
      preset->setCurrentIndex(preset->findData(QStringLiteral("classic")));
      mode->setCurrentIndex(mode->findData(QStringLiteral("follow_path")));
      rise->setCurrentIndex(rise->findData(QStringLiteral("from_path")));
      length->setValue(18);
      density->setValue(70);
      core->setValue(65);
      glow->setValue(140);
      sparks->setValue(160);
      QApplication::processEvents();
      CHECK(!notice->isVisibleTo(dialog));
      dialog->resize(900, 560);
      CHECK(process_events_until([&] { return !same_rgba_pixels(layer_pixels(), original); }, 60000));
      // Let the last queued preview land on the canvas and in the dialog.
      process_events_for(2500);
      // Far below and right of the letters nothing burns.
      CHECK(std::memcmp(layer_pixels().pixel(kFireWidth - 20, kFireHeight - 20),
                        original.pixel(kFireWidth - 20, kFireHeight - 20), 4) == 0);
      reset_readme_status_bar(window);
      const auto framed = readme_captioned_dialog(*dialog, window.windowIcon());
      const QPoint offset(window.width() - framed.width() - 14, window.height() - framed.height() - 44);
      auto base = window.grab().toImage();
      draw_readme_overlay(base, framed, offset);
      save_readme_shot("shot_readme_flame", base);
      captured = true;
    } catch (...) {
      error = std::current_exception();
    }
    if (dialog != nullptr) dialog->reject();
  });
  require_action(window, "filterAction_patchy_filters_flame")->trigger();
  QApplication::processEvents();
  if (error) std::rethrow_exception(error);
  CHECK(captured);
  CHECK(same_rgba_pixels(layer_pixels(), original));
}

}  // namespace

std::vector<patchy::test::TestCase> readme_screenshot_tests_part3() {
  return {
      {"shot_readme_web_export", shot_readme_web_export},
      {"shot_readme_perspective_planes", shot_readme_perspective_planes},
      {"shot_readme_flame", shot_readme_flame},
  };
}
