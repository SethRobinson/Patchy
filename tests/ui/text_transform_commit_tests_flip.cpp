// Part 3 of the text_transform_commit UI test group: flipped (negative-scale)
// free transforms and the transform/raster consistency chain across re-edits,
// nudges, and PSD export. Repro for the August 2026 report: flipping a text
// layer looked unflipped until the next edit, and an edited layer's saved TySh
// transform pointed somewhere entirely different from its raster.

#include "core/layer_metadata.hpp"
#include "core/pixel_tools.hpp"
#include "psd/psd_document_io.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"

#include <QAction>
#include <QApplication>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QString>
#include <QTextCursor>
#include <QTextEdit>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

using namespace patchy::test::ui;

namespace {

QImage visible_alpha_cropped_image(const patchy::PixelBuffer& pixels) {
  const auto image = image_from_pixels_for_visuals(pixels).convertToFormat(QImage::Format_ARGB32);
  const auto bounds = alpha_pixel_bounds_in_rows(pixels, 0, pixels.height());
  if (!bounds.has_value()) {
    return image;
  }
  return image.copy(*bounds);
}

// Alpha intersection-over-union between the reference image and the candidate scaled onto the
// reference's size. High (~0.7+) when the two hold the same glyphs in the same orientation, low
// when one is mirrored relative to the other ("Flip me." has no mirror symmetry).
double alpha_overlap_score(const QImage& reference, const QImage& candidate) {
  if (reference.isNull() || candidate.isNull() || reference.width() < 2 || reference.height() < 2) {
    return 0.0;
  }
  const auto scaled = candidate.scaled(reference.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                          .convertToFormat(QImage::Format_ARGB32);
  std::int64_t intersection = 0;
  std::int64_t union_sum = 0;
  for (int y = 0; y < reference.height(); ++y) {
    for (int x = 0; x < reference.width(); ++x) {
      const int reference_alpha = qAlpha(reference.pixel(x, y));
      const int candidate_alpha = qAlpha(scaled.pixel(x, y));
      intersection += std::min(reference_alpha, candidate_alpha);
      union_sum += std::max(reference_alpha, candidate_alpha);
    }
  }
  return union_sum <= 0 ? 0.0 : static_cast<double>(intersection) / static_cast<double>(union_sum);
}

// IoU between the reference render's alpha ink and the DARK pixels of a canvas grab region
// (glyphs are near-black over the light canvas/checkerboard). Used to judge orientation of the
// live free-transform drag preview, which only exists on screen.
double grab_ink_overlap_score(const QImage& grab_region, const QImage& reference) {
  if (grab_region.isNull() || reference.isNull() || grab_region.width() < 2 || grab_region.height() < 2) {
    return 0.0;
  }
  const auto scaled_reference =
      reference.scaled(grab_region.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
          .convertToFormat(QImage::Format_ARGB32);
  const auto grab = grab_region.convertToFormat(QImage::Format_ARGB32);
  std::int64_t intersection = 0;
  std::int64_t union_sum = 0;
  for (int y = 0; y < grab.height(); ++y) {
    for (int x = 0; x < grab.width(); ++x) {
      const bool grab_ink = qGray(grab.pixel(x, y)) < 144;
      const bool reference_ink = qAlpha(scaled_reference.pixel(x, y)) > 128;
      intersection += (grab_ink && reference_ink) ? 1 : 0;
      union_sum += (grab_ink || reference_ink) ? 1 : 0;
    }
  }
  return union_sum <= 0 ? 0.0 : static_cast<double>(intersection) / static_cast<double>(union_sum);
}

std::optional<patchy::LayerAffineTransform> stored_text_transform(const patchy::Layer& layer) {
  const auto found = layer.metadata().find(patchy::kLayerMetadataTextTransform);
  if (found == layer.metadata().end()) {
    return std::nullopt;
  }
  return patchy::parse_layer_affine_transform(found->second);
}

std::optional<QRectF> document_ink_rect(const patchy::Layer& layer) {
  const auto bounds = alpha_pixel_bounds_in_rows(layer.pixels(), 0, layer.pixels().height());
  if (!bounds.has_value()) {
    return std::nullopt;
  }
  return QRectF(static_cast<qreal>(layer.bounds().x + bounds->x()),
                static_cast<qreal>(layer.bounds().y + bounds->y()), static_cast<qreal>(bounds->width()),
                static_cast<qreal>(bounds->height()));
}

void print_transform_state(const char* label, const patchy::Layer& layer) {
  const auto transform = stored_text_transform(layer);
  const auto ink = document_ink_rect(layer);
  std::cout << "[text-transform] " << label << ": bounds=(" << layer.bounds().x << "," << layer.bounds().y << " "
            << layer.bounds().width << "x" << layer.bounds().height << ")";
  if (ink.has_value()) {
    std::cout << " ink=(" << ink->left() << "," << ink->top() << " " << ink->width() << "x" << ink->height()
              << ")";
  }
  if (transform.has_value()) {
    std::cout << " transform=[" << (*transform)[0] << " " << (*transform)[1] << " " << (*transform)[2] << " "
              << (*transform)[3] << " " << (*transform)[4] << " " << (*transform)[5] << "]";
  } else {
    std::cout << " transform=none";
  }
  std::cout << std::endl;
}

patchy::LayerId create_point_text(patchy::ui::MainWindow& window, patchy::ui::CanvasWidget& canvas,
                                  QPoint document_point, const QString& text) {
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto create_widget = canvas.widget_position_for_document_point(document_point);
  send_mouse(canvas, QEvent::MouseButtonPress, create_widget, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, create_widget, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  auto* editor = canvas.findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  if (editor != nullptr) {
    editor->setPlainText(text);
  }
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  const auto layer_id = patchy::ui::MainWindowTestAccess::document(window).active_layer_id();
  CHECK(layer_id.has_value());
  return layer_id.value_or(0);
}

void apply_free_transform(patchy::ui::MainWindow& window, patchy::ui::CanvasWidget& canvas,
                          double scale_x_percent, double scale_y_percent, double angle_degrees) {
  canvas.set_show_transform_controls(true);
  QApplication::processEvents();
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas.free_transform_active());
  const auto state = canvas.transform_controls_state();
  CHECK(state.has_value());
  if (state.has_value()) {
    CHECK(canvas.set_transform_controls_state(state->reference_position, scale_x_percent, scale_y_percent,
                                              angle_degrees));
  }
  QApplication::processEvents();
  send_key(canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(!canvas.free_transform_active());
}

// Opens an edit session on the active text layer at `click_document_point` and applies it without
// changing the text (applying keeps what the session showed; the commit re-renders through the
// stored transform, which is exactly the path under test).
void reedit_and_apply(patchy::ui::MainWindow& window, patchy::ui::CanvasWidget& canvas,
                      QPoint click_document_point) {
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto click_widget = canvas.widget_position_for_document_point(click_document_point);
  send_mouse(canvas, QEvent::MouseButtonPress, click_widget, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, click_widget, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  process_events_for(300);
  CHECK(canvas.findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) != nullptr);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  process_events_for(120);
  CHECK(canvas.findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
}

void ui_point_text_flip_transform_mirrors_and_survives_reedit() {
  // Flipping a text layer with the free transform must mirror the committed glyphs immediately
  // (as Photoshop does), keep them mirrored across a re-edit, and flip back cleanly.
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();

  const auto layer_id = create_point_text(window, *canvas, QPoint(420, 170), QStringLiteral("Flip me."));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* base_layer = document.find_layer(layer_id);
  CHECK(base_layer != nullptr);
  if (base_layer == nullptr) {
    return;
  }
  const auto base_image = visible_alpha_cropped_image(base_layer->pixels());
  const auto base_mirrored = base_image.mirrored(true, false);
  const auto base_ink = document_ink_rect(*base_layer);
  CHECK(base_ink.has_value());
  print_transform_state("base", *base_layer);

  // Flip X about the center.
  apply_free_transform(window, *canvas, -100.0, 100.0, 0.0);
  const auto* flipped_layer = document.find_layer(layer_id);
  CHECK(flipped_layer != nullptr);
  if (flipped_layer == nullptr) {
    return;
  }
  print_transform_state("after flip", *flipped_layer);
  const auto flipped_transform = stored_text_transform(*flipped_layer);
  CHECK(flipped_transform.has_value());
  if (flipped_transform.has_value()) {
    const auto determinant =
        (*flipped_transform)[0] * (*flipped_transform)[3] - (*flipped_transform)[1] * (*flipped_transform)[2];
    CHECK(determinant < 0.0);
  }
  const auto flipped_image = visible_alpha_cropped_image(flipped_layer->pixels());
  const auto flip_identity_score = alpha_overlap_score(flipped_image, base_image);
  const auto flip_mirror_score = alpha_overlap_score(flipped_image, base_mirrored);
  std::cout << "[text-transform] after flip: identity=" << flip_identity_score
            << " mirror=" << flip_mirror_score << std::endl;
  CHECK(flip_mirror_score > flip_identity_score + 0.1);
  const auto flipped_ink = document_ink_rect(*flipped_layer);
  CHECK(flipped_ink.has_value());
  if (base_ink.has_value() && flipped_ink.has_value()) {
    // Mirroring about the layer center must not walk the text across the document.
    CHECK(std::abs(flipped_ink->center().x() - base_ink->center().x()) <= 12.0);
    CHECK(std::abs(flipped_ink->center().y() - base_ink->center().y()) <= 12.0);
  }

  // Re-edit and apply without changing the text: the glyphs must stay mirrored and stay put.
  if (flipped_ink.has_value()) {
    reedit_and_apply(window, *canvas, flipped_ink->center().toPoint());
  }
  const auto* reedited_layer = document.find_layer(layer_id);
  CHECK(reedited_layer != nullptr);
  if (reedited_layer == nullptr) {
    return;
  }
  print_transform_state("after flip+reedit", *reedited_layer);
  const auto reedited_image = visible_alpha_cropped_image(reedited_layer->pixels());
  const auto reedit_identity_score = alpha_overlap_score(reedited_image, base_image);
  const auto reedit_mirror_score = alpha_overlap_score(reedited_image, base_mirrored);
  std::cout << "[text-transform] after flip+reedit: identity=" << reedit_identity_score
            << " mirror=" << reedit_mirror_score << std::endl;
  CHECK(reedit_mirror_score > reedit_identity_score + 0.1);
  const auto reedited_ink = document_ink_rect(*reedited_layer);
  CHECK(reedited_ink.has_value());
  if (flipped_ink.has_value() && reedited_ink.has_value()) {
    CHECK(std::abs(reedited_ink->center().x() - flipped_ink->center().x()) <= 8.0);
    CHECK(std::abs(reedited_ink->center().y() - flipped_ink->center().y()) <= 8.0);
  }

  // Flip X again: the text must come back upright, in place ("it can be fixed").
  apply_free_transform(window, *canvas, -100.0, 100.0, 0.0);
  const auto* restored_layer = document.find_layer(layer_id);
  CHECK(restored_layer != nullptr);
  if (restored_layer == nullptr) {
    return;
  }
  print_transform_state("after flip back", *restored_layer);
  const auto restored_image = visible_alpha_cropped_image(restored_layer->pixels());
  const auto restored_identity_score = alpha_overlap_score(restored_image, base_image);
  const auto restored_mirror_score = alpha_overlap_score(restored_image, base_mirrored);
  std::cout << "[text-transform] after flip back: identity=" << restored_identity_score
            << " mirror=" << restored_mirror_score << std::endl;
  CHECK(restored_identity_score > restored_mirror_score + 0.1);
  const auto restored_transform = stored_text_transform(*restored_layer);
  CHECK(restored_transform.has_value());
  if (restored_transform.has_value()) {
    const auto determinant =
        (*restored_transform)[0] * (*restored_transform)[3] - (*restored_transform)[1] * (*restored_transform)[2];
    CHECK(determinant > 0.0);
  }

  // Drag a corner handle across its anchor and inspect the LIVE preview mid-drag: it must show
  // the mirror (the plain source blit used to drop the scale signs, so the whole drag showed
  // unmirrored text and the flip only appeared at commit). Escape cancels the session after.
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  const auto* drag_layer = document.find_layer(layer_id);
  CHECK(drag_layer != nullptr);
  if (drag_layer == nullptr) {
    return;
  }
  const auto drag_bounds = drag_layer->bounds();
  const auto handle_widget = canvas->widget_position_for_document_point(
      QPoint(drag_bounds.x + drag_bounds.width, drag_bounds.y + drag_bounds.height));
  const auto crossed_widget = canvas->widget_position_for_document_point(
      QPoint(drag_bounds.x - drag_bounds.width, drag_bounds.y + drag_bounds.height));
  send_mouse(*canvas, QEvent::MouseButtonPress, handle_widget, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, (handle_widget + crossed_widget) / 2, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, crossed_widget, Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  const auto drag_grab = canvas->grab().toImage();
  const auto preview_top_left = canvas->widget_position_for_document_point(
      QPoint(drag_bounds.x - drag_bounds.width, drag_bounds.y));
  const QRect preview_region(preview_top_left.x() + 2, preview_top_left.y() + 2,
                             drag_bounds.width - 4, drag_bounds.height - 4);
  const auto preview_image = drag_grab.copy(preview_region.intersected(drag_grab.rect()));
  const auto drag_identity_score = grab_ink_overlap_score(preview_image, restored_image);
  const auto drag_mirror_score =
      grab_ink_overlap_score(preview_image, restored_image.mirrored(true, false));
  std::cout << "[text-transform] mid-drag preview: identity=" << drag_identity_score
            << " mirror=" << drag_mirror_score << std::endl;
  CHECK(drag_mirror_score > drag_identity_score);
  send_mouse(*canvas, QEvent::MouseButtonRelease, crossed_widget, Qt::LeftButton, Qt::NoButton);
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
}

void ui_rotated_text_reedit_nudge_and_export_stay_anchored() {
  // The consistency chain the corrupted pinball poster violated: after a rotate+scale transform,
  // every subsequent operation (no-change re-edit, nudge, PSD export) must keep the stored text
  // transform and the committed raster describing the SAME placement.
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();

  const auto layer_id = create_point_text(window, *canvas, QPoint(150, 190), QStringLiteral("Hello!"));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  apply_free_transform(window, *canvas, 130.0, 130.0, 20.0);
  const auto* transformed_layer = document.find_layer(layer_id);
  CHECK(transformed_layer != nullptr);
  if (transformed_layer == nullptr) {
    return;
  }
  print_transform_state("after rotate", *transformed_layer);
  const auto rotated_transform = stored_text_transform(*transformed_layer);
  const auto rotated_ink = document_ink_rect(*transformed_layer);
  CHECK(rotated_transform.has_value());
  CHECK(rotated_ink.has_value());
  if (!rotated_transform.has_value() || !rotated_ink.has_value()) {
    return;
  }
  CHECK(std::abs((*rotated_transform)[1]) > 0.1);  // the rotation reached the metadata

  // A no-change re-edit must not move the raster or rewrite the transform.
  reedit_and_apply(window, *canvas, rotated_ink->center().toPoint());
  const auto* reedited_layer = document.find_layer(layer_id);
  CHECK(reedited_layer != nullptr);
  if (reedited_layer == nullptr) {
    return;
  }
  print_transform_state("after reedit", *reedited_layer);
  const auto reedited_transform = stored_text_transform(*reedited_layer);
  const auto reedited_ink = document_ink_rect(*reedited_layer);
  CHECK(reedited_transform.has_value());
  CHECK(reedited_ink.has_value());
  if (!reedited_transform.has_value() || !reedited_ink.has_value()) {
    return;
  }
  for (std::size_t index = 0; index < 4U; ++index) {
    CHECK(std::abs((*reedited_transform)[index] - (*rotated_transform)[index]) <= 0.05);
  }
  CHECK(std::abs((*reedited_transform)[4] - (*rotated_transform)[4]) <= 6.0);
  CHECK(std::abs((*reedited_transform)[5] - (*rotated_transform)[5]) <= 6.0);
  CHECK(std::abs(reedited_ink->center().x() - rotated_ink->center().x()) <= 6.0);
  CHECK(std::abs(reedited_ink->center().y() - rotated_ink->center().y()) <= 6.0);
  CHECK(std::abs(reedited_ink->width() - rotated_ink->width()) <= 10.0);
  CHECK(std::abs(reedited_ink->height() - rotated_ink->height()) <= 10.0);

  // Nudge with the Move tool: raster and transform translation must move together.
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  for (int step = 0; step < 3; ++step) {
    send_key(*canvas, Qt::Key_Right, Qt::ShiftModifier);
  }
  QApplication::processEvents();
  const auto* nudged_layer = document.find_layer(layer_id);
  CHECK(nudged_layer != nullptr);
  if (nudged_layer == nullptr) {
    return;
  }
  print_transform_state("after nudge", *nudged_layer);
  const auto nudged_transform = stored_text_transform(*nudged_layer);
  const auto nudged_ink = document_ink_rect(*nudged_layer);
  CHECK(nudged_transform.has_value());
  CHECK(nudged_ink.has_value());
  if (!nudged_transform.has_value() || !nudged_ink.has_value()) {
    return;
  }
  const auto nudge_delta_x = nudged_ink->center().x() - reedited_ink->center().x();
  CHECK(std::abs(nudge_delta_x - 30.0) <= 2.0);
  CHECK(std::abs((*nudged_transform)[4] - ((*reedited_transform)[4] + nudge_delta_x)) <= 2.0);
  CHECK(std::abs((*nudged_transform)[5] - (*reedited_transform)[5]) <= 2.0);

  // A re-edit after the nudge must stay at the nudged position.
  reedit_and_apply(window, *canvas, nudged_ink->center().toPoint());
  const auto* settled_layer = document.find_layer(layer_id);
  CHECK(settled_layer != nullptr);
  if (settled_layer == nullptr) {
    return;
  }
  print_transform_state("after nudge+reedit", *settled_layer);
  const auto settled_transform = stored_text_transform(*settled_layer);
  const auto settled_ink = document_ink_rect(*settled_layer);
  CHECK(settled_transform.has_value());
  CHECK(settled_ink.has_value());
  if (!settled_transform.has_value() || !settled_ink.has_value()) {
    return;
  }
  CHECK(std::abs(settled_ink->center().x() - nudged_ink->center().x()) <= 6.0);
  CHECK(std::abs(settled_ink->center().y() - nudged_ink->center().y()) <= 6.0);
  CHECK(std::abs((*settled_transform)[4] - (*nudged_transform)[4]) <= 6.0);
  CHECK(std::abs((*settled_transform)[5] - (*nudged_transform)[5]) <= 6.0);

  // Export to PSD and read back: the written TySh transform must describe the raster that was
  // written next to it (the corrupted poster stored a transform ~900px away from its raster).
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  CHECK(!bytes.empty());
  const auto reopened = patchy::psd::DocumentIo::read(bytes);
  std::function<const patchy::Layer*(const std::vector<patchy::Layer>&)> find_text =
      [&](const std::vector<patchy::Layer>& layers) -> const patchy::Layer* {
    for (const auto& layer : layers) {
      const auto found = layer.metadata().find(patchy::kLayerMetadataText);
      if (found != layer.metadata().end() &&
          QString::fromStdString(found->second).trimmed() == QStringLiteral("Hello!")) {
        return &layer;
      }
      if (const auto* nested = find_text(layer.children()); nested != nullptr) {
        return nested;
      }
    }
    return nullptr;
  };
  const auto* reimported = find_text(reopened.layers());
  CHECK(reimported != nullptr);
  if (reimported == nullptr) {
    return;
  }
  print_transform_state("reimported", *reimported);
  const auto reimported_transform = stored_text_transform(*reimported);
  const auto reimported_ink = document_ink_rect(*reimported);
  CHECK(reimported_transform.has_value());
  CHECK(reimported_ink.has_value());
  if (!reimported_transform.has_value() || !reimported_ink.has_value()) {
    return;
  }
  for (std::size_t index = 0; index < 4U; ++index) {
    CHECK(std::abs((*reimported_transform)[index] - (*settled_transform)[index]) <= 0.02);
  }
  CHECK(std::abs((*reimported_transform)[4] - (*settled_transform)[4]) <= 2.0);
  CHECK(std::abs((*reimported_transform)[5] - (*settled_transform)[5]) <= 2.0);
  CHECK(std::abs(reimported_ink->center().x() - settled_ink->center().x()) <= 2.0);
  CHECK(std::abs(reimported_ink->center().y() - settled_ink->center().y()) <= 2.0);
}

// One document-geometry operation applied to a document holding a committed text layer. The
// invariant after ANY of these: a no-change re-edit commits the text back where the operation's
// raster left it (i.e. the operation kept patchy.text.transform in step with the pixels it moved).
struct GeometryScenario {
  const char* name;
  bool rotated;
  std::function<void(patchy::ui::CanvasWidget&, patchy::Document&, patchy::LayerId)> op;
};

void run_geometry_scenario(const GeometryScenario& scenario) {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();

  const auto layer_id = create_point_text(window, *canvas, QPoint(150, 190), QStringLiteral("Flip me."));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  if (scenario.rotated) {
    apply_free_transform(window, *canvas, 130.0, 130.0, 20.0);
  }
  scenario.op(*canvas, document, layer_id);
  QApplication::processEvents();

  const auto* moved_layer = document.find_layer(layer_id);
  CHECK(moved_layer != nullptr);
  if (moved_layer == nullptr) {
    return;
  }
  print_transform_state((std::string(scenario.name) + " after op").c_str(), *moved_layer);
  const auto moved_ink = document_ink_rect(*moved_layer);
  const auto moved_image = visible_alpha_cropped_image(moved_layer->pixels());
  CHECK(moved_ink.has_value());
  if (!moved_ink.has_value()) {
    return;
  }

  reedit_and_apply(window, *canvas, moved_ink->center().toPoint());
  const auto* final_layer = document.find_layer(layer_id);
  CHECK(final_layer != nullptr);
  if (final_layer == nullptr) {
    return;
  }
  print_transform_state((std::string(scenario.name) + " after reedit").c_str(), *final_layer);
  const auto final_ink = document_ink_rect(*final_layer);
  CHECK(final_ink.has_value());
  if (!final_ink.has_value()) {
    return;
  }
  CHECK(std::abs(final_ink->center().x() - moved_ink->center().x()) <= 8.0);
  CHECK(std::abs(final_ink->center().y() - moved_ink->center().y()) <= 8.0);
  CHECK(std::abs(final_ink->width() - moved_ink->width()) <=
        std::max(12.0, moved_ink->width() * 0.15));
  CHECK(std::abs(final_ink->height() - moved_ink->height()) <=
        std::max(12.0, moved_ink->height() * 0.15));
  // Orientation must survive the re-edit too: the committed render has to match the
  // operation's raster, not the pre-operation one ("Flip me." has no mirror symmetry).
  const auto final_image = visible_alpha_cropped_image(final_layer->pixels());
  const auto same_orientation_score = alpha_overlap_score(final_image, moved_image);
  const auto mirrored_orientation_score = alpha_overlap_score(final_image, moved_image.mirrored(true, false));
  std::cout << "[text-transform] " << scenario.name << " orientation: same=" << same_orientation_score
            << " mirrored=" << mirrored_orientation_score << std::endl;
  CHECK(same_orientation_score > mirrored_orientation_score);
}

void ui_image_size_keeps_text_transform_in_sync() {
  run_geometry_scenario(GeometryScenario{
      "image-size", true, [](patchy::ui::CanvasWidget& canvas, patchy::Document& document, patchy::LayerId) {
        // Non-uniform, like the pinball poster resized to A3.
        patchy::resize_image_and_layers(document, document.width() * 8 / 5, document.height() * 5 / 4);
        canvas.set_document(&document);
      }});
}

void ui_canvas_size_keeps_text_transform_in_sync() {
  run_geometry_scenario(GeometryScenario{
      "canvas-size", true, [](patchy::ui::CanvasWidget& canvas, patchy::Document& document, patchy::LayerId) {
        patchy::resize_canvas_and_layers(document, document.width() + 150, document.height() + 90,
                                         patchy::CanvasAnchor::BottomRight);
        canvas.set_document(&document);
      }});
}

void ui_crop_keeps_text_transform_in_sync() {
  run_geometry_scenario(GeometryScenario{
      "crop", true, [](patchy::ui::CanvasWidget& canvas, patchy::Document& document, patchy::LayerId) {
        CHECK(patchy::crop_document(document,
                                    patchy::Rect{60, 40, document.width() - 60, document.height() - 40}));
        canvas.set_document(&document);
      }});
}

void ui_rotate_canvas_keeps_text_transform_in_sync() {
  run_geometry_scenario(GeometryScenario{
      "rotate-canvas", true, [](patchy::ui::CanvasWidget& canvas, patchy::Document& document, patchy::LayerId) {
        patchy::rotate_document_clockwise(document);
        canvas.set_document(&document);
      }});
}

void ui_layer_flip_keeps_text_mirrored_across_reedit() {
  run_geometry_scenario(GeometryScenario{
      "flip-layer", false,
      [](patchy::ui::CanvasWidget& canvas, patchy::Document& document, patchy::LayerId layer_id) {
        static_cast<void>(patchy::flip_layer_horizontal(document, layer_id));
        canvas.set_document(&document);
      }});
}

}  // namespace

std::vector<patchy::test::TestCase> text_transform_commit_tests_part3() {
  return {
      {"ui_point_text_flip_transform_mirrors_and_survives_reedit",
       ui_point_text_flip_transform_mirrors_and_survives_reedit},
      {"ui_rotated_text_reedit_nudge_and_export_stay_anchored",
       ui_rotated_text_reedit_nudge_and_export_stay_anchored},
      {"ui_image_size_keeps_text_transform_in_sync", ui_image_size_keeps_text_transform_in_sync},
      {"ui_canvas_size_keeps_text_transform_in_sync", ui_canvas_size_keeps_text_transform_in_sync},
      {"ui_crop_keeps_text_transform_in_sync", ui_crop_keeps_text_transform_in_sync},
      {"ui_rotate_canvas_keeps_text_transform_in_sync", ui_rotate_canvas_keeps_text_transform_in_sync},
      {"ui_layer_flip_keeps_text_mirrored_across_reedit", ui_layer_flip_keeps_text_mirrored_across_reedit},
  };
}
