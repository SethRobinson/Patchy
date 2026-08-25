#pragma once

#include "core/photo_divide.hpp"

#include <QString>

#include <memory>
#include <optional>
#include <vector>

class QWidget;

namespace patchy::ui {

// Which way the photos' top edge points in the scan. Persisted as ints in
// dividePhotos/upDirection (permanent identifiers; never renumber).
enum class PhotoUpDirection : int { Up = 0, Right = 1, Down = 2, Left = 3 };

// Compensating rotation for extraction (see rotated_quarter_turns).
[[nodiscard]] constexpr int up_direction_cw_turns(PhotoUpDirection direction) {
  return (4 - static_cast<int>(direction)) % 4;
}

// Persisted as ints in dividePhotos/output (permanent identifiers).
enum class DividePhotosOutput : int { OpenDocuments = 0, SaveToFolder = 1, SaveAndOpen = 2 };

// Persisted as ints in dividePhotos/existingFiles (permanent identifiers).
enum class DividePhotosExistingFiles : int { AddNumbering = 0, Overwrite = 1 };

// Everything the dialog reads as initial values and hands back on OK; the
// caller persists it under the dividePhotos/* settings keys.
struct DividePhotosSettings {
  int sensitivity{50};
  PhotoExtractMode mode{PhotoExtractMode::Straighten};
  PhotoUpDirection up_direction{PhotoUpDirection::Up};
  DividePhotosOutput output{DividePhotosOutput::OpenDocuments};
  QString folder;  // absolute path typed or browsed in the dialog
  QString prefix{QStringLiteral("photo_")};
  QString format{QStringLiteral("png")};  // extension token, e.g. "png"
  DividePhotosExistingFiles existing_files{DividePhotosExistingFiles::AddNumbering};
};

// One row of the save-format combo; the caller builds the list from the file
// format registry (the dialog must not depend on main_window internals).
struct DividePhotosFormatChoice {
  QString display_name;  // localized
  QString extension;     // token stored in settings, e.g. "png"
};

struct DividePhotosDialogResult {
  std::vector<PhotoRegion> regions;  // final edited regions, reading order
  DividePhotosSettings settings;
};

// Modal "Divide Scanned Photos" dialog: the source image with every detected
// photo outlined, a sensitivity slider (re-detects, debounced; regions the
// user added or edited survive re-detection), the Straighten and Fix
// Perspective checkboxes (Fix Perspective implies Straighten; both off cuts
// plain axis-aligned crops), the top-edge direction picker, region editing
// (drag to move, handles to resize, corner drag in perspective mode, drag on
// empty background or Add Region to add, Delete to remove), and the output
// choice (open each photo as a new document, save numbered files to a folder,
// or both) with the folder, filename prefix, format, and existing-file
// handling kept in the dialog. nullopt on Cancel.
//
// Legal boundary (docs/legal-constraints.md, "Scanned-photo division"):
// detection runs only on open and on parameter changes, on this still image,
// with global parameters. No live re-detection is added after the dialog
// closes.
[[nodiscard]] std::optional<DividePhotosDialogResult> request_divide_photos(
    QWidget* parent, std::shared_ptr<const PixelBuffer> source, double source_ppi,
    const DividePhotosSettings& initial, const std::vector<DividePhotosFormatChoice>& formats);

}  // namespace patchy::ui
