#pragma once

// The application style: the platform style with a few hints and icons overridden,
// leaving all other rendering (including the app stylesheet) unchanged. main.cpp
// installs it; tests construct it directly.

#include <QProxyStyle>

namespace patchy::ui {

// A QToolBar whose property of this name is true reserves no band for its overflow
// extension button (AppStyle answers PM_ToolBarExtensionExtent with 0), and the window
// sheet gives the tool palette's button no height (main_window_theme.cpp). The tool palette
// sets it in wheel-scroll mode with its scroll arrows hidden (GitHub issue 668), where the
// band would only be blank space under the tools.
inline constexpr const char* kToolBarExtensionHiddenProperty = "toolBarExtensionHidden";

class AppStyle : public QProxyStyle {
 public:
  using QProxyStyle::QProxyStyle;

  int pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget) const override;
  int styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget,
                QStyleHintReturn* return_data) const override;
  QIcon standardIcon(StandardPixmap standard_icon, const QStyleOption* option,
                     const QWidget* widget) const override;
  void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                     const QWidget* widget) const override;
};

}  // namespace patchy::ui
