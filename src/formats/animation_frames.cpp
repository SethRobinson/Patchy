#include "formats/animation_frames.hpp"

#include "formats/animation_timing.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace patchy::animation {
namespace {

// A clipping unit inside one sibling list: [begin, end), siblings[begin] the base. A clipped
// layer joins the run below it; a clipped group starts its own unit, as effective_clip_base
// reads it.
struct Unit {
  std::size_t begin{0};
  std::size_t end{0};
};

[[nodiscard]] std::vector<Unit> clip_units(const std::vector<Layer>& siblings) {
  std::vector<Unit> units;
  for (std::size_t index = 0; index < siblings.size(); ++index) {
    const auto& layer = siblings[index];
    if (!units.empty() && layer.clipped() && layer.kind() != LayerKind::Group) {
      units.back().end = index + 1;
    } else {
      units.push_back(Unit{index, index + 1});
    }
  }
  return units;
}

[[nodiscard]] bool is_frame_container(const Layer& layer) {
  return layer.kind() == LayerKind::Group && is_shared_layer_name(layer.name());
}

void collect_frames(const std::vector<Layer>& siblings, bool include_hidden, std::vector<const Layer*>& containers,
                    std::vector<AnimationFrameRef>& frames) {
  const auto units = clip_units(siblings);
  for (auto it = units.rbegin(); it != units.rend(); ++it) {
    const Layer& base = siblings[it->begin];
    if (!include_hidden && !base.visible()) {
      continue;
    }
    if (is_frame_container(base)) {
      containers.push_back(&base);
      collect_frames(base.children(), include_hidden, containers, frames);
      containers.pop_back();
    } else if (!is_shared_layer_name(base.name())) {
      frames.push_back(AnimationFrameRef{&base, containers});
    }
  }
}

// The layers of `siblings` that draw `frame`, bottom first. `depth` is how many of the
// frame's containers enclose this list; `on_path` is whether the list lies on the frame's
// path (otherwise only shared layers are drawn from it).
[[nodiscard]] std::vector<Layer> frame_layers(const std::vector<Layer>& siblings, const AnimationFrameRef& frame,
                                              std::size_t depth, bool on_path) {
  std::vector<Layer> stack;
  const auto append_unit = [&](const Unit& unit, Layer base) {
    stack.push_back(std::move(base));
    for (auto index = unit.begin + 1; index < unit.end; ++index) {
      stack.push_back(siblings[index]);
    }
  };
  for (const auto& unit : clip_units(siblings)) {
    const Layer& base = siblings[unit.begin];
    if (on_path && depth == frame.containers.size() && &base == frame.layer) {
      Layer copy = base;
      copy.set_visible(true);
      append_unit(unit, std::move(copy));
    } else if (on_path && depth < frame.containers.size() && &base == frame.containers[depth]) {
      Layer copy = base;
      copy.set_visible(true);
      copy.children() = frame_layers(base.children(), frame, depth + 1, true);
      append_unit(unit, std::move(copy));
    } else if (base.visible() && is_frame_container(base)) {
      auto children = frame_layers(base.children(), frame, depth + 1, false);
      if (!children.empty()) {
        Layer copy = base;
        copy.children() = std::move(children);
        append_unit(unit, std::move(copy));
      }
    } else if (base.visible() && is_shared_layer_name(base.name())) {
      append_unit(unit, base);
    }
  }
  return stack;
}

}  // namespace

bool is_shared_layer_name(std::string_view name) {
  const auto is_space = [](char c) { return c == ' ' || c == '\t'; };
  while (!name.empty() && is_space(name.back())) {
    name.remove_suffix(1);
  }
  if (parse_layer_name_delay_ms(name).has_value()) {
    const auto separator = name.find_last_of(" \t");
    name = separator == std::string_view::npos ? std::string_view{} : name.substr(0, separator);
  }
  while (!name.empty() && is_space(name.back())) {
    name.remove_suffix(1);
  }
  while (!name.empty() && is_space(name.front())) {
    name.remove_prefix(1);
  }
  constexpr std::string_view kAll = "all";
  return name.size() == kAll.size() &&
         std::equal(name.begin(), name.end(), kAll.begin(), [](char a, char b) {
           return (a >= 'A' && a <= 'Z' ? static_cast<char>(a - 'A' + 'a') : a) == b;
         });
}

std::vector<AnimationFrameRef> animation_frames(const std::vector<Layer>& top_level, bool include_hidden) {
  std::vector<AnimationFrameRef> frames;
  std::vector<const Layer*> containers;
  collect_frames(top_level, include_hidden, containers, frames);
  return frames;
}

std::vector<Layer> animation_frame_stack(const std::vector<Layer>& top_level, const AnimationFrameRef& frame) {
  return frame_layers(top_level, frame, 0, true);
}

}  // namespace patchy::animation
