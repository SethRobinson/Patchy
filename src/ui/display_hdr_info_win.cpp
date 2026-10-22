// Windows half of display_hdr_info.hpp. DXGI (IDXGIOutput6::GetDesc1) gives the output's
// color space (PQ/BT.2020 when "Use HDR" is on) and the panel's luminance range;
// DisplayConfig gives SDR white (the SDR content brightness slider). A fresh DXGI factory
// per query: an old factory keeps reporting the color space it saw when it was created.
// Compiled on Windows only (see CMakeLists).

#include "ui/display_hdr_info.hpp"

#include <QScreen>
#include <qscreen_platform.h>

#include <cwchar>
#include <optional>
#include <vector>

#include <windows.h>

#include <dxgi1_6.h>
#include <wrl/client.h>

namespace patchy::ui {

namespace {

using Microsoft::WRL::ComPtr;

std::optional<double> positive_nits(FLOAT value) {
  if (value > 0.0F) {
    return static_cast<double>(value);
  }
  return std::nullopt;
}

std::optional<DXGI_OUTPUT_DESC1> output_description(HMONITOR monitor) {
  ComPtr<IDXGIFactory1> factory;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    return std::nullopt;
  }
  ComPtr<IDXGIAdapter1> adapter;
  for (UINT adapter_index = 0; factory->EnumAdapters1(adapter_index, &adapter) != DXGI_ERROR_NOT_FOUND;
       ++adapter_index) {
    ComPtr<IDXGIOutput> output;
    for (UINT output_index = 0; adapter->EnumOutputs(output_index, &output) != DXGI_ERROR_NOT_FOUND;
         ++output_index) {
      ComPtr<IDXGIOutput6> output6;
      DXGI_OUTPUT_DESC1 description{};
      if (SUCCEEDED(output.As(&output6)) && SUCCEEDED(output6->GetDesc1(&description)) &&
          description.Monitor == monitor) {
        return description;
      }
    }
  }
  return std::nullopt;
}

// The SDR white level of the display path whose source is `gdi_device_name`.
std::optional<double> sdr_white_nits(const wchar_t* gdi_device_name) {
  std::vector<DISPLAYCONFIG_PATH_INFO> paths;
  std::vector<DISPLAYCONFIG_MODE_INFO> modes;
  LONG status = ERROR_INSUFFICIENT_BUFFER;
  for (int attempt = 0; attempt < 4 && status == ERROR_INSUFFICIENT_BUFFER; ++attempt) {
    UINT32 path_count = 0;
    UINT32 mode_count = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) != ERROR_SUCCESS) {
      return std::nullopt;
    }
    paths.resize(path_count);
    modes.resize(mode_count);
    status = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths.data(), &mode_count, modes.data(),
                                nullptr);
    paths.resize(path_count);
  }
  if (status != ERROR_SUCCESS) {
    return std::nullopt;
  }
  for (const auto& path : paths) {
    DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
    source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
    source.header.size = sizeof(source);
    source.header.adapterId = path.sourceInfo.adapterId;
    source.header.id = path.sourceInfo.id;
    if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS ||
        std::wcscmp(source.viewGdiDeviceName, gdi_device_name) != 0) {
      continue;
    }
    DISPLAYCONFIG_SDR_WHITE_LEVEL white{};
    white.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
    white.header.size = sizeof(white);
    white.header.adapterId = path.targetInfo.adapterId;
    white.header.id = path.targetInfo.id;
    if (DisplayConfigGetDeviceInfo(&white.header) == ERROR_SUCCESS && white.SDRWhiteLevel > 0) {
      return sdr_white_nits_from_display_config(white.SDRWhiteLevel);
    }
    return std::nullopt;
  }
  return std::nullopt;
}

}  // namespace

DisplayHdrCapabilities query_screen_hdr(QScreen* screen) {
  DisplayHdrCapabilities capabilities;
  const auto* native = screen != nullptr ? screen->nativeInterface<QNativeInterface::QWindowsScreen>() : nullptr;
  HMONITOR monitor = native != nullptr ? native->handle() : nullptr;
  if (monitor == nullptr) {
    return capabilities;
  }
  const auto description = output_description(monitor);
  if (!description.has_value()) {
    return capabilities;
  }
  capabilities.queried = true;
  capabilities.hdr_enabled = description->ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
  capabilities.peak_nits = positive_nits(description->MaxLuminance);
  capabilities.full_frame_nits = positive_nits(description->MaxFullFrameLuminance);
  capabilities.min_nits = description->MinLuminance >= 0.0F && description->MaxLuminance > 0.0F
                              ? std::optional<double>(static_cast<double>(description->MinLuminance))
                              : std::nullopt;
  // SDR white only means something while HDR is on: with it off, SDR content is the
  // whole signal and the slider is hidden.
  if (capabilities.hdr_enabled) {
    MONITORINFOEXW monitor_info{};
    monitor_info.cbSize = sizeof(monitor_info);
    if (GetMonitorInfoW(monitor, &monitor_info) != 0) {
      capabilities.sdr_white_nits = sdr_white_nits(monitor_info.szDevice);
    }
  }
  capabilities.headroom =
      headroom_from_nits(capabilities.hdr_enabled, capabilities.peak_nits, capabilities.sdr_white_nits);
  capabilities.potential_headroom = capabilities.headroom;
  capabilities.remote_session = GetSystemMetrics(SM_REMOTESESSION) != 0;
  return capabilities;
}

}  // namespace patchy::ui
