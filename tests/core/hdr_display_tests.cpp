// HDR display capability math and the eligibility policy (core/hdr_display.hpp).

#include "core/hdr_display.hpp"
#include "test_harness.hpp"

#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace {

using patchy::DisplayHdrCapabilities;
using patchy::HdrDisplayContext;
using patchy::HdrDisplayOverride;
using patchy::HdrDisplayPreference;
using patchy::HdrDisplayReason;

bool nearly_equal(double a, double b) { return std::abs(a - b) < 1e-9; }

// DisplayConfig's SDR white is thousandths of 80 nits; headroom is peak over SDR white
// and never drops below 1. The figures are the ones measured on the dev box's AW3423DW
// (427-nit peak, SDR white slider at 240 nits) and its TV (1499 nits).
void hdr_display_sdr_white_and_headroom_math() {
  CHECK(nearly_equal(patchy::sdr_white_nits_from_display_config(1000), 80.0));
  CHECK(nearly_equal(patchy::sdr_white_nits_from_display_config(3000), 240.0));
  CHECK(nearly_equal(patchy::sdr_white_nits_from_display_config(0), 0.0));
  CHECK(nearly_equal(patchy::headroom_from_nits(true, 427.0, 240.0), 427.0 / 240.0));
  CHECK(std::abs(patchy::headroom_from_nits(true, 1499.0, 240.0) - 6.2458) < 1e-3);
  // HDR off, missing or invalid values, and an SDR white above the peak all give 1.
  CHECK(nearly_equal(patchy::headroom_from_nits(false, 427.0, 240.0), 1.0));
  CHECK(nearly_equal(patchy::headroom_from_nits(true, std::nullopt, 240.0), 1.0));
  CHECK(nearly_equal(patchy::headroom_from_nits(true, 427.0, std::nullopt), 1.0));
  CHECK(nearly_equal(patchy::headroom_from_nits(true, 427.0, 0.0), 1.0));
  CHECK(nearly_equal(patchy::headroom_from_nits(true, std::numeric_limits<double>::quiet_NaN(), 240.0), 1.0));
  CHECK(nearly_equal(patchy::headroom_from_nits(true, 300.0, 480.0), 1.0));
  // macOS EDR ratios pass through, sanitized to at least 1.
  CHECK(nearly_equal(patchy::headroom_from_ratio(4.0), 4.0));
  CHECK(nearly_equal(patchy::headroom_from_ratio(0.5), 1.0));
  CHECK(nearly_equal(patchy::headroom_from_ratio(std::numeric_limits<double>::infinity()), 1.0));
}

void hdr_display_env_override_parses() {
  CHECK(patchy::hdr_display_override_from_value(std::nullopt) == HdrDisplayOverride::None);
  CHECK(patchy::hdr_display_override_from_value(std::string()) == HdrDisplayOverride::None);
  CHECK(patchy::hdr_display_override_from_value(std::string("0")) == HdrDisplayOverride::ForceOff);
  CHECK(patchy::hdr_display_override_from_value(std::string("1")) == HdrDisplayOverride::ForceOn);
  CHECK(patchy::hdr_display_override_from_value(std::string("yes")) == HdrDisplayOverride::None);
}

DisplayHdrCapabilities hdr_screen(double headroom) {
  DisplayHdrCapabilities screen;
  screen.queried = true;
  screen.hdr_enabled = headroom > 1.0;
  screen.headroom = headroom;
  screen.potential_headroom = headroom;
  return screen;
}

// The eligible case, then each check turned off one at a time in priority order.
void hdr_display_policy_table() {
  HdrDisplayContext eligible;
  eligible.preference = HdrDisplayPreference::Automatic;
  eligible.float_document = true;
  eligible.platform_supported = true;
  eligible.screen = hdr_screen(1.78);

  struct Row {
    const char* label;
    HdrDisplayContext context;
    bool use;
    HdrDisplayReason reason;
  };
  std::vector<Row> rows;
  rows.push_back({"eligible", eligible, true, HdrDisplayReason::Eligible});
  {
    auto c = eligible;
    c.override_mode = HdrDisplayOverride::ForceOff;
    rows.push_back({"forced off wins over everything", c, false, HdrDisplayReason::ForcedOff});
  }
  {
    auto c = eligible;
    c.float_document = false;
    c.override_mode = HdrDisplayOverride::ForceOn;
    rows.push_back({"8/16-bit even when forced", c, false, HdrDisplayReason::NotFloatDocument});
  }
  {
    auto c = eligible;
    c.platform_supported = false;
    c.override_mode = HdrDisplayOverride::ForceOn;
    rows.push_back({"no presenter even when forced", c, false, HdrDisplayReason::PlatformUnsupported});
  }
  {
    auto c = eligible;
    c.override_mode = HdrDisplayOverride::ForceOn;
    c.preference = HdrDisplayPreference::Off;
    c.screen = DisplayHdrCapabilities{};
    c.screen.remote_session = true;
    c.crash_guard_tripped = true;
    rows.push_back({"forced on skips preference, screen, remote, guard", c, true, HdrDisplayReason::ForcedOn});
  }
  {
    auto c = eligible;
    c.preference = HdrDisplayPreference::Off;
    rows.push_back({"preference off", c, false, HdrDisplayReason::PreferenceOff});
  }
  {
    auto c = eligible;
    c.screen.remote_session = true;
    c.crash_guard_tripped = true;
    rows.push_back({"remote session before crash guard", c, false, HdrDisplayReason::RemoteSession});
  }
  {
    auto c = eligible;
    c.crash_guard_tripped = true;
    c.screen = hdr_screen(1.0);
    rows.push_back({"crash guard before headroom", c, false, HdrDisplayReason::CrashGuard});
  }
  {
    auto c = eligible;
    c.screen = DisplayHdrCapabilities{};
    rows.push_back({"unqueried screen", c, false, HdrDisplayReason::NoHeadroom});
  }
  {
    auto c = eligible;
    c.screen = hdr_screen(1.0);
    rows.push_back({"SDR screen", c, false, HdrDisplayReason::NoHeadroom});
  }
  {
    auto c = eligible;
    c.screen.hdr_enabled = true;
    c.screen.headroom = 1.0;
    c.screen.potential_headroom = 1.0;
    rows.push_back({"HDR on but SDR white at the peak", c, false, HdrDisplayReason::NoHeadroom});
  }
  {
    // macOS: current headroom 1 until content asks, potential above 1.
    auto c = eligible;
    c.screen.headroom = 1.0;
    c.screen.potential_headroom = 16.0;
    rows.push_back({"EDR potential headroom", c, true, HdrDisplayReason::Eligible});
  }
  for (const auto& row : rows) {
    const auto decision = patchy::decide_hdr_display(row.context);
    patchy::test::check(decision.use == row.use && decision.reason == row.reason, row.label, __FILE__, __LINE__);
  }
}

}  // namespace

std::vector<patchy::test::TestCase> hdr_display_tests() {
  return {
      {"hdr_display_sdr_white_and_headroom_math", hdr_display_sdr_white_and_headroom_math},
      {"hdr_display_env_override_parses", hdr_display_env_override_parses},
      {"hdr_display_policy_table", hdr_display_policy_table},
  };
}
