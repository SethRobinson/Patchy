#include "filter_runner.hpp"

#include "legacy_filter_abi.hpp"
#include "screen_shim.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

namespace patchy::legacy_host {

namespace {

using namespace patchy::abi;

// Handles are real pointer-to-pointer blocks so a plug-in that dereferences a
// handle directly (Mac habit) reads the data.
struct HandleBlock {
  char* data;
  std::int32_t size;
};

struct HostState {
  const RunRequest* request{nullptr};
  RunnerImage image;
  const RunnerCallbacks* callbacks{nullptr};

  FilterRecord record{};
  BufferProcs buffer_procs{};
  HandleProcs handle_procs{};
  PropertyProcs property_procs{};
  SPBasicSuite basic_suite{};
  PlatformData platform{};
  Str255 error_string{};

  std::unordered_set<HandleBlock*> handles;
  std::unordered_set<void*> buffers;

  std::vector<std::uint8_t> prepared_input;  // input after the case's data handling
  std::vector<std::uint8_t> in_buffer;
  std::vector<std::uint8_t> out_buffer;
  std::vector<std::uint8_t> mask_buffer;
  Rect out_rect{};
  std::int16_t out_lo{0};
  std::int16_t out_hi{0};
  bool output_pending{false};
  bool aborted{false};
  bool writes_outside_selection{false};
  bool protect_alpha{false};
};

HostState* g_state = nullptr;

// Where the last fault happened, for the error message (module + offset) and
// the trace (the stack slots hold the call site when a bad pointer was called).
struct FaultInfo {
  DWORD code{0};
  void* address{nullptr};
  ULONG_PTR access_kind{0};
  ULONG_PTR access_address{0};
  std::uintptr_t stack_pointer{0};
  std::uintptr_t return_slots[4]{};
};
FaultInfo g_last_fault;

// PATCHY_8BF_TRACE=1 in the environment prints each selector call and the
// rectangles the plug-in asked for to stderr (diagnostics for a new plug-in).
bool trace_enabled() {
  static const bool enabled = [] {
    char value[8] = {};
    return GetEnvironmentVariableA("PATCHY_8BF_TRACE", value, sizeof(value)) > 0 && value[0] == '1';
  }();
  return enabled;
}

void trace(const char* text, const FilterRecord& record, OSErr code) {
  if (!trace_enabled()) {
    return;
  }
  std::fprintf(stderr, "[8bf] %s result=%d in=(%d,%d,%d,%d) planes %d..%d out=(%d,%d,%d,%d) planes %d..%d mask=(%d,%d,%d,%d)\n",
               text, static_cast<int>(code), record.inRect.top, record.inRect.left, record.inRect.bottom,
               record.inRect.right, record.inLoPlane, record.inHiPlane, record.outRect.top, record.outRect.left,
               record.outRect.bottom, record.outRect.right, record.outLoPlane, record.outHiPlane, record.maskRect.top,
               record.maskRect.left, record.maskRect.bottom, record.maskRect.right);
  std::fflush(stderr);
  if (g_last_fault.stack_pointer != 0) {
    std::fprintf(stderr, "[8bf] stack slots: %llX %llX %llX %llX\n",
                 static_cast<unsigned long long>(g_last_fault.return_slots[0]),
                 static_cast<unsigned long long>(g_last_fault.return_slots[1]),
                 static_cast<unsigned long long>(g_last_fault.return_slots[2]),
                 static_cast<unsigned long long>(g_last_fault.return_slots[3]));
    g_last_fault.stack_pointer = 0;
  }
}

bool rect_empty(const Rect& r) noexcept { return r.right <= r.left || r.bottom <= r.top; }

std::int32_t clamp_i32(std::int32_t v, std::int32_t lo, std::int32_t hi) noexcept { return std::max(lo, std::min(v, hi)); }

// --- buffers ------------------------------------------------------------------

OSErr allocate_buffer(std::int32_t size, BufferID* buffer) {
  if (buffer == nullptr) {
    return kFilterBadParameters;
  }
  void* block = std::malloc(size > 0 ? static_cast<std::size_t>(size) : 1U);
  if (block == nullptr) {
    *buffer = nullptr;
    return kMemFullErr;
  }
  std::memset(block, 0, size > 0 ? static_cast<std::size_t>(size) : 1U);
  if (g_state != nullptr) {
    g_state->buffers.insert(block);
  }
  *buffer = block;
  return kNoErr;
}

Ptr lock_buffer(BufferID buffer, Boolean) { return static_cast<Ptr>(buffer); }

void unlock_buffer(BufferID) {}

void free_buffer(BufferID buffer) {
  if (buffer == nullptr) {
    return;
  }
  if (g_state != nullptr) {
    g_state->buffers.erase(buffer);
  }
  std::free(buffer);
}

std::int32_t buffer_space() {
#ifdef _WIN64
  return 1 << 30;
#else
  return 1 << 29;
#endif
}

// --- handles ------------------------------------------------------------------

HandleBlock* block_of(Handle handle) {
  if (handle == nullptr || g_state == nullptr) {
    return nullptr;
  }
  auto* block = reinterpret_cast<HandleBlock*>(handle);
  return g_state->handles.count(block) != 0 ? block : nullptr;
}

Handle new_handle(std::int32_t size) {
  auto* block = new (std::nothrow) HandleBlock{nullptr, size};
  if (block == nullptr) {
    return nullptr;
  }
  block->data = static_cast<char*>(std::malloc(size > 0 ? static_cast<std::size_t>(size) : 1U));
  if (block->data == nullptr) {
    delete block;
    return nullptr;
  }
  std::memset(block->data, 0, size > 0 ? static_cast<std::size_t>(size) : 1U);
  if (g_state != nullptr) {
    g_state->handles.insert(block);
  }
  return reinterpret_cast<Handle>(block);
}

void dispose_handle(Handle handle) {
  auto* block = block_of(handle);
  if (block == nullptr) {
    return;
  }
  g_state->handles.erase(block);
  std::free(block->data);
  delete block;
}

std::int32_t handle_size(Handle handle) {
  auto* block = block_of(handle);
  return block != nullptr ? block->size : 0;
}

OSErr set_handle_size(Handle handle, std::int32_t new_size) {
  auto* block = block_of(handle);
  if (block == nullptr) {
    return kFilterBadParameters;
  }
  auto* grown = static_cast<char*>(std::realloc(block->data, new_size > 0 ? static_cast<std::size_t>(new_size) : 1U));
  if (grown == nullptr) {
    return kMemFullErr;
  }
  if (new_size > block->size) {
    std::memset(grown + block->size, 0, static_cast<std::size_t>(new_size - block->size));
  }
  block->data = grown;
  block->size = new_size;
  return kNoErr;
}

Ptr lock_handle(Handle handle, Boolean) {
  auto* block = block_of(handle);
  return block != nullptr ? block->data : (handle != nullptr ? *handle : nullptr);
}

void unlock_handle(Handle) {}

void recover_space(std::int32_t) {}

Handle handle_from_bytes(const std::vector<std::uint8_t>& bytes) {
  auto handle = new_handle(static_cast<std::int32_t>(bytes.size()));
  if (handle != nullptr && !bytes.empty()) {
    std::memcpy(*handle, bytes.data(), bytes.size());
  }
  return handle;
}

Handle handle_from_string(const std::string& text) {
  return handle_from_bytes(std::vector<std::uint8_t>(text.begin(), text.end()));
}

// --- progress / abort ---------------------------------------------------------

bool poll_abort() {
  if (g_state == nullptr) {
    return true;
  }
  if (!g_state->aborted && g_state->callbacks != nullptr && g_state->callbacks->should_abort &&
      g_state->callbacks->should_abort()) {
    g_state->aborted = true;
  }
  return g_state->aborted;
}

Boolean test_abort() { return poll_abort() ? 1 : 0; }

void report_progress(std::int32_t done, std::int32_t total) {
  if (g_state != nullptr && g_state->callbacks != nullptr && g_state->callbacks->progress) {
    g_state->callbacks->progress(done, total);
  }
  poll_abort();
}

void process_event(void*) {
  MSG msg;
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  poll_abort();
}

// --- pixel access ---------------------------------------------------------------

// Source pixel with edge replication, from the prepared input.
const std::uint8_t* input_pixel(std::int32_t x, std::int32_t y) {
  const auto& image = g_state->image;
  x = clamp_i32(x, 0, image.width - 1);
  y = clamp_i32(y, 0, image.height - 1);
  return g_state->prepared_input.data() +
         (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x)) *
             static_cast<std::size_t>(image.planes);
}

std::uint8_t* output_pixel(std::int32_t x, std::int32_t y) {
  const auto& image = g_state->image;
  x = clamp_i32(x, 0, image.width - 1);
  y = clamp_i32(y, 0, image.height - 1);
  return image.output +
         (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x)) *
             static_cast<std::size_t>(image.planes);
}

void prepare_input() {
  const auto& image = g_state->image;
  const auto& request = *g_state->request;
  const std::size_t count =
      static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * static_cast<std::size_t>(image.planes);
  g_state->prepared_input.assign(image.input, image.input + count);
  if (image.planes != 4) {
    return;
  }
  // Transparency cases: the plug-in asked for transparent pixels to be matted
  // or zapped to a colour before it sees them.
  const auto handling = request.case_input_handling;
  std::uint8_t matte[3] = {0, 0, 0};
  bool mat = false;
  bool zap = false;
  switch (handling) {
    case 2: mat = true; break;                                  // black mat
    case 3: mat = true; matte[0] = matte[1] = matte[2] = 128; break;  // gray mat
    case 4: mat = true; matte[0] = matte[1] = matte[2] = 255; break;  // white mat
    case 6: zap = true; break;                                  // black zap
    case 7: zap = true; matte[0] = matte[1] = matte[2] = 128; break;
    case 8: zap = true; matte[0] = matte[1] = matte[2] = 255; break;
    case 10: zap = true; std::memcpy(matte, request.background, 3); break;
    case 11: zap = true; std::memcpy(matte, request.foreground, 3); break;
    default: break;
  }
  if (!mat && !zap) {
    return;
  }
  auto* px = g_state->prepared_input.data();
  for (std::size_t i = 0; i < count; i += 4) {
    const auto alpha = px[i + 3];
    if (zap) {
      if (alpha == 0) {
        px[i] = matte[0];
        px[i + 1] = matte[1];
        px[i + 2] = matte[2];
      }
    } else {
      for (int c = 0; c < 3; ++c) {
        px[i + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(
            (px[i + static_cast<std::size_t>(c)] * alpha + matte[c] * (255 - alpha) + 127) / 255);
      }
    }
  }
}

void fill_input() {
  auto& record = g_state->record;
  const auto& image = g_state->image;
  if (rect_empty(record.inRect)) {
    record.inData = nullptr;
    record.inRowBytes = 0;
    record.inColumnBytes = 0;
    record.inPlaneBytes = 1;
    return;
  }
  const std::int32_t lo = record.inLoPlane;
  const std::int32_t hi = std::max<std::int32_t>(record.inHiPlane, lo);
  const std::int32_t n = hi - lo + 1;
  const std::int32_t w = record.inRect.right - record.inRect.left;
  const std::int32_t h = record.inRect.bottom - record.inRect.top;
  g_state->in_buffer.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * static_cast<std::size_t>(n));
  auto* dst = g_state->in_buffer.data();
  for (std::int32_t y = 0; y < h; ++y) {
    for (std::int32_t x = 0; x < w; ++x) {
      const auto* src = input_pixel(record.inRect.left + x, record.inRect.top + y);
      for (std::int32_t p = lo; p <= hi; ++p) {
        *dst++ = p < image.planes ? src[p] : 255;
      }
    }
  }
  record.inData = g_state->in_buffer.data();
  record.inRowBytes = w * n;
  record.inColumnBytes = n;
  record.inPlaneBytes = 1;
}

void fill_output() {
  auto& record = g_state->record;
  const auto& image = g_state->image;
  if (rect_empty(record.outRect)) {
    record.outData = nullptr;
    record.outRowBytes = 0;
    record.outColumnBytes = 0;
    record.outPlaneBytes = 1;
    g_state->output_pending = false;
    return;
  }
  const std::int32_t lo = record.outLoPlane;
  const std::int32_t hi = std::max<std::int32_t>(record.outHiPlane, lo);
  const std::int32_t n = hi - lo + 1;
  const std::int32_t w = record.outRect.right - record.outRect.left;
  const std::int32_t h = record.outRect.bottom - record.outRect.top;
  g_state->out_buffer.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * static_cast<std::size_t>(n));
  auto* dst = g_state->out_buffer.data();
  for (std::int32_t y = 0; y < h; ++y) {
    for (std::int32_t x = 0; x < w; ++x) {
      const auto* src = output_pixel(record.outRect.left + x, record.outRect.top + y);
      for (std::int32_t p = lo; p <= hi; ++p) {
        *dst++ = p < image.planes ? src[p] : 255;
      }
    }
  }
  record.outData = g_state->out_buffer.data();
  record.outRowBytes = w * n;
  record.outColumnBytes = n;
  record.outPlaneBytes = 1;
  g_state->out_rect = record.outRect;
  g_state->out_lo = static_cast<std::int16_t>(lo);
  g_state->out_hi = static_cast<std::int16_t>(hi);
  g_state->output_pending = true;
}

void fill_mask() {
  auto& record = g_state->record;
  const auto& image = g_state->image;
  if (record.haveMask == 0 || image.mask == nullptr || rect_empty(record.maskRect)) {
    record.maskData = nullptr;
    record.maskRowBytes = 0;
    return;
  }
  const std::int32_t w = record.maskRect.right - record.maskRect.left;
  const std::int32_t h = record.maskRect.bottom - record.maskRect.top;
  g_state->mask_buffer.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
  auto* dst = g_state->mask_buffer.data();
  for (std::int32_t y = 0; y < h; ++y) {
    const std::int32_t sy = record.maskRect.top + y;
    for (std::int32_t x = 0; x < w; ++x) {
      const std::int32_t sx = record.maskRect.left + x;
      const bool inside = sx >= 0 && sy >= 0 && sx < image.width && sy < image.height;
      *dst++ = inside ? image.mask[static_cast<std::size_t>(sy) * static_cast<std::size_t>(image.width) +
                                   static_cast<std::size_t>(sx)]
                      : 0;
    }
  }
  record.maskData = g_state->mask_buffer.data();
  record.maskRowBytes = w;
}

// Writes the plug-in's output tile back into the image, clipped to the image
// and (unless the plug-in declared otherwise) to the filter rectangle.
void commit_output() {
  if (!g_state->output_pending) {
    return;
  }
  g_state->output_pending = false;
  const auto& image = g_state->image;
  const auto& request = *g_state->request;
  const auto rect = g_state->out_rect;
  const std::int32_t w = rect.right - rect.left;
  const std::int32_t n = g_state->out_hi - g_state->out_lo + 1;
  std::int32_t clip_top = 0;
  std::int32_t clip_left = 0;
  std::int32_t clip_bottom = image.height;
  std::int32_t clip_right = image.width;
  if (!g_state->writes_outside_selection) {
    clip_top = std::max(clip_top, request.filter_top);
    clip_left = std::max(clip_left, request.filter_left);
    clip_bottom = std::min(clip_bottom, request.filter_bottom);
    clip_right = std::min(clip_right, request.filter_right);
  }
  for (std::int32_t y = std::max<std::int32_t>(rect.top, clip_top); y < std::min<std::int32_t>(rect.bottom, clip_bottom);
       ++y) {
    for (std::int32_t x = std::max<std::int32_t>(rect.left, clip_left);
         x < std::min<std::int32_t>(rect.right, clip_right); ++x) {
      const auto* src = g_state->out_buffer.data() +
                        (static_cast<std::size_t>(y - rect.top) * static_cast<std::size_t>(w) +
                         static_cast<std::size_t>(x - rect.left)) *
                            static_cast<std::size_t>(n);
      auto* dst = output_pixel(x, y);
      for (std::int32_t p = g_state->out_lo; p <= g_state->out_hi; ++p) {
        if (p >= image.planes) {
          continue;
        }
        if (p == 3 && g_state->protect_alpha) {
          continue;
        }
        dst[p] = src[p - g_state->out_lo];
      }
    }
  }
}

OSErr advance_state() {
  if (g_state == nullptr) {
    return kFilterBadParameters;
  }
  if (trace_enabled()) {
    trace("advanceState", g_state->record, kNoErr);
  }
  commit_output();
  if (poll_abort()) {
    return kUserCanceledErr;
  }
  fill_input();
  fill_output();
  fill_mask();
  return kNoErr;
}

// --- display pixels (the plug-in's preview) -------------------------------------

OSErr display_pixels(const PSPixelMap* source, const VRect* src_rect, std::int32_t dst_row, std::int32_t dst_col,
                     void* platform_context) {
  if (source == nullptr || src_rect == nullptr || platform_context == nullptr || source->baseAddr == nullptr) {
    return kFilterBadParameters;
  }
  const std::int32_t w = src_rect->right - src_rect->left;
  const std::int32_t h = src_rect->bottom - src_rect->top;
  if (w <= 0 || h <= 0) {
    return kNoErr;
  }
  const auto* base = static_cast<const std::uint8_t*>(source->baseAddr);
  const PSPixelMask* mask = source->version >= 1 ? source->masks : nullptr;
  const bool gray = source->imageMode == kModeGrayScale;
  const std::size_t stride = (static_cast<std::size_t>(w) * 3U + 3U) & ~static_cast<std::size_t>(3U);
  std::vector<std::uint8_t> bits(stride * static_cast<std::size_t>(h));
  for (std::int32_t y = 0; y < h; ++y) {
    const std::int32_t sy = src_rect->top + y;
    auto* row = bits.data() + static_cast<std::size_t>(y) * stride;
    for (std::int32_t x = 0; x < w; ++x) {
      const std::int32_t sx = src_rect->left + x;
      const auto* px = base + static_cast<std::ptrdiff_t>(sy - source->bounds.top) * source->rowBytes +
                       static_cast<std::ptrdiff_t>(sx - source->bounds.left) * source->colBytes;
      std::uint8_t r = px[0];
      std::uint8_t g = gray ? px[0] : px[static_cast<std::ptrdiff_t>(source->planeBytes)];
      std::uint8_t b = gray ? px[0] : px[static_cast<std::ptrdiff_t>(source->planeBytes) * 2];
      if (mask != nullptr && mask->maskData != nullptr) {
        const auto* m = static_cast<const std::uint8_t*>(mask->maskData) +
                        static_cast<std::ptrdiff_t>(sy - source->bounds.top) * mask->rowBytes +
                        static_cast<std::ptrdiff_t>(sx - source->bounds.left) * mask->colBytes;
        const std::uint8_t alpha = *m;
        // Photoshop-style checkerboard behind transparent pixels.
        const bool light = (((sx / 8) + (sy / 8)) % 2) == 0;
        const std::uint8_t checker = light ? 255 : 204;
        r = static_cast<std::uint8_t>((r * alpha + checker * (255 - alpha)) / 255);
        g = static_cast<std::uint8_t>((g * alpha + checker * (255 - alpha)) / 255);
        b = static_cast<std::uint8_t>((b * alpha + checker * (255 - alpha)) / 255);
      }
      row[static_cast<std::size_t>(x) * 3U] = b;
      row[static_cast<std::size_t>(x) * 3U + 1U] = g;
      row[static_cast<std::size_t>(x) * 3U + 2U] = r;
    }
  }
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = w;
  info.bmiHeader.biHeight = -h;  // top-down
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 24;
  info.bmiHeader.biCompression = BI_RGB;
  auto* hdc = static_cast<HDC>(platform_context);
  SetStretchBltMode(hdc, COLORONCOLOR);
  StretchDIBits(hdc, dst_col, dst_row, w, h, 0, 0, w, h, bits.data(), &info, DIB_RGB_COLORS, SRCCOPY);
  return kNoErr;
}

// --- color services -------------------------------------------------------------

struct Rgb {
  double r, g, b;  // 0..1
};

Rgb from_space(std::int16_t space, const std::int16_t* c) {
  const auto clamp01 = [](double v) { return std::max(0.0, std::min(1.0, v)); };
  switch (space) {
    case kColorSpaceGray: {
      const double v = clamp01(c[0] / 255.0);
      return {v, v, v};
    }
    case kColorSpaceHSB:
    case kColorSpaceHSL: {
      const double hue = std::fmod(std::fmod(static_cast<double>(c[0]), 360.0) + 360.0, 360.0) / 60.0;
      const double s = clamp01(c[1] / 255.0);
      const double v = clamp01(c[2] / 255.0);
      double r = 0, g = 0, b = 0;
      if (space == kColorSpaceHSB) {
        const int i = static_cast<int>(std::floor(hue)) % 6;
        const double f = hue - std::floor(hue);
        const double p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
        switch (i) {
          case 0: r = v; g = t; b = p; break;
          case 1: r = q; g = v; b = p; break;
          case 2: r = p; g = v; b = t; break;
          case 3: r = p; g = q; b = v; break;
          case 4: r = t; g = p; b = v; break;
          default: r = v; g = p; b = q; break;
        }
      } else {
        const double l = v;
        const double chroma = (1 - std::fabs(2 * l - 1)) * s;
        const double x = chroma * (1 - std::fabs(std::fmod(hue, 2.0) - 1));
        const double m = l - chroma / 2;
        const int i = static_cast<int>(std::floor(hue)) % 6;
        switch (i) {
          case 0: r = chroma; g = x; break;
          case 1: r = x; g = chroma; break;
          case 2: g = chroma; b = x; break;
          case 3: g = x; b = chroma; break;
          case 4: r = x; b = chroma; break;
          default: r = chroma; b = x; break;
        }
        r += m; g += m; b += m;
      }
      return {clamp01(r), clamp01(g), clamp01(b)};
    }
    case kColorSpaceCMYK: {
      const double cc = clamp01(c[0] / 255.0), m = clamp01(c[1] / 255.0), y = clamp01(c[2] / 255.0),
                   k = clamp01(c[3] / 255.0);
      return {(1 - cc) * (1 - k), (1 - m) * (1 - k), (1 - y) * (1 - k)};
    }
    case kColorSpaceLab: {
      const double L = clamp01(c[0] / 255.0) * 100.0;
      const double a = c[1] - 128.0;
      const double bb = c[2] - 128.0;
      const double fy = (L + 16) / 116, fx = fy + a / 500, fz = fy - bb / 200;
      const auto finv = [](double t) { return t > 6.0 / 29 ? t * t * t : 3.0 * (6.0 / 29) * (6.0 / 29) * (t - 4.0 / 29); };
      const double X = 0.95047 * finv(fx), Y = 1.0 * finv(fy), Z = 1.08883 * finv(fz);
      double r = 3.2406 * X - 1.5372 * Y - 0.4986 * Z;
      double g = -0.9689 * X + 1.8758 * Y + 0.0415 * Z;
      double b = 0.0557 * X - 0.2040 * Y + 1.0570 * Z;
      const auto gam = [](double v) { return v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(std::max(v, 0.0), 1 / 2.4) - 0.055; };
      return {clamp01(gam(r)), clamp01(gam(g)), clamp01(gam(b))};
    }
    case kColorSpaceXYZ: {
      const double X = c[0] / 255.0, Y = c[1] / 255.0, Z = c[2] / 255.0;
      double r = 3.2406 * X - 1.5372 * Y - 0.4986 * Z;
      double g = -0.9689 * X + 1.8758 * Y + 0.0415 * Z;
      double b = 0.0557 * X - 0.2040 * Y + 1.0570 * Z;
      return {clamp01(r), clamp01(g), clamp01(b)};
    }
    default:
      return {clamp01(c[0] / 255.0), clamp01(c[1] / 255.0), clamp01(c[2] / 255.0)};
  }
}

void to_space(std::int16_t space, Rgb rgb, std::int16_t* c) {
  const auto to255 = [](double v) { return static_cast<std::int16_t>(std::lround(std::max(0.0, std::min(1.0, v)) * 255.0)); };
  c[0] = c[1] = c[2] = c[3] = 0;
  const double mx = std::max({rgb.r, rgb.g, rgb.b});
  const double mn = std::min({rgb.r, rgb.g, rgb.b});
  const double delta = mx - mn;
  double hue = 0;
  if (delta > 1e-9) {
    if (mx == rgb.r) {
      hue = 60 * std::fmod((rgb.g - rgb.b) / delta, 6.0);
    } else if (mx == rgb.g) {
      hue = 60 * ((rgb.b - rgb.r) / delta + 2);
    } else {
      hue = 60 * ((rgb.r - rgb.g) / delta + 4);
    }
    if (hue < 0) {
      hue += 360;
    }
  }
  switch (space) {
    case kColorSpaceGray:
      c[0] = to255(0.299 * rgb.r + 0.587 * rgb.g + 0.114 * rgb.b);
      break;
    case kColorSpaceHSB:
      c[0] = static_cast<std::int16_t>(std::lround(hue)) % 360;
      c[1] = to255(mx > 1e-9 ? delta / mx : 0);
      c[2] = to255(mx);
      break;
    case kColorSpaceHSL: {
      const double l = (mx + mn) / 2;
      c[0] = static_cast<std::int16_t>(std::lround(hue)) % 360;
      c[1] = to255(delta < 1e-9 ? 0 : delta / (1 - std::fabs(2 * l - 1)));
      c[2] = to255(l);
      break;
    }
    case kColorSpaceCMYK: {
      const double k = 1 - mx;
      c[3] = to255(k);
      if (k < 1 - 1e-9) {
        c[0] = to255((1 - rgb.r - k) / (1 - k));
        c[1] = to255((1 - rgb.g - k) / (1 - k));
        c[2] = to255((1 - rgb.b - k) / (1 - k));
      }
      break;
    }
    case kColorSpaceLab:
    case kColorSpaceXYZ: {
      const auto lin = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
      const double r = lin(rgb.r), g = lin(rgb.g), b = lin(rgb.b);
      const double X = 0.4124 * r + 0.3576 * g + 0.1805 * b;
      const double Y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
      const double Z = 0.0193 * r + 0.1192 * g + 0.9505 * b;
      if (space == kColorSpaceXYZ) {
        c[0] = to255(X); c[1] = to255(Y); c[2] = to255(Z);
      } else {
        const auto f = [](double t) { return t > std::pow(6.0 / 29, 3) ? std::cbrt(t) : t / (3 * std::pow(6.0 / 29, 2)) + 4.0 / 29; };
        const double fx = f(X / 0.95047), fy = f(Y / 1.0), fz = f(Z / 1.08883);
        const double L = 116 * fy - 16, a = 500 * (fx - fy), bb = 200 * (fy - fz);
        c[0] = to255(L / 100.0);
        c[1] = static_cast<std::int16_t>(std::lround(std::max(0.0, std::min(255.0, a + 128))));
        c[2] = static_cast<std::int16_t>(std::lround(std::max(0.0, std::min(255.0, bb + 128))));
      }
      break;
    }
    default:
      c[0] = to255(rgb.r); c[1] = to255(rgb.g); c[2] = to255(rgb.b);
      break;
  }
}

OSErr color_services(ColorServicesInfo* info) {
  if (info == nullptr || g_state == nullptr) {
    return kFilterBadParameters;
  }
  const auto& request = *g_state->request;
  Rgb rgb{0, 0, 0};
  switch (info->selector) {
    case kColorServicesChooseColor: {
      rgb = from_space(info->sourceSpace == kColorSpaceChosen ? kColorSpaceRGB : info->sourceSpace,
                       info->colorComponents);
      static COLORREF custom[16] = {};
      CHOOSECOLORW chooser{};
      chooser.lStructSize = sizeof(chooser);
      chooser.hwndOwner = static_cast<HWND>(g_state->platform.hwnd);
      chooser.rgbResult = RGB(static_cast<int>(rgb.r * 255), static_cast<int>(rgb.g * 255), static_cast<int>(rgb.b * 255));
      chooser.lpCustColors = custom;
      chooser.Flags = CC_RGBINIT | CC_FULLOPEN;
      if (!ChooseColorW(&chooser)) {
        return kUserCanceledErr;
      }
      rgb = {GetRValue(chooser.rgbResult) / 255.0, GetGValue(chooser.rgbResult) / 255.0,
             GetBValue(chooser.rgbResult) / 255.0};
      break;
    }
    case kColorServicesConvertColor:
      rgb = from_space(info->sourceSpace, info->colorComponents);
      break;
    case kColorServicesSamplePoint: {
      const Point* point = info->selectorParameter.globalSamplePoint;
      if (point == nullptr) {
        return kErrInvalidSamplePoint;
      }
      const auto& image = g_state->image;
      if (point->h < 0 || point->v < 0 || point->h >= image.width || point->v >= image.height) {
        return kErrInvalidSamplePoint;
      }
      const auto* px = input_pixel(point->h, point->v);
      rgb = {px[0] / 255.0, px[1] / 255.0, px[2] / 255.0};
      break;
    }
    case kColorServicesGetSpecialColor: {
      const auto* color = info->selectorParameter.specialColorID == kSpecialColorBackground ? request.background
                                                                                             : request.foreground;
      rgb = {color[0] / 255.0, color[1] / 255.0, color[2] / 255.0};
      break;
    }
    default:
      return kFilterBadParameters;
  }
  if (info->resultSpace == kColorSpaceChosen) {
    info->resultSpace = kColorSpaceRGB;
  }
  to_space(info->resultSpace, rgb, info->colorComponents);
  info->resultGamutInfoValid = 1;
  info->resultInGamut = 1;
  return kNoErr;
}

// --- properties -----------------------------------------------------------------

OSErr get_property(PIType signature, PIType key, std::int32_t index, std::intptr_t* simple, Handle* complex) {
  if (signature != kHostSignature || g_state == nullptr) {
    return kErrPlugInPropertyUndefined;
  }
  const auto set_simple = [simple](std::intptr_t value) {
    if (simple != nullptr) {
      *simple = value;
    }
    return kNoErr;
  };
  const auto set_complex = [complex](const std::string& text) {
    if (complex != nullptr) {
      *complex = handle_from_string(text);
    }
    return kNoErr;
  };
  const auto& image = g_state->image;
  switch (key) {
    case kPropNumberOfChannels:
      return set_simple(image.planes);
    case kPropChannelName: {
      static const char* names[] = {"Red", "Green", "Blue", "Transparency"};
      if (index < 0 || index >= image.planes) {
        return kErrPlugInPropertyUndefined;
      }
      return set_complex(names[index]);
    }
    case kPropImageMode:
      return set_simple(kModeRGBColor);
    case kPropNumberOfPaths:
      return set_simple(0);
    case kPropWorkPathIndex:
    case kPropClippingPathIndex:
    case kPropTargetPathIndex:
      return set_simple(-1);
    case kPropInterpolationMethod:
      return set_simple(3);  // bicubic
    case kPropRulerUnits:
      return set_simple(0);  // pixels
    case kPropRulerOriginH:
    case kPropRulerOriginV:
      return set_simple(0);
    case kPropSerialString:
      return set_complex("00000000");
    case kPropBigNudgeH:
    case kPropBigNudgeV:
      return set_simple(10 << 16);
    case kPropTitle:
      return set_complex(g_state->request->document_title.empty() ? "Untitled" : g_state->request->document_title);
    case kPropHostName:
      return set_complex("Patchy");
    default:
      return kErrPlugInPropertyUndefined;
  }
}

OSErr set_property(PIType, PIType, std::int32_t, std::intptr_t, Handle) { return kErrPlugInPropertyUndefined; }

// --- PICA basic suite -----------------------------------------------------------

SPErr acquire_suite(const char*, std::int32_t, const void** suite) {
  if (suite != nullptr) {
    *suite = nullptr;
  }
  return kSPSuiteNotFoundError;
}

SPErr release_suite(const char*, std::int32_t) { return kSPNoError; }

SPBoolean is_equal(const char* a, const char* b) {
  if (a == nullptr || b == nullptr) {
    return a == b ? 1 : 0;
  }
  return std::strcmp(a, b) == 0 ? 1 : 0;
}

SPErr allocate_block(std::size_t size, void** block) {
  if (block == nullptr) {
    return kSPBadParameterError;
  }
  *block = std::malloc(size > 0 ? size : 1U);
  return *block != nullptr ? kSPNoError : kSPBadParameterError;
}

SPErr free_block(void* block) {
  std::free(block);
  return kSPNoError;
}

SPErr reallocate_block(void* block, std::size_t new_size, void** new_block) {
  if (new_block == nullptr) {
    return kSPBadParameterError;
  }
  *new_block = std::realloc(block, new_size > 0 ? new_size : 1U);
  return *new_block != nullptr ? kSPNoError : kSPBadParameterError;
}

SPErr undefined_suite_call() { return kSPBadParameterError; }

// --- the guarded entry call ---------------------------------------------------------

// Structured exception handling needs a frame with no C++ objects that require
// unwinding, so the call lives in this plain function. Returns 0, or the
// exception code when the plug-in faulted.
DWORD record_fault(EXCEPTION_POINTERS* pointers) {
  g_last_fault = {};
  if (pointers != nullptr && pointers->ExceptionRecord != nullptr) {
    const auto& record = *pointers->ExceptionRecord;
    g_last_fault.code = record.ExceptionCode;
    g_last_fault.address = record.ExceptionAddress;
    if (record.NumberParameters >= 2) {
      g_last_fault.access_kind = record.ExceptionInformation[0];
      g_last_fault.access_address = record.ExceptionInformation[1];
    }
    if (pointers->ContextRecord != nullptr) {
#ifdef _WIN64
      g_last_fault.stack_pointer = pointers->ContextRecord->Rsp;
#else
      g_last_fault.stack_pointer = pointers->ContextRecord->Esp;
#endif
      const auto* slots = reinterpret_cast<const std::uintptr_t*>(g_last_fault.stack_pointer);
      for (int i = 0; i < 4; ++i) {
        g_last_fault.return_slots[i] = IsBadReadPtr(slots + i, sizeof(std::uintptr_t)) ? 0 : slots[i];
      }
    }
  }
  return EXCEPTION_EXECUTE_HANDLER;
}

DWORD call_entry_guarded(FilterEntryProc entry, std::int16_t selector, FilterRecord* record, std::intptr_t* data,
                         OSErr* result) {
  __try {
    entry(selector, record, data, result);
    return 0;
  } __except (record_fault(GetExceptionInformation())) {
    return GetExceptionCode() != 0 ? GetExceptionCode() : 1;
  }
}

std::string describe_last_fault() {
  char text[256];
  HMODULE module = nullptr;
  wchar_t module_path[MAX_PATH] = {};
  const char* module_name = "?";
  std::string narrow_name;
  std::uintptr_t offset = reinterpret_cast<std::uintptr_t>(g_last_fault.address);
  if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         static_cast<LPCWSTR>(g_last_fault.address), &module) &&
      GetModuleFileNameW(module, module_path, MAX_PATH) > 0) {
    const wchar_t* base = module_path;
    for (const wchar_t* c = module_path; *c != 0; ++c) {
      if (*c == L'\\' || *c == L'/') {
        base = c + 1;
      }
    }
    for (const wchar_t* c = base; *c != 0; ++c) {
      narrow_name.push_back(*c < 128 ? static_cast<char>(*c) : '?');
    }
    module_name = narrow_name.c_str();
    offset -= reinterpret_cast<std::uintptr_t>(module);
  }
  std::snprintf(text, sizeof(text), "at %s+0x%llX (%s address 0x%llX)", module_name,
                static_cast<unsigned long long>(offset),
                g_last_fault.access_kind == 1 ? "writing" : (g_last_fault.access_kind == 8 ? "executing" : "reading"),
                static_cast<unsigned long long>(g_last_fault.access_address));
  return text;
}

std::string hex_code(DWORD code) {
  char text[16];
  std::snprintf(text, sizeof(text), "0x%08lX", static_cast<unsigned long>(code));
  return text;
}

std::string pascal_to_utf8(const unsigned char* text) {
  const std::size_t length = text[0];
  std::string out;
  for (std::size_t i = 0; i < length; ++i) {
    const auto byte = text[1 + i];
    if (byte < 0x80) {
      out.push_back(static_cast<char>(byte));
    } else {
      out.push_back(static_cast<char>(0xC0 | (byte >> 6U)));
      out.push_back(static_cast<char>(0x80 | (byte & 0x3FU)));
    }
  }
  return out;
}

std::string error_message_for(OSErr code, const Str255& error_string) {
  switch (code) {
    case kFilterBadParameters:
      return "The plug-in reported bad parameters.";
    case kFilterBadMode:
      return "The plug-in does not support 8-bit RGB images.";
    case kMemFullErr:
      return "The plug-in ran out of memory.";
    case kErrPlugInHostInsufficient:
      return "The plug-in needs host features Patchy does not provide.";
    case kErrReportString: {
      const auto text = pascal_to_utf8(error_string);
      return text.empty() ? "The plug-in reported an error." : text;
    }
    default:
      return "The plug-in reported error " + std::to_string(code) + ".";
  }
}

void setup_record(HostState& state) {
  const auto& request = *state.request;
  const auto& image = state.image;
  auto& record = state.record;
  std::memset(&record, 0, sizeof(record));

  state.buffer_procs = {kBufferProcsVersion, kBufferProcsCount, allocate_buffer, lock_buffer, unlock_buffer,
                        free_buffer, buffer_space};
  state.handle_procs = {kHandleProcsVersion, kHandleProcsCount, new_handle, dispose_handle, handle_size,
                        set_handle_size, lock_handle, unlock_handle, recover_space, dispose_handle};
  state.property_procs = {kPropertyProcsVersion, kPropertyProcsCount, get_property, set_property};
  state.basic_suite = {acquire_suite, release_suite, is_equal, allocate_block, free_block, reallocate_block,
                       undefined_suite_call};
  state.platform.hwnd = reinterpret_cast<void*>(static_cast<std::uintptr_t>(request.parent_window));
  std::memset(state.error_string, 0, sizeof(state.error_string));

  record.serialNumber = 1;
  record.abortProc = test_abort;
  record.progressProc = report_progress;
  record.parameters = request.parameters.empty() ? nullptr : handle_from_bytes(request.parameters);
  record.imageSize = {static_cast<std::int16_t>(image.height), static_cast<std::int16_t>(image.width)};
  record.planes = static_cast<std::int16_t>(image.planes);
  record.filterRect = {static_cast<std::int16_t>(request.filter_top), static_cast<std::int16_t>(request.filter_left),
                       static_cast<std::int16_t>(request.filter_bottom),
                       static_cast<std::int16_t>(request.filter_right)};
  const auto to16 = [](std::uint8_t v) { return static_cast<std::uint16_t>(v * 257); };
  record.background = {to16(request.background[0]), to16(request.background[1]), to16(request.background[2])};
  record.foreground = {to16(request.foreground[0]), to16(request.foreground[1]), to16(request.foreground[2])};
  record.maxSpace = buffer_space();
  record.bufferSpace = buffer_space();
  record.isFloating = 0;
  record.haveMask = request.has_mask != 0 ? 1 : 0;
  record.autoMask = 0;
  for (int i = 0; i < 3; ++i) {
    record.backColor[i] = request.background[i];
    record.foreColor[i] = request.foreground[i];
  }
  record.backColor[3] = 0;
  record.foreColor[3] = 0;
  record.hostSig = kHostSignature;
  record.hostProc = nullptr;
  record.imageMode = kModeRGBColor;
  record.imageHRes = request.resolution_fixed;
  record.imageVRes = request.resolution_fixed;
  record.floatCoord = {0, 0};
  record.wholeSize = record.imageSize;
  record.monitor.gamma = static_cast<Fixed>(2.2 * 65536.0);
  record.platformData = &state.platform;
  record.bufferProcs = &state.buffer_procs;
  record.resourceProcs = nullptr;
  record.processEvent = process_event;
  record.displayPixels = display_pixels;
  record.handleProcs = &state.handle_procs;
  record.supportsDummyChannels = 0;
  record.supportsAlternateLayouts = 0;
  record.wantLayout = kLayoutTraditional;
  record.filterCase = static_cast<std::int16_t>(request.filter_case);
  record.dummyPlaneValue = -1;
  record.premiereHook = nullptr;
  record.advanceState = advance_state;
  record.supportsAbsolute = 0;
  record.wantsAbsolute = 0;
  record.getPropertyObsolete = get_property;
  record.cannotUndo = 0;
  record.supportsPadding = 1;
  record.inputPadding = kPaddingEdgeReplication;
  record.outputPadding = kPaddingEdgeReplication;
  record.maskPadding = kPaddingEdgeReplication;
  record.samplingSupport = 0;
  record.inputRate = 1 << 16;
  record.maskRate = 1 << 16;
  record.colorServices = color_services;
  const bool transparency = image.planes == 4;
  record.inLayerPlanes = transparency ? 3 : 0;
  record.inTransparencyMask = transparency ? 1 : 0;
  record.inNonLayerPlanes = transparency ? 0 : 3;
  record.outLayerPlanes = record.inLayerPlanes;
  record.outTransparencyMask = record.inTransparencyMask;
  record.outNonLayerPlanes = record.inNonLayerPlanes;
  record.absLayerPlanes = record.inLayerPlanes;
  record.absTransparencyMask = record.inTransparencyMask;
  record.absNonLayerPlanes = record.inNonLayerPlanes;
  record.imageServicesProcs = nullptr;
  record.propertyProcs = &state.property_procs;
  const auto tile = static_cast<std::int16_t>(std::min<std::int32_t>(256, std::max<std::int32_t>(1, std::max(image.width, image.height))));
  record.inTileHeight = record.inTileWidth = tile;
  record.absTileHeight = record.absTileWidth = tile;
  record.outTileHeight = record.outTileWidth = tile;
  record.maskTileHeight = record.maskTileWidth = tile;
  record.descriptorParameters = nullptr;
  record.errorString = &state.error_string;
  record.channelPortProcs = nullptr;
  record.documentInfo = nullptr;
  record.sSPBasic = &state.basic_suite;
  record.plugInRef = nullptr;
  record.depth = 8;
}

std::wstring to_wide(const std::u16string& text) { return std::wstring(text.begin(), text.end()); }

FilterEntryProc resolve_entry(HMODULE module, const std::string& entry_point) {
  if (!entry_point.empty()) {
    if (auto* proc = GetProcAddress(module, entry_point.c_str()); proc != nullptr) {
      return reinterpret_cast<FilterEntryProc>(proc);
    }
  }
  for (const char* name : {"PluginMain", "ENTRYPOINT", "PlugInMain", "main"}) {
    if (auto* proc = GetProcAddress(module, name); proc != nullptr) {
      return reinterpret_cast<FilterEntryProc>(proc);
    }
  }
  if (auto* proc = GetProcAddress(module, MAKEINTRESOURCEA(1)); proc != nullptr) {
    return reinterpret_cast<FilterEntryProc>(proc);
  }
  return nullptr;
}

}  // namespace

RunResult run_filter(const RunRequest& request, const RunnerImage& image, const RunnerCallbacks& callbacks) {
  RunResult result;
  if (image.width <= 0 || image.height <= 0 || (image.planes != 3 && image.planes != 4) || image.input == nullptr ||
      image.output == nullptr) {
    result.status = kRunError;
    result.message = "Invalid image description.";
    return result;
  }
  if (image.width > 30000 || image.height > 30000) {
    result.status = kRunError;
    result.message = "The layer is too large for a classic plug-in (30000 pixels per side).";
    return result;
  }

  // The plug-in's folder is the working directory: classic plug-ins reach their
  // data, presets and help files through relative paths as often as through
  // their module path.
  const std::wstring plugin_path = to_wide(request.plugin_path);
  if (const auto slash = plugin_path.find_last_of(L"\\/"); slash != std::wstring::npos) {
    SetCurrentDirectoryW(plugin_path.substr(0, slash).c_str());
  }
  HMODULE module = LoadLibraryExW(plugin_path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (module == nullptr) {
    result.status = kRunError;
    result.message = "The plug-in could not be loaded (Windows error " + std::to_string(GetLastError()) + ").";
    return result;
  }
  FilterEntryProc entry = resolve_entry(module, request.entry_point);
  if (entry == nullptr) {
    FreeLibrary(module);
    result.status = kRunError;
    result.message = "The plug-in has no filter entry point.";
    return result;
  }

  HostState state;
  state.request = &request;
  state.image = image;
  state.callbacks = &callbacks;
  state.writes_outside_selection = (request.case_flags1 & 0x08U) != 0;
  state.protect_alpha = (request.flags & kRunProtectAlpha) != 0;
  g_state = &state;
  prepare_input();
  setup_record(state);
  // Plug-in windows belong on the monitor that shows Patchy (screen_shim.hpp).
  install_screen_shim(static_cast<HWND>(state.platform.hwnd), request.screen_max_width, request.screen_max_height,
                      to_wide(request.window_title).c_str());

  std::intptr_t data = 0;
  OSErr code = kNoErr;
  bool started = false;
  DWORD fault = 0;
  static const char* const selector_names[] = {"about", "parameters", "prepare", "start", "continue", "finish"};
  const auto call = [&](std::int16_t selector) {
    code = kNoErr;
    fault = call_entry_guarded(entry, selector, &state.record, &data, &code);
    refresh_screen_shim();  // modules the plug-in loaded behind our back (COM servers)
    if (trace_enabled()) {
      if (fault != 0) {
        std::fprintf(stderr, "[8bf] %s faulted with %s %s\n", selector_names[selector], hex_code(fault).c_str(),
                     describe_last_fault().c_str());
      }
      trace(selector_names[selector], state.record, code);
    }
    return fault == 0 && code == kNoErr;
  };

  bool ok = true;
  if ((request.flags & kRunShowDialog) != 0) {
    ok = call(kSelectorParameters);
  }
  if (ok) {
    ok = call(kSelectorPrepare);
  }
  if (ok) {
    ok = call(kSelectorStart);
    started = ok;
    // A plug-in may do all of its work inside Start through advanceState and
    // return with the rectangles cleared (KPT does): the buffer handed out by
    // the last advanceState still holds its output.
    commit_output();
  }
  if (ok && (!rect_empty(state.record.inRect) || !rect_empty(state.record.outRect))) {
    fill_input();
    fill_output();
    fill_mask();
    for (;;) {
      ok = call(kSelectorContinue);
      commit_output();
      if (!ok) {
        break;
      }
      if (poll_abort()) {
        code = kUserCanceledErr;
        ok = false;
        break;
      }
      if (rect_empty(state.record.inRect) && rect_empty(state.record.outRect)) {
        break;
      }
      fill_input();
      fill_output();
      fill_mask();
    }
  }
  if (started && fault == 0) {
    OSErr finish_code = code;
    const DWORD finish_fault = call_entry_guarded(entry, kSelectorFinish, &state.record, &data, &finish_code);
    if (finish_fault != 0 && fault == 0) {
      fault = finish_fault;
    }
  }

  // Output handling of the chosen case: FillMask makes the filtered area opaque.
  if (ok && image.planes == 4 && request.case_output_handling == 9 && !state.protect_alpha) {
    for (std::int32_t y = std::max<std::int32_t>(0, request.filter_top);
         y < std::min<std::int32_t>(image.height, request.filter_bottom); ++y) {
      for (std::int32_t x = std::max<std::int32_t>(0, request.filter_left);
           x < std::min<std::int32_t>(image.width, request.filter_right); ++x) {
        output_pixel(x, y)[3] = 255;
      }
    }
  }

  if (auto* block = block_of(state.record.parameters); block != nullptr && block->size > 0) {
    result.parameters.assign(block->data, block->data + block->size);
  }

  if (fault != 0) {
    result.status = kRunError;
    result.message = "The plug-in crashed (exception " + hex_code(fault) + " " + describe_last_fault() + ").";
  } else if (code == kUserCanceledErr || state.aborted) {
    result.status = kRunCancelled;
  } else if (code != kNoErr) {
    result.status = kRunError;
    result.message = error_message_for(code, state.error_string);
  } else {
    result.status = kRunOk;
  }

  // Release what the plug-in left behind before unloading it.
  for (auto* block : state.handles) {
    std::free(block->data);
    delete block;
  }
  state.handles.clear();
  for (auto* buffer : state.buffers) {
    std::free(buffer);
  }
  state.buffers.clear();
  g_state = nullptr;
  FreeLibrary(module);
  return result;
}

}  // namespace patchy::legacy_host
