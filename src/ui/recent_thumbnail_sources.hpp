#pragma once

// Where the Home Screen's thumbnail worker gets the picture of a recent file
// (docs/ui-conventions-menus.md, "Where a picture comes from"). Everything here runs on that
// worker: nothing touches a widget, asks a question or shows a message, and a damaged file
// gives no picture, never an exception.
//
// Two passes. The quick one reads what the file already carries or what decodes straight to
// a small size: a PSD's embedded thumbnail, the merged image inside an OpenRaster or Krita
// file, Paint.NET's header thumbnail, a camera raw's embedded JPEG, an image Qt's readers
// decode. The slow one opens the file through Patchy's own reader and composites it (GIMP
// .xcf, HEIF, AVIF, JPEG XL, EXR and the other kinds Qt does not read), renders page 1 of a
// PDF or renders an SVG. The worker runs every queued quick pass before any slow one.

#include <QImage>
#include <QString>

namespace patchy::ui {

// A file larger than this is never read whole to find the preview it embeds (an OpenRaster or
// Krita zip, a camera raw).
inline constexpr qint64 kRecentThumbnailMaxPreviewHostBytes = 256LL * 1024 * 1024;
// A file larger than this is never opened through a document reader (the slow pass).
inline constexpr qint64 kRecentThumbnailMaxDocumentBytes = 96LL * 1024 * 1024;
// An SVG larger than this is never parsed.
inline constexpr qint64 kRecentThumbnailMaxSvgBytes = 16LL * 1024 * 1024;
// The slow pass composites no canvas with more pixels than this. Where the header says so
// (XCF, Paint.NET, PSD) the file is refused before anything is decoded.
inline constexpr qint64 kRecentThumbnailMaxDocumentPixels = 40LL * 1000 * 1000;
// The layers of a document, decoded, may not need more than this (XCF and Paint.NET, from
// their headers), and a decoded canvas may not either (16 and 32-bit flat images).
inline constexpr qint64 kRecentThumbnailMaxDecodedBytes = 384LL * 1024 * 1024;

enum class RecentThumbnailPass { quick, slow };

enum class RecentThumbnailOutcome {
  picture,     // `image` holds it
  none,        // the file as it is now has no picture: unsupported, damaged or past a cap
  slow,        // the quick pass found nothing; the slow pass may
  unreadable,  // the file could not be read just now (locked, gone, quit asked for): try again later
};

struct RecentThumbnailRead {
  QImage image;  // any size; a float image holds linear values (the caller display-encodes it)
  RecentThumbnailOutcome outcome{RecentThumbnailOutcome::none};
};

// `source_bytes` is the file's size as the caller stat'ed it. The caller has already checked
// that `path` is a local file (never a network location).
[[nodiscard]] RecentThumbnailRead read_recent_thumbnail(const QString& path, qint64 source_bytes,
                                                        RecentThumbnailPass pass);

// Tests: how many times a document reader or renderer (the slow pass's work) has started, and
// how many files either pass has opened.
[[nodiscard]] int recent_thumbnail_document_reads_for_testing() noexcept;
[[nodiscard]] int recent_thumbnail_file_opens_for_testing() noexcept;

}  // namespace patchy::ui
