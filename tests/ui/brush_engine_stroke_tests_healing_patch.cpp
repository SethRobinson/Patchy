// Part 2 of the brush-engine stroke UI test group (see
// brush_engine_stroke_tests.cpp): Spot Healing, Remove Object, the Patch tool,
// and the retouch Sample All Layers option. The heal commits are pinned
// byte-exact on synthetic documents whose uniform surroundings make the
// frequency-separation math collapse to known values.

#include "core/document.hpp"
#include "ui/app_settings.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"

#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "remove_object_fixture.hpp"

#include <QApplication>

#include <functional>
#include <memory>
#include <QTimer>
#include <QSpinBox>
#include <QLabel>
#include <QDialog>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QPoint>
#include <QPushButton>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace {

using namespace patchy::test::ui;

void ui_retouch_sample_all_layers_switches_clone_and_healing_source() {
  patchy::Document document(48, 16, patchy::PixelFormat::rgba8());
  auto base_pixels = solid_pixels(48, 16, patchy::PixelFormat::rgba8(), QColor(200, 50, 25, 255));
  auto* base_detail = base_pixels.pixel(4, 8);
  base_detail[0] = 240;
  base_detail[1] = 90;
  base_detail[2] = 60;
  document.add_pixel_layer("Base", std::move(base_pixels));
  auto edit_pixels = solid_pixels(48, 16, patchy::PixelFormat::rgba8(), QColor(10, 20, 30, 255));
  for (std::int32_t y = 0; y < 16; ++y) {
    for (std::int32_t x = 0; x < 24; ++x) {
      edit_pixels.pixel(x, y)[3] = 0;
    }
  }
  auto& edit_layer = document.add_pixel_layer("Edit", std::move(edit_pixels));
  document.set_active_layer(edit_layer.id());

  patchy::ui::CanvasWidget canvas;
  canvas.resize(192, 64);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Clone);
  canvas.set_brush_size(1);
  canvas.set_brush_opacity(100);
  canvas.set_brush_softness(0);
  canvas.set_healing_diffusion(5);
  // Unaligned keeps every stroke reading from the Alt-clicked source point.
  canvas.set_clone_aligned(false);
  canvas.show();
  QApplication::processEvents();
  // The default preserves the historical always-merged sampling.
  CHECK(canvas.retouch_sample_all_layers());

  const auto stroke_at = [&canvas](QPoint point) {
    const auto widget_point = canvas.widget_position_for_document_point(point);
    send_mouse(canvas, QEvent::MouseButtonPress, widget_point, Qt::LeftButton, Qt::LeftButton);
    send_mouse(canvas, QEvent::MouseButtonRelease, widget_point, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
  };
  const auto source = canvas.widget_position_for_document_point(QPoint(4, 8));
  send_mouse(canvas, QEvent::MouseButtonPress, source, Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
  send_mouse(canvas, QEvent::MouseButtonRelease, source, Qt::LeftButton, Qt::NoButton, Qt::AltModifier);

  // Checked: the clone source reads the merged composite (the Base layer shows
  // through the transparent half of Edit).
  stroke_at(QPoint(36, 8));
  const auto* cloned = edit_layer.pixels().pixel(36, 8);
  CHECK(cloned[0] == 240 && cloned[1] == 90 && cloned[2] == 60 && cloned[3] == 255);

  // Checked healing: detail from the composite source carried into the Edit
  // layer's local tone: (10,20,30) + (240,90,60) - (200,50,25).
  canvas.set_tool(patchy::ui::CanvasTool::Healing);
  stroke_at(QPoint(32, 8));
  const auto* healed = edit_layer.pixels().pixel(32, 8);
  CHECK(healed[0] == 50 && healed[1] == 60 && healed[2] == 65 && healed[3] == 255);

  // Unchecked: the same source point sampled from the active layer alone is
  // fully transparent, so the clone writes transparency.
  canvas.set_retouch_sample_all_layers(false);
  canvas.set_tool(patchy::ui::CanvasTool::Clone);
  stroke_at(QPoint(40, 8));
  CHECK(edit_layer.pixels().pixel(40, 8)[3] == 0);
}

void ui_spot_healing_click_heals_blemish_on_release() {
  patchy::Document document(48, 24, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(48, 24, patchy::PixelFormat::rgba8(), QColor(40, 80, 120, 255));
  for (std::int32_t y = 11; y <= 13; ++y) {
    for (std::int32_t x = 15; x <= 17; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = 200;
      px[1] = 200;
      px[2] = 200;
    }
  }
  auto& layer = document.add_pixel_layer("Spot", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(192, 96);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::SpotHealing);
  canvas.set_brush_size(12);
  canvas.set_brush_softness(0);
  canvas.set_healing_diffusion(5);
  canvas.show();
  QApplication::processEvents();

  const auto press_point = canvas.widget_position_for_document_point(QPoint(16, 12));
  const auto move_point = canvas.widget_position_for_document_point(QPoint(17, 12));
  send_mouse(canvas, QEvent::MouseButtonPress, press_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, move_point, Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  // Solve-on-release pin: nothing may change while the stroke is being drawn.
  CHECK(layer.pixels().pixel(16, 12)[0] == 200);
  send_mouse(canvas, QEvent::MouseButtonRelease, move_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  // Uniform surroundings collapse the ring-source heal to the base color
  // exactly: source, rim, and both ring tones all read (40,80,120).
  for (const auto point : {QPoint(15, 11), QPoint(16, 12), QPoint(17, 13)}) {
    const auto* healed = layer.pixels().pixel(point.x(), point.y());
    CHECK(healed[0] == 40 && healed[1] == 80 && healed[2] == 120 && healed[3] == 255);
  }
  const auto* outside = layer.pixels().pixel(30, 12);
  CHECK(outside[0] == 40 && outside[1] == 80 && outside[2] == 120 && outside[3] == 255);
}

void ui_spot_healing_escape_cancels_without_pixel_changes() {
  patchy::Document document(48, 24, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(48, 24, patchy::PixelFormat::rgba8(), QColor(40, 80, 120, 255));
  auto* blemish = pixels.pixel(16, 12);
  blemish[0] = 200;
  blemish[1] = 200;
  blemish[2] = 200;
  auto& layer = document.add_pixel_layer("Spot", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(192, 96);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::SpotHealing);
  canvas.set_brush_size(12);
  canvas.show();
  QApplication::processEvents();

  const auto press_point = canvas.widget_position_for_document_point(QPoint(16, 12));
  const auto move_point = canvas.widget_position_for_document_point(QPoint(18, 12));
  send_mouse(canvas, QEvent::MouseButtonPress, press_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, move_point, Qt::NoButton, Qt::LeftButton);
  send_key(canvas, Qt::Key_Escape);
  send_mouse(canvas, QEvent::MouseButtonRelease, move_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  const auto* untouched = layer.pixels().pixel(16, 12);
  CHECK(untouched[0] == 200 && untouched[1] == 200 && untouched[2] == 200 && untouched[3] == 255);
}

// A hard rectangular selection through the public snapshot API (the script
// engine's select_region recipe), independent of any marquee gesture.
void select_document_rect(patchy::ui::CanvasWidget& canvas, QRect rect) {
  auto snapshot = canvas.capture_selection_snapshot();
  snapshot.selection = QRegion(rect);
  snapshot.display_region = snapshot.selection;
  snapshot.mask_bounds = QRect();
  snapshot.mask_alpha = QImage();
  canvas.apply_selection_snapshot(snapshot);
  QApplication::processEvents();
}

using RemoveObjectMethod = patchy::ui::CanvasWidget::RemoveObjectMethod;

// Edit > Remove Object (Nearest Edge): the selection is the footprint;
// uniform surroundings collapse the heal to the base color exactly. Repeating
// on the same selection walks the source candidates, a new selection restarts
// the cycle, an explicit attempt pins it, and no selection is a refusal.
void ui_remove_object_heals_selection_and_cycles_sources() {
  patchy::Document document(64, 32, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(64, 32, patchy::PixelFormat::rgba8(), QColor(40, 80, 120, 255));
  for (std::int32_t y = 14; y <= 17; ++y) {
    for (std::int32_t x = 30; x <= 33; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = 200;
      px[1] = 200;
      px[2] = 200;
    }
  }
  auto& layer = document.add_pixel_layer("Object", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(256, 128);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Marquee);
  canvas.show();
  canvas.set_zoom(2.0);
  QApplication::processEvents();

  select_document_rect(canvas, QRect(28, 12, 8, 8));
  CHECK(canvas.selected_document_region().contains(QPoint(31, 15)));
  CHECK(!canvas.selected_document_region().contains(QPoint(40, 15)));

  const auto first = canvas.remove_object_in_selection(RemoveObjectMethod::NearestEdge);
  CHECK(first.applied);
  CHECK(first.method == RemoveObjectMethod::NearestEdge);
  CHECK(first.source_index == 1);
  CHECK(first.source_count >= 2);
  for (const auto point : {QPoint(30, 14), QPoint(31, 15), QPoint(33, 17)}) {
    const auto* healed = layer.pixels().pixel(point.x(), point.y());
    CHECK(healed[0] == 40 && healed[1] == 80 && healed[2] == 120 && healed[3] == 255);
  }
  const auto* outside = layer.pixels().pixel(40, 15);
  CHECK(outside[0] == 40 && outside[1] == 80 && outside[2] == 120 && outside[3] == 255);
  // The selection stays put (Patch Source-mode parity).
  CHECK(canvas.selected_document_region().contains(QPoint(31, 15)));

  const auto second = canvas.remove_object_in_selection(RemoveObjectMethod::NearestEdge);
  CHECK(second.applied);
  CHECK(second.source_index == 2);
  CHECK(second.source_count == first.source_count);
  const auto pinned = canvas.remove_object_in_selection(RemoveObjectMethod::NearestEdge, 0);
  CHECK(pinned.applied);
  CHECK(pinned.source_index == 1);

  select_document_rect(canvas, QRect(8, 8, 8, 8));
  const auto fresh = canvas.remove_object_in_selection(RemoveObjectMethod::NearestEdge);
  CHECK(fresh.applied);
  CHECK(fresh.source_index == 1);

  canvas.clear_selection();
  const auto refused = canvas.remove_object_in_selection();
  CHECK(!refused.applied);
  CHECK(!refused.error.isEmpty());
}

// The heal writer's strip fan-out (selections at or above kHealParallelArea)
// runs while the undo snapshot still shares the layer's copy-on-write pixel
// bytes. The writer must detach on the calling thread before fanning out:
// strips detaching concurrently each copied the bytes while another strip's
// replacement freed them (the September 2026 headless door_reopen.js crash:
// access violations inside the vector copy on several workers, or heap
// corruption at exit). A Document copy stands in for the snapshot; the passes
// repeat so a racy interleaving gets its chances, and every pass must heal the
// marked block back to the base color while the copy keeps its own bytes.
void ui_remove_object_parallel_heal_detaches_shared_pixels() {
  patchy::Document document(1024, 768, patchy::PixelFormat::rgba8());
  auto& layer = document.add_pixel_layer(
      "Object", solid_pixels(1024, 768, patchy::PixelFormat::rgba8(), QColor(40, 80, 120, 255)));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(512, 384);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Marquee);
  canvas.show();
  QApplication::processEvents();

  for (int pass = 0; pass < 6; ++pass) {
    for (std::int32_t y = 300; y < 340; ++y) {
      for (std::int32_t x = 400; x < 460; ++x) {
        auto* px = layer.pixels().pixel(x, y);
        px[0] = 230;
        px[1] = 230;
        px[2] = 230;
      }
    }
    const patchy::Document snapshot = document;  // shares the layer bytes like the undo snapshot
    select_document_rect(canvas, QRect(100, 100, 700, 400));  // 280,000 px: the strip path
    const auto result = canvas.remove_object_in_selection(RemoveObjectMethod::NearestEdge, 0);
    CHECK(result.applied);
    for (const auto point : {QPoint(400, 300), QPoint(430, 320), QPoint(459, 339)}) {
      const auto* healed = std::as_const(layer).pixels().pixel(point.x(), point.y());
      CHECK(healed[0] == 40 && healed[1] == 80 && healed[2] == 120 && healed[3] == 255);
    }
    const auto* kept = std::as_const(snapshot).find_layer(layer.id())->pixels().pixel(430, 320);
    CHECK(kept[0] == 230 && kept[1] == 230 && kept[2] == 230);
  }
}

// Edit > Remove Object (the content-aware default): on a periodic texture the
// exhaustive exemplar search finds exact source patches, so the marked block
// is restored to the pattern (the default Tone match of 0 leaves the raw
// fill; the tolerance predates that default) and nothing outside the
// selection moves.
void ui_remove_object_content_aware_restores_stripes() {
  constexpr std::int32_t width = 96;
  constexpr std::int32_t height = 48;
  patchy::Document document(width, height, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(width, height, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 255));
  const auto stripe = [](std::int32_t x) { return (x / 8) % 2 == 0 ? QColor(30, 60, 200) : QColor(220, 180, 40); };
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      auto* px = pixels.pixel(x, y);
      const auto color = stripe(x);
      px[0] = static_cast<std::uint8_t>(color.red());
      px[1] = static_cast<std::uint8_t>(color.green());
      px[2] = static_cast<std::uint8_t>(color.blue());
    }
  }
  for (std::int32_t y = 14; y <= 29; ++y) {
    for (std::int32_t x = 32; x <= 47; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = px[1] = px[2] = 255;
    }
  }
  auto& layer = document.add_pixel_layer("Stripes", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(256, 128);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Marquee);
  canvas.show();
  QApplication::processEvents();
  select_document_rect(canvas, QRect(30, 12, 20, 20));

  const auto result = canvas.remove_object_in_selection();
  CHECK(result.applied);
  CHECK(result.method == RemoveObjectMethod::ContentAware);
  CHECK(result.patches > 0);
  for (std::int32_t y = 12; y < 32; ++y) {
    for (std::int32_t x = 30; x < 50; ++x) {
      const auto* px = layer.pixels().pixel(x, y);
      const auto expected = stripe(x);
      CHECK(std::abs(int(px[0]) - expected.red()) <= 8 && std::abs(int(px[1]) - expected.green()) <= 8 &&
            std::abs(int(px[2]) - expected.blue()) <= 8 && px[3] == 255);
    }
  }
  const auto* outside = layer.pixels().pixel(60, 20);
  CHECK(outside[0] == stripe(60).red() && outside[1] == stripe(60).green() && outside[2] == stripe(60).blue());
}

// Finds the open Remove Object dialog (nullptr when none is up).
QDialog* find_remove_object_dialog() {
  for (auto* widget : QApplication::topLevelWidgets()) {
    if (widget->objectName() == QStringLiteral("patchyRemoveObjectDialog")) {
      return qobject_cast<QDialog*>(widget);
    }
  }
  return nullptr;
}

// Edit > Remove Object opens the non-modal dialog at once and fills on a
// worker thread: the preview lands in the layer with no history entry, Reroll
// advances the variation (a fill in flight is cancelled and replaced), a
// settled Tone match / Edge feather value re-runs the fill, Duplicate to New
// Layer copies the current result onto a new hidden layer as its own history
// entry, OK pushes ONE history entry for the fill (Undo restores the object)
// and persists the two settings under their new keys, and Cancel (from the
// Patch options-bar button and from the Patch tool's Enter) leaves the pixels
// and the history untouched. A reopened dialog on the same selection
// continues after the last variation instead of repeating variation 1.
// Every fill runs on an explicit click or a settled value, never per slider
// move (US 8050498; see canvas_widget_spot_healing.cpp).
void ui_remove_object_dialog_previews_rerolls_and_is_undoable() {
  SettingsValueRestorer tone_restorer(QStringLiteral("tools/removeObjectToneMatch"));
  SettingsValueRestorer feather_restorer(QStringLiteral("tools/removeObjectFeather"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("tools/removeObjectToneMatch"));
    settings.remove(QStringLiteral("tools/removeObjectFeather"));
  }
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  auto* layer = active.has_value() ? document.find_layer(*active) : nullptr;
  CHECK(layer != nullptr);
  if (layer == nullptr) {
    return;
  }
  const auto layer_id = layer->id();
  // A dark block on the white background is the object; the fill heals it.
  for (std::int32_t y = 30; y < 50; ++y) {
    for (std::int32_t x = 30; x < 50; ++x) {
      auto* px = layer->pixels().pixel(x, y);
      px[0] = px[1] = px[2] = 20;
      px[3] = 255;
    }
  }
  canvas->document_changed();
  const auto object_pixel = [&] {
    const auto* found = document.find_layer(layer_id);
    return found != nullptr ? std::as_const(*found).pixels().pixel(40, 40)[0] : -1;
  };
  CHECK(object_pixel() == 20);
  const auto layer_count = [&] { return document.layers().size(); };
  const auto initial_layers = layer_count();

  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(26, 26)),
       canvas->widget_position_for_document_point(QPoint(54, 54)));
  QApplication::processEvents();
  CHECK(canvas->has_selection());

  const auto depth = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  // The dialog shows at once while the first fill runs on a worker; the
  // driver re-arms until it is up, and waits for each result (OK enabled)
  // with a deadline so a stuck worker fails the CHECKs instead of hanging.
  int driver_tries = 0;
  std::function<void(std::function<void(QDialog&)>)> when_dialog_ready;
  when_dialog_ready = [&](std::function<void(QDialog&)> body) {
    driver_tries = 0;
    auto poll = std::make_shared<std::function<void()>>();
    *poll = [&, body = std::move(body), poll]() {
      auto* dialog = find_remove_object_dialog();
      if (dialog == nullptr || !dialog->isVisible()) {
        if (++driver_tries < 500) {
          QTimer::singleShot(10, *poll);
        }
        return;
      }
      body(*dialog);
    };
    QTimer::singleShot(10, *poll);
  };
  const auto wait_for_result = [](QDialog& dialog) {
    auto* buttons = dialog.findChild<QDialogButtonBox*>();
    auto* ok = buttons != nullptr ? buttons->button(QDialogButtonBox::Ok) : nullptr;
    auto* label = dialog.findChild<QLabel*>(QStringLiteral("removeObjectVariationLabel"));
    for (int i = 0; i < 400; ++i) {
      if (ok != nullptr && ok->isEnabled() && label != nullptr && !label->text().isEmpty()) {
        return true;
      }
      process_events_for(25);
    }
    return false;
  };
  const auto variation_text = [](QDialog& dialog) {
    auto* label = dialog.findChild<QLabel*>(QStringLiteral("removeObjectVariationLabel"));
    return label != nullptr ? label->text() : QString();
  };
  bool saw_dialog = false;
  bool dialog_non_modal = false;
  bool first_result = false;
  bool preview_had_no_history = false;
  bool preview_healed = false;
  QString first_label;
  bool reroll_result = false;
  QString rerolled_label;
  bool duplicate_added_hidden_layer = false;
  bool duplicate_pushed_history = false;
  bool settled_result = false;
  when_dialog_ready([&](QDialog& dialog) {
    saw_dialog = true;
    dialog_non_modal = !dialog.isModal() && dialog.windowModality() == Qt::NonModal;
    first_result = wait_for_result(dialog);
    preview_had_no_history = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth;
    preview_healed = object_pixel() > 200;
    first_label = variation_text(dialog);
    auto* reroll = dialog.findChild<QPushButton*>(QStringLiteral("removeObjectRerollButton"));
    if (reroll != nullptr) {
      reroll->click();
      reroll_result = wait_for_result(dialog);
    }
    rerolled_label = variation_text(dialog);
    auto* duplicate = dialog.findChild<QPushButton*>(QStringLiteral("removeObjectDuplicateButton"));
    if (duplicate != nullptr) {
      duplicate->click();
      QApplication::processEvents();
      duplicate_pushed_history = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth + 1;
      for (const auto& candidate : document.layers()) {
        if (candidate.name() == "Remove Object variation 2" && !candidate.visible()) {
          duplicate_added_hidden_layer = true;
        }
      }
    }
    // Settled spin values re-run the fill once each (coalesced by the settle
    // timer); the second change cancels the first's fill in flight.
    auto* tone = dialog.findChild<QSpinBox*>(QStringLiteral("removeObjectToneMatchSpin"));
    auto* feather = dialog.findChild<QSpinBox*>(QStringLiteral("removeObjectFeatherSpin"));
    if (tone != nullptr && feather != nullptr) {
      tone->setValue(40);
      process_events_for(300);
      feather->setValue(3);
      settled_result = wait_for_result(dialog);
    }
    dialog.accept();
  });
  require_action(window, "editRemoveObjectAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
  CHECK(dialog_non_modal);
  CHECK(first_result);
  CHECK(preview_had_no_history);
  CHECK(preview_healed);
  CHECK(first_label == QStringLiteral("Variation 1"));
  CHECK(reroll_result);
  CHECK(rerolled_label == QStringLiteral("Variation 2"));
  CHECK(duplicate_pushed_history);
  CHECK(duplicate_added_hidden_layer);
  CHECK(layer_count() == initial_layers + 1);
  CHECK(settled_result);
  CHECK(find_remove_object_dialog() == nullptr);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth + 2);
  CHECK(object_pixel() > 200);
  {
    auto settings = patchy::ui::app_settings();
    CHECK(settings.value(QStringLiteral("tools/removeObjectToneMatch")).toInt() == 40);
    CHECK(settings.value(QStringLiteral("tools/removeObjectFeather")).toInt() == 3);
  }
  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth + 1);
  CHECK(object_pixel() == 20);
  CHECK(layer_count() == initial_layers + 1);  // the duplicate is its own entry
  // Undo restored the selection it ran against.
  CHECK(canvas->has_selection());

  // The Patch options-bar button opens the same dialog, remembering the
  // settings and continuing the variations (the last shown was 2, so this
  // one opens on 3); Cancel restores the object with no history entry.
  canvas->set_tool(patchy::ui::CanvasTool::PatchTool);
  QApplication::processEvents();
  auto* button = window.findChild<QPushButton*>(QStringLiteral("patchRemoveObjectButton"));
  CHECK(button != nullptr);
  bool remembered = false;
  bool cancelled_dialog = false;
  QString continued_label;
  when_dialog_ready([&](QDialog& dialog) {
    auto* tone = dialog.findChild<QSpinBox*>(QStringLiteral("removeObjectToneMatchSpin"));
    auto* feather = dialog.findChild<QSpinBox*>(QStringLiteral("removeObjectFeatherSpin"));
    remembered = tone != nullptr && feather != nullptr && tone->value() == 40 && feather->value() == 3;
    (void)wait_for_result(dialog);
    continued_label = variation_text(dialog);
    cancelled_dialog = true;
    dialog.reject();
  });
  if (button != nullptr) {
    button->click();
    QApplication::processEvents();
  }
  CHECK(cancelled_dialog);
  CHECK(remembered);
  CHECK(continued_label == QStringLiteral("Variation 3"));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth + 1);
  CHECK(object_pixel() == 20);

  // Enter with the Patch tool (outline, no drag) asks the window for the
  // dialog too; cancelling while the fill is still in flight is safe.
  bool enter_opened = false;
  when_dialog_ready([&](QDialog& dialog) {
    enter_opened = true;
    dialog.reject();
  });
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(enter_opened);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth + 1);
  CHECK(object_pixel() == 20);

  // A different selection starts the variations over.
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(70, 70)),
       canvas->widget_position_for_document_point(QPoint(90, 90)));
  QApplication::processEvents();
  QString fresh_label;
  when_dialog_ready([&](QDialog& dialog) {
    (void)wait_for_result(dialog);
    fresh_label = variation_text(dialog);
    dialog.reject();
  });
  require_action(window, "editRemoveObjectAction")->trigger();
  QApplication::processEvents();
  CHECK(fresh_label == QStringLiteral("Variation 1"));
}

// Issue #23's banner (tests/remove_object_fixture.hpp) through the canvas
// command: the content-aware fill of the "1" stays as dark as the band and
// keeps the grungy stripe (the tone match reads only the fill's own hole
// cells), variations 2 and 3 are different fills that are each reproducible,
// Tone match 0 is the raw fill, and Edge feather softens the write outward
// past the selection. Before/after frames land in the artifact folder for a
// look (ui_remove_object_banner_*).
void ui_remove_object_banner_variations_tone_and_feather() {
  const auto fixture = patchy::test::make_banner_fixture();
  patchy::Document document(fixture.width, fixture.height, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer pixels(fixture.width, fixture.height, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < fixture.height; ++y) {
    std::memcpy(pixels.row(y).data(), fixture.pixel(0, y), static_cast<std::size_t>(fixture.width) * 4U);
  }
  const auto original = pixels;
  auto& layer = document.add_pixel_layer("Banner", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(fixture.width * 2 + 8, fixture.height * 2 + 8);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Marquee);
  canvas.show();
  canvas.set_zoom(2.0);
  QApplication::processEvents();
  save_widget_artifact("ui_remove_object_banner_before", canvas);
  const QRect hole(fixture.hole_x, fixture.hole_y, fixture.hole_width, fixture.hole_height);
  select_document_rect(canvas, hole);

  const auto reset = [&] {
    layer.set_pixels(original);
    canvas.document_changed();
  };
  const auto run = [&](int attempt, int tone_match, int feather) {
    reset();
    patchy::ui::CanvasWidget::RemoveObjectOptions options;
    options.attempt = attempt;
    options.tone_match = tone_match;
    options.feather = feather;
    const auto result = canvas.remove_object_in_selection(options);
    CHECK(result.applied);
    CHECK(result.method == patchy::ui::CanvasWidget::RemoveObjectMethod::ContentAware);
    CHECK(result.attempt == std::max(0, attempt));
    return std::as_const(layer).pixels();
  };
  const auto differing = [&](const patchy::PixelBuffer& a, const patchy::PixelBuffer& b, bool inside_hole) {
    std::int64_t count = 0;
    for (std::int32_t y = 0; y < fixture.height; ++y) {
      for (std::int32_t x = 0; x < fixture.width; ++x) {
        if (hole.contains(QPoint(x, y)) != inside_hole) {
          continue;
        }
        if (std::memcmp(a.pixel(x, y), b.pixel(x, y), 4U) != 0) {
          ++count;
        }
      }
    }
    return count;
  };
  const auto band_mean = [&](const patchy::PixelBuffer& image, bool stripe_rows) {
    double sum = 0.0;
    std::int32_t count = 0;
    for (std::int32_t y = hole.top(); y <= hole.bottom(); ++y) {
      if (fixture.in_stripe(y) != stripe_rows) {
        continue;
      }
      for (std::int32_t x = hole.left(); x <= hole.right(); ++x) {
        const auto* px = image.pixel(x, y);
        sum += (px[0] * 77 + px[1] * 150 + px[2] * 29) >> 8;
        ++count;
      }
    }
    return sum / std::max(1, count);
  };

  const auto first = run(0, 100, 0);
  save_widget_artifact("ui_remove_object_banner_variation_1", canvas);
  CHECK(std::abs(band_mean(first, false) - fixture.band_value) <= 12.0);
  CHECK(band_mean(first, true) - band_mean(first, false) >= 0.5 * (fixture.stripe_value - fixture.band_value));
  CHECK(differing(first, original, false) == 0);
  const auto identical = [&](const patchy::PixelBuffer& a, const patchy::PixelBuffer& b) {
    return differing(a, b, true) == 0 && differing(a, b, false) == 0;
  };
  CHECK(identical(run(0, 100, 0), first));

  const auto second = run(1, 100, 0);
  save_widget_artifact("ui_remove_object_banner_variation_2", canvas);
  CHECK(differing(second, first, true) > 0);
  CHECK(differing(second, original, false) == 0);
  CHECK(identical(run(1, 100, 0), second));
  const auto third = run(2, 100, 0);
  save_widget_artifact("ui_remove_object_banner_variation_3", canvas);
  CHECK(differing(third, second, true) > 0);
  CHECK(differing(third, first, true) > 0);
  CHECK(std::abs(band_mean(second, false) - fixture.band_value) <= 20.0);
  CHECK(std::abs(band_mean(third, false) - fixture.band_value) <= 20.0);

  // Tone match 0 is the raw exemplar fill; 100 moves some hole pixels.
  const auto raw = run(0, 0, 0);
  save_widget_artifact("ui_remove_object_banner_tone_off", canvas);
  CHECK(differing(raw, first, true) > 0);
  CHECK(differing(raw, original, false) == 0);
  CHECK(std::abs(band_mean(raw, false) - fixture.band_value) <= 12.0);

  // Edge feather writes a soft skirt past the selection; without it nothing
  // outside the selection moves.
  const auto feathered = run(0, 100, 4);
  save_widget_artifact("ui_remove_object_banner_feather_4", canvas);
  CHECK(differing(feathered, original, false) > 0);
  const QRect skirt = hole.adjusted(-12, -12, 12, 12);
  bool skirt_only = true;
  for (std::int32_t y = 0; y < fixture.height && skirt_only; ++y) {
    for (std::int32_t x = 0; x < fixture.width; ++x) {
      if (!skirt.contains(QPoint(x, y)) && std::memcmp(feathered.pixel(x, y), original.pixel(x, y), 4U) != 0) {
        skirt_only = false;
        break;
      }
    }
  }
  CHECK(skirt_only);
  canvas.clear_selection();
}

// Draws the Patch tool's freehand outline as a rectangle-ish loop and returns
// with the selection committed.
void draw_patch_outline(patchy::ui::CanvasWidget& canvas, QPoint top_left, QPoint bottom_right) {
  const auto to_widget = [&canvas](QPoint point) {
    return canvas.widget_position_for_document_point(point);
  };
  send_mouse(canvas, QEvent::MouseButtonPress, to_widget(top_left), Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, to_widget(QPoint(bottom_right.x(), top_left.y())), Qt::NoButton,
             Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, to_widget(bottom_right), Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, to_widget(QPoint(top_left.x(), bottom_right.y())), Qt::NoButton,
             Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, to_widget(QPoint(top_left.x(), bottom_right.y())),
             Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
}

void drag_patch_region(patchy::ui::CanvasWidget& canvas, QPoint from, QPoint to) {
  const auto to_widget = [&canvas](QPoint point) {
    return canvas.widget_position_for_document_point(point);
  };
  send_mouse(canvas, QEvent::MouseButtonPress, to_widget(from), Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, to_widget(QPoint((from.x() + to.x()) / 2, to.y())), Qt::NoButton,
             Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, to_widget(to), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  send_mouse(canvas, QEvent::MouseButtonRelease, to_widget(to), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
}

void ui_patch_tool_source_drag_heals_region_on_release() {
  patchy::Document document(64, 24, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(64, 24, patchy::PixelFormat::rgba8(), QColor(40, 80, 120, 255));
  for (std::int32_t y = 8; y <= 11; ++y) {
    for (std::int32_t x = 8; x <= 11; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = 220;
      px[1] = 220;
      px[2] = 220;
    }
  }
  auto& layer = document.add_pixel_layer("Patch", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(256, 96);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::PatchTool);
  canvas.show();
  canvas.set_zoom(4.0);
  QApplication::processEvents();

  draw_patch_outline(canvas, QPoint(5, 5), QPoint(14, 15));
  CHECK(canvas.selected_document_rect().has_value());
  CHECK(canvas.selected_document_region().contains(QPoint(10, 10)));

  const auto to_widget = [&canvas](QPoint point) {
    return canvas.widget_position_for_document_point(point);
  };
  send_mouse(canvas, QEvent::MouseButtonPress, to_widget(QPoint(10, 10)), Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, to_widget(QPoint(26, 10)), Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, to_widget(QPoint(42, 10)), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  // One-shot-on-release pin: the drag previews but never writes.
  CHECK(layer.pixels().pixel(8, 8)[0] == 220);
  send_mouse(canvas, QEvent::MouseButtonRelease, to_widget(QPoint(42, 10)), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  // The dragged-to area and every ring tone read the uniform base, so the
  // healed region collapses to the base color exactly.
  for (const auto point : {QPoint(8, 8), QPoint(10, 10), QPoint(11, 11)}) {
    const auto* healed = layer.pixels().pixel(point.x(), point.y());
    CHECK(healed[0] == 40 && healed[1] == 80 && healed[2] == 120 && healed[3] == 255);
  }
  // Source mode keeps the selection at the original region.
  CHECK(canvas.selected_document_region().contains(QPoint(10, 10)));
  CHECK(!canvas.selected_document_region().contains(QPoint(42, 10)));
  // The dragged-to area itself is untouched.
  const auto* source_area = layer.pixels().pixel(42, 10);
  CHECK(source_area[0] == 40 && source_area[1] == 80 && source_area[2] == 120 && source_area[3] == 255);
}

void ui_patch_tool_destination_mode_copies_detail_and_moves_selection() {
  patchy::Document document(64, 24, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(64, 24, patchy::PixelFormat::rgba8(), QColor(40, 80, 120, 255));
  auto* dot = pixels.pixel(10, 10);
  dot[0] = 90;
  dot[1] = 130;
  dot[2] = 170;
  auto& layer = document.add_pixel_layer("Patch", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(256, 96);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::PatchTool);
  canvas.set_patch_tool_mode(patchy::ui::CanvasWidget::PatchToolMode::Destination);
  canvas.show();
  canvas.set_zoom(4.0);
  QApplication::processEvents();

  draw_patch_outline(canvas, QPoint(6, 6), QPoint(15, 15));
  drag_patch_region(canvas, QPoint(10, 10), QPoint(42, 10));

  // The copy lands healed at the drop point: uniform surroundings on both
  // sides make the detail transfer exact.
  const auto* dropped = layer.pixels().pixel(42, 10);
  CHECK(dropped[0] == 90 && dropped[1] == 130 && dropped[2] == 170 && dropped[3] == 255);
  // The original region is untouched and the selection followed the drop.
  const auto* original = layer.pixels().pixel(10, 10);
  CHECK(original[0] == 90 && original[1] == 130 && original[2] == 170 && original[3] == 255);
  CHECK(canvas.selected_document_region().contains(QPoint(42, 10)));
  CHECK(!canvas.selected_document_region().contains(QPoint(10, 10)));
}

void ui_patch_tool_transparent_blends_texture_only() {
  patchy::Document document(64, 24, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(64, 24, patchy::PixelFormat::rgba8(), QColor(40, 80, 120, 255));
  for (std::int32_t y = 0; y < 24; ++y) {
    for (std::int32_t x = 36; x < 64; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = 90;
      px[1] = 60;
      px[2] = 30;
    }
  }
  auto* dot = pixels.pixel(42, 10);
  dot[0] = 140;
  dot[1] = 110;
  dot[2] = 80;
  auto& layer = document.add_pixel_layer("Patch", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(256, 96);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::PatchTool);
  canvas.set_patch_tool_transparent(true);
  canvas.show();
  canvas.set_zoom(4.0);
  QApplication::processEvents();

  draw_patch_outline(canvas, QPoint(6, 6), QPoint(15, 15));
  drag_patch_region(canvas, QPoint(10, 10), QPoint(42, 10));

  // Transparent transfers only the source's high-pass detail over the
  // destination: the dot adds (almost all of) its +50 offset onto the base -
  // the box-blurred local mean absorbs a sliver - and flat source areas stay
  // within a couple of levels of the untouched base.
  const auto* textured = layer.pixels().pixel(10, 10);
  CHECK(std::abs(static_cast<int>(textured[0]) - 90) <= 3);
  CHECK(std::abs(static_cast<int>(textured[1]) - 130) <= 3);
  CHECK(std::abs(static_cast<int>(textured[2]) - 170) <= 3);
  CHECK(textured[3] == 255);
  const auto* flat = layer.pixels().pixel(12, 12);
  CHECK(std::abs(static_cast<int>(flat[0]) - 40) <= 3);
  CHECK(std::abs(static_cast<int>(flat[1]) - 80) <= 3);
  CHECK(std::abs(static_cast<int>(flat[2]) - 120) <= 3);
  CHECK(flat[3] == 255);
}

void ui_patch_tool_click_inside_is_noop_and_escape_cancels() {
  patchy::Document document(64, 24, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(64, 24, patchy::PixelFormat::rgba8(), QColor(40, 80, 120, 255));
  auto* marker = pixels.pixel(10, 10);
  marker[0] = 220;
  auto& layer = document.add_pixel_layer("Patch", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(256, 96);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::PatchTool);
  canvas.show();
  canvas.set_zoom(4.0);
  QApplication::processEvents();

  draw_patch_outline(canvas, QPoint(6, 6), QPoint(15, 15));
  const auto to_widget = [&canvas](QPoint point) {
    return canvas.widget_position_for_document_point(point);
  };

  // A click inside the selection is a no-op that keeps the selection.
  send_mouse(canvas, QEvent::MouseButtonPress, to_widget(QPoint(10, 10)), Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, to_widget(QPoint(10, 10)), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(layer.pixels().pixel(10, 10)[0] == 220);
  CHECK(canvas.selected_document_region().contains(QPoint(10, 10)));

  // Escape mid-drag discards the gesture without writing.
  send_mouse(canvas, QEvent::MouseButtonPress, to_widget(QPoint(10, 10)), Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, to_widget(QPoint(42, 10)), Qt::NoButton, Qt::LeftButton);
  send_key(canvas, Qt::Key_Escape);
  send_mouse(canvas, QEvent::MouseButtonRelease, to_widget(QPoint(42, 10)), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(layer.pixels().pixel(10, 10)[0] == 220);
  const auto* drop_area = layer.pixels().pixel(42, 10);
  CHECK(drop_area[0] == 40 && drop_area[1] == 80 && drop_area[2] == 120 && drop_area[3] == 255);
}

// Enter with a Patch outline and no drag is the automatic heal (Remove
// Object) of the drawn region.
void ui_patch_tool_enter_removes_object() {
  patchy::Document document(64, 24, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(64, 24, patchy::PixelFormat::rgba8(), QColor(40, 80, 120, 255));
  for (std::int32_t y = 9; y <= 11; ++y) {
    for (std::int32_t x = 9; x <= 11; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = 220;
      px[1] = 220;
      px[2] = 220;
    }
  }
  auto& layer = document.add_pixel_layer("Patch", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(256, 96);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::PatchTool);
  canvas.show();
  canvas.set_zoom(4.0);
  QApplication::processEvents();

  draw_patch_outline(canvas, QPoint(6, 6), QPoint(15, 15));
  CHECK(canvas.selected_document_region().contains(QPoint(10, 10)));
  CHECK(layer.pixels().pixel(10, 10)[0] == 220);
  send_key(canvas, Qt::Key_Return);
  QApplication::processEvents();
  const auto* healed = layer.pixels().pixel(10, 10);
  CHECK(healed[0] == 40 && healed[1] == 80 && healed[2] == 120 && healed[3] == 255);
  CHECK(canvas.selected_document_region().contains(QPoint(10, 10)));
}

// Destination mode dropped onto strongly contrasting content: the membrane
// must shift the copied texture to the drop area's tone (uniform boundary
// offsets solve to a constant), never emit unbounded per-pixel values.
void ui_patch_tool_destination_onto_contrast_adapts_tone() {
  patchy::Document document(128, 64, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(128, 64, patchy::PixelFormat::rgba8(), QColor(230, 150, 60, 255));
  for (std::int32_t y = 0; y < 64; ++y) {
    for (std::int32_t x = 64; x < 128; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = 20;
      px[1] = 40;
      px[2] = 90;
    }
  }
  auto& layer = document.add_pixel_layer("Contrast", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(300, 160);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::PatchTool);
  canvas.set_patch_tool_mode(patchy::ui::CanvasWidget::PatchToolMode::Destination);
  canvas.show();
  canvas.set_zoom(2.0);
  QApplication::processEvents();

  draw_patch_outline(canvas, QPoint(8, 8), QPoint(28, 28));
  drag_patch_region(canvas, QPoint(18, 18), QPoint(90, 40));
  save_widget_artifact("ui_patch_destination_contrast_after", canvas);

  // The copy carries the orange region's (flat) texture with a constant
  // boundary offset onto the blue side, so interior drop pixels must land on
  // the blue base tone.
  for (const auto point : {QPoint(88, 38), QPoint(90, 40), QPoint(93, 42)}) {
    const auto* healed = layer.pixels().pixel(point.x(), point.y());
    CHECK(std::abs(static_cast<int>(healed[0]) - 20) <= 3);
    CHECK(std::abs(static_cast<int>(healed[1]) - 40) <= 3);
    CHECK(std::abs(static_cast<int>(healed[2]) - 90) <= 3);
    CHECK(healed[3] == 255);
  }
}

// A perfectly closed outline (release exactly on the press point) must commit
// the selection, not read as a click-to-deselect: the click test measures the
// whole traced path's extent, not press-vs-release distance. Covers the Patch
// outline and the plain Lasso, which share the gesture.
void ui_patch_and_lasso_closed_loop_outline_still_selects() {
  patchy::Document document(64, 24, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(64, 24, patchy::PixelFormat::rgba8(), QColor(40, 80, 120, 255));
  document.add_pixel_layer("Loop", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(256, 96);
  canvas.set_document(&document);
  canvas.show();
  canvas.set_zoom(4.0);
  QApplication::processEvents();

  const auto to_widget = [&canvas](QPoint point) {
    return canvas.widget_position_for_document_point(point);
  };
  const auto draw_closed_loop = [&](QPoint top_left, QPoint bottom_right) {
    send_mouse(canvas, QEvent::MouseButtonPress, to_widget(top_left), Qt::LeftButton, Qt::LeftButton);
    send_mouse(canvas, QEvent::MouseMove, to_widget(QPoint(bottom_right.x(), top_left.y())), Qt::NoButton,
               Qt::LeftButton);
    send_mouse(canvas, QEvent::MouseMove, to_widget(bottom_right), Qt::NoButton, Qt::LeftButton);
    send_mouse(canvas, QEvent::MouseMove, to_widget(QPoint(top_left.x(), bottom_right.y())), Qt::NoButton,
               Qt::LeftButton);
    // Close the loop exactly on the press point before releasing there.
    send_mouse(canvas, QEvent::MouseMove, to_widget(top_left), Qt::NoButton, Qt::LeftButton);
    send_mouse(canvas, QEvent::MouseButtonRelease, to_widget(top_left), Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
  };

  canvas.set_tool(patchy::ui::CanvasTool::PatchTool);
  draw_closed_loop(QPoint(6, 6), QPoint(15, 15));
  CHECK(canvas.selected_document_region().contains(QPoint(10, 10)));

  canvas.set_tool(patchy::ui::CanvasTool::Lasso);
  draw_closed_loop(QPoint(30, 6), QPoint(44, 16));
  CHECK(canvas.selected_document_region().contains(QPoint(37, 11)));

  // A plain click must still deselect (Replace mode), path-extent test or not.
  send_mouse(canvas, QEvent::MouseButtonPress, to_widget(QPoint(50, 10)), Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, to_widget(QPoint(50, 10)), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(!canvas.selected_document_rect().has_value());
}

// Renders before/after artifacts of both heals on a textured gradient - the
// fixture class where the pre-membrane math showed starburst streaks and tone
// rings. Inspect ui_spot_healing_gallery_* / ui_patch_tool_gallery_* when
// touching the healing algorithms.
void ui_spot_healing_and_patch_texture_gallery() {
  constexpr std::int32_t width = 256;
  constexpr std::int32_t height = 160;
  patchy::Document document(width, height, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(width, height, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 255));
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      // Smooth diagonal skin-tone gradient plus a fine deterministic weave.
      const auto ramp = static_cast<double>(x) / width * 60.0 + static_cast<double>(y) / height * 40.0;
      const auto weave = 6.0 * std::sin(x * 0.7) * std::sin(y * 0.55);
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(std::clamp(185.0 + ramp * 0.5 + weave, 0.0, 255.0));
      px[1] = static_cast<std::uint8_t>(std::clamp(135.0 + ramp * 0.4 + weave, 0.0, 255.0));
      px[2] = static_cast<std::uint8_t>(std::clamp(110.0 + ramp * 0.3 + weave, 0.0, 255.0));
    }
  }
  // Two dark blemishes: one for the spot heal, one inside the patch region.
  const auto stamp_blemish = [&pixels](QPoint center, int radius) {
    for (std::int32_t y = center.y() - radius; y <= center.y() + radius; ++y) {
      for (std::int32_t x = center.x() - radius; x <= center.x() + radius; ++x) {
        const auto dx = x - center.x();
        const auto dy = y - center.y();
        if (dx * dx + dy * dy > radius * radius || x < 0 || y < 0 || x >= width || y >= height) {
          continue;
        }
        auto* px = pixels.pixel(x, y);
        px[0] = static_cast<std::uint8_t>(px[0] * 2 / 5 + 20);
        px[1] = static_cast<std::uint8_t>(px[1] * 2 / 5 + 10);
        px[2] = static_cast<std::uint8_t>(px[2] * 2 / 5 + 10);
      }
    }
  };
  stamp_blemish(QPoint(64, 60), 11);
  stamp_blemish(QPoint(64, 116), 9);
  // A third one for Remove Object (a marquee around it).
  stamp_blemish(QPoint(220, 120), 10);
  document.add_pixel_layer("Texture", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(width + 72, height + 72);
  canvas.set_document(&document);
  canvas.show();
  canvas.set_zoom(1.0);
  QApplication::processEvents();
  save_widget_artifact("ui_healing_gallery_before", canvas);

  canvas.set_tool(patchy::ui::CanvasTool::SpotHealing);
  canvas.set_brush_size(30);
  canvas.set_brush_softness(35);
  const auto spot = canvas.widget_position_for_document_point(QPoint(64, 60));
  send_mouse(canvas, QEvent::MouseButtonPress, spot, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, spot, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  save_widget_artifact("ui_spot_healing_gallery_after", canvas);

  canvas.set_tool(patchy::ui::CanvasTool::PatchTool);
  draw_patch_outline(canvas, QPoint(48, 100), QPoint(82, 132));
  drag_patch_region(canvas, QPoint(64, 116), QPoint(180, 116));
  QApplication::processEvents();
  save_widget_artifact("ui_patch_tool_gallery_after", canvas);

  // Destination mode over the same textured gradient: copy a clean piece onto
  // a brighter area, then chain a second drop from the moved selection.
  canvas.set_patch_tool_mode(patchy::ui::CanvasWidget::PatchToolMode::Destination);
  draw_patch_outline(canvas, QPoint(150, 30), QPoint(184, 62));
  drag_patch_region(canvas, QPoint(166, 46), QPoint(96, 100));
  drag_patch_region(canvas, QPoint(96, 100), QPoint(200, 110));
  QApplication::processEvents();
  save_widget_artifact("ui_patch_destination_gallery_after", canvas);

  // Transparent over the same texture: near-invisible by design (same-texture
  // detail transfer), and above all with NO printed selection outline.
  canvas.set_patch_tool_mode(patchy::ui::CanvasWidget::PatchToolMode::Source);
  canvas.set_patch_tool_transparent(true);
  draw_patch_outline(canvas, QPoint(110, 40), QPoint(150, 80));
  drag_patch_region(canvas, QPoint(130, 60), QPoint(40, 60));
  QApplication::processEvents();
  save_widget_artifact("ui_patch_transparent_gallery_after", canvas);
  canvas.set_patch_tool_transparent(false);

  // Remove Object on a marquee around the third blemish: the content-aware
  // exemplar fill first, then the nearest-edge mirror on the same selection.
  canvas.set_tool(patchy::ui::CanvasTool::Marquee);
  select_document_rect(canvas, QRect(206, 106, 29, 29));
  const auto content_aware = canvas.remove_object_in_selection();
  CHECK(content_aware.applied);
  CHECK(content_aware.method == RemoveObjectMethod::ContentAware);
  save_widget_artifact("ui_remove_object_gallery_content_aware", canvas);
  CHECK(canvas.remove_object_in_selection(RemoveObjectMethod::NearestEdge).applied);
  save_widget_artifact("ui_remove_object_gallery_nearest_edge", canvas);
  canvas.clear_selection();
}

void ui_patch_options_sync_canvas_and_persist() {
  SettingsValueRestorer mode_restorer(QStringLiteral("tools/patchMode"));
  SettingsValueRestorer transparent_restorer(QStringLiteral("tools/patchTransparent"));
  SettingsValueRestorer sample_restorer(QStringLiteral("tools/retouchSampleAllLayers"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("tools/patchMode"));
    settings.remove(QStringLiteral("tools/patchTransparent"));
  }
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  CHECK(canvas->retouch_sample_all_layers());
  // Startup defaults are deliberate: Source mode, Transparent off.
  CHECK(canvas->patch_tool_mode() == patchy::ui::CanvasWidget::PatchToolMode::Source);
  CHECK(!canvas->patch_tool_transparent());

  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("patchModeCombo"));
  CHECK(mode_combo != nullptr);
  mode_combo->setCurrentIndex(1);
  CHECK(canvas->patch_tool_mode() == patchy::ui::CanvasWidget::PatchToolMode::Destination);

  auto* transparent_check = window.findChild<QCheckBox*>(QStringLiteral("patchTransparentCheck"));
  CHECK(transparent_check != nullptr);
  transparent_check->setChecked(true);
  CHECK(canvas->patch_tool_transparent());

  auto* sample_check = window.findChild<QCheckBox*>(QStringLiteral("retouchSampleAllLayersCheck"));
  CHECK(sample_check != nullptr);
  sample_check->setChecked(false);
  CHECK(!canvas->retouch_sample_all_layers());

  // Sample All Layers persists; Patch mode and Transparent are session-only,
  // so the retired keys must stay unwritten (every startup begins at Source
  // with Transparent off).
  auto settings = patchy::ui::app_settings();
  CHECK(!settings.value(QStringLiteral("tools/retouchSampleAllLayers")).toBool());
  CHECK(!settings.contains(QStringLiteral("tools/patchMode")));
  CHECK(!settings.contains(QStringLiteral("tools/patchTransparent")));
}

}  // namespace

std::vector<patchy::test::TestCase> brush_engine_stroke_tests_part2() {
  return {
      {"ui_retouch_sample_all_layers_switches_clone_and_healing_source",
       ui_retouch_sample_all_layers_switches_clone_and_healing_source},
      {"ui_spot_healing_click_heals_blemish_on_release", ui_spot_healing_click_heals_blemish_on_release},
      {"ui_spot_healing_escape_cancels_without_pixel_changes",
       ui_spot_healing_escape_cancels_without_pixel_changes},
      {"ui_remove_object_heals_selection_and_cycles_sources",
       ui_remove_object_heals_selection_and_cycles_sources},
      {"ui_remove_object_content_aware_restores_stripes", ui_remove_object_content_aware_restores_stripes},
      {"ui_remove_object_dialog_previews_rerolls_and_is_undoable",
       ui_remove_object_dialog_previews_rerolls_and_is_undoable},
      {"ui_remove_object_banner_variations_tone_and_feather", ui_remove_object_banner_variations_tone_and_feather},
      {"ui_remove_object_parallel_heal_detaches_shared_pixels",
       ui_remove_object_parallel_heal_detaches_shared_pixels},
      {"ui_patch_tool_enter_removes_object", ui_patch_tool_enter_removes_object},
      {"ui_patch_tool_source_drag_heals_region_on_release",
       ui_patch_tool_source_drag_heals_region_on_release},
      {"ui_patch_tool_destination_mode_copies_detail_and_moves_selection",
       ui_patch_tool_destination_mode_copies_detail_and_moves_selection},
      {"ui_patch_tool_transparent_blends_texture_only", ui_patch_tool_transparent_blends_texture_only},
      {"ui_patch_tool_click_inside_is_noop_and_escape_cancels",
       ui_patch_tool_click_inside_is_noop_and_escape_cancels},
      {"ui_patch_tool_destination_onto_contrast_adapts_tone",
       ui_patch_tool_destination_onto_contrast_adapts_tone},
      {"ui_patch_and_lasso_closed_loop_outline_still_selects",
       ui_patch_and_lasso_closed_loop_outline_still_selects},
      {"ui_spot_healing_and_patch_texture_gallery", ui_spot_healing_and_patch_texture_gallery},
      {"ui_patch_options_sync_canvas_and_persist", ui_patch_options_sync_canvas_and_persist},
  };
}
