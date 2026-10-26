// The Object Selection tool and the shared automatic-selection combine
// (docs/ai-selection.md). The drag shows only a plain rectangle outline; SAM solves once on
// release from one prompt, the dragged box or the clicked point, and the mask combines with
// the selection by plain boolean. Read the patent boundary in
// src/ai/segmentation_engine.hpp before changing how prompts or modes reach the network:
// no negative points, no box plus points in one solve, no previous mask fed back, and no
// hover preview (US 8050498 and Adobe's allowed hover-preview application).

#include "ui/canvas_widget.hpp"

#include "ai/ai_selection_math.hpp"
#include "ai/segmentation_engine.hpp"
#include "core/document.hpp"
#include "core/layer.hpp"
#include "core/layer_tree.hpp"
#include "ui/background_workers.hpp"
#include "ui/canvas_widget_shared.hpp"
#include "ui/image_document_io.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QString>
#include <Qt>
#include <QtGlobal>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <exception>
#include <future>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace patchy::ui {
namespace {

// A release within this many widget pixels of the press is a click (point prompt).
constexpr int kObjectSelectClickSlop = 4;

// A 64-bit signature of the sampled pixels (RGBA8888 rows are 4-byte aligned, so the
// whole buffer is the pixels): decides whether a stale-marked embedding still matches.
std::uint64_t source_signature(const QImage& image) {
  std::uint64_t hash = 0x9E3779B97F4A7C15ULL ^ (static_cast<std::uint64_t>(image.width()) << 32U) ^
                       static_cast<std::uint64_t>(image.height());
  const auto* bytes = image.constBits();
  const auto size = static_cast<std::size_t>(image.sizeInBytes());
  std::size_t index = 0;
  for (; index + 8U <= size; index += 8U) {
    std::uint64_t word = 0;
    std::memcpy(&word, bytes + index, 8U);
    hash = (hash ^ word) * 0x100000001B3ULL;
    hash ^= hash >> 29U;
  }
  for (; index < size; ++index) {
    hash = (hash ^ bytes[index]) * 0x100000001B3ULL;
  }
  return hash;
}

QImage grayscale_image_from_mask(const patchy::ai::AiMask& mask) {
  QImage image(mask.bounds.width, mask.bounds.height, QImage::Format_Grayscale8);
  for (int y = 0; y < mask.bounds.height; ++y) {
    std::memcpy(image.scanLine(y), mask.alpha.row(y).data(), static_cast<std::size_t>(mask.bounds.width));
  }
  return image;
}

void collect_tree_ids(const std::vector<Layer>& layers, std::vector<LayerId>& ids) {
  for (const auto& layer : layers) {
    ids.push_back(layer.id());
    collect_layer_descendant_ids(layer, ids);
  }
}

}  // namespace

QImage automatic_selection_layer_image(const Document& document, LayerId layer_id, QString* error) {
  const auto fail = [error](const char* message) {
    if (error != nullptr) {
      *error = QCoreApplication::translate("patchy::ui::CanvasWidget", message);
    }
    return QImage();
  };
  const auto* layer = find_layer_in_tree(document.layers(), layer_id);
  if (layer == nullptr || !layer->visible()) {
    return fail(QT_TRANSLATE_NOOP("patchy::ui::CanvasWidget",
                                  "Select a layer with visible content, or turn on Sample All Layers"));
  }
  std::vector<LayerId> keep{layer_id};
  if (!collect_layer_ancestor_groups(document.layers(), layer_id, keep)) {
    return fail(QT_TRANSLATE_NOOP("patchy::ui::CanvasWidget",
                                  "Select a layer with visible content, or turn on Sample All Layers"));
  }
  collect_layer_descendant_ids(*layer, keep);
  std::vector<LayerId> all;
  collect_tree_ids(document.layers(), all);
  std::vector<LayerId> hidden;
  for (const auto id : all) {
    if (std::find(keep.begin(), keep.end(), id) == keep.end()) {
      hidden.push_back(id);
    }
  }
  auto image = qimage_from_document_rect_with_hidden_layers(document, QRect(0, 0, document.width(), document.height()),
                                                            true, hidden)
                   .convertToFormat(QImage::Format_RGBA8888);
  bool any_visible = false;
  for (int y = 0; y < image.height() && !any_visible; ++y) {
    const auto* row = image.constScanLine(y);
    for (int x = 0; x < image.width(); ++x) {
      if (row[static_cast<std::size_t>(x) * 4U + 3U] != 0U) {
        any_visible = true;
        break;
      }
    }
  }
  if (!any_visible) {
    return fail(QT_TRANSLATE_NOOP("patchy::ui::CanvasWidget",
                                  "Select a layer with visible content, or turn on Sample All Layers"));
  }
  return image;
}

QString automatic_selection_out_of_memory_message(const QString& command) {
  return QCoreApplication::translate(
             "patchy::ui::CanvasWidget",
             "%1 ran out of memory. Close other programs or documents to free some memory, then try again.")
      .arg(command);
}

QImage CanvasWidget::automatic_selection_source(bool sample_all_layers, QString* error) {
  if (document_ == nullptr) {
    return {};
  }
  if (sample_all_layers) {
    wait_for_move_commit_job();  // exact pixels: a deferred Move commit may still be rendering
    ensure_render_cache();
    return render_cache_;
  }
  select_only_layer_if_none_active();
  const auto active = std::as_const(*document_).active_layer_id();
  if (!active.has_value()) {
    if (error != nullptr) {
      *error = tr("Select a layer with visible content, or turn on Sample All Layers");
    }
    return {};
  }
  return automatic_selection_layer_image(*document_, *active, error);
}

void CanvasWidget::set_object_select_sample_all_layers(bool enabled) noexcept {
  if (object_select_sample_all_layers_ != enabled) {
    object_select_sample_all_layers_ = enabled;
    invalidate_object_select_embedding();
  }
}

bool CanvasWidget::object_select_sample_all_layers() const noexcept {
  return object_select_sample_all_layers_;
}

int CanvasWidget::object_select_encodes_started() const noexcept {
  return object_select_encodes_started_;
}

void CanvasWidget::invalidate_object_select_embedding() noexcept {
  // The worker owns its promise; dropping our shared_future never blocks.
  object_select_embedding_key_.reset();
  object_select_embedding_ = {};
  object_select_encode_cancel_.reset();
  object_select_embedding_unverified_ = false;
}

void CanvasWidget::mark_object_select_embedding_unverified() noexcept {
  object_select_embedding_unverified_ = object_select_embedding_.valid();
}

void CanvasWidget::ensure_object_select_embedding() {
  if (document_ == nullptr || !patchy::ai::segmentation_available()) {
    return;
  }
  ObjectSelectEmbeddingKey key;
  key.sample_all_layers = object_select_sample_all_layers_;
  // An embedding only decodes with its own model's decoder: a model switch re-encodes.
  key.high_quality = patchy::ai::active_sam_model() == patchy::ai::SamModel::HighQuality;
  if (!object_select_sample_all_layers_) {
    select_only_layer_if_none_active();
    key.layer = std::as_const(*document_).active_layer_id().value_or(0);
  }
  const auto cached = [this, &key] {
    return object_select_embedding_key_ == key && object_select_embedding_.valid();
  };
  if (cached() && !object_select_embedding_unverified_) {
    return;
  }
  QString error;
  const auto source = automatic_selection_source(object_select_sample_all_layers_, &error);
  if (source.isNull()) {
    // Never fall back to an embedding of something else (a stale all-layers analysis).
    invalidate_object_select_embedding();
    object_select_source_error_ = error;
    return;
  }
  object_select_source_error_.clear();
  const auto signature = source_signature(source);
  if (cached() && signature == object_select_source_hash_) {
    object_select_embedding_unverified_ = false;
    return;
  }
  // The encoder sees no prompt and nothing is displayed from it; it only prepares the
  // embedding the release-time solve decodes.
  auto promise = std::make_shared<std::promise<std::shared_ptr<const patchy::ai::SamEmbedding>>>();
  auto cancel = std::make_shared<patchy::ai::InferenceCancel>();
  object_select_embedding_ = promise->get_future().share();
  object_select_encode_cancel_ = cancel;
  object_select_embedding_key_ = key;
  object_select_source_hash_ = signature;
  object_select_embedding_unverified_ = false;
  ++object_select_encodes_started_;
  object_select_encode_started_.start();
  object_select_encode_pending_ = true;
  run_tracked_background_worker([promise, cancel, source = std::move(source)] {
    try {
      promise->set_value(patchy::ai::sam_encode(pixels_from_image_rgba(source), cancel.get()));
    } catch (...) {
      promise->set_exception(std::current_exception());
    }
  });
}

void CanvasWidget::begin_object_select(QPoint widget_position, QPoint document_point,
                                       Qt::KeyboardModifiers modifiers) {
  if (document_ == nullptr) {
    return;
  }
  // A press on the pasteboard may still be a click that deselects (GitHub issue 429),
  // which needs no model, so its errors wait for a box drag to reach the release.
  const bool on_pasteboard = !QRect(0, 0, document_->width(), document_->height()).contains(document_point);
  if (!patchy::ai::segmentation_available()) {
    if (!on_pasteboard) {
      report_status_error(tr("Object Selection is not available in this build"));
      return;
    }
  } else {
    ensure_object_select_embedding();
  }
  if (!on_pasteboard && !object_select_embedding_.valid()) {
    report_status_error(object_select_source_error_.isEmpty() ? tr("Object Selection could not read the document")
                                                              : object_select_source_error_);
    return;
  }
  auto operation = selection_operation(modifiers);
  if (operation == SelectionMode::Intersect) {
    // Like Quick Select there is no Intersect mode; Shift+Alt acts as Add.
    operation = SelectionMode::Add;
  }
  selection_operation_ = operation;
  object_selecting_ = true;
  object_select_press_widget_ = widget_position;
  object_select_release_widget_ = widget_position;
  object_select_press_ = document_point;
  object_select_current_ = document_point;
}

void CanvasWidget::extend_object_select(QPoint document_point) {
  if (!object_selecting_) {
    return;
  }
  if (document_point != object_select_current_) {
    object_select_current_ = document_point;
    update();
  }
}

void CanvasWidget::cancel_object_select() {
  if (object_selecting_) {
    object_selecting_ = false;
    update();
  }
}

// Runs the one release-time solve and commits it as one undo entry.
void CanvasWidget::finish_object_select(QPoint widget_position) {
  if (!object_selecting_) {
    return;
  }
  object_select_release_widget_ = widget_position;
  object_selecting_ = false;
  if (document_ == nullptr) {
    update();
    return;
  }
  const auto document_rect = QRect(0, 0, document_->width(), document_->height());
  const bool is_click = (object_select_release_widget_ - object_select_press_widget_).manhattanLength() <=
                        kObjectSelectClickSlop;
  if (is_click && !document_rect.contains(object_select_press_)) {
    // A click on the pasteboard deselects in New mode, as a Marquee click does; Add and
    // Subtract have nothing to add or take away there (GitHub issue 429).
    if (selection_operation_ == SelectionMode::Replace) {
      const auto before = capture_selection_snapshot();
      clear_selection();
      record_selection_history(tr("Deselect"), before);
    }
    update();
    return;
  }
  if (!object_select_embedding_.valid()) {
    // A box drag begun on the pasteboard reports here what its press let pass.
    if (!document_rect.contains(object_select_press_)) {
      report_status_error(!patchy::ai::segmentation_available() ? tr("Object Selection is not available in this build")
                          : object_select_source_error_.isEmpty() ? tr("Object Selection could not read the document")
                                                                  : object_select_source_error_);
    }
    update();
    return;
  }
  const auto box = QRect(object_select_press_, object_select_current_).normalized().intersected(document_rect);
  if (!is_click && box.isEmpty()) {
    update();
    return;
  }

  solve_object_select(is_click, object_select_press_, box, selection_operation_, tr("Object Selection"));
}

// The one release-time (or command-time) decoder run: a click or a box, never both. A box
// with a zoom-to-box region (patchy::ai::sam_box_region: fixed geometry from the box alone)
// encodes that region first, once, and decodes the box once against it.
bool CanvasWidget::solve_object_select(bool is_click, QPoint point, QRect box, SelectionMode operation,
                                       QString history_label) {
  if (document_ == nullptr) {
    return false;
  }
  const auto region = is_click ? std::nullopt
                               : patchy::ai::sam_box_region(Rect{box.x(), box.y(), box.width(), box.height()},
                                                            document_->width(), document_->height());
  QImage region_source;
  if (region.has_value()) {
    QString error;
    region_source = automatic_selection_source(object_select_sample_all_layers_, &error);
    if (region_source.isNull()) {
      report_status_error(error.isEmpty() ? tr("Object Selection could not read the document") : error);
      update();
      return false;
    }
    // An unfinished whole-image encode would only compete with the region encode for the CPU.
    if (object_select_embedding_.valid() &&
        object_select_embedding_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
      if (object_select_encode_cancel_) {
        object_select_encode_cancel_->request();
      }
      invalidate_object_select_embedding();
    }
    object_select_encode_pending_ = false;
    ++object_select_encodes_started_;
  } else if (!object_select_embedding_.valid()) {
    update();
    return false;
  }
  auto embedding_future = object_select_embedding_;
  begin_processing_operation();
  // The decoder runs on a worker as well: the web engine must never run on the main thread
  // (the page-side host it waits for runs there), and the wait below keeps the overlay alive.
  QElapsedTimer region_started;
  region_started.start();
  auto solve = launch_async([embedding_future, is_click, point, box, region,
                             pixels = region.has_value() ? pixels_from_image_rgba(region_source) : PixelBuffer()] {
    const auto rect = Rect{box.x(), box.y(), box.width(), box.height()};
    if (region.has_value()) {
      return patchy::ai::sam_select_box(*patchy::ai::sam_encode_region(pixels, *region), rect);
    }
    const auto embedding = embedding_future.get();
    if (is_click) {
      return patchy::ai::sam_select_point(*embedding, point.x() + 0.5, point.y() + 0.5);
    }
    return patchy::ai::sam_select_box(*embedding, rect);
  });
  // The encoder reports no progress of its own; the percent is an estimate from the last
  // encode on this machine (whole image or region: the encoder frame is the same size),
  // held below 100 until the embedding is actually ready. The web build's first use
  // downloads the models first, which does report progress.
  const auto estimate_ms = std::max<qint64>(200, object_select_last_encode_ms_);
  const auto started = region.has_value() ? region_started : object_select_encode_started_;
  bool downloaded = false;
  bool region_pending = region.has_value();
  wait_for_processing_operation(
      [&] {
        if (object_select_encode_pending_ &&
            embedding_future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
          // A first use that downloaded the models says nothing about the encode itself.
          if (started.isValid() && !downloaded) {
            object_select_last_encode_ms_ = started.elapsed();
          }
          object_select_encode_pending_ = false;
        }
        if (solve.wait_for(std::chrono::milliseconds(16)) == std::future_status::ready) {
          if (region_pending && !downloaded) {
            object_select_last_encode_ms_ = started.elapsed();
          }
          return true;
        }
        if (const auto permille = patchy::ai::model_download_permille(); permille >= 0) {
          downloaded = true;
          set_processing_operation_message(
              tr("Downloading Object Selection model... %1%").arg(std::clamp(permille / 10, 0, 99)));
          return false;
        }
        if (object_select_encode_pending_ || region_pending) {
          const auto elapsed = started.isValid() ? started.elapsed() : 0;
          const auto percent = std::clamp<qint64>(elapsed * 100 / estimate_ms, 1, 95);
          set_processing_operation_message(tr("Analyzing image... %1%").arg(percent));
        }
        return false;
      },
      true);
  patchy::ai::AiMask mask;
  QString failure;
  try {
    mask = solve.get();
  } catch (const std::exception& error) {
    failure = patchy::ai::is_out_of_memory(error) ? automatic_selection_out_of_memory_message(tr("Object Selection"))
                                                  : tr("Object Selection failed: %1").arg(QString::fromUtf8(error.what()));
    invalidate_object_select_embedding();
  }
  end_processing_operation();

  if (!failure.isEmpty()) {
    report_status_error(failure);
    update();
    return false;
  }
  if (mask.empty()) {
    if (status_callback_) {
      status_callback_(tr("Object Selection found no object there"));
    }
    update();
    return false;
  }
  apply_automatic_selection_mask(QRect(mask.bounds.x, mask.bounds.y, mask.bounds.width, mask.bounds.height),
                                 grayscale_image_from_mask(mask), operation, std::move(history_label));
  return true;
}

bool CanvasWidget::select_object_in_selection() {
  if (document_ == nullptr || !has_selection()) {
    report_status_error(tr("Select Object needs a selection around the object"));
    return false;
  }
  if (!patchy::ai::segmentation_available()) {
    report_status_error(tr("Object Selection is not available in this build"));
    return false;
  }
  const auto box =
      selected_document_region().boundingRect().intersected(QRect(0, 0, document_->width(), document_->height()));
  if (box.isEmpty()) {
    return false;
  }
  // A box with a zoom-to-box region encodes that region itself; only a box covering most of
  // the image needs the whole-image embedding.
  if (!patchy::ai::sam_box_region(Rect{box.x(), box.y(), box.width(), box.height()}, document_->width(),
                                  document_->height())
           .has_value()) {
    ensure_object_select_embedding();
    if (!object_select_embedding_.valid()) {
      report_status_error(object_select_source_error_.isEmpty() ? tr("Object Selection could not read the document")
                                                                : object_select_source_error_);
      return false;
    }
  }
  // The selection's bounding box is the one box prompt; the object replaces the selection.
  return solve_object_select(false, QPoint(), box, SelectionMode::Replace, tr("Select Object"));
}

void CanvasWidget::apply_automatic_selection_mask(QRect bounds, QImage alpha, SelectionMode operation,
                                                  QString history_label) {
  if (document_ == nullptr || alpha.isNull() || bounds.isEmpty()) {
    return;
  }
  selection_edges_visible_ = true;
  capture_selection_before_edit();
  const auto previous_operation = selection_operation_;
  selection_operation_ = operation;
  if (!selection_antialias_) {
    for (int y = 0; y < alpha.height(); ++y) {
      auto* row = alpha.scanLine(y);
      for (int x = 0; x < alpha.width(); ++x) {
        row[x] = row[x] >= 128U ? 255U : 0U;
      }
    }
  }
  const auto document_rect = QRect(0, 0, document_->width(), document_->height());
  if (selection_feather_radius_ > 0) {
    const auto feather = selection_feather_radius_;
    const auto padding = feather_mask_padding(feather);
    const auto feather_bounds = bounds.adjusted(-padding, -padding, padding, padding).intersected(document_rect);
    QImage padded(feather_bounds.size(), QImage::Format_Grayscale8);
    padded.fill(0);
    QPainter painter(&padded);
    painter.drawImage(bounds.topLeft() - feather_bounds.topLeft(), alpha);
    painter.end();
    combine_selection_from_mask(feather_bounds, feather_blur_mask(std::move(padded), feather));
  } else {
    combine_selection_from_mask(bounds, std::move(alpha));
  }
  selection_operation_ = previous_operation;
  record_selection_history(std::move(history_label), selection_snapshot_before_edit());
  clear_selection_before_edit();
  refresh_info_display();
  update();
}

// The plain rectangle a box drag outlines. Only the user's input geometry is drawn; no
// segmentation runs or shows before release (see finish_object_select).
void CanvasWidget::draw_object_select_overlay(QPainter& painter) const {
  if (!object_selecting_ || object_select_press_ == object_select_current_) {
    return;
  }
  const auto rect = QRectF(widget_position_f(QPointF(object_select_press_)),
                           widget_position_f(QPointF(object_select_current_)))
                        .normalized();
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, false);
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(Qt::black, 1.0, Qt::SolidLine));
  painter.drawRect(rect);
  QPen dashes(Qt::white, 1.0, Qt::CustomDashLine);
  dashes.setDashPattern({4.0, 4.0});
  painter.setPen(dashes);
  painter.drawRect(rect);
  painter.restore();
}

}  // namespace patchy::ui
