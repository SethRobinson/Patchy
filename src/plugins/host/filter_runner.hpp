#pragma once

// The legacy filter host proper: loads one .8bf, builds the filter record and
// callback suites, drives the selector sequence and the tiling loop, and hands
// the pixels back. Runs inside patchy-8bf-host32.exe / patchy-8bf-host64.exe,
// never inside Patchy. Windows only.

#include "host_protocol.hpp"

#include <cstdint>
#include <functional>

namespace patchy::legacy_host {

struct RunnerImage {
  std::int32_t width{0};
  std::int32_t height{0};
  std::int32_t planes{0};          // 3 or 4, interleaved
  const std::uint8_t* input{nullptr};
  std::uint8_t* output{nullptr};   // pre-filled with the input; receives the result
  const std::uint8_t* mask{nullptr};  // width * height coverage, or null
};

struct RunnerCallbacks {
  // Reported by the plug-in's progress callback (done of total).
  std::function<void(std::int32_t, std::int32_t)> progress;
  // Polled from the plug-in's abort callback and between tiles.
  std::function<bool()> should_abort;
};

// Runs the plug-in described by `request` over `image`. Never throws; a crash
// inside the plug-in is caught and reported as kRunError with the exception
// code in the message.
[[nodiscard]] RunResult run_filter(const RunRequest& request, const RunnerImage& image,
                                   const RunnerCallbacks& callbacks);

}  // namespace patchy::legacy_host
