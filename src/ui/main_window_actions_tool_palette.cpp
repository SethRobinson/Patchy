// MainWindow::build_tool_palette(): the tool-palette phase of create_actions()
// (the left QToolBar, tool actions and flyouts, color buttons and the quick-
// mask button), split out of main_window_actions.cpp along with
// add_tool_action (this TU is its only caller) and the anonymous-namespace
// tool helpers only they use.
// Pure function moves; behavior must stay identical, and the construction
// order is load-bearing (see create_actions() for the phase order).

#include "ui/main_window.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/main_window_actions_internal.hpp"

#include "core/blend_math.hpp"
#include "core/layer_metadata.hpp"
#include "core/smart_object.hpp"
#include "core/text_warp.hpp"
#include "core/vector_shape.hpp"
#include "core/warp_mesh.hpp"
#include "core/layer_render_utils.hpp"
#include "core/layer_tree.hpp"
#include "core/palette_presets.hpp"
#include "core/pattern_presets.hpp"
#include "core/pixel_tools.hpp"
#include "formats/palette_io.hpp"
#include "filters/builtin_filters.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/heif_document_io.hpp"
#include "formats/raw_document_io.hpp"
#include "plugins/legacy_photoshop_adapter.hpp"
#include "psd/psd_document_io.hpp"
#include "psd/psd_filter_effects.hpp"
#include "psd/psd_smart_objects.hpp"
#include "ui/action_icons.hpp"
#include "ui/app_settings.hpp"
#include "render/compositor.hpp"
#include "ui/blend_mode_ui.hpp"
#include "ui/brush_dynamics_popup.hpp"
#include "ui/brush_presets.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/brush_tip_manager_dialog.hpp"
#include "ui/brush_tip_picker.hpp"
#include "ui/default_brush_tips.hpp"
#include "ui/compatibility_report.hpp"
#include "ui/image_document_io.hpp"
#include "ui/image_save_options_dialog.hpp"
#include "ui/raw_develop_dialog.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/gradient_stops_editor.hpp"
#include "ui/gradient_library.hpp"
#include "ui/gradient_manager_dialog.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/document_float_window.hpp"
#include "ui/font_picker.hpp"
#include "ui/hotkey_editor.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/color_panel.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/localization.hpp"
#include "ui/measurement_units.hpp"
#include "ui/palette_convert_dialog.hpp"
#include "ui/palette_panel.hpp"
#include "ui/pattern_library.hpp"
#include "ui/pattern_manager_dialog.hpp"
#include "ui/photo_pattern_presets.hpp"
#include "ui/style_library.hpp"
#include "ui/print_dialog.hpp"
#include "ui/smart_object_render.hpp"
#include "ui/scanner_import.hpp"
#include "ui/image_sequence_dialog.hpp"
#include "ui/sprite_sheet_dialog.hpp"
#include "ui/tile_preview_window.hpp"
#include "ui/warp_text_dialog.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/splash_dialog.hpp"
#include "ui/update_checker.hpp"
#include "ui/zoom_status_bar.hpp"
#include "support/string_utils.hpp"

#include <QAbstractItemView>
#include <QAbstractItemModel>
#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QBrush>
#include <QBuffer>
#include <QButtonGroup>
#include <QByteArray>
#include <QDateTime>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QColorSpace>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLayout>
#include <QResizeEvent>
#include <QIcon>
#include <QImageReader>
#include <QInputDialog>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLabel>
#include <QKeySequence>
#include <QListWidget>
#include <QLinearGradient>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QMessageBox>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPushButton>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPolygon>
#include <QPointer>
#include <QProcess>
#include <QProgressDialog>
#include <QRegion>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QScopeGuard>
#include <QSettings>
#include <QShowEvent>
#include <QStandardPaths>
#include <QStandardItem>
#include <QStyledItemDelegate>
#include <QMutex>
#include <QRawFont>
#include <QTextCharFormat>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextLayout>
#include <QTextOption>
#include <QSignalBlocker>
#include <QSize>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QStringList>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOption>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>
#include <QTransform>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <functional>
#include <future>
#include <iostream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <tchar.h>
#include <tpcshrd.h>
#endif

#ifndef PATCHY_VERSION
#define PATCHY_VERSION "0.0.0"
#endif

// Icon resources live in the static patchy_ui library; force registration before first use.
int qInitResources_icons();

namespace patchy::ui {

namespace {

const char* tool_action_source(CanvasTool tool) {
  switch (tool) {
    case CanvasTool::Move:
      return "Move";
    case CanvasTool::Marquee:
      return "Marquee";
    case CanvasTool::EllipticalMarquee:
      return "Elliptical Marquee";
    case CanvasTool::Lasso:
      return "Lasso";
    case CanvasTool::MagneticLasso:
      return "Magnetic Lasso";
    case CanvasTool::MagicWand:
      return "Magic Wand";
    case CanvasTool::QuickSelect:
      return "Quick Select";
    case CanvasTool::Brush:
      return "Brush";
    case CanvasTool::MixerBrush:
      return "Mixer Brush";
    case CanvasTool::Clone:
      return "Clone";
    case CanvasTool::PatternStamp:
      return "Pattern Stamp";
    case CanvasTool::Healing:
      return "Healing Brush";
    case CanvasTool::Smudge:
      return "Smudge";
    case CanvasTool::Dodge:
      return "Dodge";
    case CanvasTool::Burn:
      return "Burn";
    case CanvasTool::Sponge:
      return "Sponge";
    case CanvasTool::BlurBrush:
      return "Blur";
    case CanvasTool::SharpenBrush:
      return "Sharpen";
    case CanvasTool::Eraser:
      return "Eraser";
    case CanvasTool::Gradient:
      return "Gradient";
    case CanvasTool::Line:
      return "Line";
    case CanvasTool::Rectangle:
      return "Rect";
    case CanvasTool::Ellipse:
      return "Ellipse";
    case CanvasTool::Fill:
      return "Fill";
    case CanvasTool::Eyedropper:
      return "Pick";
    case CanvasTool::Text:
      return "Type";
    case CanvasTool::Pan:
      return "Hand";
    case CanvasTool::Zoom:
      return "Zoom";
    case CanvasTool::Pen:
      return "Pen";
    case CanvasTool::PathSelect:
      return "Path Select";
    case CanvasTool::DirectSelect:
      return "Direct Select";
    case CanvasTool::Polygon:
      return "Polygon";
    case CanvasTool::CustomShape:
      return "Custom Shape";
  }
  return "Tool";
}

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
    case CanvasTool::MagneticLasso:
      return QStringLiteral("tools.magnetic_lasso");
    case CanvasTool::MagicWand:
      return QStringLiteral("tools.magic_wand");
    case CanvasTool::QuickSelect:
      return QStringLiteral("tools.quick_select");
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
  }
  return QStringLiteral("tools.unknown");
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

// Tool icons are hand-authored SVGs in src/ui/icons/tool-*.svg (32x32 viewBox, #dce2eb
// strokes with one optional #74c0ff accent); review them with the
// ui_tool_palette_icons_render_sheet visual test.
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
    case CanvasTool::MagneticLasso:
      name = "tool-magnetic-lasso";
      break;
    case CanvasTool::MagicWand:
      name = "tool-wand";
      break;
    case CanvasTool::QuickSelect:
      name = "tool-quick-select";
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
  }
  return QIcon(QStringLiteral(":/patchy/icons/%1.svg").arg(QLatin1String(name)));
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
  tool_palette->setIconSize(QSize(20, 20));
  tool_palette->setFixedWidth(43);
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
        register_hotkey(action, tool_hotkey_id(tool), shortcut, QStringLiteral("tools"));
        tool_group->addAction(action);
        menu->addAction(action);
        addAction(action);
        register_document_action(action);
        return action;
      };
  const auto configure_tool_flyout = [](QToolBar* palette, QMenu* menu, QToolButton* button,
                                        QAction* default_action,
                                        std::initializer_list<QAction*> actions) {
    button->setProperty("toolFlyout", true);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setPopupMode(QToolButton::DelayedPopup);
    button->setMenu(menu);
    button->setDefaultAction(default_action);
    button->setToolTip(default_action->toolTip());
    palette->addWidget(button);
    for (auto* action : actions) {
      QObject::connect(action, &QAction::triggered, button, [button, menu, action] {
        button->setDefaultAction(action);
        button->setMenu(menu);
        button->setToolTip(action->toolTip());
      });
    }
  };

  move_tool_action_ = add_tool_action(tool_palette, tool_group, tr("Move"), CanvasTool::Move, QKeySequence(Qt::Key_V));
  auto* marquee_menu = new QMenu(tr("Marquee Tools"), tool_palette);
  marquee_menu->setObjectName(QStringLiteral("marqueeToolMenu"));
  bind_widget_text(marquee_menu, "Marquee Tools");
  auto* rect_marquee_action =
      create_flyout_tool_action(marquee_menu, tr("Marquee"), CanvasTool::Marquee, QKeySequence(Qt::Key_M));
  auto* elliptical_marquee_action = create_flyout_tool_action(
      marquee_menu, tr("Elliptical Marquee"), CanvasTool::EllipticalMarquee, QKeySequence(Qt::SHIFT | Qt::Key_M));
  auto* marquee_tool_button = new QToolButton(tool_palette);
  marquee_tool_button->setObjectName(QStringLiteral("marqueeToolButton"));
  configure_tool_flyout(tool_palette, marquee_menu, marquee_tool_button, rect_marquee_action,
                        {rect_marquee_action, elliptical_marquee_action});
  auto* lasso_menu = new QMenu(tr("Lasso Tools"), tool_palette);
  lasso_menu->setObjectName(QStringLiteral("lassoToolMenu"));
  bind_widget_text(lasso_menu, "Lasso Tools");
  auto* lasso_action = create_flyout_tool_action(lasso_menu, tr("Lasso"), CanvasTool::Lasso, QKeySequence(Qt::Key_L));
  auto* magnetic_lasso_action = create_flyout_tool_action(lasso_menu, tr("Magnetic Lasso"), CanvasTool::MagneticLasso,
                                                          QKeySequence(Qt::SHIFT | Qt::Key_L));
  auto* lasso_tool_button = new QToolButton(tool_palette);
  lasso_tool_button->setObjectName(QStringLiteral("lassoToolButton"));
  configure_tool_flyout(tool_palette, lasso_menu, lasso_tool_button, lasso_action,
                        {lasso_action, magnetic_lasso_action});
  auto* wand_menu = new QMenu(tr("Wand Tools"), tool_palette);
  wand_menu->setObjectName(QStringLiteral("wandToolMenu"));
  bind_widget_text(wand_menu, "Wand Tools");
  auto* magic_wand_action =
      create_flyout_tool_action(wand_menu, tr("Magic Wand"), CanvasTool::MagicWand, QKeySequence(Qt::Key_W));
  auto* quick_select_action =
      create_flyout_tool_action(wand_menu, tr("Quick Select"), CanvasTool::QuickSelect, QKeySequence(Qt::SHIFT | Qt::Key_W));
  auto* wand_tool_button = new QToolButton(tool_palette);
  wand_tool_button->setObjectName(QStringLiteral("wandToolButton"));
  configure_tool_flyout(tool_palette, wand_menu, wand_tool_button, magic_wand_action,
                        {magic_wand_action, quick_select_action});
  tool_palette->addSeparator();

  add_tool_action(tool_palette, tool_group, tr("Brush"), CanvasTool::Brush, QKeySequence(Qt::Key_B))->setChecked(true);
  add_tool_action(tool_palette, tool_group, tr("Eraser"), CanvasTool::Eraser, QKeySequence(Qt::Key_E));
  auto* gradient_menu = new QMenu(tr("Fill Tools"), tool_palette);
  gradient_menu->setObjectName(QStringLiteral("gradientToolMenu"));
  bind_widget_text(gradient_menu, "Fill Tools");
  auto* gradient_action =
      create_flyout_tool_action(gradient_menu, tr("Gradient"), CanvasTool::Gradient, QKeySequence(Qt::Key_G));
  auto* fill_action =
      create_flyout_tool_action(gradient_menu, tr("Fill"), CanvasTool::Fill, QKeySequence(Qt::SHIFT | Qt::Key_G));
  auto* gradient_tool_button = new QToolButton(tool_palette);
  gradient_tool_button->setObjectName(QStringLiteral("gradientToolButton"));
  configure_tool_flyout(tool_palette, gradient_menu, gradient_tool_button, gradient_action,
                        {gradient_action, fill_action});
  tool_palette->addSeparator();

  auto* stamp_menu = new QMenu(tr("Stamp Tools"), tool_palette);
  stamp_menu->setObjectName(QStringLiteral("stampToolMenu"));
  bind_widget_text(stamp_menu, "Stamp Tools");
  auto* clone_action = create_flyout_tool_action(stamp_menu, tr("Clone"), CanvasTool::Clone, QKeySequence(Qt::Key_S));
  auto* pattern_stamp_action = create_flyout_tool_action(stamp_menu, tr("Pattern Stamp"), CanvasTool::PatternStamp,
                                                         QKeySequence(Qt::SHIFT | Qt::Key_S));
  auto* stamp_tool_button = new QToolButton(tool_palette);
  stamp_tool_button->setObjectName(QStringLiteral("stampToolButton"));
  configure_tool_flyout(tool_palette, stamp_menu, stamp_tool_button, clone_action,
                        {clone_action, pattern_stamp_action});
  add_tool_action(tool_palette, tool_group, tr("Healing Brush"), CanvasTool::Healing,
                  QKeySequence(Qt::Key_J));

  auto* detail_menu = new QMenu(tr("Detail Tools"), tool_palette);
  detail_menu->setObjectName(QStringLiteral("detailToolMenu"));
  bind_widget_text(detail_menu, "Detail Tools");
  auto* smudge_action =
      create_flyout_tool_action(detail_menu, tr("Smudge"), CanvasTool::Smudge, QKeySequence(Qt::Key_R));
  auto* mixer_brush_action = create_flyout_tool_action(
      detail_menu, tr("Mixer Brush"), CanvasTool::MixerBrush, QKeySequence());
  auto* blur_action = create_flyout_tool_action(detail_menu, tr("Blur"), CanvasTool::BlurBrush,
                                                 QKeySequence(Qt::SHIFT | Qt::Key_R));
  auto* sharpen_action =
      create_flyout_tool_action(detail_menu, tr("Sharpen"), CanvasTool::SharpenBrush, QKeySequence());
  auto* detail_button = new QToolButton(tool_palette);
  detail_button->setObjectName(QStringLiteral("detailToolButton"));
  configure_tool_flyout(tool_palette, detail_menu, detail_button, smudge_action,
                        {smudge_action, mixer_brush_action, blur_action, sharpen_action});

  auto* tone_menu = new QMenu(tr("Toning Tools"), tool_palette);
  tone_menu->setObjectName(QStringLiteral("toneToolMenu"));
  bind_widget_text(tone_menu, "Toning Tools");
  auto* dodge_action =
      create_flyout_tool_action(tone_menu, tr("Dodge"), CanvasTool::Dodge, QKeySequence(Qt::Key_O));
  auto* burn_action = create_flyout_tool_action(tone_menu, tr("Burn"), CanvasTool::Burn,
                                                 QKeySequence(Qt::SHIFT | Qt::Key_O));
  auto* sponge_action =
      create_flyout_tool_action(tone_menu, tr("Sponge"), CanvasTool::Sponge, QKeySequence());
  auto* tone_button = new QToolButton(tool_palette);
  tone_button->setObjectName(QStringLiteral("toneToolButton"));
  configure_tool_flyout(tool_palette, tone_menu, tone_button, dodge_action,
                        {dodge_action, burn_action, sponge_action});
  tool_palette->addSeparator();

  add_tool_action(tool_palette, tool_group, tr("Pen"), CanvasTool::Pen, QKeySequence(Qt::Key_P));
  // The Path Tools flyout sits directly after the Pen: drawing and adjusting
  // paths alternate constantly, so the two slots stay adjacent.
  auto* path_select_menu = new QMenu(tr("Path Tools"), tool_palette);
  path_select_menu->setObjectName(QStringLiteral("pathSelectToolMenu"));
  bind_widget_text(path_select_menu, "Path Tools");
  auto* path_select_action = create_flyout_tool_action(path_select_menu, tr("Path Select"),
                                                       CanvasTool::PathSelect, QKeySequence(Qt::Key_A));
  auto* direct_select_action =
      create_flyout_tool_action(path_select_menu, tr("Direct Select"), CanvasTool::DirectSelect,
                                QKeySequence(Qt::SHIFT | Qt::Key_A));
  auto* path_select_button = new QToolButton(tool_palette);
  path_select_button->setObjectName(QStringLiteral("pathSelectToolButton"));
  configure_tool_flyout(tool_palette, path_select_menu, path_select_button, path_select_action,
                        {path_select_action, direct_select_action});
  auto* shape_menu = new QMenu(tr("Shape Tools"), tool_palette);
  shape_menu->setObjectName(QStringLiteral("shapeToolMenu"));
  bind_widget_text(shape_menu, "Shape Tools");
  auto* line_tool_action =
      create_flyout_tool_action(shape_menu, tr("Line"), CanvasTool::Line, QKeySequence());  // Ctrl+Shift+U belongs to Desaturate
  auto* rect_tool_action = create_flyout_tool_action(shape_menu, tr("Rect"), CanvasTool::Rectangle, QKeySequence(Qt::Key_U));
  auto* ellipse_tool_action =
      create_flyout_tool_action(shape_menu, tr("Ellipse"), CanvasTool::Ellipse, QKeySequence(Qt::SHIFT | Qt::Key_U));
  auto* polygon_tool_action =
      create_flyout_tool_action(shape_menu, tr("Polygon"), CanvasTool::Polygon, QKeySequence());
  auto* custom_shape_tool_action = create_flyout_tool_action(
      shape_menu, tr("Custom Shape"), CanvasTool::CustomShape, QKeySequence());
  auto* shape_tool_button = new QToolButton(tool_palette);
  shape_tool_button->setObjectName(QStringLiteral("shapeToolButton"));
  configure_tool_flyout(tool_palette, shape_menu, shape_tool_button, rect_tool_action,
                        {line_tool_action, rect_tool_action, ellipse_tool_action,
                         polygon_tool_action, custom_shape_tool_action});
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
            canvas_->set_zoom_centered(1.0);
            refresh_document_info();
            statusBar()->showMessage(tr("Actual Pixels"));
          }
        },
        zoom_button));
  }
  connect(tool_group, &QActionGroup::triggered, this, [this](QAction* action) {
    if (canvas_ == nullptr) {
      return;
    }
    const auto selected = static_cast<CanvasTool>(action->data().toInt());
    if (canvas_->free_transform_active()) {
      canvas_->finish_free_transform();
    }
    if (canvas_->warp_transform_active()) {
      canvas_->finish_warp_transform();
    }
    if (selected != CanvasTool::Text) {
      finish_active_text_editor();
    }
    current_tool_ = selected;
    canvas_->set_tool(selected);
    set_eraser_brush_settings_active(selected == CanvasTool::Eraser);
    if (selected != CanvasTool::Text ||
        canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr) {
      canvas_->setFocus(Qt::OtherFocusReason);
    }
    refresh_options_bar();
    refresh_document_info();
    statusBar()->showMessage(tool_name(selected));
  });
  ctx.type_menu->addAction(type_tool_action_);

  auto* palette_spacer = new QWidget(tool_palette);
  palette_spacer->setObjectName(QStringLiteral("toolPaletteSpacer"));
  palette_spacer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
  tool_palette->addWidget(palette_spacer);
  tool_palette->addSeparator();
  auto* default_colors_action = tool_palette->addAction(tr("Default Colors"));
  auto* swap_colors_action = tool_palette->addAction(tr("Swap Colors"));
  default_colors_action->setObjectName(QStringLiteral("colorDefaultAction"));
  swap_colors_action->setObjectName(QStringLiteral("colorSwapAction"));
  default_colors_action->setIcon(simple_icon(QStringLiteral("D")));
  swap_colors_action->setIcon(simple_icon(QStringLiteral("X")));
  register_hotkey(default_colors_action, "color.default", QKeySequence(Qt::Key_D), QStringLiteral("color"));
  register_hotkey(swap_colors_action, "color.swap", QKeySequence(Qt::Key_X), QStringLiteral("color"));
  primary_color_button_ = new QPushButton(tr("FG"), tool_palette);
  secondary_color_button_ = new QPushButton(tr("BG"), tool_palette);
  primary_color_button_->setObjectName(QStringLiteral("foregroundColorButton"));
  secondary_color_button_->setObjectName(QStringLiteral("backgroundColorButton"));
  primary_color_button_->setToolTip(tr("Foreground color"));
  secondary_color_button_->setToolTip(tr("Background color"));
  tool_palette->addWidget(primary_color_button_);
  tool_palette->addWidget(secondary_color_button_);
  tool_palette->addSeparator();
  tool_palette->addAction(quick_mask_action_);
  if (auto* quick_mask_button = qobject_cast<QToolButton*>(
          tool_palette->widgetForAction(quick_mask_action_));
      quick_mask_button != nullptr) {
    quick_mask_button->setObjectName(QStringLiteral("quickMaskButton"));
    quick_mask_button->setToolTip(tr("Edit in Quick Mask Mode (Q)"));
    bind_tooltip(quick_mask_button, "Edit in Quick Mask Mode (Q)");
  }
  connect(primary_color_button_, &QPushButton::clicked, this, [this] { choose_primary_color(); });
  connect(secondary_color_button_, &QPushButton::clicked, this, [this] { choose_secondary_color(); });
  connect(swap_colors_action, &QAction::triggered, this, [this] { swap_colors(); });
  connect(default_colors_action, &QAction::triggered, this, [this] { default_colors(); });
  register_document_action(default_colors_action);
  register_document_action(swap_colors_action);

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
  register_hotkey(action, tool_hotkey_id(tool), shortcut, QStringLiteral("tools"));
  group->addAction(action);
  register_document_action(action);
  return action;
}

}  // namespace patchy::ui
