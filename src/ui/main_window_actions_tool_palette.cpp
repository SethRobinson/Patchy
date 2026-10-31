// MainWindow::build_tool_palette(): the tool-palette phase of create_actions()
// (the left QToolBar, tool actions and flyouts, color buttons and the quick-
// mask button), split out of main_window_actions.cpp along with
// add_tool_action (this TU is its only caller) and the anonymous-namespace
// tool helpers only they use.
// Pure function moves; behavior must stay identical, and the construction
// order is load-bearing (see create_actions() for the phase order).

#include "ui/hotkey_registry.hpp"
#include "ui/theme_script_host.hpp"
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
#include "ui/tool_palette_customization.hpp"
#include "ui/tool_palette_footer.hpp"
#include "ui/tool_palette_layout.hpp"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QCursor>
#include <QDialog>
#include <QEvent>
#include <QHBoxLayout>
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
#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>
#include <QPoint>
#include <QPointer>
#include <QPushButton>
#include <QRect>
#include <QScreen>
#include <QSize>
#include <QString>
#include <QStringLiteral>
#include <QStyleOptionMenuItem>
#include <QTextEdit>
#include <QThread>
#include <QTimer>
#include <QVariant>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>
#include <Qt>
#include <QtGlobal>

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <qlatin1stringview.h>
#include <string>
#include <utility>
#include <vector>


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
    case CanvasTool::Ruler:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Ruler");
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
    case CanvasTool::Ruler:
      return QStringLiteral("tools.ruler");
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
    case CanvasTool::Ruler:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Drag to measure, Shift keeps 45 degrees. Drag an end or the line to adjust "
             "it, %ALT%+drag an end to measure an angle.");
    default:
      return nullptr;
  }
}

// Status-bar sentence shown when the tool is picked; nullptr shows the tool
// name. Reserved for tools whose workflow the name alone does not explain.
const char* tool_activation_hint_source(CanvasTool tool) {
  switch (tool) {
    case CanvasTool::Move:
      // GitHub issue 446: the drag gestures first, then layer selection.
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Move: drag to move, Shift+drag keeps it horizontal or vertical, %ALT%+drag "
             "duplicates. %CTRL%+click selects a layer, Shift+click toggles it. %CTRL%+drag selects a rectangle, "
             "as does a drag on empty space with Auto-Select on.");
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
    case CanvasTool::Ruler:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Ruler: drag to measure, Shift keeps 45 degrees. Drag an end or the line to "
             "adjust it, %ALT%+drag an end to add a second leg and measure the angle between them. Straighten "
             "levels the line.");
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
    default:
      return nullptr;
  }
}

// The painting tools' hint (GitHub issue 421), %1 the tool name: only the gestures a
// user would not find by painting (Shift+click line, the Alt clicks, Alt+right-drag),
// kept short for small screens. The Ctrl move and layer pick work but are not named.
const char* paint_tool_hint_source(CanvasTool tool) {
  switch (tool) {
    case CanvasTool::Brush:
    case CanvasTool::MixerBrush:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "%1: Shift+click draws a line, %ALT%+click picks a color, "
             "%ALT%+right-drag changes size and softness.");
    case CanvasTool::Clone:
    case CanvasTool::Healing:
      return QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "%1: %ALT%+click sets the source, Shift+click draws a line, "
             "%ALT%+right-drag changes size and softness.");
    default:
      return tool_has_ctrl_temporary_move(tool)
                 ? QT_TRANSLATE_NOOP("patchy::ui::MainWindow",
                                     "%1: Shift+click draws a line, %ALT%+right-drag changes size and softness.")
                 : nullptr;
  }
}

QString tool_action_object_name(CanvasTool tool) {
  auto name = QString::fromLatin1(tool_action_source(tool));
  name.remove(QLatin1Char(' '));
  return QStringLiteral("tool") + name + QStringLiteral("Action");
}

// A plain tool's palette button: "brushToolButton", "zoomToolButton".
QString plain_tool_button_object_name(CanvasTool tool) {
  auto name = QString::fromLatin1(tool_action_source(tool));
  name.remove(QLatin1Char(' '));
  if (!name.isEmpty()) {
    name[0] = name[0].toLower();
  }
  return name + QStringLiteral("ToolButton");
}

// A tool group member's own plain button (GitHub issue 499), "rectangularMarqueeMemberButton":
// the plain-button name would repeat a group button's ("moveToolButton").
QString tool_group_member_button_object_name(CanvasTool tool) {
  auto name = plain_tool_button_object_name(tool);
  name.chop(static_cast<qsizetype>(std::char_traits<char>::length("ToolButton")));
  return name + QStringLiteral("MemberButton");
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

// A plain tool button: shows its tool, and a left or a right press selects it and opens no
// menu (GitHub issue 388). The filter sees events before Qt's disabled check, so a disabled
// button stays inert.
QToolButton* make_plain_tool_button(QWidget* parent, QAction* action, const QString& object_name) {
  auto* button = new QToolButton(parent);
  button->setObjectName(object_name);
  button->setFocusPolicy(Qt::NoFocus);
  button->setDefaultAction(action);
  button->installEventFilter(new ToolButtonPressFilter(
      [button, action] {
        if (button->isEnabled() && action->isEnabled()) {
          action->trigger();
        }
      },
      {}, button));
  return button;
}

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
// row so it stays visible there. The current entry is the one the button shows, or what
// `current` returns (the screen mode button, whose entries are the View menu's actions).
class ToolFlyoutCurrentMarker final : public QObject {
public:
  ToolFlyoutCurrentMarker(QToolButton* button, QMenu* menu)
      : ToolFlyoutCurrentMarker(menu, [button = QPointer<QToolButton>(button)]() -> QAction* {
          return button != nullptr ? button->defaultAction() : nullptr;
        }) {}
  ToolFlyoutCurrentMarker(QMenu* menu, std::function<QAction*()> current)
      : QObject(menu), current_(std::move(current)) {
    menu->installEventFilter(this);
  }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() != QEvent::Paint || painting_ || !current_) {
      return QObject::eventFilter(watched, event);
    }
    auto* menu = static_cast<QMenu*>(watched);
    painting_ = true;
    QCoreApplication::sendEvent(menu, event);
    painting_ = false;
    auto* current = current_();
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
  std::function<QAction*()> current_;
  bool painting_{false};
};

// The flyout's shortcut keys start together in a column of their own (GitHub issue 499): the
// style right-aligns them, so a narrow key and a wide one ended together and started ragged.
// QMenu still sizes the menu with each row's key, and the column is the width it reserves for
// the widest; while the menu paints, the style gets each row without its key, and the keys
// are drawn after it, left aligned where that column starts.
class ToolFlyoutMenu final : public QMenu {
public:
  using QMenu::QMenu;

  // The keys' column ends this far before a row's right edge, where the style's keys ended.
  static constexpr int kKeyRightPadding = 14;

protected:
  void initStyleOption(QStyleOptionMenuItem* option, const QAction* action) const override {
    QMenu::initStyleOption(option, action);
    if (painting_) {
      if (const auto tab = option->text.indexOf(QLatin1Char('\t')); tab >= 0) {
        option->text.truncate(tab);
      }
    }
  }

  void paintEvent(QPaintEvent* event) override {
    painting_ = true;
    QMenu::paintEvent(event);
    painting_ = false;
    struct Key {
      QAction* action;
      QString text;
    };
    std::vector<Key> keys;
    const QFontMetrics metrics(font());
    int widest = 0;
    for (auto* action : actions()) {
      if (!action->isVisible() || action->isSeparator()) {
        continue;
      }
      if (auto text = key_text(*action); !text.isEmpty()) {
        widest = std::max(widest, metrics.horizontalAdvance(text));
        keys.push_back({action, std::move(text)});
      }
    }
    if (keys.empty()) {
      return;
    }
    const auto& colors = theme();
    QPainter painter(this);
    painter.setFont(font());
    for (const auto& key : keys) {
      const QRect row = actionGeometry(key.action);
      if (!row.isValid() || !event->rect().intersects(row)) {
        continue;
      }
      painter.setPen(!key.action->isEnabled()        ? colors.text_disabled
                     : activeAction() == key.action ? colors.text_on_accent
                                                    : colors.text_primary);
      painter.drawText(QRect(row.right() + 1 - kKeyRightPadding - widest, row.top(), widest, row.height()),
                       Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, key.text);
    }
  }

private:
  // The key QMenu shows for the row: text after a tab, else the shortcut (a flyout is a
  // context menu to Qt, so only while the action shows its shortcut there).
  [[nodiscard]] static QString key_text(const QAction& action) {
    const auto text = action.text();
    if (const auto tab = text.indexOf(QLatin1Char('\t')); tab >= 0) {
      return text.mid(tab + 1);
    }
    if (!action.isShortcutVisibleInContextMenu()) {
      return {};
    }
    return action.shortcut().toString(QKeySequence::NativeText);
  }

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

// When a tool group's flyout opens (GitHub issue 459; Preferences > Open tool groups on, the
// stored ToolFlyoutOpenMode). Hold keeps QToolButton's DelayedPopup. Click opens the flyout
// on the press (InstantPopup); the aboutToShow handler in configure_tool_flyout still selects
// the tool the button shows, as every other way in does. Hover opens it once the pointer has
// rested on the button for kHoverDelayMs while Patchy is active, without selecting anything,
// and closes it once the pointer has been outside the button and the menu (and the gap
// between them) for kHoverCloseMs; moving onto another tool group's button switches to that
// group's flyout at once. A click on the button while its hovered flyout is open selects the
// tool it shows.
// Press and hold, the double-click and the right-click open the flyout in every mode.
// While a flyout is open, a left press on another tool palette button closes it and then
// acts as a click there (GitHub issue 499): a plain button or a group selects its tool, and a
// group set to open on click opens its flyout. Qt only closes the menu on that press.
class ToolFlyoutOpener final : public QObject {
public:
  static constexpr int kHoverDelayMs = 250;
  static constexpr int kHoverCloseMs = 300;
  static constexpr int kHoverPollMs = 50;

  ToolFlyoutOpener(QToolButton* button, QMenu* menu) : QObject(button), button_(button), menu_(menu) {
    setObjectName(QLatin1String(kObjectName));
    hover_timer_.setSingleShot(true);
    hover_timer_.setInterval(kHoverDelayMs);
    QObject::connect(&hover_timer_, &QTimer::timeout, this, [this] { open_by_hover(); });
    close_timer_.setInterval(kHoverPollMs);
    QObject::connect(&close_timer_, &QTimer::timeout, this, [this] { poll_hover_close(); });
    button->installEventFilter(this);
    menu->installEventFilter(this);
  }

  static constexpr const char* kObjectName = "toolFlyoutOpener";
  // Set on the button while a hover opens its flyout: the aboutToShow handler then leaves
  // the active tool alone.
  static constexpr const char* kHoverOpeningProperty = "toolFlyoutHoverOpening";

  // The opener of a tool group button, or null.
  [[nodiscard]] static ToolFlyoutOpener* of(const QWidget* button) {
    if (button == nullptr) {
      return nullptr;
    }
    return dynamic_cast<ToolFlyoutOpener*>(
        button->findChild<QObject*>(QLatin1String(kObjectName), Qt::FindDirectChildrenOnly));
  }

  void set_mode(ToolFlyoutOpenMode mode) {
    mode_ = mode;
    hover_timer_.stop();
    if (button_ != nullptr) {
      button_->setPopupMode(mode == ToolFlyoutOpenMode::Click ? QToolButton::InstantPopup
                                                              : QToolButton::DelayedPopup);
    }
  }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (watched == menu_) {
      switch (event->type()) {
        case QEvent::Show:
          // The event loop the menu's own loop runs inside: a click handed on waits for it.
          show_loop_level_ = QThread::currentThread()->loopLevel();
          pending_click_ = nullptr;
          menu_->setAttribute(Qt::WA_NoMouseReplay, false);
          break;
        case QEvent::MouseButtonPress: {
          const auto* mouse = static_cast<QMouseEvent*>(event);
          const QPoint global = mouse->globalPosition().toPoint();
          // A left press on the button closes the hovered flyout (Qt does not pass it on to the
          // button): remember it, so the tool the button shows is selected once the menu is gone.
          if (hover_session_) {
            clicked_button_ = mouse->button() == Qt::LeftButton && button_global_rect().contains(global);
          }
          if (mouse->button() == Qt::LeftButton && !menu_->rect().contains(mouse->position().toPoint())) {
            if (auto* target = palette_button_at(global); target != nullptr) {
              // Handed on once the menu is gone; Qt must not also replay it (X11, macOS).
              pending_click_ = target;
              menu_->setAttribute(Qt::WA_NoMouseReplay, true);
            }
          }
          break;
        }
        case QEvent::Hide:
          if (pending_click_ != nullptr) {
            QTimer::singleShot(0, this, [this, level = show_loop_level_] { click_pending(level); });
          }
          break;
        default:
          break;
      }
      return QObject::eventFilter(watched, event);
    }
    switch (event->type()) {
      case QEvent::Enter:
        if (mode_ == ToolFlyoutOpenMode::Hover && armed_) {
          hover_timer_.start();
        }
        break;
      case QEvent::Leave:
        // Leaving re-arms the hover after a flyout closed with the pointer still on it.
        armed_ = true;
        hover_timer_.stop();
        break;
      case QEvent::MouseButtonPress:
      case QEvent::MouseButtonDblClick:
      case QEvent::Hide:
      case QEvent::EnabledChange:
        hover_timer_.stop();
        break;
      default:
        break;
    }
    return QObject::eventFilter(watched, event);
  }

private:
  [[nodiscard]] QRect button_global_rect() const {
    return button_ != nullptr ? QRect(button_->mapToGlobal(QPoint(0, 0)), button_->size()) : QRect();
  }

  [[nodiscard]] bool pointer_on_button() const { return button_global_rect().contains(QCursor::pos()); }

  // The other tool palette button (a tool's or a group's) at a global point, or null.
  [[nodiscard]] QToolButton* palette_button_at(QPoint global) const {
    QToolBar* palette = nullptr;
    for (auto* widget = button_ != nullptr ? button_->parentWidget() : nullptr; widget != nullptr && palette == nullptr;
         widget = widget->parentWidget()) {
      palette = qobject_cast<QToolBar*>(widget);
    }
    if (palette == nullptr) {
      return nullptr;
    }
    for (auto* candidate : palette->findChildren<QToolButton*>()) {
      const auto* action = candidate->defaultAction();
      if (candidate == button_ || !candidate->isVisible() || action == nullptr ||
          !action->property(kToolPaletteToolIdProperty).isValid()) {
        continue;
      }
      if (QRect(candidate->mapToGlobal(QPoint(0, 0)), candidate->size()).contains(global)) {
        return candidate;
      }
    }
    return nullptr;
  }

  // The click handed on from a press that closed the flyout, once the menu's loop has
  // unwound, so flyouts never nest.
  void click_pending(int loop_level) {
    if (QThread::currentThread()->loopLevel() > loop_level) {
      QTimer::singleShot(0, this, [this, loop_level] { click_pending(loop_level); });
      return;
    }
    const QPointer<QToolButton> target = pending_click_;
    pending_click_ = nullptr;
    if (target == nullptr || !target->isVisible() || !target->isEnabled()) {
      return;
    }
    if (const auto* opener = of(target); opener != nullptr && opener->mode_ == ToolFlyoutOpenMode::Click) {
      // As the press there would have: its flyout opens and selects the tool it shows.
      target->showMenu();
      return;
    }
    if (auto* action = target->defaultAction(); action != nullptr && action->isEnabled()) {
      action->trigger();
    }
  }

  // The hover-mode tool group button under the pointer, if it is not this one.
  [[nodiscard]] ToolFlyoutOpener* other_opener_under_pointer() const {
    for (auto* widget = QApplication::widgetAt(QCursor::pos()); widget != nullptr; widget = widget->parentWidget()) {
      if (auto* opener = of(widget); opener != nullptr) {
        return opener != this && opener->mode_ == ToolFlyoutOpenMode::Hover ? opener : nullptr;
      }
      if (widget->isWindow()) {
        break;
      }
    }
    return nullptr;
  }

  void open_by_hover() {
    if (mode_ != ToolFlyoutOpenMode::Hover || button_ == nullptr || menu_ == nullptr || !button_->isEnabled() ||
        !button_->isVisible() || QApplication::activeWindow() == nullptr ||
        QApplication::activePopupWidget() != nullptr || QGuiApplication::mouseButtons() != Qt::NoButton ||
        !pointer_on_button()) {
      return;
    }
    hover_session_ = true;
    clicked_button_ = false;
    outside_ms_ = 0;
    hand_over_to_ = nullptr;
    close_timer_.start();
    const QPointer<ToolFlyoutOpener> self(this);
    const QPointer<QToolButton> button = button_;
    button->setProperty(kHoverOpeningProperty, true);
    // Runs the menu's own event loop until it closes.
    button->showMenu();
    if (button != nullptr) {
      button->setProperty(kHoverOpeningProperty, false);
    }
    if (self == nullptr || button == nullptr) {
      return;
    }
    close_timer_.stop();
    hover_session_ = false;
    // The flyout closed with the pointer on the button (Escape, a pick): no reopening until
    // the pointer leaves it.
    armed_ = !pointer_on_button();
    if (clicked_button_) {
      auto* shown = button->defaultAction();
      if (button->isEnabled() && shown != nullptr && shown->isEnabled() && !shown->isChecked()) {
        shown->trigger();
      }
      return;
    }
    if (hand_over_to_ != nullptr) {
      // After this menu's loop has unwound, so flyouts never nest.
      QTimer::singleShot(0, hand_over_to_.data(), [next = hand_over_to_] {
        if (next != nullptr) {
          next->open_by_hover();
        }
      });
    }
  }

  void poll_hover_close() {
    if (!hover_session_ || menu_ == nullptr || button_ == nullptr || !menu_->isVisible()) {
      return;
    }
    const QPoint pointer = QCursor::pos();
    QRect button_rect = button_global_rect();
    const QRect menu_rect = menu_->geometry();
    // The gap between the button and a flyout beside it is part of the way across.
    if (menu_rect.left() > button_rect.right()) {
      button_rect.setRight(menu_rect.left());
    }
    if (button_rect.contains(pointer) || menu_rect.contains(pointer)) {
      outside_ms_ = 0;
      return;
    }
    if (auto* next = other_opener_under_pointer(); next != nullptr) {
      hand_over_to_ = next;
      menu_->close();
      return;
    }
    outside_ms_ += kHoverPollMs;
    if (outside_ms_ >= kHoverCloseMs) {
      menu_->close();
    }
  }

  QPointer<QToolButton> button_;
  QPointer<QMenu> menu_;
  ToolFlyoutOpenMode mode_{ToolFlyoutOpenMode::Hold};
  QTimer hover_timer_;
  QTimer close_timer_;
  bool armed_{true};
  bool hover_session_{false};
  bool clicked_button_{false};
  int outside_ms_{0};
  QPointer<ToolFlyoutOpener> hand_over_to_;
  QPointer<QToolButton> pending_click_;
  int show_loop_level_{0};
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
  // the extension button are not tools. A two-column row holds its buttons, so
  // their edges are mapped into the bar.
  int used_columns_width() const {
    if (palette_ == nullptr) {
      return 0;
    }
    const auto* footer = palette_->findChild<QWidget*>(QStringLiteral("toolPaletteFooter"));
    int right = 0;
    for (const auto* button : palette_->findChildren<QToolButton*>()) {
      if (button == extension_ || !button->isVisibleTo(palette_) ||
          (footer != nullptr && footer->isAncestorOf(button))) {
        continue;
      }
      right = std::max(right, button->mapTo(palette_.data(), QPoint(button->width(), 0)).x());
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
// The layout's bottom_reserve is the wheel-scroll expand row kept between the tools and
// the footer (GitHub issue 488). In two columns (GitHub issue 499) the footer is centered
// under the two tool columns and, while every tool shows, sits right under the last row
// instead of at the bottom of the bar: two columns are short, which left a wide gap.
class ToolPaletteFooterPlacer final : public QObject {
public:
  // Between the last tool row and a footer that follows the tools.
  static constexpr int kGapUnderTools = 8;

  ToolPaletteFooterPlacer(QToolBar* palette, ToolPaletteFooter* footer, QToolButton* extension,
                          ToolPaletteLayout* layout)
      : QObject(palette), palette_(palette), footer_(footer), extension_(extension), layout_(layout) {
    palette->installEventFilter(this);
    if (extension != nullptr) {
      extension->installEventFilter(this);
    }
    apply();
  }

  // Re-reserves the margin and re-places the footer after a layout pass or after the
  // expand row came or went.
  void refresh() { apply(); }

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
    // The top margin is the style's own; the bottom one carries the footer and the
    // expand row above it.
    const int base = margins.top();
    const int footer_height = footer_->footer_height();
    const int wanted = base + footer_height + extra_reserve();
    if (margins.bottom() != wanted) {
      layout->setContentsMargins(margins.left(), base, margins.right(), wanted);
    }
    int top = pinned_top();
    if (const int tools_bottom = layout_ != nullptr ? layout_->tools_bottom() : -1; tools_bottom >= 0) {
      top = std::min(top, tools_bottom + kGapUnderTools);
    }
    footer_->setGeometry(0, std::max(0, top), std::max(footer_->cell_width(), palette_->width()), footer_height);
    footer_->set_center_x(layout_ != nullptr ? layout_->footer_center_x() : -1.0);
    footer_->raise();
    seat_extension();
  }

  int extra_reserve() const { return layout_ != nullptr ? layout_->bottom_reserve() : 0; }

  // The footer's top at the bottom of the bar, where it sits while anything overflows.
  int pinned_top() const {
    const auto* layout = palette_->layout();
    const int base = layout != nullptr ? layout->contentsMargins().top() : 0;
    return std::max(0, palette_->height() - base - footer_->footer_height());
  }

  // Pins the extension button directly above the footer (above the expand row
  // while there is one), in the band
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
    // Measured from the bar's bottom: only a footer pinned there leaves room for the button.
    const int limit = pinned_top() - 1 - extra_reserve();
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
  QPointer<ToolPaletteLayout> layout_;
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
    case CanvasTool::Ruler:
      name = "tool-ruler";
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

  // Customize Tool Palette (GitHub issues 213, 316 and 199; docs/tool-palette.md,
  // "Customizing the palette"): Window > Customize Tool Palette..., the palette's
  // right-click menu and a Preferences button open the dialog.
  tool_palette_customization_ = stored_tool_palette_customization();
  tool_flyout_open_mode_ = stored_tool_flyout_open_mode();
  customize_tool_palette_action_ = new QAction(tr("Customize Tool Palette..."), this);
  customize_tool_palette_action_->setObjectName(QStringLiteral("windowCustomizeToolPaletteAction"));
  bind_action_text(customize_tool_palette_action_,
                   QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Customize Tool Palette..."));
  customize_tool_palette_action_->setIcon(simple_icon(QStringLiteral("gear")));
  customize_tool_palette_action_->setMenuRole(QAction::NoRole);
  customize_tool_palette_action_->setProperty(kWorksWithoutDocumentProperty, true);
  register_hotkey(customize_tool_palette_action_, "window.customize_tool_palette", QKeySequence());
  connect(customize_tool_palette_action_, &QAction::triggered, this,
          [this] { show_tool_palette_customization(ToolPaletteCustomizeTab::Tools); });
  if (ctx.window_menu != nullptr && window_panels_separator_ != nullptr) {
    ctx.window_menu->insertAction(window_panels_separator_, customize_tool_palette_action_);
  }
  auto* palette_stash = new QWidget(this);
  palette_stash->setObjectName(QStringLiteral("toolPaletteStash"));
  palette_stash->hide();
  tool_palette_layout_ = new ToolPaletteLayout(tool_palette, palette_stash);

  // The palette is ordered in clusters split by separators: select, paint,
  // retouch, draw/type, view. Tools sharing a slot get a flyout button.
  int cluster = 0;
  const auto create_flyout_tool_action =
      [this, tool_group](QMenu* menu, const QString& label, CanvasTool tool, QKeySequence shortcut) {
        auto* action = new QAction(label, this);
        bind_action_text(action, tool_action_source(tool));
        action->setIcon(tool_icon(tool));
        action->setCheckable(true);
        action->setData(static_cast<int>(tool));
        action->setObjectName(tool_action_object_name(tool));
        action->setProperty(kToolPaletteToolIdProperty, tool_hotkey_id(tool));
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
  const auto configure_tool_flyout = [this, &cluster](QToolBar* palette, QMenu* menu, QToolButton* button,
                                                      QAction* default_action, const QList<QAction*>& actions,
                                                      const char* cycle_source, const char* cycle_object_name,
                                                      const char* cycle_id, QKeySequence cycle_shortcut) {
    button->setProperty("toolFlyout", true);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    // Hold's popup mode; ToolFlyoutOpener sets the one the stored open mode asks for.
    button->setPopupMode(QToolButton::DelayedPopup);
    button->setMenu(menu);
    button->setDefaultAction(default_action);
    button->setToolTip(default_action->toolTip());
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
    // A flyout the pointer opened by resting on the button (Hover) selects nothing.
    QObject::connect(menu, &QMenu::aboutToShow, button, [button] {
      if (button->property(ToolFlyoutOpener::kHoverOpeningProperty).toBool()) {
        return;
      }
      auto* shown = button->defaultAction();
      if (button->isEnabled() && shown != nullptr && shown->isEnabled() && !shown->isChecked()) {
        shown->trigger();
      }
    });
    new ToolFlyoutMenuPlacement(button, menu);
    new ToolFlyoutCurrentMarker(button, menu);
    (new ToolFlyoutOpener(button, menu))->set_mode(tool_flyout_open_mode_);
    menu->setStyleSheet(tool_flyout_menu_style(palette->iconSize().width()));
    for (auto* action : actions) {
      // A member hidden from the flyout still works from its own key; the button keeps
      // showing a member that is on the palette.
      QObject::connect(action, &QAction::triggered, button, [this, button, menu, action] {
        if (tool_palette_layout_ != nullptr && tool_palette_layout_->tool_hidden(action)) {
          return;
        }
        button->setDefaultAction(action);
        button->setMenu(menu);
        button->setToolTip(action->toolTip());
      });
    }
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
    // The cycle walks the members the flyout shows (ToolPaletteLayout::cycle_members).
    QObject::connect(cycle, &QAction::triggered, button, [this, button, all_members = actions] {
      const auto members =
          tool_palette_layout_ != nullptr ? tool_palette_layout_->cycle_members(button) : all_members;
      if (members.isEmpty()) {
        return;
      }
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
    // Each member's own plain button, shown instead of the group's when it holds one tool or
    // with no tool groups (GitHub issue 499).
    QList<QToolButton*> member_buttons;
    for (auto* action : actions) {
      member_buttons.push_back(make_plain_tool_button(
          palette, action, tool_group_member_button_object_name(static_cast<CanvasTool>(action->data().toInt()))));
    }
    tool_palette_layout_->add_slot(cluster, button, menu, actions, cycle, member_buttons);
  };

  // Move and Artboard share a slot, as in Photoshop (docs/artboards.md).
  auto* move_menu = new ToolFlyoutMenu(tr("Move Tools"), tool_palette);
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
  auto* marquee_menu = new ToolFlyoutMenu(tr("Marquee Tools"), tool_palette);
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
  auto* lasso_menu = new ToolFlyoutMenu(tr("Lasso Tools"), tool_palette);
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
  auto* wand_menu = new ToolFlyoutMenu(tr("Wand Tools"), tool_palette);
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
  auto* crop_menu = new ToolFlyoutMenu(tr("Crop Tools"), tool_palette);
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
  ++cluster;

  add_tool_action(tool_palette, tool_group, tr("Brush"), CanvasTool::Brush, QKeySequence(Qt::Key_B), cluster)
      ->setChecked(true);
  add_tool_action(tool_palette, tool_group, tr("Eraser"), CanvasTool::Eraser, QKeySequence(Qt::Key_E), cluster);
  auto* gradient_menu = new ToolFlyoutMenu(tr("Fill Tools"), tool_palette);
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
  ++cluster;

  auto* stamp_menu = new ToolFlyoutMenu(tr("Stamp Tools"), tool_palette);
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
  auto* healing_menu = new ToolFlyoutMenu(tr("Healing Tools"), tool_palette);
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

  auto* detail_menu = new ToolFlyoutMenu(tr("Detail Tools"), tool_palette);
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

  auto* tone_menu = new ToolFlyoutMenu(tr("Toning Tools"), tool_palette);
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
  ++cluster;

  // The Pen Tools flyout: the Pen (default, and what P re-selects) plus the
  // dedicated Add/Delete/Convert anchor tools Photoshop users look for.
  auto* pen_menu = new ToolFlyoutMenu(tr("Pen Tools"), tool_palette);
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
  auto* path_select_menu = new ToolFlyoutMenu(tr("Path Tools"), tool_palette);
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
  auto* shape_menu = new ToolFlyoutMenu(tr("Shape Tools"), tool_palette);
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
  type_tool_action_ =
      add_tool_action(tool_palette, tool_group, tr("Type"), CanvasTool::Text, QKeySequence(Qt::Key_T), cluster);
  ++cluster;

  // Pick and Ruler share a slot, as in Photoshop (docs/pick-tool.md, GitHub issue 331).
  auto* pick_menu = new ToolFlyoutMenu(tr("Pick Tools"), tool_palette);
  pick_menu->setObjectName(QStringLiteral("pickToolMenu"));
  bind_widget_text(pick_menu, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Pick Tools"));
  auto* pick_action = create_flyout_tool_action(pick_menu, tr("Pick"), CanvasTool::Eyedropper, QKeySequence(Qt::Key_I));
  auto* ruler_action = create_flyout_tool_action(pick_menu, tr("Ruler"), CanvasTool::Ruler, QKeySequence());
  auto* pick_tool_button = new QToolButton(tool_palette);
  pick_tool_button->setObjectName(QStringLiteral("pickToolButton"));
  configure_tool_flyout(tool_palette, pick_menu, pick_tool_button, pick_action, {pick_action, ruler_action},
                        QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Cycle Pick Tools"),
                        "toolCyclePickAction", "tools.cycle.pick", QKeySequence(Qt::SHIFT | Qt::Key_I));
  add_tool_action(tool_palette, tool_group, tr("Hand"), CanvasTool::Pan, QKeySequence(Qt::Key_H), cluster);
  add_tool_action(tool_palette, tool_group, tr("Zoom"), CanvasTool::Zoom, QKeySequence(Qt::Key_Z), cluster);
  if (auto* zoom_button = findChild<QToolButton*>(QStringLiteral("zoomToolButton")); zoom_button != nullptr) {
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
    const bool tool_changed = current_tool_ != selected;
    current_tool_ = selected;
    if (canvas_ != nullptr) {
      canvas_->set_tool(selected);
    }
    if (tool_changed) {
      fire_theme_event(ThemeSoundEvent::ToolChanged, {{QStringLiteral("tool"), theme_tool_id(selected)}});
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
      statusBar()->showMessage(resolve_modifier_names(tr(hint)));
    } else if (const auto* paint_hint = paint_tool_hint_source(selected); paint_hint != nullptr) {
      statusBar()->showMessage(resolve_modifier_names(tr(paint_hint).arg(tool_name(selected))));
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
  // GIMP text layers painted on after their text was set open as pixels with the text kept
  // (GitHub issue 551, docs/xcf.md): this turns the selected ones back into editable text.
  type_restore_gimp_text_action_ = ctx.type_menu->addAction(QString());
  bind_action_text(type_restore_gimp_text_action_, QT_TR_NOOP("Restore GIMP Text"));
  type_restore_gimp_text_action_->setObjectName(QStringLiteral("typeRestoreGimpTextAction"));
  type_restore_gimp_text_action_->setMenuRole(QAction::NoRole);
  type_restore_gimp_text_action_->setEnabled(false);
  register_hotkey(type_restore_gimp_text_action_, "type.restore_gimp_text");
  connect(type_restore_gimp_text_action_, &QAction::triggered, this,
          [this] { restore_gimp_text_of_layers(rasterize_target_layer_ids(selected_or_active_layer_ids())); });

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
  // Screen modes (GitHub issue 212; docs/tool-palette.md): one flyout-style button under
  // Quick Mask whose icon shows the current mode. A click cycles like F; a hold, a
  // double-click or a right-click opens the View > Screen Mode actions, so the check marks
  // live in one place. A double-click's first click has already cycled.
  if (screen_mode_standard_action_ != nullptr && screen_mode_cycle_action_ != nullptr) {
    auto* button = new QToolButton(tool_palette_footer_);
    screen_mode_button_ = button;
    button->setObjectName(QStringLiteral("screenModeButton"));
    button->setAutoRaise(true);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setProperty("toolFlyout", true);
    button->setPopupMode(QToolButton::DelayedPopup);
    auto* menu = new ToolFlyoutMenu(button);
    menu->setObjectName(QStringLiteral("screenModeFlyoutMenu"));
    menu->addActions({screen_mode_standard_action_, screen_mode_full_with_menus_action_, screen_mode_full_action_});
    menu->addSeparator();
    menu->addAction(screen_mode_cycle_action_);
    button->setMenu(menu);
    const auto open_menu = [button] {
      if (button->isEnabled()) {
        button->showMenu();
      }
    };
    button->installEventFilter(new ToolButtonPressFilter(open_menu, open_menu, button));
    new ToolFlyoutMenuPlacement(button, menu);
    // Like a tool flyout: each mode's icon at the palette's icon size and the accent
    // marker on the current mode. The menu bar keeps its menus icon-free
    // (hide_menu_action_icons), so the shared actions show their icons only while this
    // menu is open.
    new ToolFlyoutCurrentMarker(menu, [this]() -> QAction* {
      switch (screen_mode_) {
        case ScreenMode::FullScreenWithMenus:
          return screen_mode_full_with_menus_action_;
        case ScreenMode::FullScreen:
          return screen_mode_full_action_;
        case ScreenMode::Standard:
          break;
      }
      return screen_mode_standard_action_;
    });
    const QList<QAction*> mode_actions{screen_mode_standard_action_, screen_mode_full_with_menus_action_,
                                       screen_mode_full_action_};
    const auto show_mode_icons = [mode_actions](bool shown) {
      for (auto* action : mode_actions) {
        action->setIconVisibleInMenu(shown);
      }
    };
    connect(menu, &QMenu::aboutToShow, this, [show_mode_icons] { show_mode_icons(true); });
    connect(menu, &QMenu::aboutToHide, this, [show_mode_icons] { show_mode_icons(false); });
    menu->setStyleSheet(tool_flyout_menu_style(tool_palette->iconSize().width()));
    connect(button, &QToolButton::clicked, this, [this] { cycle_screen_mode(); });
    // A rebound cycle key shows in the tooltip at once.
    connect(screen_mode_cycle_action_, &QAction::changed, this, [this] { refresh_screen_mode_button(); });
    register_retranslation([this] { refresh_screen_mode_button(); });
    tool_palette_footer_->set_screen_mode_button(button);
    refresh_screen_mode_button();
  }
  auto* footer_placer = new ToolPaletteFooterPlacer(
      tool_palette, tool_palette_footer_, tool_palette->findChild<QToolButton*>(QStringLiteral("qt_toolbar_ext_button")),
      tool_palette_layout_);
  tool_palette_layout_->on_footer_room_changed = [footer_placer = QPointer<ToolPaletteFooterPlacer>(footer_placer)] {
    if (footer_placer != nullptr) {
      footer_placer->refresh();
    }
  };
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

  // A right-click on the palette's empty space or a separator: Customize Tool Palette...
  // first, then the panel and toolbar list every toolbar's menu has (tool buttons swallow
  // their own right-clicks, GitHub issue 388).
  tool_palette->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(tool_palette, &QWidget::customContextMenuRequested, this, [this, tool_palette](const QPoint& position) {
    show_tool_palette_context_menu(tool_palette->mapToGlobal(position));
  });
  tool_palette_layout_->apply(tool_palette_customization_);
  apply_tool_palette_footer_rows();

  // Export the cross-phase locals bind_action_translations() still needs.
  ctx.tool_palette = tool_palette;
  ctx.default_colors_action = default_colors_action;
  ctx.swap_colors_action = swap_colors_action;
}

ToolPaletteCatalog MainWindow::tool_palette_catalog() const {
  // The clusters' headings in the dialog, in palette order.
  static constexpr const char* kClusterNames[] = {
      QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Select"), QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Paint"),
      QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Retouch"),
      QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Draw and Type"), QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "View")};
  constexpr int kClusterNameCount = static_cast<int>(std::size(kClusterNames));
  ToolPaletteCatalog catalog;
  if (tool_palette_layout_ != nullptr) {
    for (const auto& slot : tool_palette_layout_->entries()) {
      while (static_cast<int>(catalog.clusters.size()) <= slot.cluster) {
        const int index = std::min(static_cast<int>(catalog.clusters.size()), kClusterNameCount - 1);
        catalog.clusters.push_back({tr(kClusterNames[index]), {}});
      }
      ToolPaletteCatalogSlot entry;
      entry.flyout = slot.menu != nullptr;
      for (const auto* member : slot.members) {
        entry.members.push_back({member->property(kToolPaletteToolIdProperty).toString(), clean_action_text(member),
                                 member->icon(), member->shortcut().toString(QKeySequence::NativeText)});
      }
      if (entry.members.empty()) {
        continue;
      }
      entry.name = entry.flyout ? slot.menu->title() : entry.members.front().name;
      entry.shortcut = entry.members.front().shortcut;
      catalog.clusters[static_cast<std::size_t>(slot.cluster)].entries.push_back(std::move(entry));
    }
  }
  for (const auto& footer : layer_footer_buttons_) {
    catalog.layer_buttons.push_back({QString::fromLatin1(footer.id), tr(footer.name_source), footer.button->icon()});
  }
  if (primary_color_button_ != nullptr && secondary_color_button_ != nullptr) {
    catalog.foreground = primary_color_button_->color();
    catalog.background = secondary_color_button_->color();
  }
  if (quick_mask_action_ != nullptr) {
    catalog.quick_mask_icon = quick_mask_action_->icon();
  }
  if (screen_mode_button_ != nullptr) {
    catalog.screen_mode_icon = screen_mode_button_->icon();
  }
  return catalog;
}

void MainWindow::show_tool_palette_customization(ToolPaletteCustomizeTab tab, QWidget* parent) {
  // One dialog at a time: a second way in brings the open one forward on its tab.
  if (auto* open = findChild<QDialog*>(QStringLiteral("toolPaletteCustomizeDialog")); open != nullptr) {
    if (auto* tabs = open->findChild<QTabWidget*>(QStringLiteral("toolPaletteCustomizeTabs")); tabs != nullptr) {
      tabs->setCurrentIndex(tab == ToolPaletteCustomizeTab::LayerButtons ? 1 : 0);
    }
    open->raise();
    open->activateWindow();
    return;
  }
  const auto catalog = tool_palette_catalog();
  const ToolPaletteDialogChoice current{tool_palette_customization_, stored_toolbar_icon_size(),
                                        tool_flyout_open_mode_, layer_footer_icon_size_};
  // Opened from Preferences, the dialog is its child, so it closes with Preferences
  // (run_non_modal_dialog rejects children when their parent finishes). The footer shows the
  // Layers Panel tab's icon size while it is picked; Cancel puts the old one back.
  const auto preview_layer_icon_size = [this](ToolbarIconSize size) { apply_layer_footer_icon_size(size); };
  if (const auto chosen = request_tool_palette_customization(parent != nullptr ? parent : this, catalog, current, tab,
                                                             preview_layer_icon_size);
      chosen.has_value()) {
    apply_tool_palette_customization(chosen->customization);
    apply_tool_palette_preferences(chosen->icon_size, chosen->flyout_open_mode);
    set_stored_layer_footer_icon_size(chosen->layer_icon_size);
    apply_layer_footer_icon_size(chosen->layer_icon_size);
  }
}

void MainWindow::apply_tool_palette_customization(const ToolPaletteCustomization& customization) {
  tool_palette_customization_ = normalized_tool_palette_customization(customization);
  set_stored_tool_palette_customization(tool_palette_customization_);
  if (tool_palette_layout_ != nullptr) {
    tool_palette_layout_->apply(tool_palette_customization_);
  }
  apply_tool_palette_footer_rows();
  apply_layer_footer_visibility();
}

void MainWindow::apply_tool_palette_footer_rows() {
  if (tool_palette_footer_ == nullptr) {
    return;
  }
  const auto& hidden = tool_palette_customization_.hidden_footer_items;
  tool_palette_footer_->set_shown_rows(!hidden.contains(QLatin1String(kToolPaletteColorsId)),
                                       !hidden.contains(QLatin1String(kToolPaletteQuickMaskId)),
                                       !hidden.contains(QLatin1String(kToolPaletteScreenModeId)));
}

void MainWindow::apply_tool_palette_preferences(ToolbarIconSize icon_size, ToolFlyoutOpenMode flyout_open_mode) {
  if (icon_size != stored_toolbar_icon_size()) {
    set_stored_toolbar_icon_size(icon_size);
  }
  if (flyout_open_mode != stored_tool_flyout_open_mode()) {
    set_stored_tool_flyout_open_mode(flyout_open_mode);
  }
  apply_tool_palette_factor(toolbar_icon_size_factor(icon_size));
  tool_flyout_open_mode_ = flyout_open_mode;
  for (auto* object : findChildren<QObject*>(QLatin1String(ToolFlyoutOpener::kObjectName))) {
    if (auto* opener = dynamic_cast<ToolFlyoutOpener*>(object); opener != nullptr) {
      opener->set_mode(flyout_open_mode);
    }
  }
  // Preferences and the customize dialog both show these: an open one follows.
  const auto follow = [this](const char* combo_name, const QString& token) {
    for (auto* combo : findChildren<QComboBox*>(QLatin1String(combo_name))) {
      if (const int index = combo->findData(token); index >= 0) {
        combo->setCurrentIndex(index);
      }
    }
  };
  follow("preferencesToolbarIconSizeCombo", toolbar_icon_size_token(icon_size));
  follow("toolPaletteIconSizeCombo", toolbar_icon_size_token(icon_size));
  follow("preferencesToolFlyoutOpenCombo", tool_flyout_open_mode_token(flyout_open_mode));
  follow("toolPaletteFlyoutOpenCombo", tool_flyout_open_mode_token(flyout_open_mode));
}

void MainWindow::show_tool_palette_context_menu(const QPoint& global_position) {
  auto* menu = createPopupMenu();
  if (menu == nullptr) {
    menu = new QMenu(this);
  }
  auto* first = menu->actions().value(0);
  menu->insertAction(first, customize_tool_palette_action_);
  if (first != nullptr) {
    menu->insertSeparator(first);
  }
  menu->setAttribute(Qt::WA_DeleteOnClose);
  menu->popup(global_position);
}

QAction* MainWindow::add_tool_action(QToolBar* palette, QActionGroup* group, QString label, CanvasTool tool,
                                     QKeySequence shortcut, int cluster) {
  auto* action = new QAction(label, palette);
  bind_action_text(action, tool_action_source(tool));
  action->setIcon(tool_icon(tool));
  action->setCheckable(true);
  action->setData(static_cast<int>(tool));
  action->setObjectName(tool_action_object_name(tool));
  action->setProperty(kToolPaletteToolIdProperty, tool_hotkey_id(tool));
  if (const auto* detail = tool_tooltip_detail_source(tool); detail != nullptr) {
    action->setProperty(kActionTooltipDetailProperty, QString::fromLatin1(detail));
  }
  register_hotkey(action, tool_hotkey_id(tool), shortcut, QStringLiteral("tools"));
  group->addAction(action);
  // The window holds the action, so its shortcut works while the tool is hidden from the
  // palette (Customize Tool Palette); the palette shows it through the slot's button.
  addAction(action);
  register_tool_action(action);
  // A right-click selects the tool like a left click and opens no toolbar menu, as a
  // right-click on a flyout button does (GitHub issue 388).
  auto* button = make_plain_tool_button(palette, action, plain_tool_button_object_name(tool));
  tool_palette_layout_->add_slot(cluster, button, nullptr, {action}, nullptr);
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
  // Every flyout, the ones a customization keeps off the palette included.
  for (auto* button : findChildren<QToolButton*>()) {
    if (button->property("toolFlyout").toBool() && button->menu() != nullptr) {
      button->menu()->setStyleSheet(tool_flyout_menu_style(icon_extent));
    }
  }
  setStyleSheet(photoshop_style(factor));
}

}  // namespace patchy::ui
