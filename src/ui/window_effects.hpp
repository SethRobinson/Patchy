#pragma once

// Rounded corners and drop shadows for the windows Patchy frames itself.
//
// Patchy draws its own title bars: MainWindow on Windows (see
// main_window_chrome.cpp) and every dialog that goes through
// install_dark_dialog_chrome. Those windows are frameless, so the OS no longer
// decorates them and they render as hard-edged rectangles. Windows 11 will still
// round and shadow a frameless window if asked, and because DWM does the
// clipping and the blur in the compositor, asking costs nothing: no translucent
// background, no widget mask, no repaint, and no change to hit-testing.
//
// Every function here is a no-op where it does not apply. The DWM attributes
// arrived in Windows 11 build 22000 and older builds simply fail the call, which
// is the same unconditional-call pattern every attribute here uses. macOS and
// Linux compile these out; their
// window managers already decorate the main window, and matching their dialogs
// would need a separate per-platform path.

#include <qwindowdefs.h>

#include <cstdint>

class QColor;
class QWidget;

namespace patchy::ui {

enum class WindowCornerRadius {
  Standard,  // The radius Windows 11 gives an ordinary window, about 8 px.
  Small,     // The tighter radius it gives menus and popups, about 4 px.
};

// Applies the effects below to `window` every time it is shown, and the border
// color again whenever its style sheet changes (a theme switch).
//
// This is the entry point callers want. Both effects are DWM attributes on a
// native window handle, which does not exist while the widget is being built and
// which Qt may replace if window flags or modality change afterwards, so they
// cannot simply be applied once in a constructor. Hooking the show keeps them
// correct without the caller having to think about handle lifetime. Safe to call
// during construction; does nothing outside Windows.
void apply_frameless_window_effects_on_show(QWidget& window, WindowCornerRadius radius);

// Rounds a top-level window's corners. The window id must belong to a window
// that already exists natively, so call this after the first show, not while
// building the widget.
void apply_rounded_window_corners(WId window_id, WindowCornerRadius radius);

// Gives a frameless window the standard Windows drop shadow.
//
// A frameless window has no native frame for DWM to shadow, so this extends a
// one-pixel frame back into the client area to give it one. The client paints
// opaque pixels over that strip, leaving only the shadow outside the window.
// MainWindow does not need this: it keeps its native frame (hidden by WM_NCCALCSIZE),
// so it already has a frame and therefore already has a shadow.
void apply_frameless_window_shadow(WId window_id);

// Colors the 1px border Windows 11 draws around a window with the theme's
// window_outer_edge role, the same color whether the window is active or not.
//
// Left to itself Windows 11 draws that border in a light gray that turns near
// white when the window loses focus; with no border at all a dark window has no
// visible edge on a dark desktop (GitHub issue 674), and one darker than the
// chrome beside it (window_border) did not read as an edge either. An explicit
// color keeps one steady edge. Call it again after a theme change: DWM keeps the color it was
// given. Windows 10 does not know the attribute and draws no border, which is why
// the frameless windows still paint their own 1px window_border edge.
void apply_window_border_color(WId window_id);

// The COLORREF (0x00BBGGRR) DWM takes for `color`.
[[nodiscard]] std::uint32_t dwm_border_colorref(const QColor& color);

// The window style the frameless main window runs with on Windows, from the style
// Qt gave it. It keeps a full native frame (WS_CAPTION, WS_THICKFRAME, the system
// menu and both size boxes) because Windows animates minimize, maximize and restore
// only for a captioned window and snaps only a sizable one; WM_NCCALCSIZE removes the
// whole non-client area, so none of that frame is ever drawn. Elsewhere it returns
// `style` unchanged.
[[nodiscard]] std::intptr_t frameless_main_window_style(std::intptr_t style);

}  // namespace patchy::ui
