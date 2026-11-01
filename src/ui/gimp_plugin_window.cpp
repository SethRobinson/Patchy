// Reaching a GIMP plug-in's own dialog (gimp_plugin_window.hpp).

#include "ui/gimp_plugin_window.hpp"

#include <QImage>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace patchy::ui {

#ifdef Q_OS_WIN

std::uintptr_t gimp_plugin_window_for_process(std::uint64_t pid) {
  struct Search {
    DWORD pid;
    HWND best{nullptr};
    long long best_area{0};
  } search{static_cast<DWORD>(pid)};
  EnumWindows(
      [](HWND hwnd, LPARAM data) -> BOOL {
        auto* s = reinterpret_cast<Search*>(data);
        DWORD owner = 0;
        GetWindowThreadProcessId(hwnd, &owner);
        if (owner != s->pid || !IsWindowVisible(hwnd)) {
          return TRUE;
        }
        RECT r{};
        GetWindowRect(hwnd, &r);
        const long long area = static_cast<long long>(r.right - r.left) * (r.bottom - r.top);
        if (area > s->best_area) {
          s->best_area = area;
          s->best = hwnd;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&search));
  return reinterpret_cast<std::uintptr_t>(search.best);
}

bool capture_gimp_plugin_window(std::uintptr_t window, const QString& png_path) {
  const auto hwnd = reinterpret_cast<HWND>(window);
  RECT rect{};
  if (hwnd == nullptr || !GetWindowRect(hwnd, &rect)) {
    return false;
  }
  const int width = rect.right - rect.left;
  const int height = rect.bottom - rect.top;
  if (width <= 0 || height <= 0) {
    return false;
  }
  HDC screen = GetDC(nullptr);
  HDC memory = CreateCompatibleDC(screen);
  HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
  auto* previous = SelectObject(memory, bitmap);
  bool ok = PrintWindow(hwnd, memory, PW_RENDERFULLCONTENT) != 0;
  QImage image(width, height, QImage::Format_ARGB32);
  if (ok) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;  // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    ok = GetDIBits(memory, bitmap, 0, static_cast<UINT>(height), image.bits(), &info, DIB_RGB_COLORS) == height;
  }
  SelectObject(memory, previous);
  DeleteObject(bitmap);
  DeleteDC(memory);
  ReleaseDC(nullptr, screen);
  // GDI leaves the alpha channel undefined; the window is opaque.
  return ok && image.convertToFormat(QImage::Format_RGB32).save(png_path, "PNG");
}

bool answer_gimp_plugin_window(std::uintptr_t window, bool accept, int attempt) {
  const auto hwnd = reinterpret_cast<HWND>(window);
  if (hwnd == nullptr) {
    return false;
  }
  if (attempt > 0 && !accept) {
    return PostMessageW(hwnd, WM_CLOSE, 0, 0) != 0;
  }
  if (attempt > 1) {
    RECT window_rect{};
    GetWindowRect(hwnd, &window_rect);
    POINT point{window_rect.right - 160, window_rect.bottom - 27};
    ScreenToClient(hwnd, &point);
    const LPARAM at = MAKELPARAM(point.x, point.y);
    return PostMessageW(hwnd, WM_MOUSEMOVE, 0, at) != 0 && PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, at) != 0 &&
           PostMessageW(hwnd, WM_LBUTTONUP, 0, at) != 0;
  }
  if (attempt > 0) {
    const UINT scan = MapVirtualKeyW('O', MAPVK_VK_TO_VSC);
    const LPARAM context = 1LL << 29;  // Alt held
    const LPARAM down = 1 | (static_cast<LPARAM>(scan) << 16) | context;
    const LPARAM up = down | (1LL << 30) | (1LL << 31);
    const UINT alt_scan = MapVirtualKeyW(VK_MENU, MAPVK_VK_TO_VSC);
    const LPARAM alt_down = 1 | (static_cast<LPARAM>(alt_scan) << 16) | context;
    const LPARAM alt_up = 1 | (static_cast<LPARAM>(alt_scan) << 16) | (1LL << 30) | (1LL << 31);
    return PostMessageW(hwnd, WM_SYSKEYDOWN, VK_MENU, alt_down) != 0 &&
           PostMessageW(hwnd, WM_SYSKEYDOWN, 'O', down) != 0 && PostMessageW(hwnd, WM_SYSKEYUP, 'O', up) != 0 &&
           PostMessageW(hwnd, WM_KEYUP, VK_MENU, alt_up) != 0;
  }
  const WPARAM key = accept ? VK_RETURN : VK_ESCAPE;
  const UINT scan = MapVirtualKeyW(static_cast<UINT>(key), MAPVK_VK_TO_VSC);
  const LPARAM down = 1 | (static_cast<LPARAM>(scan) << 16);
  const LPARAM up = down | (1LL << 30) | (1LL << 31);
  return PostMessageW(hwnd, WM_KEYDOWN, key, down) != 0 && PostMessageW(hwnd, WM_KEYUP, key, up) != 0;
}

#else

std::uintptr_t gimp_plugin_window_for_process(std::uint64_t) { return 0; }
bool capture_gimp_plugin_window(std::uintptr_t, const QString&) { return false; }
bool answer_gimp_plugin_window(std::uintptr_t, bool, int) { return false; }

#endif

}  // namespace patchy::ui
