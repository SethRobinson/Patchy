#pragma once

// The tool palette's pinned bottom cluster (docs/tool-palette.md): a compact
// color well (the foreground swatch over the background swatch, with a swap
// glyph in the upper right and a default-colors glyph in the lower left) and
// the Quick Mask button. The footer is a child of the palette toolbar that sits
// in the toolbar layout's reserved bottom margin, so it never joins the
// overflow and stays at the bottom while the tools above fold into the
// extension columns. Under Quick Mask sits the screen mode button (GitHub
// issue 212), which MainWindow builds and the footer only places.

#include <QColor>
#include <QPushButton>
#include <QToolButton>
#include <QWidget>

class QAction;
class QPaintEvent;
class QResizeEvent;

namespace patchy::ui {

// Tool palette lengths at a toolbar icon size (ToolbarIconSize in app_settings.hpp): the
// Normal length times the size's factor, rounded. The collapsed column is the scaled 31 px
// tool button content plus its padding, border and the bar's side margins (12 px, not
// scaled), and the footer scales with that column (footer factor = column / 43) so the
// well fills it at every size.
[[nodiscard]] int tool_palette_length(int normal_px, double factor);
[[nodiscard]] int tool_palette_collapsed_width(double factor);
[[nodiscard]] double tool_palette_footer_factor(double factor);

// A swatch square of the color well. A QPushButton so the picker opens on
// click and the tooltip, text ("FG"/"BG") and enabled state keep their
// contract; it paints its color itself, flat, with a theme-ink border.
class ColorWellSwatch final : public QPushButton {
  Q_OBJECT

public:
  explicit ColorWellSwatch(QWidget* parent = nullptr);

  void set_color(QColor color);
  [[nodiscard]] QColor color() const noexcept { return color_; }
  void set_side(int side);
  [[nodiscard]] QSize sizeHint() const override;

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  QColor color_{Qt::black};
  int side_;
};

// The two small glyph buttons beside the swatches, each triggering its
// default action (Swap Colors, Default Colors); painted from theme ink.
class ColorWellGlyphButton final : public QToolButton {
  Q_OBJECT

public:
  enum class Glyph { Swap, Default };
  ColorWellGlyphButton(Glyph glyph, QWidget* parent = nullptr);

  void set_side(int side);
  [[nodiscard]] QSize sizeHint() const override;

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  Glyph glyph_;
  int side_;
};

class ToolPaletteFooter final : public QWidget {
  Q_OBJECT

public:
  // The collapsed palette column is 43 px at the Normal toolbar icon size; the footer is
  // designed for it. set_scale() resizes it with the column (cell_width() and friends).
  static constexpr int kWidth = 43;
  static constexpr int kWellHeight = 45;
  // Quick Mask's row, then the screen mode button's (GitHub issue 212).
  static constexpr int kQuickMaskRowHeight = 30;
  static constexpr int kScreenModeRowHeight = 22;
  static constexpr int kHeight = kWellHeight + kQuickMaskRowHeight + kScreenModeRowHeight;

  ToolPaletteFooter(QAction* default_colors, QAction* swap_colors, QAction* quick_mask, QWidget* parent = nullptr);

  [[nodiscard]] ColorWellSwatch* foreground() const noexcept { return foreground_; }
  [[nodiscard]] ColorWellSwatch* background() const noexcept { return background_; }
  [[nodiscard]] QToolButton* quick_mask_button() const noexcept { return quick_mask_; }
  // Takes the screen mode button into its row under Quick Mask (a child of the footer).
  void set_screen_mode_button(QToolButton* button);
  [[nodiscard]] QToolButton* screen_mode_button() const noexcept { return screen_mode_; }
  [[nodiscard]] QSize sizeHint() const override { return {cell_width_, height_}; }
  [[nodiscard]] QSize minimumSizeHint() const override { return {cell_width_, height_}; }

  // Lays the footer out for a toolbar icon size factor (1 is Normal, the k* sizes).
  void set_scale(double palette_factor);
  // Which rows show (Customize Tool Palette, GitHub issues 212 and 213): the color well,
  // Quick Mask and the screen mode button. A hidden row takes no height, so the rows below
  // move up and the footer shrinks; with none shown it is 0 px tall.
  void set_shown_rows(bool colors, bool quick_mask, bool screen_mode);
  [[nodiscard]] bool shows_colors() const noexcept { return shows_colors_; }
  [[nodiscard]] bool shows_quick_mask() const noexcept { return shows_quick_mask_; }
  [[nodiscard]] bool shows_screen_mode() const noexcept { return shows_screen_mode_; }
  // Two columns (GitHub issue 499): the x, in footer coordinates, the well and the buttons
  // center on; negative lays them out in the collapsed column at the footer's left edge.
  void set_center_x(double x);
  [[nodiscard]] int cell_width() const noexcept { return cell_width_; }
  [[nodiscard]] int well_height() const noexcept { return well_height_; }
  [[nodiscard]] int footer_height() const noexcept { return height_; }

protected:
  void resizeEvent(QResizeEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  // Places the controls in the column's cell, measured from the palette's right border: the
  // swap glyph 2 px clear of it (GitHub issue 251) and the Quick Mask button centered under
  // the well (224). Centered on set_center_x's x, the cell moves there whole (499).
  void place_controls();
  // The footer's height and the rows' visibility for the shown rows at the current scale.
  void apply_rows();

  ColorWellSwatch* foreground_{nullptr};
  ColorWellSwatch* background_{nullptr};
  ColorWellGlyphButton* swap_{nullptr};
  ColorWellGlyphButton* default_{nullptr};
  QToolButton* quick_mask_{nullptr};
  QToolButton* screen_mode_{nullptr};
  double scale_{1.0};
  double center_x_{-1.0};
  int cell_width_{kWidth};
  int well_height_{kWellHeight};
  int height_{kHeight};
  int glyph_size_{0};
  bool shows_colors_{true};
  bool shows_quick_mask_{true};
  bool shows_screen_mode_{true};
};

}  // namespace patchy::ui
