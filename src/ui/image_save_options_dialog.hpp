#pragma once

#include "ui/image_document_io.hpp"

#include <QSize>
#include <QString>

#include <optional>

class QComboBox;
class QWidget;

namespace patchy::ui {

// Fills a combo with the PDF image-quality presets (ui/pdf_export.hpp; item data is the
// preset id) and selects `current_id`, or the default preset when that id is unknown.
// Shared by the PDF Options and Export Multi-Page PDF dialogs.
void populate_pdf_image_quality_combo(QComboBox& combo, const QString& current_id);
// The "keep original image data" checkbox text, shared by the same two dialogs.
[[nodiscard]] QString pdf_keep_original_images_label();
[[nodiscard]] QString pdf_keep_original_images_tooltip();

[[nodiscard]] bool image_save_options_apply_to_extension(const QString& extension);
[[nodiscard]] ImageSaveOptions load_image_save_option_defaults();
void save_image_save_option_defaults(const ImageSaveOptions& options);
// for_export appends the shared Export section (smooth resize, pixel-art scale, keep or
// fill transparency, trim, reveal when done) to every raster format's dialog, including a
// section-only dialog for formats that otherwise have no options, and is passed only by
// the export flows, never Save/Save As. document_size drives the Resize row; an empty size
// hides it. ICO/CUR never get the section (their size list defines the output).
// `document` (optional) is what a preview can read: the DDS form's Preview Mipmaps button
// encodes it at the dialog's current choices and is hidden without one.
[[nodiscard]] std::optional<ImageSaveOptions> prompt_image_save_options(QWidget* parent, const QString& extension,
                                                                        ImageSaveOptions options,
                                                                        bool for_export = false,
                                                                        QSize document_size = {},
                                                                        const Document* document = nullptr);

// GIF options dialog, deliberately outside image_save_options_apply_to_extension so only
// its three call sites raise it (Save As and Export Flat Image with a 2+ layer document,
// and the Export Layers as Animated GIF action). offer_flatten_choice shows the
// animation-vs-single-image radios (Save As / Export form, remembered as
// saveOptions/gifSaveMode); false is the animated-export form: no radios, gif_animate
// always true. for_export adds the shared Export section (applied per frame when
// animating); document_size drives its Resize row. has_visible_frames false disables the
// animation radio and forces the flattened choice. Returns options with gif_animate,
// gif_frame_delay_cs, and the export fields set, or nullopt on cancel.
[[nodiscard]] std::optional<ImageSaveOptions> prompt_gif_save_options(QWidget* parent, ImageSaveOptions options,
                                                                      bool offer_flatten_choice, bool for_export,
                                                                      bool has_visible_frames,
                                                                      QSize document_size = {});

}  // namespace patchy::ui
