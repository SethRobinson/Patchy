#include "ui/webgpu_document_compositor.hpp"

#include "core/environment.hpp"

#include <QByteArray>
#include <QDebug>
#include <QThread>
#include <QtGlobal>

#ifdef PATCHY_WEBGPU_AVAILABLE
#include <webgpu/webgpu.h>
#ifdef PATCHY_DAWN_NATIVE_AVAILABLE
#include <dawn/native/VulkanBackend.h>
#endif
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace patchy::ui {

#ifdef PATCHY_WEBGPU_AVAILABLE
namespace {

constexpr uint32_t kWorkgroupSize = 8;
constexpr uint32_t kBytesPerPixel = 4;
constexpr uint32_t kBytesPerRowAlignment = 256;

constexpr char kDocumentComposeShader[] = R"WGSL(
struct Params {
  layerOrigin: vec2<i32>,
  maskOrigin: vec2<i32>,
  layerOpacity: f32,
  blendMode: u32,
  maskDefault: f32,
  maskDensity: f32,
  hasMask: u32,
  hasBlendIf: u32,
  padding: vec2<u32>,
  blendIfGrayThis: vec4<f32>,
  blendIfRedThis: vec4<f32>,
  blendIfGreenThis: vec4<f32>,
  blendIfBlueThis: vec4<f32>,
  blendIfGrayUnderlying: vec4<f32>,
  blendIfRedUnderlying: vec4<f32>,
  blendIfGreenUnderlying: vec4<f32>,
  blendIfBlueUnderlying: vec4<f32>,
  outputOrigin: vec2<i32>,
  outputPadding: vec2<u32>,
};

@group(0) @binding(0) var<uniform> params: Params;
@group(0) @binding(1) var sourceTexture: texture_2d<f32>;
@group(0) @binding(2) var backdropTexture: texture_2d<f32>;
@group(0) @binding(3) var maskTexture: texture_2d<f32>;
@group(0) @binding(4) var outputTexture: texture_storage_2d<rgba8unorm, write>;

fn colorDodge(source: f32, backdrop: f32) -> f32 {
  if (backdrop <= 0.0) { return 0.0; }
  if (source >= 1.0) { return 1.0; }
  return min(1.0, backdrop / max(0.000001, 1.0 - source));
}

fn colorBurn(source: f32, backdrop: f32) -> f32 {
  if (backdrop >= 1.0) { return 1.0; }
  if (source <= 0.0) { return 0.0; }
  return 1.0 - min(1.0, (1.0 - backdrop) / max(0.000001, source));
}

fn softLight(source: f32, backdrop: f32) -> f32 {
  if (source <= 0.5) {
    return backdrop - (1.0 - 2.0 * source) * backdrop * (1.0 - backdrop);
  }
  var base = sqrt(max(backdrop, 0.0));
  if (backdrop <= 0.25) {
    base = ((16.0 * backdrop - 12.0) * backdrop + 4.0) * backdrop;
  }
  return backdrop + (2.0 * source - 1.0) * (base - backdrop);
}

fn blendChannel(source: f32, backdrop: f32, mode: u32) -> f32 {
  if (mode == 1u) { return source; }
  if (mode == 2u) {
    let sourceByte = u32(clamp(floor(source * 255.0 + 0.5), 0.0, 255.0));
    let backdropByte = u32(clamp(floor(backdrop * 255.0 + 0.5), 0.0, 255.0));
    return f32((sourceByte * backdropByte) / 255u) / 255.0;
  }
  if (mode == 3u) { return source + backdrop - source * backdrop; }
  if (mode == 4u) { return select(2.0 * source * backdrop, 1.0 - 2.0 * (1.0 - source) * (1.0 - backdrop), backdrop >= 0.5); }
  if (mode == 5u) { return min(source, backdrop); }
  if (mode == 6u) { return max(source, backdrop); }
  if (mode == 7u) { return colorDodge(source, backdrop); }
  if (mode == 8u) { return colorBurn(source, backdrop); }
  if (mode == 9u) { return select(2.0 * source * backdrop, 1.0 - 2.0 * (1.0 - source) * (1.0 - backdrop), source >= 0.5); }
  if (mode == 10u) { return softLight(source, backdrop); }
  if (mode == 11u) { return abs(backdrop - source); }
  if (mode == 12u) { return max(0.0, source + backdrop - 1.0); }
  if (mode == 13u) { return select(min(backdrop, 2.0 * source), max(backdrop, 2.0 * (source - 0.5)), source >= 0.5); }
  if (mode == 16u) { return source + backdrop - 2.0 * source * backdrop; }
  if (mode == 19u) { return min(1.0, source + backdrop); }
  if (mode == 20u) { return max(0.0, backdrop - source); }
  if (mode == 21u) { return select(1.0, min(1.0, backdrop / source), source > 0.0); }
  if (mode == 22u) { return select(colorBurn(2.0 * source, backdrop), colorDodge(2.0 * (source - 0.5), backdrop), source >= 0.5); }
  if (mode == 23u) { return clamp(backdrop + 2.0 * source - 1.0, 0.0, 1.0); }
  if (mode == 24u) {
    let vivid = select(1.0 - min(1.0, (1.0 - backdrop) / max(0.000001, 2.0 * source)),
                       min(1.0, backdrop / max(0.000001, 2.0 * (1.0 - source))),
                       source >= 0.5);
    return select(0.0, 1.0, vivid > 0.5);
  }
  return source;
}

fn blendColor(source: vec3<f32>, backdrop: vec3<f32>, mode: u32) -> vec3<f32> {
  return vec3<f32>(blendChannel(source.r, backdrop.r, mode),
                   blendChannel(source.g, backdrop.g, mode),
                   blendChannel(source.b, backdrop.b, mode));
}

fn blendIfThresholdAlphaByte(thresholds: vec4<f32>, value: u32) -> u32 {
  let blackLow = u32(thresholds.x);
  let blackHigh = u32(thresholds.y);
  let whiteLow = u32(thresholds.z);
  let whiteHigh = u32(thresholds.w);
  if (value < blackLow || value > whiteHigh) { return 0u; }
  if (value < blackHigh) {
    let numerator = value - blackLow + 1u;
    let denominator = blackHigh - blackLow + 1u;
    return (numerator * 255u) / denominator;
  }
  if (value > whiteLow) {
    let numerator = whiteHigh - value + 1u;
    let denominator = whiteHigh - whiteLow + 1u;
    return (numerator * 255u) / denominator;
  }
  return 255u;
}

fn colorByte(value: f32) -> u32 {
  return u32(clamp(floor(value * 255.0 + 0.5), 0.0, 255.0));
}

fn blendIfColorFactor(color: vec3<f32>, source: bool) -> f32 {
  let red = colorByte(color.r);
  let green = colorByte(color.g);
  let blue = colorByte(color.b);
  let gray = (299u * red + 590u * green + 111u * blue + 500u) / 1000u;
  var factor = 255u;
  factor = (factor * blendIfThresholdAlphaByte(select(params.blendIfGrayUnderlying, params.blendIfGrayThis, source), gray)) / 255u;
  factor = (factor * blendIfThresholdAlphaByte(select(params.blendIfRedUnderlying, params.blendIfRedThis, source), red)) / 255u;
  factor = (factor * blendIfThresholdAlphaByte(select(params.blendIfGreenUnderlying, params.blendIfGreenThis, source), green)) / 255u;
  factor = (factor * blendIfThresholdAlphaByte(select(params.blendIfBlueUnderlying, params.blendIfBlueThis, source), blue)) / 255u;
  return f32(factor) / 255.0;
}

fn maskCoverage(coord: vec2<i32>) -> f32 {
  if (params.hasMask == 0u) { return 1.0; }
  let dimensions = textureDimensions(maskTexture);
  if (dimensions.x == 0 || dimensions.y == 0) { return clamp(params.maskDefault, 0.0, 1.0); }
  let relative = coord - params.maskOrigin;
  let inside = relative.x >= 0 && relative.y >= 0 && relative.x < i32(dimensions.x) && relative.y < i32(dimensions.y);
  let safeRelative = clamp(relative, vec2<i32>(0), vec2<i32>(i32(dimensions.x) - 1, i32(dimensions.y) - 1));
  let coverage = select(params.maskDefault, textureLoad(maskTexture, safeRelative, 0).r, inside);
  return clamp(coverage * params.maskDensity + (1.0 - params.maskDensity), 0.0, 1.0);
}

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) invocation: vec3<u32>) {
  let outputDimensions = textureDimensions(outputTexture);
  if (invocation.x >= outputDimensions.x || invocation.y >= outputDimensions.y) { return; }
  let localCoord = vec2<i32>(invocation.xy);
  let coord = localCoord + params.outputOrigin;
  let sourceDimensions = textureDimensions(sourceTexture);
  let sourceRelative = coord - params.layerOrigin;
  let sourceInside = sourceRelative.x >= 0 && sourceRelative.y >= 0 &&
                     sourceRelative.x < i32(sourceDimensions.x) && sourceRelative.y < i32(sourceDimensions.y);
  let safeSourceRelative = clamp(sourceRelative, vec2<i32>(0),
                                 vec2<i32>(i32(sourceDimensions.x) - 1, i32(sourceDimensions.y) - 1));
  let sourceSample = select(vec4<f32>(0.0), textureLoad(sourceTexture, safeSourceRelative, 0), sourceInside);
  let backdropSample = textureLoad(backdropTexture, localCoord, 0);
  let coverage = maskCoverage(coord);
  var sourceAlpha = clamp(sourceSample.a * params.layerOpacity * coverage, 0.0, 1.0);
  let backdropAlpha = clamp(backdropSample.a, 0.0, 1.0);
  let sourceColor = select(vec3<f32>(0.0), sourceSample.rgb / max(sourceSample.a, 0.000001), sourceAlpha > 0.000001);
  let backdropColor = select(vec3<f32>(0.0), backdropSample.rgb / max(backdropAlpha, 0.000001), backdropAlpha > 0.000001);
  if (params.hasBlendIf != 0u) {
    sourceAlpha = sourceAlpha * blendIfColorFactor(sourceColor, true);
    let underlyingFactor = blendIfColorFactor(backdropColor, false);
    sourceAlpha = sourceAlpha * ((1.0 - backdropAlpha) + backdropAlpha * underlyingFactor);
  }
  let blended = blendColor(sourceColor, backdropColor, params.blendMode);
  let outputAlpha = sourceAlpha + backdropAlpha * (1.0 - sourceAlpha);
  let outputRgb = blended * sourceAlpha * backdropAlpha +
                  sourceColor * sourceAlpha * (1.0 - backdropAlpha) +
                  backdropColor * backdropAlpha * (1.0 - sourceAlpha);
  textureStore(outputTexture, localCoord, vec4<f32>(outputRgb, outputAlpha));
}
)WGSL";

QString string_view_to_qstring(WGPUStringView value) {
  if (value.data == nullptr || value.length == 0) {
    return {};
  }
  return QString::fromUtf8(value.data, static_cast<qsizetype>(value.length));
}

WGPUStringView string_view(const char* text) {
  return WGPUStringView{text, std::strlen(text)};
}

template <typename Handle, void (*Release)(Handle)>
class WgpuHandle final {
public:
  WgpuHandle() = default;
  explicit WgpuHandle(Handle value) : value_(value) {}
  ~WgpuHandle() { reset(); }

  WgpuHandle(const WgpuHandle&) = delete;
  WgpuHandle& operator=(const WgpuHandle&) = delete;

  WgpuHandle(WgpuHandle&& other) noexcept : value_(other.value_) { other.value_ = nullptr; }
  WgpuHandle& operator=(WgpuHandle&& other) noexcept {
    if (this != &other) {
      reset();
      value_ = other.value_;
      other.value_ = nullptr;
    }
    return *this;
  }

  [[nodiscard]] Handle get() const noexcept { return value_; }
  [[nodiscard]] explicit operator bool() const noexcept { return value_ != nullptr; }
  void reset(Handle value = nullptr) noexcept {
    if (value_ != nullptr) {
      Release(value_);
    }
    value_ = value;
  }

private:
  Handle value_{nullptr};
};

using InstanceHandle = WgpuHandle<WGPUInstance, wgpuInstanceRelease>;
using AdapterHandle = WgpuHandle<WGPUAdapter, wgpuAdapterRelease>;
using DeviceHandle = WgpuHandle<WGPUDevice, wgpuDeviceRelease>;
using QueueHandle = WgpuHandle<WGPUQueue, wgpuQueueRelease>;
using ShaderModuleHandle = WgpuHandle<WGPUShaderModule, wgpuShaderModuleRelease>;
using BindGroupLayoutHandle = WgpuHandle<WGPUBindGroupLayout, wgpuBindGroupLayoutRelease>;
using PipelineLayoutHandle = WgpuHandle<WGPUPipelineLayout, wgpuPipelineLayoutRelease>;
using ComputePipelineHandle = WgpuHandle<WGPUComputePipeline, wgpuComputePipelineRelease>;
using BufferHandle = WgpuHandle<WGPUBuffer, wgpuBufferRelease>;
using TextureHandle = WgpuHandle<WGPUTexture, wgpuTextureRelease>;
using TextureViewHandle = WgpuHandle<WGPUTextureView, wgpuTextureViewRelease>;
using BindGroupHandle = WgpuHandle<WGPUBindGroup, wgpuBindGroupRelease>;
using CommandEncoderHandle = WgpuHandle<WGPUCommandEncoder, wgpuCommandEncoderRelease>;
using ComputePassHandle = WgpuHandle<WGPUComputePassEncoder, wgpuComputePassEncoderRelease>;
using CommandBufferHandle = WgpuHandle<WGPUCommandBuffer, wgpuCommandBufferRelease>;

struct AdapterRequest {
  WGPUAdapter adapter{nullptr};
  WGPURequestAdapterStatus status{WGPURequestAdapterStatus_Error};
  QString message;
};

#ifdef PATCHY_DAWN_NATIVE_AVAILABLE
template <typename Handle>
std::uintptr_t native_handle_key(Handle handle) noexcept {
  if constexpr (std::is_pointer_v<Handle>) {
    return reinterpret_cast<std::uintptr_t>(handle);
  } else {
    return static_cast<std::uintptr_t>(handle);
  }
}
#endif

void on_adapter_request(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message,
                        void* userdata1, void*) {
  auto* result = static_cast<AdapterRequest*>(userdata1);
  result->status = status;
  result->adapter = adapter;
  result->message = string_view_to_qstring(message);
}

struct DeviceRequest {
  WGPUDevice device{nullptr};
  WGPURequestDeviceStatus status{WGPURequestDeviceStatus_Error};
  QString message;
};

void on_device_request(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message,
                       void* userdata1, void*) {
  auto* result = static_cast<DeviceRequest*>(userdata1);
  result->status = status;
  result->device = device;
  result->message = string_view_to_qstring(message);
}

struct MapRequest {
  WGPUMapAsyncStatus status{WGPUMapAsyncStatus_Error};
  QString message;
};

void on_map_request(WGPUMapAsyncStatus status, WGPUStringView message, void* userdata1, void*) {
  auto* result = static_cast<MapRequest*>(userdata1);
  result->status = status;
  result->message = string_view_to_qstring(message);
}

struct QueueRequest {
  WGPUQueueWorkDoneStatus status{WGPUQueueWorkDoneStatus_Error};
  QString message;
};

void on_queue_done(WGPUQueueWorkDoneStatus status, WGPUStringView message, void* userdata1, void*) {
  auto* result = static_cast<QueueRequest*>(userdata1);
  result->status = status;
  result->message = string_view_to_qstring(message);
}

void on_uncaptured_error(WGPUDevice const*, WGPUErrorType type, WGPUStringView message, void*, void*) {
  qWarning().noquote() << "Patchy WebGPU error" << static_cast<int>(type) << string_view_to_qstring(message);
}

bool wait_for_future(WGPUInstance instance, WGPUFuture future, QString* reason) {
  WGPUFutureWaitInfo wait = WGPU_FUTURE_WAIT_INFO_INIT;
  wait.future = future;
  for (int attempt = 0; attempt < 60000; ++attempt) {
    wait.completed = WGPU_FALSE;
    const auto status = wgpuInstanceWaitAny(instance, 1, &wait, 0);
    if (status == WGPUWaitStatus_Success && wait.completed) {
      return true;
    }
    if (status != WGPUWaitStatus_TimedOut && status != WGPUWaitStatus_Success) {
      break;
    }
    QThread::msleep(1);
  }
  if (reason != nullptr) {
    *reason = QStringLiteral("WebGPU asynchronous operation did not complete");
  }
  return false;
}

bool hardware_adapter(WGPUAdapterInfo info, QString* reason) {
  const auto name = string_view_to_qstring(info.device);
  const auto description = string_view_to_qstring(info.description);
  const auto combined = (name + QLatin1Char(' ') + description).toLower();
  constexpr const char* software_markers[] = {
      "llvmpipe", "softpipe", "lavapipe", "swiftshader", "software", "warp", "cpu", "softgpu"};
  for (const auto* marker : software_markers) {
    if (combined.contains(QString::fromLatin1(marker))) {
      if (reason != nullptr) {
        *reason = QStringLiteral("WebGPU selected a software adapter: ") + name;
      }
      return false;
    }
  }
  if (info.adapterType == WGPUAdapterType_CPU || info.adapterType == WGPUAdapterType_Unknown) {
    if (reason != nullptr) {
      *reason = QStringLiteral("WebGPU did not expose a hardware adapter") +
                (name.isEmpty() ? QString{} : QStringLiteral(": ") + name);
    }
    return false;
  }
  return true;
}

struct Params {
  int32_t layer_origin_x{0};
  int32_t layer_origin_y{0};
  int32_t mask_origin_x{0};
  int32_t mask_origin_y{0};
  float layer_opacity{1.0F};
  uint32_t blend_mode{1};
  float mask_default{1.0F};
  float mask_density{1.0F};
  uint32_t has_mask{0};
  uint32_t has_blend_if{0};
  uint32_t padding[2]{0, 0};
  std::array<std::array<float, 4>, 8> blend_if{};
  int32_t output_origin_x{0};
  int32_t output_origin_y{0};
  uint32_t output_padding[2]{0, 0};
};
static_assert(sizeof(Params) == 192);

struct TextureData {
  TextureHandle texture;
  TextureViewHandle view;
  QSize size;
};

struct BindGroupKey {
  std::uintptr_t uniform{0};
  std::uintptr_t source{0};
  std::uintptr_t backdrop{0};
  std::uintptr_t mask{0};
  std::uintptr_t output{0};

  friend bool operator==(const BindGroupKey&, const BindGroupKey&) = default;
};

struct BindGroupKeyHash {
  std::size_t operator()(const BindGroupKey& key) const noexcept {
    std::size_t hash = key.uniform;
    const auto combine = [&hash](std::uintptr_t value) {
      hash ^= static_cast<std::size_t>(value) + static_cast<std::size_t>(0x9e3779b9U) +
              (hash << 6U) + (hash >> 2U);
    };
    combine(key.source);
    combine(key.backdrop);
    combine(key.mask);
    combine(key.output);
    return hash;
  }
};

struct CachedLayerResources {
  TextureData source;
  TextureData mask;
  std::unordered_map<patchy::TileKey, BufferHandle, patchy::TileKeyHash> uniforms_by_tile;
  std::unordered_map<BindGroupKey, BindGroupHandle, BindGroupKeyHash> bind_groups;
  std::uint64_t pixel_revision{0};
  std::uint64_t mask_revision{0};
  bool has_mask{false};
};

std::uint64_t texture_size_key(QSize size) {
  return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(size.width())) << 32U) |
         static_cast<std::uint32_t>(size.height());
}

std::size_t padded_upload_bytes(QSize size, uint32_t channels) {
  const auto row_bytes = static_cast<std::size_t>(size.width()) * channels;
  const auto padded_row_bytes = ((row_bytes + kBytesPerRowAlignment - 1U) / kBytesPerRowAlignment) *
                                kBytesPerRowAlignment;
  return padded_row_bytes * static_cast<std::size_t>(size.height());
}

class WebGpuImplementation final {
public:
  bool initialize(QString* reason) {
    WGPUInstanceDescriptor instance_descriptor = WGPU_INSTANCE_DESCRIPTOR_INIT;
    instance_.reset(wgpuCreateInstance(&instance_descriptor));
    if (!instance_) {
      return fail(reason, QStringLiteral("Dawn could not create a WebGPU instance"));
    }

    WGPURequestAdapterOptions adapter_options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    adapter_options.powerPreference = WGPUPowerPreference_HighPerformance;
    AdapterRequest adapter_request;
    WGPURequestAdapterCallbackInfo adapter_callback = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    adapter_callback.mode = WGPUCallbackMode_WaitAnyOnly;
    adapter_callback.callback = on_adapter_request;
    adapter_callback.userdata1 = &adapter_request;
    if (!wait_for_future(instance_.get(), wgpuInstanceRequestAdapter(instance_.get(), &adapter_options, adapter_callback), reason)) {
      return false;
    }
    if (adapter_request.status != WGPURequestAdapterStatus_Success || adapter_request.adapter == nullptr) {
      return fail(reason, adapter_request.message.isEmpty() ? QStringLiteral("Dawn could not find a WebGPU adapter")
                                                            : adapter_request.message);
    }
    adapter_.reset(adapter_request.adapter);

    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    if (wgpuAdapterGetInfo(adapter_.get(), &info) == WGPUStatus_Success) {
      adapter_name_ = string_view_to_qstring(info.device);
      native_backend_name_ = backend_name(info.backendType);
      adapter_identity_observed_ = true;
      adapter_vendor_id_ = info.vendorID;
      adapter_device_id_ = info.deviceID;
      QString hardware_reason;
      const bool hardware = hardware_adapter(info, &hardware_reason);
      wgpuAdapterInfoFreeMembers(info);
      if (!hardware) {
        return fail(reason, hardware_reason);
      }
    } else {
      adapter_name_ = QStringLiteral("Unknown WebGPU adapter");
      native_backend_name_ = QStringLiteral("Unknown native API");
      return fail(reason, QStringLiteral("Dawn could not inspect the WebGPU adapter"));
    }

    WGPUDeviceDescriptor device_descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    device_descriptor.uncapturedErrorCallbackInfo.callback = on_uncaptured_error;
    DeviceRequest device_request;
    WGPURequestDeviceCallbackInfo device_callback = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    device_callback.mode = WGPUCallbackMode_WaitAnyOnly;
    device_callback.callback = on_device_request;
    device_callback.userdata1 = &device_request;
    if (!wait_for_future(instance_.get(), wgpuAdapterRequestDevice(adapter_.get(), &device_descriptor, device_callback), reason)) {
      return false;
    }
    if (device_request.status != WGPURequestDeviceStatus_Success || device_request.device == nullptr) {
      return fail(reason, device_request.message.isEmpty() ? QStringLiteral("Dawn could not create a WebGPU device")
                                                           : device_request.message);
    }
    device_.reset(device_request.device);
    queue_.reset(wgpuDeviceGetQueue(device_.get()));
    if (!queue_) {
      return fail(reason, QStringLiteral("Dawn did not expose a WebGPU queue"));
    }
    if (!create_pipeline(reason)) {
      return false;
    }
    return true;
  }

  [[nodiscard]] bool compose(const CanvasGpuDocument& document, QImage& output, QString* reason) {
    const auto width = document.document_size.width();
    const auto height = document.document_size.height();
    if (width <= 0 || height <= 0) {
      return fail(reason, QStringLiteral("WebGPU document dimensions are invalid"));
    }
    const patchy::GpuTileScheduler scheduler(256);
    const auto plan = scheduler.full_plan(patchy::Rect{0, 0, width, height});
    return compose_tiles(document, plan, nullptr, output, reason);
  }

  [[nodiscard]] bool compose_tiles(const CanvasGpuDocument& document,
                                   const patchy::GpuTileRenderPlan& plan,
                                   const QImage* previous_frame, QImage& output, QString* reason) {
    last_metrics_ = {};
    const auto composition_start = std::chrono::steady_clock::now();
    if (!device_ || !pipeline_ || !queue_) {
      return fail(reason, QStringLiteral("WebGPU compositor is not initialized"));
    }
    const auto width = document.document_size.width();
    const auto height = document.document_size.height();
    if (width <= 0 || height <= 0) {
      return fail(reason, QStringLiteral("WebGPU document dimensions are invalid"));
    }

    QImage result(width, height, QImage::Format_RGBA8888_Premultiplied);
    result.fill(Qt::transparent);
    if (previous_frame != nullptr && previous_frame->size() == result.size()) {
      result = previous_frame->convertToFormat(result.format());
    }

    struct LayerResources {
      std::uint64_t id{0};
      Params params;
    };
    std::vector<LayerResources> layers;
    layers.reserve(document.layers.size());
    std::unordered_set<std::uint64_t> active_layer_ids;
    const auto copy_thresholds = [](const CanvasGpuBlendIfThresholds& thresholds) {
      return std::array<float, 4>{static_cast<float>(thresholds.black_low),
                                  static_cast<float>(thresholds.black_high),
                                  static_cast<float>(thresholds.white_low),
                                  static_cast<float>(thresholds.white_high)};
    };

    for (const auto& layer : document.layers) {
      if (layer.image.isNull() || layer.rect.isEmpty() || layer.opacity <= 0.0) {
        continue;
      }
      active_layer_ids.insert(layer.id);
      const auto source_image = layer.image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
      auto& cached = layer_resource_cache_[layer.id];
      const auto pixel_revision = layer.pixel_revision != 0 ? layer.pixel_revision : layer.revision;
      const bool source_matches = cached.source.texture && cached.source.view &&
                                  cached.pixel_revision == pixel_revision &&
                                  cached.source.size == source_image.size();
      if (!source_matches) {
        cached.bind_groups.clear();
        cached.source = create_texture(source_image.size(), WGPUTextureFormat_RGBA8Unorm,
                                       WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst, reason);
        if (!cached.source.texture || !cached.source.view) {
          return false;
        }
        if (!write_texture(cached.source.texture.get(), image_bytes(source_image), source_image.size(),
                           WGPUTextureFormat_RGBA8Unorm, reason)) {
          return false;
        }
        cached.pixel_revision = pixel_revision;
        last_metrics_.source_upload_bytes += padded_upload_bytes(source_image.size(), 4U);
      } else {
        ++last_metrics_.source_texture_reuses;
      }

      QImage mask_image = layer.mask_image;
      if (mask_image.isNull()) {
        mask_image = QImage(1, 1, QImage::Format_Grayscale8);
        mask_image.fill(255);
      } else {
        mask_image = mask_image.convertToFormat(QImage::Format_Grayscale8);
      }
      const auto mask_revision = layer.mask_revision != 0 ? layer.mask_revision : layer.content_revision;
      const bool mask_matches = cached.mask.texture && cached.mask.view &&
                                cached.mask_revision == mask_revision &&
                                cached.has_mask == layer.has_mask && cached.mask.size == mask_image.size();
      if (!mask_matches) {
        cached.bind_groups.clear();
        cached.mask = create_texture(mask_image.size(), WGPUTextureFormat_R8Unorm,
                                     WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst, reason);
        if (!cached.mask.texture || !cached.mask.view) {
          return false;
        }
        if (!write_texture(cached.mask.texture.get(), image_bytes(mask_image), mask_image.size(),
                           WGPUTextureFormat_R8Unorm, reason)) {
          return false;
        }
        cached.mask_revision = mask_revision;
        cached.has_mask = layer.has_mask;
        last_metrics_.mask_upload_bytes += padded_upload_bytes(mask_image.size(), 1U);
      } else {
        ++last_metrics_.mask_texture_reuses;
      }

      LayerResources resources;
      resources.id = layer.id;
      auto& params = resources.params;
      params.layer_origin_x = static_cast<int32_t>(std::llround(layer.document_rect.left()));
      params.layer_origin_y = static_cast<int32_t>(std::llround(layer.document_rect.top()));
      params.mask_origin_x = static_cast<int32_t>(std::llround(layer.mask_document_rect.left()));
      params.mask_origin_y = static_cast<int32_t>(std::llround(layer.mask_document_rect.top()));
      params.layer_opacity = static_cast<float>(std::clamp(layer.opacity, 0.0, 1.0));
      params.blend_mode = static_cast<uint32_t>(std::max(0, layer.blend_mode));
      params.mask_default = static_cast<float>(std::clamp(layer.mask_default, 0.0, 1.0));
      params.mask_density = static_cast<float>(std::clamp(layer.mask_density, 0.0, 1.0));
      params.has_mask = layer.has_mask ? 1U : 0U;
      params.has_blend_if = layer.has_blend_if ? 1U : 0U;
      for (std::size_t index = 0; index < layer.blend_if.size(); ++index) {
        params.blend_if[index * 2U] = copy_thresholds(layer.blend_if[index].this_layer);
        params.blend_if[index * 2U + 1U] = copy_thresholds(layer.blend_if[index].underlying_layer);
      }
      layers.push_back(std::move(resources));
    }

    for (auto it = layer_resource_cache_.begin(); it != layer_resource_cache_.end();) {
      if (!active_layer_ids.contains(it->first)) {
        it = layer_resource_cache_.erase(it);
      } else {
        ++it;
      }
    }

    struct PendingTile {
      patchy::TileKey key;
      patchy::Rect rect;
      QSize size;
      std::uint32_t padded_row_bytes{0};
      std::uint64_t readback_size{0};
      BufferHandle readback;
      TextureData spare_backdrop;
      TextureData backdrop;
    };
    std::vector<PendingTile> pending_tiles;
    pending_tiles.reserve(plan.tiles.size());
    CommandEncoderHandle encoder;

    for (const auto key : plan.tiles) {
      if (key.mip != 0) {
        return fail(reason, QStringLiteral("Dawn tile composition currently supports only mip 0"));
      }
      const auto tile_rect = plan.tile_rect(key);
      if (tile_rect.empty()) {
        continue;
      }
      const QSize tile_size(tile_rect.width, tile_rect.height);
      auto backdrop = acquire_scratch_texture(tile_size, reason);
      if (!backdrop.texture || !backdrop.view) {
        return false;
      }
      QImage clear_image(tile_size, QImage::Format_RGBA8888_Premultiplied);
      clear_image.fill(Qt::transparent);
      if (!write_texture(backdrop.texture.get(), image_bytes(clear_image), tile_size,
                         WGPUTextureFormat_RGBA8Unorm, reason)) {
        return false;
      }
      last_metrics_.clear_upload_bytes += padded_upload_bytes(tile_size, 4U);

      const auto padded_row_bytes = ((static_cast<uint32_t>(tile_size.width()) * kBytesPerPixel +
                                      kBytesPerRowAlignment - 1U) /
                                     kBytesPerRowAlignment) *
                                    kBytesPerRowAlignment;
      const auto readback_size = static_cast<uint64_t>(padded_row_bytes) * static_cast<uint32_t>(tile_size.height());
      auto readback = acquire_readback_buffer(static_cast<std::size_t>(readback_size));
      if (!readback) {
        return fail(reason, QStringLiteral("WebGPU could not allocate a tile readback buffer"));
      }
      if (!encoder) {
        encoder.reset(wgpuDeviceCreateCommandEncoder(device_.get(), nullptr));
        if (!encoder) {
          return fail(reason, QStringLiteral("WebGPU could not create a batch command encoder"));
        }
      }

      pending_tiles.push_back(PendingTile{key, tile_rect, tile_size, padded_row_bytes, readback_size,
                                          std::move(readback), {}, std::move(backdrop)});
      auto& pending = pending_tiles.back();

      for (const auto& layer : layers) {
        auto& cached = layer_resource_cache_.at(layer.id);
        auto& uniforms = cached.uniforms_by_tile[key];
        if (!uniforms) {
          WGPUBufferDescriptor uniform_descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
          uniform_descriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
          uniform_descriptor.size = sizeof(Params);
          uniforms.reset(wgpuDeviceCreateBuffer(device_.get(), &uniform_descriptor));
          if (!uniforms) {
            return fail(reason, QStringLiteral("WebGPU could not allocate compositor uniforms"));
          }
        } else {
          ++last_metrics_.uniform_buffer_reuses;
        }
        auto params = layer.params;
        params.output_origin_x = tile_rect.x;
        params.output_origin_y = tile_rect.y;
        wgpuQueueWriteBuffer(queue_.get(), uniforms.get(), 0, &params, sizeof(params));
      }

      for (const auto& layer : layers) {
        auto& cached = layer_resource_cache_.at(layer.id);
        auto& uniforms = cached.uniforms_by_tile.at(key);
        auto target = pending.spare_backdrop.texture ? std::move(pending.spare_backdrop)
                                                     : acquire_scratch_texture(tile_size, reason);
        if (!target.texture || !target.view) {
          return false;
        }

        WGPUBindGroupEntry bindings[5] = {};
        for (uint32_t index = 0; index < 5; ++index) {
          bindings[index] = WGPU_BIND_GROUP_ENTRY_INIT;
          bindings[index].binding = index;
        }
        bindings[0].buffer = uniforms.get();
        bindings[0].size = sizeof(Params);
        bindings[1].textureView = cached.source.view.get();
        bindings[2].textureView = pending.backdrop.view.get();
        bindings[3].textureView = cached.mask.view.get();
        bindings[4].textureView = target.view.get();
        const BindGroupKey bind_group_key{reinterpret_cast<std::uintptr_t>(bindings[0].buffer),
                                          reinterpret_cast<std::uintptr_t>(bindings[1].textureView),
                                          reinterpret_cast<std::uintptr_t>(bindings[2].textureView),
                                          reinterpret_cast<std::uintptr_t>(bindings[3].textureView),
                                          reinterpret_cast<std::uintptr_t>(bindings[4].textureView)};
        WGPUBindGroup bind_group = nullptr;
        if (const auto cached_bind_group = cached.bind_groups.find(bind_group_key);
            cached_bind_group != cached.bind_groups.end()) {
          bind_group = cached_bind_group->second.get();
          ++last_metrics_.bind_group_reuses;
        } else {
          WGPUBindGroupDescriptor bind_group_descriptor = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
          bind_group_descriptor.layout = bind_group_layout_.get();
          bind_group_descriptor.entryCount = std::size(bindings);
          bind_group_descriptor.entries = bindings;
          BindGroupHandle created_bind_group(wgpuDeviceCreateBindGroup(device_.get(), &bind_group_descriptor));
          if (!created_bind_group) {
            return fail(reason, QStringLiteral("WebGPU could not create a compositor bind group"));
          }
          bind_group = created_bind_group.get();
          cached.bind_groups.emplace(bind_group_key, std::move(created_bind_group));
        }
        WGPUComputePassDescriptor pass_descriptor = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        ComputePassHandle pass(wgpuCommandEncoderBeginComputePass(encoder.get(), &pass_descriptor));
        if (!pass) {
          return fail(reason, QStringLiteral("WebGPU could not begin a compositor compute pass"));
        }
        wgpuComputePassEncoderSetPipeline(pass.get(), pipeline_.get());
        wgpuComputePassEncoderSetBindGroup(pass.get(), 0, bind_group, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(
            pass.get(), (static_cast<uint32_t>(tile_size.width()) + kWorkgroupSize - 1U) / kWorkgroupSize,
            (static_cast<uint32_t>(tile_size.height()) + kWorkgroupSize - 1U) / kWorkgroupSize, 1);
        wgpuComputePassEncoderEnd(pass.get());
        pass.reset();
        pending.spare_backdrop = std::move(pending.backdrop);
        pending.backdrop = std::move(target);
      }

      WGPUTexelCopyTextureInfo source_info = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
      source_info.texture = pending.backdrop.texture.get();
      WGPUTexelCopyBufferInfo destination_info = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
      destination_info.buffer = pending.readback.get();
      destination_info.layout.bytesPerRow = pending.padded_row_bytes;
      destination_info.layout.rowsPerImage = static_cast<uint32_t>(tile_size.height());
      const WGPUExtent3D copy_size{static_cast<uint32_t>(tile_size.width()),
                                   static_cast<uint32_t>(tile_size.height()), 1};
      wgpuCommandEncoderCopyTextureToBuffer(encoder.get(), &source_info, &destination_info, &copy_size);
    }

    if (!pending_tiles.empty()) {
      CommandBufferHandle commands(wgpuCommandEncoderFinish(encoder.get(), nullptr));
      if (!commands) {
        return fail(reason, QStringLiteral("WebGPU could not finish the batch command buffer"));
      }
      WGPUCommandBuffer command = commands.get();
      wgpuQueueSubmit(queue_.get(), 1, &command);
      ++last_metrics_.queue_submissions;
      if (!wait_for_queue(reason)) {
        return false;
      }

      for (auto& pending : pending_tiles) {
        MapRequest map_request;
        WGPUBufferMapCallbackInfo map_callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        map_callback.mode = WGPUCallbackMode_WaitAnyOnly;
        map_callback.callback = on_map_request;
        map_callback.userdata1 = &map_request;
        if (!wait_for_future(instance_.get(),
                             wgpuBufferMapAsync(pending.readback.get(), WGPUMapMode_Read, 0, pending.readback_size,
                                                map_callback),
                             reason)) {
          return false;
        }
        if (map_request.status != WGPUMapAsyncStatus_Success) {
          return fail(reason, map_request.message.isEmpty() ? QStringLiteral("WebGPU tile readback mapping failed")
                                                            : map_request.message);
        }
        const auto* mapped = static_cast<const std::uint8_t*>(
            wgpuBufferGetConstMappedRange(pending.readback.get(), 0,
                                           static_cast<size_t>(pending.readback_size)));
        if (mapped == nullptr) {
          wgpuBufferUnmap(pending.readback.get());
          return fail(reason, QStringLiteral("WebGPU returned an empty tile readback"));
        }
        QImage tile(pending.size, QImage::Format_RGBA8888_Premultiplied);
        for (int y = 0; y < pending.size.height(); ++y) {
          std::memcpy(tile.scanLine(y), mapped + static_cast<size_t>(y) * pending.padded_row_bytes,
                      static_cast<size_t>(pending.size.width()) * kBytesPerPixel);
        }
        wgpuBufferUnmap(pending.readback.get());
        recycle_readback_buffer(static_cast<std::size_t>(pending.readback_size), std::move(pending.readback));
        recycle_scratch_texture(std::move(pending.spare_backdrop));
        recycle_scratch_texture(std::move(pending.backdrop));
        for (int y = 0; y < pending.rect.height; ++y) {
          std::memcpy(result.scanLine(pending.rect.y + y) + static_cast<qsizetype>(pending.rect.x) * kBytesPerPixel,
                      tile.constScanLine(y), static_cast<size_t>(pending.rect.width) * kBytesPerPixel);
        }
      }
    }

    last_metrics_.composition_time_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - composition_start)
            .count());
    output = std::move(result);
    return true;
  }

  [[nodiscard]] QString adapter_name() const { return adapter_name_; }
  [[nodiscard]] QString native_backend_name() const { return native_backend_name_; }
  [[nodiscard]] WebGpuCompositionMetrics last_metrics() const noexcept { return last_metrics_; }
  [[nodiscard]] DawnVulkanInteropObservation vulkan_interop_observation() const noexcept {
    DawnVulkanInteropObservation observation;
    if (native_backend_name_.compare(QStringLiteral("Vulkan"), Qt::CaseInsensitive) == 0) {
      observation.compositor_api = patchy::GpuPresentationApi::Vulkan;
    }
    observation.device_created = static_cast<bool>(device_);
#ifdef PATCHY_DAWN_NATIVE_AVAILABLE
    if (observation.compositor_api == patchy::GpuPresentationApi::Vulkan && device_) {
      const auto instance = dawn::native::vulkan::GetInstance(device_.get());
      if (instance != VK_NULL_HANDLE) {
        observation.native_instance_observed = true;
        observation.native_instance = native_handle_key(instance);
      }
      observation.adapter_identity_observed = adapter_identity_observed_;
      observation.vendor_id = adapter_vendor_id_;
      observation.device_id = adapter_device_id_;
      observation.external_image_api_available = true;
    }
#endif
    return observation;
  }

private:
  bool fail(QString* reason, QString value) {
    if (reason != nullptr) {
      *reason = std::move(value);
    }
    return false;
  }

  static QString backend_name(WGPUBackendType backend) {
    switch (backend) {
    case WGPUBackendType_D3D12:
      return QStringLiteral("D3D12");
    case WGPUBackendType_Metal:
      return QStringLiteral("Metal");
    case WGPUBackendType_Vulkan:
      return QStringLiteral("Vulkan");
    case WGPUBackendType_OpenGL:
      return QStringLiteral("OpenGL");
    case WGPUBackendType_OpenGLES:
      return QStringLiteral("OpenGLES");
    case WGPUBackendType_D3D11:
      return QStringLiteral("D3D11");
    case WGPUBackendType_WebGPU:
      return QStringLiteral("WebGPU");
    default:
      return QStringLiteral("Unknown");
    }
  }

  bool create_pipeline(QString* reason) {
    WGPUShaderSourceWGSL shader_source = WGPU_SHADER_SOURCE_WGSL_INIT;
    shader_source.code = string_view(kDocumentComposeShader);
    WGPUShaderModuleDescriptor shader_descriptor = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    shader_descriptor.nextInChain = &shader_source.chain;
    ShaderModuleHandle shader(wgpuDeviceCreateShaderModule(device_.get(), &shader_descriptor));
    if (!shader) {
      return fail(reason, QStringLiteral("WebGPU could not compile the document WGSL shader"));
    }

    std::array<WGPUBindGroupLayoutEntry, 5> entries{};
    for (uint32_t index = 0; index < entries.size(); ++index) {
      entries[index] = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
      entries[index].binding = index;
      entries[index].visibility = WGPUShaderStage_Compute;
    }
    entries[0].buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    for (std::size_t index = 1; index <= 3; ++index) {
      entries[index].texture = WGPU_TEXTURE_BINDING_LAYOUT_INIT;
      entries[index].texture.sampleType = WGPUTextureSampleType_Float;
      entries[index].texture.viewDimension = WGPUTextureViewDimension_2D;
    }
    entries[4].storageTexture = WGPU_STORAGE_TEXTURE_BINDING_LAYOUT_INIT;
    entries[4].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    entries[4].storageTexture.format = WGPUTextureFormat_RGBA8Unorm;
    entries[4].storageTexture.viewDimension = WGPUTextureViewDimension_2D;

    WGPUBindGroupLayoutDescriptor layout_descriptor = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    layout_descriptor.entryCount = entries.size();
    layout_descriptor.entries = entries.data();
    bind_group_layout_.reset(wgpuDeviceCreateBindGroupLayout(device_.get(), &layout_descriptor));
    if (!bind_group_layout_) {
      return fail(reason, QStringLiteral("WebGPU could not create the compositor bind group layout"));
    }

    WGPUPipelineLayoutDescriptor pipeline_layout_descriptor = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    pipeline_layout_descriptor.bindGroupLayoutCount = 1;
    WGPUBindGroupLayout layout = bind_group_layout_.get();
    pipeline_layout_descriptor.bindGroupLayouts = &layout;
    pipeline_layout_.reset(wgpuDeviceCreatePipelineLayout(device_.get(), &pipeline_layout_descriptor));
    if (!pipeline_layout_) {
      return fail(reason, QStringLiteral("WebGPU could not create the compositor pipeline layout"));
    }

    WGPUComputePipelineDescriptor pipeline_descriptor = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipeline_descriptor.layout = pipeline_layout_.get();
    pipeline_descriptor.compute.module = shader.get();
    pipeline_descriptor.compute.entryPoint = string_view("main");
    pipeline_.reset(wgpuDeviceCreateComputePipeline(device_.get(), &pipeline_descriptor));
    if (!pipeline_) {
      return fail(reason, QStringLiteral("WebGPU could not create the document compute pipeline"));
    }

    return true;
  }

  TextureData create_texture(QSize size, WGPUTextureFormat format, WGPUTextureUsage usage, QString* reason) {
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.usage = usage;
    descriptor.dimension = WGPUTextureDimension_2D;
    descriptor.size = WGPUExtent3D{static_cast<uint32_t>(size.width()), static_cast<uint32_t>(size.height()), 1};
    descriptor.format = format;
    descriptor.mipLevelCount = 1;
    descriptor.sampleCount = 1;
    TextureData result;
    result.size = size;
    result.texture.reset(wgpuDeviceCreateTexture(device_.get(), &descriptor));
    if (!result.texture) {
      if (reason != nullptr) {
        *reason = QStringLiteral("WebGPU could not allocate a document texture");
      }
      return result;
    }
    result.view.reset(wgpuTextureCreateView(result.texture.get(), nullptr));
    if (!result.view && reason != nullptr) {
      *reason = QStringLiteral("WebGPU could not create a document texture view");
    }
    return result;
  }

  static QByteArray image_bytes(const QImage& image) {
    const auto bytes_per_line = image.width() * static_cast<int>(image.depth() / 8U);
    QByteArray bytes;
    bytes.resize(image.height() * bytes_per_line);
    for (int y = 0; y < image.height(); ++y) {
      std::memcpy(bytes.data() + y * bytes_per_line, image.constScanLine(y), static_cast<size_t>(bytes_per_line));
    }
    return bytes;
  }

  TextureData acquire_scratch_texture(QSize size, QString* reason) {
    auto& pool = scratch_texture_pool_[texture_size_key(size)];
    if (!pool.empty()) {
      auto result = std::move(pool.back());
      pool.pop_back();
      ++last_metrics_.scratch_texture_reuses;
      return result;
    }
    return create_texture(size, WGPUTextureFormat_RGBA8Unorm,
                          WGPUTextureUsage_StorageBinding | WGPUTextureUsage_TextureBinding |
                              WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc,
                          reason);
  }

  void recycle_scratch_texture(TextureData texture) {
    if (texture.texture && texture.view) {
      scratch_texture_pool_[texture_size_key(texture.size)].push_back(std::move(texture));
    }
  }

  BufferHandle acquire_readback_buffer(std::size_t size) {
    auto& pool = readback_buffer_pool_[size];
    if (!pool.empty()) {
      auto result = std::move(pool.back());
      pool.pop_back();
      ++last_metrics_.readback_buffer_reuses;
      return result;
    }
    WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    descriptor.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    descriptor.size = size;
    return BufferHandle(wgpuDeviceCreateBuffer(device_.get(), &descriptor));
  }

  void recycle_readback_buffer(std::size_t size, BufferHandle buffer) {
    if (buffer) {
      readback_buffer_pool_[size].push_back(std::move(buffer));
    }
  }

  bool write_texture(WGPUTexture texture, const QByteArray& bytes, QSize size, WGPUTextureFormat format,
                     QString* reason) {
    const uint32_t channels = format == WGPUTextureFormat_R8Unorm ? 1U : 4U;
    const uint32_t source_row_bytes = static_cast<uint32_t>(size.width()) * channels;
    const uint32_t padded_row_bytes = ((source_row_bytes + kBytesPerRowAlignment - 1U) / kBytesPerRowAlignment) *
                                      kBytesPerRowAlignment;
    QByteArray padded(static_cast<qsizetype>(padded_row_bytes) * size.height(), '\0');
    for (int y = 0; y < size.height(); ++y) {
      std::memcpy(padded.data() + static_cast<qsizetype>(y) * padded_row_bytes,
                  bytes.constData() + static_cast<qsizetype>(y) * source_row_bytes, source_row_bytes);
    }
    WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    destination.texture = texture;
    WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    layout.bytesPerRow = padded_row_bytes;
    layout.rowsPerImage = static_cast<uint32_t>(size.height());
    const WGPUExtent3D write_size{static_cast<uint32_t>(size.width()), static_cast<uint32_t>(size.height()), 1};
    wgpuQueueWriteTexture(queue_.get(), &destination, padded.constData(), static_cast<size_t>(padded.size()), &layout,
                          &write_size);
    Q_UNUSED(reason);
    return true;
  }

  bool wait_for_queue(QString* reason) {
    ++last_metrics_.queue_waits;
    QueueRequest request;
    WGPUQueueWorkDoneCallbackInfo callback = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_WaitAnyOnly;
    callback.callback = on_queue_done;
    callback.userdata1 = &request;
    if (!wait_for_future(instance_.get(), wgpuQueueOnSubmittedWorkDone(queue_.get(), callback), reason)) {
      return false;
    }
    if (request.status != WGPUQueueWorkDoneStatus_Success) {
      return fail(reason, request.message.isEmpty() ? QStringLiteral("WebGPU queue submission failed")
                                                    : request.message);
    }
    return true;
  }

  InstanceHandle instance_;
  AdapterHandle adapter_;
  DeviceHandle device_;
  QueueHandle queue_;
  BindGroupLayoutHandle bind_group_layout_;
  PipelineLayoutHandle pipeline_layout_;
  ComputePipelineHandle pipeline_;
  QString adapter_name_;
  QString native_backend_name_;
  bool adapter_identity_observed_{false};
  std::uint32_t adapter_vendor_id_{0};
  std::uint32_t adapter_device_id_{0};
  WebGpuCompositionMetrics last_metrics_;
  std::unordered_map<std::uint64_t, CachedLayerResources> layer_resource_cache_;
  std::unordered_map<std::uint64_t, std::vector<TextureData>> scratch_texture_pool_;
  std::unordered_map<std::size_t, std::vector<BufferHandle>> readback_buffer_pool_;
};

}  // namespace
#endif

std::unique_ptr<WebGpuDocumentCompositor> WebGpuDocumentCompositor::create(QString* failure_reason) {
#ifndef PATCHY_WEBGPU_AVAILABLE
  if (failure_reason != nullptr) {
    *failure_reason = QStringLiteral("Dawn/WebGPU was not found at configure time");
  }
  return nullptr;
#else
  auto implementation = std::make_unique<WebGpuImplementation>();
  if (!implementation->initialize(failure_reason)) {
    return nullptr;
  }
  auto result = std::unique_ptr<WebGpuDocumentCompositor>(new WebGpuDocumentCompositor(implementation.release()));
  qInfo().noquote() << "Patchy WebGPU document compositor:" << result->adapter_name()
                    << ", native API:" << result->native_backend_name();
  return result;
#endif
}

bool WebGpuDocumentCompositor::should_try_automatically() {
  auto requested = patchy::environment_variable("PATCHY_RENDER_BACKEND");
  if (!requested.has_value()) {
    requested = patchy::environment_variable("PATCHY_GPU_CANVAS");
  }
  if (!requested.has_value()) {
    return true;
  }
  const auto value = QString::fromStdString(*requested).trimmed().toLower();
  return value.isEmpty() || value == QStringLiteral("auto") || value == QStringLiteral("gpu") ||
         value == QStringLiteral("webgpu");
}

WebGpuDocumentCompositor::WebGpuDocumentCompositor(void* implementation) : implementation_(implementation) {}

WebGpuDocumentCompositor::~WebGpuDocumentCompositor() {
#ifdef PATCHY_WEBGPU_AVAILABLE
  delete static_cast<WebGpuImplementation*>(implementation_);
#endif
  implementation_ = nullptr;
}

bool WebGpuDocumentCompositor::available() const noexcept {
#ifdef PATCHY_WEBGPU_AVAILABLE
  return implementation_ != nullptr;
#else
  return false;
#endif
}

QString WebGpuDocumentCompositor::adapter_name() const {
#ifdef PATCHY_WEBGPU_AVAILABLE
  return implementation_ != nullptr ? static_cast<WebGpuImplementation*>(implementation_)->adapter_name()
                                    : QString{};
#else
  return {};
#endif
}

QString WebGpuDocumentCompositor::native_backend_name() const {
#ifdef PATCHY_WEBGPU_AVAILABLE
  return implementation_ != nullptr ? static_cast<WebGpuImplementation*>(implementation_)->native_backend_name()
                                    : QString{};
#else
  return {};
#endif
}

bool WebGpuDocumentCompositor::compose(const CanvasGpuDocument& document, QImage& output, QString* failure_reason) {
#ifdef PATCHY_WEBGPU_AVAILABLE
  if (implementation_ == nullptr) {
    if (failure_reason != nullptr) {
      *failure_reason = QStringLiteral("WebGPU compositor is unavailable");
    }
    return false;
  }
  return static_cast<WebGpuImplementation*>(implementation_)->compose(document, output, failure_reason);
#else
  Q_UNUSED(document);
  Q_UNUSED(output);
  if (failure_reason != nullptr) {
    *failure_reason = QStringLiteral("Dawn/WebGPU was not found at configure time");
  }
  return false;
#endif
}

bool WebGpuDocumentCompositor::compose_tiles(const CanvasGpuDocument& document,
                                             const patchy::GpuTileRenderPlan& plan,
                                             const QImage* previous_frame, QImage& output,
                                             QString* failure_reason) {
#ifdef PATCHY_WEBGPU_AVAILABLE
  if (implementation_ == nullptr) {
    if (failure_reason != nullptr) {
      *failure_reason = QStringLiteral("WebGPU compositor is unavailable");
    }
    return false;
  }
  return static_cast<WebGpuImplementation*>(implementation_)->compose_tiles(document, plan, previous_frame, output,
                                                                              failure_reason);
#else
  Q_UNUSED(document);
  Q_UNUSED(plan);
  Q_UNUSED(previous_frame);
  Q_UNUSED(output);
  if (failure_reason != nullptr) {
    *failure_reason = QStringLiteral("Dawn/WebGPU was not found at configure time");
  }
  return false;
#endif
}

WebGpuCompositionMetrics WebGpuDocumentCompositor::last_metrics() const noexcept {
#ifdef PATCHY_WEBGPU_AVAILABLE
  return implementation_ != nullptr ? static_cast<WebGpuImplementation*>(implementation_)->last_metrics()
                                    : WebGpuCompositionMetrics{};
#else
  return {};
#endif
}

DawnVulkanInteropObservation WebGpuDocumentCompositor::vulkan_interop_observation() const noexcept {
#ifdef PATCHY_WEBGPU_AVAILABLE
  return implementation_ != nullptr
             ? static_cast<WebGpuImplementation*>(implementation_)->vulkan_interop_observation()
             : DawnVulkanInteropObservation{};
#else
  return {};
#endif
}

}  // namespace patchy::ui
