// gimp-progress-*: progress reaches Patchy's run box; the plug-in's own progress
// callbacks (gimp-progress-install) are accepted and never called.

#include "plugins/gimp/host/pdb.hpp"
#include "plugins/gimp/host/session.hpp"

namespace patchy::gimp::host {

void register_progress_procedures(PdbTable& t) {
  t.add("gimp-progress-init", [](Session& s, const Args& a) {
    s.progress(0.0, a.size() > 0 ? a.s(0) : std::string());
    return pdb_ok();
  });
  t.add("gimp-progress-update", [](Session& s, const Args& a) {
    s.progress(a.d(0), {});
    return pdb_ok();
  });
  t.add("gimp-progress-pulse", [](Session& s, const Args&) {
    s.progress(-1.0, {});
    return pdb_ok();
  });
  t.add("gimp-progress-set-text", [](Session& s, const Args& a) {
    s.progress(-1.0, a.s(0));
    return pdb_ok();
  });
  t.add("gimp-progress-end", [](Session& s, const Args&) {
    s.progress(1.0, {});
    return pdb_ok();
  });
  t.add("gimp-progress-install", [](Session& s, const Args& a) {
    s.note_temporary_procedure(a.s(0));
    return pdb_ok();
  });
  t.add("gimp-progress-uninstall", [](Session&, const Args&) { return pdb_ok(); });
  t.add("gimp-progress-cancel", [](Session&, const Args&) { return pdb_ok(); });
  t.add("gimp-progress-get-window-handle", [](Session& s, const Args&) {
    std::vector<std::uint8_t> handle(8, 0);
    const auto window = s.request() != nullptr ? s.request()->parent_window : 0;
    for (int i = 0; i < 8; ++i) handle[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(window >> (8 * i));
    return pdb_ok({bytes_param("GBytes", window != 0 ? handle : std::vector<std::uint8_t>{})});
  });
}

}  // namespace patchy::gimp::host
