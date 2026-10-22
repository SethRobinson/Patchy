#pragma once

// Per-screen HDR capability probe (docs/platform.md, site inventory). Windows reads
// DXGI and DisplayConfig (display_hdr_info_win.cpp), macOS reads NSScreen's EDR values
// (display_hdr_info_mac.mm); every other platform, and the offscreen QPA, reports
// `queried == false`. Values change at runtime ("Use HDR", the SDR content brightness
// slider, display brightness, monitor moves), so nothing is cached: each call queries.
// Main thread only.

#include "core/hdr_display.hpp"

#include <QRect>
#include <QString>

#include <vector>

class QScreen;

namespace patchy::ui {

struct ScreenHdrInfo {
  QScreen* screen = nullptr;
  QString name;
  QRect geometry;  // device-independent pixels, as QScreen::geometry()
  qreal device_pixel_ratio = 1.0;
  bool primary = false;
  DisplayHdrCapabilities hdr;
};

[[nodiscard]] DisplayHdrCapabilities query_screen_hdr(QScreen* screen);

// Every screen, the primary first.
[[nodiscard]] std::vector<ScreenHdrInfo> query_display_hdr_info();

}  // namespace patchy::ui
