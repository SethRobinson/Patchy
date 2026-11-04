// Multi-page TIFF (docs/tiff.md, "Multi-page TIFF"): the page walk over the directory chain
// and the assembly of decoded pages into one layered document. Decoding a page is the
// caller's (Qt's reader, then read_flat_tiff). Qt-free.

#include "psd/psd_tiff.hpp"

#include "core/document.hpp"
#include "core/document_depth.hpp"
#include "core/pixel_depth.hpp"
#include "psd/psd_tiff_internal.hpp"
#include "support/translate_noop.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace patchy::psd {
namespace {

using namespace tiff_detail;

constexpr std::uint16_t kTagPageName = 285;
constexpr std::size_t kMaxPageNameBytes = 255;

bool valid_utf8(const std::string& text) noexcept {
  std::size_t index = 0;
  while (index < text.size()) {
    const auto lead = static_cast<unsigned char>(text[index]);
    const std::size_t length = lead < 0x80U ? 1U : (lead >> 5U) == 0x6U ? 2U : (lead >> 4U) == 0xEU ? 3U
                               : (lead >> 3U) == 0x1EU                     ? 4U
                                                                           : 0U;
    if (length == 0U || index + length > text.size()) {
      return false;
    }
    for (std::size_t next = 1; next < length; ++next) {
      if ((static_cast<unsigned char>(text[index + next]) & 0xC0U) != 0x80U) {
        return false;
      }
    }
    index += length;
  }
  return true;
}

// PageName up to its first NUL, control characters dropped, trimmed; bytes that are not
// UTF-8 (the tag is ASCII by the spec) keep only their ASCII part.
std::string page_name(const TiffBytes& tiff, const Directory& directory) {
  const auto* entry = directory.find(kTagPageName);
  if (entry == nullptr || entry->type != 2U || entry->size == 0U) {
    return {};
  }
  std::string name;
  for (const auto byte : tiff.span(entry->offset, std::min(entry->size, kMaxPageNameBytes))) {
    if (byte == 0U) {
      break;
    }
    if (byte >= 0x20U && byte != 0x7FU) {
      name.push_back(static_cast<char>(byte));
    }
  }
  if (!valid_utf8(name)) {
    std::erase_if(name, [](char c) { return static_cast<unsigned char>(c) >= 0x80U; });
  }
  const auto first = name.find_first_not_of(' ');
  if (first == std::string::npos) {
    return {};
  }
  return name.substr(first, name.find_last_not_of(' ') - first + 1U);
}

// RGBA at `depth` (a three-channel page gains opaque alpha).
PixelBuffer rgba_pixels(PixelBuffer pixels, BitDepth depth) {
  if (pixels.format().channels == 4U) {
    return pixels;
  }
  const auto domain = deep_domain_for(depth);
  PixelBuffer rgba(pixels.width(), pixels.height(), with_bit_depth(PixelFormat::rgba8(), depth));
  std::vector<float> row(static_cast<std::size_t>(pixels.width()) * 4U);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    load_rgba_row(pixels, y, 0, pixels.width(), domain, row);
    store_rgba_row(rgba, y, 0, pixels.width(), domain, row);
  }
  return rgba;
}

}  // namespace

std::vector<TiffPage> tiff_pages(std::span<const std::uint8_t> bytes) {
  std::vector<TiffPage> pages;
  const auto little = tiff_little_endian(bytes);
  if (!little.has_value()) {
    return pages;
  }
  const TiffBytes tiff(bytes, *little);
  std::set<std::uint32_t> seen;
  std::size_t index = 0;
  for (auto offset = tiff.u32(4); offset != 0U && seen.size() < kMaxPages && seen.insert(offset).second; ++index) {
    const auto directory = read_directory(tiff, offset);
    if (!directory.has_value()) {
      break;
    }
    const auto subfile = unsigned_value(tiff, directory->find(kTagNewSubfileType)).value_or(0U);
    if ((subfile & 0x01U) == 0U) {
      TiffPage page;
      page.directory_offset = offset;
      page.directory_index = index;
      page.width = unsigned_value(tiff, directory->find(kTagImageWidth)).value_or(0U);
      page.height = unsigned_value(tiff, directory->find(kTagImageLength)).value_or(0U);
      page.name = page_name(tiff, *directory);
      pages.push_back(std::move(page));
    }
    offset = directory->next;
  }
  return pages;
}

Document tiff_pages_as_layers(std::vector<TiffPageDocument> pages, std::vector<std::string>* notices) {
  if (pages.empty()) {
    throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "The TIFF has no pages to open"));
  }
  auto depth = BitDepth::UInt8;
  std::int32_t width = 1;
  std::int32_t height = 1;
  for (const auto& page : pages) {
    const auto& document = std::as_const(page.document);
    depth = std::max(depth, document.color_state().bit_depth,
                     [](BitDepth a, BitDepth b) { return static_cast<int>(a) < static_cast<int>(b); });
    width = std::max(width, document.width());
    height = std::max(height, document.height());
  }
  auto& first = pages.front().document;
  if (std::as_const(first).color_state().bit_depth != depth) {
    convert_document_depth(first, depth);
  }
  Document merged(width, height, with_bit_depth(PixelFormat::rgba8(), depth));
  merged.print_settings() = std::as_const(first).print_settings();
  merged.color_state() = std::as_const(first).color_state();
  merged.color_state().bit_depth = depth;
  bool dropped_channels = false;
  // Bottom up, so page 1 ends on top.
  for (auto page = pages.rbegin(); page != pages.rend(); ++page) {
    auto& document = page->document;
    dropped_channels = dropped_channels || !std::as_const(document).channels().empty();
    if (std::as_const(document).color_state().bit_depth != depth) {
      convert_document_depth(document, depth);
    }
    if (document.layers().empty()) {
      continue;
    }
    auto pixels = rgba_pixels(std::move(document.layers().front().pixels()), depth);
    if (pixels.empty()) {
      continue;
    }
    merged.add_layer(Layer(merged.allocate_layer_id(), page->layer_name, std::move(pixels)));
  }
  if (!merged.layers().empty()) {
    merged.set_active_layer(merged.layers().back().id());
  }
  if (dropped_channels && notices != nullptr) {
    notices->push_back(PATCHY_TRANSLATE_NOOP(
        "QObject", "The pages' extra channels were left out: they belong to single pages, not the layered document."));
  }
  return merged;
}

}  // namespace patchy::psd
