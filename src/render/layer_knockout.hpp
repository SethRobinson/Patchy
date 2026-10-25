#pragma once

#include "core/layer.hpp"

#include <algorithm>

// The PSD knockout mode of a layer, on its own so writers that only need the mode
// (ora_document_io, native_cmyk) do not include the compositor templates.
namespace patchy::render_detail {

// Native PSD 'knko': one mode byte followed by three padding bytes. Keep the
// original block untouched; unsupported values have no rendering semantics.
[[nodiscard]] inline unsigned layer_knockout_mode(const Layer& layer) noexcept {
  for (const auto& block : layer.unknown_psd_blocks()) {
    if (block.key == "knko" && block.payload.size() == 4U) {
      return block.payload[0] <= 2U ? block.payload[0] : 0U;
    }
  }
  return 0U;
}

[[nodiscard]] inline bool layer_tree_has_knockout(const Layer& layer) noexcept {
  return layer_knockout_mode(layer) != 0U ||
         std::any_of(layer.children().begin(), layer.children().end(), layer_tree_has_knockout);
}

}  // namespace patchy::render_detail
