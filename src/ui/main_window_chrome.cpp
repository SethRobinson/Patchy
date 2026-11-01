// MainWindow window-chrome implementation, split out of main_window.cpp: the frameless-
// window machinery (Win32 WM_NCCALCSIZE/WM_NCHITTEST native frame, the Qt-neutral
// startSystemMove/startSystemResize path Linux uses, resize borders and cursors,
// maximize/restore behavior, geometry persistence). This file deliberately concentrates
// the platform-specific window code so every port has one place for its equivalents;
// keep new platform-specific window code here.

#include "ui/main_window.hpp"
#include <QWindow>
#include "core/pixel_tools.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/gradient_stops_editor.hpp"
#include <QMenuBar>
#include "ui/canvas_widget.hpp"
#include "ui/main_window_shared.hpp"

#include "ui/action_icons.hpp"
#include "ui/app_settings.hpp"
#include "ui/brush_tip_manager_dialog.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/color_panel.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/update_checker.hpp"
#include "ui/window_effects.hpp"
#include "ui/hotkey_registry.hpp"
#include "ui/icon_theme.hpp"
#include "ui/panel_column.hpp"
#include "ui/theme_palette.hpp"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QByteArray>
#include <QCursor>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLatin1String>
#include <QMainWindow>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPointer>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QScopedValueRollback>
#include <QScreen>
#include <QScrollBar>
#include <QShowEvent>
#include <QSize>
#include <QString>
#include <QStringLiteral>
#include <QTimer>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QVariantAnimation>
#include <QToolButton>
#include <QWidget>
#include <Qt>
#include <QtGlobal>

#include <algorithm>
#include <qlatin1stringview.h>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tchar.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <tpcshrd.h>

#endif

// Icon resources live in the static patchy_ui library; force registration before first use.
int qInitResources_icons();

namespace patchy::ui {

namespace {

constexpr int kWindowResizeBorder = 10;
#if defined(Q_OS_LINUX) && !defined(Q_OS_WASM)
// The Linux chrome decision, made once per process from the stored preference.
std::optional<bool> g_linux_custom_window_chrome;
// Whether this desktop takes the menus into a global menu bar, probed once; tests set it.
std::optional<bool> g_linux_global_menu_bar;
#endif
// Wayland reports no work area, so the window keeps this much of the screen height free for
// a shell panel (GNOME's top bar is about 32 logical px, a KDE panel about 44). Logical pixels.
constexpr int kWaylandShellPanelAllowance = 48;
// The clamp never asks for less than this: on an absurdly small screen the window simply
// overflows rather than collapsing the column to nothing.
constexpr int kSmallestClampedWindowHeight = 400;

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

// Sets the normal placement Windows keeps for a zoomed window (where its own restore,
// a title-bar double-click or Win+Down, goes) to `normal`, a Qt geometry. The zoomed
// window's Qt and native rects give the scale and origin on its monitor;
// rcNormalPosition counts from the monitor's work area.
void set_windows_normal_placement(HWND hwnd, const QRect& window_geometry, qreal ratio, const QRect& normal) {
  WINDOWPLACEMENT placement{};
  placement.length = sizeof(placement);
  RECT window_rect{};
  MONITORINFO monitor_info{};
  monitor_info.cbSize = sizeof(monitor_info);
  HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
  if (!normal.isValid() || ratio <= 0 || GetWindowPlacement(hwnd, &placement) == 0 ||
      GetWindowRect(hwnd, &window_rect) == 0 || monitor == nullptr || GetMonitorInfoW(monitor, &monitor_info) == 0) {
    return;
  }
  const auto physical = [ratio](int logical) { return static_cast<LONG>(qRound(logical * ratio)); };
  const LONG left = window_rect.left + physical(normal.x() - window_geometry.x()) -
                    (monitor_info.rcWork.left - monitor_info.rcMonitor.left);
  const LONG top = window_rect.top + physical(normal.y() - window_geometry.y()) -
                   (monitor_info.rcWork.top - monitor_info.rcMonitor.top);
  const RECT rect{left, top, left + physical(normal.width()), top + physical(normal.height())};
  if (EqualRect(&rect, &placement.rcNormalPosition) != 0) {
    return;
  }
  placement.rcNormalPosition = rect;
  placement.flags = 0;
  SetWindowPlacement(hwnd, &placement);
}
#endif

}  // namespace

bool MainWindow::use_custom_window_chrome() {
#if defined(Q_OS_WIN)
  // The native frame stays behind the custom look (WM_NCCALCSIZE/WM_NCHITTEST below).
  return true;
#elif defined(Q_OS_LINUX) && !defined(Q_OS_WASM)
  // A plain Qt::FramelessWindowHint window on X11 and Wayland, so Qt's own client-side
  // decoration (the light title bar GNOME gets, docs/wayland.md) never appears; drag,
  // resize, snap and tiling go through startSystemMove/startSystemResize. The "Use the
  // system title bar" preference turns this off for the whole process (the desktop's
  // decorations, native move and resize, no controls in the menu bar); it is read once
  // here, before the window is built, and a change applies on the next launch. Until the
  // user sets it, it follows the desktop: on where a global menu bar takes the menus
  // (GitHub issue 161), since the frameless chrome keeps them in the window.
  if (!g_linux_custom_window_chrome.has_value()) {
    g_linux_custom_window_chrome = !system_title_bar_effective();
  }
  return *g_linux_custom_window_chrome;
#else
  // macOS keeps the native frame and global menu bar by design.
  return false;
#endif
}

bool MainWindow::desktop_has_global_menu_bar() {
#if defined(Q_OS_LINUX) && !defined(Q_OS_WASM)
  if (!g_linux_global_menu_bar.has_value()) {
    // Ask Qt rather than the bus: a QMenuBar gets a platform menu bar exactly when the
    // platform theme would export it (the generic theme checks for
    // com.canonical.AppMenu.Registrar, KDE's its own way), and isNativeMenuBar() reports
    // that. Nothing is shown; the probe is gone before the main window is built.
    bool global = false;
    if (qobject_cast<QApplication*>(QCoreApplication::instance()) != nullptr) {
      QMenuBar probe;
      global = probe.isNativeMenuBar();
    }
    g_linux_global_menu_bar = global;
  }
  return *g_linux_global_menu_bar;
#else
  return false;
#endif
}

bool MainWindow::system_title_bar_effective() {
  return stored_system_title_bar_choice().value_or(desktop_has_global_menu_bar());
}

void MainWindow::reset_window_chrome_gate_cache() {
#if defined(Q_OS_LINUX) && !defined(Q_OS_WASM)
  g_linux_custom_window_chrome.reset();
#endif
}

void MainWindow::set_global_menu_bar_probe_for_testing(std::optional<bool> global) {
#if defined(Q_OS_LINUX) && !defined(Q_OS_WASM)
  g_linux_global_menu_bar = global;
#else
  static_cast<void>(global);
#endif
}

bool MainWindow::handle_window_resize_event(QObject* watched, QEvent* event) {
  if (!use_custom_window_chrome()) {
    // Native window frames own their resize borders; the Qt-level edge machinery below
    // exists only for the frameless window.
    return false;
  }
  if (isMaximized() || isFullScreen()) {
    if (!chrome_resizing_) {
      clear_window_resize_cursor();
    }
    return false;
  }
  if (watched == this && event->type() == QEvent::Leave && !chrome_resizing_) {
    // The override cursor is application-wide: a pointer that leaves through a resize
    // border (or is taken by the compositor's own resize on Linux) would otherwise keep
    // the resize arrow over every other Patchy window until it crossed the main window
    // again.
    clear_window_resize_cursor();
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

  // The window system resizes better than Qt can: on Wayland a client cannot move its
  // own top-left edge at all, and on X11 the window manager keeps the resize snapped and
  // the pointer grabbed. The Qt-level geometry path below stays as the fallback where no
  // system resize exists (the offscreen test platform, a window manager without
  // _NET_WM_MOVERESIZE). A real Windows window never arrives here: WM_NCHITTEST answers
  // HTLEFT and friends and the OS resizes.
  if (auto* handle = windowHandle(); handle != nullptr && handle->startSystemResize(edges)) {
    chrome_resizing_ = false;
    chrome_resize_edges_ = Qt::Edges{};
    chrome_dragging_ = false;
    clear_window_resize_cursor();
    mouse_event->accept();
    return true;
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
      // Qt maximizes this frameless window itself from the maximize button (plain resize to
      // the work area, never a native zoom), but OS-initiated maximizes (title-bar
      // double-click via HTCAPTION, drag-to-top snap, Win+Up) go through DefWindowProc,
      // whose default for a caption-less window is the full monitor. That
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
      // the maximize: Qt-initiated (maximize button) expands past the work area by the
      // resize-frame thickness on every side, while an OS-initiated maximize (title-bar
      // double-click, drag-to-top snap, Win+Up) places the window exactly on the work
      // area. Insetting by the frame thickness only suits the former — after a snap it
      // shrank the client inside the work area, exposing the white non-client frame as a
      // ring around the screen.
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
  if (message != nullptr && result != nullptr && !isFullScreen()) {
    auto* native_message = static_cast<MSG*>(message);
    if (native_message->message == WM_NCHITTEST && native_message->hwnd != nullptr) {
      const auto x = GET_X_LPARAM(native_message->lParam);
      const auto y = GET_Y_LPARAM(native_message->lParam);

      // The menu bar doubles as the title bar. Answering HTCAPTION there hands the OS
      // the whole title-bar contract: drag (with snap and the restore-on-drag of a
      // maximized window), double-click to maximize/restore, and the system menu on
      // right-click. Earlier builds emulated this in Qt from the menu-bar press, which
      // restored a maximized window on a plain click. Menu titles and the window
      // controls stay HTCLIENT so Qt keeps their clicks. Physical screen pixels map to
      // widget coordinates through the client origin and the window's scale factor
      // (the frameless client rect is the whole window, and while maximized it is
      // pinned to the work area by WM_NCCALCSIZE, so the client origin is the only
      // reliable anchor).
      if (auto* bar = menuBar(); bar != nullptr && bar->isVisible()) {
        POINT client_origin{0, 0};
        if (ClientToScreen(native_message->hwnd, &client_origin) != 0) {
          const qreal scale = devicePixelRatioF() > 0 ? devicePixelRatioF() : 1.0;
          const QPoint window_position(qRound((x - client_origin.x) / scale), qRound((y - client_origin.y) / scale));
          const QPoint bar_position = bar->mapFrom(this, window_position);
          // The resize borders win over the caption while the window is restored, as on a
          // native frame; a maximized window has no resize borders.
          const bool on_resize_border =
              !isMaximized() && resize_edges_for_window_position(size(), window_position) != Qt::Edges{};
          if (!on_resize_border && bar->rect().contains(bar_position) && title_bar_drag_area_contains(bar_position)) {
            *result = HTCAPTION;
            return true;
          }
        }
      }

      RECT window_rect;
      if (!isMaximized() && GetWindowRect(native_message->hwnd, &window_rect) != 0) {
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
  if (!brush_tool_windows_restored_) {
    // After the main window is on screen, so the floating brush windows sit above it.
    QTimer::singleShot(0, this, [this] { restore_brush_tool_windows(); });
  }
  ensure_native_resizable_frame();
  if (!native_frame_geometry_resynced_) {
    native_frame_geometry_resynced_ = true;
    // Defer to the next event-loop turn so the frameless WM_NCCALCSIZE frame change has fully
    // settled before we re-sync Qt's geometry to the real client rect.
    QTimer::singleShot(0, this, [this] { resync_native_frame_geometry(); });
  }
  if (!wayland_first_show_clamped_ &&
      QGuiApplication::platformName().startsWith(QLatin1String("wayland"), Qt::CaseInsensitive)) {
    wayland_first_show_clamped_ = true;
    // Decorations outside the client area are known only once the window is shown (none
    // for the frameless chrome; Qt's own client-side decoration if a build ever runs with
    // a native frame), so the constructor's clamp could not count them; an oversize window
    // cannot be moved back onto a Wayland screen, so shrink it now while the compositor
    // still accepts the first size. The same turn applies a restored maximized state,
    // which the compositor drops when asked before the first configure (GitHub issue 132).
    QTimer::singleShot(0, this, [this] {
      if (wayland_maximize_on_first_show_) {
        wayland_maximize_on_first_show_ = false;
        showMaximized();
        return;
      }
      if (!isMaximized() && !isFullScreen() && height() > available_window_height()) {
        clamp_window_to_available_screen();
      }
    });
  }
}

bool MainWindow::title_bar_drag_area_contains(QPoint menu_bar_position) const {
  const auto* bar = menuBar();
  if (bar == nullptr || bar->actionAt(menu_bar_position) != nullptr) {
    return false;
  }
  if (window_chrome_controls_ != nullptr) {
    const QRect controls_rect(window_chrome_controls_->pos(), window_chrome_controls_->size());
    if (controls_rect.contains(menu_bar_position)) {
      return false;
    }
  }
  if (command_search_button_ != nullptr && command_search_button_->isVisible() &&
      command_search_button_->geometry().contains(menu_bar_position)) {
    return false;
  }
  return true;
}

void MainWindow::restore_maximized_under_cursor(QPoint global_cursor) {
  if (!isMaximized()) {
    return;
  }
  const QRect maximized = geometry();
  const QRect restored = normal_bounds().isValid() ? normal_bounds() : QRect(maximized.topLeft(), maximized.size() / 2);
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

QRect MainWindow::normal_bounds() const {
  return maximized_return_normal_geometry_.isValid() ? maximized_return_normal_geometry_ : normalGeometry();
}

void MainWindow::restore_window_from_maximize() {
#ifdef Q_OS_WIN
  // After an OS snap maximize (a real native zoom), Qt's showNormal() is a plain resize
  // whose geometry the platform window applies from stale bookkeeping: the window keeps
  // the maximized size (changeEvent strips the leftover WS_MAXIMIZE bit, but Qt reapplies
  // its own geometry after that event, so the fix must happen once showNormal() returns).
  // Capture the remembered normal geometry up front and enforce it afterwards.
  const QRect restored = normal_bounds();
  showNormal();
  if (restored.isValid() && geometry().size() != restored.size()) {
    setGeometry(restored);
  }
#else
  // Linux: the restore is asynchronous (the compositor's configure brings the saved size
  // back and Qt applies it), so a geometry set here would only race it. Wayland cannot
  // position a window anyway.
  showNormal();
#endif
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
  // The window keeps WS_THICKFRAME, so DWM already shadows it; this only asks for
  // the rounded corners. DWM squares them off by itself while maximized.
  apply_rounded_window_corners(winId(), WindowCornerRadius::Standard);
  native_resizable_frame_applied_ = true;
#else
  native_resizable_frame_applied_ = true;
#endif
}

int MainWindow::available_window_height() const {
  if (available_window_height_override_ > 0) {
    return available_window_height_override_;
  }
  const QScreen* target_screen = screen();
  if (target_screen == nullptr) {
    target_screen = QGuiApplication::primaryScreen();
  }
  if (target_screen == nullptr) {
    return -1;
  }
  const QRect available = target_screen->availableGeometry();
  if (!available.isValid()) {
    return -1;
  }
  // Zero for the custom chrome (frameless); a native title bar, or Qt's own
  // Wayland decoration, once the window is shown.
  const int decorations = std::max(0, frameGeometry().height() - height());
  // Wayland reports the whole output as available: no protocol tells an app
  // where a panel sits, and the app cannot move its window clear of one either
  // (GitHub issue 108: GNOME's top bar hid the bottom of the column).
  const bool wayland =
      QGuiApplication::platformName().startsWith(QLatin1String("wayland"), Qt::CaseInsensitive);
  const int shell_allowance = wayland ? kWaylandShellPanelAllowance : 0;
  return std::max(kSmallestClampedWindowHeight, available.height() - decorations - shell_allowance);
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

  // A resize cannot go below the window's minimum, and the right column's
  // floors used to hold that minimum above a 1366x768 laptop screen at a 125%
  // scale, so the column's bottom panels sat off-screen and every expand grew
  // the window further (GitHub issue 108).
  fit_right_dock_floors_to_screen();
  if (layout() != nullptr && isVisible()) {
    // The window's explicit minimum is the layout's total from its last
    // activation; refresh it now, or resize() below is clamped to the old one.
    // Only once shown: an activation before the stylesheet lands would pin an
    // unstyled, taller minimum on the window.
    layout()->activate();
  }
  const int target_width = std::min(width(), available.width());
  const int target_height = std::min(height(), available_window_height());
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
  // normal_bounds() reports the restored (non-maximized) bounds, so the window returns to a sensible
  // size and position when the user un-maximizes after relaunch.
  // A full-screen screen mode is never restored at the next start: Standard's state is kept.
  const auto& standard = standard_screen_chrome_;
  const QRect normal = standard.has_value() && standard->normal_geometry.isValid() ? standard->normal_geometry
                                                                                   : normal_bounds();
  if (normal.isValid() && normal.width() > 0 && normal.height() > 0) {
    settings.setValue(QStringLiteral("window/normalGeometry"), normal);
  }
  settings.setValue(QStringLiteral("window/maximized"), standard.has_value() ? standard->maximized : isMaximized());
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
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"), Qt::CaseInsensitive)) {
      // A maximize requested before the first configure is dropped by the compositor
      // (GNOME), leaving a normal-size window that Qt still reports as maximized and
      // whose decoration shows the restore glyph (GitHub issue 132). Ask once the
      // window is up instead (showEvent's first-show turn).
      wayland_maximize_on_first_show_ = true;
    } else {
      // Defer to the windowing system on show; the clamped geometry above becomes the restore bounds.
      setWindowState(windowState() | Qt::WindowMaximized);
    }
  }
  return true;
}

void MainWindow::configure_window_chrome() {
  if (!use_custom_window_chrome()) {
    // Native frame (macOS, or Linux with the system title bar preference): the platform
    // draws the title bar and window buttons. Leave the menu bar fully default too:
    // QMenuBar's default (nativeMenuBar=true) moves the menus into the global menu bar.
    return;
  }
  auto* bar = menuBar();
  bar->setNativeMenuBar(false);
  bar->setFixedHeight(34);
  bar->installEventFilter(this);
  // The stylesheet's badge padding keys on this; repolish in case the sheet is already set.
  bar->setProperty("patchyBadgeChrome", true);
  bar->style()->unpolish(bar);
  bar->style()->polish(bar);

  auto* badge = new QLabel(bar);
  badge->setObjectName(QStringLiteral("patchyBadge"));
  badge->setAlignment(Qt::AlignCenter);
  badge->setAttribute(Qt::WA_TransparentForMouseEvents);
  badge->setFixedSize(18, 18);
  badge->setPixmap(patchy_app_icon().pixmap(18, 18));
  badge->move(9, 8);
  badge->show();

  auto* controls = new QWidget(bar);
  controls->setObjectName(QStringLiteral("windowChromeControls"));
  controls->setFixedSize(46 * 3, 34);
  window_chrome_controls_ = controls;
  auto* layout = new QHBoxLayout(controls);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  const auto add_chrome_button = [controls, layout](const QString& object_name, const QIcon& icon,
                                                    const QString& tooltip) {
    auto* button = new QToolButton(controls);
    button->setObjectName(object_name);
    button->setProperty("windowChromeButton", true);
    button->setAutoRaise(false);
    button->setFocusPolicy(Qt::NoFocus);
    button->setIcon(icon);
    button->setIconSize(QSize(16, 16));
    button->setToolTip(tooltip);
    button->setFixedSize(46, 34);
    layout->addWidget(button);
    return button;
  };

  auto* minimize_button =
      add_chrome_button(QStringLiteral("windowMinimizeButton"), window_chrome_icon(QStringLiteral("minimize")),
                        tr("Minimize"));
  bind_tooltip(minimize_button, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Minimize"));
  maximize_button_ =
      add_chrome_button(QStringLiteral("windowMaximizeButton"), window_chrome_icon(QStringLiteral("maximize")),
                        tr("Maximize / Restore"));
  bind_tooltip(maximize_button_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Maximize / Restore"));
  auto* close_button =
      add_chrome_button(QStringLiteral("windowCloseButton"), window_chrome_icon(QStringLiteral("close")), tr("Close"));
  bind_tooltip(close_button, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Close"));
  position_window_chrome_controls();
  controls->show();

  connect(minimize_button, &QToolButton::clicked, this, [this] { showMinimized(); });
  connect(maximize_button_, &QToolButton::clicked, this, [this] {
    if (screen_mode_ != ScreenMode::Standard) {
      // Full Screen with Menus shows the controls: maximize/restore goes back to Standard.
      set_screen_mode(ScreenMode::Standard);
    } else {
      isMaximized() ? restore_window_from_maximize() : showMaximized();
    }
  });
  connect(close_button, &QToolButton::clicked, this, &QWidget::close);
}

// ---------------------------------------------------------------------------
// Screen modes (GitHub issue 212; docs/screen-modes.md)
// ---------------------------------------------------------------------------

namespace {

constexpr int kScreenModeHintHoldMs = 2500;
constexpr int kScreenModeHintFadeMs = 700;
// A window-state report this soon after a screen-mode change belongs to the window
// system's own transition (macOS animates into its full-screen space).
constexpr int kScreenModeSettleMs = 1500;

// The note a full-screen mode shows on entry: painted from theme roles over the
// workspace, transparent to the mouse, fading out after a few seconds. It follows the
// workspace's geometry while up, since a full-screen change resizes it after the call.
class ScreenModeHint final : public QWidget {
public:
  explicit ScreenModeHint(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("screenModeHint"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);
    hold_timer_.setSingleShot(true);
    connect(&hold_timer_, &QTimer::timeout, this, [this] { fade_.start(); });
    fade_.setDuration(kScreenModeHintFadeMs);
    fade_.setStartValue(1.0);
    fade_.setEndValue(0.0);
    connect(&fade_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
      opacity_ = value.toReal();
      update();
    });
    connect(&fade_, &QVariantAnimation::finished, this, [this] { hide(); });
    hide();
  }

  void show_text(const QString& text, QWidget* anchor) {
    if (anchor_ != anchor) {
      if (anchor_ != nullptr) {
        anchor_->removeEventFilter(this);
      }
      anchor_ = anchor;
      if (anchor_ != nullptr) {
        anchor_->installEventFilter(this);
      }
    }
    text_ = text;
    setAccessibleName(text);
    fade_.stop();
    opacity_ = 1.0;
    place();
    show();
    raise();
    update();
    hold_timer_.start(kScreenModeHintHoldMs);
  }

  void dismiss() {
    hold_timer_.stop();
    fade_.stop();
    hide();
  }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (watched == anchor_ && isVisible() && (event->type() == QEvent::Resize || event->type() == QEvent::Move)) {
      place();
    }
    return QWidget::eventFilter(watched, event);
  }

  void paintEvent(QPaintEvent* event) override {
    static_cast<void>(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setOpacity(opacity_);
    const auto& palette = theme();
    painter.setPen(QPen(palette.menu_border, 1.0));
    painter.setBrush(palette.menu_bg);
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 6.0, 6.0);
    painter.setFont(hint_font());
    painter.setPen(palette.text_primary);
    painter.drawText(rect(), Qt::AlignCenter, text_);
  }

private:
  [[nodiscard]] QFont hint_font() const {
    auto hint = font();
    if (hint.pointSizeF() > 0) {
      hint.setPointSizeF(hint.pointSizeF() * 1.15);
    }
    return hint;
  }

  void place() {
    if (anchor_ == nullptr || parentWidget() == nullptr) {
      return;
    }
    const QFontMetrics metrics(hint_font());
    const QSize size(metrics.horizontalAdvance(text_) + 2 * 18, metrics.height() + 2 * 10);
    const QPoint origin = anchor_->mapTo(parentWidget(), QPoint(0, 0));
    setGeometry(QRect(origin + QPoint(std::max(0, (anchor_->width() - size.width()) / 2), 36), size));
  }

  QPointer<QWidget> anchor_;
  QString text_;
  qreal opacity_{1.0};
  QTimer hold_timer_;
  QVariantAnimation fade_;
};

}  // namespace

void MainWindow::build_screen_mode_actions(QMenu& view_menu) {
  auto* menu = view_menu.addMenu(QString());
  menu->setObjectName(QStringLiteral("viewScreenModeMenu"));
  menu->menuAction()->setMenuRole(QAction::NoRole);
  bind_action_text(menu->menuAction(), QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Screen &Mode"));
  auto* group = new QActionGroup(this);
  group->setExclusive(true);
  // Each mode's icon is what the tool palette's screen mode button shows for it.
  const auto add_mode = [this, menu, group](const char* source, const QString& object_name, const QString& id,
                                            ScreenMode mode, const QString& icon) {
    auto* action = menu->addAction(QString());
    action->setObjectName(object_name);
    action->setIcon(themed_svg_icon(icon));
    action->setMenuRole(QAction::NoRole);
    action->setCheckable(true);
    action->setActionGroup(group);
    action->setProperty(kWorksWithoutDocumentProperty, true);
    bind_action_text(action, source);
    register_hotkey(action, id);
    connect(action, &QAction::triggered, this, [this, mode] { set_screen_mode(mode); });
    return action;
  };
  screen_mode_standard_action_ =
      add_mode(QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "&Standard"), QStringLiteral("viewScreenModeStandardAction"),
               QStringLiteral("view.screen_mode_standard"), ScreenMode::Standard,
               QStringLiteral("screen-mode-standard"));
  screen_mode_full_with_menus_action_ =
      add_mode(QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Full Screen with &Menus"),
               QStringLiteral("viewScreenModeFullWithMenusAction"),
               QStringLiteral("view.screen_mode_full_with_menus"), ScreenMode::FullScreenWithMenus,
               QStringLiteral("screen-mode-full-with-menus"));
  screen_mode_full_action_ =
      add_mode(QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "&Full Screen"), QStringLiteral("viewScreenModeFullAction"),
               QStringLiteral("view.screen_mode_full"), ScreenMode::FullScreen, QStringLiteral("screen-mode-full"));
  screen_mode_standard_action_->setChecked(true);
  menu->addSeparator();
  // F, as in other image editors: a single key, so a focused text field keeps it.
  screen_mode_cycle_action_ = menu->addAction(QString());
  screen_mode_cycle_action_->setObjectName(QStringLiteral("viewScreenModeCycleAction"));
  screen_mode_cycle_action_->setMenuRole(QAction::NoRole);
  screen_mode_cycle_action_->setProperty(kWorksWithoutDocumentProperty, true);
  screen_mode_cycle_action_->setAutoRepeat(false);
  bind_action_text(screen_mode_cycle_action_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "&Cycle Screen Mode"));
  register_hotkey(screen_mode_cycle_action_, QStringLiteral("view.screen_mode_cycle"), QKeySequence(Qt::Key_F));
  connect(screen_mode_cycle_action_, &QAction::triggered, this, [this] { cycle_screen_mode(); });
  // The canvas right-click menu's way back (CanvasWidget::set_screen_mode_context_actions_callback).
  exit_full_screen_action_ = new QAction(this);
  exit_full_screen_action_->setObjectName(QStringLiteral("canvasExitFullScreenAction"));
  bind_action_text(exit_full_screen_action_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Exit Full Screen"));
  connect(exit_full_screen_action_, &QAction::triggered, this, [this] { set_screen_mode(ScreenMode::Standard); });
}

void MainWindow::cycle_screen_mode() {
  switch (screen_mode_) {
    case ScreenMode::Standard:
      set_screen_mode(ScreenMode::FullScreenWithMenus);
      break;
    case ScreenMode::FullScreenWithMenus:
      set_screen_mode(ScreenMode::FullScreen);
      break;
    case ScreenMode::FullScreen:
      set_screen_mode(ScreenMode::Standard);
      break;
  }
}

void MainWindow::set_screen_mode(ScreenMode mode) {
  if (applying_screen_mode_) {
    return;
  }
  if (mode == screen_mode_) {
    refresh_screen_mode_actions();
    return;
  }
  const QScopedValueRollback applying(applying_screen_mode_, true);
  if (screen_mode_ == ScreenMode::Standard) {
    StandardScreenChrome chrome;
    chrome.maximized = isMaximized();
    chrome.normal_geometry = normal_bounds().isValid() ? normal_bounds() : geometry();
    chrome.menu_bar = !menuBar()->isHidden();
    if (panel_column_ != nullptr) {
      chrome.panel_column = !panel_column_->isHidden();
      chrome.panel_column_width = panel_column_->width();
    }
    if (document_tabs_ != nullptr && document_tabs_->tabBar() != nullptr) {
      chrome.tab_bar = !document_tabs_->tabBar()->isHidden();
    }
    chrome.status_bar = !statusBar()->isHidden();
    for (auto* bar : findChildren<QToolBar*>(QString(), Qt::FindDirectChildrenOnly)) {
      chrome.toolbars.emplace_back(bar, !bar->isHidden());
    }
    standard_screen_chrome_ = std::move(chrome);
  }
  screen_mode_ = mode;
  screen_mode_changed_timer_.start();
  if (mode == ScreenMode::Standard) {
    const auto chrome = standard_screen_chrome_.value_or(StandardScreenChrome{});
    restore_standard_screen_chrome();
    standard_screen_chrome_.reset();
    if (chrome.maximized) {
      showMaximized();
#ifdef Q_OS_WIN
      // Qt leaves full screen for maximized by first putting the window at its saved normal
      // placement, which it queues as a restored state, and then zooming it. Delivered
      // later, that stale report made changeEvent strip the zoom and shrink the window to
      // its normal size while Qt still called it maximized (GitHub issue 491). Deliver the
      // reports now, with changeEvent's frame fixups off, so the zoom stands.
      const QScopedValueRollback settling(settling_maximized_return_, true);
      QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
      // Windows keeps the placement Qt saved as the normal one: after the maximize control
      // (Qt's own resize to the work area, no zoom) that is the maximized size, so a
      // Windows restore would stay full size. Give it the window's normal bounds.
      if (auto* hwnd = reinterpret_cast<HWND>(winId()); hwnd != nullptr && IsZoomed(hwnd) != 0) {
        set_windows_normal_placement(hwnd, geometry(), devicePixelRatioF(), chrome.normal_geometry);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
      }
      maximized_return_normal_geometry_ = isMaximized() ? chrome.normal_geometry : QRect();
#endif
    } else {
      showNormal();
#ifdef Q_OS_WIN
      // As restore_window_from_maximize: Qt can reapply stale bookkeeping to the
      // frameless window, so the remembered bounds are enforced afterwards.
      if (chrome.normal_geometry.isValid() && geometry().size() != chrome.normal_geometry.size()) {
        setGeometry(chrome.normal_geometry);
      }
#endif
    }
  } else {
    apply_screen_mode_chrome(mode);
    if (!isFullScreen()) {
#ifdef Q_OS_LINUX
      // showFullScreen() drops the maximized state, so a Wayland compositor unzooms the
      // window, and KWin ignores the set_maximized that showMaximized() sends ahead of
      // unset_fullscreen on the way back: the window came back at its normal size (GitHub
      // issue 491, Plasma). Keep the maximized state under full screen instead: Qt then
      // sends only set_fullscreen, and leaving sends only unset_fullscreen.
      if (isMaximized()) {
        setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowFullScreen);
      } else {
        showFullScreen();
      }
#else
      showFullScreen();
#endif
    }
    show_screen_mode_hint();
  }
  refresh_screen_mode_actions();
  if (canvas_ != nullptr && canvas_->window() == this && isActiveWindow()) {
    canvas_->setFocus(Qt::OtherFocusReason);
  }
}

void MainWindow::apply_screen_mode_chrome(ScreenMode mode) {
  const auto chrome = standard_screen_chrome_.value_or(StandardScreenChrome{});
  const bool canvas_only = mode == ScreenMode::FullScreen;
  const auto shown = [canvas_only](bool standard) { return !canvas_only && standard; };
  menuBar()->setVisible(shown(chrome.menu_bar));
  for (const auto& [bar, visible] : chrome.toolbars) {
    if (bar != nullptr) {
      bar->setVisible(shown(visible));
    }
  }
  if (document_tabs_ != nullptr && document_tabs_->tabBar() != nullptr) {
    document_tabs_->tabBar()->setVisible(shown(chrome.tab_bar));
  }
  statusBar()->setVisible(shown(chrome.status_bar));
  if (panel_column_ != nullptr) {
    const bool column = shown(chrome.panel_column);
    const bool was_hidden = panel_column_->isHidden();
    panel_column_->setVisible(column);
    if (column && was_hidden && workspace_splitter_ != nullptr && chrome.panel_column_width > 0) {
      // The splitter hands a re-shown column whatever width it computes; give it back its
      // own (its stretch factor is 0, so the window's resize afterwards keeps it).
      const auto total = workspace_splitter_->width() > 0 ? workspace_splitter_->width() : width();
      workspace_splitter_->setSizes(
          {std::max(0, total - workspace_splitter_->handleWidth() - chrome.panel_column_width),
           chrome.panel_column_width});
    }
  }
  set_screen_mode_shortcut_host(canvas_only);
}

void MainWindow::restore_standard_screen_chrome() {
  hide_screen_mode_hint();
  if (standard_screen_chrome_.has_value()) {
    apply_screen_mode_chrome(ScreenMode::Standard);
  }
}

void MainWindow::set_screen_mode_shortcut_host(bool host) {
  // A hidden menu bar or toolbar no longer matches its actions' shortcuts (Qt checks
  // that a widget carrying the action is visible), so while Full Screen hides them the
  // window itself carries every registered command, as a float window does.
  if (host) {
    if (!screen_mode_hosted_actions_.isEmpty()) {
      return;
    }
    const auto carried = actions();
    for (const auto& command : hotkey_registry_.commands()) {
      if (command.action != nullptr && !carried.contains(command.action.data())) {
        addAction(command.action);
        screen_mode_hosted_actions_.push_back(command.action);
      }
    }
    return;
  }
  for (const auto& action : std::as_const(screen_mode_hosted_actions_)) {
    if (action != nullptr) {
      removeAction(action);
    }
  }
  screen_mode_hosted_actions_.clear();
}

void MainWindow::refresh_screen_mode_actions() {
  const std::pair<QAction*, ScreenMode> modes[] = {
      {screen_mode_standard_action_, ScreenMode::Standard},
      {screen_mode_full_with_menus_action_, ScreenMode::FullScreenWithMenus},
      {screen_mode_full_action_, ScreenMode::FullScreen}};
  for (const auto& [action, mode] : modes) {
    if (action != nullptr) {
      action->setChecked(mode == screen_mode_);
    }
  }
  refresh_screen_mode_button();
}

void MainWindow::refresh_screen_mode_button() {
  if (screen_mode_button_ == nullptr) {
    return;
  }
  QAction* current = screen_mode_standard_action_;
  if (screen_mode_ == ScreenMode::FullScreenWithMenus) {
    current = screen_mode_full_with_menus_action_;
  } else if (screen_mode_ == ScreenMode::FullScreen) {
    current = screen_mode_full_action_;
  }
  if (current == nullptr) {
    return;
  }
  screen_mode_button_->setIcon(current->icon());
  const auto label = tr("Screen Mode: %1").arg(clean_action_text(current));
  const auto keys = action_shortcut_text(screen_mode_cycle_action_);
  screen_mode_button_->setToolTip(
      (keys.isEmpty() ? label : QStringLiteral("%1 (%2)").arg(label, keys)) + QLatin1Char('\n') +
      tr("Click for the next mode. Hold or right-click to choose one."));
}

void MainWindow::show_screen_mode_hint() {
  auto* hint = static_cast<ScreenModeHint*>(screen_mode_hint_);
  if (hint == nullptr) {
    hint = new ScreenModeHint(this);
    screen_mode_hint_ = hint;
  }
  // Full Screen names the key that brings the menus back; the mode with menus shows
  // them already, and F there hides them rather than leaving.
  const auto cycle_keys =
      screen_mode_cycle_action_ != nullptr ? screen_mode_cycle_action_->shortcut() : QKeySequence();
  const auto text = screen_mode_ == ScreenMode::FullScreen && !cycle_keys.isEmpty()
                        ? tr("Press %1 or Esc to exit full screen").arg(cycle_keys.toString(QKeySequence::NativeText))
                        : tr("Press Esc to exit full screen");
  // Over the document view, clear of the rulers and the panel column.
  QWidget* anchor = document_tabs_ != nullptr && document_tabs_->currentWidget() != nullptr
                        ? document_tabs_->currentWidget()
                        : static_cast<QWidget*>(document_tabs_);
  hint->show_text(text, anchor != nullptr ? anchor : this);
}

void MainWindow::hide_screen_mode_hint() {
  if (auto* hint = static_cast<ScreenModeHint*>(screen_mode_hint_); hint != nullptr) {
    hint->dismiss();
  }
}

bool MainWindow::exit_full_screen_on_escape() {
  if (screen_mode_ == ScreenMode::Standard || QApplication::activePopupWidget() != nullptr ||
      QApplication::activeModalWidget() != nullptr) {
    return false;
  }
  set_screen_mode(ScreenMode::Standard);
  return true;
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
  // Escape nothing below used (a panel field, the start panel, an empty workspace).
  if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier && !event->isAutoRepeat() &&
      exit_full_screen_on_escape()) {
    event->accept();
    return;
  }
  QMainWindow::keyPressEvent(event);
}

void MainWindow::schedule_screen_mode_window_state_sync() {
  if (screen_mode_ == ScreenMode::Standard || applying_screen_mode_ || screen_mode_sync_pending_) {
    return;
  }
  screen_mode_sync_pending_ = true;
  const auto elapsed =
      screen_mode_changed_timer_.isValid() ? screen_mode_changed_timer_.elapsed() : qint64{kScreenModeSettleMs};
  const int delay = static_cast<int>(std::max<qint64>(300, kScreenModeSettleMs - elapsed));
  QTimer::singleShot(delay, this, [this] {
    screen_mode_sync_pending_ = false;
    sync_screen_mode_to_window_state();
  });
}

void MainWindow::sync_screen_mode_to_window_state() {
  if (screen_mode_ == ScreenMode::Standard || applying_screen_mode_ || isFullScreen() || isMinimized()) {
    return;
  }
  // The window already left full screen (and may be maximized), so only the chrome and
  // the mode follow; the window state is the one the user or the system chose.
  const QScopedValueRollback applying(applying_screen_mode_, true);
  restore_standard_screen_chrome();
  standard_screen_chrome_.reset();
  screen_mode_ = ScreenMode::Standard;
  screen_mode_changed_timer_.start();
  refresh_screen_mode_actions();
}

}  // namespace patchy::ui
