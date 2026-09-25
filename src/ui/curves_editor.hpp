#pragma once

#include "core/adjustment_layer.hpp"

#include <QWidget>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>

class QPushButton;
class QSpinBox;
class QTabBar;

namespace patchy {

class PixelBuffer;

namespace ui {

class CurvesGraphWidget;

struct CurvesHistograms {
  std::array<std::uint32_t, 256> rgb{};
  std::array<std::uint32_t, 256> red{};
  std::array<std::uint32_t, 256> green{};
  std::array<std::uint32_t, 256> blue{};
};

// Builds all four display histograms in one pass, binning one box-averaged
// sample per 2x2 block so quantized sources chart without comb gaps;
// the composite is the sum of the channel counts, matching Photoshop. An
// optional external alpha plane lets RGB compositor output keep transparent
// pixels out without an RGBA copy; it must contain exactly one byte per source
// pixel. Invalid, empty, or unsupported sources return empty histograms rather
// than decorative data that could be mistaken for the image's actual tonal
// distribution.
[[nodiscard]] CurvesHistograms curves_histograms_from_pixels(
    const PixelBuffer* source, std::span<const std::uint8_t> external_alpha = {});

// A histogram graph's full-scale count is this multiple of the mean non-empty
// bin. Measured against Photoshop's Levels dialog (September 2026, see
// docs/adjustments-calibration.md).
inline constexpr std::uint64_t kHistogramCeilingMeanMultiple = 4;

// Full-scale count for drawing a histogram the way Photoshop's Levels and
// Curves dialogs do: four times the mean of the non-empty bins
// (total_samples / non_empty_bins). The ceiling depends on the samples and on
// how many bins they occupy, never on the tallest bin, so one clipping spike
// cannot flatten the rest of the distribution. Pass the sum and non-empty
// count of the bins actually drawn (a channel total for a channel graph, the
// composite total for the composite graph). Returns 0 for an empty histogram.
[[nodiscard]] double histogram_display_ceiling(std::uint64_t total_samples, std::uint64_t non_empty_bins);

// Bar height (0..1) for one bin: linear in the count and clipped at the
// ceiling from histogram_display_ceiling. Returns 0 when the ceiling is 0.
[[nodiscard]] double histogram_display_fraction(std::uint64_t count, double ceiling);

// Reusable Curves editing panel. The host owns the authoritative adjustment:
// every interaction reports a proposed copy through adjustment_changed, and
// the host pushes accepted state back through set_adjustment(). Keeping the
// model outside the widget makes the panel suitable for both dialogs and the
// future contextual Properties editor.
class CurvesEditorWidget final : public QWidget {
  Q_OBJECT

public:
  explicit CurvesEditorWidget(QWidget* parent = nullptr);

  void set_adjustment(const CurvesAdjustment& adjustment);
  void set_histograms(CurvesHistograms histograms);
  void set_active_channel(CurvesChannel channel);
  void set_selected_point(int index);
  [[nodiscard]] bool begin_tonal_sample(int input);
  void update_tonal_sample(int output_delta, bool finished);
  void cancel_tonal_sample();
  void apply_external_adjustment(const CurvesAdjustment& adjustment, bool finished = true);

  [[nodiscard]] const CurvesAdjustment& adjustment() const noexcept;
  [[nodiscard]] CurvesChannel active_channel() const noexcept;
  [[nodiscard]] int selected_point() const noexcept;
  [[nodiscard]] QSize sizeHint() const override;

  // gesture_finished is false for in-flight drags/spin edits and true at a
  // release/editing boundary. Dialog hosts can coalesce the former and flush
  // the latter; a persistent Properties host can use it as an undo boundary.
  std::function<void(const CurvesAdjustment& adjustment, bool gesture_finished)> adjustment_changed;
  std::function<void(CurvesChannel channel)> active_channel_changed;

private:
  void normalize_and_store(const CurvesAdjustment& adjustment);
  void sync_children();
  void sync_numeric_controls();
  void propose_adjustment(CurvesAdjustment adjustment, bool gesture_finished);
  [[nodiscard]] int add_point(int input, int output);
  void select_point(int index);
  void change_point(int index, int input, int output, bool gesture_finished);
  void delete_point(int index);
  void cycle_point(int direction);
  void reset_curves();
  void auto_curve();
  [[nodiscard]] const std::array<std::uint32_t, 256>& active_histogram() const;

  CurvesAdjustment adjustment_{};
  CurvesHistograms histograms_{};
  CurvesChannel active_channel_{CurvesChannel::Rgb};
  int selected_point_{0};
  bool updating_controls_{false};
  std::optional<CurvesAdjustment> tonal_sample_start_{};
  int tonal_sample_point_{-1};
  int tonal_sample_input_{0};
  int tonal_sample_output_{0};

  QTabBar* channel_tabs_{nullptr};
  CurvesGraphWidget* graph_{nullptr};
  QSpinBox* input_spin_{nullptr};
  QSpinBox* output_spin_{nullptr};
  QPushButton* reset_button_{nullptr};
  QPushButton* auto_button_{nullptr};
};

}  // namespace ui
}  // namespace patchy
