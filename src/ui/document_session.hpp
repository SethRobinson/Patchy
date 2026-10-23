#pragma once

// One open document and everything the window tracks about it: history, save state,
// view/canvas binding, float-window placement, and Smart Object parentage. Owned by
// MainWindow's session list (docs/code-organization.md, "Session lifetime"); other
// code refers to a session by `session_id`, never by pointer, because the list erases
// on tab close.

#include "core/document.hpp"
#include "core/layer.hpp"
#include "core/layer_tree.hpp"
#include "ui/session_history.hpp"
#include "ui/canvas_types.hpp"
#include "ui/image_save_options.hpp"

#include <QString>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace patchy::ui {

class CanvasWidget;
class DocumentFloatWindow;

struct DocumentSession {
  using HistoryState = SessionHistory::State;


  Document document;
  QString title;
  QString path;
  // Added in the background (SessionActivation::Background): its canvas has never been
  // shown, so it has no real size to fit to yet. The first activation fits it.
  bool fit_view_on_first_activation{false};
  std::optional<ImageSaveOptions> image_save_options;
  // Image > Mode > CMYK Proof (docs/cmyk.md): the canvas shows this document through the
  // working CMYK profile, and saves to JPEG, TIFF and flattened PDF write CMYK. Session-only:
  // never stored in a file.
  bool cmyk_proof{false};
  // The user agreed to save this CMYK Proof document in RGB to a format without a CMYK
  // form; the warning is not repeated in this session.
  bool cmyk_proof_rgb_save_accepted{false};
  QString image_save_options_path;
  QString image_save_options_extension;
  // Stable identity for cross-session references (ids, not pointers: the session list
  // erases on tab close, so pointers into it must never be stored).
  std::int64_t session_id{0};
  // Present on an Edit Smart Object Contents child tab: which session and source
  // uuid a Save commits back into. `external` marks a linked-file child (a normal
  // disk-backed session whose Save writes the file first, then refreshes the
  // parent's previews).
  struct SmartObjectLink {
    std::int64_t parent_session_id{0};
    std::string source_uuid;
    bool external{false};
    // Edit Contents regenerates PSD UUIDs. Retain the lineage so parent
    // history navigation can reconnect the open child to a restored source.
    std::vector<std::string> source_uuid_history{};
  };
  std::optional<SmartObjectLink> smart_object_link;
  CanvasWidget* canvas{nullptr};
  // Non-null while the document is floated in its own top-level window; the
  // canvas lives inside it instead of the tab widget. The window is a
  // MainWindow child (widget tree owns it); the session only points at it.
  DocumentFloatWindow* float_window{nullptr};
  // Tab position to restore on Dock to Tabs (clamped; -1 when never floated).
  int floated_from_tab_index{-1};
  SessionHistory history;
  std::set<LayerId> collapsed_layer_groups;
  // Layers whose effect rows are open in the Layers panel (GitHub issue 256 follow-up): closed by
  // default. Seeded from the file at open and written back on save like the folders (issue 361).
  std::set<LayerId> expanded_layer_effects;
  // Layers whose Smart Filter rows are closed in the Layers panel: open by default (issue 361).
  std::set<LayerId> collapsed_smart_filters;
  // Alt-click eye isolation. `saved` is the pre-isolation visibility snapshot
  // (pre-order); `applied` is the state right after isolating, so any outside
  // visibility change invalidates the restore and the next Alt-click starts a
  // fresh isolation instead.
  struct VisibilityIsolation {
    LayerId isolated_id{0};
    std::vector<std::pair<LayerId, bool>> saved;
    std::vector<std::pair<LayerId, bool>> applied;
  };
  std::optional<VisibilityIsolation> visibility_isolation;
  std::int64_t revision{0};
  std::int64_t saved_revision{0};

  // The document as a layered save writes it: a copy-on-write copy whose groups carry the
  // Layers panel's open/closed state (`collapsed_layer_groups`, which layer metadata only
  // seeds at open), so the file reopens with the folders the user left open (issue 179), and
  // whose layers carry the open/closed state of their layer style and Smart Filter rows (issue 361).
  [[nodiscard]] Document document_for_write() const {
    Document copy = document;
    apply_layer_group_disclosure(copy.layers(), collapsed_layer_groups);
    apply_layer_fx_disclosure(copy.layers(), expanded_layer_effects, collapsed_smart_filters);
    return copy;
  }
};

}  // namespace patchy::ui
