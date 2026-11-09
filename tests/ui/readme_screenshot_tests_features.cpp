// README screenshots (shot_readme_*), part 4: feature scenes added for the gallery in
// November 2026 (Rotate View, GEGL filters, Liquify, Remove Object, OpenAI Image Tools).
// The group-wide story lives in the aggregator (readme_screenshot_tests.cpp); helpers
// shared with the other parts live in readme_screenshot_test_support. See
// docs/readme-showcase.md for each scene's composition rules.

#include "core/document.hpp"
#include "filters/filter_registry.hpp"
#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/image_document_io.hpp"
#include "ui/main_window.hpp"
#include "ui/openai_images.hpp"
#include "ui/openai_images_dialog.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "readme_screenshot_test_support.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QPointF>
#include <QPolygonF>
#include <QString>

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

namespace {

using namespace patchy::test::ui;

QImage readme_photo(const char* name) {
  const auto path = patchy::test::committed_format_fixture_path("readme", name);
  QImage image(QString::fromStdString(path.string()));
  CHECK(!image.isNull());
  return image;
}

// Paints one brush stroke through document points with the current tool.
void paint_document_polyline(patchy::ui::CanvasWidget& canvas, const std::vector<QPointF>& points) {
  CHECK(points.size() >= 2);
  const auto at = [&canvas](QPointF point) { return canvas.widget_position_f(point).toPoint(); };
  send_mouse(canvas, QEvent::MouseButtonPress, at(points.front()), Qt::LeftButton, Qt::LeftButton);
  for (std::size_t i = 1; i < points.size(); ++i) {
    send_mouse(canvas, QEvent::MouseMove, at(points[i]), Qt::NoButton, Qt::LeftButton);
  }
  send_mouse(canvas, QEvent::MouseButtonRelease, at(points.back()), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
}

// A gull in flight, two gently curved wings, centered at `center`, `span` wide.
std::vector<QPointF> gull_points(QPointF center, double span) {
  std::vector<QPointF> points;
  for (int i = 0; i <= 16; ++i) {
    const double t = i / 16.0;
    const double x = (t - 0.5) * span;
    const double wing = std::abs(t - 0.5) * 2.0;  // 0 at the body, 1 at each tip
    const double y = -std::sin(wing * 3.14159265358979323846) * span * 0.16 + wing * span * 0.05;
    points.emplace_back(center.x() + x, center.y() + y);
  }
  return points;
}

// Rotate View: the canvas turned 24 degrees the way a sheet of paper turns under a pen,
// with gulls being sketched over the stylized sunset on their own layer. The Rotate View
// tool is active, so the options bar shows the angle and Reset View; the image itself is
// never turned.
void shot_readme_rotate_view() {
  QImage art = readme_photo("stylized_sunset_cc0.png").scaled(1200, 750, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  auto document = patchy::ui::document_from_qimage(art, "Sunset");
  patchy::ui::MainWindow window;
  show_readme_shot_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Sunset Sketch"));
  close_untitled_start_tab(window);
  QApplication::processEvents();
  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  {
    auto& staged = patchy::ui::MainWindowTestAccess::document(window);
    auto* gulls = staged.find_layer(*std::as_const(staged).active_layer_id());
    CHECK(gulls != nullptr);
    gulls->set_name("Gulls");
    patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
  }
  auto* canvas = require_canvas(window);

  canvas->set_view_rotation(-24.0);
  canvas->fit_to_view();
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_brush_size(5);
  canvas->set_brush_softness(20);
  canvas->set_primary_color(QColor(46, 22, 40));
  for (const auto& gull : {std::pair{QPointF(300, 180), 90.0}, std::pair{QPointF(420, 120), 70.0},
                           std::pair{QPointF(520, 200), 60.0}, std::pair{QPointF(860, 150), 80.0},
                           std::pair{QPointF(960, 220), 55.0}, std::pair{QPointF(700, 90), 48.0}}) {
    paint_document_polyline(*canvas, gull_points(gull.first, gull.second));
  }
  require_action_by_text(window, QStringLiteral("Rotate View"))->trigger();
  QApplication::processEvents();
  CHECK(std::abs(canvas->view_rotation() + 24.0) < 0.01);
  reset_readme_status_bar(window);
  save_readme_shot("shot_readme_rotate_view", window.grab().toImage());
  canvas->set_view_rotation(0.0);
}

// GEGL filters: Patchy's own build of GEGL in Patchy's filter dialog, with the live preview
// on the canvas and in the dialog and the GEGL credit line. Stereographic Projection turns
// the San Francisco skyline into a little planet (Mosaic or Cartoon stand in if a GEGL
// build lacks it). Skips in a build without the bundled GEGL.
void shot_readme_gegl() {
  // The skyline beside its mirror image, so the photo's left and right edges match: a little
  // planet wraps the image around 360 degrees, and a plain photo leaves a seam where they meet.
  // The pair is squeezed back to 1200 x 800 so the planet fills the canvas.
  const QImage half = readme_photo("san_francisco_cityscape_cc0.jpg").scaled(600, 800, Qt::IgnoreAspectRatio,
                                                                             Qt::SmoothTransformation);
  QImage photo(1200, 800, QImage::Format_RGB32);
  {
    QPainter painter(&photo);
    painter.drawImage(0, 0, half);
    painter.drawImage(600, 0, half.mirrored(true, false));
  }
  patchy::ui::MainWindow window;
  show_readme_shot_window(window);
  window.add_document_session(patchy::ui::document_from_qimage(photo, "Skyline"), QStringLiteral("San Francisco"));
  close_untitled_start_tab(window);
  require_action(window, "viewFitOnScreenAction")->trigger();
  QApplication::processEvents();
  patchy::ui::MainWindowTestAccess::ensure_gegl_filters_scanned(window);
  const auto registry =
      patchy::ui::MainWindowTestAccess::gegl_filter_registry(window, QStringLiteral("gegl.gaussian-blur"));
  if (registry == nullptr || registry->find("gegl.gaussian-blur") == nullptr) {
    patchy::test::skip("this build has no GEGL of its own");
  }
  QString identifier;
  for (const auto* candidate : {"gegl.stereographic-projection", "gegl.mosaic", "gegl.cartoon"}) {
    if (identifier.isEmpty() && registry->find(candidate) != nullptr) {
      identifier = QString::fromLatin1(candidate);
    }
  }
  CHECK(!identifier.isEmpty());

  bool captured = false;
  int polls = 0;
  QTimer poll;
  poll.setInterval(250);
  QObject::connect(&poll, &QTimer::timeout, &window, guarded([&] {
    ++polls;
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyFilterDialog"));
    if (dialog == nullptr) {
      if (polls > 120) {
        poll.stop();
        if (auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget())) modal->reject();
      }
      return;
    }
    auto* preview = dialog->findChild<QWidget*>(QStringLiteral("filterDialogPreview"));
    const bool rendered =
        preview != nullptr && preview->property("filterDialogRenderedFilterId").toString() == identifier;
    if (polls < 8 || (!rendered && polls < 120)) {
      return;  // let the dialog's preview and the canvas preview land
    }
    poll.stop();
    CHECK(rendered);
    const QPoint offset(window.width() - dialog->width() - 24, 96);
    dialog->move(window.geometry().topLeft() + offset);
    process_events_for(300);
    reset_readme_status_bar(window);
    auto base = window.grab().toImage();
    draw_readme_overlay(base, readme_captioned_dialog(*dialog, window.windowIcon()), offset);
    save_readme_shot("shot_readme_gegl", base);
    captured = true;
    dialog->reject();
  }));
  poll.start();
  patchy::ui::MainWindowTestAccess::apply_filter_with_dialog(window, identifier);
  CHECK(captured);
}

// Liquify on the stylized sunset: the sun bloated, the sky twirled around it and a ridge
// pushed up, with Show Mesh on so the warp reads as a bent grid. Strokes go into the
// dialog's own preview with its real tools.
void shot_readme_liquify() {
  QImage art = readme_photo("stylized_sunset_cc0.png").scaled(1200, 750, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  patchy::ui::MainWindow window;
  show_readme_shot_window(window);
  window.add_document_session(patchy::ui::document_from_qimage(art, "Sunset"), QStringLiteral("Sunset"));
  close_untitled_start_tab(window);
  require_action(window, "viewFitOnScreenAction")->trigger();
  QApplication::processEvents();

  bool captured = false;
  QTimer::singleShot(0, &window, guarded([&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("liquifyDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    dialog->resize(1240, 780);
    process_events_for(200);
    auto* preview = dialog->findChild<QWidget*>(QStringLiteral("liquifyPreview"));
    auto* mesh = dialog->findChild<QCheckBox*>(QStringLiteral("liquifyShowMeshCheck"));
    auto* size = dialog->findChild<QSpinBox*>(QStringLiteral("liquifySizeSpin"));
    CHECK(preview != nullptr && mesh != nullptr && size != nullptr);
    if (preview == nullptr || mesh == nullptr || size == nullptr) {
      dialog->reject();
      return;
    }
    // Where a point of the image (fractions of its size) sits in the preview, which
    // fits the whole image centered.
    const auto at = [preview, &art](double fx, double fy) {
      const double scale = std::min((preview->width() - 24.0) / art.width(), (preview->height() - 24.0) / art.height());
      const double left = (preview->width() - art.width() * scale) / 2.0;
      const double top = (preview->height() - art.height() * scale) / 2.0;
      return QPoint(static_cast<int>(left + fx * art.width() * scale), static_cast<int>(top + fy * art.height() * scale));
    };
    const auto tool = [dialog](const char* name) {
      auto* button = dialog->findChild<QToolButton*>(QLatin1String(name));
      CHECK(button != nullptr);
      if (button != nullptr) button->click();
      QApplication::processEvents();
    };
    const auto hold = [preview](QPoint point, int milliseconds) {
      send_mouse(*preview, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
      process_events_for(milliseconds);
      send_mouse(*preview, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
      QApplication::processEvents();
    };
    // The sun sits a little left of center, a third of the way down.
    tool("liquifyTwirlTool");
    size->setValue(420);
    hold(at(0.47, 0.30), 1400);
    tool("liquifyBloatTool");
    size->setValue(200);
    hold(at(0.47, 0.30), 900);
    tool("liquifyWarpTool");
    size->setValue(160);
    drag(*preview, at(0.78, 0.62), at(0.80, 0.42));
    drag(*preview, at(0.20, 0.70), at(0.16, 0.55));
    mesh->setChecked(true);
    process_events_for(600);
    const QPoint offset((window.width() - dialog->width()) / 2, 70);
    dialog->move(window.geometry().topLeft() + offset);
    process_events_for(200);
    reset_readme_status_bar(window);
    auto base = window.grab().toImage();
    draw_readme_overlay(base, readme_captioned_dialog(*dialog, window.windowIcon()), offset);
    save_readme_shot("shot_readme_liquify", base);
    captured = true;
    dialog->reject();
  }));
  require_action(window, "filterLiquifyAction")->trigger();
  QApplication::processEvents();
  CHECK(captured);
}

// Edit > Remove Object on the CC0 beach photo: a loose box around the walker and his
// shadow, and the fill already in place on the canvas with the dialog's variation,
// Re-roll, tone match and feather beside it. No model: the fill is Patchy's own.
void shot_readme_remove_object() {
  QImage photo = readme_photo("beach_walker_cc0.jpg");
  patchy::ui::MainWindow window;
  show_readme_shot_window(window);
  window.add_document_session(patchy::ui::document_from_qimage(photo, "Beach"), QStringLiteral("Windy Beach"));
  close_untitled_start_tab(window);
  require_action(window, "viewFitOnScreenAction")->trigger();
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  // A loose outline around the walker (about 290..345 x 285..425) and the shadow he casts
  // to the left, the way a lasso would draw it: the fill should not cut into the log or
  // the hillside behind him.
  QImage outline(photo.size(), QImage::Format_Grayscale8);
  outline.fill(0);
  {
    QPainter painter(&outline);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawEllipse(QRectF(303, 281, 30, 32));                        // head
    painter.drawRoundedRect(QRectF(286, 305, 62, 74), 16, 16);            // coat
    painter.drawRoundedRect(QRectF(294, 372, 40, 56), 10, 10);            // legs
    painter.drawPolygon(QPolygonF({QPointF(300, 418), QPointF(224, 424), QPointF(222, 440),
                                   QPointF(300, 436)}));                  // shadow
  }
  patchy::PixelBuffer mask(photo.width(), photo.height(), patchy::PixelFormat::gray8());
  for (int y = 0; y < mask.height(); ++y) {
    auto row = mask.row(y);
    const auto* line = outline.constScanLine(y);
    for (int x = 0; x < mask.width(); ++x) {
      row[static_cast<std::size_t>(x)] = line[x] > 127 ? 255 : 0;
    }
  }
  canvas->replace_selection_from_grayscale(mask, QStringLiteral("Lasso"));
  QApplication::processEvents();
  // The photo as it was, for the Before inset.
  const QRect photo_on_screen =
      canvas->view_transform().widget_bounds_of_document_rect(QRectF(0, 0, photo.width(), photo.height())).toRect();
  const QPoint canvas_origin = canvas->mapTo(&window, QPoint(0, 0));
  const QImage before = canvas->grab(photo_on_screen).toImage();
  // The marching ants would hide the fill: the shot shows the result.
  auto* edges = require_action(window, "viewShowSelectionEdgesAction");
  const bool edges_were_shown = edges->isChecked();
  if (edges_were_shown) {
    edges->trigger();
  }

  bool captured = false;
  QTimer::singleShot(0, &window, guarded([&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyRemoveObjectDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* buttons = dialog->findChild<QDialogButtonBox*>();
    auto* variation = dialog->findChild<QLabel*>(QStringLiteral("removeObjectVariationLabel"));
    CHECK(buttons != nullptr && variation != nullptr);
    CHECK(process_events_until(
        [&] {
          return buttons != nullptr && variation != nullptr && buttons->button(QDialogButtonBox::Ok) != nullptr &&
                 buttons->button(QDialogButtonBox::Ok)->isEnabled() && !variation->text().isEmpty();
        },
        60000));
    process_events_for(300);
    const QPoint offset(window.width() - dialog->width() - 400, window.height() - dialog->height() - 110);
    dialog->move(window.geometry().topLeft() + offset);
    process_events_for(200);
    reset_readme_status_bar(window);
    auto base = window.grab().toImage();
    // A small Before inset in the photo's lower left corner, labeled.
    QImage inset = before.scaledToWidth(360, Qt::SmoothTransformation).convertToFormat(QImage::Format_ARGB32);
    {
      QPainter painter(&inset);
      painter.setRenderHint(QPainter::Antialiasing, true);
      QFont font = window.font();
      font.setPixelSize(15);
      font.setBold(true);
      painter.setFont(font);
      const QRect tag(10, 10, painter.fontMetrics().horizontalAdvance(QStringLiteral("Before")) + 20, 26);
      painter.setPen(Qt::NoPen);
      painter.setBrush(QColor(0, 0, 0, 170));
      painter.drawRoundedRect(tag, 6, 6);
      painter.setPen(Qt::white);
      painter.drawText(tag, Qt::AlignCenter, QStringLiteral("Before"));
    }
    draw_readme_overlay(base, inset,
                        canvas_origin + QPoint(photo_on_screen.left() + 18, photo_on_screen.bottom() - inset.height() - 18));
    draw_readme_overlay(base, readme_captioned_dialog(*dialog, window.windowIcon()), offset);
    save_readme_shot("shot_readme_remove_object", base);
    captured = true;
    dialog->reject();
  }));
  require_action(window, "editRemoveObjectAction")->trigger();
  QApplication::processEvents();
  if (edges_were_shown) {
    edges->trigger();  // persisted view setting: put it back
  }
  CHECK(captured);
}

// A key store that never touches the system's: the key comes from OPENAI_API_KEY.
class ReadmeOpenAiKeyStore final : public patchy::ui::openai::KeyStore {
public:
  [[nodiscard]] bool available() const override { return true; }
  [[nodiscard]] QString load() const override { return {}; }
  bool save(const QString&) override { return true; }
  bool clear() override { return true; }
};

// Plugins > Companions > OpenAI Image Tools via API Key, with a real request: the beach
// photo is the reference (image 1) and the prompt asks for a watercolor of it. Each
// returned picture becomes a new layer; the dialog shows the prompt, the reference, the
// results row and the cost note. It runs only when OPENAI_API_KEY is set (the driver reads
// it from the untracked agents_secret.md), and the key field is never on screen.
void shot_readme_openai_images() {
  if (qEnvironmentVariable("OPENAI_API_KEY").isEmpty()) {
    patchy::test::skip("OPENAI_API_KEY is not set; this scene makes a real, billed OpenAI request");
  }
  patchy::ui::openai::set_key_store_factory_for_testing([] { return std::make_unique<ReadmeOpenAiKeyStore>(); });
  QImage photo = readme_photo("beach_walker_cc0.jpg");
  patchy::ui::MainWindow window;
  show_readme_shot_window(window);
  window.add_document_session(patchy::ui::document_from_qimage(photo, "Beach photo"), QStringLiteral("Windy Beach"));
  close_untitled_start_tab(window);
  require_action(window, "viewFitOnScreenAction")->trigger();
  QApplication::processEvents();

  bool captured = false;
  QTimer::singleShot(0, &window, guarded([&] {
    auto* dialog = qobject_cast<patchy::ui::OpenAiImagesDialog*>(find_top_level_dialog(QStringLiteral("openAiImagesDialog")));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* tabs = dialog->findChild<QTabWidget*>(QStringLiteral("openAiImagesModeTabs"));
    auto* prompt = dialog->findChild<QPlainTextEdit*>(QStringLiteral("openAiImagesPromptEdit"));
    auto* add_layer = dialog->findChild<QAction*>(QStringLiteral("openAiImagesAddLayerAction"));
    auto* create = dialog->findChild<QPushButton*>(QStringLiteral("openAiImagesGenerateButton"));
    auto* two = dialog->findChild<QAbstractButton*>(QStringLiteral("openAiImagesCount2"));
    CHECK(tabs != nullptr && prompt != nullptr && add_layer != nullptr && create != nullptr);
    if (tabs == nullptr || prompt == nullptr || add_layer == nullptr || create == nullptr) {
      dialog->reject();
      return;
    }
    tabs->setCurrentIndex(0);
    add_layer->trigger();
    prompt->setPlainText(QStringLiteral(
        "Repaint image 1 as a loose watercolor illustration on textured paper: the same beach, the lone walker "
        "and his shadow, soft washes of warm gold and grey, visible brush strokes."));
    if (two != nullptr) {
      two->click();
    }
    QApplication::processEvents();
    create->click();
    CHECK(process_events_until([dialog] { return !dialog->busy(); }, 300000));
    CHECK(dialog->result_count() > 0);
    process_events_for(800);
    const QPoint offset(window.width() - dialog->width() - 24, 70);
    dialog->move(window.geometry().topLeft() + offset);
    process_events_for(200);
    reset_readme_status_bar(window);
    auto base = window.grab().toImage();
    draw_readme_overlay(base, dialog->grab().toImage(), offset);
    save_readme_shot("shot_readme_openai_images", base);
    captured = true;
    dialog->close();
  }));
  window.findChild<QAction*>(QStringLiteral("pluginsOpenAiImagesAction"))->trigger();
  QApplication::processEvents();
  patchy::ui::openai::set_key_store_factory_for_testing({});
  CHECK(captured);
}

}  // namespace

std::vector<patchy::test::TestCase> readme_screenshot_tests_part4() {
  return {
      {"shot_readme_rotate_view", shot_readme_rotate_view},
      {"shot_readme_gegl", shot_readme_gegl},
      {"shot_readme_liquify", shot_readme_liquify},
      {"shot_readme_remove_object", shot_readme_remove_object},
      {"shot_readme_openai_images", shot_readme_openai_images},
  };
}
