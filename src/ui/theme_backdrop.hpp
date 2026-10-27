#pragma once

// The theme backdrop image (docs/theme-files.md, "Backdrop"): a theme package's
// "backdrop" paints the pasteboard around the document and the empty workspace
// behind the start panel. It is anchored to the top-level window, so it holds
// still while the document pans and zooms, and drawn from a pixmap cached per
// theme_generation(): a repaint only fills the area it exposes.

#include <QColor>
#include <QDateTime>
#include <QImage>
#include <QRect>
#include <QString>

class QPainter;
class QWidget;

namespace patchy::ui {

// True when the active theme carries a backdrop image.
[[nodiscard]] bool theme_backdrop_active();

// Paints `area` (widget coordinates) of `widget` with the theme backdrop over
// `fallback` (the color under transparent pixels and around a centered image).
// Returns false, painting nothing, when the active theme has no backdrop image.
bool paint_theme_backdrop(QPainter& painter, const QRect& area, const QWidget& widget, const QColor& fallback);

// The QSS url() path for the canvas scroll-bar gutter texture under a custom
// theme: the backdrop tile itself for a tiled backdrop, otherwise a two-pixel
// dither built from the theme's canvas_scrollbar_track. Empty when no custom
// theme is active or its track is the built-in one, so the authored
// scroll-dither.svg serves. The generated PNG goes to a temporary-folder cache
// named after its content.
[[nodiscard]] QString themed_scroll_gutter_url();

// Writes `image` once to <temp>/patchy-theme-cache/<prefix>-<sha1>.png (named after its
// content, so it is never rewritten while in use) and returns it as a QSS url() argument.
// Empty when it cannot be written, or once this process has written
// kThemeCacheSessionBytes there (a theme script must not fill the disk). A reused file's
// modification time is refreshed; the first call in a process prunes the folder.
[[nodiscard]] QString theme_cache_png_url(const QImage& image, const QString& prefix);
inline constexpr qint64 kThemeCacheSessionBytes = 64LL * 1024 * 1024;
// Removes the cache folder's PNGs last modified before `cutoff` (another running Patchy
// refreshes the files it reuses, so a week-old one belongs to nobody); the count removed.
int prune_theme_cache(const QDateTime& cutoff);
// Tests: the per-process write cap (kThemeCacheSessionBytes by default) and the bytes
// written so far, reset with the cap.
void set_theme_cache_session_limit_for_testing(qint64 bytes);

}  // namespace patchy::ui
