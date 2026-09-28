#pragma once

#include "plugins/pipl.hpp"

#include <filesystem>
#include <string>

namespace patchy {

enum class LegacyPhotoshopPluginKind {
  Unknown,
  Filter8bf,
  Format8bi,
  Automation8li
};

// The result of inspecting a plug-in file on disk. Pure file reads: the plug-in
// is never loaded, so a folder of untrusted files can be scanned safely.
struct LegacyPhotoshopPluginProbe {
  LegacyPhotoshopPluginKind kind{LegacyPhotoshopPluginKind::Unknown};
  bool supported{false};
  // Translatable (QObject context) explanation shown in the scan report.
  std::string reason;
  // "x86", "x64", "arm64", "pe-unknown", "mach-o", "elf", or "unknown".
  std::string architecture;
  // The property list (name, category, entry points, filter cases) when the
  // file carries one.
  pipl::PiplInfo pipl;
  // The PiPL name, or the file stem when the plug-in has no PiPL.
  std::string display_name;
  // The PiPL category (empty when absent).
  std::string category;
  // The exported entry point for this plug-in's architecture, or empty when the
  // runner should try the conventional names.
  std::string entry_point;
};

class LegacyPhotoshopAdapter {
public:
  [[nodiscard]] LegacyPhotoshopPluginProbe probe(const std::filesystem::path& path) const;
};

}  // namespace patchy
