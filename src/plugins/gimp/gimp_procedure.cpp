#include "plugins/gimp/gimp_procedure.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <array>
#include <cctype>
#include <climits>

namespace patchy::gimp {

namespace {

std::string upper_trimmed(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
    s.remove_prefix(1);
  }
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
    s.remove_suffix(1);
  }
  std::string out(s);
  for (auto& c : out) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return out;
}

bool value_type_is(const ParamDef& def, std::string_view type) { return def.value_type_name.value_or("") == type; }

// GIMP's own bundled plug-ins that register as layer filters but build their result
// with what Patchy does not provide; every one was run from Patchy and in GIMP 3.2.6
// (docs/gimp-plugins.md, "Results"). Third-party plug-ins of this kind fail at run time
// with the GIMP function they called named.
struct KnownRefusal {
  std::string_view procedure;
  std::string_view reason;
};
constexpr KnownRefusal kKnownRefusals[] = {
    {"plug-in-curve-bend", kReasonImageOperations},       // copies and pastes through GIMP's clipboard
    {"plug-in-decompose", kReasonCreatesImage},
    {"plug-in-depth-merge", kReasonSeveralLayers},        // merges two source layers
    {"plug-in-film", kReasonCreatesImage},
    {"plug-in-flame", kReasonResources},                  // the context gradient
    {"plug-in-gfig", kReasonImageOperations},             // draws on a new layer with GIMP's tools
    {"plug-in-gflare", kReasonResources},                 // gradients
    {"plug-in-gradmap", kReasonResources},                // the context gradient
    {"plug-in-pagecurl", kReasonImageOperations},         // puts the curl on a new layer
    {"plug-in-smooth-palette", kReasonCreatesImage},
    {"plug-in-spyrogimp", kReasonImageOperations},        // paints with GIMP's tools
    {"plug-in-tile", kReasonCreatesImage},                // a new image, or resizes this one
    {"plug-in-warp", kReasonImageOperations},             // works through temporary images
    {"plug-in-wavelet-decompose", kReasonImageOperations},  // adds layers
    {"python-fu-foggify", kReasonImageOperations},        // adds a layer
};

}  // namespace

bool image_types_accept_rgb(std::string_view image_types) {
  // Comma- or space-separated tokens; '*' matches the alpha variant or anything.
  std::size_t start = 0;
  while (start <= image_types.size()) {
    auto end = image_types.find_first_of(", ", start);
    if (end == std::string_view::npos) {
      end = image_types.size();
    }
    const auto token = upper_trimmed(image_types.substr(start, end - start));
    if (token == "*" || token == "RGB*" || token == "RGB" || token == "RGBA") {
      return true;
    }
    start = end + 1;
  }
  return false;
}

std::vector<std::string> image_menu_segments(std::string_view menu_path) {
  constexpr std::string_view kRoot = "<Image>";
  std::vector<std::string> segments;
  if (menu_path.substr(0, kRoot.size()) != kRoot) {
    return segments;
  }
  menu_path.remove_prefix(kRoot.size());
  std::size_t start = 0;
  while (start < menu_path.size()) {
    auto end = menu_path.find('/', start);
    if (end == std::string_view::npos) {
      end = menu_path.size();
    }
    if (end > start) {
      segments.push_back(strip_mnemonic(menu_path.substr(start, end - start)));
    }
    start = end + 1;
  }
  return segments;
}

std::string strip_mnemonic(std::string_view label) {
  std::string out;
  out.reserve(label.size());
  for (std::size_t i = 0; i < label.size(); ++i) {
    if (label[i] == '_') {
      if (i + 1 < label.size() && label[i + 1] == '_') {
        out.push_back('_');
        ++i;
      }
      continue;
    }
    out.push_back(label[i]);
  }
  return out;
}

bool is_image_procedure(const ProcInstall& install) {
  const auto& p = install.params;
  return p.size() >= 3 && p[0].kind == ParamDefKind::Enum && value_type_is(p[0], "GimpRunMode") &&
         p[1].kind == ParamDefKind::Id && value_type_is(p[1], "GimpImage") && p[2].kind == ParamDefKind::IdArray &&
         p[2].item_type.value_or("") == "GimpDrawable";
}

ProcedureClass classify_procedure(const ProcedureInfo& procedure, bool from_script_fu) {
  if (procedure.file_handler || procedure.batch_interpreter) {
    return {ProcedureUse::Hidden, std::string(kReasonFileHandler)};
  }
  if (procedure.install.type == ProcType::Temporary) {
    return {ProcedureUse::Hidden, std::string(kReasonTemporary)};
  }
  // Only commands under GIMP's image menus are user commands; the rest are helpers
  // other plug-ins call.
  std::vector<std::string> segments;
  for (const auto& path : procedure.menu_paths) {
    segments = image_menu_segments(path);
    if (!segments.empty()) {
      break;
    }
  }
  if (segments.empty()) {
    return {ProcedureUse::Hidden, std::string(kReasonNoMenu)};
  }
  for (const auto& known : kKnownRefusals) {
    if (procedure.install.name.value_or("") == known.procedure) {
      return {ProcedureUse::Unsupported, std::string(known.reason)};
    }
  }
  if (from_script_fu) {
    // Script-Fu scripts build their result from GIMP's own image operations (new
    // layers, selections, GEGL filters), which Patchy does not provide.
    return {ProcedureUse::Unsupported, std::string(kReasonScriptFu)};
  }
  if (!is_image_procedure(procedure.install)) {
    return {ProcedureUse::Unsupported, std::string(kReasonNotImageProcedure)};
  }
  if (procedure.sensitivity_set) {
    const auto mask = procedure.sensitivity_mask;
    if ((mask & kSensitiveDrawable) == 0) {
      return {ProcedureUse::Unsupported,
              std::string((mask & kSensitiveDrawables) != 0 ? kReasonSeveralLayers : kReasonNoLayer)};
    }
  }
  if (!procedure.image_types.empty() && !image_types_accept_rgb(procedure.image_types)) {
    return {ProcedureUse::Unsupported, std::string(kReasonNotRgb)};
  }
  static constexpr std::array<std::string_view, 3> kLayerMenus = {"Filters", "Colors", "Layer"};
  if (std::find(kLayerMenus.begin(), kLayerMenus.end(), segments.front()) == kLayerMenus.end()) {
    return {ProcedureUse::Unsupported, std::string(kReasonMenuOutsideFilters)};
  }
  // Filters > Animation treats the image's layers as frames.
  if (segments.size() >= 2 && segments[0] == "Filters" && segments[1] == "Animation") {
    return {ProcedureUse::Unsupported, std::string(kReasonAnimation)};
  }
  return {ProcedureUse::LayerFilter, {}};
}

std::optional<Param> default_argument(const ParamDef& def, std::string* error) {
  const auto value_type = def.value_type_name.value_or("");
  Param p;
  p.type_name = value_type;
  switch (def.kind) {
    case ParamDefKind::Int:
      p.kind = ParamKind::Int;
      p.int_value = static_cast<std::int32_t>(std::clamp<std::int64_t>(def.int_default, INT32_MIN, INT32_MAX));
      return p;
    case ParamDefKind::Unit:
    case ParamDefKind::Enum:
    case ParamDefKind::Boolean:
      p.kind = ParamKind::Int;
      p.int_value = def.int32_default;
      return p;
    case ParamDefKind::Double:
      p.kind = ParamKind::Double;
      p.double_value = def.double_default;
      return p;
    case ParamDefKind::String:
    case ParamDefKind::Choice:
      p.kind = ParamKind::String;
      p.string_value = def.string_default;
      return p;
    case ParamDefKind::File:
      p.kind = ParamKind::File;
      p.string_value = def.string_default;
      return p;
    case ParamDefKind::GeglColor:
      p.kind = ParamKind::GeglColor;
      p.color = def.color_default;
      return p;
    case ParamDefKind::Id:
      // GIMP passes no object (-1) for an object argument nobody set; a plug-in run on
      // Patchy's layer then behaves as it does in GIMP (compared on GIMP 3.2.6 output).
      p.kind = ParamKind::Int;
      p.int_value = -1;
      return p;
    case ParamDefKind::IdArray:
      p.kind = ParamKind::IdArray;
      p.item_type = def.item_type;
      return p;
    case ParamDefKind::Resource:
      p.kind = ParamKind::Int;
      p.int_value = -1;
      return p;
    case ParamDefKind::ExportOptions:
      p.kind = ParamKind::ExportOptions;
      return p;
    case ParamDefKind::Default:
      // Arguments without their own definition kind: GIMP's default for these value
      // types is the empty value.
      if (value_type == "GimpDoubleArray" || value_type == "GimpInt32Array" || value_type == "GimpFloatArray") {
        p.kind = ParamKind::Array;
        return p;
      }
      if (value_type == "GBytes") {
        p.kind = ParamKind::Bytes;
        return p;
      }
      if (value_type == "GStrv") {
        p.kind = ParamKind::Strv;
        return p;
      }
      if (value_type == "GimpColorArray") {
        p.kind = ParamKind::ColorArray;
        return p;
      }
      break;
    case ParamDefKind::Curve:
      break;
  }
  if (error != nullptr) {
    *error = "argument " + def.name.value_or("?") + " has a type Patchy cannot fill in (" + value_type + ")";
  }
  return std::nullopt;
}

bool color_to_srgb(const Color& color, double rgba[4]) {
  const auto encoding = color.encoding.value_or("");
  const auto space = encoding.find(' ');
  if (space == std::string::npos) return false;
  const auto model = encoding.substr(0, space);
  const auto type = encoding.substr(space + 1);
  const bool perceptual = model == "R'G'B'" || model == "R'G'B'A";
  const bool linear = model == "RGB" || model == "RGBA";
  if (!perceptual && !linear) return false;
  const int channels = (model.back() == 'A') ? 4 : 3;
  std::size_t width = 0;
  if (type == "u8") width = 1;
  else if (type == "u16") width = 2;
  else if (type == "float") width = 4;
  else if (type == "double") width = 8;
  else return false;
  if (color.bytes.size() < width * static_cast<std::size_t>(channels)) return false;
  rgba[3] = 1.0;
  for (int c = 0; c < channels; ++c) {
    const auto* p = color.bytes.data() + width * static_cast<std::size_t>(c);
    double v = 0;
    if (width == 1) {
      v = *p / 255.0;
    } else if (width == 2) {
      std::uint16_t u;
      std::memcpy(&u, p, 2);
      v = u / 65535.0;
    } else if (width == 4) {
      float f;
      std::memcpy(&f, p, 4);
      v = f;
    } else {
      std::memcpy(&v, p, 8);
    }
    if (linear && c < 3) {
      v = std::clamp(v, 0.0, 1.0);
      v = v <= 0.0031308 ? v * 12.92 : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
    }
    rgba[c] = std::clamp(v, 0.0, 1.0);
  }
  return true;
}

std::string base64_encode(const std::vector<std::uint8_t>& data) {
  static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((data.size() + 2) / 3 * 4);
  std::size_t i = 0;
  for (; i + 2 < data.size(); i += 3) {
    const std::uint32_t v = (std::uint32_t{data[i]} << 16) | (std::uint32_t{data[i + 1]} << 8) | data[i + 2];
    out.push_back(kAlphabet[(v >> 18) & 63]);
    out.push_back(kAlphabet[(v >> 12) & 63]);
    out.push_back(kAlphabet[(v >> 6) & 63]);
    out.push_back(kAlphabet[v & 63]);
  }
  if (i < data.size()) {
    std::uint32_t v = std::uint32_t{data[i]} << 16;
    if (i + 1 < data.size()) {
      v |= std::uint32_t{data[i + 1]} << 8;
    }
    out.push_back(kAlphabet[(v >> 18) & 63]);
    out.push_back(kAlphabet[(v >> 12) & 63]);
    out.push_back(i + 1 < data.size() ? kAlphabet[(v >> 6) & 63] : '=');
    out.push_back('=');
  }
  return out;
}

bool base64_decode(std::string_view text, std::vector<std::uint8_t>& out) {
  out.clear();
  std::uint32_t v = 0;
  int bits = 0;
  for (const char c : text) {
    int d = -1;
    if (c >= 'A' && c <= 'Z') d = c - 'A';
    else if (c >= 'a' && c <= 'z') d = c - 'a' + 26;
    else if (c >= '0' && c <= '9') d = c - '0' + 52;
    else if (c == '+') d = 62;
    else if (c == '/') d = 63;
    else if (c == '=') break;
    else return false;
    v = (v << 6) | static_cast<std::uint32_t>(d);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<std::uint8_t>((v >> bits) & 0xFF));
    }
  }
  return true;
}

}  // namespace patchy::gimp
