#include "support/startup_trace.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/time.h>
#include <unistd.h>
#elif defined(__linux__)
#include <time.h>
#include <unistd.h>
#include <fstream>
#include <iterator>
#include <sstream>
#endif

namespace patchy::startup_trace {

namespace detail {
bool g_enabled = false;
}

namespace {

constexpr std::size_t kCapacity = 256;
constexpr const char* kPreMainName = "process.pre_main";

std::array<Event, kCapacity> g_events{};
std::atomic<std::size_t> g_count{0};
std::chrono::steady_clock::time_point g_origin{};
double g_pre_main_ms = 0.0;
bool g_start_known = false;
int g_depth = 0;

// Milliseconds from the OS-recorded process creation to now, or a negative value.
double os_process_age_ms() noexcept {
#if defined(_WIN32)
  FILETIME creation{};
  FILETIME exit_time{};
  FILETIME kernel{};
  FILETIME user{};
  if (GetProcessTimes(GetCurrentProcess(), &creation, &exit_time, &kernel, &user) == FALSE) {
    return -1.0;
  }
  FILETIME now{};
  GetSystemTimePreciseAsFileTime(&now);
  const auto ticks = [](const FILETIME& time) {
    return (static_cast<unsigned long long>(time.dwHighDateTime) << 32U) | time.dwLowDateTime;
  };
  const auto start = ticks(creation);
  const auto current = ticks(now);
  return current >= start ? static_cast<double>(current - start) / 10000.0 : -1.0;  // 100 ns units
#elif defined(__APPLE__)
  int request[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, static_cast<int>(getpid())};
  kinfo_proc info{};
  std::size_t size = sizeof(info);
  if (sysctl(request, 4, &info, &size, nullptr, 0) != 0 || size == 0) {
    return -1.0;
  }
  timeval now{};
  gettimeofday(&now, nullptr);
  const auto& start = info.kp_proc.p_starttime;
  const double age = static_cast<double>(now.tv_sec - start.tv_sec) * 1000.0 +
                     static_cast<double>(now.tv_usec - start.tv_usec) / 1000.0;
  return age >= 0.0 ? age : -1.0;
#elif defined(__linux__)
  // Field 22 of /proc/self/stat: clock ticks since boot (10 ms resolution). The
  // command name (field 2) may hold spaces, so count from its closing parenthesis.
  std::ifstream stat_file("/proc/self/stat");
  const std::string stat((std::istreambuf_iterator<char>(stat_file)), std::istreambuf_iterator<char>());
  const auto paren = stat.rfind(')');
  if (paren == std::string::npos) {
    return -1.0;
  }
  std::istringstream fields(stat.substr(paren + 1));
  std::string field;
  unsigned long long start_ticks = 0;
  for (int index = 3; index <= 22 && fields >> field; ++index) {
    if (index == 22) {
      start_ticks = std::strtoull(field.c_str(), nullptr, 10);
    }
  }
  const long ticks_per_second = sysconf(_SC_CLK_TCK);
  timespec boot{};
  if (start_ticks == 0 || ticks_per_second <= 0 || clock_gettime(CLOCK_BOOTTIME, &boot) != 0) {
    return -1.0;
  }
  const double now_ms = static_cast<double>(boot.tv_sec) * 1000.0 + static_cast<double>(boot.tv_nsec) / 1.0e6;
  const double age = now_ms - static_cast<double>(start_ticks) * 1000.0 / static_cast<double>(ticks_per_second);
  return age >= 0.0 ? age : -1.0;
#else
  return -1.0;
#endif
}

void push(const Event& event) noexcept {
  const auto slot = g_count.fetch_add(1, std::memory_order_relaxed);
  if (slot < kCapacity) {
    g_events[slot] = event;
  }
}

const char* kind_name(Kind kind) noexcept {
  switch (kind) {
    case Kind::Mark:
      return "mark";
    case Kind::Background:
      return "background";
    case Kind::Phase:
      break;
  }
  return "phase";
}

// Fixed three decimals with a '.' whatever the C locale says.
std::string number(double value) {
  char buffer[48] = {};
  std::snprintf(buffer, sizeof(buffer), "%.3f", value);
  for (auto& character : buffer) {
    if (character == ',') {
      character = '.';
    }
  }
  return buffer;
}

std::string json_string(std::string_view text) {
  std::string out = "\"";
  for (const char character : text) {
    const auto byte = static_cast<unsigned char>(character);
    if (character == '"' || character == '\\') {
      out += '\\';
      out += character;
    } else if (byte < 0x20) {
      char escaped[8] = {};
      std::snprintf(escaped, sizeof(escaped), "\\u%04x", byte);
      out += escaped;
    } else {
      out += character;
    }
  }
  out += '"';
  return out;
}

std::string padded(const std::string& text, std::size_t width) {
  return text.size() >= width ? text : std::string(width - text.size(), ' ') + text;
}

}  // namespace

void initialize(bool enable) noexcept {
  g_origin = std::chrono::steady_clock::now();
  const double age = os_process_age_ms();
  g_start_known = age >= 0.0;
  g_pre_main_ms = g_start_known ? age : 0.0;
  g_count.store(0, std::memory_order_relaxed);
  g_depth = 0;
  detail::g_enabled = enable;
}

void stop() noexcept { detail::g_enabled = false; }

double now_ms() noexcept {
  return g_pre_main_ms +
         std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - g_origin).count();
}

bool process_start_known() noexcept { return g_start_known; }

double pre_main_ms() noexcept { return g_pre_main_ms; }

void mark(const char* name) noexcept {
  if (!enabled()) {
    return;
  }
  const double at = now_ms();
  push(Event{name, at, at, g_depth, Kind::Mark});
}

void record_background(const char* name, double start_ms, double end_ms) noexcept {
  if (!enabled() || start_ms < 0.0) {
    return;
  }
  push(Event{name, start_ms, std::max(start_ms, end_ms), 0, Kind::Background});
}

Scope::Scope(const char* name) noexcept : name_(name) {
  if (!enabled()) {
    return;
  }
  open_ = true;
  depth_ = g_depth++;
  start_ms_ = now_ms();
}

void Scope::end() noexcept {
  if (!open_) {
    return;
  }
  open_ = false;
  g_depth = depth_;
  if (enabled()) {
    push(Event{name_, start_ms_, now_ms(), depth_, Kind::Phase});
  }
}

std::vector<Event> events() {
  const auto count = std::min(g_count.load(std::memory_order_relaxed), kCapacity);
  std::vector<Event> result;
  result.reserve(count + 1);
  if (g_start_known) {
    result.push_back(Event{kPreMainName, 0.0, g_pre_main_ms, 0, Kind::Phase});
  }
  result.insert(result.end(), g_events.begin(), g_events.begin() + static_cast<std::ptrdiff_t>(count));
  // Phases are stored when they END (children before parents); report them by start,
  // a parent before the children that start with it.
  std::stable_sort(result.begin(), result.end(), [](const Event& lhs, const Event& rhs) {
    if (lhs.start_ms != rhs.start_ms) {
      return lhs.start_ms < rhs.start_ms;
    }
    return lhs.depth < rhs.depth;
  });
  return result;
}

double mark_time(std::string_view name) {
  for (const auto& event : events()) {
    if (event.kind == Kind::Mark && name == event.name) {
      return event.start_ms;
    }
  }
  return -1.0;
}

std::string format_table() {
  const auto all = events();
  std::string out = g_start_known ? "Patchy startup trace (ms since process creation)\n"
                                  : "Patchy startup trace (ms since main(); process start unknown)\n";
  out += "    start       ms  phase\n";
  for (std::size_t index = 0; index < all.size(); ++index) {
    const auto& event = all[index];
    if (event.kind != Kind::Phase) {
      continue;
    }
    const auto indent = std::string(static_cast<std::size_t>(event.depth) * 2U, ' ');
    out += padded(number(event.start_ms), 9) + padded(number(event.end_ms - event.start_ms), 9) + "  " + indent +
           event.name + '\n';
    double children = 0.0;
    bool has_children = false;
    for (std::size_t child = index + 1; child < all.size(); ++child) {
      const auto& candidate = all[child];
      if (candidate.start_ms > event.end_ms) {
        break;
      }
      if (candidate.kind == Kind::Phase && candidate.depth == event.depth + 1) {
        children += candidate.end_ms - candidate.start_ms;
        has_children = true;
      }
    }
    if (has_children) {
      out += std::string(9, ' ') + padded(number(event.end_ms - event.start_ms - children), 9) + "  " + indent +
             "  (rest of " + event.name + ")\n";
    }
  }
  out += "marks:\n";
  for (const auto& event : all) {
    if (event.kind == Kind::Mark) {
      out += padded(number(event.start_ms), 9) + std::string(9, ' ') + "  " + event.name + '\n';
    }
  }
  bool background_header = false;
  for (const auto& event : all) {
    if (event.kind != Kind::Background) {
      continue;
    }
    if (!background_header) {
      out += "background (off the startup path):\n";
      background_header = true;
    }
    out += padded(number(event.start_ms), 9) + padded(number(event.end_ms - event.start_ms), 9) + "  " +
           event.name + '\n';
  }
  return out;
}

std::string format_json(const std::vector<std::pair<std::string, std::string>>& fields) {
  const auto all = events();
  double ready = -1.0;
  for (const auto& event : all) {
    if (event.kind == Kind::Mark && std::strcmp(event.name, "startup.idle") == 0) {
      ready = event.start_ms;
      break;
    }
  }
  std::string out = "{\n  \"format\": \"patchy-startup-report\",\n  \"version\": 1,\n";
  for (const auto& [key, value] : fields) {
    out += "  " + json_string(key) + ": " + json_string(value) + ",\n";
  }
  out += std::string("  \"process_start_known\": ") + (g_start_known ? "true" : "false") + ",\n";
  out += "  \"pre_main_ms\": " + number(g_pre_main_ms) + ",\n";
  out += "  \"ready_ms\": " + number(ready) + ",\n";
  out += "  \"events\": [";
  for (std::size_t index = 0; index < all.size(); ++index) {
    const auto& event = all[index];
    out += index == 0 ? "\n" : ",\n";
    out += "    {\"name\": " + json_string(event.name) + ", \"kind\": \"" + kind_name(event.kind) +
           "\", \"start_ms\": " + number(event.start_ms) +
           ", \"duration_ms\": " + number(event.end_ms - event.start_ms) +
           ", \"depth\": " + std::to_string(event.depth) + "}";
  }
  out += "\n  ]\n}\n";
  return out;
}

}  // namespace patchy::startup_trace
