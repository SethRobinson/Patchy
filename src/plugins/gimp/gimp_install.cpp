#include "plugins/gimp/gimp_install.hpp"

#include "support/path_utils.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace patchy::gimp {

namespace fs = std::filesystem;

namespace {

fs::path from_utf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

bool is_dir(const fs::path& p) {
  std::error_code ec;
  return fs::is_directory(p, ec);
}

bool is_file(const fs::path& p) {
  std::error_code ec;
  return fs::is_regular_file(p, ec);
}

std::string lower(std::string s) {
  for (auto& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

std::string trim(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
    s.pop_back();
  }
  std::size_t i = 0;
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
    ++i;
  }
  return s.substr(i);
}

std::vector<fs::path> sorted_files(const fs::path& folder, const std::string& extension) {
  std::vector<fs::path> files;
  std::error_code ec;
  for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
    if (it->is_regular_file(ec) && lower(path_to_utf8(it->path().extension())) == extension) {
      files.push_back(it->path());
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

std::string env_value(const char* name) {
#ifdef _WIN32
  std::wstring wide(name, name + std::char_traits<char>::length(name));
  const DWORD n = GetEnvironmentVariableW(wide.c_str(), nullptr, 0);
  if (n == 0) {
    return {};
  }
  std::wstring value(n, L'\0');
  const DWORD got = GetEnvironmentVariableW(wide.c_str(), value.data(), n);
  value.resize(got);
  return path_to_utf8(fs::path(value));
#else
  const char* v = std::getenv(name);
  return v != nullptr ? std::string(v) : std::string();
#endif
}

// "3.2.6.0" -> "3.2.6"; series "3.2".
void set_version(GimpInstall& install, std::string version) {
  int dots = 0;
  std::string cleaned;
  for (const char c : version) {
    if (c == '.') {
      if (++dots == 3) {
        break;
      }
    } else if (!std::isdigit(static_cast<unsigned char>(c))) {
      break;
    }
    cleaned.push_back(c);
  }
  install.version = cleaned;
  const auto first = cleaned.find('.');
  const auto second = first == std::string::npos ? std::string::npos : cleaned.find('.', first + 1);
  install.series = second == std::string::npos ? cleaned : cleaned.substr(0, second);
}

// The series from a program named gimp-3.N[.exe] in `bin`, when the version is unknown.
std::string series_from_programs(const fs::path& bin) {
  std::error_code ec;
  std::string best;
  for (fs::directory_iterator it(bin, ec), end; !ec && it != end; it.increment(ec)) {
    auto name = lower(path_to_utf8(it->path().filename()));
    if (name.size() > 4 && name.ends_with(".exe")) {
      name.resize(name.size() - 4);
    }
    for (const char* prefix : {"gimp-console-", "gimp-"}) {
      const std::string p(prefix);
      if (name.rfind(p, 0) == 0) {
        const auto rest = name.substr(p.size());
        if (rest.size() >= 3 && rest[0] == '3' && rest[1] == '.' &&
            std::all_of(rest.begin() + 2, rest.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }) &&
            rest > best) {
          best = rest;
        }
      }
    }
  }
  return best;
}

void finish_install(GimpInstall& install) {
  if (install.series.empty()) {
    install.series = series_from_programs(install.bin_dir);
  }
  if (install.series.empty()) {
    install.series = "3.0";
  }
  if (install.user_dir.empty()) {
    install.user_dir = default_user_dir(install.series);
  }
  install.plugin_folders.clear();
  const auto system_plugins = install.lib_dir / "plug-ins";
  install.plugin_folders.push_back(system_plugins);
  install.plugin_folders.push_back(install.user_dir / "plug-ins");
  for (const auto& extra : gimprc_plugin_path(install.user_dir / "gimprc", install.user_dir, install.lib_dir)) {
    if (std::find(install.plugin_folders.begin(), install.plugin_folders.end(), extra) == install.plugin_folders.end()) {
      install.plugin_folders.push_back(extra);
    }
  }
}

std::optional<GimpInstall> install_at_prefix(const fs::path& prefix, std::string source) {
  GimpInstall install;
  install.prefix = prefix;
  install.lib_dir = prefix / "lib" / "gimp" / "3.0";
  install.data_dir = prefix / "share" / "gimp" / "3.0";
  install.bin_dir = prefix / "bin";
  install.source = std::move(source);
  if (!is_dir(install.lib_dir / "plug-ins")) {
    // Debian-style multiarch layouts keep the plug-in tree under lib/<triplet>.
    std::error_code ec;
    bool found = false;
    for (fs::directory_iterator it(prefix / "lib", ec), end; !ec && it != end; it.increment(ec)) {
      const auto candidate = it->path() / "gimp" / "3.0";
      if (is_dir(candidate / "plug-ins")) {
        install.lib_dir = candidate;
        found = true;
        break;
      }
    }
    if (!found && is_dir(prefix / "lib64" / "gimp" / "3.0" / "plug-ins")) {
      install.lib_dir = prefix / "lib64" / "gimp" / "3.0";
      found = true;
    }
    if (!found) {
      return std::nullopt;
    }
  }
  return install;
}

#ifdef _WIN32
std::wstring registry_string(HKEY root, const std::wstring& key, const wchar_t* value, REGSAM view) {
  HKEY handle = nullptr;
  if (RegOpenKeyExW(root, key.c_str(), 0, KEY_READ | view, &handle) != ERROR_SUCCESS) {
    return {};
  }
  wchar_t buffer[2048];
  DWORD size = sizeof(buffer);
  DWORD type = 0;
  std::wstring out;
  if (RegQueryValueExW(handle, value, nullptr, &type, reinterpret_cast<BYTE*>(buffer), &size) == ERROR_SUCCESS &&
      (type == REG_SZ || type == REG_EXPAND_SZ)) {
    out.assign(buffer, size / sizeof(wchar_t));
    while (!out.empty() && out.back() == L'\0') {
      out.pop_back();
    }
  }
  RegCloseKey(handle);
  return out;
}

// The GIMP installer (Inno Setup) writes Uninstall\GIMP-<major>_is1 with InstallLocation
// and DisplayVersion, under HKCU for a per-user install and HKLM for all users.
void add_registry_installs(std::vector<GimpInstall>& out) {
  const std::wstring base = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\";
  struct Root {
    HKEY key;
    REGSAM view;
    const char* name;
  };
  const Root roots[] = {{HKEY_CURRENT_USER, 0, "registry (this user)"},
                        {HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY, "registry (all users)"},
                        {HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY, "registry (all users)"}};
  for (const auto& root : roots) {
    HKEY uninstall = nullptr;
    if (RegOpenKeyExW(root.key, base.c_str(), 0, KEY_READ | root.view, &uninstall) != ERROR_SUCCESS) {
      continue;
    }
    for (DWORD i = 0;; ++i) {
      wchar_t name[256];
      DWORD length = static_cast<DWORD>(std::size(name));
      if (RegEnumKeyExW(uninstall, i, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
        break;
      }
      const std::wstring key_name(name, length);
      if (key_name.rfind(L"GIMP-3", 0) != 0) {
        continue;
      }
      const auto location = registry_string(root.key, base + key_name, L"InstallLocation", root.view);
      if (location.empty()) {
        continue;
      }
      auto install = install_at_prefix(fs::path(location), root.name);
      if (!install.has_value()) {
        continue;
      }
      set_version(*install, path_to_utf8(fs::path(registry_string(root.key, base + key_name, L"DisplayVersion", root.view))));
      out.push_back(std::move(*install));
    }
    RegCloseKey(uninstall);
  }
}
#endif

}  // namespace

fs::path default_user_dir(const std::string& series) {
#ifdef _WIN32
  const auto appdata = env_value("APPDATA");
  return appdata.empty() ? fs::path() : from_utf8(appdata) / "GIMP" / from_utf8(series);
#elif defined(__APPLE__)
  return from_utf8(env_value("HOME")) / "Library" / "Application Support" / "GIMP" / from_utf8(series);
#else
  auto config = env_value("XDG_CONFIG_HOME");
  if (config.empty()) {
    config = env_value("HOME") + "/.config";
  }
  return from_utf8(config) / "GIMP" / from_utf8(series);
#endif
}

std::vector<fs::path> gimprc_plugin_path(const fs::path& gimprc, const fs::path& gimp_dir, const fs::path& plugin_dir) {
  std::vector<fs::path> out;
  std::ifstream in(gimprc, std::ios::binary);
  if (!in) {
    return out;
  }
  std::stringstream buffer;
  buffer << in.rdbuf();
  const auto text = buffer.str();
  // (plug-in-path "dir1;dir2") at the start of a line; commented lines start with '#'.
  std::size_t at = 0;
  while ((at = text.find("(plug-in-path", at)) != std::string::npos) {
    const auto line_start = text.rfind('\n', at);
    const auto prefix = text.substr(line_start == std::string::npos ? 0 : line_start + 1,
                                    at - (line_start == std::string::npos ? 0 : line_start + 1));
    at += 13;
    if (trim(prefix).size() != 0) {
      continue;
    }
    const auto open = text.find('"', at);
    const auto close = open == std::string::npos ? std::string::npos : text.find('"', open + 1);
    if (close == std::string::npos) {
      break;
    }
    auto value = text.substr(open + 1, close - open - 1);
    const auto expand = [&value](const std::string& token, const fs::path& with) {
      for (std::size_t p; (p = value.find(token)) != std::string::npos;) {
        value.replace(p, token.size(), path_to_utf8(with));
      }
    };
    expand("${gimp_dir}", gimp_dir);
    expand("${gimp_plug_in_dir}", plugin_dir);
#ifdef _WIN32
    const char separator = ';';
#else
    const char separator = ':';
#endif
    out.clear();  // a later setting replaces an earlier one
    std::size_t start = 0;
    while (start <= value.size()) {
      auto end = value.find(separator, start);
      if (end == std::string::npos) {
        end = value.size();
      }
      const auto piece = trim(value.substr(start, end - start));
      if (!piece.empty() && piece.find("${") == std::string::npos) {
        out.push_back(from_utf8(piece).lexically_normal());
      }
      start = end + 1;
    }
    at = close;
  }
  return out;
}

Interpreters load_interpreters(const std::vector<fs::path>& folders) {
  Interpreters out;
  for (const auto& folder : folders) {
    for (const auto& file : sorted_files(folder, ".interp")) {
      std::ifstream in(file, std::ios::binary);
      std::string line;
      while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
          continue;
        }
        const char first = line[0];
        if (first == ':' || first == ',') {
          // binfmt-style rule, the first character is the separator:
          // :name:type:offset:magic-or-extension:mask:interpreter:flags
          std::vector<std::string> fields;
          std::size_t start = 1;
          while (start <= line.size()) {
            auto end = line.find(first, start);
            if (end == std::string::npos) {
              end = line.size();
            }
            fields.push_back(line.substr(start, end - start));
            start = end + 1;
          }
          if (fields.size() >= 6 && fields[1] == "E" && !fields[3].empty() && !fields[5].empty()) {
            out.by_extension[lower(fields[3])] = fields[5];
          }
          continue;
        }
        const auto eq = line.find('=');
        if (eq != std::string::npos && eq > 0) {
          out.programs[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
        }
      }
    }
  }
  return out;
}

std::vector<std::pair<std::string, std::string>> load_environ(const std::vector<fs::path>& folders,
                                                              const fs::path& installation_dir) {
  std::vector<std::pair<std::string, std::string>> out;
  auto install = path_to_utf8(installation_dir);
  while (!install.empty() && (install.back() == '/' || install.back() == '\\')) {
    install.pop_back();
  }
  for (const auto& folder : folders) {
    for (const auto& file : sorted_files(folder, ".env")) {
      std::ifstream in(file, std::ios::binary);
      std::string line;
      while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
          continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos || eq == 0) {
          continue;
        }
        auto value = line.substr(eq + 1);
        const std::string token = "${gimp_installation_dir}";
        for (std::size_t p; (p = value.find(token)) != std::string::npos;) {
          value.replace(p, token.size(), install);
        }
        out.emplace_back(line.substr(0, eq), value);
      }
    }
  }
  return out;
}

std::vector<fs::path> environ_search_path(const std::vector<std::pair<std::string, std::string>>& environment) {
  std::vector<fs::path> out;
  for (const auto& [name, value] : environment) {
    if (lower(name) != "path") {
      continue;
    }
#ifdef _WIN32
    const char separator = ';';
#else
    const char separator = ':';
#endif
    out.clear();
    std::size_t start = 0;
    while (start <= value.size()) {
      auto end = value.find(separator, start);
      if (end == std::string::npos) {
        end = value.size();
      }
      const auto piece = trim(value.substr(start, end - start));
      if (!piece.empty()) {
        out.push_back(from_utf8(piece));
      }
      start = end + 1;
    }
  }
  return out;
}

std::vector<fs::path> find_plugin_files(const fs::path& plugins_folder, const Interpreters& interpreters) {
  std::vector<fs::path> out;
  std::error_code ec;
  std::vector<fs::path> folders;
  for (fs::directory_iterator it(plugins_folder, ec), end; !ec && it != end; it.increment(ec)) {
    if (it->is_directory(ec)) {
      folders.push_back(it->path());
    }
  }
  std::sort(folders.begin(), folders.end());
  for (const auto& folder : folders) {
    const auto name = folder.filename();
    std::vector<fs::path> candidates;
#ifdef _WIN32
    candidates.push_back(folder / (name.native() + L".exe"));
#else
    candidates.push_back(folder / name);
#endif
    for (const auto& [extension, interpreter] : interpreters.by_extension) {
      (void)interpreter;
      candidates.push_back(folder / (path_to_utf8(name) + "." + extension));
    }
    for (const auto& candidate : candidates) {
      if (is_file(candidate)) {
        out.push_back(candidate);
        break;
      }
    }
  }
  return out;
}

std::optional<PluginCommand> resolve_plugin_command(const fs::path& plugin_file, const Interpreters& interpreters,
                                                    const std::vector<fs::path>& search_path, std::string* error) {
  const auto extension = lower(path_to_utf8(plugin_file.extension()));
  PluginCommand command;
#ifdef _WIN32
  if (extension == ".exe") {
    command.program = plugin_file;
    return command;
  }
#endif
  std::string interpreter;
  if (!extension.empty()) {
    if (const auto it = interpreters.by_extension.find(extension.substr(1)); it != interpreters.by_extension.end()) {
      interpreter = it->second;
    }
  }
  if (interpreter.empty()) {
    // A script names its interpreter on its first line ("#!/usr/bin/env python3").
    std::ifstream in(plugin_file, std::ios::binary);
    std::string first;
    std::getline(in, first);
    if (first.rfind("#!", 0) == 0) {
      std::istringstream words(trim(first.substr(2)));
      std::string program;
      std::string argument;
      words >> program >> argument;
      if (interpreters.programs.count(program) != 0) {
        interpreter = program;
      } else if (fs::path(program).filename() == "env" && !argument.empty()) {
        interpreter = argument;
      } else {
        interpreter = path_to_utf8(fs::path(program).filename());
      }
    }
  }
  if (interpreter.empty()) {
#ifdef _WIN32
    if (error != nullptr) {
      *error = "no interpreter is registered for " + path_to_utf8(plugin_file.filename());
    }
    return std::nullopt;
#else
    command.program = plugin_file;  // a native program
    return command;
#endif
  }
  std::string program = interpreter;
  if (const auto it = interpreters.programs.find(interpreter); it != interpreters.programs.end()) {
    program = it->second;
  }
  fs::path resolved = from_utf8(program);
  if (!resolved.is_absolute()) {
    bool found = false;
    for (const auto& dir : search_path) {
      if (is_file(dir / resolved)) {
        resolved = dir / resolved;
        found = true;
        break;
      }
    }
    if (!found) {
      if (error != nullptr) {
        *error = "the interpreter " + program + " for " + path_to_utf8(plugin_file.filename()) + " was not found";
      }
      return std::nullopt;
    }
  } else if (!is_file(resolved)) {
    if (error != nullptr) {
      *error = "the interpreter " + program + " does not exist";
    }
    return std::nullopt;
  }
  command.program = resolved;
  command.leading_args.push_back(path_to_utf8(plugin_file));
  command.interpreter = interpreter;
  return command;
}

std::optional<GimpInstall> gimp_install_from_folder(const fs::path& folder) {
  std::vector<fs::path> candidates{folder, folder.parent_path(), folder.parent_path().parent_path(),
                                   folder.parent_path().parent_path().parent_path()};
#ifdef __APPLE__
  candidates.insert(candidates.begin(), folder / "Contents" / "Resources");
#endif
  for (const auto& prefix : candidates) {
    if (prefix.empty()) {
      continue;
    }
    if (auto install = install_at_prefix(prefix, "folder")) {
      finish_install(*install);
      return install;
    }
  }
  return std::nullopt;
}

std::vector<GimpInstall> detect_gimp_installs() {
  std::vector<GimpInstall> found;
#ifdef _WIN32
  add_registry_installs(found);
  for (const auto* variable : {"LOCALAPPDATA", "ProgramFiles", "ProgramW6432"}) {
    const auto base = env_value(variable);
    if (base.empty()) {
      continue;
    }
    const auto prefix = std::string(variable) == "LOCALAPPDATA" ? from_utf8(base) / "Programs" / "GIMP 3"
                                                                 : from_utf8(base) / "GIMP 3";
    if (auto install = install_at_prefix(prefix, "path")) {
      found.push_back(std::move(*install));
    }
  }
#elif defined(__APPLE__)
  for (const fs::path app : {fs::path("/Applications/GIMP.app"), from_utf8(env_value("HOME")) / "Applications" / "GIMP.app"}) {
    if (auto install = install_at_prefix(app / "Contents" / "Resources", "path")) {
      install->bin_dir = app / "Contents" / "MacOS";
      found.push_back(std::move(*install));
    }
  }
#else
  for (const fs::path prefix : {fs::path("/usr"), fs::path("/usr/local"), fs::path("/opt/gimp")}) {
    if (auto install = install_at_prefix(prefix, "path")) {
      found.push_back(std::move(*install));
    }
  }
  // GIMP's Flatpak: the files of its current build, user install first.
  const auto home = from_utf8(env_value("HOME"));
  for (const fs::path root : {home / ".local/share/flatpak", fs::path("/var/lib/flatpak")}) {
    const auto files = root / "app" / "org.gimp.GIMP" / "current" / "active" / "files";
    if (auto install = install_at_prefix(files, "flatpak")) {
      install->flatpak = true;
      install->user_dir = home / ".var/app/org.gimp.GIMP/config/GIMP";
      found.push_back(std::move(*install));
    }
  }
#endif
  // One entry per install folder, the first source wins.
  std::vector<GimpInstall> unique;
  std::set<std::string> seen;
  for (auto& install : found) {
    std::error_code ec;
    auto key = lower(path_to_utf8(fs::weakly_canonical(install.lib_dir, ec)));
    if (!seen.insert(key).second) {
      continue;
    }
    if (install.flatpak) {
      // Settings live in a per-series folder below the sandbox's config.
      if (install.series.empty()) {
        install.series = "3.0";
      }
      install.user_dir /= from_utf8(install.series);
    }
    finish_install(install);
    unique.push_back(std::move(install));
  }
  return unique;
}

}  // namespace patchy::gimp
