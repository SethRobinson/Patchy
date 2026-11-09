#pragma once

#include "core/book_scan.hpp"
#include "core/pixel_buffer.hpp"
#include "ui/scanner_session.hpp"

#include <QImage>
#include <QRectF>
#include <QString>

#include <memory>
#include <mutex>
#include <optional>
#include <vector>

// A Scan a Book to PDF project (docs/book-scan.md): the folder that holds the raw scans
// and book.json, the manifest that lets a long session resume after a crash or on
// another day. Every token written to book.json is permanent (the persisted-identifier
// rule): the format id "patchy-book-scan", its version, and every key and enum token.
namespace patchy::ui {

enum class BookReadingDirection { LeftToRight, RightToLeft };
enum class BookScanMode { TwoPages, OnePage };
enum class BookPageUse { Both, LeftOnly, RightOnly };
enum class BookPageSizePolicy { Median, Largest, Cover, Custom };

// One scan of the book's pages: a spread, or one page in one-page mode.
struct BookScanEntry {
  QString file;     // relative to the project folder ("scans/spread-0001.png")
  double dpi{300.0};
  // The mode this scan was captured in (a book may scan a few pages in color).
  ScanColorMode capture_mode{ScanColorMode::Color};
  int extra_turns{0};  // clockwise quarter turns on top of the book's up direction
  SpreadLayout layout;  // in the upright image; in one-page mode `spread` is the page
  bool user_edited{false};
  BookPageUse use{BookPageUse::Both};
};

struct BookPageRef {
  enum class Kind { Scan, Blank, FrontCover, BackCover };
  Kind kind{Kind::Scan};
  int scan{-1};  // index into BookProject::scans for Kind::Scan
  BookPageSide side{BookPageSide::Left};
  int turns{0};  // extra clockwise quarter turns the user gave this page
  // This page's own output mode; nullopt follows the book's.
  std::optional<BookOutputMode> output;

  friend bool operator==(const BookPageRef&, const BookPageRef&) = default;
};

struct BookCover {
  QString file;
  double dpi{300.0};
  CoverLayout layout;  // in the upright image
  bool user_edited{false};
};

struct BookProject {
  QString folder;  // absolute; not stored in book.json
  QString title;
  QString author;
  QString device_id;
  // Capture.
  int capture_dpi{300};
  ScanColorMode capture_mode{ScanColorMode::Color};
  bool scan_book_area{true};
  std::optional<QRectF> book_area_inches;  // scanner coordinates, from the cover
  int up_turns{0};                         // clockwise quarter turns that make a scan upright
  BookReadingDirection direction{BookReadingDirection::LeftToRight};
  BookScanMode mode{BookScanMode::TwoPages};
  bool include_covers{true};
  bool finished_scanning{false};
  std::optional<BookCover> cover;
  std::vector<BookScanEntry> scans;
  // Set once the user reorders, deletes, inserts or turns pages in the review step.
  std::optional<std::vector<BookPageRef>> custom_order;
  // Output.
  BookPageSizePolicy size_policy{BookPageSizePolicy::Median};
  BookPageSize custom_size{};
  BookOutputOptions output{};
  int output_dpi{0};  // 0 = as scanned
  QString quality_id{QStringLiteral("high")};
};

inline constexpr const char* kBookProjectFileName = "book.json";

// book.json in `project.folder`, written atomically. Throws std::runtime_error with a
// translated message on failure.
void save_book_project(const BookProject& project);
// Reads `folder`/book.json. nullopt with `error` filled when it is missing or invalid.
[[nodiscard]] std::optional<BookProject> load_book_project(const QString& folder, QString* error);
// The manifest bytes (for tests: deterministic, keys sorted).
[[nodiscard]] QByteArray book_project_json(const BookProject& project);
[[nodiscard]] std::optional<BookProject> book_project_from_json(const QByteArray& bytes, QString* error);

// The pages in reading order: the custom order when set, else covers and every scan's
// pages by the reading direction and each scan's Use choice.
[[nodiscard]] std::vector<BookPageRef> generated_page_order(const BookProject& project);
[[nodiscard]] std::vector<BookPageRef> book_page_order(const BookProject& project);
// The measured size of every page the scans hold, in inches.
[[nodiscard]] std::vector<BookPageSize> measured_page_sizes(const BookProject& project);
// The one size every PDF page gets, by the project's policy; falls back to the cover
// estimate, then to US Letter, when nothing is measured.
[[nodiscard]] BookPageSize resolved_page_size(const BookProject& project);
// Pages the scans contribute (1 per scan in one-page mode, by Use in two-page mode).
[[nodiscard]] int pages_per_scan(const BookProject& project, const BookScanEntry& scan);

// The output options one page is written with: the book's, with the page's own mode.
[[nodiscard]] BookOutputOptions page_output_options(const BookProject& project, const BookPageRef& page);

// Gray8/RGB8 PixelBuffer <-> QImage (Format_Grayscale8 / Format_RGB888).
[[nodiscard]] PixelBuffer book_pixels_from_image(const QImage& image);
[[nodiscard]] QImage book_image_from_pixels(const PixelBuffer& pixels);

// Loads a scan, turned upright. Empty on failure.
[[nodiscard]] PixelBuffer load_upright_scan(const BookProject& project, const QString& relative_file, int turns);

// The last few upright scans loaded, so consecutive pages of one spread read it once.
// Thread-safe; holds at most kCapacity scans (a 600 dpi color spread is ~200 MB).
class BookScanCache {
public:
  static constexpr std::size_t kCapacity = 4;
  explicit BookScanCache(const BookProject& project) : project_(project) {}
  [[nodiscard]] std::shared_ptr<const PixelBuffer> get(const QString& file, int turns);
  // Forgets every scan (after a rescan replaced a file under the same name).
  void clear();

private:
  struct Entry {
    QString file;
    int turns{0};
    std::shared_ptr<const PixelBuffer> pixels;
  };
  const BookProject& project_;
  std::mutex mutex_;
  std::vector<Entry> entries_;  // most recently used last
};

// Where a scanned or cover page comes from: the scan file, its resolution, the quarter
// turns that make it upright, and the page box in that upright scan at `size`.
// nullopt for blank pages and missing scans.
struct BookPageSource {
  QString file;
  double dpi{300.0};
  int turns{0};
  OrientedBox box;
};
[[nodiscard]] std::optional<BookPageSource> book_page_source(const BookProject& project, const BookPageRef& page,
                                                             const BookPageSize& size);

// One finished page: cut at `size`, at `dpi` (0 = the scan's own), with the output
// options applied. Blank pages are white. Empty when its scan cannot be read.
[[nodiscard]] PixelBuffer render_book_page(const BookProject& project, const BookPageRef& page,
                                           const BookPageSize& size, int dpi, BookScanCache& cache,
                                           bool apply_output = true);

}  // namespace patchy::ui
