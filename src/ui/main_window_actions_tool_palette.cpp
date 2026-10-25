// MainWindow::build_tool_palette(): the tool-palette phase of create_actions()
// (the left QToolBar, tool actions and flyouts, color buttons and the quick-
// mask button), split out of main_window_actions.cpp along with
// add_tool_action (this TU is its only caller) and the anonymous-namespace
// tool helpers only they use.
// Pure function moves; behavior must stay identical, and the construction
// order is load-bearing (see create_actions() for the phase order).

#include "ui/hotkey_registry.hpp"
#include "ui/main_window.hpp"
#include <QLayout>
#include <QStatusBar>
#include "ui/canvas_widget.hpp"
#include "ui/canvas_widget_shared.hpp"
#include "ui/icon_theme.hpp"
#include "ui/theme_palette.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/main_window_actions_internal.hpp"

#include "ai/segmentation_engine.hpp"

#include "ui/action_icons.hpp"
#include "ui/brush_tip_manager_dialog.hpp"
#include "ui/modifier_names.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/document_float_window.hpp"
#include "ui/color_panel.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/pattern_manager_dialog.hpp"
#include "ui/scanner_import.hpp"
#include "ui/tile_preview_window.hpp"
#include "ui/warp_text_dialog.hpp"
#include "ui/update_checker.hpp"
#include "ui/tool_palette_footer.hpp"

#include <QAction>
#include <QActionGroup>
#include <QEvent>
#include <QCoreApplication>
#include <QIcon>
#include <QKeySequence>
#include <QLatin1Char>
#include <QLatin1String>
#include <QList>
#include <QMainWindow>
#include <QMenu>
#include <QMouseEvent>
#include <QObject>
#include <QPainter>
#include <QPoint>
#include <QPointer>
#include <QPushButton>
#include <QRect>
#include <QScreen>
#include <QSize>
#include <QString>
#include <QStringLiteral>
#include <QTextEdit>
#include <QTimer>
#include <QVariant>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>
#include <Qt>
#include <QtGlobal>

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <qlatin1stringview.h>
#include <utility>


// Icon resources live in the static patchy_ui library; force registration before first use.
int qInitResources_icons();

namespace patchy::ui {

namespace {

const char* tool_action_source(CanvasTool tool) {
  switch (tool) {
    case CanvasTool::Move:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Move");
    case CanvasTool::Marquee:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Marquee");
    case CanvasTool::EllipticalMarquee:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Elliptical Marquee");
    case CanvasTool::Lasso:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Lasso");
    case CanvasTool::PolygonLasso:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Polygonal Lasso");
    case CanvasTool::MagneticLasso:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Magnetic Lasso");
    case CanvasTool::MagicWand:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Magic Wand");
    case CanvasTool::QuickSelect:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Quick Select");
    case CanvasTool::ObjectSelect:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Object Selection");
    case CanvasTool::Brush:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Brush");
    case CanvasTool::MixerBrush:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Mixer Brush");
    case CanvasTool::Clone:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Clone");
    case CanvasTool::PatternStamp:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Pattern Stamp");
    case CanvasTool::Healing:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Healing Brush");
    case CanvasTool::Smudge:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Smudge");
    case CanvasTool::Dodge:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Dodge");
    case CanvasTool::Burn:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Burn");
    case CanvasTool::Sponge:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Sponge");
    case CanvasTool::BlurBrush:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Blur");
    case CanvasTool::SharpenBrush:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Sharpen");
    case CanvasTool::Eraser:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Eraser");
    case CanvasTool::Gradient:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Gradient");
    case CanvasTool::Line:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Line");
    case CanvasTool::Rectangle:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Rect");
    case CanvasTool::Ellipse:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Ellipse");
    case CanvasTool::Fill:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Fill");
    case CanvasTool::Eyedropper:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Pick");
    case CanvasTool::Text:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Type");
    case CanvasTool::Pan:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Hand");
    case CanvasTool::Zoom:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Zoom");
    case CanvasTool::Pen:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Pen");
    case CanvasTool::PathSelect:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Path Select");
    case CanvasTool::DirectSelect:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Direct Select");
    case CanvasTool::Polygon:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Polygon");
    case CanvasTool::CustomShape:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Custom Shape");
    case CanvasTool::SpotHealing:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Spot Healing");
    case CanvasTool::PatchTool:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Patch");
    case CanvasTool::RedEye:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Red Eye");
    case CanvasTool::Crop:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Crop");
    case CanvasTool::PerspectiveCrop:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Perspective Crop");
    case CanvasTool::AddAnchor:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Add Anchor");
    case CanvasTool::DeleteAnchor:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Delete Anchor");
    case CanvasTool::ConvertPoint:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Convert Point");
    case CanvasTool::Artboard:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Artboard");
  }
  return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Tool");
}

}  // namespace

QString tool_hotkey_id(CanvasTool tool) {
  switch (tool) {
    case CanvasTool::Move:
      return QStringLiteral("tools.move");
    case CanvasTool::Marquee:
      return QStringLiteral("tools.marquee");
    case CanvasTool::EllipticalMarquee:
      return QStringLiteral("tools.elliptical_marquee");
    case CanvasTool::Lasso:
      return QStringLiteral("tools.lasso");
    case CanvasTool::PolygonLasso:
      return QStringLiteral("tools.polygonal_lasso");
    case CanvasTool::MagneticLasso:
      return QStringLiteral("tools.magnetic_lasso");
    case CanvasTool::MagicWand:
      return QStringLiteral("tools.magic_wand");
    case CanvasTool::QuickSelect:
      return QStringLiteral("tools.quick_select");
    case CanvasTool::ObjectSelect:
      return QStringLiteral("tools.object_select");
    case CanvasTool::Brush:
      return QStringLiteral("tools.brush");
    case CanvasTool::MixerBrush:
      return QStringLiteral("tools.mixer_brush");
    case CanvasTool::Clone:
      return QStringLiteral("tools.clone");
    case CanvasTool::PatternStamp:
      return QStringLiteral("tools.pattern_stamp");
    case CanvasTool::Healing:
      return QStringLiteral("tools.healing");
    case CanvasTool::Smudge:
      return QStringLiteral("tools.smudge");
    case CanvasTool::Dodge:
      return QStringLiteral("tools.dodge");
    case CanvasTool::Burn:
      return QStringLiteral("tools.burn");
    case CanvasTool::Sponge:
      return QStringLiteral("tools.sponge");
    case CanvasTool::BlurBrush:
      return QStringLiteral("tools.blur");
    case CanvasTool::SharpenBrush:
      return QStringLiteral("tools.sharpen");
    case CanvasTool::Eraser:
      return QStringLiteral("tools.eraser");
    case CanvasTool::Gradient:
      return QStringLiteral("tools.gradient");
    case CanvasTool::Line:
      return QStringLiteral("tools.line");
    case CanvasTool::Rectangle:
      return QStringLiteral("tools.rect");
    case CanvasTool::Ellipse:
      return QStringLiteral("tools.ellipse");
    case CanvasTool::Fill:
      return QStringLiteral("tools.fill");
    case CanvasTool::Eyedropper:
      return QStringLiteral("tools.eyedropper");
    case CanvasTool::Text:
      return QStringLiteral("tools.type");
    case CanvasTool::Pan:
      return QStringLiteral("tools.hand");
    case CanvasTool::Zoom:
      return QStringLiteral("tools.zoom");
    case CanvasTool::Pen:
      return QStringLiteral("tools.pen");
    case CanvasTool::PathSelect:
      return QStringLiteral("tools.path_select");
    case CanvasTool::DirectSelect:
      return QStringLiteral("tools.direct_select");
    case CanvasTool::Polygon:
      return QStringLiteral("tools.polygon");
    case CanvasTool::CustomShape:
      return QStringLiteral("tools.custom_shape");
    case CanvasTool::SpotHealing:
      return QStringLiteral("tools.spot_healing");
    case CanvasTool::PatchTool:
      return QStringLiteral("tools.patch");
    case CanvasTool::RedEye:
      return QStringLiteral("tools.red_eye");
    case CanvasTool::Crop:
      return QStringLiteral("tools.crop");
    case CanvasTool::PerspectiveCrop:
      return QStringLiteral("tools.perspective_crop");
    case CanvasTool::AddAnchor:
      return QStringLiteral("tools.add_anchor");
    case CanvasTool::DeleteAnchor:
      return QStringLiteral("tools.delete_anchor");
    case CanvasTool::ConvertPoint:
      return QStringLiteral("tools.convert_point");
    case CanvasTool::Artboard:
      return QStringLiteral("tools.artboard");
  }
  return QStringLiteral("tools.unknown");
}

namespace {

// Second tooltip line for the tools whose gestures are not obvious from the
// name; the hotkey registry composes "Name (Key)" above it and translates
// the source at compose time (kActionTooltipDetailProperty). nullptr keeps
// the plain "Name (Key)" tooltip.
const char* tool_tooltip_detail_source(CanvasTool tool) {
  switch (tool) {
    case CanvasTool::Move:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "%CTRL%+click selects a layer, Shift+click toggles it. %CTRL%+drag selects layers in a "
             "rectangle; hold Shift before dragging to add. %ALT%+drag duplicates. Shift constrains layer movement.");
    case CanvasTool::Pen:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Click to place points, drag for curves, Shift keeps 45 degrees. On a path: click a "
             "segment to add a point, click a point to delete it or an open end to continue it, %ALT%+click "
             "converts it, %CTRL% moves points, %CTRL%+click away ends the path.");
    case CanvasTool::PathSelect:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Select and move whole shapes. Shift+click adds or removes a shape, %ALT%+drag "
             "duplicates it, %CTRL% switches to Direct Select. %CTRL%+T transforms the selected shapes.");
    case CanvasTool::DirectSelect:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Select and drag points and handles. %ALT%+click selects a whole subpath, "
             "%ALT%+drag on a handle breaks the pair, %CTRL% switches to Path Select. Delete removes the selected points.");
    case CanvasTool::AddAnchor:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Click a path segment to insert a point.");
    case CanvasTool::DeleteAnchor:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Click a point to remove it.");
    case CanvasTool::ConvertPoint:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Click a point to switch it between corner and smooth.");
    case CanvasTool::Artboard:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Drag on empty space to add an artboard, drag an artboard to move it with its "
             "content (%ALT% copies it), drag a handle to resize it, click + to add one beside it.");
    case CanvasTool::PolygonLasso:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Click to place points, Shift keeps 45 degrees, %ALT%+drag draws freehand. "
             "Click the start point, double-click or press Enter to close.");
    case CanvasTool::PerspectiveCrop:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Drag a box or click four corners around a skewed area, then drag the "
             "corners or sides onto its edges. The arrow marks the top of the result; Enter straightens it into a "
             "rectangle.");
    default:
      return nullptr;
  }
}

// Status-bar sentence shown when the tool is picked; nullptr shows the tool
// name. Reserved for tools whose workflow the name alone does not explain.
const char* tool_activation_hint_source(CanvasTool tool) {
  switch (tool) {
    case CanvasTool::Move:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Move: %CTRL%+click selects a layer, Shift+click toggles it. %CTRL%+drag selects a "
             "rectangle; Shift adds. Drag selected artwork to move it, %ALT%+drag to duplicate it.");
    case CanvasTool::Pen:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Pen: click to add points, drag for curves. On a path, click a segment to add a "
             "point, click a point to delete it, %ALT%+click converts it, %CTRL%+drag selects or "
             "moves points.");
    case CanvasTool::PathSelect:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Path Select: click a shape to select it, drag to move it, %ALT%+drag to duplicate "
             "it. %CTRL%+T transforms the path, Delete removes the selected shape.");
    case CanvasTool::DirectSelect:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Direct Select: click or marquee points, drag points or handles. Shift adds, "
             "arrows nudge, Delete removes, %CTRL%+T transforms the selected points.");
    case CanvasTool::Crop:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Crop: drag the handles to crop or extend the canvas (%ALT% resizes about the "
             "center), drag inside to lay out a new box, drag outside it to rotate. Enter crops, Esc "
             "resets the box.");
    case CanvasTool::PerspectiveCrop:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Perspective Crop: drag a box or click four corners, then drag the corners "
             "or sides onto the edges of the area. Drag inside to move it, just outside a corner to rotate it "
             "(Shift snaps to 15 degrees). The arrow marks the top of the result. Enter crops and straightens, Esc "
             "clears the corners.");
    case CanvasTool::AddAnchor:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Add Anchor Point: click a path segment to insert a point.");
    case CanvasTool::DeleteAnchor:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Delete Anchor Point: click a point to remove it.");
    case CanvasTool::ConvertPoint:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Convert Point: click a point to switch it between corner and smooth.");
    case CanvasTool::Artboard:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Artboard: drag on empty space to add an artboard, drag one or its name to move "
             "it with its content (%ALT% copies it), drag a handle to resize it, click + to add one beside it, "
             "double-click a name to rename it. Arrows nudge.");
    case CanvasTool::RedEye:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Red Eye: click a red pupil, or drag a box around the eye. "
             "Pupil Size takes in more of the reddish rim, Darken Amount sets how dark the pupil gets.");
    case CanvasTool::PatchTool:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Patch: draw around the area to fix, then drag the selection to a clean source area, or press Enter to remove the object automatically");
    // The selection tools name their Shift / Alt combinations (GitHub issue 192); the
    // cursor already shows the drag, resize and move gestures. Shift+Alt intersects
    // everywhere except Quick Select and Object Selection, which treat it as Add.
    case CanvasTool::Marquee:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Rectangular Marquee: drag to select. Shift+drag adds, %ALT%+drag subtracts, "
             "Shift+%ALT%+drag intersects. With nothing selected, Shift draws a square and %ALT% draws from the "
             "center.");
    case CanvasTool::EllipticalMarquee:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Elliptical Marquee: drag to select. Shift+drag adds, %ALT%+drag subtracts, "
             "Shift+%ALT%+drag intersects. With nothing selected, Shift draws a circle and %ALT% draws from the "
             "center.");
    case CanvasTool::Lasso:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Lasso: drag around the area to select. Shift+drag adds, %ALT%+drag subtracts, "
             "Shift+%ALT%+drag intersects. Press %ALT% mid-drag and let go of the button to click straight "
             "segments until %ALT% is released.");
    case CanvasTool::PolygonLasso:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Polygonal Lasso: click to place points. Start with Shift+click to add, "
             "%ALT%+click to subtract, Shift+%ALT%+click to intersect. Shift keeps 45 degrees, %ALT%+drag draws "
             "freehand. Click the start point, double-click or press Enter to close; Backspace removes the last "
             "point, Esc cancels.");
    case CanvasTool::MagneticLasso:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Magnetic Lasso: click an edge and follow it; click to pin a point. Start "
             "with Shift+click to add, %ALT%+click to subtract, Shift+%ALT%+click to intersect. Backspace "
             "removes the last point, Enter closes, Esc cancels.");
    case CanvasTool::MagicWand:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Magic Wand: click a color to select it. Shift+click adds, %ALT%+click subtracts, "
             "Shift+%ALT%+click intersects.");
    case CanvasTool::QuickSelect:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Quick Select: paint over the area to select; later strokes add to it. "
             "Shift+drag adds, %ALT%+drag subtracts.");
    case CanvasTool::ObjectSelect:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Object Selection: drag a box around an object or click it. Shift+drag "
             "adds, %ALT%+drag subtracts.");
    // The painting tools (GitHub issues 421, 423) say what a drag does and their own Alt
    // click; paint_tool_hint_tail_source adds the gestures they all share.
    case CanvasTool::Brush:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Brush: drag to paint, %ALT%+click picks a color.");
    case CanvasTool::MixerBrush:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Mixer Brush: drag to paint and blend with the colors already there, "
             "%ALT%+click picks a color.");
    case CanvasTool::PatternStamp:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Pattern Stamp: drag to paint with the pattern.");
    case CanvasTool::Eraser:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Eraser: drag to erase.");
    case CanvasTool::Clone:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Clone Stamp: %ALT%+click sets the source, then drag to paint the clone.");
    case CanvasTool::Healing:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Healing Brush: %ALT%+click sets the source, then drag over the area to heal.");
    case CanvasTool::SpotHealing:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Spot Healing: drag over a spot to heal it from its surroundings.");
    case CanvasTool::Smudge:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Smudge: drag to smear the pixels.");
    case CanvasTool::Dodge:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Dodge: drag to lighten.");
    case CanvasTool::Burn:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Burn: drag to darken.");
    case CanvasTool::Sponge:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Sponge: drag to change the saturation.");
    case CanvasTool::BlurBrush:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Blur: drag to soften detail.");
    case CanvasTool::SharpenBrush:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Sharpen: drag to sharpen detail.");
    default:
      return nullptr;
  }
}

// The gestures every painting tool shares (tool_has_ctrl_temporary_move), appended to
// its own hint; nullptr for the other tools.
const char* paint_tool_hint_tail_source(CanvasTool tool) {
  if (!tool_has_ctrl_temporary_move(tool)) {
    return nullptr;
  }
  return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Shift+click draws a straight line from the last stroke. "
         "%CTRL%+click selects the layer under the pointer, %CTRL%+drag moves the layer, "
         "%ALT%+right-drag changes size and softness.");
}

QString tool_action_object_name(CanvasTool tool) {
  auto name = QString::fromLatin1(tool_action_source(tool));
  name.remove(QLatin1Char(' '));
  return QStringLiteral("tool") + name + QStringLiteral("Action");
}

class MouseDoubleClickFilter final : public QObject {
public:
  MouseDoubleClickFilter(std::function<void()> callback, QObject* parent)
      : QObject(parent), callback_(std::move(callback)) {}

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::MouseButtonDblClick) {
      auto* mouse_event = static_cast<QMouseEvent*>(event);
      if (mouse_event->button() == Qt::LeftButton) {
        if (callback_) {
          callback_();
        }
        mouse_event->accept();
        return true;
      }
    }
    return QObject::eventFilter(watched, event);
  }

private:
  std::function<void()> callback_;
};

// Right press on a tool palette button: a flyout button opens its flyout, a plain tool
// button selects its tool (GitHub issue 388). A flyout button also opens on a left
// double-click; plain buttons pass no double-click callback and keep Qt's handling.
class ToolButtonPressFilter final : public QObject {
public:
  ToolButtonPressFilter(std::function<void()> on_right_press, std::function<void()> on_left_double_click,
                        QObject* parent)
      : QObject(parent),
        on_right_press_(std::move(on_right_press)),
        on_left_double_click_(std::move(on_left_double_click)) {}

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    // The right press already acted. Qt follows it with a separate ContextMenu
    // event (on release on Windows, on press on macOS and X11) that would
    // otherwise bubble up to the main window and open the toolbar menu (on top
    // of the flyout, GitHub issue 356), so the button swallows it.
    if (event->type() == QEvent::ContextMenu) {
      event->accept();
      return true;
    }
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick) {
      auto* mouse_event = static_cast<QMouseEvent*>(event);
      if (mouse_event->button() == Qt::RightButton && event->type() == QEvent::MouseButtonPress) {
        if (on_right_press_) {
          on_right_press_();
        }
        mouse_event->accept();
        return true;
      }
      if (on_left_double_click_ && mouse_event->button() == Qt::LeftButton &&
          event->type() == QEvent::MouseButtonDblClick) {
        on_left_double_click_();
        mouse_event->accept();
        return true;
      }
    }
    return QObject::eventFilter(watched, event);
  }

private:
  std::function<void()> on_right_press_;
  std::function<void()> on_left_double_click_;
};

// Flyout row layout (GitHub issue 284). Qt draws a menu icon at the item's left edge plus
// the sheet's icon offset and starts the name after the item's left padding plus the icon
// column it reserves (icon + 4 px), so an offset of half of padding + 4 centers the icon
// between the menu edge and the names. The tool the button shows gets a small accent
// square in that left margin, just before its icon (issue 284 follow-up); every row keeps
// the same geometry, so icons and names line up whether marked or not.
constexpr int kFlyoutItemLeftPadding = 24;
constexpr int kFlyoutIconLeft = (kFlyoutItemLeftPadding + 4) / 2;
constexpr int kFlyoutMarkerSize = 6;
constexpr int kFlyoutMarkerGap = 3;

// Paints the marker over the menu: the menu paints itself first (the event is resent to
// it), then the square goes on the current tool's row, in the text color of a highlighted
// row so it stays visible there.
class ToolFlyoutCurrentMarker final : public QObject {
public:
  ToolFlyoutCurrentMarker(QToolButton* button, QMenu* menu) : QObject(menu), button_(button) {
    menu->installEventFilter(this);
  }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() != QEvent::Paint || painting_ || button_ == nullptr) {
      return QObject::eventFilter(watched, event);
    }
    auto* menu = static_cast<QMenu*>(watched);
    painting_ = true;
    QCoreApplication::sendEvent(menu, event);
    painting_ = false;
    auto* current = button_->defaultAction();
    const QRect row = current != nullptr && current->isVisible() ? menu->actionGeometry(current) : QRect();
    if (row.isValid()) {
      const auto& colors = theme();
      QPainter painter(menu);
      painter.fillRect(QRect(row.left() + kFlyoutIconLeft - kFlyoutMarkerGap - kFlyoutMarkerSize,
                             row.top() + (row.height() - kFlyoutMarkerSize) / 2, kFlyoutMarkerSize, kFlyoutMarkerSize),
                       menu->activeAction() == current ? colors.text_on_accent : colors.accent);
    }
    return true;
  }

private:
  QPointer<QToolButton> button_;
  bool painting_{false};
};

// Opens a flyout beside its button (GitHub issue 284). QToolButton puts a vertical bar's
// menu at the button's last pixel column, over its edge, one pixel low. Every way in (the
// hold timer, a double-click, a right-click) shows the menu, so the move happens on Show,
// before the window maps. Qt's placement stays when the menu would not fit on the right.
class ToolFlyoutMenuPlacement final : public QObject {
public:
  ToolFlyoutMenuPlacement(QToolButton* button, QMenu* menu) : QObject(menu), button_(button) {
    menu->installEventFilter(this);
  }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::Show && button_ != nullptr && button_->isVisible()) {
      auto* menu = static_cast<QWidget*>(watched);
      constexpr int kGap = 2;
      QPoint target = button_->mapToGlobal(QPoint(button_->width() + kGap, 0));
      const auto* screen = button_->screen();
      const QRect available = screen != nullptr ? screen->availableGeometry() : QRect();
      if (!menu->isRightToLeft() &&
          (!available.isValid() || target.x() + menu->width() - 1 <= available.right())) {
        if (available.isValid()) {
          target.setY(std::clamp(target.y(), available.top(),
                                 std::max(available.top(), available.bottom() - menu->height() + 1)));
        }
        menu->move(target);
      }
    }
    return QObject::eventFilter(watched, event);
  }

private:
  QPointer<QToolButton> button_;
};

// Stock QToolBar collapses an expanded overflow bar half a second after the
// pointer leaves it, which makes the palette's second column nearly
// unreachable. Swallowing Leave while the extension button is checked turns
// the expansion into a real toggle; collapse happens through the extension
// button, or automatically once every item fits again (see
// ToolPaletteExpansionReservation).
class ToolPaletteExpansionLock final : public QObject {
public:
  ToolPaletteExpansionLock(QToolButton* extension, QObject* parent)
      : QObject(parent), extension_(extension) {}

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::Leave && extension_ != nullptr && extension_->isChecked()) {
      return true;
    }
    return QObject::eventFilter(watched, event);
  }

private:
  QPointer<QToolButton> extension_;
};

// The collapsed column width at the current toolbar icon size (43 px at Normal), kept on the
// bar so the expansion controller follows a live size change.
constexpr const char* kToolPaletteCollapsedWidthProperty = "toolPaletteCollapsedWidth";

int tool_palette_collapsed_min_width(const QWidget* palette) {
  const auto stored = palette != nullptr ? palette->property(kToolPaletteCollapsedWidthProperty) : QVariant();
  return stored.isValid() ? stored.toInt() : ToolPaletteFooter::kWidth;
}

// The flyout menu's sheet: tool icons at the palette's own size, centered between the menu
// edge and the names (kFlyoutIconLeft). The padding is set here so the centering does not
// depend on the app sheet's QMenu::item.
QString tool_flyout_menu_style(int icon_extent) {
  return QStringLiteral("QMenu { icon-size: %1px; } QMenu::item { padding-left: %2px; } "
                        "QMenu::icon { left: %3px; }")
      .arg(icon_extent)
      .arg(kFlyoutItemLeftPadding)
      .arg(kFlyoutIconLeft);
}

// The overflow arrow points where the expansion goes (GitHub issue 122): right
// while collapsed (the extra columns open to the right), left while expanded.
// QToolBarExtension takes its icon from the style only on an orientation or
// style change, so the checked state owns it in between.
void apply_extension_chevron(QToolButton* extension) {
  if (extension == nullptr) {
    return;
  }
  extension->setIcon(themed_svg_icon(extension->isChecked() ? QStringLiteral("chevron-left")
                                                            : QStringLiteral("chevron-right")));
}

// Qt expands the overflow columns by giving the bar a geometry wider than its
// toolbar-area slot, overlaying the canvas. While the extension button stays
// checked, this controller copies the measured expanded width into the bar's
// minimum width so the main-window layout reserves the columns and the canvas
// shrinks instead of sitting underneath them. Each pass releases the minimum
// first (a standing minimum would stop Qt from dropping back to fewer columns
// when the window grows), measures the width Qt actually applied, and re-pins
// it; identical measurements converge to a no-op. Once the whole palette fits
// its slot again the expansion has nothing left to reveal, so the controller
// clicks the extension closed instead of holding a stale empty column.
class ToolPaletteExpansionReservation final : public QObject {
public:
  ToolPaletteExpansionReservation(QToolBar* palette, QToolButton* extension)
      : QObject(palette), palette_(palette), extension_(extension) {
    palette->installEventFilter(this);
    extension->installEventFilter(this);
    connect(extension, &QToolButton::toggled, this, [this](bool checked) {
      apply_extension_chevron(extension_);
      suspend_window_animation();
      if (checked) {
        schedule_reservation();
        return;
      }
      if (palette_ == nullptr) {
        return;
      }
      palette_->setMinimumWidth(tool_palette_collapsed_min_width(palette_));
      palette_->setMaximumWidth(QWIDGETSIZE_MAX);
      // One turn later (after Qt's own collapse handling), force a toolbar
      // layout pass: when the collapsed geometry matches the expanded one (a
      // tall window whose expansion had nothing left to reveal), Qt skips
      // setGeometry and would leave the extension button visible.
      QTimer::singleShot(0, this, [this] {
        if (palette_ == nullptr || extension_ == nullptr || extension_->isChecked()) {
          return;
        }
        if (auto* bar_layout = palette_->layout(); bar_layout != nullptr) {
          bar_layout->invalidate();
          bar_layout->activate();
        }
      });
    });
  }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (watched == extension_) {
      // QToolBarExtension::changeEvent re-reads the style's icon after this
      // filter; restore the direction the checked state asks for.
      if (event->type() == QEvent::StyleChange) {
        QTimer::singleShot(0, this, [this] { apply_extension_chevron(extension_); });
      }
      return QObject::eventFilter(watched, event);
    }
    if (event->type() == QEvent::Resize && !applying_ && extension_ != nullptr && extension_->isChecked()) {
      schedule_reservation();
    }
    return QObject::eventFilter(watched, event);
  }

private:
  // The expansion must not go through the main-window widget animator. While a
  // toolbar animates, QToolBarLayout skips laying out its items, and Qt 6.11
  // reports the animation finished only when the animation object is destroyed,
  // a turn after the collapsed geometry was applied: the buttons stayed where
  // the wide bar had centered them (GitHub issue 88). toggled() is emitted
  // before the clicked() that Qt's own expand/collapse slot listens to, so
  // turning animation off here makes that slot apply its geometry directly.
  void suspend_window_animation() {
    auto* window = palette_ != nullptr ? qobject_cast<QMainWindow*>(palette_->parentWidget()) : nullptr;
    if (window == nullptr || !window->isAnimated()) {
      return;
    }
    window->setAnimated(false);
    QTimer::singleShot(0, window, [window] { window->setAnimated(true); });
  }

  void schedule_reservation() {
    if (scheduled_) {
      return;
    }
    scheduled_ = true;
    // Deferred one event-loop turn so a single pass covers a burst of layout
    // activity (the toggle itself, animation frames, a window resize).
    QTimer::singleShot(0, this, [this] {
      scheduled_ = false;
      apply_reservation();
    });
  }

  void apply_reservation() {
    if (palette_ == nullptr || extension_ == nullptr || !extension_->isChecked()) {
      return;
    }
    applying_ = true;
    const int collapsed_width = tool_palette_collapsed_min_width(palette_);
    palette_->setMinimumWidth(collapsed_width);
    palette_->setMaximumWidth(QWIDGETSIZE_MAX);
    if (auto* window_layout = palette_->window()->layout(); window_layout != nullptr) {
      // setMinimumWidth short-circuits when the value is unchanged, so force
      // the relayout that snaps any in-flight expansion animation and lets Qt
      // settle on the geometry it really wants before it is measured.
      window_layout->invalidate();
      window_layout->activate();
    }
    const bool fits_collapsed = palette_->sizeHint().height() <= palette_->height();
    // Qt sizes the expansion from a square-root guess at the column count
    // (QToolBarLayout::expandedSize), not from the columns the bar's height
    // actually needs, and that guess reserved two empty columns beside the two
    // in use (GitHub issue 124). The bar is pinned to the columns the layout
    // filled: the right edge of the rightmost visible tool plus the margin.
    const int expanded_width = std::min(palette_->width(), used_columns_width());
    if (!fits_collapsed && expanded_width > collapsed_width) {
      palette_->setMinimumWidth(expanded_width);
      palette_->setMaximumWidth(expanded_width);
    }
    applying_ = false;
    if (fits_collapsed) {
      // The window grew tall enough that nothing overflows. Qt never unchecks
      // the extension on its own, so close the expansion formally.
      QTimer::singleShot(0, extension_, [extension = extension_] {
        if (extension != nullptr && extension->isChecked()) {
          extension->click();
        }
      });
    }
  }

  // The width the columns Qt filled actually need: the rightmost visible
  // tool button's right edge plus the layout's right margin. The footer and
  // the extension button are not tools.
  int used_columns_width() const {
    if (palette_ == nullptr) {
      return 0;
    }
    const auto* footer = palette_->findChild<QWidget*>(QStringLiteral("toolPaletteFooter"));
    int right = 0;
    for (const auto* button : palette_->findChildren<QToolButton*>()) {
      if (button == extension_ || button->isHidden() || (footer != nullptr && footer->isAncestorOf(button))) {
        continue;
      }
      right = std::max(right, button->geometry().right() + 1);
    }
    if (right <= 0) {
      return palette_->width();
    }
    const auto* layout = palette_->layout();
    return right + (layout != nullptr ? layout->contentsMargins().right() : 0);
  }

  QPointer<QToolBar> palette_;
  QPointer<QToolButton> extension_;
  bool scheduled_{false};
  bool applying_{false};
};

// Keeps the footer in the toolbar layout's bottom contents margin: the layout
// lays the tools and places the extension button above that margin, so the
// footer never overflows and stays at the bottom of the collapsed column while
// an expansion widens the bar. QToolBarLayout rewrites its margins from the
// style on every polish, so the reservation is re-applied whenever it is lost.
class ToolPaletteFooterPlacer final : public QObject {
public:
  ToolPaletteFooterPlacer(QToolBar* palette, ToolPaletteFooter* footer, QToolButton* extension)
      : QObject(palette), palette_(palette), footer_(footer), extension_(extension) {
    palette->installEventFilter(this);
    if (extension != nullptr) {
      extension->installEventFilter(this);
    }
    apply();
  }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (watched == extension_) {
      // QToolBarLayout seats a vertical bar's extension button against the
      // bar's bottom edge less the RIGHT margin (a Qt quirk), so the reserved
      // bottom margin does not move it: lift it above the footer here.
      if ((event->type() == QEvent::Move || event->type() == QEvent::Resize || event->type() == QEvent::Show) &&
          !seating_extension_) {
        seat_extension();
      }
      return false;
    }
    switch (event->type()) {
      case QEvent::Resize:
      case QEvent::Show:
      case QEvent::LayoutRequest:
        apply();
        break;
      case QEvent::StyleChange:
      case QEvent::Polish:
        // QToolBar::changeEvent runs after this filter and resets the margins.
        QTimer::singleShot(0, this, [this] { apply(); });
        break;
      default:
        break;
    }
    return false;
  }

private:
  void apply() {
    if (palette_ == nullptr || footer_ == nullptr || palette_->layout() == nullptr) {
      return;
    }
    auto* layout = palette_->layout();
    const auto margins = layout->contentsMargins();
    // The top margin is the style's own; the bottom one carries the footer.
    const int base = margins.top();
    const int footer_height = footer_->footer_height();
    const int wanted = base + footer_height;
    if (margins.bottom() != wanted) {
      layout->setContentsMargins(margins.left(), base, margins.right(), wanted);
    }
    footer_->setGeometry(0, std::max(0, palette_->height() - base - footer_height),
                         std::max(footer_->cell_width(), palette_->width()), footer_height);
    footer_->raise();
    seat_extension();
  }

  // Pins the extension button directly above the footer, in the band
  // QToolBarLayout::layoutActions keeps free for it (the visible tools stop
  // one extension extent above the content bottom). Qt itself seats the button
  // at the bar's bottom edge, inside the footer, so every placement lifts it;
  // the pin must also move it back DOWN: a window that grows after a shorter
  // layout delivers the palette's Resize to this filter after the layout has
  // already moved the button, so the lift that follows reads the footer's old,
  // higher position, and a lift-only seat left the button over the last tool
  // the taller column had just revealed (GitHub issue 126).
  void seat_extension() {
    if (extension_ == nullptr || footer_ == nullptr || palette_ == nullptr || extension_->isHidden()) {
      return;
    }
    const int limit = footer_->geometry().top() - 1;
    const auto geometry = extension_->geometry();
    const int wanted_top = std::max(0, limit - geometry.height() + 1);
    if (geometry.top() == wanted_top) {
      return;
    }
    seating_extension_ = true;
    extension_->move(geometry.x(), wanted_top);
    seating_extension_ = false;
  }

  QPointer<QToolBar> palette_;
  QPointer<ToolPaletteFooter> footer_;
  QPointer<QToolButton> extension_;
  bool seating_extension_{false};
};

// Tool icons are hand-authored SVGs in src/ui/icons/tool-*.svg (32x32 viewBox,
// icon_ink strokes with one optional icon_accent accent). Author them in the dark
// values from theme_palette.hpp; icon_theme.cpp recolors them per scheme at paint
// time. Review them with the ui_tool_palette_icons_render_sheet visual test.
QIcon tool_icon(CanvasTool tool) {
  static const int icon_resources = ::qInitResources_icons();
  (void)icon_resources;
  const char* name = "tool-move";
  switch (tool) {
    case CanvasTool::Move:
      name = "tool-move";
      break;
    case CanvasTool::Marquee:
      name = "tool-marquee";
      break;
    case CanvasTool::EllipticalMarquee:
      name = "tool-marquee-ellipse";
      break;
    case CanvasTool::Lasso:
      name = "tool-lasso";
      break;
    case CanvasTool::PolygonLasso:
      name = "tool-polygon-lasso";
      break;
    case CanvasTool::MagneticLasso:
      name = "tool-magnetic-lasso";
      break;
    case CanvasTool::MagicWand:
      name = "tool-wand";
      break;
    case CanvasTool::QuickSelect:
      name = "tool-quick-select";
      break;
    case CanvasTool::ObjectSelect:
      name = "tool-object-select";
      break;
    case CanvasTool::Brush:
      name = "tool-brush";
      break;
    case CanvasTool::MixerBrush:
      name = "tool-mixer-brush";
      break;
    case CanvasTool::Clone:
      name = "tool-clone";
      break;
    case CanvasTool::PatternStamp:
      name = "tool-pattern-stamp";
      break;
    case CanvasTool::Healing:
      name = "tool-healing";
      break;
    case CanvasTool::Smudge:
      name = "tool-smudge";
      break;
    case CanvasTool::Dodge:
      name = "tool-dodge";
      break;
    case CanvasTool::Burn:
      name = "tool-burn";
      break;
    case CanvasTool::Sponge:
      name = "tool-sponge";
      break;
    case CanvasTool::BlurBrush:
      name = "tool-blur";
      break;
    case CanvasTool::SharpenBrush:
      name = "tool-sharpen";
      break;
    case CanvasTool::Eraser:
      name = "tool-eraser";
      break;
    case CanvasTool::Gradient:
      name = "tool-gradient";
      break;
    case CanvasTool::Fill:
      name = "tool-fill";
      break;
    case CanvasTool::Line:
      name = "tool-line";
      break;
    case CanvasTool::Rectangle:
      name = "tool-rect";
      break;
    case CanvasTool::Ellipse:
      name = "tool-ellipse";
      break;
    case CanvasTool::Eyedropper:
      name = "tool-eyedropper";
      break;
    case CanvasTool::Text:
      name = "tool-text";
      break;
    case CanvasTool::Pan:
      name = "tool-pan";
      break;
    case CanvasTool::Zoom:
      name = "tool-zoom";
      break;
    case CanvasTool::Pen:
      name = "tool-pen";
      break;
    case CanvasTool::PathSelect:
      name = "tool-path-select";
      break;
    case CanvasTool::DirectSelect:
      name = "tool-direct-select";
      break;
    case CanvasTool::Polygon:
      name = "tool-polygon";
      break;
    case CanvasTool::CustomShape:
      name = "tool-custom-shape";
      break;
    case CanvasTool::SpotHealing:
      name = "tool-spot-healing";
      break;
    case CanvasTool::PatchTool:
      name = "tool-patch";
      break;
    case CanvasTool::RedEye:
      name = "tool-red-eye";
      break;
    case CanvasTool::Crop:
      name = "tool-crop";
      break;
    case CanvasTool::PerspectiveCrop:
      name = "tool-perspective-crop";
      break;
    case CanvasTool::AddAnchor:
      name = "tool-add-anchor";
      break;
    case CanvasTool::DeleteAnchor:
      name = "tool-delete-anchor";
      break;
    case CanvasTool::ConvertPoint:
      name = "tool-convert-point";
      break;
    case CanvasTool::Artboard:
      name = "tool-artboard";
      break;
  }
  return themed_svg_icon(QLatin1String(name));
}

}  // namespace

void MainWindow::build_tool_palette(ActionBuildContext& ctx) {
  auto* tool_palette = new QToolBar(tr("Tool Palette"), this);
  tool_palette->setObjectName(QStringLiteral("toolPalette"));
  tool_palette->setOrientation(Qt::Vertical);
  tool_palette->setMovable(false);
  tool_palette->setFloatable(false);
  tool_palette->setAllowedAreas(Qt::LeftToolBarArea);
  tool_palette->setToolButtonStyle(Qt::ToolButtonIconOnly);
  // 20 px icons at the Normal toolbar icon size (Preferences; tool_palette_factor_).
  const int icon_extent = tool_palette_length(20, tool_palette_factor_);
  tool_palette->setIconSize(QSize(icon_extent, icon_extent));
  // Minimum, not fixed: the overflow extension button reveals hidden items by
  // widening the bar into extra columns, and a max width clamps that geometry
  // into a clipped sliver. While expanded, ToolPaletteExpansionReservation
  // raises this minimum to the measured expanded width so the layout reserves
  // the columns. QSS caps the children narrower, so the collapsed width stays
  // exactly 43 at Normal.
  tool_palette->setProperty(kToolPaletteCollapsedWidthProperty, tool_palette_collapsed_width(tool_palette_factor_));
  tool_palette->setMinimumWidth(tool_palette_collapsed_min_width(tool_palette));
  addToolBar(Qt::LeftToolBarArea, tool_palette);

  auto* tool_group = new QActionGroup(this);
  tool_group->setExclusive(true);
  tool_action_group_ = tool_group;
  // The palette is ordered in clusters split by separators: select, paint,
  // retouch, draw/type, view. Tools sharing a slot get a flyout button.
  const auto create_flyout_tool_action =
      [this, tool_group](QMenu* menu, const QString& label, CanvasTool tool, QKeySequence shortcut) {
        auto* action = new QAction(label, this);
        bind_action_text(action, tool_action_source(tool));
        action->setIcon(tool_icon(tool));
        action->setCheckable(true);
        action->setData(static_cast<int>(tool));
        action->setObjectName(tool_action_object_name(tool));
        if (const auto* detail = tool_tooltip_detail_source(tool); detail != nullptr) {
          action->setProperty(kActionTooltipDetailProperty, QString::fromLatin1(detail));
        }
        register_hotkey(action, tool_hotkey_id(tool), shortcut, QStringLiteral("tools"));
        tool_group->addAction(action);
        menu->addAction(action);
        addAction(action);
        register_tool_action(action);
        return action;
      };
  // Every flyout also owns a "Cycle <Group> Tools" command on Shift+<the group's
  // letter>, Photoshop's Shift+key convention (GitHub issue 45): it selects the
  // member after the active tool when the active tool is in the group, otherwise
  // the member after the one the button shows, wrapping. Qt fires neither of two
  // actions that share a sequence, so the members carry no Shift default of their
  // own; a user override that puts Shift+<letter> back on a member wins through
  // the registry's resolution and suppresses the cycle default.
  const auto configure_tool_flyout = [this](QToolBar* palette, QMenu* menu, QToolButton* button,
                                            QAction* default_action, const QList<QAction*>& actions,
                                            const char* cycle_source, const char* cycle_object_name,
                                            const char* cycle_id, QKeySequence cycle_shortcut) {
    button->setProperty("toolFlyout", true);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setPopupMode(QToolButton::DelayedPopup);
    button->setMenu(menu);
    button->setDefaultAction(default_action);
    button->setToolTip(default_action->toolTip());
    palette->addWidget(button);
    // Press-and-hold opens the flyout after SH_ToolButton_PopupDelay (the
    // InteractionHintsStyle in main.cpp). A left double-click is the second
    // way in: Qt would otherwise route the pair's second press back to
    // mousePressEvent and restart the hold timer, so swallow it and open the
    // menu through the same showMenu() path the timer uses. The first click
    // of the pair still selects the default tool, as in Photoshop. The filter sees
    // events before Qt's disabled check, so a disabled button (a preview dialog's edit
    // lock) stays shut here as it does for the left press.
    const auto open_flyout = [button] {
      if (button->isEnabled()) {
        button->showMenu();
      }
    };
    button->installEventFilter(new ToolButtonPressFilter(open_flyout, open_flyout, button));
    // Every way in (the hold timer, a double-click, a right-click) also selects the tool
    // the button shows as the flyout opens, so dismissing it leaves that tool active
    // (GitHub issue 368); a pick in the flyout then selects that member as usual. The
    // already-active tool is left alone, so the first click of a double-click is the only
    // selection that pair makes.
    QObject::connect(menu, &QMenu::aboutToShow, button, [button] {
      auto* shown = button->defaultAction();
      if (button->isEnabled() && shown != nullptr && shown->isEnabled() && !shown->isChecked()) {
        shown->trigger();
      }
    });
    new ToolFlyoutMenuPlacement(button, menu);
    new ToolFlyoutCurrentMarker(button, menu);
    menu->setStyleSheet(tool_flyout_menu_style(palette->iconSize().width()));
    for (auto* action : actions) {
      QObject::connect(action, &QAction::triggered, button, [button, menu, action] {
        button->setDefaultAction(action);
        button->setMenu(menu);
        button->setToolTip(action->toolTip());
      });
    }
    const QList<QAction*> members = actions;
    auto* cycle = new QAction(tr(cycle_source), this);
    bind_action_text(cycle, cycle_source);
    cycle->setObjectName(QString::fromLatin1(cycle_object_name));
    cycle->setMenuRole(QAction::NoRole);
    register_hotkey(cycle, QString::fromLatin1(cycle_id), cycle_shortcut, QStringLiteral("tools"));
    addAction(cycle);
    register_tool_action(cycle);
    // Listed after the members so the flyout shows the key that walks it.
    menu->addSeparator();
    menu->addAction(cycle);
    QObject::connect(cycle, &QAction::triggered, button, [button, members] {
      int current = -1;
      for (int i = 0; i < members.size(); ++i) {
        if (members[i]->isChecked()) {
          current = i;
        }
      }
      if (current < 0) {
        current = static_cast<int>(members.indexOf(button->defaultAction()));
      }
      members[(current + 1) % members.size()]->trigger();
    });
  };

  // Move and Artboard share a slot, as in Photoshop (docs/artboards.md).
  auto* move_menu = new QMenu(tr("Move Tools"), tool_palette);
  move_menu->setObjectName(QStringLiteral("moveToolMenu"));
  bind_widget_text(move_menu, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Move Tools"));
  move_tool_action_ = create_flyout_tool_action(move_menu, tr("Move"), CanvasTool::Move, QKeySequence(Qt::Key_V));
  auto* artboard_action = create_flyout_tool_action(move_menu, tr("Artboard"), CanvasTool::Artboard, QKeySequence());
  auto* move_tool_button = new QToolButton(tool_palette);
  move_tool_button->setObjectName(QStringLiteral("moveToolButton"));
  configure_tool_flyout(tool_palette, move_menu, move_tool_button, move_tool_action_,
                        {move_tool_action_, artboard_action},
                        QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Cycle Move Tools"),
                        "toolCycleMoveAction", "tools.cycle.move", QKeySequence(Qt::SHIFT | Qt::Key_V));
  auto* marquee_menu = new QMenu(tr("Marquee Tools"), tool_palette);
  marquee_menu->setObjectName(QStringLiteral("marqueeToolMenu"));
  bind_widget_text(marquee_menu, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Marquee Tools"));
  auto* rect_marquee_action =
      create_flyout_tool_action(marquee_menu, tr("Marquee"), CanvasTool::Marquee, QKeySequence(Qt::Key_M));
  auto* elliptical_marquee_action = create_flyout_tool_action(
      marquee_menu, tr("Elliptical Marquee"), CanvasTool::EllipticalMarquee, QKeySequence());
  auto* marquee_tool_button = new QToolButton(tool_palette);
  marquee_tool_button->setObjectName(QStringLiteral("marqueeToolButton"));
  configure_tool_flyout(tool_palette, marquee_menu, marquee_tool_button, rect_marquee_action,
                        {rect_marquee_action, elliptical_marquee_action},
                        QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Cycle Marquee Tools"),
                        "toolCycleMarqueeAction", "tools.cycle.marquee", QKeySequence(Qt::SHIFT | Qt::Key_M));
  auto* lasso_menu = new QMenu(tr("Lasso Tools"), tool_palette);
  lasso_menu->setObjectName(QStringLiteral("lassoToolMenu"));
  bind_widget_text(lasso_menu, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Lasso Tools"));
  auto* lasso_action = create_flyout_tool_action(lasso_menu, tr("Lasso"), CanvasTool::Lasso, QKeySequence(Qt::Key_L));
  auto* polygon_lasso_action =
      create_flyout_tool_action(lasso_menu, tr("Polygonal Lasso"), CanvasTool::PolygonLasso, QKeySequence());
  auto* magnetic_lasso_action = create_flyout_tool_action(lasso_menu, tr("Magnetic Lasso"), CanvasTool::MagneticLasso,
                                                          QKeySequence());
  auto* lasso_tool_button = new QToolButton(tool_palette);
  lasso_tool_button->setObjectName(QStringLiteral("lassoToolButton"));
  configure_tool_flyout(tool_palette, lasso_menu, lasso_tool_button, lasso_action,
                        {lasso_action, polygon_lasso_action, magnetic_lasso_action},
                        QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Cycle Lasso Tools"),
                        "toolCycleLassoAction", "tools.cycle.lasso", QKeySequence(Qt::SHIFT | Qt::Key_L));
  auto* wand_menu = new QMenu(tr("Wand Tools"), tool_palette);
  wand_menu->setObjectName(QStringLiteral("wandToolMenu"));
  bind_widget_text(wand_menu, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Wand Tools"));
  auto* magic_wand_action =
      create_flyout_tool_action(wand_menu, tr("Magic Wand"), CanvasTool::MagicWand, QKeySequence(Qt::Key_W));
  auto* quick_select_action =
      create_flyout_tool_action(wand_menu, tr("Quick Select"), CanvasTool::QuickSelect, QKeySequence());
  // Object Selection needs the bundled models (docs/ai-selection.md); builds without them
  // leave it out of the flyout and the cycle.
  configure_automatic_selection_models();
  QList<QAction*> wand_actions{magic_wand_action, quick_select_action};
  if (patchy::ai::segmentation_available()) {
    wand_actions.push_back(
        create_flyout_tool_action(wand_menu, tr("Object Selection"), CanvasTool::ObjectSelect, QKeySequence()));
  }
  auto* wand_tool_button = new QToolButton(tool_palette);
  wand_tool_button->setObjectName(QStringLiteral("wandToolButton"));
  configure_tool_flyout(tool_palette, wand_menu, wand_tool_button, magic_wand_action, wand_actions,
                        QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Cycle Wand Tools"),
                        "toolCycleWandAction", "tools.cycle.wand", QKeySequence(Qt::SHIFT | Qt::Key_W));
  auto* crop_menu = new QMenu(tr("Crop Tools"), tool_palette);
  crop_menu->setObjectName(QStringLiteral("cropToolMenu"));
  bind_widget_text(crop_menu, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Crop Tools"));
  auto* crop_action = create_flyout_tool_action(crop_menu, tr("Crop"), CanvasTool::Crop, QKeySequence(Qt::Key_C));
  auto* perspective_crop_action =
      create_flyout_tool_action(crop_menu, tr("Perspective Crop"), CanvasTool::PerspectiveCrop, QKeySequence());
  auto* crop_tool_button = new QToolButton(tool_palette);
  crop_tool_button->setObjectName(QStringLiteral("cropToolButton"));
  configure_tool_flyout(tool_palette, crop_menu, crop_tool_button, crop_action, {crop_action, perspective_crop_action},
                        QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Cycle Crop Tools"),
                        "toolCycleCropAction", "tools.cycle.crop", QKeySequence(Qt::SHIFT | Qt::Key_C));
  tool_palette->addSeparator();

  add_tool_action(tool_palette, tool_group, tr("Brush"), CanvasTool::Brush, QKeySequence(Qt::Key_B))->setChecked(true);
  add_tool_action(tool_palette, tool_group, tr("Eraser"), CanvasTool::Eraser, QKeySequence(Qt::Key_E));
  auto* gradient_menu = new QMenu(tr("Fill Tools"), tool_palette);
  gradient_menu->setObjectName(QStringLiteral("gradientToolMenu"));
  bind_widget_text(gradient_menu, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Fill Tools"));
  auto* gradient_action =
      create_flyout_tool_action(gradient_menu, tr("Gradient"), CanvasTool::Gradient, QKeySequence(Qt::Key_G));
  auto* fill_action =
      create_flyout_tool_action(gradient_menu, tr("Fill"), CanvasTool::Fill, QKeySequence());
  auto* gradient_tool_button = new QToolButton(tool_palette);
  gradient_tool_button->setObjectName(QStringLiteral("gradientToolButton"));
  configure_tool_flyout(tool_palette, gradient_menu, gradient_tool_button, gradient_action,
                        {gradient_action, fill_action},
                        QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Cycle Fill Tools"),
                        "toolCycleFillAction", "tools.cycle.gradient", QKeySequence(Qt::SHIFT | Qt::Key_G));
  tool_palette->addSeparator();

  auto* stamp_menu = new QMenu(tr("Stamp Tools"), tool_palette);
  stamp_menu->setObjectName(QStringLiteral("stampToolMenu"));
  bind_widget_text(stamp_menu, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Stamp Tools"));
  auto* clone_action = create_flyout_tool_action(stamp_menu, tr("Clone"), CanvasTool::Clone, QKeySequence(Qt::Key_S));
  auto* pattern_stamp_action = create_flyout_tool_action(stamp_menu, tr("Pattern Stamp"), CanvasTool::PatternStamp,
                                                         QKeySequence());
  auto* stamp_tool_button = new QToolButton(tool_palette);
  stamp_tool_button->setObjectName(QStringLiteral("stampToolButton"));
  configure_tool_flyout(tool_palette, stamp_menu, stamp_tool_button, clone_action,
                        {clone_action, pattern_stamp_action},
                        QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Cycle Stamp Tools"),
                        "toolCycleStampAction", "tools.cycle.stamp", QKeySequence(Qt::SHIFT | Qt::Key_S));
  auto* healing_menu = new QMenu(tr("Healing Tools"), tool_palette);
  healing_menu->setObjectName(QStringLiteral("healingToolMenu"));
  bind_widget_text(healing_menu, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Healing Tools"));
  auto* healing_action = create_flyout_tool_action(healing_menu, tr("Healing Brush"), CanvasTool::Healing,
                                                   QKeySequence(Qt::Key_J));
  auto* spot_healing_action = create_flyout_tool_action(healing_menu, tr("Spot Healing"), CanvasTool::SpotHealing,
                                                        QKeySequence());
  auto* patch_action =
      create_flyout_tool_action(healing_menu, tr("Patch"), CanvasTool::PatchTool, QKeySequence());
  auto* red_eye_action =
      create_flyout_tool_action(healing_menu, tr("Red Eye"), CanvasTool::RedEye, QKeySequence());
  auto* healing_tool_button = new QToolButton(tool_palette);
  healing_tool_button->setObjectName(QStringLiteral("healingToolButton"));
  configure_tool_flyout(tool_palette, healing_menu, healing_tool_button, healing_action,
                        {healing_action, spot_healing_action, patch_action, red_eye_action},
                        QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Cycle Healing Tools"),
                        "toolCycleHealingAction", "tools.cycle.healing", QKeySequence(Qt::SHIFT | Qt::Key_J));

  auto* detail_menu = new QMenu(tr("Detail Tools"), tool_palette);
  detail_menu->setObjectName(QStringLiteral("detailToolMenu"));
  bind_widget_text(detail_menu, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Detail Tools"));
  auto* smudge_action =
      create_flyout_tool_action(detail_menu, tr("Smudge"), CanvasTool::Smudge, QKeySequence(Qt::Key_R));
  auto* mixer_brush_action = create_flyout_tool_action(
      detail_menu, tr("Mixer Brush"), CanvasTool::MixerBrush, QKeySequence());
  auto* blur_action = create_flyout_tool_action(detail_menu, tr("Blur"), CanvasTool::BlurBrush, QKeySequence());
  auto* sharpen_action =
      create_flyout_tool_action(detail_menu, tr("Sharpen"), CanvasTool::SharpenBrush, QKeySequence());
  auto* detail_button = new QToolButton(tool_palette);
  detail_button->setObjectName(QStringLiteral("detailToolButton"));
  configure_tool_flyout(tool_palette, detail_menu, detail_button, smudge_action,
                        {smudge_action, mixer_brush_action, blur_action, sharpen_action},
                        QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Cycle Detail Tools"),
                        "toolCycleDetailAction", "tools.cycle.detail", QKeySequence(Qt::SHIFT | Qt::Key_R));

  auto* tone_menu = new QMenu(tr("Toning Tools"), tool_palette);
  tone_menu->setObjectName(QStringLiteral("toneToolMenu"));
  bind_widget_text(tone_menu, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Toning Tools"));
  auto* dodge_action =
      create_flyout_tool_action(tone_menu, tr("Dodge"), CanvasTool::Dodge, QKeySequence(Qt::Key_O));
  auto* burn_action = create_flyout_tool_action(tone_menu, tr("Burn"), CanvasTool::Burn, QKeySequence());
  auto* sponge_action =
      create_flyout_tool_action(tone_menu, tr("Sponge"), CanvasTool::Sponge, QKeySequence());
  auto* tone_button = new QToolButton(tool_palette);
  tone_button->setObjectName(QStringLiteral("toneToolButton"));
  configure_tool_flyout(tool_palette, tone_menu, tone_button, dodge_action,
                        {dodge_action, burn_action, sponge_action},
                        QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Cycle Toning Tools"),
                        "toolCycleToningAction", "tools.cycle.tone", QKeySequence(Qt::SHIFT | Qt::Key_O));
  tool_palette->addSeparator();

  // The Pen Tools flyout: the Pen (default, and what P re-selects) plus the
  // dedicated Add/Delete/Convert anchor tools Photoshop users look for.
  auto* pen_menu = new QMenu(tr("Pen Tools"), tool_palette);
  pen_menu->setObjectName(QStringLiteral("penToolMenu"));
  bind_widget_text(pen_menu, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Pen Tools"));
  auto* pen_action =
      create_flyout_tool_action(pen_menu, tr("Pen"), CanvasTool::Pen, QKeySequence(Qt::Key_P));
  auto* add_anchor_action =
      create_flyout_tool_action(pen_menu, tr("Add Anchor"), CanvasTool::AddAnchor, QKeySequence());
  auto* delete_anchor_action = create_flyout_tool_action(pen_menu, tr("Delete Anchor"),
                                                         CanvasTool::DeleteAnchor, QKeySequence());
  auto* convert_point_action = create_flyout_tool_action(pen_menu, tr("Convert Point"),
                                                         CanvasTool::ConvertPoint, QKeySequence());
  auto* pen_button = new QToolButton(tool_palette);
  pen_button->setObjectName(QStringLiteral("penToolButton"));
  configure_tool_flyout(tool_palette, pen_menu, pen_button, pen_action,
                        {pen_action, add_anchor_action, delete_anchor_action, convert_point_action},
                        QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Cycle Pen Tools"),
                        "toolCyclePenAction", "tools.cycle.pen", QKeySequence(Qt::SHIFT | Qt::Key_P));
  // The Path Tools flyout sits directly after the Pen: drawing and adjusting
  // paths alternate constantly, so the two slots stay adjacent.
  auto* path_select_menu = new QMenu(tr("Path Tools"), tool_palette);
  path_select_menu->setObjectName(QStringLiteral("pathSelectToolMenu"));
  bind_widget_text(path_select_menu, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Path Tools"));
  auto* path_select_action = create_flyout_tool_action(path_select_menu, tr("Path Select"),
                                                       CanvasTool::PathSelect, QKeySequence(Qt::Key_A));
  auto* direct_select_action =
      create_flyout_tool_action(path_select_menu, tr("Direct Select"), CanvasTool::DirectSelect, QKeySequence());
  auto* path_select_button = new QToolButton(tool_palette);
  path_select_button->setObjectName(QStringLiteral("pathSelectToolButton"));
  configure_tool_flyout(tool_palette, path_select_menu, path_select_button, path_select_action,
                        {path_select_action, direct_select_action},
                        QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Cycle Path Tools"),
                        "toolCyclePathAction", "tools.cycle.path_select", QKeySequence(Qt::SHIFT | Qt::Key_A));
  auto* shape_menu = new QMenu(tr("Shape Tools"), tool_palette);
  shape_menu->setObjectName(QStringLiteral("shapeToolMenu"));
  bind_widget_text(shape_menu, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Shape Tools"));
  // The lettered default leads, as in every flyout; Line sits below Polygon (GitHub issue 308).
  auto* rect_tool_action = create_flyout_tool_action(shape_menu, tr("Rect"), CanvasTool::Rectangle, QKeySequence(Qt::Key_U));
  auto* ellipse_tool_action =
      create_flyout_tool_action(shape_menu, tr("Ellipse"), CanvasTool::Ellipse, QKeySequence());
  auto* polygon_tool_action =
      create_flyout_tool_action(shape_menu, tr("Polygon"), CanvasTool::Polygon, QKeySequence());
  auto* line_tool_action =
      create_flyout_tool_action(shape_menu, tr("Line"), CanvasTool::Line, QKeySequence());  // Ctrl+Shift+U belongs to Desaturate
  auto* custom_shape_tool_action = create_flyout_tool_action(
      shape_menu, tr("Custom Shape"), CanvasTool::CustomShape, QKeySequence());
  auto* shape_tool_button = new QToolButton(tool_palette);
  shape_tool_button->setObjectName(QStringLiteral("shapeToolButton"));
  configure_tool_flyout(tool_palette, shape_menu, shape_tool_button, rect_tool_action,
                        {rect_tool_action, ellipse_tool_action, polygon_tool_action,
                         line_tool_action, custom_shape_tool_action},
                        QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Cycle Shape Tools"),
                        "toolCycleShapeAction", "tools.cycle.shape", QKeySequence(Qt::SHIFT | Qt::Key_U));
  type_tool_action_ = add_tool_action(tool_palette, tool_group, tr("Type"), CanvasTool::Text, QKeySequence(Qt::Key_T));
  tool_palette->addSeparator();

  add_tool_action(tool_palette, tool_group, tr("Pick"), CanvasTool::Eyedropper, QKeySequence(Qt::Key_I));
  add_tool_action(tool_palette, tool_group, tr("Hand"), CanvasTool::Pan, QKeySequence(Qt::Key_H));
  auto* zoom_tool_action = add_tool_action(tool_palette, tool_group, tr("Zoom"), CanvasTool::Zoom, QKeySequence(Qt::Key_Z));
  if (auto* zoom_button = qobject_cast<QToolButton*>(tool_palette->widgetForAction(zoom_tool_action));
      zoom_button != nullptr) {
    zoom_button->setObjectName(QStringLiteral("zoomToolButton"));
    zoom_button->installEventFilter(new MouseDoubleClickFilter(
        [this] {
          if (canvas_ != nullptr) {
            canvas_->set_view_zoom_centered(1.0);
            refresh_document_info();
            statusBar()->showMessage(tr("Actual Pixels"));
          }
        },
        zoom_button));
  }
  connect(tool_group, &QActionGroup::triggered, this, [this](QAction* action) {
    const auto selected = static_cast<CanvasTool>(action->data().toInt());
    // With no document open the pick still sets the tool, its options bar and the stored
    // tool (local-008); the next canvas takes current_tool_ in activate_document_canvas.
    if (canvas_ != nullptr) {
      if (canvas_->free_transform_active()) {
        canvas_->finish_free_transform();
      }
      if (canvas_->warp_transform_active()) {
        canvas_->finish_warp_transform();
      }
      if (selected != CanvasTool::Text) {
        finish_active_text_editor();
      }
    }
    current_tool_ = selected;
    if (canvas_ != nullptr) {
      canvas_->set_tool(selected);
    }
    apply_selection_edge_settings_for_tool(selected);
    set_eraser_brush_settings_active(selected == CanvasTool::Eraser);
    // The next launch starts on this tool (GitHub issue 254; tools/activeTool).
    if (canvas_ != nullptr) {
      schedule_save_tool_settings();
      if (selected != CanvasTool::Text ||
          canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr) {
        canvas_->setFocus(Qt::OtherFocusReason);
      }
    } else {
      save_active_tool_setting();
    }
    refresh_options_bar();
    refresh_document_info();
    // Tools whose gestures the name does not explain get a one-line hint.
    if (const auto* hint = tool_activation_hint_source(selected); hint != nullptr) {
      auto message = tr(hint);
      if (const auto* tail = paint_tool_hint_tail_source(selected); tail != nullptr) {
        message += QLatin1Char(' ') + tr(tail);
      }
      statusBar()->showMessage(resolve_modifier_names(message));
    } else {
      statusBar()->showMessage(tool_name(selected));
    }
    if (selected == CanvasTool::ObjectSelect) {
      // After the switch has painted: a one-time offer of the High Quality model.
      QTimer::singleShot(0, this, [this] {
        if (current_tool_ == CanvasTool::ObjectSelect) {
          maybe_offer_high_quality_object_selection_model();
        }
      });
    }
  });
  ctx.type_menu->addAction(type_tool_action_);
  ctx.type_menu->addSeparator();
  // Photoshop's Type > Resolve Missing Fonts (GitHub issue 169).
  auto* resolve_fonts_action = ctx.type_menu->addAction(QString());
  bind_action_text(resolve_fonts_action, QT_TR_NOOP("Resolve Missing Fonts..."));
  resolve_fonts_action->setObjectName(QStringLiteral("typeResolveMissingFontsAction"));
  resolve_fonts_action->setMenuRole(QAction::NoRole);
  register_hotkey(resolve_fonts_action, "type.resolve_missing_fonts");
  connect(resolve_fonts_action, &QAction::triggered, this, [this] { show_resolve_missing_fonts_dialog(); });
  register_document_action(resolve_fonts_action);

  // The color well and Quick Mask live in a footer pinned to the bar's bottom,
  // outside the toolbar layout, so they never fold into the extension and stay
  // put while the tools above expand into extra columns (ToolPaletteFooterPlacer).
  auto* default_colors_action = new QAction(tr("Default Colors"), this);
  auto* swap_colors_action = new QAction(tr("Swap Colors"), this);
  default_colors_action->setObjectName(QStringLiteral("colorDefaultAction"));
  swap_colors_action->setObjectName(QStringLiteral("colorSwapAction"));
  default_colors_action->setIcon(simple_icon(QStringLiteral("D")));
  swap_colors_action->setIcon(simple_icon(QStringLiteral("X")));
  register_hotkey(default_colors_action, "color.default", QKeySequence(Qt::Key_D), QStringLiteral("color"));
  register_hotkey(swap_colors_action, "color.swap", QKeySequence(Qt::Key_X), QStringLiteral("color"));
  tool_palette_footer_ = new ToolPaletteFooter(default_colors_action, swap_colors_action, quick_mask_action_, tool_palette);
  tool_palette_footer_->set_scale(tool_palette_factor_);
  primary_color_button_ = tool_palette_footer_->foreground();
  secondary_color_button_ = tool_palette_footer_->background();
  primary_color_button_->setToolTip(tr("Foreground color"));
  secondary_color_button_->setToolTip(tr("Background color"));
  new ToolPaletteFooterPlacer(tool_palette, tool_palette_footer_,
                              tool_palette->findChild<QToolButton*>(QStringLiteral("qt_toolbar_ext_button")));
  connect(primary_color_button_, &QPushButton::clicked, this, [this] { choose_primary_color(); });
  connect(secondary_color_button_, &QPushButton::clicked, this, [this] { choose_secondary_color(); });
  connect(swap_colors_action, &QAction::triggered, this, [this] { swap_colors(); });
  connect(default_colors_action, &QAction::triggered, this, [this] { default_colors(); });
  register_document_action(default_colors_action);
  register_document_action(swap_colors_action);

  // The overflow expansion behaves as a sticky toggle: the Leave filter keeps
  // it open while the pointer wanders, and the reservation controller widens
  // the bar's minimum so the layout reserves the extra columns instead of
  // letting them overlay the canvas. Picking a tool leaves the expansion open
  // (an auto-collapse there ate the first click of the Zoom button's
  // double-click); it closes on an extension re-click or once the window grows
  // tall enough that nothing overflows.
  if (auto* extension_button = tool_palette->findChild<QToolButton*>(QStringLiteral("qt_toolbar_ext_button"));
      extension_button != nullptr) {
    tool_palette->installEventFilter(new ToolPaletteExpansionLock(extension_button, tool_palette));
    new ToolPaletteExpansionReservation(tool_palette, extension_button);
  }

  // Export the cross-phase locals bind_action_translations() still needs.
  ctx.tool_palette = tool_palette;
  ctx.default_colors_action = default_colors_action;
  ctx.swap_colors_action = swap_colors_action;
}

QAction* MainWindow::add_tool_action(QToolBar* palette, QActionGroup* group, QString label, CanvasTool tool,
                                     QKeySequence shortcut) {
  auto* action = palette->addAction(label);
  bind_action_text(action, tool_action_source(tool));
  action->setIcon(tool_icon(tool));
  action->setCheckable(true);
  action->setData(static_cast<int>(tool));
  action->setObjectName(tool_action_object_name(tool));
  if (const auto* detail = tool_tooltip_detail_source(tool); detail != nullptr) {
    action->setProperty(kActionTooltipDetailProperty, QString::fromLatin1(detail));
  }
  register_hotkey(action, tool_hotkey_id(tool), shortcut, QStringLiteral("tools"));
  group->addAction(action);
  register_tool_action(action);
  // A right-click selects the tool like a left click and opens no toolbar menu, as a
  // right-click on a flyout button does (GitHub issue 388). The filter sees events
  // before Qt's disabled check, so a disabled button stays inert.
  if (auto* button = qobject_cast<QToolButton*>(palette->widgetForAction(action)); button != nullptr) {
    button->installEventFilter(new ToolButtonPressFilter(
        [button, action] {
          if (button->isEnabled() && action->isEnabled()) {
            action->trigger();
          }
        },
        {}, button));
  }
  return action;
}

// Preferences > Toolbar icon size, applied live: the palette's icon size, its collapsed
// column, the footer, the flyout menus' icons, and the window sheet's palette lengths.
void MainWindow::apply_tool_palette_factor(double factor) {
  if (factor == tool_palette_factor_) {
    return;
  }
  tool_palette_factor_ = factor;
  auto* palette = findChild<QToolBar*>(QStringLiteral("toolPalette"));
  if (palette == nullptr) {
    return;
  }
  // An open overflow expansion was measured for the old buttons: close it first.
  if (auto* extension = palette->findChild<QToolButton*>(QStringLiteral("qt_toolbar_ext_button"));
      extension != nullptr && extension->isChecked()) {
    extension->click();
  }
  const int icon_extent = tool_palette_length(20, factor);
  palette->setIconSize(QSize(icon_extent, icon_extent));
  palette->setProperty(kToolPaletteCollapsedWidthProperty, tool_palette_collapsed_width(factor));
  palette->setMinimumWidth(tool_palette_collapsed_min_width(palette));
  if (tool_palette_footer_ != nullptr) {
    tool_palette_footer_->set_scale(factor);
  }
  for (auto* button : palette->findChildren<QToolButton*>()) {
    if (button->property("toolFlyout").toBool() && button->menu() != nullptr) {
      button->menu()->setStyleSheet(tool_flyout_menu_style(icon_extent));
    }
  }
  setStyleSheet(photoshop_style(factor));
}

}  // namespace patchy::ui
