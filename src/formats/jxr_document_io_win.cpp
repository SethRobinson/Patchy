// Windows JPEG XR decode and encode through WIC (compiled on Windows only, see
// CMakeLists). Unlike HEIF the codec is in-box: CLSID_WICWmpDecoder and the matching
// encoder have shipped with Windows since Vista, so there is no Store package to probe, no
// missing-codec marker, and no patent surface -- Patchy ships no JPEG XR codec of its own.
//
// One thing drives the structure: JPEG XR channels may be float, and NVIDIA's in-game
// capture uses exactly that (32-bit float scRGB, or 16-bit half from the Windows Game Bar).
// Patchy's pipeline is 8-bit, so the reader splits on the frame's numeric representation:
//   - integer sources take the same path as HEIF (converter to 32bppBGRA, then the file's
//     ICC profile to sRGB), and
//   - float sources convert to 128bppRGBAFloat and go through the platform-neutral
//     scRGB tone map, with NO color transform: scRGB is implied by the pixel format rather
//     than an embedded profile, so a transform would double-correct.

#include "formats/jxr_document_io.hpp"

#include "formats/wic_com.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace patchy::jxr {

using wic::CoInitGuard;
using wic::ComPtr;
using wic::create_srgb_transform;
using wic::hresult_text;

namespace {

// 256 Mpx, a 1 GiB RGBA buffer. Same ceiling as the HEIF reader.
constexpr std::uint64_t kMaxPixels = 268'435'456;

[[nodiscard]] ComPtr<IWICImagingFactory> create_factory() {
  ComPtr<IWICImagingFactory> factory;
  const HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory,
                                      factory.put_void());
  if (FAILED(hr) || !factory) {
    throw std::runtime_error("Windows Imaging Component is unavailable (" + hresult_text(hr) + ")");
  }
  return factory;
}

[[noreturn]] void throw_decode_error(HRESULT hr) {
  throw std::runtime_error("Unable to decode this JPEG XR image (Windows error " + hresult_text(hr) + ")");
}

[[noreturn]] void throw_encode_error(HRESULT hr) {
  throw std::runtime_error("Unable to write this JPEG XR image (Windows error " + hresult_text(hr) + ")");
}

// Splits the decode into its tone-mapped and straight-conversion halves. WIC exposes the
// frame's numeric representation through the pixel format's component info, which is more
// robust than matching a list of float GUIDs and keeps working if the codec grows a format.
struct SourceKind {
  bool is_float{false};
  UINT bits_per_channel{8};
};

[[nodiscard]] SourceKind classify_pixel_format(IWICImagingFactory& factory, IWICBitmapFrameDecode& frame) {
  SourceKind kind;
  WICPixelFormatGUID format{};
  if (FAILED(frame.GetPixelFormat(&format))) {
    return kind;
  }

  ComPtr<IWICComponentInfo> component;
  if (FAILED(factory.CreateComponentInfo(format, component.put())) || !component) {
    return kind;
  }
  ComPtr<IWICPixelFormatInfo2> info;
  if (FAILED(component->QueryInterface(IID_IWICPixelFormatInfo2, info.put_void())) || !info) {
    return kind;
  }

  WICPixelFormatNumericRepresentation representation = WICPixelFormatNumericRepresentationUnspecified;
  if (SUCCEEDED(info->GetNumericRepresentation(&representation))) {
    kind.is_float = representation == WICPixelFormatNumericRepresentationFloat ||
                    representation == WICPixelFormatNumericRepresentationFixed;
  }

  UINT bits_per_pixel = 0;
  UINT channel_count = 0;
  if (SUCCEEDED(info->GetBitsPerPixel(&bits_per_pixel)) && SUCCEEDED(info->GetChannelCount(&channel_count)) &&
      channel_count > 0) {
    kind.bits_per_channel = bits_per_pixel / channel_count;
  }
  return kind;
}

// The HEIF reader's 96x96-means-untagged rule: WIC reports exactly 96 DPI when a file
// records no density, so that reading follows Photoshop's 72 PPI convention for untagged
// images. A file genuinely tagged 96 DPI is indistinguishable and gets the same treatment.
void apply_resolution(Document& document, IWICBitmapFrameDecode& frame) {
  double dpi_x = 0.0;
  double dpi_y = 0.0;
  if (FAILED(frame.GetResolution(&dpi_x, &dpi_y))) {
    dpi_x = 0.0;
    dpi_y = 0.0;
  }
  if (dpi_x > 1.0 && dpi_y > 1.0 && !(dpi_x == 96.0 && dpi_y == 96.0)) {
    document.print_settings().horizontal_ppi = dpi_x;
    document.print_settings().vertical_ppi = dpi_y;
  } else {
    document.print_settings().horizontal_ppi = 72.0;
    document.print_settings().vertical_ppi = 72.0;
  }
}

// Straight-to-8-bit path for integer frames, byte for byte what the HEIF reader does:
// normalize to straight-alpha BGRA (the converter accepts every native JPEG XR integer
// format, including the 8bpc view of 16-bit files), color-correct to sRGB when the file
// embeds a profile, then swap to RGBA.
[[nodiscard]] std::vector<std::uint8_t> decode_integer_rgba(IWICImagingFactory& factory,
                                                            IWICBitmapFrameDecode& frame, UINT width, UINT height) {
  ComPtr<IWICFormatConverter> converter;
  HRESULT hr = factory.CreateFormatConverter(converter.put());
  if (SUCCEEDED(hr)) {
    hr = converter->Initialize(&frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                               WICBitmapPaletteTypeCustom);
  }
  if (FAILED(hr)) {
    throw_decode_error(hr);
  }

  IWICBitmapSource* pixel_source = converter.get();
  ComPtr<IWICColorTransform> color_transform;
  if (create_srgb_transform(factory, frame, *pixel_source, color_transform)) {
    pixel_source = color_transform.get();
  }

  const std::size_t stride = static_cast<std::size_t>(width) * 4U;
  std::vector<std::uint8_t> bgra(stride * static_cast<std::size_t>(height));
  hr = pixel_source->CopyPixels(nullptr, static_cast<UINT>(stride), static_cast<UINT>(bgra.size()), bgra.data());
  if (FAILED(hr) && color_transform) {
    // Some codec/profile combinations fail only at pixel delivery; retry unmanaged.
    pixel_source = converter.get();
    hr = pixel_source->CopyPixels(nullptr, static_cast<UINT>(stride), static_cast<UINT>(bgra.size()), bgra.data());
  }
  if (FAILED(hr)) {
    throw_decode_error(hr);
  }

  for (std::size_t offset = 0; offset + 3 < bgra.size(); offset += 4) {
    std::swap(bgra[offset], bgra[offset + 2]);
  }
  return bgra;
}

// HDR path: convert to straight-alpha 128bppRGBAFloat (which also unpremultiplies a
// 128bppPRGBAFloat source) and hand the linear scRGB samples to the shared tone map.
[[nodiscard]] std::vector<std::uint8_t> decode_float_rgba(IWICImagingFactory& factory, IWICBitmapFrameDecode& frame,
                                                          UINT width, UINT height) {
  ComPtr<IWICFormatConverter> converter;
  HRESULT hr = factory.CreateFormatConverter(converter.put());
  if (SUCCEEDED(hr)) {
    hr = converter->Initialize(&frame, GUID_WICPixelFormat128bppRGBAFloat, WICBitmapDitherTypeNone, nullptr, 0.0,
                               WICBitmapPaletteTypeCustom);
  }
  if (FAILED(hr)) {
    throw_decode_error(hr);
  }

  const std::size_t stride = static_cast<std::size_t>(width) * 4U * sizeof(float);
  std::vector<float> samples(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
  hr = converter->CopyPixels(nullptr, static_cast<UINT>(stride),
                             static_cast<UINT>(samples.size() * sizeof(float)),
                             reinterpret_cast<BYTE*>(samples.data()));
  if (FAILED(hr)) {
    throw_decode_error(hr);
  }
  return tone_map_scrgb_to_rgba8(samples, static_cast<std::int32_t>(width), static_cast<std::int32_t>(height));
}

// Sets one IPropertyBag2 encoder option by name.
[[nodiscard]] HRESULT set_encoder_option(IPropertyBag2& bag, const wchar_t* name, VARTYPE type, const VARIANT& value) {
  PROPBAG2 option{};
  option.pstrName = const_cast<LPOLESTR>(name);
  option.dwType = PROPBAG2_TYPE_DATA;
  option.vt = type;
  return bag.Write(1, &option, const_cast<VARIANT*>(&value));
}

}  // namespace

FormatReadResult read_jxr(std::span<const std::uint8_t> bytes) {
  const CoInitGuard com_guard;
  auto factory = create_factory();

  ComPtr<IWICStream> stream;
  HRESULT hr = factory->CreateStream(stream.put());
  if (SUCCEEDED(hr)) {
    // InitializeFromMemory does not copy; `bytes` stays alive for the whole decode.
    hr = stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size()));
  }
  if (FAILED(hr)) {
    throw std::runtime_error("Unable to buffer the JPEG XR file (" + hresult_text(hr) + ")");
  }

  ComPtr<IWICBitmapDecoder> decoder;
  hr = factory->CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnDemand, decoder.put());
  if (FAILED(hr) || !decoder) {
    throw_decode_error(hr);
  }

  UINT frame_count = 0;
  if (FAILED(decoder->GetFrameCount(&frame_count))) {
    frame_count = 1;
  }

  ComPtr<IWICBitmapFrameDecode> frame;
  hr = decoder->GetFrame(0, frame.put());
  if (FAILED(hr) || !frame) {
    throw_decode_error(hr);
  }

  UINT width = 0;
  UINT height = 0;
  hr = frame->GetSize(&width, &height);
  if (FAILED(hr)) {
    throw_decode_error(hr);
  }
  if (width == 0 || height == 0 ||
      static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) > kMaxPixels) {
    throw std::runtime_error("This JPEG XR image's dimensions are not supported");
  }

  const auto kind = classify_pixel_format(*factory.get(), *frame.get());
  const auto rgba = kind.is_float ? decode_float_rgba(*factory.get(), *frame.get(), width, height)
                                  : decode_integer_rgba(*factory.get(), *frame.get(), width, height);

  bool has_alpha = false;
  for (std::size_t offset = 3; offset < rgba.size(); offset += 4) {
    if (rgba[offset] != 0xFF) {
      has_alpha = true;
      break;
    }
  }

  const auto format = has_alpha ? PixelFormat::rgba8() : PixelFormat::rgb8();
  PixelBuffer pixels(static_cast<std::int32_t>(width), static_cast<std::int32_t>(height), format);
  for (std::int32_t y = 0; y < static_cast<std::int32_t>(height); ++y) {
    const auto* source = rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4U;
    auto row = pixels.row(y);
    if (has_alpha) {
      std::memcpy(row.data(), source, row.size());
    } else {
      for (UINT x = 0; x < width; ++x) {
        std::memcpy(row.data() + static_cast<std::size_t>(x) * 3U, source + static_cast<std::size_t>(x) * 4U, 3U);
      }
    }
  }

  FormatReadResult result;
  result.document = Document(static_cast<std::int32_t>(width), static_cast<std::int32_t>(height), format);
  apply_resolution(result.document, *frame.get());
  result.document.add_pixel_layer("Background", std::move(pixels));

  if (kind.is_float) {
    result.notices.push_back(
        "This HDR image was tone mapped to 8-bit sRGB; Patchy edits 8 bits per channel.");
  } else if (kind.bits_per_channel > 8) {
    result.notices.push_back("Converted " + std::to_string(kind.bits_per_channel) +
                             "-bit channels to 8-bit; Patchy edits 8 bits per channel.");
  }
  if (frame_count > 1) {
    result.notices.push_back("Opened the primary image only (" + std::to_string(frame_count) +
                             " images in the file)");
  }
  return result;
}

std::vector<std::uint8_t> write_jxr(std::span<const std::uint8_t> rgba, std::int32_t width, std::int32_t height,
                                    bool has_alpha, double horizontal_ppi, double vertical_ppi,
                                    const WriteOptions& options) {
  if (width <= 0 || height <= 0) {
    throw std::runtime_error("Cannot write an empty document as JPEG XR");
  }
  const auto pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  if (rgba.size() < pixel_count * 4U) {
    throw std::runtime_error("JPEG XR write buffer is too small");
  }

  const CoInitGuard com_guard;
  auto factory = create_factory();

  ComPtr<IStream> memory;
  HRESULT hr = CreateStreamOnHGlobal(nullptr, TRUE, memory.put());
  if (FAILED(hr) || !memory) {
    throw_encode_error(hr);
  }

  ComPtr<IWICBitmapEncoder> encoder;
  hr = factory->CreateEncoder(GUID_ContainerFormatWmp, nullptr, encoder.put());
  if (SUCCEEDED(hr)) {
    hr = encoder->Initialize(memory.get(), WICBitmapEncoderNoCache);
  }
  if (FAILED(hr)) {
    throw_encode_error(hr);
  }

  ComPtr<IWICBitmapFrameEncode> frame;
  ComPtr<IPropertyBag2> properties;
  hr = encoder->CreateNewFrame(frame.put(), properties.put());
  if (FAILED(hr)) {
    throw_encode_error(hr);
  }
  if (properties) {
    VARIANT value;
    VariantInit(&value);
    if (options.lossless) {
      // Lossless overrides ImageQuality, so the two are never set together.
      value.vt = VT_BOOL;
      value.boolVal = VARIANT_TRUE;
      (void)set_encoder_option(*properties.get(), L"Lossless", VT_BOOL, value);
    } else {
      // UseCodecOptions stays false (the default), so the codec maps ImageQuality onto its
      // own Quality/Overlap/Subsampling table.
      value.vt = VT_R4;
      value.fltVal = static_cast<float>(std::clamp(options.quality, 1, 100)) / 100.0F;
      (void)set_encoder_option(*properties.get(), L"ImageQuality", VT_R4, value);
    }
    VariantClear(&value);
  }
  hr = frame->Initialize(properties.get());
  if (SUCCEEDED(hr)) {
    hr = frame->SetSize(static_cast<UINT>(width), static_cast<UINT>(height));
  }
  if (SUCCEEDED(hr) && horizontal_ppi > 0.0 && vertical_ppi > 0.0) {
    hr = frame->SetResolution(horizontal_ppi, vertical_ppi);
  }
  if (FAILED(hr)) {
    throw_encode_error(hr);
  }

  // WIC negotiates: it may not honor the requested format, so the pixels are packed to
  // whatever it settles on rather than to what was asked for.
  WICPixelFormatGUID requested = has_alpha ? GUID_WICPixelFormat32bppBGRA : GUID_WICPixelFormat24bppBGR;
  hr = frame->SetPixelFormat(&requested);
  if (FAILED(hr)) {
    throw_encode_error(hr);
  }
  const bool write_alpha = IsEqualGUID(requested, GUID_WICPixelFormat32bppBGRA) != FALSE;
  if (!write_alpha && !IsEqualGUID(requested, GUID_WICPixelFormat24bppBGR)) {
    throw std::runtime_error("The Windows JPEG XR encoder did not accept a BGR or BGRA frame");
  }

  const std::size_t channels = write_alpha ? 4U : 3U;
  const std::size_t stride = static_cast<std::size_t>(width) * channels;
  std::vector<std::uint8_t> pixels(stride * static_cast<std::size_t>(height));
  for (std::size_t pixel = 0; pixel < pixel_count; ++pixel) {
    const std::uint8_t* source = rgba.data() + pixel * 4U;
    std::uint8_t* destination = pixels.data() + pixel * channels;
    destination[0] = source[2];
    destination[1] = source[1];
    destination[2] = source[0];
    if (write_alpha) {
      destination[3] = source[3];
    }
  }

  hr = frame->WritePixels(static_cast<UINT>(height), static_cast<UINT>(stride), static_cast<UINT>(pixels.size()),
                          pixels.data());
  if (SUCCEEDED(hr)) {
    hr = frame->Commit();
  }
  if (SUCCEEDED(hr)) {
    hr = encoder->Commit();
  }
  if (FAILED(hr)) {
    throw_encode_error(hr);
  }

  HGLOBAL handle = nullptr;
  hr = GetHGlobalFromStream(memory.get(), &handle);
  if (FAILED(hr) || handle == nullptr) {
    throw_encode_error(hr);
  }
  STATSTG stat{};
  hr = memory->Stat(&stat, STATFLAG_NONAME);
  if (FAILED(hr)) {
    throw_encode_error(hr);
  }
  const auto size = static_cast<std::size_t>(stat.cbSize.QuadPart);
  const auto* locked = static_cast<const std::uint8_t*>(GlobalLock(handle));
  if (locked == nullptr) {
    throw std::runtime_error("Unable to read back the encoded JPEG XR image");
  }
  std::vector<std::uint8_t> encoded(locked, locked + size);
  GlobalUnlock(handle);
  return encoded;
}

}  // namespace patchy::jxr
