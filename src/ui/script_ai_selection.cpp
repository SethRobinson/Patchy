// doc.selection.selectSubject / objectSelect / automaticSelectionAvailable / subjectSelectionAvailable
// (docs/ai-selection.md, docs/scripting.md). Each call is one network solve from one
// prompt, combined with the selection by plain boolean, the same boundary as the
// interactive commands (src/ai/segmentation_engine.hpp).

#include "ui/script_api.hpp"
#include "ui/script_vector.hpp"

#include "ai/segmentation_engine.hpp"
#include "ai/watermark_engine.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/canvas_widget_shared.hpp"
#include "ui/image_document_io.hpp"
#include "ui/main_window.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/script_engine.hpp"
#include "ui/vector_operations.hpp"

#if defined(Q_OS_WASM) && defined(__EMSCRIPTEN_PTHREADS__)
#include "ui/background_workers.hpp"

#include <QEventLoop>
#include <QTimer>

#include <chrono>
#include <functional>
#include <future>
#endif

#include <QStringList>

#include <cstring>
#include <exception>

namespace patchy::ui {
namespace {

constexpr double kMaxScriptAutomaticFeather = 1000.0;
// The same 30000-pixel cap the other selection calls use.
constexpr int kMaxScriptDimension = 30000;

int operation_index(const QString& operation, bool allow_intersect) {
  QStringList operations{QStringLiteral("replace"), QStringLiteral("add"), QStringLiteral("subtract")};
  if (allow_intersect) {
    operations << QStringLiteral("intersect");
  }
  const auto index = operations.indexOf(operation);
  if (index < 0) {
    script_vector::invalid(QStringLiteral("operation"));
  }
  return static_cast<int>(index);
}

QString automatic_selection_failure(const std::exception& error) {
  if (patchy::ai::is_out_of_memory(error)) {
    return automatic_selection_out_of_memory_message(ScriptEngineHost::tr("Automatic selection"));
  }
  return ScriptEngineHost::tr("Automatic selection failed: %1").arg(QString::fromUtf8(error.what()));
}

#if defined(Q_OS_WASM) && defined(__EMSCRIPTEN_PTHREADS__)
// The web engine must not run on the main thread (the page-side host it waits for runs
// there), so a script's inference runs on a worker while the call waits in an event loop,
// keeping the script watchdog fed. Desktop builds run it inline.
patchy::ai::AiMask wait_for_inference(std::future<patchy::ai::AiMask> future, const std::function<void()>& keep_alive) {
  const auto ready = [&future] { return future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready; };
  if (!ready()) {
    QEventLoop wait_loop;
    QTimer poll_timer;
    poll_timer.setInterval(50);
    QObject::connect(&poll_timer, &QTimer::timeout, &wait_loop, [&] {
      keep_alive();
      if (ready()) {
        wait_loop.quit();
      }
    });
    poll_timer.start();
    wait_loop.exec(QEventLoop::ExcludeUserInputEvents);
  }
  return future.get();
}
#endif

// selectSubject, selectBackground and removeBackground in a build without the subject model
// (held back pending patent review; docs/ai-selection.md).
QString subject_selection_unavailable_message() {
  return ScriptEngineHost::tr(
      "Select Subject is not included in this build of Patchy; check doc.selection.subjectSelectionAvailable.");
}

}  // namespace

QString ScriptEngineHost::automatic_select(std::int64_t session_id, const AutomaticSelectionRequest& request) {
  configure_automatic_selection_models();
  if (!patchy::ai::segmentation_available()) {
    return tr("Automatic selection is not available in this build (%1).")
        .arg(QString::fromStdString(patchy::ai::segmentation_unavailable_reason()));
  }
  const bool subject_model = request.kind == AutomaticSelectionRequest::Kind::Subject ||
                             request.kind == AutomaticSelectionRequest::Kind::Background;
  if (subject_model && !patchy::ai::subject_selection_available()) {
    return subject_selection_unavailable_message();
  }
  auto* canvas = session_canvas(session_id);
  const auto* document = session_document_const(session_id);
  if (canvas == nullptr || document == nullptr) {
    return tr("The document is no longer open.");
  }
  if (canvas->quick_mask_active()) {
    return tr("Exit Quick Mask before using automatic selection.");
  }
  const QSize size(document->width(), document->height());
  QImage source;
  if (request.sample_all_layers) {
    source = qimage_from_document(*document, true).convertToFormat(QImage::Format_RGBA8888);
  } else {
    const auto active = document->active_layer_id();
    if (!active.has_value()) {
      return tr("sampleAllLayers false needs an active layer.");
    }
    QString error;
    source = automatic_selection_layer_image(*document, *active, &error);
    if (source.isNull()) {
      return error;
    }
  }
  if (source.isNull()) {
    return tr("Automatic selection could not read the document.");
  }

  const auto box = request.box.intersected(QRect(QPoint(0, 0), size));
  const auto infer = [subject_model, is_box = request.kind == AutomaticSelectionRequest::Kind::Box, box,
                      point = request.point, pixels = pixels_from_image_rgba(source)] {
    if (subject_model) {
      return patchy::ai::subject_matte(pixels);
    }
    if (is_box) {
      // Zoom-to-box like the tool: the box's own region when sam_box_region gives one.
      const auto rect = Rect{box.x(), box.y(), box.width(), box.height()};
      const auto region = patchy::ai::sam_box_region(rect, pixels.width(), pixels.height());
      const auto embedding =
          region.has_value() ? patchy::ai::sam_encode_region(pixels, *region) : patchy::ai::sam_encode(pixels);
      return patchy::ai::sam_select_box(*embedding, rect);
    }
    return patchy::ai::sam_select_point(*patchy::ai::sam_encode(pixels), point.x(), point.y());
  };
  patchy::ai::AiMask mask;
  try {
#if defined(Q_OS_WASM) && defined(__EMSCRIPTEN_PTHREADS__)
    mask = wait_for_inference(launch_async(infer), [this] { keep_alive_for_ui(); });
#else
    mask = infer();
#endif
  } catch (const std::exception& error) {
    return automatic_selection_failure(error);
  }
#if defined(Q_OS_WASM) && defined(__EMSCRIPTEN_PTHREADS__)
  // The wait ran an event loop: the document may have closed or changed size meanwhile.
  if (const auto* current = session_document_const(session_id);
      current == nullptr || current->width() != size.width() || current->height() != size.height()) {
    return tr("The document changed during automatic selection.");
  }
#endif

  PixelBuffer coverage(size.width(), size.height(), PixelFormat::gray8());
  coverage.clear(0);
  for (int y = 0; y < mask.bounds.height; ++y) {
    std::memcpy(coverage.row(mask.bounds.y + y).data() + mask.bounds.x, std::as_const(mask.alpha).row(y).data(),
                static_cast<std::size_t>(mask.bounds.width));
  }
  if (request.kind == AutomaticSelectionRequest::Kind::Background) {
    // Everything the subject matte does not cover (plain inversion of the one result).
    for (int y = 0; y < coverage.height(); ++y) {
      for (auto& value : coverage.row(y)) {
        value = static_cast<std::uint8_t>(255U - value);
      }
    }
  }
  feather_selection_coverage(coverage, request.feather);
  if (!request.antialias) {
    for (int y = 0; y < coverage.height(); ++y) {
      for (auto& value : coverage.row(y)) {
        value = value >= 128U ? 255U : 0U;
      }
    }
  }
  if (canvas->has_selection()) {
    combine_selection_coverage(coverage, request.operation, selection_mask_pixels(*canvas, QRect(QPoint(0, 0), size)));
  }
  if (engine()->isInterrupted() || !prepare_mutation(session_id) || engine()->isInterrupted()) {
    return tr("The script was interrupted.");
  }
  canvas = session_canvas(session_id);
  if (canvas == nullptr) {
    return tr("The document is no longer open.");
  }
  canvas->apply_grayscale_to_selection(coverage);
  note_pixels_changed(session_id, {});
  return {};
}

QString ScriptEngineHost::remove_background(std::int64_t session_id, LayerId layer_id, bool sample_all_layers) {
  configure_automatic_selection_models();
  if (!patchy::ai::segmentation_available()) {
    return tr("Automatic selection is not available in this build (%1).")
        .arg(QString::fromStdString(patchy::ai::segmentation_unavailable_reason()));
  }
  if (!patchy::ai::subject_selection_available()) {
    return subject_selection_unavailable_message();
  }
  const auto* document = session_document_const(session_id);
  if (document == nullptr) {
    return tr("The document is no longer open.");
  }
  const auto* layer = document->find_layer(layer_id);
  if (layer == nullptr) {
    return tr("The layer no longer exists.");
  }
  if (layer->kind() != LayerKind::Pixel && layer->kind() != LayerKind::Adjustment && layer->kind() != LayerKind::Group) {
    return tr("removeBackground needs a pixel, adjustment, or group layer.");
  }
  QImage source;
  if (sample_all_layers) {
    source = qimage_from_document(*document, true).convertToFormat(QImage::Format_RGBA8888);
  } else {
    QString error;
    source = automatic_selection_layer_image(*document, layer_id, &error);
    if (source.isNull()) {
      return error;
    }
  }
  patchy::ai::AiMask matte;
  try {
    matte = patchy::ai::subject_matte(pixels_from_image_rgba(source));
  } catch (const std::exception& error) {
    return automatic_selection_failure(error);
  }
  if (matte.empty()) {
    return tr("removeBackground found no subject.");
  }
  if (engine()->isInterrupted() || !prepare_mutation(session_id) || engine()->isInterrupted()) {
    return tr("The script was interrupted.");
  }
  auto* writable_document = session_document(session_id);
  auto* writable_layer = writable_document != nullptr ? writable_document->find_layer(layer_id) : nullptr;
  if (writable_layer == nullptr) {
    return tr("The layer no longer exists.");
  }
  apply_subject_matte_to_layer_mask(*writable_document, *writable_layer, matte);
  note_structure_changed(session_id);
  return {};
}

void ScriptLayerObject::removeBackground(const QJSValue& options) {
  const ScriptApiCall api_call(host_);
  bool sample_all_layers = true;
  try {
    const auto args = script_vector::object(options, true);
    script_vector::keys(args, {QStringLiteral("sampleAllLayers")});
    sample_all_layers = script_vector::boolean(args, QStringLiteral("sampleAllLayers"), true);
  } catch (const std::exception& error) {
    host_.throw_js_error(ScriptEngineHost::tr("Invalid removeBackground option: %1.").arg(QString::fromUtf8(error.what())));
    return;
  }
  if (const auto error = host_.remove_background(session_id_, layer_id_, sample_all_layers); !error.isEmpty()) {
    host_.throw_js_error(error);
  }
}

bool ScriptSelectionObject::automatic_selection_available() const {
  const ScriptApiCall api_call(host_);
  configure_automatic_selection_models();
  return patchy::ai::segmentation_available();
}

bool ScriptSelectionObject::subject_selection_available() const {
  const ScriptApiCall api_call(host_);
  configure_automatic_selection_models();
  return patchy::ai::subject_selection_available();
}

QString ScriptSelectionObject::object_selection_model() const {
  const ScriptApiCall api_call(host_);
  configure_automatic_selection_models();
  return patchy::ai::active_sam_model() == patchy::ai::SamModel::HighQuality ? QStringLiteral("highQuality")
                                                                             : QStringLiteral("standard");
}

bool ScriptAppObject::downloadObjectSelectionModel() {
  const ScriptApiCall api_call(host_);
  QString error;
  // No dialog: progress goes to the status bar while the call waits, feeding the watchdog.
  if (host_.window().download_high_quality_object_selection_model(false, [this] { host_.keep_alive_for_ui(); },
                                                                   &error)) {
    return true;
  }
  host_.throw_js_error(error.isEmpty() ? ScriptEngineHost::tr("The download was cancelled.")
                                       : ScriptEngineHost::tr("The High Quality model could not be downloaded: %1")
                                             .arg(error));
  return false;
}

bool ScriptAppObject::downloadWatermarkModels() {
  const ScriptApiCall api_call(host_);
  QString error;
  // No dialog: progress goes to the status bar while the call waits, feeding the watchdog.
  if (host_.window().download_watermark_models(false, [this] { host_.keep_alive_for_ui(); }, &error)) {
    return true;
  }
  host_.throw_js_error(error.isEmpty() ? ScriptEngineHost::tr("The download was cancelled.")
                                       : ScriptEngineHost::tr("The watermark models could not be downloaded: %1")
                                             .arg(error));
  return false;
}

bool ScriptAppObject::watermark_models_available() const {
  const ScriptApiCall api_call(host_);
  configure_automatic_selection_models();
  return patchy::ai::trustmark_models_available();
}

void ScriptSelectionObject::selectSubject(const QJSValue& options) {
  run_subject_selection(options, false);
}

void ScriptSelectionObject::selectBackground(const QJSValue& options) {
  run_subject_selection(options, true);
}

void ScriptSelectionObject::run_subject_selection(const QJSValue& options, bool background) {
  const ScriptApiCall api_call(host_);
  ScriptEngineHost::AutomaticSelectionRequest request;
  try {
    const auto args = script_vector::object(options, true);
    script_vector::keys(args, {QStringLiteral("operation"), QStringLiteral("feather"), QStringLiteral("antialias"),
                               QStringLiteral("sampleAllLayers")});
    request.kind = background ? ScriptEngineHost::AutomaticSelectionRequest::Kind::Background
                              : ScriptEngineHost::AutomaticSelectionRequest::Kind::Subject;
    request.sample_all_layers = script_vector::boolean(args, QStringLiteral("sampleAllLayers"), true);
    request.operation = operation_index(script_vector::string(args, QStringLiteral("operation"), QStringLiteral("replace")), true);
    request.feather = script_vector::number(args, QStringLiteral("feather"), 0, 0, kMaxScriptAutomaticFeather);
    request.antialias = script_vector::boolean(args, QStringLiteral("antialias"), true);
  } catch (const std::exception& error) {
    const auto message = background ? ScriptEngineHost::tr("Invalid selectBackground option: %1.")
                                    : ScriptEngineHost::tr("Invalid selectSubject option: %1.");
    host_.throw_js_error(message.arg(QString::fromUtf8(error.what())));
    return;
  }
  if (const auto error = host_.automatic_select(session_id_, request); !error.isEmpty()) {
    host_.throw_js_error(error);
  }
}

void ScriptSelectionObject::objectSelect(const QJSValue& options) {
  const ScriptApiCall api_call(host_);
  ScriptEngineHost::AutomaticSelectionRequest request;
  try {
    const auto args = script_vector::object(options);
    script_vector::keys(args, {QStringLiteral("rect"), QStringLiteral("point"), QStringLiteral("operation"),
                               QStringLiteral("sampleAllLayers"), QStringLiteral("feather"),
                               QStringLiteral("antialias")});
    const bool has_rect = args.contains(QStringLiteral("rect"));
    const bool has_point = args.contains(QStringLiteral("point"));
    // One prompt per solve: a box or a point, never both (docs/ai-selection.md).
    if (has_rect == has_point) {
      script_vector::invalid(QStringLiteral("rect or point (exactly one)"));
    }
    if (has_rect) {
      const auto rect = script_vector::child_object(args, QStringLiteral("rect"));
      script_vector::keys(rect, {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("width"), QStringLiteral("height")});
      request.kind = ScriptEngineHost::AutomaticSelectionRequest::Kind::Box;
      request.box = QRect(script_vector::integer(rect, QStringLiteral("x"), 0, -kMaxScriptDimension, kMaxScriptDimension),
                          script_vector::integer(rect, QStringLiteral("y"), 0, -kMaxScriptDimension, kMaxScriptDimension),
                          script_vector::integer(rect, QStringLiteral("width"), 0, 1, kMaxScriptDimension),
                          script_vector::integer(rect, QStringLiteral("height"), 0, 1, kMaxScriptDimension));
    } else {
      const auto point = script_vector::child_object(args, QStringLiteral("point"));
      script_vector::keys(point, {QStringLiteral("x"), QStringLiteral("y")});
      request.kind = ScriptEngineHost::AutomaticSelectionRequest::Kind::Point;
      request.point = QPointF(script_vector::required_number(point, QStringLiteral("x"), 0, kMaxScriptDimension),
                              script_vector::required_number(point, QStringLiteral("y"), 0, kMaxScriptDimension));
    }
    request.operation =
        operation_index(script_vector::string(args, QStringLiteral("operation"), QStringLiteral("replace")), false);
    request.sample_all_layers = script_vector::boolean(args, QStringLiteral("sampleAllLayers"), true);
    request.feather = script_vector::number(args, QStringLiteral("feather"), 0, 0, kMaxScriptAutomaticFeather);
    request.antialias = script_vector::boolean(args, QStringLiteral("antialias"), true);
  } catch (const std::exception& error) {
    host_.throw_js_error(ScriptEngineHost::tr("Invalid objectSelect option: %1.").arg(QString::fromUtf8(error.what())));
    return;
  }
  if (const auto error = host_.automatic_select(session_id_, request); !error.isEmpty()) {
    host_.throw_js_error(error);
  }
}

}  // namespace patchy::ui
