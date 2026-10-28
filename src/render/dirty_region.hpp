#pragma once

#include "core/layer.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace patchy {

struct DirtyRegion {
  Rect bounds;
  std::int32_t mip{0};
};

class DirtyRegionSet {
public:
  void add(Rect bounds, std::int32_t mip = 0);
  void add(const DirtyRegion& region);

  [[nodiscard]] bool empty() const noexcept;
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] const std::vector<DirtyRegion>& regions() const noexcept;

  void clear() noexcept;

private:
  std::vector<DirtyRegion> regions_;
};

}  // namespace patchy
