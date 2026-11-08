// The shortcut sets' tool modifiers (GitHub issue 302; docs/hotkey-sets.md "Tool modifiers"):
// GIMP-style and Krita-style move the gestures their program's manual gives Ctrl, Patchy
// default and Photoshop-style keep Alt.
#include "core/document.hpp"
#include "ui/canvas_types.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "ui/modifier_names.hpp"
#include "ui/tool_cursors.hpp"
#include "ui/tool_modifier_style.hpp"
#include "test_harness.hpp"
#include <QAction>
#include <QApplication>
#include <QCursor>
#include <QKeyEvent>
#include <QStatusBar>
#include <cmath>
#include <utility>
#include <vector>
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

namespace {

using namespace patchy::test::ui;
using patchy::ui::CanvasTool;
using patchy::ui::ToolModifierRole;
using patchy::ui::ToolModifierStyle;

// Sets the canvas's tool modifiers for one test and puts the previous ones back.
class ToolModifierStyleScope {
public:
  explicit ToolModifierStyleScope(ToolModifierStyle style) : saved_(patchy::ui::active_tool_modifier_style()) {
    patchy::ui::set_active_tool_modifier_style(style);
  }
  ~ToolModifierStyleScope() { patchy::ui::set_active_tool_modifier_style(saved_); }
  ToolModifierStyleScope(const ToolModifierStyleScope&) = delete;
  ToolModifierStyleScope& operator=(const ToolModifierStyleScope&) = delete;

private:
  ToolModifierStyle saved_;
};

// A small canvas on one opaque layer, shown at 100%.
struct GestureCanvas {
  patchy::Document document{120, 80, patchy::PixelFormat::rgba8()};
  patchy::ui::CanvasWidget canvas;
  patchy::LayerId layer_id{};

  GestureCanvas() {
    auto pixels = solid_pixels(120, 80, patchy::PixelFormat::rgba8(), QColor(180, 140, 100, 255));
    layer_id = document.add_pixel_layer("Photo", std::move(pixels)).id();
    canvas.resize(360, 240);
    canvas.set_document(&document);
    canvas.set_zoom(1.0);
    canvas.show();
    QApplication::processEvents();
  }
  [[nodiscard]] QPoint at(QPoint document_point) const { return canvas.widget_position_for_document_point(document_point); }
  void click(QPoint document_point, Qt::KeyboardModifiers modifiers) {
    send_mouse(canvas, QEvent::MouseButtonPress, at(document_point), Qt::LeftButton, Qt::LeftButton, modifiers);
    send_mouse(canvas, QEvent::MouseButtonRelease, at(document_point), Qt::LeftButton, Qt::NoButton, modifiers);
    QApplication::processEvents();
  }
  void drag(QPoint from, QPoint to, Qt::KeyboardModifiers modifiers) {
    send_mouse(canvas, QEvent::MouseButtonPress, at(from), Qt::LeftButton, Qt::LeftButton, modifiers);
    send_mouse(canvas, QEvent::MouseMove, at((from + to) / 2), Qt::LeftButton, Qt::LeftButton, modifiers);
    send_mouse(canvas, QEvent::MouseMove, at(to), Qt::LeftButton, Qt::LeftButton, modifiers);
    send_mouse(canvas, QEvent::MouseButtonRelease, at(to), Qt::LeftButton, Qt::NoButton, modifiers);
    QApplication::processEvents();
  }
};

// A Zoom tool click with `modifiers` from 100%: true when it zoomed out.
bool zoom_click_zooms_out(Qt::KeyboardModifiers modifiers) {
  GestureCanvas f;
  f.canvas.set_tool(CanvasTool::Zoom);
  f.canvas.set_zoom_scrubby(false);
  f.click(QPoint(60, 40), modifiers);
  return f.canvas.zoom() < 0.99;
}

// A wheel notch with `modifiers` (wheel zoom off): true when it zoomed.
bool wheel_zooms(Qt::KeyboardModifiers modifiers) {
  GestureCanvas f;
  f.canvas.set_wheel_zooms(false);
  send_wheel(f.canvas, f.at(QPoint(60, 40)), 120, modifiers);
  return std::abs(f.canvas.zoom() - 1.0) > 0.01;
}

// Select All, then a Rectangular Marquee drag with `modifiers` over the left half: true when
// the drag took that half out of the selection.
bool marquee_drag_subtracts(Qt::KeyboardModifiers modifiers) {
  GestureCanvas f;
  f.canvas.set_tool(CanvasTool::Marquee);
  f.canvas.select_all();
  f.drag(QPoint(0, 0), QPoint(60, 80), modifiers);
  return !f.canvas.selection_contains(QPoint(20, 40)) && f.canvas.selection_contains(QPoint(100, 40));
}

// A Clone click with `modifiers`: true when it set the source.
bool clone_click_sets_source(Qt::KeyboardModifiers modifiers) {
  GestureCanvas f;
  f.canvas.set_tool(CanvasTool::Clone);
  f.canvas.set_brush_size(5);
  f.click(QPoint(30, 30), modifiers);
  const auto slot = f.canvas.clone_source_slot_state(f.canvas.clone_source_slot());
  return slot.set && slot.point == QPoint(30, 30);
}

// A Brush click with `modifiers` over a filled document: true when it picked the color.
bool brush_click_picks_color(CanvasTool tool, Qt::KeyboardModifiers modifiers) {
  // A MainWindow loads the stored tool modifiers at startup: put the test's back afterwards.
  const auto style = patchy::ui::active_tool_modifier_style();
  auto&& [window, canvas] = ShownWindowFixture{};
  patchy::ui::set_active_tool_modifier_style(style);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(12, 180, 240));
  use_solid_fill_settings(canvas);
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  canvas->set_tool(tool);
  canvas->set_primary_color(QColor(230, 20, 20));
  const auto at = canvas->widget_position_for_document_point(QPoint(80, 80));
  send_mouse(*canvas, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton, modifiers);
  send_mouse(*canvas, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton, modifiers);
  QApplication::processEvents();
  return color_close(canvas->primary_color(), QColor(12, 180, 240), 4);
}

// The cursor a painting tool shows while `key` alone is held.
QCursor cursor_while_held(CanvasTool tool, Qt::Key key) {
  GestureCanvas f;
  f.canvas.set_tool(tool);
  f.canvas.set_brush_size(9);
  if (tool == CanvasTool::Clone || tool == CanvasTool::Healing) {
    f.canvas.set_clone_source(QPoint(10, 10));
  }
  send_mouse(f.canvas, QEvent::MouseMove, f.at(QPoint(60, 40)), Qt::NoButton, Qt::NoButton);
  QApplication::processEvents();
  send_key_press(f.canvas, key);
  const auto cursor = f.canvas.cursor();
  send_key_release(f.canvas, key, key == Qt::Key_Alt ? Qt::AltModifier : Qt::ControlModifier);
  return cursor;
}

bool is_eyedropper(const QCursor& cursor) {
  const auto expected = patchy::ui::eyedropper_cursor();
  return cursor.shape() == Qt::BitmapCursor && cursor.hotSpot() == expected.hotSpot() &&
         cursor.pixmap().toImage() == expected.pixmap().toImage();
}

// The Clone and Healing source cursor (its hot spot sits at 12, 12; GitHub issue 380).
bool is_source_cursor(const QCursor& cursor) {
  return cursor.shape() == Qt::BitmapCursor && cursor.hotSpot() == QPoint(12, 12);
}

void ui_tool_modifier_style_maps_roles_per_set() {
  using patchy::ui::tool_modifier;
  constexpr auto alt = Qt::AltModifier;
  constexpr auto ctrl = Qt::ControlModifier;
  // Patchy default and Photoshop-style: every gesture on Alt, the temporary Move on Ctrl.
  CHECK(patchy::ui::builtin_set_tool_modifier_style(u"patchy") == ToolModifierStyle::Patchy);
  CHECK(patchy::ui::builtin_set_tool_modifier_style(u"photoshop") == ToolModifierStyle::Patchy);
  CHECK(patchy::ui::builtin_set_tool_modifier_style(u"gimp") == ToolModifierStyle::Gimp);
  CHECK(patchy::ui::builtin_set_tool_modifier_style(u"krita") == ToolModifierStyle::Krita);
  const auto patchy_style = ToolModifierStyle::Patchy;
  for (const auto tool : {CanvasTool::Brush, CanvasTool::Eraser, CanvasTool::Fill, CanvasTool::MixerBrush}) {
    CHECK(tool_modifier(ToolModifierRole::ColorPick, tool, patchy_style) == alt);
  }
  CHECK(tool_modifier(ToolModifierRole::ZoomOut, CanvasTool::Zoom, patchy_style) == alt);
  CHECK(tool_modifier(ToolModifierRole::WheelZoom, CanvasTool::Zoom, patchy_style) == alt);
  CHECK(tool_modifier(ToolModifierRole::RetouchSource, CanvasTool::Clone, patchy_style) == alt);
  CHECK(tool_modifier(ToolModifierRole::ToningSwap, CanvasTool::Dodge, patchy_style) == alt);
  CHECK(tool_modifier(ToolModifierRole::SelectionSubtract, CanvasTool::Marquee, patchy_style) == alt);
  CHECK(tool_modifier(ToolModifierRole::TemporaryMove, CanvasTool::Brush, patchy_style) == ctrl);
  CHECK(tool_modifier(ToolModifierRole::TemporaryMove, CanvasTool::Clone, patchy_style) == ctrl);

  // GIMP-style: the GIMP 3.0 manual's Ctrl gestures.
  const auto gimp = ToolModifierStyle::Gimp;
  CHECK(tool_modifier(ToolModifierRole::ZoomOut, CanvasTool::Zoom, gimp) == ctrl);
  CHECK(tool_modifier(ToolModifierRole::WheelZoom, CanvasTool::Zoom, gimp) == ctrl);
  CHECK(tool_modifier(ToolModifierRole::ColorPick, CanvasTool::Brush, gimp) == ctrl);
  CHECK(tool_modifier(ToolModifierRole::ColorPick, CanvasTool::Eraser, gimp) == ctrl);
  CHECK(tool_modifier(ToolModifierRole::ColorPick, CanvasTool::Fill, gimp) == ctrl);
  CHECK(tool_modifier(ToolModifierRole::ColorPick, CanvasTool::MixerBrush, gimp) == alt);
  CHECK(tool_modifier(ToolModifierRole::ColorPick, CanvasTool::Gradient, gimp) == alt);
  CHECK(tool_modifier(ToolModifierRole::RetouchSource, CanvasTool::Healing, gimp) == ctrl);
  CHECK(tool_modifier(ToolModifierRole::ToningSwap, CanvasTool::BlurBrush, gimp) == ctrl);
  CHECK(tool_modifier(ToolModifierRole::SelectionSubtract, CanvasTool::Lasso, gimp) == ctrl);
  CHECK(tool_modifier(ToolModifierRole::TemporaryMove, CanvasTool::Brush, gimp) == alt);
  CHECK(tool_modifier(ToolModifierRole::TemporaryMove, CanvasTool::Clone, gimp) == alt);
  CHECK(tool_modifier(ToolModifierRole::TemporaryMove, CanvasTool::Dodge, gimp) == alt);
  CHECK(tool_modifier(ToolModifierRole::TemporaryMove, CanvasTool::MixerBrush, gimp) == ctrl);
  CHECK(tool_modifier(ToolModifierRole::TemporaryMove, CanvasTool::Sponge, gimp) == ctrl);

  // Krita-style: Ctrl zooms out, samples and sets the clone source; selections keep Alt.
  const auto krita = ToolModifierStyle::Krita;
  CHECK(tool_modifier(ToolModifierRole::ZoomOut, CanvasTool::Zoom, krita) == ctrl);
  CHECK(tool_modifier(ToolModifierRole::WheelZoom, CanvasTool::Zoom, krita) == alt);
  for (const auto tool : {CanvasTool::Brush, CanvasTool::MixerBrush, CanvasTool::Smudge, CanvasTool::Eraser}) {
    CHECK(tool_modifier(ToolModifierRole::ColorPick, tool, krita) == ctrl);
    CHECK(tool_modifier(ToolModifierRole::TemporaryMove, tool, krita) == alt);
  }
  CHECK(tool_modifier(ToolModifierRole::ColorPick, CanvasTool::Fill, krita) == alt);
  CHECK(tool_modifier(ToolModifierRole::RetouchSource, CanvasTool::Clone, krita) == ctrl);
  CHECK(tool_modifier(ToolModifierRole::ToningSwap, CanvasTool::Burn, krita) == alt);
  CHECK(tool_modifier(ToolModifierRole::TemporaryMove, CanvasTool::Burn, krita) == ctrl);
  CHECK(tool_modifier(ToolModifierRole::SelectionSubtract, CanvasTool::Marquee, krita) == alt);

  // Persisted tokens round-trip; anything else reads as Patchy's.
  for (const auto style : {ToolModifierStyle::Patchy, ToolModifierStyle::Gimp, ToolModifierStyle::Krita}) {
    CHECK(patchy::ui::tool_modifier_style_from_token(patchy::ui::tool_modifier_style_token(style)) == style);
  }
  CHECK(patchy::ui::tool_modifier_style_token(ToolModifierStyle::Gimp) == QStringLiteral("gimp"));
  CHECK(patchy::ui::tool_modifier_style_token(ToolModifierStyle::Krita) == QStringLiteral("krita"));
  CHECK(patchy::ui::tool_modifier_style_token(ToolModifierStyle::Patchy) == QStringLiteral("patchy"));
  CHECK(patchy::ui::tool_modifier_style_from_token(u"paint-shop") == ToolModifierStyle::Patchy);

  // Sentences name the role's key for the tool.
  const ToolModifierStyleScope scope(ToolModifierStyle::Gimp);
  const auto text = QStringLiteral("%SUBTRACT_KEY%+drag subtracts, %PICK_KEY%+click picks, %ALT%+right-drag");
  CHECK(patchy::ui::resolve_tool_modifier_names(text, CanvasTool::Marquee).startsWith(
      patchy::ui::ctrl_key_name() + QStringLiteral("+drag subtracts")));
  CHECK(patchy::ui::resolve_tool_modifier_names(text, CanvasTool::Brush)
            .endsWith(patchy::ui::ctrl_key_name() + QStringLiteral("+click picks, ") + patchy::ui::alt_key_name() +
                      QStringLiteral("+right-drag")));
  CHECK(patchy::ui::resolve_tool_modifier_names(text, CanvasTool::MixerBrush)
            .contains(patchy::ui::alt_key_name() + QStringLiteral("+click picks")));
}

// GIMP-style on the canvas: Ctrl zooms out, zooms with the wheel, picks with the Brush, sets
// the clone source, subtracts from a selection; Alt is the Brush's and Clone's temporary Move.
void ui_tool_modifier_style_gimp_moves_gestures_to_ctrl() {
  const ToolModifierStyleScope scope(ToolModifierStyle::Gimp);
  CHECK(zoom_click_zooms_out(Qt::ControlModifier));
  CHECK(!zoom_click_zooms_out(Qt::AltModifier));
  CHECK(wheel_zooms(Qt::ControlModifier));
  CHECK(!wheel_zooms(Qt::AltModifier));
  CHECK(marquee_drag_subtracts(Qt::ControlModifier));
  CHECK(!marquee_drag_subtracts(Qt::AltModifier));
  CHECK(clone_click_sets_source(Qt::ControlModifier));
  CHECK(!clone_click_sets_source(Qt::AltModifier));
  CHECK(brush_click_picks_color(CanvasTool::Brush, Qt::ControlModifier));
  CHECK(!brush_click_picks_color(CanvasTool::Brush, Qt::AltModifier));
  // The Mixer Brush is not one of GIMP's paint tools: it keeps Alt.
  CHECK(brush_click_picks_color(CanvasTool::MixerBrush, Qt::AltModifier));
  // Cursors: the four-way Move arrow under Alt, the eyedropper or source cursor under Ctrl.
  CHECK(cursor_while_held(CanvasTool::Brush, Qt::Key_Alt).shape() == Qt::SizeAllCursor);
  CHECK(is_eyedropper(cursor_while_held(CanvasTool::Brush, Qt::Key_Control)));
  CHECK(cursor_while_held(CanvasTool::Clone, Qt::Key_Alt).shape() == Qt::SizeAllCursor);
  CHECK(is_source_cursor(cursor_while_held(CanvasTool::Clone, Qt::Key_Control)));

  // An Alt drag with the Brush moves the layer instead of painting.
  GestureCanvas f;
  f.canvas.set_tool(CanvasTool::Brush);
  f.canvas.set_brush_size(5);
  const auto before = std::as_const(f.document).find_layer(f.layer_id)->bounds();
  f.drag(QPoint(40, 40), QPoint(50, 40), Qt::AltModifier);
  const auto after = std::as_const(f.document).find_layer(f.layer_id)->bounds();
  CHECK(after.x == before.x + 10);

  // Ctrl swaps Dodge and Burn: the status bar names the swap while it is held.
  QString status;
  f.canvas.set_status_callback([&status](QString message) { status = std::move(message); });
  f.canvas.set_tool(CanvasTool::Dodge);
  QKeyEvent ctrl_down(QEvent::KeyPress, Qt::Key_Control, Qt::NoModifier);
  QApplication::sendEvent(&f.canvas, &ctrl_down);
  CHECK(status.contains(QStringLiteral("Burn")));
  CHECK(status.contains(patchy::ui::ctrl_key_name()));
  QKeyEvent ctrl_up(QEvent::KeyRelease, Qt::Key_Control, Qt::ControlModifier);
  QApplication::sendEvent(&f.canvas, &ctrl_up);
  CHECK(status == QStringLiteral("Dodge"));
}

// Krita-style: Ctrl zooms out, picks and sets the clone source, but Alt still subtracts and
// the wheel keeps Patchy's keys.
void ui_tool_modifier_style_krita_keeps_alt_for_selections() {
  const ToolModifierStyleScope scope(ToolModifierStyle::Krita);
  CHECK(zoom_click_zooms_out(Qt::ControlModifier));
  CHECK(clone_click_sets_source(Qt::ControlModifier));
  CHECK(brush_click_picks_color(CanvasTool::Brush, Qt::ControlModifier));
  CHECK(marquee_drag_subtracts(Qt::AltModifier));
  CHECK(!marquee_drag_subtracts(Qt::ControlModifier));
  CHECK(wheel_zooms(Qt::AltModifier));
}

// Patchy default (and Photoshop-style) keep every gesture where it was.
void ui_tool_modifier_style_patchy_default_keeps_alt() {
  const ToolModifierStyleScope scope(ToolModifierStyle::Patchy);
  CHECK(zoom_click_zooms_out(Qt::AltModifier));
  CHECK(!zoom_click_zooms_out(Qt::ControlModifier));
  CHECK(wheel_zooms(Qt::AltModifier));
  CHECK(!wheel_zooms(Qt::ControlModifier));
  CHECK(marquee_drag_subtracts(Qt::AltModifier));
  CHECK(!marquee_drag_subtracts(Qt::ControlModifier));
  CHECK(clone_click_sets_source(Qt::AltModifier));
  CHECK(!clone_click_sets_source(Qt::ControlModifier));
  CHECK(brush_click_picks_color(CanvasTool::Brush, Qt::AltModifier));
  CHECK(!brush_click_picks_color(CanvasTool::Brush, Qt::ControlModifier));
  CHECK(cursor_while_held(CanvasTool::Brush, Qt::Key_Control).shape() == Qt::SizeAllCursor);
  CHECK(is_eyedropper(cursor_while_held(CanvasTool::Brush, Qt::Key_Alt)));
  CHECK(cursor_while_held(CanvasTool::Clone, Qt::Key_Control).shape() == Qt::SizeAllCursor);
  CHECK(is_source_cursor(cursor_while_held(CanvasTool::Clone, Qt::Key_Alt)));
}

// The status-bar hints name the key the set gives each gesture.
void ui_tool_modifier_style_hints_name_the_sets_keys() {
  patchy::ui::MainWindow window;
  show_window(window);
  QApplication::processEvents();
  // After the window: it loads the stored tool modifiers at startup.
  const ToolModifierStyleScope scope(ToolModifierStyle::Gimp);
  const auto ctrl = patchy::ui::ctrl_key_name();
  const auto alt = patchy::ui::alt_key_name();
  const auto hint = [&window](const char* action) {
    require_action(window, action)->trigger();
    QApplication::processEvents();
    return window.statusBar()->currentMessage();
  };
  const auto brush = hint("toolBrushAction");
  CHECK(brush.contains(ctrl + QStringLiteral("+click picks a color")));
  CHECK(brush.endsWith(alt + QStringLiteral("+right-drag changes size and softness.")));
  CHECK(hint("toolMixerBrushAction").contains(alt + QStringLiteral("+click picks a color")));
  CHECK(hint("toolCloneAction").contains(ctrl + QStringLiteral("+click sets the source")));
  CHECK(hint("toolDodgeAction").contains(ctrl + QStringLiteral("+drag paints with Burn")));
  const auto marquee = hint("toolMarqueeAction");
  CHECK(marquee.contains(ctrl + QStringLiteral("+drag subtracts")));
  CHECK(marquee.contains(QStringLiteral("Shift+") + ctrl + QStringLiteral("+drag intersects")));
  const auto lasso = hint("toolLassoAction");
  CHECK(lasso.contains(ctrl + QStringLiteral("+drag subtracts")));
  CHECK(lasso.contains(QStringLiteral("Press ") + alt + QStringLiteral(" mid-drag")));
  require_action(window, "toolMoveAction")->trigger();
  QApplication::processEvents();
}

}  // namespace

std::vector<patchy::test::TestCase> tool_modifier_style_tests() {
  return {
      {"ui_tool_modifier_style_maps_roles_per_set", ui_tool_modifier_style_maps_roles_per_set},
      {"ui_tool_modifier_style_gimp_moves_gestures_to_ctrl", ui_tool_modifier_style_gimp_moves_gestures_to_ctrl},
      {"ui_tool_modifier_style_krita_keeps_alt_for_selections", ui_tool_modifier_style_krita_keeps_alt_for_selections},
      {"ui_tool_modifier_style_patchy_default_keeps_alt", ui_tool_modifier_style_patchy_default_keeps_alt},
      {"ui_tool_modifier_style_hints_name_the_sets_keys", ui_tool_modifier_style_hints_name_the_sets_keys},
  };
}
