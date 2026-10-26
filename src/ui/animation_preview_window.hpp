#pragma once

#include "core/document.hpp"

#include <QDialog>
#include <QTimer>

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;

namespace patchy::ui {

// The layers panel's film button opens this small floating tool window. Play hides every
// top-level layer except the current frame and cycles through the layers that were
// visible when playback started (every top-level layer with "Play hidden layers"), top to
// bottom (the GIF and WebP export order); Stop restores the visibility captured at start.
// A trailing "0.25s" layer-name token overrides the default frame delay, matching the
// export, unless "Ignore delays in layer names" is on; the delay spin and its presets edit
// the shared saveOptions/animationFrameDelayMs default. Both checkboxes affect the preview
// only, never what export writes. Playback
// mutates visibility directly (visibility is deliberately non-undoable and does not dirty
// the session) and never outlives its document: MainWindow stops playback on tab switches
// and before a session's document is destroyed, and stop re-checks the provider anyway.
class AnimationPreviewWindow : public QDialog {
  Q_OBJECT

public:
  // document_provider returns the ACTIVE document (or null), pulled fresh at play start
  // and on every advance; playback aborts without restoring if it stops matching the
  // document playback started on. visuals_changed repaints after visibility mutations:
  // final_refresh false is the per-frame path (canvas plus a cheap panel eye sync), true
  // runs once after stop (full panel refresh). apply_selection_frame_time stamps (a value)
  // or strips (nullopt) the trailing frame-time name token on the layers-panel selection;
  // the window stops playback first so the caller's undo snapshot never captures a preview
  // frame's visibility. all_layers widens it from the selection to every top-level layer.
  AnimationPreviewWindow(
      std::function<Document*()> document_provider, std::function<void(bool final_refresh)> visuals_changed,
      std::function<void(std::optional<std::uint32_t> delay_ms, bool all_layers)> apply_frame_time,
      QWidget* parent);

  [[nodiscard]] bool playing() const noexcept { return playback_document_ != nullptr; }
  void toggle_playback();
  void start_playback();
  // restore_visibility puts every surviving top-level layer back to its captured state
  // (skipped defensively when the playback document is no longer the provider's).
  void stop_playback(bool restore_visibility);
  // Stops with restore only when `document` is the one playback started on. MainWindow
  // calls this on tab switches (while the outgoing document is still active) and before a
  // closing session's document is destroyed.
  void stop_playback_for(const Document* document);
  // Advances to the next frame and rearms the timer; public so tests can step
  // deterministically (the playback timer calls this).
  void advance_frame();
  // The display time of the current frame, as the timer uses it (name token unless names
  // are ignored, else the spin's default, floored so 0-delay frames cannot spin the event
  // loop).
  [[nodiscard]] int current_frame_delay_ms() const;

  // Every dismissal funnels here and stops playback with restore (see TilePreviewWindow's
  // header note: never override reject() to call close()).
  void done(int result) override;

private:
  void show_current_frame();
  [[nodiscard]] std::uint32_t frame_delay_ms(std::size_t index) const;
  void update_playback_controls();
  // Shows the preset matching the delay spin, or Custom.
  void sync_delay_preset();

  std::function<Document*()> provider_;
  std::function<void(bool final_refresh)> visuals_changed_;
  std::function<void(std::optional<std::uint32_t> delay_ms, bool all_layers)> apply_frame_time_;
  QTimer timer_;
  QPushButton* play_button_{nullptr};
  QDoubleSpinBox* delay_spin_{nullptr};
  QComboBox* delay_preset_combo_{nullptr};
  QCheckBox* ignore_name_delays_check_{nullptr};
  QCheckBox* play_hidden_check_{nullptr};
  QCheckBox* all_layers_check_{nullptr};
  QDoubleSpinBox* selection_time_spin_{nullptr};
  QLabel* status_label_{nullptr};

  // Playback state; playback_document_ doubles as the playing flag.
  Document* playback_document_{nullptr};
  std::vector<LayerId> frame_ids_;                          // top to bottom, the frames at start
  std::vector<std::pair<LayerId, bool>> saved_visibility_;  // every top-level layer at start
  std::size_t frame_index_{0};
};

}  // namespace patchy::ui
