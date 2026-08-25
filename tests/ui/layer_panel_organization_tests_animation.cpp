// Animation-preview tests, a part file of the layer_panel_organization group (the
// aggregator in layer_panel_organization_tests.cpp appends this part's vector).

#include "ui/animation_preview_window.hpp"
#include "ui/app_settings.hpp"
#include "ui/main_window.hpp"

#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_support.hpp"

#include <QApplication>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QToolButton>

#include <utility>
#include <vector>

using namespace patchy::test::ui;

namespace {

// Bottom to top: Base, Hidden (invisible), "blink 0.25s", Top. Playback frames are the
// visible layers top to bottom: Top, blink, Base.
patchy::Document animation_test_document() {
  patchy::Document document(16, 12, patchy::PixelFormat::rgba8());
  for (const auto* name : {"Base", "Hidden", "blink 0.25s", "Top"}) {
    document.add_layer(patchy::Layer(document.allocate_layer_id(), name,
                                     solid_pixels(16, 12, patchy::PixelFormat::rgba8(),
                                                  QColor(40, 90, 220, 255))));
  }
  document.layers()[1].set_visible(false);
  return document;
}

// Visibility of the four layers bottom to top.
std::vector<bool> top_level_visibility(patchy::ui::MainWindow& window) {
  std::vector<bool> visibility;
  const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  for (const auto& layer : document.layers()) {
    visibility.push_back(layer.visible());
  }
  return visibility;
}

void ui_animation_preview_plays_visible_layers_and_restores() {
  SettingsValueRestorer delay_setting(QStringLiteral("saveOptions/gifFrameDelayCs"));
  patchy::ui::app_settings().remove(QStringLiteral("saveOptions/gifFrameDelayCs"));

  patchy::ui::MainWindow window;
  window.add_document_session(animation_test_document(), QStringLiteral("Animation"));
  show_window(window);

  auto* animation_button = window.findChild<QPushButton*>(QStringLiteral("layerAnimationButton"));
  CHECK(animation_button != nullptr);
  animation_button->click();
  QApplication::processEvents();
  auto* panel = window.findChild<patchy::ui::AnimationPreviewWindow*>(QStringLiteral("animationPreviewWindow"));
  CHECK(panel != nullptr);
  CHECK(panel->isVisible());

  // The delay spin edits the shared export default.
  auto* delay_spin = panel->findChild<QDoubleSpinBox*>(QStringLiteral("animationFrameDelaySpin"));
  auto* play_button = panel->findChild<QPushButton*>(QStringLiteral("animationPlayButton"));
  auto* status_label = panel->findChild<QLabel*>(QStringLiteral("animationFrameStatusLabel"));
  CHECK(delay_spin != nullptr);
  CHECK(play_button != nullptr);
  CHECK(status_label != nullptr);
  CHECK(delay_spin->value() == 0.10);
  delay_spin->setValue(0.30);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("saveOptions/gifFrameDelayCs")).toInt() == 30);

  const auto undo_depth_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  CHECK(top_level_visibility(window) == (std::vector<bool>{true, false, true, true}));

  play_button->click();
  CHECK(play_button->text().contains(QStringLiteral("Stop")));
  CHECK(panel->playing());
  // Frame 1 is the TOP layer (the animated GIF export's order); the hidden layer and
  // every other layer are off.
  CHECK(top_level_visibility(window) == (std::vector<bool>{false, false, false, true}));
  CHECK(status_label->text().contains(QStringLiteral("1")));
  CHECK(status_label->text().contains(QStringLiteral("3")));
  // "Top" has no name token, so the spin's default applies.
  CHECK(panel->current_frame_delay_ms() == 300);

  panel->advance_frame();
  CHECK(top_level_visibility(window) == (std::vector<bool>{false, false, true, false}));
  // "blink 0.25s" overrides the default.
  CHECK(panel->current_frame_delay_ms() == 250);
  // The panel rows follow without a rebuild: row 0 is the top layer (off), row 1 the
  // current frame (on).
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  CHECK(layer_list->count() == 4);
  CHECK(layer_list->item(0)->checkState() == Qt::Unchecked);
  CHECK(layer_list->item(1)->checkState() == Qt::Checked);

  panel->advance_frame();
  CHECK(top_level_visibility(window) == (std::vector<bool>{true, false, false, false}));
  panel->advance_frame();  // wraps back to the top layer
  CHECK(top_level_visibility(window) == (std::vector<bool>{false, false, false, true}));

  play_button->click();
  CHECK(!panel->playing());
  CHECK(play_button->text().contains(QStringLiteral("Play")));
  CHECK(status_label->text().isEmpty());
  CHECK(top_level_visibility(window) == (std::vector<bool>{true, false, true, true}));
  // Playback is preview-only: no undo entries, and the session never went modified.
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_depth_before);
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));

  // The playback timer really advances frames on its own.
  delay_spin->setValue(0.01);
  play_button->click();
  CHECK(top_level_visibility(window) == (std::vector<bool>{false, false, false, true}));
  CHECK(process_events_until(
      [&window] { return top_level_visibility(window) == (std::vector<bool>{false, false, true, false}); }));
  play_button->click();
  CHECK(top_level_visibility(window) == (std::vector<bool>{true, false, true, true}));

  // Clicking the layers-panel film button again closes the panel (WA_DeleteOnClose
  // destroys it, so only the guard may be inspected afterwards).
  QPointer<patchy::ui::AnimationPreviewWindow> panel_guard(panel);
  animation_button->click();
  QApplication::processEvents();
  CHECK(panel_guard == nullptr);
}

void ui_animation_preview_stops_on_tab_switch_and_close() {
  patchy::ui::MainWindow window;
  window.add_document_session(animation_test_document(), QStringLiteral("Animation A"));
  show_window(window);
  auto* canvas_a = patchy::ui::MainWindowTestAccess::canvas(window);
  CHECK(canvas_a != nullptr);

  auto* animation_button = window.findChild<QPushButton*>(QStringLiteral("layerAnimationButton"));
  CHECK(animation_button != nullptr);
  animation_button->click();
  QApplication::processEvents();
  auto* panel = window.findChild<patchy::ui::AnimationPreviewWindow*>(QStringLiteral("animationPreviewWindow"));
  CHECK(panel != nullptr);
  auto* play_button = panel->findChild<QPushButton*>(QStringLiteral("animationPlayButton"));
  CHECK(play_button != nullptr);

  play_button->click();
  CHECK(panel->playing());
  CHECK(top_level_visibility(window) == (std::vector<bool>{false, false, false, true}));

  // Opening a second document switches tabs; playback stops and document A's visibility
  // is restored while it is still the outgoing active document.
  window.add_document_session(patchy::Document(8, 8, patchy::PixelFormat::rgba8()),
                              QStringLiteral("Animation B"));
  QApplication::processEvents();
  CHECK(!panel->playing());
  {
    const auto* document_a = patchy::ui::MainWindowTestAccess::document_for_canvas(window, canvas_a);
    CHECK(document_a != nullptr);
    const auto& layers = std::as_const(*document_a).layers();
    CHECK(layers[0].visible());
    CHECK(!layers[1].visible());
    CHECK(layers[2].visible());
    CHECK(layers[3].visible());
  }

  // Back on document A: closing the panel mid-play restores too (done() stops playback;
  // WA_DeleteOnClose then destroys the panel, so only the guard may be inspected).
  patchy::ui::MainWindowTestAccess::activate_canvas(window, canvas_a);
  QApplication::processEvents();
  play_button->click();
  CHECK(panel->playing());
  CHECK(top_level_visibility(window) == (std::vector<bool>{false, false, false, true}));
  auto* chrome_close = panel->findChild<QToolButton*>(QStringLiteral("dialogChromeCloseButton"));
  CHECK(chrome_close != nullptr);
  QPointer<patchy::ui::AnimationPreviewWindow> closed_panel_guard(panel);
  chrome_close->click();
  QApplication::processEvents();
  CHECK(closed_panel_guard == nullptr);
  CHECK(top_level_visibility(window) == (std::vector<bool>{true, false, true, true}));

  // Closing the playing document's tab stops playback before the document is destroyed.
  animation_button->click();
  QApplication::processEvents();
  panel = window.findChild<patchy::ui::AnimationPreviewWindow*>(QStringLiteral("animationPreviewWindow"));
  CHECK(panel != nullptr);
  play_button = panel->findChild<QPushButton*>(QStringLiteral("animationPlayButton"));
  play_button->click();
  CHECK(panel->playing());
  CHECK(patchy::ui::MainWindowTestAccess::close_document_tab(window, 0));
  QApplication::processEvents();
  CHECK(!panel->playing());
}

}  // namespace

std::vector<patchy::test::TestCase> layer_panel_organization_tests_animation_part() {
  return {
      {"ui_animation_preview_plays_visible_layers_and_restores",
       ui_animation_preview_plays_visible_layers_and_restores},
      {"ui_animation_preview_stops_on_tab_switch_and_close",
       ui_animation_preview_stops_on_tab_switch_and_close},
  };
}
