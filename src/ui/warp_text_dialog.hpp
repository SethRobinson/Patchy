#pragma once

#include "core/text_warp.hpp"

#include <functional>
#include <optional>

class QWidget;

namespace patchy::ui {

// Photoshop's Warp Text dialog: style (all fifteen styles), horizontal/vertical
// orientation, bend, and the two distortion percentages. `preview` fires on every
// control change with the dialog's current warp (style None => identity warp) so
// the caller can live-update the layer; the caller owns restore-on-cancel and the
// undo step. Returns the chosen warp on OK (style "warpNone" when the user picked
// None), nullopt on Cancel.
[[nodiscard]] std::optional<TextWarp> request_text_warp(QWidget* parent, const TextWarp& initial,
                                                        const std::function<void(const TextWarp&)>& preview);

}  // namespace patchy::ui
