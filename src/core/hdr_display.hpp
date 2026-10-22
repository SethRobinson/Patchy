#pragma once

// HDR display capability model and the eligibility policy for native HDR monitor output
// (docs/high-bit-depth.md, "Display"). Qt-free: the platform queries in
// ui/display_hdr_info fill DisplayHdrCapabilities, and every number they derive goes
// through the helpers here so the arithmetic is pinned by core tests.
//
// Units follow the OS models. Windows reports nits: SDR white comes from DisplayConfig
// (the SDR content brightness slider), peak and full-frame luminance from DXGI, and
// headroom is peak divided by SDR white. macOS reports EDR headroom directly as a
// ratio (1.0 is the current SDR white) and no nits.

#include <cstdint>
#include <optional>
#include <string>

namespace patchy {

struct DisplayHdrCapabilities {
  // The platform answered for this screen. False on Linux, wasm, offscreen and when a
  // query failed; every other field then keeps its default.
  bool queried = false;
  // Windows: the output's color space is PQ/BT.2020 ("Use HDR" on). macOS: the screen
  // can show EDR content at all (potential headroom above 1).
  bool hdr_enabled = false;
  std::optional<double> sdr_white_nits;
  std::optional<double> peak_nits;
  std::optional<double> full_frame_nits;
  std::optional<double> min_nits;
  // Brightest displayable value as a multiple of SDR white, at least 1. macOS: the
  // current EDR headroom, which moves with brightness.
  double headroom = 1.0;
  // macOS: what the screen could reach at the current settings. Windows: same as headroom.
  double potential_headroom = 1.0;
  // A remote desktop session (Windows RDP); HDR presentation stays off there.
  bool remote_session = false;
};

// DISPLAYCONFIG_SDR_WHITE_LEVEL::SDRWhiteLevel is a multiplier of 80 nits in thousandths
// (1000 = 80 nits, the scRGB reference white).
[[nodiscard]] double sdr_white_nits_from_display_config(std::uint32_t sdr_white_level) noexcept;

// Peak over SDR white, never below 1. 1 when HDR is off or either value is missing or
// not a positive finite number.
[[nodiscard]] double headroom_from_nits(bool hdr_enabled, std::optional<double> peak_nits,
                                        std::optional<double> sdr_white_nits) noexcept;

// An EDR component value reported by macOS, sanitized to at least 1.
[[nodiscard]] double headroom_from_ratio(double ratio) noexcept;

// The user's preference. Not persisted yet: the settings key arrives with the canvas
// presenter and is a permanent identifier from then on.
enum class HdrDisplayPreference : std::uint8_t {
  Off,
  Automatic
};

// PATCHY_HDR_DISPLAY: "0" forces HDR presentation off, "1" forces it on (for testing on
// SDR screens); unset, empty or anything else leaves the decision to the policy.
enum class HdrDisplayOverride : std::uint8_t {
  None,
  ForceOff,
  ForceOn
};

[[nodiscard]] HdrDisplayOverride hdr_display_override_from_value(const std::optional<std::string>& value) noexcept;
[[nodiscard]] HdrDisplayOverride hdr_display_override_from_environment();

struct HdrDisplayContext {
  HdrDisplayPreference preference = HdrDisplayPreference::Off;
  HdrDisplayOverride override_mode = HdrDisplayOverride::None;
  bool float_document = false;     // only 32-bit documents hold values above SDR white
  bool platform_supported = false;  // this build has an HDR presenter for the platform
  DisplayHdrCapabilities screen;    // the screen showing the document
  bool crash_guard_tripped = false;  // a previous run died before its first HDR present
};

// Why the policy decided as it did, in the order the checks run.
enum class HdrDisplayReason : std::uint8_t {
  ForcedOff,
  NotFloatDocument,
  PlatformUnsupported,
  ForcedOn,
  PreferenceOff,
  RemoteSession,
  CrashGuard,
  NoHeadroom,
  Eligible
};

struct HdrDisplayDecision {
  bool use = false;
  HdrDisplayReason reason = HdrDisplayReason::PreferenceOff;
};

// ForceOn still needs a 32-bit document and a platform presenter; it skips only the
// preference, screen, remote-session and crash-guard checks.
[[nodiscard]] HdrDisplayDecision decide_hdr_display(const HdrDisplayContext& context) noexcept;

}  // namespace patchy
