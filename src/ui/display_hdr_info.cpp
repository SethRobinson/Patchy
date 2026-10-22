#include "ui/display_hdr_info.hpp"

#include <QGuiApplication>
#include <QScreen>

namespace patchy::ui {

#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
DisplayHdrCapabilities query_screen_hdr(QScreen* screen) {
  static_cast<void>(screen);
  return {};
}
#endif

std::vector<ScreenHdrInfo> query_display_hdr_info() {
  std::vector<ScreenHdrInfo> result;
  QScreen* primary = QGuiApplication::primaryScreen();
  const auto screens = QGuiApplication::screens();
  result.reserve(static_cast<std::size_t>(screens.size()));
  const auto append = [&result, primary](QScreen* screen) {
    ScreenHdrInfo info;
    info.screen = screen;
    info.name = screen->name();
    info.geometry = screen->geometry();
    info.device_pixel_ratio = screen->devicePixelRatio();
    info.primary = screen == primary;
    info.hdr = query_screen_hdr(screen);
    result.push_back(std::move(info));
  };
  if (primary != nullptr) {
    append(primary);
  }
  for (QScreen* screen : screens) {
    if (screen != nullptr && screen != primary) {
      append(screen);
    }
  }
  return result;
}

}  // namespace patchy::ui
