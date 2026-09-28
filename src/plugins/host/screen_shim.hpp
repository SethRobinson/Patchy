#pragma once

// A virtual screen for the plug-in: classic plug-ins predate multiple monitors
// and size or place their windows from GetSystemMetrics(SM_CXSCREEN),
// GetDeviceCaps(HORZRES), the work area or the desktop window, all of which
// describe the primary monitor. The shim patches the import tables of the
// plug-in's own modules (never the system DLLs, never this helper) so those
// calls describe one rectangle instead: the work area of the monitor that
// shows Patchy, optionally capped to a smaller size and centred there, and
// windows created or moved to coordinates meant for that rectangle land on
// it. A borderless window the size of that rectangle (a full-screen canvas
// such as KPT's) is created inside a frame window of the helper's own, with a
// title bar and a taskbar button, so the user can move it. Windows only; see
// docs/plugins.md.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace patchy::legacy_host {

// Chooses the virtual screen (the work area of `anchor`'s monitor, or of the
// primary monitor when `anchor` is null, less the frame's borders; at most
// `max_width` x `max_height` when those are positive) and patches every module
// loaded so far. Modules the plug-in loads later are patched through its
// LoadLibrary imports and by `refresh_screen_shim`. `frame_title` names the
// frame window a full-screen canvas gets. `include_own_module` also patches
// this executable's imports (the --self-test-screen mode uses it; never in a
// real run).
void install_screen_shim(HWND anchor, int max_width, int max_height, const wchar_t* frame_title,
                         bool include_own_module = false);

// Patches modules loaded since the last call (cheap: already patched thunks
// are skipped).
void refresh_screen_shim();

// The virtual screen in real screen coordinates, empty before install. Once a
// frame exists this is its client area, which moves with it.
[[nodiscard]] RECT screen_shim_rect();

// The frame window holding a full-screen canvas, or null while none exists.
[[nodiscard]] HWND screen_shim_frame();

// GetWindowRect through the real function, in real screen coordinates even
// when this executable's own imports are patched (the self-test).
BOOL screen_shim_real_window_rect(HWND hwnd, RECT* rect);

// Called when the user closes the frame (its X or system menu) and the
// plug-in has not taken the hint within a second: the run is over. The pipe
// mode reports "cancelled" to Patchy and ends the process from here.
using ScreenShimCloseHandler = void (*)();
void set_screen_shim_close_handler(ScreenShimCloseHandler handler);

// The window class of the frame, so Patchy can tell it apart.
inline constexpr const wchar_t* kScreenShimFrameClass = L"PatchyPluginFrame";

}  // namespace patchy::legacy_host
