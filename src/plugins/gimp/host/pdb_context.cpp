// gimp-context-*: Patchy's foreground and background colors and plain defaults. A
// plug-in may change its context for the run; nothing reaches Patchy.

#include "plugins/gimp/host/pdb.hpp"
#include "plugins/gimp/host/session.hpp"

#include <array>

namespace patchy::gimp::host {

namespace {

struct ContextState {
  std::array<double, 4> foreground{0, 0, 0, 1};
  std::array<double, 4> background{1, 1, 1, 1};
  std::optional<Color> foreground_set;
  std::optional<Color> background_set;
  bool initialized{false};
};

const std::vector<std::uint8_t>& profile(Session& s) {
  static const std::vector<std::uint8_t> none;
  return s.request() != nullptr ? s.request()->color_profile : none;
}

ContextState& context_state() {
  static ContextState context;
  return context;
}

ContextState& state(Session& s) {
  auto& context = context_state();
  if (!context.initialized && s.request() != nullptr) {
    for (int i = 0; i < 4; ++i) {
      context.foreground[static_cast<std::size_t>(i)] = s.request()->foreground[i];
      context.background[static_cast<std::size_t>(i)] = s.request()->background[i];
    }
    context.initialized = true;
  }
  return context;
}

}  // namespace

bool context_foreground_changed(double rgba[4]) {
  const auto& c = context_state();
  return c.foreground_set.has_value() && color_to_srgb(*c.foreground_set, rgba);
}

bool context_background_changed(double rgba[4]) {
  const auto& c = context_state();
  return c.background_set.has_value() && color_to_srgb(*c.background_set, rgba);
}

void register_context_procedures(PdbTable& t) {
  t.add("gimp-context-get-foreground", [](Session& s, const Args&) {
    auto& c = state(s);
    return pdb_ok({color_param(c.foreground_set ? *c.foreground_set : pdb_color_rgba(c.foreground.data(), profile(s)))});
  });
  t.add("gimp-context-get-background", [](Session& s, const Args&) {
    auto& c = state(s);
    return pdb_ok({color_param(c.background_set ? *c.background_set : pdb_color_rgba(c.background.data(), profile(s)))});
  });
  t.add("gimp-context-set-foreground", [](Session& s, const Args& a) {
    const auto& p = a.at(0);
    if (p.kind != ParamKind::GeglColor) return pdb_calling_error();
    state(s).foreground_set = p.color;
    return pdb_ok();
  });
  t.add("gimp-context-set-background", [](Session& s, const Args& a) {
    const auto& p = a.at(0);
    if (p.kind != ParamKind::GeglColor) return pdb_calling_error();
    state(s).background_set = p.color;
    return pdb_ok();
  });
  t.add("gimp-context-set-default-colors", [](Session& s, const Args&) {
    auto& c = state(s);
    const double black[4] = {0, 0, 0, 1};
    const double white[4] = {1, 1, 1, 1};
    c.foreground_set = pdb_color_rgba(black, profile(s));
    c.background_set = pdb_color_rgba(white, profile(s));
    return pdb_ok();
  });
  t.add("gimp-context-swap-colors", [](Session& s, const Args&) {
    auto& c = state(s);
    auto fg = c.foreground_set ? *c.foreground_set : pdb_color_rgba(c.foreground.data(), profile(s));
    auto bg = c.background_set ? *c.background_set : pdb_color_rgba(c.background.data(), profile(s));
    c.foreground_set = bg;
    c.background_set = fg;
    return pdb_ok();
  });
  for (const char* name : {"gimp-context-push", "gimp-context-pop", "gimp-context-set-defaults"}) {
    t.add(name, [](Session&, const Args&) { return pdb_ok(); });
  }
  t.add("gimp-context-get-opacity", [](Session&, const Args&) { return pdb_ok({pdb_double(100.0)}); });
  t.add("gimp-context-get-paint-mode", [](Session&, const Args&) { return pdb_ok({int_param("GimpLayerMode", 28)}); });
  t.add("gimp-context-get-interpolation", [](Session&, const Args&) {
    return pdb_ok({int_param("GimpInterpolationType", 2)});  // cubic
  });
  t.add("gimp-context-get-antialias", [](Session&, const Args&) { return pdb_ok({pdb_bool(true)}); });
  t.add("gimp-context-get-feather", [](Session&, const Args&) { return pdb_ok({pdb_bool(false)}); });
  t.add("gimp-context-get-sample-merged", [](Session&, const Args&) { return pdb_ok({pdb_bool(false)}); });
  t.add("gimp-context-get-transform-direction", [](Session&, const Args&) {
    return pdb_ok({int_param("GimpTransformDirection", 0)});
  });
}

}  // namespace patchy::gimp::host
