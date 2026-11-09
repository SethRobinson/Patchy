#pragma once

#include <QtGlobal>

// Clipboard checks that never open the system clipboard (docs/clipboard.md, "Slow or
// locked clipboards"). Qt's Windows clipboard opens it for every query (QMimeData::hasImage,
// hasText), and while another process holds it open, Qt sleeps and retries for about half
// a second per query before it gives up. The window's action state asks these first, so a
// locked clipboard costs nothing when it holds no such data.
namespace patchy::ui {

enum class ClipboardData { Image, Text };

// False only when the system clipboard certainly offers no `kind` data for Qt to read: on
// Qt's native Windows platform, when none of the formats Qt converts to that kind is listed
// (IsClipboardFormatAvailable, which neither opens the clipboard nor calls its owner).
// Elsewhere, and under Qt's other platforms (the offscreen test platform keeps a clipboard
// of its own), it returns true and the caller asks Qt.
[[nodiscard]] bool system_clipboard_may_offer(ClipboardData kind);

// A number that changes whenever the system clipboard's contents change (Windows
// GetClipboardSequenceNumber on the native platform). 0 means unknown: never cache on it.
[[nodiscard]] quint32 system_clipboard_change_number();

}  // namespace patchy::ui
