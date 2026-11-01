// Messages, displays, settings, stored plug-in data and PDB introspection.

#include "plugins/gimp/host/pdb.hpp"
#include "plugins/gimp/host/session.hpp"

#include <atomic>

namespace patchy::gimp::host {

namespace {

std::vector<std::uint8_t> window_handle_bytes(Session& s) {
  const auto window = s.request() != nullptr ? s.request()->parent_window : 0;
  if (window == 0) return {};
  std::vector<std::uint8_t> handle(8, 0);
  for (int i = 0; i < 8; ++i) handle[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(window >> (8 * i));
  return handle;
}

}  // namespace

void register_misc_procedures(PdbTable& t) {
  t.add("gimp-message", [](Session& s, const Args& a) {
    s.show_message(a.s(0));
    return pdb_ok();
  });
  t.add("gimp-message-get-handler", [](Session&, const Args&) {
    return pdb_ok({int_param("GimpMessageHandlerType", 0)});  // message box
  });
  t.add("gimp-message-set-handler", [](Session&, const Args&) { return pdb_ok(); });

  t.add("gimp-displays-flush", [](Session& s, const Args&) {
    s.flush_display();
    return pdb_ok();
  });
  t.add("gimp-display-id-is-valid", [](Session& s, const Args& a) {
    return pdb_ok({pdb_bool(s.model() != nullptr && a.i(0) == kDisplayId)});
  });
  t.add("gimp-display-get-window-handle", [](Session& s, const Args& a) {
    if (s.model() == nullptr || a.i(0) != kDisplayId) return pdb_error();
    return pdb_ok({bytes_param("GBytes", window_handle_bytes(s))});
  });
  t.add("gimp-display-present", [](Session&, const Args&) { return pdb_ok(); });

  // gimprc values plug-ins ask for. Unknown tokens are an error, as in GIMP.
  t.add("gimp-gimprc-query", [](Session& s, const Args& a) {
    const auto token = a.s(0);
    // Data search paths ("gimpressionist-path", "gfig-path", "script-fu-path"): the
    // user's folder in Patchy's GIMP settings, then GIMP's own data folder.
    constexpr std::string_view kPath = "-path";
    if (token.size() > kPath.size() && token.compare(token.size() - kPath.size(), kPath.size(), kPath) == 0 &&
        s.launch() != nullptr && !s.launch()->data_dir.empty()) {
      auto folder = token.substr(0, token.size() - kPath.size());
      if (folder == "script-fu") folder = "scripts";
#ifdef _WIN32
      const std::string separator = ";";
      const std::string slash = "\\";
#else
      const std::string separator = ":";
      const std::string slash = "/";
#endif
      std::string value;
      if (!s.launch()->profile_dir.empty()) value = s.launch()->profile_dir + slash + folder + separator;
      value += s.launch()->data_dir + slash + folder;
      return pdb_ok({pdb_string(value)});
    }
    if (token == "default-image-width" || token == "default-image-height") return pdb_ok({pdb_string("1920")});
    return pdb_error();
  });
  t.add("gimp-gimprc-set", [](Session&, const Args&) { return pdb_ok(); });
  t.add("gimp-get-default-comment", [](Session&, const Args&) { return pdb_ok({pdb_string("")}); });
  t.add("gimp-get-monitor-resolution", [](Session&, const Args&) {
    return pdb_ok({pdb_double(96.0), pdb_double(96.0)});
  });
  t.add("gimp-get-color-configuration", [](Session&, const Args&) { return pdb_ok({pdb_string("")}); });

  // Data a plug-in stores between runs (its last values). Patchy keeps it for the
  // session and hands it back on the next run.
  t.add("gimp-pdb-get-data", [](Session& s, const Args& a) {
    const auto it = s.stored_data().find(a.s(0));
    if (it == s.stored_data().end()) return pdb_error();
    return pdb_ok({bytes_param("GBytes", it->second)});
  });
  t.add("gimp-pdb-get-data-size", [](Session& s, const Args& a) {
    const auto it = s.stored_data().find(a.s(0));
    if (it == s.stored_data().end()) return pdb_error();
    return pdb_ok({pdb_int(static_cast<std::int32_t>(it->second.size()))});
  });
  t.add("gimp-pdb-set-data", [](Session& s, const Args& a) {
    const auto& data = a.at(1);
    if (data.kind != ParamKind::Bytes && data.kind != ParamKind::Array) return pdb_calling_error();
    s.stored_data()[a.s(0)] = data.bytes;
    return pdb_ok();
  });

  // Introspection: a plug-in may check that a procedure exists before calling it.
  t.add("gimp-pdb-proc-exists", [](Session& s, const Args& a) {
    const auto name = a.s(0);
    const bool exists = pdb_table().find(name) != nullptr || s.registered(name) != nullptr ||
                        (s.request() != nullptr && s.request()->procedure == name);
    return pdb_ok({pdb_bool(exists)});
  });
  t.add("gimp-pdb-temp-procedure-name", [](Session&, const Args&) {
    static std::atomic<int> counter{0};
    return pdb_ok({pdb_string("temp-procedure-" + std::to_string(++counter))});
  });
  t.add("gimp-pdb-temp-name", [](Session&, const Args&) {
    static std::atomic<int> counter{0};
    return pdb_ok({pdb_string("temp-procedure-" + std::to_string(++counter))});
  });
  t.add("gimp-pdb-query", [](Session&, const Args&) {
    // Patchy's own table is not GIMP's PDB; a script-language plug-in that binds the
    // whole PDB by name gets nothing to bind.
    Param list;
    list.kind = ParamKind::Strv;
    list.type_name = std::string("GStrv");
    return pdb_ok({list});
  });
}

}  // namespace patchy::gimp::host
