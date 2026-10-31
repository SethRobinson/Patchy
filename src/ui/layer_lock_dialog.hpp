#pragma once

// Layer > Lock Layers... (GitHub issue 544; docs/layer-shortcuts.md, "Lock Layers dialog"): the
// selected layers' locks in one modal dialog. MainWindow::show_layer_locks_dialog runs it and
// applies the result as one undo step.

#include "core/layer.hpp"

#include <cstdint>
#include <optional>
#include <span>

class QWidget;

namespace patchy::ui {

// One lock across the selection: every layer has it, none does, or a mix. A mixed lock the
// user leaves alone stays as each layer has it.
enum class LayerLockChoice : std::uint8_t { Off, On, Mixed };

struct LayerLockChoices {
  LayerLockChoice transparent_pixels{LayerLockChoice::Off};
  LayerLockChoice image_pixels{LayerLockChoice::Off};
  LayerLockChoice position{LayerLockChoice::Off};
  LayerLockChoice auto_nesting{LayerLockChoice::Off};  // Prevent Auto-Nesting

  friend bool operator==(const LayerLockChoices&, const LayerLockChoices&) = default;
};

// The dialog's starting state for layers whose own lock flags are `flags`.
[[nodiscard]] LayerLockChoices layer_lock_choices_for(std::span<const LayerLockFlags> flags);

// A layer's flags after the chosen locks: On sets a lock, Off clears it, Mixed keeps it.
[[nodiscard]] LayerLockFlags apply_layer_lock_choices(LayerLockFlags flags, const LayerLockChoices& choices);

// Runs the modal dialog (object name layerLocksDialog) with Transparent Pixels, Image Pixels,
// Position, All and Prevent Auto-Nesting boxes. A mixed lock starts partly checked; a click
// settles it on or off. Returns the choices, or nullopt on Cancel.
[[nodiscard]] std::optional<LayerLockChoices> request_layer_locks(QWidget* parent, const LayerLockChoices& initial,
                                                                  int layer_count);

}  // namespace patchy::ui
