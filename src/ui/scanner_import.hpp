#pragma once

#include <QString>

class QWidget;

namespace patchy::ui {

// Native scanner acquisition. Windows uses WIA and also exposes cameras; macOS uses
// ImageKit/ImageCaptureCore and exposes scanners only. Implemented in per-OS translation
// units following the AGENTS.md platform convention.
enum class ScannerAcquireStatus {
  Acquired,   // file_path holds the scanned image (caller deletes it after import)
  Cancelled,  // the user closed the native acquisition UI; stay silent
  NoDevice,   // the native backend reported that no compatible device is available
  Failed,     // error holds a short description
};

struct ScannerAcquireResult {
  ScannerAcquireStatus status{ScannerAcquireStatus::Failed};
  QString file_path;
  QString error;
};

[[nodiscard]] ScannerAcquireResult acquire_image_from_scanner(QWidget* parent);

}  // namespace patchy::ui
