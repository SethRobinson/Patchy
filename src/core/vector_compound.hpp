#pragma once

#include "core/document.hpp"
#include "core/vector_shape.hpp"

#include <span>

namespace patchy {

[[nodiscard]] bool layer_is_compound_vector(const Layer& layer);
[[nodiscard]] bool document_has_compound_vectors(const Document& document);
[[nodiscard]] VectorShapeContent combine_vector_appearances(std::span<const Layer* const> layers);
[[nodiscard]] VectorShapeContent vector_shape_part_content(const VectorShapeContent& shape,
                                                           const VectorShapePart& part);
// Runtime/legacy group markers. PSD stores these associations in a plug-in
// image resource, never as unknown per-layer tagged blocks.
enum class CompoundVectorGroupKind : std::uint32_t { None, Content, FillOpacity, OpenPathStrokes };
[[nodiscard]] CompoundVectorGroupKind compound_vector_group_kind(const Layer& layer);
void set_compound_vector_group_kind(Layer& layer, CompoundVectorGroupKind kind);
// A normal group of native shapes is the interchange representation. Other
// readers receive fully editable, individually painted vectors.
[[nodiscard]] Layer expand_compound_vector_layer(const Layer& layer);
[[nodiscard]] Document expand_compound_vectors(const Document& document, bool bake);
// PSD closes open contours when a native stroked shape has multiple subpaths.
// Keep solid center strokes in separate native shapes with a reversible group.
[[nodiscard]] bool document_has_open_path_strokes(const Document& document);
[[nodiscard]] Document expand_open_path_strokes(const Document& document);
void collapse_compound_vector_groups(Document& document);
void transform_vector_part_appearance(VectorShapeContent& shape, const std::array<double, 6>& matrix,
                                      double stroke_scale);
// Only explicitly changed fields are applied to all parts. Unrelated edits
// must never replace the distinct colors and strokes of a merged object.
void update_vector_part_appearance(VectorShapeContent& shape, const VectorFill& previous_fill,
                                   const VectorStroke& previous_stroke);

}  // namespace patchy
