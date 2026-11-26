#include "ui/recent_thumbnails.hpp"

#include "support/atomic_file_write.hpp"
#include "support/network_path.hpp"
#include "support/path_utils.hpp"
#include "support/startup_trace.hpp"
#include "ui/background_workers.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/network_mounts.hpp"
#include "ui/qt_paths.hpp"
#include "ui/recent_thumbnail_sources.hpp"

#include <QAbstractEventDispatcher>
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
#include <QThread>
#include <QTimer>

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
// "1" was a 256 px picture (Patchy 1.42's first grid). It is still shown for a file that has
// no cheap preview to make a new one from, and replaced as soon as there is one.
constexpr auto kVersionValue = "2";
constexpr auto kOlderVersionValue = "1";
constexpr auto kSourceSizeKey = "PatchySourceSize";
constexpr auto kSourceModifiedKey = "PatchySourceModified";

// One name per path, from the path's text alone (no disk access): the same clean absolute
// form the Home Screen lists.
QString thumbnail_key(const QString& path) {
  if (path.trimmed().isEmpty()) {
    return {};
  }
  return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString cache_base_name(const QString& key) {
  return QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex());
}

QString cache_file_name(const QString& key) {
  return cache_base_name(key) + QStringLiteral(".png");
}

// The "no picture" marker of a path: a few lines of text beside where its PNG would be, saying
// that the file as it was (size, modified time) has no picture, so it is not decoded again at
// every start. Never a second picture.
QString marker_file_name(const QString& key) {
  return cache_base_name(key) + QStringLiteral(".none");
}

// Only names this cache writes are ever deleted from the folder, whatever else is in it:
// PATCHY_RECENT_THUMBNAILS_DIR may point anywhere.
bool is_cache_file_name(const QString& name) {
  static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{40}\\.(png|none)$"));
  return pattern.match(name).hasMatch();
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

// A marker's first line. A build that can picture more kinds, or has other caps, raises the
// number, and every older marker stops counting.
constexpr auto kMarkerVersionLine = "PatchyNoThumbnail 1";

bool marker_matches(const QString& file, const SourceStamp& stamp) {
  QFile marker(file);
  if (!marker.open(QIODevice::ReadOnly)) {
    return false;
  }
  const auto lines = marker.read(256).split('\n');
  return lines.size() >= 3 && lines[0] == kMarkerVersionLine && lines[1] == QByteArray::number(stamp.bytes) &&
         lines[2] == QByteArray::number(stamp.modified_ms);
}

void write_marker(const QString& directory, const QString& file, const SourceStamp& stamp) {
  const QByteArray bytes = QByteArray(kMarkerVersionLine) + '\n' + QByteArray::number(stamp.bytes) + '\n' +
                           QByteArray::number(stamp.modified_ms) + '\n';
  if (!QDir().mkpath(directory)) {
    return;
  }
  try {
    write_file_bytes_atomically(
        to_filesystem_path(file),
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                      static_cast<std::size_t>(bytes.size())),
        "Could not create the thumbnail file", "Could not write the thumbnail file");
  } catch (const std::exception&) {
    // Only a note to skip work: without it the file is looked at again next time.
  }
}

struct CachedPicture {
  QImage image;         // null: no cache file for the file as it is now
  bool current{false};  // written at the current stored size
};

CachedPicture read_cache_file(const QString& file, const SourceStamp& stamp) {
  QImage image;
  if (!QFileInfo::exists(file) || !image.load(file, "PNG")) {
    return {};
  }
  if (image.text(QString::fromLatin1(kSourceSizeKey)) != QString::number(stamp.bytes) ||
      image.text(QString::fromLatin1(kSourceModifiedKey)) != QString::number(stamp.modified_ms)) {
    return {};
  }
  const auto version = image.text(QString::fromLatin1(kVersionKey));
  if (version == QString::fromLatin1(kVersionValue)) {
    return {image, true};
  }
  return version == QString::fromLatin1(kOlderVersionValue) ? CachedPicture{image, false} : CachedPicture{};
}

}  // namespace

QString recent_thumbnails_directory() {
  if (const auto redirected = qEnvironmentVariable("PATCHY_RECENT_THUMBNAILS_DIR"); !redirected.isEmpty()) {
    return QDir(redirected).absolutePath();
  }
  // An isolated run (automation, the startup benchmark, a README capture) has its own recent
  // list in its own settings folder. Its pictures go there too: with the user's folder, the
  // sweep that keeps one picture per recent entry would delete the user's real thumbnails.
  if (const auto settings_root = qEnvironmentVariable("PATCHY_SETTINGS_DIR"); !settings_root.isEmpty()) {
    return QDir(settings_root).absoluteFilePath(QStringLiteral("RTsoft/Patchy/recent-thumbnails"));
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
  constexpr int kCoarseSide = 2 * kRecentThumbnailStoredSide;
  if (composite.isNull() || (composite.width() <= kCoarseSide && composite.height() <= kCoarseSide)) {
    return composite;
  }
  return composite.scaled(kCoarseSide, kCoarseSide, Qt::KeepAspectRatio, Qt::FastTransformation);
}

struct RecentThumbnailCache::Job {
  enum class Kind { load, store, forget, retain };
  Kind kind{Kind::load};
  bool slow{false};   // load: the quick pass found no picture; this is the slow pass
  QString key;        // load, store: the clean absolute path
  QImage image;       // store
  QStringList keys;   // forget, retain
  QSize display_box;  // load, store
};

struct RecentThumbnailCache::Shared {
  std::mutex mutex;
  std::deque<Job> jobs;
  // Loads whose quick pass found nothing (recent_thumbnail_sources.hpp). The worker takes one
  // only while `jobs` is empty, so a file that needs Patchy's own reader never holds a quick
  // picture, a stored picture or a deletion behind it.
  std::deque<Job> slow_jobs;
  bool running{false};
  bool closed{false};  // the owner is gone: only the deletions still matter
  QString directory;
};

namespace {

struct LoadResult {
  QImage image;
  bool available{false};
  bool reduced{false};     // the stored picture is larger than `image`
  bool needs_slow{false};  // no answer yet: the slow pass has to look
};

// Runs on the worker. Returns the picture for the grid, no picture, or (from the quick pass
// only) the word that the slow pass has to look.
LoadResult make_thumbnail(const QString& directory, const QString& key, const QImage& stored_source,
                          QSize display_box, const std::vector<MountEntry>& mounts, RecentThumbnailPass pass) {
  if (directory.isEmpty() || !source_location_allowed(key, mounts)) {
    return {};
  }
  const QFileInfo info(key);
  if (!info.isFile()) {
    return {};
  }
  const SourceStamp stamp{info.size(), info.lastModified().toMSecsSinceEpoch()};
  const QDir folder(directory);
  const auto file = folder.filePath(cache_file_name(key));
  const auto marker = folder.filePath(marker_file_name(key));
  CachedPicture cached;
  if (stored_source.isNull()) {
    cached = read_cache_file(file, stamp);
  }
  QImage image = cached.current ? cached.image : QImage();
  if (image.isNull()) {
    if (!stored_source.isNull()) {
      image = stored_source;
    } else if (!marker_matches(marker, stamp)) {
      auto read = read_recent_thumbnail(key, stamp.bytes, pass);
      switch (read.outcome) {
        case RecentThumbnailOutcome::picture:
          image = std::move(read.image);
          break;
        case RecentThumbnailOutcome::slow:
          if (pass == RecentThumbnailPass::quick) {
            LoadResult later;
            later.needs_slow = true;
            return later;
          }
          break;
        case RecentThumbnailOutcome::none:
          // Unsupported, damaged or past a cap: not looked at again until the file changes.
          write_marker(directory, marker, stamp);
          break;
        case RecentThumbnailOutcome::unreadable:
          break;  // locked or gone just now: the next look tries again
      }
    }
    if (!image.isNull()) {
      image = fitted(display_image(std::move(image)), {kRecentThumbnailStoredSide, kRecentThumbnailStoredSide});
      if (write_cache_file(directory, file, image, stamp)) {  // one file per path: an older picture is replaced
        QFile::remove(marker);
      }
    } else {
      // Nothing to make a new picture from: the older, smaller one Patchy stored for this
      // same file is still its picture.
      image = cached.image;
    }
    if (image.isNull()) {
      return {};
    }
  }
  auto shown = fitted(image, display_box);
  const bool reduced = shown.size() != image.size();
  return {shown.convertToFormat(QImage::Format_ARGB32_Premultiplied), true, reduced, false};
}

void delete_cache_files(const QString& directory, const QStringList& keys) {
  if (directory.isEmpty()) {
    return;
  }
  const QDir folder(directory);
  for (const auto& key : keys) {
    if (!key.isEmpty()) {
      QFile::remove(folder.filePath(cache_file_name(key)));
      QFile::remove(folder.filePath(marker_file_name(key)));
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
    keep.insert(marker_file_name(key));
  }
  for (const auto& name : folder.entryList(QDir::Files)) {
    if (is_cache_file_name(name) && !keep.contains(name)) {
      QFile::remove(folder.filePath(name));
    }
  }
}

}  // namespace

// The folder is not looked up here: the constructor runs while the main window is built, and
// the worker asks for it when its first job starts (recent_thumbnails_directory touches no disk,
// but the known-folder lookup behind it is not free).
RecentThumbnailCache::RecentThumbnailCache(QObject* parent) : QObject(parent), shared_(std::make_shared<Shared>()) {}

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
  entry.last_used = ++clock_;
  // A picture fitted to a smaller box than today's (the window grew) would be drawn enlarged:
  // it is loaded again at the new size, and stays shown until that arrives.
  const bool outgrown = entry.state == RecentThumbnailState::ready && entry.reduced &&
                        (entry.loaded_box.width() < display_box_.width() ||
                         entry.loaded_box.height() < display_box_.height());
  if (entry.state != RecentThumbnailState::unknown && !outgrown) {
    return;
  }
  entry.state = RecentThumbnailState::pending;
  ++requests_made_;
  ++loads_in_flight_;
  if (requests_made_ == 1 && startup_trace::enabled()) {
    first_request_ms_ = startup_trace::now_ms();
  }
  Job job;
  job.kind = Job::Kind::load;
  job.key = key;
  job.display_box = display_box_;
  enqueue(std::move(job));
}

void RecentThumbnailCache::set_display_box(QSize device_pixels) {
  if (device_pixels.isEmpty()) {
    return;
  }
  constexpr int kStep = 48;
  display_box_ = QSize((device_pixels.width() + kStep - 1) / kStep * kStep,
                       (device_pixels.height() + kStep - 1) / kStep * kStep);
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

void RecentThumbnailCache::load_answered() {
  if (loads_in_flight_ > 0 && --loads_in_flight_ == 0 && first_request_ms_ >= 0.0) {
    // Every picture the first paint asked for has arrived (or has none).
    startup_trace::record_background("recent_thumbnails.first_cells", first_request_ms_, startup_trace::now_ms());
    first_request_ms_ = -1.0;
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

void RecentThumbnailCache::clear() {
  entries_.clear();
  retain_only({});  // a deletion job: it runs on the worker, even when the process is quitting
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
  return shared_->running || !shared_->jobs.empty() || !shared_->slow_jobs.empty() || !held_.empty();
}

void RecentThumbnailCache::trim_loaded() {
  qsizetype bytes = 0;
  std::vector<std::pair<quint64, QString>> loaded;
  for (auto entry = entries_.cbegin(); entry != entries_.cend(); ++entry) {
    if (!entry->image.isNull()) {
      bytes += entry->image.sizeInBytes();
      loaded.emplace_back(entry->last_used, entry.key());
    }
  }
  if (bytes <= kRecentThumbnailLoadedBytes || loaded.size() <= static_cast<std::size_t>(kRecentThumbnailLoadedFloor)) {
    return;
  }
  std::sort(loaded.begin(), loaded.end());
  const auto spare = loaded.size() - static_cast<std::size_t>(kRecentThumbnailLoadedFloor);
  for (std::size_t index = 0; index < spare && bytes > kRecentThumbnailLoadedBytes; ++index) {
    auto& entry = entries_[loaded[index].second];
    bytes -= entry.image.sizeInBytes();
    entry.image = QImage();
    if (entry.state == RecentThumbnailState::ready) {
      entry.state = RecentThumbnailState::unknown;  // loads again when its cell is painted
    }
  }
}

void RecentThumbnailCache::finish(const QString& key, bool was_load, QImage image, bool available, bool reduced,
                                  QSize box) {
  if (was_load) {
    load_answered();
  }
  const auto found = entries_.find(key);
  if (found == entries_.end()) {
    return;  // forgotten, or out of the list, while the worker was on it
  }
  if (found->state == RecentThumbnailState::unknown) {
    return;  // refresh_all asked for a newer look, which is queued behind this one
  }
  found->state = available ? RecentThumbnailState::ready : RecentThumbnailState::unavailable;
  found->image = available ? std::move(image) : QImage();
  found->reduced = available && reduced;
  found->loaded_box = box;
  trim_loaded();
  emit thumbnail_changed(key);
}

void RecentThumbnailCache::enqueue(Job job) {
  if (!released_once_ && job.kind == Job::Kind::load) {
    held_.push_back(std::move(job));
    if (held_.size() == 1) {
      constexpr int kHoldLimitMs = 500;
      if (auto* dispatcher = QAbstractEventDispatcher::instance(); dispatcher != nullptr) {
        connect(dispatcher, &QAbstractEventDispatcher::aboutToBlock, this, &RecentThumbnailCache::release_held);
      }
      QTimer::singleShot(kHoldLimitMs, this, &RecentThumbnailCache::release_held);
    }
    return;
  }
  // Anything else (a stored picture, a deletion) keeps its place behind the held loads.
  release_held();
  std::vector<Job> jobs;
  jobs.push_back(std::move(job));
  send_to_worker(std::move(jobs));
}

void RecentThumbnailCache::release_held() {
  if (released_once_) {
    return;
  }
  released_once_ = true;
  if (auto* dispatcher = QAbstractEventDispatcher::instance(); dispatcher != nullptr) {
    disconnect(dispatcher, &QAbstractEventDispatcher::aboutToBlock, this, &RecentThumbnailCache::release_held);
  }
  if (!held_.empty()) {
    send_to_worker(std::exchange(held_, {}));
  }
}

void RecentThumbnailCache::send_to_worker(std::vector<Job> jobs) {
  int dropped_loads = 0;
  bool start_worker = false;
  {
    const std::lock_guard<std::mutex> lock(shared_->mutex);
    for (auto& job : jobs) {
      // A slow load waits behind every deletion, so one whose path a deletion names would
      // write its picture after the deletion ran: it leaves the queue with the deletion's
      // arrival, under the same lock the worker takes to put a load there.
      if (job.kind == Job::Kind::forget || job.kind == Job::Kind::retain) {
        const QSet<QString> named(job.keys.cbegin(), job.keys.cend());
        const bool deletes_named = job.kind == Job::Kind::forget;
        const auto before = shared_->slow_jobs.size();
        std::erase_if(shared_->slow_jobs,
                      [&](const Job& slow) { return named.contains(slow.key) == deletes_named; });
        dropped_loads += static_cast<int>(before - shared_->slow_jobs.size());
      }
      shared_->jobs.push_back(std::move(job));
    }
    start_worker = !shared_->running;
    shared_->running = true;
  }
  for (int index = 0; index < dropped_loads; ++index) {
    load_answered();  // nobody will answer these
  }
  if (!start_worker) {
    return;
  }
  // One worker drains the queue and ends: at most one thumbnail is being made at a time, and
  // nothing is left running while the queue is empty. Tracked, so quit waits for it within the
  // usual bound (docs/code-organization.md); it stops between two files when quit is asked for.
  auto* app = QCoreApplication::instance();
  QPointer<RecentThumbnailCache> owner(this);
  run_tracked_background_worker([shared = shared_, app, owner] {
    // Below the GUI thread's priority: pictures for the grid never compete with startup or
    // with the user's own work for a core.
    if (auto* thread = QThread::currentThread(); thread != nullptr) {
      thread->setPriority(QThread::LowPriority);
    }
    {
      const std::lock_guard<std::mutex> lock(shared->mutex);
      if (shared->directory.isEmpty()) {
        shared->directory = recent_thumbnails_directory();
      }
    }
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
        if (deletions_only) {
          shared->slow_jobs.clear();
        }
        // Every quick answer, stored picture and deletion goes before the next slow load.
        auto& queue = shared->jobs.empty() ? shared->slow_jobs : shared->jobs;
        if (queue.empty()) {
          shared->running = false;
          return;
        }
        job = std::move(queue.front());
        queue.pop_front();
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
          auto result = make_thumbnail(shared->directory, job.key, job.image, job.display_box, mounts,
                                       job.slow ? RecentThumbnailPass::slow : RecentThumbnailPass::quick);
          if (result.needs_slow) {
            // Asked again after everything quick. A deletion queued since this load was taken
            // may name the path, or a quit: then the worker answers "no picture" instead.
            const std::lock_guard<std::mutex> lock(shared->mutex);
            const bool still_wanted =
                !shared->closed && !background_workers_stop_requested() &&
                std::none_of(shared->jobs.cbegin(), shared->jobs.cend(), [&job](const Job& queued) {
                  return (queued.kind == Job::Kind::forget && queued.keys.contains(job.key)) ||
                         (queued.kind == Job::Kind::retain && !queued.keys.contains(job.key));
                });
            if (still_wanted) {
              job.slow = true;
              shared->slow_jobs.push_back(std::move(job));
              break;
            }
            result = {};
          }
          if (app == nullptr) {
            break;
          }
          QMetaObject::invokeMethod(
              app,
              [owner, key = job.key, was_load = job.kind == Job::Kind::load, box = job.display_box,
               result = std::move(result)]() mutable {
                if (owner != nullptr) {
                  owner->finish(key, was_load, std::move(result.image), result.available, result.reduced, box);
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
