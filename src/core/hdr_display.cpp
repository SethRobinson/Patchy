#include "core/hdr_display.hpp"

#include "core/environment.hpp"

#include <algorithm>
#include <cmath>

namespace patchy {
namespace {

constexpr double kScrgbReferenceWhiteNits = 80.0;

bool positive_finite(std::optional<double> value) noexcept {
  return value.has_value() && std::isfinite(*value) && *value > 0.0;
}

}  // namespace

double sdr_white_nits_from_display_config(std::uint32_t sdr_white_level) noexcept {
  return static_cast<double>(sdr_white_level) * kScrgbReferenceWhiteNits / 1000.0;
}

double headroom_from_nits(bool hdr_enabled, std::optional<double> peak_nits,
                          std::optional<double> sdr_white_nits) noexcept {
  if (!hdr_enabled || !positive_finite(peak_nits) || !positive_finite(sdr_white_nits)) {
    return 1.0;
  }
  return std::max(1.0, *peak_nits / *sdr_white_nits);
}

double headroom_from_ratio(double ratio) noexcept {
  return std::isfinite(ratio) ? std::max(1.0, ratio) : 1.0;
}

HdrDisplayOverride hdr_display_override_from_value(const std::optional<std::string>& value) noexcept {
  if (!value.has_value()) {
    return HdrDisplayOverride::None;
  }
  if (*value == "0") {
    return HdrDisplayOverride::ForceOff;
  }
  if (*value == "1") {
    return HdrDisplayOverride::ForceOn;
  }
  return HdrDisplayOverride::None;
}

HdrDisplayOverride hdr_display_override_from_environment() {
  return hdr_display_override_from_value(environment_variable("PATCHY_HDR_DISPLAY"));
}

HdrDisplayDecision decide_hdr_display(const HdrDisplayContext& context) noexcept {
  const auto decide = [](bool use, HdrDisplayReason reason) { return HdrDisplayDecision{use, reason}; };
  if (context.override_mode == HdrDisplayOverride::ForceOff) {
    return decide(false, HdrDisplayReason::ForcedOff);
  }
  if (!context.float_document) {
    return decide(false, HdrDisplayReason::NotFloatDocument);
  }
  if (!context.platform_supported) {
    return decide(false, HdrDisplayReason::PlatformUnsupported);
  }
  if (context.override_mode == HdrDisplayOverride::ForceOn) {
    return decide(true, HdrDisplayReason::ForcedOn);
  }
  if (context.preference == HdrDisplayPreference::Off) {
    return decide(false, HdrDisplayReason::PreferenceOff);
  }
  if (context.screen.remote_session) {
    return decide(false, HdrDisplayReason::RemoteSession);
  }
  if (context.crash_guard_tripped) {
    return decide(false, HdrDisplayReason::CrashGuard);
  }
  // macOS reports potential headroom above 1 on screens that can brighten EDR content
  // even while the current headroom is 1 (the system raises it when content asks).
  if (!context.screen.queried || !context.screen.hdr_enabled || context.screen.potential_headroom <= 1.0) {
    return decide(false, HdrDisplayReason::NoHeadroom);
  }
  return decide(true, HdrDisplayReason::Eligible);
}

}  // namespace patchy
