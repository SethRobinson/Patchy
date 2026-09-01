#pragma once

// Windows-only COM/WIC plumbing shared by the format readers that decode through the
// Windows Imaging Component: HEIF/HEIC (heif_document_io_win.cpp) and JPEG XR
// (jxr_document_io_win.cpp). Header-only and deliberately tiny; anything format-specific
// stays in the reader. Only ever included from a source guarded by WIN32 in CMakeLists.txt,
// where patchy_formats already links windowscodecs and ole32.

#include <windows.h>

#include <objbase.h>
#include <ocidl.h>
#include <wincodec.h>

#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace patchy::wic {

template <typename T>
class ComPtr {
public:
  ComPtr() = default;
  ComPtr(const ComPtr&) = delete;
  ComPtr& operator=(const ComPtr&) = delete;
  // Movable so a helper can hand an interface back by value; copying stays deleted because
  // that would need an AddRef nobody here wants implicitly.
  ComPtr(ComPtr&& other) noexcept : pointer_(std::exchange(other.pointer_, nullptr)) {}
  ComPtr& operator=(ComPtr&& other) noexcept {
    if (this != &other) {
      reset();
      pointer_ = std::exchange(other.pointer_, nullptr);
    }
    return *this;
  }
  ~ComPtr() {
    reset();
  }

  void reset() {
    if (pointer_ != nullptr) {
      pointer_->Release();
      pointer_ = nullptr;
    }
  }

  [[nodiscard]] T** put() {
    reset();
    return &pointer_;
  }

  [[nodiscard]] void** put_void() {
    return reinterpret_cast<void**>(put());
  }

  [[nodiscard]] T* get() const noexcept {
    return pointer_;
  }

  [[nodiscard]] T* operator->() const noexcept {
    return pointer_;
  }

  [[nodiscard]] explicit operator bool() const noexcept {
    return pointer_ != nullptr;
  }

private:
  T* pointer_{nullptr};
};

// Qt initializes COM on the GUI thread; S_FALSE / RPC_E_CHANGED_MODE mean it is already
// up and must not be torn down (the scanner import uses the same pattern).
class CoInitGuard {
public:
  CoInitGuard() : balance_uninitialize_(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {}
  CoInitGuard(const CoInitGuard&) = delete;
  CoInitGuard& operator=(const CoInitGuard&) = delete;
  ~CoInitGuard() {
    if (balance_uninitialize_) {
      CoUninitialize();
    }
  }

private:
  bool balance_uninitialize_{false};
};

[[nodiscard]] inline std::string hresult_text(HRESULT hr) {
  std::ostringstream stream;
  stream << "0x" << std::hex << static_cast<unsigned long>(hr);
  return stream.str();
}

// Wraps `source` in a transform converting the file's color space (iPhone HEICs embed a
// Display P3 ICC profile) to sRGB. Returns false when the file carries no usable profile
// or WIC cannot build the transform; the caller then uses the unmanaged pixels, matching
// the no-profile-means-sRGB convention of the other readers.
//
// Only for integer pixel paths. Float/HDR sources carry their color space in the pixel
// format (scRGB), not in a profile, so running this on them would double-correct.
[[nodiscard]] inline bool create_srgb_transform(IWICImagingFactory& factory, IWICBitmapFrameDecode& frame,
                                                IWICBitmapSource& source, ComPtr<IWICColorTransform>& transform) {
  UINT count = 0;
  if (FAILED(frame.GetColorContexts(0, nullptr, &count)) || count == 0) {
    return false;
  }
  std::vector<ComPtr<IWICColorContext>> contexts(count);
  std::vector<IWICColorContext*> raw_contexts(count, nullptr);
  for (UINT i = 0; i < count; ++i) {
    if (FAILED(factory.CreateColorContext(contexts[i].put()))) {
      return false;
    }
    raw_contexts[i] = contexts[i].get();
  }
  UINT actual = 0;
  if (FAILED(frame.GetColorContexts(count, raw_contexts.data(), &actual)) || actual == 0) {
    return false;
  }

  ComPtr<IWICColorContext> srgb;
  if (FAILED(factory.CreateColorContext(srgb.put())) ||
      FAILED(srgb->InitializeFromExifColorSpace(1))) {  // 1 = sRGB
    return false;
  }
  if (FAILED(factory.CreateColorTransformer(transform.put()))) {
    return false;
  }
  if (FAILED(transform->Initialize(&source, raw_contexts[0], srgb.get(), GUID_WICPixelFormat32bppBGRA))) {
    transform.reset();
    return false;
  }
  return true;
}

}  // namespace patchy::wic
