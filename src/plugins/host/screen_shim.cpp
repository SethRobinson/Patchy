#include "screen_shim.hpp"

#include <shellapi.h>
#include <tlhelp32.h>
#include <windowsx.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

namespace patchy::legacy_host {

namespace {

// --- state -------------------------------------------------------------------

RECT g_screen{};             // the virtual screen, real coordinates, until a frame exists
int g_width = 0;             // the virtual screen's size
int g_height = 0;
bool g_installed = false;
bool g_include_self = false;
HMODULE g_self = nullptr;
std::wstring g_windows_dir;  // system modules live under it and are never patched
std::wstring g_frame_title;
HWND g_owner = nullptr;      // Patchy's window: the frame is owned by it, so it always stays above it
HWND g_frame = nullptr;      // created for the first full-screen canvas
HWND g_canvas = nullptr;     // the plug-in's window inside the frame
constexpr DWORD kFrameStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
constexpr DWORD kFrameExStyle = WS_EX_APPWINDOW;

bool trace_enabled() {
  static const bool enabled = [] {
    char value[8] = {};
    const DWORD length = GetEnvironmentVariableA("PATCHY_8BF_TRACE", value, sizeof(value));
    return length > 0 && length < sizeof(value) && value[0] != '0';
  }();
  return enabled;
}

// The real functions, resolved once. Every hook calls through these, never
// through the import table (which is patched in the self-test).
using GetSystemMetricsFn = int(WINAPI*)(int);
using GetDeviceCapsFn = int(WINAPI*)(HDC, int);
using SystemParametersInfoAFn = BOOL(WINAPI*)(UINT, UINT, PVOID, UINT);
using SystemParametersInfoWFn = BOOL(WINAPI*)(UINT, UINT, PVOID, UINT);
using GetWindowRectFn = BOOL(WINAPI*)(HWND, LPRECT);
using GetClientRectFn = BOOL(WINAPI*)(HWND, LPRECT);
using CreateWindowExAFn = HWND(WINAPI*)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE,
                                        LPVOID);
using CreateWindowExWFn = HWND(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE,
                                        LPVOID);
using SetWindowPosFn = BOOL(WINAPI*)(HWND, HWND, int, int, int, int, UINT);
using MoveWindowFn = BOOL(WINAPI*)(HWND, int, int, int, int, BOOL);
using ShowWindowFn = BOOL(WINAPI*)(HWND, int);
using ClipCursorFn = BOOL(WINAPI*)(const RECT*);
using GetCursorPosFn = BOOL(WINAPI*)(LPPOINT);
using SetCursorPosFn = BOOL(WINAPI*)(int, int);
using ClientToScreenFn = BOOL(WINAPI*)(HWND, LPPOINT);
using ScreenToClientFn = BOOL(WINAPI*)(HWND, LPPOINT);
using WindowFromPointFn = HWND(WINAPI*)(POINT);
using GetMessagePosFn = DWORD(WINAPI*)();
using PeekMessageAFn = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT, UINT);
using PeekMessageWFn = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT, UINT);
using GetMessageAFn = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT);
using GetMessageWFn = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT);
using LoadLibraryAFn = HMODULE(WINAPI*)(LPCSTR);
using LoadLibraryWFn = HMODULE(WINAPI*)(LPCWSTR);
using LoadLibraryExAFn = HMODULE(WINAPI*)(LPCSTR, HANDLE, DWORD);
using LoadLibraryExWFn = HMODULE(WINAPI*)(LPCWSTR, HANDLE, DWORD);

GetSystemMetricsFn g_real_GetSystemMetrics = nullptr;
GetDeviceCapsFn g_real_GetDeviceCaps = nullptr;
SystemParametersInfoAFn g_real_SystemParametersInfoA = nullptr;
SystemParametersInfoWFn g_real_SystemParametersInfoW = nullptr;
GetWindowRectFn g_real_GetWindowRect = nullptr;
GetClientRectFn g_real_GetClientRect = nullptr;
CreateWindowExAFn g_real_CreateWindowExA = nullptr;
CreateWindowExWFn g_real_CreateWindowExW = nullptr;
SetWindowPosFn g_real_SetWindowPos = nullptr;
MoveWindowFn g_real_MoveWindow = nullptr;
ShowWindowFn g_real_ShowWindow = nullptr;
ClipCursorFn g_real_ClipCursor = nullptr;
GetCursorPosFn g_real_GetCursorPos = nullptr;
SetCursorPosFn g_real_SetCursorPos = nullptr;
ClientToScreenFn g_real_ClientToScreen = nullptr;
ScreenToClientFn g_real_ScreenToClient = nullptr;
WindowFromPointFn g_real_WindowFromPoint = nullptr;
GetMessagePosFn g_real_GetMessagePos = nullptr;
PeekMessageAFn g_real_PeekMessageA = nullptr;
PeekMessageWFn g_real_PeekMessageW = nullptr;
GetMessageAFn g_real_GetMessageA = nullptr;
GetMessageWFn g_real_GetMessageW = nullptr;
LoadLibraryAFn g_real_LoadLibraryA = nullptr;
LoadLibraryWFn g_real_LoadLibraryW = nullptr;
LoadLibraryExAFn g_real_LoadLibraryExA = nullptr;
LoadLibraryExWFn g_real_LoadLibraryExW = nullptr;

// --- the frame ---------------------------------------------------------------

// Where the virtual screen currently is: the frame's client area once one
// exists (the user may have moved it), the chosen rectangle before that.
RECT current_screen() {
  if (g_frame != nullptr && IsWindow(g_frame)) {
    POINT origin{0, 0};
    g_real_ClientToScreen(g_frame, &origin);
    return RECT{origin.x, origin.y, origin.x + g_width, origin.y + g_height};
  }
  return g_screen;
}

// Two coordinate models. Before a canvas exists (ordinary dialog plug-ins),
// everything keeps real screen coordinates and only creation and moves are
// nudged onto the virtual screen by the overlap rule below. Once a canvas is
// framed, the plug-in lives in a consistent virtual space whose origin is the
// canvas: full-screen interfaces treat screen and canvas coordinates as one
// and hit-test the mouse against layouts built for (0,0), so every screen
// coordinate API is offset by the frame's client origin, both ways.
bool virtual_space() { return g_canvas != nullptr && IsWindow(g_canvas); }

POINT origin() {
  const RECT screen = current_screen();
  return POINT{screen.left, screen.top};
}

ScreenShimCloseHandler g_close_handler = nullptr;
constexpr UINT_PTR kCloseTimer = 1;

LRESULT CALLBACK frame_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_CLOSE:
      // The user closed the frame: that is Cancel. The plug-in gets the
      // hint first (Escape, the cancel key of every plug-in interface, then a
      // close request); if its canvas is still there a second later, the run
      // is ended as cancelled from here, since a plug-in that ignores both
      // would otherwise keep a window the user cannot get rid of.
      if (g_canvas != nullptr && IsWindow(g_canvas)) {
        PostMessageW(g_canvas, WM_KEYDOWN, VK_ESCAPE, 0x00010001);
        PostMessageW(g_canvas, WM_KEYUP, VK_ESCAPE, 0xC0010001);
        PostMessageW(g_canvas, WM_CLOSE, 0, 0);
        SetTimer(hwnd, kCloseTimer, 1000, nullptr);
      } else if (g_close_handler != nullptr) {
        g_close_handler();
      }
      return 0;
    case WM_TIMER:
      if (wparam == kCloseTimer) {
        KillTimer(hwnd, kCloseTimer);
        if (g_canvas != nullptr && IsWindow(g_canvas) && IsWindowVisible(hwnd) && g_close_handler != nullptr) {
          g_close_handler();
        }
      }
      return 0;
    case WM_SETFOCUS:
      if (g_canvas != nullptr && IsWindow(g_canvas)) {
        SetFocus(g_canvas);
      }
      return 0;
    case WM_PARENTNOTIFY:
      // The canvas is gone: the frame goes with it instead of lingering
      // empty until the run ends.
      if (LOWORD(wparam) == WM_DESTROY && reinterpret_cast<HWND>(lparam) == g_canvas) {
        g_canvas = nullptr;
        g_real_ShowWindow(hwnd, SW_HIDE);
      }
      return 0;
    default:
      return DefWindowProcW(hwnd, message, wparam, lparam);
  }
}

HWND ensure_frame() {
  if (g_frame != nullptr && IsWindow(g_frame)) {
    return g_frame;
  }
  static bool registered = false;
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  if (!registered) {
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.style = CS_DBLCLKS;
    cls.lpfnWndProc = frame_proc;
    cls.hInstance = instance;
    cls.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW, without the TCHAR macro
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    cls.lpszClassName = kScreenShimFrameClass;
    // Patchy's own icon, from the application next to this helper.
    wchar_t path[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, path, MAX_PATH) > 0) {
      std::wstring app(path);
      if (const auto slash = app.find_last_of(L'\\'); slash != std::wstring::npos) {
        app = app.substr(0, slash + 1) + L"patchy.exe";
        HICON large = nullptr;
        HICON small = nullptr;
        if (ExtractIconExW(app.c_str(), 0, &large, &small, 1) > 0) {
          cls.hIcon = large;
          cls.hIconSm = small;
        }
      }
    }
    registered = RegisterClassExW(&cls) != 0;
  }
  RECT rect = g_screen;
  AdjustWindowRectEx(&rect, kFrameStyle, FALSE, kFrameExStyle);
  // Owned by Patchy's window (an owner in another process is allowed): an
  // owned window always stays above its owner in the z-order, whichever of
  // the two processes is active, so nothing Patchy does (its progress box,
  // its own activation) can push the plug-in behind it. It also minimizes
  // and closes with Patchy.
  const HWND owner = g_owner != nullptr && IsWindow(g_owner) ? g_owner : nullptr;
  g_frame = g_real_CreateWindowExW(kFrameExStyle, kScreenShimFrameClass, g_frame_title.c_str(), kFrameStyle, rect.left,
                                   rect.top, rect.right - rect.left, rect.bottom - rect.top, owner, nullptr, instance,
                                   nullptr);
  if (trace_enabled()) {
    std::fprintf(stderr, "[8bf] virtual screen: frame %p '%ls' at %ld,%ld\n", static_cast<void*>(g_frame),
                 g_frame_title.c_str(), rect.left, rect.top);
  }
  return g_frame;
}

void show_frame() {
  if (g_frame != nullptr && IsWindow(g_frame) && !IsWindowVisible(g_frame)) {
    g_real_ShowWindow(g_frame, SW_SHOWNORMAL);
    // In front of Patchy (which allowed this process the foreground before
    // starting it); a plug-in's own SetForegroundWindow targets its canvas,
    // which is a child now, so the frame takes over that job.
    SetForegroundWindow(g_frame);
  }
}

bool is_canvas(HWND hwnd);

// Activation requests aimed at the canvas go to its frame.
using SetForegroundWindowFn = BOOL(WINAPI*)(HWND);
using SetActiveWindowFn = HWND(WINAPI*)(HWND);
using BringWindowToTopFn = BOOL(WINAPI*)(HWND);
SetForegroundWindowFn g_real_SetForegroundWindow = nullptr;
SetActiveWindowFn g_real_SetActiveWindow = nullptr;
BringWindowToTopFn g_real_BringWindowToTop = nullptr;

BOOL WINAPI hook_SetForegroundWindow(HWND hwnd) {
  if (is_canvas(hwnd) && g_frame != nullptr) {
    show_frame();
    return g_real_SetForegroundWindow(g_frame);
  }
  return g_real_SetForegroundWindow(hwnd);
}

HWND WINAPI hook_SetActiveWindow(HWND hwnd) {
  if (is_canvas(hwnd) && g_frame != nullptr) {
    return g_real_SetActiveWindow(g_frame);
  }
  return g_real_SetActiveWindow(hwnd);
}

BOOL WINAPI hook_BringWindowToTop(HWND hwnd) {
  if (is_canvas(hwnd) && g_frame != nullptr) {
    return g_real_BringWindowToTop(g_frame);
  }
  return g_real_BringWindowToTop(hwnd);
}

bool is_canvas(HWND hwnd) { return hwnd != nullptr && hwnd == g_canvas; }

// A click inside the canvas must activate the frame. Activation-on-click goes
// through WM_MOUSEACTIVATE, which the canvas answers first; a plug-in window
// procedure that swallows it (KPT's returns 0) leaves the frame inactive, so
// the canvas is subclassed to answer it with "activate" and to raise the frame
// on every button press (this process just received the click, so Windows
// allows the foreground change).
WNDPROC g_canvas_proc = nullptr;
bool g_canvas_unicode = false;  // subclassed through the A or the W entry points, matching the window

LRESULT CALLBACK canvas_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_MOUSEACTIVATE:
      if (g_frame != nullptr && IsWindow(g_frame) && GetForegroundWindow() != g_frame) {
        SetForegroundWindow(g_frame);
      }
      return MA_ACTIVATE;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
      if (g_frame != nullptr && IsWindow(g_frame) && GetForegroundWindow() != g_frame) {
        SetForegroundWindow(g_frame);
      }
      break;
    case WM_NCDESTROY:
      if (hwnd == g_canvas) {
        g_canvas = nullptr;
      }
      break;
    default:
      break;
  }
  if (g_canvas_proc == nullptr) {
    return DefWindowProcW(hwnd, message, wparam, lparam);
  }
  return g_canvas_unicode ? CallWindowProcW(g_canvas_proc, hwnd, message, wparam, lparam)
                          : CallWindowProcA(g_canvas_proc, hwnd, message, wparam, lparam);
}

void subclass_canvas(HWND hwnd) {
  g_canvas = hwnd;
  g_canvas_unicode = IsWindowUnicode(hwnd) != FALSE;
  const auto replacement = reinterpret_cast<LONG_PTR>(&canvas_proc);
  g_canvas_proc = reinterpret_cast<WNDPROC>(g_canvas_unicode ? SetWindowLongPtrW(hwnd, GWLP_WNDPROC, replacement)
                                                             : SetWindowLongPtrA(hwnd, GWLP_WNDPROC, replacement));
}

// A borderless top-level window the size of the virtual screen is a
// full-screen canvas: it goes inside the frame. Windows with their own
// caption already have a title bar to move them by and stay top-level.
bool is_canvas_size(int width, int height) {
  return width != CW_USEDEFAULT && height != CW_USEDEFAULT && width >= g_width * 9 / 10 &&
         height >= g_height * 9 / 10;
}

bool wants_frame(DWORD style, int x, int width, int height) {
  return (style & (WS_CHILD | WS_CAPTION)) == 0 && x != CW_USEDEFAULT && is_canvas_size(width, height);
}

// Some canvases start as a small stub and grow to the screen size a moment
// later (KPT's MetaOS): the first resize to canvas size moves the window
// into the frame, as creation at that size would have.
bool adopt_canvas(HWND hwnd) {
  if (g_canvas != nullptr || hwnd == nullptr || hwnd == g_frame) {
    return false;
  }
  const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
  if ((style & (WS_CHILD | WS_CAPTION)) != 0 || ensure_frame() == nullptr) {
    return false;
  }
  const bool visible = (style & WS_VISIBLE) != 0;
  SetWindowLongPtrW(hwnd, GWL_STYLE,
                    (style & ~static_cast<LONG_PTR>(WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX |
                                                    WS_MAXIMIZEBOX)) |
                        WS_CHILD);
  SetWindowLongPtrW(hwnd, GWL_EXSTYLE,
                    GetWindowLongPtrW(hwnd, GWL_EXSTYLE) &
                        ~static_cast<LONG_PTR>(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOPARENTNOTIFY));
  SetParent(hwnd, g_frame);
  g_real_SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                      SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  subclass_canvas(hwnd);
  if (visible) {
    show_frame();
  }
  if (trace_enabled()) {
    std::fprintf(stderr, "[8bf] virtual screen: canvas %p adopted into the frame\n", static_cast<void*>(hwnd));
  }
  return true;
}

// --- placement ---------------------------------------------------------------

std::int64_t overlap_area(const RECT& a, const RECT& b) {
  const auto left = std::max(a.left, b.left);
  const auto top = std::max(a.top, b.top);
  const auto right = std::min(a.right, b.right);
  const auto bottom = std::min(a.bottom, b.bottom);
  if (right <= left || bottom <= top) {
    return 0;
  }
  return static_cast<std::int64_t>(right - left) * static_cast<std::int64_t>(bottom - top);
}

// Before a canvas exists a plug-in hands over either coordinates meant for the
// virtual screen (its idea of "the screen", origin 0,0) or real ones it read
// back from a window, such as its owner. The two cannot be told apart by
// value, so the placement that overlaps the virtual screen more wins: a
// window at (0,0) moves onto it, a dialog centred on Patchy's window stays.
// In the virtual space every coordinate is virtual and is always offset.
bool should_translate(const RECT& screen, int x, int y, int width, int height) {
  if (!g_installed) {
    return false;
  }
  if (virtual_space()) {
    return true;
  }
  if (screen.left == 0 && screen.top == 0) {
    return false;
  }
  const int w = std::max(width, 1);
  const int h = std::max(height, 1);
  const RECT as_is{x, y, x + w, y + h};
  const RECT moved{x + screen.left, y + screen.top, x + screen.left + w, y + screen.top + h};
  return overlap_area(moved, screen) > overlap_area(as_is, screen);
}

bool is_top_level(HWND hwnd) {
  return hwnd != nullptr && (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD) == 0;
}

bool is_desktop(HWND hwnd) { return hwnd != nullptr && hwnd == GetDesktopWindow(); }

bool is_display_dc(HDC hdc) { return g_real_GetDeviceCaps(hdc, TECHNOLOGY) == DT_RASDISPLAY; }

// --- hooks: metrics ----------------------------------------------------------

int WINAPI hook_GetSystemMetrics(int index) {
  switch (index) {
    case SM_CXSCREEN:
    case SM_CXFULLSCREEN:
    case SM_CXMAXIMIZED:
      return g_width;
    case SM_CYSCREEN:
    case SM_CYFULLSCREEN:
    case SM_CYMAXIMIZED:
      return g_height;
    default:
      return g_real_GetSystemMetrics(index);
  }
}

int WINAPI hook_GetDeviceCaps(HDC hdc, int index) {
  switch (index) {
    case HORZRES:
    case DESKTOPHORZRES:
      return is_display_dc(hdc) ? g_width : g_real_GetDeviceCaps(hdc, index);
    case VERTRES:
    case DESKTOPVERTRES:
      return is_display_dc(hdc) ? g_height : g_real_GetDeviceCaps(hdc, index);
    default:
      return g_real_GetDeviceCaps(hdc, index);
  }
}

BOOL WINAPI hook_SystemParametersInfoA(UINT action, UINT param, PVOID data, UINT flags) {
  if (action == SPI_GETWORKAREA && data != nullptr) {
    *static_cast<RECT*>(data) = RECT{0, 0, g_width, g_height};
    return TRUE;
  }
  return g_real_SystemParametersInfoA(action, param, data, flags);
}

BOOL WINAPI hook_SystemParametersInfoW(UINT action, UINT param, PVOID data, UINT flags) {
  if (action == SPI_GETWORKAREA && data != nullptr) {
    *static_cast<RECT*>(data) = RECT{0, 0, g_width, g_height};
    return TRUE;
  }
  return g_real_SystemParametersInfoW(action, param, data, flags);
}

// --- hooks: coordinates ------------------------------------------------------

BOOL WINAPI hook_GetWindowRect(HWND hwnd, LPRECT rect) {
  if (is_desktop(hwnd) && rect != nullptr) {
    *rect = RECT{0, 0, g_width, g_height};
    return TRUE;
  }
  const BOOL ok = g_real_GetWindowRect(hwnd, rect);
  if (ok && rect != nullptr && virtual_space()) {
    const POINT o = origin();
    OffsetRect(rect, -o.x, -o.y);
  }
  return ok;
}

BOOL WINAPI hook_GetClientRect(HWND hwnd, LPRECT rect) {
  if (is_desktop(hwnd) && rect != nullptr) {
    *rect = RECT{0, 0, g_width, g_height};
    return TRUE;
  }
  return g_real_GetClientRect(hwnd, rect);
}

BOOL WINAPI hook_GetCursorPos(LPPOINT point) {
  const BOOL ok = g_real_GetCursorPos(point);
  if (ok && point != nullptr && virtual_space()) {
    const POINT o = origin();
    point->x -= o.x;
    point->y -= o.y;
  }
  return ok;
}

BOOL WINAPI hook_SetCursorPos(int x, int y) {
  if (virtual_space()) {
    const POINT o = origin();
    x += o.x;
    y += o.y;
  }
  return g_real_SetCursorPos(x, y);
}

BOOL WINAPI hook_ClientToScreen(HWND hwnd, LPPOINT point) {
  const BOOL ok = g_real_ClientToScreen(hwnd, point);
  if (ok && point != nullptr && virtual_space()) {
    const POINT o = origin();
    point->x -= o.x;
    point->y -= o.y;
  }
  return ok;
}

BOOL WINAPI hook_ScreenToClient(HWND hwnd, LPPOINT point) {
  if (point != nullptr && virtual_space()) {
    const POINT o = origin();
    point->x += o.x;
    point->y += o.y;
  }
  return g_real_ScreenToClient(hwnd, point);
}

HWND WINAPI hook_WindowFromPoint(POINT point) {
  if (virtual_space()) {
    const POINT o = origin();
    point.x += o.x;
    point.y += o.y;
  }
  return g_real_WindowFromPoint(point);
}

DWORD WINAPI hook_GetMessagePos() {
  const DWORD packed = g_real_GetMessagePos();
  if (!virtual_space()) {
    return packed;
  }
  const POINT o = origin();
  return MAKELONG(static_cast<short>(GET_X_LPARAM(packed) - o.x), static_cast<short>(GET_Y_LPARAM(packed) - o.y));
}

// Posted messages whose lParam carries screen coordinates get the same
// offset when the plug-in's own loop retrieves them.
void translate_message(MSG* msg) {
  if (msg == nullptr || !virtual_space()) {
    return;
  }
  switch (msg->message) {
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    case WM_NCMOUSEMOVE:
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONUP:
    case WM_NCLBUTTONDBLCLK:
    case WM_NCRBUTTONDOWN:
    case WM_NCRBUTTONUP:
    case WM_NCRBUTTONDBLCLK:
    case WM_NCMBUTTONDOWN:
    case WM_NCMBUTTONUP:
    case WM_NCMBUTTONDBLCLK:
      break;
    case WM_CONTEXTMENU:
      if (msg->lParam == static_cast<LPARAM>(-1)) {
        return;
      }
      break;
    default:
      return;
  }
  const POINT o = origin();
  msg->lParam = MAKELPARAM(static_cast<short>(GET_X_LPARAM(msg->lParam) - o.x),
                           static_cast<short>(GET_Y_LPARAM(msg->lParam) - o.y));
}

BOOL WINAPI hook_PeekMessageA(LPMSG msg, HWND hwnd, UINT min, UINT max, UINT remove) {
  const BOOL got = g_real_PeekMessageA(msg, hwnd, min, max, remove);
  if (got) {
    translate_message(msg);
  }
  return got;
}

BOOL WINAPI hook_PeekMessageW(LPMSG msg, HWND hwnd, UINT min, UINT max, UINT remove) {
  const BOOL got = g_real_PeekMessageW(msg, hwnd, min, max, remove);
  if (got) {
    translate_message(msg);
  }
  return got;
}

BOOL WINAPI hook_GetMessageA(LPMSG msg, HWND hwnd, UINT min, UINT max) {
  const BOOL got = g_real_GetMessageA(msg, hwnd, min, max);
  if (got > 0) {
    translate_message(msg);
  }
  return got;
}

BOOL WINAPI hook_GetMessageW(LPMSG msg, HWND hwnd, UINT min, UINT max) {
  const BOOL got = g_real_GetMessageW(msg, hwnd, min, max);
  if (got > 0) {
    translate_message(msg);
  }
  return got;
}

// --- hooks: windows ----------------------------------------------------------

// Shared by both CreateWindowEx hooks: either frames a full-screen canvas
// (child of the frame, at its client origin) or moves a top-level window
// whose coordinates were meant for the virtual screen onto it.
struct Creation {
  DWORD ex_style;
  DWORD style;
  int x;
  int y;
  HWND parent;
  bool framed{false};
};

void prepare_creation(Creation& c, int width, int height) {
  if (wants_frame(c.style, c.x, width, height) && g_canvas == nullptr && ensure_frame() != nullptr) {
    c.framed = true;
    c.parent = g_frame;
    c.style = (c.style & ~(WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) | WS_CHILD;
    c.ex_style &= ~static_cast<DWORD>(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOPARENTNOTIFY);
    c.x = 0;
    c.y = 0;
    if ((c.style & WS_VISIBLE) != 0) {
      show_frame();
    }
    return;
  }
  if ((c.style & WS_CHILD) != 0 || c.x == CW_USEDEFAULT) {
    return;
  }
  const int w = width == CW_USEDEFAULT ? 0 : width;
  const int h = height == CW_USEDEFAULT ? 0 : height;
  const RECT screen = current_screen();
  if (should_translate(screen, c.x, c.y, w, h)) {
    c.x += screen.left;
    c.y += screen.top;
  }
}

void note_created(const Creation& c, HWND hwnd) {
  if (c.framed && hwnd != nullptr) {
    subclass_canvas(hwnd);
    if (trace_enabled()) {
      std::fprintf(stderr, "[8bf] virtual screen: canvas %p framed\n", static_cast<void*>(hwnd));
    }
  }
}

HWND WINAPI hook_CreateWindowExA(DWORD ex_style, LPCSTR class_name, LPCSTR window_name, DWORD style, int x, int y,
                                 int width, int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID param) {
  Creation c{ex_style, style, x, y, parent};
  prepare_creation(c, width, height);
  HWND hwnd = g_real_CreateWindowExA(c.ex_style, class_name, window_name, c.style, c.x, c.y, width, height, c.parent,
                                     menu, instance, param);
  note_created(c, hwnd);
  return hwnd;
}

HWND WINAPI hook_CreateWindowExW(DWORD ex_style, LPCWSTR class_name, LPCWSTR window_name, DWORD style, int x, int y,
                                 int width, int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID param) {
  Creation c{ex_style, style, x, y, parent};
  prepare_creation(c, width, height);
  HWND hwnd = g_real_CreateWindowExW(c.ex_style, class_name, window_name, c.style, c.x, c.y, width, height, c.parent,
                                     menu, instance, param);
  note_created(c, hwnd);
  return hwnd;
}

BOOL WINAPI hook_SetWindowPos(HWND hwnd, HWND insert_after, int x, int y, int width, int height, UINT flags) {
  if (!is_canvas(hwnd) && (flags & SWP_NOSIZE) == 0 && is_top_level(hwnd) && is_canvas_size(width, height) &&
      adopt_canvas(hwnd)) {
    // Just adopted: it fills the frame's client area from its origin.
    x = 0;
    y = 0;
    flags &= ~static_cast<UINT>(SWP_NOMOVE);
  }
  if (is_canvas(hwnd)) {
    // Inside the frame: coordinates are already client-relative; topmost
    // requests mean "in front of my siblings" there.
    if (insert_after == HWND_TOPMOST || insert_after == HWND_NOTOPMOST) {
      insert_after = HWND_TOP;
    }
    if ((flags & SWP_SHOWWINDOW) != 0) {
      show_frame();
    }
    return g_real_SetWindowPos(hwnd, insert_after, x, y, width, height, flags);
  }
  if ((flags & SWP_NOMOVE) == 0 && is_top_level(hwnd)) {
    int w = width;
    int h = height;
    if ((flags & SWP_NOSIZE) != 0) {
      RECT current{};
      if (g_real_GetWindowRect(hwnd, &current)) {
        w = current.right - current.left;
        h = current.bottom - current.top;
      }
    }
    const RECT screen = current_screen();
    if (should_translate(screen, x, y, w, h)) {
      x += screen.left;
      y += screen.top;
    }
  }
  return g_real_SetWindowPos(hwnd, insert_after, x, y, width, height, flags);
}

BOOL WINAPI hook_MoveWindow(HWND hwnd, int x, int y, int width, int height, BOOL repaint) {
  if (!is_canvas(hwnd) && is_top_level(hwnd) && is_canvas_size(width, height) && adopt_canvas(hwnd)) {
    x = 0;
    y = 0;
  }
  if (!is_canvas(hwnd) && is_top_level(hwnd)) {
    const RECT screen = current_screen();
    if (should_translate(screen, x, y, width, height)) {
      x += screen.left;
      y += screen.top;
    }
  }
  return g_real_MoveWindow(hwnd, x, y, width, height, repaint);
}

BOOL WINAPI hook_ShowWindow(HWND hwnd, int command) {
  if (is_canvas(hwnd) && command != SW_HIDE) {
    show_frame();
  }
  return g_real_ShowWindow(hwnd, command);
}

BOOL WINAPI hook_ClipCursor(const RECT* rect) {
  if (rect != nullptr) {
    const RECT screen = current_screen();
    if (should_translate(screen, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top)) {
      const RECT moved{rect->left + screen.left, rect->top + screen.top, rect->right + screen.left,
                       rect->bottom + screen.top};
      return g_real_ClipCursor(&moved);
    }
  }
  return g_real_ClipCursor(rect);
}

HMODULE WINAPI hook_LoadLibraryA(LPCSTR name) {
  HMODULE module = g_real_LoadLibraryA(name);
  refresh_screen_shim();
  return module;
}

HMODULE WINAPI hook_LoadLibraryW(LPCWSTR name) {
  HMODULE module = g_real_LoadLibraryW(name);
  refresh_screen_shim();
  return module;
}

HMODULE WINAPI hook_LoadLibraryExA(LPCSTR name, HANDLE file, DWORD flags) {
  HMODULE module = g_real_LoadLibraryExA(name, file, flags);
  refresh_screen_shim();
  return module;
}

HMODULE WINAPI hook_LoadLibraryExW(LPCWSTR name, HANDLE file, DWORD flags) {
  HMODULE module = g_real_LoadLibraryExW(name, file, flags);
  refresh_screen_shim();
  return module;
}

// --- import patching ---------------------------------------------------------

struct Hook {
  const char* name;
  void* replacement;
  std::vector<void*> targets;  // every address an import of this name may resolve to
};

std::vector<Hook> g_hooks;

template <typename Fn>
void add_hook(const char* name, Fn* replacement, Fn*& real, std::initializer_list<HMODULE> modules) {
  Hook hook;
  hook.name = name;
  hook.replacement = reinterpret_cast<void*>(replacement);
  for (HMODULE module : modules) {
    if (module == nullptr) {
      continue;
    }
    if (auto* proc = GetProcAddress(module, name); proc != nullptr) {
      hook.targets.push_back(reinterpret_cast<void*>(proc));
      if (real == nullptr) {
        real = reinterpret_cast<Fn*>(proc);
      }
    }
  }
  if (real != nullptr) {
    g_hooks.push_back(std::move(hook));
  }
}

bool starts_with_no_case(const std::wstring& text, const std::wstring& prefix) {
  return text.size() >= prefix.size() && _wcsnicmp(text.c_str(), prefix.c_str(), prefix.size()) == 0;
}

// Rewrites the import thunks of one loaded module that point at hooked
// functions. Returns how many were rewritten.
int patch_module(HMODULE module) {
  auto* base = reinterpret_cast<std::uint8_t*>(module);
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
    return 0;
  }
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) {
    return 0;
  }
  const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (directory.VirtualAddress == 0 || directory.Size == 0) {
    return 0;
  }
  int patched = 0;
  const auto* descriptor = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
  for (; descriptor->Name != 0; ++descriptor) {
    if (descriptor->FirstThunk == 0) {
      continue;
    }
    auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
    for (; thunk->u1.Function != 0; ++thunk) {
      auto* current = reinterpret_cast<void*>(thunk->u1.Function);
      for (const auto& hook : g_hooks) {
        if (std::find(hook.targets.begin(), hook.targets.end(), current) == hook.targets.end()) {
          continue;
        }
        DWORD old_protect = 0;
        if (VirtualProtect(&thunk->u1.Function, sizeof(thunk->u1.Function), PAGE_READWRITE, &old_protect)) {
          thunk->u1.Function = reinterpret_cast<decltype(thunk->u1.Function)>(hook.replacement);
          VirtualProtect(&thunk->u1.Function, sizeof(thunk->u1.Function), old_protect, &old_protect);
          ++patched;
        }
        break;
      }
    }
  }
  return patched;
}

bool should_patch(HMODULE module, const std::wstring& path) {
  if (module == g_self) {
    return g_include_self;
  }
  if (!g_windows_dir.empty() && starts_with_no_case(path, g_windows_dir)) {
    return false;  // system DLLs keep their real imports
  }
  return true;
}

}  // namespace

void refresh_screen_shim() {
  if (!g_installed) {
    return;
  }
  // A frame whose canvas the plug-in has destroyed is destroyed too, not just
  // hidden: destroying an owned window makes Windows activate the owner
  // (Patchy) through the normal messages, which is what Qt needs to mark its
  // window active again; a hidden frame in a live process hands focus back
  // silently and left Patchy's hotkeys dead until the user switched apps.
  // This runs after every selector call and on every library load, both of
  // which happen on the plug-in's own thread, outside its canvas's teardown.
  if (g_frame != nullptr && IsWindow(g_frame) && (g_canvas == nullptr || !IsWindow(g_canvas))) {
    DestroyWindow(g_frame);
    g_frame = nullptr;
    g_canvas = nullptr;
  }
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
  if (snapshot == INVALID_HANDLE_VALUE) {
    return;
  }
  MODULEENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  for (BOOL more = Module32FirstW(snapshot, &entry); more; more = Module32NextW(snapshot, &entry)) {
    if (!should_patch(entry.hModule, entry.szExePath)) {
      continue;
    }
    const int patched = patch_module(entry.hModule);
    if (patched > 0 && trace_enabled()) {
      std::fprintf(stderr, "[8bf] virtual screen: %d imports redirected in %ls\n", patched, entry.szModule);
    }
  }
  CloseHandle(snapshot);
}

void install_screen_shim(HWND anchor, int max_width, int max_height, const wchar_t* frame_title,
                         bool include_own_module) {
  if (g_installed) {
    return;
  }
  g_frame_title = frame_title != nullptr ? frame_title : L"";
  g_owner = anchor;
  HMONITOR monitor = anchor != nullptr ? MonitorFromWindow(anchor, MONITOR_DEFAULTTOPRIMARY)
                                       : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  RECT work{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
  if (monitor != nullptr && GetMonitorInfoW(monitor, &info)) {
    work = info.rcWork;
  }
  // The frame's borders and title bar must fit the work area too.
  RECT borders{0, 0, 0, 0};
  AdjustWindowRectEx(&borders, kFrameStyle, FALSE, kFrameExStyle);
  const int work_width = (work.right - work.left) - (borders.right - borders.left);
  const int work_height = (work.bottom - work.top) - (borders.bottom - borders.top);
  g_width = max_width > 0 ? std::min(max_width, work_width) : work_width;
  g_height = max_height > 0 ? std::min(max_height, work_height) : work_height;
  g_screen.left = work.left - borders.left + (work_width - g_width) / 2;
  g_screen.top = work.top - borders.top + (work_height - g_height) / 2;
  g_screen.right = g_screen.left + g_width;
  g_screen.bottom = g_screen.top + g_height;

  g_self = GetModuleHandleW(nullptr);
  g_include_self = include_own_module;
  wchar_t windows_dir[MAX_PATH] = {};
  if (GetWindowsDirectoryW(windows_dir, MAX_PATH) > 0) {
    g_windows_dir = windows_dir;
    if (!g_windows_dir.empty() && g_windows_dir.back() != L'\\') {
      g_windows_dir.push_back(L'\\');
    }
  }

  HMODULE user32 = GetModuleHandleW(L"user32.dll");
  HMODULE gdi32 = GetModuleHandleW(L"gdi32.dll");
  HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
  HMODULE kernelbase = GetModuleHandleW(L"kernelbase.dll");
  add_hook("GetSystemMetrics", &hook_GetSystemMetrics, g_real_GetSystemMetrics, {user32});
  add_hook("GetDeviceCaps", &hook_GetDeviceCaps, g_real_GetDeviceCaps, {gdi32});
  add_hook("SystemParametersInfoA", &hook_SystemParametersInfoA, g_real_SystemParametersInfoA, {user32});
  add_hook("SystemParametersInfoW", &hook_SystemParametersInfoW, g_real_SystemParametersInfoW, {user32});
  add_hook("GetWindowRect", &hook_GetWindowRect, g_real_GetWindowRect, {user32});
  add_hook("GetClientRect", &hook_GetClientRect, g_real_GetClientRect, {user32});
  add_hook("GetCursorPos", &hook_GetCursorPos, g_real_GetCursorPos, {user32});
  add_hook("SetCursorPos", &hook_SetCursorPos, g_real_SetCursorPos, {user32});
  add_hook("ClientToScreen", &hook_ClientToScreen, g_real_ClientToScreen, {user32});
  add_hook("ScreenToClient", &hook_ScreenToClient, g_real_ScreenToClient, {user32});
  add_hook("WindowFromPoint", &hook_WindowFromPoint, g_real_WindowFromPoint, {user32});
  add_hook("GetMessagePos", &hook_GetMessagePos, g_real_GetMessagePos, {user32});
  add_hook("PeekMessageA", &hook_PeekMessageA, g_real_PeekMessageA, {user32});
  add_hook("PeekMessageW", &hook_PeekMessageW, g_real_PeekMessageW, {user32});
  add_hook("GetMessageA", &hook_GetMessageA, g_real_GetMessageA, {user32});
  add_hook("GetMessageW", &hook_GetMessageW, g_real_GetMessageW, {user32});
  add_hook("CreateWindowExA", &hook_CreateWindowExA, g_real_CreateWindowExA, {user32});
  add_hook("CreateWindowExW", &hook_CreateWindowExW, g_real_CreateWindowExW, {user32});
  add_hook("SetWindowPos", &hook_SetWindowPos, g_real_SetWindowPos, {user32});
  add_hook("MoveWindow", &hook_MoveWindow, g_real_MoveWindow, {user32});
  add_hook("ShowWindow", &hook_ShowWindow, g_real_ShowWindow, {user32});
  add_hook("SetForegroundWindow", &hook_SetForegroundWindow, g_real_SetForegroundWindow, {user32});
  add_hook("SetActiveWindow", &hook_SetActiveWindow, g_real_SetActiveWindow, {user32});
  add_hook("BringWindowToTop", &hook_BringWindowToTop, g_real_BringWindowToTop, {user32});
  add_hook("ClipCursor", &hook_ClipCursor, g_real_ClipCursor, {user32});
  add_hook("LoadLibraryA", &hook_LoadLibraryA, g_real_LoadLibraryA, {kernel32, kernelbase});
  add_hook("LoadLibraryW", &hook_LoadLibraryW, g_real_LoadLibraryW, {kernel32, kernelbase});
  add_hook("LoadLibraryExA", &hook_LoadLibraryExA, g_real_LoadLibraryExA, {kernel32, kernelbase});
  add_hook("LoadLibraryExW", &hook_LoadLibraryExW, g_real_LoadLibraryExW, {kernel32, kernelbase});
  g_installed = true;
  if (trace_enabled()) {
    std::fprintf(stderr, "[8bf] virtual screen: %ld,%ld %dx%d (monitor work area %ld,%ld %ldx%ld, cap %dx%d)\n",
                 g_screen.left, g_screen.top, g_width, g_height, work.left, work.top, work.right - work.left,
                 work.bottom - work.top, max_width, max_height);
  }
  refresh_screen_shim();
}

RECT screen_shim_rect() { return g_installed ? current_screen() : RECT{}; }

HWND screen_shim_frame() { return g_frame != nullptr && IsWindow(g_frame) ? g_frame : nullptr; }

void set_screen_shim_close_handler(ScreenShimCloseHandler handler) { g_close_handler = handler; }

BOOL screen_shim_real_window_rect(HWND hwnd, RECT* rect) {
  return g_real_GetWindowRect != nullptr ? g_real_GetWindowRect(hwnd, rect) : GetWindowRect(hwnd, rect);
}

}  // namespace patchy::legacy_host
