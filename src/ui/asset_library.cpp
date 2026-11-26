#include "ui/asset_library.hpp"

#include "ui/background_workers.hpp"

#include <QBuffer>
#include <QCollator>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QMimeData>
#include <QPointer>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QThread>
#include <QUuid>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <utility>

namespace patchy::ui {

namespace {

// The index file's own tokens: permanent (a file format).
constexpr auto kIndexFileName = "index.json";
constexpr auto kIndexFormat = "patchy-assets";
constexpr int kIndexVersion = 1;
constexpr int kMaximumNameLength = 120;
// Far above any real library; bounds what a damaged index can make the panel build.
constexpr int kMaximumIndexBytes = 16 * 1024 * 1024;
// How long a library being destroyed waits for the job its worker is on: the worker holds a
// file of the folder open, and whoever destroys the window may be about to delete the folder.
constexpr auto kCloseWait = std::chrono::seconds(10);

std::atomic<int> g_ui_thread_accesses{0};
std::atomic<int> g_worker_accesses{0};

// A PSD or PSB starts with this signature: enough to leave a foreign or truncated file out
// without decoding it.
bool file_has_psd_signature(const QString& path) {
  count_asset_file_access();
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return false;
  }
  return file.read(4) == QByteArrayLiteral("8BPS");
}

bool write_file_atomically(const QString& path, const QByteArray& bytes, QString* error) {
  count_asset_file_access();
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
    if (error != nullptr) {
      *error = QCoreApplication::translate("patchy::ui::AssetLibrary", "Could not write %1: %2")
                   .arg(QDir::toNativeSeparators(path), file.errorString());
    }
    return false;
  }
  return true;
}

QByteArray png_bytes(const QImage& image) {
  QByteArray bytes;
  QBuffer buffer(&bytes);
  if (image.isNull() || !buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
    return {};
  }
  return bytes;
}

QString new_id() { return QUuid::createUuid().toString(QUuid::Id128); }

QString index_file(const QString& directory) {
  return directory.isEmpty() ? QString() : QDir(directory).filePath(QLatin1String(kIndexFileName));
}

QString asset_file(const QString& directory, const QString& asset_id) {
  return QDir(directory).filePath(asset_id + QStringLiteral(".psd"));
}

QString thumbnail_file(const QString& directory, const QString& asset_id) {
  return QDir(directory).filePath(asset_id + QStringLiteral(".png"));
}

// The index as it is written, with no look at the asset files: one read, whatever the size
// of the library. False when there is no usable index.
bool read_index(const QString& directory, std::vector<AssetCategory>& categories) {
  categories.clear();
  const auto path = index_file(directory);
  if (path.isEmpty()) {
    return false;
  }
  count_asset_file_access();
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() > kMaximumIndexBytes) {
    return false;
  }
  QJsonParseError parse_error;
  const auto json = QJsonDocument::fromJson(file.readAll(), &parse_error);
  const auto root = json.object();
  if (parse_error.error != QJsonParseError::NoError || !json.isObject() ||
      root.value(QStringLiteral("format")).toString() != QLatin1String(kIndexFormat) ||
      !root.value(QStringLiteral("categories")).isArray()) {
    return false;
  }
  QSet<QString> seen_categories;
  QSet<QString> seen_assets;
  const auto listed = root.value(QStringLiteral("categories")).toArray();
  for (const auto& category_value : listed) {
    const auto category_object = category_value.toObject();
    AssetCategory category;
    category.id = category_object.value(QStringLiteral("id")).toString();
    if (category.id.isEmpty() || seen_categories.contains(category.id)) {
      category.id = new_id();
    }
    seen_categories.insert(category.id);
    category.name = AssetLibrary::cleaned_name(category_object.value(QStringLiteral("name")).toString());
    const auto assets = category_object.value(QStringLiteral("assets")).toArray();
    category.assets.reserve(static_cast<std::size_t>(assets.size()));
    for (const auto& asset_value : assets) {
      const auto asset_object = asset_value.toObject();
      AssetEntry entry;
      entry.id = asset_object.value(QStringLiteral("id")).toString();
      if (!AssetLibrary::is_asset_id(entry.id) || seen_assets.contains(entry.id)) {
        continue;
      }
      seen_assets.insert(entry.id);
      entry.name = AssetLibrary::cleaned_name(asset_object.value(QStringLiteral("name")).toString());
      category.assets.push_back(std::move(entry));
    }
    categories.push_back(std::move(category));
  }
  return true;
}

// No usable index: every asset file still in the folder comes back in the first category,
// oldest first, so a lost index loses names and categories but no asset. False when `stop`
// ended it early.
bool rebuild_from_files(const QString& directory, const std::function<QString(const QString&)>& name_provider,
                        const std::function<bool()>& stop, std::vector<AssetCategory>& categories) {
  categories.clear();
  categories.push_back(AssetCategory{QLatin1String(kDefaultAssetCategoryId), QString(), {}});
  if (directory.isEmpty()) {
    return true;
  }
  count_asset_file_access();
  const QDir folder(directory);
  if (!folder.exists()) {
    return true;
  }
  const auto files = folder.entryInfoList({QStringLiteral("*.psd")}, QDir::Files, QDir::Time | QDir::Reversed);
  for (const auto& info : files) {
    if (stop && stop()) {
      return false;
    }
    const auto id = info.completeBaseName();
    if (!AssetLibrary::is_asset_id(id) || !file_has_psd_signature(info.absoluteFilePath())) {
      continue;
    }
    QString name;
    if (name_provider) {
      name = AssetLibrary::cleaned_name(name_provider(info.absoluteFilePath()));
    }
    categories.front().assets.push_back(AssetEntry{id, name});
  }
  return true;
}

// The listed assets whose file is gone or is not a PSD. False when `stop` ended it early.
bool find_invalid_assets(const QString& directory, const QStringList& asset_ids, const std::function<bool()>& stop,
                         QStringList& invalid) {
  invalid.clear();
  for (const auto& id : asset_ids) {
    if (stop && stop()) {
      return false;
    }
    if (!file_has_psd_signature(asset_file(directory, id))) {
      invalid.push_back(id);
    }
  }
  return true;
}

QStringList all_asset_ids(const std::vector<AssetCategory>& categories) {
  QStringList ids;
  for (const auto& category : categories) {
    for (const auto& asset : category.assets) {
      ids.push_back(asset.id);
    }
  }
  return ids;
}

}  // namespace

void count_asset_file_access() {
  const auto* app = QCoreApplication::instance();
  if (app != nullptr && QThread::currentThread() == app->thread()) {
    g_ui_thread_accesses.fetch_add(1, std::memory_order_relaxed);
  } else {
    g_worker_accesses.fetch_add(1, std::memory_order_relaxed);
  }
}

AssetFileAccessCounts asset_file_access_counts() {
  return {g_ui_thread_accesses.load(std::memory_order_relaxed), g_worker_accesses.load(std::memory_order_relaxed)};
}

QString asset_library_directory() {
#ifdef Q_OS_WASM
  return {};
#else
  // Isolation knob, like PATCHY_USER_FONTS_DIR: the test suites never read or write the
  // developer's own library.
  if (const auto override_dir = qEnvironmentVariable("PATCHY_ASSETS_DIR"); !override_dir.isEmpty()) {
    return QDir::cleanPath(override_dir);
  }
  const auto base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (base.isEmpty()) {
    return {};
  }
  return base + QStringLiteral("/assets");
#endif
}

QStringList asset_ids_from_mime_data(const QMimeData* mime_data) {
  QStringList ids;
  if (mime_data == nullptr || !mime_data->hasFormat(QLatin1String(kAssetDragMimeType))) {
    return ids;
  }
  const auto lines = QString::fromUtf8(mime_data->data(QLatin1String(kAssetDragMimeType))).split(QLatin1Char('\n'));
  for (const auto& line : lines) {
    if (AssetLibrary::is_asset_id(line) && !ids.contains(line)) {
      ids.push_back(line);
    }
  }
  return ids;
}

bool asset_drag_is_from_this_process(const QMimeData* mime_data) {
  if (mime_data == nullptr || !mime_data->hasFormat(QLatin1String(kAssetDragSourceMimeType))) {
    return false;
  }
  return mime_data->data(QLatin1String(kAssetDragSourceMimeType)) ==
         QByteArray::number(QCoreApplication::applicationPid());
}

// The worker side. One tracked worker drains the queue and ends, so the library's jobs run one
// at a time and in order, and nothing runs while the queue is empty. Each job's `done` is set
// under the mutex, so the UI thread can wait for one it needs now.
struct AssetLibrary::Shared {
  std::mutex mutex;
  std::condition_variable changed;  // a job finished, or the worker went idle
  std::deque<std::function<void()>> jobs;
  bool running{false};
  std::atomic<bool> closed{false};  // the library is gone: nothing more is read or written

  [[nodiscard]] bool stopping() const { return closed.load() || background_workers_stop_requested(); }
};

struct AssetLibrary::LoadJob {
  std::vector<AssetCategory> categories;
  bool checked{false};   // the entries came from the files themselves: no file check is owed
  bool complete{false};  // false when quit ended the read early: never adopted
  bool done{false};
  bool adopted{false};
};

struct AssetLibrary::VerifyJob {
  QStringList asset_ids;
  QStringList invalid;
  bool complete{false};
  bool done{false};
};

struct AssetLibrary::AddJob {
  QString id;
  QString category_id;
  QString name;
  std::function<EncodedAsset()> encode;
  QImage thumbnail;
  QString error;
  bool written{false};
  bool done{false};
  bool completed{false};
};

AssetLibrary::AssetLibrary(QString directory, QObject* parent)
    : QObject(parent), directory_(std::move(directory)), shared_(std::make_shared<Shared>()) {
  ensure_default_category();
}

AssetLibrary::~AssetLibrary() {
  // A second line behind MainWindow's close: an asset still being written is listed before
  // the library goes. Nobody listens any more.
  blockSignals(true);
  finish_pending_adds();
  std::unique_lock<std::mutex> lock(shared_->mutex);
  shared_->closed.store(true);
  shared_->jobs.clear();
  shared_->changed.wait_for(lock, kCloseWait, [this] { return !shared_->running; });
}

void AssetLibrary::set_recovered_name_provider(std::function<QString(const QString& asset_path)> provider) {
  recovered_name_provider_ = std::move(provider);
}

bool AssetLibrary::is_asset_id(const QString& text) {
  if (text.size() != 32) {
    return false;
  }
  return std::all_of(text.begin(), text.end(), [](QChar character) {
    const auto code = character.unicode();
    return (code >= u'0' && code <= u'9') || (code >= u'a' && code <= u'f');
  });
}

QString AssetLibrary::cleaned_name(const QString& name) {
  auto cleaned = name;
  cleaned.replace(QLatin1Char('\n'), QLatin1Char(' '));
  cleaned.replace(QLatin1Char('\r'), QLatin1Char(' '));
  cleaned.replace(QLatin1Char('\t'), QLatin1Char(' '));
  cleaned = cleaned.simplified();
  if (cleaned.size() > kMaximumNameLength) {
    cleaned.truncate(kMaximumNameLength);
    cleaned = cleaned.trimmed();
  }
  return cleaned;
}

QString AssetLibrary::asset_path(const QString& asset_id) const {
  if (directory_.isEmpty() || !is_asset_id(asset_id)) {
    return {};
  }
  return asset_file(directory_, asset_id);
}

QString AssetLibrary::thumbnail_path(const QString& asset_id) const {
  if (directory_.isEmpty() || !is_asset_id(asset_id)) {
    return {};
  }
  return thumbnail_file(directory_, asset_id);
}

QString AssetLibrary::index_path() const { return index_file(directory_); }

void AssetLibrary::ensure_default_category() {
  if (categories_.empty()) {
    categories_.push_back(AssetCategory{QLatin1String(kDefaultAssetCategoryId), QString(), {}});
  }
}

void AssetLibrary::start_worker() {
  {
    const std::lock_guard<std::mutex> lock(shared_->mutex);
    if (shared_->running || shared_->jobs.empty()) {
      return;
    }
    shared_->running = true;
  }
  // Tracked, so quit waits for it within the usual bound (docs/code-organization.md); the
  // reads stop between two files once quit is asked for.
  run_tracked_background_worker([shared = shared_] {
    for (;;) {
      std::function<void()> job;
      {
        const std::lock_guard<std::mutex> lock(shared->mutex);
        if (shared->closed.load()) {
          shared->jobs.clear();
        }
        if (shared->jobs.empty()) {
          shared->running = false;
          shared->changed.notify_all();
          return;
        }
        job = std::move(shared->jobs.front());
        shared->jobs.pop_front();
      }
      job();
    }
  });
}

void AssetLibrary::load_in_background() {
  if (loaded_ || load_job_ != nullptr) {
    return;
  }
  auto job = std::make_shared<LoadJob>();
  load_job_ = job;
  auto* app = QCoreApplication::instance();
  QPointer<AssetLibrary> owner(this);
  {
    const std::lock_guard<std::mutex> lock(shared_->mutex);
    shared_->jobs.push_back([shared = shared_, job, directory = directory_, provider = recovered_name_provider_, app,
                             owner] {
      std::vector<AssetCategory> categories;
      bool checked = false;
      bool complete = true;
      if (!read_index(directory, categories)) {
        checked = true;
        complete = rebuild_from_files(directory, provider, [&shared] { return shared->stopping(); }, categories);
      }
      {
        const std::lock_guard<std::mutex> done_lock(shared->mutex);
        job->categories = std::move(categories);
        job->checked = checked;
        job->complete = complete;
        job->done = true;
      }
      shared->changed.notify_all();
      if (app != nullptr) {
        QMetaObject::invokeMethod(
            app,
            [owner, job] {
              if (owner != nullptr) {
                owner->adopt_load(job);
              }
            },
            Qt::QueuedConnection);
      }
    });
  }
  start_worker();
}

void AssetLibrary::adopt_load(const std::shared_ptr<LoadJob>& job, bool queue_file_check) {
  if (job == nullptr || job->adopted) {
    return;
  }
  job->adopted = true;
  if (load_job_ == job) {
    load_job_.reset();
  }
  if (!job->complete) {
    return;  // quit ended the read: the library stays unloaded
  }
  categories_ = std::move(job->categories);
  ensure_default_category();
  loaded_ = true;
  if (!job->checked && queue_file_check) {
    queue_verification();
  }
  emit changed();
}

void AssetLibrary::queue_verification() {
  auto job = std::make_shared<VerifyJob>();
  job->asset_ids = all_asset_ids(categories_);
  if (job->asset_ids.isEmpty()) {
    verify_job_.reset();
    return;
  }
  verify_job_ = job;
  auto* app = QCoreApplication::instance();
  QPointer<AssetLibrary> owner(this);
  {
    const std::lock_guard<std::mutex> lock(shared_->mutex);
    shared_->jobs.push_back([shared = shared_, job, directory = directory_, app, owner] {
      QStringList invalid;
      const bool complete =
          find_invalid_assets(directory, job->asset_ids, [&shared] { return shared->stopping(); }, invalid);
      {
        const std::lock_guard<std::mutex> done_lock(shared->mutex);
        job->invalid = std::move(invalid);
        job->complete = complete;
        job->done = true;
      }
      shared->changed.notify_all();
      if (app != nullptr) {
        QMetaObject::invokeMethod(
            app,
            [owner, job] {
              if (owner != nullptr) {
                owner->apply_verification(job);
              }
            },
            Qt::QueuedConnection);
      }
    });
  }
  start_worker();
}

void AssetLibrary::apply_verification(const std::shared_ptr<VerifyJob>& job) {
  if (job == nullptr || verify_job_ != job) {
    return;  // a later load checked the files itself
  }
  verify_job_.reset();
  if (job->complete) {
    remove_entries(job->invalid);
  }
}

void AssetLibrary::remove_entries(const QStringList& asset_ids) {
  if (asset_ids.isEmpty()) {
    return;
  }
  // Left out of the library in memory, as a load always has done; the files and the index on
  // disk are not touched.
  const QSet<QString> gone(asset_ids.begin(), asset_ids.end());
  QStringList removed;
  for (auto& entry : categories_) {
    const auto first = std::remove_if(entry.assets.begin(), entry.assets.end(), [&](const AssetEntry& asset) {
      if (!gone.contains(asset.id)) {
        return false;
      }
      removed.push_back(asset.id);
      return true;
    });
    entry.assets.erase(first, entry.assets.end());
  }
  if (!removed.isEmpty()) {
    emit assets_removed(removed);
    emit changed();
  }
}

void AssetLibrary::ensure_loaded() {
  if (loaded_) {
    return;
  }
  if (const auto job = load_job_; job != nullptr) {
    // The worker is reading the index: wait for that one read rather than making a second.
    {
      std::unique_lock<std::mutex> lock(shared_->mutex);
      shared_->changed.wait(lock, [&job] { return job->done; });
    }
    adopt_load(job);
    if (loaded_) {
      return;
    }
  }
  load();
}

void AssetLibrary::load() {
  bool checked = false;
  bool landed = false;
  if (const auto job = load_job_; job != nullptr) {
    {
      std::unique_lock<std::mutex> lock(shared_->mutex);
      shared_->changed.wait(lock, [&job] { return job->done; });
    }
    checked = job->checked;
    const QSignalBlocker quiet(this);  // one changed() for the whole load, below
    adopt_load(job, false);
    landed = job->complete;
  }
  if (!landed) {
    // Nothing in flight (or quit cut it short): read here.
    std::vector<AssetCategory> categories;
    checked = !read_index(directory_, categories);
    if (checked) {
      rebuild_from_files(directory_, recovered_name_provider_, {}, categories);
    }
    categories_ = std::move(categories);
    ensure_default_category();
    loaded_ = true;
  }
  // Whoever calls load() wants the settled library: the file check runs here, and a queued
  // one is no longer wanted.
  verify_job_.reset();
  if (!checked) {
    QStringList invalid;
    find_invalid_assets(directory_, all_asset_ids(categories_), {}, invalid);
    const QSignalBlocker quiet(this);
    remove_entries(invalid);
  }
  emit changed();
}

bool AssetLibrary::busy() const { return load_job_ != nullptr || verify_job_ != nullptr || !add_jobs_.empty(); }

const AssetCategory* AssetLibrary::find_category(const QString& category_id) const {
  const auto it = std::find_if(categories_.begin(), categories_.end(),
                               [&](const AssetCategory& entry) { return entry.id == category_id; });
  return it != categories_.end() ? &*it : nullptr;
}

AssetCategory* AssetLibrary::category(const QString& category_id) {
  const auto it = std::find_if(categories_.begin(), categories_.end(),
                               [&](const AssetCategory& entry) { return entry.id == category_id; });
  return it != categories_.end() ? &*it : nullptr;
}

const AssetEntry* AssetLibrary::find_asset(const QString& asset_id) const {
  for (const auto& entry : categories_) {
    const auto it = std::find_if(entry.assets.begin(), entry.assets.end(),
                                 [&](const AssetEntry& asset) { return asset.id == asset_id; });
    if (it != entry.assets.end()) {
      return &*it;
    }
  }
  return nullptr;
}

QString AssetLibrary::category_of(const QString& asset_id) const {
  for (const auto& entry : categories_) {
    if (std::any_of(entry.assets.begin(), entry.assets.end(),
                    [&](const AssetEntry& asset) { return asset.id == asset_id; })) {
      return entry.id;
    }
  }
  return {};
}

int AssetLibrary::asset_count() const {
  int count = 0;
  for (const auto& entry : categories_) {
    count += static_cast<int>(entry.assets.size());
  }
  return count;
}

bool AssetLibrary::save_index(QString* error) {
  if (directory_.isEmpty()) {
    if (error != nullptr) {
      *error = tr("There is no folder to keep assets in");
    }
    return false;
  }
  if (!QDir().mkpath(directory_)) {
    if (error != nullptr) {
      *error = tr("Could not create %1").arg(QDir::toNativeSeparators(directory_));
    }
    return false;
  }
  QJsonArray categories;
  for (const auto& entry : categories_) {
    QJsonArray assets;
    for (const auto& asset : entry.assets) {
      assets.push_back(QJsonObject{{QStringLiteral("id"), asset.id}, {QStringLiteral("name"), asset.name}});
    }
    categories.push_back(QJsonObject{{QStringLiteral("id"), entry.id},
                                     {QStringLiteral("name"), entry.name},
                                     {QStringLiteral("assets"), assets}});
  }
  const QJsonObject root{{QStringLiteral("format"), QLatin1String(kIndexFormat)},
                         {QStringLiteral("version"), kIndexVersion},
                         {QStringLiteral("categories"), categories}};
  return write_file_atomically(index_path(), QJsonDocument(root).toJson(QJsonDocument::Indented), error);
}

QString AssetLibrary::unused_id() const {
  // File names are generated ids, never the user's name for the asset.
  for (;;) {
    const auto id = new_id();
    const bool queued = std::any_of(add_jobs_.begin(), add_jobs_.end(),
                                    [&id](const std::shared_ptr<AddJob>& job) { return job->id == id; });
    if (!queued && find_asset(id) == nullptr && !QFileInfo::exists(asset_file(directory_, id))) {
      return id;
    }
  }
}

std::optional<QString> AssetLibrary::add_asset(const QString& category_id, const QString& name,
                                               std::span<const std::uint8_t> psd_bytes, const QImage& thumbnail,
                                               QString* error) {
  ensure_loaded();
  auto* target = category(category_id);
  if (target == nullptr) {
    target = &categories_.front();
  }
  if (directory_.isEmpty() || psd_bytes.empty()) {
    if (error != nullptr) {
      *error = tr("There is no folder to keep assets in");
    }
    return std::nullopt;
  }
  if (!QDir().mkpath(directory_)) {
    if (error != nullptr) {
      *error = tr("Could not create %1").arg(QDir::toNativeSeparators(directory_));
    }
    return std::nullopt;
  }
  const auto id = unused_id();
  const auto file_path = asset_path(id);
  const QByteArray bytes(reinterpret_cast<const char*>(psd_bytes.data()), static_cast<qsizetype>(psd_bytes.size()));
  if (!write_file_atomically(file_path, bytes, error)) {
    return std::nullopt;
  }
  if (!thumbnail.isNull()) {
    store_thumbnail(id, thumbnail);  // a cache: a failed write is made again when it is next needed
  }
  const auto before = categories_;
  target->assets.push_back(AssetEntry{id, cleaned_name(name)});
  if (!save_index(error)) {
    categories_ = before;
    QFile::remove(file_path);
    QFile::remove(thumbnail_path(id));
    return std::nullopt;
  }
  emit changed();
  return id;
}

void AssetLibrary::add_asset_in_background(const QString& category_id, const QString& name,
                                           std::function<EncodedAsset()> encode) {
  ensure_loaded();
  if (directory_.isEmpty() || !encode) {
    background_failure_ = tr("There is no folder to keep assets in");
    if (add_jobs_.empty()) {
      const auto failure = std::exchange(background_failure_, QString());
      emit background_adds_finished(std::exchange(background_added_, 0), failure);
    }
    return;
  }
  auto job = std::make_shared<AddJob>();
  job->id = unused_id();
  job->category_id = category_id;
  job->name = cleaned_name(name);
  job->encode = std::move(encode);
  add_jobs_.push_back(job);
  auto* app = QCoreApplication::instance();
  QPointer<AssetLibrary> owner(this);
  {
    const std::lock_guard<std::mutex> lock(shared_->mutex);
    shared_->jobs.push_back([shared = shared_, job, directory = directory_, app, owner] {
      // The encode and both file writes: the part that costs time for a large layer.
      EncodedAsset encoded = job->encode();
      QString error = encoded.error;
      bool written = false;
      if (error.isEmpty()) {
        const QByteArray bytes(reinterpret_cast<const char*>(encoded.psd_bytes.data()),
                               static_cast<qsizetype>(encoded.psd_bytes.size()));
        if (bytes.isEmpty() || !QDir().mkpath(directory)) {
          error = QCoreApplication::translate("patchy::ui::AssetLibrary", "Could not create %1")
                      .arg(QDir::toNativeSeparators(directory));
        } else if (write_file_atomically(asset_file(directory, job->id), bytes, &error)) {
          written = true;
          if (const auto png = png_bytes(encoded.thumbnail); !png.isEmpty()) {
            // A cache: a failed write is made again when the picture is next needed.
            write_file_atomically(thumbnail_file(directory, job->id), png, nullptr);
          }
        }
      }
      {
        const std::lock_guard<std::mutex> done_lock(shared->mutex);
        job->encode = nullptr;
        job->thumbnail = std::move(encoded.thumbnail);
        job->error = std::move(error);
        job->written = written;
        job->done = true;
      }
      shared->changed.notify_all();
      if (app != nullptr) {
        QMetaObject::invokeMethod(
            app,
            [owner, job] {
              if (owner != nullptr) {
                owner->complete_add(job);
              }
            },
            Qt::QueuedConnection);
      }
    });
  }
  start_worker();
}

void AssetLibrary::complete_add(const std::shared_ptr<AddJob>& job) {
  if (job == nullptr || job->completed) {
    return;
  }
  job->completed = true;
  add_jobs_.erase(std::remove(add_jobs_.begin(), add_jobs_.end(), job), add_jobs_.end());
  if (job->written) {
    auto* target = category(job->category_id);
    if (target == nullptr) {
      target = &categories_.front();
    }
    const auto before = categories_;
    target->assets.push_back(AssetEntry{job->id, job->name});
    QString error;
    if (save_index(&error)) {
      ++background_added_;
      emit asset_added(job->id, job->thumbnail);
      emit changed();
    } else {
      categories_ = before;
      QFile::remove(asset_file(directory_, job->id));
      QFile::remove(thumbnail_file(directory_, job->id));
      background_failure_ = error;
    }
  } else if (!job->error.isEmpty()) {
    background_failure_ = job->error;
  }
  if (add_jobs_.empty()) {
    const auto failure = std::exchange(background_failure_, QString());
    emit background_adds_finished(std::exchange(background_added_, 0), failure);
  }
}

void AssetLibrary::finish_pending_adds() {
  while (!add_jobs_.empty()) {
    const auto job = add_jobs_.front();
    {
      std::unique_lock<std::mutex> lock(shared_->mutex);
      shared_->changed.wait(lock, [&job] { return job->done; });
    }
    complete_add(job);
  }
}

bool AssetLibrary::rename_asset(const QString& asset_id, const QString& name, QString* error) {
  ensure_loaded();
  const auto before = categories_;
  for (auto& entry : categories_) {
    for (auto& asset : entry.assets) {
      if (asset.id != asset_id) {
        continue;
      }
      asset.name = cleaned_name(name);
      if (!save_index(error)) {
        categories_ = before;
        return false;
      }
      emit changed();
      return true;
    }
  }
  return false;
}

bool AssetLibrary::remove_asset(const QString& asset_id, QString* error) {
  ensure_loaded();
  const auto before = categories_;
  for (auto& entry : categories_) {
    const auto it = std::find_if(entry.assets.begin(), entry.assets.end(),
                                 [&](const AssetEntry& asset) { return asset.id == asset_id; });
    if (it == entry.assets.end()) {
      continue;
    }
    entry.assets.erase(it);
    // The index first: a stop between the two steps leaves a file nothing lists, never
    // an entry without its file.
    if (!save_index(error)) {
      categories_ = before;
      return false;
    }
    count_asset_file_access();
    QFile::remove(asset_path(asset_id));
    QFile::remove(thumbnail_path(asset_id));
    emit assets_removed({asset_id});
    emit changed();
    return true;
  }
  return false;
}

bool AssetLibrary::move_asset(const QString& asset_id, const QString& category_id, QString* error) {
  ensure_loaded();
  if (category(category_id) == nullptr) {
    return false;
  }
  const auto before = categories_;
  for (auto& entry : categories_) {
    const auto it = std::find_if(entry.assets.begin(), entry.assets.end(),
                                 [&](const AssetEntry& asset) { return asset.id == asset_id; });
    if (it == entry.assets.end()) {
      continue;
    }
    if (entry.id == category_id) {
      return true;
    }
    auto moved = std::move(*it);
    entry.assets.erase(it);
    category(category_id)->assets.push_back(std::move(moved));
    if (!save_index(error)) {
      categories_ = before;
      return false;
    }
    emit changed();
    return true;
  }
  return false;
}

std::optional<QString> AssetLibrary::add_category(const QString& name, QString* error) {
  ensure_loaded();
  const auto cleaned = cleaned_name(name);
  if (cleaned.isEmpty()) {
    return std::nullopt;
  }
  const auto before = categories_;
  auto id = new_id();
  categories_.push_back(AssetCategory{id, cleaned, {}});
  if (!save_index(error)) {
    categories_ = before;
    return std::nullopt;
  }
  emit changed();
  return id;
}

bool AssetLibrary::rename_category(const QString& category_id, const QString& name, QString* error) {
  ensure_loaded();
  const auto cleaned = cleaned_name(name);
  auto* target = category(category_id);
  if (target == nullptr || cleaned.isEmpty()) {
    return false;
  }
  const auto before = categories_;
  target->name = cleaned;
  if (!save_index(error)) {
    categories_ = before;
    return false;
  }
  emit changed();
  return true;
}

bool AssetLibrary::remove_category(const QString& category_id, QString* error) {
  ensure_loaded();
  const auto it = std::find_if(categories_.begin(), categories_.end(),
                               [&](const AssetCategory& entry) { return entry.id == category_id; });
  if (it == categories_.end() || categories_.size() <= 1U) {
    return false;
  }
  const auto before = categories_;
  const auto removed = it->assets;
  categories_.erase(it);
  if (!save_index(error)) {
    categories_ = before;
    return false;
  }
  QStringList removed_ids;
  for (const auto& asset : removed) {
    count_asset_file_access();
    QFile::remove(asset_path(asset.id));
    QFile::remove(thumbnail_path(asset.id));
    removed_ids.push_back(asset.id);
  }
  if (!removed_ids.isEmpty()) {
    emit assets_removed(removed_ids);
  }
  emit changed();
  return true;
}

bool AssetLibrary::sort_category_by_name(const QString& category_id, QString* error) {
  ensure_loaded();
  auto* target = category(category_id);
  if (target == nullptr) {
    return false;
  }
  const auto before = categories_;
  QCollator collator;
  collator.setCaseSensitivity(Qt::CaseInsensitive);
  collator.setNumericMode(true);
  std::stable_sort(target->assets.begin(), target->assets.end(), [&collator](const AssetEntry& left, const AssetEntry& right) {
    return collator.compare(left.name, right.name) < 0;
  });
  if (!save_index(error)) {
    categories_ = before;
    return false;
  }
  emit changed();
  return true;
}

QImage AssetLibrary::thumbnail(const QString& asset_id) const {
  const auto path = thumbnail_path(asset_id);
  if (path.isEmpty()) {
    return {};
  }
  count_asset_file_access();
  if (!QFileInfo::exists(path)) {
    return {};
  }
  QImage image;
  image.load(path, "PNG");
  return image;
}

bool AssetLibrary::store_thumbnail(const QString& asset_id, const QImage& thumbnail) {
  const auto path = thumbnail_path(asset_id);
  if (path.isEmpty() || thumbnail.isNull() || !QDir().mkpath(directory_)) {
    return false;
  }
  const auto bytes = png_bytes(thumbnail);
  return !bytes.isEmpty() && write_file_atomically(path, bytes, nullptr);
}

QByteArray AssetLibrary::read_asset_bytes(const QString& asset_id) const {
  const auto path = asset_path(asset_id);
  if (path.isEmpty()) {
    return {};
  }
  count_asset_file_access();
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}

}  // namespace patchy::ui
