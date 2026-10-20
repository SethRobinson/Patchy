#pragma once

// The tool palette's pinned bottom cluster (docs/tool-palette.md): a compact
// color well (the foreground swatch over the background swatch, with a swap
// glyph in the upper right and a default-colors glyph in the lower left) and
// the Quick Mask button. The footer is a child of the palette toolbar that sits
// in the toolbar layout's reserved bottom margin, so it never joins the
// overflow and stays at the bottom while the tools above fold into the
// extension columns.

#include <QColor>
#include <QPushButton>
#include <QToolButton>
#include <QWidget>

class QAction;
class QPaintEvent;
class QResizeEvent;

namespace patchy::ui {

// A swatch square of the color well. A QPushButton so the picker opens on
// click and the tooltip, text ("FG"/"BG") and enabled state keep their
// contract; it paints its color itself, flat, with a theme-ink border.
class ColorWellSwatch final : public QPushButton {
  Q_OBJECT

public:
  explicit ColorWellSwatch(QWidget* parent = nullptr);

  void set_color(QColor color);
  [[nodiscard]] QColor color() const noexcept { return color_; }
  [[nodiscard]] QSize sizeHint() const override;

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  QColor color_{Qt::black};
};

// The two small glyph buttons beside the swatches, each triggering its
// default action (Swap Colors, Default Colors); painted from theme ink.
class ColorWellGlyphButton final : public QToolButton {
  Q_OBJECT

public:
  enum class Glyph { Swap, Default };
  ColorWellGlyphButton(Glyph glyph, QWidget* parent = nullptr);

  [[nodiscard]] QSize sizeHint() const override;

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  Glyph glyph_;
};

class ToolPaletteFooter final : public QWidget {
  Q_OBJECT

public:
  // The collapsed palette column is 43 px; the footer is designed for it.
  static constexpr int kWidth = 43;
  static constexpr int kWellHeight = 45;
  static constexpr int kHeight = 75;

  ToolPaletteFooter(QAction* default_colors, QAction* swap_colors, QAction* quick_mask, QWidget* parent = nullptr);

  [[nodiscard]] ColorWellSwatch* foreground() const noexcept { return foreground_; }
  [[nodiscard]] ColorWellSwatch* background() const noexcept { return background_; }
  [[nodiscard]] QToolButton* quick_mask_button() const noexcept { return quick_mask_; }
  [[nodiscard]] QSize sizeHint() const override { return {kWidth, kHeight}; }
  [[nodiscard]] QSize minimumSizeHint() const override { return {kWidth, kHeight}; }

protected:
  void resizeEvent(QResizeEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  // Centers the Quick Mask button under the well in the footer's real width,
  // which can be a pixel or two narrower than kWidth (GitHub issue 224).
  void place_quick_mask();

  ColorWellSwatch* foreground_{nullptr};
  ColorWellSwatch* background_{nullptr};
  ColorWellGlyphButton* swap_{nullptr};
  ColorWellGlyphButton* default_{nullptr};
  QToolButton* quick_mask_{nullptr};
};

}  // namespace patchy::ui
