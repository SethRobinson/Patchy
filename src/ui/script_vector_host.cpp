#include "ui/script_vector.hpp"
#include "ui/main_window.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/paths_panel.hpp"
#include "ui/vector_operations.hpp"
#include "ui/custom_shape_library.hpp"
#include "ui/gradient_library.hpp"
#include "ui/pattern_library.hpp"

namespace patchy::ui {
PatternLibrary& ScriptEngineHost::vector_pattern_library() { return window_.pattern_library(); }
GradientLibrary& ScriptEngineHost::vector_gradient_library() { return window_.gradient_library(); }
CustomShapeLibrary& ScriptEngineHost::vector_custom_shape_library() { return window_.custom_shape_library(); }

void ScriptEngineHost::note_vector_changed(std::int64_t id, const QRect& dirty, bool structure) {
  auto& pending = pending_refresh_[id];
  pending.paths = true;
  pending.structure = pending.structure || structure;
  if (structure) { pending.full_canvas = true; }
  else if (!dirty.isEmpty()) { pending.dirty += dirty; }
  if (auto* canvas = session_canvas(id)) {
    canvas->clear_path_edit_selection();
    const auto* doc = session_document_const(id);
    if (doc && canvas->active_document_path() && !doc->find_path(*canvas->active_document_path())) {
      canvas->set_active_document_path(std::nullopt);
      if (active_session_id() == id) { window_.active_document_path_id_.reset(); }
    }
    if (doc && canvas->layer_edit_target() == CanvasWidget::LayerEditTarget::VectorMask) {
      const auto* selected = doc->active_layer_id() ? doc->find_layer(*doc->active_layer_id()) : nullptr;
      if (!selected || !selected->vector_mask()) { canvas->set_layer_edit_target(CanvasWidget::LayerEditTarget::Content); }
    }
  }
  schedule_refresh_flush();
  pump_progress_indicator();
  complete_mutation(id);
}
void ScriptEngineHost::activate_document_path(std::int64_t session, DocumentPathId path) {
  if (!script_vector::document(*this, session).find_path(path)) { script_vector::invalid("path.id"); }
  activate_session(session);
  window_.target_document_path_row(path);
}
QJsonObject ScriptEngineHost::vector_target(std::int64_t session) const {
  const auto* doc = session_document_const(session);
  const auto* canvas = session_canvas(session);
  if (!doc || !canvas) { return {{"kind", "none"}}; }
  const auto* item = doc->active_layer_id() ? doc->find_layer(*doc->active_layer_id()) : nullptr;
  if (item && canvas->layer_edit_target() == CanvasWidget::LayerEditTarget::VectorMask && item->vector_mask()) {
    return {{"kind", "vectorMask"}, {"layerId", QString::number(item->id())}};
  }
  if (const auto path = canvas->active_document_path(); path && doc->find_path(*path)) {
    return {{"kind", "path"}, {"pathId", QString::number(*path)}};
  }
  if (item && item->vector_shape()) { return {{"kind", "shape"}, {"layerId", QString::number(item->id())}}; }
  return {{"kind", "none"}};
}
void ScriptEngineHost::select_vector_path(std::int64_t session, const VectorPath& path,
                                        double feather, bool antialias, const QString& operation) {
  const auto& doc = script_vector::document(*this, session);
  auto* canvas = session_canvas(session);
  if (!canvas || canvas->quick_mask_active()) { script_vector::invalid("selection.target"); }
  const auto existing = selection_mask_pixels(*canvas, QRect(0, 0, doc.width(), doc.height()));
  const int op = canvas->has_selection() ? static_cast<int>(QStringList{"replace", "add", "subtract", "intersect"}.indexOf(operation)) : 0;
  const auto coverage = make_path_selection_coverage(path, doc.width(), doc.height(), feather, antialias, op, &existing);
  (void)script_vector::writable(*this, session);
  canvas = session_canvas(session);
  if (!canvas) { script_vector::invalid("selection.target"); }
  canvas->apply_grayscale_to_selection(coverage);
  note_pixels_changed(session, {});
}
VectorPath ScriptEngineHost::selection_vector_path(std::int64_t session, double tolerance) const {
  (void)script_vector::document(const_cast<ScriptEngineHost&>(*this), session);
  const auto* canvas = session_canvas(session);
  if (!canvas || !canvas->has_selection()) { script_vector::invalid("selection.empty"); }
  auto path = fit_selection_vector_path(canvas->selected_document_region(), tolerance);
  if (path.empty()) { script_vector::invalid("selection.empty"); }
  return path;
}
}  // namespace patchy::ui
