// Linux JPEG XR decode and encode through the distribution's jxrlib, run in the separate
// patchy-jxr-host process (compiled for native Linux builds and the AppImage when CMake
// defines PATCHY_JXR_SYSTEM_LIBRARY, see CMakeLists and docs/jxr.md, Linux). Patchy ships
// no JPEG XR codec here either: the helper dlopens libjxrglue.so.0 from the user's package
// manager, and the AppImage and Flatpak never bundle it (docs/legal-licensing.md).
//
// Why a process: jxrlib is known to crash on malformed files (GitHub issue 269). A crash or
// a hang ends the helper, never Patchy, and is reported as a decode failure.
//
// The document built from the helper's reply mirrors jxr_document_io_win.cpp exactly: float
// and fixed-point frames open as 32-bit linear scRGB with deep editing on and go through
// tone_map_scrgb_to_rgba8 otherwise; integer frames come down to 8 bits; 96 DPI reads as
// untagged (72 PPI); the same notices. Integer frames carry no ICC conversion here (WIC
// applies an embedded profile); see docs/jxr.md.

#include "formats/jxr_document_io.hpp"

#include "core/pixel_depth.hpp"
#include "formats/jxr_host_protocol.hpp"
#include "support/translate_noop.hpp"

#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

extern char** environ;

namespace patchy::jxr {

namespace {

namespace host = patchy::jxr::host;

constexpr const char* kHelperName = "patchy-jxr-host";
constexpr auto kProbeTimeout = std::chrono::seconds(10);
constexpr auto kCodecTimeout = std::chrono::seconds(300);

// The missing-library message ends with the exact install command per distribution, like
// the HEIF reader's: the open-failed box shows it as selectable text to copy into a terminal.
constexpr std::string_view kLibraryMissingMessage = PATCHY_TRANSLATE_NOOP(
    "QObject",
    "Patchy cannot open or save JPEG XR images because the system's jxrlib library is not installed. "
    "Install it and try again.\nDebian/Ubuntu: sudo apt install libjxr0\nFedora/RHEL (EPEL): sudo dnf install jxrlib");

constexpr std::string_view kHelperMissingMessage = PATCHY_TRANSLATE_NOOP(
    "QObject", "This build of Patchy has no JPEG XR helper (patchy-jxr-host), so it cannot open or save JPEG XR images.");

constexpr std::string_view kHelperFailedMessage = PATCHY_TRANSLATE_NOOP(
    "QObject", "The system's JPEG XR library (jxrlib) failed on this image. The file may be damaged.");

constexpr std::string_view kFlatpakMessage = PATCHY_TRANSLATE_NOOP(
    "QObject",
    "JPEG XR images cannot be opened or saved in the Flatpak version of Patchy. Use the AppImage or a "
    "distribution package of Patchy with the system's jxrlib library installed.");

constexpr std::string_view kUnsupportedFormatMessage =
    PATCHY_TRANSLATE_NOOP("QObject", "This JPEG XR image's pixel format is not supported");

constexpr std::string_view kDimensionsMessage =
    PATCHY_TRANSLATE_NOOP("QObject", "This JPEG XR image's dimensions are not supported");

[[nodiscard]] bool running_in_flatpak() {
  return std::getenv("FLATPAK_ID") != nullptr;
}

// patchy-jxr-host beside the running executable (patchy, or a test binary in the build tree).
[[nodiscard]] std::filesystem::path helper_path() {
  std::error_code error;
  const auto executable = std::filesystem::read_symlink("/proc/self/exe", error);
  if (error) {
    return {};
  }
  return executable.parent_path() / kHelperName;
}

struct Reply {
  host::Status status{host::Status::BadRequest};
  std::string detail;
  std::vector<std::uint8_t> body;  // the whole reply message
  std::size_t payload_offset{0};
};

enum class RunOutcome { Replied, HelperMissing, HelperFailed };

// Starts the helper, sends one request and reads one reply. A crash, a hang past `timeout`
// or a malformed reply is HelperFailed; the helper is always reaped.
RunOutcome run_helper(const std::vector<std::uint8_t>& request, std::chrono::seconds timeout, Reply& reply) {
  const auto path = helper_path();
  if (path.empty() || access(path.c_str(), X_OK) != 0) {
    return RunOutcome::HelperMissing;
  }
  int sockets[2] = {-1, -1};
  if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) != 0) {
    return RunOutcome::HelperFailed;
  }
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, sockets[1], STDIN_FILENO);
  posix_spawn_file_actions_adddup2(&actions, sockets[1], STDOUT_FILENO);
  const std::string program = path.string();
  char* argv[] = {const_cast<char*>(program.c_str()), nullptr};
  pid_t pid = -1;
  const int spawned = posix_spawn(&pid, program.c_str(), &actions, nullptr, argv, environ);
  posix_spawn_file_actions_destroy(&actions);
  close(sockets[1]);
  const int fd = sockets[0];
  if (spawned != 0) {
    close(fd);
    return RunOutcome::HelperFailed;
  }

  const auto deadline = std::chrono::steady_clock::now() + timeout;
  auto wait_ready = [&](short events) {
    for (;;) {
      const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
      if (left.count() <= 0) {
        return false;
      }
      pollfd entry{fd, events, 0};
      const int ready = poll(&entry, 1, static_cast<int>(std::min<long long>(left.count(), 1000)));
      if (ready > 0) {
        return true;
      }
      if (ready < 0 && errno != EINTR) {
        return false;
      }
    }
  };

  // send with MSG_NOSIGNAL: a helper that died early must not take Patchy down by SIGPIPE.
  bool ok = true;
  const std::uint64_t size = request.size();
  std::vector<std::uint8_t> framed(sizeof(size) + request.size());
  std::memcpy(framed.data(), &size, sizeof(size));
  std::memcpy(framed.data() + sizeof(size), request.data(), request.size());
  for (std::size_t sent = 0; ok && sent < framed.size();) {
    if (!wait_ready(POLLOUT)) {
      ok = false;
      break;
    }
    const ssize_t written = send(fd, framed.data() + sent, framed.size() - sent, MSG_NOSIGNAL);
    if (written < 0 && errno == EINTR) {
      continue;
    }
    if (written <= 0) {
      ok = false;
      break;
    }
    sent += static_cast<std::size_t>(written);
  }
  shutdown(fd, SHUT_WR);

  std::vector<std::uint8_t> incoming;
  std::uint64_t expected = 0;
  bool have_length = false;
  while (ok) {
    if (have_length && incoming.size() >= sizeof(expected) + expected) {
      break;
    }
    if (!wait_ready(POLLIN)) {
      ok = false;
      break;
    }
    std::uint8_t buffer[65536];
    const ssize_t got = recv(fd, buffer, sizeof(buffer), 0);
    if (got < 0 && errno == EINTR) {
      continue;
    }
    if (got <= 0) {
      ok = false;
      break;
    }
    incoming.insert(incoming.end(), buffer, buffer + got);
    if (!have_length && incoming.size() >= sizeof(expected)) {
      std::memcpy(&expected, incoming.data(), sizeof(expected));
      have_length = true;
      if (expected > host::kMaximumMessageBytes) {
        ok = false;
      }
    }
  }
  close(fd);

  int status = 0;
  if (!ok) {
    kill(pid, SIGKILL);
  }
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
  if (!ok) {
    return RunOutcome::HelperFailed;
  }

  reply.body.assign(incoming.begin() + static_cast<std::ptrdiff_t>(sizeof(expected)), incoming.end());
  host::Reader reader(reply.body);
  if (reader.u32() != host::kMagic) {
    return RunOutcome::HelperFailed;
  }
  reply.status = static_cast<host::Status>(reader.u32());
  reply.detail = reader.text();
  if (!reader.ok()) {
    return RunOutcome::HelperFailed;
  }
  reply.payload_offset = sizeof(std::uint32_t) * 2U + sizeof(std::uint64_t) + reply.detail.size();
  return RunOutcome::Replied;
}

[[noreturn]] void throw_for(RunOutcome outcome, const Reply& reply, bool encoding) {
  if (outcome == RunOutcome::HelperMissing) {
    throw std::runtime_error(std::string(kHelperMissingMessage));
  }
  if (outcome == RunOutcome::HelperFailed) {
    throw std::runtime_error(std::string(kHelperFailedMessage));
  }
  switch (reply.status) {
    case host::Status::LibraryMissing: throw std::runtime_error(std::string(kLibraryMissingMessage));
    case host::Status::UnsupportedFormat: throw std::runtime_error(std::string(kUnsupportedFormatMessage));
    case host::Status::BadDimensions: throw std::runtime_error(std::string(kDimensionsMessage));
    default: break;
  }
  if (encoding) {
    throw std::runtime_error("Unable to write this JPEG XR image (" + reply.detail + ")");
  }
  throw std::runtime_error("Unable to decode this JPEG XR image (" + reply.detail + ")");
}

std::vector<std::uint8_t> request_header(host::Operation operation) {
  host::Writer writer;
  writer.u32(host::kMagic);
  writer.u32(static_cast<std::uint32_t>(operation));
  return writer.bytes();
}

void apply_resolution(Document& document, double dpi_x, double dpi_y) {
  if (dpi_x > 1.0 && dpi_y > 1.0 && !(dpi_x == 96.0 && dpi_y == 96.0)) {
    document.print_settings().horizontal_ppi = dpi_x;
    document.print_settings().vertical_ppi = dpi_y;
  } else {
    document.print_settings().horizontal_ppi = 72.0;
    document.print_settings().vertical_ppi = 72.0;
  }
}

// The WIC reader's float_pixels: values above 1.0 and negative components survive, non-finite
// colors become 0, alpha is clamped coverage with NaN as opaque.
PixelBuffer float_pixels(const std::vector<float>& samples, std::int32_t width, std::int32_t height) {
  bool has_alpha = false;
  for (std::size_t offset = 3; offset < samples.size(); offset += 4) {
    if (samples[offset] < 1.0F) {
      has_alpha = true;
      break;
    }
  }
  const std::size_t channels = has_alpha ? 4U : 3U;
  PixelBuffer pixels(width, height,
                     with_bit_depth(has_alpha ? PixelFormat::rgba8() : PixelFormat::rgb8(), BitDepth::Float32));
  auto* out = reinterpret_cast<float*>(pixels.data().data());
  const auto pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  for (std::size_t i = 0; i < pixel_count; ++i) {
    const float* in = samples.data() + i * 4U;
    float* dst = out + i * channels;
    for (std::size_t c = 0; c < 3U; ++c) {
      dst[c] = std::isfinite(in[c]) ? in[c] : 0.0F;
    }
    if (has_alpha) {
      dst[3] = std::isnan(in[3]) ? 1.0F : std::clamp(in[3], 0.0F, 1.0F);
    }
  }
  return pixels;
}

void add_frame_notice(FormatReadResult& result, std::uint32_t frames) {
  if (frames > 1) {
    result.notices.push_back("Opened the primary image only (" + std::to_string(frames) + " images in the file)");
  }
}

struct ProbeResult {
  bool available{false};
};

const ProbeResult& probe() {
  static ProbeResult result;
  static std::once_flag once;
  std::call_once(once, [] {
    if (running_in_flatpak()) {
      return;
    }
    Reply reply;
    result.available = run_helper(request_header(host::Operation::Probe), kProbeTimeout, reply) ==
                           RunOutcome::Replied &&
                       reply.status == host::Status::Ok;
  });
  return result;
}

}  // namespace

bool is_available() noexcept {
  try {
    return probe().available;
  } catch (...) {
    return false;
  }
}

FormatReadResult read_jxr(std::span<const std::uint8_t> bytes) {
  if (running_in_flatpak()) {
    throw std::runtime_error(std::string(kFlatpakMessage));
  }
  host::Writer writer;
  writer.append(request_header(host::Operation::Decode));
  writer.blob(bytes.data(), bytes.size());
  Reply reply;
  const auto outcome = run_helper(writer.bytes(), kCodecTimeout, reply);
  if (outcome != RunOutcome::Replied || reply.status != host::Status::Ok) {
    throw_for(outcome, reply, false);
  }

  host::Reader reader(std::span<const std::uint8_t>(reply.body).subspan(reply.payload_offset));
  const std::int32_t width = reader.i32();
  const std::int32_t height = reader.i32();
  const double dpi_x = reader.f64();
  const double dpi_y = reader.f64();
  const std::uint32_t frames = reader.u32();
  const bool is_float = reader.u8() != 0;
  const std::uint32_t bits_per_channel = reader.u32();
  const auto pixel_bytes = reader.blob();
  const auto pixel_count = static_cast<std::size_t>(std::max(width, 0)) * static_cast<std::size_t>(std::max(height, 0));
  if (!reader.ok() || width <= 0 || height <= 0 ||
      static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) > host::kMaximumPixels ||
      pixel_bytes.size() != pixel_count * 4U * (is_float ? sizeof(float) : 1U)) {
    throw std::runtime_error(std::string(kHelperFailedMessage));
  }

  std::vector<float> samples;
  if (is_float) {
    samples.resize(pixel_count * 4U);
    std::memcpy(samples.data(), pixel_bytes.data(), pixel_bytes.size());
    if (deep_editing_enabled()) {
      auto pixels = float_pixels(samples, width, height);
      FormatReadResult result;
      result.document = Document(width, height, pixels.format());
      result.document.color_state().bit_depth = BitDepth::Float32;
      apply_resolution(result.document, dpi_x, dpi_y);
      result.document.add_pixel_layer("Background", std::move(pixels));
      add_frame_notice(result, frames);
      return result;
    }
  }
  const std::vector<std::uint8_t> rgba = is_float ? tone_map_scrgb_to_rgba8(samples, width, height) : pixel_bytes;

  bool has_alpha = false;
  for (std::size_t offset = 3; offset < rgba.size(); offset += 4) {
    if (rgba[offset] != 0xFF) {
      has_alpha = true;
      break;
    }
  }
  const auto format = has_alpha ? PixelFormat::rgba8() : PixelFormat::rgb8();
  PixelBuffer pixels(width, height, format);
  for (std::int32_t y = 0; y < height; ++y) {
    const auto* source = rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4U;
    auto row = pixels.row(y);
    if (has_alpha) {
      std::memcpy(row.data(), source, row.size());
    } else {
      for (std::int32_t x = 0; x < width; ++x) {
        std::memcpy(row.data() + static_cast<std::size_t>(x) * 3U, source + static_cast<std::size_t>(x) * 4U, 3U);
      }
    }
  }

  FormatReadResult result;
  result.document = Document(width, height, format);
  apply_resolution(result.document, dpi_x, dpi_y);
  result.document.add_pixel_layer("Background", std::move(pixels));
  if (is_float) {
    result.notices.push_back(
        PATCHY_TRANSLATE_NOOP("QObject", "This HDR image was tone mapped to 8-bit sRGB; Patchy edits 8 bits per channel."));
  } else if (bits_per_channel > 8) {
    result.notices.push_back("Converted " + std::to_string(bits_per_channel) +
                             "-bit channels to 8-bit; Patchy edits 8 bits per channel.");
  }
  add_frame_notice(result, frames);
  return result;
}

std::vector<std::uint8_t> write_jxr(std::span<const std::uint8_t> rgba, std::int32_t width, std::int32_t height,
                                    bool has_alpha, double horizontal_ppi, double vertical_ppi,
                                    const WriteOptions& options) {
  if (width <= 0 || height <= 0) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Cannot write an empty document as JPEG XR"));
  }
  if (rgba.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "JPEG XR write buffer is too small"));
  }
  if (running_in_flatpak()) {
    throw std::runtime_error(std::string(kFlatpakMessage));
  }
  host::Writer writer;
  writer.append(request_header(host::Operation::Encode));
  writer.i32(width);
  writer.i32(height);
  writer.u8(has_alpha ? 1 : 0);
  writer.f64(horizontal_ppi);
  writer.f64(vertical_ppi);
  writer.i32(options.quality);
  writer.u8(options.lossless ? 1 : 0);
  writer.blob(rgba.data(), static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
  Reply reply;
  const auto outcome = run_helper(writer.bytes(), kCodecTimeout, reply);
  if (outcome != RunOutcome::Replied || reply.status != host::Status::Ok) {
    throw_for(outcome, reply, true);
  }
  host::Reader reader(std::span<const std::uint8_t>(reply.body).subspan(reply.payload_offset));
  auto encoded = reader.blob();
  if (!reader.ok() || encoded.empty()) {
    throw std::runtime_error(std::string(kHelperFailedMessage));
  }
  return encoded;
}

}  // namespace patchy::jxr
