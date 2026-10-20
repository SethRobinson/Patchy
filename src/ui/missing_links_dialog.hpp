#pragma once

#include <QString>
#include <QStringList>

#include <functional>
#include <optional>
#include <string>
#include <vector>

class QWidget;

namespace patchy::ui {

// The Missing Linked Files dialog shown after an interactive open when a linked smart object's
// file cannot be found (GitHub issue 257; Photoshop's "Cannot locate linked assets" box). One
// row per missing link element, naming the layers placed from it and the stored path, each with
// a Relink... button that runs the caller's relink flow. MainWindow collects the rows and relinks
// (show_missing_linked_files_dialog); this file owns only the dialog.

struct MissingLinkEntry {
  std::string source_uuid;  // the link element every listed layer references
  QString file_name;        // the stored file name, e.g. "logo.svg"
  QString stored_path;      // the path the document names for it (native, as stored)
  QStringList layer_names;  // layers placed from it, top of the stack first
};

struct MissingLinksDialogResult {
  int relinked{0};  // rows whose Relink... succeeded while the dialog was open
  // The "Don't show when opening documents" box, inverted (imports/showMissingLinksDialog).
  bool show_on_open{true};
};

// `relink` runs the relink flow for one entry and returns the new file name on success (the
// row then reports it and its button disables); nullopt leaves the row as it was. Continue (or
// closing the dialog) returns the result; `show_on_open` seeds the "Don't show" box.
[[nodiscard]] MissingLinksDialogResult run_missing_links_dialog(
    QWidget* parent, const std::vector<MissingLinkEntry>& links, bool show_on_open,
    const std::function<std::optional<QString>(const MissingLinkEntry&)>& relink);

}  // namespace patchy::ui
