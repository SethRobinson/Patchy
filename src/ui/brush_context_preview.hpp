#pragma once

#include <QColor>
#include <QImage>
#include <QPolygonF>
#include <QSize>
#include <QSizeF>
#include <QWidget>

#include <memory>

namespace patchy {
struct BrushTip;
struct BrushTipMipChain;
}  // namespace patchy

namespace patchy::ui {

// What the canvas menu's brush preview shows (GitHub issue 734 follow-up): the tip in use and
// the settings its rows edit. `tip` is the bitmap tip a tool stamps (null = the procedural
// round or square footprint); Roundness and Angle count only where `tip_shape`.
struct BrushContextPreviewState {
  int size{25};
  int softness{0};
  int roundness{100};
  double angle_degrees{0.0};
  bool tip_shape{true};
  bool square{false};
  std::shared_ptr<const patchy::BrushTip> tip;
  QColor ink{0, 0, 0};  // color-literal-ok: black paint depicts the document, as in the Brush Dynamics preview
  bool smoothing_shown{false};
  int smoothing{0};

  [[nodiscard]] bool operator==(const BrushContextPreviewState& other) const;
};

// The header of the canvas menu's brush settings: on the left the footprint at its real size
// (scaled down to fit its tile when larger, the size in pixels on a chip), its softness as the
// edge falloff and its roundness and angle as the shape; on the right a short sample stroke
// stamped with that footprint. For a tool with Smooth the stroke shows a jittery pointer path
// (a thin light line in a dark casing) and the stroke it paints, which follows the path less closely as Smooth
// rises. Both tiles are cached images keyed on the state and the device pixel ratio, so a
// repaint only draws them; nothing here touches document pixels.
class BrushContextPreview final : public QWidget {
  Q_OBJECT

public:
  explicit BrushContextPreview(QWidget* parent = nullptr);
  ~BrushContextPreview() override;

  void set_state(const BrushContextPreviewState& state);
  [[nodiscard]] const BrushContextPreviewState& state() const noexcept { return state_; }
  // The footprint as the left tile draws it, in logical pixels, before its turn: the tip's
  // width and its height after Roundness.
  [[nodiscard]] QSizeF footprint_extent() const;
  [[nodiscard]] bool smoothing_shown() const noexcept { return state_.smoothing_shown; }
  // Renders the cached tiles now at `device_pixel_ratio` (tests read them; painting does it).
  void render_now(double device_pixel_ratio);
  [[nodiscard]] const QImage& footprint_image() const noexcept { return footprint_; }
  [[nodiscard]] const QImage& stroke_image() const noexcept { return stroke_; }
  [[nodiscard]] int render_count() const noexcept { return render_count_; }

  [[nodiscard]] QSize sizeHint() const override;
  [[nodiscard]] QSize minimumSizeHint() const override;

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  [[nodiscard]] QRect footprint_tile() const;
  [[nodiscard]] QRect stroke_tile() const;
  [[nodiscard]] double displayed_diameter() const;
  [[nodiscard]] QImage render_tip(double diameter_device) const;
  void ensure_rendered(double device_pixel_ratio);

  BrushContextPreviewState state_;
  QImage footprint_;
  QImage stroke_;
  // The jittery pointer path the stroke tile draws for a tool with Smooth (tile coordinates).
  QPolygonF raw_path_;
  double rendered_ratio_{0.0};
  QSize rendered_stroke_size_;
  bool dirty_{true};
  int render_count_{0};
  // The tip's mip chain, built once per tip.
  const patchy::BrushTip* mips_tip_{nullptr};
  std::shared_ptr<const patchy::BrushTipMipChain> mips_;
};

enum class BrushSettingGlyphKind { Size, Softness, Roundness, Angle, Smoothing };

// A small live glyph at the start of a brush settings row: a dot that grows with Size, a dot
// whose edge fades with Soft, an ellipse that flattens with Roundness, a turned bar for Angle,
// and a line that straightens as Smooth rises. Painted in the text role.
class BrushSettingGlyph final : public QWidget {
  Q_OBJECT

public:
  BrushSettingGlyph(BrushSettingGlyphKind kind, int value, QWidget* parent = nullptr);

  void set_value(int value);
  [[nodiscard]] int value() const noexcept { return value_; }
  [[nodiscard]] BrushSettingGlyphKind kind() const noexcept { return kind_; }
  [[nodiscard]] QSize sizeHint() const override;

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  BrushSettingGlyphKind kind_;
  int value_{0};
};

}  // namespace patchy::ui
