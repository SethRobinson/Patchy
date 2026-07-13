// Windows HEIF/HEIC decode through WIC (compiled on Windows only, see CMakeLists). The
// codecs come from the Microsoft Store "HEIF Image Extensions" (container) and "HEVC
// Video Extensions" (bitstream) packages -- in-box on Windows 11 22H2+ -- so Patchy ships
// no HEVC decoder and carries no codec patent license. Two quirks drive the structure:
//   - A stub HEIF codec is always registered, so availability cannot be enumerated; the
//     only reliable probe is attempting the decode. With the HEIF package installed but
//     HEVC missing, decoder creation and GetFrame SUCCEED and only the pixel request
//     fails with MF_E_TOPO_CODEC_NOT_FOUND (the codec invokes the HEVC MFT lazily).
//   - The decoder returns UNROTATED pixels; the container rotation (irot/imir) is
//     surfaced as an EXIF-style value at /heifProps/Orientation, applied here via
//     apply_exif_orientation.

#include "formats/heif_document_io.hpp"

#include <windows.h>

#include <wincodec.h>

#include <algorithm>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace patchy::heif {

namespace {

// MF_E_TOPO_CODEC_NOT_FOUND (mferror.h; redeclared to keep the Media Foundation headers
// out of a WIC-only translation unit).
constexpr HRESULT kMfTopoCodecNotFound = static_cast<HRESULT>(0xC00D5212);

template <typename T>
class ComPtr {
public:
  ComPtr() = default;
  ComPtr(const ComPtr&) = delete;
  ComPtr& operator=(const ComPtr&) = delete;
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

[[nodiscard]] std::string hresult_text(HRESULT hr) {
  std::ostringstream stream;
  stream << "0x" << std::hex << static_cast<unsigned long>(hr);
  return stream.str();
}

[[noreturn]] void throw_decode_error(HRESULT hr, bool container_opened) {
  if (hr == WINCODEC_ERR_COMPONENTNOTFOUND && !container_opened) {
    throw std::runtime_error(std::string(kHeifPackageMissingMarker) +
                             " Opening HEIC images uses Windows' HEIF codec, which is not installed. "
                             "Install the free 'HEIF Image Extensions' package from the Microsoft Store "
                             "and try again.");
  }
  if (hr == kMfTopoCodecNotFound || hr == WINCODEC_ERR_COMPONENTNOTFOUND) {
    // The container parsed but the HEVC bitstream decoder is missing.
    throw std::runtime_error(std::string(kHevcPackageMissingMarker) +
                             " Opening HEIC images uses Windows' HEVC codec, which is not installed. "
                             "Install the 'HEVC Video Extensions' package from the Microsoft Store and "
                             "try again.");
  }
  throw std::runtime_error("Unable to decode this HEIF image (Windows error " + hresult_text(hr) + ")");
}

[[nodiscard]] int read_heif_orientation(IWICBitmapFrameDecode& frame) {
  ComPtr<IWICMetadataQueryReader> query;
  if (FAILED(frame.GetMetadataQueryReader(query.put())) || !query) {
    return 1;
  }
  PROPVARIANT value;
  PropVariantInit(&value);
  // The HEIF metadata block normalizes irot/imir to one EXIF-style 1-8 value; it is
  // authoritative over any EXIF IFD copy in the file.
  if (FAILED(query->GetMetadataByName(L"/heifProps/Orientation", &value))) {
    return 1;
  }
  int orientation = 1;
  switch (value.vt) {
    case VT_UI1:
      orientation = static_cast<int>(value.bVal);
      break;
    case VT_UI2:
      orientation = static_cast<int>(value.uiVal);
      break;
    case VT_UI4:
      orientation = static_cast<int>(value.ulVal);
      break;
    case VT_I2:
      orientation = static_cast<int>(value.iVal);
      break;
    case VT_I4:
      orientation = static_cast<int>(value.lVal);
      break;
    default:
      break;
  }
  PropVariantClear(&value);
  return orientation >= 1 && orientation <= 8 ? orientation : 1;
}

// Wraps `source` in a transform converting the file's color space (iPhone HEICs embed a
// Display P3 ICC profile) to sRGB. Returns false when the file carries no usable profile
// or WIC cannot build the transform; the caller then uses the unmanaged pixels, matching
// the no-profile-means-sRGB convention of the other readers.
[[nodiscard]] bool create_srgb_transform(IWICImagingFactory& factory, IWICBitmapFrameDecode& frame,
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

}  // namespace

FormatReadResult read_heif(std::span<const std::uint8_t> bytes) {
  const CoInitGuard com_guard;

  ComPtr<IWICImagingFactory> factory;
  HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory,
                                factory.put_void());
  if (FAILED(hr) || !factory) {
    throw std::runtime_error("Windows Imaging Component is unavailable (" + hresult_text(hr) + ")");
  }

  ComPtr<IWICStream> stream;
  hr = factory->CreateStream(stream.put());
  if (SUCCEEDED(hr)) {
    // InitializeFromMemory does not copy; `bytes` stays alive for the whole decode.
    hr = stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size()));
  }
  if (FAILED(hr)) {
    throw std::runtime_error("Unable to buffer the HEIF file (" + hresult_text(hr) + ")");
  }

  ComPtr<IWICBitmapDecoder> decoder;
  hr = factory->CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnDemand, decoder.put());
  if (FAILED(hr) || !decoder) {
    throw_decode_error(hr, /*container_opened*/ false);
  }

  UINT frame_count = 0;
  if (FAILED(decoder->GetFrameCount(&frame_count))) {
    frame_count = 1;
  }

  ComPtr<IWICBitmapFrameDecode> frame;
  hr = decoder->GetFrame(0, frame.put());
  if (FAILED(hr) || !frame) {
    throw_decode_error(hr, /*container_opened*/ true);
  }

  UINT width = 0;
  UINT height = 0;
  hr = frame->GetSize(&width, &height);
  if (FAILED(hr)) {
    throw_decode_error(hr, /*container_opened*/ true);
  }
  constexpr std::uint64_t kMaxPixels = 268'435'456;  // 256 Mpx (a 1 GiB RGBA buffer)
  if (width == 0 || height == 0 ||
      static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) > kMaxPixels) {
    throw std::runtime_error("This HEIF image's dimensions are not supported");
  }

  const int orientation = read_heif_orientation(*frame.get());

  double dpi_x = 0.0;
  double dpi_y = 0.0;
  if (FAILED(frame->GetResolution(&dpi_x, &dpi_y))) {
    dpi_x = 0.0;
    dpi_y = 0.0;
  }

  // Normalize to straight-alpha BGRA first (the converter accepts every native HEIF
  // format, including the 8bpc view of 10-bit files), then color-correct to sRGB when the
  // file embeds a profile.
  ComPtr<IWICFormatConverter> converter;
  hr = factory->CreateFormatConverter(converter.put());
  if (SUCCEEDED(hr)) {
    hr = converter->Initialize(frame.get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                               WICBitmapPaletteTypeCustom);
  }
  if (FAILED(hr)) {
    throw_decode_error(hr, /*container_opened*/ true);
  }
  IWICBitmapSource* pixel_source = converter.get();
  ComPtr<IWICColorTransform> color_transform;
  if (create_srgb_transform(*factory.get(), *frame.get(), *pixel_source, color_transform)) {
    pixel_source = color_transform.get();
  }

  const std::size_t stride = static_cast<std::size_t>(width) * 4U;
  std::vector<std::uint8_t> bgra(stride * static_cast<std::size_t>(height));
  hr = pixel_source->CopyPixels(nullptr, static_cast<UINT>(stride), static_cast<UINT>(bgra.size()), bgra.data());
  if (FAILED(hr) && color_transform) {
    // Some codec/profile combinations fail only at pixel delivery; retry unmanaged before
    // concluding anything about missing codecs.
    pixel_source = converter.get();
    hr = pixel_source->CopyPixels(nullptr, static_cast<UINT>(stride), static_cast<UINT>(bgra.size()), bgra.data());
  }
  if (FAILED(hr)) {
    throw_decode_error(hr, /*container_opened*/ true);
  }

  // BGRA -> RGBA in place, then apply the container orientation (unrotated files move the
  // buffer straight through; a 48 MP phone photo should not pay a copy for nothing).
  for (std::size_t offset = 0; offset + 3 < bgra.size(); offset += 4) {
    std::swap(bgra[offset], bgra[offset + 2]);
  }
  OrientedImage oriented;
  if (orientation == 1) {
    oriented.width = static_cast<std::int32_t>(width);
    oriented.height = static_cast<std::int32_t>(height);
    oriented.rgba = std::move(bgra);
  } else {
    oriented = apply_exif_orientation(bgra, static_cast<std::int32_t>(width), static_cast<std::int32_t>(height),
                                      orientation);
    bgra.clear();
    bgra.shrink_to_fit();
  }

  bool has_alpha = false;
  for (std::size_t offset = 3; offset < oriented.rgba.size(); offset += 4) {
    if (oriented.rgba[offset] != 0xFF) {
      has_alpha = true;
      break;
    }
  }

  PixelBuffer pixels(oriented.width, oriented.height, has_alpha ? PixelFormat::rgba8() : PixelFormat::rgb8());
  for (std::int32_t y = 0; y < oriented.height; ++y) {
    const auto* source = oriented.rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(oriented.width) * 4U;
    auto row = pixels.row(y);
    if (has_alpha) {
      std::memcpy(row.data(), source, row.size());
    } else {
      for (std::int32_t x = 0; x < oriented.width; ++x) {
        std::memcpy(row.data() + static_cast<std::size_t>(x) * 3U, source + static_cast<std::size_t>(x) * 4U, 3U);
      }
    }
  }

  FormatReadResult result;
  result.document = Document(oriented.width, oriented.height, has_alpha ? PixelFormat::rgba8() : PixelFormat::rgb8());
  if (dpi_x > 1.0 && dpi_y > 1.0) {
    result.document.print_settings().horizontal_ppi = dpi_x;
    result.document.print_settings().vertical_ppi = dpi_y;
  }
  result.document.add_pixel_layer("Background", std::move(pixels));
  if (frame_count > 1) {
    result.notices.push_back("Opened the primary image only (" + std::to_string(frame_count) +
                             " images in the file)");
  }
  return result;
}

}  // namespace patchy::heif
