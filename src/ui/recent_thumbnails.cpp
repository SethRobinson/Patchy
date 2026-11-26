#include "ui/recent_thumbnails.hpp"

#include "formats/metadata/iptc_iim.hpp"
#include "support/atomic_file_write.hpp"
#include "support/network_path.hpp"
#include "support/path_utils.hpp"
#include "ui/background_workers.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/network_mounts.hpp"
#include "ui/qt_paths.hpp"

#include <QBuffer>
#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QMetaObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <exception>
#include <iterator>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

// Text keys of a cache file. The thumbnail belongs to the file as it was when its size and
// modified time were these; a file that changed gets a new picture. The path is not written.
constexpr auto kVersionKey = "PatchyThumbnail";
constexpr auto kVersionValue = "1";
constexpr auto kSourceSizeKey = "PatchySourceSize";
constexpr auto kSourceModifiedKey = "PatchySourceModified";

// A PSD keeps its sections in order: header, color mode data, image resources. The embedded
// thumbnail is an image resource, so these two caps bound what is read to find it.
constexpr qint64 kPsdHeaderBytes = 26;
constexpr quint32 kMaxPsdColorModeBytes = 16U * 1024U * 1024U;
constexpr quint32 kMaxPsdResourceBytes = 16U * 1024U * 1024U;

// One name per path, from the path's text alone (no disk access): the same clean absolute
// form the Home Screen lists.
QString thumbnail_key(const QString& path) {
  if (path.trimmed().isEmpty()) {
    return {};
  }
  return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString cache_file_name(const QString& key) {
  return QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex()) +
         QStringLiteral(".png");
}

// Only names this cache writes are ever deleted from the folder, whatever else is in it:
// PATCHY_RECENT_THUMBNAILS_DIR may point anywhere.
bool is_cache_file_name(const QString& name) {
  static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{40}\\.png$"));
  return pattern.match(name).hasMatch();
}

quint32 big_endian_32(const QByteArray& bytes, qsizetype offset) {
  const auto* data = reinterpret_cast<const unsigned char*>(bytes.constData()) + offset;
  return (static_cast<quint32>(data[0]) << 24) | (static_cast<quint32>(data[1]) << 16) |
         (static_cast<quint32>(data[2]) << 8) | static_cast<quint32>(data[3]);
}

// The JPEG thumbnail a PSD or PSB carries in its image resources (resource 1036), read from the
// front of the file without decoding a layer or the composite. Null for any other file.
QImage psd_embedded_thumbnail(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  const auto head = file.read(kPsdHeaderBytes + 4);
  if (head.size() < kPsdHeaderBytes + 4 || !head.startsWith("8BPS")) {
    return {};
  }
  const auto color_mode_bytes = big_endian_32(head, kPsdHeaderBytes);
  if (color_mode_bytes > kMaxPsdColorModeBytes || !file.seek(kPsdHeaderBytes + 4 + color_mode_bytes)) {
    return {};
  }
  const auto length_field = file.read(4);
  if (length_field.size() < 4) {
    return {};
  }
  const auto resource_bytes = big_endian_32(length_field, 0);
  if (resource_bytes == 0 || resource_bytes > kMaxPsdResourceBytes) {
    return {};
  }
  const auto resources = file.read(resource_bytes);
  if (resources.size() != static_cast<qsizetype>(resource_bytes)) {
    return {};
  }
  const std::span<const std::uint8_t> span(reinterpret_cast<const std::uint8_t*>(resources.constData()),
                                           static_cast<std::size_t>(resources.size()));
  for (const auto& resource : metadata::parse_irb(span)) {
    if (resource.id != metadata::psd_resource::kThumbnail) {
      continue;
    }
    const auto jpeg = metadata::psd_thumbnail_jpeg(resource.data);
    QImage image;
    if (!jpeg.empty() && image.loadFromData(jpeg.data(), static_cast<int>(jpeg.size()), "JPG")) {
      return image;
    }
  }
  return {};
}

// A decoded image as an 8-bit picture a painter can draw. A float image holds linear values
// (docs/high-bit-depth.md): it goes through the display encoder first, never through a Qt
// 8-bit conversion.
QImage display_image(QImage image) {
  switch (image.format()) {
    case QImage::Format_RGBX16FPx4:
    case QImage::Format_RGBA16FPx4:
    case QImage::Format_RGBA16FPx4_Premultiplied:
    case QImage::Format_RGBX32FPx4:
    case QImage::Format_RGBA32FPx4_Premultiplied:
      image = image.convertToFormat(QImage::Format_RGBA32FPx4);
      break;
    default:
      break;
  }
  image = display_image_from_deep_qimage(image, 0.0F);
  return image.convertToFormat(QImage::Format_RGBA8888);
}

QImage fitted(const QImage& image, QSize box) {
  if (image.isNull() || box.isEmpty() || (image.width() <= box.width() && image.height() <= box.height())) {
    return image;
  }
  return image.scaled(box, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

// The cheap previews: a PSD's embedded thumbnail, or an image Qt's readers decode (JPEG straight
// to the reduced size). Every other kind of file, and anything past the caps, has none until
// Patchy opens or saves it and stores the document's own picture.
QImage decoded_thumbnail(const QString& path, qint64 source_bytes) {
  if (source_bytes <= 0 || source_bytes > kRecentThumbnailMaxSourceBytes) {
    return {};
  }
  if (auto embedded = psd_embedded_thumbnail(path); !embedded.isNull()) {
    return embedded;
  }
  QImageReader reader(path);
  reader.setDecideFormatFromContent(true);
  if (!reader.canRead()) {
    return {};
  }
  const auto format = reader.format().toLower();
  // Vector and page formats are rendered, not decoded: left to the stored document picture.
  if (format == "svg" || format == "svgz" || format == "pdf") {
    return {};
  }
  const auto full = reader.size();
  if (!full.isValid() || full.isEmpty()) {
    return {};
  }
  const bool decodes_reduced = format == "jpeg" || format == "jpg";
  if (!decodes_reduced && static_cast<qint64>(full.width()) * full.height() > kRecentThumbnailMaxSourcePixels) {
    return {};
  }
  reader.setAutoTransform(true);
  if (full.width() > kRecentThumbnailStoredSide || full.height() > kRecentThumbnailStoredSide) {
    reader.setScaledSize(
        full.scaled(kRecentThumbnailStoredSide, kRecentThumbnailStoredSide, Qt::KeepAspectRatio).expandedTo({1, 1}));
  }
  return reader.read();
}

struct SourceStamp {
  qint64 bytes{0};
  qint64 modified_ms{0};
};

// Whether a thumbnail may be made for `path` at all: a network or device location never is.
bool source_location_allowed(const QString& path, const std::vector<MountEntry>& mounts) {
  if (path_is_on_network_location(path, mounts) ||
      path_is_on_network_location(QDir::toNativeSeparators(path), mounts)) {
    return false;
  }
  return classify_path_location(path_to_utf8(to_filesystem_path(path))).kind == PathLocationKind::Local;
}

bool write_cache_file(const QString& directory, const QString& file, const QImage& image, const SourceStamp& stamp) {
  QByteArray bytes;
  {
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly)) {
      return false;
    }
    QImageWriter writer(&buffer, "png");
    writer.setText(QString::fromLatin1(kVersionKey), QString::fromLatin1(kVersionValue));
    writer.setText(QString::fromLatin1(kSourceSizeKey), QString::number(stamp.bytes));
    writer.setText(QString::fromLatin1(kSourceModifiedKey), QString::number(stamp.modified_ms));
    if (!writer.write(image)) {
      return false;
    }
  }
  if (!QDir().mkpath(directory)) {
    return false;
  }
  try {
    write_file_bytes_atomically(
        to_filesystem_path(file),
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                      static_cast<std::size_t>(bytes.size())),
        "Could not create the thumbnail file", "Could not write the thumbnail file");
  } catch (const std::exception&) {
    return false;  // a cache: the grid shows a placeholder and tries again next time
  }
  return true;
}

QImage read_cache_file(const QString& file, const SourceStamp& stamp) {
  QImage image;
  if (!QFileInfo::exists(file) || !image.load(file, "PNG")) {
    return {};
  }
  if (image.text(QString::fromLatin1(kVersionKey)) != QString::fromLatin1(kVersionValue) ||
      image.text(QString::fromLatin1(kSourceSizeKey)) != QString::number(stamp.bytes) ||
      image.text(QString::fromLatin1(kSourceModifiedKey)) != QString::number(stamp.modified_ms)) {
    return {};
  }
  return image;
}

}  // namespace

QString recent_thumbnails_directory() {
  if (const auto redirected = qEnvironmentVariable("PATCHY_RECENT_THUMBNAILS_DIR"); !redirected.isEmpty()) {
    return QDir(redirected).absolutePath();
  }
  const auto root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  return root.isEmpty() ? QString() : QDir(root).filePath(QStringLiteral("recent-thumbnails"));
}

QString recent_thumbnail_cache_file(const QString& path) {
  const auto key = thumbnail_key(path);
  const auto directory = recent_thumbnails_directory();
  if (key.isEmpty() || directory.isEmpty()) {
    return {};
  }
  return QDir(directory).filePath(cache_file_name(key));
}

QImage recent_thumbnail_source(const QImage& composite) {
  constexpr int kCoarseSide = 4 * kRecentThumbnailStoredSide;
  if (composite.isNull() || (composite.width() <= kCoarseSide && composite.height() <= kCoarseSide)) {
    return composite;
  }
  return composite.scaled(kCoarseSide, kCoarseSide, Qt::KeepAspectRatio, Qt::FastTransformation);
}

struct RecentThumbnailCache::Job {
  enum class Kind { load, store, forget, retain };
  Kind kind{Kind::load};
  QString key;        // load, store: the clean absolute path
  QImage image;       // store
  QStringList keys;   // forget, retain
  QSize display_box;  // load, store
};

struct RecentThumbnailCache::Shared {
  std::mutex mutex;
  std::deque<Job> jobs;
  bool running{false};
  bool closed{false};  // the owner is gone: only the deletions still matter
  QString directory;
};

namespace {

struct LoadResult {
  QImage image;
  bool available{false};
};

// Runs on the worker. Returns the picture for the grid, or no picture.
LoadResult make_thumbnail(const QString& directory, const QString& key, const QImage& stored_source,
                          QSize display_box, const std::vector<MountEntry>& mounts) {
  if (directory.isEmpty() || !source_location_allowed(key, mounts)) {
    return {};
  }
  const QFileInfo info(key);
  if (!info.isFile()) {
    return {};
  }
  const SourceStamp stamp{info.size(), info.lastModified().toMSecsSinceEpoch()};
  const auto file = QDir(directory).filePath(cache_file_name(key));
  QImage image;
  if (stored_source.isNull()) {
    image = read_cache_file(file, stamp);
  }
  if (image.isNull()) {
    image = stored_source.isNull() ? decoded_thumbnail(key, stamp.bytes) : stored_source;
    if (image.isNull()) {
      return {};
    }
    image = fitted(display_image(std::move(image)), {kRecentThumbnailStoredSide, kRecentThumbnailStoredSide});
    write_cache_file(directory, file, image, stamp);
  }
  return {fitted(image, display_box).convertToFormat(QImage::Format_ARGB32_Premultiplied), true};
}

void delete_cache_files(const QString& directory, const QStringList& keys) {
  if (directory.isEmpty()) {
    return;
  }
  const QDir folder(directory);
  for (const auto& key : keys) {
    if (!key.isEmpty()) {
      QFile::remove(folder.filePath(cache_file_name(key)));
    }
  }
}

void delete_cache_files_except(const QString& directory, const QStringList& keys) {
  const QDir folder(directory);
  if (directory.isEmpty() || !folder.exists()) {
    return;
  }
  QSet<QString> keep;
  for (const auto& key : keys) {
    keep.insert(cache_file_name(key));
  }
  for (const auto& name : folder.entryList(QDir::Files)) {
    if (is_cache_file_name(name) && !keep.contains(name)) {
      QFile::remove(folder.filePath(name));
    }
  }
}

}  // namespace

RecentThumbnailCache::RecentThumbnailCache(QObject* parent) : QObject(parent), shared_(std::make_shared<Shared>()) {
  shared_->directory = recent_thumbnails_directory();
}

RecentThumbnailCache::~RecentThumbnailCache() {
  const std::lock_guard<std::mutex> lock(shared_->mutex);
  shared_->closed = true;
}

RecentThumbnailState RecentThumbnailCache::state(const QString& path) const {
  const auto found = entries_.constFind(thumbnail_key(path));
  return found == entries_.constEnd() ? RecentThumbnailState::unknown : found->state;
}

const QImage* RecentThumbnailCache::image(const QString& path) const {
  const auto found = entries_.constFind(thumbnail_key(path));
  return found == entries_.constEnd() || found->image.isNull() ? nullptr : &found->image;
}

void RecentThumbnailCache::request(const QString& path) {
  const auto key = thumbnail_key(path);
  if (key.isEmpty()) {
    return;
  }
  auto& entry = entries_[key];
  if (entry.state != RecentThumbnailState::unknown) {
    return;
  }
  entry.state = RecentThumbnailState::pending;
  Job job;
  job.kind = Job::Kind::load;
  job.key = key;
  job.display_box = display_box_;
  enqueue(std::move(job));
}

void RecentThumbnailCache::set_display_box(QSize device_pixels) {
  if (!device_pixels.isEmpty()) {
    display_box_ = device_pixels;
  }
}

void RecentThumbnailCache::store(const QString& path, const QImage& composite) {
  const auto key = thumbnail_key(path);
  if (key.isEmpty() || composite.isNull()) {
    return;
  }
  entries_[key].state = RecentThumbnailState::pending;
  Job job;
  job.kind = Job::Kind::store;
  job.key = key;
  job.image = composite;
  job.display_box = display_box_;
  enqueue(std::move(job));
}

void RecentThumbnailCache::forget(const QStringList& paths) {
  Job job;
  job.kind = Job::Kind::forget;
  for (const auto& path : paths) {
    const auto key = thumbnail_key(path);
    if (key.isEmpty()) {
      continue;
    }
    entries_.remove(key);
    job.keys << key;
  }
  if (!job.keys.isEmpty()) {
    enqueue(std::move(job));
  }
}

void RecentThumbnailCache::retain_only(const QStringList& paths) {
  Job job;
  job.kind = Job::Kind::retain;
  for (const auto& path : paths) {
    if (const auto key = thumbnail_key(path); !key.isEmpty()) {
      job.keys << key;
    }
  }
  enqueue(std::move(job));
}

void RecentThumbnailCache::keep_loaded_only(const QStringList& paths) {
  if (entries_.isEmpty()) {
    return;
  }
  QSet<QString> keep;
  for (const auto& path : paths) {
    keep.insert(thumbnail_key(path));
  }
  for (auto entry = entries_.begin(); entry != entries_.end();) {
    entry = keep.contains(entry.key()) ? std::next(entry) : entries_.erase(entry);
  }
}

void RecentThumbnailCache::refresh_all() {
  for (auto& entry : entries_) {
    if (entry.state == RecentThumbnailState::ready || entry.state == RecentThumbnailState::unavailable) {
      entry.state = RecentThumbnailState::unknown;  // the image stays until the answer comes
    }
  }
}

bool RecentThumbnailCache::busy() const {
  const std::lock_guard<std::mutex> lock(shared_->mutex);
  return shared_->running || !shared_->jobs.empty();
}

void RecentThumbnailCache::finish(const QString& key, QImage image, bool available) {
  const auto found = entries_.find(key);
  if (found == entries_.end()) {
    return;  // forgotten, or out of the list, while the worker was on it
  }
  if (found->state == RecentThumbnailState::unknown) {
    return;  // refresh_all asked for a newer look, which is queued behind this one
  }
  found->state = available ? RecentThumbnailState::ready : RecentThumbnailState::unavailable;
  found->image = available ? std::move(image) : QImage();
  emit thumbnail_changed(key);
}

void RecentThumbnailCache::enqueue(Job job) {
  {
    const std::lock_guard<std::mutex> lock(shared_->mutex);
    shared_->jobs.push_back(std::move(job));
    if (shared_->running) {
      return;
    }
    shared_->running = true;
  }
  // One worker drains the queue and ends: at most one thumbnail is being made at a time, and
  // nothing is left running while the queue is empty. Tracked, so quit waits for it within the
  // usual bound (docs/code-organization.md); it stops between two files when quit is asked for.
  auto* app = QCoreApplication::instance();
  QPointer<RecentThumbnailCache> owner(this);
  run_tracked_background_worker([shared = shared_, app, owner] {
    const auto mounts = read_system_mounts();  // cached kernel data; contacts nothing
    for (;;) {
      Job job;
      {
        const std::lock_guard<std::mutex> lock(shared->mutex);
        // A quitting process, or a cache whose owner is gone, makes no more pictures. The
        // deletions still run: a removed entry's thumbnail must not stay on disk.
        const bool deletions_only = shared->closed || background_workers_stop_requested();
        while (deletions_only && !shared->jobs.empty() &&
               (shared->jobs.front().kind == Job::Kind::load || shared->jobs.front().kind == Job::Kind::store)) {
          shared->jobs.pop_front();
        }
        if (shared->jobs.empty()) {
          shared->running = false;
          return;
        }
        job = std::move(shared->jobs.front());
        shared->jobs.pop_front();
      }
      switch (job.kind) {
        case Job::Kind::forget:
          delete_cache_files(shared->directory, job.keys);
          break;
        case Job::Kind::retain:
          delete_cache_files_except(shared->directory, job.keys);
          break;
        case Job::Kind::load:
        case Job::Kind::store: {
          auto result = make_thumbnail(shared->directory, job.key, job.image, job.display_box, mounts);
          if (app == nullptr) {
            break;
          }
          QMetaObject::invokeMethod(
              app,
              [owner, key = job.key, result = std::move(result)]() mutable {
                if (owner != nullptr) {
                  owner->finish(key, std::move(result.image), result.available);
                }
              },
              Qt::QueuedConnection);
          break;
        }
      }
    }
  });
}

}  // namespace patchy::ui
