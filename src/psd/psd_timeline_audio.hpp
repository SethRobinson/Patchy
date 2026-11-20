#pragma once

#include <compare>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace patchy::psd {

// File references in Photoshop's timeline audio clips (image resource 1075).
// Reading them for a missing-media notice never modifies the resource.
struct TimelineAudioLink {
  std::string name;
  std::string relative_path;
  std::string full_path;
  std::string original_path;
  auto operator<=>(const TimelineAudioLink&) const = default;
};

[[nodiscard]] std::vector<TimelineAudioLink> timeline_audio_links(std::span<const std::uint8_t> image_resources);

}  // namespace patchy::psd
