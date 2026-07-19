#pragma once

#include "core/document.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace patchy::af {

// Affinity by Canva's native container (.af since the 2025 unified app; the same
// magic covers the older .afphoto/.afdesign/.afpub generations, which Patchy does
// not register). Proprietary and undocumented; everything here was derived from
// files authored with a licensed Affinity install plus the MIT-licensed afread
// project's notes on the 2020-era format - never from disassembling Affinity.
// Container layout, verification corpus, and the legal record live in
// docs/file-formats.md (".af (Affinity)").
[[nodiscard]] bool sniff(std::span<const std::uint8_t> bytes) noexcept;

class DocumentIo {
public:
  [[nodiscard]] static bool can_read(std::span<const std::uint8_t> bytes) noexcept;
  // Tier-0 importer: walks the container (header, stream table, zstd/zlib
  // streams, CRCs) and imports the embedded full-document preview PNG (at most
  // ~512 px) as a single pixel layer, with notices stating that only the
  // preview was imported. Real layer decoding builds on the same walk later.
  [[nodiscard]] static Document read(std::span<const std::uint8_t> bytes,
                                     std::vector<std::string>* notices = nullptr);
  [[nodiscard]] static Document read_file(const std::filesystem::path& path,
                                          std::vector<std::string>* notices = nullptr);
};

}  // namespace patchy::af
