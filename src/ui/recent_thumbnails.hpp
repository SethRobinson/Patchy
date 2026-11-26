#pragma once

// Thumbnails for the Home Screen's Recent Files grid (GitHub issue 750; docs/ui-conventions-menus.md,
// "Recent file thumbnails"). One small PNG per recent file in a per-user cache folder, made and read
// on a background worker; the panel paints a placeholder until one arrives.
//
// Rules this file keeps:
// - Nothing here decodes, stats or reads on the UI thread, and nothing runs per repaint: the list
//   asks for a path once, and every later paint is a hash lookup.
// - A path on a network location (path_is_on_network_location, classify_path_location) is never
//   stat'ed or read. It gets no thumbnail.
// - A file that is missing, unreadable, of a kind with no cheap preview, or too large gets no
//   thumbnail and no message.
// - One picture per file, in one size, each in a file of its own (docs/legal-constraints.md,
//   "Home Screen recent files").

#include <QHash>
#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

namespace patchy::ui {

// Longest side of a stored thumbnail, in pixels: twice the grid's picture box (up to about
// 250 px wide in a row of two or more cells), so a cell stays sharp on a 200% display.
inline constexpr int kRecentThumbnailStoredSide = 512;
// The pictures kept loaded are bounded: past this many bytes the ones painted longest ago are
// let go (their cache files stay) and load again when their cells are painted next. The most
// recently painted kRecentThumbnailLoadedFloor are never let go, whatever their size.
inline constexpr qsizetype kRecentThumbnailLoadedBytes = 48 * 1024 * 1024;
inline constexpr int kRecentThumbnailLoadedFloor = 64;
// How long after the main window is built the thumbnail folder is swept of pictures no recent
// entry owns: housekeeping that must stay off the startup path.
inline constexpr int kRecentThumbnailSweepDelayMs = 4000;
// A source file larger than this is never opened for a thumbnail.
inline constexpr qint64 kRecentThumbnailMaxSourceBytes = 512LL * 1024 * 1024;
// An image with more pixels than this is not decoded for a thumbnail, unless its format decodes
// straight to a reduced size (JPEG).
inline constexpr qint64 kRecentThumbnailMaxSourcePixels = 64LL * 1000 * 1000;

// The cache folder: PATCHY_RECENT_THUMBNAILS_DIR when set (the test suites), else a folder inside
// a redirected settings root (PATCHY_SETTINGS_DIR: an isolated run keeps its own recent list, and
// its sweep must never reach the user's real pictures), else
// `<AppLocalDataLocation>/recent-thumbnails`. Never created by this call.
[[nodiscard]] QString recent_thumbnails_directory();
// The cache file that belongs to `path`: a name made from the path's text alone, so removing a
// recent entry can delete its thumbnail without touching the file it pictures.
[[nodiscard]] QString recent_thumbnail_cache_file(const QString& path);

enum class RecentThumbnailState {
  unknown,      // never asked for
  pending,      // queued or being made
  ready,        // image() returns it
  unavailable,  // no thumbnail: network location, missing, unreadable, unsupported or too large
};

class RecentThumbnailCache final : public QObject {
  Q_OBJECT

 public:
  explicit RecentThumbnailCache(QObject* parent = nullptr);
  ~RecentThumbnailCache() override;

  [[nodiscard]] RecentThumbnailState state(const QString& path) const;
  // The loaded thumbnail, or null. Still returned while a refresh of the same path is pending.
  [[nodiscard]] const QImage* image(const QString& path) const;
  // Queues the load of a path whose state is unknown, or whose loaded picture was scaled down for
  // a smaller box than the current one; anything else is a hash lookup, so a delegate may call
  // it from every paint. It also marks the path as just used.
  void request(const QString& path);
  // The size, in device pixels, a loaded thumbnail is scaled down to before it is kept in
  // memory. Rounded up to a coarse step, so a window being dragged wider reloads its pictures
  // a few times, not on every pixel.
  void set_display_box(QSize device_pixels);
  [[nodiscard]] QSize display_box() const noexcept { return display_box_; }

  // Stores the picture of a document Patchy has open, as the thumbnail of the file it was just
  // opened from or saved to. `composite` is an 8-bit display image (never linear floats).
  void store(const QString& path, const QImage& composite);
  // Deletes the cached thumbnails of `paths` (a removed or cleared recent entry) and forgets them.
  void forget(const QStringList& paths);
  // Deletes every cache file that belongs to none of `paths`: what bounds the folder to one
  // thumbnail per recent entry.
  void retain_only(const QStringList& paths);
  // Forgets every loaded picture and deletes every cache file: Recent Files went to list view,
  // which keeps no pictures of the user's files on disk.
  void clear();
  // Drops the loaded images of paths outside `paths`; the cache files stay.
  void keep_loaded_only(const QStringList& paths);
  // Marks every loaded path for one more look at its file (changed size or time), keeping the
  // image shown meanwhile. The panel calls it when it reappears.
  void refresh_all();

  // True while a job is queued or running (tests wait on it).
  [[nodiscard]] bool busy() const;
  // Loads asked for and not yet answered. The startup report waits for this to reach zero so
  // the span of the first cells' pictures lands in it (background `recent_thumbnails.first_cells`:
  // from the first request to the moment none is pending; docs/startup-performance.md).
  [[nodiscard]] int loads_in_flight() const noexcept { return loads_in_flight_; }
  // Requests made since construction (the startup guard tests read it).
  [[nodiscard]] int requests_made() const noexcept { return requests_made_; }

 signals:
  void thumbnail_changed(const QString& path);

 private:
  struct Entry {
    RecentThumbnailState state{RecentThumbnailState::unknown};
    QImage image;
    QSize loaded_box;      // the display box the image was fitted to
    bool reduced{false};   // the stored picture is larger than what is loaded
    quint64 last_used{0};  // the cache's clock at the latest request
  };
  struct Shared;
  struct Job;

  void enqueue(Job job);
  // Hands jobs to the worker, starting it when it is not running.
  void send_to_worker(std::vector<Job> jobs);
  // Releases the loads held for the event loop's first idle moment (see held_).
  void release_held();
  // The worker's answer for one path, delivered on the UI thread.
  void finish(const QString& key, bool was_load, QImage image, bool available, bool reduced, QSize box);
  // Lets the pictures used longest ago go once the loaded ones pass their byte bound.
  void trim_loaded();

  QHash<QString, Entry> entries_;
  QSize display_box_{192, 144};
  quint64 clock_{0};
  int loads_in_flight_{0};
  int requests_made_{0};
  double first_request_ms_{-1.0};  // startup trace clock; -1 when not tracing or already reported
  std::shared_ptr<Shared> shared_;
  // Loads asked for before the event loop has gone idle once. The first paint asks for a
  // screenful while startup still has work queued; they start when the loop is first about to
  // block (startup is idle then), so their arrivals never stretch startup, or after
  // kHoldLimitMs when the loop never rests. Later requests go straight to the worker.
  std::vector<Job> held_;
  bool released_once_{false};
};

// The picture MainWindow hands to RecentThumbnailCache::store: `composite` picked down to at most
// twice the stored side without smoothing, so the UI thread touches about a megapixel
// whatever the document's size. The smooth scale to the stored size happens on the worker.
[[nodiscard]] QImage recent_thumbnail_source(const QImage& composite);

}  // namespace patchy::ui
