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
// The size a card shows, in its unit ("210 x 297 mm", "1920 x 1080 px").
inline constexpr int kNewDocumentPresetSizeTextRole = Qt::UserRole + 7;

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
  // Image > Mode > CMYK Proof from the start (GitHub issue 360; docs/cmyk.md). The Color
  // Mode list offers it only while Preferences names a CMYK profile.
  bool cmyk_proof{false};
  // The Empty Layer checkbox (GitHub issue 363): an empty layer above the Background. It
  // opens at the newDocument/addEmptyLayer preference and never writes it.
  bool add_empty_layer{true};
  // The Name field (GitHub issue 374): the tab title and the default Save As name, already
  // through sanitized_new_document_name; never empty.
  QString name;
};

// The Name field's rule: drops the characters a file name cannot hold on Windows (the
// strictest file system: \ / : * ? " < > | and control codes) and the trailing dots and
// spaces it rejects. Unicode letters stay. Empty when nothing usable is left.
[[nodiscard]] QString sanitized_new_document_name(const QString& name);

// Shows the New Document dialog (category chips + clickable preset card grid on the
// left, the width/height/resolution/background details pane on the right) and
// returns the accepted settings, or nullopt on cancel. Remembers the last accepted
// settings (size, PPI, background, bit depth, Artboards, the W/H unit and the
// resolution unit) in the "newDocument" settings group and opens on a Last Used card
// that restores them; a Clipboard create remembers only its size and PPI. Before the
// first create there is no Last Used card and Default is selected. A clipboard image
// never preselects the Clipboard card. On a first run the W/H unit follows the ruler
// unit preference (`view/rulerUnits`). The Name field starts as `default_name`
// ("Untitled-N"; "Untitled-1" when empty), which the result also falls back to; it is
// never remembered. Saved presets (Save Preset, `newDocument/userPresets`) fill a Saved
// group; docs/new-document.md.
[[nodiscard]] std::optional<NewDocumentSettings> request_new_document_settings(QWidget* parent,
                                                                               const QString& default_name = {});

// Whether the system clipboard offers image data: the New Document dialog's Clipboard card
// and File > Open from Clipboard (GitHub issue 218) are enabled only then. It reads the
// clipboard's format list, never the image, so it costs no transfer or decode.
[[nodiscard]] bool clipboard_offers_image();

// The New Document size presets an artboard can take (every built-in template; not Clipboard
// or the user's saved presets), for
// the Artboard tool and the New Artboard dialog, in the dialog's group order.
struct ArtboardSizePreset {
  QString id;  // the New Document preset id (persisted there; never renamed)
  QString name;
  QSize size;
  QString group;  // the template group's name; pickers group the presets by it
};
[[nodiscard]] std::vector<ArtboardSizePreset> artboard_size_presets();

// The New Document search rule (GitHub issue 223), shared with the Artboard bar's preset
// picker: every whitespace-separated word of `text` must appear, ignoring case, in the
// preset's name or its group's name; a word holding a digit may also match the size
// ("1080 x 1920", "1080x1920") or the size the card shows in its own unit, `shown_size`
// ("210 x 297 mm", so "210" and "8.5" find paper).
[[nodiscard]] bool size_preset_matches_search(const QString& name, const QString& group, QSize size,
                                              const QString& text, const QString& shown_size = {});

}  // namespace patchy::ui
