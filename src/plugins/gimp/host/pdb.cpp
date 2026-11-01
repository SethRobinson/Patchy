#include "plugins/gimp/host/pdb.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace patchy::gimp::host {

const Param& Args::at(std::size_t index) const {
  if (index >= params_.size()) {
    throw PdbCallingError(std::string(procedure_) + ": missing argument " + std::to_string(index + 1));
  }
  return params_[index];
}

std::int32_t Args::i(std::size_t index) const {
  const auto& p = at(index);
  if (p.kind != ParamKind::Int) {
    throw PdbCallingError(std::string(procedure_) + ": argument " + std::to_string(index + 1) + " is not a number");
  }
  return p.int_value;
}

double Args::d(std::size_t index) const {
  const auto& p = at(index);
  if (p.kind == ParamKind::Double) return p.double_value;
  if (p.kind == ParamKind::Int) return p.int_value;
  throw PdbCallingError(std::string(procedure_) + ": argument " + std::to_string(index + 1) + " is not a number");
}

std::string Args::s(std::size_t index) const {
  const auto& p = at(index);
  if (p.kind != ParamKind::String && p.kind != ParamKind::File) {
    throw PdbCallingError(std::string(procedure_) + ": argument " + std::to_string(index + 1) + " is not text");
  }
  return p.string_value.value_or("");
}

std::vector<std::string> Args::strv(std::size_t index) const {
  const auto& p = at(index);
  if (p.kind != ParamKind::Strv) {
    throw PdbCallingError(std::string(procedure_) + ": argument " + std::to_string(index + 1) + " is not a text list");
  }
  std::vector<std::string> out;
  for (const auto& s : p.strings) out.push_back(s.value_or(""));
  return out;
}

std::vector<std::string> PdbTable::names() const {
  std::vector<std::string> out;
  out.reserve(handlers_.size());
  for (const auto& [name, handler] : handlers_) {
    (void)handler;
    out.push_back(name);
  }
  std::sort(out.begin(), out.end());
  return out;
}

const PdbTable& pdb_table() {
  static const PdbTable table = [] {
    PdbTable t;
    register_registration_procedures(t);
    register_image_procedures(t);
    register_item_procedures(t);
    register_drawable_procedures(t);
    register_selection_procedures(t);
    register_context_procedures(t);
    register_progress_procedures(t);
    register_misc_procedures(t);
    return t;
  }();
  return table;
}

Color pdb_color_rgba(const double rgba[4], const std::vector<std::uint8_t>& profile) {
  Color c;
  c.profile = profile;
  c.bytes.resize(4 * sizeof(double));
  std::memcpy(c.bytes.data(), rgba, 4 * sizeof(double));  // native order, as observed
  c.encoding = std::string("R'G'B'A double");
  return c;
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

}  // namespace patchy::gimp::host
