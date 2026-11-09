#include "ui/clipboard_format_probe.hpp"

#include <QGuiApplication>
#include <QString>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <initializer_list>
#endif

namespace patchy::ui {

namespace {

#ifdef Q_OS_WIN
// Only Qt's native Windows platforms read the system clipboard.
bool native_windows_clipboard() {
  const QString platform = QGuiApplication::platformName();
  return platform == QLatin1String("windows") || platform == QLatin1String("direct2d");
}

bool any_format_listed(std::initializer_list<UINT> formats) {
  for (const UINT format : formats) {
    if (format != 0 && IsClipboardFormatAvailable(format) != 0) {
      return true;
    }
  }
  return false;
}
#endif

}  // namespace

bool system_clipboard_may_offer([[maybe_unused]] ClipboardData kind) {
#ifdef Q_OS_WIN
  if (!native_windows_clipboard()) {
    return true;
  }
  // The formats Qt 6.8's Windows converters accept (qwindowsmimeregistry.cpp): its image
  // and text converters, plus the registered format of the same MIME name its last-resort
  // converter checks. Windows lists CF_DIB for CF_BITMAP and CF_DIBV5 data and
  // CF_UNICODETEXT for CF_TEXT and CF_OEMTEXT data, as it synthesizes those.
  switch (kind) {
    case ClipboardData::Image: {
      static const UINT png = RegisterClipboardFormatW(L"PNG");
      static const UINT qt_image = RegisterClipboardFormatW(L"application/x-qt-image");
      return any_format_listed({CF_DIB, png, qt_image});
    }
    case ClipboardData::Text: {
      static const UINT qt_text = RegisterClipboardFormatW(L"text/plain");
      return any_format_listed({CF_UNICODETEXT, CF_TEXT, qt_text});
    }
  }
#endif
  return true;
}

quint32 system_clipboard_change_number() {
#ifdef Q_OS_WIN
  return native_windows_clipboard() ? static_cast<quint32>(GetClipboardSequenceNumber()) : 0U;
#else
  return 0U;
#endif
}

}  // namespace patchy::ui
