#pragma once

#include <bit>
#include <cstdint>

namespace patchy {

// Which color channels a canvas shows (docs/channels.md, "Channel visibility"): the
// Channels panel's eyes. A view setting of one canvas only: it never changes pixels, is
// not undoable and is never saved. Color is Red, Green, Blue (indices 0..2); Ink is the
// CMYK proof's Cyan, Magenta, Yellow, Black plates (0..3), listed only while proofing.
enum class ChannelViewGroup : std::uint8_t { Color, Ink };

struct ChannelView {
  ChannelViewGroup group{ChannelViewGroup::Color};
  // One bit per channel of the group; never zero.
  std::uint8_t visible{0b111};

  friend constexpr bool operator==(const ChannelView&, const ChannelView&) = default;
};

// What the Channels panel targets: the composite, or one channel of a group.
struct ChannelViewTarget {
  bool composite{true};
  ChannelViewGroup group{ChannelViewGroup::Color};
  int index{0};

  friend constexpr bool operator==(const ChannelViewTarget&, const ChannelViewTarget&) = default;
};

struct ChannelViewChange {
  ChannelView view;
  ChannelViewTarget target;
};

[[nodiscard]] constexpr int channel_view_group_size(ChannelViewGroup group) noexcept {
  return group == ChannelViewGroup::Color ? 3 : 4;
}

[[nodiscard]] constexpr std::uint8_t channel_view_all_bits(ChannelViewGroup group) noexcept {
  return static_cast<std::uint8_t>((1U << static_cast<unsigned>(channel_view_group_size(group))) - 1U);
}

[[nodiscard]] constexpr ChannelView composite_channel_view() noexcept {
  return {};
}

[[nodiscard]] constexpr ChannelViewTarget composite_channel_target() noexcept {
  return {};
}

[[nodiscard]] constexpr ChannelViewTarget channel_view_target(ChannelViewGroup group, int index) noexcept {
  return {false, group, index};
}

[[nodiscard]] constexpr ChannelView single_channel_view(ChannelViewGroup group, int index) noexcept {
  return {group, static_cast<std::uint8_t>(1U << static_cast<unsigned>(index))};
}

// Every channel shows: the normal composite. All four plates are the proof composite.
[[nodiscard]] constexpr bool channel_view_is_composite(ChannelView view) noexcept {
  return view.visible == channel_view_all_bits(view.group);
}

[[nodiscard]] constexpr int channel_view_visible_count(ChannelView view) noexcept {
  return std::popcount(view.visible);
}

[[nodiscard]] constexpr bool channel_view_shows(ChannelView view, ChannelViewGroup group, int index) noexcept {
  return view.group == group && ((view.visible >> static_cast<unsigned>(index)) & 1U) != 0;
}

// The eye of a row: every eye is on while the composite shows; otherwise only the visible
// channels of the shown group.
[[nodiscard]] constexpr bool channel_view_eye_on(ChannelView view, ChannelViewGroup group, int index) noexcept {
  return channel_view_is_composite(view) || channel_view_shows(view, group, index);
}

[[nodiscard]] constexpr int channel_view_first_visible(ChannelView view) noexcept {
  return std::countr_zero(static_cast<unsigned>(view.visible));
}

// Clicking a channel row: that channel alone, targeted.
[[nodiscard]] constexpr ChannelViewChange select_single_channel(ChannelViewGroup group, int index) noexcept {
  return {single_channel_view(group, index), channel_view_target(group, index)};
}

// Clicking the composite row or its eye: every channel on, the composite targeted.
[[nodiscard]] constexpr ChannelViewChange select_composite_channel() noexcept {
  return {composite_channel_view(), composite_channel_target()};
}

// Clicking the eye of channel `index` of `group`. The last visible channel stays on;
// turning every channel on returns to the composite; one channel left becomes the target.
// With two or more of the group showing, the target stays when it is still shown (a
// composite target too, for Color: painting continues with a channel hidden from view),
// otherwise it moves to the first visible channel.
[[nodiscard]] constexpr ChannelViewChange toggle_channel_eye(ChannelView view, ChannelViewTarget target,
                                                             ChannelViewGroup group, int index) noexcept {
  if (index < 0 || index >= channel_view_group_size(group)) {
    return {view, target};
  }
  const auto bit = static_cast<std::uint8_t>(1U << static_cast<unsigned>(index));
  if (channel_view_is_composite(view)) {
    // Every eye shows on: start from all of this group.
    view = {group, channel_view_all_bits(group)};
  } else if (view.group != group) {
    // This group's eyes all show off: the click turns this one on alone.
    return select_single_channel(group, index);
  }
  if ((view.visible & bit) != 0) {
    if (view.visible == bit) {
      return {view, target};
    }
    view.visible = static_cast<std::uint8_t>(view.visible & ~bit);
  } else {
    view.visible = static_cast<std::uint8_t>(view.visible | bit);
  }
  if (channel_view_is_composite(view)) {
    return select_composite_channel();
  }
  if (channel_view_visible_count(view) == 1) {
    return {view, channel_view_target(group, channel_view_first_visible(view))};
  }
  const bool keep_channel_target = !target.composite && channel_view_shows(view, target.group, target.index);
  const bool keep_composite_target = target.composite && group == ChannelViewGroup::Color;
  if (keep_channel_target || keep_composite_target) {
    return {view, target};
  }
  return {view, channel_view_target(group, channel_view_first_visible(view))};
}

// The view to keep when the target changes some other way (a layer or mask thumbnail, a
// saved channel, history): a composite target keeps a Color view of two channels and
// resets anything else; a channel target keeps a view that shows it, otherwise shows it alone.
[[nodiscard]] constexpr ChannelView channel_view_for_target(ChannelView view, ChannelViewTarget target) noexcept {
  if (target.composite) {
    return view.group == ChannelViewGroup::Color && channel_view_visible_count(view) >= 2 ? view
                                                                                         : composite_channel_view();
  }
  return channel_view_shows(view, target.group, target.index) ? view
                                                              : single_channel_view(target.group, target.index);
}

}  // namespace patchy
