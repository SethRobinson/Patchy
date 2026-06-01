#pragma once

#include "core/layer.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace patchy {

struct DocumentColorState {
  ColorMode working_mode{ColorMode::RGB};
  BitDepth bit_depth{BitDepth::UInt8};
  std::vector<std::uint8_t> embedded_icc_profile;
  std::string ocio_view;
};

struct DocumentMetadata {
  std::map<std::string, std::string> values;
  std::vector<UnknownPsdBlock> unknown_psd_resources;
  std::vector<std::uint8_t> raw_psd_image_resources;
};

struct DocumentPrintSettings {
  double horizontal_ppi{300.0};
  double vertical_ppi{300.0};
};

struct DocumentIndexedPalette {
  std::vector<RgbColor> colors;
  std::uint16_t source_bit_depth{0};
};

enum class GuideOrientation {
  Vertical,
  Horizontal
};

struct DocumentGuide {
  GuideOrientation orientation{GuideOrientation::Vertical};
  std::int32_t position_32{0};
};

struct DocumentGridSettings {
  std::int32_t horizontal_cycle_32{576};
  std::int32_t vertical_cycle_32{576};
};

class Document {
public:
  Document() = default;
  Document(std::int32_t width, std::int32_t height, PixelFormat format);

  [[nodiscard]] std::int32_t width() const noexcept;
  [[nodiscard]] std::int32_t height() const noexcept;
  [[nodiscard]] PixelFormat format() const noexcept;
  [[nodiscard]] const DocumentColorState& color_state() const noexcept;
  [[nodiscard]] DocumentColorState& color_state() noexcept;
  [[nodiscard]] const DocumentMetadata& metadata() const noexcept;
  [[nodiscard]] DocumentMetadata& metadata() noexcept;
  [[nodiscard]] const DocumentPrintSettings& print_settings() const noexcept;
  [[nodiscard]] DocumentPrintSettings& print_settings() noexcept;
  [[nodiscard]] const std::optional<DocumentIndexedPalette>& indexed_palette() const noexcept;
  [[nodiscard]] std::optional<DocumentIndexedPalette>& indexed_palette() noexcept;
  [[nodiscard]] const DocumentGridSettings& grid_settings() const noexcept;
  [[nodiscard]] DocumentGridSettings& grid_settings() noexcept;
  [[nodiscard]] const std::vector<DocumentGuide>& guides() const noexcept;
  [[nodiscard]] std::vector<DocumentGuide>& guides() noexcept;
  [[nodiscard]] const std::vector<Layer>& layers() const noexcept;
  [[nodiscard]] std::vector<Layer>& layers() noexcept;
  [[nodiscard]] std::optional<LayerId> active_layer_id() const noexcept;

  Layer& add_pixel_layer(std::string name, PixelBuffer pixels);
  Layer& add_layer(Layer layer);
  [[nodiscard]] Layer* find_layer(LayerId id) noexcept;
  [[nodiscard]] const Layer* find_layer(LayerId id) const noexcept;

  void set_active_layer(LayerId id);
  void clear_active_layer() noexcept;
  bool remove_layer(LayerId id);
  void resize_canvas(std::int32_t width, std::int32_t height);
  [[nodiscard]] LayerId allocate_layer_id() noexcept;

private:
  Layer* find_layer_recursive(std::vector<Layer>& layers, LayerId id) noexcept;
  const Layer* find_layer_recursive(const std::vector<Layer>& layers, LayerId id) const noexcept;
  bool remove_layer_recursive(std::vector<Layer>& layers, LayerId id) noexcept;
  [[nodiscard]] std::optional<LayerId> last_layer_id(const std::vector<Layer>& layers) const noexcept;

  std::int32_t width_{0};
  std::int32_t height_{0};
  PixelFormat format_{PixelFormat::rgb8()};
  DocumentColorState color_state_{};
  DocumentMetadata metadata_{};
  DocumentPrintSettings print_settings_{};
  std::optional<DocumentIndexedPalette> indexed_palette_{};
  DocumentGridSettings grid_settings_{};
  std::vector<DocumentGuide> guides_{};
  std::vector<Layer> layers_{};
  std::optional<LayerId> active_layer_id_{};
  LayerId next_layer_id_{1};
};

}  // namespace patchy
