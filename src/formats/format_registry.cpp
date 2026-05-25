#include "formats/format_registry.hpp"

#include "psd/psd_document_io.hpp"
#include "support/string_utils.hpp"

#include <stdexcept>
#include <utility>

namespace photoslop {

void FormatRegistry::register_handler(FormatHandler handler) {
  if (handler.identifier.empty()) {
    throw std::invalid_argument("Format identifier cannot be empty");
  }
  if (!handler.read || !handler.write) {
    throw std::invalid_argument("Format handler must provide read and write functions");
  }
  handlers_.push_back(std::move(handler));
}

const FormatHandler* FormatRegistry::find_by_extension(std::string_view extension) const noexcept {
  const auto normalized = normalized_extension(extension);
  for (const auto& handler : handlers_) {
    for (const auto& supported : handler.extensions) {
      if (normalized_extension(supported) == normalized) {
        return &handler;
      }
    }
  }
  return nullptr;
}

const std::vector<FormatHandler>& FormatRegistry::handlers() const noexcept {
  return handlers_;
}

void register_builtin_formats(FormatRegistry& registry) {
  registry.register_handler({"photoslop.formats.psd",
                             "Photoshop Document",
                             {".psd", ".psb"},
                             [](std::span<const std::uint8_t> bytes) { return psd::DocumentIo::read(bytes); },
                             [](const Document& document) { return psd::DocumentIo::write_layered_rgb8(document); }});
}

}  // namespace photoslop
