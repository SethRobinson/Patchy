#pragma once

// Which blend modes each layered save format can store, and the "Keep the look" bake for
// the ones it cannot (GitHub issue 379; docs/blend-modes.md, "Layered saves"). Qt-free:
// the Save path asks the user, scripts choose through saveAs({unsupportedBlendModes}).

#include "core/document.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace patchy {

enum class LayeredSaveFormat : std::uint8_t {
  Photoshop,  // PSD, PSB and layered TIFF (the same layer records)
  Aseprite,
};

// True when `format` stores `mode` on a layer (or, for `is_group`, a folder) of a document
// of `depth` as itself. Photoshop lacks Paint.NET's four modes and, at 32 bits, the modes
// blend_mode_supported_at_depth refuses; Aseprite lacks every mode its writer maps to Normal,
// Pass Through included.
[[nodiscard]] bool layered_format_stores_blend_mode(LayeredSaveFormat format, BlendMode mode, bool is_group,
                                                    BitDepth depth) noexcept;

struct UnsupportedBlendModes {
  std::vector<BlendMode> modes;          // each once, in first-found order (bottom up)
  std::vector<std::string> layer_names;  // every affected layer or folder, bottom up
  [[nodiscard]] bool empty() const noexcept { return layer_names.empty(); }
};

// The layers and folders whose blend mode `format` would write as Normal, hidden ones
// included (they lose the mode too). A Pass Through folder counts only where the format
// lacks Pass Through and the folder holds a visible layer with another mode than Normal:
// otherwise isolating it changes nothing.
[[nodiscard]] UnsupportedBlendModes find_unsupported_blend_modes(const Document& document, LayeredSaveFormat format);

struct BlendBakeResult {
  std::size_t baked{0};   // layers rebuilt as Normal pixel layers
  std::size_t merged{0};  // of those, units that merged several layers (a folder, a clipping run)
  std::size_t cleared{0}; // hidden layers whose mode became Normal
};

// "Keep the look", run on a save snapshot, never on the open document. Each visible layer
// with a mode `format` lacks becomes a Normal pixel layer at full opacity whose pixels,
// composited Normal over what lies below it, give exactly the composite the original layer
// gave there. A folder with such a mode (or a Pass Through folder `format` cannot isolate
// faithfully), and a clipping run that holds one, become one such layer. Hidden layers just
// switch to Normal. The result composites like the original within rounding (one level);
// the trade-off is that a baked layer has frozen what was below it wherever it changed it.
BlendBakeResult bake_unsupported_blend_modes(Document& document, LayeredSaveFormat format);

}  // namespace patchy
