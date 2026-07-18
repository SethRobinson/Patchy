#pragma once

#include "core/document.hpp"

#include <QSize>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

class QWidget;

namespace patchy::ui {

// Image-sequence import/export helpers and their dialogs (the sibling of the
// sprite-sheet pair in sprite_sheet_dialog.hpp). The file-dialog-driven flows live in
// MainWindow (import_image_sequence / export_image_sequence): they need the document
// render helpers and the session machinery.

// Natural-sorts by file name (numeric-aware, case-insensitive), so crap2.bmp orders
// before crap10.bmp; the full path breaks ties.
[[nodiscard]] QStringList sorted_sequence_paths(QStringList paths);

// If the file's base name ends in a digit run, returns the natural-sorted run of
// sibling files sharing the same prefix + digit run + extension (case-insensitive);
// otherwise returns just the input path. Used when the user selects a single file.
[[nodiscard]] QStringList expand_numbered_sequence(const QString& path);

// One layer per file, bottom to top in list order, named after the file's base name;
// only the first layer starts visible (matching slice_sprite_sheet). Canvas is the
// max width x max height across frames; smaller frames sit at the top-left. A file
// that fails to load aborts with its name in *error.
[[nodiscard]] std::optional<Document> document_from_image_sequence(const QStringList& paths, QString* error);

struct ImageSequenceNaming {
  bool use_layer_names{false};
  QString prefix;
  int start{1};
  int padding{3};
};

// Derives numbered-export defaults from the base name typed in the save dialog: a
// trailing digit run supplies prefix/start/padding; otherwise "_" is appended and
// numbering starts at 001.
[[nodiscard]] ImageSequenceNaming naming_from_save_base_name(const QString& base_name);

// File names (no directory) for each frame. Numbered mode: prefix + zero-padded
// number + extension. Layer-name mode: sanitized layer names (filesystem-illegal
// characters replaced, empty names become "Frame N") deduplicated with " 2", " 3"...
[[nodiscard]] QStringList image_sequence_file_names(const std::vector<QString>& layer_names,
                                                    const ImageSequenceNaming& naming, const QString& extension);

// Confirmation dialog listing the ordered files (so an auto-detected run is visible
// before it imports). canvas_size may be invalid when no frame size could be probed.
[[nodiscard]] bool prompt_image_sequence_import_options(QWidget* parent, const QStringList& ordered_paths,
                                                        QSize canvas_size);

struct ImageSequenceExportOptions {
  ImageSequenceNaming naming;
  bool visible_layers_only{true};
};

// The scope radios (visible layers only / all layers) drive the frame count and name
// preview; visible-only is the default but disabled (falling back to all) when nothing
// is visible.
[[nodiscard]] std::optional<ImageSequenceExportOptions> prompt_image_sequence_export_options(
    QWidget* parent, const std::vector<QString>& visible_layer_names, const std::vector<QString>& all_layer_names,
    const ImageSequenceNaming& suggested, const QString& extension);

}  // namespace patchy::ui
