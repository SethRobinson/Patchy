#include "ui/memory_info.hpp"

#include <QFile>
#include <QString>

#include <algorithm>

#ifdef Q_OS_WIN
#define NOMINMAX
#define PSAPI_VERSION 2
#include <windows.h>

#include <psapi.h>
#endif
#ifdef Q_OS_LINUX
#include <unistd.h>
#endif
#ifdef Q_OS_MACOS
#include <mach/mach.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#endif
#ifdef Q_OS_WASM
#include <emscripten.h>
#include <emscripten/heap.h>
#endif

namespace patchy::ui {

qint64 total_physical_ram_mb() {
#ifdef Q_OS_WIN
  MEMORYSTATUSEX status{};
  status.dwLength = sizeof(status);
  if (GlobalMemoryStatusEx(&status) != 0) {
    return static_cast<qint64>(status.ullTotalPhys / (1024ULL * 1024ULL));
  }
#endif
#ifdef Q_OS_LINUX
  const auto pages = sysconf(_SC_PHYS_PAGES);
  const auto page_size = sysconf(_SC_PAGE_SIZE);
  if (pages > 0 && page_size > 0) {
    return static_cast<qint64>((static_cast<long long>(pages) * page_size) / (1024LL * 1024LL));
  }
#endif
#ifdef Q_OS_MACOS
  std::uint64_t bytes = 0;
  std::size_t size = sizeof(bytes);
  if (sysctlbyname("hw.memsize", &bytes, &size, nullptr, 0) == 0) {
    return static_cast<qint64>(bytes / (1024ULL * 1024ULL));
  }
#endif
  return -1;
}

qint64 current_process_memory_mb() {
#ifdef Q_OS_WIN
  PROCESS_MEMORY_COUNTERS counters{};
  if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != 0) {
    return static_cast<qint64>(counters.WorkingSetSize / (1024ULL * 1024ULL));
  }
#endif
#ifdef Q_OS_LINUX
  QFile status(QStringLiteral("/proc/self/status"));
  if (status.open(QIODevice::ReadOnly | QIODevice::Text)) {
    while (!status.atEnd()) {
      const auto line = QString::fromUtf8(status.readLine());
      if (line.startsWith(QStringLiteral("VmRSS:"))) {
        const auto parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() >= 2) {
          return parts[1].toLongLong() / 1024;
        }
      }
    }
  }
#endif
#ifdef Q_OS_MACOS
  mach_task_basic_info info{};
  mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info),
                &count) == KERN_SUCCESS) {
    return static_cast<qint64>(info.resident_size / (1024ULL * 1024ULL));
  }
#endif
#ifdef Q_OS_WASM
  return static_cast<qint64>(emscripten_get_heap_size() / (1024ULL * 1024ULL));
#endif
  return -1;
}

qint64 peak_process_memory_mb() {
#ifdef Q_OS_WIN
  PROCESS_MEMORY_COUNTERS counters{};
  if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != 0) {
    return static_cast<qint64>(counters.PeakWorkingSetSize / (1024ULL * 1024ULL));
  }
#endif
#ifdef Q_OS_LINUX
  QFile status(QStringLiteral("/proc/self/status"));
  if (status.open(QIODevice::ReadOnly | QIODevice::Text)) {
    while (!status.atEnd()) {
      const auto line = QString::fromUtf8(status.readLine());
      if (line.startsWith(QStringLiteral("VmHWM:"))) {
        const auto parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() >= 2) {
          return parts[1].toLongLong() / 1024;
        }
      }
    }
  }
#endif
#ifdef Q_OS_MACOS
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) == 0) {
    return static_cast<qint64>(usage.ru_maxrss / (1024LL * 1024LL));
  }
#endif
  return -1;
}

qint64 wasm_heap_limit_mb() {
#ifdef Q_OS_WASM
  // The About dialog and every other caller run on the main thread, so this
  // reads the page's globalThis (workers would see their own scope). The
  // fallback is Emscripten's baked MAXIMUM_MEMORY, which overstates the real
  // ceiling whenever the shell page constructed a smaller memory.
  const double page_bytes = EM_ASM_DOUBLE({
    var value = globalThis.patchyWasmMemoryMaximumBytes;
    return (typeof value === 'number' && value > 0) ? value : 0;
  });
  if (page_bytes > 0) {
    return static_cast<qint64>(page_bytes / (1024.0 * 1024.0));
  }
  return static_cast<qint64>(emscripten_get_heap_max() / (1024ULL * 1024ULL));
#else
  return -1;
#endif
}

std::size_t history_memory_budget_bytes() {
  // Test hook, read every call so suites can toggle it at runtime. "0" is a
  // meaningful value (forces eviction down to the per-session floor).
  if (qEnvironmentVariableIsSet("PATCHY_HISTORY_BUDGET_TEST_MB")) {
    const qint64 mb = std::clamp<qint64>(
        qEnvironmentVariableIntValue("PATCHY_HISTORY_BUDGET_TEST_MB"), 0, 2000);
    return static_cast<std::size_t>(mb) * 1024U * 1024U;
  }
#ifdef Q_OS_WASM
  // Wasm linear memory never shrinks, so history is the main heap ratchet;
  // Safari's tab budgets (roughly 1-1.5 GB on desktop, less on iPhone) leave
  // little headroom above the working set. Keep history small.
  return 256U * 1024U * 1024U;
#else
  static const std::size_t default_budget = [] {
    const qint64 ram_mb = total_physical_ram_mb();
    const qint64 budget_mb = ram_mb > 0 ? std::clamp<qint64>(ram_mb / 4, 512, 4096) : 1024;
    return static_cast<std::size_t>(budget_mb) * 1024ULL * 1024ULL;
  }();
  return default_budget;
#endif
}

}  // namespace patchy::ui
