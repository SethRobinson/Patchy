#include "core/document_channel.hpp"

#include <atomic>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace patchy {

namespace {

// Copies intentionally retain their source revision: an undo snapshot holds the
// same COW pixels and therefore the same cache identity. Every fresh channel or
// later mutation takes a never-reused value, including edits after abandoning a
// redo branch.
std::atomic<std::uint64_t> g_document_channel_revision_counter{0};

std::uint64_t next_document_channel_revision() noexcept {
  return ++g_document_channel_revision_counter;
}

void validate_channel_pixels(const PixelBuffer& pixels) {
  if (pixels.format() != PixelFormat::gray8()) {
    throw std::invalid_argument("Document channels must use 8-bit grayscale pixels");
  }
}

void validate_display_info(const DocumentChannelDisplayInfo& display_info) {
  if (!std::isfinite(display_info.opacity) || display_info.opacity < 0.0F || display_info.opacity > 1.0F) {
    throw std::out_of_range("Document channel display opacity must be in the inclusive range [0, 1]");
  }
}

}  // namespace

DocumentChannel::DocumentChannel(ChannelId id, std::string name, DocumentChannelKind kind, PixelBuffer pixels)
    : id_(id), name_(std::move(name)), kind_(kind), pixels_(std::move(pixels)) {
  if (id == 0) {
    throw std::invalid_argument("Document channel id 0 is reserved");
  }
  validate_channel_pixels(pixels_);
  if (kind_ == DocumentChannelKind::Spot) {
    display_info_.color_indicates = DocumentChannelColorIndicates::SpotColor;
  }
  content_revision_ = next_document_channel_revision();
}

ChannelId DocumentChannel::id() const noexcept {
  return id_;
}

const std::string& DocumentChannel::name() const noexcept {
  return name_;
}

DocumentChannelKind DocumentChannel::kind() const noexcept {
  return kind_;
}

PixelBuffer& DocumentChannel::pixels() noexcept {
  mark_changed();
  return pixels_;
}

const PixelBuffer& DocumentChannel::pixels() const noexcept {
  return pixels_;
}

const std::optional<std::uint32_t>& DocumentChannel::photoshop_identifier() const noexcept {
  return photoshop_identifier_;
}

const DocumentChannelDisplayInfo& DocumentChannel::display_info() const noexcept {
  return display_info_;
}

const std::vector<std::uint8_t>& DocumentChannel::raw_photoshop_display_info() const noexcept {
  return raw_photoshop_display_info_;
}

std::uint64_t DocumentChannel::content_revision() const noexcept {
  return content_revision_;
}

void DocumentChannel::set_name(std::string name) {
  name_ = std::move(name);
  mark_changed();
}

void DocumentChannel::set_pixels(PixelBuffer pixels) {
  validate_channel_pixels(pixels);
  pixels_ = std::move(pixels);
  mark_changed();
}

void DocumentChannel::set_photoshop_identifier(std::optional<std::uint32_t> identifier) noexcept {
  photoshop_identifier_ = identifier;
  mark_changed();
}

void DocumentChannel::set_display_info(DocumentChannelDisplayInfo display_info) {
  validate_display_info(display_info);
  display_info_ = display_info;
  // Once normalized presentation state is edited, the imported record is stale.
  // The PSD writer can regenerate it from display_info().
  raw_photoshop_display_info_.clear();
  mark_changed();
}

void DocumentChannel::set_raw_photoshop_display_info(std::vector<std::uint8_t> record) noexcept {
  raw_photoshop_display_info_ = std::move(record);
  mark_changed();
}

void DocumentChannel::mark_changed() noexcept {
  content_revision_ = next_document_channel_revision();
}

}  // namespace patchy
