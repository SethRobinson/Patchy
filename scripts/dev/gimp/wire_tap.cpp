// gimp-wire-tap: a development tool for the GIMP 3 plug-in host (docs/gimp-plugins.md).
//
// It stands in for a GIMP 3 plug-in: GIMP (or Patchy) starts it in the plug-in's place,
// it records how it was started (arguments, environment, inherited descriptors), starts
// the real plug-in the same way over a fresh pair of pipes and relays the traffic in both
// directions, writing every chunk to a binary log. scripts/dev/gimp/wire_dump.py prints
// the log. Observed traffic is one of the allowed sources in docs/legal-licensing.md
// ("GIMP 3 plug-in hosting"); the tool reads and writes bytes only and links nothing of
// GIMP. Windows only (the build machines with GIMP installed); never shipped.
//
// Setup: copy the tap to <dir>/<plug-in name>/<plug-in name>.exe and write
// <plug-in name>.target beside it: first line the real program, every further line one
// argument put before the host's own (a script path for Python plug-ins). Logs go to
// %PATCHY_GIMP_TAP_LOG% (default %TEMP%\patchy-gimp-tap), <name>-<pid>.txt and .bin.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <fcntl.h>
#include <io.h>
#include <process.h>

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

std::mutex g_log_mutex;
FILE* g_text = nullptr;
FILE* g_bin = nullptr;
LARGE_INTEGER g_start{};
LARGE_INTEGER g_freq{};

void text(const wchar_t* format, ...) {
  if (!g_text) return;
  std::lock_guard<std::mutex> lock(g_log_mutex);
  va_list args;
  va_start(args, format);
  vfwprintf(g_text, format, args);
  va_end(args);
  fflush(g_text);
}

// Binary record: u8 direction ('H' host to plug-in, 'P' plug-in to host), u32 length,
// u64 microseconds since start, then the bytes. All little-endian.
void record(char direction, const unsigned char* data, uint32_t length) {
  if (!g_bin) return;
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  const uint64_t us = static_cast<uint64_t>((now.QuadPart - g_start.QuadPart) * 1000000 / g_freq.QuadPart);
  std::lock_guard<std::mutex> lock(g_log_mutex);
  fputc(direction, g_bin);
  fwrite(&length, 4, 1, g_bin);
  fwrite(&us, 8, 1, g_bin);
  fwrite(data, 1, length, g_bin);
  fflush(g_bin);
}

bool is_number(const wchar_t* s) {
  if (!s || !*s) return false;
  for (const wchar_t* p = s; *p; ++p)
    if (*p < L'0' || *p > L'9') return false;
  return true;
}

void relay(int from, int to, char direction, std::atomic<bool>* done) {
  std::vector<unsigned char> buffer(1 << 16);
  for (;;) {
    const int got = _read(from, buffer.data(), static_cast<unsigned>(buffer.size()));
    if (got <= 0) break;
    record(direction, buffer.data(), static_cast<uint32_t>(got));
    int offset = 0;
    while (offset < got) {
      const int wrote = _write(to, buffer.data() + offset, static_cast<unsigned>(got - offset));
      if (wrote <= 0) {
        text(L"relay %c: write failed\n", direction);
        done->store(true);
        return;
      }
      offset += wrote;
    }
  }
  text(L"relay %c: end of stream\n", direction);
  _close(to);
  done->store(true);
}

std::wstring quote(const std::wstring& arg) {
  if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) return arg;
  std::wstring out = L"\"";
  size_t backslashes = 0;
  for (wchar_t c : arg) {
    if (c == L'\\') {
      ++backslashes;
      continue;
    }
    if (c == L'"') out.append(backslashes * 2 + 1, L'\\');
    else out.append(backslashes, L'\\');
    backslashes = 0;
    out.push_back(c);
  }
  out.append(backslashes * 2, L'\\');
  out.push_back(L'"');
  return out;
}

void ignore_invalid_parameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) {}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  // Probing a number that is not an open descriptor must fail quietly, not abort.
  _set_invalid_parameter_handler(ignore_invalid_parameter);
  QueryPerformanceFrequency(&g_freq);
  QueryPerformanceCounter(&g_start);

  wchar_t module_path[MAX_PATH * 4];
  GetModuleFileNameW(nullptr, module_path, static_cast<DWORD>(std::size(module_path)));
  const std::filesystem::path self(module_path);
  const std::wstring stem = self.stem().wstring();

  std::filesystem::path log_dir;
  if (const wchar_t* env = _wgetenv(L"PATCHY_GIMP_TAP_LOG"); env && *env) log_dir = env;
  else log_dir = std::filesystem::temp_directory_path() / L"patchy-gimp-tap";
  std::error_code ec;
  std::filesystem::create_directories(log_dir, ec);
  const std::wstring base = stem + L"-" + std::to_wstring(GetCurrentProcessId());
  g_text = _wfopen((log_dir / (base + L".txt")).c_str(), L"w, ccs=UTF-8");
  g_bin = _wfopen((log_dir / (base + L".bin")).c_str(), L"wb");

  text(L"tap: %ls\n", module_path);
  for (int i = 0; i < argc; ++i) text(L"argv[%d] = %ls\n", i, argv[i]);
  wchar_t cwd[MAX_PATH * 4];
  GetCurrentDirectoryW(static_cast<DWORD>(std::size(cwd)), cwd);
  text(L"cwd = %ls\n", cwd);
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  GetStartupInfoW(&si);
  text(L"startup cbReserved2 = %u, stdin=%p stdout=%p stderr=%p flags=0x%x\n", si.cbReserved2,
       si.hStdInput, si.hStdOutput, si.hStdError, si.dwFlags);
  for (int i = 1; i < argc; ++i) {
    if (!is_number(argv[i])) continue;
    const int fd = _wtoi(argv[i]);
    const intptr_t handle = _get_osfhandle(fd);
    DWORD type = handle == -1 ? 0 : GetFileType(reinterpret_cast<HANDLE>(handle));
    DWORD flags = 0;
    const BOOL direct = GetHandleInformation(reinterpret_cast<HANDLE>(static_cast<intptr_t>(fd)), &flags);
    text(L"argv[%d] as CRT fd: handle=%lld type=%lu; as raw HANDLE: valid=%d\n", i,
         static_cast<long long>(handle), type, direct ? 1 : 0);
  }
  if (LPWCH env = GetEnvironmentStringsW()) {
    for (LPWCH p = env; *p; p += wcslen(p) + 1) text(L"env %ls\n", p);
    FreeEnvironmentStringsW(env);
  }

  // The real plug-in to start.
  std::vector<std::wstring> target;
  {
    std::wifstream in(self.parent_path() / (stem + L".target"));
    std::wstring line;
    while (std::getline(in, line)) {
      if (!line.empty() && line.back() == L'\r') line.pop_back();
      if (!line.empty()) target.push_back(line);
    }
  }
  if (target.empty()) {
    text(L"no %ls.target: logging only\n", stem.c_str());
    return 1;
  }

  // The host's descriptors: "-gimp <protocol> <read fd> <write fd> ..." (as observed).
  int gimp_at = -1;
  for (int i = 1; i + 3 < argc; ++i)
    if (wcscmp(argv[i], L"-gimp") == 0) {
      gimp_at = i;
      break;
    }
  if (gimp_at < 0 || !is_number(argv[gimp_at + 2]) || !is_number(argv[gimp_at + 3])) {
    text(L"no -gimp descriptors: nothing to relay\n");
    return 1;
  }
  const int host_read = _wtoi(argv[gimp_at + 2]);
  const int host_write = _wtoi(argv[gimp_at + 3]);

  // Plug-in side pipes: the tap keeps non-inherited ends, the child gets inherited copies.
  int to_plugin[2];
  int from_plugin[2];
  if (_pipe(to_plugin, 1 << 16, _O_BINARY | _O_NOINHERIT) != 0 ||
      _pipe(from_plugin, 1 << 16, _O_BINARY | _O_NOINHERIT) != 0) {
    text(L"pipe failed\n");
    return 1;
  }
  const int child_read = _dup(to_plugin[0]);
  const int child_write = _dup(from_plugin[1]);
  _close(to_plugin[0]);
  _close(from_plugin[1]);
  SetHandleInformation(reinterpret_cast<HANDLE>(_get_osfhandle(host_read)), HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(reinterpret_cast<HANDLE>(_get_osfhandle(host_write)), HANDLE_FLAG_INHERIT, 0);

  std::vector<std::wstring> args = target;
  for (int i = 1; i < argc; ++i) {
    if (i == gimp_at + 2) args.push_back(std::to_wstring(child_read));
    else if (i == gimp_at + 3) args.push_back(std::to_wstring(child_write));
    else args.push_back(argv[i]);
  }
  std::vector<std::wstring> quoted;
  for (const auto& a : args) quoted.push_back(quote(a));
  std::vector<const wchar_t*> raw;
  for (const auto& a : quoted) raw.push_back(a.c_str());
  raw.push_back(nullptr);
  for (size_t i = 0; i < args.size(); ++i) text(L"child argv[%zu] = %ls\n", i, args[i].c_str());

  const intptr_t child = _wspawnv(_P_NOWAIT, target[0].c_str(), raw.data());
  _close(child_read);
  _close(child_write);
  if (child == -1) {
    text(L"spawn failed: errno %d\n", errno);
    return 1;
  }
  std::atomic<bool> host_done{false};
  std::atomic<bool> plugin_done{false};
  std::thread down(relay, host_read, to_plugin[1], 'H', &host_done);
  std::thread up(relay, from_plugin[0], host_write, 'P', &plugin_done);
  int status = 0;
  _cwait(&status, child, 0);
  text(L"child exited: %d\n", status);
  up.join();
  // The host's side stays open until GIMP closes it; do not wait for it.
  down.detach();
  if (g_text) fclose(g_text);
  if (g_bin) fclose(g_bin);
  _exit(status);
}
