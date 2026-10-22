// macOS half of display_hdr_info.hpp. NSScreen reports EDR headroom directly as a ratio
// to the current SDR white: the current value moves with display brightness and with
// what content asks for, the potential value is what the screen could reach now. No
// nits are reported. Screen Sharing reports a headroom of 1, so it needs no remote
// check. Compiled with ARC on macOS only (see CMakeLists).

#include "ui/display_hdr_info.hpp"

#include <QGuiApplication>
#include <QScreen>

#import <AppKit/AppKit.h>

namespace patchy::ui {

namespace {

bool platform_is_cocoa() {
  return QGuiApplication::platformName().compare(QStringLiteral("cocoa"), Qt::CaseInsensitive) == 0;
}

// QScreen geometry is in points with a top-left origin on the primary screen; NSScreen
// frames use a bottom-left origin on the first screen (the one with the menu bar).
QRect qt_geometry(NSScreen* ns_screen, CGFloat primary_height) {
  const NSRect frame = ns_screen.frame;
  return QRect(static_cast<int>(frame.origin.x),
               static_cast<int>(primary_height - (frame.origin.y + frame.size.height)),
               static_cast<int>(frame.size.width), static_cast<int>(frame.size.height));
}

NSScreen* ns_screen_for(QScreen* screen) {
  NSArray<NSScreen*>* screens = NSScreen.screens;
  if (screens.count == 0) {
    return nil;
  }
  const CGFloat primary_height = screens.firstObject.frame.size.height;
  const QRect geometry = screen->geometry();
  for (NSScreen* candidate in screens) {
    if (qt_geometry(candidate, primary_height) == geometry) {
      return candidate;
    }
  }
  const QString name = screen->name();
  for (NSScreen* candidate in screens) {
    if (QString::fromNSString(candidate.localizedName) == name) {
      return candidate;
    }
  }
  return nil;
}

}  // namespace

DisplayHdrCapabilities query_screen_hdr(QScreen* screen) {
  DisplayHdrCapabilities capabilities;
  if (screen == nullptr || !platform_is_cocoa()) {
    return capabilities;
  }
  @autoreleasepool {
    NSScreen* ns_screen = ns_screen_for(screen);
    if (ns_screen == nil) {
      return capabilities;
    }
    capabilities.queried = true;
    capabilities.headroom = headroom_from_ratio(ns_screen.maximumExtendedDynamicRangeColorComponentValue);
    capabilities.potential_headroom =
        headroom_from_ratio(ns_screen.maximumPotentialExtendedDynamicRangeColorComponentValue);
    capabilities.hdr_enabled = capabilities.potential_headroom > 1.0;
  }
  return capabilities;
}

}  // namespace patchy::ui
