// MainWindow window-chrome implementation, split out of main_window.cpp: the frameless-
// window machinery (Win32 WM_NCCALCSIZE/WM_NCHITTEST native frame, resize borders and
// cursors, maximize/restore behavior, geometry persistence). This file deliberately
// concentrates the Q_OS_WIN-specific window code so a future Linux/macOS port has one
// place to add platform equivalents; keep new platform-specific window code here.
// Pure function moves from main_window.cpp; behavior must stay identical.

#include "ui/main_window.hpp"

#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "core/layer_tree.hpp"
#include "core/palette_presets.hpp"
#include "core/pixel_tools.hpp"
#include "formats/palette_io.hpp"
#include "filters/builtin_filters.hpp"
#include "formats/bmp_document_io.hpp"
#include "plugins/legacy_photoshop_adapter.hpp"
#include "psd/psd_document_io.hpp"
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
#include "ui/filter_workflows.hpp"
#include "ui/gradient_stops_editor.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/hotkey_editor.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/color_panel.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/localization.hpp"
#include "ui/palette_convert_dialog.hpp"
#include "ui/palette_panel.hpp"
#include "ui/print_dialog.hpp"
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
#include <QButtonGroup>
#include <QByteArray>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QCursor>
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
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFileDialog>
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

constexpr int kWindowResizeBorder = 10;

Qt::Edges resize_edges_for_window_position(QSize window_size, QPoint position) {
  Qt::Edges edges;
  if (window_size.isEmpty()) {
    return edges;
  }
  if (position.x() >= 0 && position.x() < kWindowResizeBorder) {
    edges |= Qt::LeftEdge;
  } else if (position.x() < window_size.width() && position.x() >= window_size.width() - kWindowResizeBorder) {
    edges |= Qt::RightEdge;
  }
  if (position.y() >= 0 && position.y() < kWindowResizeBorder) {
    edges |= Qt::TopEdge;
  } else if (position.y() < window_size.height() && position.y() >= window_size.height() - kWindowResizeBorder) {
    edges |= Qt::BottomEdge;
  }
  return edges;
}

bool widget_is_or_contains_scroll_bar(const QWidget* widget) {
  for (auto* current = widget; current != nullptr; current = current->parentWidget()) {
    if (qobject_cast<const QScrollBar*>(current) != nullptr) {
      return true;
    }
  }
  return false;
}

QWidget* deepest_child_at(QWidget* root, QPoint position) {
  if (root == nullptr || !root->rect().contains(position)) {
    return nullptr;
  }

  auto* parent = root;
  auto parent_position = position;
  auto* child = parent->childAt(parent_position);
  while (child != nullptr) {
    const auto child_position = child->mapFrom(parent, parent_position);
    auto* next = child->childAt(child_position);
    if (next == nullptr || next == child) {
      return child;
    }
    parent = child;
    parent_position = child_position;
    child = next;
  }
  return nullptr;
}

bool visible_scroll_bar_contains_global_point(QWidget* root, QPoint global_position) {
  if (root == nullptr) {
    return false;
  }
  for (auto* scroll_bar : root->findChildren<QScrollBar*>()) {
    if (scroll_bar == nullptr || !scroll_bar->isVisibleTo(root) || scroll_bar->window() != root->window()) {
      continue;
    }
    const QRect global_rect(scroll_bar->mapToGlobal(QPoint(0, 0)), scroll_bar->size());
    if (global_rect.contains(global_position)) {
      return true;
    }
  }
  return false;
}

bool window_resize_hit_targets_scroll_bar(QWidget* window, QPoint global_position) {
  if (window == nullptr) {
    return false;
  }
  if (visible_scroll_bar_contains_global_point(window, global_position)) {
    return true;
  }
  return widget_is_or_contains_scroll_bar(deepest_child_at(window, window->mapFromGlobal(global_position)));
}

Qt::CursorShape resize_cursor_for_edges(Qt::Edges edges) {
  const bool left = edges.testFlag(Qt::LeftEdge);
  const bool right = edges.testFlag(Qt::RightEdge);
  const bool top = edges.testFlag(Qt::TopEdge);
  const bool bottom = edges.testFlag(Qt::BottomEdge);
  if ((top && left) || (bottom && right)) {
    return Qt::SizeFDiagCursor;
  }
  if ((top && right) || (bottom && left)) {
    return Qt::SizeBDiagCursor;
  }
  if (left || right) {
    return Qt::SizeHorCursor;
  }
  if (top || bottom) {
    return Qt::SizeVerCursor;
  }
  return Qt::ArrowCursor;
}

#ifdef Q_OS_WIN
void apply_windows_frameless_resize_style(WId window_id) {
  auto* hwnd = reinterpret_cast<HWND>(window_id);
  if (hwnd == nullptr) {
    return;
  }

  const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
  if (style == 0) {
    return;
  }
  const LONG_PTR next_style = (style | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX) & ~WS_CAPTION;
  if (next_style != style) {
    SetWindowLongPtrW(hwnd, GWL_STYLE, next_style);
  }
  SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

  // Windows 11 draws a 1px DWM "visible border" around WS_THICKFRAME windows that turns
  // light/white when the window is deactivated. COLOR_NONE removes it in every state; our
  // own dark QSS frame still draws inside the client area. The call is a harmless no-op on
  // pre-22000 builds (it just returns a failure HRESULT), so we call it unconditionally.
  constexpr DWORD kDwmwaBorderColor = 34;        // DWMWA_BORDER_COLOR (Win11 22000+)
  constexpr COLORREF kDwmColorNone = 0xFFFFFFFE; // DWMWA_COLOR_NONE
  DwmSetWindowAttribute(hwnd, kDwmwaBorderColor, &kDwmColorNone, sizeof(kDwmColorNone));
}

void apply_windows_pen_feedback_suppression(WId window_id) {
  auto* hwnd = reinterpret_cast<HWND>(window_id);
  if (hwnd == nullptr) {
    return;
  }
  // Disable the Windows pen "press and hold" ring (and the related tap/barrel
  // feedback and flicks) so drawing does not show the distracting circle. This
  // is the standard MicrosoftTabletPenServiceProperty technique used by drawing
  // applications.
  const DWORD_PTR flags = TABLET_DISABLE_PRESSANDHOLD | TABLET_DISABLE_PENTAPFEEDBACK |
                          TABLET_DISABLE_PENBARRELFEEDBACK | TABLET_DISABLE_FLICKS;
  const ATOM atom = ::GlobalAddAtom(MICROSOFT_TABLETPENSERVICE_PROPERTY);
  ::SetProp(hwnd, MICROSOFT_TABLETPENSERVICE_PROPERTY, reinterpret_cast<HANDLE>(flags));
  if (atom != 0) {
    ::GlobalDeleteAtom(atom);
  }
}
#endif

}  // namespace

bool MainWindow::handle_window_resize_event(QObject* watched, QEvent* event) {
  if (isMaximized() || isFullScreen()) {
    if (!chrome_resizing_) {
      clear_window_resize_cursor();
    }
    return false;
  }

  if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseButtonRelease &&
      event->type() != QEvent::MouseMove) {
    return false;
  }

  auto* mouse_event = static_cast<QMouseEvent*>(event);
  if (auto* widget = qobject_cast<QWidget*>(watched);
      widget_is_or_contains_scroll_bar(widget) ||
      window_resize_hit_targets_scroll_bar(this, mouse_event->globalPosition().toPoint())) {
    if (chrome_resizing_ && event->type() == QEvent::MouseButtonRelease && mouse_event->button() == Qt::LeftButton) {
      chrome_resizing_ = false;
      chrome_resize_edges_ = Qt::Edges{};
      clear_window_resize_cursor();
    }
    return false;
  }

  if (chrome_resizing_) {
    if (event->type() == QEvent::MouseMove && (mouse_event->buttons() & Qt::LeftButton) != 0) {
      resize_window_from_global_point(mouse_event->globalPosition().toPoint());
      mouse_event->accept();
      return true;
    }
    if (event->type() == QEvent::MouseButtonRelease && mouse_event->button() == Qt::LeftButton) {
      resize_window_from_global_point(mouse_event->globalPosition().toPoint());
      chrome_resizing_ = false;
      chrome_resize_edges_ = Qt::Edges{};
      releaseMouse();
      clear_window_resize_cursor();
      mouse_event->accept();
      return true;
    }
    return false;
  }

  auto* widget = qobject_cast<QWidget*>(watched);
  if (widget == nullptr || widget->window() != this) {
    return false;
  }
  if (widget_is_or_contains_scroll_bar(widget)) {
    clear_window_resize_cursor();
    return false;
  }

  const auto edges = resize_edges_for_window_position(size(), mapFromGlobal(mouse_event->globalPosition().toPoint()));
  if (event->type() == QEvent::MouseMove && mouse_event->buttons() == Qt::NoButton) {
    update_window_resize_cursor(edges);
    return false;
  }
  if (event->type() != QEvent::MouseButtonPress || mouse_event->button() != Qt::LeftButton ||
      edges == Qt::Edges{}) {
    return false;
  }

  chrome_resize_edges_ = edges;
  chrome_resize_start_global_ = mouse_event->globalPosition().toPoint();
  chrome_resize_start_geometry_ = geometry();
  chrome_resizing_ = true;
  chrome_dragging_ = false;
  update_window_resize_cursor(edges);
  grabMouse();
  mouse_event->accept();
  return true;
}

void MainWindow::update_window_resize_cursor(Qt::Edges edges) {
  if (edges == Qt::Edges{}) {
    clear_window_resize_cursor();
    return;
  }

  const QCursor cursor(resize_cursor_for_edges(edges));
  if (chrome_resize_cursor_active_) {
    QApplication::changeOverrideCursor(cursor);
  } else {
    QApplication::setOverrideCursor(cursor);
    chrome_resize_cursor_active_ = true;
  }
}

void MainWindow::clear_window_resize_cursor() {
  if (!chrome_resize_cursor_active_) {
    return;
  }
  QApplication::restoreOverrideCursor();
  chrome_resize_cursor_active_ = false;
}

void MainWindow::resize_window_from_global_point(QPoint global_position) {
  QRect next = chrome_resize_start_geometry_;
  const QPoint delta = global_position - chrome_resize_start_global_;
  const QSize minimum = minimumSize().expandedTo(minimumSizeHint());
  const QSize maximum = maximumSize();

  if (chrome_resize_edges_.testFlag(Qt::LeftEdge)) {
    int left = chrome_resize_start_geometry_.left() + delta.x();
    left = std::min(left, chrome_resize_start_geometry_.right() - minimum.width() + 1);
    if (maximum.width() < QWIDGETSIZE_MAX) {
      left = std::max(left, chrome_resize_start_geometry_.right() - maximum.width() + 1);
    }
    next.setLeft(left);
  } else if (chrome_resize_edges_.testFlag(Qt::RightEdge)) {
    int right = chrome_resize_start_geometry_.right() + delta.x();
    right = std::max(right, chrome_resize_start_geometry_.left() + minimum.width() - 1);
    if (maximum.width() < QWIDGETSIZE_MAX) {
      right = std::min(right, chrome_resize_start_geometry_.left() + maximum.width() - 1);
    }
    next.setRight(right);
  }

  if (chrome_resize_edges_.testFlag(Qt::TopEdge)) {
    int top = chrome_resize_start_geometry_.top() + delta.y();
    top = std::min(top, chrome_resize_start_geometry_.bottom() - minimum.height() + 1);
    if (maximum.height() < QWIDGETSIZE_MAX) {
      top = std::max(top, chrome_resize_start_geometry_.bottom() - maximum.height() + 1);
    }
    next.setTop(top);
  } else if (chrome_resize_edges_.testFlag(Qt::BottomEdge)) {
    int bottom = chrome_resize_start_geometry_.bottom() + delta.y();
    bottom = std::max(bottom, chrome_resize_start_geometry_.top() + minimum.height() - 1);
    if (maximum.height() < QWIDGETSIZE_MAX) {
      bottom = std::min(bottom, chrome_resize_start_geometry_.top() + maximum.height() - 1);
    }
    next.setBottom(bottom);
  }

  if (next.isValid() && next != geometry()) {
    setGeometry(next);
  }
}

void MainWindow::set_window_screen_size(QSize physical_size) {
  if (isMaximized() || isFullScreen()) {
    showNormal();
  }
#ifdef Q_OS_WIN
  // Size the window in physical pixels so screen recordings capture exactly the
  // advertised resolution regardless of display scaling. The window is
  // frameless, so the outer window rect already includes the custom title bar
  // and resize borders.
  auto* hwnd = reinterpret_cast<HWND>(winId());
  if (hwnd != nullptr) {
    RECT window_rect{};
    if (GetWindowRect(hwnd, &window_rect) == 0) {
      return;
    }
    int x = window_rect.left;
    int y = window_rect.top;
    MONITORINFO monitor_info{};
    monitor_info.cbSize = sizeof(monitor_info);
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (monitor != nullptr && GetMonitorInfoW(monitor, &monitor_info) != 0) {
      const RECT& screen = monitor_info.rcMonitor;
      x = std::clamp(x, static_cast<int>(screen.left),
                     std::max(static_cast<int>(screen.left), static_cast<int>(screen.right) - physical_size.width()));
      y = std::clamp(y, static_cast<int>(screen.top),
                     std::max(static_cast<int>(screen.top), static_cast<int>(screen.bottom) - physical_size.height()));
    }
    SetWindowPos(hwnd, nullptr, x, y, physical_size.width(), physical_size.height(), SWP_NOZORDER | SWP_NOACTIVATE);
    return;
  }
#endif
  const qreal ratio = devicePixelRatioF();
  resize(qRound(physical_size.width() / ratio), qRound(physical_size.height() / ratio));
}

bool MainWindow::nativeEvent(const QByteArray& event_type, void* message, qintptr* result) {
#ifdef Q_OS_WIN
  if (message != nullptr && result != nullptr) {
    auto* nc_message = static_cast<MSG*>(message);
    if (nc_message->message == WM_NCACTIVATE) {
      // lParam == -1 tells DefWindowProc not to repaint the non-client border to reflect
      // the active-state change, preventing the white inactive-border flash. Return TRUE so
      // the window still activates/deactivates normally (taskbar, focus). This runs in every
      // window state (including maximized/fullscreen), where the border also appears.
      *result = DefWindowProcW(nc_message->hwnd, WM_NCACTIVATE, nc_message->wParam, -1);
      return true;
    }
    if (nc_message->message == WM_GETMINMAXINFO) {
      // Qt maximizes this frameless window itself (plain resize to the work area, never a
      // native zoom), but OS-initiated maximizes — drag-to-top snap, Win+Up — go through
      // DefWindowProc, whose default for a caption-less window is the full monitor. That
      // hides the taskbar and, combined with the WM_NCCALCSIZE handling below, exposed a
      // white non-client ring around the screen. Publish the work area as the maximize
      // geometry so the OS path lands exactly where Qt's own maximize does. ptMaxPosition
      // is relative to the monitor the window maximizes on. Deliberately not consumed:
      // Qt still applies its min/max size hints to the same MINMAXINFO afterwards.
      MONITORINFO monitor_info{};
      monitor_info.cbSize = sizeof(monitor_info);
      HMONITOR monitor = MonitorFromWindow(nc_message->hwnd, MONITOR_DEFAULTTONEAREST);
      if (monitor != nullptr && GetMonitorInfoW(monitor, &monitor_info) != 0) {
        auto* minmax = reinterpret_cast<MINMAXINFO*>(nc_message->lParam);
        minmax->ptMaxPosition.x = monitor_info.rcWork.left - monitor_info.rcMonitor.left;
        minmax->ptMaxPosition.y = monitor_info.rcWork.top - monitor_info.rcMonitor.top;
        minmax->ptMaxSize.x = monitor_info.rcWork.right - monitor_info.rcWork.left;
        minmax->ptMaxSize.y = monitor_info.rcWork.bottom - monitor_info.rcWork.top;
      }
    }
    if (nc_message->message == WM_NCCALCSIZE && nc_message->wParam != FALSE) {
      // Strip the entire non-client area so the client fills the window (no native frame,
      // hence no white top line). When maximized, the window rect depends on who initiated
      // the maximize: Qt-initiated (maximize button, double-click) expands past the work
      // area by the resize-frame thickness on every side, while an OS-initiated snap
      // (drag-to-top, Win+Up) places the window exactly on the work area. Insetting by the
      // frame thickness only suits the former — after a snap it shrank the client inside
      // the work area, exposing the white non-client frame as a ring around the screen.
      // Pin the client rect to the monitor work area instead, which is correct for both.
      // Normal and fullscreen states keep the full window (no adjustment).
      if (IsZoomed(nc_message->hwnd) != 0) {
        auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(nc_message->lParam);
        MONITORINFO monitor_info{};
        monitor_info.cbSize = sizeof(monitor_info);
        HMONITOR monitor = MonitorFromRect(&params->rgrc[0], MONITOR_DEFAULTTONEAREST);
        if (monitor != nullptr && GetMonitorInfoW(monitor, &monitor_info) != 0) {
          params->rgrc[0] = monitor_info.rcWork;
        } else {
          const int border_x = GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
          const int border_y = GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
          params->rgrc[0].left += border_x;
          params->rgrc[0].top += border_y;
          params->rgrc[0].right -= border_x;
          params->rgrc[0].bottom -= border_y;
        }
      }
      *result = 0;
      return true;
    }
  }
  if (message != nullptr && result != nullptr && !isMaximized() && !isFullScreen()) {
    auto* native_message = static_cast<MSG*>(message);
    if (native_message->message == WM_NCHITTEST) {
      RECT window_rect;
      if (native_message->hwnd != nullptr && GetWindowRect(native_message->hwnd, &window_rect) != 0) {
        const auto x = GET_X_LPARAM(native_message->lParam);
        const auto y = GET_Y_LPARAM(native_message->lParam);
        const bool left = x >= window_rect.left && x < window_rect.left + kWindowResizeBorder;
        const bool right = x < window_rect.right && x >= window_rect.right - kWindowResizeBorder;
        const bool top = y >= window_rect.top && y < window_rect.top + kWindowResizeBorder;
        const bool bottom = y < window_rect.bottom && y >= window_rect.bottom - kWindowResizeBorder;

        if ((left || right || top || bottom) &&
            window_resize_hit_targets_scroll_bar(this, QPoint(x, y))) {
          *result = HTCLIENT;
          return true;
        }

        if (top && left) {
          *result = HTTOPLEFT;
          return true;
        }
        if (top && right) {
          *result = HTTOPRIGHT;
          return true;
        }
        if (bottom && left) {
          *result = HTBOTTOMLEFT;
          return true;
        }
        if (bottom && right) {
          *result = HTBOTTOMRIGHT;
          return true;
        }
        if (left) {
          *result = HTLEFT;
          return true;
        }
        if (right) {
          *result = HTRIGHT;
          return true;
        }
        if (top) {
          *result = HTTOP;
          return true;
        }
        if (bottom) {
          *result = HTBOTTOM;
          return true;
        }
      }
    }
  }
#endif
  return QMainWindow::nativeEvent(event_type, message, result);
}
void MainWindow::showEvent(QShowEvent* event) {
  QMainWindow::showEvent(event);
  ensure_native_resizable_frame();
  if (!native_frame_geometry_resynced_) {
    native_frame_geometry_resynced_ = true;
    // Defer to the next event-loop turn so the frameless WM_NCCALCSIZE frame change has fully
    // settled before we re-sync Qt's geometry to the real client rect.
    QTimer::singleShot(0, this, [this] { resync_native_frame_geometry(); });
  }
}

void MainWindow::restore_maximized_under_cursor(QPoint global_cursor) {
  if (!isMaximized()) {
    return;
  }
  const QRect maximized = geometry();
  const QRect restored = normalGeometry().isValid() ? normalGeometry() : QRect(maximized.topLeft(), maximized.size() / 2);
  // Keep the cursor at the same fractional X along the title bar after restoring, and place
  // the (shorter) restored title bar under the cursor vertically.
  const double x_fraction = maximized.width() > 0
                                ? double(global_cursor.x() - maximized.x()) / double(maximized.width())
                                : 0.5;
  restore_window_from_maximize();
  const int new_left = global_cursor.x() - static_cast<int>(x_fraction * restored.width());
  const int new_top = global_cursor.y() - std::min(restored.height() / 2, menuBar()->height() / 2);
  move(new_left, new_top);
}

void MainWindow::restore_window_from_maximize() {
  // After an OS snap maximize (a real native zoom), Qt's showNormal() is a plain resize
  // whose geometry the platform window applies from stale bookkeeping — the window keeps
  // the maximized size (changeEvent strips the leftover WS_MAXIMIZE bit, but Qt reapplies
  // its own geometry after that event, so the fix must happen once showNormal() returns).
  // Capture the remembered normal geometry up front and enforce it afterwards.
  const QRect restored = normalGeometry();
  showNormal();
  if (restored.isValid() && geometry().size() != restored.size()) {
    setGeometry(restored);
  }
}

void MainWindow::resync_native_frame_geometry() {
#ifdef Q_OS_WIN
  // The frameless window removes its native frame via WM_NCCALCSIZE, which enlarges the client area.
  // Qt's cached frame margins can lag behind that change, so the central content is laid out slightly
  // smaller than the window and the never-repainted backing store shows through as a grey band along
  // the bottom and sides until the user resizes. Replicate that resize programmatically: a momentary
  // 1px grow-then-restore forces a layout pass and a full repaint of the client area.
  if (isMaximized() || isFullScreen() || isMinimized() || !isVisible()) {
    update();
    return;
  }
  const QSize current = size();
  if (current.width() > 0 && current.height() > 0) {
    QMainWindow::resize(current.width(), current.height() + 1);
    QMainWindow::resize(current);
  }
  update();
#endif
}

void MainWindow::refresh_native_frame_after_overlay() {
  // Closing an owned top-level overlay (the startup splash) re-activates this window and can leave
  // the frameless client area out of sync, re-introducing the grey edge band. Re-sync on the next
  // event-loop turn, once the overlay window is fully gone.
  QTimer::singleShot(0, this, [this] { resync_native_frame_geometry(); });
}

void MainWindow::position_window_chrome_controls() {
  if (window_chrome_controls_ == nullptr || menuBar() == nullptr) {
    return;
  }
  window_chrome_controls_->move(std::max(0, menuBar()->width() - window_chrome_controls_->width()), 0);
  window_chrome_controls_->raise();
}

void MainWindow::ensure_native_resizable_frame() {
#ifdef Q_OS_WIN
  apply_windows_frameless_resize_style(winId());
  apply_windows_pen_feedback_suppression(winId());
  native_resizable_frame_applied_ = true;
#else
  native_resizable_frame_applied_ = true;
#endif
}

void MainWindow::clamp_window_to_available_screen() {
  // A large interface scale (QT_SCALE_FACTOR) shrinks the logical desktop, so the default window
  // can be larger than the screen or land partly off it. Shrink to fit and nudge it fully on-screen.
  const QScreen* target_screen = screen();
  if (target_screen == nullptr) {
    target_screen = QGuiApplication::primaryScreen();
  }
  if (target_screen == nullptr) {
    return;
  }
  const QRect available = target_screen->availableGeometry();
  if (!available.isValid()) {
    return;
  }

  const int target_width = std::min(width(), available.width());
  const int target_height = std::min(height(), available.height());
  if (target_width != width() || target_height != height()) {
    resize(target_width, target_height);
  }

  QRect frame = frameGeometry();
  frame.setSize(QSize(target_width, target_height));
  if (frame.right() > available.right()) {
    frame.moveRight(available.right());
  }
  if (frame.bottom() > available.bottom()) {
    frame.moveBottom(available.bottom());
  }
  if (frame.left() < available.left()) {
    frame.moveLeft(available.left());
  }
  if (frame.top() < available.top()) {
    frame.moveTop(available.top());
  }
  move(frame.topLeft());
}

void MainWindow::save_window_geometry() const {
  auto settings = app_settings();
  // normalGeometry() reports the restored (non-maximized) bounds, so the window returns to a sensible
  // size and position when the user un-maximizes after relaunch.
  const QRect normal = normalGeometry();
  if (normal.isValid() && normal.width() > 0 && normal.height() > 0) {
    settings.setValue(QStringLiteral("window/normalGeometry"), normal);
  }
  settings.setValue(QStringLiteral("window/maximized"), isMaximized());
}

bool MainWindow::restore_window_geometry() {
  auto settings = app_settings();
  const auto stored = settings.value(QStringLiteral("window/normalGeometry"));
  if (!stored.canConvert<QRect>()) {
    return false;
  }
  const QRect normal = stored.toRect();
  if (!normal.isValid() || normal.width() <= 0 || normal.height() <= 0) {
    return false;
  }
  setGeometry(normal);
  clamp_window_to_available_screen();
  if (settings.value(QStringLiteral("window/maximized"), false).toBool()) {
    // Defer to the windowing system on show; the clamped geometry above becomes the restore bounds.
    setWindowState(windowState() | Qt::WindowMaximized);
  }
  return true;
}

}  // namespace patchy::ui
