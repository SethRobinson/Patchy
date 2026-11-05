#include "support/scan_frame.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace patchy::support {
namespace {

int frame_channels(ScanFrameFormat format) {
  return format == ScanFrameFormat::Rgb ? 3 : 1;
}

bool is_single_pass_color(ScanFrameFormat format) {
  return format == ScanFrameFormat::Red || format == ScanFrameFormat::Green ||
         format == ScanFrameFormat::Blue;
}

int pass_channel(ScanFrameFormat format) {
  switch (format) {
    case ScanFrameFormat::Green:
      return 1;
    case ScanFrameFormat::Blue:
      return 2;
    default:
      return 0;
  }
}

std::size_t checked_multiply(std::size_t a, std::size_t b) {
  if (a != 0 && b > std::numeric_limits<std::size_t>::max() / a) {
    throw std::invalid_argument("scan image is too large");
  }
  return a * b;
}

}  // namespace

void ScanImageAssembler::begin_frame(const ScanFrameParameters& parameters) {
  if (complete_) {
    throw std::logic_error("scan already complete");
  }
  if (in_frame_) {
    throw std::logic_error("previous scan frame not ended");
  }
  const auto format_value = static_cast<int>(parameters.format);
  if (format_value < 0 || format_value > static_cast<int>(ScanFrameFormat::Blue)) {
    throw std::invalid_argument("unsupported scan frame format");
  }
  if (parameters.depth != 1 && parameters.depth != 8 && parameters.depth != 16) {
    throw std::invalid_argument("unsupported scan sample depth");
  }
  if (parameters.pixels_per_line <= 0 || parameters.bytes_per_line <= 0 || parameters.lines < -1) {
    throw std::invalid_argument("invalid scan frame geometry");
  }
  const auto bits_per_line = checked_multiply(
      checked_multiply(static_cast<std::size_t>(parameters.pixels_per_line),
                       static_cast<std::size_t>(frame_channels(parameters.format))),
      static_cast<std::size_t>(parameters.depth));
  if (static_cast<std::size_t>(parameters.bytes_per_line) < (bits_per_line + 7) / 8) {
    throw std::invalid_argument("scan line stride is shorter than its pixels");
  }

  const bool color_pass = is_single_pass_color(parameters.format);
  if (passes_seen_ == 0) {
    three_pass_ = color_pass;
    if (!three_pass_ && !parameters.last_frame) {
      throw std::invalid_argument("single-frame scan announced further frames");
    }
    image_ = {};
    image_.width = parameters.pixels_per_line;
    image_.channels = three_pass_ ? 3 : frame_channels(parameters.format);
    image_.bit_depth = parameters.depth == 16 ? 16 : 8;
    if (parameters.lines > 0) {
      const auto row_bytes = checked_multiply(
          checked_multiply(static_cast<std::size_t>(image_.width), static_cast<std::size_t>(image_.channels)),
          static_cast<std::size_t>(image_.bit_depth / 8));
      image_.samples.reserve(checked_multiply(row_bytes, static_cast<std::size_t>(parameters.lines)));
    }
  } else {
    if (!three_pass_ || !color_pass) {
      throw std::invalid_argument("scan frame does not continue a three-pass scan");
    }
    if (parameters.pixels_per_line != frame_.pixels_per_line || parameters.depth != frame_.depth ||
        parameters.bytes_per_line != frame_.bytes_per_line) {
      throw std::invalid_argument("three-pass scan frames differ in geometry");
    }
  }
  frame_ = parameters;
  in_frame_ = true;
  rows_in_frame_ = 0;
  pending_row_.clear();
  pending_row_.reserve(static_cast<std::size_t>(parameters.bytes_per_line));
}

void ScanImageAssembler::append(std::span<const std::uint8_t> bytes) {
  if (!in_frame_) {
    throw std::logic_error("scan data outside a frame");
  }
  const auto stride = static_cast<std::size_t>(frame_.bytes_per_line);
  while (!bytes.empty()) {
    if (frame_.lines >= 0 && rows_in_frame_ >= frame_.lines) {
      return;  // backends may pad the last read; the announced line count wins
    }
    if (pending_row_.empty() && bytes.size() >= stride) {
      store_row(bytes.first(stride));
      bytes = bytes.subspan(stride);
      continue;
    }
    const auto take = std::min(stride - pending_row_.size(), bytes.size());
    pending_row_.insert(pending_row_.end(), bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(take));
    bytes = bytes.subspan(take);
    if (pending_row_.size() == stride) {
      store_row(pending_row_);
      pending_row_.clear();
    }
  }
}

void ScanImageAssembler::store_row(std::span<const std::uint8_t> row) {
  const bool first_pass = passes_seen_ == 0;
  if (!first_pass && rows_in_frame_ >= image_.height) {
    throw std::runtime_error("three-pass scan frames differ in height");
  }
  const auto out_sample_bytes = static_cast<std::size_t>(image_.bit_depth / 8);
  const auto out_row_bytes =
      static_cast<std::size_t>(image_.width) * static_cast<std::size_t>(image_.channels) * out_sample_bytes;
  if (first_pass) {
    const auto new_size = image_.samples.size() + out_row_bytes;
    if (new_size < image_.samples.size()) {
      throw std::runtime_error("scan image is too large");
    }
    image_.samples.resize(new_size);
    ++image_.height;
  }
  auto* out_row = image_.samples.data() + static_cast<std::size_t>(rows_in_frame_) * out_row_bytes;
  const int in_channels = frame_channels(frame_.format);
  const int channel_offset = three_pass_ ? pass_channel(frame_.format) : 0;
  // One-bit gray is line art, where SANE's 1 is black; in one-bit color frames 1 is full
  // intensity (sane-backends' test backend draws solid white as 0x00 gray, 0xff color).
  const bool ones_are_black = frame_.format == ScanFrameFormat::Gray;
  for (int x = 0; x < image_.width; ++x) {
    for (int c = 0; c < in_channels; ++c) {
      const auto in_index = static_cast<std::size_t>(x) * static_cast<std::size_t>(in_channels) +
                            static_cast<std::size_t>(c);
      auto* out = out_row + (static_cast<std::size_t>(x) * static_cast<std::size_t>(image_.channels) +
                             static_cast<std::size_t>(c + channel_offset)) *
                                out_sample_bytes;
      switch (frame_.depth) {
        case 1: {
          const auto bit = (row[in_index / 8] >> (7 - static_cast<int>(in_index % 8))) & 1U;
          *out = (bit != 0) != ones_are_black ? 255 : 0;
          break;
        }
        case 8:
          *out = row[in_index];
          break;
        default:
          std::memcpy(out, row.data() + in_index * 2, 2);
          break;
      }
    }
  }
  ++rows_in_frame_;
}

void ScanImageAssembler::end_frame() {
  if (!in_frame_) {
    throw std::logic_error("no scan frame to end");
  }
  in_frame_ = false;
  pending_row_.clear();  // a partial trailing line is not a line
  if (frame_.lines >= 0 && rows_in_frame_ < frame_.lines) {
    throw std::runtime_error("the scanner delivered fewer lines than announced");
  }
  if (passes_seen_ > 0 && rows_in_frame_ != image_.height) {
    throw std::runtime_error("three-pass scan frames differ in height");
  }
  ++passes_seen_;
  if (frame_.last_frame) {
    if (three_pass_ && passes_seen_ != 3) {
      throw std::runtime_error("three-pass scan ended before all colors arrived");
    }
    if (image_.height == 0) {
      throw std::runtime_error("the scanner delivered no image lines");
    }
    complete_ = true;
  } else if (three_pass_ && passes_seen_ >= 3) {
    throw std::runtime_error("three-pass scan announced a fourth frame");
  }
}

ScanImage ScanImageAssembler::finish() {
  if (!complete_) {
    throw std::logic_error("scan is not complete");
  }
  auto image = std::move(image_);
  image_ = {};
  return image;
}

double sane_resolution_to_dpi(std::int32_t word, bool is_fixed) noexcept {
  const double value = is_fixed ? static_cast<double>(word) / 65536.0 : static_cast<double>(word);
  return value > 0.0 ? value : 0.0;
}

bool is_network_scanner(std::string_view device_name) noexcept {
  for (const std::string_view prefix : {"airscan:", "escl:", "net:"}) {
    if (device_name.starts_with(prefix)) {
      return true;
    }
  }
  return false;
}

std::string network_scanner_address(std::string_view model) {
  const auto at = model.find("ip=");
  if (at == std::string_view::npos) {
    return {};
  }
  const auto rest = model.substr(at + 3);
  const auto end = rest.find_first_of(" \t,)");
  const auto address = rest.substr(0, end);
  const bool plausible = !address.empty() && std::all_of(address.begin(), address.end(), [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || c == '.' || c == ':';
  });
  return plausible ? std::string(address) : std::string{};
}

AcquireFailureCause classify_sane_failure(int sane_status, std::string_view device_name) noexcept {
  // SANE_Status values from sane.h.
  switch (sane_status) {
    case 1:  // SANE_STATUS_UNSUPPORTED
    case 4:  // SANE_STATUS_INVAL
      return AcquireFailureCause::UnsupportedSettings;
    case 3:  // SANE_STATUS_DEVICE_BUSY
      return AcquireFailureCause::Busy;
    case 6:  // SANE_STATUS_JAMMED
      return AcquireFailureCause::Jammed;
    case 7:  // SANE_STATUS_NO_DOCS
      return AcquireFailureCause::FeederEmpty;
    case 8:  // SANE_STATUS_COVER_OPEN
      return AcquireFailureCause::CoverOpen;
    case 9:  // SANE_STATUS_IO_ERROR
      return is_network_scanner(device_name) ? AcquireFailureCause::NetworkUnreachable
                                             : AcquireFailureCause::ConnectionLost;
    case 10:  // SANE_STATUS_NO_MEM
      return AcquireFailureCause::OutOfMemory;
    case 11:  // SANE_STATUS_ACCESS_DENIED
      return AcquireFailureCause::AccessDenied;
    default:
      return AcquireFailureCause::Unknown;
  }
}

AcquireFailureCause classify_gphoto_failure(int gphoto_status) noexcept {
  // GP_ERROR_* values from gphoto2-port-result.h and gphoto2-result.h.
  switch (gphoto_status) {
    case -3:  // GP_ERROR_NO_MEMORY
      return AcquireFailureCause::OutOfMemory;
    case -6:    // GP_ERROR_NOT_SUPPORTED
    case -105:  // GP_ERROR_MODEL_NOT_FOUND
      return AcquireFailureCause::CameraNotSupported;
    case -7:   // GP_ERROR_IO
    case -10:  // GP_ERROR_TIMEOUT
    case -34:  // GP_ERROR_IO_READ
    case -35:  // GP_ERROR_IO_WRITE
    case -52:  // GP_ERROR_IO_USB_FIND
    case -113: // GP_ERROR_CAMERA_ERROR
      return AcquireFailureCause::ConnectionLost;
    case -31:  // GP_ERROR_IO_INIT: usually the USB device node is not writable
      return AcquireFailureCause::AccessDenied;
    case -53:   // GP_ERROR_IO_USB_CLAIM
    case -60:   // GP_ERROR_IO_LOCK
    case -110:  // GP_ERROR_CAMERA_BUSY
      return AcquireFailureCause::CameraClaimed;
    case -102:  // GP_ERROR_CORRUPTED_DATA
      return AcquireFailureCause::CorruptedData;
    case -107:  // GP_ERROR_DIRECTORY_NOT_FOUND
    case -108:  // GP_ERROR_FILE_NOT_FOUND
      return AcquireFailureCause::ImageMissing;
    default:
      return AcquireFailureCause::Unknown;
  }
}

}  // namespace patchy::support
