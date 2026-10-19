#pragma once

#include "core/pixel_buffer.hpp"

#include <QColor>
#include <QImage>
#include <QSize>
#include <QString>
#include <QtCore/qnamespace.h>

#include <cstdint>
#include <optional>
#include <vector>

class QWidget;

namespace patchy::ui {

// Item-data roles on the preset card list (objectName newDocumentPresetList).
// Exposed so UI tests can drive the cards without duplicating the values.
inline constexpr int kNewDocumentPresetIdRole = Qt::UserRole + 1;
inline constexpr int kNewDocumentPresetSizeRole = Qt::UserRole + 2;
inline constexpr int kNewDocumentPresetPpiRole = Qt::UserRole + 3;

struct NewDocumentSettings {
  std::int32_t width{1024};
  std::int32_t height{768};
  double resolution_ppi{72.0};
  QColor background{Qt::white};
  // The document's bits per channel (docs/high-bit-depth.md); offered while deep
  // editing is on, 8 otherwise.
  BitDepth bit_depth{BitDepth::UInt8};
  bool from_clipboard{false};
  QImage clipboard_image;
  // Photoshop's Artboards checkbox: the document starts as one artboard the size of the
  // canvas, its background the chosen one (docs/artboards.md).
  bool artboards{false};
};

// Shows the New Document dialog (category chips + clickable preset card grid on the
// left, the width/height/resolution/background details pane on the right) and
// returns the accepted settings, or nullopt on cancel. Remembers the last accepted
// settings (size, PPI, background, bit depth, Artboards, the W/H unit and the
// resolution unit) in the "newDocument" settings group and opens on a Last Used card
// that restores them; a Clipboard create remembers only its size and PPI. Before the
// first create there is no Last Used card and Default is selected. A clipboard image
// never preselects the Clipboard card. On a first run the W/H unit follows the ruler
// unit preference (`view/rulerUnits`).
[[nodiscard]] std::optional<NewDocumentSettings> request_new_document_settings(QWidget* parent);

// Whether the system clipboard offers image data: the New Document dialog's Clipboard card
// and File > Open from Clipboard (GitHub issue 218) are enabled only then. It reads the
// clipboard's format list, never the image, so it costs no transfer or decode.
[[nodiscard]] bool clipboard_offers_image();

// The New Document size presets an artboard can take (every screen and print card but
// Clipboard), for the Artboard tool and the New Artboard dialog.
struct ArtboardSizePreset {
  QString name;
  QSize size;
};
[[nodiscard]] std::vector<ArtboardSizePreset> artboard_size_presets();

}  // namespace patchy::ui
