#pragma once

// MainWindowTestAccess, moved verbatim from tests/ui_visual_tests.cpp. It is
// befriended BY NAME in src/ui/main_window.hpp (friend class MainWindowTestAccess
// inside namespace patchy::ui), so the qualified name
// patchy::ui::MainWindowTestAccess must not change.

#include "ui/document_float_window.hpp"
#include "ui/main_window.hpp"

#include <QPoint>
#include <QRect>
#include <QString>

#include <cstddef>
#include <utility>

namespace patchy::ui {

class MainWindowTestAccess {
public:
  static Document& document(MainWindow& window) {
    return window.document();
  }

  static CanvasWidget* canvas(MainWindow& window) {
    return window.canvas_;
  }

  static void open_document_path(MainWindow& window, QString path) {
    window.open_document_path(std::move(path));
  }

  static void refresh_document_info(MainWindow& window) {
    window.refresh_document_info();
  }

  static bool apply_text_warp(MainWindow& window, Layer& layer, const TextWarp& warp) {
    return window.apply_text_warp_to_layer(layer, warp);
  }

  static void request_warp_text_dialog(MainWindow& window) {
    window.request_warp_text_dialog();
  }

  static ImageSaveOptions image_save_defaults(MainWindow& window) {
    return window.image_save_defaults_for_document();
  }

  static StressReport run_stress_scenario(MainWindow& window, const StressTestOptions& options) {
    return window.run_stress_test_scenario(options);
  }

  static void import_from_scanner(MainWindow& window) {
    window.import_from_scanner();
  }

  static void set_ruler_unit_preference(MainWindow& window, MeasurementUnit unit) {
    window.set_ruler_unit_preference(unit);
  }

  static void open_smart_object_contents(MainWindow& window) {
    window.open_smart_object_contents();
  }

  static void replace_smart_object_contents_with_path(MainWindow& window, const QString& path) {
    window.replace_smart_object_contents_with_path(path);
  }

  static void place_embedded_file_with_path(MainWindow& window, const QString& path) {
    window.place_embedded_file_with_path(path);
  }

  static void relink_smart_object_contents_with_path(MainWindow& window, const QString& path) {
    window.relink_smart_object_contents_with_path(path);
  }

  static void show_layer_context_menu(MainWindow& window, QPoint position) {
    window.show_layer_context_menu(position);
  }

  static void refresh_layer_ui(MainWindow& window) {
    window.refresh_layer_list();
    window.refresh_layer_controls();
  }

  static void set_right_dock_stack_width(MainWindow& window, int width) {
    window.set_right_dock_stack_width(width);
  }

  static void update_document_action_state(MainWindow& window) {
    window.update_document_action_state();
  }

  static void set_round_brush_session(MainWindow& window, BrushDynamics dynamics,
                                      double base_angle_degrees, double base_roundness) {
    window.round_brush_dynamics_ = std::move(dynamics);
    window.round_brush_base_angle_degrees_ = base_angle_degrees;
    window.round_brush_base_roundness_ = base_roundness;
  }

  static bool save_document(MainWindow& window) {
    return window.save_document();
  }

  static bool save_document_to_path(MainWindow& window, QString path, ImageSaveOptions options) {
    return window.save_document_to_path(std::move(path), std::move(options));
  }

  static QString active_session_path(MainWindow& window) {
    return window.session().path;
  }

  static bool close_document_tab(MainWindow& window, int index) {
    return window.close_document_tab(index);
  }

  static void float_active_document(MainWindow& window) {
    window.float_active_document();
  }

  static void dock_active_document(MainWindow& window) {
    window.dock_active_document();
  }

  static void consolidate_all_to_tabs(MainWindow& window) {
    window.consolidate_all_to_tabs();
  }

  // Deterministic offscreen substitute for OS window activation: the exact entry
  // point float WindowActivate / canvas FocusIn wiring funnels into.
  static void activate_canvas(MainWindow& window, CanvasWidget* canvas) {
    window.activate_document_canvas(canvas);
  }

  // Deterministic substitute for the drag-settle timer path: the timer ends in
  // exactly this call with QCursor::pos().
  static void dock_float_at(MainWindow& window, QWidget* float_window, QPoint global_position) {
    window.maybe_dock_float_at(qobject_cast<DocumentFloatWindow*>(float_window), global_position);
  }

  static QRect float_dock_zone(MainWindow& window) {
    return window.float_dock_zone_global();
  }

  // Deterministic substitute for the per-moveEvent highlight update, which reads
  // QCursor::pos() in production.
  static void float_dock_feedback_at(MainWindow& window, QPoint global_position) {
    window.update_float_dock_highlight(global_position);
  }

  static std::size_t session_count(MainWindow& window) {
    return window.sessions_.size();
  }

  static bool active_session_is_floated(MainWindow& window) {
    return window.session().float_window != nullptr;
  }

  static Document* document_for_canvas(MainWindow& window, CanvasWidget* canvas) {
    auto* session = window.session_for_canvas(canvas);
    return session == nullptr ? nullptr : &session->document;
  }

  static std::ptrdiff_t undo_depth_for_canvas(MainWindow& window, CanvasWidget* canvas) {
    auto* session = window.session_for_canvas(canvas);
    return session == nullptr ? -1 : static_cast<std::ptrdiff_t>(session->undo_stack.size());
  }

  static std::size_t active_session_undo_depth(MainWindow& window) {
    return window.session().undo_stack.size();
  }

  static bool active_session_is_modified(MainWindow& window) {
    return window.session_is_modified(window.session());
  }

  static bool active_session_is_smart_object_child(MainWindow& window) {
    return window.session().smart_object_link.has_value();
  }
};

}  // namespace patchy::ui
