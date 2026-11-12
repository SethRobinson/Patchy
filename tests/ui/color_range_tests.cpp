// Select > Color Range... (GitHub issue 709; docs/color-range.md): the script call, the
// dialog's sampling, preview and commit, and the cut-out it is for.

#include "core/color_range.hpp"
#include "core/document.hpp"
#include "core/layer.hpp"
#include "core/pixel_depth.hpp"
#include "scripting_test_support.hpp"
#include "test_harness.hpp"
#include "ui/app_settings.hpp"
#include "ui/canvas_types.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/color_range_dialog.hpp"
#include "ui/main_window.hpp"
#include "ui/script_engine.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QImage>
#include <QPushButton>
#include <QRegion>
#include <QScopeGuard>
#include <QSpinBox>
#include <QTest>
#include <QWidget>

#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace patchy::test::ui;
using namespace patchy::test::ui::scripting_support;
using patchy::ui::MainWindowTestAccess;
using patchy::ui::SelectionMode;

const QString kDialogName = QStringLiteral("patchyColorRangeDialog");

void print_backlog(patchy::ui::MainWindow& window) {
  for (const auto& line : window.script_engine_host().message_backlog()) {
    std::cout << "  script: " << line.toStdString() << std::endl;
  }
}

// Four 40 px squares on a new, otherwise clear layer over the white background: red, blue,
// gray and a duller red, at x = 20, 80, 140 and 200 (y = 20). One setPixels block: a
// fillRect outside a new layer's first rectangle would be clipped to it.
const char* const kSquaresScript = R"JS(
  var doc = app.activeDocument;
  var squares = doc.addLayer('Squares');
  (function () {
    var W = doc.width, H = doc.height, px = new Uint8Array(W * H * 4);
    function square(left, r, g, b) {
      for (var y = 20; y < 60; y++) {
        for (var x = left; x < left + 40; x++) {
          var i = (y * W + x) * 4;
          px[i] = r; px[i + 1] = g; px[i + 2] = b; px[i + 3] = 255;
        }
      }
    }
    square(20, 255, 0, 0);
    square(80, 0, 0, 255);
    square(140, 128, 128, 128);
    square(200, 192, 64, 64);
    squares.setPixels({x: 0, y: 0, width: W, height: H, data: px.buffer});
  })();
  doc.activeLayer = squares;
)JS";

// The dialog's controls, found by object name. Null members fail the calling test.
struct ColorRangeControls {
  QComboBox* mode{};
  QSpinBox* fuzziness{};
  QWidget* preview{};
  QWidget* strip{};
  QPushButton* pick{};
  QPushButton* add{};
  QPushButton* subtract{};
  QPushButton* clear{};
  QComboBox* result{};
  QCheckBox* sample_all{};
  QCheckBox* invert{};

  explicit ColorRangeControls(QDialog& dialog)
      : mode(dialog.findChild<QComboBox*>(QStringLiteral("colorRangeModeCombo"))),
        fuzziness(dialog.findChild<QSpinBox*>(QStringLiteral("colorRangeFuzzinessSpin"))),
        preview(dialog.findChild<QWidget*>(QStringLiteral("colorRangePreview"))),
        strip(dialog.findChild<QWidget*>(QStringLiteral("colorRangeSampleStrip"))),
        pick(dialog.findChild<QPushButton*>(QStringLiteral("colorRangePickButton"))),
        add(dialog.findChild<QPushButton*>(QStringLiteral("colorRangeAddButton"))),
        subtract(dialog.findChild<QPushButton*>(QStringLiteral("colorRangeSubtractButton"))),
        clear(dialog.findChild<QPushButton*>(QStringLiteral("colorRangeClearSamplesButton"))),
        result(dialog.findChild<QComboBox*>(QStringLiteral("colorRangeResultCombo"))),
        sample_all(dialog.findChild<QCheckBox*>(QStringLiteral("colorRangeSampleAllLayersCheck"))),
        invert(dialog.findChild<QCheckBox*>(QStringLiteral("colorRangeInvertCheck"))) {}

  [[nodiscard]] bool complete() const {
    return mode != nullptr && fuzziness != nullptr && preview != nullptr && strip != nullptr && pick != nullptr &&
           add != nullptr && subtract != nullptr && clear != nullptr && result != nullptr && sample_all != nullptr &&
           invert != nullptr;
  }
  [[nodiscard]] int selected() const { return preview->property("selectedPixels").toInt(); }
  [[nodiscard]] int revision() const { return preview->property("maskRevision").toInt(); }
  [[nodiscard]] int samples() const { return strip->property("sampleCount").toInt(); }
};

// The same set of pixels, however each region lists its rectangles.
bool same_region(const QRegion& region, const QRegion& expected) {
  return region.xored(expected).isEmpty();
}

void click_document_point(patchy::ui::CanvasWidget& canvas, QPoint document_point,
                          Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QTest::mouseClick(&canvas, Qt::LeftButton, modifiers, widget_center_of_document_pixel(canvas, document_point));
  QApplication::processEvents();
}

// doc.selection.selectColorRange: sampled colors, the fixed ranges, the soft edge, every
// operation, the active layer alone, 16 and 32-bit documents, and bad options.
void ui_script_select_color_range_selects_by_color() {
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::set_deep_editing_override(true);
  const auto restore_deep_editing = qScopeGuard([] { patchy::set_deep_editing_override(std::nullopt); });
  const bool ran = run_script(window, QString::fromLatin1(kSquaresScript) + QStringLiteral(R"JS(
    var sel = doc.selection;
    function box() { var b = sel.bounds; return b ? b.x + ',' + b.y + ',' + b.width + 'x' + b.height : 'none'; }
    function at(x, y) { return new Uint8Array(sel.getMask({x: x, y: y, width: 1, height: 1}).data)[0]; }
    sel.selectColorRange({colors: ['#ff0000'], fuzziness: 0});
    console.log('red=' + box());
    sel.selectColorRange({colors: ['#0000ff'], operation: 'add'});
    console.log('add=' + box());
    sel.selectColorRange({colors: ['#ff0000'], operation: 'subtract'});
    console.log('subtract=' + box());
    sel.selectColorRange({range: 'blues', fuzziness: 0, operation: 'intersect'});
    console.log('intersect=' + box());
    sel.selectColorRange({range: 'reds'});
    console.log('reds=' + box() + ' gray=' + at(160, 40) + ' white=' + at(5, 5));
    sel.selectColorRange({colors: ['#ffffff'], invert: true});
    console.log('invert=' + box() + ' white=' + at(5, 5) + ' red=' + at(40, 40));
    sel.selectColorRange({range: 'highlights', fuzziness: 0});
    console.log('highlights white=' + at(5, 5) + ' red=' + at(40, 40) + ' gray=' + at(160, 40));
    sel.selectColorRange({range: 'shadows', fuzziness: 0});
    console.log('shadows blue=' + at(100, 40) + ' red=' + at(40, 40) + ' white=' + at(5, 5));
    sel.selectColorRange({colors: ['#ff0000'], fuzziness: 200});
    var soft = at(220, 40);
    console.log('soft red=' + at(40, 40) + ' partial=' + (soft > 0 && soft < 255) + ' gray=' + at(160, 40));
    sel.selectColorRange({colors: ['#ff0000', '#0000ff'], subtractColors: ['#0000ff'], fuzziness: 10});
    console.log('carved=' + box());
    sel.selectColorRange({colors: ['#ffffff'], sampleAllLayers: false});
    console.log('layer white=' + sel.exists);
    sel.selectColorRange({colors: ['#ff0000'], sampleAllLayers: false, fuzziness: 0});
    console.log('layer red=' + box());
    sel.deselect();
    sel.selectColorRange({colors: ['#ff0000'], operation: 'subtract'});
    console.log('subtract from none=' + sel.exists);

    var errors = [];
    [function () { sel.selectColorRange({}); },
     function () { sel.selectColorRange({range: 'skin'}); },
     function () { sel.selectColorRange({colors: ['#ff0000'], fuzziness: 201}); },
     function () { sel.selectColorRange({colors: ['#ff0000'], fuzziness: 1.5}); },
     function () { sel.selectColorRange({colors: '#ff0000'}); },
     function () { sel.selectColorRange({colors: ['not a color']}); },
     function () { sel.selectColorRange({colors: ['#ff0000'], operation: 'xor'}); },
     function () { sel.selectColorRange({colors: ['#ff0000'], faces: true}); },
     function () { sel.selectColorRange({subtractColors: ['#ff0000']}); }].forEach(function (f) {
      try { f(); errors.push('no-throw'); } catch (e) { errors.push(e.message.length > 0 ? 'thrown' : 'empty'); }
    });
    console.log('errors=' + errors.join(','));

    doc.convertBitDepth(16);
    sel.selectColorRange({colors: ['#ff0000'], fuzziness: 0});
    console.log('deep16=' + doc.bitDepth + ' ' + box());
    doc.convertBitDepth(32);
    sel.selectColorRange({colors: ['#ff0000'], fuzziness: 4});
    console.log('deep32=' + doc.bitDepth + ' ' + box());
    sel.selectColorRange({range: 'blues'});
    console.log('deep32 blues=' + box());
  )JS"));
  CHECK(ran);
  bool all_found = ran;
  const auto expect = [&](const QString& line) {
    if (!backlog_contains(window, line)) {
      std::cout << "  missing: " << line.toStdString() << std::endl;
      all_found = false;
    }
  };
  expect(QStringLiteral("red=20,20,40x40"));
  expect(QStringLiteral("add=20,20,100x40"));
  expect(QStringLiteral("subtract=80,20,40x40"));
  expect(QStringLiteral("intersect=80,20,40x40"));
  expect(QStringLiteral("reds=20,20,220x40 gray=0 white=0"));
  expect(QStringLiteral("invert=20,20,220x40 white=0 red=255"));
  expect(QStringLiteral("highlights white=255 red=0 gray=0"));
  expect(QStringLiteral("shadows blue=255 red=255 white=0"));
  expect(QStringLiteral("soft red=255 partial=true gray=0"));
  expect(QStringLiteral("carved=20,20,40x40"));
  expect(QStringLiteral("layer white=false"));
  expect(QStringLiteral("layer red=20,20,40x40"));
  expect(QStringLiteral("subtract from none=false"));
  expect(QStringLiteral("errors=thrown,thrown,thrown,thrown,thrown,thrown,thrown,thrown,thrown"));
  expect(QStringLiteral("deep16=16 20,20,40x40"));
  expect(QStringLiteral("deep32=32 20,20,40x40"));
  expect(QStringLiteral("deep32 blues=80,20,40x40"));
  if (!all_found) {
    print_backlog(window);
  }
  CHECK(all_found);
}

// The dialog from the Select menu: canvas clicks sample (plain, Shift, Alt), each change
// rebuilds the bounded preview once, nothing touches the selection before OK, OK is one
// undo step, Cancel changes nothing, and the Result choice combines with the selection.
void ui_color_range_dialog_samples_previews_and_commits() {
  SettingsValueRestorer fuzziness_key{QString::fromLatin1(patchy::ui::kColorRangeFuzzinessKey)};
  SettingsValueRestorer sample_all_key{QString::fromLatin1(patchy::ui::kColorRangeSampleAllLayersKey)};
  patchy::ui::app_settings().remove(QString::fromLatin1(patchy::ui::kColorRangeFuzzinessKey));
  patchy::ui::app_settings().remove(QString::fromLatin1(patchy::ui::kColorRangeSampleAllLayersKey));
  patchy::ui::MainWindow window;
  show_window_empty(window);
  MainWindowTestAccess::create_document(window, 300, 200);
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  CHECK(run_script(window, QString::fromLatin1(kSquaresScript)));
  QApplication::processEvents();
  auto* action = require_action(window, "selectColorRangeAction");
  CHECK(action->isEnabled());
  CHECK(action->menuRole() == QAction::NoRole);
  CHECK(action->shortcut().isEmpty());
  CHECK(!canvas->has_selection());

  // 1. Sample on the canvas, watch the preview, OK.
  bool drove = false;
  when_dialog_visible(kDialogName, [&](QDialog& dialog) {
    const ColorRangeControls controls(dialog);
    CHECK(controls.complete());
    if (!controls.complete()) {
      dialog.reject();
      return;
    }
    CHECK(clipped_labels(dialog).isEmpty());
    CHECK(controls.fuzziness->value() == patchy::kDefaultColorRangeFuzziness);
    CHECK(controls.sample_all->isChecked());
    CHECK(controls.mode->currentData().toInt() == static_cast<int>(patchy::ColorRangeMode::SampledColors));
    CHECK(controls.result->currentData().toInt() == static_cast<int>(SelectionMode::Replace));
    // The foreground color is the first sample; nothing in the picture is black.
    CHECK(controls.samples() == 1);
    CHECK(controls.selected() == 0);
    CHECK(canvas->has_transient_read_interaction());

    auto revision = controls.revision();
    click_document_point(*canvas, QPoint(40, 40));  // the red square: Pick starts over
    CHECK(controls.samples() == 1);
    CHECK(controls.revision() == revision + 1);
    const auto red_only = controls.selected();
    CHECK(red_only > 0);

    click_document_point(*canvas, QPoint(100, 40), Qt::ShiftModifier);  // Shift adds blue
    CHECK(controls.samples() == 2);
    const auto red_and_blue = controls.selected();
    CHECK(red_and_blue > red_only);

    click_document_point(*canvas, QPoint(100, 40), Qt::AltModifier);  // Alt takes blue out
    CHECK(controls.samples() == 3);
    CHECK(controls.selected() == red_only);

    // A click on a clear part of the sampled picture samples nothing (the layer alone).
    controls.sample_all->setChecked(false);
    QApplication::processEvents();
    revision = controls.revision();
    click_document_point(*canvas, QPoint(280, 150));
    CHECK(controls.samples() == 3);
    CHECK(controls.revision() == revision);
    controls.sample_all->setChecked(true);
    QApplication::processEvents();

    // The Add button does what Shift does; the preview takes clicks too.
    controls.add->click();
    CHECK(controls.add->isChecked());
    click_document_point(*canvas, QPoint(160, 40));  // gray
    if (controls.samples() != 4) {
      std::cout << "  after Add click: samples=" << controls.samples() << " selected=" << controls.selected()
                << " picker armed=" << canvas->has_transient_read_interaction() << std::endl;
    }
    CHECK(controls.samples() == 4);
    CHECK(controls.selected() > red_only);
    controls.pick->click();
    // The 300 x 200 document is its own preview copy, centered in the 300 px box behind a
    // 1 px frame: document point (40, 40) is at (41, 91).
    QTest::mouseClick(controls.preview, Qt::LeftButton, Qt::NoModifier, QPoint(1 + 40, 1 + 50 + 40));
    QApplication::processEvents();
    CHECK(controls.samples() == 1);
    CHECK(controls.selected() == red_only);

    // Each setting change is one rebuild of the bounded preview, and none on a repaint.
    revision = controls.revision();
    controls.fuzziness->setValue(200);
    CHECK(controls.revision() == revision + 1);
    CHECK(controls.selected() > red_only);  // the duller red joins, softly
    controls.fuzziness->setValue(0);
    CHECK(controls.selected() == red_only);
    controls.invert->setChecked(true);
    const auto inverted = controls.selected();
    CHECK(inverted > red_only);
    controls.invert->setChecked(false);
    revision = controls.revision();
    controls.preview->repaint();
    dialog.repaint();
    QApplication::processEvents();
    CHECK(controls.revision() == revision);

    // A fixed range needs no samples: the sampling controls go quiet and so does the canvas.
    controls.mode->setCurrentIndex(controls.mode->findData(static_cast<int>(patchy::ColorRangeMode::Blues)));
    QApplication::processEvents();
    CHECK(!controls.pick->isEnabled());
    CHECK(!controls.clear->isEnabled());
    CHECK(!canvas->has_transient_read_interaction());
    CHECK(controls.selected() > 0);
    controls.mode->setCurrentIndex(
        controls.mode->findData(static_cast<int>(patchy::ColorRangeMode::SampledColors)));
    QApplication::processEvents();
    CHECK(controls.pick->isEnabled());
    CHECK(canvas->has_transient_read_interaction());
    CHECK(controls.selected() == red_only);

    // Nothing reaches the document's selection before OK.
    CHECK(!canvas->has_selection());
    save_widget_artifact("ui_color_range_dialog", dialog);
    drove = true;
    dialog.accept();
  });
  action->trigger();
  QApplication::processEvents();
  CHECK(drove);
  CHECK(!canvas->has_transient_read_interaction());
  CHECK(canvas->has_selection());
  CHECK(same_region(canvas->selected_document_region(), QRegion(20, 20, 40, 40)));
  CHECK(!canvas->selection_has_partial_alpha());
  CHECK(patchy::ui::app_settings().value(QString::fromLatin1(patchy::ui::kColorRangeFuzzinessKey)).toInt() == 0);
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(!canvas->has_selection());
  require_action_by_text(window, QStringLiteral("Redo"))->trigger();
  QApplication::processEvents();
  CHECK(same_region(canvas->selected_document_region(), QRegion(20, 20, 40, 40)));

  // 2. Cancel leaves the selection as it was, after samples and setting changes.
  drove = false;
  when_dialog_visible(kDialogName, [&](QDialog& dialog) {
    const ColorRangeControls controls(dialog);
    CHECK(controls.complete());
    if (controls.complete()) {
      CHECK(controls.fuzziness->value() == 0);  // remembered
      click_document_point(*canvas, QPoint(100, 40));
      controls.fuzziness->setValue(120);
      controls.invert->setChecked(true);
      CHECK(controls.selected() > 0);
      drove = true;
    }
    // Escape on the canvas cancels: the canvas is a sibling window, so the key would
    // never reach the dialog by itself.
    QTest::keyClick(canvas, Qt::Key_Escape);
    QApplication::processEvents();
    const bool closed = !dialog.isVisible();
    if (!closed) {
      dialog.reject();
    }
    CHECK(closed);
  });
  action->trigger();
  QApplication::processEvents();
  CHECK(drove);
  CHECK(!canvas->has_transient_read_interaction());
  CHECK(same_region(canvas->selected_document_region(), QRegion(20, 20, 40, 40)));
  CHECK(patchy::ui::app_settings().value(QString::fromLatin1(patchy::ui::kColorRangeFuzzinessKey)).toInt() == 0);

  // 3. Result: Add to Selection with a fixed range, then Subtract, each one undo step.
  const auto run_with_result = [&](patchy::ColorRangeMode mode, SelectionMode result) {
    bool ran = false;
    when_dialog_visible(kDialogName, [&](QDialog& dialog) {
      const ColorRangeControls controls(dialog);
      CHECK(controls.complete());
      if (!controls.complete()) {
        dialog.reject();
        return;
      }
      controls.mode->setCurrentIndex(controls.mode->findData(static_cast<int>(mode)));
      controls.result->setCurrentIndex(controls.result->findData(static_cast<int>(result)));
      ran = true;
      dialog.accept();
    });
    action->trigger();
    QApplication::processEvents();
    CHECK(ran);
  };
  run_with_result(patchy::ColorRangeMode::Blues, SelectionMode::Add);
  CHECK(same_region(canvas->selected_document_region(), QRegion(20, 20, 40, 40).united(QRegion(80, 20, 40, 40))));
  run_with_result(patchy::ColorRangeMode::Reds, SelectionMode::Subtract);
  CHECK(same_region(canvas->selected_document_region(), QRegion(80, 20, 40, 40)));
  run_with_result(patchy::ColorRangeMode::Reds, SelectionMode::Intersect);
  CHECK(!canvas->has_selection());
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(same_region(canvas->selected_document_region(), QRegion(80, 20, 40, 40)));
}

// What the command is for (GitHub issue 709): a subject on a plain, slightly uneven
// background. One click on the background, a little Fuzziness, OK and Delete leave the
// subject on transparency with a soft edge. The artifacts are the card's images.
void ui_color_range_cuts_out_a_plain_background() {
  SettingsValueRestorer fuzziness_key{QString::fromLatin1(patchy::ui::kColorRangeFuzzinessKey)};
  SettingsValueRestorer sample_all_key{QString::fromLatin1(patchy::ui::kColorRangeSampleAllLayersKey)};
  patchy::ui::app_settings().remove(QString::fromLatin1(patchy::ui::kColorRangeFuzzinessKey));
  patchy::ui::app_settings().remove(QString::fromLatin1(patchy::ui::kColorRangeSampleAllLayersKey));
  patchy::ui::MainWindow window;
  show_window_empty(window);
  MainWindowTestAccess::create_document(window, 480, 320);
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  // A shaded orange ball with a green leaf on a blue backdrop that drifts a few levels.
  const bool painted = run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var W = doc.width, H = doc.height;
    var photo = doc.addLayer('Photo');
    var px = new Uint8Array(W * H * 4);
    function clamp(v) { return Math.max(0, Math.min(255, Math.round(v))); }
    for (var y = 0; y < H; y++) {
      for (var x = 0; x < W; x++) {
        var drift = (y / H) * 14 - 7 + ((x * 7 + y * 13) % 5) - 2;
        var r = 52 + drift, g = 118 + drift, b = 196 + drift;
        var dx = x - 240, dy = y - 170, d = Math.sqrt(dx * dx + dy * dy);
        var lx = x - 262, ly = y - 62;
        var leaf = (lx * lx) / (44 * 44) + (ly * ly) / (17 * 17);
        var cover = 0, cr = 0, cg = 0, cb = 0;
        if (leaf < 1.15) {
          cover = Math.min(1, (1.15 - leaf) / 0.15);
          cr = 60; cg = 150 - 40 * leaf; cb = 50;
        }
        if (d < 101) {
          var ball = Math.min(1, 101 - d);
          var lit = Math.max(0, 1 - Math.sqrt((dx + 35) * (dx + 35) + (dy + 40) * (dy + 40)) / 150);
          var br = 205 + 50 * lit, bg = 95 + 90 * lit, bb = 20 + 40 * lit;
          cr = cr * cover * (1 - ball) + br * ball;
          cg = cg * cover * (1 - ball) + bg * ball;
          cb = cb * cover * (1 - ball) + bb * ball;
          cover = cover * (1 - ball) + ball;
          if (cover > 0) { cr /= cover; cg /= cover; cb /= cover; }
        }
        var i = (y * W + x) * 4;
        px[i] = clamp(r * (1 - cover) + cr * cover);
        px[i + 1] = clamp(g * (1 - cover) + cg * cover);
        px[i + 2] = clamp(b * (1 - cover) + cb * cover);
        px[i + 3] = 255;
      }
    }
    photo.setPixels({x: 0, y: 0, width: W, height: H, data: px.buffer});
    doc.layers.forEach(function (layer) { if (layer.id !== photo.id) { layer.visible = false; } });
    doc.activeLayer = photo;
  )JS"));
  CHECK(painted);
  if (!painted) {
    print_backlog(window);
    return;
  }
  QApplication::processEvents();
  require_action(window, "viewFitOnScreenAction")->trigger();
  QApplication::processEvents();
  save_widget_artifact("ui_color_range_cutout_before", *canvas);

  bool drove = false;
  when_dialog_visible(kDialogName, [&](QDialog& dialog) {
    const ColorRangeControls controls(dialog);
    CHECK(controls.complete());
    if (!controls.complete()) {
      dialog.reject();
      return;
    }
    click_document_point(*canvas, QPoint(40, 160));  // the backdrop
    controls.fuzziness->setValue(60);
    QApplication::processEvents();
    CHECK(controls.samples() == 1);
    CHECK(controls.selected() > 0);
    save_widget_artifact("ui_color_range_cutout_dialog", dialog);
    drove = true;
    dialog.accept();
  });
  require_action(window, "selectColorRangeAction")->trigger();
  QApplication::processEvents();
  CHECK(drove);
  CHECK(canvas->has_selection());
  // The backdrop is selected everywhere, the ball and the leaf are not, and the rim between
  // them is partly selected.
  const auto coverage = canvas->selection_coverage_buffer(QRect(0, 0, 480, 320));
  const auto at = [&coverage](int x, int y) { return static_cast<int>(*coverage.pixel(x, y)); };
  CHECK(at(40, 160) == 255);
  CHECK(at(5, 5) == 255);
  CHECK(at(470, 310) == 255);
  CHECK(at(240, 170) == 0);
  CHECK(at(262, 62) == 0);
  CHECK(canvas->selection_has_partial_alpha());
  // The rim between them is soft: some of its pixels are partly selected.
  int partial = 0;
  for (int y = 60; y < 280; ++y) {
    for (int x = 130; x < 350; ++x) {
      partial += at(x, y) > 0 && at(x, y) < 255 ? 1 : 0;
    }
  }
  CHECK(partial >= 20);
  save_widget_artifact("ui_color_range_cutout_selection", *canvas);

  require_hotkey_action(window, QStringLiteral("layer.clear"))->trigger();
  QApplication::processEvents();
  const auto& document = MainWindowTestAccess::document(window);
  const auto active = std::as_const(document).active_layer_id();
  CHECK(active.has_value());
  const auto* layer = active.has_value() ? std::as_const(document).find_layer(*active) : nullptr;
  CHECK(layer != nullptr);
  if (layer != nullptr) {
    const auto& pixels = layer->pixels();
    const auto bounds = layer->bounds();
    const auto alpha = [&](int x, int y) {
      if (x < bounds.x || y < bounds.y || x >= bounds.x + bounds.width || y >= bounds.y + bounds.height) {
        return 0;
      }
      return static_cast<int>(pixels.pixel(x - bounds.x, y - bounds.y)[3]);
    };
    CHECK(alpha(40, 160) == 0);    // the backdrop is gone
    CHECK(alpha(5, 5) == 0);
    CHECK(alpha(240, 170) == 255);  // the ball and the leaf stay
    CHECK(alpha(262, 62) == 255);
  }
  save_widget_artifact("ui_color_range_cutout_after", *canvas);
}

}  // namespace

std::vector<patchy::test::TestCase> color_range_ui_tests() {
  return {
      {"ui_script_select_color_range_selects_by_color", ui_script_select_color_range_selects_by_color},
      {"ui_color_range_dialog_samples_previews_and_commits", ui_color_range_dialog_samples_previews_and_commits},
      {"ui_color_range_cuts_out_a_plain_background", ui_color_range_cuts_out_a_plain_background},
  };
}
