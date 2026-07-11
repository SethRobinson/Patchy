#pragma once

#include "core/layer.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace patchy {

using ChannelId = std::uint64_t;

inline constexpr std::size_t kMaximumPhotoshopChannelCount = 56U;

enum class DocumentChannelKind {
  Alpha,
  Spot
};

// Photoshop's channel-options "Color Indicates" setting. The channel kind is
// kept separately because an imported spot channel remains a spot channel even
// when an unusual display-info record needs to be preserved verbatim.
enum class DocumentChannelColorIndicates {
  MaskedAreas,
  SelectedAreas,
  SpotColor
};

// Editor-friendly channel presentation state. opacity is normalized to [0, 1];
// the source PSD record is also retained on DocumentChannel for exact round trips
// when it contains a color space or fields Patchy does not understand.
struct DocumentChannelDisplayInfo {
  RgbColor color{255, 0, 0};
  float opacity{0.5F};
  DocumentChannelColorIndicates color_indicates{DocumentChannelColorIndicates::MaskedAreas};
};

class DocumentChannel {
public:
  DocumentChannel() = default;
  DocumentChannel(ChannelId id, std::string name, DocumentChannelKind kind, PixelBuffer pixels);

  [[nodiscard]] ChannelId id() const noexcept;
  [[nodiscard]] const std::string& name() const noexcept;
  [[nodiscard]] DocumentChannelKind kind() const noexcept;
  [[nodiscard]] PixelBuffer& pixels() noexcept;
  [[nodiscard]] const PixelBuffer& pixels() const noexcept;
  [[nodiscard]] const std::optional<std::uint32_t>& photoshop_identifier() const noexcept;
  [[nodiscard]] const DocumentChannelDisplayInfo& display_info() const noexcept;
  [[nodiscard]] const std::vector<std::uint8_t>& raw_photoshop_display_info() const noexcept;
  [[nodiscard]] std::uint64_t content_revision() const noexcept;

  void set_name(std::string name);
  void set_pixels(PixelBuffer pixels);
  void set_photoshop_identifier(std::optional<std::uint32_t> identifier) noexcept;
  void set_display_info(DocumentChannelDisplayInfo display_info);
  void set_raw_photoshop_display_info(std::vector<std::uint8_t> record) noexcept;

private:
  void mark_changed() noexcept;

  ChannelId id_{0};
  std::string name_;
  DocumentChannelKind kind_{DocumentChannelKind::Alpha};
  PixelBuffer pixels_;
  std::optional<std::uint32_t> photoshop_identifier_;
  DocumentChannelDisplayInfo display_info_;
  std::vector<std::uint8_t> raw_photoshop_display_info_;
  std::uint64_t content_revision_{0};
};

}  // namespace patchy
