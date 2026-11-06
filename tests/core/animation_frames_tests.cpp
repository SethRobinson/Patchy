// Frames and layers named "all" (docs/animation.md#frames-and-layers-named-all): the frame
// list and per-frame layer stacks every animated writer and the Animation Preview use.
#include "core/layer.hpp"
#include "formats/animation_frames.hpp"
#include "test_harness.hpp"

#include <string>
#include <utility>
#include <vector>

namespace {

using patchy::Layer;
using patchy::LayerId;
using patchy::LayerKind;
using patchy::animation::AnimationFrameRef;
using patchy::animation::animation_frame_stack;
using patchy::animation::animation_frames;
using patchy::animation::is_shared_layer_name;

LayerId g_next_id = 1;

Layer pixel(const std::string& name, bool visible = true, bool clipped = false) {
  Layer layer(g_next_id++, name, patchy::PixelBuffer(2, 2, patchy::PixelFormat::rgba8()));
  layer.set_visible(visible);
  layer.set_clipped(clipped);
  return layer;
}

// Children bottom first.
Layer group(const std::string& name, std::vector<Layer> children, bool visible = true) {
  Layer layer(g_next_id++, name, LayerKind::Group);
  layer.children() = std::move(children);
  layer.set_visible(visible);
  return layer;
}

std::vector<std::string> frame_names(const std::vector<AnimationFrameRef>& frames) {
  std::vector<std::string> names;
  for (const auto& frame : frames) {
    names.push_back(frame.layer->name());
  }
  return names;
}

// Names of a stack, a group written as name{children}.
std::string describe(const std::vector<Layer>& stack) {
  std::string text;
  for (const auto& layer : stack) {
    if (!text.empty()) text += ",";
    text += layer.name();
    if (layer.kind() == LayerKind::Group) {
      text += "{" + describe(layer.children()) + "}";
    }
  }
  return text;
}

void animation_frames_match_all_names() {
  for (const auto* name : {"all", "ALL", "All", " all ", "all 0.5s", "all\t2s", "All .25s"}) {
    CHECK(is_shared_layer_name(name));
  }
  for (const auto* name : {"", "allow", "all0.5s", "Frame all", "a ll", "0.5s", "al"}) {
    CHECK(!is_shared_layer_name(name));
  }
}

void animation_frames_draw_shared_layers_in_every_frame() {
  // Bottom to top: a shared background, three frames, a shared overlay.
  const std::vector<Layer> layers{pixel("all"), pixel("one"), pixel("two 0.2s"), pixel("three"), pixel("All 1s")};
  const auto frames = animation_frames(layers, false);
  CHECK(frame_names(frames) == (std::vector<std::string>{"three", "two 0.2s", "one"}));
  CHECK(describe(animation_frame_stack(layers, frames[0])) == "all,three,All 1s");
  CHECK(describe(animation_frame_stack(layers, frames[1])) == "all,two 0.2s,All 1s");
  CHECK(describe(animation_frame_stack(layers, frames[2])) == "all,one,All 1s");
  // Without "all" names nothing changes: one frame per visible top-level layer.
  const std::vector<Layer> plain{pixel("a"), pixel("b", false), pixel("c")};
  const auto plain_frames = animation_frames(plain, false);
  CHECK(frame_names(plain_frames) == (std::vector<std::string>{"c", "a"}));
  CHECK(describe(animation_frame_stack(plain, plain_frames[1])) == "a");
}

void animation_frames_expand_all_groups_into_their_children() {
  // An "all" group holds two frames and a normal group (one frame); the group's opacity
  // travels with each frame's copy of it.
  auto container = group("all", {pixel("c1"), pixel("c2 0.3s"), group("pair", {pixel("x"), pixel("y")})});
  container.set_opacity(0.5F);
  const std::vector<Layer> layers{pixel("all"), std::move(container), pixel("solo")};
  const auto frames = animation_frames(layers, false);
  CHECK(frame_names(frames) == (std::vector<std::string>{"solo", "pair", "c2 0.3s", "c1"}));
  CHECK(frames[1].containers.size() == 1U);
  CHECK(frames[0].containers.empty());
  CHECK(describe(animation_frame_stack(layers, frames[0])) == "all,solo");
  CHECK(describe(animation_frame_stack(layers, frames[1])) == "all,all{pair{x,y}}");
  CHECK(describe(animation_frame_stack(layers, frames[2])) == "all,all{c2 0.3s}");
  const auto stack = animation_frame_stack(layers, frames[3]);
  CHECK(describe(stack) == "all,all{c1}");
  CHECK(stack[1].opacity() == 0.5F);
  CHECK(stack[1].children()[0].visible());
}

void animation_frames_share_all_children_and_nested_all_groups() {
  // Inside an "all" group a child named "all" is drawn in every frame of the animation (inside
  // the group), and an "all" group inside it expands again. A group holding only "all"
  // children has no frames: a shared group.
  const std::vector<Layer> layers{
      group("all", {pixel("all"), pixel("All"), pixel("all 1s")}),
      pixel("f0"),
      group("all 0.5s", {pixel("all"), pixel("c1"), group("ALL", {pixel("n1"), pixel("n2")})}),
  };
  const auto frames = animation_frames(layers, false);
  CHECK(frame_names(frames) == (std::vector<std::string>{"n2", "n1", "c1", "f0"}));
  CHECK(frames[0].containers.size() == 2U);
  CHECK(describe(animation_frame_stack(layers, frames[0])) == "all{all,All,all 1s},all 0.5s{all,ALL{n2}}");
  CHECK(describe(animation_frame_stack(layers, frames[2])) == "all{all,All,all 1s},all 0.5s{all,c1}");
  CHECK(describe(animation_frame_stack(layers, frames[3])) == "all{all,All,all 1s},f0,all 0.5s{all}");
}

void animation_frames_keep_clipped_layers_with_their_base() {
  // A layer clipped to a frame is drawn with it and is never a frame of its own; one clipped
  // to an "all" group is drawn with the group in each of its frames; one clipped to a shared
  // layer is shared with it.
  const std::vector<Layer> layers{
      pixel("all"), pixel("tint", true, true),
      pixel("f1"), pixel("shade", true, true), pixel("glow", false, true),
      group("all", {pixel("c1"), pixel("c1 clip", true, true), pixel("c2")}), pixel("group clip", true, true),
  };
  const auto frames = animation_frames(layers, false);
  CHECK(frame_names(frames) == (std::vector<std::string>{"c2", "c1", "f1"}));
  CHECK(describe(animation_frame_stack(layers, frames[0])) == "all,tint,all{c2},group clip");
  CHECK(describe(animation_frame_stack(layers, frames[1])) == "all,tint,all{c1,c1 clip},group clip");
  const auto f1 = animation_frame_stack(layers, frames[2]);
  CHECK(describe(f1) == "all,tint,f1,shade,glow");
  CHECK(!f1[4].visible());  // a hidden clipped layer stays hidden
}

void animation_frames_follow_visibility() {
  const std::vector<Layer> layers{
      pixel("all", false), pixel("all"), pixel("f1"), pixel("f2", false),
      group("all", {pixel("c1"), pixel("c2", false)}, false),
  };
  // Visible only: no hidden frame, no hidden shared layer, a hidden "all" group adds nothing.
  const auto frames = animation_frames(layers, false);
  CHECK(frame_names(frames) == (std::vector<std::string>{"f1"}));
  CHECK(describe(animation_frame_stack(layers, frames[0])) == "all,f1");
  // Every frame (the preview's Play hidden layers): hidden frames and the children of a hidden
  // "all" group play, drawn in a visible copy of their group, but hidden shared layers stay out.
  const auto every = animation_frames(layers, true);
  CHECK(frame_names(every) == (std::vector<std::string>{"c2", "c1", "f2", "f1"}));
  const auto c2 = animation_frame_stack(layers, every[0]);
  CHECK(describe(c2) == "all,all{c2}");
  CHECK(c2[1].visible());
  CHECK(c2[1].children()[0].visible());
  CHECK(describe(animation_frame_stack(layers, every[2])) == "all,f2");
  // Only shared layers: no frames.
  CHECK(animation_frames(std::vector<Layer>{pixel("all"), group("all", {pixel("all")})}, false).empty());
}

}  // namespace

std::vector<patchy::test::TestCase> animation_frames_tests() {
  return {
      {"animation_frames_match_all_names", animation_frames_match_all_names},
      {"animation_frames_draw_shared_layers_in_every_frame", animation_frames_draw_shared_layers_in_every_frame},
      {"animation_frames_expand_all_groups_into_their_children", animation_frames_expand_all_groups_into_their_children},
      {"animation_frames_share_all_children_and_nested_all_groups",
       animation_frames_share_all_children_and_nested_all_groups},
      {"animation_frames_keep_clipped_layers_with_their_base", animation_frames_keep_clipped_layers_with_their_base},
      {"animation_frames_follow_visibility", animation_frames_follow_visibility},
  };
}
