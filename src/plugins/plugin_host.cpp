#include "plugins/plugin_host.hpp"

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace patchy {

void PluginHost::register_plugin(PluginDescriptor descriptor) {
  if (descriptor.identifier.empty()) {
    throw std::invalid_argument("Plugin identifier cannot be empty");
  }
  if (find(descriptor.identifier) != nullptr) {
    throw std::invalid_argument("Plugin identifier is already registered");
  }
  plugins_.push_back(std::move(descriptor));
}

const std::vector<PluginDescriptor>& PluginHost::plugins() const noexcept {
  return plugins_;
}

std::vector<PluginDescriptor> PluginHost::plugins_by_kind(PatchyPluginKind kind) const {
  std::vector<PluginDescriptor> result;
  std::copy_if(plugins_.begin(), plugins_.end(), std::back_inserter(result),
               [kind](const PluginDescriptor& descriptor) { return descriptor.kind == kind; });
  return result;
}

const PluginDescriptor* PluginHost::find(std::string_view identifier) const noexcept {
  const auto found = std::find_if(plugins_.begin(), plugins_.end(), [identifier](const PluginDescriptor& descriptor) {
    return descriptor.identifier == identifier;
  });
  return found == plugins_.end() ? nullptr : &*found;
}

void PluginHost::clear() {
  plugins_.clear();
}

}  // namespace patchy
