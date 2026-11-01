#pragma once

// The procedures Patchy answers when a GIMP 3 plug-in calls back into the "procedural
// database" (docs/gimp-plugins.md, "PDB procedures"). Names and argument lists are
// GIMP's public interface (its Procedure Browser and observed calls); the behaviour is
// Patchy's own, written for a one-layer image. Every procedure not in the table gets an
// execution error, and the run reports "Patchy does not support <procedure> yet".
//
// One file per group: pdb_registration.cpp (query-time gimp-pdb-set-proc-*),
// pdb_image.cpp, pdb_item.cpp, pdb_drawable.cpp, pdb_selection.cpp, pdb_context.cpp,
// pdb_progress.cpp, pdb_misc.cpp.

#include "plugins/gimp/gimp_protocol.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace patchy::gimp::host {

class Session;

struct PdbReply {
  PdbStatus status{PdbStatus::Success};
  std::vector<Param> values;  // after the status
};

// Thrown by argument accessors when a call does not match the procedure's signature.
class PdbCallingError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class Args {
public:
  Args(std::string_view procedure, const std::vector<Param>& params) : procedure_(procedure), params_(params) {}
  [[nodiscard]] std::size_t size() const noexcept { return params_.size(); }
  [[nodiscard]] const Param& at(std::size_t i) const;
  [[nodiscard]] std::int32_t i(std::size_t index) const;   // Int (ints, booleans, enums, ids)
  [[nodiscard]] double d(std::size_t index) const;         // Double (or Int)
  [[nodiscard]] std::string s(std::size_t index) const;    // String (NULL as "")
  [[nodiscard]] bool b(std::size_t index) const { return i(index) != 0; }
  [[nodiscard]] std::vector<std::string> strv(std::size_t index) const;

private:
  std::string_view procedure_;
  const std::vector<Param>& params_;
};

using PdbHandler = std::function<PdbReply(Session&, const Args&)>;

class PdbTable {
public:
  void add(std::string name, PdbHandler handler) { handlers_[std::move(name)] = std::move(handler); }
  [[nodiscard]] const PdbHandler* find(const std::string& name) const {
    const auto it = handlers_.find(name);
    return it == handlers_.end() ? nullptr : &it->second;
  }
  [[nodiscard]] std::vector<std::string> names() const;

private:
  std::unordered_map<std::string, PdbHandler> handlers_;
};

// The table, built once from every group.
const PdbTable& pdb_table();

void register_registration_procedures(PdbTable& table);
void register_image_procedures(PdbTable& table);
void register_item_procedures(PdbTable& table);
void register_drawable_procedures(PdbTable& table);
void register_selection_procedures(PdbTable& table);
void register_context_procedures(PdbTable& table);
void register_progress_procedures(PdbTable& table);
void register_misc_procedures(PdbTable& table);

// Reply builders.
inline PdbReply pdb_ok(std::vector<Param> values = {}) { return {PdbStatus::Success, std::move(values)}; }
inline PdbReply pdb_error() { return {PdbStatus::ExecutionError, {}}; }
inline PdbReply pdb_calling_error() { return {PdbStatus::CallingError, {}}; }
inline Param pdb_bool(bool v) { return int_param("gboolean", v ? 1 : 0); }
inline Param pdb_int(std::int32_t v) { return int_param("gint", v); }
inline Param pdb_double(double v) { return double_param("gdouble", v); }
inline Param pdb_string(std::string v) { return string_param("gchararray", std::move(v)); }

// A color as GIMP sends them: "R'G'B'A double" pixels and the run's sRGB profile
// (plug-ins crash on a color without a profile, observed with GIMP 3.2.6).
Color pdb_color_rgba(const double rgba[4], const std::vector<std::uint8_t>& profile);

// A plug-in's color as non-linear sRGB in 0..1 (R'G'B'(A) and linear RGB(A) encodings
// in u8, u16, float or double; the profile is taken as sRGB). False for other encodings.
bool color_to_srgb(const Color& color, double rgba[4]);

// The context colors a run's plug-in set, for the result (pdb_context.cpp).
bool context_foreground_changed(double rgba[4]);
bool context_background_changed(double rgba[4]);

}  // namespace patchy::gimp::host
