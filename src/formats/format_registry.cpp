#include "formats/format_registry.hpp"

#include "formats/af_document_io.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/avif_document_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/exr_document_io.hpp"
#include "formats/hdr_document_io.hpp"
#include "formats/heif_document_io.hpp"
#include "formats/jp2_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/ilbm_document_io.hpp"
#include "formats/jxl_document_io.hpp"
#include "formats/jxr_document_io.hpp"
#include "formats/pcx_document_io.hpp"
#include "formats/pdn_document_io.hpp"
#include "formats/raw_document_io.hpp"
#include "formats/dds_document_io.hpp"
#include "formats/rttex_document_io.hpp"
#include "formats/svg_document_io.hpp"
#include "formats/tga_document_io.hpp"
#include "psd/psd_document_io.hpp"
#include "support/string_utils.hpp"
#include "support/translate_noop.hpp"

#include <stdexcept>
#include <utility>

namespace patchy {

void FormatRegistry::register_handler(FormatHandler handler) {
  if (handler.identifier.empty()) {
    throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "Format identifier cannot be empty"));
  }
  if (!handler.read) {
    throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "Format handler must provide a read function"));
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
  registry.register_handler({"patchy.formats.svg",
                             "SVG Image",
                             svg::svg_extensions(),
                             [](std::span<const std::uint8_t> bytes) {
                               FormatReadResult result;
                               result.document = svg::DocumentIo::read(bytes, &result.notices);
                               return result;
                             },
                             [](const Document& document) { return svg::DocumentIo::write(document); },
                             [](std::span<const std::uint8_t> bytes) { return svg::sniff(bytes); }});
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
  // HEIF/HEIC is a read-only source (write stays null). Native builds use platform
  // codecs; the browser build parses the container in WASM and sends HEVC data to the
  // browser's WebCodecs decoder (see heif_document_io.hpp for the licensing boundary).
  registry.register_handler({"patchy.formats.heif",
                             "HEIF Image",
                             heif::heif_extensions(),
                             [](std::span<const std::uint8_t> bytes) { return heif::read_heif(bytes); },
                             nullptr,
                             [](std::span<const std::uint8_t> bytes) { return heif::sniff(bytes); }});
  // JPEG XR reads and writes through the in-box Windows Imaging Component codec; the write
  // function is registered on every platform so is_read_only_source_extension keeps Save on
  // .jxr, and the non-Windows stubs throw (see jxr_document_io.hpp).
  registry.register_handler({"patchy.formats.jxr",
                             "JPEG XR Image",
                             jxr::jxr_extensions(),
                             [](std::span<const std::uint8_t> bytes) { return jxr::read_jxr(bytes); },
                             [](const Document& document) { return jxr::write_jxr(document); },
                             [](std::span<const std::uint8_t> bytes) { return jxr::sniff(bytes); }});
  // Proton SDK textures (Seth's engine format): raw 8888/888/4444/565 pixels or an embedded
  // JPEG inside an optional RTPACK zlib wrapper, opened at the true size and written the way
  // RTPack writes them. Patchy's own codec, so it reads and writes on every platform.
  registry.register_handler({"patchy.formats.rttex",
                             "Proton Texture",
                             rttex::rttex_extensions(),
                             [](std::span<const std::uint8_t> bytes) { return rttex::read_rttex(bytes); },
                             [](const Document& document) { return rttex::write_rttex(document); },
                             [](std::span<const std::uint8_t> bytes) { return rttex::sniff(bytes); }});
  // DirectDraw Surface textures: masked uncompressed and BC1-BC7 block formats read on every
  // platform through the vendored bcdec; the writer emits A8R8G8B8, DXT1 or DXT5 with an
  // optional mip chain (docs/dds.md). Patchy's own codec, so nothing gates the row.
  registry.register_handler({"patchy.formats.dds",
                             "DirectDraw Surface",
                             dds::dds_extensions(),
                             [](std::span<const std::uint8_t> bytes) { return dds::read_dds(bytes); },
                             [](const Document& document) { return dds::write_dds(document); },
                             [](std::span<const std::uint8_t> bytes) { return dds::sniff(bytes); }});
  // Radiance HDR and OpenEXR open as 32-bit floating-point documents and write the
  // flattened document's linear light (docs/hdr-formats.md). Patchy's own RGBE code; EXR
  // through the vendored tinyexr.
  registry.register_handler({"patchy.formats.hdr",
                             "Radiance HDR Image",
                             hdr::hdr_extensions(),
                             [](std::span<const std::uint8_t> bytes) { return hdr::read_hdr(bytes); },
                             [](const Document& document) { return hdr::write_hdr(document); },
                             [](std::span<const std::uint8_t> bytes) { return hdr::sniff(bytes); }});
  registry.register_handler({"patchy.formats.exr",
                             "OpenEXR Image",
                             exr::exr_extensions(),
                             [](std::span<const std::uint8_t> bytes) { return exr::read_exr(bytes); },
                             [](const Document& document) { return exr::write_exr(document); },
                             [](std::span<const std::uint8_t> bytes) { return exr::sniff(bytes); }});
  // JPEG 2000 through the vendored OpenJPEG (docs/hdr-formats.md, docs/legal-constraints.md).
  registry.register_handler({"patchy.formats.jp2",
                             "JPEG 2000 Image",
                             jp2::jp2_extensions(),
                             [](std::span<const std::uint8_t> bytes) { return jp2::read_jp2(bytes); },
                             [](const Document& document) { return jp2::write_jp2(document); },
                             [](std::span<const std::uint8_t> bytes) { return jp2::sniff(bytes); }});
  // AVIF through the vendored libavif: dav1d decodes, libaom encodes (docs/avif.md,
  // docs/legal-constraints.md); the browser build registers the row with throwing functions.
  registry.register_handler({"patchy.formats.avif",
                             "AVIF Image",
                             avif::avif_extensions(),
                             [](std::span<const std::uint8_t> bytes) { return avif::read_avif(bytes); },
                             [](const Document& document) { return avif::write_avif(document); },
                             [](std::span<const std::uint8_t> bytes) { return avif::sniff(bytes); }});
  // JPEG XL through the vendored libjxl on every platform (docs/jxl.md). The encoder's
  // patches and dots stay off on every write (docs/legal-constraints.md).
  registry.register_handler({"patchy.formats.jxl",
                             "JPEG XL Image",
                             jxl::jxl_extensions(),
                             [](std::span<const std::uint8_t> bytes) { return jxl::read_jxl(bytes); },
                             [](const Document& document) { return jxl::write_jxl(document); },
                             [](std::span<const std::uint8_t> bytes) { return jxl::sniff(bytes); }});
  // Affinity's native container is a read-only source (write stays null). The 2.x
  // .afphoto/.afdesign/.afpub generations share the .af magic and wire grammar, so
  // one handler covers them all; pre-2.x files that fail the tree parse fall back
  // to the embedded-preview import with a notice. The container spec record lives
  // in docs/file-formats.md ".af (Affinity)".
  registry.register_handler({"patchy.formats.af",
                             "Affinity Document",
                             {".af", ".afphoto", ".afdesign", ".afpub"},
                             [](std::span<const std::uint8_t> bytes) {
                               FormatReadResult result;
                               result.document = af::DocumentIo::read(bytes, &result.notices);
                               return result;
                             },
                             nullptr,
                             [](std::span<const std::uint8_t> bytes) { return af::sniff(bytes); }});
  // Paint.NET documents are a read-only source like Affinity's (docs/pdn-format.md). The
  // sniff matches PDN3 files only; Paint.NET 1.x/2.x files are a bare gzip stream and open
  // by their extension.
  registry.register_handler({"patchy.formats.pdn",
                             "Paint.NET Image",
                             pdn::pdn_extensions(),
                             [](std::span<const std::uint8_t> bytes) {
                               FormatReadResult result;
                               result.document = pdn::DocumentIo::read(bytes, &result.notices);
                               return result;
                             },
                             nullptr,
                             [](std::span<const std::uint8_t> bytes) { return pdn::sniff(bytes); }});
  // Camera raws are read-only sources (write stays null). This headless path develops with
  // camera-JPEG-like defaults; the interactive develop dialog lives in the UI layer and runs
  // BEFORE load_document_from_path reaches the registry.
  registry.register_handler({"patchy.formats.camera_raw",
                             "Camera Raw Image",
                             raw::camera_raw_extensions(),
                             [](std::span<const std::uint8_t> bytes) {
                               return raw::read_camera_raw(bytes, raw::DevelopParams{});
                             },
                             nullptr,
                             nullptr});
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
