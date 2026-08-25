#pragma once

#include "ui/image_document_io.hpp"

#include <QString>

#include <optional>

class QWidget;

namespace patchy::ui {

[[nodiscard]] bool image_save_options_apply_to_extension(const QString& extension);
[[nodiscard]] ImageSaveOptions load_image_save_option_defaults();
void save_image_save_option_defaults(const ImageSaveOptions& options);
// for_export adds the nearest-neighbor Scale combo (1x/2x/4x/8x) to every raster format's
// dialog — including a scale-only dialog for formats that otherwise have no options — and
// is passed only by the Export Flat Image flow, never Save/Save As.
[[nodiscard]] std::optional<ImageSaveOptions> prompt_image_save_options(QWidget* parent, const QString& extension,
                                                                        ImageSaveOptions options,
                                                                        bool for_export = false);

// GIF options dialog, deliberately outside image_save_options_apply_to_extension so only
// its three call sites raise it (Save As and Export Flat Image with a 2+ layer document,
// and the Export Layers as Animated GIF action). offer_flatten_choice shows the
// animation-vs-single-image radios (Save As / Export form, remembered as
// saveOptions/gifSaveMode); false is the animated-export form: no radios, gif_animate
// always true. for_export adds the Scale combo (applied per frame when animating).
// has_visible_frames false disables the animation radio and forces the flattened choice.
// Returns options with gif_animate, gif_frame_delay_cs, and export_scale set, or nullopt
// on cancel.
[[nodiscard]] std::optional<ImageSaveOptions> prompt_gif_save_options(QWidget* parent, ImageSaveOptions options,
                                                                      bool offer_flatten_choice, bool for_export,
                                                                      bool has_visible_frames);

}  // namespace patchy::ui
