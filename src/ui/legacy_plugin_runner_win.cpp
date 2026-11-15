#include "ui/legacy_plugin_runner.hpp"

#include "plugins/host/host_protocol.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QUuid>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <vector>

namespace patchy::ui {

namespace {

using namespace patchy::legacy_host;

// Frames arriving on the socket, split from the byte stream.
struct FrameReader {
  QByteArray buffer;
  bool next(std::uint32_t& type, std::vector<std::uint8_t>& payload) {
    if (buffer.size() < 8) {
      return false;
    }
    Reader header(reinterpret_cast<const std::uint8_t*>(buffer.constData()), 8);
    const auto frame_type = header.u32();
    const auto length = header.u32();
    if (static_cast<std::size_t>(buffer.size()) < 8U + length) {
      return false;
    }
    type = frame_type;
    payload.assign(buffer.constData() + 8, buffer.constData() + 8 + length);
    buffer.remove(0, static_cast<int>(8U + length));
    return true;
  }
};

void send_frame(QLocalSocket& socket, std::uint32_t type, const std::vector<std::uint8_t>& payload) {
  const auto bytes = frame_message(type, payload);
  socket.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size()));
  socket.flush();
}

// Auto-accepting a dialog the plug-in opened although none was asked for: the
// helper's visible top-level windows are answered through their OK button (or
// the IDOK command when no such button exists) once they have been up for a
// moment, and again every second while they stay.
struct WindowScan {
  DWORD pid{0};
  std::vector<HWND> windows;
  // Standard dialogs (class #32770) the helper owns that Windows reports as not
  // visible; see dialogs_can_be_hidden().
  std::vector<HWND> hidden_dialogs;
};

BOOL CALLBACK collect_helper_windows(HWND hwnd, LPARAM lparam) {
  auto* scan = reinterpret_cast<WindowScan*>(lparam);
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid != scan->pid) {
    return TRUE;
  }
  if (IsWindowVisible(hwnd)) {
    scan->windows.push_back(hwnd);
    return TRUE;
  }
  wchar_t class_name[16] = {};
  GetClassNameW(hwnd, class_name, 16);
  RECT rect{};
  if (std::wcscmp(class_name, L"#32770") == 0 && GetWindowRect(hwnd, &rect) && rect.right > rect.left &&
      rect.bottom > rect.top) {
    scan->hidden_dialogs.push_back(hwnd);
  }
  return TRUE;
}

// True when Patchy runs on a non-interactive window station: a service, or an ssh
// session (session 0, "Service-0x0-...$"). There a plug-in's modal dialog is created
// and runs its message loop, but Windows never reports it visible, so an unattended
// run that only answered visible windows waited on it forever (October 2026, the
// Windows offload host). On the interactive station (WinSta0) a hidden dialog is one
// the plug-in has not shown yet and is left alone.
bool dialogs_can_be_hidden() {
  static const bool hidden = [] {
    wchar_t name[64] = {};
    DWORD needed = 0;
    return GetUserObjectInformationW(GetProcessWindowStation(), UOI_NAME, name, sizeof(name), &needed) != FALSE &&
           _wcsicmp(name, L"WinSta0") != 0;
  }();
  return hidden;
}

BOOL CALLBACK find_ok_button(HWND hwnd, LPARAM lparam) {
  wchar_t class_name[32] = {};
  GetClassNameW(hwnd, class_name, 32);
  if (_wcsicmp(class_name, L"Button") != 0) {
    return TRUE;
  }
  wchar_t text[64] = {};
  GetWindowTextW(hwnd, text, 64);
  std::wstring caption;
  for (const wchar_t* c = text; *c != 0; ++c) {
    if (*c != L'&') {
      caption.push_back(*c);
    }
  }
  if (_wcsicmp(caption.c_str(), L"OK") == 0) {
    *reinterpret_cast<HWND*>(lparam) = hwnd;
    return FALSE;
  }
  return TRUE;
}

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

// Measures and renders in the window's own DPI context for as long as it lives. The
// plug-in helper is not DPI aware: on a scaled display Windows stretches its windows,
// PrintWindow still renders them at their own size, and GetWindowRect answers in the
// caller's (Patchy's, per-monitor) pixels. At 150% that made the bitmap half again as
// large as the picture drawn into its corner.
class WindowDpiContextScope {
public:
  explicit WindowDpiContextScope(HWND hwnd)
      : previous_(SetThreadDpiAwarenessContext(GetWindowDpiAwarenessContext(hwnd))) {}
  ~WindowDpiContextScope() {
    if (previous_ != nullptr) {
      SetThreadDpiAwarenessContext(previous_);
    }
  }
  WindowDpiContextScope(const WindowDpiContextScope&) = delete;
  WindowDpiContextScope& operator=(const WindowDpiContextScope&) = delete;

private:
  DPI_AWARENESS_CONTEXT previous_;
};

// Saves a top-level window of another process to a PNG (its client and
// non-client area, as PrintWindow renders it).
bool capture_window_to_png(HWND hwnd, const QString& path) {
  const WindowDpiContextScope window_pixels(hwnd);
  RECT rect{};
  if (!GetWindowRect(hwnd, &rect)) {
    return false;
  }
  const int width = rect.right - rect.left;
  const int height = rect.bottom - rect.top;
  if (width <= 0 || height <= 0) {
    return false;
  }
  HDC screen = GetDC(nullptr);
  HDC memory = CreateCompatibleDC(screen);
  HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
  auto* previous = SelectObject(memory, bitmap);
  bool ok = PrintWindow(hwnd, memory, PW_RENDERFULLCONTENT) != 0;
  QImage image(width, height, QImage::Format_ARGB32);
  if (ok) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;  // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    ok = GetDIBits(memory, bitmap, 0, static_cast<UINT>(height), image.bits(), &info, DIB_RGB_COLORS) ==
         height;
  }
  SelectObject(memory, previous);
  DeleteObject(bitmap);
  DeleteDC(memory);
  ReleaseDC(nullptr, screen);
  if (!ok) {
    return false;
  }
  // GDI leaves the alpha channel undefined; the window is opaque.
  return image.convertToFormat(QImage::Format_RGB32).save(path, "PNG");
}

// PATCHY_8BF_TRACE=1 also lists the helper's windows here; PATCHY_8BF_DIALOG_SETTLE_MS
// (default 700) is how long a new window is left alone before the first capture or answer.
bool trace_enabled() {
  static const bool enabled = qEnvironmentVariableIntValue("PATCHY_8BF_TRACE") == 1;
  return enabled;
}

qint64 settle_ms() {
  static const qint64 value = [] {
    bool ok = false;
    const int parsed = qEnvironmentVariableIntValue("PATCHY_8BF_DIALOG_SETTLE_MS", &ok);
    return ok && parsed > 0 ? static_cast<qint64>(parsed) : 700;
  }();
  return value;
}

struct DialogAcceptor {
  DWORD pid{0};
  bool accept{false};
  QString capture_path;
  bool captured{false};
  // True once every answer was tried on a window that is still up: the
  // caller ends the run as cancelled instead of waiting forever.
  bool gave_up{false};
  struct Seen {
    qint64 settled_at_ms{0};    // clock time from which the window counts as built
    qint64 next_action_ms{0};   // clock time of the next answer attempt
    int attempts{0};
  };
  std::map<HWND, Seen> first_seen;  // window -> schedule
  QElapsedTimer clock;

  void poll() {
    if (pid == 0) {
      return;
    }
    if (!clock.isValid()) {
      clock.start();
    }
    WindowScan scan;
    scan.pid = pid;
    EnumWindows(collect_helper_windows, reinterpret_cast<LPARAM>(&scan));
    const auto now = clock.elapsed();
    // Off the interactive window station the dialog never counts as visible, so the
    // helper's standard dialogs are answered as if they were.
    const bool use_hidden = scan.windows.empty() && dialogs_can_be_hidden();
    const auto& candidates = use_hidden ? scan.hidden_dialogs : scan.windows;
    // Plug-in UIs can start as a small stub and grow into a full-screen
    // canvas a moment later (KPT's MetaOS); the largest visible window is the
    // dialog, and only it is captured and answered.
    HWND largest = nullptr;
    long largest_area = 0;
    for (HWND hwnd : candidates) {
      RECT rect{};
      GetWindowRect(hwnd, &rect);
      const long area = static_cast<long>(rect.right - rect.left) * static_cast<long>(rect.bottom - rect.top);
      if (first_seen.find(hwnd) == first_seen.end()) {
        first_seen.emplace(hwnd, Seen{now + settle_ms(), 0, 0});
        if (trace_enabled()) {
          wchar_t class_name[64] = {};
          wchar_t title[128] = {};
          GetClassNameW(hwnd, class_name, 64);
          GetWindowTextW(hwnd, title, 128);
          std::fprintf(stderr, "[8bf] window %p class '%ls' title '%ls' %ldx%ld%s\n", static_cast<void*>(hwnd),
                       class_name, title, static_cast<long>(rect.right - rect.left),
                       static_cast<long>(rect.bottom - rect.top), use_hidden ? " (not visible)" : "");
          std::fflush(stderr);
        }
      }
      if (area > largest_area) {
        largest_area = area;
        largest = hwnd;
      }
    }
    if (largest == nullptr) {
      return;
    }
    // A full-screen canvas lives inside the helper's movable frame
    // ("<plug-in> via Patchy", class PatchyPluginFrame): the frame is the
    // largest window and gets captured, but the plug-in's own window inside
    // it is what answers clicks and keys.
    HWND target = largest;
    {
      wchar_t class_name[64] = {};
      GetClassNameW(largest, class_name, 64);
      if (std::wcscmp(class_name, L"PatchyPluginFrame") == 0) {
        if (HWND canvas = GetWindow(largest, GW_CHILD); canvas != nullptr) {
          target = canvas;
        }
      }
    }
    auto& seen = first_seen[largest];
    if (now < seen.settled_at_ms) {
      return;
    }
    // A window Windows does not show has nothing to capture.
    if (!captured && !capture_path.isEmpty() && !use_hidden) {
      captured = capture_window_to_png(largest, capture_path);
    }
    if (!accept || now < seen.next_action_ms) {
      return;
    }
    // Escalate while the window stays: a standard OK button, the IDOK
    // command, the Enter key (custom UIs such as KPT's), then a close
    // request, which makes the plug-in return "cancelled" instead of
    // holding an unattended run forever.
    const int attempt = seen.attempts++;
    if (attempt >= 8) {
      gave_up = true;
      return;
    }
    HWND ok_button = nullptr;
    EnumChildWindows(target, find_ok_button, reinterpret_cast<LPARAM>(&ok_button));
    // PATCHY_8BF_ACCEPT_CLICK=dx,dy (developer knob): press the window at that
    // offset from its bottom-right corner first, for canvases whose OK mark
    // lives there (KPT's MetaOS draws its checkmark in the corner).
    static const QString click_offset = qEnvironmentVariable("PATCHY_8BF_ACCEPT_CLICK");
    if (!click_offset.isEmpty() && attempt < 2) {
      const auto parts = click_offset.split(QLatin1Char(','));
      RECT rect{};
      GetClientRect(target, &rect);
      const int x = rect.right - (parts.size() > 0 ? parts[0].toInt() : 30);
      const int y = rect.bottom - (parts.size() > 1 ? parts[1].toInt() : 30);
      const LPARAM at = MAKELPARAM(x, y);
      PostMessageW(target, WM_MOUSEMOVE, 0, at);
      PostMessageW(target, WM_LBUTTONDOWN, MK_LBUTTON, at);
      PostMessageW(target, WM_LBUTTONUP, 0, at);
    } else if (ok_button != nullptr && attempt < 2) {
      PostMessageW(ok_button, BM_CLICK, 0, 0);
    } else if (attempt < 2) {
      PostMessageW(target, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
    } else if (attempt < 4) {
      PostMessageW(target, WM_KEYDOWN, VK_RETURN, 0x001C0001);
      PostMessageW(target, WM_KEYUP, VK_RETURN, 0xC01C0001);
    } else if (attempt < 6) {
      PostMessageW(target, WM_CLOSE, 0, 0);
    } else {
      PostMessageW(target, WM_SYSCOMMAND, SC_CLOSE, 0);
    }
    seen.next_action_ms = now + 1500;
  }
};

struct MappingHandle {
  HANDLE handle{nullptr};
  std::uint8_t* view{nullptr};
  ~MappingHandle() {
    if (view != nullptr) {
      UnmapViewOfFile(view);
    }
    if (handle != nullptr) {
      CloseHandle(handle);
    }
  }
};

}  // namespace

LegacyPluginProcessTree::LegacyPluginProcessTree() : job_(CreateJobObjectW(nullptr, nullptr)) {}

LegacyPluginProcessTree::~LegacyPluginProcessTree() {
  if (job_ != nullptr) {
    CloseHandle(static_cast<HANDLE>(job_));
  }
}

bool LegacyPluginProcessTree::assign(qint64 process_id) {
  if (job_ == nullptr || process_id <= 0) {
    return false;
  }
  HANDLE process = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, static_cast<DWORD>(process_id));
  if (process == nullptr) {
    return false;
  }
  const bool assigned = AssignProcessToJobObject(static_cast<HANDLE>(job_), process) != FALSE;
  CloseHandle(process);
  return assigned;
}

std::vector<qint64> LegacyPluginProcessTree::process_ids() const {
  std::vector<qint64> ids;
  if (job_ == nullptr) {
    return ids;
  }
  // The list is a count followed by that many ids; 256 is far more than a
  // plug-in starts, and a longer list only truncates this report.
  constexpr DWORD kMaxProcesses = 256;
  std::vector<std::uint8_t> buffer(sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST) + kMaxProcesses * sizeof(ULONG_PTR));
  auto* list = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(buffer.data());
  if (QueryInformationJobObject(static_cast<HANDLE>(job_), JobObjectBasicProcessIdList, list,
                                static_cast<DWORD>(buffer.size()), nullptr) == FALSE &&
      GetLastError() != ERROR_MORE_DATA) {
    return ids;
  }
  for (DWORD index = 0; index < list->NumberOfProcessIdsInList; ++index) {
    ids.push_back(static_cast<qint64>(list->ProcessIdList[index]));
  }
  return ids;
}

void LegacyPluginProcessTree::terminate() {
  if (job_ != nullptr) {
    TerminateJobObject(static_cast<HANDLE>(job_), 1);
  }
}

QString legacy_plugin_helper_path(const std::string& architecture) {
  const auto name = architecture == "x86" ? QStringLiteral("patchy-8bf-host32.exe") : QStringLiteral("patchy-8bf-host64.exe");
  return QDir(QCoreApplication::applicationDirPath()).filePath(name);
}

LegacyPluginRunResult run_legacy_plugin_out_of_process(const LegacyPluginRunInput& input,
                                                       const LegacyPluginRunCallbacks& callbacks) {
  LegacyPluginRunResult result;
  if (input.input == nullptr || input.output == nullptr || input.width <= 0 || input.height <= 0 ||
      (input.planes != 3 && input.planes != 4)) {
    result.message = QCoreApplication::translate("LegacyPluginRunner", "The plug-in host was given an invalid image.");
    return result;
  }
  const std::size_t pixel_bytes = static_cast<std::size_t>(input.width) * static_cast<std::size_t>(input.height) *
                                  static_cast<std::size_t>(input.planes);
  const std::size_t mask_bytes =
      input.mask != nullptr ? static_cast<std::size_t>(input.width) * static_cast<std::size_t>(input.height) : 0U;
  if (input.input->size() != pixel_bytes || input.output->size() != pixel_bytes ||
      (input.mask != nullptr && input.mask->size() != mask_bytes)) {
    result.message = QCoreApplication::translate("LegacyPluginRunner", "The plug-in host was given an invalid image.");
    return result;
  }
  if (!QFileInfo::exists(input.helper_executable)) {
    result.message = QCoreApplication::translate("LegacyPluginRunner", "The plug-in host program is missing: %1").arg(QDir::toNativeSeparators(input.helper_executable));
    return result;
  }

  const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const QString pipe_name = QStringLiteral("patchy-8bf-") + token;
  const std::wstring mapping_name = (QStringLiteral("Local\\patchy-8bf-") + token).toStdWString();

  // The pixel exchange area: input, output (pre-filled with the input) and mask.
  const std::uint64_t mapping_size = static_cast<std::uint64_t>(pixel_bytes) * 2U + mask_bytes;
  MappingHandle mapping;
  mapping.handle = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                      static_cast<DWORD>(mapping_size >> 32U),
                                      static_cast<DWORD>(mapping_size & 0xFFFFFFFFU), mapping_name.c_str());
  if (mapping.handle == nullptr) {
    result.message = QCoreApplication::translate("LegacyPluginRunner", "Not enough memory to hand the layer to the plug-in.");
    return result;
  }
  mapping.view = static_cast<std::uint8_t*>(MapViewOfFile(mapping.handle, FILE_MAP_ALL_ACCESS, 0, 0, 0));
  if (mapping.view == nullptr) {
    result.message = QCoreApplication::translate("LegacyPluginRunner", "Not enough memory to hand the layer to the plug-in.");
    return result;
  }
  std::memcpy(mapping.view, input.input->data(), pixel_bytes);
  std::memcpy(mapping.view + pixel_bytes, input.input->data(), pixel_bytes);
  if (input.mask != nullptr) {
    std::memcpy(mapping.view + pixel_bytes * 2U, input.mask->data(), mask_bytes);
  }

  QLocalServer server;
  if (!server.listen(pipe_name)) {
    result.message = QCoreApplication::translate("LegacyPluginRunner", "The plug-in host could not be contacted: %1").arg(server.errorString());
    return result;
  }

  // Let the helper bring its dialogs to the front (it is a new process, which
  // Windows would otherwise keep behind the active window).
  AllowSetForegroundWindow(ASFW_ANY);

  QProcess process;
  process.setProgram(input.helper_executable);
  process.setArguments({QStringLiteral("--pipe"), pipe_name});
  process.setProcessChannelMode(QProcess::ForwardedErrorChannel);
  process.start();
  if (!process.waitForStarted(5000)) {
    result.message = QCoreApplication::translate("LegacyPluginRunner", "The plug-in host program could not be started: %1").arg(process.errorString());
    return result;
  }
  // The helper loads the plug-in only after the Run request, so nothing it
  // starts can escape the job. Every return below ends the tree except a run
  // that applied: a cancelled, stopped or crashed plug-in must not leave its
  // own programs behind (G'MIC-Qt kept running without a window).
  struct ProcessTreeGuard {
    LegacyPluginProcessTree tree;
    bool keep{false};
    ~ProcessTreeGuard() {
      if (!keep) {
        tree.terminate();
      }
    }
  } process_tree;
  process_tree.tree.assign(process.processId());

  DialogAcceptor acceptor;
  acceptor.accept = input.auto_accept_dialogs;
  acceptor.capture_path = input.capture_dialog_path;
  if (input.auto_accept_dialogs || !input.capture_dialog_path.isEmpty()) {
    acceptor.pid = static_cast<DWORD>(process.processId());
  }
  // The plug-in's windows belong to another process. Windows grants a process
  // the foreground once, and a plug-in may spend that on an invisible stub
  // before its real window exists (KPT does), after which its own
  // SetForegroundWindow calls are refused and the window opens behind Patchy.
  // Patchy is the foreground process, so it brings each new plug-in window
  // forward itself, renews the grant, and tells the caller where the window
  // is so the progress box can sit below it instead of on top.
  const DWORD helper_pid = static_cast<DWORD>(process.processId());
  std::set<HWND> raised;
  QElapsedTimer window_scan;
  window_scan.start();
  QRect last_window_rect;
  const auto watch_windows = [&] {
    if (window_scan.elapsed() < 50 || process.state() != QProcess::Running) {
      return;
    }
    window_scan.restart();
    WindowScan scan;
    scan.pid = helper_pid;
    EnumWindows(collect_helper_windows, reinterpret_cast<LPARAM>(&scan));
    HWND largest = nullptr;
    long largest_area = 0;
    RECT largest_rect{};
    for (HWND hwnd : scan.windows) {
      RECT rect{};
      GetWindowRect(hwnd, &rect);
      const long area = static_cast<long>(rect.right - rect.left) * static_cast<long>(rect.bottom - rect.top);
      if (area > largest_area) {
        largest_area = area;
        largest = hwnd;
        largest_rect = rect;
      }
      if (area > 0 && raised.insert(hwnd).second) {
        AllowSetForegroundWindow(helper_pid);
        SetForegroundWindow(hwnd);
      }
    }
    // The user asked for the window back (the "Show Plug-in Window" button):
    // restore it if minimized and put it in front again.
    if (largest != nullptr && callbacks.raise_plugin_window && callbacks.raise_plugin_window()) {
      AllowSetForegroundWindow(helper_pid);
      if (IsIconic(largest)) {
        ShowWindow(largest, SW_RESTORE);
      }
      SetForegroundWindow(largest);
    }
    const QRect rect = largest != nullptr ? QRect(QPoint(largest_rect.left, largest_rect.top),
                                                  QPoint(largest_rect.right - 1, largest_rect.bottom - 1))
                                          : QRect();
    if (rect != last_window_rect && callbacks.plugin_window) {
      last_window_rect = rect;
      callbacks.plugin_window(rect);
    }
  };
  const auto pump = [&] {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    if (callbacks.tick) {
      callbacks.tick();
    }
    watch_windows();
    if (acceptor.pid != 0 && process.state() == QProcess::Running) {
      acceptor.poll();
    }
  };

  QElapsedTimer timer;
  timer.start();
  while (!server.hasPendingConnections() && process.state() == QProcess::Running && timer.elapsed() < 15000) {
    server.waitForNewConnection(20);
    pump();
  }
  if (!server.hasPendingConnections()) {
    process.kill();
    process.waitForFinished(2000);
    result.message = QCoreApplication::translate("LegacyPluginRunner", "The plug-in host did not respond.");
    return result;
  }
  std::unique_ptr<QLocalSocket> socket(server.nextPendingConnection());
  server.close();

  Writer hello;
  hello.u32(kProtocolVersion);
  hello.u32(static_cast<std::uint32_t>(sizeof(void*)));
  send_frame(*socket, kMessageHello, hello.data());

  RunRequest request;
  request.flags = (input.show_dialog ? kRunShowDialog : 0U) | (input.protect_alpha ? kRunProtectAlpha : 0U);
  request.parent_window = static_cast<std::uint64_t>(input.parent_window);
  request.plugin_path = QDir::toNativeSeparators(input.plugin_path).toStdU16String();
  request.entry_point = input.entry_point;
  request.mapping_name.assign(mapping_name.begin(), mapping_name.end());
  request.mapping_size = mapping_size;
  request.input_offset = 0;
  request.output_offset = pixel_bytes;
  request.mask_offset = pixel_bytes * 2U;
  request.width = input.width;
  request.height = input.height;
  request.planes = input.planes;
  request.filter_case = input.filter_case;
  request.filter_top = input.filter_rect.top();
  request.filter_left = input.filter_rect.left();
  request.filter_bottom = input.filter_rect.top() + input.filter_rect.height();
  request.filter_right = input.filter_rect.left() + input.filter_rect.width();
  request.has_mask = input.mask != nullptr ? 1 : 0;
  request.case_input_handling = input.case_info.input_handling;
  request.case_output_handling = input.case_info.output_handling;
  request.case_flags1 = input.case_info.flags1;
  std::copy(input.foreground.begin(), input.foreground.end(), request.foreground);
  std::copy(input.background.begin(), input.background.end(), request.background);
  request.resolution_fixed = input.resolution_fixed;
  request.document_title = input.document_title.toStdString();
  request.parameters.assign(input.parameters.begin(), input.parameters.end());
  request.screen_max_width = input.screen_max_width;
  request.screen_max_height = input.screen_max_height;
  request.window_title = input.window_title.toStdU16String();

  FrameReader frames;
  bool hello_seen = false;
  bool run_sent = false;
  bool cancel_sent = false;
  bool got_result = false;
  RunResult run_result;
  QElapsedTimer cancel_timer;
  timer.restart();
  for (;;) {
    pump();
    frames.buffer.append(socket->readAll());
    std::uint32_t type = 0;
    std::vector<std::uint8_t> payload;
    while (frames.next(type, payload)) {
      if (type == kMessageHello) {
        Reader reader(payload.data(), payload.size());
        const auto version = reader.u32();
        if (version != kProtocolVersion) {
          process.kill();
          process.waitForFinished(2000);
          result.message = QCoreApplication::translate("LegacyPluginRunner", "The plug-in host program does not match this Patchy build.");
          return result;
        }
        hello_seen = true;
      } else if (type == kMessageProgress) {
        Reader reader(payload.data(), payload.size());
        const auto done = reader.i32();
        const auto total = reader.i32();
        if (callbacks.progress) {
          callbacks.progress(done, total);
        }
      } else if (type == kMessagePhase) {
        Reader reader(payload.data(), payload.size());
        const auto selector = reader.i32();
        if (callbacks.phase) {
          auto phase = LegacyPluginPhase::Unknown;
          switch (selector) {
            case 1: phase = LegacyPluginPhase::Parameters; break;
            case 2: phase = LegacyPluginPhase::Prepare; break;
            case 3: phase = LegacyPluginPhase::Start; break;
            case 4: phase = LegacyPluginPhase::Continue; break;
            case 5: phase = LegacyPluginPhase::Finish; break;
            default: break;
          }
          callbacks.phase(phase);
        }
      } else if (type == kMessageResult) {
        got_result = decode_run_result(payload.data(), payload.size(), run_result);
        if (!got_result) {
          run_result.status = kRunError;
          run_result.message.clear();
        }
        got_result = true;
      }
    }
    if (got_result) {
      break;
    }
    if (hello_seen && !run_sent) {
      send_frame(*socket, kMessageRun, encode_run_request(request));
      run_sent = true;
    }
    if (!hello_seen && timer.elapsed() > 15000) {
      process.kill();
      process.waitForFinished(2000);
      result.message = QCoreApplication::translate("LegacyPluginRunner", "The plug-in host did not respond.");
      return result;
    }
    if (!cancel_sent && ((callbacks.cancelled && callbacks.cancelled()) || acceptor.gave_up)) {
      send_frame(*socket, kMessageCancel, {});
      cancel_sent = true;
      cancel_timer.start();
    }
    if (cancel_sent && cancel_timer.elapsed() > 3000) {
      // The plug-in ignored the request (a modal dialog, or no abort polling):
      // end it. The layer is untouched because nothing was written back yet.
      process.kill();
      process.waitForFinished(2000);
      result.status = LegacyPluginRunStatus::Cancelled;
      return result;
    }
    if (process.state() != QProcess::Running) {
      frames.buffer.append(socket->readAll());
      if (frames.next(type, payload) && type == kMessageResult &&
          decode_run_result(payload.data(), payload.size(), run_result)) {
        got_result = true;
        break;
      }
      if (cancel_sent) {
        result.status = LegacyPluginRunStatus::Cancelled;
        return result;
      }
      const auto exit_code = process.exitStatus() == QProcess::CrashExit ? -1 : process.exitCode();
      result.message = QCoreApplication::translate("LegacyPluginRunner", "The plug-in crashed (host exit code %1).").arg(exit_code);
      return result;
    }
  }

  process.waitForFinished(3000);
  if (process.state() == QProcess::Running) {
    process.kill();
    process.waitForFinished(1000);
  }

  result.parameters = QByteArray(reinterpret_cast<const char*>(run_result.parameters.data()),
                                 static_cast<int>(run_result.parameters.size()));
  if (run_result.status == kRunOk) {
    std::memcpy(input.output->data(), mapping.view + pixel_bytes, pixel_bytes);
    result.status = LegacyPluginRunStatus::Ok;
    process_tree.keep = true;
  } else if (run_result.status == kRunCancelled) {
    result.status = LegacyPluginRunStatus::Cancelled;
  } else {
    result.status = LegacyPluginRunStatus::Error;
    result.message = run_result.message.empty() ? QCoreApplication::translate("LegacyPluginRunner", "The plug-in reported an error.")
                                                : QString::fromUtf8(run_result.message.c_str());
  }
  return result;
}

}  // namespace patchy::ui
