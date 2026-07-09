#include "formats/format_registry.hpp"

#include "formats/aseprite_document_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/ilbm_document_io.hpp"
#include "formats/pcx_document_io.hpp"
#include "formats/tga_document_io.hpp"
#include "psd/psd_document_io.hpp"
#include "support/string_utils.hpp"

#include <stdexcept>
#include <utility>

namespace patchy {

void FormatRegistry::register_handler(FormatHandler handler) {
  if (handler.identifier.empty()) {
    throw std::invalid_argument("Format identifier cannot be empty");
  }
  if (!handler.read) {
    throw std::invalid_argument("Format handler must provide a read function");
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
  registry.register_handler({"patchy.formats.psd",
                             "Photoshop Document",
                             {".psd", ".psb"},
                             [](std::span<const std::uint8_t> bytes) {
                               FormatReadResult result;
                               psd::ReadOptions options;
                               options.notices = &result.notices;
                               result.document = psd::DocumentIo::read(bytes, options);
                               return result;
                             },
                             [](const Document& document) { return psd::DocumentIo::write_layered_rgb8(document); },
                             nullptr});
  registry.register_handler({"patchy.formats.bmp",
                             "Bitmap Image",
                             {".bmp"},
                             [](std::span<const std::uint8_t> bytes) {
                               return FormatReadResult{bmp::DocumentIo::read(bytes), {}};
                             },
                             [](const Document& document) { return bmp::DocumentIo::write(document); },
                             nullptr});
  registry.register_handler({"patchy.formats.ico",
                             "Windows Icon",
                             {".ico", ".cur"},
                             [](std::span<const std::uint8_t> bytes) {
                               FormatReadResult result;
                               result.document = ico::DocumentIo::read(bytes, &result.notices);
                               return result;
                             },
                             [](const Document& document) { return ico::DocumentIo::write(document); },
                             [](std::span<const std::uint8_t> bytes) { return ico::DocumentIo::can_read(bytes); }});
  registry.register_handler({"patchy.formats.tga",
                             "Targa Image",
                             {".tga"},
                             [](std::span<const std::uint8_t> bytes) {
                               FormatReadResult result;
                               result.document = tga::DocumentIo::read(bytes, &result.notices);
                               return result;
                             },
                             [](const Document& document) { return tga::DocumentIo::write(document); },
                             [](std::span<const std::uint8_t> bytes) { return tga::DocumentIo::can_read(bytes); }});
  registry.register_handler({"patchy.formats.aseprite",
                             "Aseprite Image",
                             {".aseprite", ".ase"},
                             [](std::span<const std::uint8_t> bytes) {
                               FormatReadResult result;
                               result.document = aseprite::DocumentIo::read(bytes, &result.notices);
                               return result;
                             },
                             [](const Document& document) { return aseprite::DocumentIo::write(document); },
                             [](std::span<const std::uint8_t> bytes) { return aseprite::sniff(bytes); }});
  registry.register_handler({"patchy.formats.pcx",
                             "PCX Image",
                             {".pcx"},
                             [](std::span<const std::uint8_t> bytes) {
                               FormatReadResult result;
                               result.document = pcx::DocumentIo::read(bytes, &result.notices);
                               return result;
                             },
                             [](const Document& document) { return pcx::DocumentIo::write(document); },
                             [](std::span<const std::uint8_t> bytes) { return pcx::DocumentIo::can_read(bytes); }});
  registry.register_handler({"patchy.formats.ilbm",
                             "Amiga IFF Image",
                             {".lbm", ".iff", ".bbm"},
                             [](std::span<const std::uint8_t> bytes) {
                               FormatReadResult result;
                               result.document = ilbm::DocumentIo::read(bytes, &result.notices);
                               return result;
                             },
                             [](const Document& document) { return ilbm::DocumentIo::write(document); },
                             [](std::span<const std::uint8_t> bytes) { return ilbm::DocumentIo::can_read(bytes); }});
}

const FormatRegistry& builtin_format_registry() {
  static const FormatRegistry registry = [] {
    FormatRegistry built;
    register_builtin_formats(built);
    return built;
  }();
  return registry;
}

}  // namespace patchy
