// patchy-gegl-host (docs/gegl-filters.md): the one Patchy process that loads GEGL. It loads
// the user's own GEGL, babl and GLib from their GIMP 3 install at run time (nothing is
// linked or shipped; the legal rule is "GEGL operations through a helper" in
// docs/legal-licensing.md) and does one job per run:
//
//   patchy-gegl-host --gegl <libgegl> [--gpl3] --list   every operation and its settings, as JSON
//   patchy-gegl-host --gegl <libgegl> [--gpl3] --run    one operation on the pixels from stdin
//
// --run reads text lines ("op <name>", "size <w> <h>", "format <babl format>",
// "set <d|i|u|b|e|c|s> <property> <value>", "end"), then width * height pixels of that
// format, tightly packed; it writes the result in the same format to stdout. Errors go to
// stderr with a nonzero exit code.
//
// The declarations below are Patchy's own, written from the installed public headers of
// GEGL 0.4, babl 0.1 and GLib 2.0 (signatures, struct layouts, type constants;
// NOTICE-THIRD-PARTY.md). No GEGL source was read.

#include <algorithm>
#include <clocale>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

// GLib / GObject facts (glib-2.0/gobject headers).
using GType = std::size_t;
constexpr GType kTypeBoolean = 5U << 2U;
constexpr GType kTypeInt = 6U << 2U;
constexpr GType kTypeUInt = 7U << 2U;
constexpr GType kTypeEnum = 12U << 2U;
constexpr GType kTypeFloat = 14U << 2U;
constexpr GType kTypeDouble = 15U << 2U;
constexpr GType kTypeString = 16U << 2U;
constexpr GType kTypeObject = 20U << 2U;

struct TypeClass {
  GType g_type;
};
struct TypeInstance {
  TypeClass* g_class;
};
struct ParamSpec {
  TypeInstance g_type_instance;
  const char* name;
  int flags;
  GType value_type;
  GType owner_type;
  char* nick;
  char* blurb;
  void* qdata;
  unsigned ref_count;
  unsigned param_id;
};
struct ParamSpecInt {
  ParamSpec parent;
  int minimum;
  int maximum;
  int default_value;
};
struct ParamSpecUInt {
  ParamSpec parent;
  unsigned minimum;
  unsigned maximum;
  unsigned default_value;
};
struct ParamSpecFloat {
  ParamSpec parent;
  float minimum;
  float maximum;
  float default_value;
  float epsilon;
};
struct ParamSpecDouble {
  ParamSpec parent;
  double minimum;
  double maximum;
  double default_value;
  double epsilon;
};
struct GeglParamSpecDouble {
  ParamSpecDouble parent;
  double ui_minimum;
  double ui_maximum;
  double ui_gamma;
  double ui_step_small;
  double ui_step_big;
  int ui_digits;
};
struct GeglParamSpecInt {
  ParamSpecInt parent;
  int ui_minimum;
  int ui_maximum;
  double ui_gamma;
  int ui_step_small;
  int ui_step_big;
};
struct GeglParamSpecSeed {
  ParamSpecUInt parent;
  unsigned ui_minimum;
  unsigned ui_maximum;
};
struct EnumValue {
  int value;
  const char* value_name;
  const char* value_nick;
};
struct EnumClass {
  TypeClass g_type_class;
  int minimum;
  int maximum;
  unsigned n_values;
  EnumValue* values;
};
struct Value {
  GType g_type;
  std::uint64_t data[2];
};
struct GeglRectangle {
  int x;
  int y;
  int width;
  int height;
};

struct Api {
  void (*gegl_init)(int*, char***) = nullptr;
  void* (*gegl_config)() = nullptr;
  void (*g_object_set)(void*, const char*, ...) = nullptr;
  char** (*gegl_list_operations)(unsigned*) = nullptr;
  ParamSpec** (*gegl_operation_list_properties)(const char*, unsigned*) = nullptr;
  const char* (*gegl_operation_get_key)(const char*, const char*) = nullptr;
  const char* (*gegl_param_spec_get_property_key)(ParamSpec*, const char*) = nullptr;
  void* (*gegl_node_new)() = nullptr;
  void* (*gegl_node_new_child)(void*, const char*, ...) = nullptr;
  void (*gegl_node_link)(void*, void*) = nullptr;
  void (*gegl_node_set)(void*, const char*, ...) = nullptr;
  int (*gegl_node_has_pad)(void*, const char*) = nullptr;
  void (*gegl_node_blit)(void*, double, const GeglRectangle*, const void*, void*, int, int) = nullptr;
  void* (*gegl_buffer_new)(const GeglRectangle*, const void*) = nullptr;
  void (*gegl_buffer_set)(void*, const GeglRectangle*, int, const void*, const void*, int) = nullptr;
  void* (*gegl_color_new)(const char*) = nullptr;
  void (*gegl_color_set_pixel)(void*, const void*, const void*) = nullptr;
  void (*gegl_color_get_pixel)(void*, const void*, void*) = nullptr;
  void* (*gegl_param_spec_color_get_default)(ParamSpec*) = nullptr;
  const void* (*babl_format)(const char*) = nullptr;
  void (*g_object_unref)(void*) = nullptr;
  void (*g_free)(void*) = nullptr;
  const char* (*g_type_name)(GType) = nullptr;
  GType (*g_type_fundamental)(GType) = nullptr;
  const Value* (*g_param_spec_get_default_value)(ParamSpec*) = nullptr;
  int (*g_value_get_boolean)(const Value*) = nullptr;
  int (*g_value_get_int)(const Value*) = nullptr;
  unsigned (*g_value_get_uint)(const Value*) = nullptr;
  int (*g_value_get_enum)(const Value*) = nullptr;
  float (*g_value_get_float)(const Value*) = nullptr;
  double (*g_value_get_double)(const Value*) = nullptr;
  const char* (*g_value_get_string)(const Value*) = nullptr;
  // libintl's (or the C library's) dgettext; optional. GLib's g_dgettext would decline:
  // it translates a library only when the program has a catalog of its own.
  const char* (*dgettext)(const char*, const char*) = nullptr;
  const char* (*bindtextdomain)(const char*, const char*) = nullptr;
  const char* (*bind_textdomain_codeset)(const char*, const char*) = nullptr;
};

Api api;

[[noreturn]] void fail(int code, const std::string& message) {
  std::fprintf(stderr, "%s\n", message.c_str());
  std::fflush(stderr);
  std::exit(code);
}

#ifdef _WIN32
std::wstring widen(const std::string& utf8) {
  const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
  std::wstring out(n > 0 ? static_cast<std::size_t>(n) : 0U, L'\0');
  if (n > 0) {
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, out.data(), n);
    out.resize(static_cast<std::size_t>(n - 1));
  }
  return out;
}

std::string narrow(const wchar_t* text) {
  const int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  std::string out(n > 0 ? static_cast<std::size_t>(n) : 0U, '\0');
  if (n > 0) {
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), n, nullptr, nullptr);
    out.resize(static_cast<std::size_t>(n - 1));
  }
  return out;
}

std::vector<HMODULE> modules;

void* find_symbol(const char* name) {
  for (auto* module : modules) {
    if (const FARPROC found = GetProcAddress(module, name); found != nullptr) {
      void* symbol = nullptr;
      std::memcpy(&symbol, &found, sizeof(symbol));
      return symbol;
    }
  }
  return nullptr;
}

void load_gegl(const std::string& library) {
  const auto wide = widen(library);
  const auto slash = wide.find_last_of(L"\\/");
  const auto folder = slash == std::wstring::npos ? std::wstring(L".") : wide.substr(0, slash);
  // GEGL loads its operation modules later, and they find GIMP's other DLLs on the search
  // path: put GIMP's bin folder first for the whole process.
  SetDllDirectoryW(folder.c_str());
  std::wstring path = folder;
  if (const DWORD size = GetEnvironmentVariableW(L"PATH", nullptr, 0); size > 0) {
    std::wstring old(size, L'\0');
    old.resize(GetEnvironmentVariableW(L"PATH", old.data(), size));
    path += L";" + old;
  }
  SetEnvironmentVariableW(L"PATH", path.c_str());
  auto* gegl = LoadLibraryExW(wide.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (gegl == nullptr) {
    fail(3, "Could not load " + library + " (error " + std::to_string(GetLastError()) + ")");
  }
  modules.push_back(gegl);
  for (const auto* name : {L"libgobject-2.0-0.dll", L"libglib-2.0-0.dll", L"libbabl-0.1-0.dll", L"libintl-8.dll"}) {
    if (auto* module = GetModuleHandleW(name); module != nullptr) {
      modules.push_back(module);
    }
  }
}
#else
void* library_handle = nullptr;

void* find_symbol(const char* name) { return dlsym(library_handle, name); }

void load_gegl(const std::string& library) {
  library_handle = dlopen(library.c_str(), RTLD_NOW | RTLD_GLOBAL);
  if (library_handle == nullptr) {
    const char* why = dlerror();
    fail(3, "Could not load " + library + ": " + (why != nullptr ? why : "unknown error"));
  }
}
#endif

template <typename Fn>
void bind(Fn& target, const char* name) {
  auto* symbol = find_symbol(name);
  if (symbol == nullptr) {
    fail(3, std::string("GEGL lacks ") + name);
  }
  static_assert(sizeof(Fn) == sizeof(void*));
  std::memcpy(&target, &symbol, sizeof(target));  // a data pointer to a function pointer, without a cast
}

void bind_api() {
  bind(api.gegl_init, "gegl_init");
  bind(api.gegl_config, "gegl_config");
  bind(api.g_object_set, "g_object_set");
  bind(api.gegl_list_operations, "gegl_list_operations");
  bind(api.gegl_operation_list_properties, "gegl_operation_list_properties");
  bind(api.gegl_operation_get_key, "gegl_operation_get_key");
  bind(api.gegl_param_spec_get_property_key, "gegl_param_spec_get_property_key");
  bind(api.gegl_node_new, "gegl_node_new");
  bind(api.gegl_node_new_child, "gegl_node_new_child");
  bind(api.gegl_node_link, "gegl_node_link");
  bind(api.gegl_node_set, "gegl_node_set");
  bind(api.gegl_node_has_pad, "gegl_node_has_pad");
  bind(api.gegl_node_blit, "gegl_node_blit");
  bind(api.gegl_buffer_new, "gegl_buffer_new");
  bind(api.gegl_buffer_set, "gegl_buffer_set");
  bind(api.gegl_color_new, "gegl_color_new");
  bind(api.gegl_color_set_pixel, "gegl_color_set_pixel");
  bind(api.gegl_color_get_pixel, "gegl_color_get_pixel");
  bind(api.gegl_param_spec_color_get_default, "gegl_param_spec_color_get_default");
  bind(api.babl_format, "babl_format");
  bind(api.g_object_unref, "g_object_unref");
  bind(api.g_free, "g_free");
  bind(api.g_type_name, "g_type_name");
  bind(api.g_type_fundamental, "g_type_fundamental");
  bind(api.g_param_spec_get_default_value, "g_param_spec_get_default_value");
  bind(api.g_value_get_boolean, "g_value_get_boolean");
  bind(api.g_value_get_int, "g_value_get_int");
  bind(api.g_value_get_uint, "g_value_get_uint");
  bind(api.g_value_get_enum, "g_value_get_enum");
  bind(api.g_value_get_float, "g_value_get_float");
  bind(api.g_value_get_double, "g_value_get_double");
  bind(api.g_value_get_string, "g_value_get_string");
  const auto optional = [](auto& target, std::initializer_list<const char*> names) {
    for (const auto* name : names) {
      if (auto* symbol = find_symbol(name); symbol != nullptr && target == nullptr) {
        std::memcpy(&target, &symbol, sizeof(target));
      }
    }
  };
  optional(api.dgettext, {"libintl_dgettext", "dgettext"});
  optional(api.bindtextdomain, {"libintl_bindtextdomain", "bindtextdomain"});
  optional(api.bind_textdomain_codeset, {"libintl_bind_textdomain_codeset", "bind_textdomain_codeset"});
}

// GEGL's text in the LANGUAGE Patchy set, from GEGL's own catalog.
const char* translated(const char* text) {
  if (text == nullptr || *text == '\0' || api.dgettext == nullptr) {
    return text;
  }
  return api.dgettext("gegl-0.4", text);
}

void start_gegl(bool allow_gpl3, const std::string& library) {
#ifndef _WIN32
  // gettext ignores LANGUAGE (Patchy's language for GEGL's labels) in the "C" locale,
  // where a program starts. Only the messages category moves: the numbers the helper
  // reads and writes stay in the C locale's notation.
  if (const char* messages = std::setlocale(LC_MESSAGES, "");
      messages == nullptr || std::strcmp(messages, "C") == 0 || std::strcmp(messages, "POSIX") == 0) {
    for (const auto* fallback : {"en_US.UTF-8", "C.UTF-8"}) {
      if (std::setlocale(LC_MESSAGES, fallback) != nullptr) {
        break;
      }
    }
  }
#endif
  // GEGL reads its thread count from the environment at start.
  const auto threads = std::to_string(std::max(1U, std::min(64U, std::thread::hardware_concurrency())));
#ifdef _WIN32
  if (GetEnvironmentVariableW(L"GEGL_THREADS", nullptr, 0) == 0) {
    SetEnvironmentVariableW(L"GEGL_THREADS", widen(threads).c_str());
  }
#else
  setenv("GEGL_THREADS", threads.c_str(), 0);
#endif
  // GEGL loads its GPL-3 operations only for a program that declares a GPL-3 compatible
  // license (GeglConfig's documented "application-license"). The helper is MIT, which is;
  // Patchy passes --gpl3 (docs/legal-licensing.md, "GEGL operations through a helper").
  // Set before gegl_init: a change afterwards reloads every operation module.
  if (allow_gpl3) {
    api.g_object_set(api.gegl_config(), "application-license", "GPL3", nullptr);
  }
  api.gegl_init(nullptr, nullptr);
#ifndef _WIN32
  // GEGL's catalog is the one beside the library: GEGL can bind its domain to the folder
  // of the machine it was built on (GIMP.app's does, and GIMP binds it again itself).
  // Windows GEGL finds it relative to its DLL, and libintl there reads paths in the ANSI
  // code page, so it is left alone.
  if (api.bindtextdomain != nullptr) {
    const auto slash = library.find_last_of('/');
    const auto locale = (slash == std::string::npos ? std::string(".") : library.substr(0, slash)) + "/../share/locale";
    api.bindtextdomain("gegl-0.4", locale.c_str());
  }
#else
  (void)library;
#endif
  if (api.bind_textdomain_codeset != nullptr) {
    api.bind_textdomain_codeset("gegl-0.4", "UTF-8");  // JSON is UTF-8
  }
}

std::string json_string(const char* text) {
  std::string out = "\"";
  for (const char* p = text != nullptr ? text : ""; *p != '\0'; ++p) {
    const auto c = static_cast<unsigned char>(*p);
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20U) {
          char escaped[8];
          std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
          out += escaped;
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  return out + "\"";
}

std::string json_number(double value) {
  char text[64];
  std::snprintf(text, sizeof(text), "%.17g", value);
  return text;
}

bool type_named(ParamSpec* spec, const char* name) {
  const char* type = api.g_type_name(spec->g_type_instance.g_class->g_type);
  return type != nullptr && std::strcmp(type, name) == 0;
}

// One property as JSON: {name, label, help, kind, default, min, max, uiMin, uiMax, unit, ...}.
std::string describe_property(ParamSpec* spec) {
  std::string out = "{\"name\":" + json_string(spec->name) + ",\"label\":" + json_string(translated(spec->nick)) +
                    ",\"help\":" + json_string(translated(spec->blurb));
  const auto add_key = [&](const char* key, const char* json_key) {
    if (const char* value = api.gegl_param_spec_get_property_key(spec, key); value != nullptr && *value != '\0') {
      out += std::string(",\"") + json_key + "\":" + json_string(value);
    }
  };
  add_key("unit", "unit");
  add_key("axis", "axis");
  add_key("role", "role");
  const auto fundamental = api.g_type_fundamental(spec->value_type);
  const auto* value = api.g_param_spec_get_default_value(spec);
  if (fundamental == kTypeDouble) {
    const auto* d = reinterpret_cast<const ParamSpecDouble*>(spec);
    out += ",\"kind\":\"double\",\"default\":" + json_number(api.g_value_get_double(value)) +
           ",\"min\":" + json_number(d->minimum) + ",\"max\":" + json_number(d->maximum);
    if (type_named(spec, "GeglParamDouble")) {
      const auto* g = reinterpret_cast<const GeglParamSpecDouble*>(spec);
      out += ",\"uiMin\":" + json_number(g->ui_minimum) + ",\"uiMax\":" + json_number(g->ui_maximum) +
             ",\"digits\":" + std::to_string(g->ui_digits);
    }
  } else if (fundamental == kTypeFloat) {
    const auto* f = reinterpret_cast<const ParamSpecFloat*>(spec);
    out += ",\"kind\":\"float\",\"default\":" + json_number(api.g_value_get_float(value)) +
           ",\"min\":" + json_number(f->minimum) + ",\"max\":" + json_number(f->maximum);
  } else if (fundamental == kTypeInt) {
    const auto* i = reinterpret_cast<const ParamSpecInt*>(spec);
    out += ",\"kind\":\"int\",\"default\":" + std::to_string(api.g_value_get_int(value)) +
           ",\"min\":" + std::to_string(i->minimum) + ",\"max\":" + std::to_string(i->maximum);
    if (type_named(spec, "GeglParamInt")) {
      const auto* g = reinterpret_cast<const GeglParamSpecInt*>(spec);
      out += ",\"uiMin\":" + std::to_string(g->ui_minimum) + ",\"uiMax\":" + std::to_string(g->ui_maximum);
    }
  } else if (fundamental == kTypeUInt) {
    const auto* u = reinterpret_cast<const ParamSpecUInt*>(spec);
    out += std::string(",\"kind\":\"") + (type_named(spec, "GeglParamSeed") ? "seed" : "uint") +
           "\",\"default\":" + std::to_string(api.g_value_get_uint(value)) + ",\"min\":" + std::to_string(u->minimum) +
           ",\"max\":" + std::to_string(u->maximum);
    if (type_named(spec, "GeglParamSeed")) {
      const auto* g = reinterpret_cast<const GeglParamSpecSeed*>(spec);
      out += ",\"uiMin\":" + std::to_string(g->ui_minimum) + ",\"uiMax\":" + std::to_string(g->ui_maximum);
    }
  } else if (fundamental == kTypeBoolean) {
    out += std::string(",\"kind\":\"bool\",\"default\":") + (api.g_value_get_boolean(value) != 0 ? "true" : "false");
  } else if (fundamental == kTypeEnum) {
    // GParamSpecEnum: the parent, then the enum class pointer.
    const auto* enum_class = *reinterpret_cast<EnumClass* const*>(reinterpret_cast<const char*>(spec) + sizeof(ParamSpec));
    out += ",\"kind\":\"enum\",\"default\":" + std::to_string(api.g_value_get_enum(value)) + ",\"choices\":[";
    for (unsigned i = 0; enum_class != nullptr && i < enum_class->n_values; ++i) {
      const auto& v = enum_class->values[i];
      out += std::string(i == 0 ? "" : ",") + "{\"value\":" + std::to_string(v.value) + ",\"nick\":" +
             json_string(v.value_nick) + ",\"label\":" + json_string(translated(v.value_name)) + "}";
    }
    out += "]";
  } else if (fundamental == kTypeString) {
    out += ",\"kind\":\"string\",\"default\":" + json_string(api.g_value_get_string(value));
  } else if (fundamental == kTypeObject && type_named(spec, "GeglParamColor")) {
    double rgba[4] = {0.0, 0.0, 0.0, 1.0};
    if (auto* color = api.gegl_param_spec_color_get_default(spec); color != nullptr) {
      api.gegl_color_get_pixel(color, api.babl_format("R'G'B'A double"), rgba);
    }
    out += ",\"kind\":\"color\",\"default\":[" + json_number(rgba[0]) + "," + json_number(rgba[1]) + "," +
           json_number(rgba[2]) + "," + json_number(rgba[3]) + "]";
  } else {
    out += ",\"kind\":\"other\",\"type\":" + json_string(api.g_type_name(spec->value_type));
  }
  return out + "}";
}

int list_operations() {
  unsigned count = 0;
  char** names = api.gegl_list_operations(&count);
  auto* graph = api.gegl_node_new();
  std::string out = "{\"operations\":[";
  bool first = true;
  for (unsigned i = 0; i < count; ++i) {
    const char* name = names[i];
    auto* node = api.gegl_node_new_child(graph, "operation", name, nullptr);
    if (node == nullptr) {
      continue;
    }
    out += std::string(first ? "" : ",") + "{\"name\":" + json_string(name) +
           ",\"title\":" + json_string(translated(api.gegl_operation_get_key(name, "title"))) +
           ",\"categories\":" + json_string(api.gegl_operation_get_key(name, "categories")) +
           ",\"license\":" + json_string(api.gegl_operation_get_key(name, "license")) +
           ",\"description\":" + json_string(translated(api.gegl_operation_get_key(name, "description"))) +
           ",\"input\":" + (api.gegl_node_has_pad(node, "input") != 0 ? "true" : "false") +
           ",\"aux\":" + (api.gegl_node_has_pad(node, "aux") != 0 ? "true" : "false") +
           ",\"output\":" + (api.gegl_node_has_pad(node, "output") != 0 ? "true" : "false") + ",\"properties\":[";
    first = false;
    unsigned property_count = 0;
    ParamSpec** properties = api.gegl_operation_list_properties(name, &property_count);
    for (unsigned p = 0; p < property_count; ++p) {
      out += (p == 0 ? "" : ",") + describe_property(properties[p]);
    }
    api.g_free(properties);
    out += "]}";
  }
  out += "]}\n";
  api.g_object_unref(graph);
  api.g_free(names);  // the array only: GEGL owns the names
  std::fwrite(out.data(), 1, out.size(), stdout);
  std::fflush(stdout);
  return 0;
}

std::size_t bytes_per_pixel(const std::string& format) {
  const bool alpha = format.find('A') != std::string::npos;
  const std::size_t channels = alpha ? 4U : 3U;
  if (format.find(" u8") != std::string::npos) return channels;
  if (format.find(" u16") != std::string::npos) return channels * 2U;
  if (format.find(" float") != std::string::npos) return channels * 4U;
  return 0;
}

int run_operation() {
  std::string op;
  std::string format;
  int width = 0;
  int height = 0;
  struct Setting {
    char kind;
    std::string name;
    std::string value;
  };
  std::vector<Setting> settings;
  std::string line;
  while (std::getline(std::cin, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line == "end") break;
    const auto space = line.find(' ');
    const auto word = line.substr(0, space);
    const auto rest = space == std::string::npos ? std::string() : line.substr(space + 1);
    if (word == "op") {
      op = rest;
    } else if (word == "format") {
      format = rest;
    } else if (word == "size") {
      char* end = nullptr;
      width = static_cast<int>(std::strtol(rest.c_str(), &end, 10));
      height = static_cast<int>(std::strtol(end, nullptr, 10));
    } else if (word == "set" && rest.size() > 2) {
      const auto name_end = rest.find(' ', 2);
      if (name_end == std::string::npos) fail(2, "Bad set line: " + line);
      settings.push_back({rest[0], rest.substr(2, name_end - 2), rest.substr(name_end + 1)});
    }
  }
  const auto pixel_size = bytes_per_pixel(format);
  if (op.empty() || width <= 0 || height <= 0 || pixel_size == 0) {
    fail(2, "Incomplete run request");
  }
  const auto row = static_cast<std::size_t>(width) * pixel_size;
  std::vector<unsigned char> pixels(row * static_cast<std::size_t>(height));
  if (!std::cin.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()))) {
    fail(2, "The pixels ended early");
  }

  const auto* babl = api.babl_format(format.c_str());
  const GeglRectangle rect{0, 0, width, height};
  auto* buffer = api.gegl_buffer_new(&rect, babl);
  api.gegl_buffer_set(buffer, &rect, 0, babl, pixels.data(), static_cast<int>(row));
  auto* graph = api.gegl_node_new();
  auto* source = api.gegl_node_new_child(graph, "operation", "gegl:buffer-source", "buffer", buffer, nullptr);
  auto* node = api.gegl_node_new_child(graph, "operation", op.c_str(), nullptr);
  if (node == nullptr) fail(4, "Unknown operation " + op);
  std::vector<void*> colors;
  for (const auto& s : settings) {
    const char* name = s.name.c_str();
    switch (s.kind) {
      case 'd': api.gegl_node_set(node, name, std::strtod(s.value.c_str(), nullptr), nullptr); break;
      case 'f': api.gegl_node_set(node, name, std::strtod(s.value.c_str(), nullptr), nullptr); break;  // float promotes
      case 'i':
      case 'e':
      case 'b': api.gegl_node_set(node, name, static_cast<int>(std::strtol(s.value.c_str(), nullptr, 10)), nullptr); break;
      case 'u': api.gegl_node_set(node, name, static_cast<unsigned>(std::strtoul(s.value.c_str(), nullptr, 10)), nullptr); break;
      case 's': api.gegl_node_set(node, name, s.value.c_str(), nullptr); break;
      case 'c': {
        double rgba[4] = {0.0, 0.0, 0.0, 1.0};
        const char* cursor = s.value.c_str();
        for (auto& channel : rgba) {
          char* next = nullptr;
          channel = std::strtod(cursor, &next);
          if (next == cursor) fail(2, "Bad color for " + s.name);
          cursor = next;
        }
        auto* color = api.gegl_color_new(nullptr);
        api.gegl_color_set_pixel(color, api.babl_format("R'G'B'A double"), rgba);
        api.gegl_node_set(node, name, color, nullptr);
        colors.push_back(color);
        break;
      }
      default: fail(2, "Unknown setting kind for " + s.name);
    }
  }
  api.gegl_node_link(source, node);
  api.gegl_node_blit(node, 1.0, &rect, babl, pixels.data(), static_cast<int>(row), 0 /* GEGL_BLIT_DEFAULT */);
  std::fwrite(pixels.data(), 1, pixels.size(), stdout);
  std::fflush(stdout);
  for (auto* color : colors) api.g_object_unref(color);
  api.g_object_unref(graph);
  api.g_object_unref(buffer);
  return 0;
}

}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) args.push_back(narrow(argv[i]));
#else
int main(int argc, char** argv) {
  std::vector<std::string> args(argv + 1, argv + argc);
#endif
  std::string library;
  std::string mode;
  bool allow_gpl3 = false;
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--gegl" && i + 1 < args.size()) {
      library = args[++i];
    } else if (args[i] == "--gpl3") {
      allow_gpl3 = true;
    } else if (args[i] == "--list" || args[i] == "--run") {
      mode = args[i];
    }
  }
  if (library.empty() || mode.empty()) {
    fail(2, "usage: patchy-gegl-host --gegl <libgegl> --list | --run");
  }
  load_gegl(library);
  bind_api();
  start_gegl(allow_gpl3, library);
  return mode == "--list" ? list_operations() : run_operation();
}
