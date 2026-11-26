#pragma once

// Thumbnails for the Assets panel's grid (GitHub issue 751; docs/assets-panel.md, "Loading and
// thumbnails"). The picture of an asset is the PNG beside its PSD; one that is missing is
// rendered from the asset's layers and stored again. All of it happens on one background
// worker: the grid paints a placeholder until a picture arrives.
//
// Rules this file keeps:
// - Nothing here reads, decodes or renders on the UI thread, and nothing runs per repaint: the
//   grid asks for an asset once, and every later paint is a hash lookup.
// - Only assets the grid has painted are asked for, so an asset never scrolled into view is
//   never decoded.
// - The pictures kept in memory are bounded (kAssetThumbnailMemoryBytes): the least recently
//   painted ones are dropped and decoded again if they are scrolled back to.

#include "core/document.hpp"

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <list>
#include <memory>
#include <optional>

namespace patchy::ui {

// The stored thumbnail's longest side: the grid shows it at 64 px, doubled on dense screens,
// with room for a later larger grid.
inline constexpr int kAssetThumbnailStoredSide = 192;
// The grid's picture box, in logical pixels.
inline constexpr int kAssetThumbnailDisplaySide = 64;
// What the decoded pictures of one panel may hold in memory before the least recently painted
// ones are dropped.
inline constexpr qint64 kAssetThumbnailMemoryBytes = 32LL * 1024 * 1024;
// Requests waiting for the worker beyond this many are dropped, oldest first (a fast scroll
// through a large library); a dropped asset is asked for again when it is next painted.
inline constexpr int kAssetThumbnailQueueLimit = 256;

// The asset PSD's bytes as a document; nullopt when they are not a PSD Patchy reads. Pure: safe
// on a worker.
[[nodiscard]] std::optional<Document> read_asset_document(const QByteArray& psd_bytes);
// The asset's layers alone on a transparent ground, the roots shown whatever their eyes say, at
// most kAssetThumbnailStoredSide a side. Pure: safe on a worker.
[[nodiscard]] QImage render_asset_thumbnail(const Document& asset);
// The name of the layer an asset file holds (its topmost root); empty when the file cannot be
// read. Names the assets of a library rebuilt from its files. Pure: safe on a worker.
[[nodiscard]] QString asset_file_layer_name(const QString& asset_path);

enum class AssetThumbnailState {
  unknown,      // never asked for, or dropped to bound the memory or the queue
  pending,      // queued or being made
  ready,        // image() returns it
  unavailable,  // the asset's file is missing or unreadable
};

class AssetThumbnailCache final : public QObject {
  Q_OBJECT

 public:
  // `directory` is the library's folder; the cache never looks anywhere else.
  explicit AssetThumbnailCache(QString directory, QObject* parent = nullptr);
  ~AssetThumbnailCache() override;

  [[nodiscard]] AssetThumbnailState state(const QString& asset_id) const;
  // The decoded picture, or null. Marks the asset as recently used.
  [[nodiscard]] const QImage* image(const QString& asset_id);
  // Queues the load of an asset whose state is unknown; anything else is a no-op, so a delegate
  // may call it from every paint.
  void request(const QString& asset_id);
  // The side, in device pixels, a picture is scaled down to before it is kept in memory.
  void set_display_side(int device_pixels);

  // A picture the caller already has (an asset just added): shown at once, nothing is read.
  void insert(const QString& asset_id, const QImage& stored_thumbnail);
  // Drops what is kept for assets that left the library.
  void forget(const QStringList& asset_ids);

  // True while a picture is queued or being made (tests wait on it).
  [[nodiscard]] bool busy() const;
  // Every asset asked for since the cache was made, in order (tests: only painted assets are).
  [[nodiscard]] const QStringList& requested_ids() const noexcept { return requested_; }
  // Bytes held by the decoded pictures now.
  [[nodiscard]] qint64 memory_bytes() const noexcept { return memory_bytes_; }

 signals:
  void thumbnail_changed(const QString& asset_id);

 private:
  struct Entry {
    AssetThumbnailState state{AssetThumbnailState::unknown};
    QImage image;
    std::list<QString>::iterator recent;  // valid while the image is not null
  };
  struct Shared;

  void finish(const QString& asset_id, QImage image, bool available);
  void keep(const QString& asset_id, Entry& entry, QImage image);
  void drop_image(Entry& entry);
  void trim_memory(const QString& keep_id);

  QString directory_;
  QHash<QString, Entry> entries_;
  std::list<QString> recent_;  // most recently painted first
  qint64 memory_bytes_{0};
  int display_side_{kAssetThumbnailDisplaySide};
  QStringList requested_;
  std::shared_ptr<Shared> shared_;
};

}  // namespace patchy::ui
