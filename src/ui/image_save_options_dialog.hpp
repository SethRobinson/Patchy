#pragma once

#include "ui/image_document_io.hpp"

#include <QColor>
#include <QSize>
#include <QString>

#include <functional>
#include <optional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QVBoxLayout;
class QWidget;

namespace patchy::ui {

// Fills a combo with the PDF image-quality presets (ui/pdf_export.hpp; item data is the
// preset id) and selects `current_id`, or the default preset when that id is unknown.
// Shared by the PDF Options and Export Multi-Page PDF dialogs.
void populate_pdf_image_quality_combo(QComboBox& combo, const QString& current_id);
// The "keep original image data" checkbox text, shared by the same two dialogs.
[[nodiscard]] QString pdf_keep_original_images_label();
[[nodiscard]] QString pdf_keep_original_images_tooltip();

// The animation timing controls every animated export shows (the WebP, JPEG XL, Animated
// PNG and GIF options dialogs and File > Export for Web, docs/web-export.md): "Default frame
// delay:" in seconds, Forever / Play count where the format stores a play count, and the
// explanation of layer-name delays. GIF keeps its centisecond wire range (2 decimals, up to
// 655.35 s) and loops forever, so it has no play count; the others take 3 decimals up to
// animation::kMaxFrameDelayMs. Object names are `<prefix>FrameDelaySpin`, `<prefix>ForeverCheck`,
// `<prefix>PlayCountSpin` and `<prefix>AnimationExplanationLabel`. `controls` holds the rows
// in a form; the explanation is a separate label the caller places.
// The export Metadata choices in order: None, Basic, Copyright and Contact Info, All
// (metadata_policy_key data, a tooltip each). Export for Web and the Export section share it.
void add_export_metadata_choices(QComboBox& combo);

enum class AnimationTimingKind { Gif, Webp, Jxl, Apng };
struct AnimationTimingControls {
  AnimationTimingKind kind{AnimationTimingKind::Webp};
  QWidget* controls{nullptr};
  QFormLayout* form{nullptr};
  QDoubleSpinBox* frame_delay{nullptr};
  QCheckBox* forever_check{nullptr};  // null for GIF
  QSpinBox* plays{nullptr};           // null for GIF
  QLabel* explanation{nullptr};
};
[[nodiscard]] AnimationTimingControls create_animation_timing_controls(QWidget* parent, AnimationTimingKind kind,
                                                                       const QString& object_prefix, int delay_ms,
                                                                       int loop_count);
// The default delay the controls show, in milliseconds (GIF: whole centiseconds times 10).
[[nodiscard]] int animation_timing_delay_ms(const AnimationTimingControls& controls);
void set_animation_timing_delay_ms(const AnimationTimingControls& controls, int delay_ms);
// 0 for Forever (and always for GIF), else the play count.
[[nodiscard]] int animation_timing_loop_count(const AnimationTimingControls& controls);
void set_animation_timing_loop_count(const AnimationTimingControls& controls, int loop_count);

// The Transparency choice of the Export section and File > Export for Web: "Keep transparent"
// or "Fill with:" and a background color swatch that opens Patchy's color picker. Object
// names `<prefix>KeepTransparencyRadio`, `<prefix>FillTransparencyRadio`,
// `<prefix>BackgroundLabel`, `<prefix>BackgroundColorSwatch`. With forced fill (JPEG, no alpha)
// the radios hide and "Background:" labels the swatch. `changed` runs when the choice or the
// picked color changes.
struct TransparencyChoiceWidgets {
  QRadioButton* keep{nullptr};
  QRadioButton* fill{nullptr};
  QLabel* background_label{nullptr};
  QPushButton* swatch{nullptr};
  QWidget* fill_row{nullptr};
};
[[nodiscard]] TransparencyChoiceWidgets add_transparency_choice(QVBoxLayout* layout, QWidget* parent,
                                                                QWidget& picker_parent, const QString& object_prefix,
                                                                QColor color, bool fill, bool forced_fill,
                                                                std::function<void()> changed = {});
// Switches a choice between forced fill (JPEG) and the free choice without changing which
// radio is checked, so a format switch keeps the user's choice for the formats with alpha.
void set_transparency_choice_forced_fill(const TransparencyChoiceWidgets& widgets, bool forced);
[[nodiscard]] QColor transparency_choice_color(const TransparencyChoiceWidgets& widgets);
void set_transparency_choice_color(const TransparencyChoiceWidgets& widgets, QColor color);

// "Show in Explorer when done" (Finder, file manager), the export dialogs' reveal checkbox.
[[nodiscard]] QString export_reveal_check_label();
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

// TIFF Options for a layered document (Save As only, docs/tiff.md): "Keep layers", on
// unless options.tiff_layers says otherwise. Returns options with tiff_layers set, or
// nullopt on cancel. Outside image_save_options_apply_to_extension, like GIF: a flat
// document saves TIFF without a dialog.
[[nodiscard]] std::optional<ImageSaveOptions> prompt_tiff_save_options(QWidget* parent, ImageSaveOptions options);

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
