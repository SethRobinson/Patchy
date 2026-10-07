#pragma once

#include <QAbstractNativeEventFilter>

namespace patchy::ui {

// Alt+Space during a canvas pointer gesture (GitHub issue 78).
//
// On Windows, Qt's key mapper never delivers Space while Alt is held: it opens
// the window's system menu itself (TrackPopupMenu, a native modal loop) and
// returns before any QKeyEvent exists. Mid-drag that loop swallows the mouse
// release and the Alt/Space key-ups, so a marquee or crop drag that used Alt
// for draw-from-center and Space to reposition was left believing the button
// was still down. This application-level native filter runs ahead of the key
// mapper: while the focused CanvasWidget has a pointer gesture in flight it
// consumes the Alt+Space key messages (WM_SYSKEYDOWN, the WM_SYSCHAR that
// TranslateMessage derives from it, and the matching key-up) and hands the
// canvas the Space press/release it would have received without Alt. With no
// gesture in flight Alt+Space keeps opening the system menu.
//
// Off Windows the filter is inert and never installed.

// Installs the process-wide filter once; safe to call from every CanvasWidget
// constructor. No-op without a QCoreApplication or off Windows.
void install_alt_space_drag_filter();

// The installed filter (null off Windows), exposed so tests can feed it native
// messages directly: the offscreen platform has no Win32 message loop.
[[nodiscard]] QAbstractNativeEventFilter* alt_space_drag_native_filter();

}  // namespace patchy::ui
