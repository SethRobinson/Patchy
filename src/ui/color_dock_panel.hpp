#pragma once

// The right column's Color panel (docs/right-docks.md, "Color panel"; GitHub issue 365):
// the foreground/background pair with its swap and default glyphs, and one of four
// views picked from the settings gear (GitHub issue 404): the Hue Cube (a saturation/value
// field and a hue strip), RGB Sliders, HSB Sliders or CMYK Sliders. Every view shows
// and edits the foreground. In the cube a click or drag sets the foreground; with Alt
// (Option on macOS) held at the press it sets the background, for the whole drag, as the
// Pick tool's Alt-click does. A slider or its number sets the foreground. MainWindow
// applies each pick and pushes every color change back through set_colors.

#include "ui/color_field_widgets.hpp"

#include <QColor>
#include <QString>
#include <QWidget>

#include <array>
#include <memory>
#include <optional>

class QAction;
class QActionGroup;
class QEvent;
class QLabel;
class QMenu;
class QSpinBox;
class QStackedWidget;
class QToolButton;
class QVBoxLayout;

namespace patchy {
class CmykWorkingSpace;
}

namespace patchy::ui {

class BackgroundPickCursor;
class ColorChannelTrack;
class ColorWellSwatch;
class ColorWellGlyphButton;

class ColorDockPanel final : public QWidget, public HsvFieldSource {
  Q_OBJECT

public:
  // Append only: the order is the menu's.
  enum class View { HueCube, RgbSliders, HsbSliders, CmykSliders };
  Q_ENUM(View)
  // Red, Green and Blue are 0..255, Hue 0..359 degrees, Saturation and Brightness
  // 0..100 percent, Cyan, Magenta, Yellow and Black 0..100 percent ink in the working
  // CMYK space, the Color Picker window's numbers.
  enum class Channel { Red, Green, Blue, Hue, Saturation, Brightness, Cyan, Magenta, Yellow, Black };

  ColorDockPanel(QAction* swap_colors, QAction* default_colors, QWidget* parent = nullptr);

  // Shows the current pair. Cheap: it stores the colors and repaints only what changed.
  void set_colors(QColor foreground, QColor background);

  [[nodiscard]] View view() const noexcept { return view_; }
  // Shows `view` and checks its menu entry; emits view_changed when it changes.
  void set_view(View view);
  // The settings value for a view ("hueCube", "rgbSliders", "hsbSliders", "cmykSliders";
  // persisted, never renamed) and back (nullopt for anything else).
  [[nodiscard]] static QString view_token(View view);
  [[nodiscard]] static std::optional<View> view_from_token(const QString& token);
  // The panel's height for the current view, margins included, when the view has a
  // natural height (the slider views); 0 for the Hue Cube, which fills what it gets.
  [[nodiscard]] int fitted_height() const;

  [[nodiscard]] ColorWellSwatch* foreground_chip() const noexcept { return foreground_chip_; }
  [[nodiscard]] ColorWellSwatch* background_chip() const noexcept { return background_chip_; }
  [[nodiscard]] ColorPlaneWidget* field() const noexcept { return field_; }
  [[nodiscard]] HueSliderWidget* hue_strip() const noexcept { return hue_strip_; }
  [[nodiscard]] BackgroundPickCursor* field_cursor() const noexcept { return field_cursor_; }
  [[nodiscard]] BackgroundPickCursor* hue_strip_cursor() const noexcept { return hue_strip_cursor_; }
  // The settings gear with the view menu. MainWindow places it in the panel's title row,
  // so it is not a child of the panel.
  [[nodiscard]] QToolButton* settings_button() const noexcept { return settings_button_; }
  [[nodiscard]] QAction* view_action(View view) const;
  [[nodiscard]] QWidget* channel_track(Channel channel) const;
  [[nodiscard]] QSpinBox* channel_spin(Channel channel) const;

  // The foreground in a channel's own range, as its slider and number show it.
  [[nodiscard]] int channel_value(Channel channel) const;
  [[nodiscard]] static int channel_maximum(Channel channel);
  // The color a channel's track shows at `level`, the other channels held.
  [[nodiscard]] QColor channel_color(Channel channel, int level) const;
  // Sets one channel of the foreground and emits the pick.
  void set_channel(Channel channel, int level);
  // The working CMYK space changed (Preferences): the same foreground has other ink numbers.
  void working_cmyk_space_changed();
  // A press on a channel track: until its release the canvas's answer does not move it.
  void begin_channel_drag();
  void end_channel_drag();
  // A press in the field, the hue strip or a track is held: its moves are picks of one drag.
  [[nodiscard]] bool picking() const noexcept { return picking_; }

  // HsvFieldSource: the foreground's hue, saturation and value.
  [[nodiscard]] int hue() const override { return hue_; }
  [[nodiscard]] int saturation() const override { return saturation_; }
  [[nodiscard]] int value() const override { return value_; }
  void begin_field_pick(Qt::KeyboardModifiers modifiers) override;
  void end_field_pick() override;
  void set_saturation_value_from_point(QPoint point, QSize size) override;
  void set_hue_from_point(QPoint point, QSize size) override;

signals:
  // A pick in the field, the hue strip or a slider, on the press and on every move of
  // its drag, and a number edit.
  void color_picked(QColor color, bool background);
  // The release that ends a drag in the field, the hue strip or a track. MainWindow
  // writes the settings the drag's picks left for it here (GitHub issue 464).
  void pick_finished();
  // The user (or set_view) picked another view.
  void view_changed(patchy::ui::ColorDockPanel::View view);

protected:
  void changeEvent(QEvent* event) override;

private:
  static constexpr int kChannelCount = 10;
  static constexpr int kViewCount = 4;

  QWidget* build_slider_page(Channel first, int rows);
  void retranslate();
  void adopt_foreground(QColor color);
  void produce_foreground(QColor color);
  void refresh_channel_views();
  // Takes an RGB or CMYK slider's color: HSV follows, keeping the hue of a gray and the
  // hue and saturation of black.
  void take_slider_color(QColor color);
  // The inks for rgb_, unless they already are the ones it came from.
  void update_cmyk();
  [[nodiscard]] QColor color_from_cmyk(const std::array<int, 4>& cmyk) const;
  void emit_pick(QColor color);

  ColorWellSwatch* foreground_chip_{nullptr};
  ColorWellSwatch* background_chip_{nullptr};
  ColorWellGlyphButton* swap_{nullptr};
  ColorWellGlyphButton* default_{nullptr};
  QVBoxLayout* well_column_{nullptr};
  QToolButton* settings_button_{nullptr};
  QMenu* view_menu_{nullptr};
  QActionGroup* view_group_{nullptr};
  std::array<QAction*, kViewCount> view_actions_{};
  QStackedWidget* views_{nullptr};
  ColorPlaneWidget* field_{nullptr};
  HueSliderWidget* hue_strip_{nullptr};
  BackgroundPickCursor* field_cursor_{nullptr};
  BackgroundPickCursor* hue_strip_cursor_{nullptr};
  std::array<QLabel*, kChannelCount> channel_labels_{};
  std::array<ColorChannelTrack*, kChannelCount> channel_tracks_{};
  std::array<QSpinBox*, kChannelCount> channel_spins_{};
  View view_{View::HueCube};
  int hue_{0};
  int saturation_{0};
  int value_{0};
  // The foreground as RGB: the RGB sliders edit it directly, never through HSV, whose
  // 8-bit round trip can move a channel by one.
  QColor rgb_{Qt::black};
  // The foreground as ink percentages. They keep what the user set: RGB cannot hold every
  // ink mix, so they are re-derived only when the foreground is no longer cmyk_color_ (the
  // color those inks gave) or the working space changed.
  std::array<int, 4> cmyk_{0, 0, 0, 100};
  QColor cmyk_color_;
  std::shared_ptr<const CmykWorkingSpace> cmyk_space_;
  // The foreground this panel last produced: when the canvas hands the same color
  // back, the field keeps its own hue and saturation rather than re-deriving them
  // through RGB, which would move the crosshair under the pointer.
  QColor produced_foreground_;
  bool picking_{false};
  bool pick_background_{false};
};

}  // namespace patchy::ui
