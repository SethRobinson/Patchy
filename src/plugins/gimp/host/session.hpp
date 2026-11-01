#pragma once

// One conversation with one plug-in process: a query (what it registers) or a run (one
// procedure on the layer). Owns the message loop: plug-in messages, Patchy's Cancel and
// the time limit arrive as events on one queue, and the main thread answers them all,
// so replies keep GIMP's strict request/answer order (docs/gimp-plugins.md, "Parts").

#include "plugins/gimp/gimp_host_link.hpp"
#include "plugins/gimp/gimp_procedure.hpp"
#include "plugins/gimp/gimp_protocol.hpp"
#include "plugins/gimp/host/image_model.hpp"
#include "plugins/gimp/host/plugin_process.hpp"

#include <chrono>
#include <cstdio>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace patchy::gimp::host {

// Frames to Patchy (thread-safe; the helper's stdout).
void send_to_patchy(const std::vector<std::uint8_t>& frame);
void log_to_patchy(const std::string& text);
bool tracing();
void set_tracing(bool on);

// Patchy's frames after the first request arrive on the helper's stdin; a reader thread
// turns them into events.
struct LinkEvents {
  std::mutex mutex;
  bool cancel{false};
  bool closed{false};
};
LinkEvents& link_events();

struct QueryOutcome {
  bool ok{false};
  std::string error;
  bool has_init{false};
  std::vector<ProcedureInfo> procedures;
  bool script_fu{false};
};

struct RunOutcome {
  link::RunResult result;
};

class Session {
public:
  enum class Mode { Query, Run };

  Session(Mode mode, std::unique_ptr<PluginProcess> process, ImageModel* model, const link::RunRequest* request);
  ~Session();

  QueryOutcome run_query(std::chrono::milliseconds timeout);
  RunOutcome run_procedure();

  // For the PDB groups.
  [[nodiscard]] Mode mode() const noexcept { return mode_; }
  [[nodiscard]] ImageModel* model() const noexcept { return model_; }
  [[nodiscard]] const link::RunRequest* request() const noexcept { return request_; }
  [[nodiscard]] const link::LaunchSpec* launch() const noexcept { return launch_; }
  void set_launch(const link::LaunchSpec* launch) noexcept { launch_ = launch; }
  ProcedureInfo* registered(const std::string& name);
  [[nodiscard]] const std::vector<ProcedureInfo>& registered_all() const noexcept { return procedures_; }
  void note_temporary_procedure(const std::string& name) { temporary_.insert(name); }
  std::map<std::string, std::vector<std::uint8_t>>& stored_data() noexcept { return stored_data_; }
  void show_message(const std::string& text);
  void progress(double fraction, const std::string& text);
  [[nodiscard]] const std::string& progress_text() const noexcept { return progress_text_; }
  void flush_display();
  // A procedure Patchy has but cannot carry out for this run: reported like a missing one.
  void note_refused(const std::string& procedure);
  [[nodiscard]] bool is_script_fu() const noexcept { return script_fu_; }
  void set_script_fu() noexcept { script_fu_ = true; }

private:
  struct Event {
    enum class Kind { Message, Closed, Broken } kind{Kind::Message};
    Message message;
    std::string error;
  };

  void start_reader();
  std::optional<Event> next_event(std::chrono::steady_clock::time_point deadline, bool* cancelled);
  bool send(const Message& message);
  void answer_call(const ProcCall& call);
  void handle_tile_request(const TileRequest& request);
  void watch_plugin_windows();
  void record_wire(char direction, const std::uint8_t* data, std::size_t size);

  Mode mode_;
  std::unique_ptr<PluginProcess> process_;
  ImageModel* model_;
  const link::RunRequest* request_;
  const link::LaunchSpec* launch_{nullptr};
  std::vector<ProcedureInfo> procedures_;
  std::set<std::string> temporary_;
  std::map<std::string, std::vector<std::uint8_t>> stored_data_;
  std::vector<std::string> unsupported_;
  std::string progress_text_;
  bool script_fu_{false};
  bool has_init_{false};
  bool expecting_tile_write_{false};

  std::FILE* wire_log_{nullptr};
  std::mutex wire_mutex_;
  std::chrono::steady_clock::time_point started_{std::chrono::steady_clock::now()};
  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;
  std::deque<Event> queue_;
  std::thread reader_;
#ifdef _WIN32
  std::chrono::steady_clock::time_point last_window_check_{};
  std::uint64_t last_window_key_{0};
#endif
};

// The plug-in output collected for error reports (shared with the process callback).
void append_plugin_output(const std::string& text);
std::string plugin_output_tail(std::size_t bytes);

// Writes the query outcomes as the JSON document Patchy caches.
std::string query_json(const std::vector<std::pair<link::LaunchSpec, QueryOutcome>>& outcomes);

}  // namespace patchy::gimp::host
