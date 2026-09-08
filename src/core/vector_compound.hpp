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
// A normal group of native shapes is the interchange representation. PSD uses
// a versioned private group marker so Patchy can restore one vector layer;
// other readers still receive fully editable, individually painted vectors.
[[nodiscard]] Layer expand_compound_vector_layer(const Layer& layer);
[[nodiscard]] Document expand_compound_vectors(const Document& document, bool bake);
void collapse_compound_vector_groups(Document& document);
void transform_vector_part_appearance(VectorShapeContent& shape, const std::array<double, 6>& matrix,
                                      double stroke_scale);
// Only explicitly changed fields are applied to all parts. Unrelated edits
// must never replace the distinct colors and strokes of a merged object.
void update_vector_part_appearance(VectorShapeContent& shape, const VectorFill& previous_fill,
                                   const VectorStroke& previous_stroke);

}  // namespace patchy
