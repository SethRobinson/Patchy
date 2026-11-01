// patchy-gimp-host: runs GIMP 3 plug-ins for Patchy (docs/gimp-plugins.md, "Parts").
//
//   patchy-gimp-host                 the real mode: one request on stdin (a query of
//                                    several plug-ins, or one run), reports on stdout
//   patchy-gimp-host --query-plugin <program> [script]
//                                    queries one plug-in and prints Patchy's JSON
//                                    (development; the environment is the caller's)
//
// Nothing of GIMP is loaded here: plug-ins are separate programs spoken to over pipes.

#include "plugins/gimp/gimp_host_link.hpp"
#include "plugins/gimp/host/image_model.hpp"
#include "plugins/gimp/host/plugin_process.hpp"
#include "plugins/gimp/host/session.hpp"
#include "plugins/gimp/host/shared_pixels.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <csignal>
#include <unistd.h>
#endif

using namespace patchy::gimp;
using namespace patchy::gimp::host;

namespace {

std::ptrdiff_t read_stdin(std::uint8_t* data, std::size_t size) {
#ifdef _WIN32
  DWORD got = 0;
  if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), data, static_cast<DWORD>(size), &got, nullptr)) return -1;
  return static_cast<std::ptrdiff_t>(got);
#else
  for (;;) {
    const auto got = ::read(0, data, size);
    if (got < 0 && errno == EINTR) continue;
    return got;
  }
#endif
}

// Blocks until one whole frame has arrived.
std::optional<std::pair<link::Frame, std::vector<std::uint8_t>>> read_frame(link::FrameBuffer& buffer) {
  std::uint8_t chunk[65536];
  for (;;) {
    if (auto frame = buffer.next()) return frame;
    const auto got = read_stdin(chunk, sizeof chunk);
    if (got <= 0) return std::nullopt;
    buffer.append(chunk, static_cast<std::size_t>(got));
  }
}

// After the request, stdin carries only Cancel; its end means Patchy is gone.
void watch_stdin(link::FrameBuffer buffer) {
  std::thread([buffer = std::move(buffer)]() mutable {
    for (;;) {
      auto frame = read_frame(buffer);
      auto& events = link_events();
      std::lock_guard<std::mutex> lock(events.mutex);
      if (!frame.has_value()) {
        events.closed = true;
        return;
      }
      if (frame->first == link::Frame::Cancel) events.cancel = true;
    }
  }).detach();
}

void forward_output(std::string text) {
  append_plugin_output(text);
  if (tracing()) log_to_patchy("plug-in output: " + text);
}

int run_query(const link::QueryRequest& request) {
  set_tracing(request.trace);
  std::vector<std::pair<link::LaunchSpec, QueryOutcome>> outcomes;
  for (const auto& launch : request.plugins) {
    {
      auto& events = link_events();
      std::lock_guard<std::mutex> lock(events.mutex);
      if (events.cancel || events.closed) break;
    }
    std::string error;
    QueryOutcome outcome;
    if (auto process = start_plugin_process(launch, "-query", forward_output, &error)) {
      Session session(Session::Mode::Query, std::move(process), nullptr, nullptr);
      session.set_launch(&launch);
      outcome = session.run_query(std::chrono::milliseconds(request.timeout_ms));
      if (!outcome.ok && outcome.error.find("ended without") != std::string::npos) {
        const auto tail = plugin_output_tail(600);
        if (!tail.empty()) outcome.error += ": " + tail;
      }
    } else {
      outcome.error = error;
    }
    outcomes.emplace_back(launch, std::move(outcome));
  }
  send_to_patchy(link::encode_text(link::Frame::QueryResult, query_json(outcomes)));
  return 0;
}

int run_procedure(const link::RunRequest& request) {
  set_tracing(request.trace);
  link::RunResult failure;
  failure.status = link::RunStatus::Error;
  std::string error;
  auto pixels = SharedPixels::open(request.pixel_file, request.pixel_file_size, &error);
  const std::uint64_t layer_bytes = static_cast<std::uint64_t>(request.layer_width) * request.layer_height *
                                    request.channels * (request.bit_depth / 8);
  if (pixels == nullptr || request.output_offset + layer_bytes > pixels->size() ||
      request.input_offset + layer_bytes > pixels->size() ||
      (request.has_mask && request.mask_offset + static_cast<std::uint64_t>(request.layer_width) * request.layer_height > pixels->size())) {
    failure.code = "pixels";
    failure.message = pixels == nullptr ? error : "the shared pixel file is smaller than the layer";
    send_to_patchy(link::encode(failure));
    return 1;
  }
  if ((request.bit_depth != 8 && request.bit_depth != 16 && request.bit_depth != 32) ||
      (request.channels != 3 && request.channels != 4)) {
    failure.code = "format";
    failure.message = "unsupported layer format";
    send_to_patchy(link::encode(failure));
    return 1;
  }
  ImageModel model(request, pixels->data() + request.input_offset, pixels->data() + request.output_offset,
                   request.has_mask ? pixels->data() + request.mask_offset : nullptr);
  auto process = start_plugin_process(request.launch, "-run", forward_output, &error);
  if (process == nullptr) {
    failure.code = "start";
    failure.message = error;
    send_to_patchy(link::encode(failure));
    return 1;
  }
  Session session(Session::Mode::Run, std::move(process), &model, &request);
  auto outcome = session.run_procedure();
  send_to_patchy(link::encode(outcome.result));
  return 0;
}

int query_one_for_development(int argc, char** argv) {
  link::QueryRequest request;
  link::LaunchSpec launch;
  launch.program = argv[2];
  for (int i = 3; i < argc; ++i) launch.leading_args.push_back(argv[i]);
  request.plugins.push_back(launch);
  request.trace = true;
  set_tracing(true);
  return run_query(request);
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stdin), _O_BINARY);
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#else
  std::signal(SIGPIPE, SIG_IGN);
#endif
  if (argc >= 3 && std::strcmp(argv[1], "--query-plugin") == 0) {
    return query_one_for_development(argc, argv);
  }
  link::FrameBuffer buffer;
  std::optional<std::pair<link::Frame, std::vector<std::uint8_t>>> first;
  try {
    first = read_frame(buffer);
  } catch (const link::LinkError&) {
    return 2;
  }
  if (!first.has_value()) return 2;
  send_to_patchy(link::encode_hello());
  try {
    if (first->first == link::Frame::QueryRequest) {
      const auto request = link::decode_query_request(first->second);
      watch_stdin(std::move(buffer));
      const int code = run_query(request);
      std::fflush(stdout);
      std::_Exit(code);
    }
    if (first->first == link::Frame::RunRequest) {
      const auto request = link::decode_run_request(first->second);
      watch_stdin(std::move(buffer));
      const int code = run_procedure(request);
      std::fflush(stdout);
      std::_Exit(code);
    }
  } catch (const link::LinkError& e) {
    link::RunResult failure;
    failure.code = "link";
    failure.message = e.what();
    send_to_patchy(link::encode(failure));
    std::fflush(stdout);
    std::_Exit(2);
  }
  return 2;
}
