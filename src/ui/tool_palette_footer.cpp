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

int tool_palette_length(int normal_px, double factor) {
  return qRound(normal_px * factor);
}

int tool_palette_collapsed_width(double factor) {
  return tool_palette_length(31, factor) + (ToolPaletteFooter::kWidth - 31);
}

double tool_palette_footer_factor(double factor) {
  return static_cast<double>(tool_palette_collapsed_width(factor)) / ToolPaletteFooter::kWidth;
}

ColorWellSwatch::ColorWellSwatch(QWidget* parent) : QPushButton(parent), side_(kSwatchSize) {
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

void ColorWellSwatch::set_side(int side) {
  side_ = side;
  setFixedSize(side, side);
}

QSize ColorWellSwatch::sizeHint() const {
  return {side_, side_};
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

ColorWellGlyphButton::ColorWellGlyphButton(Glyph glyph, QWidget* parent)
    : QToolButton(parent), glyph_(glyph), side_(kGlyphSize) {
  setAutoRaise(true);
  setFocusPolicy(Qt::NoFocus);
  setCursor(Qt::PointingHandCursor);
  setFixedSize(kGlyphSize, kGlyphSize);
  setAttribute(Qt::WA_Hover, true);
}

void ColorWellGlyphButton::set_side(int side) {
  side_ = side;
  setFixedSize(side, side);
}

QSize ColorWellGlyphButton::sizeHint() const {
  return {side_, side_};
}

void ColorWellGlyphButton::paintEvent(QPaintEvent* /*event*/) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  // The glyphs are drawn on the Normal 14 px grid and scaled with the toolbar icon size.
  if (side_ != kGlyphSize) {
    painter.scale(static_cast<double>(side_) / kGlyphSize, static_cast<double>(side_) / kGlyphSize);
  }
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
  foreground_ = new ColorWellSwatch(this);
  foreground_->setObjectName(QStringLiteral("foregroundColorButton"));
  foreground_->setText(QStringLiteral("FG"));
  foreground_->raise();

  swap_ = new ColorWellGlyphButton(ColorWellGlyphButton::Glyph::Swap, this);
  swap_->setObjectName(QStringLiteral("colorSwapButton"));
  swap_->setDefaultAction(swap_colors);
  default_ = new ColorWellGlyphButton(ColorWellGlyphButton::Glyph::Default, this);
  default_->setObjectName(QStringLiteral("colorDefaultButton"));
  default_->setDefaultAction(default_colors);

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
  set_scale(1.0);
}

void ToolPaletteFooter::set_scale(double palette_factor) {
  scale_ = tool_palette_footer_factor(palette_factor);
  const auto length = [this](int normal_px) { return tool_palette_length(normal_px, scale_); };
  cell_width_ = tool_palette_collapsed_width(palette_factor);
  well_height_ = length(kWellHeight);
  height_ = length(kHeight);
  glyph_size_ = length(kGlyphSize);
  setFixedHeight(height_);
  setMinimumWidth(cell_width_);
  foreground_->set_side(length(kSwatchSize));
  background_->set_side(length(kSwatchSize));
  swap_->set_side(glyph_size_);
  default_->set_side(glyph_size_);
  // Qt places the toolbar's extension button 2 px into the bottom margin, so
  // the well starts 3 px down.
  background_->move(length(17), length(19));
  foreground_->move(length(4), length(7));
  default_->move(1, length(30));
  quick_mask_->setIconSize(QSize(length(22), length(22)));
  if (screen_mode_ != nullptr) {
    screen_mode_->setIconSize(QSize(length(17), length(17)));
  }
  place_controls();
  updateGeometry();
}

void ToolPaletteFooter::place_controls() {
  if (quick_mask_ == nullptr || swap_ == nullptr) {
    return;
  }
  // The well is laid out for the collapsed column, so an expanded palette keeps
  // these under the well rather than spread across the wider bar. The footer
  // spans the palette's full width, so its last column is the palette's 1 px
  // right border.
  const int cell = std::min(width(), cell_width_) - kPaletteBorder;
  // The swap glyph stays 2 px clear of that border (GitHub issue 251: its arrow
  // ended in the column next to it and read as touching it), and sits as far
  // above the background swatch as it sits right of the foreground swatch.
  swap_->move(cell - glyph_size_ - 2, tool_palette_length(5, scale_));
  const auto size = quick_mask_->size();
  const int quick_mask_row = tool_palette_length(kQuickMaskRowHeight, scale_);
  quick_mask_->move(std::max(0, (cell - size.width()) / 2),
                    well_height_ + std::max(0, (quick_mask_row - size.height()) / 2));
  if (screen_mode_ != nullptr) {
    // The screen mode button's own row under Quick Mask, centered the same way.
    const auto mode_size = screen_mode_->size();
    const int row_top = well_height_ + quick_mask_row;
    screen_mode_->move(std::max(0, (cell - mode_size.width()) / 2),
                       row_top + std::max(0, (height_ - row_top - mode_size.height()) / 2));
  }
}

void ToolPaletteFooter::set_screen_mode_button(QToolButton* button) {
  if (screen_mode_ != nullptr) {
    screen_mode_->removeEventFilter(this);
  }
  screen_mode_ = button;
  if (screen_mode_ != nullptr) {
    screen_mode_->setParent(this);
    screen_mode_->setIconSize(QSize(tool_palette_length(17, scale_), tool_palette_length(17, scale_)));
    // The QSS polish resizes the button after it joins the footer.
    screen_mode_->installEventFilter(this);
    screen_mode_->show();
  }
  place_controls();
}

void ToolPaletteFooter::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  place_controls();
}

bool ToolPaletteFooter::eventFilter(QObject* watched, QEvent* event) {
  if ((watched == quick_mask_ || (screen_mode_ != nullptr && watched == screen_mode_)) &&
      event->type() == QEvent::Resize) {
    place_controls();
  }
  return QWidget::eventFilter(watched, event);
}

}  // namespace patchy::ui
