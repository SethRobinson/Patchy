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


}  // namespace patchy::gimp::host
