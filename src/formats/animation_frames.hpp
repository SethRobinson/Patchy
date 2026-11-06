#pragma once

#include "core/layer.hpp"

#include <string_view>
#include <vector>

namespace patchy::animation {

// Which layers an animation's frames are and what each frame draws
// (docs/animation.md#frames-and-layers-named-all). Every animated writer, Export for Web's frame list and the
// Animation Preview window go through these, so the rules live in one place.
//
// A sibling list is read in clipping units: a layer that is not clipped plus the clipped
// layers directly above it. A unit is a frame, shared, or a frame container, decided by its
// base layer's name:
//  - a group named "all" (case-insensitive, trimmed, a trailing time token ignored) is a frame
//    container: its own units become the frames, drawn inside the group, so the group's
//    opacity, blend mode, effects, mask and clipped layers apply to each of them;
//  - any other layer named "all" is shared: drawn in every frame at its own stacking position;
//  - every other unit is one frame (a normal group is one frame, as before).
// The units considered are the top-level ones and those inside "all" groups. Frames run top
// first; a hidden unit is neither a frame nor drawn, unless `include_hidden` asks for every
// frame (the preview's "Play hidden layers"), which still never draws a hidden shared layer.

[[nodiscard]] bool is_shared_layer_name(std::string_view name);

struct AnimationFrameRef {
  // The frame's base layer: its name gives the frame's delay.
  const Layer* layer{nullptr};
  // The "all" groups that hold it, outermost first (empty for a top-level frame).
  std::vector<const Layer*> containers;
};

// The frames, top first.
[[nodiscard]] std::vector<AnimationFrameRef> animation_frames(const std::vector<Layer>& top_level, bool include_hidden);

// The top-level layer stack (bottom first, copies) that draws `frame`: the frame's unit with
// its base made visible, its containers holding only what this frame draws, and every visible
// shared unit. `frame` must come from animation_frames on the same layers.
[[nodiscard]] std::vector<Layer> animation_frame_stack(const std::vector<Layer>& top_level,
                                                       const AnimationFrameRef& frame);

}  // namespace patchy::animation
