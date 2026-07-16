// MainWindow's action/menu/tool-palette/options-bar construction, split out of
// main_window.cpp: create_actions() builds the menu bar, tool palette and Options
// bar, plus add_tool_action and the anonymous-namespace helpers only they use.
// Also the retranslation machinery those menus feed (register_retranslation,
// retranslate_ui, retranslate_bound_children, refresh_language_actions and the
// combo retranslators).
// Pure function moves from main_window.cpp; behavior must stay identical.

#include "ui/main_window.hpp"
#include "ui/main_window_shared.hpp"

#include "core/blend_math.hpp"
#include "core/layer_metadata.hpp"
#include "core/smart_object.hpp"
#include "core/text_warp.hpp"
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

QString escape_qaction_ampersands(QString text) {
  return text.replace(QLatin1Char('&'), QStringLiteral("&&"));
}

void bind_translated_status_tip(QObject* object, const char* source,
                                const char* context = kMainWindowTranslationContext) {
  if (object == nullptr) {
    return;
  }
  object->setProperty(kTranslationContextProperty, QString::fromLatin1(context));
  object->setProperty(kTranslationStatusTipProperty, QString::fromLatin1(source));
}

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

// A left-to-right layout that wraps its items onto additional rows when the
// available width is too small, skipping hidden widgets so the active tool's
// options pack tightly. Used by the Options bar so controls fold to a second
// line instead of being clipped.
class FlowLayout final : public QLayout {
public:
  explicit FlowLayout(QWidget* parent, int horizontal_spacing = 6, int vertical_spacing = 4)
      : QLayout(parent), horizontal_spacing_(horizontal_spacing), vertical_spacing_(vertical_spacing) {
    setContentsMargins(0, 0, 0, 0);
  }
  ~FlowLayout() override {
    while (QLayoutItem* item = takeAt(0)) {
      delete item;
    }
  }

  void addItem(QLayoutItem* item) override { items_.append(item); }
  int count() const override { return static_cast<int>(items_.size()); }
  QLayoutItem* itemAt(int index) const override { return items_.value(index); }
  QLayoutItem* takeAt(int index) override {
    return (index >= 0 && index < items_.size()) ? items_.takeAt(index) : nullptr;
  }
  Qt::Orientations expandingDirections() const override { return {}; }
  bool hasHeightForWidth() const override { return true; }
  int heightForWidth(int width) const override { return do_layout(QRect(0, 0, width, 0), true); }
  void setGeometry(const QRect& rect) override {
    QLayout::setGeometry(rect);
    do_layout(rect, false);
  }
  QSize sizeHint() const override { return minimumSize(); }
  QSize minimumSize() const override {
    QSize size;
    for (auto* item : items_) {
      const QWidget* widget = item->widget();
      if (widget != nullptr && widget->isHidden()) {
        continue;
      }
      size = size.expandedTo(item->minimumSize());
    }
    const auto margins = contentsMargins();
    size += QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
    return size;
  }

private:
  int do_layout(const QRect& rect, bool test_only) const {
    const auto margins = contentsMargins();
    const QRect effective = rect.adjusted(margins.left(), margins.top(), -margins.right(), -margins.bottom());
    int x = effective.x();
    int y = effective.y();
    int line_height = 0;
    for (auto* item : items_) {
      QWidget* widget = item->widget();
      if (widget != nullptr && widget->isHidden()) {
        continue;
      }
      const QSize hint = item->sizeHint();
      int next_x = x + hint.width() + horizontal_spacing_;
      if (next_x - horizontal_spacing_ > effective.right() + 1 && line_height > 0) {
        x = effective.x();
        y = y + line_height + vertical_spacing_;
        next_x = x + hint.width() + horizontal_spacing_;
        line_height = 0;
      }
      if (!test_only) {
        item->setGeometry(QRect(QPoint(x, y), hint));
      }
      x = next_x;
      line_height = std::max(line_height, hint.height());
    }
    return y + line_height - rect.y() + margins.bottom();
  }

  QList<QLayoutItem*> items_;
  int horizontal_spacing_;
  int vertical_spacing_;
};

// Hosts the Options bar controls in a FlowLayout and reports the wrapped height
// for its current width so the surrounding QToolBar grows to a second row.
class OptionsFlowContainer final : public QWidget {
public:
  using QWidget::QWidget;

  QSize sizeHint() const override {
    const int available = width() > 0 ? width() : 1200;
    const int height = layout() != nullptr ? layout()->heightForWidth(available) : 0;
    return QSize(available, height);
  }
  QSize minimumSizeHint() const override {
    const int available = width() > 0 ? width() : 0;
    const int height = layout() != nullptr ? layout()->heightForWidth(std::max(available, 1)) : 0;
    return QSize(layout() != nullptr ? layout()->minimumSize().width() : 0, height);
  }

protected:
  void resizeEvent(QResizeEvent* event) override {
    QWidget::resizeEvent(event);
    // Width changed: the wrapped height may differ, so ask the toolbar to relayout.
    updateGeometry();
  }
};

class CheckGlyphBox final : public QCheckBox {
public:
  explicit CheckGlyphBox(const QString& text, QWidget* parent = nullptr) : QCheckBox(text, parent) {
    setMinimumHeight(24);
  }

  QSize sizeHint() const override {
    const auto text_width = fontMetrics().horizontalAdvance(text());
    const auto minimum = objectName() == QStringLiteral("shapeFillCheck") ? 58 : 92;
    return QSize(std::max(minimum, text_width + 34), 24);
  }

protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const bool framed = objectName() == QStringLiteral("moveAutoSelectCheck") ||
                        objectName() == QStringLiteral("selectionAntiAliasCheck") ||
                        objectName() == QStringLiteral("cloneAlignedCheck") ||
                        objectName() == QStringLiteral("shapeFillCheck");
    if (framed) {
      painter.fillRect(rect(), QColor(41, 41, 41));
      painter.setPen(QPen(QColor(23, 23, 23), 1));
      painter.drawRect(rect().adjusted(0, 0, -1, -1));
      painter.setPen(QPen(QColor(93, 93, 93), 1));
      painter.drawLine(rect().topLeft(), rect().topRight());
    }

    const QRect box(7, (height() - 14) / 2, 14, 14);
    painter.setBrush(isChecked() ? QColor(20, 115, 230) : QColor(31, 31, 31));
    painter.setPen(QPen(isChecked() ? QColor(156, 207, 255) : QColor(120, 120, 120), 1));
    painter.drawRect(box);
    if (isChecked()) {
      painter.setPen(QPen(QColor(255, 255, 255), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      painter.drawLine(QPointF(box.left() + 3.0, box.center().y() + 0.5), QPointF(box.left() + 6.0, box.bottom() - 3.0));
      painter.drawLine(QPointF(box.left() + 6.0, box.bottom() - 3.0), QPointF(box.right() - 2.0, box.top() + 3.0));
    }

    painter.setPen(isEnabled() ? QColor(240, 240, 240) : QColor(145, 145, 145));
    painter.drawText(QRect(box.right() + 7, 0, width() - box.right() - 10, height()), Qt::AlignVCenter | Qt::AlignLeft,
                     text());
  }
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
  }
  return QIcon(QStringLiteral(":/patchy/icons/%1.svg").arg(QLatin1String(name)));
}

}  // namespace

void MainWindow::create_actions() {
  // Startup builds the options bar before any document exists (canvas_ is null):
  // a throwaway default-constructed canvas donates the initial control values,
  // which are identical to a fresh session canvas's. Once the first document
  // arrives, load_tool_settings() + sync_tool_option_controls_from_canvas()
  // re-read the controls from the real canvas with the stored settings applied.
  CanvasWidget startup_defaults_canvas;
  auto* canvas_defaults = canvas_ != nullptr ? canvas_ : &startup_defaults_canvas;

  auto* file_menu = menuBar()->addMenu(tr("&File"));
  auto* edit_menu = menuBar()->addMenu(tr("&Edit"));
  auto* image_menu = menuBar()->addMenu(tr("&Image"));
  auto* layer_menu = menuBar()->addMenu(tr("&Layer"));
  auto* type_menu = menuBar()->addMenu(tr("&Type"));
  auto* select_menu = menuBar()->addMenu(tr("&Select"));
  auto* filter_menu = menuBar()->addMenu(tr("&Filter"));
  auto* plugins_menu = menuBar()->addMenu(tr("&Plugins"));
  auto* view_menu = menuBar()->addMenu(tr("&View"));
  auto* window_menu = menuBar()->addMenu(tr("&Window"));
  auto* help_menu = menuBar()->addMenu(tr("&Help"));
  filter_menu->setObjectName(QStringLiteral("filterMenu"));
  bind_action_text(file_menu->menuAction(), "&File");
  bind_action_text(edit_menu->menuAction(), "&Edit");
  bind_action_text(image_menu->menuAction(), "&Image");
  bind_action_text(layer_menu->menuAction(), "&Layer");
  bind_action_text(type_menu->menuAction(), "&Type");
  bind_action_text(select_menu->menuAction(), "&Select");
  bind_action_text(filter_menu->menuAction(), "&Filter");
  bind_action_text(plugins_menu->menuAction(), "&Plugins");
  bind_action_text(view_menu->menuAction(), "&View");
  bind_action_text(window_menu->menuAction(), "&Window");
  bind_action_text(help_menu->menuAction(), "&Help");
  filter_menu->setObjectName(QStringLiteral("filterMenu"));

  auto* new_action = file_menu->addAction(tr("&New"));
  auto* open_action = file_menu->addAction(tr("&Open..."));
  recent_files_menu_ = file_menu->addMenu(tr("Open &Recent File"));
  recent_files_menu_->setObjectName(QStringLiteral("fileOpenRecentMenu"));
  configure_recent_files_context_menu(recent_files_menu_);
  recent_folders_menu_ = file_menu->addMenu(tr("Open Recent &Folder"));
  recent_folders_menu_->setObjectName(QStringLiteral("fileOpenRecentFolderMenu"));
  configure_recent_files_context_menu(recent_folders_menu_);
  recent_folders_menu_->setProperty(kRecentFoldersMenuProperty, true);
  auto* import_menu = file_menu->addMenu(tr("I&mport"));
  import_menu->setObjectName(QStringLiteral("fileImportMenu"));
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
#ifdef Q_OS_MACOS
  auto* import_scanner_action = import_menu->addAction(tr("From &Scanner..."));
#else
  auto* import_scanner_action = import_menu->addAction(tr("From &Scanner or Camera..."));
#endif
  import_scanner_action->setObjectName(QStringLiteral("fileImportScannerAction"));
  register_hotkey(import_scanner_action, "file.import_scanner");
  connect(import_scanner_action, &QAction::triggered, this, [this] { import_from_scanner(); });
#endif
  auto* import_sprite_sheet_action = import_menu->addAction(tr("Sprite Sheet to &Layers..."));
  import_sprite_sheet_action->setObjectName(QStringLiteral("fileImportSpriteSheetAction"));
  register_hotkey(import_sprite_sheet_action, "file.import_sprite_sheet");
  connect(import_sprite_sheet_action, &QAction::triggered, this, [this] { import_sprite_sheet(); });
  auto* place_embedded_action = file_menu->addAction(tr("Place &Embedded..."));
  place_embedded_action->setObjectName(QStringLiteral("filePlaceEmbeddedAction"));
  register_hotkey(place_embedded_action, "file.place_embedded");
  connect(place_embedded_action, &QAction::triggered, this, [this] { place_embedded_file(); });
  auto* save_action = file_menu->addAction(tr("&Save"));
  auto* save_as_action = file_menu->addAction(tr("Save &As..."));
  auto* export_flat_action = file_menu->addAction(tr("Export &Flat Image..."));
  auto* export_sprite_sheet_action = file_menu->addAction(tr("Export Layers as Sprite S&heet..."));
  export_sprite_sheet_action->setObjectName(QStringLiteral("fileExportSpriteSheetAction"));
  register_hotkey(export_sprite_sheet_action, "file.export_sprite_sheet");
  connect(export_sprite_sheet_action, &QAction::triggered, this, [this] { export_sprite_sheet(); });
  register_document_action(export_sprite_sheet_action);
  auto* page_setup_action = file_menu->addAction(tr("Page Set&up..."));
  auto* print_action = file_menu->addAction(tr("&Print..."));
  file_menu->addSeparator();
  auto* close_action = file_menu->addAction(tr("&Close"));
  auto* close_all_action = file_menu->addAction(tr("Close &All"));
  file_menu->addSeparator();
  auto* preferences_action = file_menu->addAction(tr("&Preferences..."));
  file_menu->addSeparator();
  auto* quit_action = file_menu->addAction(tr("&Quit"));
  new_action->setObjectName(QStringLiteral("fileNewAction"));
  open_action->setObjectName(QStringLiteral("fileOpenAction"));
  save_action->setObjectName(QStringLiteral("fileSaveAction"));
  save_as_action->setObjectName(QStringLiteral("fileSaveAsAction"));
  export_flat_action->setObjectName(QStringLiteral("fileExportFlatAction"));
  page_setup_action->setObjectName(QStringLiteral("filePageSetupAction"));
  print_action->setObjectName(QStringLiteral("filePrintAction"));
  close_action->setObjectName(QStringLiteral("fileCloseAction"));
  close_all_action->setObjectName(QStringLiteral("fileCloseAllAction"));
  preferences_action->setObjectName(QStringLiteral("filePreferencesAction"));
  new_action->setIcon(style()->standardIcon(QStyle::SP_FileIcon));
  open_action->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
  save_action->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
  save_as_action->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
  export_flat_action->setIcon(style()->standardIcon(QStyle::SP_DriveHDIcon));
  page_setup_action->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
  print_action->setIcon(style()->standardIcon(QStyle::SP_FileDialogContentsView));
  close_action->setIcon(style()->standardIcon(QStyle::SP_DialogCloseButton));
  close_all_action->setIcon(style()->standardIcon(QStyle::SP_DialogCloseButton));
  preferences_action->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
  register_hotkey(new_action, "file.new", QKeySequence(Qt::CTRL | Qt::Key_N));
  register_hotkey(open_action, "file.open", QKeySequence(Qt::CTRL | Qt::Key_O));
  register_hotkey(save_action, "file.save", QKeySequence(Qt::CTRL | Qt::Key_S));
  register_hotkey(save_as_action, "file.save_as", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
  register_hotkey(print_action, "file.print", QKeySequence(Qt::CTRL | Qt::Key_P));
  register_hotkey(close_action, "file.close", QKeySequence(Qt::CTRL | Qt::Key_W));
  register_hotkey(close_all_action, "file.close_all", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_W));
  // macOS relocation into the app menu (About / Settings… / Quit) happens via the menu
  // roles; Qt ignores the roles everywhere else, so they are set unconditionally.
  preferences_action->setMenuRole(QAction::PreferencesRole);
  quit_action->setMenuRole(QAction::QuitRole);
  register_hotkey(quit_action, "file.quit", QKeySequence(Qt::CTRL | Qt::Key_Q));
  register_hotkey(export_flat_action, "file.export_flat");
  register_hotkey(page_setup_action, "file.page_setup");
  // Cmd+, is the universal macOS settings shortcut (Qt maps CTRL to Cmd there);
  // Windows/Linux keep Preferences unbound by default.
  register_hotkey(preferences_action, "file.preferences",
                  platform_hotkey(QKeySequence(), QKeySequence(Qt::CTRL | Qt::Key_Comma)));

  connect(new_action, &QAction::triggered, this, [this] { create_new_document(); });
  connect(open_action, &QAction::triggered, this, [this] { open_document(); });
  connect(save_action, &QAction::triggered, this, [this] { save_document(); });
  connect(save_as_action, &QAction::triggered, this, [this] { save_document_as(); });
  connect(export_flat_action, &QAction::triggered, this, [this] { export_flat_image(); });
  connect(page_setup_action, &QAction::triggered, this, [this] { page_setup(); });
  connect(print_action, &QAction::triggered, this, [this] { print_document(); });
  connect(close_action, &QAction::triggered, this, [this] { close_active_document(); });
  connect(close_all_action, &QAction::triggered, this, [this] { close_all_document_tabs(); });
  connect(preferences_action, &QAction::triggered, this, [this] { show_preferences(); });
  connect(quit_action, &QAction::triggered, this, &QWidget::close);
  for (auto* action :
       {save_action, save_as_action, export_flat_action, page_setup_action, print_action, close_action, close_all_action}) {
    register_document_action(action);
  }

  undo_action_ = edit_menu->addAction(tr("&Undo"));
  redo_action_ = edit_menu->addAction(tr("&Redo"));
  undo_action_->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
  redo_action_->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
  // Ctrl+Alt+Z is Photoshop's "step backward" muscle memory; Ctrl+Y matches Windows redo.
  register_hotkey(undo_action_, "edit.undo",
                  QList<QKeySequence>{QKeySequence(Qt::CTRL | Qt::Key_Z), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Z)});
  register_hotkey(redo_action_, "edit.redo",
                  QList<QKeySequence>{QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), QKeySequence(Qt::CTRL | Qt::Key_Y)});
  connect(undo_action_, &QAction::triggered, this, [this] { undo(); });
  connect(redo_action_, &QAction::triggered, this, [this] { redo(); });
  edit_menu->addSeparator();
  auto* cut_action = edit_menu->addAction(tr("Cu&t"));
  auto* copy_action = edit_menu->addAction(tr("&Copy"));
  auto* copy_merged_action = edit_menu->addAction(tr("Copy Merged"));
  auto* paste_action = edit_menu->addAction(tr("&Paste"));
  auto* transform_action = edit_menu->addAction(tr("Free &Transform..."));
  auto* warp_transform_action = edit_menu->addAction(tr("Warp Transform"));
  cut_action->setObjectName(QStringLiteral("editCutAction"));
  copy_action->setObjectName(QStringLiteral("editCopyAction"));
  copy_merged_action->setObjectName(QStringLiteral("editCopyMergedAction"));
  paste_action->setObjectName(QStringLiteral("editPasteAction"));
  transform_action->setObjectName(QStringLiteral("editFreeTransformAction"));
  warp_transform_action->setObjectName(QStringLiteral("editWarpTransformAction"));
  cut_action->setIcon(simple_icon(QStringLiteral("CT")));
  copy_action->setIcon(simple_icon(QStringLiteral("CP")));
  copy_merged_action->setIcon(simple_icon(QStringLiteral("CM")));
  paste_action->setIcon(simple_icon(QStringLiteral("paste")));
  transform_action->setIcon(simple_icon(QStringLiteral("TR")));
  warp_transform_action->setIcon(simple_icon(QStringLiteral("WP")));
  register_hotkey(cut_action, "edit.cut", QKeySequence(Qt::CTRL | Qt::Key_X));
  register_hotkey(copy_action, "edit.copy", QKeySequence(Qt::CTRL | Qt::Key_C));
  register_hotkey(copy_merged_action, "edit.copy_merged", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
  register_hotkey(paste_action, "edit.paste", QKeySequence(Qt::CTRL | Qt::Key_V));
  register_hotkey(transform_action, "edit.free_transform", QKeySequence(Qt::CTRL | Qt::Key_T));
  register_hotkey(warp_transform_action, "edit.warp_transform", QKeySequence());
  connect(cut_action, &QAction::triggered, this, [this] { cut_selection(); });
  connect(copy_action, &QAction::triggered, this, [this] { copy_selection(); });
  connect(copy_merged_action, &QAction::triggered, this, [this] { copy_merged(); });
  connect(paste_action, &QAction::triggered, this, [this] { paste_clipboard(); });
  connect(transform_action, &QAction::triggered, this, [this] { transform_active_layer_dialog(); });
  connect(warp_transform_action, &QAction::triggered, this, [this] { warp_transform_active_layer(); });
  for (auto* action : {cut_action, copy_action, copy_merged_action, paste_action, transform_action,
                       warp_transform_action}) {
    register_document_action(action);
  }
  edit_menu->addSeparator();
  auto* select_all_action = edit_menu->addAction(tr("Select &All"));
  auto* clear_selection_action = edit_menu->addAction(tr("&Clear Selection"));
  auto* reselect_action = edit_menu->addAction(tr("&Reselect"));
  auto* inverse_selection_action = edit_menu->addAction(tr("&Inverse"));
  quick_mask_action_ = new QAction(tr("Edit in &Quick Mask Mode"), this);
  quick_mask_action_->setObjectName(QStringLiteral("selectQuickMaskAction"));
  quick_mask_action_->setCheckable(true);
  quick_mask_action_->setIcon(
      simple_icon(QStringLiteral("QM"), QColor(235, 95, 110)));
  bind_action_text(quick_mask_action_, "Edit in &Quick Mask Mode");
  auto* grow_selection_action = new QAction(tr("&Grow"), this);
  auto* similar_selection_action = new QAction(tr("Simi&lar"), this);
  auto* expand_selection_action = new QAction(tr("&Expand..."), this);
  auto* contract_selection_action = new QAction(tr("Con&tract..."), this);
  auto* border_selection_action = new QAction(tr("&Border..."), this);
  auto* layer_transparency_action = new QAction(tr("Load Layer &Transparency"), this);
  auto* stroke_selection_action = edit_menu->addAction(tr("&Stroke Selection"));
  auto* define_brush_tip_action = edit_menu->addAction(tr("Define Brush Tip from Selection"));
  define_brush_tip_action->setObjectName(QStringLiteral("editDefineBrushTipAction"));
  register_hotkey(define_brush_tip_action, "edit.define_brush_tip");
  connect(define_brush_tip_action, &QAction::triggered, this, [this] { define_brush_tip_from_selection(); });
  register_document_action(define_brush_tip_action);
  select_all_action->setObjectName(QStringLiteral("editSelectAllAction"));
  clear_selection_action->setObjectName(QStringLiteral("editDeselectAction"));
  reselect_action->setObjectName(QStringLiteral("selectReselectAction"));
  inverse_selection_action->setObjectName(QStringLiteral("selectInverseAction"));
  grow_selection_action->setObjectName(QStringLiteral("selectGrowAction"));
  similar_selection_action->setObjectName(QStringLiteral("selectSimilarAction"));
  expand_selection_action->setObjectName(QStringLiteral("selectExpandAction"));
  contract_selection_action->setObjectName(QStringLiteral("selectContractAction"));
  border_selection_action->setObjectName(QStringLiteral("selectBorderAction"));
  layer_transparency_action->setObjectName(QStringLiteral("selectLayerTransparencyAction"));
  stroke_selection_action->setObjectName(QStringLiteral("editStrokeSelectionAction"));
  select_all_action->setIcon(simple_icon(QStringLiteral("SA")));
  clear_selection_action->setIcon(simple_icon(QStringLiteral("DS")));
  reselect_action->setIcon(simple_icon(QStringLiteral("RS")));
  inverse_selection_action->setIcon(simple_icon(QStringLiteral("INV")));
  grow_selection_action->setIcon(simple_icon(QStringLiteral("GR")));
  similar_selection_action->setIcon(simple_icon(QStringLiteral("SIM")));
  expand_selection_action->setIcon(simple_icon(QStringLiteral("EXP")));
  contract_selection_action->setIcon(simple_icon(QStringLiteral("CTR")));
  border_selection_action->setIcon(simple_icon(QStringLiteral("BD")));
  layer_transparency_action->setIcon(simple_icon(QStringLiteral("AL")));
  stroke_selection_action->setIcon(simple_icon(QStringLiteral("stroke")));
  register_hotkey(select_all_action, "select.all", QKeySequence(Qt::CTRL | Qt::Key_A));
  register_hotkey(clear_selection_action, "select.deselect", QKeySequence(Qt::CTRL | Qt::Key_D));
  register_hotkey(reselect_action, "select.reselect", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D));
  register_hotkey(inverse_selection_action, "select.inverse", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I));
  register_hotkey(quick_mask_action_, "select.quick_mask",
                  QKeySequence(Qt::Key_Q));
  register_hotkey(grow_selection_action, "select.grow");
  register_hotkey(similar_selection_action, "select.similar");
  register_hotkey(expand_selection_action, "select.expand");
  register_hotkey(contract_selection_action, "select.contract");
  register_hotkey(border_selection_action, "select.border");
  register_hotkey(layer_transparency_action, "select.layer_transparency");
  register_hotkey(stroke_selection_action, "edit.stroke_selection");
  connect(select_all_action, &QAction::triggered, this,
          [this] { canvas_->run_selection_command(tr("Select All"), [this] { canvas_->select_all(); }); });
  connect(clear_selection_action, &QAction::triggered, this,
          [this] { canvas_->run_selection_command(tr("Deselect"), [this] { canvas_->clear_selection(); }); });
  connect(reselect_action, &QAction::triggered, this,
          [this] { canvas_->run_selection_command(tr("Reselect"), [this] { canvas_->reselect(); }); });
  connect(inverse_selection_action, &QAction::triggered, this,
          [this] { canvas_->run_selection_command(tr("Inverse Selection"), [this] { canvas_->invert_selection(); }); });
  connect(quick_mask_action_, &QAction::triggered, this,
          [this] { toggle_quick_mask_mode(); });
  connect(grow_selection_action, &QAction::triggered, this,
          [this] { canvas_->run_selection_command(tr("Grow Selection"), [this] { canvas_->grow_selection(); }); });
  connect(similar_selection_action, &QAction::triggered, this, [this] {
    canvas_->run_selection_command(tr("Select Similar"), [this] { canvas_->select_similar_to_selection(); });
  });
  connect(expand_selection_action, &QAction::triggered, this, [this] { expand_selection_dialog(); });
  connect(contract_selection_action, &QAction::triggered, this, [this] { contract_selection_dialog(); });
  connect(border_selection_action, &QAction::triggered, this, [this] { border_selection_dialog(); });
  connect(layer_transparency_action, &QAction::triggered, this, [this] {
    canvas_->run_selection_command(tr("Load Layer Transparency"),
                                   [this] { canvas_->select_active_layer_opaque_pixels(); });
  });
  connect(stroke_selection_action, &QAction::triggered, this, [this] { stroke_selection(); });
  for (auto* action : {select_all_action, clear_selection_action, reselect_action, inverse_selection_action,
                       quick_mask_action_,
                       grow_selection_action, similar_selection_action, expand_selection_action,
                       contract_selection_action, border_selection_action, layer_transparency_action,
                       stroke_selection_action}) {
    register_document_action(action);
  }
  for (auto* action : {select_all_action, clear_selection_action, reselect_action,
                       grow_selection_action, similar_selection_action,
                       expand_selection_action, contract_selection_action,
                       border_selection_action, layer_transparency_action,
                       stroke_selection_action}) {
    action->setProperty("patchy.quickMaskBlocked", true);
  }
  select_menu->addAction(select_all_action);
  select_menu->addAction(clear_selection_action);
  select_menu->addAction(reselect_action);
  select_menu->addAction(inverse_selection_action);
  select_menu->addSeparator();
  select_menu->addAction(quick_mask_action_);
  select_menu->addSeparator();
  select_menu->addAction(grow_selection_action);
  select_menu->addAction(similar_selection_action);
  select_menu->addAction(expand_selection_action);
  select_menu->addAction(contract_selection_action);
  select_menu->addAction(border_selection_action);
  select_menu->addAction(layer_transparency_action);
  select_menu->addSeparator();
  select_menu->addAction(stroke_selection_action);

  auto* add_layer_action = layer_menu->addAction(tr("&New Layer"));
  auto* add_folder_action = layer_menu->addAction(tr("New &Folder"));
  auto* new_adjustment_layer_menu = layer_menu->addMenu(tr("New &Adjustment Layer"));
  new_adjustment_layer_menu->setObjectName(QStringLiteral("layerNewAdjustmentMenu"));
  populate_new_adjustment_layer_menu(new_adjustment_layer_menu, QStringLiteral("layerNew"));
  auto* layer_via_copy_action = layer_menu->addAction(tr("Layer Via &Copy"));
  auto* layer_via_cut_action = layer_menu->addAction(tr("Layer Via Cu&t"));
  auto* add_mask_action = layer_menu->addAction(tr("Add Layer &Mask"));
  edit_layer_mask_action_ = layer_menu->addAction(tr("&Edit Layer Mask"));
  mask_overlay_action_ = layer_menu->addAction(tr("Show Mask &Overlay"));
  view_layer_mask_action_ = layer_menu->addAction(tr("View Layer Mask"));
  delete_layer_mask_action_ = layer_menu->addAction(tr("&Delete Layer Mask"));
  link_layer_mask_action_ = layer_menu->addAction(tr("Link Layer &Mask"));
  disable_layer_mask_action_ = layer_menu->addAction(tr("&Disable Layer Mask"));
  invert_layer_mask_action_ = layer_menu->addAction(tr("&Invert Layer Mask"));
  apply_layer_mask_action_ = layer_menu->addAction(tr("&Apply Layer Mask"));
  layer_clipping_mask_action_ = layer_menu->addAction(tr("Create Clipping Mask"));
  layer_menu->addSeparator();
  auto* edit_adjustment_action = layer_menu->addAction(tr("&Edit Adjustment..."));
  layer_blending_options_action_ = layer_menu->addAction(tr("Edit Layer &Styles..."));
  layer_copy_style_action_ = new QAction(tr("Copy Layer Style"), this);
  layer_paste_style_action_ = new QAction(tr("Paste Layer Style"), this);
  layer_delete_style_action_ = new QAction(tr("Delete Layer Style"), this);
  layer_rasterize_action_ = new QAction(tr("Rasterize"), this);
  layer_rasterize_layer_style_action_ = new QAction(tr("Rasterize (including layer style)"), this);
  layer_convert_smart_object_action_ = new QAction(tr("Convert to Smart Object"), this);
  layer_smart_object_edit_action_ = new QAction(tr("Edit Smart Object Contents"), this);
  layer_smart_object_replace_action_ = new QAction(tr("Replace Smart Object Contents..."), this);
  layer_smart_object_export_action_ = new QAction(tr("Export Smart Object Contents..."), this);
  layer_smart_object_via_copy_action_ = new QAction(tr("New Smart Object via Copy"), this);
  layer_smart_object_update_action_ = new QAction(tr("Update Smart Object Content"), this);
  layer_smart_object_relink_action_ = new QAction(tr("Relink to File..."), this);
  layer_smart_object_embed_action_ = new QAction(tr("Embed Linked Smart Object"), this);
  // The same operation as Rasterize, named so people who don't know the term can
  // still find "make this a plain layer again" where they look for it.
  layer_smart_object_to_normal_action_ = new QAction(tr("Convert to Normal Layer (Rasterize)"), this);
  auto* layer_smart_objects_menu = layer_menu->addMenu(tr("Smart Objects"));
  layer_smart_objects_menu->setObjectName(QStringLiteral("layerSmartObjectsMenu"));
  layer_smart_objects_menu->addAction(layer_convert_smart_object_action_);
  layer_smart_objects_menu->addAction(layer_smart_object_edit_action_);
  layer_smart_objects_menu->addAction(layer_smart_object_replace_action_);
  layer_smart_objects_menu->addAction(layer_smart_object_update_action_);
  layer_smart_objects_menu->addAction(layer_smart_object_relink_action_);
  layer_smart_objects_menu->addAction(layer_smart_object_embed_action_);
  layer_smart_objects_menu->addAction(layer_smart_object_export_action_);
  layer_smart_objects_menu->addAction(layer_smart_object_via_copy_action_);
  layer_smart_objects_menu->addSeparator();
  layer_smart_objects_menu->addAction(layer_smart_object_to_normal_action_);
  layer_menu->addSeparator();
  auto* duplicate_layer_action = layer_menu->addAction(tr("&Duplicate Layer"));
  auto* merge_visible_action = layer_menu->addAction(tr("Merge &Visible to New Layer"));
  merge_visible_action->setObjectName(QStringLiteral("layerMergeVisibleAction"));
  auto* merge_down_action = layer_menu->addAction(tr("Merge &Down"));
  merge_down_action->setObjectName(QStringLiteral("layerMergeDownAction"));
  auto* rename_layer_action = layer_menu->addAction(tr("&Rename Layer..."));
  auto* delete_layer_action = layer_menu->addAction(tr("&Delete Layer"));
  layer_menu->addSeparator();
  auto* fill_layer_action = layer_menu->addAction(tr("&Fill Layer / Selection"));
  auto* fill_background_action = layer_menu->addAction(tr("Fill With &Background Color"));
  auto* clear_layer_action = layer_menu->addAction(tr("&Clear Layer / Selection"));
  layer_menu->addSeparator();
  auto* flip_h_action = layer_menu->addAction(tr("Flip Layer &Horizontal"));
  auto* flip_v_action = layer_menu->addAction(tr("Flip Layer &Vertical"));
  layer_menu->addSeparator();
  auto* layer_up_action = layer_menu->addAction(tr("Move Layer &Up"));
  auto* layer_down_action = layer_menu->addAction(tr("Move Layer &Down"));
  add_layer_action->setObjectName(QStringLiteral("layerNewAction"));
  add_folder_action->setObjectName(QStringLiteral("layerNewFolderAction"));
  layer_via_copy_action->setObjectName(QStringLiteral("layerViaCopyAction"));
  layer_via_cut_action->setObjectName(QStringLiteral("layerViaCutAction"));
  add_mask_action->setObjectName(QStringLiteral("layerAddMaskAction"));
  edit_layer_mask_action_->setObjectName(QStringLiteral("layerEditMaskAction"));
  mask_overlay_action_->setObjectName(QStringLiteral("layerMaskOverlayAction"));
  view_layer_mask_action_->setObjectName(QStringLiteral("layerViewMaskAction"));
  delete_layer_mask_action_->setObjectName(QStringLiteral("layerDeleteMaskAction"));
  link_layer_mask_action_->setObjectName(QStringLiteral("layerLinkMaskAction"));
  disable_layer_mask_action_->setObjectName(QStringLiteral("layerDisableMaskAction"));
  invert_layer_mask_action_->setObjectName(QStringLiteral("layerInvertMaskAction"));
  apply_layer_mask_action_->setObjectName(QStringLiteral("layerApplyMaskAction"));
  layer_clipping_mask_action_->setObjectName(QStringLiteral("layerClippingMaskAction"));
  edit_adjustment_action->setObjectName(QStringLiteral("layerEditAdjustmentAction"));
  layer_blending_options_action_->setObjectName(QStringLiteral("layerBlendingOptionsAction"));
  layer_copy_style_action_->setObjectName(QStringLiteral("layerCopyStyleAction"));
  layer_paste_style_action_->setObjectName(QStringLiteral("layerPasteStyleAction"));
  layer_delete_style_action_->setObjectName(QStringLiteral("layerDeleteStyleAction"));
  layer_rasterize_action_->setObjectName(QStringLiteral("layerRasterizeAction"));
  layer_rasterize_layer_style_action_->setObjectName(QStringLiteral("layerRasterizeLayerStyleAction"));
  layer_convert_smart_object_action_->setObjectName(QStringLiteral("layerConvertSmartObjectAction"));
  layer_smart_object_edit_action_->setObjectName(QStringLiteral("layerSmartObjectEditAction"));
  layer_smart_object_replace_action_->setObjectName(QStringLiteral("layerSmartObjectReplaceAction"));
  layer_smart_object_export_action_->setObjectName(QStringLiteral("layerSmartObjectExportAction"));
  layer_smart_object_via_copy_action_->setObjectName(QStringLiteral("layerSmartObjectViaCopyAction"));
  layer_smart_object_update_action_->setObjectName(QStringLiteral("layerSmartObjectUpdateAction"));
  layer_smart_object_relink_action_->setObjectName(QStringLiteral("layerSmartObjectRelinkAction"));
  layer_smart_object_embed_action_->setObjectName(QStringLiteral("layerSmartObjectEmbedAction"));
  layer_smart_object_to_normal_action_->setObjectName(QStringLiteral("layerSmartObjectToNormalAction"));
  duplicate_layer_action->setObjectName(QStringLiteral("layerDuplicateAction"));
  delete_layer_action->setObjectName(QStringLiteral("layerDeleteAction"));
  fill_layer_action->setObjectName(QStringLiteral("layerFillForegroundAction"));
  fill_background_action->setObjectName(QStringLiteral("layerFillBackgroundAction"));
  clear_layer_action->setObjectName(QStringLiteral("layerClearAction"));
  add_layer_action->setIcon(simple_icon(QStringLiteral("new")));
  add_folder_action->setIcon(simple_icon(QStringLiteral("dir"), QColor(245, 205, 105)));
  layer_via_copy_action->setIcon(simple_icon(QStringLiteral("copy")));
  layer_via_cut_action->setIcon(simple_icon(QStringLiteral("cut"), QColor(255, 185, 120)));
  add_mask_action->setIcon(simple_icon(QStringLiteral("mask"), QColor(210, 220, 230)));
  edit_layer_mask_action_->setIcon(simple_icon(QStringLiteral("mask"), QColor(150, 205, 255)));
  mask_overlay_action_->setIcon(simple_icon(QStringLiteral("mask"), QColor(255, 120, 120)));
  view_layer_mask_action_->setIcon(simple_icon(QStringLiteral("mask"), QColor(235, 235, 235)));
  delete_layer_mask_action_->setIcon(simple_icon(QStringLiteral("mask"), QColor(255, 150, 150)));
  link_layer_mask_action_->setIcon(simple_icon(QStringLiteral("link"), QColor(210, 220, 230)));
  disable_layer_mask_action_->setIcon(simple_icon(QStringLiteral("off"), QColor(255, 190, 120)));
  invert_layer_mask_action_->setIcon(simple_icon(QStringLiteral("inv"), QColor(210, 220, 230)));
  apply_layer_mask_action_->setIcon(simple_icon(QStringLiteral("ok"), QColor(160, 220, 165)));
  layer_clipping_mask_action_->setIcon(simple_icon(QStringLiteral("clip"), QColor(150, 205, 255)));
  link_layer_mask_action_->setCheckable(true);
  disable_layer_mask_action_->setCheckable(true);
  edit_layer_mask_action_->setCheckable(true);
  mask_overlay_action_->setCheckable(true);
  view_layer_mask_action_->setCheckable(true);
  edit_adjustment_action->setIcon(simple_icon(QStringLiteral("ADJ"), QColor(190, 220, 255)));
  layer_blending_options_action_->setIcon(simple_icon(QStringLiteral("fx"), QColor(170, 210, 255)));
  layer_copy_style_action_->setIcon(simple_icon(QStringLiteral("fx"), QColor(170, 210, 255)));
  layer_paste_style_action_->setIcon(simple_icon(QStringLiteral("fx"), QColor(170, 210, 255)));
  layer_delete_style_action_->setIcon(simple_icon(QStringLiteral("fx"), QColor(255, 150, 150)));
  layer_rasterize_action_->setIcon(simple_icon(QStringLiteral("RA"), QColor(220, 220, 160)));
  layer_rasterize_layer_style_action_->setIcon(simple_icon(QStringLiteral("fx"), QColor(170, 210, 255)));
  layer_smart_object_to_normal_action_->setIcon(simple_icon(QStringLiteral("RA"), QColor(220, 220, 160)));
  duplicate_layer_action->setIcon(simple_icon(QStringLiteral("dup")));
  merge_visible_action->setIcon(simple_icon(QStringLiteral("merge")));
  merge_down_action->setIcon(simple_icon(QStringLiteral("merge"), QColor(160, 220, 255)));
  rename_layer_action->setIcon(simple_icon(QStringLiteral("RN")));
  delete_layer_action->setIcon(simple_icon(QStringLiteral("trash")));
  fill_layer_action->setIcon(simple_icon(QStringLiteral("fill")));
  fill_background_action->setIcon(simple_icon(QStringLiteral("fill"), QColor(160, 190, 255)));
  clear_layer_action->setIcon(simple_icon(QStringLiteral("clear")));
  flip_h_action->setIcon(simple_icon(QStringLiteral("FH")));
  flip_v_action->setIcon(simple_icon(QStringLiteral("FV")));
  layer_up_action->setIcon(style()->standardIcon(QStyle::SP_ArrowUp));
  layer_down_action->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
  register_hotkey(add_layer_action, "layer.new", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
  register_hotkey(layer_via_copy_action, "layer.via_copy", QKeySequence(Qt::CTRL | Qt::Key_J));
  register_hotkey(layer_via_cut_action, "layer.via_cut", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_J));
  register_hotkey(merge_visible_action, "layer.merge_visible", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
  register_hotkey(merge_down_action, "layer.merge_down", QKeySequence(Qt::CTRL | Qt::Key_E));
  register_hotkey(edit_layer_mask_action_, "layer.edit_mask", QKeySequence(Qt::CTRL | Qt::Key_Backslash));
  register_hotkey(mask_overlay_action_, "layer.mask_overlay", QKeySequence(Qt::Key_Backslash));
  register_hotkey(view_layer_mask_action_, "layer.view_mask");
  register_hotkey(fill_layer_action, "layer.fill", QKeySequence(Qt::ALT | Qt::Key_Backspace));
  register_hotkey(fill_background_action, "layer.fill_background", QKeySequence(Qt::CTRL | Qt::Key_Backspace));
  // Mac keyboards' delete key sends Backspace (Photoshop accepts both there); the
  // forward-delete key stays bound too. Windows/Linux keep plain Delete.
  register_hotkey(clear_layer_action, "layer.clear",
                  platform_hotkeys({QKeySequence(Qt::Key_Delete)},
                                   {QKeySequence(Qt::Key_Backspace), QKeySequence(Qt::Key_Delete)}));
  register_hotkey(add_folder_action, "layer.new_folder");
  register_hotkey(add_mask_action, "layer.add_mask");
  register_hotkey(delete_layer_mask_action_, "layer.delete_mask");
  register_hotkey(invert_layer_mask_action_, "layer.invert_mask");
  register_hotkey(apply_layer_mask_action_, "layer.apply_mask");
  register_hotkey(layer_clipping_mask_action_, "layer.toggle_clipping_mask",
                  QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_G));
  register_hotkey(edit_adjustment_action, "layer.edit_adjustment");
  register_hotkey(layer_blending_options_action_, "layer.styles");
  register_hotkey(duplicate_layer_action, "layer.duplicate");
  register_hotkey(rename_layer_action, "layer.rename");
  register_hotkey(delete_layer_action, "layer.delete");
  register_hotkey(flip_h_action, "layer.flip_horizontal");
  register_hotkey(flip_v_action, "layer.flip_vertical");
  register_hotkey(layer_up_action, "layer.move_up");
  register_hotkey(layer_down_action, "layer.move_down");
  connect(add_layer_action, &QAction::triggered, this, [this] { add_layer(); });
  connect(add_folder_action, &QAction::triggered, this, [this] { create_layer_folder(); });
  connect(layer_via_copy_action, &QAction::triggered, this, [this] { layer_via_copy(); });
  connect(layer_via_cut_action, &QAction::triggered, this, [this] { layer_via_cut(); });
  connect(add_mask_action, &QAction::triggered, this, [this] { add_layer_mask(); });
  connect(edit_layer_mask_action_, &QAction::triggered, this, [this](bool checked) {
    if (!checked) {
      set_layer_edit_target_ui(CanvasWidget::LayerEditTarget::Content, true);
      return;
    }
    const auto active = std::as_const(document()).active_layer_id();
    const auto* layer = active.has_value()
                            ? std::as_const(document()).find_layer(*active)
                            : nullptr;
    if (layer != nullptr && layer->mask().has_value()) {
      set_layer_edit_target_ui(CanvasWidget::LayerEditTarget::Mask, true);
    } else if (active.has_value()) {
      static_cast<void>(set_smart_filter_mask_edit_target_ui(
          *active, CanvasWidget::MaskDisplayMode::Overlay, true));
    }
  });
  connect(mask_overlay_action_, &QAction::triggered, this, [this](bool checked) { set_mask_overlay_shown(checked); });
  connect(view_layer_mask_action_, &QAction::triggered, this,
          [this](bool checked) { set_layer_mask_view_shown(checked); });
  connect(delete_layer_mask_action_, &QAction::triggered, this, [this] { delete_active_layer_mask(); });
  connect(link_layer_mask_action_, &QAction::triggered, this,
          [this](bool checked) { set_active_layer_mask_linked(checked); });
  connect(disable_layer_mask_action_, &QAction::triggered, this,
          [this](bool checked) { set_active_layer_mask_disabled(checked); });
  connect(invert_layer_mask_action_, &QAction::triggered, this, [this] { invert_active_layer_mask(); });
  connect(apply_layer_mask_action_, &QAction::triggered, this, [this] { apply_active_layer_mask(); });
  connect(layer_clipping_mask_action_, &QAction::triggered, this, [this] { toggle_active_layer_clipping(); });
  connect(edit_adjustment_action, &QAction::triggered, this, [this] { edit_active_adjustment_layer(); });
  connect(layer_blending_options_action_, &QAction::triggered, this, [this] { edit_active_layer_style(); });
  connect(layer_copy_style_action_, &QAction::triggered, this, [this] { copy_active_layer_style(); });
  connect(layer_paste_style_action_, &QAction::triggered, this, [this] { paste_layer_style_to_selected_layers(); });
  connect(layer_delete_style_action_, &QAction::triggered, this, [this] { delete_selected_layer_styles(); });
  connect(layer_rasterize_action_, &QAction::triggered, this, [this] { rasterize_active_layers(); });
  connect(layer_rasterize_layer_style_action_, &QAction::triggered, this,
          [this] { rasterize_active_layer_styles(); });
  connect(layer_convert_smart_object_action_, &QAction::triggered, this, [this] { convert_to_smart_object(); });
  connect(layer_smart_object_edit_action_, &QAction::triggered, this, [this] { open_smart_object_contents(); });
  connect(layer_smart_object_replace_action_, &QAction::triggered, this,
          [this] { replace_smart_object_contents(); });
  connect(layer_smart_object_export_action_, &QAction::triggered, this, [this] { export_smart_object_contents(); });
  connect(layer_smart_object_via_copy_action_, &QAction::triggered, this, [this] { new_smart_object_via_copy(); });
  connect(layer_smart_object_to_normal_action_, &QAction::triggered, this, [this] { rasterize_active_layers(); });
  connect(layer_smart_object_update_action_, &QAction::triggered, this, [this] { update_smart_object_content(); });
  connect(layer_smart_object_relink_action_, &QAction::triggered, this,
          [this] { relink_smart_object_contents(); });
  connect(layer_smart_object_embed_action_, &QAction::triggered, this, [this] { embed_linked_smart_object(); });
  connect(duplicate_layer_action, &QAction::triggered, this, [this] { duplicate_active_layer(); });
  connect(merge_visible_action, &QAction::triggered, this, [this] { merge_visible_to_new_layer(); });
  connect(merge_down_action, &QAction::triggered, this, [this] { merge_down(); });
  connect(rename_layer_action, &QAction::triggered, this, [this] { rename_active_layer(); });
  connect(delete_layer_action, &QAction::triggered, this, [this] { delete_active_layer(); });
  connect(fill_layer_action, &QAction::triggered, this, [this] { fill_active_layer(); });
  connect(fill_background_action, &QAction::triggered, this, [this] {
    fill_active_layer_with_color(canvas_->secondary_color(), tr("Fill background"));
  });
  connect(clear_layer_action, &QAction::triggered, this, [this] { clear_active_layer(); });
  connect(flip_h_action, &QAction::triggered, this, [this] { flip_active_layer_horizontal(); });
  connect(flip_v_action, &QAction::triggered, this, [this] { flip_active_layer_vertical(); });
  connect(layer_up_action, &QAction::triggered, this, [this] { move_active_layer(1); });
  connect(layer_down_action, &QAction::triggered, this, [this] { move_active_layer(-1); });
  for (auto* action : {add_layer_action, add_folder_action, new_adjustment_layer_menu->menuAction(),
                       layer_via_copy_action, layer_via_cut_action, add_mask_action, layer_clipping_mask_action_,
                       duplicate_layer_action, merge_visible_action, merge_down_action, rename_layer_action,
                       delete_layer_action, fill_layer_action, fill_background_action, clear_layer_action,
                       flip_h_action, flip_v_action, layer_up_action, layer_down_action}) {
    register_document_action(action);
  }

  auto* image_mode_menu = image_menu->addMenu(tr("&Mode"));
  image_mode_menu->setObjectName(QStringLiteral("imageModeMenu"));
  bind_action_text(image_mode_menu->menuAction(), "&Mode");
  image_mode_rgb_action_ = image_mode_menu->addAction(tr("&RGB Color"));
  image_mode_rgb_action_->setObjectName(QStringLiteral("imageModeRgbAction"));
  image_mode_rgb_action_->setCheckable(true);
  image_mode_rgb_action_->setChecked(true);
  register_hotkey(image_mode_rgb_action_, "image.mode_rgb");
  connect(image_mode_rgb_action_, &QAction::triggered, this, [this] { convert_document_to_rgb(); });
  image_mode_indexed_action_ = image_mode_menu->addAction(tr("&Indexed (Palette)..."));
  image_mode_indexed_action_->setObjectName(QStringLiteral("imageModeIndexedAction"));
  image_mode_indexed_action_->setCheckable(true);
  register_hotkey(image_mode_indexed_action_, "image.mode_indexed");
  connect(image_mode_indexed_action_, &QAction::triggered, this, [this] { convert_document_to_indexed(); });
  auto* image_mode_group = new QActionGroup(this);
  image_mode_group->setExclusive(true);
  image_mode_group->addAction(image_mode_rgb_action_);
  image_mode_group->addAction(image_mode_indexed_action_);
  snap_layer_to_palette_action_ = image_menu->addAction(tr("Snap &Layer to Palette"));
  snap_layer_to_palette_action_->setObjectName(QStringLiteral("imageSnapLayerToPaletteAction"));
  register_hotkey(snap_layer_to_palette_action_, "image.snap_layer_to_palette");
  connect(snap_layer_to_palette_action_, &QAction::triggered, this, [this] { snap_layers_to_palette(true); });
  snap_image_to_palette_action_ = image_menu->addAction(tr("Snap Image to &Palette"));
  snap_image_to_palette_action_->setObjectName(QStringLiteral("imageSnapImageToPaletteAction"));
  register_hotkey(snap_image_to_palette_action_, "image.snap_image_to_palette");
  connect(snap_image_to_palette_action_, &QAction::triggered, this, [this] { snap_layers_to_palette(false); });
  for (auto* action : {image_mode_menu->menuAction(), image_mode_rgb_action_, image_mode_indexed_action_,
                       snap_layer_to_palette_action_, snap_image_to_palette_action_}) {
    register_document_action(action);
  }
  image_menu->addSeparator();

  auto* adjustments_menu = image_menu->addMenu(tr("&Adjustments"));
  adjustments_menu->setObjectName(QStringLiteral("imageAdjustmentsMenu"));
  const auto add_adjustment_action = [this, adjustments_menu](const QString& label, const QString& object_name,
                                                              const QString& identifier,
                                                              const QKeySequence& shortcut = {}) {
    auto* action = adjustments_menu->addAction(label);
    action->setObjectName(object_name);
    action->setIcon(simple_icon(label.left(3).toUpper()));
    register_hotkey(action, identifier, shortcut);
    connect(action, &QAction::triggered, this, [this, identifier] { apply_filter(identifier); });
    register_document_action(action);
    return action;
  };
  add_adjustment_action(tr("&Invert"), QStringLiteral("imageAdjustInvertAction"),
                        QStringLiteral("patchy.filters.invert"), QKeySequence(Qt::CTRL | Qt::Key_I));
  auto* levels_action = adjustments_menu->addAction(tr("&Levels..."));
  levels_action->setObjectName(QStringLiteral("imageAdjustLevelsAction"));
  levels_action->setIcon(simple_icon(QStringLiteral("LVL")));
  register_hotkey(levels_action, "image.levels", QKeySequence(Qt::CTRL | Qt::Key_L));
  connect(levels_action, &QAction::triggered, this, [this] { levels_dialog(); });
  register_document_action(levels_action);
  auto* curves_action = adjustments_menu->addAction(tr("&Curves..."));
  curves_action->setObjectName(QStringLiteral("imageAdjustCurvesAction"));
  curves_action->setIcon(simple_icon(QStringLiteral("CRV")));
  register_hotkey(curves_action, "image.curves", QKeySequence(Qt::CTRL | Qt::Key_M));
  connect(curves_action, &QAction::triggered, this, [this] { curves_dialog(); });
  register_document_action(curves_action);
  auto* hue_saturation_action = adjustments_menu->addAction(tr("&Hue/Saturation..."));
  hue_saturation_action->setObjectName(QStringLiteral("imageAdjustHueSaturationAction"));
  hue_saturation_action->setIcon(simple_icon(QStringLiteral("HSL")));
  register_hotkey(hue_saturation_action, "image.hue_saturation", QKeySequence(Qt::CTRL | Qt::Key_U));
  connect(hue_saturation_action, &QAction::triggered, this, [this] { hue_saturation_dialog(); });
  register_document_action(hue_saturation_action);
  auto* color_balance_action = adjustments_menu->addAction(tr("Color &Balance..."));
  color_balance_action->setObjectName(QStringLiteral("imageAdjustColorBalanceAction"));
  color_balance_action->setIcon(simple_icon(QStringLiteral("CB")));
  register_hotkey(color_balance_action, "image.color_balance", QKeySequence(Qt::CTRL | Qt::Key_B));
  connect(color_balance_action, &QAction::triggered, this, [this] { color_balance_dialog(); });
  register_document_action(color_balance_action);
  add_adjustment_action(tr("&Desaturate"), QStringLiteral("imageAdjustDesaturateAction"),
                        QStringLiteral("patchy.filters.desaturate"),
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U));
  add_adjustment_action(tr("Auto &Contrast"), QStringLiteral("imageAdjustAutoContrastAction"),
                        QStringLiteral("patchy.filters.auto_contrast"),
                        QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_L));
  adjustments_menu->addSeparator();
  add_adjustment_action(tr("&Brightness/Contrast..."), QStringLiteral("imageAdjustBrightnessContrastAction"),
                        QStringLiteral("patchy.filters.brightness_contrast"));
  add_adjustment_action(tr("&Threshold"), QStringLiteral("imageAdjustThresholdAction"),
                        QStringLiteral("patchy.filters.threshold"));
  add_adjustment_action(tr("&Posterize"), QStringLiteral("imageAdjustPosterizeAction"),
                        QStringLiteral("patchy.filters.posterize"));
  image_menu->addSeparator();

  auto* image_size_action = image_menu->addAction(tr("&Image Size..."));
  image_size_action->setObjectName(QStringLiteral("imageSizeAction"));
  auto* canvas_size_action = image_menu->addAction(tr("&Canvas Size..."));
  canvas_size_action->setObjectName(QStringLiteral("imageCanvasSizeAction"));
  auto* crop_action = image_menu->addAction(tr("&Crop to Selection"));
  crop_action->setObjectName(QStringLiteral("imageCropToSelectionAction"));
  image_menu->addSeparator();
  auto* rotate_cw_action = image_menu->addAction(tr("Rotate 90 &Clockwise"));
  auto* rotate_ccw_action = image_menu->addAction(tr("Rotate 90 Counterclockwise"));
  rotate_cw_action->setObjectName(QStringLiteral("imageRotateClockwiseAction"));
  rotate_ccw_action->setObjectName(QStringLiteral("imageRotateCounterclockwiseAction"));
  image_size_action->setIcon(simple_icon(QStringLiteral("IS")));
  canvas_size_action->setIcon(simple_icon(QStringLiteral("CS")));
  crop_action->setIcon(simple_icon(QStringLiteral("crop")));
  rotate_cw_action->setIcon(simple_icon(QStringLiteral("rotate")));
  rotate_ccw_action->setIcon(simple_icon(QStringLiteral("rotate")));
  register_hotkey(image_size_action, "image.size", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_I));
  register_hotkey(canvas_size_action, "image.canvas_size", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_C));
  register_hotkey(crop_action, "image.crop_to_selection", QKeySequence(Qt::Key_C));
  register_hotkey(rotate_cw_action, "image.rotate_cw", QKeySequence(Qt::CTRL | Qt::Key_BracketRight));
  register_hotkey(rotate_ccw_action, "image.rotate_ccw", QKeySequence(Qt::CTRL | Qt::Key_BracketLeft));
  connect(image_size_action, &QAction::triggered, this, [this] { resize_image_dialog(); });
  connect(canvas_size_action, &QAction::triggered, this, [this] { resize_canvas_dialog(); });
  connect(crop_action, &QAction::triggered, this, [this] { crop_to_selection(); });
  connect(rotate_cw_action, &QAction::triggered, this, [this] { rotate_canvas_clockwise(); });
  connect(rotate_ccw_action, &QAction::triggered, this, [this] { rotate_canvas_counterclockwise(); });
  for (auto* action : {adjustments_menu->menuAction(), image_size_action, canvas_size_action, crop_action,
                       rotate_cw_action, rotate_ccw_action}) {
    register_document_action(action);
  }

  filter_convert_smart_filters_action_ =
      filter_menu->addAction(tr("Convert for Smart Filters"));
  filter_convert_smart_filters_action_->setObjectName(
      QStringLiteral("filterConvertForSmartFiltersAction"));
  filter_convert_smart_filters_action_->setProperty(
      "patchy.channelViewBlocked", true);
  filter_convert_smart_filters_action_->setIcon(
      simple_icon(QStringLiteral("SO")));
  filter_convert_smart_filters_action_->setStatusTip(
      tr("Convert the active layer to a Smart Object for editable filters"));
  bind_action_text(filter_convert_smart_filters_action_,
                   "Convert for Smart Filters");
  bind_translated_status_tip(
      filter_convert_smart_filters_action_,
      "Convert the active layer to a Smart Object for editable filters");
  apply_bound_translation(filter_convert_smart_filters_action_);
  refresh_action_tooltip(filter_convert_smart_filters_action_);
  register_hotkey(filter_convert_smart_filters_action_,
                  "filter.convert_for_smart_filters");
  connect(filter_convert_smart_filters_action_, &QAction::triggered, this,
          [this] { convert_for_smart_filters(); });
  register_document_action(filter_convert_smart_filters_action_);
  filter_menu->addSeparator();

  auto* filter_gallery_action = filter_menu->addAction(tr("Filter &Gallery..."));
  filter_gallery_action->setObjectName(QStringLiteral("filterGalleryAction"));
  filter_gallery_action->setProperty("patchy.channelViewBlocked", true);
  filter_gallery_action->setIcon(simple_icon(QStringLiteral("FX")));
  filter_gallery_action->setStatusTip(tr("Preview and apply visual filters and photo looks"));
  bind_action_text(filter_gallery_action, "Filter &Gallery...");
  bind_translated_status_tip(filter_gallery_action, "Preview and apply visual filters and photo looks");
  apply_bound_translation(filter_gallery_action);
  refresh_action_tooltip(filter_gallery_action);
  register_hotkey(filter_gallery_action, "filter.gallery");
  connect(filter_gallery_action, &QAction::triggered, this, [this] { visual_filter_gallery_dialog(); });
  register_document_action(filter_gallery_action);

  auto* liquify_action = filter_menu->addAction(tr("&Liquify..."));
  liquify_action->setObjectName(QStringLiteral("filterLiquifyAction"));
  liquify_action->setProperty("patchy.channelViewBlocked", true);
  liquify_action->setIcon(simple_icon(QStringLiteral("LIQ")));
  liquify_action->setStatusTip(
      tr("Push, pull, twist, pucker, or bloat pixels with a brush"));
  bind_action_text(liquify_action, "&Liquify...");
  bind_translated_status_tip(
      liquify_action,
      "Push, pull, twist, pucker, or bloat pixels with a brush");
  apply_bound_translation(liquify_action);
  refresh_action_tooltip(liquify_action);
  register_hotkey(liquify_action, "filter.liquify",
                  QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_X));
  connect(liquify_action, &QAction::triggered, this,
          [this] { liquify_dialog(); });
  register_document_action(liquify_action);
  filter_menu->addSeparator();

  const auto add_filter_submenu = [this, filter_menu](
                                      const char* object_name,
                                      FilterCategory category) {
    auto* menu = filter_menu->addMenu(filter_category_display_name(category));
    menu->setObjectName(QString::fromLatin1(object_name));
    QPointer<QAction> menu_action(menu->menuAction());
    register_retranslation([menu_action, category] {
      if (menu_action != nullptr) {
        menu_action->setText(filter_category_display_name(category));
      }
    });
    register_document_action(menu->menuAction());
    return menu;
  };
  auto* filter_photo_looks_menu = add_filter_submenu(
      "filterPhotoLooksMenu", FilterCategory::PhotoLooks);
  auto* filter_blur_menu =
      add_filter_submenu("filterBlurMenu", FilterCategory::Blur);
  auto* filter_sharpen_menu =
      add_filter_submenu("filterSharpenMenu", FilterCategory::Sharpen);
  auto* filter_distort_menu =
      add_filter_submenu("filterDistortMenu", FilterCategory::Distort);
  auto* filter_noise_menu =
      add_filter_submenu("filterNoiseMenu", FilterCategory::Noise);
  auto* filter_pixelate_menu =
      add_filter_submenu("filterPixelateMenu", FilterCategory::Pixelate);
  auto* filter_artistic_menu =
      add_filter_submenu("filterArtisticMenu", FilterCategory::Artistic);
  auto* filter_stylize_menu =
      add_filter_submenu("filterStylizeMenu", FilterCategory::Stylize);
  auto* filter_render_menu =
      add_filter_submenu("filterRenderMenu", FilterCategory::Render);
  const auto menu_for_filter = [filter_menu, filter_photo_looks_menu, filter_blur_menu, filter_sharpen_menu,
                                filter_distort_menu, filter_noise_menu, filter_pixelate_menu, filter_stylize_menu,
                                filter_render_menu, filter_artistic_menu](FilterCategory category) {
    switch (category) {
      case FilterCategory::PhotoLooks:
        return filter_photo_looks_menu;
      case FilterCategory::Blur:
        return filter_blur_menu;
      case FilterCategory::Sharpen:
        return filter_sharpen_menu;
      case FilterCategory::Distort:
        return filter_distort_menu;
      case FilterCategory::Noise:
        return filter_noise_menu;
      case FilterCategory::Pixelate:
        return filter_pixelate_menu;
      case FilterCategory::Stylize:
        return filter_stylize_menu;
      case FilterCategory::Render:
        return filter_render_menu;
      case FilterCategory::Artistic:
        return filter_artistic_menu;
      case FilterCategory::Uncategorized:
      case FilterCategory::Adjustment:
        return filter_menu;
    }
    return filter_menu;
  };

  for (const auto& filter : filters_.filters()) {
    const auto identifier = QString::fromStdString(filter.identifier);
    if (is_adjustment_only_filter(filter)) {
      continue;
    }
    const auto display_name = filter_display_name(filter);
    auto* action = menu_for_filter(filter.catalog.category)
                       ->addAction(escape_qaction_ampersands(display_name));
    action->setObjectName(filter_action_object_name(identifier));
    action->setProperty("patchy.channelViewBlocked", true);
    action->setIcon(simple_icon(display_name.left(3).toUpper()));
    action->setStatusTip(tr("Apply %1 to the active layer").arg(display_name));
    refresh_action_tooltip(action);
    QPointer<QAction> filter_action(action);
    register_retranslation([this, filter_action, identifier] {
      if (filter_action == nullptr) {
        return;
      }
      const auto* filter = filters_.find(identifier.toStdString());
      if (filter == nullptr) {
        return;
      }
      const auto display_name = filter_display_name(*filter);
      filter_action->setText(escape_qaction_ampersands(display_name));
      filter_action->setIcon(simple_icon(display_name.left(3).toUpper()));
      filter_action->setStatusTip(tr("Apply %1 to the active layer").arg(display_name));
      refresh_action_tooltip(filter_action);
    });
    connect(action, &QAction::triggered, this, [this, identifier] { apply_filter(identifier); });
    register_document_action(action);
  }

  auto* scan_legacy_plugins_action = plugins_menu->addAction(tr("&Scan Legacy Photoshop Plug-ins..."));
  scan_legacy_plugins_action->setObjectName(QStringLiteral("pluginsScanLegacyAction"));
  scan_legacy_plugins_action->setIcon(simple_icon(QStringLiteral("8BF")));
  connect(scan_legacy_plugins_action, &QAction::triggered, this, [this] { scan_legacy_plugins(); });
#ifndef Q_OS_WIN
  // 8BF plug-ins are Windows binaries (the probe rejects them here with the same
  // message); a disabled note manages expectations up front.
  auto* legacy_windows_only_note = plugins_menu->addAction(tr("Legacy 8BF plug-ins run on Windows only"));
  legacy_windows_only_note->setObjectName(QStringLiteral("pluginsLegacyWindowsOnlyNote"));
  legacy_windows_only_note->setEnabled(false);
  bind_action_text(legacy_windows_only_note, "Legacy 8BF plug-ins run on Windows only");
#endif
  legacy_plugins_menu_ = plugins_menu->addMenu(tr("Legacy Photoshop Plug-ins"));
  legacy_plugins_menu_->setObjectName(QStringLiteral("legacyPluginsMenu"));

  auto* zoom_in = view_menu->addAction(tr("Zoom &In"));
  auto* zoom_out = view_menu->addAction(tr("Zoom &Out"));
  auto* fit_on_screen = view_menu->addAction(tr("&Fit on Screen"));
  auto* zoom_reset = view_menu->addAction(tr("&Actual Pixels"));
  auto* selection_edges_action = view_menu->addAction(tr("Show Selection &Edges"));
  view_menu->addSeparator();
  view_rulers_action_ = view_menu->addAction(tr("&Rulers"));
  view_grid_action_ = view_menu->addAction(tr("&Grid"));
  view_guides_action_ = view_menu->addAction(tr("&Guides"));
  view_snap_action_ = view_menu->addAction(tr("&Snap"));
  view_lock_guides_action_ = view_menu->addAction(tr("Lock Guides"));
  auto* snap_to_menu = view_menu->addMenu(tr("Snap &To"));
  view_snap_guides_action_ = snap_to_menu->addAction(tr("Guides"));
  view_snap_grid_action_ = snap_to_menu->addAction(tr("Grid"));
  view_snap_document_action_ = snap_to_menu->addAction(tr("Document Bounds and Center"));
  view_snap_layers_action_ = snap_to_menu->addAction(tr("Layer Bounds and Centers"));
  view_snap_selection_action_ = snap_to_menu->addAction(tr("Selection Bounds and Center"));
  auto* guides_menu = view_menu->addMenu(tr("Guide Operations"));
  auto* new_guide_action = guides_menu->addAction(tr("New Guide..."));
  auto* new_guide_layout_action = guides_menu->addAction(tr("New Guide Layout..."));
  auto* clear_selected_guides_action = guides_menu->addAction(tr("Clear Selected Guides"));
  auto* clear_guides_action = guides_menu->addAction(tr("Clear Guides"));
  view_menu->addSeparator();
  auto* tile_preview_action = view_menu->addAction(tr("Seamless &Tile Preview"));
  tile_preview_action->setObjectName(QStringLiteral("viewTilePreviewAction"));
  tile_preview_action->setCheckable(true);
  register_hotkey(tile_preview_action, "view.tile_preview");
  connect(tile_preview_action, &QAction::toggled, this,
          [this, tile_preview_action](bool checked) { set_tile_preview_visible(checked, tile_preview_action); });
  zoom_in->setObjectName(QStringLiteral("viewZoomInAction"));
  zoom_out->setObjectName(QStringLiteral("viewZoomOutAction"));
  fit_on_screen->setObjectName(QStringLiteral("viewFitOnScreenAction"));
  zoom_reset->setObjectName(QStringLiteral("viewActualPixelsAction"));
  selection_edges_action->setObjectName(QStringLiteral("viewToggleSelectionEdgesAction"));
  view_rulers_action_->setObjectName(QStringLiteral("viewToggleRulersAction"));
  view_grid_action_->setObjectName(QStringLiteral("viewToggleGridAction"));
  view_guides_action_->setObjectName(QStringLiteral("viewToggleGuidesAction"));
  view_snap_action_->setObjectName(QStringLiteral("viewToggleSnapAction"));
  view_lock_guides_action_->setObjectName(QStringLiteral("viewLockGuidesAction"));
  snap_to_menu->setObjectName(QStringLiteral("viewSnapToMenu"));
  guides_menu->setObjectName(QStringLiteral("viewGuideOperationsMenu"));
  view_snap_guides_action_->setObjectName(QStringLiteral("viewSnapToGuidesAction"));
  view_snap_grid_action_->setObjectName(QStringLiteral("viewSnapToGridAction"));
  view_snap_document_action_->setObjectName(QStringLiteral("viewSnapToDocumentAction"));
  view_snap_layers_action_->setObjectName(QStringLiteral("viewSnapToLayersAction"));
  view_snap_selection_action_->setObjectName(QStringLiteral("viewSnapToSelectionAction"));
  new_guide_action->setObjectName(QStringLiteral("viewNewGuideAction"));
  new_guide_layout_action->setObjectName(QStringLiteral("viewNewGuideLayoutAction"));
  clear_selected_guides_action->setObjectName(QStringLiteral("viewClearSelectedGuidesAction"));
  clear_guides_action->setObjectName(QStringLiteral("viewClearGuidesAction"));
  zoom_in->setIcon(simple_icon(QStringLiteral("zoomIn")));
  zoom_out->setIcon(simple_icon(QStringLiteral("zoomOut")));
  fit_on_screen->setIcon(simple_icon(QStringLiteral("fit")));
  zoom_reset->setIcon(simple_icon(QStringLiteral("1x")));
  selection_edges_action->setIcon(simple_icon(QStringLiteral("SE")));
  view_rulers_action_->setIcon(simple_icon(QStringLiteral("RU")));
  view_grid_action_->setIcon(simple_icon(QStringLiteral("GRD")));
  view_guides_action_->setIcon(simple_icon(QStringLiteral("GDE")));
  view_snap_action_->setIcon(simple_icon(QStringLiteral("SN")));
  view_lock_guides_action_->setIcon(simple_icon(QStringLiteral("LK")));
  new_guide_action->setIcon(simple_icon(QStringLiteral("NG")));
  new_guide_layout_action->setIcon(simple_icon(QStringLiteral("NGL")));
  clear_selected_guides_action->setIcon(simple_icon(QStringLiteral("CSG")));
  clear_guides_action->setIcon(simple_icon(QStringLiteral("CG")));
  view_rulers_action_->setCheckable(true);
  view_grid_action_->setCheckable(true);
  view_guides_action_->setCheckable(true);
  view_snap_action_->setCheckable(true);
  view_lock_guides_action_->setCheckable(true);
  view_snap_guides_action_->setCheckable(true);
  view_snap_grid_action_->setCheckable(true);
  view_snap_document_action_->setCheckable(true);
  view_snap_layers_action_->setCheckable(true);
  view_snap_selection_action_->setCheckable(true);
  view_rulers_action_->setChecked(view_rulers_visible_);
  view_grid_action_->setChecked(view_grid_visible_);
  view_guides_action_->setChecked(view_guides_visible_);
  view_snap_action_->setChecked(view_snap_enabled_);
  view_lock_guides_action_->setChecked(view_guides_locked_);
  view_snap_guides_action_->setChecked(view_snap_to_guides_);
  view_snap_grid_action_->setChecked(view_snap_to_grid_);
  view_snap_document_action_->setChecked(view_snap_to_document_);
  view_snap_layers_action_->setChecked(view_snap_to_layers_);
  view_snap_selection_action_->setChecked(view_snap_to_selection_);
  auto zoom_in_defaults = QKeySequence::keyBindings(QKeySequence::ZoomIn);
  if (!zoom_in_defaults.contains(QKeySequence(Qt::CTRL | Qt::Key_Equal))) {
    zoom_in_defaults << QKeySequence(Qt::CTRL | Qt::Key_Equal);
  }
  register_hotkey(zoom_in, "view.zoom_in", zoom_in_defaults);
  register_hotkey(zoom_out, "view.zoom_out", QKeySequence::keyBindings(QKeySequence::ZoomOut));
  register_hotkey(fit_on_screen, "view.fit_on_screen", QKeySequence(Qt::CTRL | Qt::Key_0));
  register_hotkey(zoom_reset, "view.actual_pixels", QKeySequence(Qt::CTRL | Qt::Key_1));
  register_hotkey(selection_edges_action, "view.selection_edges", QKeySequence(Qt::CTRL | Qt::Key_H));
  register_hotkey(view_rulers_action_, "view.rulers", QKeySequence(Qt::CTRL | Qt::Key_R));
  register_hotkey(view_grid_action_, "view.grid", QKeySequence(Qt::CTRL | Qt::Key_Apostrophe));
  register_hotkey(view_guides_action_, "view.guides", QKeySequence(Qt::CTRL | Qt::Key_Semicolon));
  register_hotkey(view_snap_action_, "view.snap", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Semicolon));
  register_hotkey(view_lock_guides_action_, "view.lock_guides", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Semicolon));
  register_hotkey(new_guide_action, "view.new_guide");
  register_hotkey(new_guide_layout_action, "view.new_guide_layout");
  register_hotkey(clear_selected_guides_action, "view.clear_selected_guides");
  register_hotkey(clear_guides_action, "view.clear_guides");
  connect(zoom_in, &QAction::triggered, this, [this] { canvas_->set_zoom_centered(canvas_->zoom() * 1.25); });
  connect(zoom_out, &QAction::triggered, this, [this] { canvas_->set_zoom_centered(canvas_->zoom() * 0.8); });
  connect(fit_on_screen, &QAction::triggered, this, [this] { canvas_->fit_to_view(); });
  connect(zoom_reset, &QAction::triggered, this, [this] { canvas_->set_zoom_centered(1.0); });
  connect(selection_edges_action, &QAction::triggered, this, [this] {
    if (canvas_ != nullptr) {
      canvas_->toggle_selection_edges_visible();
    }
  });
  const auto apply_view_settings = [this] {
    for (const auto& active_session : sessions_) {
      apply_canvas_aid_settings(active_session->canvas);
    }
    save_view_settings();
  };
  connect(view_rulers_action_, &QAction::toggled, this, [this, apply_view_settings](bool checked) {
    view_rulers_visible_ = checked;
    apply_view_settings();
  });
  connect(view_grid_action_, &QAction::toggled, this, [this, apply_view_settings](bool checked) {
    view_grid_visible_ = checked;
    apply_view_settings();
  });
  connect(view_guides_action_, &QAction::toggled, this, [this, apply_view_settings](bool checked) {
    view_guides_visible_ = checked;
    apply_view_settings();
  });
  connect(view_snap_action_, &QAction::toggled, this, [this, apply_view_settings](bool checked) {
    view_snap_enabled_ = checked;
    apply_view_settings();
  });
  connect(view_lock_guides_action_, &QAction::toggled, this, [this, apply_view_settings](bool checked) {
    view_guides_locked_ = checked;
    apply_view_settings();
  });
  connect(view_snap_guides_action_, &QAction::toggled, this, [this, apply_view_settings](bool checked) {
    view_snap_to_guides_ = checked;
    apply_view_settings();
  });
  connect(view_snap_grid_action_, &QAction::toggled, this, [this, apply_view_settings](bool checked) {
    view_snap_to_grid_ = checked;
    apply_view_settings();
  });
  connect(view_snap_document_action_, &QAction::toggled, this, [this, apply_view_settings](bool checked) {
    view_snap_to_document_ = checked;
    apply_view_settings();
  });
  connect(view_snap_layers_action_, &QAction::toggled, this, [this, apply_view_settings](bool checked) {
    view_snap_to_layers_ = checked;
    apply_view_settings();
  });
  connect(view_snap_selection_action_, &QAction::toggled, this, [this, apply_view_settings](bool checked) {
    view_snap_to_selection_ = checked;
    apply_view_settings();
  });
  connect(new_guide_action, &QAction::triggered, this, [this] { new_guide_dialog(); });
  connect(new_guide_layout_action, &QAction::triggered, this, [this] { new_guide_layout_dialog(); });
  connect(clear_selected_guides_action, &QAction::triggered, this, [this] { clear_selected_guides(); });
  connect(clear_guides_action, &QAction::triggered, this, [this] { clear_guides(); });
  for (auto* action : {zoom_in, zoom_out, fit_on_screen, zoom_reset, selection_edges_action, view_rulers_action_,
                       view_grid_action_, view_guides_action_, view_snap_action_, view_lock_guides_action_,
                       snap_to_menu->menuAction(), view_snap_guides_action_, view_snap_grid_action_,
                       view_snap_document_action_, view_snap_layers_action_, view_snap_selection_action_,
                       guides_menu->menuAction(), new_guide_action, new_guide_layout_action,
                       clear_selected_guides_action, clear_guides_action}) {
    register_document_action(action);
  }

  auto* language_group = new QActionGroup(this);
  language_group->setExclusive(true);
  language_english_action_ = new QAction(tr("&English"), this);
  language_japanese_action_ = new QAction(QStringLiteral("日本語"), this);
  language_english_action_->setObjectName(QStringLiteral("preferencesLanguageEnglishAction"));
  language_japanese_action_->setObjectName(QStringLiteral("preferencesLanguageJapaneseAction"));
  language_english_action_->setCheckable(true);
  language_japanese_action_->setCheckable(true);
  language_group->addAction(language_english_action_);
  language_group->addAction(language_japanese_action_);
  bind_action_text(language_english_action_, "&English");
  connect(language_english_action_, &QAction::triggered, this, [this] {
    LocalizationManager::instance().set_language(QStringLiteral("en"));
    refresh_language_actions();
  });
  connect(language_japanese_action_, &QAction::triggered, this, [this] {
    LocalizationManager::instance().set_language(QStringLiteral("ja"));
    refresh_language_actions();
  });
  refresh_language_actions();

  float_document_action_ = window_menu->addAction(tr("Float in &Window"));
  float_document_action_->setObjectName(QStringLiteral("windowFloatDocumentAction"));
  bind_action_text(float_document_action_, "Float in &Window");
  register_hotkey(float_document_action_, "window.float_document", QKeySequence());
  connect(float_document_action_, &QAction::triggered, this, [this] { float_active_document(); });
  register_document_action(float_document_action_);

  dock_document_action_ = window_menu->addAction(tr("&Dock to Tabs"));
  dock_document_action_->setObjectName(QStringLiteral("windowDockDocumentAction"));
  bind_action_text(dock_document_action_, "&Dock to Tabs");
  register_hotkey(dock_document_action_, "window.dock_document", QKeySequence());
  connect(dock_document_action_, &QAction::triggered, this, [this] { dock_active_document(); });
  register_document_action(dock_document_action_);

  float_all_action_ = window_menu->addAction(tr("Float A&ll in Windows"));
  float_all_action_->setObjectName(QStringLiteral("windowFloatAllAction"));
  bind_action_text(float_all_action_, "Float A&ll in Windows");
  register_hotkey(float_all_action_, "window.float_all", QKeySequence());
  connect(float_all_action_, &QAction::triggered, this, [this] { float_all_documents(); });
  register_document_action(float_all_action_);

  consolidate_tabs_action_ = window_menu->addAction(tr("&Consolidate All to Tabs"));
  consolidate_tabs_action_->setObjectName(QStringLiteral("windowConsolidateTabsAction"));
  bind_action_text(consolidate_tabs_action_, "&Consolidate All to Tabs");
  register_hotkey(consolidate_tabs_action_, "window.consolidate_all_to_tabs", QKeySequence());
  connect(consolidate_tabs_action_, &QAction::triggered, this, [this] { consolidate_all_to_tabs(); });
  register_document_action(consolidate_tabs_action_);

  window_menu->addSeparator();

  tile_windows_action_ = window_menu->addAction(tr("&Tile"));
  tile_windows_action_->setObjectName(QStringLiteral("windowTileAction"));
  bind_action_text(tile_windows_action_, "&Tile");
  register_hotkey(tile_windows_action_, "window.tile_windows", QKeySequence());
  connect(tile_windows_action_, &QAction::triggered, this, [this] { tile_float_windows(); });
  register_document_action(tile_windows_action_);

  cascade_windows_action_ = window_menu->addAction(tr("Ca&scade"));
  cascade_windows_action_->setObjectName(QStringLiteral("windowCascadeAction"));
  bind_action_text(cascade_windows_action_, "Ca&scade");
  register_hotkey(cascade_windows_action_, "window.cascade_windows", QKeySequence());
  connect(cascade_windows_action_, &QAction::triggered, this, [this] { cascade_float_windows(); });
  register_document_action(cascade_windows_action_);

  window_menu->addSeparator();

  auto* screen_size_menu = window_menu->addMenu(tr("Set Screen Size"));
  screen_size_menu->setObjectName(QStringLiteral("windowSetScreenSizeMenu"));
  struct ScreenSizePreset {
    int width;
    int height;
    const char* label;
  };
  static constexpr ScreenSizePreset kScreenSizePresets[] = {
      {1280, 720, "1280 x 720 (HD)"},     {1366, 768, "1366 x 768"},
      {1600, 900, "1600 x 900"},          {1920, 1080, "1920 x 1080 (Full HD)"},
      {2560, 1440, "2560 x 1440 (QHD)"},  {3840, 2160, "3840 x 2160 (4K UHD)"},
  };
  for (const auto& preset : kScreenSizePresets) {
    auto* action = screen_size_menu->addAction(QString());
    action->setObjectName(QStringLiteral("windowSetScreenSize%1x%2Action").arg(preset.width).arg(preset.height));
    bind_action_text(action, preset.label);
    connect(action, &QAction::triggered, this,
            [this, preset] { set_window_screen_size(QSize(preset.width, preset.height)); });
  }

  auto* force_refresh_action = window_menu->addAction(tr("Force Refresh"));
  force_refresh_action->setObjectName(QStringLiteral("windowForceRefreshAction"));
  force_refresh_action->setIcon(simple_icon(QStringLiteral("RF")));
  register_hotkey(force_refresh_action, "window.force_refresh", QKeySequence(Qt::Key_F5));
  connect(force_refresh_action, &QAction::triggered, this, [this] {
    if (canvas_ != nullptr) {
      canvas_->force_refresh();
    }
  });
  register_document_action(force_refresh_action);

  auto* about_action = help_menu->addAction(tr("&About Patchy"));
  about_action->setMenuRole(QAction::AboutRole);
  connect(about_action, &QAction::triggered, this, [this] { show_about(); });

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

  auto* shape_menu = new QMenu(tr("Shape Tools"), tool_palette);
  shape_menu->setObjectName(QStringLiteral("shapeToolMenu"));
  bind_widget_text(shape_menu, "Shape Tools");
  auto* line_tool_action =
      create_flyout_tool_action(shape_menu, tr("Line"), CanvasTool::Line, QKeySequence());  // Ctrl+Shift+U belongs to Desaturate
  auto* rect_tool_action = create_flyout_tool_action(shape_menu, tr("Rect"), CanvasTool::Rectangle, QKeySequence(Qt::Key_U));
  auto* ellipse_tool_action =
      create_flyout_tool_action(shape_menu, tr("Ellipse"), CanvasTool::Ellipse, QKeySequence(Qt::SHIFT | Qt::Key_U));
  auto* shape_tool_button = new QToolButton(tool_palette);
  shape_tool_button->setObjectName(QStringLiteral("shapeToolButton"));
  configure_tool_flyout(tool_palette, shape_menu, shape_tool_button, rect_tool_action,
                        {line_tool_action, rect_tool_action, ellipse_tool_action});
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
  type_menu->addAction(type_tool_action_);

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

  auto* toolbar = new QToolBar(tr("Options"), this);
  toolbar->setObjectName(QStringLiteral("Options"));
  toolbar->setMovable(false);
  toolbar->setFloatable(false);
  toolbar->setAllowedAreas(Qt::TopToolBarArea);
  toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
  toolbar->setIconSize(QSize(18, 18));
  addToolBar(Qt::TopToolBarArea, toolbar);

  // Host the tool options in a wrapping flow layout so they fold onto a second
  // row when the window is too narrow, instead of being clipped off the edge.
  auto* options_content = new OptionsFlowContainer(toolbar);
  options_content->setObjectName(QStringLiteral("OptionsContent"));
  options_content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  auto* options_flow = new FlowLayout(options_content, 5, 4);
  options_flow->setContentsMargins(0, 3, 0, 3);
  options_content->setLayout(options_flow);
  toolbar->addWidget(options_content);
  options_flow_container_ = options_content;

  option_actions_.clear();
  transform_option_actions_.clear();
  warp_option_actions_.clear();
  transform_session_actions_.clear();
  const auto make_option_separator = [options_content, options_flow]() -> QWidget* {
    auto* line = new QFrame(options_content);
    line->setObjectName(QStringLiteral("optionSeparator"));
    line->setFrameShape(QFrame::VLine);
    line->setFrameShadow(QFrame::Plain);
    line->setFixedHeight(24);
    options_flow->addWidget(line);
    return line;
  };
  const auto add_option_separator = [this, make_option_separator](std::initializer_list<CanvasTool> tools) {
    register_option_action(make_option_separator(), tools);
  };
  const auto add_option_action = [this, options_content, options_flow](const QIcon& icon, const QString& text,
                                                                       std::initializer_list<CanvasTool> tools) {
    auto* action = new QAction(icon, text, this);
    auto* button = new QToolButton(options_content);
    button->setDefaultAction(action);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setIconSize(QSize(18, 18));
    button->setAutoRaise(true);
    // Tagged for the compact Options-bar button style (see the app stylesheet).
    // The default QToolButton min-height + padding makes it the tallest item in
    // the row, which grows the whole Options toolbar when a selection tool is
    // active. The icon keeps its full size; only the padding around it shrinks.
    button->setProperty("optionsBarButton", true);
    options_flow->addWidget(button);
    register_option_action(button, tools);
    return action;
  };
  const auto add_option_widget = [this, options_flow](QWidget* widget, std::initializer_list<CanvasTool> tools) {
    options_flow->addWidget(widget);
    register_option_action(widget, tools);
    return widget;
  };
  const auto add_transform_option_widget = [this, options_flow](QWidget* widget) {
    options_flow->addWidget(widget);
    transform_option_actions_.push_back(widget);
    return widget;
  };
  const auto add_option_label = [options_content, add_option_widget](const QString& text,
                                                                     std::initializer_list<CanvasTool> tools) {
    auto* label = new QLabel(text, options_content);
    label->setProperty("optionLabel", true);
    label->setAlignment(Qt::AlignVCenter);
    return add_option_widget(label, tools);
  };

  move_auto_select_check_ = new CheckGlyphBox(tr("Auto-Select"), toolbar);
  move_auto_select_check_->setObjectName(QStringLiteral("moveAutoSelectCheck"));
  move_auto_select_check_->setToolTip(tr("Automatically select the clicked layer while using Move"));
  move_auto_select_check_->setChecked(canvas_defaults->auto_select_layer());
  add_option_widget(move_auto_select_check_, {CanvasTool::Move});
  connect(move_auto_select_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_auto_select_layer(checked);
    }
  });
  move_show_transform_controls_check_ = new CheckGlyphBox(tr("Show Transform Controls"), toolbar);
  move_show_transform_controls_check_->setObjectName(QStringLiteral("moveShowTransformControlsCheck"));
  move_show_transform_controls_check_->setToolTip(tr("Show transform controls when selecting a layer with Move"));
  move_show_transform_controls_check_->setChecked(canvas_defaults->show_transform_controls());
  add_option_widget(move_show_transform_controls_check_, {CanvasTool::Move});
  connect(move_show_transform_controls_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_show_transform_controls(checked);
    }
  });

  transform_reference_combo_ = new QComboBox(toolbar);
  transform_reference_combo_->setObjectName(QStringLiteral("freeTransformReferenceCombo"));
  transform_reference_combo_->setToolTip(tr("Reference point"));
  transform_reference_combo_->setMinimumWidth(96);
  add_transform_option_widget(transform_reference_combo_);
  register_retranslation([this] {
    if (transform_reference_combo_ == nullptr) {
      return;
    }
    const auto current = transform_reference_combo_->currentData();
    QSignalBlocker blocker(transform_reference_combo_);
    transform_reference_combo_->clear();
    transform_reference_combo_->addItem(tr("Top Left"), static_cast<int>(CanvasAnchor::TopLeft));
    transform_reference_combo_->addItem(tr("Top"), static_cast<int>(CanvasAnchor::Top));
    transform_reference_combo_->addItem(tr("Top Right"), static_cast<int>(CanvasAnchor::TopRight));
    transform_reference_combo_->addItem(tr("Left"), static_cast<int>(CanvasAnchor::Left));
    transform_reference_combo_->addItem(tr("Center"), static_cast<int>(CanvasAnchor::Center));
    transform_reference_combo_->addItem(tr("Right"), static_cast<int>(CanvasAnchor::Right));
    transform_reference_combo_->addItem(tr("Bottom Left"), static_cast<int>(CanvasAnchor::BottomLeft));
    transform_reference_combo_->addItem(tr("Bottom"), static_cast<int>(CanvasAnchor::Bottom));
    transform_reference_combo_->addItem(tr("Bottom Right"), static_cast<int>(CanvasAnchor::BottomRight));
    const auto index = transform_reference_combo_->findData(current.isValid() ? current : QVariant(static_cast<int>(CanvasAnchor::Center)));
    transform_reference_combo_->setCurrentIndex(index >= 0 ? index : transform_reference_combo_->findData(static_cast<int>(CanvasAnchor::Center)));
  });
  connect(transform_reference_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (updating_transform_controls_ || canvas_ == nullptr || transform_reference_combo_ == nullptr || index < 0) {
      return;
    }
    canvas_->set_transform_reference_point(
        static_cast<CanvasAnchor>(transform_reference_combo_->itemData(index).toInt()));
    sync_transform_controls_from_canvas();
  });

  const auto make_transform_label = [toolbar, add_transform_option_widget](const char* source) {
    auto* label = new QLabel(QObject::tr(source), toolbar);
    label->setProperty("optionLabel", true);
    label->setAlignment(Qt::AlignVCenter);
    bind_widget_text(label, source);
    add_transform_option_widget(label);
    return label;
  };
  const auto make_transform_spin = [toolbar, add_transform_option_widget](const QString& object_name,
                                                                          double minimum, double maximum,
                                                                          int decimals, const QString& suffix) {
    auto* spin = new QDoubleSpinBox(toolbar);
    spin->setObjectName(object_name);
    spin->setRange(minimum, maximum);
    spin->setDecimals(decimals);
    spin->setKeyboardTracking(false);
    spin->setSuffix(suffix);
    spin->setMinimumWidth(82);
    configure_dialog_spinbox(spin, 82);
    add_transform_option_widget(spin);
    return spin;
  };

  make_transform_label("X:");
  transform_x_spin_ = make_transform_spin(QStringLiteral("freeTransformXSpin"), -30000.0, 30000.0, 2,
                                          QStringLiteral(" px"));
  transform_x_spin_->setToolTip(tr("Reference X position"));
  make_transform_label("Y:");
  transform_y_spin_ = make_transform_spin(QStringLiteral("freeTransformYSpin"), -30000.0, 30000.0, 2,
                                          QStringLiteral(" px"));
  transform_y_spin_->setToolTip(tr("Reference Y position"));
  make_transform_label("W:");
  transform_scale_x_spin_ = make_transform_spin(QStringLiteral("freeTransformScaleXSpin"), -10000.0, 10000.0, 2,
                                                 QStringLiteral("%"));
  transform_scale_x_spin_->setToolTip(tr("Horizontal scale"));
  transform_link_scale_button_ = new QPushButton(toolbar);
  transform_link_scale_button_->setObjectName(QStringLiteral("freeTransformLinkScaleButton"));
  transform_link_scale_button_->setCheckable(true);
  transform_link_scale_button_->setChecked(true);
  transform_link_scale_button_->setIcon(simple_icon(QStringLiteral("link"), QColor(220, 226, 235)));
  transform_link_scale_button_->setToolTip(tr("Link horizontal and vertical scale"));
  transform_link_scale_button_->setFixedWidth(28);
  add_transform_option_widget(transform_link_scale_button_);
  make_transform_label("H:");
  transform_scale_y_spin_ = make_transform_spin(QStringLiteral("freeTransformScaleYSpin"), -10000.0, 10000.0, 2,
                                                 QStringLiteral("%"));
  transform_scale_y_spin_->setToolTip(tr("Vertical scale"));
  make_transform_label("Angle:");
  transform_rotation_spin_ = make_transform_spin(QStringLiteral("freeTransformRotationSpin"), -3600.0, 3600.0, 2,
                                                 QStringLiteral(" deg"));
  transform_rotation_spin_->setToolTip(tr("Rotation angle"));
  transform_interpolation_combo_ = new QComboBox(toolbar);
  transform_interpolation_combo_->setObjectName(QStringLiteral("freeTransformInterpolationCombo"));
  transform_interpolation_combo_->setToolTip(tr("Interpolation"));
  transform_interpolation_combo_->setMinimumWidth(132);
  add_transform_option_widget(transform_interpolation_combo_);
  register_retranslation([this] {
    if (transform_interpolation_combo_ == nullptr) {
      return;
    }
    const auto current = transform_interpolation_combo_->currentData();
    QSignalBlocker blocker(transform_interpolation_combo_);
    transform_interpolation_combo_->clear();
    transform_interpolation_combo_->addItem(tr("Nearest Neighbor"),
                                            static_cast<int>(CanvasWidget::TransformInterpolation::NearestNeighbor));
    transform_interpolation_combo_->addItem(tr("Bilinear"),
                                            static_cast<int>(CanvasWidget::TransformInterpolation::Bilinear));
    transform_interpolation_combo_->addItem(tr("Bicubic"),
                                            static_cast<int>(CanvasWidget::TransformInterpolation::Bicubic));
    const auto fallback = static_cast<int>(CanvasWidget::TransformInterpolation::Bicubic);
    const auto index = transform_interpolation_combo_->findData(current.isValid() ? current : QVariant(fallback));
    transform_interpolation_combo_->setCurrentIndex(index >= 0 ? index : transform_interpolation_combo_->findData(fallback));
  });
  const auto apply_transform_from_spin = [this] { apply_transform_controls_from_ui(); };
  connect(transform_x_spin_, &QDoubleSpinBox::valueChanged, this, apply_transform_from_spin);
  connect(transform_y_spin_, &QDoubleSpinBox::valueChanged, this, apply_transform_from_spin);
  connect(transform_scale_x_spin_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
    if (!updating_transform_controls_ && transform_link_scale_button_ != nullptr && transform_link_scale_button_->isChecked() &&
        transform_scale_y_spin_ != nullptr) {
      QSignalBlocker blocker(transform_scale_y_spin_);
      transform_scale_y_spin_->setValue(value);
    }
    apply_transform_controls_from_ui();
  });
  connect(transform_scale_y_spin_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
    if (!updating_transform_controls_ && transform_link_scale_button_ != nullptr && transform_link_scale_button_->isChecked() &&
        transform_scale_x_spin_ != nullptr) {
      QSignalBlocker blocker(transform_scale_x_spin_);
      transform_scale_x_spin_->setValue(value);
    }
    apply_transform_controls_from_ui();
  });
  connect(transform_link_scale_button_, &QPushButton::toggled, this, [this](bool checked) {
    if (checked && transform_scale_x_spin_ != nullptr && transform_scale_y_spin_ != nullptr) {
      QSignalBlocker blocker(transform_scale_y_spin_);
      transform_scale_y_spin_->setValue(transform_scale_x_spin_->value());
      apply_transform_controls_from_ui();
    }
  });
  connect(transform_rotation_spin_, &QDoubleSpinBox::valueChanged, this, apply_transform_from_spin);
  connect(transform_interpolation_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (updating_transform_controls_ || canvas_ == nullptr || transform_interpolation_combo_ == nullptr || index < 0) {
      return;
    }
    canvas_->set_transform_interpolation(
        static_cast<CanvasWidget::TransformInterpolation>(transform_interpolation_combo_->itemData(index).toInt()));
  });
  // Warp Transform options: visible only while the warp cage is active.
  const auto add_warp_option_widget = [this, options_flow](QWidget* widget) {
    options_flow->addWidget(widget);
    warp_option_actions_.push_back(widget);
    return widget;
  };
  {
    auto* label = new QLabel(QObject::tr("Warp:"), toolbar);
    label->setProperty("optionLabel", true);
    label->setAlignment(Qt::AlignVCenter);
    bind_widget_text(label, "Warp:");
    add_warp_option_widget(label);
  }
  warp_style_combo_ = new QComboBox(toolbar);
  warp_style_combo_->setObjectName(QStringLiteral("warpStyleCombo"));
  warp_style_combo_->setToolTip(tr("Warp style"));
  warp_style_combo_->setMinimumWidth(110);
  add_warp_option_widget(warp_style_combo_);
  register_retranslation([this] {
    if (warp_style_combo_ == nullptr) {
      return;
    }
    const auto current = warp_style_combo_->currentData();
    QSignalBlocker blocker(warp_style_combo_);
    warp_style_combo_->clear();
    warp_style_combo_->addItem(tr("Custom"), QStringLiteral("warpCustom"));
    warp_style_combo_->addItem(tr("Arc"), QStringLiteral("warpArc"));
    warp_style_combo_->addItem(tr("Arc Lower"), QStringLiteral("warpArcLower"));
    warp_style_combo_->addItem(tr("Arc Upper"), QStringLiteral("warpArcUpper"));
    warp_style_combo_->addItem(tr("Arch"), QStringLiteral("warpArch"));
    warp_style_combo_->addItem(tr("Bulge"), QStringLiteral("warpBulge"));
    warp_style_combo_->addItem(tr("Shell Lower"), QStringLiteral("warpShellLower"));
    warp_style_combo_->addItem(tr("Shell Upper"), QStringLiteral("warpShellUpper"));
    warp_style_combo_->addItem(tr("Flag"), QStringLiteral("warpFlag"));
    warp_style_combo_->addItem(tr("Wave"), QStringLiteral("warpWave"));
    warp_style_combo_->addItem(tr("Fish"), QStringLiteral("warpFish"));
    warp_style_combo_->addItem(tr("Rise"), QStringLiteral("warpRise"));
    warp_style_combo_->addItem(tr("Fisheye"), QStringLiteral("warpFisheye"));
    warp_style_combo_->addItem(tr("Inflate"), QStringLiteral("warpInflate"));
    warp_style_combo_->addItem(tr("Squeeze"), QStringLiteral("warpSqueeze"));
    warp_style_combo_->addItem(tr("Twist"), QStringLiteral("warpTwist"));
    const auto index = warp_style_combo_->findData(current.isValid() ? current : QVariant(QStringLiteral("warpCustom")));
    warp_style_combo_->setCurrentIndex(std::max(0, index));
  });
  {
    auto* label = new QLabel(QObject::tr("Bend:"), toolbar);
    label->setProperty("optionLabel", true);
    label->setAlignment(Qt::AlignVCenter);
    bind_widget_text(label, "Bend:");
    add_warp_option_widget(label);
  }
  warp_bend_spin_ = new QDoubleSpinBox(toolbar);
  warp_bend_spin_->setObjectName(QStringLiteral("warpBendSpin"));
  warp_bend_spin_->setRange(-100.0, 100.0);
  warp_bend_spin_->setDecimals(0);
  warp_bend_spin_->setKeyboardTracking(false);
  warp_bend_spin_->setSuffix(QStringLiteral("%"));
  warp_bend_spin_->setValue(50.0);
  warp_bend_spin_->setToolTip(tr("Warp bend"));
  configure_dialog_spinbox(warp_bend_spin_, 74);
  add_warp_option_widget(warp_bend_spin_);
  const auto apply_warp_style_from_ui = [this] {
    if (updating_transform_controls_ || canvas_ == nullptr || warp_style_combo_ == nullptr ||
        warp_bend_spin_ == nullptr) {
      return;
    }
    canvas_->apply_warp_style_preset(warp_style_combo_->currentData().toString(), warp_bend_spin_->value());
  };
  connect(warp_style_combo_, &QComboBox::currentIndexChanged, this,
          [apply_warp_style_from_ui](int index) {
            if (index >= 0) {
              apply_warp_style_from_ui();
            }
          });
  connect(warp_bend_spin_, &QDoubleSpinBox::valueChanged, this,
          [apply_warp_style_from_ui](double) { apply_warp_style_from_ui(); });

  // Shared session trio, laid out after both control sets so it closes the row in
  // either mode (Photoshop's options-bar order: mode toggle, then cancel/commit).
  // Apply/cancel dispatch on whichever session is active.
  const auto add_session_option_widget = [this, options_flow](QWidget* widget) {
    options_flow->addWidget(widget);
    transform_session_actions_.push_back(widget);
    return widget;
  };
  transform_warp_mode_button_ = new QPushButton(toolbar);
  transform_warp_mode_button_->setObjectName(QStringLiteral("transformWarpModeButton"));
  transform_warp_mode_button_->setCheckable(true);
  transform_warp_mode_button_->setIcon(simple_icon(QStringLiteral("warp"), QColor(220, 226, 235)));
  transform_warp_mode_button_->setToolTip(tr("Switch between free transform and warp"));
  transform_warp_mode_button_->setFixedWidth(30);
  // Session buttons render their icons at 20px (the QPushButton default of 16px
  // reads tiny on the bar); optionsSessionButton relaxes the QSS side padding so
  // the larger icon is not clipped.
  transform_warp_mode_button_->setIconSize(QSize(20, 20));
  transform_warp_mode_button_->setProperty("optionsSessionButton", true);
  add_session_option_widget(transform_warp_mode_button_);
  transform_apply_button_ = new QPushButton(toolbar);
  transform_apply_button_->setObjectName(QStringLiteral("freeTransformApplyButton"));
  transform_apply_button_->setIcon(simple_icon(QStringLiteral("ok"), QColor(160, 220, 165)));
  transform_apply_button_->setToolTip(tr("Apply transform"));
  transform_apply_button_->setFixedWidth(30);
  transform_apply_button_->setIconSize(QSize(20, 20));
  transform_apply_button_->setProperty("optionsSessionButton", true);
  add_session_option_widget(transform_apply_button_);
  transform_cancel_button_ = new QPushButton(toolbar);
  transform_cancel_button_->setObjectName(QStringLiteral("freeTransformCancelButton"));
  transform_cancel_button_->setIcon(simple_icon(QStringLiteral("clear"), QColor(255, 150, 150)));
  transform_cancel_button_->setToolTip(tr("Cancel transform"));
  transform_cancel_button_->setFixedWidth(30);
  transform_cancel_button_->setIconSize(QSize(20, 20));
  transform_cancel_button_->setProperty("optionsSessionButton", true);
  add_session_option_widget(transform_cancel_button_);
  connect(transform_warp_mode_button_, &QPushButton::clicked, this, [this] {
    if (canvas_ == nullptr) {
      return;
    }
    if (canvas_->warp_transform_active()) {
      canvas_->switch_warp_to_free_transform();
    } else if (canvas_->free_transform_active()) {
      canvas_->begin_warp_transform();  // refusal reasons land in the status bar
    }
    // Re-sync the checked state (a refused switch leaves the mode unchanged).
    refresh_options_bar();
  });
  connect(transform_apply_button_, &QPushButton::clicked, this, [this] {
    if (canvas_ == nullptr) {
      return;
    }
    if (canvas_->warp_transform_active()) {
      canvas_->finish_warp_transform();
    } else {
      canvas_->finish_free_transform();
    }
  });
  connect(transform_cancel_button_, &QPushButton::clicked, this, [this] {
    if (canvas_ == nullptr) {
      return;
    }
    if (canvas_->warp_transform_active()) {
      canvas_->cancel_warp_transform();
    } else {
      canvas_->cancel_free_transform();
    }
  });

  auto* selection_new = add_option_action(
      simple_icon(QStringLiteral("N")), tr("New Selection"),
      {CanvasTool::Marquee, CanvasTool::EllipticalMarquee, CanvasTool::Lasso, CanvasTool::MagneticLasso,
       CanvasTool::MagicWand, CanvasTool::QuickSelect});
  selection_new->setObjectName(QStringLiteral("selectionNewModeAction"));
  auto* selection_add = add_option_action(
      simple_icon(QStringLiteral("+")), tr("Add to Selection"),
      {CanvasTool::Marquee, CanvasTool::EllipticalMarquee, CanvasTool::Lasso, CanvasTool::MagneticLasso,
       CanvasTool::MagicWand, CanvasTool::QuickSelect});
  selection_add->setObjectName(QStringLiteral("selectionAddModeAction"));
  auto* selection_subtract = add_option_action(
      simple_icon(QStringLiteral("-")), tr("Subtract from Selection"),
      {CanvasTool::Marquee, CanvasTool::EllipticalMarquee, CanvasTool::Lasso, CanvasTool::MagneticLasso,
       CanvasTool::MagicWand, CanvasTool::QuickSelect});
  selection_subtract->setObjectName(QStringLiteral("selectionSubtractModeAction"));
  auto* selection_intersect = add_option_action(simple_icon(QStringLiteral("Ix")), tr("Intersect Selection"),
                                                {CanvasTool::Marquee, CanvasTool::EllipticalMarquee, CanvasTool::Lasso,
                                                 CanvasTool::MagneticLasso, CanvasTool::MagicWand});
  selection_intersect->setObjectName(QStringLiteral("selectionIntersectModeAction"));
  selection_new_mode_action_ = selection_new;
  selection_add_mode_action_ = selection_add;
  selection_subtract_mode_action_ = selection_subtract;
  selection_intersect_mode_action_ = selection_intersect;
  auto* selection_mode_group = new QActionGroup(this);
  selection_mode_group->setExclusive(true);
  const auto configure_selection_mode_action = [selection_mode_group](QAction* action) {
    action->setCheckable(true);
    selection_mode_group->addAction(action);
  };
  configure_selection_mode_action(selection_new);
  configure_selection_mode_action(selection_add);
  configure_selection_mode_action(selection_subtract);
  configure_selection_mode_action(selection_intersect);
  selection_new->setChecked(true);
  const auto set_selection_mode = [this](CanvasWidget::SelectionMode mode) {
    // Each selection tool keeps its own combine mode; store it for the active
    // tool (so new documents inherit it) and apply it to the live canvas.
    if (const auto index = CanvasWidget::selection_tool_index(current_tool_); index >= 0) {
      selection_modes_[static_cast<std::size_t>(index)] = mode;
    }
    if (canvas_ != nullptr) {
      canvas_->set_selection_mode(mode);
    }
    refresh_options_bar();
  };
  connect(selection_new, &QAction::triggered, this,
          [set_selection_mode] { set_selection_mode(CanvasWidget::SelectionMode::Replace); });
  connect(selection_add, &QAction::triggered, this,
          [set_selection_mode] { set_selection_mode(CanvasWidget::SelectionMode::Add); });
  connect(selection_subtract, &QAction::triggered, this,
          [set_selection_mode] { set_selection_mode(CanvasWidget::SelectionMode::Subtract); });
  connect(selection_intersect, &QAction::triggered, this,
          [set_selection_mode] { set_selection_mode(CanvasWidget::SelectionMode::Intersect); });
  add_option_separator({CanvasTool::Marquee, CanvasTool::EllipticalMarquee, CanvasTool::Lasso,
                        CanvasTool::MagneticLasso, CanvasTool::MagicWand, CanvasTool::QuickSelect});

  auto* feather_group = new QWidget(toolbar);
  feather_group->setObjectName(QStringLiteral("selectionFeatherGroup"));
  auto* feather_layout = new QHBoxLayout(feather_group);
  feather_layout->setContentsMargins(0, 0, 0, 0);
  feather_layout->setSpacing(0);
  auto* feather_label = new QLabel(tr("Feather:"), feather_group);
  feather_label->setAlignment(Qt::AlignCenter);
  feather_layout->addWidget(feather_label);
  auto* feather = new QSpinBox(feather_group);
  feather->setObjectName(QStringLiteral("selectionFeatherSpin"));
  feather->setRange(0, 250);
  feather->setSuffix(QStringLiteral(" px"));
  feather->setValue(current_selection_feather_radius_);
  configure_toolbar_spinbox(feather, 64);
  feather_layout->addWidget(feather);
  add_option_widget(feather_group, {CanvasTool::Marquee, CanvasTool::EllipticalMarquee, CanvasTool::Lasso,
                                    CanvasTool::MagneticLasso, CanvasTool::MagicWand, CanvasTool::QuickSelect});
  auto* anti_alias = new CheckGlyphBox(tr("Anti-alias"), toolbar);
  anti_alias->setObjectName(QStringLiteral("selectionAntiAliasCheck"));
  anti_alias->setChecked(current_selection_antialias_);
  add_option_widget(anti_alias,
                    {CanvasTool::Marquee, CanvasTool::EllipticalMarquee, CanvasTool::Lasso,
                     CanvasTool::MagneticLasso, CanvasTool::MagicWand});
  const auto apply_selection_edge_settings = [this, feather, anti_alias] {
    current_selection_feather_radius_ = feather->value();
    current_selection_antialias_ = anti_alias->isChecked();
    if (canvas_ != nullptr) {
      canvas_->set_selection_feather_radius(current_selection_feather_radius_);
      canvas_->set_selection_antialias(current_selection_antialias_);
    }
    refresh_document_info();
  };
  connect(feather, &QSpinBox::valueChanged, this, [apply_selection_edge_settings](int) {
    apply_selection_edge_settings();
  });
  connect(anti_alias, &QCheckBox::toggled, this, [apply_selection_edge_settings](bool) {
    apply_selection_edge_settings();
  });
  add_option_label(tr("Radius:"), {CanvasTool::Marquee});
  auto* marquee_corner_radius = new QSpinBox(toolbar);
  marquee_corner_radius->setObjectName(QStringLiteral("selectionCornerRadiusSpin"));
  marquee_corner_radius->setRange(0, 512);
  marquee_corner_radius->setValue(current_marquee_corner_radius_);
  marquee_corner_radius->setSuffix(QStringLiteral(" px"));
  marquee_corner_radius->setToolTip(tr("Rounded-corner radius for the rectangular marquee (0 = sharp corners)"));
  configure_toolbar_spinbox(marquee_corner_radius, 64);
  add_option_widget(marquee_corner_radius, {CanvasTool::Marquee});
  connect(marquee_corner_radius, &QSpinBox::valueChanged, this, [this](int value) {
    current_marquee_corner_radius_ = value;
    if (canvas_ != nullptr) {
      canvas_->set_marquee_corner_radius(value);
    }
  });
  add_option_label(tr("Style:"), {CanvasTool::Marquee, CanvasTool::EllipticalMarquee});
  auto* style_combo = new QComboBox(toolbar);
  style_combo->setObjectName(QStringLiteral("selectionStyleCombo"));
  style_combo->addItems({tr("Normal"), tr("Fixed Ratio"), tr("Fixed Size")});
  style_combo->setCurrentText(tr("Normal"));
  style_combo->setFixedWidth(92);
  QPointer<QComboBox> selection_style_combo(style_combo);
  register_retranslation([selection_style_combo] {
    if (selection_style_combo == nullptr || selection_style_combo->count() < 3) {
      return;
    }
    QSignalBlocker blocker(selection_style_combo);
    selection_style_combo->setItemText(0, QObject::tr("Normal"));
    selection_style_combo->setItemText(1, QObject::tr("Fixed Ratio"));
    selection_style_combo->setItemText(2, QObject::tr("Fixed Size"));
  });
  add_option_widget(style_combo, {CanvasTool::Marquee, CanvasTool::EllipticalMarquee});
  add_option_label(tr("Width:"), {CanvasTool::Marquee, CanvasTool::EllipticalMarquee});
  auto* fixed_width = new QSpinBox(toolbar);
  fixed_width->setObjectName(QStringLiteral("selectionFixedWidthSpin"));
  fixed_width->setRange(1, 30000);
  fixed_width->setValue(has_active_document() ? document().width() : 1024);
  fixed_width->setSuffix(QStringLiteral(" px"));
  configure_toolbar_spinbox(fixed_width, 78);
  add_option_widget(fixed_width, {CanvasTool::Marquee, CanvasTool::EllipticalMarquee});
  add_option_label(tr("Height:"), {CanvasTool::Marquee, CanvasTool::EllipticalMarquee});
  auto* fixed_height = new QSpinBox(toolbar);
  fixed_height->setObjectName(QStringLiteral("selectionFixedHeightSpin"));
  fixed_height->setRange(1, 30000);
  fixed_height->setValue(has_active_document() ? document().height() : 768);
  fixed_height->setSuffix(QStringLiteral(" px"));
  configure_toolbar_spinbox(fixed_height, 78);
  add_option_widget(fixed_height, {CanvasTool::Marquee, CanvasTool::EllipticalMarquee});
  const auto apply_marquee_settings = [this, style_combo, fixed_width, fixed_height] {
    switch (style_combo->currentIndex()) {
      case 1:
        current_marquee_style_ = CanvasWidget::MarqueeStyle::FixedRatio;
        break;
      case 2:
        current_marquee_style_ = CanvasWidget::MarqueeStyle::FixedSize;
        break;
      default:
        current_marquee_style_ = CanvasWidget::MarqueeStyle::Normal;
        break;
    }
    current_marquee_width_ = fixed_width->value();
    current_marquee_height_ = fixed_height->value();
    if (canvas_ != nullptr) {
      canvas_->set_marquee_style(current_marquee_style_);
      canvas_->set_marquee_fixed_size(current_marquee_width_, current_marquee_height_);
    }
  };
  connect(style_combo, &QComboBox::currentIndexChanged, this, [apply_marquee_settings](int) {
    apply_marquee_settings();
  });
  connect(fixed_width, &QSpinBox::valueChanged, this, [apply_marquee_settings](int) {
    apply_marquee_settings();
  });
  connect(fixed_height, &QSpinBox::valueChanged, this, [apply_marquee_settings](int) {
    apply_marquee_settings();
  });
  add_option_separator({CanvasTool::Marquee, CanvasTool::EllipticalMarquee, CanvasTool::Lasso,
                        CanvasTool::MagneticLasso, CanvasTool::MagicWand});

  add_option_label(tr("Preset:"),
                   {CanvasTool::Brush, CanvasTool::Clone, CanvasTool::Healing, CanvasTool::Smudge,
                    CanvasTool::Eraser});
  brush_preset_combo_ = new QComboBox(toolbar);
  brush_preset_combo_->setObjectName(QStringLiteral("brushPresetCombo"));
  brush_preset_combo_->setMinimumWidth(132);
  for (const auto& preset : builtin_brush_presets()) {
    brush_preset_combo_->addItem(brush_preset_display_name(preset), preset.id);
  }
  {
    const auto preset_index = brush_preset_combo_->findData(default_startup_brush_preset_id());
    if (preset_index >= 0) {
      brush_preset_combo_->setCurrentIndex(preset_index);
    }
  }
  add_option_widget(
      brush_preset_combo_,
      {CanvasTool::Brush, CanvasTool::Clone, CanvasTool::Healing, CanvasTool::Smudge,
       CanvasTool::Eraser});

  // The raster brush controls double as the shape tools' Pixels-mode options;
  // refresh_vector_tool_options_visibility hides them in the vector modes.
  vector_pixel_only_option_widgets_.push_back(add_option_label(
      tr("Size:"),
      {CanvasTool::Brush, CanvasTool::MixerBrush, CanvasTool::PatternStamp, CanvasTool::Clone, CanvasTool::Healing, CanvasTool::Smudge,
       CanvasTool::Dodge, CanvasTool::Burn, CanvasTool::Sponge,
       CanvasTool::BlurBrush, CanvasTool::SharpenBrush,
       CanvasTool::Eraser, CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse}));
  auto* brush_size = new QSpinBox(toolbar);
  brush_size->setObjectName(QStringLiteral("brushSizeSpin"));
  brush_size->setRange(1, kMaxBrushSize);
  brush_size->setValue(canvas_defaults->brush_size());
  configure_toolbar_spinbox(brush_size, 58);
  add_option_widget(brush_size,
                    {CanvasTool::Brush, CanvasTool::MixerBrush, CanvasTool::PatternStamp, CanvasTool::Clone, CanvasTool::Healing, CanvasTool::Smudge,
                     CanvasTool::Dodge, CanvasTool::Burn, CanvasTool::Sponge,
                     CanvasTool::BlurBrush, CanvasTool::SharpenBrush,
                     CanvasTool::Eraser, CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse});
  auto* brush_size_slider = new QSlider(Qt::Horizontal, toolbar);
  brush_size_slider->setObjectName(QStringLiteral("brushSizeSlider"));
  brush_size_slider->setRange(1, kMaxBrushSize);
  brush_size_slider->setValue(canvas_defaults->brush_size());
  brush_size_slider->setFixedWidth(150);
  brush_size_slider->setToolTip(tr("Brush size — press [ or ], or Alt+Right-drag on the canvas"));
  add_option_widget(brush_size_slider,
                    {CanvasTool::Brush, CanvasTool::Clone, CanvasTool::Healing, CanvasTool::Smudge,
                     CanvasTool::Dodge, CanvasTool::Burn, CanvasTool::Sponge,
                     CanvasTool::BlurBrush, CanvasTool::SharpenBrush,
                     CanvasTool::Eraser, CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse});
  vector_pixel_only_option_widgets_.push_back(add_option_label(
      tr("Opacity:"),
      {CanvasTool::Brush, CanvasTool::PatternStamp, CanvasTool::Clone, CanvasTool::Healing, CanvasTool::Smudge,
       CanvasTool::Eraser, CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse}));
  auto* brush_opacity = new QSpinBox(toolbar);
  brush_opacity->setObjectName(QStringLiteral("brushOpacitySpin"));
  brush_opacity->setRange(1, 100);
  brush_opacity->setValue(canvas_defaults->brush_opacity());
  brush_opacity->setSuffix(QStringLiteral("%"));
  configure_toolbar_spinbox(brush_opacity, 52);
  add_option_widget(brush_opacity,
                    {CanvasTool::Brush, CanvasTool::PatternStamp, CanvasTool::Clone, CanvasTool::Healing, CanvasTool::Smudge,
                     CanvasTool::Eraser, CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse});
  auto* brush_opacity_slider = new QSlider(Qt::Horizontal, toolbar);
  brush_opacity_slider->setObjectName(QStringLiteral("brushOpacitySlider"));
  brush_opacity_slider->setRange(1, 100);
  brush_opacity_slider->setValue(canvas_defaults->brush_opacity());
  brush_opacity_slider->setFixedWidth(120);
  brush_opacity_slider->setToolTip(tr("Brush opacity — press number keys (5 = 50%, 0 = 100%)"));
  add_option_widget(brush_opacity_slider,
                    {CanvasTool::Brush, CanvasTool::Clone, CanvasTool::Healing, CanvasTool::Smudge,
                     CanvasTool::Eraser, CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse});
  vector_pixel_only_option_widgets_.push_back(add_option_label(
      tr("Soft:"),
      {CanvasTool::Brush, CanvasTool::MixerBrush, CanvasTool::PatternStamp, CanvasTool::Clone, CanvasTool::Healing, CanvasTool::Smudge,
       CanvasTool::Dodge, CanvasTool::Burn, CanvasTool::Sponge,
       CanvasTool::BlurBrush, CanvasTool::SharpenBrush,
       CanvasTool::Eraser, CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse}));
  auto* brush_softness = new QSpinBox(toolbar);
  brush_softness->setObjectName(QStringLiteral("brushSoftnessSpin"));
  brush_softness->setRange(0, 100);
  brush_softness->setValue(canvas_defaults->brush_softness());
  brush_softness->setSuffix(QStringLiteral("%"));
  configure_toolbar_spinbox(brush_softness, 52);
  add_option_widget(brush_softness,
                    {CanvasTool::Brush, CanvasTool::MixerBrush, CanvasTool::PatternStamp, CanvasTool::Clone, CanvasTool::Healing, CanvasTool::Smudge,
                     CanvasTool::Dodge, CanvasTool::Burn, CanvasTool::Sponge,
                     CanvasTool::BlurBrush, CanvasTool::SharpenBrush,
                     CanvasTool::Eraser, CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse});
  auto* brush_softness_slider = new QSlider(Qt::Horizontal, toolbar);
  brush_softness_slider->setObjectName(QStringLiteral("brushSoftnessSlider"));
  brush_softness_slider->setRange(0, 100);
  brush_softness_slider->setValue(canvas_defaults->brush_softness());
  brush_softness_slider->setFixedWidth(110);
  brush_softness_slider->setToolTip(tr("Brush edge softness — Alt+Right-drag up or down on the canvas"));
  add_option_widget(brush_softness_slider,
                    {CanvasTool::Brush, CanvasTool::Clone, CanvasTool::Healing, CanvasTool::Smudge,
                     CanvasTool::Dodge, CanvasTool::Burn, CanvasTool::Sponge,
                     CanvasTool::BlurBrush, CanvasTool::SharpenBrush,
                     CanvasTool::Eraser, CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse});
  for (auto* raster_only :
       std::initializer_list<QWidget*>{brush_size, brush_size_slider, brush_opacity,
                                       brush_opacity_slider, brush_softness, brush_softness_slider}) {
    vector_pixel_only_option_widgets_.push_back(raster_only);
  }
  connect(brush_size, &QSpinBox::valueChanged, brush_size_slider, &QSlider::setValue);
  connect(brush_size_slider, &QSlider::valueChanged, brush_size, &QSpinBox::setValue);
  connect(brush_size, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_brush_size(value);
      schedule_save_tool_settings();
      refresh_document_info();
    }
  });
  connect(brush_opacity, &QSpinBox::valueChanged, brush_opacity_slider, &QSlider::setValue);
  connect(brush_opacity_slider, &QSlider::valueChanged, brush_opacity, &QSpinBox::setValue);
  connect(brush_opacity, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_brush_opacity(value);
      schedule_save_tool_settings();
      refresh_document_info();
    }
  });

  auto* brush_flow_label = add_option_label(tr("Flow:"), {CanvasTool::Brush, CanvasTool::PatternStamp});
  bind_widget_text(brush_flow_label, "Flow:");
  auto* brush_flow = new QSpinBox(toolbar);
  brush_flow->setObjectName(QStringLiteral("brushFlowSpin"));
  brush_flow->setRange(1, 100);
  brush_flow->setValue(canvas_defaults->brush_flow());
  brush_flow->setSuffix(QStringLiteral("%"));
  brush_flow->setToolTip(tr("Brush flow - Shift+number keys (number keys with Airbrush)"));
  bind_tooltip(brush_flow, "Brush flow - Shift+number keys (number keys with Airbrush)");
  configure_toolbar_spinbox(brush_flow, 60);
  add_option_widget(brush_flow, {CanvasTool::Brush, CanvasTool::PatternStamp});
  auto* brush_airbrush = new CheckGlyphBox(tr("Airbrush"), toolbar);
  brush_airbrush->setObjectName(QStringLiteral("brushAirbrushCheck"));
  bind_widget_text(brush_airbrush, "Airbrush");
  brush_airbrush->setChecked(canvas_defaults->brush_build_up());
  brush_airbrush->setToolTip(tr("Build paint while the pointer is held still"));
  bind_tooltip(brush_airbrush, "Build paint while the pointer is held still");
  add_option_widget(brush_airbrush, {CanvasTool::Brush});
  connect(brush_flow, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_brush_flow(value);
      schedule_save_tool_settings();
      refresh_document_info();
    }
  });
  connect(brush_airbrush, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_brush_build_up(checked);
      schedule_save_tool_settings();
      refresh_document_info();
    }
  });

  const auto add_mixer_percentage = [this, toolbar, add_option_label, add_option_widget](
                                        const char* label_source, const char* object_name,
                                        int minimum, int value, auto setter) {
    auto* label = add_option_label(tr(label_source), {CanvasTool::MixerBrush});
    bind_widget_text(label, label_source);
    auto* spin = new QSpinBox(toolbar);
    spin->setObjectName(QString::fromLatin1(object_name));
    spin->setRange(minimum, 100);
    spin->setValue(value);
    spin->setSuffix(QStringLiteral("%"));
    configure_toolbar_spinbox(spin, 60);
    add_option_widget(spin, {CanvasTool::MixerBrush});
    connect(spin, &QSpinBox::valueChanged, this, [this, setter](int new_value) {
      setter(*this, new_value);
      schedule_save_tool_settings();
      refresh_document_info();
    });
    return spin;
  };
  add_mixer_percentage("Wet:", "mixerWetSpin", 0, current_mixer_wet_,
                       [](MainWindow& window, int value) {
                         window.current_mixer_wet_ = value;
                         if (window.canvas_ != nullptr) {
                           window.canvas_->set_mixer_wet(value);
                         }
                       });
  add_mixer_percentage("Load:", "mixerLoadSpin", 1, current_mixer_load_,
                       [](MainWindow& window, int value) {
                         window.current_mixer_load_ = value;
                         if (window.canvas_ != nullptr) {
                           window.canvas_->set_mixer_load(value);
                         }
                       });
  add_mixer_percentage("Mix:", "mixerMixSpin", 0, current_mixer_mix_,
                       [](MainWindow& window, int value) {
                         window.current_mixer_mix_ = value;
                         if (window.canvas_ != nullptr) {
                           window.canvas_->set_mixer_mix(value);
                         }
                       });
  add_mixer_percentage("Flow:", "mixerFlowSpin", 1, current_mixer_flow_,
                       [](MainWindow& window, int value) {
                         window.current_mixer_flow_ = value;
                         if (window.canvas_ != nullptr) {
                           window.canvas_->set_mixer_flow(value);
                         }
                       });

  connect(brush_softness, &QSpinBox::valueChanged, brush_softness_slider, &QSlider::setValue);
  connect(brush_softness_slider, &QSlider::valueChanged, brush_softness, &QSpinBox::setValue);
  connect(brush_softness, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_brush_softness(value);
      schedule_save_tool_settings();
      refresh_document_info();
    }
  });
  connect(brush_preset_combo_, &QComboBox::currentIndexChanged, this,
          [this, brush_size, brush_opacity, brush_flow, brush_softness,
           brush_airbrush](int index) {
    if (brush_preset_combo_ == nullptr || canvas_ == nullptr || index < 0) {
      return;
    }
    const auto preset_id = brush_preset_combo_->itemData(index).toString();
    const auto* preset = find_brush_preset(preset_id);
    if (preset == nullptr) {
      return;
    }
    if (preset_id == QStringLiteral("airbrush")) {
      // The quick Airbrush preset is a predictable soft Round brush. Existing sampled tips
      // already cover Smoke/Spray/Spatter/Stipple, so do not invent a duplicate airbrush tip or
      // carry a surprising Round dynamics session into this basic preset.
      round_brush_dynamics_ = {};
      round_brush_base_angle_degrees_ = 0.0;
      round_brush_base_roundness_ = 100.0;
      set_active_brush_tip(builtin_round_brush_tip_id(), false, false);
    }
    apply_brush_preset(*canvas_, *preset);
    brush_size->setValue(preset->size);
    brush_opacity->setValue(preset->opacity);
    brush_flow->setValue(preset->flow);
    brush_softness->setValue(preset->softness);
    brush_airbrush->setChecked(preset->build_up);
    save_tool_settings();
    refresh_document_info();
    statusBar()->showMessage(tr("Brush preset: %1").arg(brush_preset_display_name(*preset)));
  });

  add_option_label(tr("Tip:"),
                   {CanvasTool::Brush, CanvasTool::MixerBrush, CanvasTool::PatternStamp,
                    CanvasTool::Eraser});
  brush_tip_picker_ = new BrushTipPicker(brush_tip_library(), toolbar);
  // The options bar is built after load_tool_settings() reset the active tip to Round.
  brush_tip_picker_->set_current_tip_id(active_brush_tip_id_);
  add_option_widget(brush_tip_picker_,
                    {CanvasTool::Brush, CanvasTool::MixerBrush, CanvasTool::PatternStamp,
                     CanvasTool::Eraser});
  connect(brush_tip_picker_, &BrushTipPicker::tip_selected, this,
          [this](const QString& id) { set_active_brush_tip(id, true); });
  connect(brush_tip_picker_, &BrushTipPicker::import_requested, this,
          [this] { import_brush_tips_from_abr(); });
  connect(brush_tip_picker_, &BrushTipPicker::define_requested, this,
          [this] { define_brush_tip_from_selection(); });
  connect(brush_tip_picker_, &BrushTipPicker::manage_requested, this, [this] { open_brush_tip_manager(); });
  connect(&brush_tip_library(), &BrushTipLibrary::changed, this, [this] {
    // A removed tip must not stay active; re-resolving also refreshes renamed/respaced tips.
    // Re-applying after a library edit must not reset Flow/Airbrush to imported tool settings.
    set_active_brush_tip(active_brush_tip_id_, false, false);
  });
  QPointer<BrushTipPicker> tip_picker(brush_tip_picker_);
  register_retranslation([tip_picker] {
    if (tip_picker != nullptr) {
      tip_picker->refresh();
    }
  });

  brush_dynamics_button_ = new BrushDynamicsButton(toolbar);
  add_option_widget(brush_dynamics_button_, {CanvasTool::Brush});
  connect(brush_dynamics_button_, &BrushDynamicsButton::dynamics_edited, this,
          [this](const QString& tip_id, const patchy::BrushDynamics& dynamics, double base_angle,
                 double base_roundness) {
            if (tip_id == builtin_round_brush_tip_id()) {
              // Session-only: the Round brush's dynamics live in the window, not the library,
              // and deliberately reset on the next launch.
              round_brush_dynamics_ = dynamics;
              round_brush_base_angle_degrees_ = base_angle;
              round_brush_base_roundness_ = base_roundness;
              if (canvas_ != nullptr &&
                  (active_brush_tip_id_.isEmpty() ||
                   active_brush_tip_id_ == builtin_round_brush_tip_id())) {
                canvas_->set_brush_dynamics(dynamics);
                canvas_->set_brush_base_shape(base_angle, static_cast<int>(std::lround(base_roundness)));
              }
              return;
            }
            if (canvas_ != nullptr && tip_id == active_brush_tip_id_) {
              canvas_->set_brush_dynamics(dynamics);
              canvas_->set_brush_base_shape(base_angle, static_cast<int>(std::lround(base_roundness)));
            }
            // Persisting to the sidecar emits changed(), which re-applies the (identical) values.
            brush_tip_library().set_tip_dynamics(tip_id, dynamics, base_angle, base_roundness);
          });
  // The options bar is built after load_tool_settings() already selected the startup tip, so
  // seed the button's model now (Round session values, or the entry if a tip is active).
  if (active_brush_tip_id_.isEmpty() || active_brush_tip_id_ == builtin_round_brush_tip_id()) {
    brush_dynamics_button_->set_round_session(builtin_round_brush_tip_id(), round_brush_dynamics_,
                                              round_brush_base_angle_degrees_,
                                              round_brush_base_roundness_);
  } else if (const auto* entry = brush_tip_library().find_entry(active_brush_tip_id_);
             entry != nullptr) {
    brush_dynamics_button_->set_active_entry(entry);
  }
  QPointer<BrushDynamicsButton> dynamics_button(brush_dynamics_button_);
  register_retranslation([dynamics_button] {
    if (dynamics_button != nullptr) {
      dynamics_button->retranslate();
    }
  });

  auto* pattern_label = add_option_label(tr("Pattern:"), {CanvasTool::PatternStamp});
  bind_widget_text(pattern_label, "Pattern:");
  pattern_stamp_pattern_combo_ = new QComboBox(toolbar);
  pattern_stamp_pattern_combo_->setObjectName(QStringLiteral("patternStampPatternCombo"));
  pattern_stamp_pattern_combo_->setIconSize(QSize(18, 18));
  pattern_stamp_pattern_combo_->setMinimumWidth(150);
  refresh_pattern_stamp_pattern_combo();
  add_option_widget(pattern_stamp_pattern_combo_, {CanvasTool::PatternStamp});
  connect(pattern_stamp_pattern_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (pattern_stamp_pattern_combo_ == nullptr || index < 0) {
      return;
    }
    current_pattern_stamp_pattern_id_ = pattern_stamp_pattern_combo_->itemData(index).toString();
    apply_pattern_stamp_settings_to_canvas(canvas_);
    schedule_save_tool_settings();
  });

  auto* manage_patterns = new QPushButton(toolbar);
  manage_patterns->setObjectName(QStringLiteral("patternStampManageButton"));
  manage_patterns->setText(tr("Manage..."));
  bind_widget_text(manage_patterns, "Manage...");
  manage_patterns->setToolTip(tr("Import or manage patterns"));
  bind_tooltip(manage_patterns, "Import or manage patterns");
  add_option_widget(manage_patterns, {CanvasTool::PatternStamp});
  connect(manage_patterns, &QPushButton::clicked, this, [this] {
    const auto selected_storage_id =
        request_pattern_manager(this, pattern_library(), current_pattern_stamp_pattern_id_);
    if (selected_storage_id.isEmpty()) {
      return;
    }
    if (const auto* entry = pattern_library().find_entry(selected_storage_id); entry != nullptr) {
      current_pattern_stamp_pattern_id_ = entry->id;
      refresh_pattern_stamp_pattern_combo();
      apply_pattern_stamp_settings_to_canvas(canvas_);
      save_tool_settings();
    }
  });

  pattern_stamp_aligned_check_ = new CheckGlyphBox(tr("Aligned"), toolbar);
  pattern_stamp_aligned_check_->setObjectName(QStringLiteral("patternStampAlignedCheck"));
  pattern_stamp_aligned_check_->setChecked(current_pattern_stamp_aligned_);
  pattern_stamp_aligned_check_->setToolTip(tr("Keep pattern alignment continuous across strokes"));
  bind_tooltip(pattern_stamp_aligned_check_, "Keep pattern alignment continuous across strokes");
  add_option_widget(pattern_stamp_aligned_check_, {CanvasTool::PatternStamp});
  connect(pattern_stamp_aligned_check_, &QCheckBox::toggled, this, [this](bool checked) {
    current_pattern_stamp_aligned_ = checked;
    apply_pattern_stamp_settings_to_canvas(canvas_);
    save_tool_settings();
  });

  add_option_label(tr("Method:"), {CanvasTool::Gradient});
  gradient_method_combo_ = new QComboBox(toolbar);
  gradient_method_combo_->setObjectName(QStringLiteral("gradientMethodCombo"));
  gradient_method_combo_->addItem(tr("Linear"), static_cast<int>(GradientMethod::Linear));
  gradient_method_combo_->addItem(tr("Radial"), static_cast<int>(GradientMethod::Radial));
  gradient_method_combo_->setFixedWidth(86);
  add_option_widget(gradient_method_combo_, {CanvasTool::Gradient});
  QPointer<QComboBox> gradient_method_combo(gradient_method_combo_);
  register_retranslation([gradient_method_combo] {
    if (gradient_method_combo == nullptr || gradient_method_combo->count() < 2) {
      return;
    }
    const QSignalBlocker blocker(gradient_method_combo);
    gradient_method_combo->setItemText(0, QCoreApplication::translate(kMainWindowTranslationContext, "Linear"));
    gradient_method_combo->setItemText(1, QCoreApplication::translate(kMainWindowTranslationContext, "Radial"));
  });

  add_option_label(tr("Opacity:"), {CanvasTool::Gradient});
  gradient_opacity_spin_ = new QSpinBox(toolbar);
  gradient_opacity_spin_->setObjectName(QStringLiteral("gradientOpacitySpin"));
  gradient_opacity_spin_->setRange(0, 100);
  gradient_opacity_spin_->setValue(canvas_defaults->gradient_opacity());
  gradient_opacity_spin_->setSuffix(QStringLiteral("%"));
  configure_toolbar_spinbox(gradient_opacity_spin_, 52);
  add_option_widget(gradient_opacity_spin_, {CanvasTool::Gradient});
  gradient_opacity_slider_ = new QSlider(Qt::Horizontal, toolbar);
  gradient_opacity_slider_->setObjectName(QStringLiteral("gradientOpacitySlider"));
  gradient_opacity_slider_->setRange(0, 100);
  gradient_opacity_slider_->setValue(canvas_defaults->gradient_opacity());
  gradient_opacity_slider_->setFixedWidth(110);
  gradient_opacity_slider_->setToolTip(tr("Gradient opacity"));
  bind_tooltip(gradient_opacity_slider_, "Gradient opacity");
  add_option_widget(gradient_opacity_slider_, {CanvasTool::Gradient});
  gradient_reverse_check_ = new CheckGlyphBox(tr("Reverse"), toolbar);
  gradient_reverse_check_->setObjectName(QStringLiteral("gradientReverseCheck"));
  gradient_reverse_check_->setChecked(canvas_defaults->gradient_reverse());
  add_option_widget(gradient_reverse_check_, {CanvasTool::Gradient});
  gradient_preview_button_ = new QPushButton(toolbar);
  gradient_preview_button_->setObjectName(QStringLiteral("gradientPreviewButton"));
  gradient_preview_button_->setToolTip(tr("Gradient preview"));
  bind_tooltip(gradient_preview_button_, "Gradient preview");
  add_option_widget(gradient_preview_button_, {CanvasTool::Gradient});
  gradient_edit_stops_button_ = new QPushButton(tr("Edit Stops..."), toolbar);
  gradient_edit_stops_button_->setObjectName(QStringLiteral("gradientEditStopsButton"));
  add_option_widget(gradient_edit_stops_button_, {CanvasTool::Gradient});
  refresh_gradient_controls_from_canvas();
  connect(gradient_method_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (canvas_ == nullptr || gradient_method_combo_ == nullptr || index < 0) {
      return;
    }
    canvas_->set_gradient_method(static_cast<GradientMethod>(gradient_method_combo_->itemData(index).toInt()));
    save_tool_settings();
    refresh_document_info();
  });
  connect(gradient_opacity_spin_, &QSpinBox::valueChanged, gradient_opacity_slider_, &QSlider::setValue);
  connect(gradient_opacity_slider_, &QSlider::valueChanged, gradient_opacity_spin_, &QSpinBox::setValue);
  connect(gradient_opacity_spin_, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_gradient_opacity(value);
      refresh_gradient_controls_from_canvas();
      schedule_save_tool_settings();
      refresh_document_info();
    }
  });
  connect(gradient_reverse_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_gradient_reverse(checked);
      refresh_gradient_controls_from_canvas();
      save_tool_settings();
      refresh_document_info();
    }
  });
  connect(gradient_preview_button_, &QPushButton::clicked, this, [this] { edit_gradient_stops(); });
  connect(gradient_edit_stops_button_, &QPushButton::clicked, this, [this] { edit_gradient_stops(); });

  clone_aligned_check_ = new CheckGlyphBox(tr("Aligned"), toolbar);
  clone_aligned_check_->setObjectName(QStringLiteral("cloneAlignedCheck"));
  clone_aligned_check_->setChecked(canvas_defaults->clone_aligned());
  clone_aligned_check_->setToolTip(tr("Keep sample source offset aligned across strokes"));
  add_option_widget(clone_aligned_check_, {CanvasTool::Clone, CanvasTool::Healing});
  connect(clone_aligned_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_clone_aligned(checked);
      save_tool_settings();
    }
  });

  add_option_label(tr("Diffusion:"), {CanvasTool::Healing});
  auto* healing_diffusion = new QSpinBox(toolbar);
  healing_diffusion->setObjectName(QStringLiteral("healingDiffusionSpin"));
  healing_diffusion->setRange(1, 7);
  healing_diffusion->setValue(current_healing_diffusion_);
  healing_diffusion->setToolTip(tr("Lower values preserve fine texture; higher values adapt more quickly"));
  configure_toolbar_spinbox(healing_diffusion, 42);
  add_option_widget(healing_diffusion, {CanvasTool::Healing});
  connect(healing_diffusion, &QSpinBox::valueChanged, this, [this](int value) {
    current_healing_diffusion_ = value;
    if (canvas_ != nullptr) {
      canvas_->set_healing_diffusion(value);
      save_tool_settings();
    }
  });

  add_option_label(tr("Strength:"), {CanvasTool::Dodge, CanvasTool::Burn, CanvasTool::Sponge,
                                      CanvasTool::BlurBrush, CanvasTool::SharpenBrush});
  local_adjustment_strength_spin_ = new QSpinBox(toolbar);
  local_adjustment_strength_spin_->setObjectName(QStringLiteral("localAdjustmentStrengthSpin"));
  local_adjustment_strength_spin_->setRange(1, 100);
  local_adjustment_strength_spin_->setValue(current_local_adjustment_strength_);
  local_adjustment_strength_spin_->setSuffix(QStringLiteral("%"));
  local_adjustment_strength_spin_->setToolTip(tr("Maximum adjustment applied during one stroke"));
  bind_tooltip(local_adjustment_strength_spin_, "Maximum adjustment applied during one stroke");
  configure_toolbar_spinbox(local_adjustment_strength_spin_, 52);
  add_option_widget(local_adjustment_strength_spin_,
                    {CanvasTool::Dodge, CanvasTool::Burn, CanvasTool::Sponge,
                     CanvasTool::BlurBrush, CanvasTool::SharpenBrush});
  connect(local_adjustment_strength_spin_, &QSpinBox::valueChanged, this, [this](int value) {
    current_local_adjustment_strength_ = value;
    if (canvas_ != nullptr) {
      canvas_->set_local_adjustment_strength(value);
    }
    schedule_save_tool_settings();
    refresh_document_info();
  });

  add_option_label(tr("Range:"), {CanvasTool::Dodge, CanvasTool::Burn});
  local_tone_range_combo_ = new QComboBox(toolbar);
  local_tone_range_combo_->setObjectName(QStringLiteral("localToneRangeCombo"));
  local_tone_range_combo_->addItem(tr("Shadows"), static_cast<int>(CanvasWidget::LocalToneRange::Shadows));
  local_tone_range_combo_->addItem(tr("Midtones"), static_cast<int>(CanvasWidget::LocalToneRange::Midtones));
  local_tone_range_combo_->addItem(tr("Highlights"), static_cast<int>(CanvasWidget::LocalToneRange::Highlights));
  local_tone_range_combo_->setCurrentIndex(
      std::max(0, local_tone_range_combo_->findData(static_cast<int>(current_local_tone_range_))));
  local_tone_range_combo_->setFixedWidth(92);
  add_option_widget(local_tone_range_combo_, {CanvasTool::Dodge, CanvasTool::Burn});
  connect(local_tone_range_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (index < 0 || local_tone_range_combo_ == nullptr) {
      return;
    }
    current_local_tone_range_ =
        static_cast<CanvasWidget::LocalToneRange>(local_tone_range_combo_->itemData(index).toInt());
    if (canvas_ != nullptr) {
      canvas_->set_local_tone_range(current_local_tone_range_);
    }
    save_tool_settings();
    refresh_document_info();
  });
  QPointer<QComboBox> local_tone_range_combo(local_tone_range_combo_);
  register_retranslation([local_tone_range_combo] {
    if (local_tone_range_combo == nullptr || local_tone_range_combo->count() < 3) {
      return;
    }
    const QSignalBlocker blocker(local_tone_range_combo);
    local_tone_range_combo->setItemText(
        0, QCoreApplication::translate(kMainWindowTranslationContext, "Shadows"));
    local_tone_range_combo->setItemText(
        1, QCoreApplication::translate(kMainWindowTranslationContext, "Midtones"));
    local_tone_range_combo->setItemText(
        2, QCoreApplication::translate(kMainWindowTranslationContext, "Highlights"));
  });

  local_protect_tones_check_ = new CheckGlyphBox(tr("Protect Tones"), toolbar);
  local_protect_tones_check_->setObjectName(QStringLiteral("localProtectTonesCheck"));
  local_protect_tones_check_->setChecked(current_local_protect_tones_);
  local_protect_tones_check_->setToolTip(tr("Preserve local color differences while lightening or darkening"));
  bind_tooltip(local_protect_tones_check_, "Preserve local color differences while lightening or darkening");
  bind_widget_text(local_protect_tones_check_, "Protect Tones");
  add_option_widget(local_protect_tones_check_, {CanvasTool::Dodge, CanvasTool::Burn});
  connect(local_protect_tones_check_, &QCheckBox::toggled, this, [this](bool checked) {
    current_local_protect_tones_ = checked;
    if (canvas_ != nullptr) {
      canvas_->set_local_protect_tones(checked);
    }
    save_tool_settings();
  });

  add_option_label(tr("Mode:"), {CanvasTool::Sponge});
  sponge_mode_combo_ = new QComboBox(toolbar);
  sponge_mode_combo_->setObjectName(QStringLiteral("spongeModeCombo"));
  sponge_mode_combo_->addItem(tr("Saturate"), static_cast<int>(CanvasWidget::SpongeMode::Saturate));
  sponge_mode_combo_->addItem(tr("Desaturate"), static_cast<int>(CanvasWidget::SpongeMode::Desaturate));
  sponge_mode_combo_->setCurrentIndex(
      std::max(0, sponge_mode_combo_->findData(static_cast<int>(current_sponge_mode_))));
  sponge_mode_combo_->setFixedWidth(94);
  add_option_widget(sponge_mode_combo_, {CanvasTool::Sponge});
  connect(sponge_mode_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (index < 0 || sponge_mode_combo_ == nullptr) {
      return;
    }
    current_sponge_mode_ =
        static_cast<CanvasWidget::SpongeMode>(sponge_mode_combo_->itemData(index).toInt());
    if (canvas_ != nullptr) {
      canvas_->set_sponge_mode(current_sponge_mode_);
    }
    save_tool_settings();
    refresh_document_info();
  });
  QPointer<QComboBox> sponge_mode_combo(sponge_mode_combo_);
  register_retranslation([sponge_mode_combo] {
    if (sponge_mode_combo == nullptr || sponge_mode_combo->count() < 2) {
      return;
    }
    const QSignalBlocker blocker(sponge_mode_combo);
    sponge_mode_combo->setItemText(
        0, QCoreApplication::translate(kMainWindowTranslationContext, "Saturate"));
    sponge_mode_combo->setItemText(
        1, QCoreApplication::translate(kMainWindowTranslationContext, "Desaturate"));
  });

  sponge_vibrance_check_ = new CheckGlyphBox(tr("Vibrance"), toolbar);
  sponge_vibrance_check_->setObjectName(QStringLiteral("spongeVibranceCheck"));
  sponge_vibrance_check_->setChecked(current_sponge_vibrance_);
  sponge_vibrance_check_->setToolTip(tr("Reduce the adjustment on colors that are already strongly saturated"));
  bind_tooltip(sponge_vibrance_check_,
               "Reduce the adjustment on colors that are already strongly saturated");
  bind_widget_text(sponge_vibrance_check_, "Vibrance");
  add_option_widget(sponge_vibrance_check_, {CanvasTool::Sponge});
  connect(sponge_vibrance_check_, &QCheckBox::toggled, this, [this](bool checked) {
    current_sponge_vibrance_ = checked;
    if (canvas_ != nullptr) {
      canvas_->set_sponge_vibrance(checked);
    }
    save_tool_settings();
  });

  add_option_label(tr("Size:"), {CanvasTool::QuickSelect});
  auto* quick_select_size = new QSpinBox(toolbar);
  quick_select_size->setObjectName(QStringLiteral("quickSelectSizeSpin"));
  quick_select_size->setRange(1, 512);
  quick_select_size->setValue(canvas_defaults->quick_select_size());
  configure_toolbar_spinbox(quick_select_size, 46);
  add_option_widget(quick_select_size, {CanvasTool::QuickSelect});
  auto* quick_select_size_slider = new QSlider(Qt::Horizontal, toolbar);
  quick_select_size_slider->setObjectName(QStringLiteral("quickSelectSizeSlider"));
  quick_select_size_slider->setRange(1, 512);
  quick_select_size_slider->setValue(canvas_defaults->quick_select_size());
  quick_select_size_slider->setFixedWidth(150);
  quick_select_size_slider->setToolTip(tr("Quick Select brush size — press [ or ]"));
  bind_tooltip(quick_select_size_slider, "Quick Select brush size — press [ or ]");
  add_option_widget(quick_select_size_slider, {CanvasTool::QuickSelect});
  connect(quick_select_size, &QSpinBox::valueChanged, quick_select_size_slider, &QSlider::setValue);
  connect(quick_select_size_slider, &QSlider::valueChanged, quick_select_size, &QSpinBox::setValue);
  connect(quick_select_size, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_quick_select_size(value);
      canvas_->refresh_tool_cursor();
      schedule_save_tool_settings();
      refresh_document_info();
    }
  });

  quick_select_sample_all_layers_check_ = new CheckGlyphBox(tr("Sample All Layers"), toolbar);
  quick_select_sample_all_layers_check_->setObjectName(QStringLiteral("quickSelectSampleAllLayersCheck"));
  quick_select_sample_all_layers_check_->setChecked(canvas_defaults->quick_select_sample_all_layers());
  quick_select_sample_all_layers_check_->setToolTip(tr("Sample the merged document instead of the active layer"));
  add_option_widget(quick_select_sample_all_layers_check_, {CanvasTool::QuickSelect});
  connect(quick_select_sample_all_layers_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_quick_select_sample_all_layers(checked);
      save_tool_settings();
      refresh_document_info();
    }
  });

  quick_select_enhance_edge_check_ = new CheckGlyphBox(tr("Enhance Edge"), toolbar);
  quick_select_enhance_edge_check_->setObjectName(QStringLiteral("quickSelectEnhanceEdgeCheck"));
  quick_select_enhance_edge_check_->setChecked(canvas_defaults->quick_select_enhance_edge());
  quick_select_enhance_edge_check_->setToolTip(tr("Smooth the selection boundary after each stroke"));
  add_option_widget(quick_select_enhance_edge_check_, {CanvasTool::QuickSelect});
  connect(quick_select_enhance_edge_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_quick_select_enhance_edge(checked);
      save_tool_settings();
      refresh_document_info();
    }
  });

  add_option_label(tr("Width:"), {CanvasTool::MagneticLasso});
  auto* magnetic_width = new QSpinBox(toolbar);
  magnetic_width->setObjectName(QStringLiteral("magneticLassoWidthSpin"));
  magnetic_width->setRange(1, 256);
  magnetic_width->setSuffix(QStringLiteral(" px"));
  magnetic_width->setValue(canvas_defaults->magnetic_lasso_width());
  magnetic_width->setToolTip(tr("Edge search width in document pixels — press [ or ]"));
  bind_tooltip(magnetic_width, "Edge search width in document pixels — press [ or ]");
  configure_toolbar_spinbox(magnetic_width, 64);
  add_option_widget(magnetic_width, {CanvasTool::MagneticLasso});
  connect(magnetic_width, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_magnetic_lasso_width(value);
      schedule_save_tool_settings();
      refresh_document_info();
    }
  });
  add_option_label(tr("Contrast:"), {CanvasTool::MagneticLasso});
  auto* magnetic_contrast = new QSpinBox(toolbar);
  magnetic_contrast->setObjectName(QStringLiteral("magneticLassoContrastSpin"));
  magnetic_contrast->setRange(1, 100);
  magnetic_contrast->setSuffix(QStringLiteral("%"));
  magnetic_contrast->setValue(canvas_defaults->magnetic_lasso_edge_contrast());
  magnetic_contrast->setToolTip(tr("Minimum edge contrast the trace snaps to"));
  bind_tooltip(magnetic_contrast, "Minimum edge contrast the trace snaps to");
  configure_toolbar_spinbox(magnetic_contrast, 56);
  add_option_widget(magnetic_contrast, {CanvasTool::MagneticLasso});
  connect(magnetic_contrast, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_magnetic_lasso_edge_contrast(value);
      schedule_save_tool_settings();
      refresh_document_info();
    }
  });
  add_option_label(tr("Frequency:"), {CanvasTool::MagneticLasso});
  auto* magnetic_frequency = new QSpinBox(toolbar);
  magnetic_frequency->setObjectName(QStringLiteral("magneticLassoFrequencySpin"));
  magnetic_frequency->setRange(0, 100);
  magnetic_frequency->setValue(canvas_defaults->magnetic_lasso_frequency());
  magnetic_frequency->setToolTip(tr("How often anchor points are placed while tracing"));
  bind_tooltip(magnetic_frequency, "How often anchor points are placed while tracing");
  configure_toolbar_spinbox(magnetic_frequency, 46);
  add_option_widget(magnetic_frequency, {CanvasTool::MagneticLasso});
  connect(magnetic_frequency, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_magnetic_lasso_frequency(value);
      schedule_save_tool_settings();
      refresh_document_info();
    }
  });

  auto* brush_smaller_action = new QAction(tr("Brush Smaller"), this);
  auto* brush_larger_action = new QAction(tr("Brush Larger"), this);
  auto* brush_much_smaller_action = new QAction(tr("Brush Much Smaller"), this);
  auto* brush_much_larger_action = new QAction(tr("Brush Much Larger"), this);
  brush_smaller_action->setObjectName(QStringLiteral("brushSmallerAction"));
  brush_larger_action->setObjectName(QStringLiteral("brushLargerAction"));
  brush_much_smaller_action->setObjectName(QStringLiteral("brushMuchSmallerAction"));
  brush_much_larger_action->setObjectName(QStringLiteral("brushMuchLargerAction"));
  register_hotkey(brush_smaller_action, "brush.smaller", QKeySequence(Qt::Key_BracketLeft), QStringLiteral("brush"));
  register_hotkey(brush_larger_action, "brush.larger", QKeySequence(Qt::Key_BracketRight), QStringLiteral("brush"));
  register_hotkey(brush_much_smaller_action, "brush.much_smaller", QKeySequence(Qt::SHIFT | Qt::Key_BracketLeft), QStringLiteral("brush"));
  register_hotkey(brush_much_larger_action, "brush.much_larger", QKeySequence(Qt::SHIFT | Qt::Key_BracketRight), QStringLiteral("brush"));
  addAction(brush_smaller_action);
  addAction(brush_larger_action);
  addAction(brush_much_smaller_action);
  addAction(brush_much_larger_action);
  // The bracket keys resize whichever brush the active tool uses (Quick Select
  // has its own; for the Magnetic Lasso they adjust the edge search width).
  const auto adjust_brush_size = [this, brush_size, quick_select_size, magnetic_width](int direction, bool coarse) {
    const bool quick_select = current_tool_ == CanvasTool::QuickSelect;
    const bool magnetic = current_tool_ == CanvasTool::MagneticLasso;
    auto* spin = quick_select ? quick_select_size : magnetic ? magnetic_width : brush_size;
    const int cap = quick_select ? 512 : magnetic ? 256 : kMaxBrushSize;
    const int value = spin->value();
    const int step = proportional_brush_step(value, direction, coarse);
    spin->setValue(std::clamp(value + direction * step, 1, cap));
  };
  connect(brush_smaller_action, &QAction::triggered, brush_size,
          [adjust_brush_size] { adjust_brush_size(-1, false); });
  connect(brush_larger_action, &QAction::triggered, brush_size,
          [adjust_brush_size] { adjust_brush_size(1, false); });
  connect(brush_much_smaller_action, &QAction::triggered, brush_size,
          [adjust_brush_size] { adjust_brush_size(-1, true); });
  connect(brush_much_larger_action, &QAction::triggered, brush_size,
          [adjust_brush_size] { adjust_brush_size(1, true); });
  for (auto* action : {brush_smaller_action, brush_larger_action, brush_much_smaller_action,
                       brush_much_larger_action}) {
    register_document_action(action);
  }

  add_option_label(tr("Tol:"), {CanvasTool::MagicWand});
  auto* wand_tolerance = new QSpinBox(toolbar);
  wand_tolerance->setObjectName(QStringLiteral("wandToleranceSpin"));
  wand_tolerance->setRange(0, 255);
  wand_tolerance->setValue(canvas_defaults->wand_tolerance());
  configure_toolbar_spinbox(wand_tolerance, 46);
  add_option_widget(wand_tolerance, {CanvasTool::MagicWand});
  connect(wand_tolerance, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_wand_tolerance(value);
      schedule_save_tool_settings();
      refresh_document_info();
    }
  });

  wand_contiguous_check_ = new CheckGlyphBox(tr("Contiguous"), toolbar);
  wand_contiguous_check_->setObjectName(QStringLiteral("wandContiguousCheck"));
  wand_contiguous_check_->setChecked(canvas_defaults->wand_contiguous());
  wand_contiguous_check_->setToolTip(tr("Limit Magic Wand selection to connected pixels"));
  add_option_widget(wand_contiguous_check_, {CanvasTool::MagicWand});
  connect(wand_contiguous_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_wand_contiguous(checked);
      save_tool_settings();
      refresh_document_info();
    }
  });

  wand_sample_all_layers_check_ = new CheckGlyphBox(tr("Sample All Layers"), toolbar);
  wand_sample_all_layers_check_->setObjectName(QStringLiteral("wandSampleAllLayersCheck"));
  wand_sample_all_layers_check_->setChecked(canvas_defaults->wand_sample_all_layers());
  wand_sample_all_layers_check_->setToolTip(tr("Sample the merged document instead of the active layer"));
  add_option_widget(wand_sample_all_layers_check_, {CanvasTool::MagicWand});
  connect(wand_sample_all_layers_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_wand_sample_all_layers(checked);
      save_tool_settings();
      refresh_document_info();
    }
  });

  // Shape | Path | Pixels for the vector-capable draw tools (Shape is the
  // Photoshop-parity default; Pixels is the legacy raster behavior). The
  // vector appearance/combine widgets below register for the same tools and
  // refresh_vector_tool_options_visibility() refines them per mode.
  add_option_label(tr("Mode:"), {CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse});
  vector_mode_combo_ = new QComboBox(toolbar);
  vector_mode_combo_->setObjectName(QStringLiteral("vectorModeCombo"));
  vector_mode_combo_->addItems({tr("Shape"), tr("Path"), tr("Pixels")});
  vector_mode_combo_->setCurrentIndex(0);
  vector_mode_combo_->setFixedWidth(76);
  vector_mode_combo_->setToolTip(
      tr("What the shape tools create: a shape layer, work-path subpaths, or raster pixels"));
  QPointer<QComboBox> vector_mode_combo_pointer(vector_mode_combo_);
  register_retranslation([vector_mode_combo_pointer] {
    if (vector_mode_combo_pointer == nullptr || vector_mode_combo_pointer->count() < 3) {
      return;
    }
    QSignalBlocker blocker(vector_mode_combo_pointer);
    // MainWindow::tr (not QObject::tr): "Pixels"/"Subtract" exist in the
    // QObject context with unrelated meanings (color mode, blend mode).
    vector_mode_combo_pointer->setItemText(0, MainWindow::tr("Shape"));
    vector_mode_combo_pointer->setItemText(1, MainWindow::tr("Path"));
    vector_mode_combo_pointer->setItemText(2, MainWindow::tr("Pixels"));
  });
  add_option_widget(vector_mode_combo_, {CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse});
  connect(vector_mode_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    current_vector_tool_mode_ = index == 1   ? VectorToolMode::Path
                                : index == 2 ? VectorToolMode::Pixels
                                             : VectorToolMode::Shape;
    if (canvas_ != nullptr) {
      canvas_->set_vector_tool_mode(current_vector_tool_mode_);
      schedule_save_tool_settings();
    }
    refresh_options_bar();
  });

  vector_shape_mode_option_widgets_.push_back(
      add_option_label(tr("Fill:"), {CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse}));
  vector_fill_swatch_button_ = new QToolButton(toolbar);
  vector_fill_swatch_button_->setObjectName(QStringLiteral("vectorFillSwatchButton"));
  vector_fill_swatch_button_->setToolTip(tr("Shape fill color"));
  vector_fill_swatch_button_->setAutoRaise(true);
  vector_fill_swatch_button_->setProperty("optionsBarButton", true);
  add_option_widget(vector_fill_swatch_button_,
                    {CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse});
  vector_shape_mode_option_widgets_.push_back(vector_fill_swatch_button_);
  connect(vector_fill_swatch_button_, &QToolButton::clicked, this, [this] {
    const auto chosen = request_patchy_color(this, current_vector_fill_color_, tr("Shape Fill Color"));
    if (chosen.has_value()) {
      current_vector_fill_color_ = *chosen;
      update_vector_swatch_icons();
      schedule_save_tool_settings();
    }
  });

  auto* vector_stroke_check = new CheckGlyphBox(tr("Stroke"), toolbar);
  vector_stroke_check->setObjectName(QStringLiteral("vectorStrokeCheck"));
  vector_stroke_check->setChecked(current_vector_stroke_enabled_);
  vector_stroke_check->setToolTip(tr("Stroke the shape outline"));
  add_option_widget(vector_stroke_check, {CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse});
  vector_shape_mode_option_widgets_.push_back(vector_stroke_check);
  connect(vector_stroke_check, &QCheckBox::toggled, this, [this](bool checked) {
    current_vector_stroke_enabled_ = checked;
    schedule_save_tool_settings();
  });

  vector_stroke_swatch_button_ = new QToolButton(toolbar);
  vector_stroke_swatch_button_->setObjectName(QStringLiteral("vectorStrokeSwatchButton"));
  vector_stroke_swatch_button_->setToolTip(tr("Shape stroke color"));
  vector_stroke_swatch_button_->setAutoRaise(true);
  vector_stroke_swatch_button_->setProperty("optionsBarButton", true);
  add_option_widget(vector_stroke_swatch_button_,
                    {CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse});
  vector_shape_mode_option_widgets_.push_back(vector_stroke_swatch_button_);
  connect(vector_stroke_swatch_button_, &QToolButton::clicked, this, [this] {
    const auto chosen =
        request_patchy_color(this, current_vector_stroke_color_, tr("Shape Stroke Color"));
    if (chosen.has_value()) {
      current_vector_stroke_color_ = *chosen;
      update_vector_swatch_icons();
      schedule_save_tool_settings();
    }
  });

  auto* vector_stroke_width = new QDoubleSpinBox(toolbar);
  vector_stroke_width->setObjectName(QStringLiteral("vectorStrokeWidthSpin"));
  vector_stroke_width->setRange(0.1, 1000.0);
  vector_stroke_width->setDecimals(1);
  vector_stroke_width->setValue(current_vector_stroke_width_);
  vector_stroke_width->setSuffix(QStringLiteral(" px"));
  vector_stroke_width->setToolTip(tr("Stroke width"));
  configure_toolbar_spinbox(vector_stroke_width, 64);
  add_option_widget(vector_stroke_width, {CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse});
  vector_shape_mode_option_widgets_.push_back(vector_stroke_width);
  connect(vector_stroke_width, &QDoubleSpinBox::valueChanged, this, [this](double value) {
    current_vector_stroke_width_ = value;
    schedule_save_tool_settings();
  });

  vector_vector_mode_option_widgets_.push_back(
      add_option_label(tr("Weight:"), {CanvasTool::Line}));
  auto* vector_line_weight = new QSpinBox(toolbar);
  vector_line_weight->setObjectName(QStringLiteral("vectorLineWeightSpin"));
  vector_line_weight->setRange(1, 1000);
  vector_line_weight->setValue(current_vector_line_weight_);
  vector_line_weight->setSuffix(QStringLiteral(" px"));
  vector_line_weight->setToolTip(tr("Line thickness"));
  configure_toolbar_spinbox(vector_line_weight, 58);
  add_option_widget(vector_line_weight, {CanvasTool::Line});
  vector_vector_mode_option_widgets_.push_back(vector_line_weight);
  connect(vector_line_weight, &QSpinBox::valueChanged, this, [this](int value) {
    current_vector_line_weight_ = value;
    schedule_save_tool_settings();
  });

  vector_vector_mode_option_widgets_.push_back(add_option_label(
      tr("Combine:"), {CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse}));
  auto* vector_combine_combo = new QComboBox(toolbar);
  vector_combine_combo->setObjectName(QStringLiteral("vectorCombineCombo"));
  vector_combine_combo->addItems(
      {tr("New Layer"), tr("Add"), tr("Subtract"), tr("Intersect"), tr("Exclude")});
  vector_combine_combo->setCurrentIndex(0);
  vector_combine_combo->setFixedWidth(96);
  vector_combine_combo->setToolTip(
      tr("How the next shape combines with the active shape layer or work path"));
  QPointer<QComboBox> vector_combine_combo_pointer(vector_combine_combo);
  register_retranslation([vector_combine_combo_pointer] {
    if (vector_combine_combo_pointer == nullptr || vector_combine_combo_pointer->count() < 5) {
      return;
    }
    QSignalBlocker blocker(vector_combine_combo_pointer);
    vector_combine_combo_pointer->setItemText(0, MainWindow::tr("New Layer"));
    vector_combine_combo_pointer->setItemText(1, MainWindow::tr("Add"));
    vector_combine_combo_pointer->setItemText(2, MainWindow::tr("Subtract"));
    vector_combine_combo_pointer->setItemText(3, MainWindow::tr("Intersect"));
    vector_combine_combo_pointer->setItemText(4, MainWindow::tr("Exclude"));
  });
  add_option_widget(vector_combine_combo,
                    {CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse});
  vector_vector_mode_option_widgets_.push_back(vector_combine_combo);
  connect(vector_combine_combo, &QComboBox::currentIndexChanged, this,
          [this](int index) { current_vector_combine_index_ = index; });
  update_vector_swatch_icons();

  auto* fill_shapes = new CheckGlyphBox(tr("Fill"), toolbar);
  fill_shapes->setObjectName(QStringLiteral("shapeFillCheck"));
  add_option_widget(fill_shapes, {CanvasTool::Rectangle, CanvasTool::Ellipse});
  vector_pixel_only_option_widgets_.push_back(fill_shapes);
  connect(fill_shapes, &QCheckBox::toggled, this, [this](bool checked) {
    current_fill_shapes_ = checked;
    if (canvas_ != nullptr) {
      canvas_->set_fill_shapes(checked);
    }
  });

  add_option_label(tr("Radius:"), {CanvasTool::Rectangle});
  auto* shape_corner_radius = new QSpinBox(toolbar);
  shape_corner_radius->setObjectName(QStringLiteral("shapeCornerRadiusSpin"));
  shape_corner_radius->setRange(0, 512);
  shape_corner_radius->setValue(canvas_defaults->shape_corner_radius());
  shape_corner_radius->setSuffix(QStringLiteral(" px"));
  shape_corner_radius->setToolTip(tr("Rounded-corner radius for the rectangle tool (0 = sharp corners)"));
  configure_toolbar_spinbox(shape_corner_radius, 64);
  add_option_widget(shape_corner_radius, {CanvasTool::Rectangle});
  connect(shape_corner_radius, &QSpinBox::valueChanged, this, [this](int value) {
    current_shape_corner_radius_ = value;
    if (canvas_ != nullptr) {
      canvas_->set_shape_corner_radius(value);
      schedule_save_tool_settings();
    }
  });

  // Style / Width / Height for the shape draw tools, mirroring the marquee's
  // Normal / Fixed Ratio / Fixed Size options (session-only, like the marquee's).
  add_option_label(tr("Style:"), {CanvasTool::Rectangle, CanvasTool::Ellipse});
  auto* shape_style_combo = new QComboBox(toolbar);
  shape_style_combo->setObjectName(QStringLiteral("shapeStyleCombo"));
  shape_style_combo->addItems({tr("Normal"), tr("Fixed Ratio"), tr("Fixed Size")});
  shape_style_combo->setCurrentText(tr("Normal"));
  shape_style_combo->setFixedWidth(92);
  QPointer<QComboBox> shape_style_combo_pointer(shape_style_combo);
  register_retranslation([shape_style_combo_pointer] {
    if (shape_style_combo_pointer == nullptr || shape_style_combo_pointer->count() < 3) {
      return;
    }
    QSignalBlocker blocker(shape_style_combo_pointer);
    shape_style_combo_pointer->setItemText(0, QObject::tr("Normal"));
    shape_style_combo_pointer->setItemText(1, QObject::tr("Fixed Ratio"));
    shape_style_combo_pointer->setItemText(2, QObject::tr("Fixed Size"));
  });
  add_option_widget(shape_style_combo, {CanvasTool::Rectangle, CanvasTool::Ellipse});
  add_option_label(tr("Width:"), {CanvasTool::Rectangle, CanvasTool::Ellipse});
  auto* shape_fixed_width = new QSpinBox(toolbar);
  shape_fixed_width->setObjectName(QStringLiteral("shapeFixedWidthSpin"));
  shape_fixed_width->setRange(1, 30000);
  shape_fixed_width->setValue(has_active_document() ? document().width() : 1024);
  shape_fixed_width->setSuffix(QStringLiteral(" px"));
  configure_toolbar_spinbox(shape_fixed_width, 78);
  add_option_widget(shape_fixed_width, {CanvasTool::Rectangle, CanvasTool::Ellipse});
  add_option_label(tr("Height:"), {CanvasTool::Rectangle, CanvasTool::Ellipse});
  auto* shape_fixed_height = new QSpinBox(toolbar);
  shape_fixed_height->setObjectName(QStringLiteral("shapeFixedHeightSpin"));
  shape_fixed_height->setRange(1, 30000);
  shape_fixed_height->setValue(has_active_document() ? document().height() : 768);
  shape_fixed_height->setSuffix(QStringLiteral(" px"));
  configure_toolbar_spinbox(shape_fixed_height, 78);
  add_option_widget(shape_fixed_height, {CanvasTool::Rectangle, CanvasTool::Ellipse});
  const auto apply_shape_style_settings = [this, shape_style_combo, shape_fixed_width, shape_fixed_height] {
    switch (shape_style_combo->currentIndex()) {
      case 1:
        current_shape_style_ = CanvasWidget::MarqueeStyle::FixedRatio;
        break;
      case 2:
        current_shape_style_ = CanvasWidget::MarqueeStyle::FixedSize;
        break;
      default:
        current_shape_style_ = CanvasWidget::MarqueeStyle::Normal;
        break;
    }
    current_shape_width_ = shape_fixed_width->value();
    current_shape_height_ = shape_fixed_height->value();
    if (canvas_ != nullptr) {
      canvas_->set_shape_style(current_shape_style_);
      canvas_->set_shape_fixed_size(current_shape_width_, current_shape_height_);
    }
  };
  connect(shape_style_combo, &QComboBox::currentIndexChanged, this, [apply_shape_style_settings](int) {
    apply_shape_style_settings();
  });
  connect(shape_fixed_width, &QSpinBox::valueChanged, this, [apply_shape_style_settings](int) {
    apply_shape_style_settings();
  });
  connect(shape_fixed_height, &QSpinBox::valueChanged, this, [apply_shape_style_settings](int) {
    apply_shape_style_settings();
  });

  // Fill tool / Fill hotkey settings (independent of the brush; default 100% opacity, 0 softness).
  add_option_label(tr("Opacity:"), {CanvasTool::Fill});
  auto* fill_opacity = new QSpinBox(toolbar);
  fill_opacity->setObjectName(QStringLiteral("fillOpacitySpin"));
  fill_opacity->setRange(1, 100);
  fill_opacity->setValue(canvas_defaults->fill_opacity());
  fill_opacity->setSuffix(QStringLiteral("%"));
  configure_toolbar_spinbox(fill_opacity, 52);
  add_option_widget(fill_opacity, {CanvasTool::Fill});
  auto* fill_opacity_slider = new QSlider(Qt::Horizontal, toolbar);
  fill_opacity_slider->setObjectName(QStringLiteral("fillOpacitySlider"));
  fill_opacity_slider->setRange(1, 100);
  fill_opacity_slider->setValue(canvas_defaults->fill_opacity());
  fill_opacity_slider->setFixedWidth(120);
  fill_opacity_slider->setToolTip(tr("Fill opacity for the Fill tool and Fill shortcut"));
  add_option_widget(fill_opacity_slider, {CanvasTool::Fill});
  add_option_label(tr("Soft:"), {CanvasTool::Fill});
  auto* fill_softness = new QSpinBox(toolbar);
  fill_softness->setObjectName(QStringLiteral("fillSoftnessSpin"));
  fill_softness->setRange(0, 100);
  fill_softness->setValue(canvas_defaults->fill_softness());
  fill_softness->setSuffix(QStringLiteral("%"));
  configure_toolbar_spinbox(fill_softness, 52);
  add_option_widget(fill_softness, {CanvasTool::Fill});
  auto* fill_softness_slider = new QSlider(Qt::Horizontal, toolbar);
  fill_softness_slider->setObjectName(QStringLiteral("fillSoftnessSlider"));
  fill_softness_slider->setRange(0, 100);
  fill_softness_slider->setValue(canvas_defaults->fill_softness());
  fill_softness_slider->setFixedWidth(110);
  fill_softness_slider->setToolTip(tr("Soft edge feather for the Fill tool and Fill shortcut"));
  add_option_widget(fill_softness_slider, {CanvasTool::Fill});
  connect(fill_opacity, &QSpinBox::valueChanged, fill_opacity_slider, &QSlider::setValue);
  connect(fill_opacity_slider, &QSlider::valueChanged, fill_opacity, &QSpinBox::setValue);
  connect(fill_opacity, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_fill_opacity(value);
      schedule_save_tool_settings();
    }
  });
  connect(fill_softness, &QSpinBox::valueChanged, fill_softness_slider, &QSlider::setValue);
  connect(fill_softness_slider, &QSlider::valueChanged, fill_softness, &QSpinBox::setValue);
  connect(fill_softness, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_fill_softness(value);
      schedule_save_tool_settings();
    }
  });

  add_option_label(tr("Font:"), {CanvasTool::Text});
  text_font_combo_ = new FontPickerCombo(toolbar);
  text_font_combo_->setObjectName(QStringLiteral("textFontCombo"));
  text_font_combo_->setCurrentFont(font());
  text_font_combo_->setFixedWidth(210);
  add_option_widget(text_font_combo_, {CanvasTool::Text});
  add_option_label(tr("Size:"), {CanvasTool::Text});
  text_size_spin_ = new QDoubleSpinBox(toolbar);
  text_size_spin_->setObjectName(QStringLiteral("textSizeSpin"));
  text_size_spin_->setDecimals(3);
  text_size_spin_->setRange(0.01, 10000.0);
  text_size_spin_->setSingleStep(0.25);
  // 48 px at the default document's 72 ppi = 48 pt (startup builds the bar with
  // no document open).
  text_size_spin_->setValue(has_active_document() ? text_pixels_to_points(48, document()) : 48.0);
  text_size_spin_->setSuffix(tr(" pt"));
  configure_toolbar_spinbox(text_size_spin_, 74);
  add_option_widget(text_size_spin_, {CanvasTool::Text});
  text_bold_button_ = new QPushButton(tr("B"), toolbar);
  text_bold_button_->setObjectName(QStringLiteral("textBoldButton"));
  text_bold_button_->setCheckable(true);
  text_bold_button_->setToolTip(tr("Bold"));
  text_bold_button_->setFixedSize(30, 26);
  QFont bold_button_font = text_bold_button_->font();
  bold_button_font.setBold(true);
  text_bold_button_->setFont(bold_button_font);
  add_option_widget(text_bold_button_, {CanvasTool::Text});
  text_italic_button_ = new QPushButton(tr("I"), toolbar);
  text_italic_button_->setObjectName(QStringLiteral("textItalicButton"));
  text_italic_button_->setCheckable(true);
  text_italic_button_->setToolTip(tr("Italic"));
  text_italic_button_->setFixedSize(30, 26);
  QFont italic_button_font = text_italic_button_->font();
  italic_button_font.setItalic(true);
  text_italic_button_->setFont(italic_button_font);
  add_option_widget(text_italic_button_, {CanvasTool::Text});
  add_option_label(tr("Smoothing:"), {CanvasTool::Text});
  text_smoothing_combo_ = new QComboBox(toolbar);
  text_smoothing_combo_->setObjectName(QStringLiteral("textSmoothingCombo"));
  text_smoothing_combo_->setToolTip(tr("Text smoothing"));
  text_smoothing_combo_->addItem(tr("None"), 0);
  text_smoothing_combo_->addItem(tr("Sharp"), 4);
  text_smoothing_combo_->addItem(tr("Crisp"), 2);
  text_smoothing_combo_->addItem(tr("Strong"), 1);
  text_smoothing_combo_->addItem(tr("Smooth"), 3);
  text_smoothing_combo_->addItem(tr("Windows LCD"), 5);
  text_smoothing_combo_->addItem(tr("Windows"), 6);
  text_smoothing_combo_->setFixedWidth(116);
  set_text_smoothing_combo_value(
      text_smoothing_combo_,
      app_settings().value(QStringLiteral("tools/textSmoothing"), kDefaultTextAntiAlias).toInt());
  add_option_widget(text_smoothing_combo_, {CanvasTool::Text});
  add_option_label(tr("Color:"), {CanvasTool::Text});
  text_color_button_ = new QPushButton(tr("T"), toolbar);
  text_color_button_->setObjectName(QStringLiteral("textColorButton"));
  text_color_button_->setToolTip(tr("Text color"));
  text_color_button_->setFixedSize(30, 26);
  add_option_widget(text_color_button_, {CanvasTool::Text});
  add_option_label(tr("Align:"), {CanvasTool::Text});
  auto* text_alignment_group = new QButtonGroup(toolbar);
  text_alignment_group->setExclusive(true);
  text_align_left_button_ = new QPushButton(tr("L"), toolbar);
  text_align_left_button_->setObjectName(QStringLiteral("textAlignLeftButton"));
  text_align_left_button_->setCheckable(true);
  text_align_left_button_->setChecked(true);
  text_align_left_button_->setToolTip(tr("Align Left"));
  text_align_left_button_->setFixedSize(30, 26);
  text_alignment_group->addButton(text_align_left_button_);
  add_option_widget(text_align_left_button_, {CanvasTool::Text});
  text_align_center_button_ = new QPushButton(tr("C"), toolbar);
  text_align_center_button_->setObjectName(QStringLiteral("textAlignCenterButton"));
  text_align_center_button_->setCheckable(true);
  text_align_center_button_->setToolTip(tr("Align Center"));
  text_align_center_button_->setFixedSize(30, 26);
  text_alignment_group->addButton(text_align_center_button_);
  add_option_widget(text_align_center_button_, {CanvasTool::Text});
  text_align_right_button_ = new QPushButton(tr("R"), toolbar);
  text_align_right_button_->setObjectName(QStringLiteral("textAlignRightButton"));
  text_align_right_button_->setCheckable(true);
  text_align_right_button_->setToolTip(tr("Align Right"));
  text_align_right_button_->setFixedSize(30, 26);
  text_alignment_group->addButton(text_align_right_button_);
  add_option_widget(text_align_right_button_, {CanvasTool::Text});
  text_warp_button_ = new QPushButton(tr("Warp..."), toolbar);
  text_warp_button_->setObjectName(QStringLiteral("textWarpButton"));
  text_warp_button_->setToolTip(tr("Warp Text (Photoshop-style styles: arc, flag, fish, ...)"));
  add_option_widget(text_warp_button_, {CanvasTool::Text});
  // Character panel: works on the LIVE editor session, so Qt::NoFocus is load-bearing here
  // exactly like the session apply/cancel buttons (a focus-taking button would fire the
  // editor's focus-loss auto-commit on mouse press).
  text_character_button_ = new QPushButton(tr("Character..."), toolbar);
  text_character_button_->setObjectName(QStringLiteral("textCharacterButton"));
  text_character_button_->setToolTip(tr("Character panel (leading, tracking, glyph scales)"));
  text_character_button_->setFocusPolicy(Qt::NoFocus);
  add_option_widget(text_character_button_, {CanvasTool::Text});
  connect(text_character_button_, &QPushButton::clicked, this, [this] { open_text_character_dialog(); });
  connect(text_font_combo_, &QFontComboBox::currentFontChanged, this,
          [this](const QFont&) { apply_text_family_to_active_editor(); });
  connect(text_size_spin_, &QDoubleSpinBox::valueChanged, this,
          [this](double) {
    apply_text_size_to_active_editor();
    refresh_document_info();
  });
  connect(text_bold_button_, &QPushButton::toggled, this,
          [this](bool) { apply_text_bold_to_active_editor(); });
  connect(text_italic_button_, &QPushButton::toggled, this,
          [this](bool) { apply_text_italic_to_active_editor(); });
  connect(text_smoothing_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this](int) {
            apply_text_smoothing_to_active_editor();
            save_tool_settings();
            refresh_document_info();
          });
  connect(text_color_button_, &QPushButton::clicked, this, [this] { choose_text_color(); });
  connect(text_align_left_button_, &QPushButton::clicked, this,
          [this] { apply_text_alignment_to_active_editor(Qt::AlignLeft); });
  connect(text_align_center_button_, &QPushButton::clicked, this,
          [this] { apply_text_alignment_to_active_editor(Qt::AlignHCenter); });
  connect(text_align_right_button_, &QPushButton::clicked, this,
          [this] { apply_text_alignment_to_active_editor(Qt::AlignRight); });
  connect(text_warp_button_, &QPushButton::clicked, this, [this] { request_warp_text_dialog(); });
  // Session apply/cancel, shown only while an inline text editor is open (the
  // text controls above stay visible too -- they apply live to the editor, so
  // unlike a transform session the bar keeps them).  Qt::NoFocus is load-bearing:
  // a focus-taking button would fire the editor's focus-loss auto-commit on
  // mouse press, committing the text before a Cancel click could cancel it.
  text_apply_button_ = new QPushButton(toolbar);
  text_apply_button_->setObjectName(QStringLiteral("textApplyButton"));
  text_apply_button_->setIcon(simple_icon(QStringLiteral("ok"), QColor(160, 220, 165)));
  text_apply_button_->setToolTip(tr("Apply text edit"));
  text_apply_button_->setFixedWidth(30);
  text_apply_button_->setIconSize(QSize(20, 20));
  text_apply_button_->setProperty("optionsSessionButton", true);
  text_apply_button_->setFocusPolicy(Qt::NoFocus);
  options_flow->addWidget(text_apply_button_);
  text_cancel_button_ = new QPushButton(toolbar);
  text_cancel_button_->setObjectName(QStringLiteral("textCancelButton"));
  text_cancel_button_->setIcon(simple_icon(QStringLiteral("clear"), QColor(255, 150, 150)));
  text_cancel_button_->setToolTip(tr("Cancel text edit"));
  text_cancel_button_->setFixedWidth(30);
  text_cancel_button_->setIconSize(QSize(20, 20));
  text_cancel_button_->setProperty("optionsSessionButton", true);
  text_cancel_button_->setFocusPolicy(Qt::NoFocus);
  options_flow->addWidget(text_cancel_button_);
  connect(text_apply_button_, &QPushButton::clicked, this, [this] { commit_active_text_editor(); });
  connect(text_cancel_button_, &QPushButton::clicked, this, [this] { cancel_active_text_editor(); });

  window_menu->addAction(tool_palette->toggleViewAction());
  window_menu->addAction(toolbar->toggleViewAction());
  // The "Options" toggle would otherwise be captured by macOS's menu-text heuristic
  // (any menubar action containing "options" gets relocated as a Preferences item).
  tool_palette->toggleViewAction()->setMenuRole(QAction::NoRole);
  toolbar->toggleViewAction()->setMenuRole(QAction::NoRole);
  bind_widget_text(tool_palette, "Tool Palette");
  bind_widget_text(toolbar, "Options");
  const std::vector<std::pair<QAction*, const char*>> translated_actions = {
      {new_action, "&New"},
      {open_action, "&Open..."},
      {recent_files_menu_->menuAction(), "Open &Recent File"},
      {recent_folders_menu_->menuAction(), "Open Recent &Folder"},
      {save_action, "&Save"},
      {save_as_action, "Save &As..."},
      {export_flat_action, "Export &Flat Image..."},
      {page_setup_action, "Page Set&up..."},
      {print_action, "&Print..."},
      {close_action, "&Close"},
      {close_all_action, "Close &All"},
      {preferences_action, "&Preferences..."},
      {quit_action, "&Quit"},
      {undo_action_, "&Undo"},
      {redo_action_, "&Redo"},
      {cut_action, "Cu&t"},
      {copy_action, "&Copy"},
      {copy_merged_action, "Copy Merged"},
      {paste_action, "&Paste"},
      {transform_action, "Free &Transform..."},
      {select_all_action, "Select &All"},
      {clear_selection_action, "&Clear Selection"},
      {reselect_action, "&Reselect"},
      {inverse_selection_action, "&Inverse"},
      {quick_mask_action_, "Edit in &Quick Mask Mode"},
      {grow_selection_action, "&Grow"},
      {similar_selection_action, "Simi&lar"},
      {expand_selection_action, "&Expand..."},
      {contract_selection_action, "Con&tract..."},
      {border_selection_action, "&Border..."},
      {layer_transparency_action, "Load Layer &Transparency"},
      {stroke_selection_action, "&Stroke Selection"},
      {define_brush_tip_action, "Define Brush Tip from Selection"},
      {add_layer_action, "&New Layer"},
      {add_folder_action, "New &Folder"},
      {new_adjustment_layer_menu->menuAction(), "New &Adjustment Layer"},
      {layer_via_copy_action, "Layer Via &Copy"},
      {layer_via_cut_action, "Layer Via Cu&t"},
      {add_mask_action, "Add Layer &Mask"},
      {edit_layer_mask_action_, "&Edit Layer Mask"},
      {mask_overlay_action_, "Show Mask &Overlay"},
      {delete_layer_mask_action_, "&Delete Layer Mask"},
      {link_layer_mask_action_, "Link Layer &Mask"},
      {disable_layer_mask_action_, "&Disable Layer Mask"},
      {invert_layer_mask_action_, "&Invert Layer Mask"},
      {apply_layer_mask_action_, "&Apply Layer Mask"},
      {edit_adjustment_action, "&Edit Adjustment..."},
      {layer_blending_options_action_, "Edit Layer &Styles..."},
      {layer_copy_style_action_, "Copy Layer Style"},
      {layer_paste_style_action_, "Paste Layer Style"},
      {layer_delete_style_action_, "Delete Layer Style"},
      {layer_rasterize_action_, "Rasterize"},
      {layer_rasterize_layer_style_action_, "Rasterize (including layer style)"},
      {duplicate_layer_action, "&Duplicate Layer"},
      {merge_visible_action, "Merge &Visible to New Layer"},
      {merge_down_action, "Merge &Down"},
      {rename_layer_action, "&Rename Layer..."},
      {delete_layer_action, "&Delete Layer"},
      {fill_layer_action, "&Fill Layer / Selection"},
      {fill_background_action, "Fill With &Background Color"},
      {clear_layer_action, "&Clear Layer / Selection"},
      {flip_h_action, "Flip Layer &Horizontal"},
      {flip_v_action, "Flip Layer &Vertical"},
      {layer_up_action, "Move Layer &Up"},
      {layer_down_action, "Move Layer &Down"},
      {adjustments_menu->menuAction(), "&Adjustments"},
      {levels_action, "&Levels..."},
      {curves_action, "&Curves..."},
      {hue_saturation_action, "&Hue/Saturation..."},
      {color_balance_action, "Color &Balance..."},
      {image_size_action, "&Image Size..."},
      {canvas_size_action, "&Canvas Size..."},
      {crop_action, "&Crop to Selection"},
      {rotate_cw_action, "Rotate 90 &Clockwise"},
      {rotate_ccw_action, "Rotate 90 Counterclockwise"},
      {scan_legacy_plugins_action, "&Scan Legacy Photoshop Plug-ins..."},
      {legacy_plugins_menu_->menuAction(), "Legacy Photoshop Plug-ins"},
      {zoom_in, "Zoom &In"},
      {zoom_out, "Zoom &Out"},
      {fit_on_screen, "&Fit on Screen"},
      {zoom_reset, "&Actual Pixels"},
      {selection_edges_action, "Show Selection &Edges"},
      {view_rulers_action_, "&Rulers"},
      {view_grid_action_, "&Grid"},
      {view_guides_action_, "&Guides"},
      {view_snap_action_, "&Snap"},
      {view_lock_guides_action_, "Lock Guides"},
      {snap_to_menu->menuAction(), "Snap &To"},
      {view_snap_guides_action_, "Guides"},
      {view_snap_grid_action_, "Grid"},
      {view_snap_document_action_, "Document Bounds and Center"},
      {view_snap_layers_action_, "Layer Bounds and Centers"},
      {view_snap_selection_action_, "Selection Bounds and Center"},
      {guides_menu->menuAction(), "Guide Operations"},
      {new_guide_action, "New Guide..."},
      {new_guide_layout_action, "New Guide Layout..."},
      {clear_selected_guides_action, "Clear Selected Guides"},
      {clear_guides_action, "Clear Guides"},
      {screen_size_menu->menuAction(), "Set Screen Size"},
      {force_refresh_action, "Force Refresh"},
      {language_english_action_, "&English"},
      {about_action, "&About Patchy"},
      {default_colors_action, "Default Colors"},
      {swap_colors_action, "Swap Colors"},
      {brush_smaller_action, "Brush Smaller"},
      {brush_larger_action, "Brush Larger"},
      {brush_much_smaller_action, "Brush Much Smaller"},
      {brush_much_larger_action, "Brush Much Larger"},
  };
  for (const auto& [action, source] : translated_actions) {
    bind_action_text(action, source);
    refresh_action_tooltip(action);
  }
  const std::vector<std::pair<QObject*, const char*>> translated_widgets = {
      {primary_color_button_, "FG"},
      {secondary_color_button_, "BG"},
      {move_auto_select_check_, "Auto-Select"},
      {move_show_transform_controls_check_, "Show Transform Controls"},
      {clone_aligned_check_, "Aligned"},
      {gradient_reverse_check_, "Reverse"},
      {gradient_edit_stops_button_, "Edit Stops..."},
      {wand_contiguous_check_, "Contiguous"},
      {wand_sample_all_layers_check_, "Sample All Layers"},
      {quick_select_sample_all_layers_check_, "Sample All Layers"},
      {quick_select_enhance_edge_check_, "Enhance Edge"},
      {fill_shapes, "Fill"},
      {text_bold_button_, "B"},
      {text_italic_button_, "I"},
      {text_color_button_, "T"},
      {text_align_left_button_, "L"},
      {text_align_center_button_, "C"},
      {text_align_right_button_, "R"},
  };
  for (const auto& [widget, source] : translated_widgets) {
    bind_widget_text(widget, source);
  }
  retranslate_brush_preset_combo();
  for (auto* action : menuBar()->actions()) {
    hide_menu_action_icons(action->menu());
  }
  refresh_options_bar();
  refresh_color_buttons();

  update_undo_redo_actions();
}

void MainWindow::sync_tool_option_controls_from_canvas() {
  if (canvas_ == nullptr) {
    return;
  }
  // Re-reads the options-bar controls that create_actions initialized from the
  // defaults-donor canvas. Runs after the deferred startup load_tool_settings()
  // so the bar shows the stored settings the first document's canvas now holds.
  const auto set_spin_value = [this](const QString& name, int value) {
    if (auto* spin = findChild<QSpinBox*>(name); spin != nullptr) {
      const QSignalBlocker blocker(spin);
      spin->setValue(value);
    }
  };
  const auto set_slider_value = [this](const QString& name, int value) {
    if (auto* slider = findChild<QSlider*>(name); slider != nullptr) {
      const QSignalBlocker blocker(slider);
      slider->setValue(value);
    }
  };
  const auto set_checked = [](QCheckBox* check, bool value) {
    if (check != nullptr) {
      const QSignalBlocker blocker(check);
      check->setChecked(value);
    }
  };
  set_checked(move_auto_select_check_, canvas_->auto_select_layer());
  set_checked(move_show_transform_controls_check_, canvas_->show_transform_controls());
  set_checked(clone_aligned_check_, canvas_->clone_aligned());
  set_checked(wand_contiguous_check_, canvas_->wand_contiguous());
  set_checked(wand_sample_all_layers_check_, canvas_->wand_sample_all_layers());
  set_checked(quick_select_sample_all_layers_check_, canvas_->quick_select_sample_all_layers());
  set_checked(quick_select_enhance_edge_check_, canvas_->quick_select_enhance_edge());
  set_spin_value(QStringLiteral("wandToleranceSpin"), canvas_->wand_tolerance());
  set_spin_value(QStringLiteral("quickSelectSizeSpin"), canvas_->quick_select_size());
  set_slider_value(QStringLiteral("quickSelectSizeSlider"), canvas_->quick_select_size());
  set_spin_value(QStringLiteral("magneticLassoWidthSpin"), canvas_->magnetic_lasso_width());
  set_spin_value(QStringLiteral("magneticLassoContrastSpin"), canvas_->magnetic_lasso_edge_contrast());
  set_spin_value(QStringLiteral("magneticLassoFrequencySpin"), canvas_->magnetic_lasso_frequency());
  set_spin_value(QStringLiteral("shapeCornerRadiusSpin"), canvas_->shape_corner_radius());
  set_spin_value(QStringLiteral("fillOpacitySpin"), canvas_->fill_opacity());
  set_slider_value(QStringLiteral("fillOpacitySlider"), canvas_->fill_opacity());
  set_spin_value(QStringLiteral("fillSoftnessSpin"), canvas_->fill_softness());
  set_slider_value(QStringLiteral("fillSoftnessSlider"), canvas_->fill_softness());
  refresh_gradient_controls_from_canvas();
  sync_brush_controls_from_canvas();
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

void MainWindow::register_retranslation(std::function<void()> callback) {
  if (!callback) {
    return;
  }
  callback();
  retranslation_callbacks_.push_back(std::move(callback));
}

void MainWindow::retranslate_bound_children() {
  apply_bound_translation(this);
  const auto children = findChildren<QObject*>();
  for (auto* child : children) {
    apply_bound_translation(child);
  }
}

void MainWindow::retranslate_blend_combo() {
  if (blend_combo_ == nullptr) {
    return;
  }
  QSignalBlocker blocker(blend_combo_);
  for (int index = 0; index < blend_combo_->count(); ++index) {
    blend_combo_->setItemText(index, blend_mode_name(static_cast<BlendMode>(blend_combo_->itemData(index).toInt())));
  }
}

void MainWindow::retranslate_brush_preset_combo() {
  if (brush_preset_combo_ == nullptr) {
    return;
  }
  QSignalBlocker blocker(brush_preset_combo_);
  for (int index = 0; index < brush_preset_combo_->count(); ++index) {
    if (const auto* preset = find_brush_preset(brush_preset_combo_->itemData(index).toString()); preset != nullptr) {
      brush_preset_combo_->setItemText(index, brush_preset_display_name(*preset));
    }
  }
}

void MainWindow::refresh_language_actions() {
  const auto current = LocalizationManager::instance().current_language();
  if (language_english_action_ != nullptr) {
    language_english_action_->setChecked(current == QStringLiteral("en"));
  }
  if (language_japanese_action_ != nullptr) {
    language_japanese_action_->setChecked(current == QStringLiteral("ja"));
  }
}

void MainWindow::retranslate_ui() {
  retranslate_bound_children();
  if (menuBar() != nullptr) {
    for (auto* action : menuBar()->actions()) {
      apply_bound_translation(action);
    }
  }
  for (const auto& callback : retranslation_callbacks_) {
    callback();
  }
  refresh_language_actions();
  retranslate_blend_combo();
  retranslate_brush_preset_combo();
  if (text_size_spin_ != nullptr) {
    text_size_spin_->setSuffix(tr(" pt"));
  }
  const auto actions = findChildren<QAction*>();
  for (auto* action : actions) {
    refresh_action_tooltip(action);
  }
  rebuild_recent_files_menu();
  rebuild_recent_folders_menu();
  refresh_layer_list();
  refresh_layer_controls();
  refresh_channel_panel();
  refresh_document_info();
  refresh_color_buttons();
  refresh_text_color_button();
  update_undo_redo_actions();
  update_document_action_state();
  if (statusBar() != nullptr) {
    statusBar()->showMessage(tr("Ready"));
  }
}

}  // namespace patchy::ui
