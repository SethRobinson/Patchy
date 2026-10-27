// Linux font types from the user's fontconfig (GitHub issue 463); see fontconfig_font_class.hpp.
//
// The C ABI mirrored below is the subset fontconfig has kept since 2.0: opaque pattern and config
// handles, the public FcFontSet struct, FcMatchPattern = 0 and FcResultMatch = 0. No fontconfig
// header is included, so the build needs no fontconfig development package.

#include "ui/fontconfig_font_class.hpp"

#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <mutex>
#include <utility>

namespace patchy::ui {
namespace {

struct FcPattern;
struct FcConfig;
struct FcCharSet;
struct FcFontSet {
  int nfont;
  int sfont;
  FcPattern** fonts;
};
using FcChar8 = unsigned char;
using FcBool = int;
constexpr int kFcMatchPattern = 0;
constexpr int kFcResultMatch = 0;
constexpr const char* kFcFamily = "family";

struct Fontconfig {
  bool loaded{false};
  FcBool (*init)() = nullptr;
  FcPattern* (*pattern_create)() = nullptr;
  void (*pattern_destroy)(FcPattern*) = nullptr;
  FcBool (*pattern_add_string)(FcPattern*, const char*, const FcChar8*) = nullptr;
  int (*pattern_get_string)(const FcPattern*, const char*, int, FcChar8**) = nullptr;
  FcBool (*config_substitute)(FcConfig*, FcPattern*, int) = nullptr;
  void (*default_substitute)(FcPattern*) = nullptr;
  FcFontSet* (*font_sort)(FcConfig*, FcPattern*, FcBool, FcCharSet**, int*) = nullptr;
  void (*font_set_destroy)(FcFontSet*) = nullptr;
};

template <typename Function>
bool resolve(void* handle, const char* name, Function& target) {
  target = reinterpret_cast<Function>(dlsym(handle, name));
  return target != nullptr;
}

// Loaded once and never closed: Qt holds the same library for the life of the process.
const Fontconfig& fontconfig() {
  static Fontconfig library;
  static std::once_flag once;
  std::call_once(once, [] {
    void* handle = nullptr;
    for (const char* candidate : {"libfontconfig.so.1", "libfontconfig.so"}) {
      handle = dlopen(candidate, RTLD_NOW | RTLD_LOCAL);
      if (handle != nullptr) {
        break;
      }
    }
    if (handle == nullptr) {
      return;
    }
    library.loaded = resolve(handle, "FcInit", library.init) &&
                     resolve(handle, "FcPatternCreate", library.pattern_create) &&
                     resolve(handle, "FcPatternDestroy", library.pattern_destroy) &&
                     resolve(handle, "FcPatternAddString", library.pattern_add_string) &&
                     resolve(handle, "FcPatternGetString", library.pattern_get_string) &&
                     resolve(handle, "FcConfigSubstitute", library.config_substitute) &&
                     resolve(handle, "FcDefaultSubstitute", library.default_substitute) &&
                     resolve(handle, "FcFontSort", library.font_sort) &&
                     resolve(handle, "FcFontSetDestroy", library.font_set_destroy) && library.init() != 0;
  });
  return library;
}

struct Generic {
  std::string_view name;
  FontClass font_class;
};
constexpr std::array<Generic, 5> kGenerics = {{
    {"serif", FontClass::serif},
    {"sans-serif", FontClass::sans},
    {"monospace", FontClass::mono},
    {"cursive", FontClass::script},
    {"fantasy", FontClass::display},
}};

FcPattern* substituted_pattern(const Fontconfig& library, std::string_view family) {
  FcPattern* pattern = library.pattern_create();
  if (pattern == nullptr) {
    return nullptr;
  }
  const std::string name(family);
  library.pattern_add_string(pattern, kFcFamily, reinterpret_cast<const FcChar8*>(name.c_str()));
  library.config_substitute(nullptr, pattern, kFcMatchPattern);
  return pattern;
}

}  // namespace

std::optional<FontconfigFontClass> fontconfig_font_class(std::string_view family) {
  const auto& library = fontconfig();
  if (!library.loaded) {
    return std::nullopt;
  }
  FontconfigFontClass answer;
  FcPattern* pattern = substituted_pattern(library, family);
  if (pattern == nullptr) {
    return answer;
  }
  // The first generic name among the family list the rules built: 45-latin.conf appends the
  // font's own type; 49-sansserif.conf appends sans-serif to a pattern that has none.
  FcChar8* value = nullptr;
  for (int index = 0; library.pattern_get_string(pattern, kFcFamily, index, &value) == kFcResultMatch; ++index) {
    const std::string_view name(reinterpret_cast<const char*>(value));
    const auto generic = std::ranges::find_if(kGenerics, [name](const Generic& item) { return item.name == name; });
    if (generic != kGenerics.end()) {
      answer.font_class = generic->font_class;
      answer.definite = generic->font_class != FontClass::sans;
      break;
    }
  }
  library.pattern_destroy(pattern);
  return answer;
}

namespace {

std::vector<std::string> sorted_families_for_generic(const Fontconfig& library, std::string_view generic) {
  std::vector<std::string> families;
  FcPattern* pattern = substituted_pattern(library, generic);
  if (pattern == nullptr) {
    return families;
  }
  library.default_substitute(pattern);
  int result = 0;
  FcFontSet* sorted = library.font_sort(nullptr, pattern, 1, nullptr, &result);
  if (sorted != nullptr) {
    for (int index = 0; index < sorted->nfont && families.size() < 32U; ++index) {
      FcChar8* value = nullptr;
      if (library.pattern_get_string(sorted->fonts[index], kFcFamily, 0, &value) != kFcResultMatch ||
          value == nullptr) {
        continue;
      }
      std::string name(reinterpret_cast<const char*>(value));
      if (!name.empty() && std::ranges::find(families, name) == families.end()) {
        families.push_back(std::move(name));
      }
    }
    library.font_set_destroy(sorted);
  }
  library.pattern_destroy(pattern);
  return families;
}

}  // namespace

std::vector<std::string> fontconfig_families_for_class(FontClass font_class) {
  const auto& library = fontconfig();
  std::string_view generic;
  switch (font_class) {
    case FontClass::serif:
      generic = "serif";
      break;
    case FontClass::sans:
      generic = "sans-serif";
      break;
    case FontClass::mono:
      generic = "monospace";
      break;
    case FontClass::unknown:
    case FontClass::script:
    case FontClass::display:
      return {};
  }
  if (!library.loaded) {
    return {};
  }
  // The system font set does not change under a running process in any way Patchy follows
  // (fonts Patchy registers itself never reach fontconfig), so one sort per type is kept.
  static std::mutex cache_mutex;
  static std::array<std::optional<std::vector<std::string>>, 3> cache;
  const auto slot = font_class == FontClass::serif ? 0U : font_class == FontClass::sans ? 1U : 2U;
  const std::lock_guard lock(cache_mutex);
  if (!cache[slot].has_value()) {
    cache[slot] = sorted_families_for_generic(library, generic);
  }
  return *cache[slot];
}

}  // namespace patchy::ui
