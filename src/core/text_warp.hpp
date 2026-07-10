#pragma once

#include "core/layer.hpp"
#include "core/warp_mesh.hpp"

#include <optional>
#include <string>
#include <string_view>

// Photoshop's Warp Text (the TySh warp descriptor): style + bend + the two
// distortion percentages + orientation, evaluated over the text's layout 'bounds'
// box in TEXT-LOCAL space (the wt_* COM captures pin the box choice: warping acts on
// the layout bounds, never the ink box, for point AND box text). Unlike smart-object
// warps there is no mesh in the file - the style is parametric and the mesh is
// regenerated from these values on every render.
namespace patchy {

struct TextWarp {
  std::string style{"warpNone"};  // warpStyle enum token, e.g. "warpArc"
  std::string rotate{"Hrzn"};     // warpRotate: "Hrzn" | "Vrtc"
  double value{0.0};              // bend percent, -100..100
  double perspective{0.0};        // horizontal distortion percent (warpPerspective)
  double perspective_other{0.0};  // vertical distortion percent (warpPerspectiveOther)
  // Warp reference box in text-local coordinates (the TySh text 'bounds' rect,
  // relative to the text transform's origin).
  double bounds_left{0.0};
  double bounds_top{0.0};
  double bounds_right{0.0};
  double bounds_bottom{0.0};
};

// Metadata key holding the serialized warp on a text layer. Absent = no warp.
inline constexpr const char* kLayerMetadataTextWarp = "patchy.text.warp";

// True when the warp bends nothing (style None, or every parameter zero): the text
// renders through the historical unwarped path bit for bit.
[[nodiscard]] bool text_warp_is_identity(const TextWarp& warp);

[[nodiscard]] std::string serialize_text_warp(const TextWarp& warp);
[[nodiscard]] std::optional<TextWarp> parse_text_warp(std::string_view text);
[[nodiscard]] std::optional<TextWarp> text_warp_from_layer(const Layer& layer);

// The warp surface for rendering: the style construction over the warp box's
// extent, distortion applied, control points translated into text-local space
// (bounds origin). Nullopt for identity warps, unknown styles, or a degenerate box.
[[nodiscard]] std::optional<WarpMeshGrid> generate_text_warp_mesh(const TextWarp& warp);

}  // namespace patchy
