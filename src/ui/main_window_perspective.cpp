// Filter > Perspective Planes...: the workspace's entry point and the one-undo-step write
// that the workspace and the patchy.perspective script calls share
// (docs/perspective-planes.md).

#include "core/perspective_planes.hpp"
#include "core/pixel_depth.hpp"
#include "core/smart_object.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/image_document_io.hpp"
#include "ui/main_window.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/perspective_planes_dialog.hpp"
#include "ui/qt_geometry.hpp"

#include <QApplication>
#include <QClipboard>
#include <QImage>
#include <QStatusBar>

#include <algorithm>
#include <cstring>
#include <functional>
#include <utility>

namespace patchy::ui {

namespace {

// Straight RGBA8 from an 8-bit gray, RGB or RGBA buffer.
PixelBuffer rgba8_from(const PixelBuffer& pixels) {
  if (pixels.format() == PixelFormat::rgba8()) {
    return pixels;
  }
  PixelBuffer result(pixels.width(), pixels.height(), PixelFormat::rgba8());
  const auto channels = pixels.format().channels;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto* source = pixels.pixel(x, y);
      auto* out = result.pixel(x, y);
      if (channels >= 3) {
        out[0] = source[0];
        out[1] = source[1];
        out[2] = source[2];
        out[3] = channels >= 4 ? source[3] : 255;
      } else {
        out[0] = out[1] = out[2] = source[0];
        out[3] = channels == 2 ? source[1] : 255;
      }
    }
  }
  return result;
}

}  // namespace

std::optional<MainWindow::PerspectiveLayerInput> MainWindow::perspective_layer_input(LayerId layer_id) const {
  const auto& doc = std::as_const(document());
  const auto* layer = doc.find_layer(layer_id);
  if (layer == nullptr || !editable_rgb_layer_any_depth(layer) || layer_is_smart_object(*layer)) {
    return std::nullopt;
  }
  const auto& pixels = std::as_const(*layer).pixels();
  if (pixels.format().bit_depth == BitDepth::Float32) {
    return std::nullopt;
  }
  PerspectiveLayerInput input;
  input.bounds = layer->bounds();
  const bool deep = pixels.format().bit_depth != BitDepth::UInt8;
  input.pixels = rgba8_from(deep ? convert_pixel_buffer_depth(pixels, BitDepth::UInt8, SampleKind::Color) : pixels);
  // The edits go onto the layer only where it can hold them unchanged: an 8-bit layer
  // with transparency, or an opaque one that covers the whole canvas.
  const Rect canvas{0, 0, doc.width(), doc.height()};
  const bool covers_canvas = input.bounds.x <= 0 && input.bounds.y <= 0 &&
                             input.bounds.x + input.bounds.width >= canvas.width &&
                             input.bounds.y + input.bounds.height >= canvas.height;
  input.can_write_onto_layer = !deep && (pixels.format().channels == 4 || covers_canvas);
  return input;
}

std::optional<LayerId> MainWindow::apply_perspective_edits(LayerId layer_id, std::pair<PixelBuffer, Rect> edits,
                                                           bool onto_layer, const QString& undo_label,
                                                           bool record_history) {
  auto& doc = document();
  const auto input = perspective_layer_input(layer_id);
  if (!input.has_value() || edits.first.empty() || edits.second.width <= 0 || edits.second.height <= 0) {
    return std::nullopt;
  }
  auto& [pixels, rect] = edits;
  // A selection limits the result like any pixel edit: coverage scales the edits' alpha.
  if (canvas_ != nullptr && !canvas_->selected_document_region().isEmpty()) {
    const auto coverage = selection_mask_pixels(*canvas_, to_qrect(rect));
    if (coverage.width() == pixels.width() && coverage.height() == pixels.height()) {
      for (std::int32_t y = 0; y < pixels.height(); ++y) {
        for (std::int32_t x = 0; x < pixels.width(); ++x) {
          auto* pixel = pixels.pixel(x, y);
          pixel[3] = static_cast<std::uint8_t>((pixel[3] * coverage.pixel(x, y)[0] + 127) / 255);
        }
      }
    }
  }
  bool any = false;
  for (std::int32_t y = 0; y < pixels.height() && !any; ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      if (pixels.pixel(x, y)[3] != 0) {
        any = true;
        break;
      }
    }
  }
  if (!any) {
    return std::nullopt;
  }
  const auto depth = std::as_const(*doc.find_layer(layer_id)).pixels().format().bit_depth;
  if (onto_layer && input->can_write_onto_layer) {
    Rect bounds{};
    auto merged = composite_edits_onto_layer(input->pixels, input->bounds, pixels, rect, bounds);
    const auto channels = std::as_const(*doc.find_layer(layer_id)).pixels().format().channels;
    if (channels == 3) {
      // An opaque layer covering the canvas stays opaque and keeps its bounds: the edits lie
      // inside the canvas, so the merge only lays them over it.
      const auto merged_bounds = bounds;
      bounds = input->bounds;
      PixelBuffer rgb(input->bounds.width, input->bounds.height, PixelFormat::rgb8());
      for (std::int32_t y = 0; y < rgb.height(); ++y) {
        for (std::int32_t x = 0; x < rgb.width(); ++x) {
          const auto* source = merged.pixel(x + input->bounds.x - merged_bounds.x, y + input->bounds.y - merged_bounds.y);
          auto* out = rgb.pixel(x, y);
          out[0] = source[0];
          out[1] = source[1];
          out[2] = source[2];
        }
      }
      merged = std::move(rgb);
    }
    if (record_history) {
      push_undo_snapshot(undo_label);
    }
    auto* layer = doc.find_layer(layer_id);
    if (layer == nullptr) {
      return std::nullopt;
    }
    const auto before = layer->bounds();
    set_layer_pixels_with_bounds(*layer, std::move(merged), bounds);
    canvas_->document_changed(to_qrect(bounds).united(to_qrect(before)));
    schedule_palette_compliance_check();
    return layer_id;
  }
  auto layer_pixels = depth == BitDepth::UInt8 ? std::move(pixels)
                                               : convert_pixel_buffer_depth(pixels, depth, SampleKind::Color);
  if (record_history) {
    push_undo_snapshot(undo_label);
  }
  Layer layer(doc.allocate_layer_id(), tr("Perspective").toStdString(), std::move(layer_pixels));
  layer.set_bounds(rect);
  const auto new_id = layer.id();
  insert_layer_after_anchor(doc, std::move(layer), layer_id);
  doc.set_active_layer(new_id);
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed(to_qrect(rect));
  schedule_palette_compliance_check();
  return new_id;
}

void MainWindow::perspective_planes_dialog() {
  if (!has_active_document()) {
    return;
  }
  if (canvas_ != nullptr && canvas_->quick_mask_active()) {
    show_status_error(tr("Perspective Planes is unavailable in Quick Mask mode"));
    return;
  }
  if (canvas_ != nullptr && (canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::DocumentChannel ||
                             CanvasWidget::is_component_preview(canvas_->layer_edit_target()))) {
    show_status_error(tr("Perspective Planes is unavailable while viewing a document channel"));
    return;
  }
  auto& doc = document();
  select_only_layer_if_none_active();
  const auto active = doc.active_layer_id();
  if (!active.has_value()) {
    return;
  }
  const auto* source_layer = std::as_const(doc).find_layer(*active);
  if (source_layer == nullptr || !editable_rgb_layer_any_depth(source_layer)) {
    show_status_error(tr("Select an editable RGB pixel layer"));
    return;
  }
  if (std::as_const(*source_layer).pixels().format().bit_depth == BitDepth::Float32) {
    show_status_error(tr("Perspective Planes works on 8-bit and 16-bit documents"));
    return;
  }
  if (layer_is_smart_object(*source_layer)) {
    show_status_error(tr("Rasterize the Smart Object before using Perspective Planes"));
    return;
  }
  if (layer_id_locks_image_pixels(*active)) {
    show_status_error(tr("Layer pixels are locked."));
    return;
  }
  if (!prompt_rasterize_procedural_layer(*active, tr("Perspective Planes"), false)) {
    return;
  }
  const auto input = perspective_layer_input(*active);
  if (!input.has_value()) {
    show_status_error(tr("Select an editable RGB pixel layer"));
    return;
  }

  PerspectiveWorkspaceInput workspace;
  workspace.layer = input->pixels;
  workspace.layer_bounds = input->bounds;
  workspace.document_width = doc.width();
  workspace.document_height = doc.height();
  workspace.can_write_onto_layer = input->can_write_onto_layer;
  workspace.write_onto_layer = perspective_write_onto_layer_ && input->can_write_onto_layer;
  workspace.foreground = canvas_ != nullptr ? canvas_->primary_color() : QColor(Qt::black);
  const auto session_id = active_session() != nullptr ? active_session()->session_id : 0;
  if (active_session() != nullptr) {
    workspace.planes = active_session()->perspective_planes;
  }
  // What Paste offers: the clipboard image, the selection on this layer, other layers.
  if (const auto* clipboard = QApplication::clipboard(); clipboard != nullptr) {
    const auto image = clipboard->image();
    if (!image.isNull()) {
      workspace.paste_sources.push_back({tr("Clipboard Image"), rgba8_from(pixels_from_image_rgba(image))});
    }
  }
  if (canvas_ != nullptr && !canvas_->selected_document_region().isEmpty()) {
    const auto area = canvas_->selected_document_region().boundingRect().intersected(to_qrect(input->bounds));
    if (!area.isEmpty()) {
      const auto coverage = selection_mask_pixels(*canvas_, area);
      PixelBuffer copy(area.width(), area.height(), PixelFormat::rgba8());
      for (int y = 0; y < area.height(); ++y) {
        for (int x = 0; x < area.width(); ++x) {
          const auto* source = input->pixels.pixel(area.x() + x - input->bounds.x, area.y() + y - input->bounds.y);
          auto* out = copy.pixel(x, y);
          std::memcpy(out, source, 4);
          if (coverage.width() == area.width() && coverage.height() == area.height()) {
            out[3] = static_cast<std::uint8_t>((out[3] * coverage.pixel(x, y)[0] + 127) / 255);
          }
        }
      }
      workspace.paste_sources.push_back({tr("Selection on This Layer"), std::move(copy)});
    }
  }
  int layer_sources = 0;
  const std::function<void(const std::vector<Layer>&)> add_layers = [&](const std::vector<Layer>& layers) {
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
      const auto& layer = *it;
      if (layer.kind() == LayerKind::Group) {
        add_layers(layer.children());
        continue;
      }
      if (layer.id() == *active || layer.kind() != LayerKind::Pixel || layer_is_smart_object(layer) ||
          layer_sources >= 12) {
        continue;
      }
      const auto& pixels = std::as_const(layer).pixels();
      if (pixels.empty() || pixels.format().bit_depth == BitDepth::Float32 || pixels.format().channels < 3) {
        continue;
      }
      const bool deep = pixels.format().bit_depth != BitDepth::UInt8;
      workspace.paste_sources.push_back(
          {tr("Layer: %1").arg(QString::fromStdString(layer.name())),
           rgba8_from(deep ? convert_pixel_buffer_depth(pixels, BitDepth::UInt8, SampleKind::Color) : pixels)});
      ++layer_sources;
    }
  };
  add_layers(std::as_const(doc).layers());

  auto result = run_perspective_planes_workspace(this, std::move(workspace));
  // The planes stay with the document for this session, whatever the outcome.
  if (auto* session = session_with_id(session_id); session != nullptr) {
    session->perspective_planes = result.planes;
  }
  if (!result.accepted) {
    statusBar()->showMessage(tr("Cancelled Perspective Planes"));
    return;
  }
  perspective_write_onto_layer_ = result.write_onto_layer;
  if (!result.edits.has_value() || active_session() == nullptr || active_session()->session_id != session_id) {
    statusBar()->showMessage(tr("Perspective Planes made no changes"));
    return;
  }
  const auto written =
      apply_perspective_edits(*active, std::move(*result.edits), result.write_onto_layer, tr("Perspective Planes"));
  statusBar()->showMessage(written.has_value() ? tr("Applied Perspective Planes")
                                               : tr("Perspective Planes made no changes"));
}

}  // namespace patchy::ui
