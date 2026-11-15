#pragma once

// The 32-bit color controls (docs/high-bit-depth-color.md, "Color choice at 32 bits"): a color's
// linear value, its Intensity in stops and a strip of swatches showing it a few stops
// darker and brighter, so a color above display white can be judged on an SDR screen.
// The Foreground and Background Color windows show the full form, the Color panel the
// compact one; both only while the active document is 32-bit. The base color itself stays
// with the picker or the panel; these controls own the Intensity.
// The full form is a line, then two columns: the caption and the swatches on the left,
// the Intensity and the linear fields on the right (GitHub issue 255).

#include "core/hdr_color.hpp"

#include <QColor>
#include <QPointer>
#include <QWidget>

#include <array>

class QBoxLayout;
class QDialog;
class QDoubleSpinBox;
class QLabel;
class QSlider;

namespace patchy::ui {

class HdrStopPreviewStrip;

class HdrColorControls final : public QWidget {
  Q_OBJECT

public:
  enum class Form { Full, Compact };

  explicit HdrColorControls(Form form, QWidget* parent = nullptr);

  // Shows `base` at `stops`; emits nothing.
  void set_color(QColor base, double stops);
  [[nodiscard]] QColor base() const noexcept { return base_; }
  [[nodiscard]] double stops() const noexcept { return color_.stops; }
  [[nodiscard]] HdrColor hdr_color() const noexcept { return color_; }
  [[nodiscard]] LinearRgb linear() const noexcept;

  [[nodiscard]] QDoubleSpinBox* intensity_spin() const noexcept { return intensity_spin_; }
  // The Full form's editable linear R, G and B (null in the Compact form).
  [[nodiscard]] QDoubleSpinBox* linear_spin(int channel) const;
  [[nodiscard]] HdrStopPreviewStrip* preview_strip() const noexcept { return strip_; }
  // The Compact form's linear readout (null in the Full form).
  [[nodiscard]] QLabel* linear_label() const noexcept { return linear_label_; }

  // The Full form's two columns take the widths of `left` and `right`, two side-by-side
  // widgets of the window above it (the color picker's swatch and picker columns), and
  // follow them as the window resizes. Without them the left column is as wide as the
  // swatches. No effect on the Compact form.
  void follow_columns(QWidget* left, QWidget* right);

  // The stop offsets the preview strip shows, darkest first; 0 is the color itself.
  static constexpr std::array<int, 7> kPreviewOffsets{-3, -2, -1, 0, 1, 2, 3};

signals:
  // The user changed the Intensity (spin, slider or a preview swatch).
  void intensity_changed(double stops);
  // The user typed a linear value: `base` and `stops` hold it now.
  void linear_color_entered(QColor base, double stops);

protected:
  void changeEvent(QEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  void fit_columns();
  void retranslate();
  void refresh();
  void take_intensity(double stops);
  void take_linear_fields();

  QColor base_{Qt::black};
  HdrColor color_{};
  QLabel* caption_{nullptr};
  QLabel* intensity_label_{nullptr};
  QLabel* stops_label_{nullptr};
  QLabel* linear_title_{nullptr};
  QSlider* intensity_slider_{nullptr};
  QDoubleSpinBox* intensity_spin_{nullptr};
  std::array<QLabel*, 3> linear_channel_labels_{};
  std::array<QDoubleSpinBox*, 3> linear_spins_{};
  QLabel* linear_label_{nullptr};
  HdrStopPreviewStrip* strip_{nullptr};
  // The Full form's column row and the widgets its columns follow.
  QBoxLayout* columns_{nullptr};
  QPointer<QWidget> left_like_;
  QPointer<QWidget> right_like_;
  bool followed_laid_out_{false};
};

// The preview swatches: the color at each of HdrColorControls::kPreviewOffsets, the current
// one wider and outlined, each labeled with its offset. A click on another swatch moves the
// Intensity by its offset.
class HdrStopPreviewStrip final : public QWidget {
  Q_OBJECT

public:
  HdrStopPreviewStrip(bool compact, QWidget* parent = nullptr);

  void set_color(const HdrColor& color);
  // The swatch color shown at `offset` stops (one of kPreviewOffsets).
  [[nodiscard]] QColor swatch_color(int offset) const;
  // The swatch rectangle for `offset`, in widget coordinates.
  [[nodiscard]] QRect swatch_rect(int offset) const;
  [[nodiscard]] QSize sizeHint() const override;
  [[nodiscard]] QSize minimumSizeHint() const override;

signals:
  void offset_clicked(int offset);

protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;

private:
  [[nodiscard]] int label_height() const;

  bool compact_;
  HdrColor color_{};
};

// Adds the Full form to a color window (create_patchy_color_panel) above its buttons,
// its columns following the picker's, hidden until the caller shows it for a 32-bit
// document.
HdrColorControls* add_hdr_color_controls(QDialog& dialog);

}  // namespace patchy::ui
