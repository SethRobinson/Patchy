#pragma once

#include "core/adjustment_layer.hpp"

#include <QWidget>

#include <array>
#include <cstdint>
#include <functional>
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

// Builds all four display histograms in one bounded pass. An optional external
// alpha plane lets RGB compositor output keep transparent pixels out without an
// RGBA copy; it must contain exactly one byte per source pixel. Invalid, empty,
// or unsupported sources return empty histograms rather than decorative data
// that could be mistaken for the image's actual tonal distribution.
[[nodiscard]] CurvesHistograms curves_histograms_from_pixels(
    const PixelBuffer* source, std::span<const std::uint8_t> external_alpha = {});

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

  QTabBar* channel_tabs_{nullptr};
  CurvesGraphWidget* graph_{nullptr};
  QSpinBox* input_spin_{nullptr};
  QSpinBox* output_spin_{nullptr};
  QPushButton* reset_button_{nullptr};
  QPushButton* auto_button_{nullptr};
};

}  // namespace ui
}  // namespace patchy
