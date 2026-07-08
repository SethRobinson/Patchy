#pragma once

#include "core/document.hpp"

#include <functional>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace patchy {

// Notices are user-facing messages about features a reader dropped or approximated (for
// example "imported the first frame only"). They surface in an Import Notes dialog after the
// document opens; like reader error strings they are plain English (the formats library is
// Qt-free, so they cannot go through tr()).
struct FormatReadResult {
  Document document;
  std::vector<std::string> notices;
};

using FormatReadFn = std::function<FormatReadResult(std::span<const std::uint8_t>)>;
using FormatWriteFn = std::function<std::vector<std::uint8_t>(const Document&)>;
using FormatSniffFn = std::function<bool(std::span<const std::uint8_t>)>;

struct FormatHandler {
  std::string identifier;
  std::string display_name;
  std::vector<std::string> extensions;
  FormatReadFn read;
  // Null for read-only formats (the UI hides them from save/export).
  FormatWriteFn write;
  // Optional content check used when an extension is ambiguous (".ase" is both an Aseprite
  // image and an Adobe swatch palette). Null means "extension match is enough".
  FormatSniffFn sniff;

  [[nodiscard]] bool can_write() const noexcept {
    return static_cast<bool>(write);
  }
};

class FormatRegistry {
public:
  void register_handler(FormatHandler handler);
  [[nodiscard]] const FormatHandler* find_by_extension(std::string_view extension) const noexcept;
  [[nodiscard]] const std::vector<FormatHandler>& handlers() const noexcept;

private:
  std::vector<FormatHandler> handlers_;
};

void register_builtin_formats(FormatRegistry& registry);

// The single shared registry instance consulted by the document open/save paths.
[[nodiscard]] const FormatRegistry& builtin_format_registry();

}  // namespace patchy
