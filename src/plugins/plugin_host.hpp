#pragma once

#include "plugins/plugin_api.h"

#include <filesystem>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace patchy {

struct PluginDescriptor {
  PatchyPluginKind kind{PATCHY_PLUGIN_FILTER};
  std::string identifier;
  std::string display_name;
  std::uint32_t major_version{0};
  std::uint32_t minor_version{0};
  std::uint32_t patch_version{0};
  std::filesystem::path path;
};

class PluginHost {
public:
  void register_plugin(PluginDescriptor descriptor);
  [[nodiscard]] const std::vector<PluginDescriptor>& plugins() const noexcept;
  [[nodiscard]] std::vector<PluginDescriptor> plugins_by_kind(PatchyPluginKind kind) const;
  [[nodiscard]] const PluginDescriptor* find(std::string_view identifier) const noexcept;
  void clear();

private:
  std::vector<PluginDescriptor> plugins_;
};

}  // namespace patchy
