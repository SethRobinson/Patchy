#pragma once

#include "core/document.hpp"

#include <QImage>
#include <QSize>
#include <QString>

#include <optional>

class QWidget;

namespace patchy::ui {

// Options dialogs for the sprite-sheet features. The composition/slicing itself lives in
// MainWindow (export_sprite_sheet / import_sprite_sheet): it needs the document render
// helpers and the session machinery.

struct SpriteSheetExportOptions {
  int columns{4};
  int padding{0};
  bool transparent_background{true};
};

[[nodiscard]] std::optional<SpriteSheetExportOptions> prompt_sprite_sheet_export_options(QWidget* parent,
                                                                                         int frame_count);

struct SpriteSheetImportOptions {
  int cell_width{32};
  int cell_height{32};
  int margin{0};
  int spacing{0};
};

[[nodiscard]] std::optional<SpriteSheetImportOptions> prompt_sprite_sheet_import_options(QWidget* parent,
                                                                                         QSize image_size);

// One frame per visible top-level layer, bottom to top (hidden layers contribute nothing);
// groups render as their flattened subtree against an empty backdrop. Cell size = document
// size; padding surrounds and separates cells.
[[nodiscard]] QImage compose_sprite_sheet(const Document& document, const SpriteSheetExportOptions& options);

// Slices a sheet into a cell-sized document with one layer per non-empty cell (row-major,
// named via frame_name_format's %1; only the first visible). Returns nullopt when no
// non-empty cell fits.
[[nodiscard]] std::optional<Document> slice_sprite_sheet(const QImage& sheet,
                                                         const SpriteSheetImportOptions& options,
                                                         const QString& frame_name_format);

}  // namespace patchy::ui
