// Running a GIMP 3 plug-in on the active layer (docs/gimp-plugins.md, "Running" and
// "Settings dialog and canvas preview"): the layer handed over and written back as one
// undo step, the plug-in's own dialog with its run box, Patchy's settings dialog with
// the canvas preview, and the blocking run scripts use.

#include "ui/main_window.hpp"

#include "color/rgb_profiles.hpp"
#include "core/layer.hpp"
#include "core/pixel_buffer.hpp"
#include "ui/app_settings.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/gimp_plugin_arguments.hpp"
#include "ui/gimp_plugin_catalog.hpp"
#include "ui/gimp_plugin_layer_input.hpp"
#include "ui/gimp_plugin_run_dialog.hpp"
#include "ui/gimp_plugin_runner.hpp"
#include "ui/gimp_plugin_settings_dialog.hpp"
#include "ui/main_window_shared.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QMessageBox>
#include <QPointer>
#include <QScopeGuard>
#include <QStatusBar>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace patchy::ui {

namespace {

// A settings change starts a preview once the control has been still this long (a
// slider drag reports every step).
constexpr int kPreviewSettleMs = 250;
// The quick pass renders at most this many pixels along its longer side.
constexpr int kQuickPassMaxSide = 1600;

// The visible part of the layer, in layer pixels.
QRect visible_layer_rect(const CanvasWidget& canvas, const GimpLayerInput& layer) {
  const QRectF widget(QPointF(0, 0), QSizeF(canvas.width(), canvas.height()));
  QRectF document;
  for (const auto& corner : {widget.topLeft(), widget.topRight(), widget.bottomLeft(), widget.bottomRight()}) {
    const auto point = canvas.document_point_for_widget_position(corner);
    document = document.isNull() ? QRectF(point, QSizeF(0, 0)) : document.united(QRectF(point, QSizeF(0, 0)));
  }
  return document.toAlignedRect().intersected(layer.layer_rect).translated(-layer.layer_rect.topLeft());
}

// A nearest-neighbor copy of `rect` of a packed buffer, `scale` times its size.
std::vector<std::uint8_t> scaled_region(const std::vector<std::uint8_t>& source, int source_width, std::size_t pixel_bytes,
                                        QRect rect, int width, int height) {
  std::vector<std::uint8_t> out(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * pixel_bytes);
  for (int y = 0; y < height; ++y) {
    const int sy = rect.top() + std::min(rect.height() - 1, static_cast<int>((y + 0.5) * rect.height() / height));
    for (int x = 0; x < width; ++x) {
      const int sx = rect.left() + std::min(rect.width() - 1, static_cast<int>((x + 0.5) * rect.width() / width));
      std::memcpy(out.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * pixel_bytes,
                  source.data() + (static_cast<std::size_t>(sy) * static_cast<std::size_t>(source_width) + static_cast<std::size_t>(sx)) *
                                      pixel_bytes,
                  pixel_bytes);
    }
  }
  return out;
}

#ifdef Q_OS_WIN
// The plug-in's dialog disables its owner (Patchy's window) while it is open; the canvas
// stays usable for zoom and scroll (edits are locked), so the window is enabled again.
void keep_window_enabled(QWidget& window) {
  const HWND self = reinterpret_cast<HWND>(window.winId());
  if (!IsWindowEnabled(self)) {
    EnableWindow(self, TRUE);
  }
}
#endif

}  // namespace

bool MainWindow::prepare_gimp_layer_input(DocumentSession& session, LayerId layer_id, GimpLayerInput& in, QString* error) {
  const auto fail = [error](const QString& message) {
    if (error != nullptr) *error = message;
    return false;
  };
  const auto* layer = std::as_const(session.document).find_layer(layer_id);
  if (layer == nullptr || layer->kind() != LayerKind::Pixel) {
    return fail(tr("Select an editable pixel layer before running the plug-in"));
  }
  const auto& source = layer->pixels();
  const auto format = source.format();
  if (format.color_mode != ColorMode::RGB || (format.channels != 3 && format.channels != 4)) {
    return fail(tr("GIMP plug-ins run on RGB layers. Convert the document to RGB first."));
  }
  in.session_id = session.session_id;
  in.layer_id = layer_id;
  in.width = source.width();
  in.height = source.height();
  const auto bounds = layer->bounds();
  in.layer_rect = QRect(bounds.x, bounds.y, in.width, in.height);
  in.format = format;
  in.layer_name = QString::fromStdString(layer->name());
  // A layer without alpha that may hold transparency is handed over with an opaque alpha
  // channel, as GIMP plug-ins expect to add one (Pagecurl); the result keeps alpha only
  // when it uses it. A layer whose transparency is locked (the Background) stays RGB.
  in.expand_alpha = format.channels == 3 && !layer_id_locks_transparent_pixels(layer_id);
  in.protect_alpha = format.channels == 4 && layer_id_locks_transparent_pixels(layer_id);
  in.channel_bytes = bytes_per_channel(format.bit_depth);
  in.layer_pixel_bytes = bytes_per_pixel(format);
  in.pixel_bytes = in.expand_alpha ? in.channel_bytes * 4 : in.layer_pixel_bytes;
  in.row_bytes = static_cast<std::size_t>(in.width) * in.pixel_bytes;
  in.opaque.assign(in.channel_bytes, 0);
  if (format.bit_depth == BitDepth::UInt8) {
    in.opaque[0] = 0xFF;
  } else if (format.bit_depth == BitDepth::UInt16) {
    in.opaque[0] = in.opaque[1] = 0xFF;
  } else {
    const float one = 1.0f;
    std::memcpy(in.opaque.data(), &one, sizeof one);
  }
  in.input.assign(in.row_bytes * static_cast<std::size_t>(in.height), 0);
  for (std::int32_t y = 0; y < in.height; ++y) {
    const auto row = source.row(y);
    auto* out = in.input.data() + static_cast<std::size_t>(y) * in.row_bytes;
    if (!in.expand_alpha) {
      std::memcpy(out, row.data(), in.row_bytes);
      continue;
    }
    for (std::int32_t x = 0; x < in.width; ++x) {
      std::memcpy(out + static_cast<std::size_t>(x) * in.pixel_bytes,
                  row.data() + static_cast<std::size_t>(x) * in.layer_pixel_bytes, in.layer_pixel_bytes);
      std::memcpy(out + static_cast<std::size_t>(x) * in.pixel_bytes + in.layer_pixel_bytes, in.opaque.data(),
                  in.channel_bytes);
    }
  }
  // The selection, in layer pixels: its coverage limits where results land.
  in.coverage.clear();
  in.mask_bounds = QRect();
  if (session.canvas != nullptr) {
    const auto selection = session.canvas->selected_document_region();
    if (!selection.isEmpty()) {
      const auto selected = selection.boundingRect().intersected(in.layer_rect);
      if (selected.isEmpty()) {
        return fail(tr("The selection does not touch the active layer."));
      }
      in.mask_bounds = selected.translated(-in.layer_rect.topLeft());
      const auto mask = selection_mask_pixels(*session.canvas, in.layer_rect);
      in.coverage.resize(static_cast<std::size_t>(in.width) * static_cast<std::size_t>(in.height));
      for (std::int32_t y = 0; y < in.height; ++y) {
        std::memcpy(in.coverage.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(in.width),
                    mask.row(y).data(), static_cast<std::size_t>(in.width));
      }
    }
  }
  in.original = source;  // shared storage, no copy until the layer is written
  return true;
}

bool MainWindow::gimp_run_input(DocumentSession& session, const GimpPluginProcedure& entry, const GimpLayerInput& layer,
                                const GimpPluginApplyOptions& options, GimpPluginRunInput& run, QString* error) {
  if (gimp_plugins_ == nullptr || !gimp_plugins_->install.has_value()) {
    if (error != nullptr) *error = tr("GIMP 3 was not found, so its plug-ins cannot run.");
    return false;
  }
  const auto display_name = gimp_plugin_display_name(entry.label);
  QString launch_error;
  auto launch = gimp_plugin_launch_spec(*gimp_plugins_->install, entry.plugin_file, gimp_plugin_language(), &launch_error);
  if (!launch.has_value()) {
    if (error != nullptr) *error = tr("%1 could not be started: %2").arg(display_name, launch_error);
    return false;
  }
  run.helper = gimp_plugin_helper_path();
  run.launch = std::move(*launch);
  run.procedure = entry.procedure;
  run.install = entry.install;
  run.run_mode = options.run_mode;
  run.arguments = options.arguments;
  run.argument_count = options.argument_count;
  run.image_width = session.document.width();
  run.image_height = session.document.height();
  run.layer_x = layer.layer_rect.x();
  run.layer_y = layer.layer_rect.y();
  run.width = layer.width;
  run.height = layer.height;
  run.bit_depth = static_cast<std::uint32_t>(layer.format.bit_depth);
  run.channels = layer.expand_alpha ? 4u : layer.format.channels;
  run.protect_alpha = layer.protect_alpha;
  run.layer_name = layer.layer_name;
  run.image_name = session.title;
  run.resolution = text_size_ppi(session.document);
  run.input = &layer.input;
  run.mask = layer.coverage.empty() ? nullptr : &layer.coverage;
  run.mask_bounds = layer.mask_bounds;
  if (session.canvas != nullptr) {
    const auto fg = session.canvas->primary_color();
    const auto bg = session.canvas->secondary_color();
    const double f[4] = {fg.redF(), fg.greenF(), fg.blueF(), 1.0};
    const double b[4] = {bg.redF(), bg.greenF(), bg.blueF(), 1.0};
    std::memcpy(run.foreground, f, sizeof f);
    std::memcpy(run.background, b, sizeof b);
  }
  run.parent_window = QGuiApplication::platformName() == QLatin1String("offscreen") || options.run_mode != 0
                          ? 0
                          : static_cast<std::uint64_t>(winId());
  run.language = gimp_plugin_language();
  run.timeout_ms = options.timeout_ms;
  for (const auto& [name, bytes] : gimp_plugin_stored_data_) {
    run.stored_data.emplace_back(name, bytes);
  }
  {
    // Patchy's RGB is sRGB; GIMP's colors carry their profile.
    static const auto srgb = create_rgb_matrix_profile(RgbMatrixProfileSpec{"sRGB (Patchy)"});
    run.color_profile = srgb;
  }
  run.capture_dialog_path = options.capture_dialog;
  run.answer_dialog = options.answer_dialog;
  run.trace = options.trace || qEnvironmentVariableIsSet("PATCHY_GIMP_TRACE");
  run.wire_log = options.wire_log.isEmpty() ? qEnvironmentVariable("PATCHY_GIMP_WIRE_LOG") : options.wire_log;
  return true;
}

void MainWindow::restore_gimp_layer(DocumentSession& session, const GimpLayerInput& layer) {
  auto* target = session.document.find_layer(layer.layer_id);
  if (target == nullptr) {
    return;
  }
  target->set_pixels(layer.original);
  if (session.canvas != nullptr) {
    session.canvas->document_changed(layer.layer_rect);
  }
}

void MainWindow::show_gimp_preview_pixels(DocumentSession& session, const GimpLayerInput& layer, const std::uint8_t* pixels,
                                          int width, int height, QRect rect, double scale, bool restore_first) {
  auto* target = session.document.find_layer(layer.layer_id);
  if (target == nullptr || pixels == nullptr) {
    return;
  }
  if (restore_first) {
    target->set_pixels(layer.original);
  }
  const auto area = rect.intersected(QRect(0, 0, layer.width, layer.height));
  if (area.isEmpty() || std::as_const(*target).pixels().width() != layer.width ||
      std::as_const(*target).pixels().height() != layer.height) {
    return;
  }
  auto& dst = target->pixels();
  (void)dst.data();  // detach shared storage once before the row writes
  // The preview shows the colors (and alpha the layer has); alpha the plug-in adds lands on OK.
  const auto copy_bytes = layer.layer_pixel_bytes;
  for (int y = area.top(); y <= area.bottom(); ++y) {
    auto* out = dst.row(y).data();
    const int sy = std::clamp(static_cast<int>((y - rect.top() + 0.5) * scale), 0, height - 1);
    for (int x = area.left(); x <= area.right(); ++x) {
      const int sx = std::clamp(static_cast<int>((x - rect.left() + 0.5) * scale), 0, width - 1);
      std::memcpy(out + static_cast<std::size_t>(x) * layer.layer_pixel_bytes,
                  pixels + (static_cast<std::size_t>(sy) * static_cast<std::size_t>(width) + static_cast<std::size_t>(sx)) *
                               layer.pixel_bytes,
                  copy_bytes);
    }
  }
  if (session.canvas != nullptr) {
    session.canvas->document_changed((restore_first ? QRect(0, 0, layer.width, layer.height) : area)
                                         .translated(layer.layer_rect.topLeft()));
  }
}

MainWindow::GimpPluginApplyStatus MainWindow::finish_gimp_run(DocumentSession& session, const QString& display_name,
                                                              const GimpLayerInput& in, const GimpPluginRunResult& result,
                                                              const std::function<void()>& before_write, QString* error) {
  const auto fail = [error](const QString& message) {
    if (error != nullptr) *error = message;
    return GimpPluginApplyStatus::Error;
  };
  for (const auto& [name, bytes] : result.stored_data) {
    gimp_plugin_stored_data_[name] = bytes;
  }
  auto* layer = session.document.find_layer(in.layer_id);
  if (layer != nullptr && std::as_const(*layer).pixels().data().data() != in.original.data().data()) {
    restore_gimp_layer(session, in);
  }
  if (result.status == GimpPluginRunStatus::Cancelled) {
    return GimpPluginApplyStatus::Cancelled;
  }
  if (result.status != GimpPluginRunStatus::Ok) {
    return fail(gimp_plugin_error_text(display_name, result));
  }
  // Colors the plug-in picked become Patchy's, as in GIMP (Border Average).
  if (session.canvas != nullptr && result.foreground_changed) {
    session.canvas->set_primary_color(QColor::fromRgbF(static_cast<float>(result.foreground[0]),
                                                       static_cast<float>(result.foreground[1]),
                                                       static_cast<float>(result.foreground[2])));
  }
  if (session.canvas != nullptr && result.background_changed) {
    session.canvas->set_secondary_color(QColor::fromRgbF(static_cast<float>(result.background[0]),
                                                         static_cast<float>(result.background[1]),
                                                         static_cast<float>(result.background[2])));
  }
  if (layer == nullptr) {
    return fail(tr("The layer no longer exists."));
  }
  if (result.output.size() != in.input.size() || !result.changed || result.output == in.input) {
    return GimpPluginApplyStatus::NoChange;
  }
  if (before_write) {
    before_write();
  }
  layer = session.document.find_layer(in.layer_id);
  if (layer == nullptr) {
    return fail(tr("The layer no longer exists."));
  }
  bool uses_alpha = false;
  if (in.expand_alpha) {
    for (std::size_t i = in.layer_pixel_bytes; i < result.output.size() && !uses_alpha; i += in.pixel_bytes) {
      uses_alpha = std::memcmp(result.output.data() + i, in.opaque.data(), in.channel_bytes) != 0;
    }
  }
  if (uses_alpha) {
    // The plug-in made pixels transparent: the layer gains an alpha channel.
    PixelBuffer with_alpha(in.width, in.height, PixelFormat{in.format.color_mode, in.format.bit_depth, 4});
    for (std::int32_t y = 0; y < in.height; ++y) {
      std::memcpy(with_alpha.row(y).data(), result.output.data() + static_cast<std::size_t>(y) * in.row_bytes, in.row_bytes);
    }
    layer->set_pixels(std::move(with_alpha));
  } else {
    auto& pixels = layer->pixels();
    (void)pixels.data();
    for (std::int32_t y = 0; y < in.height; ++y) {
      const auto* src = result.output.data() + static_cast<std::size_t>(y) * in.row_bytes;
      auto* out = pixels.row(y).data();
      if (!in.expand_alpha) {
        std::memcpy(out, src, in.row_bytes);
        continue;
      }
      for (std::int32_t x = 0; x < in.width; ++x) {
        std::memcpy(out + static_cast<std::size_t>(x) * in.layer_pixel_bytes, src + static_cast<std::size_t>(x) * in.pixel_bytes,
                    in.layer_pixel_bytes);
      }
    }
  }
  if (session.canvas != nullptr) {
    session.canvas->document_changed(in.layer_rect);
  }
  return GimpPluginApplyStatus::Applied;
}

// Scripts: one blocking run with live canvas updates (a script runs one call at a time).
MainWindow::GimpPluginApplyStatus MainWindow::apply_gimp_plugin(DocumentSession& session, LayerId layer_id,
                                                                const GimpPluginProcedure& entry,
                                                                const GimpPluginApplyOptions& options,
                                                                const std::function<void()>& before_write,
                                                                QString* error) {
  const auto display_name = gimp_plugin_display_name(entry.label);
  if (gimp_plugins_ == nullptr || !gimp_plugins_->install.has_value()) {
    if (error != nullptr) *error = tr("GIMP 3 was not found, so its plug-ins cannot run.");
    return GimpPluginApplyStatus::Error;
  }
  if (entry.use != QLatin1String("layer")) {
    if (error != nullptr) *error = tr("%1 cannot run in Patchy: %2").arg(display_name, gimp_plugin_reason_text(entry.reason));
    return GimpPluginApplyStatus::Error;
  }
  if (const auto* layer = std::as_const(session.document).find_layer(layer_id);
      layer != nullptr && layer->kind() == LayerKind::Pixel && layer->pixels().empty()) {
    return GimpPluginApplyStatus::NoChange;
  }
  GimpLayerInput in;
  if (!prepare_gimp_layer_input(session, layer_id, in, error)) {
    return GimpPluginApplyStatus::Error;
  }
  GimpPluginRunInput run;
  if (!gimp_run_input(session, entry, in, options, run, error)) {
    return GimpPluginApplyStatus::Error;
  }
  const auto session_id = session.session_id;
  GimpPluginRunDialog box(display_name, this);
  QElapsedTimer elapsed;
  elapsed.start();
  GimpPluginRunCallbacks callbacks;
  callbacks.update = [this, session_id, &in](const QRect& rect, const std::uint8_t* pixels) {
    if (auto* target = session_with_id(session_id); target != nullptr) {
      show_gimp_preview_pixels(*target, in, pixels, in.width, in.height, rect, 1.0, false);
    }
  };
  callbacks.progress = [&box](double fraction, const QString& text) { box.set_progress(fraction, text); };
  callbacks.plugin_window = [&box](const QRect& rect) { box.set_plugin_window(rect); };
  callbacks.cancelled = [&box] { return box.stop_requested(); };
  callbacks.tick = [&box, &elapsed] {
    if (elapsed.elapsed() > kFilterProgressMinimumDurationMs) {
      box.reveal();
    }
  };
  begin_preview_dialog_edit_lock();
  const auto result = run_gimp_plugin_out_of_process(run, callbacks);
  box.hide();
  end_preview_dialog_edit_lock();
  return finish_gimp_run(session, display_name, in, result, before_write, error);
}

void MainWindow::run_gimp_plugin(const QString& identifier, int run_mode) {
  if (gimp_plugin_active_) {
    show_status_error(tr("A GIMP plug-in is already running"));
    return;
  }
  if (canvas_ != nullptr && canvas_->quick_mask_active()) {
    show_status_error(tr("Filters are unavailable in Quick Mask mode"));
    return;
  }
  if (canvas_ != nullptr &&
      (canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::DocumentChannel ||
       CanvasWidget::is_component_preview(canvas_->layer_edit_target()))) {
    show_status_error(tr("Filters are unavailable while viewing a document channel"));
    return;
  }
  const auto* found = find_gimp_plugin(identifier);
  if (found == nullptr || found->use != QLatin1String("layer")) {
    return;
  }
  // A rescan may replace the catalog while the dialog is up: work on a copy.
  const GimpPluginProcedure entry = *found;
  auto* session = active_session();
  if (session == nullptr) {
    return;
  }
  select_only_layer_if_none_active();
  const auto active = document().active_layer_id();
  if (!active.has_value()) {
    show_status_error(tr("Select a pixel layer before running the plug-in"));
    return;
  }
  if (selected_or_active_layer_ids().size() > 1) {
    (void)show_warning_message(this, tr("GIMP 3 Plug-in"),
                               tr("Plug-ins work on one layer at a time. Select a single layer and run it again."),
                               QMessageBox::Ok, QMessageBox::Ok, QStringLiteral("gimpPluginOneLayerMessageBox"));
    return;
  }
  const auto* layer = std::as_const(document()).find_layer(*active);
  if (layer == nullptr) {
    show_status_error(tr("Select a pixel layer before running the plug-in"));
    return;
  }
  const auto display_name = gimp_plugin_display_name(entry.label);
  if (layer->kind() == LayerKind::Text || layer_pixels_are_procedural(*layer)) {
    if (!prompt_rasterize_procedural_layer(*active, display_name, /*offer_convert_to_smart_object=*/false)) {
      return;
    }
    layer = std::as_const(document()).find_layer(*active);
    if (layer == nullptr) {
      return;
    }
  }
  if (layer->kind() != LayerKind::Pixel || layer_pixels_are_procedural(*layer)) {
    show_status_error(tr("Select an editable pixel layer before running the plug-in"));
    return;
  }
  if (layer_id_locks_image_pixels(*active)) {
    show_status_error(tr("Layer pixels are locked."));
    return;
  }
  if (layer->pixels().empty()) {
    statusBar()->showMessage(tr("%1 made no changes").arg(display_name));
    return;
  }
  gimp_plugin_active_ = true;
  const auto active_guard = qScopeGuard([this] {
    gimp_plugin_active_ = false;
  });
  const auto session_id = session->session_id;
  const auto arguments = gimp_plugin_arguments(entry);
  const bool patchy_dialog_possible = arguments.own_dialog_reason.isEmpty() && run_mode == 0;
  bool own_dialog = !patchy_dialog_possible || stored_gimp_plugin_own_dialog(entry.procedure);
  QString error;
  GimpDialogOutcome outcome = GimpDialogOutcome::Cancelled;
  for (;;) {
    auto* target = session_with_id(session_id);
    if (target == nullptr) {
      return;
    }
    outcome = own_dialog ? run_gimp_plugin_own_dialog(*target, *active, entry, run_mode, patchy_dialog_possible, &error)
                         : run_gimp_plugin_settings_dialog(*target, *active, entry, arguments, &error);
    if (outcome != GimpDialogOutcome::UseOwnDialog) {
      break;
    }
    own_dialog = true;
  }
  activateWindow();
  if (auto* focus = fallback_active_canvas(); focus != nullptr) {
    focus->setFocus(Qt::OtherFocusReason);
  }
  switch (outcome) {
    case GimpDialogOutcome::Applied: {
      const auto undo_key = undo_action_ != nullptr ? undo_action_->shortcut() : QKeySequence();
      statusBar()->showMessage(undo_key.isEmpty()
                                   ? tr("Applied %1").arg(display_name)
                                   : tr("Applied %1 (%2 undoes it)").arg(display_name, undo_key.toString(QKeySequence::NativeText)));
      break;
    }
    case GimpDialogOutcome::NoChange:
      statusBar()->showMessage(tr("%1 made no changes").arg(display_name));
      break;
    case GimpDialogOutcome::Cancelled:
    case GimpDialogOutcome::UseOwnDialog:
      statusBar()->showMessage(tr("Cancelled %1").arg(display_name));
      break;
    case GimpDialogOutcome::Failed:
      (void)show_warning_message(this, tr("GIMP 3 Plug-in"), error, QMessageBox::Ok, QMessageBox::Ok,
                                 QStringLiteral("gimpPluginErrorMessageBox"));
      break;
  }
}

MainWindow::GimpDialogOutcome MainWindow::run_gimp_plugin_own_dialog(DocumentSession& session, LayerId layer_id,
                                                                     const GimpPluginProcedure& entry, int run_mode,
                                                                     bool offer_patchy_dialog, QString* error) {
  const auto display_name = gimp_plugin_display_name(entry.label);
  GimpLayerInput in;
  if (!prepare_gimp_layer_input(session, layer_id, in, error)) {
    return GimpDialogOutcome::Failed;
  }
  GimpPluginApplyOptions options;
  options.run_mode = run_mode;
  options.answer_dialog = std::exchange(gimp_plugin_test_answer_, 0);
  if (options.answer_dialog != 0) {
    options.timeout_ms = 120000;
  }
  GimpPluginRunInput input;
  if (!gimp_run_input(session, entry, in, options, input, error)) {
    return GimpDialogOutcome::Failed;
  }
  const auto session_id = session.session_id;
  GimpPluginRunDialog box(display_name, this);
  if (offer_patchy_dialog) {
    box.set_patchy_dialog_option(true, [procedure = entry.procedure](bool use_patchy) {
      set_stored_gimp_plugin_own_dialog(procedure, !use_patchy);
    });
  }
  QElapsedTimer elapsed;
  elapsed.start();
  QEventLoop loop;
  std::optional<GimpPluginRunResult> result;
  auto* run = new GimpPluginRun(this);
  GimpPluginRunCallbacks callbacks;
  callbacks.update = [this, session_id, &in](const QRect& rect, const std::uint8_t* pixels) {
    if (auto* target = session_with_id(session_id); target != nullptr) {
      show_gimp_preview_pixels(*target, in, pixels, in.width, in.height, rect, 1.0, false);
    }
  };
  callbacks.progress = [&box](double fraction, const QString& text) { box.set_progress(fraction, text); };
  callbacks.plugin_window = [&box](const QRect& rect) { box.set_plugin_window(rect); };
  callbacks.cancelled = [&box] { return box.stop_requested(); };
  callbacks.tick = [this, &box, &elapsed] {
    if (elapsed.elapsed() > kFilterProgressMinimumDurationMs) {
      box.reveal();
    }
#ifdef Q_OS_WIN
    keep_window_enabled(*this);
#endif
  };

  begin_preview_dialog_edit_lock();
  run->start(input, std::move(callbacks), [&result, &loop](GimpPluginRunResult r) {
    result = std::move(r);
    loop.quit();
  });
  if (!result.has_value()) {
    loop.exec();
  }
  if (run->running()) {
    run->abort();  // deletes itself once its helper is gone
  } else {
    run->deleteLater();
  }
  box.hide();
  end_preview_dialog_edit_lock();
  auto* target = session_with_id(session_id);
  if (target == nullptr) {
    return GimpDialogOutcome::Cancelled;
  }
  const auto status = finish_gimp_run(
      *target, display_name, in, *result,
      [this, display_name] { push_undo_snapshot(tr("GIMP Plug-in: %1").arg(display_name)); }, error);
  switch (status) {
    case GimpPluginApplyStatus::Applied:
      return GimpDialogOutcome::Applied;
    case GimpPluginApplyStatus::NoChange:
      return GimpDialogOutcome::NoChange;
    case GimpPluginApplyStatus::Cancelled:
      return GimpDialogOutcome::Cancelled;
    case GimpPluginApplyStatus::Error:
      break;
  }
  return GimpDialogOutcome::Failed;
}

MainWindow::GimpDialogOutcome MainWindow::run_gimp_plugin_settings_dialog(DocumentSession& session, LayerId layer_id,
                                                                          const GimpPluginProcedure& entry,
                                                                          const GimpPluginArgumentSet& arguments,
                                                                          QString* error) {
  const auto display_name = gimp_plugin_display_name(entry.label);
  GimpLayerInput in;
  if (!prepare_gimp_layer_input(session, layer_id, in, error)) {
    return GimpDialogOutcome::Failed;
  }
  const auto session_id = session.session_id;
  const auto stored = gimp_plugin_values_from_json(stored_gimp_plugin_values(entry.procedure));
  GimpPluginSettingsDialog dialog(display_name, entry.blurb, arguments, gimp_plugin_merge_values(arguments, stored),
                                  stored_gimp_plugin_preview(entry.procedure), this);

  // The preview pipeline: a quick pass of what is on screen at screen resolution, then the
  // full pass of the whole layer, which OK reuses while the settings stay as they are.
  struct Pass {
    QPointer<GimpPluginRun> run;
    bool full{false};
  };
  struct Preview {
    int generation{0};
    Pass pass;
    bool shown{false};          // the layer shows a preview, not its own pixels
    bool full_ready{false};     // `full_result` belongs to `generation`
    GimpPluginRunResult full_result;
    bool applying{false};       // OK waits for the full pass
    bool switch_to_own{false};
    std::optional<GimpPluginApplyStatus> applied;
  } state;
  QTimer settle;
  settle.setSingleShot(true);
  settle.setInterval(kPreviewSettleMs);

  const auto stop_pass = [this, &state] {
    if (state.pass.run != nullptr && state.pass.run->running()) {
      state.pass.run->abort();
      ++gimp_preview_runs_aborted_;
    } else if (state.pass.run != nullptr) {
      state.pass.run->deleteLater();
    }
    state.pass.run = nullptr;
  };
  const auto target_session = [this, session_id]() { return session_with_id(session_id); };
  const auto restore = [this, &state, &in, target_session] {
    if (state.shown) {
      if (auto* target = target_session(); target != nullptr) {
        restore_gimp_layer(*target, in);
      }
      state.shown = false;
    }
  };
  const auto apply_full = [this, &state, &in, &dialog, target_session, display_name, error] {
    auto* target = target_session();
    if (target == nullptr) {
      state.applied = GimpPluginApplyStatus::Cancelled;
      dialog.finish_accept();
      return;
    }
    state.shown = false;  // finish_gimp_run puts the layer back first
    state.applied = finish_gimp_run(
        *target, display_name, in, state.full_result,
        [this, display_name] { push_undo_snapshot(tr("GIMP Plug-in: %1").arg(display_name)); }, error);
    if (*state.applied == GimpPluginApplyStatus::Error) {
      dialog.set_applying(false);
      state.applying = false;
      dialog.set_preview_state(GimpPluginSettingsDialog::PreviewState::Failed, error != nullptr ? *error : QString());
      state.applied.reset();
      return;
    }
    dialog.finish_accept();
  };
  std::function<void(bool)> start_pass;
  start_pass = [&, target_session](bool full) {
    stop_pass();
    auto* target = target_session();
    if (target == nullptr) {
      return;
    }
    QByteArray message;
    std::uint32_t count = 0;
    QString why;
    if (!encode_gimp_plugin_arguments(entry, arguments, dialog.values(), &message, &count, &why)) {
      dialog.set_preview_state(GimpPluginSettingsDialog::PreviewState::Failed, why);
      return;
    }
    GimpPluginApplyOptions options;
    options.run_mode = 1;
    options.arguments = message;
    options.argument_count = count;
    GimpPluginRunInput input;
    if (!gimp_run_input(*target, entry, in, options, input, &why)) {
      dialog.set_preview_state(GimpPluginSettingsDialog::PreviewState::Failed, why);
      return;
    }
    // The quick pass: the visible part of the layer at screen resolution (its result
    // is approximate for settings measured in pixels; the full pass replaces it).
    QRect region(0, 0, in.width, in.height);
    double scale = 1.0;
    std::vector<std::uint8_t> small_pixels;
    std::vector<std::uint8_t> small_mask;
    if (!full && target->canvas != nullptr) {
      region = visible_layer_rect(*target->canvas, in);
      if (!in.mask_bounds.isEmpty()) {
        region = region.intersected(in.mask_bounds.adjusted(-8, -8, 8, 8)).intersected(QRect(0, 0, in.width, in.height));
      }
      const double zoom = std::max(0.01, target->canvas->zoom());
      scale = std::min({1.0, zoom, static_cast<double>(kQuickPassMaxSide) / std::max(1, std::max(region.width(), region.height()))});
      const bool whole = region == QRect(0, 0, in.width, in.height);
      if (region.isEmpty() || (whole && scale > 0.99)) {
        full = true;  // the quick pass would be the full one
        region = QRect(0, 0, in.width, in.height);
        scale = 1.0;
      }
    }
    int width = in.width;
    int height = in.height;
    if (!full) {
      width = std::max(1, static_cast<int>(std::lround(region.width() * scale)));
      height = std::max(1, static_cast<int>(std::lround(region.height() * scale)));
      small_pixels = scaled_region(in.input, in.width, in.pixel_bytes, region, width, height);
      if (!in.coverage.empty()) {
        small_mask = scaled_region(in.coverage, in.width, 1, region, width, height);
      }
      input.image_width = width;
      input.image_height = height;
      input.layer_x = 0;
      input.layer_y = 0;
      input.width = width;
      input.height = height;
      input.input = &small_pixels;
      input.mask = small_mask.empty() ? nullptr : &small_mask;
      input.mask_bounds = small_mask.empty() ? QRect() : QRect(0, 0, width, height);
    }
    state.pass.full = full;
    auto* run = new GimpPluginRun(this);
    state.pass.run = run;
    ++gimp_preview_runs_started_;
    const int generation = state.generation;
    const double back = full ? 1.0 : static_cast<double>(width) / std::max(1, region.width());
    dialog.set_preview_state(GimpPluginSettingsDialog::PreviewState::Updating);
    run->start(input, {}, [&, run, generation, full, region, width, height, back, target_session](GimpPluginRunResult result) {
      run->deleteLater();
      if (state.pass.run == run) {
        state.pass.run = nullptr;
      }
      if (generation != state.generation) {
        return;
      }
      auto* t = target_session();
      if (t == nullptr) {
        return;
      }
      if (result.status != GimpPluginRunStatus::Ok) {
        restore();
        const auto text = gimp_plugin_error_text(display_name, result);
        dialog.set_preview_state(GimpPluginSettingsDialog::PreviewState::Failed, text);
        if (state.applying) {
          state.applying = false;
          dialog.set_applying(false);
        }
        return;
      }
      if (!full) {
        if (dialog.preview_enabled()) {
          show_gimp_preview_pixels(*t, in, result.output.data(), width, height, region, back, true);
          state.shown = true;
          dialog.set_preview_state(GimpPluginSettingsDialog::PreviewState::Quick);
        }
        start_pass(true);
        return;
      }
      state.full_result = std::move(result);
      state.full_ready = true;
      if (state.applying) {
        apply_full();
        return;
      }
      if (dialog.preview_enabled()) {
        show_gimp_preview_pixels(*t, in, state.full_result.output.data(), in.width, in.height,
                                 QRect(0, 0, in.width, in.height), 1.0, true);
        state.shown = true;
      }
      dialog.set_preview_state(GimpPluginSettingsDialog::PreviewState::Ready);
    });
  };
  const auto restart = [&] {
    ++state.generation;
    state.full_ready = false;
    stop_pass();
    if (dialog.preview_enabled()) {
      start_pass(false);
    } else {
      restore();
      dialog.set_preview_state(GimpPluginSettingsDialog::PreviewState::Off);
    }
  };
  QObject::connect(&settle, &QTimer::timeout, &dialog, restart);
  dialog.set_values_changed_callback([&] {
    // A stale run ends at once; the next starts when the control settles.
    ++state.generation;
    state.full_ready = false;
    stop_pass();
    if (dialog.preview_enabled()) {
      dialog.set_preview_state(GimpPluginSettingsDialog::PreviewState::Updating);
    }
    settle.start();
  });
  dialog.set_preview_toggled_callback([&](bool on) {
    set_stored_gimp_plugin_preview(entry.procedure, on);
    if (!on) {
      ++state.generation;
      state.full_ready = false;
      stop_pass();
      restore();
      dialog.set_preview_state(GimpPluginSettingsDialog::PreviewState::Off);
    } else {
      restart();
    }
  });
  dialog.set_own_dialog_callback([&] {
    set_stored_gimp_plugin_own_dialog(entry.procedure, true);
    state.switch_to_own = true;
    dialog.reject();
  });
  dialog.set_accept_callback([&]() -> bool {
    set_stored_gimp_plugin_values(entry.procedure, gimp_plugin_values_to_json(dialog.values()));
    if (state.full_ready) {
      apply_full();
      return false;  // apply_full closed the dialog (or reported why it could not)
    }
    // The full pass for these settings: still running, or started now.
    state.applying = true;
    dialog.set_applying(true);
    settle.stop();
    if (state.pass.run == nullptr || !state.pass.full) {
      ++state.generation;
      start_pass(true);
    }
    return false;
  });

  begin_preview_dialog_edit_lock();
  if (dialog.preview_enabled()) {
    QTimer::singleShot(0, &dialog, restart);
  }
  const int result = run_non_modal_dialog(dialog);
  settle.stop();
  ++state.generation;
  stop_pass();
  if (result != QDialog::Accepted || !state.applied.has_value()) {
    restore();
  }
  end_preview_dialog_edit_lock();
  if (state.switch_to_own) {
    return GimpDialogOutcome::UseOwnDialog;
  }
  if (result != QDialog::Accepted || !state.applied.has_value()) {
    return GimpDialogOutcome::Cancelled;
  }
  switch (*state.applied) {
    case GimpPluginApplyStatus::Applied:
      return GimpDialogOutcome::Applied;
    case GimpPluginApplyStatus::NoChange:
      return GimpDialogOutcome::NoChange;
    case GimpPluginApplyStatus::Cancelled:
      return GimpDialogOutcome::Cancelled;
    case GimpPluginApplyStatus::Error:
      break;
  }
  return GimpDialogOutcome::Failed;
}

}  // namespace patchy::ui
