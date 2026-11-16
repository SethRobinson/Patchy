#pragma once

// Text recognition for the pages of a PDF open in the viewer (docs/pdf-viewer-ocr.md):
// reads the pages that have no text of their own, one at a time on a worker, and hands each
// page's text to whoever listens (the viewer gives it to Find and to text selection).
//
// Binding rules (the "Text recognition" entry of docs/legal-constraints-scanning.md):
// nothing is read until start() is called, which only the user's explicit command does;
// the engine and language are the ones passed in (the user's picks); a new document starts
// with nothing recognized; and only the text and where it is are kept, in memory, for as
// long as the window lives. Nothing is written to the PDF or anywhere else.

#include "ocr/ocr_engine.hpp"
#include "ui/pdf_ocr_text.hpp"

#include <QObject>
#include <QString>

#include <atomic>
#include <cstdint>
#include <deque>
#include <future>
#include <memory>
#include <string>
#include <vector>

class QPdfDocument;

namespace patchy::ui {

class PdfPageOcr final : public QObject {
  Q_OBJECT

public:
  enum class PageState {
    Unread,      // not looked at yet
    OwnText,     // the page has text of its own: never recognized
    Recognized,  // read, and text was found
    NoText,      // read, and nothing was found
  };

  explicit PdfPageOcr(QObject* parent = nullptr);
  ~PdfPageOcr() override;

  // The document is not owned and must outlive this object, or be detached first with
  // set_document(nullptr). A new document forgets everything and is not running.
  void set_document(QPdfDocument* document);

  // Reads `pages` in that order with `engine` and `language`, the pages rendered turned
  // `quarter_turns` clockwise (the view's rotation: the page as the user sees it). Pages
  // already looked at are skipped. A run in progress is replaced.
  void start(std::shared_ptr<ocr::Engine> engine, std::string language, const std::vector<int>& pages,
             int quarter_turns);
  // Stops after the page being read (whose read is cancelled). What was read stays.
  void stop();

  [[nodiscard]] bool running() const { return running_; }
  // The run's pages: how many are done and how many it has, for "page N of M".
  [[nodiscard]] int pages_done() const { return done_; }
  [[nodiscard]] int pages_total() const { return total_; }
  [[nodiscard]] PageState page_state(int page) const;
  // The page's recognized text; null unless its state is Recognized.
  [[nodiscard]] std::shared_ptr<const PdfOcrPageText> page_text(int page) const;
  // How many pages were recognized with text so far.
  [[nodiscard]] int recognized_pages() const { return recognized_; }
  // The engine's words when the last run ended because the engine failed (empty otherwise).
  // The page it failed on stays unread, so another run can take it.
  [[nodiscard]] QString last_error() const { return last_error_; }

  // Tests: true while a page is being read.
  [[nodiscard]] bool busy() const { return busy_; }

signals:
  // A page was looked at: page_state(page) says what came of it.
  void page_done(int page);
  void progress_changed();
  // The run ended: finished every page, or was stopped.
  void finished(bool stopped);

private:
  struct Session;
  void stop_worker();
  void pump();
  void end_run(bool stopped);

  QPdfDocument* document_{nullptr};
  std::vector<PageState> states_;
  std::vector<std::shared_ptr<const PdfOcrPageText>> texts_;
  int recognized_{0};

  std::shared_ptr<Session> session_;
  std::deque<int> queue_;
  int quarter_turns_{0};
  bool running_{false};
  int done_{0};
  int total_{0};
  QString last_error_;

  std::future<void> in_flight_;
  bool busy_{false};
  std::uint64_t generation_{0};
};

}  // namespace patchy::ui
