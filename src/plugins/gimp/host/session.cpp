#include "plugins/gimp/host/session.hpp"

#include "plugins/gimp/gimp_wire_codec.hpp"
#include "plugins/gimp/host/pdb.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <climits>
#include <cstdlib>
#include <sstream>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace patchy::gimp::host {

namespace {

std::mutex g_out_mutex;
std::atomic<bool> g_trace{false};
std::mutex g_output_mutex;
std::string g_output;

}  // namespace

void send_to_patchy(const std::vector<std::uint8_t>& frame) {
  std::lock_guard<std::mutex> lock(g_out_mutex);
  std::fwrite(frame.data(), 1, frame.size(), stdout);
  std::fflush(stdout);
}

void log_to_patchy(const std::string& text) {
  if (g_trace.load()) {
    send_to_patchy(link::encode_text(link::Frame::Log, text));
  }
}

bool tracing() { return g_trace.load(); }
void set_tracing(bool on) { g_trace.store(on); }

LinkEvents& link_events() {
  static LinkEvents events;
  return events;
}

void append_plugin_output(const std::string& text) {
  std::lock_guard<std::mutex> lock(g_output_mutex);
  g_output += text;
  if (g_output.size() > 64 * 1024) {
    g_output.erase(0, g_output.size() - 32 * 1024);
  }
}

std::string plugin_output_tail(std::size_t bytes) {
  std::lock_guard<std::mutex> lock(g_output_mutex);
  return g_output.size() <= bytes ? g_output : g_output.substr(g_output.size() - bytes);
}

Session::Session(Mode mode, std::unique_ptr<PluginProcess> process, ImageModel* model, const link::RunRequest* request)
    : mode_(mode), process_(std::move(process)), model_(model), request_(request) {
  if (request_ != nullptr) {
    launch_ = &request_->launch;
    if (!request_->wire_log.empty()) {
#ifdef _WIN32
      std::wstring wide(request_->wire_log.size(), L'\0');
      wide.resize(static_cast<std::size_t>(MultiByteToWideChar(CP_UTF8, 0, request_->wire_log.data(),
                                                               static_cast<int>(request_->wire_log.size()), wide.data(),
                                                               static_cast<int>(wide.size()))));
      if (_wfopen_s(&wire_log_, wide.c_str(), L"wb") != 0) wire_log_ = nullptr;
#else
      wire_log_ = std::fopen(request_->wire_log.c_str(), "wb");
#endif
    }
    for (const auto& [name, data] : request_->stored_data) {
      stored_data_[name] = data;
    }
  }
}

Session::~Session() {
  if (process_) {
    process_->kill_tree();
  }
  if (reader_.joinable()) {
    reader_.join();
  }
  if (wire_log_ != nullptr) {
    std::fclose(wire_log_);
  }
}

// The same records scripts/dev/gimp/wire_tap.cpp writes: u8 direction ('H' host to
// plug-in, 'P' plug-in to host), u32 length, u64 microseconds, the bytes (little-endian).
void Session::record_wire(char direction, const std::uint8_t* data, std::size_t size) {
  if (wire_log_ == nullptr || size == 0) {
    return;
  }
  const auto us = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started_).count());
  const auto length = static_cast<std::uint32_t>(size);
  std::uint8_t header[13];
  header[0] = static_cast<std::uint8_t>(direction);
  for (int i = 0; i < 4; ++i) header[1 + i] = static_cast<std::uint8_t>(length >> (8 * i));
  for (int i = 0; i < 8; ++i) header[5 + i] = static_cast<std::uint8_t>(us >> (8 * i));
  std::lock_guard<std::mutex> lock(wire_mutex_);
  std::fwrite(header, 1, sizeof header, wire_log_);
  std::fwrite(data, 1, size, wire_log_);
  std::fflush(wire_log_);
}

void Session::start_reader() {
  reader_ = std::thread([this] {
    FunctionSource source([this](std::uint8_t* data, std::size_t size) {
      const auto got = process_->read(data, size);
      if (got > 0) record_wire('P', data, static_cast<std::size_t>(got));
      return got;
    });
    for (;;) {
      Event event;
      try {
        event.message = read_message(source);
        event.kind = Event::Kind::Message;
      } catch (const WireError& e) {
        event.kind = std::string(e.what()) == "the plug-in closed its connection" ? Event::Kind::Closed : Event::Kind::Broken;
        event.error = e.what();
      }
      const bool last = event.kind != Event::Kind::Message;
      {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queue_.push_back(std::move(event));
      }
      queue_cv_.notify_one();
      if (last) {
        break;
      }
    }
  });
}

std::optional<Session::Event> Session::next_event(std::chrono::steady_clock::time_point deadline, bool* cancelled) {
  for (;;) {
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      queue_cv_.wait_for(lock, std::chrono::milliseconds(50), [this] { return !queue_.empty(); });
      if (!queue_.empty()) {
        Event e = std::move(queue_.front());
        queue_.pop_front();
        return e;
      }
    }
    {
      auto& link = link_events();
      std::lock_guard<std::mutex> lock(link.mutex);
      if (link.cancel || link.closed) {
        *cancelled = true;
        return std::nullopt;
      }
    }
    if (mode_ == Mode::Run) {
      flush_display();
      watch_plugin_windows();
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      return std::nullopt;
    }
  }
}

bool Session::send(const Message& message) {
  if (tracing() && message.type != MessageType::TileData) {
    log_to_patchy("-> " + describe_message(message));
  }
  try {
    const auto bytes = encode_message(message);
    record_wire('H', bytes.data(), bytes.size());
    return process_->write(bytes.data(), bytes.size());
  } catch (const WireError& e) {
    log_to_patchy(std::string("encode failed: ") + e.what());
    return false;
  }
}

ProcedureInfo* Session::registered(const std::string& name) {
  for (auto& p : procedures_) {
    if (p.install.name.value_or("") == name) {
      return &p;
    }
  }
  return nullptr;
}

void Session::show_message(const std::string& text) {
  if (mode_ == Mode::Run) {
    send_to_patchy(link::encode_text(link::Frame::Message, text));
  } else {
    log_to_patchy("message: " + text);
  }
}

void Session::progress(double fraction, const std::string& text) {
  if (!text.empty() || fraction < 0) {
    progress_text_ = text.empty() ? progress_text_ : text;
  }
  if (mode_ == Mode::Run) {
    link::Progress p;
    p.fraction = fraction;
    p.text = progress_text_;
    send_to_patchy(link::encode(p));
  }
}

void Session::note_refused(const std::string& procedure) {
  if (std::find(unsupported_.begin(), unsupported_.end(), procedure) == unsupported_.end()) {
    unsupported_.push_back(procedure);
    send_to_patchy(link::encode_text(link::Frame::Unsupported, procedure));
  }
}

void Session::flush_display() {
  if (model_ == nullptr) {
    return;
  }
  if (const auto rect = model_->take_pending_update()) {
    send_to_patchy(link::encode_rect(link::Frame::Update, {rect->x, rect->y, rect->width, rect->height}));
  }
}

void Session::watch_plugin_windows() {
#ifdef _WIN32
  const auto now = std::chrono::steady_clock::now();
  if (now - last_window_check_ < std::chrono::milliseconds(200)) {
    return;
  }
  last_window_check_ = now;
  struct Search {
    DWORD pid;
    RECT best{};
    long long best_area{0};
  } search{static_cast<DWORD>(process_->pid())};
  EnumWindows(
      [](HWND hwnd, LPARAM data) -> BOOL {
        auto* s = reinterpret_cast<Search*>(data);
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid != s->pid || !IsWindowVisible(hwnd)) {
          return TRUE;
        }
        RECT r{};
        GetWindowRect(hwnd, &r);
        const long long area = static_cast<long long>(r.right - r.left) * (r.bottom - r.top);
        if (area > s->best_area) {
          s->best_area = area;
          s->best = r;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&search));
  const std::uint64_t key = search.best_area == 0
                                ? 0
                                : (static_cast<std::uint64_t>(static_cast<std::uint16_t>(search.best.left)) << 48) ^
                                      (static_cast<std::uint64_t>(static_cast<std::uint16_t>(search.best.top)) << 32) ^
                                      static_cast<std::uint64_t>(search.best_area);
  if (key != last_window_key_) {
    last_window_key_ = key;
    send_to_patchy(link::encode_rect(link::Frame::PluginWindow,
                                     {search.best.left, search.best.top, search.best.right - search.best.left,
                                      search.best.bottom - search.best.top},
                                     // The plug-in's process id, 0 while no window is up.
                                     search.best_area > 0 ? static_cast<std::uint32_t>(process_->pid()) : 0u));
  }
#endif
}

void Session::answer_call(const ProcCall& call) {
  const auto name = call.name.value_or("");
  if (tracing()) {
    Message m;
    m.type = MessageType::ProcRun;
    m.call = call;
    log_to_patchy("<- " + describe_message(m));
  }
  PdbReply reply;
  if (const auto* handler = pdb_table().find(name)) {
    try {
      reply = (*handler)(*this, Args(name, call.params));
    } catch (const PdbCallingError& e) {
      log_to_patchy(std::string("calling error: ") + e.what());
      reply = pdb_calling_error();
    }
  } else {
    if (std::find(unsupported_.begin(), unsupported_.end(), name) == unsupported_.end()) {
      unsupported_.push_back(name);
      send_to_patchy(link::encode_text(link::Frame::Unsupported, name));
    }
    log_to_patchy("unsupported procedure: " + name);
    reply = pdb_error();
  }
  Message out;
  out.type = MessageType::ProcReturn;
  out.call.name = name;
  out.call.params.push_back(status_param(reply.status));
  for (auto& v : reply.values) {
    out.call.params.push_back(std::move(v));
  }
  send(out);
}

void Session::handle_tile_request(const TileRequest& request) {
  Message out;
  out.type = MessageType::TileData;
  if (request.drawable_id == -1) {
    // The plug-in asks to send a tile: answer with an empty tile, then its data arrives.
    out.tile_data.drawable_id = -1;
    expecting_tile_write_ = true;
    send(out);
    return;
  }
  if (model_ == nullptr || !model_->read_tile(request.drawable_id, request.tile_num, request.shadow != 0, out.tile_data)) {
    log_to_patchy("tile request out of range: drawable " + std::to_string(request.drawable_id) + " tile " +
                  std::to_string(request.tile_num));
    // An empty tile keeps the protocol in step; the plug-in sees no pixels there.
    out.tile_data = TileData{};
    out.tile_data.drawable_id = request.drawable_id;
    out.tile_data.tile_num = request.tile_num;
  }
  send(out);
}

QueryOutcome Session::run_query(std::chrono::milliseconds timeout) {
  QueryOutcome outcome;
  // A Script-Fu plug-in started through GIMP's Script-Fu interpreter is a Script-Fu
  // script like the ones the Script-Fu extension registers.
  if (launch_ != nullptr) {
    const auto names_script_fu = [](const std::string& s) { return s.find("script-fu-interpreter") != std::string::npos; };
    script_fu_ = names_script_fu(launch_->program) ||
                 std::any_of(launch_->leading_args.begin(), launch_->leading_args.end(), names_script_fu);
  }
  start_reader();
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  for (;;) {
    bool cancelled = false;
    auto event = next_event(deadline, &cancelled);
    if (!event.has_value()) {
      process_->kill_tree();
      outcome.error = cancelled ? "cancelled" : "the plug-in did not finish registering within " +
                                                    std::to_string(timeout.count() / 1000) + " seconds";
      outcome.ok = false;
      break;
    }
    if (event->kind != Event::Kind::Message) {
      process_->wait(2000);
      outcome.ok = !procedures_.empty() && event->kind == Event::Kind::Closed;
      if (!outcome.ok) {
        outcome.error = event->kind == Event::Kind::Broken
                            ? "the plug-in sent a message Patchy cannot read: " + event->error
                            : "the plug-in ended without registering anything (" + process_->exit_description() + ")";
      }
      break;
    }
    auto& m = event->message;
    if (m.type == MessageType::ProcInstall) {
      if (tracing()) log_to_patchy("<- " + describe_message(m));
      ProcedureInfo info;
      info.install = std::move(m.install);
      const auto name = info.install.name.value_or("");
      if (name == "extension-script-fu" || name == "plug-in-script-fu-eval") {
        script_fu_ = true;
      }
      procedures_.push_back(std::move(info));
    } else if (m.type == MessageType::ProcRun) {
      answer_call(m.call);
    } else if (m.type == MessageType::HasInit) {
      has_init_ = true;
    } else if (m.type == MessageType::Quit) {
      process_->wait(2000);
      outcome.ok = true;
      break;
    } else if (m.type == MessageType::TileRequest) {
      handle_tile_request(m.tile_request);
    }
  }
  process_->kill_tree();
  outcome.has_init = has_init_;
  outcome.script_fu = script_fu_;
  outcome.procedures = std::move(procedures_);
  return outcome;
}

RunOutcome Session::run_procedure() {
  RunOutcome outcome;
  auto& result = outcome.result;
  const auto& req = *request_;
  const auto finish_error = [&](std::string code, std::string message) {
    result.status = link::RunStatus::Error;
    result.code = std::move(code);
    result.message = std::move(message);
  };

  // The procedure's signature, as its query recorded it.
  ProcInstall install;
  try {
    SpanSource source(req.install);
    auto m = read_message(source);
    if (m.type != MessageType::ProcInstall) throw WireError("not a registration");
    install = std::move(m.install);
  } catch (const WireError& e) {
    finish_error("bad-registration", std::string("the stored registration could not be read: ") + e.what());
    return outcome;
  }
  if (!is_image_procedure(install)) {
    finish_error("not-image-procedure", "the procedure does not take an image and a layer");
    return outcome;
  }

  ProcCall run;
  run.name = req.procedure;
  run.params.push_back(int_param("GimpRunMode", req.run_mode));
  run.params.push_back(int_param("GimpImage", kImageId));
  run.params.push_back(id_array_param("GimpCoreObjectArray", "GimpItem", {kLayerId}));
  try {
    std::vector<Param> overrides;
    if (req.argument_count > 0) {
      // The caller's own values for the arguments after the image ones, in order,
      // encoded as the parameter list of one PROC_RUN message.
      SpanSource source(req.arguments);
      auto decoded = read_message(source);
      overrides = std::move(decoded.call.params);
    }
    for (std::size_t i = 3; i < install.params.size(); ++i) {
      const auto k = i - 3;
      if (k < overrides.size()) {
        run.params.push_back(overrides[k]);
        continue;
      }
      std::string why;
      auto value = default_argument(install.params[i], &why);
      if (!value.has_value()) {
        throw PdbCallingError(why);
      }
      run.params.push_back(std::move(*value));
    }
  } catch (const PdbCallingError& e) {
    finish_error("unsupported-argument", e.what());
    return outcome;
  } catch (const WireError& e) {
    finish_error("bad-arguments", e.what());
    return outcome;
  }

  start_reader();
  Message config;
  config.type = MessageType::Config;
  auto& c = config.config;
  const double light[4] = {0.6, 0.6, 0.6, 1.0};
  const double dark[4] = {0.4, 0.4, 0.4, 1.0};
  c.check_custom_color1 = pdb_color_rgba(light, req.color_profile);
  c.check_custom_color2 = pdb_color_rgba(dark, req.color_profile);
  c.show_help_button = 0;
  c.default_display_id = kDisplayId;
  c.app_name = std::string("Patchy");
  c.monitor_number = 0;
  if (!req.icon_theme_dir.empty()) c.icon_theme_dir = req.icon_theme_dir;
  // As GIMP sends them; plug-ins expand the swap path themselves.
  c.swap_path = std::string("${gimp_cache_dir}");
  c.swap_compression = std::string("fast");
  c.num_processors = static_cast<std::int32_t>(std::max(1u, std::thread::hardware_concurrency()));
  Message call;
  call.type = MessageType::ProcRun;
  call.call = run;
  if (!send(config) || !send(call)) {
    process_->wait(2000);
    finish_error("crashed", "the plug-in ended before it could be started (" + process_->exit_description() + ")");
    return outcome;
  }

  const auto deadline = req.timeout_ms == 0 ? std::chrono::steady_clock::time_point::max()
                                            : std::chrono::steady_clock::now() + std::chrono::milliseconds(req.timeout_ms);
  bool returned = false;
  for (;;) {
    bool cancelled = false;
    auto event = next_event(deadline, &cancelled);
    if (!event.has_value()) {
      process_->kill_tree();
      if (cancelled) {
        result.status = link::RunStatus::Cancelled;
        result.code = "cancelled";
        result.message = "cancelled in Patchy";
      } else {
        finish_error("timeout", "the plug-in did not finish within " + std::to_string(req.timeout_ms / 1000) + " seconds");
      }
      break;
    }
    if (event->kind != Event::Kind::Message) {
      process_->wait(3000);
      if (!returned) {
        if (event->kind == Event::Kind::Broken) {
          process_->kill_tree();
          finish_error("protocol", "the plug-in sent a message Patchy cannot read: " + event->error);
        } else {
          finish_error("crashed", "the plug-in ended without finishing (" + process_->exit_description() + ")");
        }
      }
      break;
    }
    auto& m = event->message;
    switch (m.type) {
      case MessageType::ProcRun:
        answer_call(m.call);
        break;
      case MessageType::TileRequest:
        handle_tile_request(m.tile_request);
        break;
      case MessageType::TileData: {
        const bool accepted = expecting_tile_write_ && model_->write_tile(m.tile_data);
        expecting_tile_write_ = false;
        if (!accepted) {
          log_to_patchy("tile write refused: drawable " + std::to_string(m.tile_data.drawable_id) + " tile " +
                        std::to_string(m.tile_data.tile_num));
        }
        Message ack;
        ack.type = MessageType::TileAck;
        send(ack);
        break;
      }
      case MessageType::ProcReturn:
      case MessageType::TempProcReturn: {
        if (m.call.name.value_or("") != req.procedure) {
          log_to_patchy("unexpected return from " + m.call.name.value_or(""));
          break;
        }
        returned = true;
        const auto status = m.call.params.empty() ? PdbStatus::ExecutionError
                                                  : static_cast<PdbStatus>(m.call.params.front().int_value);
        std::string message;
        if (m.call.params.size() > 1 && m.call.params[1].kind == ParamKind::String) {
          message = m.call.params[1].string_value.value_or("");
        }
        if (status == PdbStatus::Success) {
          result.status = link::RunStatus::Ok;
        } else if (status == PdbStatus::Cancel) {
          result.status = link::RunStatus::Cancelled;
          result.code = "cancelled";
        } else {
          result.status = link::RunStatus::Error;
          result.code = unsupported_.empty() ? "plugin-error" : "unsupported-procedure";
          result.message = message;
        }
        break;
      }
      case MessageType::ProcInstall:
        note_temporary_procedure(m.install.name.value_or(""));
        break;
      case MessageType::Quit:
        if (!returned) {
          finish_error("quit", "the plug-in quit without finishing");
        }
        break;
      default:
        break;
    }
    if (returned) {
      break;
    }
  }
  flush_display();
  if (returned) {
    // A finished plug-in exits by itself; give it a moment, then end what is left.
    process_->wait(5000);
  }
  process_->kill_tree();
  if (result.status == link::RunStatus::Error && !unsupported_.empty()) {
    result.code = "unsupported-procedure";
    result.detail = unsupported_.front();
  }
  if (result.status == link::RunStatus::Error && (result.code == "crashed" || result.code == "plugin-error")) {
    const auto tail = plugin_output_tail(1500);
    if (!tail.empty()) {
      result.message += (result.message.empty() ? "" : "\n") + tail;
    }
  }
  const auto changed = model_->changed_area();
  result.changed = !changed.empty();
  result.changed_x = changed.x;
  result.changed_y = changed.y;
  result.changed_width = changed.width;
  result.changed_height = changed.height;
  for (const auto& [name, data] : stored_data_) {
    result.stored_data.emplace_back(name, data);
  }
  result.unsupported_calls = unsupported_;
  if (result.status == link::RunStatus::Ok) {
    result.foreground_changed = context_foreground_changed(result.foreground);
    result.background_changed = context_background_changed(result.background);
  }
  return outcome;
}

// ---------------------------------------------------------------------------------
// JSON for the query cache.

namespace {

void json_string(std::ostringstream& out, const std::string& s) {
  out << '"';
  for (const unsigned char c : s) {
    switch (c) {
      case '"': out << "\\\""; break;
      case '\\': out << "\\\\"; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default:
        if (c < 0x20) {
          char buffer[8];
          std::snprintf(buffer, sizeof buffer, "\\u%04x", c);
          out << buffer;
        } else {
          out << static_cast<char>(c);
        }
    }
  }
  out << '"';
}

void json_field(std::ostringstream& out, const char* name, const std::string& value, bool comma = true) {
  json_string(out, name);
  out << ':';
  json_string(out, value);
  if (comma) out << ',';
}

const char* def_kind_name(ParamDefKind kind) {
  switch (kind) {
    case ParamDefKind::Default: return "default";
    case ParamDefKind::Int: return "int";
    case ParamDefKind::Unit: return "unit";
    case ParamDefKind::Enum: return "enum";
    case ParamDefKind::Choice: return "choice";
    case ParamDefKind::Boolean: return "boolean";
    case ParamDefKind::Double: return "double";
    case ParamDefKind::String: return "string";
    case ParamDefKind::GeglColor: return "color";
    case ParamDefKind::Id: return "id";
    case ParamDefKind::IdArray: return "id-array";
    case ParamDefKind::ExportOptions: return "export-options";
    case ParamDefKind::Resource: return "resource";
    case ParamDefKind::File: return "file";
    case ParamDefKind::Curve: return "curve";
  }
  return "unknown";
}

}  // namespace

std::string query_json(const std::vector<std::pair<link::LaunchSpec, QueryOutcome>>& outcomes) {
  std::ostringstream out;
  out << "{\"plugins\":[";
  bool first_plugin = true;
  for (const auto& [launch, q] : outcomes) {
    if (!first_plugin) out << ',';
    first_plugin = false;
    out << '{';
    json_field(out, "program", launch.program);
    json_string(out, "args");
    out << ":[";
    for (std::size_t i = 0; i < launch.leading_args.size(); ++i) {
      if (i) out << ',';
      json_string(out, launch.leading_args[i]);
    }
    out << "],\"ok\":" << (q.ok ? "true" : "false") << ',';
    json_field(out, "error", q.error);
    out << "\"hasInit\":" << (q.has_init ? "true" : "false") << ",\"scriptFu\":" << (q.script_fu ? "true" : "false")
        << ",\"procedures\":[";
    bool first = true;
    for (const auto& p : q.procedures) {
      if (!first) out << ',';
      first = false;
      const auto cls = classify_procedure(p, q.script_fu);
      out << '{';
      json_field(out, "name", p.install.name.value_or(""));
      out << "\"type\":" << static_cast<std::uint32_t>(p.install.type) << ',';
      json_field(out, "menuLabel", p.menu_label);
      json_string(out, "menuPaths");
      out << ":[";
      for (std::size_t i = 0; i < p.menu_paths.size(); ++i) {
        if (i) out << ',';
        json_string(out, p.menu_paths[i]);
      }
      out << "],";
      json_field(out, "imageTypes", p.image_types);
      out << "\"sensitivitySet\":" << (p.sensitivity_set ? "true" : "false") << ",\"sensitivity\":" << p.sensitivity_mask
          << ',';
      json_field(out, "blurb", p.blurb);
      json_field(out, "help", p.help);
      json_field(out, "authors", p.authors);
      json_field(out, "copyright", p.copyright);
      json_field(out, "date", p.date);
      json_field(out, "use",
                 cls.use == ProcedureUse::LayerFilter ? "layer" : cls.use == ProcedureUse::Unsupported ? "unsupported" : "hidden");
      json_field(out, "reason", cls.reason);
      json_string(out, "params");
      out << ":[";
      for (std::size_t i = 0; i < p.install.params.size(); ++i) {
        const auto& d = p.install.params[i];
        if (i) out << ',';
        out << '{';
        json_field(out, "name", d.name.value_or(""));
        json_field(out, "nick", strip_mnemonic(d.nick.value_or("")));
        json_field(out, "blurb", d.blurb.value_or(""));
        json_field(out, "kind", def_kind_name(d.kind));
        json_field(out, "valueType", d.value_type_name.value_or(""), false);
        out << '}';
      }
      out << "],";
      Message m;
      m.type = MessageType::ProcInstall;
      m.install = p.install;
      json_field(out, "install", base64_encode(encode_message(m)), false);
      out << '}';
    }
    out << "]}";
  }
  out << "]}";
  return out.str();
}

}  // namespace patchy::gimp::host
