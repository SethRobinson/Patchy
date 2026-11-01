// Windows: the plug-in gets its protocol pipes as C runtime descriptors 3 and 4 through
// the inherited-descriptor table in STARTUPINFO (cbReserved2/lpReserved2), the
// documented mechanism the C runtime uses to pass descriptors to a child; stdin is NUL
// and stdout/stderr go to one pipe the helper forwards. The process starts suspended,
// joins a kill-on-close job object, then runs.

#include "plugins/gimp/host/plugin_process.hpp"

#include "plugins/gimp/gimp_protocol.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <map>
#include <thread>

namespace patchy::gimp::host {

namespace {

std::wstring widen(const std::string& utf8) {
  if (utf8.empty()) {
    return {};
  }
  const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring out(static_cast<std::size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
  return out;
}

// Command-line quoting as the C runtime parses it back into argv.
void append_argument(std::wstring& line, const std::wstring& arg) {
  if (!line.empty()) {
    line.push_back(L' ');
  }
  if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) {
    line += arg;
    return;
  }
  line.push_back(L'"');
  std::size_t backslashes = 0;
  for (const wchar_t c : arg) {
    if (c == L'\\') {
      ++backslashes;
      continue;
    }
    line.append(c == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
    backslashes = 0;
    line.push_back(c);
  }
  line.append(backslashes * 2, L'\\');
  line.push_back(L'"');
}

std::wstring upper(std::wstring s) {
  for (auto& c : s) {
    c = static_cast<wchar_t>(std::towupper(c));
  }
  return s;
}

// The helper's own environment with the launch's changes, as a sorted block.
std::vector<wchar_t> environment_block(const link::LaunchSpec& launch) {
  std::map<std::wstring, std::pair<std::wstring, std::wstring>> vars;  // upper name -> (name, value)
  if (LPWCH env = GetEnvironmentStringsW()) {
    for (LPWCH p = env; *p; p += wcslen(p) + 1) {
      std::wstring entry(p);
      const auto eq = entry.find(L'=', 1);  // "=C:=C:\..." drive entries start with '='
      if (eq == std::wstring::npos) {
        continue;
      }
      const auto name = entry.substr(0, eq);
      vars[upper(name)] = {name, entry.substr(eq + 1)};
    }
    FreeEnvironmentStringsW(env);
  }
  for (const auto& name : launch.unset) {
    vars.erase(upper(widen(name)));
  }
  for (const auto& entry : launch.environment) {
    const auto wide = widen(entry);
    const auto eq = wide.find(L'=');
    if (eq == std::wstring::npos || eq == 0) {
      continue;
    }
    const auto name = wide.substr(0, eq);
    vars[upper(name)] = {name, wide.substr(eq + 1)};
  }
  std::vector<wchar_t> block;
  for (const auto& [key, entry] : vars) {
    (void)key;
    const auto line = entry.first + L"=" + entry.second;
    block.insert(block.end(), line.begin(), line.end());
    block.push_back(L'\0');
  }
  block.push_back(L'\0');
  return block;
}

std::string describe_exit(DWORD code) {
  switch (code) {
    case 0xC0000005: return "crashed (access violation)";
    case 0xC00000FD: return "crashed (stack overflow)";
    case 0xC0000409: return "crashed (stack buffer overrun or abort)";
    case 0xC0000135: return "could not start: a DLL it needs was not found";
    case 0xC0000139: return "could not start: a DLL entry point is missing (wrong GIMP version)";
    case 0xC000007B: return "could not start: a DLL has the wrong architecture";
    case 0xC0000142: return "could not start: a DLL failed to initialize";
    default: break;
  }
  if (code >= 0xC0000000) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "crashed (exception 0x%08lX)", static_cast<unsigned long>(code));
    return buffer;
  }
  return "exit code " + std::to_string(code);
}

class WinPluginProcess final : public PluginProcess {
public:
  ~WinPluginProcess() override {
    kill_tree();
    // The forwarder ends when the last writer of the output pipe is gone; it owns its
    // handle and must never hold up the helper's exit.
    if (output_thread_.joinable()) {
      output_thread_.detach();
    }
    for (HANDLE h : {to_plugin_, from_plugin_, process_, job_}) {
      if (h != nullptr && h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
      }
    }
  }

  std::ptrdiff_t read(std::uint8_t* data, std::size_t size) override {
    DWORD got = 0;
    if (!ReadFile(from_plugin_, data, static_cast<DWORD>(std::min<std::size_t>(size, 1u << 20)), &got, nullptr)) {
      return GetLastError() == ERROR_BROKEN_PIPE ? 0 : -1;
    }
    return static_cast<std::ptrdiff_t>(got);
  }

  bool write(const std::uint8_t* data, std::size_t size) override {
    while (size > 0) {
      DWORD wrote = 0;
      if (!WriteFile(to_plugin_, data, static_cast<DWORD>(std::min<std::size_t>(size, 1u << 20)), &wrote, nullptr) ||
          wrote == 0) {
        return false;
      }
      data += wrote;
      size -= wrote;
    }
    return true;
  }

  void kill_tree() override {
    if (job_ != nullptr) {
      TerminateJobObject(job_, 1);
    } else if (process_ != nullptr) {
      TerminateProcess(process_, 1);
    }
  }

  bool wait(int timeout_ms) override {
    return WaitForSingleObject(process_, timeout_ms < 0 ? INFINITE : static_cast<DWORD>(timeout_ms)) == WAIT_OBJECT_0;
  }

  int exit_code() const override {
    DWORD code = 0;
    GetExitCodeProcess(process_, &code);
    return static_cast<int>(code);
  }

  std::string exit_description() const override {
    DWORD code = 0;
    GetExitCodeProcess(process_, &code);
    return code == STILL_ACTIVE ? std::string("still running") : describe_exit(code);
  }

  std::uint64_t pid() const override { return pid_; }

  HANDLE to_plugin_{nullptr};
  HANDLE from_plugin_{nullptr};
  HANDLE process_{nullptr};
  HANDLE job_{nullptr};
  std::uint64_t pid_{0};
  std::thread output_thread_;
};

}  // namespace

std::unique_ptr<PluginProcess> start_plugin_process(const link::LaunchSpec& launch, const std::string& mode,
                                                    std::function<void(std::string)> output, std::string* error) {
  const auto fail = [error](std::string message) -> std::unique_ptr<PluginProcess> {
    if (error != nullptr) {
      *error = std::move(message);
    }
    return nullptr;
  };
  SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  HANDLE plugin_reads = nullptr;
  HANDLE host_writes = nullptr;
  HANDLE host_reads = nullptr;
  HANDLE plugin_writes = nullptr;
  HANDLE output_read = nullptr;
  HANDLE output_write = nullptr;
  if (!CreatePipe(&plugin_reads, &host_writes, &inherit, 1 << 16) ||
      !CreatePipe(&host_reads, &plugin_writes, &inherit, 1 << 16) ||
      !CreatePipe(&output_read, &output_write, &inherit, 1 << 12)) {
    return fail("could not create the plug-in's pipes");
  }
  for (HANDLE own : {host_writes, host_reads, output_read}) {
    SetHandleInformation(own, HANDLE_FLAG_INHERIT, 0);
  }
  HANDLE null_input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr);

  // The C runtime's inherited-descriptor table: an int count, one flag byte per
  // descriptor, then one HANDLE per descriptor. Flags: 0x01 open, 0x08 pipe, 0x40 device.
  constexpr int kCount = 5;
  std::vector<std::uint8_t> table(sizeof(int) + kCount + kCount * sizeof(HANDLE));
  const int count = kCount;
  std::memcpy(table.data(), &count, sizeof count);
  const std::uint8_t flags[kCount] = {0x41, 0x09, 0x09, 0x09, 0x09};
  std::memcpy(table.data() + sizeof(int), flags, kCount);
  const HANDLE handles[kCount] = {null_input, output_write, output_write, plugin_reads, plugin_writes};
  std::memcpy(table.data() + sizeof(int) + kCount, handles, sizeof handles);

  std::wstring command_line;
  const auto program = widen(launch.program);
  append_argument(command_line, program);
  for (const auto& arg : launch.leading_args) {
    append_argument(command_line, widen(arg));
  }
  append_argument(command_line, L"-gimp");
  append_argument(command_line, std::to_wstring(kProtocolVersion));
  append_argument(command_line, L"3");
  append_argument(command_line, L"4");
  append_argument(command_line, widen(mode));
  append_argument(command_line, L"0");  // stack-trace mode: never

  auto env = environment_block(launch);
  STARTUPINFOW si{};
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = null_input;
  si.hStdOutput = output_write;
  si.hStdError = output_write;
  si.cbReserved2 = static_cast<WORD>(table.size());
  si.lpReserved2 = table.data();
  PROCESS_INFORMATION pi{};
  const auto working_dir = widen(launch.working_dir);
  std::vector<wchar_t> mutable_line(command_line.begin(), command_line.end());
  mutable_line.push_back(L'\0');
  const BOOL started = CreateProcessW(program.c_str(), mutable_line.data(), nullptr, nullptr, TRUE,
                                      CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW, env.data(),
                                      working_dir.empty() ? nullptr : working_dir.c_str(), &si, &pi);
  const DWORD start_error = started ? 0 : GetLastError();
  for (HANDLE child_side : {plugin_reads, plugin_writes, output_write, null_input}) {
    if (child_side != nullptr && child_side != INVALID_HANDLE_VALUE) {
      CloseHandle(child_side);
    }
  }
  if (!started) {
    CloseHandle(host_writes);
    CloseHandle(host_reads);
    CloseHandle(output_read);
    return fail(start_error == ERROR_FILE_NOT_FOUND || start_error == ERROR_PATH_NOT_FOUND
                    ? "the plug-in program was not found"
                    : "Windows could not start the plug-in (error " + std::to_string(start_error) + ")");
  }
  auto process = std::make_unique<WinPluginProcess>();
  process->to_plugin_ = host_writes;
  process->from_plugin_ = host_reads;
  process->process_ = pi.hProcess;
  process->pid_ = pi.dwProcessId;
  process->job_ = CreateJobObjectW(nullptr, nullptr);
  if (process->job_ != nullptr) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(process->job_, JobObjectExtendedLimitInformation, &limits, sizeof limits);
    if (!AssignProcessToJobObject(process->job_, pi.hProcess)) {
      CloseHandle(process->job_);
      process->job_ = nullptr;
    }
  }
  ResumeThread(pi.hThread);
  CloseHandle(pi.hThread);
  process->output_thread_ = std::thread([output_read, output = std::move(output)] {
    char buffer[4096];
    for (;;) {
      DWORD got = 0;
      if (!ReadFile(output_read, buffer, sizeof buffer, &got, nullptr) || got == 0) {
        break;
      }
      if (output) {
        output(std::string(buffer, got));
      }
    }
    CloseHandle(output_read);
  });
  return process;
}

}  // namespace patchy::gimp::host
