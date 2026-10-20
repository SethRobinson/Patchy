#include "ui/tool_palette_footer.hpp"

#include "ui/theme_palette.hpp"

#include <QAction>
#include <QEvent>
#include <QResizeEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

#include <algorithm>

namespace patchy::ui {

namespace {

constexpr int kSwatchSize = 22;
constexpr int kGlyphSize = 14;
// QToolBar#toolPalette's border-right (main_window_theme.cpp).
constexpr int kPaletteBorder = 1;

QColor ink_for(const QWidget& widget) {
  const auto& palette = theme();
  if (!widget.isEnabled()) {
    QColor dim = palette.icon_ink;
    dim.setAlphaF(0.4F);
    return dim;
  }
  return widget.underMouse() ? palette.accent_bright : palette.icon_ink;
}

}  // namespace

ColorWellSwatch::ColorWellSwatch(QWidget* parent) : QPushButton(parent) {
  setFlat(true);
  setFocusPolicy(Qt::NoFocus);
  setCursor(Qt::PointingHandCursor);
  setFixedSize(kSwatchSize, kSwatchSize);
  setAttribute(Qt::WA_Hover, true);
}

void ColorWellSwatch::set_color(QColor color) {
  if (color_ == color) {
    return;
  }
  color_ = color;
  update();
}

QSize ColorWellSwatch::sizeHint() const {
  return {kSwatchSize, kSwatchSize};
}

void ColorWellSwatch::paintEvent(QPaintEvent* /*event*/) {
  QPainter painter(this);
  const QRect box = rect().adjusted(0, 0, -1, -1);
  // Always opaque: with no document the swatches are disabled, and a translucent
  // pair read as one odd overlapping shape. Only the border dims.
  painter.fillRect(box, color_);
  // A hairline of the opposite tone inside the ink border keeps a swatch that
  // matches the palette background visible.
  painter.setPen(QPen(color_.lightness() < 128 ? QColor(255, 255, 255, 60) : QColor(0, 0, 0, 60), 1.0));
  painter.drawRect(box.adjusted(1, 1, -1, -1));
  painter.setPen(QPen(ink_for(*this), 1.0));
  painter.drawRect(box);
}

ColorWellGlyphButton::ColorWellGlyphButton(Glyph glyph, QWidget* parent) : QToolButton(parent), glyph_(glyph) {
  setAutoRaise(true);
  setFocusPolicy(Qt::NoFocus);
  setCursor(Qt::PointingHandCursor);
  setFixedSize(kGlyphSize, kGlyphSize);
  setAttribute(Qt::WA_Hover, true);
}

QSize ColorWellGlyphButton::sizeHint() const {
  return {kGlyphSize, kGlyphSize};
}

void ColorWellGlyphButton::paintEvent(QPaintEvent* /*event*/) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  const QColor ink = ink_for(*this);
  if (glyph_ == Glyph::Default) {
    // A tiny black-over-white pair: the default colors.
    painter.setPen(QPen(ink, 1.0));
    painter.setBrush(Qt::white);
    painter.drawRect(QRectF(5.5, 5.5, 7.0, 7.0));
    painter.setBrush(Qt::black);
    painter.drawRect(QRectF(1.5, 1.5, 7.0, 7.0));
    return;
  }
  // A double-headed arrow bending around the free upper-right corner, as in
  // Photoshop: one head points left at the foreground swatch beside it, the
  // other down at the background swatch below it (GitHub issue 115 reported the
  // earlier glyph, which bulged the other way, as rotated by 90 degrees).
  QPen pen(ink, 1.3);
  pen.setCapStyle(Qt::RoundCap);
  pen.setJoinStyle(Qt::RoundJoin);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);
  QPainterPath arc;
  arc.moveTo(2.5, 3.5);
  arc.quadTo(10.5, 3.5, 10.5, 11.5);
  painter.drawPath(arc);
  painter.drawLine(QPointF(2.5, 3.5), QPointF(5.0, 1.5));
  painter.drawLine(QPointF(2.5, 3.5), QPointF(5.0, 5.5));
  painter.drawLine(QPointF(10.5, 11.5), QPointF(8.5, 9.5));
  painter.drawLine(QPointF(10.5, 11.5), QPointF(12.5, 9.5));
}

ToolPaletteFooter::ToolPaletteFooter(QAction* default_colors, QAction* swap_colors, QAction* quick_mask,
                                     QWidget* parent)
    : QWidget(parent) {
  setObjectName(QStringLiteral("toolPaletteFooter"));
  setFixedHeight(kHeight);
  setMinimumWidth(kWidth);

  // The well: background square to the lower right, foreground square over
  // it, swap glyph in the free upper-right corner, default glyph in the free
  // lower-left corner. Everything is placed by hand inside a 43 x 42 cell.
  background_ = new ColorWellSwatch(this);
  background_->setObjectName(QStringLiteral("backgroundColorButton"));
  background_->setText(QStringLiteral("BG"));
  // Qt places the toolbar's extension button 2 px into the bottom margin, so
  // the well starts 3 px down.
  background_->move(17, 19);
  foreground_ = new ColorWellSwatch(this);
  foreground_->setObjectName(QStringLiteral("foregroundColorButton"));
  foreground_->setText(QStringLiteral("FG"));
  foreground_->move(4, 7);
  foreground_->raise();

  swap_ = new ColorWellGlyphButton(ColorWellGlyphButton::Glyph::Swap, this);
  swap_->setObjectName(QStringLiteral("colorSwapButton"));
  swap_->setDefaultAction(swap_colors);
  default_ = new ColorWellGlyphButton(ColorWellGlyphButton::Glyph::Default, this);
  default_->setObjectName(QStringLiteral("colorDefaultButton"));
  default_->setDefaultAction(default_colors);
  default_->move(1, 30);

  quick_mask_ = new QToolButton(this);
  quick_mask_->setObjectName(QStringLiteral("quickMaskButton"));
  quick_mask_->setDefaultAction(quick_mask);
  quick_mask_->setAutoRaise(true);
  // The action's two-state icon (GitHub issue 224) at 22 px: its frame fills less
  // of the 32 px art than the tool glyphs do. QSS sizes the button (32 x 22
  // content, 36 x 26 with padding and border) so the checked box, border
  // included, fits the footer on every side; place_controls centers it.
  quick_mask_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  quick_mask_->setIconSize(QSize(22, 22));
  // The QSS polish resizes the button after construction.
  quick_mask_->installEventFilter(this);
  place_controls();
}

void ToolPaletteFooter::place_controls() {
  if (quick_mask_ == nullptr || swap_ == nullptr) {
    return;
  }
  // The well is laid out for the collapsed column, so an expanded palette keeps
  // these under the well rather than spread across the wider bar. The footer
  // spans the palette's full width, so its last column is the palette's 1 px
  // right border.
  const int cell = std::min(width(), kWidth) - kPaletteBorder;
  // The swap glyph stays 2 px clear of that border (GitHub issue 251: its arrow
  // ended in the column next to it and read as touching it).
  swap_->move(cell - kGlyphSize - 2, 4);
  const auto size = quick_mask_->size();
  quick_mask_->move(std::max(0, (cell - size.width()) / 2),
                    kWellHeight + std::max(0, (kHeight - kWellHeight - size.height()) / 2));
}

void ToolPaletteFooter::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  place_controls();
}

bool ToolPaletteFooter::eventFilter(QObject* watched, QEvent* event) {
  if (watched == quick_mask_ && event->type() == QEvent::Resize) {
    place_controls();
  }
  return QWidget::eventFilter(watched, event);
}

}  // namespace patchy::ui
