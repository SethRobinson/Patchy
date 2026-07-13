#pragma once

#include "core/smart_filter_effects.hpp"
#include "core/smart_filter.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace patchy::psd {

// Defensive decoded-mask ceiling for editable imported/authored Smart Filters.
// Larger native records remain byte-preserved but preview-locked.
inline constexpr std::uint64_t kMaximumEditableSmartFilterMaskPixels =
    64ULL * 1024ULL * 1024ULL;

// Parses a document-global FEid/FXid payload. Broken outer version/record
// boundaries return an opaque block holding the exact shared payload. A record
// whose own bounded contents are unrecognized remains raw/rekeyable but reports
// data_supported=false.
[[nodiscard]] SmartFilterEffectsBlock parse_filter_effects_block(
    std::string key, std::shared_ptr<const std::vector<std::uint8_t>> payload,
    bool long_length = false, std::size_t original_global_index = SIZE_MAX);

// Convenience overload for tests/callers that do not already own shared bytes.
[[nodiscard]] SmartFilterEffectsBlock parse_filter_effects_block(
    std::string_view key, std::span<const std::uint8_t> payload,
    bool long_length = false, std::size_t original_global_index = SIZE_MAX);

// Exact original payload while untouched/opaque. A changed parsed block is
// rebuilt as outer-version + u64-length records; every record body remains raw
// except for a requested Pascal placed-id replacement.
[[nodiscard]] std::vector<std::uint8_t>
serialize_filter_effects_block(const SmartFilterEffectsBlock &block);

// Returns the shared raw record-body span, or an empty span for an invalid
// range.
[[nodiscard]] std::span<const std::uint8_t>
raw_filter_effects_record_body(const SmartFilterEffectsRecord &record) noexcept;

// Builds the Photoshop version-1 FEid record used by a freshly rendered Smart
// Filter instance. Cache planes contain the unfiltered placed/warped Smart
// Object on the full document rectangle; the shared filter mask is always
// written as an explicit 8-bit PackBits plane.
[[nodiscard]] std::optional<SmartFilterEffectsRecord>
author_filter_effects_record(std::string_view placed_uuid,
                             Rect document_bounds,
                             const PixelBuffer &unfiltered_pixels,
                             Rect unfiltered_bounds,
                             const SmartFilterMask &mask);

} // namespace patchy::psd
