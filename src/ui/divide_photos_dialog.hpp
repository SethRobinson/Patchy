#pragma once

#include "core/photo_divide.hpp"

#include <memory>
#include <optional>
#include <vector>

class QWidget;

namespace patchy::ui {

struct DividePhotosDialogResult {
  std::vector<PhotoRegion> regions;  // final edited regions, reading order
  PhotoExtractMode mode{PhotoExtractMode::Straighten};
  int sensitivity{50};
  bool save_to_folder{false};
};

// Modal "Divide Scanned Photos" dialog: the source image with every detected
// photo outlined, a sensitivity slider (re-detects, debounced; regions the
// user added or edited survive re-detection), the Straighten and Fix
// Perspective checkboxes (Fix Perspective implies Straighten; both off cuts
// plain axis-aligned crops), region editing (drag to move, handles to
// resize, corner drag in perspective mode, drag on empty background or Add
// Region to add, Delete to remove), and the output choice (open each photo
// as a new document, or save numbered files to a folder). nullopt on Cancel.
//
// Legal boundary (docs/legal-constraints.md, "Scanned-photo division"):
// detection runs only on open and on parameter changes, on this still image,
// with global parameters. No live re-detection is added after the dialog
// closes.
[[nodiscard]] std::optional<DividePhotosDialogResult> request_divide_photos(
    QWidget* parent, std::shared_ptr<const PixelBuffer> source, double source_ppi,
    int initial_sensitivity, bool initial_straighten, bool initial_perspective,
    bool initial_save_to_folder);

}  // namespace patchy::ui
