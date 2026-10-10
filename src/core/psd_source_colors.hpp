#pragma once

#include "core/layer.hpp"

namespace patchy {

struct SmartObjectSource;

// Original color data, independent of the RGB editing buffers. Shared by undo
// snapshots; session-only, never serialized as private PSD tags.
struct PsdNativeColorSpace {
  std::uint16_t mode{0};
  BitDepth depth{BitDepth::UInt16};
  std::vector<std::uint8_t> profile;
};

struct PsdNativeLayerColors {
  std::shared_ptr<const PsdNativeColorSpace> space;
  std::int32_t width{0};
  std::int32_t height{0};
  std::array<std::vector<std::uint8_t>, 4> planes;
  // No children or source-color pointer in this snapshot (no ownership cycle).
  // Its pixels remain copy-on-write. Revision changes trigger content comparison,
  // since a mutable read also bumps revisions without necessarily editing anything.
  std::shared_ptr<const Layer> imported;
  // The placed source belongs to the document, not the layer snapshot. Keep its
  // immutable import state too so replacing embedded contents invalidates inks.
  std::shared_ptr<const SmartObjectSource> smart_object_source;
  std::uint64_t content_revision{0};
};

}  // namespace patchy
