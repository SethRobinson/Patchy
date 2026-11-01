#pragma once

// Starting one GIMP 3 plug-in as its own process with the two protocol pipes GIMP's
// command-line convention expects (docs/gimp-plugins.md, "Starting a plug-in"):
//
//   <program> [script] -gimp <protocol, decimal> <read fd> <write fd> <-query|-init|-run> <stack-trace mode>
//
// The descriptors are C runtime file descriptors in the plug-in (observed: the GIMP 3.2.6
// Windows build hands them over through the C runtime's inherited-descriptor table).
// The plug-in and everything it starts live in a process group Patchy can end at once:
// a job object on Windows, a process group on Linux and macOS.

#include "plugins/gimp/gimp_host_link.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace patchy::gimp::host {

class PluginProcess {
public:
  virtual ~PluginProcess() = default;

  // Blocking read from the plug-in's write pipe: bytes read, 0 at end, < 0 on error.
  virtual std::ptrdiff_t read(std::uint8_t* data, std::size_t size) = 0;
  // Blocking write to the plug-in's read pipe; false when the plug-in is gone.
  virtual bool write(const std::uint8_t* data, std::size_t size) = 0;
  // Ends the plug-in and every process it started.
  virtual void kill_tree() = 0;
  // Waits up to timeout_ms for the plug-in to exit; true when it has.
  virtual bool wait(int timeout_ms) = 0;
  [[nodiscard]] virtual int exit_code() const = 0;
  // A short description of how it ended ("exit code 3", "crashed (access violation)").
  [[nodiscard]] virtual std::string exit_description() const = 0;
  [[nodiscard]] virtual std::uint64_t pid() const = 0;
};

// `mode` is "-query", "-init" or "-run". `output` receives the plug-in's stdout and
// stderr as they arrive (on a worker thread).
std::unique_ptr<PluginProcess> start_plugin_process(const link::LaunchSpec& launch, const std::string& mode,
                                                    std::function<void(std::string)> output, std::string* error);

}  // namespace patchy::gimp::host
