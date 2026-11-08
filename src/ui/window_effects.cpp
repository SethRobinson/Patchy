#include "ui/window_effects.hpp"

#include "ui/theme_palette.hpp"

#include <QColor>
#include <QEvent>
#include <QObject>
#include <QWidget>
#include <QWindow>
#include <QtGlobal>

#ifdef Q_OS_WIN
// clang-format off
#include <windows.h>

#include <dwmapi.h>
// clang-format on
#endif

namespace patchy::ui {

#ifdef Q_OS_WIN
namespace {

// Spelled out rather than taken from the SDK headers, which do not declare them
// unless the build targets a recent enough Windows SDK. Passing an attribute an
// older DWM does not know just returns a failure HRESULT and changes nothing, so
// the calls below are unconditional.
constexpr DWORD kDwmwaWindowCornerPreference = 33;  // DWMWA_WINDOW_CORNER_PREFERENCE
constexpr DWORD kDwmwaBorderColor = 34;             // DWMWA_BORDER_COLOR
constexpr DWORD kDwmwcpRound = 2;                   // DWMWCP_ROUND
constexpr DWORD kDwmwcpRoundSmall = 3;              // DWMWCP_ROUNDSMALL

// The offscreen platform used by the UI suite still hands out window ids, but
// they are not HWNDs. IsWindow is the cheap way to tell a real native window from
// one of those, so the suite does not spend every dialog show on a DWM call that
// can only fail.

// Reapplies on every show rather than only the first. A show is cheap, Qt can
// hand the widget a new native window between shows, and the alternative is
// tracking handle lifetime here.
class FramelessWindowEffects final : public QObject {
public:
  FramelessWindowEffects(QWidget& window, WindowCornerRadius radius)
      : QObject(&window), window_(window), radius_(radius) {}

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    // Read the handle rather than calling winId(), which would create a native
    // window as a side effect if one did not already exist.
    if (event->type() == QEvent::Show) {
      if (auto* handle = window_.windowHandle(); handle != nullptr) {
        apply_rounded_window_corners(handle->winId(), radius_);
        apply_frameless_window_shadow(handle->winId());
        apply_window_border_color(handle->winId());
      }
    } else if (event->type() == QEvent::StyleChange) {
      // A theme switch restyles the dialog (Preferences stays open through it),
      // so its border follows the new window_outer_edge color.
      if (auto* handle = window_.windowHandle(); handle != nullptr && window_.isVisible()) {
        apply_window_border_color(handle->winId());
      }
    }
    return QObject::eventFilter(watched, event);
  }

private:
  QWidget& window_;
  WindowCornerRadius radius_;
};

}  // namespace
#endif

void apply_frameless_window_effects_on_show([[maybe_unused]] QWidget& window,
                                            [[maybe_unused]] WindowCornerRadius radius) {
#ifdef Q_OS_WIN
  window.installEventFilter(new FramelessWindowEffects(window, radius));
#endif
}

void apply_rounded_window_corners([[maybe_unused]] WId window_id,
                                  [[maybe_unused]] WindowCornerRadius radius) {
#ifdef Q_OS_WIN
  auto* hwnd = reinterpret_cast<HWND>(window_id);
  if (!IsWindow(hwnd)) {
    return;
  }
  const DWORD preference =
      radius == WindowCornerRadius::Small ? kDwmwcpRoundSmall : kDwmwcpRound;
  DwmSetWindowAttribute(hwnd, kDwmwaWindowCornerPreference, &preference, sizeof(preference));
#endif
}

void apply_frameless_window_shadow([[maybe_unused]] WId window_id) {
#ifdef Q_OS_WIN
  auto* hwnd = reinterpret_cast<HWND>(window_id);
  if (!IsWindow(hwnd)) {
    return;
  }
  // MARGINS orders its fields left, right, top, bottom. Any non-zero edge is
  // enough to make DWM treat the window as framed and draw its shadow; one pixel
  // along the top is the smallest strip the client is certain to paint over.
  const MARGINS frame{0, 0, 1, 0};
  DwmExtendFrameIntoClientArea(hwnd, &frame);
#endif
}

void apply_window_border_color([[maybe_unused]] WId window_id) {
#ifdef Q_OS_WIN
  auto* hwnd = reinterpret_cast<HWND>(window_id);
  if (!IsWindow(hwnd)) {
    return;
  }
  const COLORREF color = dwm_border_colorref(theme().window_outer_edge);
  DwmSetWindowAttribute(hwnd, kDwmwaBorderColor, &color, sizeof(color));
#endif
}

std::uint32_t dwm_border_colorref(const QColor& color) {
  const QColor rgb = color.toRgb();
  return static_cast<std::uint32_t>(rgb.red()) | (static_cast<std::uint32_t>(rgb.green()) << 8U) |
         (static_cast<std::uint32_t>(rgb.blue()) << 16U);
}

std::intptr_t frameless_main_window_style(std::intptr_t style) {
#ifdef Q_OS_WIN
  return style | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
#else
  return style;
#endif
}

}  // namespace patchy::ui
