#pragma once

// The clone source settings (GitHub issue 424, docs/healing.md "Clone source settings"):
// four source slots, the active source's point and offset, the Clone Stamp's scale and
// rotation with a Reset, and the overlay preview with its opacity. It shows what
// MainWindow hands it through set_state (signals blocked) and reports each edit through
// its signals; MainWindow applies them to the active canvas. The options bar's gear
// popup shows it.

#include "core/clone_source_transform.hpp"

#include <QPoint>
#include <QWidget>

#include <array>

class QButtonGroup;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QToolButton;

namespace patchy::ui {

class CloneSourcePanel final : public QWidget {
  Q_OBJECT

public:
  static constexpr int kSlotCount = 4;

  struct State {
    bool has_document{false};
    int slot{0};
    std::array<bool, kSlotCount> slot_set{};
    std::array<QPoint, kSlotCount> slot_point{};
    QPoint offset{};
    bool offset_set{false};
    bool aligned{true};
    patchy::CloneSourceTransform transform{};
    // False on the Healing Brush, which samples its source untransformed.
    bool transform_applies{true};
    bool overlay_shown{false};
    int overlay_opacity{50};
  };

  explicit CloneSourcePanel(QWidget* parent = nullptr);

  void set_state(const State& state);
  [[nodiscard]] const State& state() const noexcept { return state_; }

  [[nodiscard]] QToolButton* slot_button(int slot) const;
  [[nodiscard]] QLabel* source_label() const noexcept { return source_label_; }
  [[nodiscard]] QSpinBox* offset_x_spin() const noexcept { return offset_x_spin_; }
  [[nodiscard]] QSpinBox* offset_y_spin() const noexcept { return offset_y_spin_; }
  [[nodiscard]] QDoubleSpinBox* scale_spin() const noexcept { return scale_spin_; }
  [[nodiscard]] QDoubleSpinBox* rotation_spin() const noexcept { return rotation_spin_; }
  [[nodiscard]] QPushButton* reset_button() const noexcept { return reset_button_; }
  [[nodiscard]] QCheckBox* overlay_check() const noexcept { return overlay_check_; }
  [[nodiscard]] QSpinBox* overlay_opacity_spin() const noexcept { return overlay_opacity_spin_; }

signals:
  void slot_picked(int slot);
  void offset_edited(QPoint offset);
  void transform_edited(patchy::CloneSourceTransform transform);
  void overlay_edited(bool shown, int opacity);

protected:
  void changeEvent(QEvent* event) override;

private:
  void retranslate();
  void refresh();
  void emit_transform();

  State state_;
  std::array<QToolButton*, kSlotCount> slot_buttons_{};
  QButtonGroup* slot_group_{nullptr};
  QLabel* slots_label_{nullptr};
  QLabel* source_caption_{nullptr};
  QLabel* source_label_{nullptr};
  QLabel* offset_label_{nullptr};
  QLabel* offset_x_label_{nullptr};
  QLabel* offset_y_label_{nullptr};
  QSpinBox* offset_x_spin_{nullptr};
  QSpinBox* offset_y_spin_{nullptr};
  QLabel* offset_note_{nullptr};
  QLabel* scale_label_{nullptr};
  QDoubleSpinBox* scale_spin_{nullptr};
  QLabel* rotation_label_{nullptr};
  QDoubleSpinBox* rotation_spin_{nullptr};
  QPushButton* reset_button_{nullptr};
  QLabel* transform_note_{nullptr};
  QCheckBox* overlay_check_{nullptr};
  QLabel* overlay_opacity_label_{nullptr};
  QSpinBox* overlay_opacity_spin_{nullptr};
};

}  // namespace patchy::ui
