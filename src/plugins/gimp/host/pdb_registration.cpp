// Query-time procedures: how a plug-in describes the procedures it installed
// (observed in GIMP 3.2.6 query traffic). Patchy records what it needs for its menu and
// accepts the rest.

#include "plugins/gimp/host/pdb.hpp"
#include "plugins/gimp/host/session.hpp"

namespace patchy::gimp::host {

namespace {

// Calls naming one of the plug-in's own procedures as their first argument.
PdbReply with_procedure(Session& session, const Args& args, const std::function<void(ProcedureInfo&)>& apply) {
  if (auto* procedure = session.registered(args.s(0))) {
    apply(*procedure);
    return pdb_ok();
  }
  // A procedure of another plug-in or one Patchy did not see installed.
  return pdb_error();
}

}  // namespace

void register_registration_procedures(PdbTable& t) {
  t.add("gimp-pdb-set-proc-image-types", [](Session& s, const Args& a) {
    return with_procedure(s, a, [&](ProcedureInfo& p) { p.image_types = a.s(1); });
  });
  t.add("gimp-pdb-set-proc-menu-label", [](Session& s, const Args& a) {
    return with_procedure(s, a, [&](ProcedureInfo& p) { p.menu_label = a.s(1); });
  });
  t.add("gimp-pdb-add-proc-menu-path", [](Session& s, const Args& a) {
    return with_procedure(s, a, [&](ProcedureInfo& p) { p.menu_paths.push_back(a.s(1)); });
  });
  t.add("gimp-pdb-set-proc-sensitivity-mask", [](Session& s, const Args& a) {
    return with_procedure(s, a, [&](ProcedureInfo& p) {
      p.sensitivity_set = true;
      p.sensitivity_mask = static_cast<std::uint32_t>(a.i(1));
    });
  });
  t.add("gimp-pdb-set-proc-documentation", [](Session& s, const Args& a) {
    return with_procedure(s, a, [&](ProcedureInfo& p) {
      p.blurb = a.s(1);
      p.help = a.s(2);
      p.help_id = a.size() > 3 ? a.s(3) : std::string();
    });
  });
  t.add("gimp-pdb-set-proc-attribution", [](Session& s, const Args& a) {
    return with_procedure(s, a, [&](ProcedureInfo& p) {
      p.authors = a.s(1);
      p.copyright = a.s(2);
      p.date = a.size() > 3 ? a.s(3) : std::string();
    });
  });
  t.add("gimp-pdb-set-proc-icon", [](Session& s, const Args& a) {
    return with_procedure(s, a, [](ProcedureInfo&) {});
  });
  // File load and export handlers are listed nowhere in Patchy's menus.
  for (const char* name : {"gimp-pdb-set-file-proc-load-handler", "gimp-pdb-set-file-proc-export-handler",
                           "gimp-pdb-set-file-proc-mime-types", "gimp-pdb-set-file-proc-handles-remote",
                           "gimp-pdb-set-file-proc-handles-raw", "gimp-pdb-set-file-proc-handles-vector",
                           "gimp-pdb-set-file-proc-thumbnail-loader", "gimp-pdb-set-file-proc-priority",
                           "gimp-pdb-set-load-handler", "gimp-pdb-set-export-handler"}) {
    t.add(name, [](Session& s, const Args& a) {
      return with_procedure(s, a, [](ProcedureInfo& p) { p.file_handler = true; });
    });
  }
  t.add("gimp-pdb-set-batch-interpreter", [](Session& s, const Args& a) {
    return with_procedure(s, a, [](ProcedureInfo& p) { p.batch_interpreter = true; });
  });
  for (const char* name : {"gimp-plug-in-help-register", "gimp-plug-in-menu-branch-register",
                           "gimp-plug-in-domain-register", "gimp-plug-in-set-pdb-error-handler"}) {
    t.add(name, [](Session&, const Args&) { return pdb_ok(); });
  }
  t.add("gimp-plug-in-get-pdb-error-handler", [](Session&, const Args&) {
    return pdb_ok({int_param("GimpPDBErrorHandler", 0)});
  });
}

}  // namespace patchy::gimp::host
