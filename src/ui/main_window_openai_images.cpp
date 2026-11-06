// Plugins > OpenAI Image Tools (docs/openai-images.md): the window side of the dialog. Results are
// plain pixel layers; nothing about the request is kept on them for regeneration
// (docs/legal-constraints.md, "Prompt-driven image generation").
#include "core/layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "core/pixel_buffer.hpp"
#include "core/pixel_depth.hpp"
#include "formats/document_flatten.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/edit_conversions.hpp"
#include "formats/gif_document_io.hpp"
#include "ui/image_document_io.hpp"
#include "ui/image_sequence_dialog.hpp"
#include "ui/main_window.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/openai_images.hpp"
#include "ui/openai_images_dialog.hpp"

#include <QPainter>
#include <QStatusBar>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <utility>

namespace patchy::ui {
namespace {

constexpr int kLayerNamePromptChars = 48;

QString layer_name_for_prompt(const QString& prompt, int index, int count) {
  auto words = prompt.simplified();
  if (words.size() > kLayerNamePromptChars) {
    words = words.left(kLayerNamePromptChars).trimmed() + QStringLiteral("...");
  }
  if (count > 1) {
    return MainWindow::tr("OpenAI %1: %2").arg(index + 1).arg(words);
  }
  return MainWindow::tr("OpenAI: %1").arg(words);
}

PixelBuffer pixels_at_depth(const QImage& image, BitDepth depth) {
  auto pixels = pixels_from_image_rgba(image.convertToFormat(QImage::Format_RGBA8888));
  if (pixels.format().bit_depth != depth) {
    pixels = convert_pixel_buffer_depth(pixels, depth, SampleKind::Color);
  }
  return pixels;
}

// The image as the layer's pixels: scaled to fit `canvas` when asked, centered on it.
std::pair<PixelBuffer, QPoint> placed_pixels(const QImage& image, QSize canvas, bool fit_to_canvas, BitDepth depth) {
  QPoint origin;
  const auto placed = openai::placed_image(image, canvas, fit_to_canvas, &origin);
  return {pixels_at_depth(placed, depth), origin};
}

}  // namespace

void MainWindow::open_openai_images_dialog() {
  if (openai_images_dialog_ != nullptr) {
    openai_images_dialog_->show();
    openai_images_dialog_->raise();
    openai_images_dialog_->activateWindow();
    return;
  }
  auto* dialog = new OpenAiImagesDialog(this, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  openai_images_dialog_ = dialog;
  run_non_modal_dialog(*dialog);
}

QSize MainWindow::openai_canvas_size() const {
  if (!has_active_document()) {
    return {};
  }
  const auto& doc = document();
  return {doc.width(), doc.height()};
}

std::int64_t MainWindow::openai_target_session_id() const {
  return has_active_document() ? session().session_id : -1;
}

QImage MainWindow::openai_reference_from_active_layer(QString* label, QString* error) const {
  const auto fail = [error](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    return QImage();
  };
  if (!has_active_document()) {
    return fail(tr("Open a document first."));
  }
  const auto& doc = document();
  const auto active_id = doc.active_layer_id();
  const auto* layer = active_id.has_value() ? doc.find_layer(*active_id) : nullptr;
  if (layer == nullptr) {
    return fail(tr("Select a layer first."));
  }
  const auto full = render_layer_isolated(doc, *layer);
  // Crop to the pixels the layer actually draws, so a small layer is not sent as a
  // mostly transparent canvas.
  int left = full.width();
  int top = full.height();
  int right = -1;
  int bottom = -1;
  const auto rgba = full.convertToFormat(QImage::Format_RGBA8888);
  for (int y = 0; y < rgba.height(); ++y) {
    const auto* row = rgba.constScanLine(y);
    for (int x = 0; x < rgba.width(); ++x) {
      if (row[x * 4 + 3] != 0) {
        left = std::min(left, x);
        right = std::max(right, x);
        top = std::min(top, y);
        bottom = std::max(bottom, y);
      }
    }
  }
  if (right < left || bottom < top) {
    return fail(tr("The active layer has no visible pixels."));
  }
  if (label != nullptr) {
    *label = QString::fromStdString(layer->name());
  }
  return rgba.copy(QRect(QPoint(left, top), QPoint(right, bottom)));
}

QImage MainWindow::openai_reference_from_visible_image(QString* error) const {
  if (!has_active_document()) {
    if (error != nullptr) {
      *error = tr("Open a document first.");
    }
    return {};
  }
  const auto& doc = document();
  const auto pixels = flatten_document_rgba8_rect(doc, Rect{0, 0, doc.width(), doc.height()});
  return display_qimage_from_pixel_buffer(pixels);
}

int MainWindow::add_openai_generated_layers(std::int64_t session_id, const std::vector<QImage>& images,
                                            const QString& prompt, bool fit_to_canvas, LayerId* added_top) {
  std::vector<QImage> usable;
  for (const auto& image : images) {
    if (!image.isNull()) {
      usable.push_back(image);
    }
  }
  if (usable.empty() || shutting_down_) {
    return 0;
  }
  const int count = static_cast<int>(usable.size());
  auto* target = session_id >= 0 ? session_with_id(session_id) : nullptr;
  if (target == nullptr) {
    // The document closed while OpenAI worked, or there was none: a new document the size
    // of the first image.
    const auto first_size = usable.front().size();
    Document new_document(first_size.width(), first_size.height(), PixelFormat::rgba8());
    new_document.print_settings().horizontal_ppi = kUntaggedImportPpi;
    new_document.print_settings().vertical_ppi = kUntaggedImportPpi;
    for (int index = 0; index < count; ++index) {
      auto [pixels, origin] = placed_pixels(usable[static_cast<std::size_t>(index)], first_size, fit_to_canvas,
                                            BitDepth::UInt8);
      const auto width = pixels.width();
      const auto height = pixels.height();
      auto& layer = new_document.add_pixel_layer(layer_name_for_prompt(prompt, index, count).toStdString(),
                                                 std::move(pixels));
      layer.set_bounds(Rect{origin.x(), origin.y(), width, height});
    }
    if (added_top != nullptr && !new_document.layers().empty()) {
      *added_top = new_document.layers().back().id();
    }
    add_document_session(std::move(new_document), tr("Untitled-%1").arg(sessions_.size() + 1), QString(),
                         tr("OpenAI Image Tools"));
    mark_session_modified(session());
    apply_opening_view(session());
    refresh_layer_list();
    refresh_layer_controls();
    update_undo_redo_actions();
    statusBar()->showMessage(tr("Added %n layer(s)", nullptr, count));
    return count;
  }
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return 0;
  }
  if (target != active_session()) {
    activate_document_session(*target);
  }
  canvas_->finish_free_transform();
  finish_active_text_editor();
  auto& doc = document();
  const QSize canvas(doc.width(), doc.height());
  const auto depth = std::as_const(doc).color_state().bit_depth;
  const auto selected = selected_layer_ids();
  std::optional<LayerId> anchor = selected.empty() ? std::nullopt : std::optional<LayerId>{selected.front()};
  push_undo_snapshot(tr("OpenAI Image Tools"));
  std::vector<LayerId> added_top_to_bottom;
  for (int index = 0; index < count; ++index) {
    auto [pixels, origin] = placed_pixels(usable[static_cast<std::size_t>(index)], canvas, fit_to_canvas, depth);
    const auto width = pixels.width();
    const auto height = pixels.height();
    Layer layer(doc.allocate_layer_id(), layer_name_for_prompt(prompt, index, count).toStdString(), std::move(pixels));
    layer.set_bounds(Rect{origin.x(), origin.y(), width, height});
    const auto id = layer.id();
    insert_layer_after_anchor(doc, std::move(layer), anchor);
    // The next image lands above this one, so they stack upward in order.
    anchor = id;
    added_top_to_bottom.insert(added_top_to_bottom.begin(), id);
  }
  doc.set_active_layer(added_top_to_bottom.front());
  if (added_top != nullptr) {
    *added_top = added_top_to_bottom.front();
  }
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  select_layers_in_layer_list(added_top_to_bottom, added_top_to_bottom.front());
  update_undo_redo_actions();
  statusBar()->showMessage(tr("Added %n layer(s)", nullptr, count));
  return count;
}

bool MainWindow::openai_masked_source(std::int64_t* session_id, QRect* region, QImage* context, QImage* coverage,
                                      QString* error, bool whole_when_none) const {
  const auto fail = [error](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };
  if (!has_active_document() || canvas_ == nullptr) {
    return fail(tr("Open a document first."));
  }
  const auto& doc = document();
  const auto bounds = canvas_->selected_document_rect();
  const bool selected = canvas_->has_selection() && bounds.has_value() && !bounds->isEmpty();
  if (!selected && !whole_when_none) {
    return fail(tr("Select an area first, with a marquee, lasso or Object Selection."));
  }
  // No selection (Edit): the whole canvas, fully covered (Seth, 2026-11-06).
  const QRect canvas_rect(0, 0, doc.width(), doc.height());
  // Edit (whole_when_none) sends a crop around the selection, Remove the wider context.
  const auto area = openai::context_region(selected ? *bounds : canvas_rect, canvas_rect.size(), whole_when_none);
  if (area.isEmpty()) {
    return fail(tr("The selection is outside the canvas."));
  }
  QImage cover_image(area.size(), QImage::Format_Grayscale8);
  if (selected) {
    const auto cover = canvas_->selection_coverage_buffer(area);
    for (int y = 0; y < area.height(); ++y) {
      std::memcpy(cover_image.scanLine(y), cover.row(y).data(), static_cast<std::size_t>(area.width()));
    }
  } else {
    cover_image.fill(255);
  }
  *session_id = session().session_id;
  *region = area;
  *context = display_qimage_from_pixel_buffer(
      flatten_document_rgba8_rect(doc, Rect{area.x(), area.y(), area.width(), area.height()}));
  *coverage = cover_image;
  return true;
}

int MainWindow::add_openai_masked_layers(std::int64_t session_id, const std::vector<QImage>& images, QRect region,
                                         const QImage& coverage, const QString& name, double feather,
                                         LayerId* added_top) {
  std::vector<QImage> usable;
  for (const auto& image : images) {
    if (!image.isNull() && image.size() == region.size()) {
      usable.push_back(image);
    }
  }
  auto* target = session_id >= 0 ? session_with_id(session_id) : nullptr;
  if (usable.empty() || shutting_down_ || target == nullptr || (!coverage.isNull() && coverage.size() != region.size())) {
    return 0;
  }
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return 0;
  }
  if (target != active_session()) {
    activate_document_session(*target);
  }
  canvas_->finish_free_transform();
  finish_active_text_editor();
  auto& doc = document();
  const auto depth = std::as_const(doc).color_state().bit_depth;
  const Rect canvas = Rect::from_size(doc.width(), doc.height());
  // A null coverage adds no mask: the image's own transparency is the shape.
  PixelBuffer mask_pixels;
  if (!coverage.isNull()) {
    mask_pixels = PixelBuffer(region.width(), region.height(), PixelFormat::gray8());
    const auto cover = coverage.convertToFormat(QImage::Format_Grayscale8);
    for (int y = 0; y < region.height(); ++y) {
      std::memcpy(mask_pixels.row(y).data(), cover.constScanLine(y), static_cast<std::size_t>(region.width()));
    }
    mask_pixels = coverage_at_document_depth(doc, std::move(mask_pixels));
  }
  const int count = static_cast<int>(usable.size());
  const auto selected = selected_layer_ids();
  std::optional<LayerId> anchor = selected.empty() ? std::nullopt : std::optional<LayerId>{selected.front()};
  push_undo_snapshot(tr("OpenAI Image Tools"));
  std::vector<LayerId> added_top_to_bottom;
  for (int index = 0; index < count; ++index) {
    auto pixels = pixels_from_image_rgba(usable[static_cast<std::size_t>(index)].convertToFormat(QImage::Format_RGBA8888));
    if (pixels.format().bit_depth != depth) {
      pixels = convert_pixel_buffer_depth(pixels, depth, SampleKind::Color);
    }
    const auto layer_name = count > 1 ? tr("%1 %2").arg(name).arg(index + 1) : name;
    Layer layer(doc.allocate_layer_id(), layer_name.toStdString(), std::move(pixels));
    layer.set_bounds(Rect{region.x(), region.y(), region.width(), region.height()});
    if (!coverage.isNull()) {
      // The selection saved when the request was sent as the layer's mask, feathered across the
      // seam band at render time (as Photoshop's mask Feather).
      LayerMask mask;
      mask.bounds = Rect{region.x(), region.y(), region.width(), region.height()};
      mask.pixels = mask_pixels;
      mask.default_color = 0;
      mask.feather = feather;
      layer.set_mask(std::move(mask));
      sync_layer_mask_feather_canvas(layer, canvas);
    }
    const auto id = layer.id();
    insert_layer_after_anchor(doc, std::move(layer), anchor);
    anchor = id;
    added_top_to_bottom.insert(added_top_to_bottom.begin(), id);
  }
  doc.set_active_layer(added_top_to_bottom.front());
  if (added_top != nullptr) {
    *added_top = added_top_to_bottom.front();
  }
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  select_layers_in_layer_list(added_top_to_bottom, added_top_to_bottom.front());
  update_undo_redo_actions();
  statusBar()->showMessage(tr("Added %n layer(s)", nullptr, count));
  return count;
}

bool MainWindow::openai_expand_source(std::int64_t* session_id, QSize* canvas, QImage* visible,
                                      QString* error) const {
  if (!has_active_document()) {
    if (error != nullptr) {
      *error = tr("Open a document first.");
    }
    return false;
  }
  const auto& doc = document();
  *session_id = session().session_id;
  *canvas = QSize(doc.width(), doc.height());
  *visible = display_qimage_from_pixel_buffer(flatten_document_rgba8_rect(doc, Rect{0, 0, doc.width(), doc.height()}));
  return true;
}

int MainWindow::add_openai_expanded_layers(std::int64_t session_id, const std::vector<QImage>& images,
                                           QSize canvas_before, QMargins margins, const QString& name,
                                           LayerId* added_top) {
  auto* target = session_id >= 0 ? session_with_id(session_id) : nullptr;
  if (target == nullptr || shutting_down_ ||
      QSize(std::as_const(target->document).width(), std::as_const(target->document).height()) != canvas_before) {
    return -1;
  }
  std::vector<QImage> usable;
  for (const auto& image : images) {
    if (!image.isNull()) {
      usable.push_back(image);
    }
  }
  if (usable.empty()) {
    return 0;
  }
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    return 0;
  }
  if (target != active_session()) {
    activate_document_session(*target);
  }
  canvas_->finish_free_transform();
  finish_active_text_editor();
  if (refuse_document_geometry_change()) {
    return 0;
  }
  const QSize grown(canvas_before.width() + margins.left() + margins.right(),
                    canvas_before.height() + margins.top() + margins.bottom());
  push_undo_snapshot(tr("OpenAI Image Tools"));
  // Canvas Size's own work, the old origin at (left, top); then the new layers over it.
  (void)resize_canvas_without_history(Rect{-margins.left(), -margins.top(), grown.width(), grown.height()},
                                      QColor(Qt::white), false, false);  // color-literal-ok: hidden fill
  auto& doc = document();
  const auto depth = std::as_const(doc).color_state().bit_depth;
  const int count = static_cast<int>(usable.size());
  const auto selected = selected_layer_ids();
  std::optional<LayerId> anchor = selected.empty() ? std::nullopt : std::optional<LayerId>{selected.front()};
  std::vector<LayerId> added_top_to_bottom;
  for (int index = 0; index < count; ++index) {
    auto image = usable[static_cast<std::size_t>(index)].convertToFormat(QImage::Format_RGBA8888);
    if (image.size() != grown) {
      image = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied)
                  .scaled(grown, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                  .convertToFormat(QImage::Format_RGBA8888);
    }
    auto pixels = pixels_from_image_rgba(image);
    if (pixels.format().bit_depth != depth) {
      pixels = convert_pixel_buffer_depth(pixels, depth, SampleKind::Color);
    }
    const auto layer_name = count > 1 ? tr("%1 %2").arg(name).arg(index + 1) : name;
    // The image arrives transparent over the original except a crossfade band at the seam
    // (openai::expand_layer_image), so the original pixels show through unchanged; no mask.
    Layer layer(doc.allocate_layer_id(), layer_name.toStdString(), std::move(pixels));
    layer.set_bounds(Rect{0, 0, grown.width(), grown.height()});
    const auto id = layer.id();
    insert_layer_after_anchor(doc, std::move(layer), anchor);
    anchor = id;
    added_top_to_bottom.insert(added_top_to_bottom.begin(), id);
  }
  doc.set_active_layer(added_top_to_bottom.front());
  if (added_top != nullptr) {
    *added_top = added_top_to_bottom.front();
  }
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  select_layers_in_layer_list(added_top_to_bottom, added_top_to_bottom.front());
  update_undo_redo_actions();
  statusBar()->showMessage(tr("Canvas %1 x %2").arg(grown.width()).arg(grown.height()));
  return count;
}

int MainWindow::add_openai_animation_documents(const std::vector<QByteArray>& gifs) {
  std::vector<std::vector<openai::GifFrame>> animations;
  for (const auto& gif : gifs) {
    QString error;
    auto frames = openai::gif_frames(gif, &error);
    if (!frames.empty()) {
      animations.push_back(std::move(frames));
    }
  }
  return add_openai_animation_frame_documents(std::move(animations));
}

int MainWindow::add_openai_animation_frame_documents(std::vector<std::vector<openai::GifFrame>> animations) {
  if (shutting_down_) {
    return 0;
  }
  int added = 0;
  for (auto& frames : animations) {
    if (frames.empty()) {
      continue;
    }
    std::vector<QImage> images;
    QStringList names;
    for (std::size_t index = 0; index < frames.size(); ++index) {
      const auto delay_cs = static_cast<std::uint16_t>(
          std::clamp<long long>(std::llround(frames[index].delay_ms / 10.0), 0, 0xffff));
      names.push_back(QObject::tr("Frame %1").arg(static_cast<int>(index) + 1) + QLatin1Char(' ') +
                      QString::fromStdString(gif::format_delay_seconds_token(delay_cs)));
      images.push_back(std::move(frames[index].image));
    }
    auto document = document_from_frames(std::move(images), names, {.first_frame_on_top = true, .all_layers_visible = true});
    if (!document.has_value()) {
      continue;
    }
    document->print_settings().horizontal_ppi = kUntaggedImportPpi;
    document->print_settings().vertical_ppi = kUntaggedImportPpi;
    add_document_session(std::move(*document), tr("OpenAI Animation"), QString(), tr("OpenAI Image Tools"));
    mark_session_modified(session());
    apply_opening_view(session());
    ++added;
  }
  if (added > 0) {
    refresh_layer_list();
    refresh_layer_controls();
    update_undo_redo_actions();
    statusBar()->showMessage(tr("Opened %n animation(s); export one with File > Export > Layers as Animated GIF.",
                                nullptr, added));
  }
  return added;
}

std::optional<std::uint64_t> MainWindow::openai_layer_revision(std::int64_t session_id, LayerId layer_id) const {
  auto* target = session_id >= 0 ? const_cast<MainWindow*>(this)->session_with_id(session_id) : nullptr;
  if (target == nullptr) {
    return std::nullopt;
  }
  const auto* layer = std::as_const(target->document).find_layer(layer_id);
  if (layer == nullptr) {
    return std::nullopt;
  }
  return layer->content_revision();
}

bool MainWindow::openai_replace_layer_pixels(std::int64_t session_id, LayerId layer_id, std::uint64_t expected_revision,
                                             const QImage& image, std::uint64_t* revision) {
  if (openai_layer_revision(session_id, layer_id) != expected_revision || shutting_down_ || preview_dialog_edit_locked()) {
    return false;
  }
  auto* target = session_with_id(session_id);
  if (target != active_session()) {
    activate_document_session(*target);
  }
  auto& doc = document();
  auto* layer = doc.find_layer(layer_id);
  if (layer == nullptr || std::as_const(*layer).kind() != LayerKind::Pixel) {
    return false;
  }
  const auto bounds = std::as_const(*layer).bounds();
  if (image.width() != bounds.width || image.height() != bounds.height) {
    return false;
  }
  // A variation swap: the pixels of the layer this dialog made, same place, same mask. It adds
  // no history step of its own: Undo removes the layer the request added.
  // set_pixels puts the layer at (0, 0): put it back where it was.
  layer->set_pixels(pixels_at_depth(image, std::as_const(doc).color_state().bit_depth));
  layer->set_bounds(bounds);
  mark_session_modified(session());
  canvas_->document_changed();
  refresh_layer_controls();
  if (revision != nullptr) {
    *revision = std::as_const(*layer).content_revision();
  }
  return true;
}

bool MainWindow::openai_set_layer_mask(std::int64_t session_id, LayerId layer_id, std::uint64_t expected_revision,
                                       QRect region, const QImage& coverage, double feather, std::uint64_t* revision) {
  if (openai_layer_revision(session_id, layer_id) != expected_revision || shutting_down_ || preview_dialog_edit_locked() ||
      coverage.size() != region.size()) {
    return false;
  }
  auto* target = session_with_id(session_id);
  if (target != active_session()) {
    activate_document_session(*target);
  }
  auto& doc = document();
  auto* layer = doc.find_layer(layer_id);
  if (layer == nullptr) {
    return false;
  }
  PixelBuffer pixels(region.width(), region.height(), PixelFormat::gray8());
  const auto cover = coverage.convertToFormat(QImage::Format_Grayscale8);
  for (int y = 0; y < region.height(); ++y) {
    std::memcpy(pixels.row(y).data(), cover.constScanLine(y), static_cast<std::size_t>(region.width()));
  }
  LayerMask mask;
  mask.bounds = Rect{region.x(), region.y(), region.width(), region.height()};
  mask.pixels = coverage_at_document_depth(doc, std::move(pixels));
  mask.default_color = 0;
  mask.feather = feather;
  layer->set_mask(std::move(mask));
  sync_layer_mask_feather_canvas(*layer, Rect::from_size(doc.width(), doc.height()));
  mark_session_modified(session());
  canvas_->document_changed();
  refresh_layer_controls();
  if (revision != nullptr) {
    *revision = std::as_const(*layer).content_revision();
  }
  return true;
}

bool MainWindow::openai_active_layer_region(QRect region, std::int64_t* session_id, LayerId* layer_id,
                                            std::uint64_t* revision, QImage* pixels, QString* error) const {
  const auto fail = [error](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };
  if (!has_active_document()) {
    return fail(tr("Open a document first."));
  }
  const auto& doc = document();
  if (doc.color_state().bit_depth != BitDepth::UInt8) {
    return fail(tr("Writing into the active layer works in 8-bit documents; choose New layer as the target."));
  }
  const auto active = doc.active_layer_id();
  const auto* layer = active.has_value() ? doc.find_layer(*active) : nullptr;
  if (layer == nullptr || layer->kind() != LayerKind::Pixel) {
    return fail(tr("The active layer is not a pixel layer; choose New layer as the target."));
  }
  if (layer_effectively_locks_image_pixels(doc.layers(), layer->id())) {
    return fail(tr("The active layer's pixels are locked; choose New layer as the target."));
  }
  // The layer's pixels over the region, transparent where the layer has none.
  QImage over(region.size(), QImage::Format_RGBA8888);
  over.fill(Qt::transparent);
  const auto bounds = layer->bounds();
  const auto layer_image = display_qimage_from_pixel_buffer(layer->pixels());
  {
    QPainter painter(&over);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(QPoint(bounds.x - region.x(), bounds.y - region.y()), layer_image);
  }
  *session_id = session().session_id;
  *layer_id = layer->id();
  *revision = layer->content_revision();
  *pixels = over;
  return true;
}

bool MainWindow::openai_write_into_layer(std::int64_t session_id, LayerId layer_id, std::uint64_t expected_revision,
                                         QRect region, const QImage& composed, bool push_undo, std::uint64_t* revision) {
  if (openai_layer_revision(session_id, layer_id) != expected_revision || shutting_down_ || preview_dialog_edit_locked() ||
      composed.size() != region.size()) {
    return false;
  }
  auto* target = session_with_id(session_id);
  if (target != active_session()) {
    activate_document_session(*target);
  }
  canvas_->finish_free_transform();
  finish_active_text_editor();
  auto& doc = document();
  auto* layer = doc.find_layer(layer_id);
  if (layer == nullptr || std::as_const(*layer).kind() != LayerKind::Pixel ||
      std::as_const(doc).color_state().bit_depth != BitDepth::UInt8) {
    return false;
  }
  const auto bounds = std::as_const(*layer).bounds();
  const auto old_format = std::as_const(*layer).pixels().format();
  const QRect old_rect(bounds.x, bounds.y, bounds.width, bounds.height);
  const auto united = old_rect.united(region);
  // An opaque layer (a Background) stays opaque and keeps its size: the region lies on the
  // canvas it covers. Anything else grows to cover the region.
  const bool opaque = old_format.channels == 3;
  if (opaque && united != old_rect) {
    return false;
  }
  QImage whole(united.size(), QImage::Format_RGBA8888);
  whole.fill(Qt::transparent);
  {
    QPainter painter(&whole);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(old_rect.topLeft() - united.topLeft(),
                      display_qimage_from_pixel_buffer(std::as_const(*layer).pixels()));
    painter.drawImage(region.topLeft() - united.topLeft(), composed.convertToFormat(QImage::Format_RGBA8888));
  }
  if (push_undo) {
    push_undo_snapshot(tr("OpenAI Image Tools"));
    layer = doc.find_layer(layer_id);
  }
  if (opaque) {
    PixelBuffer rgb(united.width(), united.height(), PixelFormat::rgb8());
    for (int y = 0; y < united.height(); ++y) {
      const auto* source = whole.constScanLine(y);
      auto row = rgb.row(y);
      for (int x = 0; x < united.width(); ++x) {
        row[static_cast<std::size_t>(x) * 3 + 0] = source[x * 4 + 0];
        row[static_cast<std::size_t>(x) * 3 + 1] = source[x * 4 + 1];
        row[static_cast<std::size_t>(x) * 3 + 2] = source[x * 4 + 2];
      }
    }
    layer->set_pixels(std::move(rgb));
    layer->set_bounds(bounds);
  } else {
    layer->set_pixels(pixels_from_image_rgba(whole));
    layer->set_bounds(Rect{united.x(), united.y(), united.width(), united.height()});
  }
  mark_session_modified(session());
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  update_undo_redo_actions();
  if (revision != nullptr) {
    *revision = std::as_const(*layer).content_revision();
  }
  return true;
}

}  // namespace patchy::ui
