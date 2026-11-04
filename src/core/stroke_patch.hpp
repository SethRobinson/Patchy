#pragma once

// A paint stroke's undo record: the rectangle the stroke touched inside one
// pixel buffer (a layer's pixels, its mask, or a saved alpha channel) with the
// pixels that rectangle held before the stroke, and, once an undo has run,
// after it. History keeps one of these instead of a whole-document snapshot
// for Brush, Eraser, Clone, Healing, Smudge, Dodge/Burn, Mixer and Pattern
// Stamp strokes (docs/performance-strokes.md, "History"). Everything else about the
// document is unchanged by such a stroke, so the patch is relative to the
// next newer history state (or the live document).

#include "core/document_channel.hpp"
#include "core/layer.hpp"
#include "core/pixel_buffer.hpp"

#include <cstddef>
#include <optional>

namespace patchy {

class Document;

struct StrokePatch {
  enum class Target {
    LayerContent,
    LayerMask,
    DocumentChannel
  };

  Target target{Target::LayerContent};
  LayerId layer_id{0};
  ChannelId channel_id{0};
  // The patched rectangle in the target buffer's own coordinates (layer- or
  // mask-local; document coordinates for a saved channel).
  Rect rect{};
  // The same rectangle in document coordinates, for the repaint after a restore.
  Rect document_rect{};
  // Pixels of `rect` before the stroke (always present).
  PixelBuffer before{};
  // Pixels of `rect` after the stroke; captured by the first undo so redo can
  // put them back. Empty until then.
  PixelBuffer after{};

  [[nodiscard]] std::size_t byte_size() const noexcept { return before.byte_size() + after.byte_size(); }
};

// The buffer a patch targets inside `document`, or nullptr when the layer,
// mask or channel is gone. The mutable form goes through the revision-bumping
// accessors, so callers must only use it when they are about to write.
[[nodiscard]] const PixelBuffer* stroke_patch_buffer(const Document& document, const StrokePatch& patch) noexcept;
[[nodiscard]] PixelBuffer* stroke_patch_buffer(Document& document, const StrokePatch& patch) noexcept;

// Builds the patch for a stroke that turned `before` (the pre-stroke snapshot)
// into `after` (the live document), given the document-space rectangle the
// stroke reported dirty. Returns nullopt, and the caller keeps the full
// snapshot, when the two documents differ anywhere outside that rectangle of
// the target buffer (another buffer detached, the target buffer changed size
// or position, the layer or channel is missing), or when nothing was written.
[[nodiscard]] std::optional<StrokePatch> capture_stroke_patch(const Document& before, const Document& after,
                                                              StrokePatch::Target target, LayerId layer_id,
                                                              ChannelId channel_id, Rect document_dirty);

// Writes the patch into `live`: `restore_before` puts the pre-stroke pixels
// back (capturing the current pixels into `after` first when it is empty);
// otherwise the post-stroke pixels return. Goes through the mutable accessors,
// so the layer/channel revisions advance. Returns false when the target is
// missing or no longer matches the patch geometry (nothing is written).
bool apply_stroke_patch(Document& live, StrokePatch& patch, bool restore_before);

// Rect helpers shared with the tests: a copy of `rect` out of `source` (rect
// must lie inside the buffer) and the inverse write.
[[nodiscard]] PixelBuffer copy_pixel_rect(const PixelBuffer& source, Rect rect);
void write_pixel_rect(PixelBuffer& target, Rect rect, const PixelBuffer& source);

}  // namespace patchy
