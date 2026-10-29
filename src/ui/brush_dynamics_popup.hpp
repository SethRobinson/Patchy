#pragma once

#include "core/brush_dynamics.hpp"
#include "ui/brush_tool_window.hpp"

#include <QElapsedTimer>
#include <QHash>
#include <QPointer>
#include <QString>
#include <QToolButton>
#include <QWidget>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QTimer;

namespace patchy {
struct BrushTip;
}

namespace patchy::ui {

struct BrushTipEntry;
class BrushStrokePreview;
class BrushToolWindow;

// The dynamics editing form, shared by the options-bar Dynamics popup and the Brush Tips
// manager's editor (GitHub issue 506): a stroke preview strip on top, a column of section tabs
// (Tip Shape, Shape Dynamics, Scattering, Transfer, Texture, Dual Brush, Color Dynamics, Brush
// Effects) beside one page per section, and Reset All. Switchable sections carry their on/off
// box in the tab; a dot marks a section whose values differ from the defaults; each page has
// Reset Section. The current tab is remembered (brushes/dynamicsSection). Emits edited() on
// every user change; hosts read the values back via the getters.
class BrushDynamicsPanel : public QWidget {
  Q_OBJECT

public:
  // Tab order. Persisted by id (section_id), never by index.
  enum class Section {
    TipShape = 0,
    ShapeDynamics,
    Scattering,
    Transfer,
    Texture,
    DualBrush,
    ColorDynamics,
    BrushEffects,
  };
  static constexpr int kSectionCount = 8;

  explicit BrushDynamicsPanel(QWidget* parent = nullptr);

  void set_values(const patchy::BrushDynamics& dynamics, double base_angle_degrees,
                  double base_roundness);
  [[nodiscard]] patchy::BrushDynamics dynamics() const;
  [[nodiscard]] double base_angle_degrees() const;
  [[nodiscard]] double base_roundness() const;

  // The tip the preview strip paints (null hides the strip) and its dab spacing.
  void set_preview_tip(std::shared_ptr<const patchy::BrushTip> tip, double spacing);
  [[nodiscard]] BrushStrokePreview* preview() const noexcept { return preview_; }

  void select_section(Section section);
  [[nodiscard]] Section current_section() const;
  // True while the section's tab shows the changed-from-default dot.
  [[nodiscard]] bool section_marked_changed(Section section) const;
  [[nodiscard]] static QString section_id(Section section);

signals:
  void edited();

private:
  struct SectionTab {
    QPushButton* button{nullptr};
    QCheckBox* enabled{nullptr};  // null for Tip Shape and Brush Effects
    QWidget* dot{nullptr};
    QWidget* body{nullptr};       // the page's rows; disabled while the section is off
    QLabel* title{nullptr};
    QPushButton* reset{nullptr};
  };

  void reset_to_defaults();
  void reset_section(Section section);
  void after_values_changed();
  // Fade-steps spins show only while their combo says Fade; minimum rows are live only while
  // their jitter or control can reach them; Count rows only while Count is above 1; a section's
  // rows only while it is on.
  void refresh_control_dependent_widgets();
  void refresh_section_markers();
  void set_row_enabled(QWidget* field, bool enabled);
  void retitle_tabs();

  std::array<SectionTab, kSectionCount> tabs_{};
  QStackedWidget* stack_{nullptr};
  BrushStrokePreview* preview_{nullptr};
  // Each value field's row label and slider, so a row disables as one.
  QHash<QWidget*, std::pair<QLabel*, QSlider*>> rows_;
  QCheckBox* shape_dynamics_enabled_check_{nullptr};
  QCheckBox* scattering_enabled_check_{nullptr};
  QCheckBox* transfer_enabled_check_{nullptr};

  QSpinBox* base_angle_spin_{nullptr};
  QSpinBox* base_roundness_spin_{nullptr};
  QSpinBox* size_jitter_spin_{nullptr};
  QSpinBox* minimum_diameter_spin_{nullptr};
  QComboBox* size_control_combo_{nullptr};
  QSpinBox* size_fade_steps_spin_{nullptr};
  QSpinBox* angle_jitter_spin_{nullptr};
  QComboBox* angle_control_combo_{nullptr};
  QSpinBox* fade_steps_spin_{nullptr};
  QSpinBox* roundness_jitter_spin_{nullptr};
  QSpinBox* minimum_roundness_spin_{nullptr};
  QComboBox* roundness_control_combo_{nullptr};
  QSpinBox* roundness_fade_steps_spin_{nullptr};
  QCheckBox* flip_x_check_{nullptr};
  QCheckBox* flip_y_check_{nullptr};
  QSpinBox* scatter_spin_{nullptr};
  QComboBox* scatter_control_combo_{nullptr};
  QSpinBox* scatter_fade_steps_spin_{nullptr};
  QCheckBox* both_axes_check_{nullptr};
  QSpinBox* count_spin_{nullptr};
  QSpinBox* count_jitter_spin_{nullptr};
  QComboBox* count_control_combo_{nullptr};
  QSpinBox* count_fade_steps_spin_{nullptr};
  QSpinBox* opacity_jitter_spin_{nullptr};
  QSpinBox* minimum_opacity_spin_{nullptr};
  QComboBox* opacity_control_combo_{nullptr};
  QSpinBox* opacity_fade_steps_spin_{nullptr};
  QSpinBox* flow_jitter_spin_{nullptr};
  QSpinBox* minimum_flow_spin_{nullptr};
  QComboBox* flow_control_combo_{nullptr};
  QSpinBox* flow_fade_steps_spin_{nullptr};
  QCheckBox* texture_enabled_check_{nullptr};
  QComboBox* texture_style_combo_{nullptr};
  QSpinBox* texture_scale_spin_{nullptr};
  QSpinBox* texture_depth_spin_{nullptr};
  QCheckBox* texture_invert_check_{nullptr};
  std::uint32_t texture_seed_{0x5A17C9E3U};  // imported/persisted, intentionally not user-edited
  QCheckBox* dual_brush_enabled_check_{nullptr};
  QSpinBox* dual_brush_size_spin_{nullptr};
  QSpinBox* dual_brush_hardness_spin_{nullptr};
  QSpinBox* dual_brush_spacing_spin_{nullptr};
  QCheckBox* color_dynamics_enabled_check_{nullptr};
  QSpinBox* foreground_background_jitter_spin_{nullptr};
  QComboBox* color_control_combo_{nullptr};
  QSpinBox* color_fade_steps_spin_{nullptr};
  QSpinBox* hue_jitter_spin_{nullptr};
  QSpinBox* saturation_jitter_spin_{nullptr};
  QSpinBox* brightness_jitter_spin_{nullptr};
  QSpinBox* purity_spin_{nullptr};
  QCheckBox* color_per_tip_check_{nullptr};
  QCheckBox* wet_edges_check_{nullptr};
  bool loading_{false};
};

// Options-bar Dynamics button for the Brush tool (a sliders icon, checked while open): opens a popup hosting a BrushDynamicsPanel
// for the active bitmap tip, or for the procedural Round brush's session-only dynamics. Edits
// are debounced and emitted via dynamics_edited; MainWindow persists bitmap-tip values to the
// library sidecar, while Round values live in the window for the session and reset on launch.
class BrushDynamicsButton : public QToolButton {
  Q_OBJECT

public:
  explicit BrushDynamicsButton(QWidget* parent = nullptr);
  ~BrushDynamicsButton() override;

  // Loads the button's model from the active tip entry (null = clear/disable). While the
  // popup is open for the same tip, or an edit is still waiting on its debounce, the reload is
  // skipped so a library changed() echo does not fight the controls or revert the edit.
  void set_active_entry(const BrushTipEntry* entry);
  // Supplies a library tip's coverage mask for the popup's preview strip (MainWindow passes
  // the library lookup). The Round and Square brushes use a synthesized disc or square.
  void set_preview_tip_provider(
      std::function<std::shared_ptr<const patchy::BrushTip>(const QString& tip_id)> provider);
  // Loads the button's model for the procedural Round brush's session-only dynamics; the id is
  // the builtin round key MainWindow routes on. Same popup/edit flow as a bitmap tip.
  void set_round_session(const QString& round_tip_id, const patchy::BrushDynamics& dynamics,
                         double base_angle_degrees, double base_roundness);
  // True while a bitmap tip is active; MainWindow::refresh_options_bar combines this with the
  // document-editability flag instead of blanket-enabling the button.
  [[nodiscard]] bool has_active_tip() const noexcept { return !tip_id_.isEmpty(); }
  // Opens the popup as a click would; does nothing while it is open.
  void open_popup();
  [[nodiscard]] bool popup_open() const noexcept { return popup_ != nullptr; }
  void retranslate();

  // The floating Brush Settings window (Window > Brush Settings, or the popup's Keep Open;
  // GitHub issue 506 follow-up): one panel that edits the same model as the popup, live, while
  // the user paints. While it is open the button raises it instead of opening the popup.
  void open_window(std::optional<QPoint> global_top_left = std::nullopt, bool activate = true);
  [[nodiscard]] BrushToolWindow* tool_window() const noexcept { return window_; }
  [[nodiscard]] BrushDynamicsPanel* window_panel() const noexcept { return window_panel_; }
  void set_window_key_target(std::function<QWidget*()> key_target);
  // False while the active tool is not the Brush (or no document is open): the window says
  // why and its panel is disabled.
  void set_tool_supported(bool supported, const QString& reason = {});
  void hide_window_for_shutdown();

signals:
  void dynamics_edited(const QString& tip_id, const patchy::BrushDynamics& dynamics,
                       double base_angle_degrees, double base_roundness);

private:
  void show_popup();
  void sync_checked();
  // Loads the model (values and preview tip) into a popup or window panel.
  void load_panel(BrushDynamicsPanel& panel) const;
  // A reload from outside (a tip switch, a script, a preset) reaches the open panels.
  void reload_open_panels();
  void refresh_window_availability();
  void connect_panel(BrushDynamicsPanel& panel);
  void schedule_emit();
  [[nodiscard]] bool edit_pending() const;
  void refresh_active_indicator();

  QString tip_id_;
  double tip_spacing_{0.25};
  std::function<std::shared_ptr<const patchy::BrushTip>(const QString&)> preview_tip_provider_;
  bool round_session_{false};  // model is the Round brush's session dynamics, not a library tip
  patchy::BrushDynamics dynamics_{};
  double base_angle_degrees_{0.0};
  double base_roundness_{100.0};
  QPointer<QFrame> popup_;
  QPointer<BrushDynamicsPanel> popup_panel_;
  QPointer<BrushToolWindow> window_;
  QPointer<BrushDynamicsPanel> window_panel_;
  std::function<QWidget*()> window_key_target_;
  bool tool_supported_{true};
  QString unsupported_reason_;
  // The debounced edit, captured when scheduled: the timer emits these, not the live model, so
  // a reload that lands before it fires cannot turn the edit back into the stored values.
  QTimer* emit_timer_{nullptr};
  QString pending_tip_id_;
  patchy::BrushDynamics pending_dynamics_{};
  double pending_base_angle_degrees_{0.0};
  double pending_base_roundness_{100.0};
  // Toggle guard: the click that dismisses the Qt::Popup is replayed onto the button (or lands
  // while the popup is still closing), which would instantly reopen it. See show_popup().
  QElapsedTimer popup_clock_;
  qint64 popup_dismissed_ms_{-1};
};

}  // namespace patchy::ui
