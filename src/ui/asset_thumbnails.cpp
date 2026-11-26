#include "ui/asset_thumbnails.hpp"

#include "psd/psd_document_io.hpp"
#include "ui/asset_library.hpp"
#include "ui/background_workers.hpp"
#include "ui/image_document_io.hpp"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QSaveFile>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <span>
#include <utility>

namespace patchy::ui {

namespace {

// How long a cache being destroyed waits for the picture its worker is on: the worker holds
// a file of the folder open, and whoever destroys the panel may be about to delete the folder.
constexpr auto kCloseWait = std::chrono::seconds(10);
// requested_ids() keeps at most this many entries (a diagnostic, not a history).
constexpr qsizetype kRequestedIdsLimit = 4096;

QByteArray read_file(const QString& path) {
  count_asset_file_access();
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QImage fitted(const QImage& image, int side) {
  if (image.isNull() || side <= 0 || (image.width() <= side && image.height() <= side)) {
    return image;
  }
  return image.scaled(QSize(side, side), Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

struct Loaded {
  QImage image;
  bool available{false};
};

// Runs on the worker: the stored picture, or one rendered from the asset's layers and stored
// again when the PNG is gone.
Loaded load_thumbnail(const QString& directory, const QString& asset_id, int display_side) {
  const QDir folder(directory);
  const auto png_path = folder.filePath(asset_id + QStringLiteral(".png"));
  QImage stored;
  count_asset_file_access();
  if (QFileInfo::exists(png_path)) {
    stored.load(png_path, "PNG");
  }
  if (stored.isNull()) {
    const auto document = read_asset_document(read_file(folder.filePath(asset_id + QStringLiteral(".psd"))));
    if (!document.has_value()) {
      return {};
    }
    stored = render_asset_thumbnail(*document);
    if (stored.isNull()) {
      return {};
    }
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (buffer.open(QIODevice::WriteOnly) && stored.save(&buffer, "PNG")) {
      // A cache: a failed write is made again the next time the picture is needed.
      QSaveFile file(png_path);
      if (file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size()) {
        file.commit();
      }
    }
  }
  return {fitted(stored, display_side).convertToFormat(QImage::Format_ARGB32_Premultiplied), true};
}

}  // namespace

std::optional<Document> read_asset_document(const QByteArray& psd_bytes) {
  if (psd_bytes.isEmpty()) {
    return std::nullopt;
  }
  try {
    return psd::DocumentIo::read(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(psd_bytes.constData()),
                                      static_cast<std::size_t>(psd_bytes.size())));
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

QImage render_asset_thumbnail(const Document& asset) {
  if (asset.width() <= 0 || asset.height() <= 0 || asset.layers().empty()) {
    return {};
  }
  Document shown = asset;
  for (auto& root : shown.layers()) {
    root.set_visible(true);
    root.set_clipped(false);
  }
  return fitted(qimage_from_document(std::as_const(shown), true), kAssetThumbnailStoredSide);
}

QString asset_file_layer_name(const QString& asset_path) {
  const auto document = read_asset_document(read_file(asset_path));
  if (!document.has_value() || document->layers().empty()) {
    return {};
  }
  return QString::fromStdString(document->layers().back().name());
}

struct AssetThumbnailCache::Shared {
  struct Job {
    QString asset_id;
    int display_side{kAssetThumbnailDisplaySide};
  };
  std::mutex mutex;
  std::condition_variable idle;
  std::deque<Job> jobs;
  bool running{false};
  bool closed{false};  // the owner is gone: nothing more is made
  QString directory;
};

AssetThumbnailCache::AssetThumbnailCache(QString directory, QObject* parent)
    : QObject(parent), directory_(std::move(directory)), shared_(std::make_shared<Shared>()) {
  shared_->directory = directory_;
}

AssetThumbnailCache::~AssetThumbnailCache() {
  std::unique_lock<std::mutex> lock(shared_->mutex);
  shared_->closed = true;
  shared_->jobs.clear();
  // The worker ends after the picture it is on. Waiting for it here keeps a destroyed panel
  // from holding a file of the folder open (a test, or an uninstall, may delete it next).
  shared_->idle.wait_for(lock, kCloseWait, [this] { return !shared_->running; });
}

AssetThumbnailState AssetThumbnailCache::state(const QString& asset_id) const {
  const auto found = entries_.constFind(asset_id);
  return found == entries_.constEnd() ? AssetThumbnailState::unknown : found->state;
}

const QImage* AssetThumbnailCache::image(const QString& asset_id) {
  const auto found = entries_.find(asset_id);
  if (found == entries_.end() || found->image.isNull()) {
    return nullptr;
  }
  recent_.splice(recent_.begin(), recent_, found->recent);
  return &found->image;
}

void AssetThumbnailCache::set_display_side(int device_pixels) {
  if (device_pixels > 0) {
    display_side_ = device_pixels;
  }
}

void AssetThumbnailCache::request(const QString& asset_id) {
  if (!AssetLibrary::is_asset_id(asset_id) || directory_.isEmpty()) {
    return;
  }
  auto& entry = entries_[asset_id];
  if (entry.state != AssetThumbnailState::unknown) {
    return;
  }
  entry.state = AssetThumbnailState::pending;
  requested_.push_back(asset_id);
  if (requested_.size() > kRequestedIdsLimit) {
    requested_.remove(0, kRequestedIdsLimit / 2);
  }
  QStringList dropped;
  {
    const std::lock_guard<std::mutex> lock(shared_->mutex);
    shared_->jobs.push_back(Shared::Job{asset_id, display_side_});
    // A fast scroll through a large library: the oldest requests are for assets no longer in
    // view. They go back to unknown and are asked for again if they are painted again.
    while (static_cast<int>(shared_->jobs.size()) > kAssetThumbnailQueueLimit) {
      dropped.push_back(shared_->jobs.front().asset_id);
      shared_->jobs.pop_front();
    }
    if (shared_->running) {
      for (const auto& id : dropped) {
        if (const auto found = entries_.find(id); found != entries_.end()) {
          found->state = AssetThumbnailState::unknown;
        }
      }
      return;
    }
    shared_->running = true;
  }
  // One worker drains the queue and ends, as the Home Screen's thumbnails do
  // (recent_thumbnails.cpp): one picture at a time, nothing running while the queue is empty.
  // Tracked, so quit waits for it within the usual bound; it stops between two pictures once
  // quit is asked for.
  auto* app = QCoreApplication::instance();
  QPointer<AssetThumbnailCache> owner(this);
  run_tracked_background_worker([shared = shared_, app, owner] {
    for (;;) {
      Shared::Job job;
      {
        const std::lock_guard<std::mutex> lock(shared->mutex);
        if (shared->closed || background_workers_stop_requested()) {
          shared->jobs.clear();
        }
        if (shared->jobs.empty()) {
          shared->running = false;
          shared->idle.notify_all();
          return;
        }
        job = std::move(shared->jobs.front());
        shared->jobs.pop_front();
      }
      auto loaded = load_thumbnail(shared->directory, job.asset_id, job.display_side);
      if (app == nullptr) {
        continue;
      }
      QMetaObject::invokeMethod(
          app,
          [owner, asset_id = job.asset_id, loaded = std::move(loaded)]() mutable {
            if (owner != nullptr) {
              owner->finish(asset_id, std::move(loaded.image), loaded.available);
            }
          },
          Qt::QueuedConnection);
    }
  });
}

void AssetThumbnailCache::insert(const QString& asset_id, const QImage& stored_thumbnail) {
  if (!AssetLibrary::is_asset_id(asset_id) || stored_thumbnail.isNull()) {
    return;
  }
  auto& entry = entries_[asset_id];
  entry.state = AssetThumbnailState::ready;
  keep(asset_id, entry,
       fitted(stored_thumbnail, display_side_).convertToFormat(QImage::Format_ARGB32_Premultiplied));
  trim_memory(asset_id);
  emit thumbnail_changed(asset_id);
}

void AssetThumbnailCache::forget(const QStringList& asset_ids) {
  for (const auto& id : asset_ids) {
    if (const auto found = entries_.find(id); found != entries_.end()) {
      drop_image(*found);
      entries_.erase(found);
    }
  }
}

bool AssetThumbnailCache::busy() const {
  const std::lock_guard<std::mutex> lock(shared_->mutex);
  return shared_->running || !shared_->jobs.empty();
}

void AssetThumbnailCache::keep(const QString& asset_id, Entry& entry, QImage image) {
  drop_image(entry);
  if (image.isNull()) {
    return;
  }
  entry.image = std::move(image);
  memory_bytes_ += entry.image.sizeInBytes();
  recent_.push_front(asset_id);
  entry.recent = recent_.begin();
}

void AssetThumbnailCache::drop_image(Entry& entry) {
  if (entry.image.isNull()) {
    return;
  }
  memory_bytes_ -= entry.image.sizeInBytes();
  recent_.erase(entry.recent);
  entry.image = QImage();
}

void AssetThumbnailCache::trim_memory(const QString& keep_id) {
  while (memory_bytes_ > kAssetThumbnailMemoryBytes && !recent_.empty() && recent_.back() != keep_id) {
    const auto found = entries_.find(recent_.back());
    if (found == entries_.end()) {
      recent_.pop_back();
      continue;
    }
    drop_image(*found);
    found->state = AssetThumbnailState::unknown;  // decoded again if it is painted again
  }
}

void AssetThumbnailCache::finish(const QString& asset_id, QImage image, bool available) {
  const auto found = entries_.find(asset_id);
  if (found == entries_.end() || found->state != AssetThumbnailState::pending) {
    return;  // deleted, or already answered another way, while the worker was on it
  }
  found->state = available ? AssetThumbnailState::ready : AssetThumbnailState::unavailable;
  if (available) {
    keep(asset_id, *found, std::move(image));
    trim_memory(asset_id);
  }
  emit thumbnail_changed(asset_id);
}

}  // namespace patchy::ui
