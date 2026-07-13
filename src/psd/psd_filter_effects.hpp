#pragma once

#include "core/smart_filter_effects.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace patchy::psd {

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

} // namespace patchy::psd
