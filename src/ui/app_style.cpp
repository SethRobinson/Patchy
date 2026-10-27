#include "ui/app_style.hpp"

#include "ui/icon_theme.hpp"

#include <QFormLayout>
#include <QIcon>
#include <QStyleOptionSpinBox>

namespace patchy::ui {

int AppStyle::styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget,
                        QStyleHintReturn* return_data) const {
  // Sliders: a left-click anywhere on the groove jumps straight to that value and begins
  // dragging, instead of Qt's default page-step that crawls toward the cursor in chunks.
  if (hint == SH_Slider_AbsoluteSetButtons) {
    return Qt::LeftButton;
  }
  // Tool-button flyouts (DelayedPopup, e.g. the marquee/shape buttons in the tool
  // palette) open after a short Photoshop-like hold instead of Qt's much longer default
  // (600 ms). A left double-click opens them too; that lives in configure_tool_flyout
  // (main_window_actions_tool_palette.cpp).
  if (hint == SH_ToolButton_PopupDelay) {
    return 200;  // ms; press-and-hold flyout delay (300 felt sluggish)
  }
  // Dialog buttons carry text only, as on Windows. On Linux the desktop's icon theme
  // supplies OK/Cancel glyphs drawn for its own light or dark scheme, which need not match
  // Patchy's (dark checkmarks on dark buttons, GitHub issue 99).
  if (hint == SH_DialogButtonBox_ButtonsHaveIcons) {
    return 0;
  }
#ifdef Q_OS_MACOS
  // Form layouts consult the style: QMacStyle keeps fields at their size hint and
  // right-aligns labels (Aqua HIG), which shrinks line edits and combos to slivers in
  // dialogs designed around the roomy Windows form behavior (Brush Tips manager, Layer
  // Style pages). Pin the Windows behavior everywhere.
  if (hint == SH_FormLayoutFieldGrowthPolicy) {
    return QFormLayout::AllNonFixedFieldsGrow;
  }
  if (hint == SH_FormLayoutLabelAlignment) {
    return Qt::AlignLeft | Qt::AlignVCenter;
  }
#endif
  return QProxyStyle::styleHint(hint, option, widget, return_data);
}

QIcon AppStyle::standardIcon(StandardPixmap standard_icon, const QStyleOption* option,
                             const QWidget* widget) const {
  // A toolbar's overflow button: Patchy's own chevron in the scheme's icon ink, not the
  // platform's glyph (issue 99: the desktop theme's dark chevron on a dark button).
  if (standard_icon == SP_ToolBarHorizontalExtensionButton) {
    return themed_svg_icon(QStringLiteral("chevron-right"));
  }
  // A vertical bar's overflow opens extra columns to the RIGHT, so its arrow
  // points right too (GitHub issue 122: the old down arrow suggested the tools
  // would slide up). The tool palette flips it to a left arrow while expanded
  // (apply_extension_chevron in main_window_actions_tool_palette.cpp).
  if (standard_icon == SP_ToolBarVerticalExtensionButton) {
    return themed_svg_icon(QStringLiteral("chevron-right"));
  }
  return QProxyStyle::standardIcon(standard_icon, option, widget);
}

void AppStyle::drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                             const QWidget* widget) const {
  // A spin box framed by the app stylesheet (the Color panel's number boxes) gets its step
  // arrows from the base style. Fusion, the Linux desktop style and the style of every
  // offscreen run (Qt's offscreen platform names no other), receives an arrow rect one frame
  // width too small and draws nothing in it, so the buttons came out blank (the internal
  // screenshots of GitHub issue 404). Hand it the frame back; the Windows style draws there
  // already.
  if ((element == PE_IndicatorSpinUp || element == PE_IndicatorSpinDown) && option != nullptr &&
      baseStyle()->name().compare(QStringLiteral("fusion"), Qt::CaseInsensitive) == 0) {
    const int frame = baseStyle()->pixelMetric(PM_DefaultFrameWidth, option, widget);
    if (const auto* spin = qstyleoption_cast<const QStyleOptionSpinBox*>(option)) {
      QStyleOptionSpinBox widened(*spin);
      widened.rect = spin->rect.adjusted(-frame, -frame, frame, frame);
      QProxyStyle::drawPrimitive(element, &widened, painter, widget);
      return;
    }
    QStyleOption widened(*option);
    widened.rect = option->rect.adjusted(-frame, -frame, frame, frame);
    QProxyStyle::drawPrimitive(element, &widened, painter, widget);
    return;
  }
  QProxyStyle::drawPrimitive(element, option, painter, widget);
}

}  // namespace patchy::ui
