#pragma once

#include "formats/raw_tone.hpp"

#include <QString>
#include <QWidget>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace patchy::ui {

// 256-bin histograms of a developed raw preview (interleaved 8-bit sRGB). Rows
// and columns are read on a fixed stride so even a full-resolution develop
// costs at most about kRawHistogramMaxSamples pixel reads; the worker thread
// builds it once per completed develop, never per repaint.
struct RawPreviewHistogram {
  std::array<std::uint32_t, 256> red{};
  std::array<std::uint32_t, 256> green{};
  std::array<std::uint32_t, 256> blue{};
  // (30 R + 59 G + 11 B) / 100 in integers, the luminosity weights of the
  // Image > Adjustments histograms.
  std::array<std::uint32_t, 256> luminance{};
  std::uint64_t samples{0};
};

inline constexpr std::uint64_t kRawHistogramMaxSamples = 1U << 20U;

[[nodiscard]] RawPreviewHistogram raw_preview_histogram(std::span<const std::uint8_t> interleaved_rgb, int width,
                                                        int height);

// The histogram's tone regions, left to right. The dialog has no Blacks or
// Whites control, so Blacks folds into Shadows and Whites into Highlights,
// the nearest controls it has.
enum class RawHistogramRegion { None, Shadows, Exposure, Highlights };

// Horizontal span of a region as fractions of the plot width, [begin, end).
[[nodiscard]] std::pair<double, double> raw_histogram_region_span(RawHistogramRegion region);
// The region under a fraction of the plot width; None outside [0, 1].
[[nodiscard]] RawHistogramRegion raw_histogram_region_at(double fraction);
// "shadows", "exposure", "highlights" or "none": test and script tokens.
[[nodiscard]] QString raw_histogram_region_token(RawHistogramRegion region);

// Shift-click auto values for the Tone sliders (GitHub issue 394). They are
// solved from a baseline: the luminance histogram of a develop with all four
// Tone sliders at their defaults and every other setting as it is, so the
// answer never depends on where the sliders sit and repeats exactly. The
// target is Auto Tone's levels stretch (Image > Adjustments: the 0.1% black
// and white points of auto_levels_clip_scan moved to 0 and 255). The four
// values are fitted together through RawToneModel, so Shift-clicking all four
// lands on that stretch and each slider's value is its share of it.
struct RawToneAutoValues {
  double exposure_ev{0.0};
  double contrast{0.0};
  double highlights{0.0};
  double shadows{0.0};

  friend bool operator==(const RawToneAutoValues&, const RawToneAutoValues&) = default;
};

// What the Tone sliders do to one luminance value of a baseline develop
// (0..1): Exposure through LibRaw's exp_bef curve in linear light (with the
// highlight preservation raw_document_io sets), then the rendering profile's
// tone curve on luma, then raw_tone's Shadows/Highlights/Contrast curve.
// Color steps (saturation, the profile's color enhancement) are left out.
class RawToneModel {
public:
  RawToneModel(raw::RenderingProfile profile, int processing_version);

  [[nodiscard]] double map(double baseline, const RawToneAutoValues& values) const;
  // The profile curve and its inverse on sRGB-encoded values; identity for Neutral.
  [[nodiscard]] double profile(double value) const;
  [[nodiscard]] double before_profile(double output) const;
  // Exposure on an sRGB-encoded value, as LibRaw applies exp_shift.
  [[nodiscard]] static double expose(double value, double exposure_ev);

private:
  std::vector<std::uint16_t> profile_lut_;
};

// The Tone slider values whose combined effect on `baseline` best matches
// Auto Tone's stretch of it, in the sliders' own steps (0.01 EV, whole units),
// or nullopt when the histogram is empty or flat.
[[nodiscard]] std::optional<RawToneAutoValues> raw_auto_tone(const RawPreviewHistogram& baseline,
                                                             const RawToneModel& model);

// Shift-click auto values for the Tone sliders. Each reads the luminance
// histogram of a preview developed with the slider at `rendered` and returns
// the new slider value (already clamped to its range), or nullopt when the
// histogram is empty or flat. They reuse Image > Adjustments' clip scan
// (auto_levels_clip_scan: 0.1% per end) and Auto Color's midtone target 128:
//   Exposure (EV, -2..3): moves the mean luminance toward 128, never pushing
//     the clip white point past white; darkens only when whites clip.
//   Contrast (-100..100): Auto Contrast's stretch of the clip black and white
//     points, as the S-curve slope that matches it at mid gray.
//   Highlights (-100..100): pulls back clipped whites, or carries a white
//     point short of white up to it.
//   Shadows (-100..100): opens crushed blacks, or deepens a black point that
//     stops short of black.
[[nodiscard]] std::optional<double> raw_auto_exposure_ev(const RawPreviewHistogram& histogram, double rendered);
[[nodiscard]] std::optional<double> raw_auto_contrast(const RawPreviewHistogram& histogram, double rendered);
[[nodiscard]] std::optional<double> raw_auto_highlights(const RawPreviewHistogram& histogram, double rendered);
[[nodiscard]] std::optional<double> raw_auto_shadows(const RawPreviewHistogram& histogram, double rendered);

enum class RawHistogramDragPhase { Begin, Move, End };

// The Develop Raw dialog's histogram (GitHub issue 393): RGB and luminance of
// the latest preview, split into tone regions. Pointing at a region highlights
// it and reports it; a horizontal drag there reports the travel as a fraction
// of the plot width so the host can move that region's slider.
class RawHistogramWidget final : public QWidget {
public:
  explicit RawHistogramWidget(QWidget* parent = nullptr);

  void set_histogram(std::optional<RawPreviewHistogram> histogram);
  [[nodiscard]] const std::optional<RawPreviewHistogram>& histogram() const noexcept { return histogram_; }
  [[nodiscard]] RawHistogramRegion hovered_region() const noexcept { return hovered_; }
  // The plot area in widget coordinates (inside the frame).
  [[nodiscard]] QRect plot_rect() const;

  void set_hover_callback(std::function<void(RawHistogramRegion)> callback);
  // travel = (pointer x - press x) / plot width, cumulative over the drag.
  void set_drag_callback(std::function<void(RawHistogramRegion, double travel, RawHistogramDragPhase)> callback);

  [[nodiscard]] QSize sizeHint() const override;
  [[nodiscard]] QSize minimumSizeHint() const override;

protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void leaveEvent(QEvent* event) override;
  void changeEvent(QEvent* event) override;

private:
  [[nodiscard]] RawHistogramRegion region_at(QPointF position) const;
  void set_hovered(RawHistogramRegion region);
  void finish_drag(double travel, QPointF position);
  void publish_state();

  std::optional<RawPreviewHistogram> histogram_;
  RawHistogramRegion hovered_{RawHistogramRegion::None};
  RawHistogramRegion dragging_{RawHistogramRegion::None};
  double press_x_{0.0};
  double last_travel_{0.0};
  std::function<void(RawHistogramRegion)> hover_changed_;
  std::function<void(RawHistogramRegion, double, RawHistogramDragPhase)> dragged_;
};

}  // namespace patchy::ui
