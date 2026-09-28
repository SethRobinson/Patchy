// patchy-8bf-host32.exe / patchy-8bf-host64.exe: the out-of-process host for
// classic Photoshop filter plug-ins. Patchy starts one per run, hands it the
// pixels through a named file mapping and the request over a named pipe, and
// reads the result back. A plug-in crash ends this process, never Patchy.
//
//   patchy-8bf-hostNN.exe --pipe <name>
//   patchy-8bf-hostNN.exe --self-test <plugin.8bf> <entry|-> <in.raw> <out.raw> <width> <height> <planes>
//   patchy-8bf-hostNN.exe --self-test-crash
//   patchy-8bf-hostNN.exe --self-test-screen <max width> <max height> <report.txt>
//
// The self-test mode runs the plug-in over a raw interleaved 8-bit buffer with
// no dialog (the core test suite uses it); --self-test-crash faults on purpose
// so the crash reporting path can be tested; --self-test-screen applies the
// virtual screen (screen_shim.hpp) to this executable's own imports and writes
// what the plug-in would see, plus where hidden test windows land.

#include "filter_runner.hpp"
#include "host_protocol.hpp"
#include "screen_shim.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using namespace patchy::legacy_host;

bool write_all(HANDLE pipe, const std::vector<std::uint8_t>& bytes) {
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    DWORD written = 0;
    if (!WriteFile(pipe, bytes.data() + offset, static_cast<DWORD>(bytes.size() - offset), &written, nullptr)) {
      return false;
    }
    offset += written;
  }
  return true;
}

bool read_exact(HANDLE pipe, std::uint8_t* out, std::size_t size) {
  std::size_t offset = 0;
  while (offset < size) {
    DWORD read = 0;
    if (!ReadFile(pipe, out + offset, static_cast<DWORD>(size - offset), &read, nullptr) || read == 0) {
      return false;
    }
    offset += read;
  }
  return true;
}

bool read_message(HANDLE pipe, std::uint32_t& type, std::vector<std::uint8_t>& payload) {
  std::uint8_t header[8];
  if (!read_exact(pipe, header, sizeof(header))) {
    return false;
  }
  Reader reader(header, sizeof(header));
  type = reader.u32();
  const auto length = reader.u32();
  payload.resize(length);
  return length == 0 || read_exact(pipe, payload.data(), length);
}

bool send_message(HANDLE pipe, std::uint32_t type, const std::vector<std::uint8_t>& payload) {
  return write_all(pipe, frame_message(type, payload));
}

struct PipeSession {
  HANDLE pipe{INVALID_HANDLE_VALUE};
  bool cancelled{false};
  std::chrono::steady_clock::time_point last_progress{};

  // Reads whatever complete messages are waiting without blocking.
  void poll() {
    for (;;) {
      DWORD available = 0;
      if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) {
        cancelled = true;  // Patchy went away
        return;
      }
      if (available < 8) {
        return;
      }
      std::uint32_t type = 0;
      std::vector<std::uint8_t> payload;
      if (!read_message(pipe, type, payload)) {
        cancelled = true;
        return;
      }
      if (type == kMessageCancel) {
        cancelled = true;
      }
    }
  }
};

// The frame's close button ends a run the plug-in will not end itself: the
// result "cancelled" goes to Patchy and this process exits from inside the
// plug-in's own message loop, since there is no other way out of it.
HANDLE g_close_pipe = INVALID_HANDLE_VALUE;

void cancel_from_frame_close() {
  if (g_close_pipe != INVALID_HANDLE_VALUE) {
    RunResult result;
    result.status = kRunCancelled;
    (void)send_message(g_close_pipe, kMessageResult, encode_run_result(result));
    FlushFileBuffers(g_close_pipe);
    CloseHandle(g_close_pipe);
    g_close_pipe = INVALID_HANDLE_VALUE;
  }
  ExitProcess(0);
}

int run_pipe_mode(const std::wstring& name) {
  const std::wstring path = L"\\\\.\\pipe\\" + name;
  HANDLE pipe = INVALID_HANDLE_VALUE;
  for (int attempt = 0; attempt < 100 && pipe == INVALID_HANDLE_VALUE; ++attempt) {
    pipe = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
      Sleep(50);
    }
  }
  if (pipe == INVALID_HANDLE_VALUE) {
    return 2;
  }
  PipeSession session;
  session.pipe = pipe;
  g_close_pipe = pipe;
  set_screen_shim_close_handler(&cancel_from_frame_close);

  Writer hello;
  hello.u32(kProtocolVersion);
  hello.u32(static_cast<std::uint32_t>(sizeof(void*)));
  if (!send_message(pipe, kMessageHello, hello.data())) {
    return 3;
  }

  std::uint32_t type = 0;
  std::vector<std::uint8_t> payload;
  if (!read_message(pipe, type, payload) || type != kMessageHello) {
    return 4;
  }
  {
    Reader reader(payload.data(), payload.size());
    if (reader.u32() != kProtocolVersion) {
      return 5;
    }
  }
  if (!read_message(pipe, type, payload) || type != kMessageRun) {
    return 6;
  }
  RunRequest request;
  if (!decode_run_request(payload.data(), payload.size(), request)) {
    return 7;
  }

  RunResult result;
  const std::wstring mapping_name(request.mapping_name.begin(), request.mapping_name.end());
  HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, mapping_name.c_str());
  void* view = mapping != nullptr ? MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0) : nullptr;
  if (view == nullptr) {
    result.status = kRunError;
    result.message = "The pixel exchange area could not be opened.";
  } else {
    auto* base = static_cast<std::uint8_t*>(view);
    RunnerImage image;
    image.width = request.width;
    image.height = request.height;
    image.planes = request.planes;
    image.input = base + request.input_offset;
    image.output = base + request.output_offset;
    image.mask = request.has_mask != 0 ? base + request.mask_offset : nullptr;

    RunnerCallbacks callbacks;
    callbacks.progress = [&session](std::int32_t done, std::int32_t total) {
      const auto now = std::chrono::steady_clock::now();
      if (now - session.last_progress > std::chrono::milliseconds(40) || done >= total) {
        session.last_progress = now;
        Writer w;
        w.i32(done);
        w.i32(total);
        (void)send_message(session.pipe, kMessageProgress, w.data());
      }
      session.poll();
    };
    callbacks.should_abort = [&session] {
      session.poll();
      return session.cancelled;
    };
    result = run_filter(request, image, callbacks);
    g_close_pipe = INVALID_HANDLE_VALUE;  // the normal path owns the pipe from here
    UnmapViewOfFile(view);
  }
  if (mapping != nullptr) {
    CloseHandle(mapping);
  }
  const bool sent = send_message(pipe, kMessageResult, encode_run_result(result));
  FlushFileBuffers(pipe);
  CloseHandle(pipe);
  return sent ? 0 : 8;
}

int run_self_test(int argc, wchar_t** argv) {
  if (argc < 9) {
    std::fprintf(stderr, "usage: --self-test <plugin.8bf> <entry|-> <in.raw> <out.raw> <width> <height> <planes>\n");
    return 2;
  }
  RunRequest request;
  request.plugin_path.assign(argv[2], argv[2] + std::wcslen(argv[2]));
  if (std::wcscmp(argv[3], L"-") != 0) {
    for (const wchar_t* c = argv[3]; *c != 0; ++c) {
      request.entry_point.push_back(static_cast<char>(*c));  // exported names are ASCII
    }
  }
  const std::wstring in_path(argv[4]);
  const std::wstring out_path(argv[5]);
  request.width = _wtoi(argv[6]);
  request.height = _wtoi(argv[7]);
  request.planes = _wtoi(argv[8]);
  request.filter_top = 0;
  request.filter_left = 0;
  request.filter_bottom = request.height;
  request.filter_right = request.width;
  request.filter_case = request.planes == 4 ? 4 : 1;
  request.case_input_handling = 1;
  request.case_output_handling = 1;

  std::ifstream input(in_path, std::ios::binary);
  std::vector<std::uint8_t> in_bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  const std::size_t expected = static_cast<std::size_t>(request.width) * static_cast<std::size_t>(request.height) *
                               static_cast<std::size_t>(request.planes);
  if (in_bytes.size() != expected) {
    std::fprintf(stderr, "input holds %zu bytes, expected %zu\n", in_bytes.size(), expected);
    return 3;
  }
  std::vector<std::uint8_t> out_bytes = in_bytes;
  RunnerImage image;
  image.width = request.width;
  image.height = request.height;
  image.planes = request.planes;
  image.input = in_bytes.data();
  image.output = out_bytes.data();
  RunnerCallbacks callbacks;
  callbacks.should_abort = [] { return false; };
  // Opened first: run_filter makes the plug-in's folder the working directory.
  std::ofstream output(out_path, std::ios::binary);
  const auto result = run_filter(request, image, callbacks);
  output.write(reinterpret_cast<const char*>(out_bytes.data()), static_cast<std::streamsize>(out_bytes.size()));
  std::printf("status %d %s\n", static_cast<int>(result.status), result.message.c_str());
  return result.status == kRunOk ? 0 : 1;
}

// Reports the virtual screen as a plug-in would see it, and where windows
// created for it land. Every window stays hidden; nothing touches the desktop.
int run_screen_self_test(int argc, wchar_t** argv) {
  if (argc < 5) {
    std::fprintf(stderr, "usage: --self-test-screen <max width> <max height> <report.txt>\n");
    return 2;
  }
  // A hidden stand-in for Patchy's window: the frame must be owned by it.
  HWND anchor = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, GetModuleHandleW(nullptr),
                                nullptr);
  install_screen_shim(anchor, _wtoi(argv[2]), _wtoi(argv[3]), L"Self-test via Patchy", /*include_own_module=*/true);
  const RECT screen = screen_shim_rect();
  std::ofstream report{std::wstring(argv[4])};
  report << "virtual " << screen.left << ' ' << screen.top << ' ' << (screen.right - screen.left) << ' '
         << (screen.bottom - screen.top) << '\n';
  report << "metrics " << GetSystemMetrics(SM_CXSCREEN) << ' ' << GetSystemMetrics(SM_CYSCREEN) << '\n';
  RECT work{};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  report << "workarea " << work.left << ' ' << work.top << ' ' << work.right << ' ' << work.bottom << '\n';
  RECT desktop{};
  GetWindowRect(GetDesktopWindow(), &desktop);
  report << "desktop " << desktop.left << ' ' << desktop.top << ' ' << desktop.right << ' ' << desktop.bottom << '\n';
  HDC dc = GetDC(nullptr);
  report << "caps " << GetDeviceCaps(dc, HORZRES) << ' ' << GetDeviceCaps(dc, VERTRES) << '\n';
  ReleaseDC(nullptr, dc);

  // A "full screen" window at the origin moves onto the virtual screen; one
  // already placed on it (real coordinates, as read back from another window)
  // stays; a later move to virtual coordinates follows the same rule.
  HWND at_origin = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 100, 100, nullptr, nullptr,
                                   GetModuleHandleW(nullptr), nullptr);
  HWND already_there = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, screen.left + 10, screen.top + 10, 100, 100,
                                       nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  RECT rect{};
  GetWindowRect(at_origin, &rect);
  report << "window0 " << rect.left << ' ' << rect.top << '\n';
  GetWindowRect(already_there, &rect);
  report << "window1 " << rect.left << ' ' << rect.top << '\n';
  SetWindowPos(at_origin, nullptr, 20, 30, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  GetWindowRect(at_origin, &rect);
  report << "moved " << rect.left << ' ' << rect.top << '\n';
  MoveWindow(already_there, screen.left + 40, screen.top + 50, 100, 100, FALSE);
  GetWindowRect(already_there, &rect);
  report << "kept " << rect.left << ' ' << rect.top << '\n';
  DestroyWindow(already_there);

  // A small stub that grows to the screen size (KPT's pattern) is adopted
  // into the frame on that resize; destroying it frees the frame again.
  const int width = screen.right - screen.left;
  const int height = screen.bottom - screen.top;
  MoveWindow(at_origin, 0, 0, width, height, FALSE);
  report << "grown " << (GetParent(at_origin) != nullptr && GetParent(at_origin) == screen_shim_frame() ? 1 : 0)
         << '\n';
  DestroyWindow(at_origin);

  // A borderless window the size of the virtual screen is a full-screen
  // canvas: it becomes a child of the titled frame (still hidden here, since
  // nothing shows it), filling the frame's client area. From then on the
  // plug-in's coordinate space has the canvas at (0,0): rectangles, client
  // conversions and new windows are all offset by the frame's client origin.
  HWND canvas = CreateWindowExW(WS_EX_TOPMOST, L"STATIC", L"", WS_POPUP, 0, 0, width, height, nullptr, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
  HWND frame = GetParent(canvas);
  wchar_t title[64] = {};
  wchar_t frame_class[64] = {};
  if (frame != nullptr) {
    GetWindowTextW(frame, title, 64);
    GetClassNameW(frame, frame_class, 64);
  }
  RECT canvas_rect{};
  GetWindowRect(canvas, &canvas_rect);  // virtual: the canvas is the origin
  RECT canvas_real{};
  screen_shim_real_window_rect(canvas, &canvas_real);
  RECT client{};
  POINT client_origin{0, 0};
  if (frame != nullptr) {
    GetClientRect(frame, &client);
    RECT frame_rect{};
    screen_shim_real_window_rect(frame, &frame_rect);
    client_origin.x = frame_rect.left;
    client_origin.y = frame_rect.top;
    // The client origin in real coordinates: the frame's rect plus its
    // borders (ClientToScreen is virtual now).
    RECT borders{0, 0, 0, 0};
    AdjustWindowRectEx(&borders, static_cast<DWORD>(GetWindowLongPtrW(frame, GWL_STYLE)), FALSE,
                       static_cast<DWORD>(GetWindowLongPtrW(frame, GWL_EXSTYLE)));
    client_origin.x -= borders.left;
    client_origin.y -= borders.top;
  }
  screen_shim_real_window_rect(canvas, &canvas_real);
  report << "framed " << (frame != nullptr ? 1 : 0) << ' ' << (frame == screen_shim_frame() ? 1 : 0) << ' '
         << (std::wcscmp(title, L"Self-test via Patchy") == 0 ? 1 : 0) << ' '
         << (std::wcscmp(frame_class, kScreenShimFrameClass) == 0 ? 1 : 0) << ' '
         << (frame != nullptr && IsWindowVisible(frame) ? 1 : 0) << '\n';
  report << "owner " << (frame != nullptr && GetWindow(frame, GW_OWNER) == anchor ? 1 : 0) << '\n';
  report << "canvas " << canvas_rect.left << ' ' << canvas_rect.top << ' ' << (canvas_rect.right - canvas_rect.left)
         << ' ' << (canvas_rect.bottom - canvas_rect.top) << '\n';
  report << "canvasreal " << canvas_real.left << ' ' << canvas_real.top << '\n';
  report << "frameclient " << client_origin.x << ' ' << client_origin.y << ' ' << client.right << ' ' << client.bottom
         << '\n';
  POINT to_screen{0, 0};
  ClientToScreen(canvas, &to_screen);
  POINT to_client{10, 10};
  ScreenToClient(canvas, &to_client);
  HWND popup = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 5, 5, 100, 100, nullptr, nullptr,
                               GetModuleHandleW(nullptr), nullptr);
  RECT popup_rect{};
  GetWindowRect(popup, &popup_rect);
  RECT popup_real{};
  screen_shim_real_window_rect(popup, &popup_real);
  report << "vspace " << to_screen.x << ' ' << to_screen.y << ' ' << to_client.x << ' ' << to_client.y << ' '
         << popup_rect.left << ' ' << popup_rect.top << ' ' << popup_real.left << ' ' << popup_real.top << '\n';
  DestroyWindow(popup);
  DestroyWindow(canvas);
  if (frame != nullptr) {
    DestroyWindow(frame);
  }
  return report.good() ? 0 : 3;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc >= 3 && std::wcscmp(argv[1], L"--pipe") == 0) {
    return run_pipe_mode(argv[2]);
  }
  if (argc >= 2 && std::wcscmp(argv[1], L"--self-test") == 0) {
    return run_self_test(argc, argv);
  }
  if (argc >= 2 && std::wcscmp(argv[1], L"--self-test-screen") == 0) {
    return run_screen_self_test(argc, argv);
  }
  if (argc >= 2 && std::wcscmp(argv[1], L"--self-test-crash") == 0) {
    // Deliberate access violation for the crash reporting tests.
    volatile int* null_pointer = nullptr;
    *null_pointer = 1;
    return 0;
  }
  std::fprintf(stderr, "patchy-8bf-host: started by Patchy to run a legacy Photoshop filter; not for direct use.\n");
  return 1;
}
