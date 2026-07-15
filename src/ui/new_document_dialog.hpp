#pragma once

#include <QColor>
#include <QImage>
#include <QtCore/qnamespace.h>

#include <cstdint>
#include <optional>

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
  bool from_clipboard{false};
  QImage clipboard_image;
};

// Shows the New Document dialog (category chips + clickable preset card grid on the
// left, the width/height/resolution/background details pane on the right) and
// returns the accepted settings, or nullopt on cancel. Remembers the last accepted
// non-clipboard settings in the "newDocument" settings group and restores them the
// next time it opens; a clipboard image preselects the Clipboard card instead.
[[nodiscard]] std::optional<NewDocumentSettings> request_new_document_settings(QWidget* parent);

}  // namespace patchy::ui
