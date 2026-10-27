#pragma once

// The application style: the platform style with a few hints and icons overridden,
// leaving all other rendering (including the app stylesheet) unchanged. main.cpp
// installs it; tests construct it directly.

#include <QProxyStyle>

namespace patchy::ui {

class AppStyle : public QProxyStyle {
 public:
  using QProxyStyle::QProxyStyle;

  int styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget,
                QStyleHintReturn* return_data) const override;
  QIcon standardIcon(StandardPixmap standard_icon, const QStyleOption* option,
                     const QWidget* widget) const override;
  void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                     const QWidget* widget) const override;
};

}  // namespace patchy::ui
