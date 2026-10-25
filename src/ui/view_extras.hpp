#pragma once

#include <cstdint>
#include <optional>

class QWidget;

namespace patchy::ui {

// View > Extras (Ctrl+H, GitHub issue 420; docs/view-navigation.md, "Extras"): the
// canvas overlays the master toggle can hide. A covered overlay draws only while its own
// View toggle is on and Extras is on; an uncovered one ignores Extras. Bit values are
// internal; the settings keys below are the persisted form.
enum class ViewExtra : std::uint32_t {
  SelectionEdges = 1u << 0,
  TargetPath = 1u << 1,
  LayerOutlines = 1u << 2,
  Guides = 1u << 3,
  SmartGuides = 1u << 4,
  Grid = 1u << 5,
  PixelGrid = 1u << 6,
  ArtboardNames = 1u << 7,
};

inline constexpr std::uint32_t kAllViewExtras = 0xffu;

[[nodiscard]] constexpr std::uint32_t view_extra_bit(ViewExtra extra) noexcept {
  return static_cast<std::uint32_t>(extra);
}

// Which overlays Extras covers, from the view/extras/<item> settings (every item defaults
// to covered: Ctrl+H hides everything until the user unticks something).
[[nodiscard]] std::uint32_t load_view_extras_covered();
void save_view_extras_covered(std::uint32_t covered);

// View > Show > Show Extras Options: one checkbox per overlay. Returns the new mask on
// OK, nothing on Cancel. Does not save; the caller stores and applies the result.
[[nodiscard]] std::optional<std::uint32_t> request_view_extras_options(QWidget* parent, std::uint32_t covered);

}  // namespace patchy::ui
