#pragma once

#include <filesystem>
#include <string>

namespace patchy {

enum class LegacyPhotoshopPluginKind {
  Unknown,
  Filter8bf,
  Format8bi,
  Automation8li
};

struct LegacyPhotoshopPluginProbe {
  LegacyPhotoshopPluginKind kind{LegacyPhotoshopPluginKind::Unknown};
  bool supported{false};
  std::string reason;
  std::string architecture;
};

class LegacyPhotoshopAdapter {
public:
  [[nodiscard]] LegacyPhotoshopPluginProbe probe(const std::filesystem::path& path) const;
};

}  // namespace patchy
